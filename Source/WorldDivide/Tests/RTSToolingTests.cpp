#include "Interfaces/IPluginManager.h"
#include "Internationalization/Regex.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
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

namespace
{
	// The ToolsetRegistry's rule: a /slash-wrapped/ pattern is a regex, anything else a case-insensitive substring.
	bool MatchesAnyNamePattern(const FString& Name, const TArray<FString>& Patterns)
	{
		for (const FString& Pattern : Patterns)
		{
			const bool bRegex = Pattern.Len() > 2 && Pattern.StartsWith(TEXT("/")) && Pattern.EndsWith(TEXT("/"));
			FRegexMatcher Matcher(FRegexPattern(bRegex ? Pattern.Mid(1, Pattern.Len() - 2) : TEXT("(?i)\\Q") + Pattern + TEXT("\\E")), Name);
			if (Matcher.FindNext())
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMCPWriteToolsHiddenTest, "WorldDivide.Tooling.MCPWriteToolsHidden", TestFlags)

bool FRTSMCPWriteToolsHiddenTest::RunTest(const FString& Parameters)
{
	TArray<FString> Allowed;
	TArray<FString> Blocked;
	GConfig->GetArray(TEXT("/Script/ToolsetRegistry.ToolsetRegistrySettings"), TEXT("AllowedNames"), Allowed, GEditorPerProjectIni);
	GConfig->GetArray(TEXT("/Script/ToolsetRegistry.ToolsetRegistrySettings"), TEXT("BlockedNames"), Blocked, GEditorPerProjectIni);
	if (!TestTrue(TEXT("The MCP toolsets are limited by an allow-list"), Allowed.Num() > 0))
	{
		return false;
	}
	auto IsVisible = [&](const FString& Name) { return !MatchesAnyNamePattern(Name, Blocked) && MatchesAnyNamePattern(Name, Allowed); };

	const TCHAR* Kept[] = {
		TEXT("WorldDivideMCP.RTSConsoleToolset"), TEXT("WorldDivideMCP.RTSConsoleToolset.ExecuteConsoleCommand"),
		TEXT("EditorToolset.LogsToolset.GetLogEntries"),
		TEXT("EditorToolset.EditorAppToolset.StartPIE"), TEXT("EditorToolset.EditorAppToolset.StopPIE"),
		TEXT("EditorToolset.EditorAppToolset.CaptureViewport"),
		TEXT("SlateInspectorToolset.SlateInspectorToolset.Snapshot"), TEXT("SlateInspectorToolset.SlateInspectorToolset.Screenshot"),
		TEXT("editor_toolset.toolsets.scene.SceneTools.find_actors"),
		TEXT("editor_toolset.toolsets.actor.ActorTools.get_actor_transform"),
		TEXT("editor_toolset.toolsets.object.ObjectTools.get_properties"),
	};
	for (const TCHAR* Name : Kept)
	{
		TestTrue(FString::Printf(TEXT("%s stays visible"), Name), IsVisible(Name));
	}

	const TCHAR* Hidden[] = {
		TEXT("editor_toolset.toolsets.asset.AssetTools"), TEXT("editor_toolset.toolsets.blueprint.BlueprintTools"),
		TEXT("editor_toolset.toolsets.material.MaterialTools"), TEXT("editor_toolset.toolsets.data_asset.DataAssetTools"),
		TEXT("editor_toolset.toolsets.data_table.DataTableTools"), TEXT("editor_toolset.toolsets.static_mesh.StaticMeshTools"),
		TEXT("editor_toolset.toolsets.texture.TextureTools"), TEXT("editor_toolset.toolsets.primitive.PrimitiveTools"),
		TEXT("editor_toolset.toolsets.programmatic.ProgrammaticToolset"), TEXT("ToolsetRegistry.AgentSkillToolset"),
		TEXT("editor_toolset.toolsets.scene.SceneTools.save_actor"), TEXT("editor_toolset.toolsets.scene.SceneTools.remove_from_scene"),
		TEXT("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_class"),
		TEXT("editor_toolset.toolsets.actor.ActorTools.set_actor_transform"), TEXT("editor_toolset.toolsets.actor.ActorTools.add_component"),
		TEXT("editor_toolset.toolsets.object.ObjectTools.set_properties"),
		TEXT("EditorToolset.EditorAppToolset.OpenEditorForAsset"),
		TEXT("SlateInspectorToolset.SlateInspectorToolset.Click"), TEXT("SlateInspectorToolset.SlateInspectorToolset.PressKey"),
		TEXT("SlateInspectorToolset.SlateInspectorToolset.Type"),
	};
	for (const TCHAR* Name : Hidden)
	{
		TestFalse(FString::Printf(TEXT("%s is hidden"), Name), IsVisible(Name));
	}

	TArray<FString> BlockedCommands;
	GConfig->GetArray(TEXT("WorldDivideMCP"), TEXT("BlockedConsoleCommands"), BlockedCommands, GEditorPerProjectIni);
	auto IsCommandBlocked = [&](const FString& Command)
	{
		for (const FString& Pattern : BlockedCommands)
		{
			FRegexMatcher Matcher(FRegexPattern(Pattern, ERegexPatternFlags::CaseInsensitive), Command);
			if (Matcher.FindNext())
			{
				return true;
			}
		}
		return false;
	};
	for (const TCHAR* Command : { TEXT("py print(1)"), TEXT("Python unreal.EditorAssetLibrary.save_asset('/Game/X')"), TEXT("obj savepackage package=/Game/X"), TEXT("SaveAll") })
	{
		TestTrue(FString::Printf(TEXT("'%s' is refused over MCP"), Command), IsCommandBlocked(Command));
	}
	for (const TCHAR* Command : { TEXT("stat fps"), TEXT("rts.UI.Reload"), TEXT("rts.UI.Screenshot ResourceBar USA"), TEXT("showdebug") })
	{
		TestFalse(FString::Printf(TEXT("'%s' still runs over MCP"), Command), IsCommandBlocked(Command));
	}
	return true;
}

#endif
