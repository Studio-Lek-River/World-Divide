#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Data/RTSCivilBuildingData.h"
#include "Engine/StaticMesh.h"
#include "Tests/RTSTestFlags.h"

namespace RTSCivilBuildingTests
{
	using RTSTests::TestFlags;

	const TCHAR* CivilDir = TEXT("/Game/Data/Civil");

	/** How far a firing position may stand outside the pristine mesh's box: the tower's belfry sits in its wall. */
	constexpr float PositionTolerance = 100.f;

	/** The entry point lies outside the footprint, within this of it. */
	constexpr float MaxEntryDistance = 1000.f;
}

/**
 * Every civilian building (#408, URTSCivilBuildingData in /Game/Data/Civil) has its three state meshes, each its own, with
 * collision; health; and a garrison #252 can use: no more capacity than firing positions, each named once, standing in
 * the building and looking out of it, and an entry point just outside the footprint. One without capacity has no
 * firing positions. The village set has at least the seven types of 05_PRODUCER_ACTIONS (houses, farmhouse, barn,
 * workshop, landmark) that can be garrisoned.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCivilBuildingDataTest, "WorldDivide.Data.CivilBuildings", RTSCivilBuildingTests::TestFlags)

bool FRTSCivilBuildingDataTest::RunTest(const FString& Parameters)
{
	using namespace RTSCivilBuildingTests;

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.SearchAllAssets(true);
	TArray<FAssetData> Assets;
	Registry.GetAssetsByPath(CivilDir, Assets, true);
	int32 Garrisonable = 0;
	for (const FAssetData& Asset : Assets)
	{
		const URTSCivilBuildingData* Data = Cast<URTSCivilBuildingData>(Asset.GetAsset());
		if (!Data)
		{
			continue;
		}
		const FString Name = Asset.AssetName.ToString();
		const UStaticMesh* States[] = {Data->PristineMesh, Data->DamagedMesh, Data->RubbleMesh};
		bool bMeshes = true;
		for (const UStaticMesh* Mesh : States)
		{
			bMeshes &= TestNotNull(*FString::Printf(TEXT("%s has a mesh for each state"), *Name), Mesh);
			if (Mesh)
			{
				TestNotNull(*FString::Printf(TEXT("%s's %s has collision"), *Name, *Mesh->GetName()), Mesh->GetBodySetup());
			}
		}
		if (!bMeshes)
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s's states are three meshes"), *Name),
			States[0] != States[1] && States[1] != States[2] && States[0] != States[2]);
		TestTrue(*FString::Printf(TEXT("%s has health"), *Name), Data->MaxHealth > 0.f);

		const FBox Box = Data->PristineMesh->GetBoundingBox();
		TestTrue(*FString::Printf(TEXT("%s has no more capacity than firing positions"), *Name),
			Data->GarrisonCapacity <= Data->FiringPositions.Num());
		if (!Data->CanGarrison())
		{
			TestEqual(*FString::Printf(TEXT("%s, not garrisonable, has no firing positions"), *Name), Data->FiringPositions.Num(), 0);
			continue;
		}
		++Garrisonable;
		TSet<FName> Names;
		for (const FRTSGarrisonFiringPosition& Position : Data->FiringPositions)
		{
			bool bDuplicate = false;
			Names.Add(Position.Name, &bDuplicate);
			TestFalse(*FString::Printf(TEXT("%s names %s once"), *Name, *Position.Name.ToString()), bDuplicate);
			TestTrue(*FString::Printf(TEXT("%s's %s stands in the building"), *Name, *Position.Name.ToString()),
				Box.ExpandBy(PositionTolerance).IsInside(Position.Location));
			// Looking out: away from the footprint's middle.
			const FVector Out = FRotator(0.f, Position.Yaw, 0.f).Vector();
			const FVector FromMiddle = (Position.Location - Box.GetCenter()) * FVector(1.f, 1.f, 0.f);
			TestTrue(*FString::Printf(TEXT("%s's %s looks out"), *Name, *Position.Name.ToString()), FVector::DotProduct(Out, FromMiddle) > 0.f);
			TestTrue(*FString::Printf(TEXT("%s's %s covers an arc"), *Name, *Position.Name.ToString()), Position.ArcDegrees > 0.f);
		}
		const FBox2D Footprint(FVector2D(Box.Min), FVector2D(Box.Max));
		const FVector2D Entry(Data->EntryPoint);
		const float EntryDistance = FMath::Sqrt(Footprint.ComputeSquaredDistanceToPoint(Entry));
		TestTrue(*FString::Printf(TEXT("%s's entry point lies just outside it (%.0f cm)"), *Name, EntryDistance),
			EntryDistance > 0.f && EntryDistance <= MaxEntryDistance);
	}
	TestTrue(*FString::Printf(TEXT("At least 7 civilian buildings can be garrisoned (%d)"), Garrisonable), Garrisonable >= 7);
	return true;
}

#endif
