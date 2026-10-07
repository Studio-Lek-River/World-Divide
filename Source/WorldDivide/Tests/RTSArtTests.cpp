#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Tests/RTSTestFlags.h"

/**
 * The placeholder models are a few hundred triangles each: Nanite gains nothing on them, and the translucent placement
 * ghost cannot render on a Nanite mesh. placeholder_art.py imports them without it; this keeps a later import change honest.
 */
namespace
{
	using RTSTests::TestFlags;

	const FName PlaceholderMeshDir(TEXT("/Game/Art/Placeholder/Meshes"));
	const TCHAR* TeamMaterialPath = TEXT("/Game/Materials/M_PlaceholderTeam.M_PlaceholderTeam");
	constexpr int32 PlaceholderMeshCount = 12;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSPlaceholderWithoutNaniteTest, "WorldDivide.Art.PlaceholderWithoutNanite", TestFlags)

bool FRTSPlaceholderWithoutNaniteTest::RunTest(const FString& Parameters)
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.SearchAllAssets(true);

	TArray<FAssetData> Meshes;
	Registry.GetAssetsByPath(PlaceholderMeshDir, Meshes, true);
	Meshes.RemoveAll([](const FAssetData& Asset) { return Asset.AssetClassPath != UStaticMesh::StaticClass()->GetClassPathName(); });
	TestEqual(TEXT("Every placeholder model is imported"), Meshes.Num(), PlaceholderMeshCount);

	for (const FAssetData& Asset : Meshes)
	{
		const UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.GetAsset());
		if (!TestNotNull(*FString::Printf(TEXT("%s loads"), *Asset.AssetName.ToString()), Mesh))
		{
			continue;
		}
		TestFalse(*FString::Printf(TEXT("%s has Nanite off"), *Asset.AssetName.ToString()), Mesh->IsNaniteEnabled());
	}

	const UMaterial* TeamMaterial = LoadObject<UMaterial>(nullptr, TeamMaterialPath);
	if (TestNotNull(TEXT("M_PlaceholderTeam loads"), TeamMaterial))
	{
		TestFalse(TEXT("M_PlaceholderTeam is not flagged for Nanite"), static_cast<bool>(TeamMaterial->bUsedWithNanite));
	}
	return true;
}

#endif
