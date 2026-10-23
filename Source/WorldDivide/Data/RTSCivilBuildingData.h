#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RTSCivilBuildingData.generated.h"

class UStaticMesh;

/** What a civilian building shows (#408): it falls from Pristine to Damaged to Rubble as it loses health, and stays as rubble. */
UENUM(BlueprintType)
enum class ERTSCivilBuildingState : uint8
{
	Pristine,
	Damaged,
	Rubble,
};

/**
 * One place a garrisoned squad member fires from (#252): a window or a door, in the building's own frame (origin at the
 * ground in the middle of the footprint, X forward, Z up, cm).
 */
USTRUCT(BlueprintType)
struct FRTSGarrisonFiringPosition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garrison")
	FName Name;

	/** Where the shooter stands, just inside the opening, at eye height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garrison")
	FVector Location = FVector::ZeroVector;

	/** The way the opening looks out, in degrees about Z from the building's X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garrison")
	float Yaw = 0.f;

	/** The full horizontal arc the shooter covers through the opening, centred on Yaw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garrison", meta = (ClampMin = "0", ClampMax = "180"))
	float ArcDegrees = 90.f;
};

/**
 * A civilian building of a map (#408): a village house, a barn or the bridge. Nobody builds or owns it; it stands where the
 * map puts it (ARTSCivilBuilding), can be shot and falls to rubble, and an infantry squad can garrison it (#252) through
 * the garrison fields. A building without capacity, such as the bridge, cannot be garrisoned.
 */
UCLASS(BlueprintType)
class WORLDDIVIDE_API URTSCivilBuildingData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** The mesh the state shows: a missing damaged or rubble mesh falls back to the state before. */
	UFUNCTION(BlueprintCallable, Category = "Civil Building")
	UStaticMesh* GetMesh(ERTSCivilBuildingState State) const;

	bool CanGarrison() const { return GarrisonCapacity > 0; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	TObjectPtr<UStaticMesh> PristineMesh;

	/** None shows the pristine mesh while damaged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	TObjectPtr<UStaticMesh> DamagedMesh;

	/** What is left once its health is gone; it keeps its collision and stays. None shows the damaged mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	TObjectPtr<UStaticMesh> RubbleMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats", meta = (ClampMin = "1"))
	float MaxHealth = 1000.f;

	/** At or below this share of its health the building shows Damaged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats", meta = (ClampMin = "0", ClampMax = "1"))
	float DamagedHealthFraction = 0.5f;

	/** How many infantry fit inside; 0 cannot be garrisoned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garrison", meta = (ClampMin = "0"))
	int32 GarrisonCapacity = 0;

	/** Where a squad walks to enter and where it leaves, outside the main door, in the building's frame (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garrison")
	FVector EntryPoint = FVector::ZeroVector;

	/** The windows and doors the garrison fires from; at least as many as the capacity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Garrison")
	TArray<FRTSGarrisonFiringPosition> FiringPositions;
};
