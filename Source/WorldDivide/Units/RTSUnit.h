#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Combat/RTSTargetable.h"
#include "Commands/RTSCommand.h"
#include "RTSUnit.generated.h"

class AAIController;
class UCapsuleComponent;
class UStaticMeshComponent;
class UFloatingPawnMovement;
class URTSBuilderComponent;
class URTSHarvesterComponent;
class URTSHealthComponent;
class URTSWeaponComponent;
class URTSUnitData;
struct FPathFollowingResult;

/**
 * A light pawn (no ACharacter) so hundreds of units stay cheap. Stats come from URTSUnitData.
 * Orders are executed on the server only; clients see the result through replication.
 */
UCLASS()
class WORLDDIVIDE_API ARTSUnit : public APawn, public IRTSTargetable
{
	GENERATED_BODY()

public:
	ARTSUnit();

	/** Server only, between SpawnActorDeferred and FinishSpawning. */
	void InitFromData(URTSUnitData* InUnitData, int32 InTeamId);

	/** Server only. Sets weapon damage and maximum health from the data and the upgrades the owner has researched. */
	void ApplyUpgrades();

	/**
	 * Server only. Replaces the order queue, or appends to it when Command.bQueue is set.
	 * A Harvest for a unit that cannot harvest becomes a move up to the source, as a Build naming an own construction site
	 * becomes a move up to the site for a unit that cannot build. A builder's new building is paid here, queued or not, and
	 * refused when the player cannot afford it; it is refunded if the order ends before the building is placed.
	 */
	void ExecuteCommand(const FRTSCommand& InCommand);

	/**
	 * Server only. Moves to Location ahead of the current order, then picks the queue up again where it was: a builder resumes
	 * the site it already placed. Used to clear a building site, not a player order.
	 */
	void StepAside(const FVector& Location);

	/**
	 * Server only. Pauses the current move when the next stretch of its path leads into Footprint, a new building whose
	 * navmesh hole is not built yet, so the unit waits at its edge. Returns true when it paused.
	 */
	bool HoldShortOf(const FBox& Footprint);

	/** Server only. True while HoldShortOf keeps the move paused; any new move ends the hold. */
	bool IsMoveHeld() const;

	/** Server only. Ends a hold: the current order paths again, now around the building. */
	void ReleaseHold();

	/** Server only. Called by the AI controller whenever a path-following request ends. */
	void NotifyMoveFinished(const FPathFollowingResult& Result);

	URTSUnitData* GetUnitData() const { return UnitData; }
	virtual int32 GetTeamId() const override { return TeamId; }
	virtual URTSHealthComponent* GetHealthComponent() const override { return HealthComponent; }
	URTSWeaponComponent* GetWeaponComponent() const { return WeaponComponent; }
	URTSHarvesterComponent* GetHarvesterComponent() const { return HarvesterComponent; }
	URTSBuilderComponent* GetBuilderComponent() const { return BuilderComponent; }
	float GetSelectionRadius() const;
	int32 GetQueuedCommandCount() const { return CommandQueue.Num(); }

	/** The edge of the selection circle facing From. */
	virtual FVector GetClosestFootprintPoint(const FVector& From) const override;

	/** Every machine. Jolts the mesh back for a moment, as a shot recoils; cosmetic only. */
	void PlayFireKick(float EffectScale);

	/** Distance from the actor origin down to the ground contact point. */
	float GetGroundOffset() const;

	static FLinearColor GetTeamColor(int32 InTeamId);

	/** Material vector parameter that receives the team colour, on units and buildings alike. */
	static const FName TeamColorParameter;

	/** Feet, not eye height: navmesh queries start from the ground. */
	virtual FVector GetNavAgentLocation() const override;

	virtual void PostInitializeComponents() override;
	virtual void Destroyed() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_UnitData();

	UFUNCTION()
	void OnRep_TeamId();

	/** Server only. */
	UFUNCTION()
	void HandleDeath(AActor* Killer);

private:
	void ApplyUnitData();
	void ApplyTeamColor();
	void StartNextCommand();

	/** Empties the order queue, refunding what its orders still hold. */
	void ClearCommands();

	/** Drops the front order, refunding what it still holds. */
	void PopCommand();

	/**
	 * Returns false when the command is already done or invalid and should be dropped. A Build hands its payment to the builder
	 * component.
	 */
	bool StartCommand(AAIController& AI, FRTSCommand& Command);

	/**
	 * Paths to Location, stopping at the edge on the unit's side when it lies inside a wall or a building. Returns true while
	 * the unit moves; false when it cannot path there or already stands at the goal.
	 */
	bool RequestMove(AAIController& AI, const FVector& Location);

	/**
	 * Each tick while an Attack is the current order: drop it once the target is gone, otherwise close in or hold.
	 * Units are followed; buildings stand in their own navmesh hole, so the unit walks to a reachable spot outside the
	 * footprint, facing side first, and drops the order when there is none in range.
	 */
	void UpdateAttackOrder();

	/**
	 * Each tick while an AttackMove is the current order: stand and fight while the weapon has an enemy in range, otherwise
	 * walk on to the target, and end the order once the unit stands there.
	 */
	void UpdateAttackMoveOrder();

	/** Each tick while a Harvest is the current order: the harvester component runs it and says when it is over. */
	void UpdateHarvestOrder(float DeltaSeconds);

	/** Each tick while a Build is the current order: the builder component runs it and says when it is over. */
	void UpdateBuildOrder(float DeltaSeconds);

	/**
	 * Points the weapon at what the current order allows: the attack target, anything in range when idle or attack-moving,
	 * nothing while moving otherwise.
	 */
	void ApplyWeaponMode();

	void FaceTowardYaw(float Yaw, float DeltaSeconds);

	/** Eases the mesh back from a fire kick. */
	void UpdateFireKick(float DeltaSeconds);

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UCapsuleComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UFloatingPawnMovement> Movement;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<URTSHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<URTSWeaponComponent> WeaponComponent;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<URTSHarvesterComponent> HarvesterComponent;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<URTSBuilderComponent> BuilderComponent;

	UPROPERTY(ReplicatedUsing = OnRep_UnitData)
	TObjectPtr<URTSUnitData> UnitData;

	UPROPERTY(ReplicatedUsing = OnRep_TeamId)
	int32 TeamId = 0;

	/** Server only. Front entry is the order being executed. A UPROPERTY so GC sees and clears attack targets. */
	UPROPERTY(Transient)
	TArray<FRTSCommand> CommandQueue;

	bool bRequestingMove = false;
	bool bMoveFinishedDuringRequest = false;

	/** Set while an attack-move stops to fight, so the aborted move does not end the order. */
	bool bStoppingToFight = false;

	/** Where ApplyUnitData stood the mesh; a fire kick moves it back from here and returns it. */
	FVector MeshRestLocation = FVector::ZeroVector;
	float FireKickDistance = 0.f;
	float FireKickTimeLeft = 0.f;
};
