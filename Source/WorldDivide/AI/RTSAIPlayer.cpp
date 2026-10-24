#include "AI/RTSAIPlayer.h"

#include "Buildings/RTSBuilderComponent.h"
#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSTargetable.h"
#include "Combat/RTSWeaponComponent.h"
#include "Commands/RTSCommand.h"
#include "Commands/RTSCommandDispatch.h"
#include "Core/RTSGameState.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "Data/RTSHarvesterData.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSUpgradeData.h"
#include "Data/RTSWeaponData.h"
#include "Economy/RTSHarvesterComponent.h"
#include "Economy/RTSSupplySource.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Navigation/RTSNavAgents.h"
#include "TimerManager.h"
#include "Units/RTSAircraftComponent.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitShape.h"
#include "WorldDivide.h"

namespace
{
	/** How far out past its first ring the ring search for a building spot goes, in cm. */
	constexpr float BuildSearchDistance = 12000.f;

	/** How far past the edge of the base building a supply source may lie for a depot to go beside it, in cm. */
	constexpr float DepotSourceDistance = 12000.f;

	/** Army units a production building has queued at most once the build order is done. */
	constexpr int32 ArmyQueueLength = 2;

	bool IsStanding(const AActor* Actor, const URTSHealthComponent* Health)
	{
		return IsValid(Actor) && !Actor->IsActorBeingDestroyed() && Health && !Health->IsDead();
	}

	bool IsStanding(const ARTSBuilding* Building)
	{
		return Building && IsStanding(Building, Building->GetHealthComponent());
	}

	bool IsStanding(const ARTSUnit* Unit)
	{
		return Unit && IsStanding(Unit, Unit->GetHealthComponent());
	}

	bool IsStanding(const AActor* Actor)
	{
		const IRTSTargetable* Targetable = Cast<IRTSTargetable>(Actor);
		return Targetable && IsStanding(Actor, Targetable->GetHealthComponent());
	}

	/**
	 * How far a building of Data keeps from a supply source: Gap, and for a depot also the length of the harvester it sends
	 * out, so a harvester docked at either still has a lane past the other instead of stalling between them.
	 */
	float GetSourceGap(const URTSBuildingData& Data, float Gap)
	{
		const URTSUnitData* Harvester = Data.bAcceptsSupply ? RTSBuildPlacement::GetWidestExitUnit(Data) : nullptr;
		return Gap + (Harvester ? 2.f * RTSUnitShape::FromData(*Harvester).GetHalfLength() : 0.f);
	}

	FVector GetCentroid(TConstArrayView<ARTSUnit*> Units)
	{
		FVector Sum = FVector::ZeroVector;
		for (const ARTSUnit* Unit : Units)
		{
			Sum += Unit->GetActorLocation();
		}
		return Units.IsEmpty() ? Sum : Sum / Units.Num();
	}

	/** Damage a second of Weapon's fire deals, 0 unarmed. */
	float GetDamagePerSecond(const URTSWeaponComponent& Weapon)
	{
		const URTSWeaponData* Data = Weapon.GetWeaponData();
		return Data ? Weapon.GetDamage() / Data->ReloadSeconds : 0.f;
	}

	/** The point of Target's footprint nearest From: a building's or unit's edge, a supply source's, or its location. */
	FVector GetClosestPoint(const AActor& Target, const FVector& From)
	{
		if (const IRTSTargetable* Targetable = Cast<IRTSTargetable>(&Target))
		{
			return Targetable->GetClosestFootprintPoint(From);
		}
		if (const ARTSSupplySource* Source = Cast<ARTSSupplySource>(&Target))
		{
			return Source->GetClosestFootprintPoint(From);
		}
		return Target.GetActorLocation();
	}
}

namespace RTSAI
{
	int32 FindCurrentStep(TConstArrayView<FRTSBuildOrderStep> Steps, TFunctionRef<int32(const URTSEntityData&)> CountOwned)
	{
		return FindCurrentStep(Steps, CountOwned, [](const URTSEntityData&) { return true; });
	}

