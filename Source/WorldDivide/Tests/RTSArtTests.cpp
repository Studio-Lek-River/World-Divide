#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Data/RTSUnitData.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "StaticMeshAttributes.h"
#include "Algo/AllOf.h"
#include "Algo/AnyOf.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshResources.h"
#include "Tests/RTSTestFlags.h"
#include "Units/RTSUnit.h"

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
	/** The barrel, the RWS, two tracks, and twelve road wheels, two sprockets and two idlers. */
	constexpr int32 M20PartCount = 20;
	constexpr int32 M20WheelCount = 16;
	/** The hull's length as exported, in cm. */
	constexpr double M20HullLength = 710.2;

	/**
	 * The M20's LODs (#149): each LOD's triangles per tank, and the distances in cm at which every part switches to LOD1, 2
	 * and 3 (vehicle_art.LOD_DISTANCES) through the RTS camera's lens, 60 degrees across at 16:9 with the vertical field of
	 * view kept.
	 */
	constexpr int32 M20LODCount = 4;
	const FInt32Interval M20LODBudgets[] = { { 25000, 35000 }, { 6000, 10000 }, { 1000, 2500 } };
	const double M20LODDistances[] = { 3500.0, 11000.0, 26000.0 };
	const double RTSLensScreenMultiple = 0.5 / (FMath::Tan(FMath::DegreesToRadians(30.0)) / (16.0 / 9.0));
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
 * with, at real size, without Nanite (the parts move), each slot on the vehicle materials, the wheels' materials flagged for
 * the instanced sets ARTSUnit draws them in (#215), and the texture maps compressed as their kind needs (sRGB colour with the
 * team mask in alpha, BC5 normals, linear masks).
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
	for (const FRTSVehiclePart& Part : Data->Parts)
	{
		if (!Part.bWheel || Part.MountedOn != ERTSVehiclePartMount::Hull || !Part.Mesh)
		{
			continue;
		}
		for (const FStaticMaterial& Slot : Part.Mesh->GetStaticMaterials())
		{
			const UMaterial* Master = Slot.MaterialInterface ? Slot.MaterialInterface->GetMaterial() : nullptr;
			TestTrue(*FString::Printf(TEXT("%s slot %s may be drawn instanced"), *Part.Mesh->GetName(), *Slot.MaterialSlotName.ToString()),
				Master && Master->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes));
		}
	}
	// Shots leave from the gun's Muzzle socket (#217), at the bore's tip the Blender script measured.
	const UStaticMeshSocket* Muzzle = Data->GunMesh ? Data->GunMesh->FindSocket(ARTSUnit::MuzzleSocketName) : nullptr;
	if (TestNotNull(TEXT("The gun has a Muzzle socket"), Muzzle))
	{
		TestTrue(FString::Printf(TEXT("It sits at GunMuzzleOffset (%s)"), *Muzzle->RelativeLocation.ToString()),
			Muzzle->RelativeLocation.Equals(Data->GunMuzzleOffset, 1.0) && Muzzle->RelativeRotation.IsNearlyZero());
	}
	// The barrel is its own part on the gun's pivot, which recoils through the mantlet (#218), and reaches the muzzle.
	const FRTSVehiclePart* Barrel = Data->Parts.FindByPredicate([](const FRTSVehiclePart& Part) { return Part.bRecoils; });
	if (TestNotNull(TEXT("The barrel recoils"), Barrel) && TestNotNull(TEXT("The barrel has a mesh"), Barrel->Mesh.Get()))
	{
		TestTrue(TEXT("It rides on the gun at its pivot"), Barrel->MountedOn == ERTSVehiclePartMount::Gun && Barrel->Offset.IsNearlyZero());
		TestTrue(FString::Printf(TEXT("It ends at the muzzle (%.1f cm)"), Barrel->Mesh->GetBoundingBox().Max.X),
			FMath::IsNearlyEqual(Barrel->Mesh->GetBoundingBox().Max.X, Data->GunMuzzleOffset.X, 1.0));
		TestTrue(TEXT("It has a recoil stroke"), Data->GunRecoil.Distance > 0.f);
	}
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
		// The mesh build drops degenerate triangles; the v5 mantlet (SM_M20_Gun) and RWS export a few dozen, nothing else is lost.
		const int32 Exported = static_cast<int32>(Part.Value->AsNumber());
		const int32 Imported = Mesh->GetNumTriangles(0);
		TestTrue(*FString::Printf(TEXT("%s keeps its triangles (%d of %d)"), *Part.Key, Imported, Exported),
			Imported <= Exported && Imported >= Exported - FMath::Max(Exported / 100, 30));
		TestFalse(*FString::Printf(TEXT("%s has Nanite off"), *Part.Key), Mesh->IsNaniteEnabled());
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

