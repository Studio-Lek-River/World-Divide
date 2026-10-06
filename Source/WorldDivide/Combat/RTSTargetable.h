#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "RTSTargetable.generated.h"

class URTSHealthComponent;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class URTSTargetable : public UInterface
{
	GENERATED_BODY()
};

/** Anything weapons can shoot: units and buildings alike. Weapons and the Attack order only talk to targets through this. */
class WORLDDIVIDE_API IRTSTargetable
{
	GENERATED_BODY()

public:
	/** Targets of any other team are enemies. */
	virtual int32 GetTeamId() const = 0;

	virtual URTSHealthComponent* GetHealthComponent() const = 0;

	/** The point of the footprint nearest to From, at the target's height. Weapon range is measured to it. */
	virtual FVector GetClosestFootprintPoint(const FVector& From) const = 0;
};
