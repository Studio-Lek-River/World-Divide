#include "Buildings/RTSBuilding.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSPrimitiveData.h"
#include "Units/RTSUnit.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Team colours as custom primitive data (#150): every drawn part of a unit or building carries its team's colour at
 * RTSPrimitiveData::TeamColor and keeps its mesh's shared material instances, so no unit makes a dynamic material instance
 * and the same part of every unit, whatever its team, is drawn with the same material.
 */
namespace RTSTeamColorTests
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	const TCHAR* M20DataPath = TEXT("/Game/Data/Units/DA_TestM20Guardian.DA_TestM20Guardian");
	const TCHAR* FactoryDataPath = TEXT("/Game/Data/Buildings/DA_TestVehicleFactory.DA_TestVehicleFactory");
	const TCHAR* VehicleMaterialPath = TEXT("/Game/Materials/M_Vehicle.M_Vehicle");
	const TCHAR* TeamMaterialPath = TEXT("/Game/Materials/M_PlaceholderTeam.M_PlaceholderTeam");

	/** Hull, turret, gun, barrel (#218), weapon station, two tracks and one instanced component for each of the three wheel meshes. */
	constexpr int32 M20DrawnParts = 10;

	/** The teams a match can have: the colours repeat after these. */
	constexpr int32 TeamColors = 6;

	/** The components that draw a mesh, in the order the actor made them. */
	TArray<UStaticMeshComponent*> GetDrawnParts(const AActor& Actor)
	{
		TArray<UStaticMeshComponent*> Parts;
		Actor.GetComponents(Parts);
		Parts.RemoveAll([](const UStaticMeshComponent* Part) { return !Part->GetStaticMesh(); });
		return Parts;
	}

	/** Part's team colour, RGBA from RTSPrimitiveData::TeamColor; unset when it has none. */
	TOptional<FLinearColor> GetColor(const UPrimitiveComponent& Part)
	{
		const TArray<float>& Data = Part.GetCustomPrimitiveData().Data;
		constexpr int32 Index = RTSPrimitiveData::TeamColor;
		if (Data.Num() < Index + 4)
		{
			return {};
		}
		return FLinearColor(Data[Index], Data[Index + 1], Data[Index + 2], Data[Index + 3]);
	}

	/** What a client does when the team replicates: the new value, then its OnRep. */
	void ReplicateTeam(AActor& Actor, int32 TeamId)
	{
		FIntProperty* Property = FindFProperty<FIntProperty>(Actor.GetClass(), TEXT("TeamId"));
		check(Property);
		Property->SetPropertyValue_InContainer(&Actor, TeamId);
		Actor.ProcessEvent(Actor.FindFunctionChecked(TEXT("OnRep_TeamId")), nullptr);
	}

	/** Every drawn part of Actor has the colour of TeamId and no dynamic material instance; returns how many parts it has. */
	int32 TestColored(FAutomationTestBase& Test, const TCHAR* What, const AActor& Actor, int32 TeamId)
	{
		const FLinearColor Expected = ARTSUnit::GetTeamColor(TeamId);
		const TArray<UStaticMeshComponent*> Parts = GetDrawnParts(Actor);
		for (const UStaticMeshComponent* Part : Parts)
		{
			const TOptional<FLinearColor> Color = GetColor(*Part);
			Test.TestTrue(FString::Printf(TEXT("%s: %s has team %d's colour (%s)"), What, *Part->GetName(), TeamId,
				Color ? *Color->ToString() : TEXT("none")), Color && Color->Equals(Expected, 1.e-6f));
			for (int32 Slot = 0; Slot < Part->GetNumMaterials(); ++Slot)
			{
				Test.TestFalse(FString::Printf(TEXT("%s: %s slot %d is no dynamic material instance"), What, *Part->GetName(), Slot),
					Part->GetMaterial(Slot) && Part->GetMaterial(Slot)->IsA<UMaterialInstanceDynamic>());
			}
		}
		return Parts.Num();
	}

	/** A and B, of one kind, draw each part with the same materials. */
	void TestSharesMaterials(FAutomationTestBase& Test, const TCHAR* What, const AActor& A, const AActor& B)
	{
		const TArray<UStaticMeshComponent*> PartsA = GetDrawnParts(A);
		const TArray<UStaticMeshComponent*> PartsB = GetDrawnParts(B);
		if (!Test.TestEqual(FString::Printf(TEXT("%s: both have the same parts"), What), PartsA.Num(), PartsB.Num()))
		{
			return;
		}
		int32 Shared = 0;
		int32 Slots = 0;
		for (int32 Index = 0; Index < PartsA.Num(); ++Index)
		{
			Test.TestTrue(FString::Printf(TEXT("%s: part %d is the same mesh"), What, Index), PartsA[Index]->GetStaticMesh() == PartsB[Index]->GetStaticMesh());
			for (int32 Slot = 0; Slot < PartsA[Index]->GetNumMaterials(); ++Slot)
			{
				++Slots;
				Shared += PartsA[Index]->GetMaterial(Slot) == PartsB[Index]->GetMaterial(Slot) ? 1 : 0;
			}
		}
		Test.TestTrue(FString::Printf(TEXT("%s: every slot of every part shares its material (%d of %d)"), What, Shared, Slots),
			Slots > 0 && Shared == Slots);
	}
}

