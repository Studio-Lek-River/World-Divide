#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "RTSGameState.generated.h"

class ARTSBuilding;

UENUM(BlueprintType)
enum class ERTSMatchResult : uint8
{
	InProgress,
	Victory,
	Defeat,
};

/**
 * Tracks which teams are still in the match and how it ended. A team is in while it has a building that is not a defence,
 * construction sites included; the match ends once a team drops out and at most one is left, which then wins.
 * The server keeps it up to date as buildings come and go; every client reads the replicated result.
 */
UCLASS()
class WORLDDIVIDE_API ARTSGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	/** Building keeps its team in the match: it has data and is not a defence. */
	static bool CountsForSurvival(const ARTSBuilding& Building);

	/**
	 * Server only. Rebuilds the teams still in from the buildings standing, leaving out Removed (a building being destroyed),
	 * and ends the match once a team dropped out and at most one is left. Nothing changes once the match is over.
	 */
	void RefreshTeams(const ARTSBuilding* Removed = nullptr);

	/** Teams with a building that counts for survival, in ascending order. */
	const TArray<int32>& GetAliveTeams() const { return AliveTeams; }

	bool IsMatchOver() const { return bMatchOver; }

	/** The team left standing, or INDEX_NONE while the match runs or when no team is left. */
	int32 GetWinningTeam() const { return WinningTeam; }

	/** InProgress while the match runs, then Victory for the winning team and Defeat for every other. */
	ERTSMatchResult GetMatchResult(int32 TeamId) const;

	/** Whether the match in World is over; false for a world without an ARTSGameState. */
	static bool IsMatchOver(const UWorld& World);

	/** Server only. Lists Building under its team, moving it there from any other team's list. */
	void RegisterBuilding(ARTSBuilding& Building);

	/** Server only. Takes Building off its team's list. */
	void UnregisterBuilding(const ARTSBuilding& Building);

	/** Server only. Calls Visit for every standing building of a team other than TeamId. */
	void ForEachEnemyBuilding(int32 TeamId, TFunctionRef<void(ARTSBuilding&)> Visit) const;

	/** Server only. Calls Visit for every standing building of every team. */
	void ForEachBuilding(TFunctionRef<void(ARTSBuilding&)> Visit) const;

	virtual void PostInitializeComponents() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(Replicated)
	TArray<int32> AliveTeams;

	UPROPERTY(Replicated)
	bool bMatchOver = false;

	UPROPERTY(Replicated)
	int32 WinningTeam = INDEX_NONE;

	/** Server only, not replicated: each team's buildings, so weapons need not scan every actor for them. */
	TMap<int32, TArray<TWeakObjectPtr<ARTSBuilding>>> BuildingsByTeam;
};
