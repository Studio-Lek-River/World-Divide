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

	/**
	 * The same, passing over the steps CanWorkOn rules out because nothing left could produce or build them, such as builders
	 * once the headquarters that made them is gone (#179). INDEX_NONE once every step is met or out of reach.
	 */
	WORLDDIVIDE_API int32 FindCurrentStep(TConstArrayView<FRTSBuildOrderStep> Steps, TFunctionRef<int32(const URTSEntityData&)> CountOwned,
		TFunctionRef<bool(const URTSEntityData&)> CanWorkOn);

	/** BuildOrder's waves scaled for Difficulty: sizes rounded, at least 1, MinSize never above Size. */
	WORLDDIVIDE_API FRTSAIWaveSettings GetWaveSettings(const URTSAIBuildOrderData& BuildOrder, ERTSAIDifficulty Difficulty);

	/**
	 * An army of ArmySize goes now: it reached Size, or Interval has passed and it is at least MinSize, or of any size once
	 * Interval has passed if it cannot grow any more (bCanGrow false, #179), so a side out of credits still fights on.
	 */
	WORLDDIVIDE_API bool ShouldLaunchWave(const FRTSAIWaveSettings& Settings, int32 ArmySize, float SecondsSinceLastWave, bool bCanGrow = true);

	/**
	 * Of Enemies within Radius of any of Buildings (2D), or flagged in Attacked (one per enemy, or empty) for a recent hit on
	 * one wherever they stand, the one nearest a building, or INDEX_NONE.
	 */
	WORLDDIVIDE_API int32 FindThreat(TConstArrayView<FVector> Enemies, TConstArrayView<FVector> Buildings, float Radius,
		TConstArrayView<bool> Attacked = {});

	/** Share of Army (rounded up, at least 1 of a non-empty army) to send at Threat: their indices, nearest first (2D). */
	WORLDDIVIDE_API TArray<int32> PickResponders(TConstArrayView<FVector> Army, const FVector& Threat, float Share);

	/** One side of a fight between a group of units and a defence, as CanOvercome weighs it. */
	struct FFighter
	{
		float Health = 0.f;
		float DamagePerSecond = 0.f;
		/** Weapon range in cm. */
		float Range = 0.f;
		/** Move speed in cm/s. */
		float Speed = 0.f;
		/** An attacker's distance from the defence's footprint in cm; negative sets off from the edge of its range. */
		float Distance = -1.f;
	};

	/**
	 * Attackers close in on Defence from their Distance and fire once each is within its own range; Defence fires at the one
	 * within its range with the shortest range first, the nearest it, and never at one that outranges it (#181). True when
	 * the attackers deal Margin times Defence's health before every one of them is destroyed.
	 */
	WORLDDIVIDE_API bool CanOvercome(TConstArrayView<FFighter> Attackers, const FFighter& Defence, float Margin = 1.f);
}

/**
 * An AI opponent on the server: a player without a connection, with its own ARTSPlayerState (team, faction, credits) that the
 * game mode sets up like a player's. Every ThinkInterval it follows its faction's URTSAIBuildOrderData: idle builders place
 * the next building near its headquarters, production buildings queue the next unit or upgrade (passing over a step nothing
 * it has left could work on), idle harvesters go back to work and the army gathers ahead of the base until a wave goes at the
 * nearest enemy building, unless an enemy unit comes
 * near its buildings, or an enemy unit or building fires on them from further out: then the army, and wave units still near
 * the base, fight it off first. An enemy that hits one of its builders or harvesters elsewhere draws part of the army. Waves
 * weigh the enemy's defences (#181): they go at a building no defence covers, or first at a covering defence they can
 * destroy, and wave units that come under fire of a defence close in on it together, or fall back when they cannot beat
 * it. A step it cannot afford while no harvester is bringing credits is passed over for the army, and an idle harvester
 * with no source in its search radius is sent to the nearest source elsewhere that no enemy defence covers. Its orders
 * take the same path as a player's (RTSCommands::Issue), paid from its own credits, and it sees what every player sees.
 */
UCLASS()
class WORLDDIVIDE_API ARTSAIPlayer : public AAIController
{
	GENERATED_BODY()

public:
	ARTSAIPlayer();

	/** Seconds between two rounds of decisions. */
	static constexpr float ThinkInterval = 1.f;

	/** Room left on either side of the widest nav agent in the lane between two of the AI's buildings, in cm. */
	static constexpr float VehicleLaneMargin = 200.f;

	/**
	 * Free space kept between the AI's buildings and anything else on the ground, in cm: a lane the widest nav agent drives
	 * through with VehicleLaneMargin on either side, in whole grid cells (8 m with the Vehicle agent's 190 cm radius).
	 */
	static float GetBuildingGap();

	/** How far past the edge of its base building, towards the enemy, the army gathers between waves, in cm. */
	static constexpr float GatherDistance = 3600.f;

