#include "Data/RTSUnitData.h"
#include "Misc/AutomationTest.h"
#include "Player/RTSPlayerController.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSUnit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSSelectionGroupingTest, "WorldDivide.UI.Selection.Grouping", TestFlags)

bool FRTSSelectionGroupingTest::RunTest(const FString& Parameters)
{
	FRTSTestWorld TestWorld;
	URTSUnitData* Tank = NewObject<URTSUnitData>();
	URTSUnitData* Infantry = NewObject<URTSUnitData>();

	// Mixed order: the tank comes first, so its group does too.
	TArray<TWeakObjectPtr<ARTSUnit>> Selection;
	for (URTSUnitData* Data : { Tank, Infantry, Tank, Infantry, Infantry, Tank })
	{
		Selection.Add(TestWorld.SpawnUnit(Data, 0, FVector(100.f * Selection.Num(), 0.f, 0.f)));
	}

	TArray<FRTSSelectionGroup> Groups = ARTSPlayerController::GroupByType(Selection);
	if (!TestEqual(TEXT("Two types make two groups"), Groups.Num(), 2))
	{
		return false;
	}
	TestTrue(TEXT("The first selected type comes first"), Groups[0].Data == Tank);
	TestEqual(TEXT("with its three units"), Groups[0].Units.Num(), 3);
	TestTrue(TEXT("The second type comes second"), Groups[1].Data == Infantry);
	TestEqual(TEXT("with its three units"), Groups[1].Units.Num(), 3);
	TestTrue(TEXT("A group keeps its units in selection order"), Groups[0].Units[1] == Selection[2]);

	// A unit that is gone is left out; a type with none left has no group.
	Selection[1]->Destroy();
	Selection[3]->Destroy();
	Selection[4]->Destroy();
	Selection[5]->Destroy();
	Groups = ARTSPlayerController::GroupByType(Selection);
	if (!TestEqual(TEXT("Only the type with living units is left"), Groups.Num(), 1))
	{
		return false;
	}
	TestTrue(TEXT("the tanks"), Groups[0].Data == Tank);
	TestEqual(TEXT("two of them"), Groups[0].Units.Num(), 2);

	TestEqual(TEXT("Nothing selected, no groups"), ARTSPlayerController::GroupByType({}).Num(), 0);
	return true;
}

#endif
