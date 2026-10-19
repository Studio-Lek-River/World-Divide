#include "Buildings/RTSBuilding.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSUnit.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The occlusion of unit parts (#214): every drawn part of a unit is lit as a group under the unit's collision box, which gives
 * it the approximate, grouped occlusion queries of static scenery instead of one query of its own in every frame. The parts
 * keep their meshes, materials, shadows and visibility; rts.Render.GroupPartOcclusion 0 makes units with a query per part, as
 * before; buildings are left as they were.
 */
namespace RTSPartOcclusionTests
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	const TCHAR* M20DataPath = TEXT("/Game/Data/Units/DA_TestM20Guardian.DA_TestM20Guardian");
	const TCHAR* TankDataPath = TEXT("/Game/Data/Units/DA_TestTank.DA_TestTank");
	const TCHAR* FactoryDataPath = TEXT("/Game/Data/Buildings/DA_TestVehicleFactory.DA_TestVehicleFactory");
	const TCHAR* GroupCVarName = TEXT("rts.Render.GroupPartOcclusion");

	/** Hull, turret, gun, barrel (#218), weapon station, two tracks and one instanced component for each of the three wheel meshes. */
	constexpr int32 M20DrawnParts = 10;

	TArray<UStaticMeshComponent*> GetDrawnParts(const AActor& Actor)
	{
		TArray<UStaticMeshComponent*> Parts;
		Actor.GetComponents(Parts);
		Parts.RemoveAll([](const UStaticMeshComponent* Part) { return !Part->GetStaticMesh(); });
		return Parts;
	}

	/** How many of Unit's drawn parts are grouped under its collision box; each one that is not is a failure when bGrouped. */
	int32 TestGrouped(FAutomationTestBase& Test, const TCHAR* What, const ARTSUnit& Unit, bool bGrouped)
	{
		const UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Unit.GetRootComponent());
		int32 Grouped = 0;
		for (const UStaticMeshComponent* Part : GetDrawnParts(Unit))
		{
			const bool bPartGrouped = Root && Part->GetLightingAttachmentRoot() == Root;
			Test.TestEqual(FString::Printf(TEXT("%s: %s is %sgrouped under the collision box"), What, *Part->GetName(), bGrouped ? TEXT("") : TEXT("not ")),
				bPartGrouped, bGrouped);
			Grouped += bPartGrouped ? 1 : 0;
		}
		return Grouped;
	}

	/** Sets rts.Render.GroupPartOcclusion for its lifetime. */
	struct FScopedGroupCVar
	{
		explicit FScopedGroupCVar(bool bValue)
			: CVar(IConsoleManager::Get().FindConsoleVariable(GroupCVarName))
		{
			if (CVar)
			{
				Saved = CVar->GetString();
				CVar->Set(bValue ? TEXT("1") : TEXT("0"), ECVF_SetByConsole);
			}
		}

		~FScopedGroupCVar()
		{
			if (CVar)
			{
				CVar->Set(*Saved, ECVF_SetByConsole);
			}
		}

		IConsoleVariable* CVar = nullptr;
		FString Saved;
	};
}

// Scoped to the test: RTSTeamColorTests has helpers of the same names, and a unity build can put both files in one unit.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSPartOcclusionTest, "WorldDivide.Units.PartOcclusion", RTSPartOcclusionTests::TestFlags)

bool FRTSPartOcclusionTest::RunTest(const FString& Parameters)
{
	using namespace RTSPartOcclusionTests;
	URTSUnitData* M20 = LoadObject<URTSUnitData>(nullptr, M20DataPath);
	URTSUnitData* Tank = LoadObject<URTSUnitData>(nullptr, TankDataPath);
	URTSBuildingData* Factory = LoadObject<URTSBuildingData>(nullptr, FactoryDataPath);
	if (!TestNotNull(TEXT("The M20 Guardian's data loads"), M20) || !TestNotNull(TEXT("The tank's data loads"), Tank)
		|| !TestNotNull(TEXT("The vehicle factory's data loads"), Factory))
	{
		return false;
	}
	const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(GroupCVarName);
	if (!TestNotNull(TEXT("rts.Render.GroupPartOcclusion exists"), CVar))
	{
		return false;
	}
	TestTrue(TEXT("Units group their parts' occlusion by default"), CVar->GetBool());

	const FRTSTestWorld TestWorld;
	{
		const FScopedGroupCVar Group(true);
		const ARTSUnit* Grouped = TestWorld.SpawnUnit(M20, 0, FVector::ZeroVector);
		TestEqual(TEXT("All ten parts of an M20 are grouped"), TestGrouped(*this, TEXT("M20"), *Grouped, true), M20DrawnParts);
		TArray<UInstancedStaticMeshComponent*> Wheels;
		Grouped->GetComponents(Wheels);
		TestEqual(TEXT("The M20's three wheel sets are among them"), Wheels.Num(), 3);
		TestTrue(TEXT("A placeholder tank groups its parts as well"), TestGrouped(*this, TEXT("Tank"), *TestWorld.SpawnUnit(Tank, 1, FVector(0.f, 3000.f, 0.f)), true) > 0);

		// Grouping only changes how the parts are queried: they stay visible and movable, cast shadows and do not collide.
		for (const UStaticMeshComponent* Part : GetDrawnParts(*Grouped))
		{
			const FString Name = Part->GetName();
			TestTrue(FString::Printf(TEXT("%s is still visible"), *Name), Part->IsVisible());
			TestTrue(FString::Printf(TEXT("%s still casts a shadow"), *Name), Part->CastShadow);
			TestFalse(FString::Printf(TEXT("%s does not leave the group"), *Name), Part->bExcludeFromLightAttachmentGroup);
			TestFalse(FString::Printf(TEXT("%s still does not collide"), *Name), Part->IsCollisionEnabled());
			TestEqual(FString::Printf(TEXT("%s stays movable"), *Name), Part->Mobility.GetValue(), EComponentMobility::Movable);
		}
		const UPrimitiveComponent* Box = Cast<UPrimitiveComponent>(Grouped->GetRootComponent());
		TestTrue(TEXT("The collision box still blocks picking traces"), Box && Box->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block);
		TestTrue(TEXT("The collision box draws nothing of its own"), Box && Box->bHiddenInGame);

		const ARTSBuilding* Building = TestWorld.SpawnBuilding(Factory, 0, FVector(0.f, 9000.f, 0.f));
		for (const UStaticMeshComponent* Part : GetDrawnParts(*Building))
		{
			TestNull(FString::Printf(TEXT("The factory's %s keeps the occlusion it had"), *Part->GetName()), Part->GetLightingAttachmentRoot());
		}
	}
	{
		const FScopedGroupCVar Group(false);
		const ARTSUnit* Exact = TestWorld.SpawnUnit(M20, 0, FVector(0.f, -3000.f, 0.f));
		TestEqual(TEXT("With rts.Render.GroupPartOcclusion 0 no part is grouped"), TestGrouped(*this, TEXT("M20, exact"), *Exact, false), 0);
	}
	return true;
}

#endif
