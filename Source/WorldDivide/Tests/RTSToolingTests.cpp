#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using RTSTests::TestFlags;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMCPPluginsOffHeadlessTest, "WorldDivide.Tooling.MCPPluginsOffHeadless", TestFlags)

bool FRTSMCPPluginsOffHeadlessTest::RunTest(const FString& Parameters)
{
	// Only the headless ue.ps1 runs leave the MCP plugins out; an editor opened with ue.ps1 -Task editor has them.
	if (!FApp::IsUnattended())
	{
		AddInfo(TEXT("Not an unattended run: the MCP plugins may be loaded here, nothing to check."));
		return true;
	}

	for (const TCHAR* Name : { TEXT("ModelContextProtocol"), TEXT("EditorToolset"), TEXT("SlateInspectorToolset"), TEXT("WorldDivideMCP") })
	{
		TestFalse(FString::Printf(TEXT("The headless run does not load %s"), Name), IPluginManager::Get().FindEnabledPlugin(Name).IsValid());
	}
	return true;
}

#endif