/**
 * The M20's tracks carry their link shift (#151): UV0 as before and the four animation channels m20_guardian_bake.py writes,
 * read here as the material reads them, so a V flip or a lost channel on import shows. On a straight run a vertex's place in
 * the previous link is the opposite of its place in the next, one pitch along the run, and the next-but-one is twice as far; the turn to the next link is at most a sprocket tooth's. Their parts are tracks of that pitch, with the rigid material,
 * and the wheels roll by where they meet the track.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSM20TrackArtTest, "WorldDivide.Art.M20Tracks", TestFlags)

bool FRTSM20TrackArtTest::RunTest(const FString& Parameters)
{
	const URTSUnitData* Data = LoadObject<URTSUnitData>(nullptr, M20DataPath);
	if (!TestNotNull(TEXT("DA_TestM20Guardian loads"), Data))
	{
		return false;
	}
	const UMaterialInterface* Animated = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("%s/Materials/MI_M20_Track.MI_M20_Track"), M20Dir));
	const UMaterialInterface* Rigid = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("%s/Materials/MI_M20_Track_Static.MI_M20_Track_Static"), M20Dir));
	auto Switch = [](const UMaterialInterface* Material)
	{
		bool bValue = false;
		FGuid Unused;
		return Material && Material->GetStaticSwitchParameterValue(FHashedMaterialParameterInfo(TEXT("TrackAnimation")), bValue, Unused) && bValue;
	};
	TestTrue(TEXT("MI_M20_Track moves the links"), Switch(Animated));
	TestTrue(TEXT("MI_M20_Track_Static does not"), Rigid && !Switch(Rigid));
	for (const TCHAR* Other : { TEXT("Hull"), TEXT("Turret"), TEXT("Running") })
	{
		const UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("%s/Materials/MI_M20_%s.MI_M20_%s"), M20Dir, Other, Other));
		TestTrue(*FString::Printf(TEXT("MI_M20_%s keeps the plain material"), Other), Material && !Switch(Material));
	}

	int32 Tracks = 0;
	for (const FRTSVehiclePart& Part : Data->Parts)
	{
		if (Part.bWheel)
		{
			TestTrue(*FString::Printf(TEXT("%s rolls by where it meets the track"), *GetNameSafe(Part.Mesh)), Part.RollRadius > 30.f && Part.RollRadius < 40.f);
		}
		if (Part.TrackPitch <= 0.f)
		{
			continue;
		}
		++Tracks;
		TestTrue(*FString::Printf(TEXT("%s's links are %.2f cm long"), *GetNameSafe(Part.Mesh), Part.TrackPitch), FMath::IsNearlyEqual(Part.TrackPitch, 16.f, 0.1f));
		TestTrue(*FString::Printf(TEXT("%s has the rigid material for when it does not animate"), *GetNameSafe(Part.Mesh)), Part.RigidMaterial == Rigid);
		const UStaticMesh* Mesh = Part.Mesh;
		const FMeshDescription* Description = Mesh ? Mesh->GetMeshDescription(0) : nullptr;
		if (!TestNotNull(TEXT("The track has its mesh description"), Description))
		{
			continue;
		}
		// Its near LODs move their links, its far ones (the belts, #149) are drawn rigid.
		const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
		for (int32 LOD = 0; RenderData && LOD < RenderData->LODResources.Num(); ++LOD)
		{
			const UMaterialInterface* Expected = LOD < 2 ? Animated : Rigid;
			for (const FStaticMeshSection& Section : RenderData->LODResources[LOD].Sections)
			{
				TestTrue(*FString::Printf(TEXT("%s LOD%d is drawn with %s"), *Mesh->GetName(), LOD, *GetNameSafe(Expected)),
					Mesh->GetStaticMaterials().IsValidIndex(Section.MaterialIndex) && Mesh->GetStaticMaterials()[Section.MaterialIndex].MaterialInterface == Expected);
			}
		}
		TestTrue(*FString::Printf(TEXT("%s LOD1 keeps UV0 and the four animation channels"), *Mesh->GetName()),
			RenderData && RenderData->LODResources.Num() > 1 && RenderData->LODResources[1].VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords() == 5);
		const FStaticMeshConstAttributes Attributes(*Description);
		const TVertexInstanceAttributesConstRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
		if (!TestEqual(*FString::Printf(TEXT("%s has UV0 and four animation channels"), *Mesh->GetName()), UVs.GetNumChannels(), 5))
		{
			continue;
		}
		int32 Straight = 0;
		int32 Count = 0;
		float MaxTurn = 0.f;
		for (const FVertexInstanceID Instance : Description->VertexInstances().GetElementIDs())
		{
			const FVector2f Prev = UVs.Get(Instance, 1);
			const FVector2f Next = UVs.Get(Instance, 2);
			const FVector2f Next2 = UVs.Get(Instance, 3);
			MaxTurn = FMath::Max(MaxTurn, FMath::Abs(UVs.Get(Instance, 4).X));
			++Count;
			// On a straight run, level or sloped: a link either way, in line, and the next-but-one twice as far.
			Straight += FMath::IsNearlyEqual(Next.Size(), Part.TrackPitch, 0.1f) && (Prev + Next).Size() < 0.05f
				&& (Next2 - 2.f * Next).Size() < 0.05f ? 1 : 0;
		}
		TestTrue(*FString::Printf(TEXT("%s: most vertices lie on straight runs, one link apart each way (%d of %d)"), *Mesh->GetName(), Straight, Count),
			Straight > Count / 2);
		TestTrue(*FString::Printf(TEXT("%s: no link turns more than 35 degrees to the next (%.1f)"), *Mesh->GetName(), FMath::RadiansToDegrees(MaxTurn)),
			MaxTurn > 0.f && MaxTurn < FMath::DegreesToRadians(35.f));
	}
	TestEqual(TEXT("Both tracks animate"), Tracks, 2);
	return true;
}

/**
 * The M20's LODs (#149), as vehicle_art.build_lods makes them: every part has LOD1 to LOD3 after its unchanged LOD0, the
 * tank's triangles at each LOD stay within the budget, and every part switches at the same distances from the camera, its
 * screen sizes scaled by its own radius (an instanced wheel's box, as the GPU measures it), descending. The reductions keep
 * each part's material slots; only the tracks gain the rigid one for their belts. No part gains collision: the unit's box
 * and the navmesh stay what they were.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSM20GuardianLODTest, "WorldDivide.Art.M20GuardianLODs", TestFlags)

bool FRTSM20GuardianLODTest::RunTest(const FString& Parameters)
{
	const URTSUnitData* Data = LoadObject<URTSUnitData>(nullptr, M20DataPath);
	if (!TestNotNull(TEXT("DA_TestM20Guardian loads"), Data))
	{
		return false;
	}
	// Each mesh the tank draws, once per time it is drawn, and whether it is drawn instanced (the wheels).
	TArray<TPair<const UStaticMesh*, bool>> Drawn = { { Data->Mesh, false }, { Data->TurretMesh, false }, { Data->GunMesh, false } };
	for (const FRTSVehiclePart& Part : Data->Parts)
	{
		Drawn.Emplace(Part.Mesh, Part.bWheel);
	}

	int32 Totals[M20LODCount] = {};
	TSet<const UStaticMesh*> Checked;
	for (const TPair<const UStaticMesh*, bool>& Piece : Drawn)
	{
		const UStaticMesh* Mesh = Piece.Key;
		const FStaticMeshRenderData* RenderData = Mesh ? Mesh->GetRenderData() : nullptr;
		if (!TestNotNull(TEXT("Every part has a built mesh"), RenderData)
			|| !TestEqual(*FString::Printf(TEXT("%s has four LODs"), *Mesh->GetName()), Mesh->GetNumLODs(), M20LODCount))
		{
			continue;
		}
		for (int32 LOD = 0; LOD < M20LODCount; ++LOD)
		{
			Totals[LOD] += Mesh->GetNumTriangles(LOD);
		}
		if (Checked.Contains(Mesh))
		{
			continue;
		}
		Checked.Add(Mesh);

		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		const double Radius = Piece.Value ? Bounds.BoxExtent.Size() : Bounds.SphereRadius;
		for (int32 LOD = 1; LOD < M20LODCount; ++LOD)
		{
			const float ScreenSize = RenderData->ScreenSize[LOD].GetValue();
			TestTrue(*FString::Printf(TEXT("%s: LOD%d's screen size is below LOD%d's"), *Mesh->GetName(), LOD, LOD - 1),
				ScreenSize > 0.f && ScreenSize < RenderData->ScreenSize[LOD - 1].GetValue());
			TestTrue(*FString::Printf(TEXT("%s: fewer triangles at LOD%d than at LOD%d"), *Mesh->GetName(), LOD, LOD - 1),
				Mesh->GetNumTriangles(LOD) < Mesh->GetNumTriangles(LOD - 1));
			// ComputeBoundsScreenSize turned round: where a part of this radius reaches its LOD's screen size.
			const double Distance = ScreenSize > 0.f ? 2.0 * RTSLensScreenMultiple * Radius / ScreenSize : 0.0;
			TestTrue(*FString::Printf(TEXT("%s switches to LOD%d at %.0f m, with the rest of the tank"), *Mesh->GetName(), LOD, Distance / 100.0),
				FMath::IsNearlyEqual(Distance, M20LODDistances[LOD - 1], M20LODDistances[LOD - 1] * 0.01));
		}

		const TArray<FStaticMaterial>& Slots = Mesh->GetStaticMaterials();
		const bool bTrack = Algo::AnyOf(Data->Parts, [Mesh](const FRTSVehiclePart& Part) { return Part.Mesh == Mesh && Part.TrackPitch > 0.f; });
		TestEqual(*FString::Printf(TEXT("%s keeps its material slots"), *Mesh->GetName()), Slots.Num(),
			Mesh == Data->Mesh ? 2 : bTrack ? 2 : 1);
		for (int32 LOD = 0; LOD < M20LODCount; ++LOD)
		{
			for (const FStaticMeshSection& Section : RenderData->LODResources[LOD].Sections)
			{
				TestTrue(*FString::Printf(TEXT("%s LOD%d draws with one of its slots' materials"), *Mesh->GetName(), LOD),
					Slots.IsValidIndex(Section.MaterialIndex) && Slots[Section.MaterialIndex].MaterialInterface != nullptr);
			}
		}
		const UBodySetup* Body = Mesh->GetBodySetup();
		TestTrue(*FString::Printf(TEXT("%s has no collision of its own"), *Mesh->GetName()),
			!Body || Body->AggGeom.GetElementCount() == 0);
	}

	TestTrue(*FString::Printf(TEXT("LOD0 is the exported tank (%d triangles)"), Totals[0]), Totals[0] > 89000 && Totals[0] <= 89684);
	for (int32 LOD = 1; LOD < M20LODCount; ++LOD)
	{
		const FInt32Interval& Budget = M20LODBudgets[LOD - 1];
		TestTrue(*FString::Printf(TEXT("LOD%d is %d triangles per tank, within %d to %d"), LOD, Totals[LOD], Budget.Min, Budget.Max),
			Totals[LOD] >= Budget.Min && Totals[LOD] <= Budget.Max);
	}
	return true;
}

#endif
