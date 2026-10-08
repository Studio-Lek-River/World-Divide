#pragma once

#include "CoreMinimal.h"
#include "Core/RTSSkirmishSeats.h"
#include "GameFramework/GameStateBase.h"
#include "RTSLobbyGameState.generated.h"

class URTSMapData;

/** The lobby's map and seats, kept by the server and replicated to every player; seat tokens stay on the server. */
UCLASS()
class WORLDDIVIDE_API ARTSLobbyGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	const TArray<FRTSSeat>& GetSeats() const { return Seats; }

	/** Server only. */
	TArray<FRTSSeat>& GetMutableSeats() { return Seats; }

	/** The start spots of the chosen map, one per seat. */
	int32 GetSpotCount() const { return Seats.Num(); }

	/** The chosen map's index in URTSSettings::SkirmishMaps. */
	int32 GetMapIndex() const { return MapIndex; }

	/** Server only. */
	void SetMapIndex(int32 Index) { MapIndex = Index; }

	/** The chosen map, loaded; null when SkirmishMaps has no such entry. */
	const URTSMapData* GetMap() const { return LoadMap(MapIndex); }

	/** The map at Index in URTSSettings::SkirmishMaps, loaded; null when there is none. */
	static const URTSMapData* LoadMap(int32 Index);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(Replicated)
	TArray<FRTSSeat> Seats;

	UPROPERTY(Replicated)
	int32 MapIndex = 0;
};
