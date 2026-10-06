#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "RTSPlayerState.generated.h"

class URTSFactionData;

UCLASS()
class WORLDDIVIDE_API ARTSPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	int32 GetTeamId() const { return TeamId; }

	/** Server only. */
	void SetTeamId(int32 InTeamId);

	URTSFactionData* GetFaction() const { return Faction; }

	/** Server only. */
	void SetFaction(URTSFactionData* InFaction);

	/** Money to build with. Only the owning player sees it replicate. */
	int32 GetCredits() const { return Credits; }

	/** Server only. A negative Delta takes credits away, never below zero. */
	void AddCredits(int32 Delta);

	/** Server only. Takes Amount and returns true, or changes nothing and returns false when the player cannot afford it. */
	bool TrySpendCredits(int32 Amount);

	/** The player playing TeamId, or null when no player does (units placed by the map for an empty seat). */
	static ARTSPlayerState* FindForTeam(const UWorld& World, int32 InTeamId);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(Replicated)
	int32 TeamId = 0;

	UPROPERTY(Replicated)
	TObjectPtr<URTSFactionData> Faction;

	UPROPERTY(Replicated)
	int32 Credits = 0;
};
