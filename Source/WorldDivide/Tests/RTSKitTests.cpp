#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Data/RTSKitPieceData.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureLightProfile.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/RTSTestFlags.h"

namespace RTSKitTests
{
	using RTSTests::TestFlags;

	const TCHAR* CatalogueFile = TEXT("Art/Shared/kit/kit_catalogue.json");
	const TCHAR* ManifestFile = TEXT("Art/Shared/kit/build/kit_manifest.json");
	const TCHAR* KitDir = TEXT("/Game/Art/Shared/Kit/");
	/** Where a slot's master may live: the shared masters, or a faction's kit (/Game/Art/<Faction>/Kit/). */
	const TCHAR* SharedDir = TEXT("/Game/Art/Shared/");
	const TCHAR* LensSlot = TEXT("Lens");

	TSharedPtr<FJsonObject> ReadJson(const TCHAR* File)
	{
		FString Text;
		TSharedPtr<FJsonObject> Json;
		if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), File)))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
		}
		return Json;
	}

	/** A kit asset belongs under the shared kit or a faction's kit, nowhere else (BUILDING_KIT.md, Folders); a candidate
	 * not yet accepted stays in /Game/Art/Candidates until its integration. */
	bool IsKitPath(const FString& Path)
	{
		TArray<FString> Folders;
		Path.ParseIntoArray(Folders, TEXT("/"));
		return Path.StartsWith(KitDir) || Path.StartsWith(TEXT("/Game/Art/Candidates/"))
			|| (Folders.Num() > 4 && Folders[0] == TEXT("Game") && Folders[1] == TEXT("Art")
			&& Folders[3] == TEXT("Kit"));
	}

	bool IsMasterPath(const FString& Path)
	{
		TArray<FString> Folders;
		Path.ParseIntoArray(Folders, TEXT("/"));
		return Path.StartsWith(SharedDir) || (Folders.Num() > 4 && Folders[1] == TEXT("Art") && Folders[3] == TEXT("Kit"));
	}

	FVector ReadVector(const TArray<TSharedPtr<FJsonValue>>& Values)
	{
		return Values.Num() == 3 ? FVector(Values[0]->AsNumber(), Values[1]->AsNumber(), Values[2]->AsNumber()) : FVector::ZeroVector;
	}

	template <typename EnumType>
	FString EnumName(EnumType Value)
	{
		return StaticEnum<EnumType>()->GetNameStringByValue(static_cast<int64>(Value));
	}
}