	int32 FindCurrentStep(TConstArrayView<FRTSBuildOrderStep> Steps, TFunctionRef<int32(const URTSEntityData&)> CountOwned,
		TFunctionRef<bool(const URTSEntityData&)> CanWorkOn)
	{
		for (int32 Index = 0; Index < Steps.Num(); ++Index)
		{
			const URTSEntityData* Entry = Steps[Index].Entry;
			if (Entry && CountOwned(*Entry) < Steps[Index].Count && CanWorkOn(*Entry))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	FRTSAIWaveSettings GetWaveSettings(const URTSAIBuildOrderData& BuildOrder, ERTSAIDifficulty Difficulty)
	{
		const FRTSAIWaveScale* Scale = BuildOrder.DifficultyScales.Find(Difficulty);
		const float IntervalScale = Scale ? Scale->IntervalScale : 1.f;
		const float SizeScale = Scale ? Scale->SizeScale : 1.f;

		FRTSAIWaveSettings Settings;
		Settings.Interval = BuildOrder.Waves.Interval * IntervalScale;
		Settings.Size = FMath::Max(FMath::RoundToInt(BuildOrder.Waves.Size * SizeScale), 1);
		Settings.MinSize = FMath::Clamp(FMath::RoundToInt(BuildOrder.Waves.MinSize * SizeScale), 1, Settings.Size);
		return Settings;
	}

	bool ShouldLaunchWave(const FRTSAIWaveSettings& Settings, int32 ArmySize, float SecondsSinceLastWave, bool bCanGrow)
	{
		if (ArmySize <= 0)
		{
			return false;
		}
		return ArmySize >= Settings.Size
			|| (SecondsSinceLastWave >= Settings.Interval && (ArmySize >= Settings.MinSize || !bCanGrow));
	}

	int32 FindThreat(TConstArrayView<FVector> Enemies, TConstArrayView<FVector> Buildings, float Radius, TConstArrayView<bool> Attacked)
	{
		int32 Threat = INDEX_NONE;
		float ThreatDistance = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Enemies.Num(); ++Index)
		{
			// A recent attacker is a threat at any distance; the rest only within Radius.
			const bool bAttacked = Attacked.IsValidIndex(Index) && Attacked[Index];
			const float Reach = bAttacked ? TNumericLimits<float>::Max() : FMath::Square(Radius);
			for (const FVector& Building : Buildings)
			{
				const float Distance = FVector::DistSquared2D(Enemies[Index], Building);
				if (Distance <= Reach && Distance < ThreatDistance)
				{
					Threat = Index;
					ThreatDistance = Distance;
				}
			}
		}
		return Threat;
	}

	TArray<int32> PickResponders(TConstArrayView<FVector> Army, const FVector& Threat, float Share)
	{
		TArray<int32> Indices;
		for (int32 Index = 0; Index < Army.Num(); ++Index)
		{
			Indices.Add(Index);
		}
		Indices.StableSort([&Army, &Threat](int32 A, int32 B)
		{
			return FVector::DistSquared2D(Army[A], Threat) < FVector::DistSquared2D(Army[B], Threat);
		});
		const int32 Count = Army.IsEmpty() ? 0 : FMath::Clamp(FMath::CeilToInt(Army.Num() * Share), 1, Army.Num());
		Indices.SetNum(Count);
		return Indices;
	}

	bool CanOvercome(TConstArrayView<FFighter> Attackers, const FFighter& Defence, float Margin)
	{
		// The fight in short steps, until a side is spent or it has gone on far longer than an assault does.
		constexpr float Step = 0.25f;
		constexpr float MaxSeconds = 300.f;

		TArray<int32> Order;
		for (int32 Index = 0; Index < Attackers.Num(); ++Index)
		{
			if (Attackers[Index].DamagePerSecond > 0.f && Attackers[Index].Health > 0.f)
			{
				Order.Add(Index);
			}
		}
		const float Needed = Defence.Health * Margin;
		if (Needed <= 0.f)
		{
			return true;
		}
		if (Order.IsEmpty())
		{
			return false;
		}
		// The shortest ranges close in furthest, so the defence has them nearest.
		Order.StableSort([&Attackers](int32 A, int32 B) { return Attackers[A].Range < Attackers[B].Range; });

		// When each attacker comes into the defence's range, and into its own.
		TArray<float> Health;
		TArray<float> UnderFireFrom;
		TArray<float> FiringFrom;
		for (const FFighter& Attacker : Attackers)
		{
			const float Start = Attacker.Distance < 0.f ? Defence.Range : Attacker.Distance;
			const float Speed = FMath::Max(Attacker.Speed, 1.f);
			Health.Add(Attacker.Health);
			UnderFireFrom.Add(FMath::Max(Start - Defence.Range, 0.f) / Speed);
			FiringFrom.Add(FMath::Max(Start - Attacker.Range, 0.f) / Speed);
		}

		float Dealt = 0.f;
		for (float Time = 0.f; Time < MaxSeconds; Time += Step)
		{
			bool bAnyLeft = false;
			for (const int32 Index : Order)
			{
				if (Health[Index] > 0.f)
				{
					bAnyLeft = true;
					Dealt += Time >= FiringFrom[Index] ? Attackers[Index].DamagePerSecond * Step : 0.f;
				}
			}
			if (Dealt >= Needed)
			{
				return true;
			}
			if (!bAnyLeft)
			{
				return false;
			}
			for (const int32 Index : Order)
			{
				if (Health[Index] > 0.f && Attackers[Index].Range <= Defence.Range && Time >= UnderFireFrom[Index])
				{
					Health[Index] -= Defence.DamagePerSecond * Step;
					break;
				}
			}
		}
		return false;
	}
}

ARTSAIPlayer::ARTSAIPlayer()
{
	// A player without a connection: the game mode gives the player state a team, a faction and credits.
	bWantsPlayerState = true;
}

ARTSPlayerState* ARTSAIPlayer::GetRTSPlayerState() const
{
	return GetPlayerState<ARTSPlayerState>();
}

const URTSAIBuildOrderData* ARTSAIPlayer::GetBuildOrder() const
{
	const ARTSPlayerState* Player = GetRTSPlayerState();
	return Player && Player->GetFaction() ? Player->GetFaction()->AIBuildOrder.Get() : nullptr;
}

FRTSAIWaveSettings ARTSAIPlayer::GetWaveSettings() const
{
	if (WaveOverride.IsSet())
	{
		return WaveOverride.GetValue();
	}
	const URTSAIBuildOrderData* BuildOrder = GetBuildOrder();
	return BuildOrder ? RTSAI::GetWaveSettings(*BuildOrder, Difficulty) : FRTSAIWaveSettings();
}

void ARTSAIPlayer::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(ThinkTimer, this, &ARTSAIPlayer::ThinkOnTimer, ThinkInterval, /*bLoop*/ true);
	}
}

void ARTSAIPlayer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ThinkTimer);
	Super::EndPlay(EndPlayReason);
}

void ARTSAIPlayer::NotifyBuildingDamaged(const ARTSBuilding& Building, AActor* DamageInstigator)
{
	const IRTSTargetable* Attacker = Cast<IRTSTargetable>(DamageInstigator);
	if (!Attacker || Attacker->GetTeamId() == Building.GetTeamId())
	{
		return;
	}
	for (TActorIterator<ARTSAIPlayer> It(Building.GetWorld()); It; ++It)
	{
		const ARTSPlayerState* Player = It->GetRTSPlayerState();
		if (Player && Player->GetTeamId() == Building.GetTeamId())
		{
			It->Attackers.Add(DamageInstigator, 0.f);
		}
	}
}

void ARTSAIPlayer::NotifyUnitDamaged(const ARTSUnit& Unit, AActor* DamageInstigator)
{
	const IRTSTargetable* Attacker = Cast<IRTSTargetable>(DamageInstigator);
	if (!Attacker || Attacker->GetTeamId() == Unit.GetTeamId()
		|| (!Unit.GetBuilderComponent()->CanBuild() && !Unit.GetHarvesterComponent()->CanHarvest()))
	{
		return;
	}
	for (TActorIterator<ARTSAIPlayer> It(Unit.GetWorld()); It; ++It)
	{
		const ARTSPlayerState* Player = It->GetRTSPlayerState();
		if (Player && Player->GetTeamId() == Unit.GetTeamId())
		{
			It->UnitAttackers.Add(DamageInstigator, 0.f);
		}
	}
}

void ARTSAIPlayer::ThinkOnTimer()
{
	Think(ThinkInterval);
}

void ARTSAIPlayer::Think(float DeltaSeconds)
{
	const UWorld* World = GetWorld();
	if (!World || !GetRTSPlayerState() || ARTSGameState::IsMatchOver(*World))
	{
		return;
	}

	PrunePendingBuilds();
	KeepHarvesting();
	ResumeSites();
	FollowBuildOrder();
	if (const URTSAIBuildOrderData* BuildOrder = GetBuildOrder())
	{
		SetRallyPoints(*BuildOrder);
	}
	DefendBase(DeltaSeconds);
	RespondToUnitAttacks(DeltaSeconds);
	UpdateWaves(DeltaSeconds);
}

void ARTSAIPlayer::Issue(TConstArrayView<ARTSUnit*> Units, const FRTSCommand& Command)
{
	if (ARTSPlayerState* Player = GetRTSPlayerState())
	{
		RTSCommands::Issue(*Player, Units, Command);
		++IssuedCommands;
	}
}

