using UnrealBuildTool;

public class WorldDivide : ModuleRules
{
	public WorldDivide(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"NavigationSystem",
			"GameplayTags",
			"DeveloperSettings",
			"Slate",
			"SlateCore",
		});

		// PIE-driven smoke tests, the placeholder art test and the map preview capture only.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "AssetRegistry", "ImageCore", "RenderCore" });
		}
	}
}
