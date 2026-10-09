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
#include "Economy/RTSHarvesterComponent.h"
#include "Economy/RTSSupplySource.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Units/RTSUnit.h"
#include "WorldDivide.h"

namespace
{
	/** How far out from its centre the ring search for a building spot goes, in cm. */
	constexpr float BuildSearchDistance = 4000.f;

	/** How far from the base a supply source may lie for a depot to go beside it, in cm. */
	constexpr float DepotSourceDistance = 4000.f;

	/** The strip in front of a production building's exit kept free for the units it produces, in cm. */
	constexpr float ExitLaneLength = 600.f;

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

	/** Two footprints, given by centre and half size, are at least Gap apart along X or along Y. */
	bool AreApart(const FVector& A, const FVector& ExtentA, const FVector& B, const FVector& ExtentB, float Gap)
	{
		const float GapX = FMath::Abs(A.X - B.X) - ExtentA.X - ExtentB.X;
		const float GapY = FMath::Abs(A.Y - B.Y) - ExtentA.Y - ExtentB.Y;
		return FMath::Max(GapX, GapY) >= Gap;
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
}

namespace RTSAI
{
	int32 FindCurrentStep(TConstArrayView<FRTSBuildOrderStep> Steps, TFunctionRef<int32(const URTSEntityData&)> CountOwned)
	{
		for (int32 Index = 0; Index < Steps.Num(); ++Index)
		{
			const URTSEntityData* Entry = Steps[Index].Entry;
			if (Entry && CountOwned(*Entry) < Steps[Index].Count)
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

	bool ShouldLaunchWave(const FRTSAIWaveSettings& Settings, int32 ArmySize, float SecondsSinceLastWave)
	{
		if (ArmySize <= 0)
		{
			return false;
		}
		return ArmySize >= Settings.Size || (SecondsSinceLastWave >= Settings.Interval && ArmySize >= Settings.MinSize);
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
	const int32 Step = RTSAI::FindCurrentStep(BuildOrder->Steps, [this](const URTSEntityData& Entry) { return CountOwned(Entry); });
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

void ARTSAIPlayer::KeepHarvesting()
{
	const UWorld* World = GetWorld();
	const int32 TeamId = GetRTSPlayerState()->GetTeamId();
	for (TActorIterator<ARTSUnit> It(World); It; ++It)
	{
		ARTSUnit* Unit = *It;
		const URTSHarvesterData* Data = Cast<URTSHarvesterData>(Unit->GetUnitData());
		if (Unit->GetTeamId() != TeamId || !Data || !IsStanding(Unit) || Unit->GetQueuedCommandCount() > 0)
		{
			continue;
		}
		// Only while there is work: a source in reach and a depot to take it to.
		const FVector Location = Unit->GetActorLocation();
		if (!ARTSSupplySource::FindNearest(*World, Location, Data->SearchRadius)
			|| !URTSHarvesterComponent::FindNearestDepot(*World, TeamId, Location))
		{
			continue;
		}
		FRTSCommand Harvest;
		Harvest.Type = ERTSCommandType::Harvest;
		Issue({ Unit }, Harvest);
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

	// Wave units that reached their target with nothing left in range go on to the next building.
	TArray<ARTSUnit*> Done;
	for (const TWeakObjectPtr<ARTSUnit>& Unit : WaveUnits)
	{
		if (Unit->GetQueuedCommandCount() == 0 && !Unit->GetWeaponComponent()->GetTarget())
		{
			Done.Add(Unit.Get());
		}
	}
	if (const ARTSBuilding* Next = Done.IsEmpty() ? nullptr : FindNearestEnemyBuilding(GetCentroid(Done)))
	{
		FRTSCommand AttackMove;
		AttackMove.Type = ERTSCommandType::AttackMove;
		AttackMove.TargetLocation = Next->GetActorLocation();
		Issue(Done, AttackMove);
	}

	// The army stays home while it fights off an enemy in the base.
	TArray<ARTSUnit*> Army = GatherArmy();
	if (bDefending || !RTSAI::ShouldLaunchWave(GetWaveSettings(), Army.Num(), SecondsSinceWave))
	{
		return;
	}
	const ARTSBuilding* Target = FindNearestEnemyBuilding(GetCentroid(Army));
	if (!Target)
	{
		return;
	}
	FRTSCommand AttackMove;
	AttackMove.Type = ERTSCommandType::AttackMove;
	AttackMove.TargetLocation = Target->GetActorLocation();
	Issue(Army, AttackMove);
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
		if (It->GetTeamId() != TeamId && IsStanding(*It))
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
		const FVector Location = Unit->GetActorLocation();
		if (IsStanding(Unit.Get()) && Buildings.ContainsByPredicate([&Location](const FVector& Building)
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

bool ARTSAIPlayer::GetGatherPoint(FVector& OutLocation) const
{
	const FVector Base = GetBaseLocation();
	const ARTSBuilding* Enemy = FindNearestEnemyBuilding(Base);
	if (!Enemy)
	{
		return false;
	}
	OutLocation = Base + (Enemy->GetActorLocation() - Base).GetSafeNormal2D() * GatherDistance;
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
			&& !Unit->GetHarvesterComponent()->CanHarvest() && !WaveUnits.Contains(Unit))
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

	FVector Center = Base;
	float StartRadius = Extent.Size2D() + BuildingGap + 600.f;
	if (Data.bAcceptsSupply)
	{
		// A depot goes beside the source its harvesters will work.
		if (const ARTSSupplySource* Source = ARTSSupplySource::FindNearest(*World, Base, DepotSourceDistance))
		{
			Center = Source->GetActorLocation();
			StartRadius = (RTSBuildPlacement::GetFootprintExtent(Source->Footprint) + Extent).GetMax() + BuildingGap;
		}
	}

	constexpr float RingStep = 2.f * RTSBuildPlacement::GridSize;
	constexpr float CandidateSpacing = 300.f;
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
	const FVector Extent = RTSBuildPlacement::GetFootprintExtent(Data.Footprint);

	for (TActorIterator<ARTSBuilding> It(World); It; ++It)
	{
		const URTSBuildingData* Other = It->GetBuildingData();
		if (!Other || It->IsActorBeingDestroyed())
		{
			continue;
		}
		const FVector OtherExtent = RTSBuildPlacement::GetFootprintExtent(Other->Footprint);
		if (!AreApart(Location, Extent, It->GetActorLocation(), OtherExtent, BuildingGap))
		{
			return false;
		}
		// Units come out of the +X edge; nothing may stand in their way.
		if (It->GetTeamId() == TeamId && !Other->Units.IsEmpty())
		{
			const FVector LaneExtent(ExitLaneLength * 0.5f, OtherExtent.Y, 0.f);
			const FVector Lane = It->GetActorLocation() + FVector(OtherExtent.X + LaneExtent.X, 0.f, 0.f);
			if (!AreApart(Location, Extent, Lane, LaneExtent, 0.f))
			{
				return false;
			}
		}
	}
	for (TActorIterator<ARTSSupplySource> It(World); It; ++It)
	{
		if (!AreApart(Location, Extent, It->GetActorLocation(), RTSBuildPlacement::GetFootprintExtent(It->Footprint), BuildingGap))
		{
			return false;
		}
	}
	for (const FPendingBuild& Pending : PendingBuilds)
	{
		const URTSBuildingData* Other = Pending.Data.Get();
		if (Other && !Pending.Site.IsValid()
			&& !AreApart(Location, Extent, Pending.Location, RTSBuildPlacement::GetFootprintExtent(Other->Footprint), BuildingGap))
		{
			return false;
		}
	}
	return true;
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
