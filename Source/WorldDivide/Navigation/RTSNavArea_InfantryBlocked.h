#pragma once

#include "CoreMinimal.h"
#include "NavAreas/NavArea_Null.h"
#include "RTSNavArea_InfantryBlocked.generated.h"

/**
 * A hole in the infantry navmesh alone (#158, D4 of River Crossing's 05_PRODUCER_ACTIONS): tree trunks, which infantry walks
 * around and vehicles drive over. Every other agent class ignores the area, so its navmesh runs through as if it were not
 * there. Infantry is the first class in NavigationSystemV1 SupportedAgents (Config/DefaultEngine.ini), the narrowest.
 */
UCLASS(Config = Engine)
class WORLDDIVIDE_API URTSNavArea_InfantryBlocked : public UNavArea_Null
{
	GENERATED_BODY()

public:
	URTSNavArea_InfantryBlocked(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