int32 ARTSAIPlayer::CountOwned(const URTSEntityData& Entry) const
{
	const UWorld* World = GetWorld();
	const ARTSPlayerState* Player = GetRTSPlayerState();
	if (!World || !Player)
	{
		return 0;
	}
	const int32 TeamId = Player->GetTeamId();

	if (const URTSUpgradeData* Upgrade = Cast<URTSUpgradeData>(&Entry))
	{
		return Player->HasUpgrade(Upgrade) || RTSProduction::IsUpgradeQueued(*World, TeamId, *Upgrade) ? 1 : 0;
	}

	int32 Count = 0;
	if (Entry.IsA<URTSBuildingData>())
	{
		for (TActorIterator<ARTSBuilding> It(World); It; ++It)
		{
			Count += It->GetTeamId() == TeamId && It->GetBuildingData() == &Entry && IsStanding(*It) ? 1 : 0;
		}
		for (const FPendingBuild& Pending : PendingBuilds)
		{
			Count += Pending.Data.Get() == &Entry && !Pending.Site.IsValid() ? 1 : 0;
		}
		return Count;
	}

	for (TActorIterator<ARTSUnit> It(World); It; ++It)
	{
		Count += It->GetTeamId() == TeamId && It->GetUnitData() == &Entry && IsStanding(*It) ? 1 : 0;
	}
	for (TActorIterator<ARTSBuilding> It(World); It; ++It)
	{
		if (It->GetTeamId() == TeamId && IsStanding(*It))
		{
			for (const URTSEntityData* Queued : It->GetProductionComponent()->GetQueue())
			{
				Count += Queued == &Entry ? 1 : 0;
			}
		}
	}
	return Count;
}

TArray<ARTSUnit*> ARTSAIPlayer::GetWaveUnits() const
{
	TArray<ARTSUnit*> Units;
	for (const TWeakObjectPtr<ARTSUnit>& Unit : WaveUnits)
	{
		if (IsStanding(Unit.Get()))
		{
			Units.Add(Unit.Get());
		}
	}
	return Units;
}

TArray<ARTSUnit*> ARTSAIPlayer::GetDefenders() const
{
	TArray<ARTSUnit*> Units;
	for (const TWeakObjectPtr<ARTSUnit>& Unit : Defenders)
	{
		if (IsStanding(Unit.Get()))
		{
			Units.Add(Unit.Get());
		}
	}
	return Units;
}

TArray<ARTSUnit*> ARTSAIPlayer::GetResponders() const
{
	TArray<ARTSUnit*> Units;
	for (const TWeakObjectPtr<ARTSUnit>& Unit : Responders)
	{
		if (IsStanding(Unit.Get()))
		{
			Units.Add(Unit.Get());
		}
	}
	return Units;
}

void ARTSAIPlayer::PrunePendingBuilds()
{
	PendingBuilds.RemoveAll([](const FPendingBuild& Pending)
	{
		const ARTSUnit* Builder = Pending.Builder.Get();
		if (!IsStanding(Builder) || Builder->GetQueuedCommandCount() == 0)
		{
			return true;
		}
		// A new building is counted as a site once placed; a site to resume is the builder's own as soon as it sets off.
		return Pending.Site.IsValid() || Builder->GetBuilderComponent()->GetSite() != nullptr;
	});
}

ARTSUnit* ARTSAIPlayer::FindIdleBuilder(const URTSBuildingData* Data) const
{
	const ARTSPlayerState* Player = GetRTSPlayerState();
	const FVector Base = GetBaseLocation();
	ARTSUnit* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
	{
		ARTSUnit* Unit = *It;
		const URTSBuilderComponent* Builder = Unit->GetBuilderComponent();
		if (Unit->GetTeamId() != Player->GetTeamId() || !IsStanding(Unit) || !Builder->CanBuild()
			|| (Data && !Builder->CanBuild(Data)) || Unit->GetQueuedCommandCount() > 0
			|| PendingBuilds.ContainsByPredicate([Unit](const FPendingBuild& Pending) { return Pending.Builder.Get() == Unit; }))
		{
			continue;
		}
		const float Distance = FVector::DistSquared2D(Unit->GetActorLocation(), Base);
		if (Distance < BestDistance)
		{
			Best = Unit;
			BestDistance = Distance;
		}
	}
	return Best;
}

void ARTSAIPlayer::ResumeSites()
{
	const int32 TeamId = GetRTSPlayerState()->GetTeamId();
	TSet<const ARTSBuilding*> Attended;
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
	{
		if (It->GetTeamId() == TeamId && It->GetBuilderComponent()->GetSite())
		{
			Attended.Add(It->GetBuilderComponent()->GetSite());
		}
	}
	for (const FPendingBuild& Pending : PendingBuilds)
	{
		Attended.Add(Pending.Site.Get());
	}

	for (TActorIterator<ARTSBuilding> It(GetWorld()); It; ++It)
	{
		ARTSBuilding* Site = *It;
		if (Site->GetTeamId() != TeamId || Site->IsConstructed() || !IsStanding(Site) || Attended.Contains(Site))
		{
			continue;
		}
		ARTSUnit* Builder = FindIdleBuilder(Site->GetBuildingData());
		if (!Builder)
		{
			return;
		}
		FRTSCommand Resume;
		Resume.Type = ERTSCommandType::Build;
		Resume.TargetActor = Site;
		Resume.EntityData = Site->GetBuildingData();
		Resume.TargetLocation = Site->GetActorLocation();
		Issue({ Builder }, Resume);
		if (Builder->GetQueuedCommandCount() > 0)
		{
			PendingBuilds.Add({ Builder, Site->GetBuildingData(), Site->GetActorLocation(), Site });
		}
	}
}

void ARTSAIPlayer::FollowBuildOrder()
{
	const URTSAIBuildOrderData* BuildOrder = GetBuildOrder();
	if (!BuildOrder)
	{
		return;
	}
	// A step the credits do not cover is worth waiting for only while a harvester brings more (#181).
	const int32 Credits = GetRTSPlayerState()->GetCredits();
	const bool bIncomeComing = HasIncomeComing();
	const int32 Step = RTSAI::FindCurrentStep(BuildOrder->Steps, [this](const URTSEntityData& Entry) { return CountOwned(Entry); },
		[this, Credits, bIncomeComing](const URTSEntityData& Entry)
		{
			return (Credits >= Entry.Cost || bIncomeComing) && CanWorkOn(Entry);
		});
	if (Step == INDEX_NONE)
	{
		ProduceArmy(*BuildOrder);
		return;
	}

	// One step at a time, waiting for the credits rather than skipping ahead.
	URTSEntityData* Entry = BuildOrder->Steps[Step].Entry;
	if (URTSBuildingData* Building = Cast<URTSBuildingData>(Entry))
	{
		TryBuild(*Building);
	}
	else
	{
		TryProduce(*Entry);
	}
}

bool ARTSAIPlayer::CanWorkOn(const URTSEntityData& Entry) const
{
	const int32 TeamId = GetRTSPlayerState()->GetTeamId();
	if (const URTSBuildingData* Building = Cast<URTSBuildingData>(&Entry))
	{
		for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
		{
			if (It->GetTeamId() == TeamId && IsStanding(*It) && It->GetBuilderComponent()->CanBuild(Building))
			{
				return true;
			}
		}
		return false;
	}
	const URTSUnitData* Unit = Cast<URTSUnitData>(&Entry);
	const URTSUpgradeData* Upgrade = Cast<URTSUpgradeData>(&Entry);
	const auto Offers = [Unit, Upgrade](const URTSBuildingData* Data)
	{
		return Data && ((Unit && Data->Units.Contains(Unit)) || (Upgrade && Data->Upgrades.Contains(Upgrade)));
	};
	for (TActorIterator<ARTSBuilding> It(GetWorld()); It; ++It)
	{
		if (It->GetTeamId() == TeamId && IsStanding(*It) && Offers(It->GetBuildingData()))
		{
			return true;
		}
	}
	// A building a builder is on its way to place counts as it does for the build order: its units wait for it.
	return PendingBuilds.ContainsByPredicate([&Offers](const FPendingBuild& Pending) { return Offers(Pending.Data.Get()); });
}

