#pragma once

#include "CoreMinimal.h"
#include "AI/Navigation/NavRelevantInterface.h"
#include "Components/ActorComponent.h"
#include "RTSTrunkObstaclesComponent.generated.h"

/**
 * The trunks of a map's trees in the navmesh (#158, D4 of River Crossing's 05_PRODUCER_ACTIONS): a cylinder of
 * URTSNavArea_InfantryBlocked at each, so infantry walks around every trunk and vehicles drive over it. The trees
 * themselves have no collision and no navigation influence; an instanced mesh exports a navigation modifier for the whole
 * component, not one per instance, hence this component. Recast does not widen a modifier by the agent's radius: a path
 * may brush a trunk, never cross it.
 */
UCLASS(ClassGroup = Navigation)
class WORLDDIVIDE_API URTSTrunkObstaclesComponent : public UActorComponent, public INavRelevantInterface
{
	GENERATED_BODY()

public:
	URTSTrunkObstaclesComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Replaces the trunks with one at the foot of each of Feet (world) and updates the navigation data. */
	void SetTrunks(const TArray<FVector>& Feet);

	const TArray<FVector>& GetTrunks() const { return Trunks; }

	//~ Begin UActorComponent Interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	//~ End UActorComponent Interface

	//~ Begin INavRelevantInterface
	virtual void GetNavigationData(FNavigationRelevantData& Data) const override;
	virtual FBox GetNavigationBounds() const override;
	virtual bool IsNavigationRelevant() const override;
	//~ End INavRelevantInterface

	/** A trunk's footprint radius, cm: a mature alder or spruce at chest height. */
	UPROPERTY(EditAnywhere, Category = "Navigation")
	float Radius = 25.f;

	/** From below the ground at the foot (Sink) to above a soldier's head, cm. */
	UPROPERTY(EditAnywhere, Category = "Navigation")
	float Height = 400.f;

	UPROPERTY(EditAnywhere, Category = "Navigation")
	float Sink = 100.f;

private:
	/** World locations of the trunks' feet. */
	UPROPERTY()
	TArray<FVector> Trunks;
};
