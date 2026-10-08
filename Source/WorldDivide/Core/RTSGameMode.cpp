#include "Core/RTSGameMode.h"

#include "AI/RTSAIPlayer.h"
#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
#include "Core/RTSGameState.h"
#include "Core/RTSPlayerState.h"
#include "Core/RTSSettings.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
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

	/** The first PlayerStart tagged "Team<id>" of each team, by team id. */
	TMap<int32, APlayerStart*> FindTeamStarts(UWorld& World)
	{
		TMap<int32, APlayerStart*> Starts;
		for (TActorIterator<APlayerStart> It(&World); It; ++It)
		{
			const int32 TeamId = ParseTeamTag(It->PlayerStartTag);
			if (TeamId != INDEX_NONE && !Starts.Contains(TeamId))
			{
				Starts.Add(TeamId, *It);
			}
		}
		Starts.KeySort(TLess<int32>());
		return Starts;
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

void ARTSGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	const FString Difficulty = UGameplayStatics::ParseOption(Options, TEXT("AIDifficulty"));
	if (Difficulty.IsEmpty())
	{
		return;
	}
	const UEnum* Enum = StaticEnum<ERTSAIDifficulty>();
	for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
	{
		if (Enum->GetNameStringByIndex(Index).Equals(Difficulty, ESearchCase::IgnoreCase))
		{
			AIDifficulty = static_cast<ERTSAIDifficulty>(Enum->GetValueByIndex(Index));
			return;
		}
	}
	UE_LOG(LogWorldDivide, Warning, TEXT("Unknown AIDifficulty %s, keeping %s"), *Difficulty, *Enum->GetNameStringByValue(static_cast<int64>(AIDifficulty)));
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
	// Before BeginPlay goes out, so the headquarters starts play along with the map. The AI comes first, so its
	// headquarters is of its faction.
	if (bSpawnAIOpponents)
	{
		SpawnAIOpponents();
	}
	SpawnHeadquarters();
	Super::StartPlay();
}

void ARTSGameMode::SpawnAIOpponents()
{
	UWorld* World = GetWorld();
	for (const TPair<int32, APlayerStart*>& Start : FindTeamStarts(*World))
	{
		const int32 TeamId = Start.Key;
		if (ARTSPlayerState::FindForTeam(*World, TeamId))
		{
			continue;
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ARTSAIPlayer* AI = World->SpawnActor<ARTSAIPlayer>(Start.Value->GetActorLocation(), FRotator::ZeroRotator, Params);
		// The AI controller creates its player state as it spawns, from PlayerStateClass.
		ARTSPlayerState* PlayerState = AI ? AI->GetPlayerState<ARTSPlayerState>() : nullptr;
		if (!PlayerState)
		{
			UE_LOG(LogWorldDivide, Warning, TEXT("Could not spawn an AI opponent for team %d"), TeamId);
			if (AI)
			{
				AI->Destroy();
			}
			continue;
		}
		PlayerState->SetTeamId(TeamId);
		PlayerState->SetFaction(GetDefault<URTSSettings>()->DefaultFaction.LoadSynchronous());
		PlayerState->AddCredits(StartingCredits - PlayerState->GetCredits());
		AI->SetDifficulty(AIDifficulty);
		AI->SetStartLocation(Start.Value->GetActorLocation());
		NextTeamId = FMath::Max(NextTeamId, TeamId + 1);
		UE_LOG(LogWorldDivide, Log, TEXT("AI opponent plays team %d"), TeamId);
	}
}

void ARTSGameMode::SpawnHeadquarters()
{
	UWorld* World = GetWorld();
	for (const TPair<int32, APlayerStart*>& Start : FindTeamStarts(*World))
	{
		const int32 TeamId = Start.Key;
		APlayerStart* PlayerStart = Start.Value;
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
		FVector Location = RTSBuildPlacement::SnapToGrid(PlayerStart->GetActorLocation(), Data->Footprint);
		FHitResult GroundHit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RTSHeadquartersGround));
		Params.AddIgnoredActor(PlayerStart);
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