bool ARTSAIPlayer::TryBuild(URTSBuildingData& Data)
{
	const UWorld* World = GetWorld();
	const ARTSPlayerState* Player = GetRTSPlayerState();
	if (Player->GetCredits() < Data.Cost || RTSProduction::FindMissingPrerequisite(*World, Player->GetTeamId(), Data)
		|| RTSProduction::IsAtBuildLimit(*World, Player->GetTeamId(), Data))
	{
		return false;
	}
	ARTSUnit* Builder = FindIdleBuilder(&Data);
	if (!Builder)
	{
		return false;
	}
	FVector Location;
	if (!FindBuildSpot(Data, Location))
	{
		UE_LOG(LogWorldDivide, Verbose, TEXT("AI team %d finds no spot for %s"), Player->GetTeamId(), *Data.GetName());
		return false;
	}

	FRTSCommand Build;
	Build.Type = ERTSCommandType::Build;
	Build.EntityData = &Data;
	Build.TargetLocation = Location;
	Issue({ Builder }, Build);
	if (Builder->GetQueuedCommandCount() == 0)
	{
		return false;
	}
	PendingBuilds.Add({ Builder, &Data, Location, nullptr });
	return true;
}

bool ARTSAIPlayer::TryProduce(URTSEntityData& Entry)
{
	const ARTSPlayerState* Player = GetRTSPlayerState();
	if (Player->GetCredits() < Entry.Cost)
	{
		return false;
	}
	ARTSBuilding* Best = nullptr;
	for (TActorIterator<ARTSBuilding> It(GetWorld()); It; ++It)
	{
		const URTSProductionComponent* Production = It->GetProductionComponent();
		const int32 Queued = Production->GetQueue().Num();
		if (It->GetTeamId() == Player->GetTeamId() && Production->CanProduce(&Entry) && Queued < URTSProductionComponent::MaxQueue
			&& (!Best || Queued < Best->GetProductionComponent()->GetQueue().Num()))
		{
			Best = *It;
		}
	}
	if (!Best)
	{
		return false;
	}
	const int32 Before = Best->GetProductionComponent()->GetQueue().Num();
	FRTSCommand Produce;
	Produce.Type = ERTSCommandType::Produce;
	Produce.TargetActor = Best;
	Produce.EntityData = &Entry;
	Issue({}, Produce);
	return Best->GetProductionComponent()->GetQueue().Num() > Before;
}

void ARTSAIPlayer::ProduceArmy(const URTSAIBuildOrderData& BuildOrder)
{
	if (BuildOrder.ArmyUnits.IsEmpty())
	{
		return;
	}
	const ARTSPlayerState* Player = GetRTSPlayerState();
	for (TActorIterator<ARTSBuilding> It(GetWorld()); It; ++It)
	{
		ARTSBuilding* Building = *It;
		const URTSProductionComponent* Production = Building->GetProductionComponent();
		if (Building->GetTeamId() != Player->GetTeamId() || !Production->CanProduce()
			|| Production->GetQueue().Num() >= ArmyQueueLength)
		{
			continue;
		}
		// The next army unit in turn that this building offers and the AI can afford and have.
		for (int32 Offset = 0; Offset < BuildOrder.ArmyUnits.Num(); ++Offset)
		{
			const int32 Index = (NextArmyUnit + Offset) % BuildOrder.ArmyUnits.Num();
			URTSUnitData* Unit = BuildOrder.ArmyUnits[Index];
			if (!Unit || !Production->CanProduce(Unit) || Player->GetCredits() < Unit->Cost
				|| RTSProduction::FindMissingPrerequisite(*GetWorld(), Player->GetTeamId(), *Unit))
			{
				continue;
			}
			FRTSCommand Produce;
			Produce.Type = ERTSCommandType::Produce;
			Produce.TargetActor = Building;
			Produce.EntityData = Unit;
			Issue({}, Produce);
			NextArmyUnit = Index + 1;
			break;
		}
	}
}

bool ARTSAIPlayer::CanArmyGrow(const URTSAIBuildOrderData& BuildOrder) const
{
	const UWorld* World = GetWorld();
	const ARTSPlayerState* Player = GetRTSPlayerState();
	if (HasIncomeComing())
	{
		return true;
	}
	for (TActorIterator<ARTSBuilding> It(World); It; ++It)
	{
		const URTSProductionComponent* Production = It->GetProductionComponent();
		if (It->GetTeamId() != Player->GetTeamId() || !Production->CanProduce())
		{
			continue;
		}
		for (const URTSUnitData* Unit : BuildOrder.ArmyUnits)
		{
			const bool bAffordable = Unit && Production->CanProduce(Unit) && Player->GetCredits() >= Unit->Cost
				&& !RTSProduction::FindMissingPrerequisite(*World, Player->GetTeamId(), *Unit);
			if (bAffordable || (Unit && Production->GetQueue().Contains(Unit)))
			{
				return true;
			}
		}
	}
	return false;
}

bool ARTSAIPlayer::HasIncomeComing() const
{
	const int32 TeamId = GetRTSPlayerState()->GetTeamId();
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
	{
		if (It->GetTeamId() == TeamId && IsStanding(*It) && It->GetHarvesterComponent()->CanHarvest() && It->GetQueuedCommandCount() > 0)
		{
			return true;
		}
	}
	return false;
}

void ARTSAIPlayer::KeepHarvesting()
{
	const UWorld* World = GetWorld();
	const int32 TeamId = GetRTSPlayerState()->GetTeamId();
	TOptional<TArray<ARTSBuilding*>> Defences;
	for (TActorIterator<ARTSUnit> It(World); It; ++It)
	{
		ARTSUnit* Unit = *It;
		const URTSHarvesterData* Data = Cast<URTSHarvesterData>(Unit->GetUnitData());
		if (Unit->GetTeamId() != TeamId || !Data || !IsStanding(Unit) || Unit->GetQueuedCommandCount() > 0)
		{
			continue;
		}
		// Only while there is work: a source and a depot to take it to.
		const FVector Location = Unit->GetActorLocation();
		if (!URTSHarvesterComponent::FindNearestDepot(*World, TeamId, Location))
		{
			continue;
		}
		FRTSCommand Harvest;
		Harvest.Type = ERTSCommandType::Harvest;
		if (!ARTSSupplySource::FindNearest(*World, Location, Data->SearchRadius))
		{
			// None in its own search radius: the nearest elsewhere on the map, as a player would send it, that no enemy
			// defence covers (#181).
			if (!Defences.IsSet())
			{
				Defences = FindEnemyDefences();
			}
			ARTSSupplySource* Nearest = nullptr;
			float NearestDistance = TNumericLimits<float>::Max();
			for (TActorIterator<ARTSSupplySource> Source(World); Source; ++Source)
			{
				const float Distance = FVector::DistSquared2D(Location, Source->GetActorLocation());
				if (Source->IsDepleted() || Distance >= NearestDistance
					|| Defences->ContainsByPredicate([&Source](const ARTSBuilding* Defence)
					{
						const FVector From = Defence->GetActorLocation();
						return FVector::Dist2D(From, Source->GetClosestFootprintPoint(From))
							<= Defence->GetWeaponComponent()->GetRange() + SafeHarvestMargin;
					}))
				{
					continue;
				}
				Nearest = *Source;
				NearestDistance = Distance;
			}
			if (!Nearest)
			{
				continue;
			}
			Harvest.TargetActor = Nearest;
		}
		Issue({ Unit }, Harvest);
	}
}

