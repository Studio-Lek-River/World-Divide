#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Data/RTSAIBuildOrderData.h"
#include "Templates/Function.h"
#include "RTSAIPlayer.generated.h"

class ARTSBuilding;
class ARTSPlayerState;
class ARTSUnit;
class URTSBuildingData;
class URTSEntityData;
struct FRTSCommand;

namespace RTSAI
{
	/** The first step of which CountOwned reports fewer than its Count, or INDEX_NONE once every step is met. */
	WORLDDIVIDE_API int32 FindCurrentStep(TConstArrayView<FRTSBuildOrderStep> Steps, TFunctionRef<int32(const URTSEntityData&)> CountOwned);

	/** BuildOrder's waves scaled for Difficulty: sizes rounded, at least 1, MinSize never above Size. */
	WORLDDIVIDE_API FRTSAIWaveSettings GetWaveSettings(const URTSAIBuildOrderData& BuildOrder, ERTSAIDifficulty Difficulty);

	/** An army of ArmySize goes now: it reached Size, or Interval has passed and it is at least MinSize. */
	WORLDDIVIDE_API bool ShouldLaunchWave(const FRTSAIWaveSettings& Settings, int32 ArmySize, float SecondsSinceLastWave);

	/**
	 * Of Enemies within Radius of any of Buildings (2D), or flagged in Attacked (one per enemy, or empty) for a recent hit on
	 * one wherever they stand, the one nearest a building, or INDEX_NONE.
	 */
	WORLDDIVIDE_API int32 FindThreat(TConstArrayView<FVector> Enemies, TConstArrayView<FVector> Buildings, float Radius,
		TConstArrayView<bool> Attacked = {});
}

/**
 * An AI opponent on the server: a player without a connection, with its own ARTSPlayerState (team, faction, credits) that the
 * game mode sets up like a player's. Every ThinkInterval it follows its faction's URTSAIBuildOrderData: idle builders place
 * the next building near its headquarters, production buildings queue the next unit or upgrade, idle harvesters go back to
 * work and the army gathers ahead of the base until a wave goes at the nearest enemy building, unless an enemy unit comes
 * near its buildings, or an enemy unit or building fires on them from further out: then the army, and wave units still near
 * the base, fight it off first. Its orders take the same path as a player's (RTSCommands::Issue), paid from its own credits, and it sees what every
 * player sees.
 */
UCLASS()
class WORLDDIVIDE_API ARTSAIPlayer : public AAIController
{
	GENERATED_BODY()

public:
	ARTSAIPlayer();

	/** Seconds between two rounds of decisions. */
	static constexpr float ThinkInterval = 1.f;

	/** Free space kept between the AI's buildings and anything else on the ground, in cm. */
	static constexpr float BuildingGap = 300.f;

	/** How far ahead of the base, towards the enemy, the army gathers between waves, in cm. */
	static constexpr float GatherDistance = 1500.f;

	/** How close to one of the AI's buildings an enemy unit comes before the army turns on it, in cm. */
	static constexpr float DefenseRadius = 2500.f;

	/** How long an enemy unit or building that hit one of the AI's buildings stays a threat wherever it is, in seconds. */
	static constexpr float AttackerMemory = 10.f;

	/** Server only. Building was hit: every AI on its team remembers DamageInstigator if it is an enemy unit or building. */
	static void NotifyBuildingDamaged(const ARTSBuilding& Building, AActor* DamageInstigator);

	void SetDifficulty(ERTSAIDifficulty InDifficulty) { Difficulty = InDifficulty; }
	ERTSAIDifficulty GetDifficulty() const { return Difficulty; }

	/** Where the AI's base is when it has no headquarters: its team's PlayerStart. */
	void SetStartLocation(const FVector& Location) { StartLocation = Location; }

	ARTSPlayerState* GetRTSPlayerState() const;

	/** The build order of the AI's faction, or null. */
	const URTSAIBuildOrderData* GetBuildOrder() const;

	/** The waves of the build order for the AI's difficulty, unless SetWaveSettings replaced them. */
	FRTSAIWaveSettings GetWaveSettings() const;

	/** Replaces the waves of the build order, as tests do to bring a wave on sooner. */
	void SetWaveSettings(const FRTSAIWaveSettings& Settings) { WaveOverride = Settings; }

	/** Server only. One round of decisions, DeltaSeconds after the last. Does nothing once the match is over. */
	void Think(float DeltaSeconds);

	/**
	 * How many of Entry count for the build order: own buildings standing, construction sites and buildings a builder is on
	 * its way to place included; own units alive and queued; an upgrade researched or queued is 1.
	 */
	int32 CountOwned(const URTSEntityData& Entry) const;

	/** Orders the AI has issued so far. */
	int32 GetIssuedCommandCount() const { return IssuedCommands; }

	/** Attack waves the AI has sent so far. */
	int32 GetWaveCount() const { return Waves; }

