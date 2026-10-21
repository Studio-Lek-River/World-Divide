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
class URTSProductionComponent;
class URTSSuperweaponComponent;
class URTSUnitData;
class URTSWeaponComponent;
struct FNavAgentProperties;

/**
 * A structure on the build grid. Stats come from URTSBuildingData. It blocks the navmesh from the moment it is placed,
 * constructs over BuildTime and stays inert, weapon and production included, until construction completes. Completion also
 * spawns the data's SpawnOnComplete unit, such as a depot's harvester, which sets off on a Harvest order.
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

	/** Server only. Sets weapon damage and maximum health from the data and the upgrades the owner has researched. */
	void ApplyUpgrades();

	/**
	 * Server only. Advances construction, slower while the owner is low on power; completing it arms the weapon, adds the
	 * building to the owner's power grid, spawns the SpawnOnComplete unit, puts the rally point in front of the exit and,
	 * for a superweapon, starts its charge and warns the opponents.
	 * Public so tests and builders can drive it.
	 */
	void UpdateConstruction(float DeltaSeconds);

	/** Server only. Arms the weapon once constructed, unless the data requires power and the owner is low on power. */
	void RefreshWeapon();

	/**
	 * Server only. Fire at Target, of any team, until it dies or leaves range; when it is null, at the ground at Location until
	 * StopFiring. Refused, returning false, unless the building is constructed, its weapon can force-fire there
	 * (URTSWeaponComponent::CanForceFireAt) and the target or spot is already in range: a building cannot close in.
	 */
	bool ForceFire(AActor* Target, const FVector& Location);

	/** Server only. Drops a force-fire order; the weapon engages enemies in range on its own again. */
	void StopFiring();

	URTSBuildingData* GetBuildingData() const { return BuildingData; }
	virtual int32 GetTeamId() const override { return TeamId; }
	virtual URTSHealthComponent* GetHealthComponent() const override { return HealthComponent; }
	URTSWeaponComponent* GetWeaponComponent() const { return WeaponComponent; }
	URTSProductionComponent* GetProductionComponent() const { return ProductionComponent; }
	URTSSuperweaponComponent* GetSuperweaponComponent() const { return SuperweaponComponent; }

	/**
	 * Where units leave the building: outside its front edge (the data's +X edge, turned with the building), Clearance beyond
	 * it, nudged onto Agent's navmesh, or the narrowest agent's without one, when it exists.
	 */
	FVector GetExitLocation(float Clearance, const FNavAgentProperties* Agent = nullptr) const;

	/** Server only. Spawns a unit of Data for the building's team at the exit. */
	ARTSUnit* SpawnUnitAtExit(URTSUnitData* Data);

	/** Aircraft pads, from the data; 0 for a building that is not an airfield. */
	int32 GetPadCount() const;

	/** Where an aircraft stands on pad Index: on the roof, pads side by side across the footprint along the data's Y. */
	FVector GetPadLocation(int32 Index) const;

	/** The aircraft bound to pad Index, or null while it is free. */
	ARTSUnit* GetPadAircraft(int32 Index) const;

	/** Pads neither bound to an aircraft nor kept for an aircraft in the production queue. */
	int32 GetFreePadCount() const;

	/**
	 * Server only. Binds Aircraft to a free pad and returns its index, or the pad it already has here; INDEX_NONE when none is
	 * free.
	 */
	int32 ClaimPad(ARTSUnit& Aircraft);

	/** Server only. Frees Aircraft's pad here, if it has one. */
	void ReleasePad(const ARTSUnit& Aircraft);

	/** The data's footprint in world cells, turned with the building (RTSBuildPlacement::GetRotatedFootprint). */
	FIntPoint GetFootprint() const;

	/** From clamped to the footprint rectangle. Under construction or not, the building can be shot. */
	virtual FVector GetClosestFootprintPoint(const FVector& From) const override;

	/** The meshes drawn with Mesh for a building of several (URTSBuildingData::Parts), in the data's order. */
	const TArray<TObjectPtr<UStaticMeshComponent>>& GetPartComponents() const { return PartComponents; }

	/**
	 * The boxes that block units and cut the navmesh: the footprint box alone, or the data's CollisionBoxes, the first of them
	 * in that box.
	 */
	TArray<UBoxComponent*> GetCollisionComponents() const;

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

	/** Server only. Warns the team's players unless the instigator is on the building's own team. */
	UFUNCTION()
	void HandleDamaged(float Amount, AActor* DamageInstigator);

	/** Server only. */
	UFUNCTION()
	void HandleDeath(AActor* Killer);

private:
	void ApplyBuildingData();
	void ApplyTeamColor();

	/** Makes the data's Parts, replacing any made before, attached to Mesh so they rise and scale with it. */
	void ApplyParts();

	/** Sizes the collision: one box over the footprint up to Height, or the data's CollisionBoxes. */
	void ApplyCollision(float Height);

	/** The mesh rises out of the ground as construction progresses. */
	void ApplyConstructionVisual();

	/** Server only. Spawns SpawnOnComplete at the exit and sends a harvester off to work. */
	void SpawnCompletionUnit();

	/** Server only, while the navmesh hole is pending: holds units headed into the footprint, releases them once it is built. */
	void UpdateNavCut(float DeltaSeconds);

	void ReleaseHeldUnits();

	/** Server only, each tick once constructed: drops a force-fired target that is gone or, while armed, out of range. */
	void UpdateForcedFire();

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	/** Sized to the footprint. Blocks mouse traces for picking and cuts a hole in the navmesh. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UBoxComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** The data's Parts, on Mesh. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> PartComponents;

	/** The data's CollisionBoxes after the first, which Collision takes. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBoxComponent>> ExtraCollision;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<URTSHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<URTSWeaponComponent> WeaponComponent;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<URTSProductionComponent> ProductionComponent;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<URTSSuperweaponComponent> SuperweaponComponent;

	UPROPERTY(ReplicatedUsing = OnRep_BuildingData)
	TObjectPtr<URTSBuildingData> BuildingData;

	UPROPERTY(ReplicatedUsing = OnRep_TeamId)
	int32 TeamId = 0;

	UPROPERTY(ReplicatedUsing = OnRep_ConstructionProgress)
	float ConstructionProgress = 0.f;

	UPROPERTY(Replicated)
	int32 PaidCost = 0;

	float MeshBaseOffset = 0.f;

	/** Where UpdateNavCut looks for the navmesh hole, in the actor's frame: inside a collision box. */
	FVector NavProbeOffset = FVector::ZeroVector;

	/** Server only. */
	bool bNavCutPending = false;
	float NavCutWaited = 0.f;
	TArray<TWeakObjectPtr<ARTSUnit>> HeldUnits;

	/** Server only. The actor a force-fire order is at; unset for none or an order at the ground. */
	TWeakObjectPtr<AActor> ForcedTarget;

	/** Server only. The aircraft bound to each pad; one destroyed frees its pad by itself. */
	TArray<TWeakObjectPtr<ARTSUnit>> Pads;
};