TArray<ARTSBuilding*> ARTSAIPlayer::FindEnemyDefences() const
{
	const int32 TeamId = GetRTSPlayerState()->GetTeamId();
	TArray<ARTSBuilding*> Defences;
	for (TActorIterator<ARTSBuilding> It(GetWorld()); It; ++It)
	{
		// A building's weapon is only set while it is constructed and powered: the ones that fire.
		const URTSWeaponData* Weapon = It->GetWeaponComponent()->GetWeaponData();
		if (It->GetTeamId() != TeamId && IsStanding(*It) && Weapon && Weapon->CanHit(/*bAirborne*/ false))
		{
			Defences.Add(*It);
		}
	}
	return Defences;
}

TArray<ARTSBuilding*> ARTSAIPlayer::FindCovering(TConstArrayView<ARTSBuilding*> Defences, const AActor& Target)
{
	TArray<ARTSBuilding*> Covering;
	for (ARTSBuilding* Defence : Defences)
	{
		const FVector From = Defence->GetActorLocation();
		if (Defence == &Target || FVector::Dist2D(From, GetClosestPoint(Target, From)) <= Defence->GetWeaponComponent()->GetRange())
		{
			Covering.Add(Defence);
		}
	}
	return Covering;
}

bool ARTSAIPlayer::CanOvercome(TConstArrayView<ARTSUnit*> Units, const ARTSBuilding& Defence, TConstArrayView<ARTSBuilding*> Defences,
	float Margin, bool bWhereTheyStand)
{
	TArray<RTSAI::FFighter> Attackers;
	for (const ARTSUnit* Unit : Units)
	{
		const URTSWeaponComponent* Weapon = Unit->GetWeaponComponent();
		if (Weapon->GetWeaponData() && Weapon->GetWeaponData()->CanHit(/*bAirborne*/ false))
		{
			// Units still outside its range count as arriving at its edge, as the wave was weighed when it set off.
			const FVector Location = Unit->GetActorLocation();
			const float Distance = FMath::Min(static_cast<float>(FVector::Dist2D(Location, Defence.GetClosestFootprintPoint(Location))),
				Defence.GetWeaponComponent()->GetRange());
			Attackers.Add({ Unit->GetHealthComponent()->GetHealth(), GetDamagePerSecond(*Weapon), Weapon->GetRange(),
				Unit->GetUnitData() ? Unit->GetUnitData()->MoveSpeed : 0.f, bWhereTheyStand ? Distance : -1.f });
		}
	}
	// The defences in range of it fire on the attackers too.
	RTSAI::FFighter Target;
	Target.Health = Defence.GetHealthComponent()->GetHealth();
	Target.Range = Defence.GetWeaponComponent()->GetRange();
	for (const ARTSBuilding* Covering : FindCovering(Defences, Defence))
	{
		Target.DamagePerSecond += GetDamagePerSecond(*Covering->GetWeaponComponent());
	}
	return RTSAI::CanOvercome(Attackers, Target, Margin);
}

AActor* ARTSAIPlayer::ChooseWaveTarget(TConstArrayView<ARTSUnit*> Units, TConstArrayView<ARTSBuilding*> Defences, float Margin) const
{
	const FVector Center = GetCentroid(Units);
	const int32 TeamId = GetRTSPlayerState()->GetTeamId();
	TArray<ARTSBuilding*> Buildings;
	for (TActorIterator<ARTSBuilding> It(GetWorld()); It; ++It)
	{
		if (It->GetTeamId() != TeamId && IsStanding(*It))
		{
			Buildings.Add(*It);
		}
	}
	Buildings.StableSort([&Center](const ARTSBuilding& A, const ARTSBuilding& B)
	{
		return FVector::DistSquared2D(A.GetActorLocation(), Center) < FVector::DistSquared2D(B.GetActorLocation(), Center);
	});

	for (ARTSBuilding* Building : Buildings)
	{
		TArray<ARTSBuilding*> Covering = FindCovering(Defences, *Building);
		if (Covering.IsEmpty())
		{
			return Building;
		}
		// The defences have to go first, the nearest of them first.
		Covering.StableSort([&Center](const ARTSBuilding& A, const ARTSBuilding& B)
		{
			return FVector::DistSquared2D(A.GetActorLocation(), Center) < FVector::DistSquared2D(B.GetActorLocation(), Center);
		});
		if (CanOvercome(Units, *Covering[0], Defences, Margin))
		{
			return Covering[0];
		}
	}
	return nullptr;
}

void ARTSAIPlayer::AttackMove(TConstArrayView<ARTSUnit*> Units, const AActor& Target)
{
	// An attack-move, so enemy units on the way and around the target are fought first; EngageDefences turns them on a
	// defence once it fires on them.
	FRTSCommand Command;
	Command.Type = ERTSCommandType::AttackMove;
	Command.TargetLocation = Target.GetActorLocation();
	Issue(Units, Command);
	for (ARTSUnit* Unit : Units)
	{
		WaveAssaults.Remove(Unit);
	}
}

void ARTSAIPlayer::EngageDefences(TConstArrayView<ARTSBuilding*> Defences)
{
	for (ARTSBuilding* Defence : Defences)
	{
		// The whole wave weighs in: units still on their way join the fight as they arrive.
		const TArray<ARTSUnit*> Wave = GetWaveUnits();
		const float Range = Defence->GetWeaponComponent()->GetRange();
		TArray<ARTSUnit*> Group;
		bool bUnderFire = false;
		for (const TWeakObjectPtr<ARTSUnit>& Unit : WaveUnits)
		{
			const FVector Location = Unit->GetActorLocation();
			const float Distance = FVector::Dist2D(Location, Defence->GetClosestFootprintPoint(Location));
			if (Distance <= Range + AssaultReach)
			{
				Group.Add(Unit.Get());
				bUnderFire |= Distance <= Range;
			}
		}
		if (!bUnderFire)
		{
			continue;
		}

		if (CanOvercome(Wave, *Defence, Defences, 1.f, /*bWhereTheyStand*/ true))
		{
			// At the defence rather than at another building nearer them while it fires on them; units fighting enemy units
			// carry on with that.
			TArray<ARTSUnit*> Ordered;
			for (ARTSUnit* Unit : Group)
			{
				const AActor* Target = Unit->GetWeaponComponent()->GetTarget();
				if (WaveAssaults.FindRef(Unit).Get() != Defence && Target != Defence && !Cast<ARTSUnit>(Target))
				{
					Ordered.Add(Unit);
				}
			}
			if (!Ordered.IsEmpty())
			{
				FRTSCommand Attack;
				Attack.Type = ERTSCommandType::Attack;
				Attack.TargetActor = Defence;
				Attack.TargetLocation = Defence->GetActorLocation();
				Issue(Ordered, Attack);
				for (ARTSUnit* Unit : Ordered)
				{
					WaveAssaults.Add(Unit, Defence);
				}
			}
			continue;
		}

		// Too strong for them: back to the gather point, to go again with the army.
		FVector Gather;
		if (!GetGatherPoint(Gather))
		{
			continue;
		}
		FRTSCommand Retreat;
		Retreat.Type = ERTSCommandType::Move;
		Retreat.TargetLocation = Gather;
		Issue(Group, Retreat);
		for (ARTSUnit* Unit : Group)
		{
			WaveUnits.Remove(Unit);
			WaveAssaults.Remove(Unit);
			Retreating.Add(Unit);
		}
		UE_LOG(LogWorldDivide, Log, TEXT("AI team %d pulls %d wave units back from %s"),
			GetRTSPlayerState()->GetTeamId(), Group.Num(), *Defence->GetName());
	}
}

