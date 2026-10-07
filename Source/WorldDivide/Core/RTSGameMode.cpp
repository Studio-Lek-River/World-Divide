#include "Core/RTSGameMode.h"

#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
#include "Core/RTSGameState.h"
#include "Core/RTSPlayerState.h"
#include "Core/RTSSettings.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Player/RTSCameraPawn.h"
#include "Player/RTSHUD.h"
#include "Player/RTSPlayerController.h"
#include "WorldDivide.h"

namespace
{
	/** How far below its PlayerStart the ground under a headquarters may lie. */
	constexpr float GroundSearchDepth = 1000.f;

	/** The team of a PlayerStart tagged "Team<id>", or INDEX_NONE for any other tag. */
	int32 ParseTeamTag(FName Tag)
	{
		FString Text = Tag.ToString();
		return Text.RemoveFromStart(TEXT("Team")) && !Text.IsEmpty() && Text.IsNumeric() ? FCString::Atoi(*Text) : INDEX_NONE;
	}
}

ARTSGameMode::ARTSGameMode()
{
	DefaultPawnClass = ARTSCameraPawn::StaticClass();
	PlayerControllerClass = ARTSPlayerController::StaticClass();
	PlayerStateClass = ARTSPlayerState::StaticClass();
	GameStateClass = ARTSGameState::StaticClass();
	HUDClass = ARTSHUD::StaticClass();
}

FString ARTSGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
	const FString& Options, const FString& Portal)
{
	// The team must be known before Super picks the start spot.
	if (ARTSPlayerState* PlayerState = NewPlayerController->GetPlayerState<ARTSPlayerState>())
	{
		PlayerState->SetTeamId(NextTeamId++);
		PlayerState->SetFaction(GetDefault<URTSSettings>()->DefaultFaction.LoadSynchronous());
		PlayerState->AddCredits(StartingCredits - PlayerState->GetCredits());
	}
	return Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
}

AActor* ARTSGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (const ARTSPlayerState* PlayerState = Player ? Player->GetPlayerState<ARTSPlayerState>() : nullptr)
	{
		const FName TeamTag(*FString::Printf(TEXT("Team%d"), PlayerState->GetTeamId()));
		for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
		{
			if (It->PlayerStartTag == TeamTag)
			{
				return *It;
			}
		}
	}
	return Super::ChoosePlayerStart_Implementation(Player);
}

void ARTSGameMode::StartPlay()
{
	// Before BeginPlay goes out, so the headquarters starts play along with the map.
	SpawnHeadquarters();
	Super::StartPlay();
}

void ARTSGameMode::SpawnHeadquarters()
{
	UWorld* World = GetWorld();
	TSet<int32> Teams;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		const int32 TeamId = ParseTeamTag(It->PlayerStartTag);
		if (TeamId == INDEX_NONE || Teams.Contains(TeamId))
		{
			continue;
		}
		Teams.Add(TeamId);

		const ARTSPlayerState* Player = ARTSPlayerState::FindForTeam(*World, TeamId);
		const URTSFactionData* Faction = Player && Player->GetFaction()
			? Player->GetFaction() : GetDefault<URTSSettings>()->DefaultFaction.LoadSynchronous();
		URTSBuildingData* Data = Faction ? Faction->Headquarters.Get() : nullptr;
		if (!Data)
		{
			UE_LOG(LogWorldDivide, Warning, TEXT("Team %d's faction %s has no headquarters"), TeamId, *GetNameSafe(Faction));
			continue;
		}

		// The PlayerStart floats above the ground; the headquarters stands on it.
		FVector Location = RTSBuildPlacement::SnapToGrid(It->GetActorLocation(), Data->Footprint);
		FHitResult GroundHit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RTSHeadquartersGround));
		Params.AddIgnoredActor(*It);
		if (World->LineTraceSingleByObjectType(GroundHit, Location, Location - FVector(0.f, 0.f, GroundSearchDepth), ECC_WorldStatic, Params))
		{
			Location.Z = GroundHit.Location.Z;
		}

		const FTransform Transform(Location);
		ARTSBuilding* Building = World->SpawnActorDeferred<ARTSBuilding>(
			ARTSBuilding::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Building)
		{
			continue;
		}
		Building->InitFromData(Data, TeamId);
		Building->FinishSpawning(Transform);
		// Enough to complete it even while the team is low on power.
		Building->UpdateConstruction(Data->BuildTime / ARTSPlayerState::LowPowerProductionRate);
	}
}
