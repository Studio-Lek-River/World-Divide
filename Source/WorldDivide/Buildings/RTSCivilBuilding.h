#pragma once

#include "CoreMinimal.h"
#include "Combat/RTSTargetable.h"
#include "Data/RTSCivilBuildingData.h"
#include "GameFramework/Actor.h"
#include "RTSCivilBuilding.generated.h"

class UStaticMeshComponent;
class URTSHealthComponent;

/**
 * A civilian building placed on a map (#408): a village house, a barn, the church or a bridge span, described by its
 * URTSCivilBuildingData. It belongs to no team, so nothing fires at it by itself and a right-click on it is no attack;
 * a force-fire order shoots it. Its mesh is its collision and blocks the navmesh. As it loses health it shows Damaged and
 * then Rubble, which keeps standing for the rest of the match (DESIGN_FOUNDATION section 6); blasts damage it too (#433).
 * The server works the state out from its health and replicates it; every machine shows the state's mesh, and the navmesh
 * follows its collision. A bridge span's rubble leaves a gap (#444): the ground units it held fall with it and die. The
 * garrison (#252) reads the data.
 */
UCLASS()
class WORLDDIVIDE_API ARTSCivilBuilding : public AActor, public IRTSTargetable
{
	GENERATED_BODY()

public:
	ARTSCivilBuilding();

	/** The state at HealthFraction: Rubble at no health, Damaged at or below DamagedFraction, else Pristine. */
	static ERTSCivilBuildingState ResolveState(float HealthFraction, float DamagedFraction);

	URTSCivilBuildingData* GetData() const { return Data; }
	ERTSCivilBuildingState GetState() const { return State; }

	/** The firing positions its current state still has. */
	TArray<FRTSGarrisonFiringPosition> GetFiringPositions() const
	{
		return Data ? Data->GetFiringPositions(State) : TArray<FRTSGarrisonFiringPosition>();
	}

	//~ IRTSTargetable
	virtual int32 GetTeamId() const override { return INDEX_NONE; }
	virtual URTSHealthComponent* GetHealthComponent() const override { return HealthComponent; }
	virtual FVector GetClosestFootprintPoint(const FVector& From) const override;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Civil Building")
	TObjectPtr<URTSCivilBuildingData> Data;

	/** The state a map starts it in: an already damaged house or a ruin. The server sets its health to match. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Civil Building")
	ERTSCivilBuildingState StartState = ERTSCivilBuildingState::Pristine;

private:
	UFUNCTION()
	void HandleDamaged(float Amount, AActor* DamageInstigator);

	UFUNCTION()
	void OnRep_State();

	/** Server only. Works the state out from the health and replicates it when it changed. */
	void UpdateState(AActor* DamageInstigator = nullptr);

	/**
	 * Server only, once its rubble is shown: kills the ground units within Bounds, its collision before, that no longer
	 * stand on it or on other ground, with DamageInstigator as their killer.
	 */
	void KillUnitsWithoutGround(const FBox& Bounds, AActor* DamageInstigator);

	void ShowState();

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<URTSHealthComponent> HealthComponent;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	ERTSCivilBuildingState State = ERTSCivilBuildingState::Pristine;
};
