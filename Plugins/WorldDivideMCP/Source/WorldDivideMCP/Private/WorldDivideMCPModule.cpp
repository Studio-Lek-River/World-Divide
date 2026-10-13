#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

#include "RTSConsoleToolset.h"

class FWorldDivideMCPModule : public IModuleInterface
{
	virtual void StartupModule() override
	{
		UToolsetRegistry::RegisterToolsetClass(URTSConsoleToolset::StaticClass());
	}

	virtual void ShutdownModule() override
	{
		UToolsetRegistry::UnregisterToolsetClass(URTSConsoleToolset::StaticClass());
	}
};

IMPLEMENT_MODULE(FWorldDivideMCPModule, WorldDivideMCP)
