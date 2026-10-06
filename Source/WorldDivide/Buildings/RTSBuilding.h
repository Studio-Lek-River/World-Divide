#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RTSBuilding.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class URTSBuildingData;
class URTSHealthComponent;
class URTSWeaponComponent;

/**
 * A structure on the build grid. Stats come from URTSBuildingData. It blocks the navmesh from the moment it is placed,
 * constructs over BuildTime and stays inert, weapon included, until construction completes.
 * Construction runs on the server; clients see the progress through replication.
 */
UCLASS()
class WORLDDIVIDE_API ARTSBuilding : public AActor
{
	GENERATED_BODY()

public:
	ARTSBuilding();

	/** Server only, between SpawnActorDeferred and FinishSpawning. The actor origin is the footprint centre on the ground. */
	void InitFromData(URTSBuildingData* InBuildingData, int32 InTeamId);

	/** Server only. Advances construction; completing it arms the weapon. Public so tests and builders can drive it. */
	void UpdateConstruction(float DeltaSeconds);

	URTSBuildingData* GetBuildingData() const { return BuildingData; }
	int32 GetTeamId() const { return TeamId; }
	URTSHealthComponent* GetHealthComponent() const { return HealthComponent; }
	URTSWeaponComponent* GetWeaponComponent() const { return WeaponComponent; }

	/** 0 when placed, 1 when complete. */
	float GetConstructionProgress() const { return ConstructionProgress; }
	bool IsConstructed() const { return ConstructionProgress >= 1.f; }

	/** Server only. While set the building constructs itself each tick; a builder unit will clear it and drive UpdateConstruction. */
	UPROPERTY(EditAnywhere, Category = "Construction")
	bool bAutoConstruct = true;

	virtual void PostInitializeComponents() override;
	virtual void Tick(float DeltaSeconds) override;
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

	float MeshBaseOffset = 0.f;
};