void ARTSAIPlayer::SetRallyPoints(const URTSAIBuildOrderData& BuildOrder)
{
	FVector Gather;
	if (!GetGatherPoint(Gather))
	{
		return;
	}

	const int32 TeamId = GetRTSPlayerState()->GetTeamId();
	for (TActorIterator<ARTSBuilding> It(GetWorld()); It; ++It)
	{
		ARTSBuilding* Building = *It;
		if (Building->GetTeamId() != TeamId || !Building->GetProductionComponent()->CanProduce()
			|| RalliedBuildings.Contains(Building))
		{
			continue;
		}
		const bool bProducesArmy = BuildOrder.ArmyUnits.ContainsByPredicate([Building](const URTSUnitData* Unit)
		{
			return Building->GetProductionComponent()->CanProduce(Unit);
		});
		if (!bProducesArmy)
		{
			continue;
		}
		FRTSCommand Rally;
		Rally.Type = ERTSCommandType::SetRallyPoint;
		Rally.TargetActor = Building;
		Rally.TargetLocation = Gather;
		Issue({}, Rally);
		RalliedBuildings.Add(Building);
	}
}

void ARTSAIPlayer::UpdateWaves(float DeltaSeconds)
{
	SecondsSinceWave += DeltaSeconds;
	WaveUnits.RemoveAll([](const TWeakObjectPtr<ARTSUnit>& Unit) { return !IsStanding(Unit.Get()); });
	for (auto It = WaveAssaults.CreateIterator(); It; ++It)
	{
		if (!WaveUnits.Contains(It->Key))
		{
			It.RemoveCurrent();
		}
	}
	// Units pulled back count as army again once they are back at the gather point.
	Retreating.RemoveAll([](const TWeakObjectPtr<ARTSUnit>& Unit) { return !IsStanding(Unit.Get()) || Unit->GetQueuedCommandCount() == 0; });

	const TArray<ARTSBuilding*> Defences = FindEnemyDefences();
	EngageDefences(Defences);

	// Wave units that reached their target with nothing left in range go on to the next one they can take, or back.
	TArray<ARTSUnit*> Done;
	for (const TWeakObjectPtr<ARTSUnit>& Unit : WaveUnits)
	{
		if (Unit->GetQueuedCommandCount() == 0 && !Unit->GetWeaponComponent()->GetTarget())
		{
			Done.Add(Unit.Get());
		}
	}
	if (!Done.IsEmpty())
	{
		FVector Gather;
		if (const AActor* Next = ChooseWaveTarget(Done, Defences, 1.f))
		{
			AttackMove(Done, *Next);
		}
		else if (GetGatherPoint(Gather))
		{
			FRTSCommand AttackMove;
			AttackMove.Type = ERTSCommandType::AttackMove;
			AttackMove.TargetLocation = Gather;
			Issue(Done, AttackMove);
			WaveUnits.RemoveAll([&Done](const TWeakObjectPtr<ARTSUnit>& Unit) { return Done.Contains(Unit.Get()); });
		}
	}

	// The army stays home while it fights off an enemy in the base; responders stay on their own fight.
	TArray<ARTSUnit*> Army = GatherArmy();
	Army.RemoveAll([this](const ARTSUnit* Unit) { return Responders.Contains(Unit) || Retreating.Contains(Unit); });
	if (bDefending || Army.IsEmpty())
	{
		return;
	}
	const URTSAIBuildOrderData* BuildOrder = GetBuildOrder();
	const bool bCanGrow = !BuildOrder || CanArmyGrow(*BuildOrder);
	if (!RTSAI::ShouldLaunchWave(GetWaveSettings(), Army.Num(), SecondsSinceWave, bCanGrow))
	{
		return;
	}
	// An army that cannot take anything left keeps gathering rather than throwing itself at the defences. While it can
	// grow it waits for a margin; once it cannot, it goes as soon as it can win at all.
	const AActor* Target = ChooseWaveTarget(Army, Defences, bCanGrow ? AssaultMargin : 1.f);
	if (!Target)
	{
		return;
	}
	AttackMove(Army, *Target);
	for (ARTSUnit* Unit : Army)
	{
		WaveUnits.Add(Unit);
	}
	SecondsSinceWave = 0.f;
	++Waves;
	UE_LOG(LogWorldDivide, Log, TEXT("AI team %d sends wave %d of %d units at %s"),
		GetRTSPlayerState()->GetTeamId(), Waves, Army.Num(), *Target->GetName());
}