using namespace RTSTeamColorTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSTeamColorValuesTest, "WorldDivide.Units.TeamColor.Values", TestFlags)

bool FRTSTeamColorValuesTest::RunTest(const FString& Parameters)
{
	for (int32 A = 0; A < TeamColors; ++A)
	{
		for (int32 B = A + 1; B < TeamColors; ++B)
		{
			TestFalse(*FString::Printf(TEXT("Teams %d and %d have different colours"), A, B),
				ARTSUnit::GetTeamColor(A).Equals(ARTSUnit::GetTeamColor(B), 0.05f));
		}
	}

	URTSUnitData* Data = LoadObject<URTSUnitData>(nullptr, M20DataPath);
	if (!TestNotNull(TEXT("The M20 Guardian's data loads"), Data))
	{
		return false;
	}
	const FRTSTestWorld TestWorld;
	ARTSUnit* Blue = TestWorld.SpawnUnit(Data, 0, FVector::ZeroVector);
	ARTSUnit* Red = TestWorld.SpawnUnit(Data, 1, FVector(0.f, 2000.f, 0.f));
	ARTSUnit* AlsoBlue = TestWorld.SpawnUnit(Data, 0, FVector(0.f, 4000.f, 0.f));
	TestEqual(TEXT("An M20 draws its ten parts in its team's colour"), TestColored(*this, TEXT("Team 0"), *Blue, 0), M20DrawnParts);
	TestColored(*this, TEXT("Team 1"), *Red, 1);
	TestColored(*this, TEXT("Team 0, another"), *AlsoBlue, 0);
	TestSharesMaterials(*this, TEXT("Two M20s of opposing teams"), *Blue, *Red);
	TestSharesMaterials(*this, TEXT("Two M20s of one team"), *Blue, *AlsoBlue);

	// The wheels: one instanced component per wheel mesh, coloured as a whole like every other part.
	TArray<UInstancedStaticMeshComponent*> Wheels;
	Red->GetComponents(Wheels);
	TestEqual(TEXT("The M20's wheels are three instanced sets"), Wheels.Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSTeamColorChangeTest, "WorldDivide.Units.TeamColor.TeamChange", TestFlags)

bool FRTSTeamColorChangeTest::RunTest(const FString& Parameters)
{
	URTSUnitData* Data = LoadObject<URTSUnitData>(nullptr, M20DataPath);
	if (!TestNotNull(TEXT("The M20 Guardian's data loads"), Data))
	{
		return false;
	}
	const FRTSTestWorld TestWorld;
	ARTSUnit* Changed = TestWorld.SpawnUnit(Data, 0, FVector::ZeroVector);
	ARTSUnit* Neighbour = TestWorld.SpawnUnit(Data, 0, FVector(0.f, 2000.f, 0.f));
	TArray<const UMaterialInterface*> Before;
	for (const UStaticMeshComponent* Part : GetDrawnParts(*Changed))
	{
		Before.Add(Part->GetMaterial(0));
	}

	ReplicateTeam(*Changed, 2);
	TestColored(*this, TEXT("After its team became 2"), *Changed, 2);
	TestColored(*this, TEXT("Its neighbour, unchanged"), *Neighbour, 0);
	TArray<const UMaterialInterface*> After;
	for (const UStaticMeshComponent* Part : GetDrawnParts(*Changed))
	{
		After.Add(Part->GetMaterial(0));
	}
	TestTrue(TEXT("A team change keeps the shared materials"), Before == After);
	TestSharesMaterials(*this, TEXT("Changed and its neighbour"), *Changed, *Neighbour);

	// The same team again, as a client's first replication of a unit sees it: nothing changes.
	ReplicateTeam(*Changed, 2);
	TestColored(*this, TEXT("The same team replicated again"), *Changed, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBuildingTeamColorTest, "WorldDivide.Buildings.TeamColor", TestFlags)

bool FRTSBuildingTeamColorTest::RunTest(const FString& Parameters)
{
	URTSBuildingData* Data = LoadObject<URTSBuildingData>(nullptr, FactoryDataPath);
	if (!TestNotNull(TEXT("The vehicle factory's data loads"), Data))
	{
		return false;
	}
	const FRTSTestWorld TestWorld;
	ARTSBuilding* Blue = TestWorld.SpawnBuilding(Data, 0, FVector::ZeroVector);
	ARTSBuilding* Green = TestWorld.SpawnBuilding(Data, 2, FVector(0.f, 5000.f, 0.f));
	TestEqual(TEXT("The factory draws one mesh in its team's colour"), TestColored(*this, TEXT("Team 0"), *Blue, 0), 1);
	TestColored(*this, TEXT("Team 2"), *Green, 2);
	TestSharesMaterials(*this, TEXT("Two factories of different teams"), *Blue, *Green);

	ReplicateTeam(*Green, 3);
	TestColored(*this, TEXT("After its team became 3"), *Green, 3);
	TestColored(*this, TEXT("The other factory, unchanged"), *Blue, 0);
	return true;
}

#if WITH_EDITORONLY_DATA
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSTeamColorMaterialsTest, "WorldDivide.Art.TeamColorMaterials", TestFlags)

bool FRTSTeamColorMaterialsTest::RunTest(const FString& Parameters)
{
	// Each named parameter of Material reads custom primitive data from its index. A material can keep an unconnected copy
	// of a parameter among its expressions, so the one that reads it is looked for.
	auto TestReads = [this](const TCHAR* Path, TMap<FName, int32> Indices)
	{
		const UMaterial* Material = LoadObject<UMaterial>(nullptr, Path);
		if (!TestNotNull(*FString::Printf(TEXT("%s loads"), Path), Material))
		{
			return;
		}
		for (const TObjectPtr<UMaterialExpression>& Expression : Material->GetExpressions())
		{
			int32 Index = INDEX_NONE;
			FName Name;
			if (const UMaterialExpressionVectorParameter* Vector = Cast<UMaterialExpressionVectorParameter>(Expression))
			{
				Name = Vector->ParameterName;
				Index = Vector->bUseCustomPrimitiveData ? Vector->PrimitiveDataIndex : INDEX_NONE;
			}
			else if (const UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expression))
			{
				Name = Scalar->ParameterName;
				Index = Scalar->bUseCustomPrimitiveData ? Scalar->PrimitiveDataIndex : INDEX_NONE;
			}
			if (const int32* Expected = Indices.Find(Name); Expected && *Expected == Index)
			{
				Indices.Remove(Name);
			}
		}
		for (const TPair<FName, int32>& Missing : Indices)
		{
			AddError(FString::Printf(TEXT("%s has no parameter %s that reads custom primitive data %d"), *Material->GetName(),
				*Missing.Key.ToString(), Missing.Value));
		}
	};
	TestReads(VehicleMaterialPath, { { TEXT("Color"), RTSPrimitiveData::TeamColor }, { TEXT("TrackPhase"), RTSPrimitiveData::TrackPhase },
		{ TEXT("TrackRate"), RTSPrimitiveData::TrackRate }, { TEXT("TrackStart"), RTSPrimitiveData::TrackStart },
		{ TEXT("TrackAcceleration"), RTSPrimitiveData::TrackAcceleration } });
	TestReads(TeamMaterialPath, { { TEXT("Color"), RTSPrimitiveData::TeamColor } });
	TestReads(TEXT("/Game/Materials/M_ShotFx.M_ShotFx"), { { TEXT("Color"), RTSPrimitiveData::ShotGlow } });
	TestTrue(TEXT("The team colour's four floats do not overlap the track phase"),
		RTSPrimitiveData::TeamColor > RTSPrimitiveData::TrackAcceleration);
	return true;
}
#endif

#endif
