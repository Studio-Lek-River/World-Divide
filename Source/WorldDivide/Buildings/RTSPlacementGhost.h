#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RTSPlacementGhost.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class URTSBuildingData;

/** Local-only preview of a building under the cursor, tinted by placement validity. Never replicates or collides. */
UCLASS(NotPlaceable)
class WORLDDIVIDE_API ARTSPlacementGhost : public AActor
{
	GENERATED_BODY()

public:
	ARTSPlacementGhost();

	/** Shows Data's mesh with Material (needs a "Color" parameter); without one the mesh's own material is tinted. */
	void SetBuildingData(const URTSBuildingData& Data, UMaterialInterface* Material);

	void SetValid(bool bValid);

private:
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MaterialInstance;
};
