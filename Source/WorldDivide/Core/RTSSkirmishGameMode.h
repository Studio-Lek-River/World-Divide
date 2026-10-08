#pragma once

#include "CoreMinimal.h"
#include "Core/RTSGameMode.h"
#include "RTSSkirmishGameMode.generated.h"

/**
 * The game mode of a skirmish map such as L_Skirmish, played with the seats the lobby passes in the Seats option. Without
 * one, a player takes team 0 and an AI opponent of Normal difficulty team 1, from their own start spots; the other seats
 * stay closed.
 */
UCLASS()
class WORLDDIVIDE_API ARTSSkirmishGameMode : public ARTSGameMode
{
	GENERATED_BODY()

public:
	ARTSSkirmishGameMode()
	{
		const auto MakeSeat = [](ERTSSeatType Type, int32 StartSpot)
		{
			FRTSSeat Seat;
			Seat.Type = Type;
			Seat.StartSpot = StartSpot;
			return Seat;
		};
		Seats = { MakeSeat(ERTSSeatType::Open, 0), MakeSeat(ERTSSeatType::AI, 1), MakeSeat(ERTSSeatType::Closed, INDEX_NONE),
			MakeSeat(ERTSSeatType::Closed, INDEX_NONE) };
	}
};