	/**
	 * How close to one of the AI's buildings (its centre) an enemy unit comes before the army turns on it, in cm: a tank's
	 * range (65 m) outside the edge of a 28 m headquarters.
	 */
	static constexpr float DefenseRadius = 8000.f;

	/** How long an enemy unit or building that hit one of the AI's buildings stays a threat wherever it is, in seconds. */
	static constexpr float AttackerMemory = 10.f;

	/** Server only. Building was hit: every AI on its team remembers DamageInstigator if it is an enemy unit or building. */
	static void NotifyBuildingDamaged(const ARTSBuilding& Building, AActor* DamageInstigator);

	/** Share of the idle army sent at an enemy that hit one of the AI's builders or harvesters. */
	static constexpr float ResponseShare = 0.5f;

	/**
	 * How many times a defence's health a wave must be able to deal (RTSAI::CanOvercome) before it sets off at it, while the
	 * army can still grow; once it cannot, once its health.
	 */
	static constexpr float AssaultMargin = 1.5f;

	/**
	 * How far past a defence's range wave units still count as in its fight, in cm: they join the attack on it, or fall back
	 * with the rest.
	 */
	static constexpr float AssaultReach = 3000.f;

	/** Room beyond an enemy defence's range that a far source must lie for an idle harvester to be sent to it, in cm. */
	static constexpr float SafeHarvestMargin = 1000.f;

	/**
	 * Server only. Unit was hit: if it is a builder or harvester, every AI on its team remembers DamageInstigator for
	 * AttackerMemory seconds if it is an enemy unit or building.
	 */
	static void NotifyUnitDamaged(const ARTSUnit& Unit, AActor* DamageInstigator);

	void SetDifficulty(ERTSAIDifficulty InDifficulty) { Difficulty = InDifficulty; }
	ERTSAIDifficulty GetDifficulty() const { return Difficulty; }

	/** Where the AI's base is when it has no headquarters, and the way it faces: its team's PlayerStart. */
	void SetStartLocation(const FVector& Location, float Yaw = 0.f) { StartLocation = Location; StartYaw = Yaw; }

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

	/** The enemy that most recently hit one of the AI's builders or harvesters, while remembered, or null. */
	AActor* GetUnitThreat() const { return UnitThreat.Get(); }

	/** Units sent at an enemy that hit one of the AI's builders or harvesters that are still alive. */
	TArray<ARTSUnit*> GetResponders() const;

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

	/** Works on the current step of the build order, or on the army once every step is met or out of reach. */
	void FollowBuildOrder();

	/**
	 * Something of the AI's could still produce or build Entry: an own builder that can build it, or an own building that
	 * offers the unit or upgrade, a construction site or one a builder is on its way to place included.
	 */
	bool CanWorkOn(const URTSEntityData& Entry) const;

	/**
	 * The army can still grow: an army unit is queued, a harvester is at work, or the credits buy an army unit an own
	 * building can produce now.
	 */
	bool CanArmyGrow(const URTSAIBuildOrderData& BuildOrder) const;

	/** Credits are on their way: an own harvester is at work. */
	bool HasIncomeComing() const;

	/** Sends an idle builder to place Data near the base. False when it cannot yet: no credits, builder or spot. */
	bool TryBuild(URTSBuildingData& Data);

	/** Queues Entry, a unit or upgrade, at the own building that offers it with the shortest queue. */
	bool TryProduce(URTSEntityData& Entry);

	/** Keeps the army growing once the build order is done: the next affordable army unit, a short queue per building. */
	void ProduceArmy(const URTSAIBuildOrderData& BuildOrder);

	/**
	 * Sends idle harvesters back to work while an own depot stands: at the nearest source in their search radius, or else at
	 * the nearest source anywhere that no enemy defence covers.
	 */
	void KeepHarvesting();

	/** The enemy's constructed defences whose weapons fire: armed, and powered when they need it. */
	TArray<ARTSBuilding*> FindEnemyDefences() const;

	/** Of Defences, those whose range reaches Target's footprint edge; Target itself when it is one of them. */
	static TArray<ARTSBuilding*> FindCovering(TConstArrayView<ARTSBuilding*> Defences, const AActor& Target);

	/**
	 * Units (those that can hit the ground) destroy Defence with Margin to spare while it and the other Defences in range of
	 * it fire on them (RTSAI::CanOvercome): from where they stand within its range when bWhereTheyStand, the rest from its edge.
	 */
	static bool CanOvercome(TConstArrayView<ARTSUnit*> Units, const ARTSBuilding& Defence, TConstArrayView<ARTSBuilding*> Defences,
		float Margin, bool bWhereTheyStand = false);

	/**
	 * Where Units go next, nearest their centre first: an enemy building no defence covers, or the nearest defence covering
	 * one that they can overcome with Margin. Null when every building left is covered by defences too strong for them.
	 */
	AActor* ChooseWaveTarget(TConstArrayView<ARTSUnit*> Units, TConstArrayView<ARTSBuilding*> Defences, float Margin) const;

