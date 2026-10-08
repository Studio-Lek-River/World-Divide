#pragma once

#include "CoreMinimal.h"
#include "Data/RTSAIBuildOrderData.h"
#include "Math/RandomStream.h"
#include "RTSSkirmishSeats.generated.h"

/** Who plays a team seat of a skirmish. */
UENUM(BlueprintType)
enum class ERTSSeatType : uint8
{
	/** Free for a human: the first player to join without a seat of their own takes it. */
	Open,
	/** Held by a human from the lobby, known by their seat token. */
	Human,
	AI,
	/** Nobody plays it: no headquarters, no starting units. */
	Closed,
};

/** One team seat. Its index in the seat list is the team id, which also sets the team's colour. */
USTRUCT(BlueprintType)
struct WORLDDIVIDE_API FRTSSeat
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Seat")
	ERTSSeatType Type = ERTSSeatType::Closed;

	/** How an AI seat plays. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Seat")
	ERTSAIDifficulty Difficulty = ERTSAIDifficulty::Normal;

	/** The PlayerStart tagged "Team<StartSpot>" the team plays from, or INDEX_NONE for a random free one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Seat")
	int32 StartSpot = INDEX_NONE;

	/** Lobby only: the APlayerState::GetPlayerId of the human holding the seat, or INDEX_NONE. */
	UPROPERTY()
	int32 PlayerId = INDEX_NONE;

	/** Lobby only: the name of the human holding the seat. */
	UPROPERTY()
	FString PlayerName;

	/** The secret by which a human seat finds its player again after travelling to the match. Never sent to other players. */
	UPROPERTY(NotReplicated)
	FString PlayerToken;

	/** Lobby only: the human holding the seat is done setting it up. The host always is. */
	UPROPERTY()
	bool bReady = false;

	bool operator==(const FRTSSeat& Other) const = default;
};

/** The pure rules of skirmish seats, shared by the lobby and the match's game mode. */
namespace RTSSkirmish
{
	/** Easy, Normal or Hard, ignoring case. */
	WORLDDIVIDE_API bool ParseDifficulty(const FString& Text, ERTSAIDifficulty& OutDifficulty);

	/**
	 * Seats from a comma-separated list such as "Human@3~<token>,AI-Hard@1,Open,Closed": the type, for an AI an optional
	 * difficulty (Normal without one), an optional start spot after '@' and, for a human, an optional token after '~'
	 * (a '#' would end the URL the list travels in). False, leaving OutSeats empty, on anything else.
	 */
	WORLDDIVIDE_API bool ParseSeats(const FString& Text, TArray<FRTSSeat>& OutSeats);

	/** The list ParseSeats reads back as Seats; lobby-only names and player ids are left out. */
	WORLDDIVIDE_API FString FormatSeats(TConstArrayView<FRTSSeat> Seats);

	/** The seat takes part in a match: everything but Closed. An Open seat waits for a player. */
	WORLDDIVIDE_API bool IsPlayed(const FRTSSeat& Seat);

	/**
	 * Gives each played seat with a random start spot one of the spots 0..SpotCount-1 no other seat has, drawn from Random,
	 * and clears the spot of Closed seats. False, changing nothing, when a chosen spot is out of range, two seats share
	 * one or there are not enough spots.
	 */
	WORLDDIVIDE_API bool AssignStartSpots(TArray<FRTSSeat>& Seats, int32 SpotCount, FRandomStream& Random);

	/**
	 * The seat a joining human plays, among those not in TakenSeats: the Human seat holding Token, else the first Open
	 * seat, else the first Human seat without a token, else the first Human seat. INDEX_NONE when none is left.
	 */
	WORLDDIVIDE_API int32 FindSeatForPlayer(TConstArrayView<FRTSSeat> Seats, const FString& Token, const TSet<int32>& TakenSeats);

	/**
	 * Lobby: the first Open seat becomes Human for this player, ready when it is the host's. Its index, or INDEX_NONE when
	 * no seat is open.
	 */
	WORLDDIVIDE_API int32 TakeOpenSeat(TArray<FRTSSeat>& Seats, int32 PlayerId, const FString& PlayerName, const FString& Token,
		bool bIsHost);

	/** Lobby: the seat PlayerId holds opens again, keeping its start spot but not its ready flag. False when PlayerId holds none. */
	WORLDDIVIDE_API bool ReleaseSeat(TArray<FRTSSeat>& Seats, int32 PlayerId);

	/** Lobby: the host may make a seat Open, Closed or an AI, unless a human holds it. */
	WORLDDIVIDE_API bool CanChangeType(TConstArrayView<FRTSSeat> Seats, int32 Index, bool bIsHost, ERTSSeatType NewType);

	/**
	 * Lobby: the host may set the start spot of any seat but a Closed one, a player only that of its own seat. The spot is
	 * random or in 0..SpotCount-1, and no other seat has it.
	 */
	WORLDDIVIDE_API bool CanChangeStartSpot(TConstArrayView<FRTSSeat> Seats, int32 Index, bool bIsHost, int32 PlayerId,
		int32 Spot, int32 SpotCount);

	/**
	 * Lobby: moves seat Index to Spot once CanChangeStartSpot allows it. A seat that moves must be readied again, unless
	 * it is the host's own. False, changing nothing, when refused.
	 */
	WORLDDIVIDE_API bool ChangeStartSpot(TArray<FRTSSeat>& Seats, int32 Index, bool bIsHost, int32 PlayerId, int32 Spot,
		int32 SpotCount);

	/** Lobby: a player readies its own seat or takes that back. False for the host, which is always ready, or without a seat. */
	WORLDDIVIDE_API bool SetReady(TArray<FRTSSeat>& Seats, int32 PlayerId, bool bIsHost, bool bReady);

	/** Lobby: at least one human and two seats with a player or an AI, and every human ready. */
	WORLDDIVIDE_API bool CanStart(TConstArrayView<FRTSSeat> Seats);

	/** Lobby: the seats the match is played with. Open seats nobody took are closed; names, player ids and ready flags are dropped. */
	WORLDDIVIDE_API TArray<FRTSSeat> ToMatchSeats(TConstArrayView<FRTSSeat> Seats);
}