	/** Units sent in a wave that are still alive. */
	TArray<ARTSUnit*> GetWaveUnits() const;

	/**
	 * An enemy unit is within DefenseRadius of the AI's buildings, or an enemy unit or building hit one within the last
	 * AttackerMemory seconds.
	 */
	bool IsDefending() const { return bDefending; }

	/** The enemy unit or building the army is sent at while defending, or null. */
	AActor* GetThreat() const { return Threat.Get(); }

	/** Units pulled in to fight an enemy in the base that are still alive. */
	TArray<ARTSUnit*> GetDefenders() const;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** A builder sent to place Data at Location, or to resume Site, that has not placed it yet. */
	struct FPendingBuild
	{
		TWeakObjectPtr<ARTSUnit> Builder;
		TWeakObjectPtr<URTSBuildingData> Data;
		FVector Location = FVector::ZeroVector;
		TWeakObjectPtr<ARTSBuilding> Site;
	};

	void ThinkOnTimer();

	/** Sends Command for Units through RTSCommands::Issue, as a player's controller does. */
	void Issue(TConstArrayView<ARTSUnit*> Units, const FRTSCommand& Command);

	/** Drops pending builds whose builder has died, placed its building or given up. */
	void PrunePendingBuilds();

	/** Sends idle builders to construction sites no builder is working on, such as one whose builder died. */
	void ResumeSites();

	/** Works on the current step of the build order, or on the army once every step is met. */
	void FollowBuildOrder();

	/** Sends an idle builder to place Data near the base. False when it cannot yet: no credits, builder or spot. */
	bool TryBuild(URTSBuildingData& Data);

	/** Queues Entry, a unit or upgrade, at the own building that offers it with the shortest queue. */
	bool TryProduce(URTSEntityData& Entry);

	/** Keeps the army growing once the build order is done: the next affordable army unit, a short queue per building. */
	void ProduceArmy(const URTSAIBuildOrderData& BuildOrder);

	/** Sends idle harvesters back to the nearest source, while one is in reach of an own depot. */
	void KeepHarvesting();

	/** Points the rally point of each building that produces army units at the gather point, once. */
	void SetRallyPoints(const URTSAIBuildOrderData& BuildOrder);

	/** Sends a wave when the army is ready, and moves wave units that have nothing left to fight on to the next building. */
	void UpdateWaves(float DeltaSeconds);

	/**
	 * Sends the army, and wave units still near the base, at the enemy nearest the AI's buildings while an enemy unit is
	 * within DefenseRadius or an enemy unit or building is a recent attacker, and back to the gather point once none is left.
	 * Attackers age by DeltaSeconds.
	 */
	void DefendBase(float DeltaSeconds);

	/** Ahead of the base, towards the nearest enemy building. False without an enemy building. */
	bool GetGatherPoint(FVector& OutLocation) const;

	/** An own idle builder, not on its way to a pending build, that can build Data (any building when null). */
	ARTSUnit* FindIdleBuilder(const URTSBuildingData* Data) const;

	/**
	 * A spot for Data on a ring around the base, or around the supply source nearest it for a depot, that a builder can place
	 * it on and that keeps BuildingGap to everything else, the exits of own production buildings and pending builds.
	 */
	bool FindBuildSpot(const URTSBuildingData& Data, FVector& OutLocation) const;

	/** The footprint at Location keeps BuildingGap to every building, supply source, pending build and own exit lane. */
	bool HasRoom(const URTSBuildingData& Data, const FVector& Location) const;

	/** The own headquarters, else the PlayerStart, else any own building, else the world origin. */
	FVector GetBaseLocation() const;

	/** The standing enemy building nearest From, or null. */
	ARTSBuilding* FindNearestEnemyBuilding(const FVector& From) const;

	/** Own armed units that are neither builders nor harvesters, and are not in a wave. */
	TArray<ARTSUnit*> GatherArmy() const;

	ERTSAIDifficulty Difficulty = ERTSAIDifficulty::Normal;

	TOptional<FVector> StartLocation;

	TOptional<FRTSAIWaveSettings> WaveOverride;

	TArray<FPendingBuild> PendingBuilds;

	TArray<TWeakObjectPtr<ARTSUnit>> WaveUnits;

	TSet<TWeakObjectPtr<ARTSBuilding>> RalliedBuildings;

	TArray<TWeakObjectPtr<ARTSUnit>> Defenders;

	/** Enemy units and buildings that hit an own building, with the seconds since their last hit. */
	TMap<TWeakObjectPtr<AActor>, float> Attackers;

	TWeakObjectPtr<AActor> Threat;

	bool bDefending = false;

	float SecondsSinceWave = 0.f;

	/** Which of ArmyUnits to produce next. */
	int32 NextArmyUnit = 0;

	int32 IssuedCommands = 0;

	int32 Waves = 0;

	FTimerHandle ThinkTimer;
};
