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
			"UMG",
			"CommonUI",
			"CommonInput",
			"Projects",
			"Json",
		});

		// The HUD screenshots of rts.UI.Screenshot write their PNGs from an image view.
		PrivateDependencyModuleNames.Add("ImageCore");

		// rts.Debug.UnitShots logs the frame's draw calls, GPU time and game thread time.
		PrivateDependencyModuleNames.AddRange(new string[] { "RHI", "RenderCore" });

		// The shared shot sound waves (URTSShotSoundWave) derive from USoundWave, whose proxy factory lives here.
		PrivateDependencyModuleNames.Add("AudioExtensions");

		// The muzzle flash of vehicle guns (#220) is a Niagara system.
		PrivateDependencyModuleNames.Add("Niagara");

		// A map without a NavMeshBoundsVolume takes its preview area from its Landscape (RTSMaps::FindMapArea, #289).
		PrivateDependencyModuleNames.Add("Landscape");

		// The HUD reads its layout files at runtime (URTSLayoutWidget), so they ship with the game.
		RuntimeDependencies.Add("$(ProjectDir)/UI/Layouts/...", StagedFileType.UFS);

		// PIE-driven smoke tests (ApplicationCore: their simulated key presses), the placeholder art test, the cook rules test
		// (DeveloperToolSettings: the packaging settings), the map preview capture and the muzzle flash system that
		// setup_testmap.py builds (NiagaraEditor) only.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "AssetRegistry", "RenderCore", "ApplicationCore", "MeshDescription", "StaticMeshDescription", "DeveloperToolSettings", "NiagaraEditor" });
		}
	}
}