void ARTSAIPlayer::DefendBase(float DeltaSeconds)
{
	const int32 TeamId = GetRTSPlayerState()->GetTeamId();
	for (auto It = Attackers.CreateIterator(); It; ++It)
	{
		It->Value += DeltaSeconds;
		if (!IsStanding(It->Key.Get()) || It->Value > AttackerMemory)
		{
			It.RemoveCurrent();
		}
	}

	TArray<FVector> Buildings;
	for (TActorIterator<ARTSBuilding> It(GetWorld()); It; ++It)
	{
		if (It->GetTeamId() == TeamId && IsStanding(*It))
		{
			Buildings.Add(It->GetActorLocation());
		}
	}
	TArray<AActor*> EnemyActors;
	TArray<FVector> Enemies;
	TArray<bool> Attacked;
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
	{
		// Aircraft in flight are left to the base's anti-air; the army cannot shoot them.
		if (It->GetTeamId() != TeamId && IsStanding(*It) && !It->IsAirborne())
		{
			EnemyActors.Add(*It);
			Enemies.Add(It->GetActorLocation());
			Attacked.Add(Attackers.Contains(*It));
		}
	}
	// An enemy building is only a threat once it has fired on the base, such as a defence in range of it.
	for (const TPair<TWeakObjectPtr<AActor>, float>& Attacker : Attackers)
	{
		if (ARTSBuilding* Building = Cast<ARTSBuilding>(Attacker.Key.Get()))
		{
			EnemyActors.Add(Building);
			Enemies.Add(Building->GetActorLocation());
			Attacked.Add(true);
		}
	}
	Defenders.RemoveAll([](const TWeakObjectPtr<ARTSUnit>& Unit) { return !IsStanding(Unit.Get()); });

	const int32 ThreatIndex = RTSAI::FindThreat(Enemies, Buildings, DefenseRadius, Attacked);
	bDefending = ThreatIndex != INDEX_NONE;
	Threat = bDefending ? EnemyActors[ThreatIndex] : nullptr;
	if (!bDefending)
	{
		// Each defender goes back to the gather point once its own fight is over.
		TArray<ARTSUnit*> Done;
		for (const TWeakObjectPtr<ARTSUnit>& Unit : Defenders)
		{
			if (!Unit->GetWeaponComponent()->GetTarget())
			{
				Done.Add(Unit.Get());
			}
		}
		FVector Gather;
		if (!Done.IsEmpty() && GetGatherPoint(Gather))
		{
			FRTSCommand AttackMove;
			AttackMove.Type = ERTSCommandType::AttackMove;
			AttackMove.TargetLocation = Gather;
			Issue(Done, AttackMove);
		}
		Defenders.RemoveAll([&Done](const TWeakObjectPtr<ARTSUnit>& Unit) { return Done.Contains(Unit.Get()); });
		return;
	}

	// The army, and wave units that have not left the base behind yet; those rejoin the army.
	TArray<ARTSUnit*> Pulled = GatherArmy();
	for (const TWeakObjectPtr<ARTSUnit>& Unit : WaveUnits)
	{
		// A wave unit may have died since the last think.
		if (!IsStanding(Unit.Get()))
		{
			continue;
		}
		const FVector Location = Unit->GetActorLocation();
		if (Buildings.ContainsByPredicate([&Location](const FVector& Building)
		{
			return FVector::DistSquared2D(Location, Building) <= FMath::Square(DefenseRadius);
		}))
		{
			Pulled.Add(Unit.Get());
		}
	}
	WaveUnits.RemoveAll([&Pulled](const TWeakObjectPtr<ARTSUnit>& Unit) { return Pulled.Contains(Unit.Get()); });

	// New defenders, and those that have nothing left to fight, go at the threat; the rest are already fighting.
	TArray<ARTSUnit*> Ordered;
	for (ARTSUnit* Unit : Pulled)
	{
		if (!Defenders.Contains(Unit))
		{
			Defenders.Add(Unit);
			Ordered.Add(Unit);
		}
		else if (Unit->GetQueuedCommandCount() == 0 && !Unit->GetWeaponComponent()->GetTarget())
		{
			Ordered.Add(Unit);
		}
	}
	if (!Ordered.IsEmpty())
	{
		FRTSCommand AttackMove;
		AttackMove.Type = ERTSCommandType::AttackMove;
		AttackMove.TargetLocation = Enemies[ThreatIndex];
		Issue(Ordered, AttackMove);
	}
}

void ARTSAIPlayer::RespondToUnitAttacks(float DeltaSeconds)
{
	AActor* Latest = nullptr;
	float LatestAge = TNumericLimits<float>::Max();
	for (auto It = UnitAttackers.CreateIterator(); It; ++It)
	{
		It->Value += DeltaSeconds;
		if (!IsStanding(It->Key.Get()) || It->Value > AttackerMemory)
		{
			It.RemoveCurrent();
		}
		else if (It->Value < LatestAge)
		{
			Latest = It->Key.Get();
			LatestAge = It->Value;
		}
	}
	UnitThreat = Latest;
	Responders.RemoveAll([](const TWeakObjectPtr<ARTSUnit>& Unit) { return !IsStanding(Unit.Get()); });

	// A fight in the base comes first and pulls the responders in with the rest of the army.
	if (bDefending)
	{
		Responders.Reset();
		return;
	}

	if (!Latest)
	{
		// Each responder goes back to the gather point once its own fight is over.
		TArray<ARTSUnit*> Done;
		for (const TWeakObjectPtr<ARTSUnit>& Unit : Responders)
		{
			if (!Unit->GetWeaponComponent()->GetTarget())
			{
				Done.Add(Unit.Get());
			}
		}
		FVector Gather;
		if (!Done.IsEmpty() && GetGatherPoint(Gather))
		{
			FRTSCommand AttackMove;
			AttackMove.Type = ERTSCommandType::AttackMove;
			AttackMove.TargetLocation = Gather;
			Issue(Done, AttackMove);
		}
		Responders.RemoveAll([&Done](const TWeakObjectPtr<ARTSUnit>& Unit) { return Done.Contains(Unit.Get()); });
		return;
	}

	// The first time, part of the idle army goes; after that, only responders with nothing left to fight are sent again.
	TArray<ARTSUnit*> Ordered;
	if (Responders.IsEmpty())
	{
		TArray<ARTSUnit*> Army = GatherArmy();
		Army.RemoveAll([this](const ARTSUnit* Unit) { return Defenders.Contains(Unit); });
		TArray<FVector> Locations;
		for (const ARTSUnit* Unit : Army)
		{
			Locations.Add(Unit->GetActorLocation());
		}
		for (const int32 Index : RTSAI::PickResponders(Locations, Latest->GetActorLocation(), ResponseShare))
		{
			Responders.Add(Army[Index]);
			Ordered.Add(Army[Index]);
		}
	}
	else
	{
		for (const TWeakObjectPtr<ARTSUnit>& Unit : Responders)
		{
			if (Unit->GetQueuedCommandCount() == 0 && !Unit->GetWeaponComponent()->GetTarget())
			{
				Ordered.Add(Unit.Get());
			}
		}
	}
	if (!Ordered.IsEmpty())
	{
		FRTSCommand AttackMove;
		AttackMove.Type = ERTSCommandType::AttackMove;
		AttackMove.TargetLocation = Latest->GetActorLocation();
		Issue(Ordered, AttackMove);
	}
}

bool ARTSAIPlayer::GetGatherPoint(FVector& OutLocation) const
{
	const FVector Base = GetBaseLocation();
	const ARTSBuilding* Enemy = FindNearestEnemyBuilding(Base);
	if (!Enemy)
	{
		return false;
	}
	OutLocation = Base + (Enemy->GetActorLocation() - Base).GetSafeNormal2D() * (GetBaseExtent().GetMax() + GatherDistance);
	return true;
}

TArray<ARTSUnit*> ARTSAIPlayer::GatherArmy() const
{
	const int32 TeamId = GetRTSPlayerState()->GetTeamId();
	TArray<ARTSUnit*> Army;
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
	{
		ARTSUnit* Unit = *It;
		const URTSUnitData* Data = Unit->GetUnitData();
		if (Unit->GetTeamId() == TeamId && IsStanding(Unit) && Data && Data->Weapon && !Unit->GetBuilderComponent()->CanBuild()
			&& !Unit->GetHarvesterComponent()->CanHarvest() && !Unit->GetAircraftComponent()->CanFly() && !WaveUnits.Contains(Unit))
		{
			Army.Add(Unit);
		}
	}
	return Army;
}

