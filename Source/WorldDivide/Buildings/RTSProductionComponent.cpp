#include "Buildings/RTSProductionComponent.h"

#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Commands/RTSCommand.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Units/RTSUnit.h"
#include "WorldDivide.h"

namespace RTSProduction
{
	const URTSEntityData* FindMissingPrerequisite(const UWorld& World, int32 TeamId, const URTSEntityData& Data)
	{
		for (const TObjectPtr<URTSEntityData>& Prerequisite : Data.Prerequisites)
		{
			if (!Prerequisite)
			{
				continue;
			}
			bool bOwned = false;
			if (Prerequisite->IsA<URTSBuildingData>())
			{
				for (TActorIterator<ARTSBuilding> It(&World); It && !bOwned; ++It)
				{
					bOwned = It->GetTeamId() == TeamId && It->GetBuildingData() == Prerequisite && It->IsConstructed()
						&& !It->IsActorBeingDestroyed() && !It->GetHealthComponent()->IsDead();
				}
			}
			else
			{
				for (TActorIterator<ARTSUnit> It(&World); It && !bOwned; ++It)
				{
					bOwned = It->GetTeamId() == TeamId && It->GetUnitData() == Prerequisite && !It->IsActorBeingDestroyed()
						&& !It->GetHealthComponent()->IsDead();
				}
			}
			if (!bOwned)
			{
				return Prerequisite;
			}
		}
		return nullptr;
	}
}

URTSProductionComponent::URTSProductionComponent()
{
	// The building's tick drives the queue once it is constructed.
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

ARTSBuilding* URTSProductionComponent::GetBuilding() const
{
	return Cast<ARTSBuilding>(GetOwner());
}

bool URTSProductionComponent::CanProduce() const
{
	const ARTSBuilding* Building = GetBuilding();
	const URTSBuildingData* Data = Building ? Building->GetBuildingData() : nullptr;
	return Data && !Data->Units.IsEmpty() && Building->IsConstructed() && !Building->IsActorBeingDestroyed()
		&& !Building->GetHealthComponent()->IsDead();
}

bool URTSProductionComponent::CanProduce(const URTSUnitData* Data) const
{
	return Data && CanProduce() && GetBuilding()->GetBuildingData()->Units.Contains(Data);
}

bool URTSProductionComponent::Enqueue(ARTSPlayerState& Player, URTSUnitData* Data)
{
	check(GetOwner()->HasAuthority());
	const ARTSBuilding* Building = GetBuilding();
	if (!Building || Building->GetTeamId() != Player.GetTeamId() || !CanProduce(Data))
	{
		UE_LOG(LogWorldDivide, Verbose, TEXT("Team %d cannot produce %s at %s"), Player.GetTeamId(), *GetNameSafe(Data), *GetNameSafe(Building));
		return false;
	}
	if (const URTSEntityData* Missing = RTSProduction::FindMissingPrerequisite(*GetWorld(), Player.GetTeamId(), *Data))
	{
		UE_LOG(LogWorldDivide, Verbose, TEXT("Team %d needs %s for %s"), Player.GetTeamId(), *Missing->GetName(), *Data->GetName());
		return false;
	}
	if (Queue.Num() >= MaxQueue || !Player.TrySpendCredits(Data->Cost))
	{
		UE_LOG(LogWorldDivide, Verbose, TEXT("Team %d cannot queue %s: %d queued, %d of %d credits"),
			Player.GetTeamId(), *Data->GetName(), Queue.Num(), Player.GetCredits(), Data->Cost);
		return false;
	}
	Queue.Add(Data);
	return true;
}

bool URTSProductionComponent::CancelLast(ARTSPlayerState& Player, const URTSUnitData* Data)
{
	check(GetOwner()->HasAuthority());
	const ARTSBuilding* Building = GetBuilding();
	const int32 Index = Queue.FindLastByPredicate([Data](const TObjectPtr<URTSUnitData>& Entry) { return Entry.Get() == Data; });
	if (!Building || Building->GetTeamId() != Player.GetTeamId() || !Data || Index == INDEX_NONE)
	{
		UE_LOG(LogWorldDivide, Verbose, TEXT("Team %d cannot cancel %s at %s"), Player.GetTeamId(), *GetNameSafe(Data), *GetNameSafe(Building));
		return false;
	}
	Queue.RemoveAt(Index);
	if (Index == 0)
	{
		Progress = 0.f;
	}
	Player.AddCredits(Data->Cost);
	return true;
}

void URTSProductionComponent::SetRallyPoint(const FVector& Location)
{
	check(GetOwner()->HasAuthority());
	RallyPoint = Location;
}

void URTSProductionComponent::UpdateProduction(float DeltaSeconds)
{
	check(GetOwner()->HasAuthority());
	ARTSBuilding* Building = GetBuilding();
	if (Queue.IsEmpty() || !CanProduce())
	{
		return;
	}

	URTSUnitData* Data = Queue[0];
	const float Rate = ARTSPlayerState::GetProductionRateForTeam(*GetWorld(), Building->GetTeamId());
	Progress = Data && Data->BuildTime > 0.f ? FMath::Min(Progress + DeltaSeconds * Rate / Data->BuildTime, 1.f) : 1.f;
	if (Progress < 1.f)
	{
		return;
	}

	Queue.RemoveAt(0);
	Progress = 0.f;
	if (ARTSUnit* Unit = Data ? Building->SpawnUnitAtExit(Data) : nullptr)
	{
		FRTSCommand Move;
		Move.Type = ERTSCommandType::Move;
		Move.TargetLocation = RallyPoint;
		Unit->ExecuteCommand(Move);
	}
}

void URTSProductionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(URTSProductionComponent, Queue);
	DOREPLIFETIME(URTSProductionComponent, Progress);
	DOREPLIFETIME(URTSProductionComponent, RallyPoint);
}
