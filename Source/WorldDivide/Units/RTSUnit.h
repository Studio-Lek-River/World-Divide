#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Commands/RTSCommand.h"
#include "RTSUnit.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;
class UFloatingPawnMovement;
class URTSUnitData;
struct FPathFollowingResult;

/**
 * A light pawn (no ACharacter) so hundreds of units stay cheap. Stats come from URTSUnitData.
 * Orders are executed on the server only; clients see the result through replication.
 */
UCLASS()
class WORLDDIVIDE_API ARTSUnit : public APawn
{
	GENERATED_BODY()

public:
	ARTSUnit();

	/** Server only, between SpawnActorDeferred and FinishSpawning. */
	void InitFromData(URTSUnitData* InUnitData, int32 InTeamId);

	/** Server only. Replaces the order queue, or appends to it when Command.bQueue is set. */
	void ExecuteCommand(const FRTSCommand& Command);

	/** Server only. Called by the AI controller whenever a path-following request ends. */
	void NotifyMoveFinished(const FPathFollowingResult& Result);

	URTSUnitData* GetUnitData() const { return UnitData; }
	int32 GetTeamId() const { return TeamId; }
	float GetHealth() const { return Health; }
	float GetMaxHealth() const;
	float GetSelectionRadius() const;
	int32 GetQueuedCommandCount() const { return CommandQueue.Num(); }

	/** Distance from the actor origin down to the ground contact point. */
	float GetGroundOffset() const;

	static FLinearColor GetTeamColor(int32 InTeamId);

	/** Feet, not eye height: navmesh queries start from the ground. */
	virtual FVector GetNavAgentLocation() const override;

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_UnitData();

	UFUNCTION()
	void OnRep_TeamId();

private:
	void ApplyUnitData();
	void ApplyTeamColor();
	void StartNextCommand();

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UCapsuleComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UFloatingPawnMovement> Movement;

	UPROPERTY(ReplicatedUsing = OnRep_UnitData)
	TObjectPtr<URTSUnitData> UnitData;

	UPROPERTY(ReplicatedUsing = OnRep_TeamId)
	int32 TeamId = 0;

	UPROPERTY(Replicated)
	float Health = 0.f;

	/** Server only. Front entry is the order being executed. */
	TArray<FRTSCommand> CommandQueue;

	bool bRequestingMove = false;
	bool bMoveFinishedDuringRequest = false;
};
