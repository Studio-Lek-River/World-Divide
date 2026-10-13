using UnrealBuildTool;

public class WorldDivideMCP : ModuleRules
{
	public WorldDivideMCP(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"ToolsetRegistry",
		});
	}
}
