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

	/**
	 * Server only. Replaces the order queue, or appends to it when Command.bQueue is set.
	 * A Harvest for a unit that cannot harvest becomes a move up to the source.
	 */
	void ExecuteCommand(const FRTSCommand& InCommand);

	/** Server only. Called by the AI controller whenever a path-following request ends. */
	void NotifyMoveFinished(const FPathFollowingResult& Result);

	URTSUnitData* GetUnitData() const { return UnitData; }
	virtual int32 GetTeamId() const override { return TeamId; }
	virtual URTSHealthComponent* GetHealthComponent() const override { return HealthComponent; }
	URTSWeaponComponent* GetWeaponComponent() const { return WeaponComponent; }
	URTSHarvesterComponent* GetHarvesterComponent() const { return HarvesterComponent; }
	float GetSelectionRadius() const;
	int32 GetQueuedCommandCount() const { return CommandQueue.Num(); }

	/** The edge of the selection circle facing From. */
	virtual FVector GetClosestFootprintPoint(const FVector& From) const override;

	/** Distance from the actor origin down to the ground contact point. */
	float GetGroundOffset() const;

	static FLinearColor GetTeamColor(int32 InTeamId);

	/** Material vector parameter that receives the team colour, on units and buildings alike. */
	static const FName TeamColorParameter;

	/** Feet, not eye height: navmesh queries start from the ground. */
	virtual FVector GetNavAgentLocation() const override;

	virtual void PostInitializeComponents() override;
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

	/** Returns false when the command is already done or invalid and should be dropped. */
	bool StartCommand(AAIController& AI, const FRTSCommand& Command);

	/**
	 * Each tick while an Attack is the current order: drop it once the target is gone, otherwise close in or hold.
	 * Units are followed; buildings stand in their own navmesh hole, so the unit walks to a spot outside the nearest edge.
	 */
	void UpdateAttackOrder();

	/** Each tick while a Harvest is the current order: the harvester component runs it and says when it is over. */
	void UpdateHarvestOrder(float DeltaSeconds);

	/** Points the weapon at what the current order allows: the attack target, anything in range when idle, nothing while moving. */
	void ApplyWeaponMode();

	void FaceTowardYaw(float Yaw, float DeltaSeconds);

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

	UPROPERTY(ReplicatedUsing = OnRep_UnitData)
	TObjectPtr<URTSUnitData> UnitData;

	UPROPERTY(ReplicatedUsing = OnRep_TeamId)
	int32 TeamId = 0;

	/** Server only. Front entry is the order being executed. A UPROPERTY so GC sees and clears attack targets. */
	UPROPERTY(Transient)
	TArray<FRTSCommand> CommandQueue;

	bool bRequestingMove = false;
	bool bMoveFinishedDuringRequest = false;
};