	/** Attack-moves Units at Target. */
	void AttackMove(TConstArrayView<ARTSUnit*> Units, const AActor& Target);

	/**
	 * Wave units within AssaultReach of an enemy defence's range that it fires on: while the wave can overcome it, those not
	 * fighting enemy units attack it rather than another building; otherwise they all fall back to the gather point and
	 * rejoin the army.
	 */
	void EngageDefences(TConstArrayView<ARTSBuilding*> Defences);

	/** Points the rally point of each building that produces army units at the gather point, once. */
	void SetRallyPoints(const URTSAIBuildOrderData& BuildOrder);

	/**
	 * Lets wave units take on or fall back from the defences firing on them (EngageDefences), sends a wave when the army is
	 * ready and has a target it can take (ChooseWaveTarget), and moves wave units that have nothing left to fight on to the
	 * next one, or back to the gather point when none is left that they can take.
	 */
	void UpdateWaves(float DeltaSeconds);

	/**
	 * Sends the army, and wave units still near the base, at the enemy nearest the AI's buildings while an enemy unit is
	 * within DefenseRadius or an enemy unit or building is a recent attacker, and back to the gather point once none is left.
	 * Attackers age by DeltaSeconds.
	 */
	void DefendBase(float DeltaSeconds);

	/**
	 * Sends ResponseShare of the idle army at the enemy that most recently hit an own builder or harvester, unless the base is
	 * under attack, and back to the gather point once it is forgotten. Its attackers age by DeltaSeconds.
	 */
	void RespondToUnitAttacks(float DeltaSeconds);

	/** Ahead of the base, towards the nearest enemy building. False without an enemy building. */
	bool GetGatherPoint(FVector& OutLocation) const;

	/** An own idle builder, not on its way to a pending build, that can build Data (any building when null). */
	ARTSUnit* FindIdleBuilder(const URTSBuildingData* Data) const;

	/**
	 * A spot for Data on a ring around the base, or around the supply source nearest it for a depot, that a builder can place
	 * it on and that keeps GetBuildingGap to everything else, the exits of own production buildings and pending builds.
	 */
	bool FindBuildSpot(const URTSBuildingData& Data, FVector& OutLocation) const;

	/** The footprint at Location keeps GetBuildingGap to every building, supply source, pending build and own exit lane. */
	bool HasRoom(const URTSBuildingData& Data, const FVector& Location) const;

	/** The own headquarters, else the PlayerStart, else any own building, else the world origin. */
	FVector GetBaseLocation() const;

	/**
	 * Half the box round the faction's headquarters at the base location: turned to its own yaw while it stands, else to
	 * RTSBuildPlacement::GetHeadquartersYaw of the start spot's facing; zero without one.
	 */
	FVector GetBaseExtent() const;

	/** The standing enemy building nearest From, or null. */
	ARTSBuilding* FindNearestEnemyBuilding(const FVector& From) const;

	/** Own armed units that are neither builders, harvesters nor aircraft, and are not in a wave. */
	TArray<ARTSUnit*> GatherArmy() const;

	ERTSAIDifficulty Difficulty = ERTSAIDifficulty::Normal;

	TOptional<FVector> StartLocation;

	float StartYaw = 0.f;

	TOptional<FRTSAIWaveSettings> WaveOverride;

	TArray<FPendingBuild> PendingBuilds;

	TArray<TWeakObjectPtr<ARTSUnit>> WaveUnits;

	/** The defence each wave unit was last ordered to attack. */
	TMap<TWeakObjectPtr<ARTSUnit>, TWeakObjectPtr<ARTSBuilding>> WaveAssaults;

	/** Wave units pulled back from a defence on their way to the gather point; they join no wave before they get there. */
	TArray<TWeakObjectPtr<ARTSUnit>> Retreating;

	TSet<TWeakObjectPtr<ARTSBuilding>> RalliedBuildings;

	TArray<TWeakObjectPtr<ARTSUnit>> Defenders;

	/** Enemy units and buildings that hit an own building, with the seconds since their last hit. */
	TMap<TWeakObjectPtr<AActor>, float> Attackers;

	TWeakObjectPtr<AActor> Threat;

	/** Enemy units and buildings that hit an own builder or harvester, with the seconds since their last hit. */
	TMap<TWeakObjectPtr<AActor>, float> UnitAttackers;

	TWeakObjectPtr<AActor> UnitThreat;

	TArray<TWeakObjectPtr<ARTSUnit>> Responders;

	bool bDefending = false;

	float SecondsSinceWave = 0.f;

	/** Which of ArmyUnits to produce next. */
	int32 NextArmyUnit = 0;

	int32 IssuedCommands = 0;

	int32 Waves = 0;

	FTimerHandle ThinkTimer;
};
