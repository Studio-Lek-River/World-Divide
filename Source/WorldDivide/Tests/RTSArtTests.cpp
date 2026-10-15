#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Data/RTSUnitData.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
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
	constexpr int32 PlaceholderMeshCount = 14;

	const TCHAR* M20DataPath = TEXT("/Game/Data/Units/DA_TestM20Guardian.DA_TestM20Guardian");
	const TCHAR* M20Dir = TEXT("/Game/Art/USA/Vehicles/M20Guardian");
	/** The triangles of each exported part, as m20_guardian_blender.py counted them. */
	const TCHAR* M20TrianglesFile = TEXT("Art/USA/vehicles/m20_guardian/export_triangles.json");
	/** The RWS, two tracks, and twelve road wheels, two sprockets and two idlers. */
	constexpr int32 M20PartCount = 19;
	constexpr int32 M20WheelCount = 16;
	/** The hull's length as exported, in cm. */
	constexpr double M20HullLength = 710.2;
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
		TestFalse(TEXT("M_PlaceholderTeam is not flagged for Nanite"), TeamMaterial->GetUsageByFlag(MATUSAGE_Nanite));
	}
	return true;
}

/**
 * The M20 Guardian as setup_testmap.py imports it from its Blender export: every part at the triangle count it was exported
 * with, at real size, without Nanite (the parts move), each slot on the vehicle materials, and the texture maps compressed as
 * their kind needs (sRGB colour with the team mask in alpha, BC5 normals, linear masks).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSM20GuardianArtTest, "WorldDivide.Art.M20Guardian", TestFlags)

bool FRTSM20GuardianArtTest::RunTest(const FString& Parameters)
{
	const URTSUnitData* Data = LoadObject<URTSUnitData>(nullptr, M20DataPath);
	if (!TestNotNull(TEXT("DA_TestM20Guardian loads"), Data))
	{
		return false;
	}
	TestTrue(TEXT("It has a hull, a turret and a gun"), Data->Mesh && Data->TurretMesh && Data->GunMesh);
	TestEqual(TEXT("Its parts are all there"), Data->Parts.Num(), M20PartCount);
	TestEqual(TEXT("Its wheels roll"), Data->Parts.FilterByPredicate([](const FRTSVehiclePart& Part) { return Part.bWheel; }).Num(),
		M20WheelCount);
	TestTrue(TEXT("It is at real size"), Data->MeshScale.Equals(FVector::OneVector));
	if (Data->Mesh)
	{
		const FBox Box = Data->Mesh->GetBoundingBox();
		TestTrue(FString::Printf(TEXT("The hull is %.1f cm long, as exported"), Box.GetSize().X),
			FMath::IsNearlyEqual(Box.GetSize().X, M20HullLength, 2.0));
	}

	FString TrianglesText;
	TSharedPtr<FJsonObject> Triangles;
	if (!TestTrue(TEXT("The export's triangle counts are readable"),
		FFileHelper::LoadFileToString(TrianglesText, *FPaths::Combine(FPaths::ProjectDir(), M20TrianglesFile))
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TrianglesText), Triangles) && Triangles.IsValid()))
	{
		return false;
	}
	const UMaterialInterface* VehicleMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Vehicle.M_Vehicle"));
	const UMaterialInterface* LampMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_VehicleLamp.M_VehicleLamp"));
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Part : Triangles->Values)
	{
		const UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%s/Meshes/%s.%s"), M20Dir, *Part.Key, *Part.Key));
		if (!TestNotNull(*FString::Printf(TEXT("%s is imported"), *Part.Key), Mesh))
		{
			continue;
		}
		// The mesh build drops degenerate triangles; the v5 gun and RWS export a few dozen, nothing else is lost.
		const int32 Exported = static_cast<int32>(Part.Value->AsNumber());
		const int32 Imported = Mesh->GetNumTriangles(0);
		TestTrue(*FString::Printf(TEXT("%s keeps its triangles (%d of %d)"), *Part.Key, Imported, Exported),
			Imported <= Exported && Imported >= Exported - FMath::Max(Exported / 100, 1));
		TestFalse(*FString::Printf(TEXT("%s has Nanite off"), *Part.Key), Mesh->IsNaniteEnabled());
		TestEqual(*FString::Printf(TEXT("%s has no LODs yet"), *Part.Key), Mesh->GetNumLODs(), 1);
		for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials())
		{
			const UMaterialInstance* Instance = Cast<UMaterialInstance>(Slot.MaterialInterface);
			const UMaterialInterface* Parent = Instance ? Instance->Parent.Get() : Slot.MaterialInterface.Get();
			TestTrue(*FString::Printf(TEXT("%s slot %s uses the vehicle materials"), *Part.Key, *Slot.MaterialSlotName.ToString()),
				Parent && (Parent == VehicleMaterial || Parent == LampMaterial));
		}
	}

	for (const TCHAR* Set : { TEXT("Hull"), TEXT("Turret"), TEXT("Running"), TEXT("Track") })
	{
		auto Load = [Set](const TCHAR* Kind)
		{
			const FString Name = FString::Printf(TEXT("T_M20_%s_%s"), Set, Kind);
			return LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("%s/Textures/%s.%s"), M20Dir, *Name, *Name));
		};
		const UTexture2D* Color = Load(TEXT("BC"));
		const UTexture2D* Normal = Load(TEXT("N"));
		const UTexture2D* Masks = Load(TEXT("ORM"));
		if (!TestTrue(*FString::Printf(TEXT("The %s maps are imported"), Set), Color && Normal && Masks))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s colour is sRGB with its alpha kept"), Set),
			Color->SRGB && Color->CompressionSettings == TC_Default && !Color->CompressionNoAlpha);
		TestTrue(*FString::Printf(TEXT("%s normals are a normal map, green as exported"), Set),
			!Normal->SRGB && Normal->CompressionSettings == TC_Normalmap && !Normal->bFlipGreenChannel);
		TestTrue(*FString::Printf(TEXT("%s masks are linear"), Set), !Masks->SRGB && Masks->CompressionSettings == TC_Masks);
	}
	return true;
}

#endif