/**
 * The shared building kit (docs/art/BUILDING_KIT.md, #389): every piece of kit_catalogue.json is in the manifest
 * kit_blender.py derives and has its generated DA_Kit_<Piece>, and every DA_Kit and manifest entry is in the catalogue, the
 * data agreeing with both. No kit asset lies outside the kit's folders. Each piece's mesh fits its class and its own
 * budgets (an S piece keeps one LOD), has no Nanite or collision, at most two slots named as in the catalogue, each an
 * instance of a shared or faction-kit master that draws on instanced meshes. Its real light, which an S piece never has,
 * stays within its class's candela, cone and radius, stands in its Lens and shines out of it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSKitArtTest, "WorldDivide.Art.Kit", RTSKitTests::TestFlags)

bool FRTSKitArtTest::RunTest(const FString& Parameters)
{
	using namespace RTSKitTests;

	const TSharedPtr<FJsonObject> Catalogue = ReadJson(CatalogueFile);
	const TSharedPtr<FJsonObject> Manifest = ReadJson(ManifestFile);
	if (!TestTrue(TEXT("kit_catalogue.json is readable"), Catalogue.IsValid())
		|| !TestTrue(TEXT("kit_manifest.json is readable"), Manifest.IsValid()))
	{
		return false;
	}
	const TSharedPtr<FJsonObject>& Built = Manifest->GetObjectField(TEXT("pieces"));

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.SearchAllAssets(true);
	TArray<FAssetData> Assets;
	Registry.GetAssetsByPath(TEXT("/Game"), Assets, true);
	TSet<FString> DataPaths;
	for (const FAssetData& Asset : Assets)
	{
		const FString Name = Asset.AssetName.ToString();
		if (Name.StartsWith(TEXT("SM_Kit_")) || Name.StartsWith(TEXT("DA_Kit_")))
		{
			TestTrue(*FString::Printf(TEXT("%s lies in a kit folder"), *Asset.PackageName.ToString()), IsKitPath(Asset.PackageName.ToString()));
		}
		if (Name.StartsWith(TEXT("DA_Kit_")))
		{
			DataPaths.Add(Asset.PackageName.ToString());
		}
	}

	TSet<FString> Ids;
	for (const TSharedPtr<FJsonValue>& Value : Catalogue->GetArrayField(TEXT("pieces")))
	{
		const TSharedPtr<FJsonObject>& Piece = Value->AsObject();
		const FString Id = Piece->GetStringField(TEXT("id"));
		Ids.Add(Id);
		const FString MeshName = Piece->GetStringField(TEXT("mesh"));
		const TSharedPtr<FJsonObject>* Record = nullptr;
		if (!TestTrue(*FString::Printf(TEXT("%s is in the manifest"), *Id), Built->TryGetObjectField(Id, Record)))
		{
			continue;
		}

		// The generated data, in /Game/Art/Shared/Kit/<Category>/DA_Kit_<Piece>.
		FString Category;
		Id.Split(TEXT("/"), &Category, nullptr);
		const FString Directory = FString::Printf(TEXT("%s%s%s"), KitDir, *Category.Left(1).ToUpper(), *Category.Mid(1).ToLower());
		const FString DataName = MeshName.Replace(TEXT("SM_Kit_"), TEXT("DA_Kit_"));
		const FString DataPath = FString::Printf(TEXT("%s/%s"), *Directory, *DataName);
		DataPaths.Remove(DataPath);
		const URTSKitPieceData* Data = LoadObject<URTSKitPieceData>(nullptr, *FString::Printf(TEXT("%s.%s"), *DataPath, *DataName));
		if (!TestNotNull(*FString::Printf(TEXT("%s has its %s"), *Id, *DataPath), Data)
			|| !TestNotNull(*FString::Printf(TEXT("%s has a mesh"), *Id), Data->Mesh.Get()))
		{
			continue;
		}
		const UStaticMesh* Mesh = Data->Mesh;
		TestEqual(*FString::Printf(TEXT("%s: the data's id"), *Id), Data->Id, Id);
		TestEqual(*FString::Printf(TEXT("%s: its mesh"), *Id), Mesh->GetPathName(), FString::Printf(TEXT("%s/%s.%s"), *Directory, *MeshName, *MeshName));
		TestEqual(*FString::Printf(TEXT("%s: its class"), *Id), EnumName(Data->Class), Piece->GetStringField(TEXT("class")));
		TestEqual(*FString::Printf(TEXT("%s: its faction"), *Id), Data->Faction, FName(Piece->GetStringField(TEXT("faction"))));
		TestEqual(*FString::Printf(TEXT("%s: its emergency default"), *Id), Data->bEmergencyDefault, Piece->GetBoolField(TEXT("emergency_default")));
		TestEqual(*FString::Printf(TEXT("%s: its fault response"), *Id), EnumName(Data->FaultResponse), Piece->GetStringField(TEXT("fault_response")));
		double WPODisableDistance = 0.0;
		Piece->TryGetNumberField(TEXT("wpo_disable_distance"), WPODisableDistance);
		TestEqual(*FString::Printf(TEXT("%s: its WPO disable distance"), *Id), Data->WPODisableDistance, static_cast<float>(WPODisableDistance));
		const TSharedPtr<FJsonObject>& Budgets = Piece->GetObjectField(TEXT("budgets"));
		TestEqual(*FString::Printf(TEXT("%s: its LOD0 budget"), *Id), Data->BudgetLOD0, static_cast<int32>(Budgets->GetNumberField(TEXT("lod0"))));
		TestEqual(*FString::Printf(TEXT("%s: its LOD1 budget"), *Id), Data->BudgetLOD1, static_cast<int32>(Budgets->GetNumberField(TEXT("lod1"))));
		const TSharedPtr<FJsonObject>& Points = Piece->GetObjectField(TEXT("mount_points"));
		TestEqual(*FString::Printf(TEXT("%s: its mount points"), *Id), Data->MountPoints.Num(), Points->Values.Num());
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Point : Points->Values)
		{
			const FTransform* Frame = Data->MountPoints.Find(FName(Point.Key));
			TestTrue(*FString::Printf(TEXT("%s: mount point %s"), *Id, *Point.Key), Frame
				&& Frame->GetLocation().Equals(ReadVector(Point.Value->AsObject()->GetArrayField(TEXT("location"))), 0.01));
		}

		// The mesh as the manifest measured it, within its class and its own budgets.
		const RTSKit::FClassLimits Limits = RTSKit::GetClassLimits(Data->Class);
		const TArray<TSharedPtr<FJsonValue>>& Triangles = (*Record)->GetArrayField(TEXT("triangles"));
		TestEqual(*FString::Printf(TEXT("%s: its LODs are the manifest's"), *Id), Mesh->GetNumLODs(), Triangles.Num());
		TestEqual(*FString::Printf(TEXT("%s: an S piece keeps LOD0 alone, the others have LOD1"), *Id), Mesh->GetNumLODs(),
			Data->Class == ERTSKitClass::S ? 1 : 2);
		for (int32 LOD = 0; LOD < FMath::Min(Mesh->GetNumLODs(), Triangles.Num()); ++LOD)
		{
			const int32 Count = Mesh->GetNumTriangles(LOD);
			TestEqual(*FString::Printf(TEXT("%s LOD%d: the manifest's triangles"), *Id, LOD), Count, static_cast<int32>(Triangles[LOD]->AsNumber()));
			const int32 Budget = FMath::Min(LOD == 0 ? Data->BudgetLOD0 : Data->BudgetLOD1, LOD == 0 ? Limits.LOD0 : Limits.LOD1);
			TestTrue(*FString::Printf(TEXT("%s LOD%d is %d triangles, within %d"), *Id, LOD, Count, Budget), Count <= Budget);
		}
		TestFalse(*FString::Printf(TEXT("%s has Nanite off"), *Id), Mesh->IsNaniteEnabled());
		const UBodySetup* Body = Mesh->GetBodySetup();
		TestTrue(*FString::Printf(TEXT("%s has no collision"), *Id), !Body || Body->AggGeom.GetElementCount() == 0);

		const TArray<TSharedPtr<FJsonValue>>& Slots = Piece->GetArrayField(TEXT("slots"));
		const TArray<FStaticMaterial>& Materials = Mesh->GetStaticMaterials();
		TestTrue(*FString::Printf(TEXT("%s has at most 2 slots"), *Id), Materials.Num() <= 2);
		TestEqual(*FString::Printf(TEXT("%s: its slots"), *Id), Materials.Num(), Slots.Num());
		bool bLensSlot = false;
		for (int32 Index = 0; Index < FMath::Min(Materials.Num(), Slots.Num()); ++Index)
		{
			const FStaticMaterial& Slot = Materials[Index];
			TestEqual(*FString::Printf(TEXT("%s slot %d"), *Id, Index), Slot.MaterialSlotName.ToString(), Slots[Index]->AsString());
			bLensSlot |= Slot.MaterialSlotName == LensSlot;
			const UMaterialInstance* Instance = Cast<UMaterialInstance>(Slot.MaterialInterface);
			const UMaterialInterface* Parent = Instance ? Instance->Parent.Get() : nullptr;
			const UMaterial* Master = Instance ? Instance->GetMaterial() : nullptr;
			TestTrue(*FString::Printf(TEXT("%s slot %s is an instance of a shared or faction-kit master"), *Id, *Slot.MaterialSlotName.ToString()),
				Parent && IsMasterPath(Parent->GetPathName()));
			TestTrue(*FString::Printf(TEXT("%s slot %s draws on instanced meshes"), *Id, *Slot.MaterialSlotName.ToString()),
				Master && Master->bUsedWithInstancedStaticMeshes);
		}

		// The lens as the manifest measured it, and the light in it.
		const TArray<TSharedPtr<FJsonValue>>* Lens = nullptr;
		const bool bLens = (*Record)->TryGetArrayField(TEXT("lens_bounds"), Lens) && Lens->Num() == 2;
		TestEqual(*FString::Printf(TEXT("%s: a lens only where the manifest measured one"), *Id), static_cast<bool>(Data->LensBounds.IsValid), bLens);
		if (bLens && Data->LensBounds.IsValid)
		{
			const FBox Measured(ReadVector((*Lens)[0]->AsArray()), ReadVector((*Lens)[1]->AsArray()));
			TestTrue(*FString::Printf(TEXT("%s: its lens bounds are the manifest's"), *Id),
				Data->LensBounds.Min.Equals(Measured.Min, 0.01) && Data->LensBounds.Max.Equals(Measured.Max, 0.01));
		}
		const TSharedPtr<FJsonObject>* Light = nullptr;
		const bool bLight = Piece->TryGetObjectField(TEXT("light"), Light);
		TestEqual(*FString::Printf(TEXT("%s: a light only where the catalogue has one"), *Id), Data->bHasLight, bLight);
		if (Data->Class == ERTSKitClass::S)
		{
			TestFalse(*FString::Printf(TEXT("%s is class S: no real light"), *Id), Data->bHasLight);
		}
		// Its night look (#440): a beam from a Spot or Rect light, a glare halo round a lens, a profile, as the catalogue has them.
		const TSharedPtr<FJsonObject>* Beam = nullptr;
		const bool bBeam = Piece->TryGetObjectField(TEXT("beam"), Beam);
		TestEqual(*FString::Printf(TEXT("%s: a beam only where the catalogue has one"), *Id), Data->BeamMesh != nullptr, bBeam);
		if (bBeam && Data->BeamMesh)
		{
			TestEqual(*FString::Printf(TEXT("%s: its beam's strength"), *Id), Data->BeamStrength, static_cast<float>((*Beam)->GetNumberField(TEXT("strength"))));
			TestTrue(*FString::Printf(TEXT("%s: its beam comes from a Spot or Rect light"), *Id),
				Data->bHasLight && Data->Light.Type != ERTSBuildingLightType::Point);
			TestEqual(*FString::Printf(TEXT("%s: its beam is as long as the catalogue's"), *Id),
				static_cast<float>(Data->BeamMesh->GetBoundingBox().Max.X), static_cast<float>((*Beam)->GetNumberField(TEXT("length"))), 2.f);
		}
		double Glare = 0.0;
		const bool bGlare = Piece->TryGetNumberField(TEXT("glare"), Glare);
		TestEqual(*FString::Printf(TEXT("%s: a glare halo only where the catalogue has one"), *Id), Data->GlareMesh != nullptr, bGlare);
		if (bGlare)
		{
			TestEqual(*FString::Printf(TEXT("%s: its glare's strength"), *Id), Data->GlareStrength, static_cast<float>(Glare));
			TestTrue(*FString::Printf(TEXT("%s: its glare stands at a lens"), *Id), Data->LensBounds.IsValid && Data->GlareRadius > 0.f);
		}
		if (!bLight || !Data->bHasLight)
		{
			continue;
		}
		const FRTSKitLight& Real = Data->Light;
		TestEqual(*FString::Printf(TEXT("%s: its light's type"), *Id), EnumName(Real.Type), (*Light)->GetStringField(TEXT("type")));
		TestEqual(*FString::Printf(TEXT("%s: its candela"), *Id), Real.Candelas, static_cast<float>((*Light)->GetNumberField(TEXT("candela"))));
		TestEqual(*FString::Printf(TEXT("%s: its kelvin"), *Id), Real.Kelvin, static_cast<float>((*Light)->GetNumberField(TEXT("kelvin"))));
		TestEqual(*FString::Printf(TEXT("%s: its radius"), *Id), Real.Radius, static_cast<float>((*Light)->GetNumberField(TEXT("radius"))));
		TestEqual(*FString::Printf(TEXT("%s: its group"), *Id), Real.Group, FName((*Light)->GetStringField(TEXT("group"))));
		TestEqual(*FString::Printf(TEXT("%s: its behaviour"), *Id), EnumName(Real.Behaviour), (*Light)->GetStringField(TEXT("behaviour")));
		TestEqual(*FString::Printf(TEXT("%s: its priority"), *Id), EnumName(Real.Priority), (*Light)->GetStringField(TEXT("priority")));
		TestEqual(*FString::Printf(TEXT("%s: its draw distance"), *Id), Real.MaxDrawDistance,
			static_cast<float>((*Light)->GetNumberField(TEXT("max_draw_distance"))));
		const TArray<TSharedPtr<FJsonValue>>* Cone = nullptr;
		if ((*Light)->TryGetArrayField(TEXT("cone"), Cone) && Cone->Num() == 2)
		{
			TestEqual(*FString::Printf(TEXT("%s: its inner cone"), *Id), Real.InnerConeAngle, static_cast<float>((*Cone)[0]->AsNumber()));
			TestEqual(*FString::Printf(TEXT("%s: its outer cone"), *Id), Real.OuterConeAngle, static_cast<float>((*Cone)[1]->AsNumber()));
		}
		FString Profile;
		const bool bProfile = (*Light)->TryGetStringField(TEXT("profile"), Profile);
		TestEqual(*FString::Printf(TEXT("%s: a light profile only where the catalogue has one"), *Id), Real.Profile != nullptr, bProfile);
		if (bProfile && Real.Profile)
		{
			TestTrue(*FString::Printf(TEXT("%s: its profile is the catalogue's %s"), *Id, *Profile), Real.Profile->GetName().Equals(TEXT("LP_Kit_") + Profile, ESearchCase::IgnoreCase));
		}

		TestTrue(*FString::Printf(TEXT("%s: %.0f cd, within its class's %.0f"), *Id, Real.Candelas, Limits.MaxCandelas), Real.Candelas <= Limits.MaxCandelas);
		TestTrue(*FString::Printf(TEXT("%s: a %.0f cm radius, within its class's %.0f"), *Id, Real.Radius, Limits.MaxRadius), Real.Radius <= Limits.MaxRadius);
		if (Real.Type != ERTSBuildingLightType::Point)
		{
			TestTrue(*FString::Printf(TEXT("%s: a %.0f degree outer cone, within its class's %.0f"), *Id, Real.OuterConeAngle, Limits.MaxOuterCone),
				Real.OuterConeAngle <= Limits.MaxOuterCone);
		}
		TestTrue(*FString::Printf(TEXT("%s: its light shines from a Lens slot"), *Id), bLensSlot);
		// Its light.offset (#455) stands it out along its light.
		double Offset = 0.0;
		(*Light)->TryGetNumberField(TEXT("offset"), Offset);
		TestTrue(*FString::Printf(TEXT("%s: its light stands in its Lens"), *Id),
			Data->LensBounds.IsValid && Data->LensBounds.ExpandBy(0.5).IsInside(Real.Location - Real.Rotation.Vector() * Offset));
		// A turning beacon's light stands on its axis, which it turns about (#459).
		TestTrue(*FString::Printf(TEXT("%s: its light shines out of its Lens"), *Id),
			Data->LensBounds.IsValid && (Real.Behaviour == ERTSKitLightBehaviour::Rotate
				|| FVector::DotProduct(Real.Rotation.Vector(), Real.Location - Data->LensBounds.GetCenter()) > 0.0));
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Built->Values)
	{
		TestTrue(*FString::Printf(TEXT("The manifest's %s is in the catalogue"), *Entry.Key), Ids.Contains(Entry.Key));
	}
	for (const FString& Path : DataPaths)
	{
		AddError(FString::Printf(TEXT("%s is not a piece of the catalogue"), *Path));
	}
	AddInfo(FString::Printf(TEXT("%d kit pieces"), Ids.Num()));
	return true;
}

#endif
