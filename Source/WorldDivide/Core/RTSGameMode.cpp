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
#include "Units/RTSUnitSpawner.h"
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
	TMap<int32, APlayerStart*> FindTaggedStarts(UWorld& World)
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

const TCHAR* ARTSGameMode::SeatTokenOption = TEXT("SeatToken");

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

	if (UGameplayStatics::HasOption(Options, TEXT("Seats")))
	{
		const FString SeatsOption = UGameplayStatics::ParseOption(Options, TEXT("Seats"));
		TArray<FRTSSeat> Parsed;
		if (RTSSkirmish::ParseSeats(SeatsOption, Parsed))
		{
			Seats = MoveTemp(Parsed);
		}
		else
		{
			UE_LOG(LogWorldDivide, Warning, TEXT("Unknown Seats %s, keeping the default seats"), *SeatsOption);
		}
	}
	FindTeamStarts();
}

void ARTSGameMode::FindTeamStarts()
{
	const TMap<int32, APlayerStart*> Tagged = FindTaggedStarts(*GetWorld());
	TeamStarts.Reset();
	if (Seats.IsEmpty())
	{
		for (const TPair<int32, APlayerStart*>& Start : Tagged)
		{
			TeamStarts.Add(Start.Key, Start.Value);
		}
		return;
	}

	int32 SpotCount = 0;
	for (const TPair<int32, APlayerStart*>& Start : Tagged)
	{
		SpotCount = FMath::Max(SpotCount, Start.Key + 1);
	}
	FRandomStream Random(FMath::Rand());
	if (!RTSSkirmish::AssignStartSpots(Seats, SpotCount, Random))
	{
		UE_LOG(LogWorldDivide, Warning, TEXT("Seats %s do not fit the %d start spots of this map"),
			*RTSSkirmish::FormatSeats(Seats), SpotCount);
		return;
	}
	for (int32 TeamId = 0; TeamId < Seats.Num(); ++TeamId)
	{
		if (APlayerStart* const* Start = Tagged.Find(Seats[TeamId].StartSpot))
		{
			TeamStarts.Add(TeamId, *Start);
		}
	}
	UE_LOG(LogWorldDivide, Log, TEXT("Playing with seats %s"), *RTSSkirmish::FormatSeats(Seats));
}

APlayerStart* ARTSGameMode::FindTeamStart(int32 TeamId) const
{
	const TObjectPtr<APlayerStart>* Start = TeamStarts.Find(TeamId);
	return Start ? Start->Get() : nullptr;
}

int32 ARTSGameMode::ChooseTeam(const FString& Options, const APlayerState* Joining) const
{
	TSet<int32> Taken;
	if (const AGameStateBase* State = GameState.Get())
	{
		for (const APlayerState* Player : State->PlayerArray)
		{
			const ARTSPlayerState* RTSPlayer = Cast<ARTSPlayerState>(Player);
			if (RTSPlayer && RTSPlayer != Joining)
			{
				Taken.Add(RTSPlayer->GetTeamId());
			}
		}
	}
	// AI opponents take their seats once play starts.
	for (int32 TeamId = 0; TeamId < Seats.Num(); ++TeamId)
	{
		if (Seats[TeamId].Type == ERTSSeatType::AI)
		{
			Taken.Add(TeamId);
		}
	}
	return RTSSkirmish::FindSeatForPlayer(Seats, UGameplayStatics::ParseOption(Options, SeatTokenOption), Taken);
}

void ARTSGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (ErrorMessage.IsEmpty() && !Seats.IsEmpty() && ChooseTeam(Options, nullptr) == INDEX_NONE)
	{
		ErrorMessage = TEXT("No free seat");
	}
}

FString ARTSGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
	const FString& Options, const FString& Portal)
{
	// The team must be known before Super picks the start spot.
	if (ARTSPlayerState* PlayerState = NewPlayerController->GetPlayerState<ARTSPlayerState>())
	{
		const int32 TeamId = Seats.IsEmpty() ? NextTeamId++ : ChooseTeam(Options, PlayerState);
		if (TeamId == INDEX_NONE)
		{
			UE_LOG(LogWorldDivide, Warning, TEXT("No free seat for %s"), *PlayerState->GetPlayerName());
		}
		PlayerState->SetTeamId(TeamId);
		PlayerState->SetFaction(GetDefault<URTSSettings>()->DefaultFaction.LoadSynchronous());
		PlayerState->AddCredits(StartingCredits - PlayerState->GetCredits());
	}
	return Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
}

AActor* ARTSGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (const ARTSPlayerState* PlayerState = Player ? Player->GetPlayerState<ARTSPlayerState>() : nullptr)
	{
		if (APlayerStart* Start = FindTeamStart(PlayerState->GetTeamId()))
		{
			return Start;
		}
	}
	return Super::ChoosePlayerStart_Implementation(Player);
}

FString ARTSGameMode::GetRestartURL() const
{
	if (Seats.IsEmpty())
	{
		return TEXT("?Restart");
	}
	// The start spots stay where they fell; the humans find their seats again by their tokens.
	FString URL = UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName());
	if (GetNetMode() == NM_ListenServer)
	{
		URL += TEXT("?listen");
	}
	return URL + TEXT("?Seats=") + RTSSkirmish::FormatSeats(Seats);
}

void ARTSGameMode::StartPlay()
{
	// Before BeginPlay goes out, so the starting units and the headquarters start play along with the map. The AI comes
	// first, so its headquarters is of its faction.
	if (!Seats.IsEmpty())
	{
		AssignUnitSpawners();
		SpawnAIOpponents();
	}
	SpawnHeadquarters();
	Super::StartPlay();
}

void ARTSGameMode::AssignUnitSpawners()
{
	// The map places each spot's starting units with the team id of the spot.
	TMap<int32, int32> TeamBySpot;
	for (const TPair<int32, TObjectPtr<APlayerStart>>& Start : TeamStarts)
	{
		TeamBySpot.Add(ParseTeamTag(Start.Value->PlayerStartTag), Start.Key);
	}
	for (TActorIterator<ARTSUnitSpawner> It(GetWorld()); It; ++It)
	{
		if (const int32* TeamId = TeamBySpot.Find(It->TeamId))
		{
			It->TeamId = *TeamId;
		}
		else
		{
			It->Destroy();
		}
	}
}

void ARTSGameMode::SpawnAIOpponents()
{
	UWorld* World = GetWorld();
	for (int32 TeamId = 0; TeamId < Seats.Num(); ++TeamId)
	{
		const APlayerStart* Start = FindTeamStart(TeamId);
		if (Seats[TeamId].Type != ERTSSeatType::AI || !Start)
		{
			continue;
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ARTSAIPlayer* AI = World->SpawnActor<ARTSAIPlayer>(Start->GetActorLocation(), FRotator::ZeroRotator, Params);
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
		AI->SetDifficulty(Seats[TeamId].Difficulty);
		AI->SetStartLocation(Start->GetActorLocation());
		UE_LOG(LogWorldDivide, Log, TEXT("AI opponent plays team %d from %s"), TeamId, *Start->PlayerStartTag.ToString());
	}
}

void ARTSGameMode::SpawnHeadquarters()
{
	UWorld* World = GetWorld();
	for (const TPair<int32, TObjectPtr<APlayerStart>>& Start : TeamStarts)
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
