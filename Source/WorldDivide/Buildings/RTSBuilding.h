#pragma once

#include "CoreMinimal.h"
#include "Combat/RTSTargetable.h"
#include "GameFramework/Actor.h"
#include "RTSBuilding.generated.h"

class ARTSUnit;
class UBoxComponent;
class UStaticMeshComponent;
class URTSBuildingData;
class URTSHealthComponent;
class URTSWeaponComponent;

/**
 * A structure on the build grid. Stats come from URTSBuildingData. It blocks the navmesh from the moment it is placed,
 * constructs over BuildTime and stays inert, weapon included, until construction completes. Completion also spawns the
 * data's SpawnOnComplete unit, such as a depot's harvester, which sets off on a Harvest order.
 * Construction runs on the server; clients see the progress through replication.
 */
UCLASS()
class WORLDDIVIDE_API ARTSBuilding : public AActor, public IRTSTargetable
{
	GENERATED_BODY()

public:
	ARTSBuilding();

	/** Server only, between SpawnActorDeferred and FinishSpawning. The actor origin is the footprint centre on the ground. */
	void InitFromData(URTSBuildingData* InBuildingData, int32 InTeamId);

	/**
	 * Server only. Advances construction, slower while the owner is low on power; completing it arms the weapon, adds the
	 * building to the owner's power grid and spawns the SpawnOnComplete unit. Public so tests and builders can drive it.
	 */
	void UpdateConstruction(float DeltaSeconds);

	/** Server only. Arms the weapon once constructed, unless the data requires power and the owner is low on power. */
	void RefreshWeapon();

	URTSBuildingData* GetBuildingData() const { return BuildingData; }
	virtual int32 GetTeamId() const override { return TeamId; }
	virtual URTSHealthComponent* GetHealthComponent() const override { return HealthComponent; }
	URTSWeaponComponent* GetWeaponComponent() const { return WeaponComponent; }

	/** From clamped to the footprint rectangle. Under construction or not, the building can be shot. */
	virtual FVector GetClosestFootprintPoint(const FVector& From) const override;

	/** 0 when placed, 1 when complete. */
	float GetConstructionProgress() const { return ConstructionProgress; }
	bool IsConstructed() const { return ConstructionProgress >= 1.f; }

	/** Credits the owner paid for the building; 0 for one placed for free, such as by the map. */
	int32 GetPaidCost() const { return PaidCost; }

	/** Server only. Set right after a purchase, while the building is still in its initial replication. */
	void SetPaidCost(int32 InPaidCost);

	/**
	 * What cancelling returns now: the unbuilt share of PaidCost, rounded down, so nothing once constructed.
	 * A construction site that is destroyed refunds nothing.
	 */
	int32 GetCancelRefund() const;

	/**
	 * Server only. True from placement until the navmesh hole under the footprint is built. Meanwhile units whose path leads
	 * into the footprint wait at its edge, and route around it once the hole is there.
	 */
	bool IsNavCutPending() const { return bNavCutPending; }

	/**
	 * Server only. While set the building constructs itself each tick, as one from the debug build menu does; a builder unit
	 * clears it on the site it places and drives UpdateConstruction while it stands there.
	 */
	UPROPERTY(EditAnywhere, Category = "Construction")
	bool bAutoConstruct = true;

	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Destroyed() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_BuildingData();

	UFUNCTION()
	void OnRep_TeamId();

	UFUNCTION()
	void OnRep_ConstructionProgress();

	/** Server only. */
	UFUNCTION()
	void HandleDeath(AActor* Killer);

private:
	void ApplyBuildingData();
	void ApplyTeamColor();

	/** The mesh rises out of the ground as construction progresses. */
	void ApplyConstructionVisual();

	/** Server only. Spawns SpawnOnComplete just outside the footprint and sends a harvester off to work. */
	void SpawnCompletionUnit();

	/** Server only, while the navmesh hole is pending: holds units headed into the footprint, releases them once it is built. */
	void UpdateNavCut(float DeltaSeconds);

	void ReleaseHeldUnits();

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	/** Sized to the footprint. Blocks mouse traces for picking and cuts a hole in the navmesh. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UBoxComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<URTSHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<URTSWeaponComponent> WeaponComponent;

	UPROPERTY(ReplicatedUsing = OnRep_BuildingData)
	TObjectPtr<URTSBuildingData> BuildingData;

	UPROPERTY(ReplicatedUsing = OnRep_TeamId)
	int32 TeamId = 0;

	UPROPERTY(ReplicatedUsing = OnRep_ConstructionProgress)
	float ConstructionProgress = 0.f;

	UPROPERTY(Replicated)
	int32 PaidCost = 0;

	float MeshBaseOffset = 0.f;

	/** Server only. */
	bool bNavCutPending = false;
	float NavCutWaited = 0.f;
	TArray<TWeakObjectPtr<ARTSUnit>> HeldUnits;
};
