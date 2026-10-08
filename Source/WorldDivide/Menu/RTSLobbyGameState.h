#pragma once

#include "CoreMinimal.h"
#include "Core/RTSSkirmishSeats.h"
#include "GameFramework/GameStateBase.h"
#include "RTSLobbyGameState.generated.h"

/** The lobby's seats, kept by the server and replicated to every player; seat tokens stay on the server. */
UCLASS()
class WORLDDIVIDE_API ARTSLobbyGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	const TArray<FRTSSeat>& GetSeats() const { return Seats; }

	/** Server only. */
	TArray<FRTSSeat>& GetMutableSeats() { return Seats; }

	/** The start spots of the skirmish map, one per seat. */
	int32 GetSpotCount() const { return Seats.Num(); }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(Replicated)
	TArray<FRTSSeat> Seats;
};
