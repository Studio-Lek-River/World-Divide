#include "Core/RTSPlayerState.h"

#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

void ARTSPlayerState::SetTeamId(int32 InTeamId)
{
	check(HasAuthority());
	TeamId = InTeamId;
	RecalculatePower();
}

void ARTSPlayerState::SetFaction(URTSFactionData* InFaction)
{
	check(HasAuthority());
	Faction = InFaction;
	// Whether the faction uses power decides whether its defences are powered.
	RecalculatePower();
}

void ARTSPlayerState::AddCredits(int32 Delta)
{
	check(HasAuthority());
	Credits = FMath::Max(Credits + Delta, 0);
}

bool ARTSPlayerState::TrySpendCredits(int32 Amount)
{
	check(HasAuthority());
	if (Amount < 0 || Credits < Amount)
	{
		return false;
	}
	Credits -= Amount;
	return true;
}

ARTSPlayerState* ARTSPlayerState::FindForTeam(const UWorld& World, int32 InTeamId)
{
	if (const AGameStateBase* GameState = World.GetGameState())
	{
		for (APlayerState* Player : GameState->PlayerArray)
		{
			ARTSPlayerState* State = Cast<ARTSPlayerState>(Player);
			if (State && State->TeamId == InTeamId)
			{
				return State;
			}
		}
	}
	return nullptr;
}

float ARTSPlayerState::ComputePowerEfficiency(int32 Produced, int32 Consumed)
{
	return Consumed > Produced ? FMath::Max(Produced, 0) / static_cast<float>(Consumed) : 1.f;
}

float ARTSPlayerState::GetPowerEfficiency() const
{
	if (Faction && !Faction->bUsesPower)
	{
		return 1.f;
	}
	return ComputePowerEfficiency(PowerProduced, PowerConsumed);
}

float ARTSPlayerState::GetProductionRateForTeam(const UWorld& World, int32 InTeamId)
{
	const ARTSPlayerState* Player = FindForTeam(World, InTeamId);
	return Player ? Player->GetProductionRate() : 1.f;
}

bool ARTSPlayerState::IsLowPowerForTeam(const UWorld& World, int32 InTeamId)
{
	const ARTSPlayerState* Player = FindForTeam(World, InTeamId);
	return Player && Player->IsLowPower();
}

void ARTSPlayerState::RecalculatePower(const ARTSBuilding* Removed)
{
	check(HasAuthority());
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// A full scan rather than running totals, so nothing can drift; it only runs when a building completes or falls.
	TArray<ARTSBuilding*> Buildings;
	int32 Produced = 0;
	int32 Consumed = 0;
	for (TActorIterator<ARTSBuilding> It(World); It; ++It)
	{
		ARTSBuilding* Building = *It;
		const URTSBuildingData* Data = Building->GetBuildingData();
		if (Building == Removed || Building->IsActorBeingDestroyed() || !Data || Building->GetTeamId() != TeamId
			|| !Building->IsConstructed() || Building->GetHealthComponent()->IsDead())
		{
			continue;
		}
		Produced += Data->PowerOutput;
		Consumed += Data->PowerDraw;
		Buildings.Add(Building);
	}
	PowerProduced = Produced;
	PowerConsumed = Consumed;

	for (ARTSBuilding* Building : Buildings)
	{
		Building->RefreshWeapon();
	}
}

void ARTSPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ARTSPlayerState, TeamId);
	DOREPLIFETIME(ARTSPlayerState, Faction);
	DOREPLIFETIME_CONDITION(ARTSPlayerState, Credits, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ARTSPlayerState, PowerProduced, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ARTSPlayerState, PowerConsumed, COND_OwnerOnly);
}
