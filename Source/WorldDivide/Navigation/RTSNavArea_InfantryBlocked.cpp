#include "Navigation/RTSNavArea_InfantryBlocked.h"

URTSNavArea_InfantryBlocked::URTSNavArea_InfantryBlocked(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedAgents.Empty();
	SupportedAgents.Set(0);
	SupportedAgents.MarkInitialized();
}
