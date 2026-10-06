#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RTSSupplySource.generated.h"

class UBoxComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * A map-placed pile of supplies that harvesters load from until it runs dry. Like a building it stands on the 100 cm
 * grid and cuts a hole in the navmesh; the mesh sinks as the supplies run out. Only the server takes from it.
 */
UCLASS()
class WORLDDIVIDE_API ARTSSupplySource : public AActor
{
	GENERATED_BODY()

public:
	ARTSSupplySource();

	/** Server only. Removes up to Wanted supplies and returns how many were taken. */
	int32 Take(int32 Wanted);

	int32 GetRemaining() const { return Remaining; }
	bool IsDepleted() const { return Remaining <= 0; }

	/** From clamped to the footprint rectangle. */
	FVector GetClosestFootprintPoint(const FVector& From) const;

	/** The source with supplies left whose footprint edge is nearest From and within MaxDistance, or null. */
	static ARTSSupplySource* FindNearest(const UWorld& World, const FVector& From, float MaxDistance);

	/** Supplies at the start of the match. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Supply", meta = (ClampMin = "0"))
	int32 Amount = 5000;

	/** Size in 100 cm grid cells along X and Y. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Supply", meta = (ClampMin = "1"))
	FIntPoint Footprint = FIntPoint(3, 3);

	/** Stretched to fill the footprint up to Height while full. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Display")
	TObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Display", meta = (ClampMin = "10"))
	float Height = 150.f;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_Remaining();

private:
	void ApplyFootprint();

	/** The mesh height follows the supplies left; an empty source keeps a flat stub. */
	void ApplyRemainingVisual();

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	/** Sized to the footprint. Blocks mouse traces for picking, building placement, and cuts a hole in the navmesh. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UBoxComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(ReplicatedUsing = OnRep_Remaining)
	int32 Remaining = 0;
};