bool ARTSAIPlayer::FindBuildSpot(const URTSBuildingData& Data, FVector& OutLocation) const
{
	const UWorld* World = GetWorld();
	const int32 TeamId = GetRTSPlayerState()->GetTeamId();
	const FVector Base = GetBaseLocation();
	const FVector Extent = RTSBuildPlacement::GetFootprintExtent(Data.Footprint);
	const FVector BaseExtent = GetBaseExtent();
	const float Gap = GetBuildingGap();

	// The first ring clears the base building; HasRoom then keeps its exit lane free.
	FVector Center = Base;
	float StartRadius = BaseExtent.Size2D() + Extent.Size2D() + Gap;
	if (Data.bAcceptsSupply)
	{
		// A depot goes beside the source its harvesters will work.
		if (const ARTSSupplySource* Source = ARTSSupplySource::FindNearest(*World, Base, BaseExtent.GetMax() + DepotSourceDistance))
		{
			Center = Source->GetActorLocation();
			StartRadius = (RTSBuildPlacement::GetFootprintExtent(Source->Footprint) + Extent).GetMax() + GetSourceGap(Data, Gap);
		}
	}

	// Rings and the candidates on them half the building's shorter side apart, at least two grid cells: a big building
	// searches as many candidates as a small one.
	const float RingStep = FMath::Max(2.f * RTSBuildPlacement::GridSize,
		FMath::GridSnap(FMath::Min(Extent.X, Extent.Y), RTSBuildPlacement::GridSize));
	const float CandidateSpacing = RingStep;
	for (float Radius = StartRadius; Radius <= StartRadius + BuildSearchDistance; Radius += RingStep)
	{
		const int32 Candidates = FMath::Max(8, FMath::CeilToInt(2.f * PI * Radius / CandidateSpacing));
		for (int32 Index = 0; Index < Candidates; ++Index)
		{
			const float Angle = 2.f * PI * Index / Candidates;
			const FVector Candidate(Center.X + Radius * FMath::Cos(Angle), Center.Y + Radius * FMath::Sin(Angle), Base.Z);
			if (HasRoom(Data, RTSBuildPlacement::SnapToGrid(Candidate, Data.Footprint))
				&& RTSBuildPlacement::CanBuilderPlace(*World, Data, Candidate, OutLocation, TeamId))
			{
				return true;
			}
		}
	}
	return false;
}

bool ARTSAIPlayer::HasRoom(const URTSBuildingData& Data, const FVector& Location) const
{
	const UWorld* World = GetWorld();
	const int32 TeamId = GetRTSPlayerState()->GetTeamId();
	// The AI places its buildings unturned.
	const FRTSBuildingFootprint Footprint = RTSBuildPlacement::MakeFootprint(Data.Footprint, Location, 0.f);
	const float BuildingGap = GetBuildingGap();

	for (TActorIterator<ARTSBuilding> It(World); It; ++It)
	{
		const URTSBuildingData* Other = It->GetBuildingData();
		if (!Other || It->IsActorBeingDestroyed())
		{
			continue;
		}
		const FRTSBuildingFootprint OtherFootprint = It->GetGroundFootprint();
		if (!Footprint.IsApart(OtherFootprint, BuildingGap))
		{
			return false;
		}
		// Units come out of the building's own +X edge; nothing may stand in their way until they have driven clear of it.
		if (It->GetTeamId() == TeamId && !Other->Units.IsEmpty())
		{
			FRTSBuildingFootprint Lane = OtherFootprint;
			Lane.Extent.X = RTSBuildPlacement::GetExitLaneLength(*Other) * 0.5f;
			Lane.Center = OtherFootprint.ToWorld(FVector2D(OtherFootprint.Extent.X + Lane.Extent.X, 0.f));
			if (!Footprint.IsApart(Lane, 0.f))
			{
				return false;
			}
		}
	}
	const float SourceGap = GetSourceGap(Data, BuildingGap);
	for (TActorIterator<ARTSSupplySource> It(World); It; ++It)
	{
		if (!Footprint.IsApart(It->GetGroundFootprint(), SourceGap))
		{
			return false;
		}
	}
	for (const FPendingBuild& Pending : PendingBuilds)
	{
		const URTSBuildingData* Other = Pending.Data.Get();
		if (Other && !Pending.Site.IsValid()
			&& !Footprint.IsApart(RTSBuildPlacement::MakeFootprint(Other->Footprint, Pending.Location, 0.f), BuildingGap))
		{
			return false;
		}
	}
	return true;
}

float ARTSAIPlayer::GetBuildingGap()
{
	const float Lane = 2.f * (RTSNavAgents::GetAgents().Last().AgentRadius + VehicleLaneMargin);
	return FMath::CeilToFloat(Lane / RTSBuildPlacement::GridSize) * RTSBuildPlacement::GridSize;
}

FVector ARTSAIPlayer::GetBaseExtent() const
{
	const ARTSPlayerState* Player = GetRTSPlayerState();
	const URTSBuildingData* Headquarters = Player && Player->GetFaction() ? Player->GetFaction()->Headquarters.Get() : nullptr;
	if (!Headquarters)
	{
		return FVector::ZeroVector;
	}
	// The box round the headquarters as it stands, or as it will stand on the start spot.
	float Yaw = RTSBuildPlacement::GetHeadquartersYaw(StartYaw);
	for (TActorIterator<ARTSBuilding> It(GetWorld()); It; ++It)
	{
		if (It->GetTeamId() == Player->GetTeamId() && IsStanding(*It) && It->GetBuildingData() == Headquarters)
		{
			Yaw = It->GetActorRotation().Yaw;
			break;
		}
	}
	const FBox2D Bounds = RTSBuildPlacement::MakeFootprint(Headquarters->Footprint, FVector::ZeroVector, Yaw).GetBounds();
	return FVector(Bounds.GetExtent(), 0.f);
}

FVector ARTSAIPlayer::GetBaseLocation() const
{
	const ARTSPlayerState* Player = GetRTSPlayerState();
	const URTSBuildingData* Headquarters = Player && Player->GetFaction() ? Player->GetFaction()->Headquarters.Get() : nullptr;
	const ARTSBuilding* Any = nullptr;
	for (TActorIterator<ARTSBuilding> It(GetWorld()); It; ++It)
	{
		if (!Player || It->GetTeamId() != Player->GetTeamId() || !IsStanding(*It))
		{
			continue;
		}
		if (Headquarters && It->GetBuildingData() == Headquarters)
		{
			return It->GetActorLocation();
		}
		Any = Any ? Any : *It;
	}
	if (StartLocation.IsSet())
	{
		return StartLocation.GetValue();
	}
	return Any ? Any->GetActorLocation() : FVector::ZeroVector;
}

ARTSBuilding* ARTSAIPlayer::FindNearestEnemyBuilding(const FVector& From) const
{
	const int32 TeamId = GetRTSPlayerState()->GetTeamId();
	ARTSBuilding* Nearest = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<ARTSBuilding> It(GetWorld()); It; ++It)
	{
		if (It->GetTeamId() == TeamId || !IsStanding(*It))
		{
			continue;
		}
		const float Distance = FVector::DistSquared2D(It->GetActorLocation(), From);
		if (Distance < NearestDistance)
		{
			Nearest = *It;
			NearestDistance = Distance;
		}
	}
	return Nearest;
}
