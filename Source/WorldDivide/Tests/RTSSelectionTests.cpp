#include "Buildings/RTSBuilding.h"
#include "Core/RTSPlayerState.h"
#include "Core/RTSSettings.h"
#include "Data/RTSAircraftData.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSUpgradeData.h"
#include "Misc/AutomationTest.h"
#include "Player/RTSPlayerController.h"
#include "UI/Layout/RTSUIBinding.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSUnit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;
}

// Named rather than anonymous: unity builds merge anonymous namespaces, and other tests have helpers of the same names.
namespace RTSSelectionTests
{

	/** The selection panel's binding source, read for Controller. */
	struct FSelectionPanel
	{
		const IRTSUIBindingSource* Source = nullptr;
		FRTSUIContext Context;

		explicit FSelectionPanel(ARTSPlayerController* Controller)
		{
			for (const IRTSUIBindingSource* Each : RTSUIBindings::GetDefaultSources())
			{
				if (Each->GetPanels().Contains(FName(TEXT("Selection"))))
				{
					Source = Each;
				}
			}
			Context.Player = Controller;
		}

		FRTSUIValue Value(const TCHAR* Key) const
		{
			FRTSUIValue Out;
			Source->GetValue(Key, Context, Out);
			return Out;
		}

		int32 Count(const TCHAR* List) const
		{
			return Source->GetListCount(List, Context);
		}

		FRTSUIValue Item(const TCHAR* List, int32 Index, const TCHAR* Key) const
		{
			FRTSUIContext ItemContext = Context;
			ItemContext.List = List;
			ItemContext.Index = Index;
			FRTSUIValue Out;
			Source->GetListValue(Key, ItemContext, Out);
			return Out;
		}
	};

	/** A local controller for Player, without input or a HUD. */
	ARTSPlayerController* SpawnController(const FRTSTestWorld& TestWorld, ARTSPlayerState* Player)
	{
		ARTSPlayerController* Controller = TestWorld.World->SpawnActor<ARTSPlayerController>();
		Controller->PlayerState = Player;
		return Controller;
	}

	/** "Upgrade <Number>", weapon damage x1.1 on Target. */
	URTSUpgradeData* MakeUpgrade(URTSEntityData* Target, int32 Number)
	{
		URTSUpgradeData* Upgrade = NewObject<URTSUpgradeData>();
		Upgrade->DisplayName = FText::FromString(FString::Printf(TEXT("Upgrade %d"), Number));
		Upgrade->Multiplier = 1.1f;
		Upgrade->Targets.Add(Target);
		return Upgrade;
	}
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSSelectionUpgradesTest, "WorldDivide.UI.Selection.Upgrades", TestFlags)

bool FRTSSelectionUpgradesTest::RunTest(const FString& Parameters)
{
	using namespace RTSSelectionTests;
	FRTSTestWorld TestWorld;
	TestWorld.SpawnGameState();
	ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 0);
	ARTSPlayerController* Controller = SpawnController(TestWorld, Player);
	URTSUnitData* TankData = NewObject<URTSUnitData>();
	URTSUnitData* OtherData = NewObject<URTSUnitData>();
	ARTSUnit* Tank = TestWorld.SpawnUnit(TankData, 0, FVector::ZeroVector);
	ARTSUnit* Other = TestWorld.SpawnUnit(OtherData, 0, FVector(1000.f, 0.f, 0.f));
	const FSelectionPanel Panel(Controller);
	if (!TestNotNull(TEXT("A source serves the selection panel"), Panel.Source))
	{
		return false;
	}
	const int32 Slots = GetDefault<URTSSettings>()->SelectionUpgradeSlots;
	TestEqual(TEXT("Six icons before the overflow by default"), Slots, 6);

	// An upgrade for another unit never shows on this one.
	Player->CompleteUpgrade(*MakeUpgrade(OtherData, 0));
	Controller->SelectUnits({ Tank });
	for (int32 Installed = 0; Installed <= 9; ++Installed)
	{
		if (Installed > 0)
		{
			// Completed while selected: the panel follows without reselecting.
			Player->CompleteUpgrade(*MakeUpgrade(TankData, Installed));
		}
		const int32 Hidden = FMath::Max(Installed - Slots, 0);
		TestEqual(FString::Printf(TEXT("%d installed: as many icons, up to the slots"), Installed), Panel.Count(TEXT("selection.upgrades")),
			FMath::Min(Installed, Slots));
		TestEqual(FString::Printf(TEXT("%d installed: the row shows only with one"), Installed),
			Panel.Value(TEXT("selection.hasUpgrades")).bBool, Installed > 0);
		TestEqual(FString::Printf(TEXT("%d installed: a +N entry only past the slots"), Installed), Panel.Count(TEXT("selection.upgradeMore")),
			Hidden > 0 ? 1 : 0);
		if (Hidden > 0)
		{
			TestEqual(FString::Printf(TEXT("%d installed: +%d"), Installed, Hidden), Panel.Item(TEXT("selection.upgradeMore"), 0, TEXT("label")).String,
				FString::Printf(TEXT("+%d"), Hidden));
			TArray<FString> Lines;
			Panel.Item(TEXT("selection.upgradeMore"), 0, TEXT("description")).String.ParseIntoArrayLines(Lines);
			TestEqual(FString::Printf(TEXT("%d installed: its tooltip names every hidden one"), Installed), Lines.Num(), Hidden);
			TestTrue(TEXT("the last one last"), Lines.Num() > 0 && Lines.Last().StartsWith(FString::Printf(TEXT("Upgrade %d:"), Installed)));
		}
	}
	TestEqual(TEXT("The icons go in the order completed"), Panel.Item(TEXT("selection.upgrades"), 0, TEXT("name")).String, FString(TEXT("Upgrade 1")));
	TestEqual(TEXT("each installed"), Panel.Item(TEXT("selection.upgrades"), 0, TEXT("status")).String, FString(TEXT("Installed")));
	TestTrue(TEXT("and never disabled yet"), Panel.Item(TEXT("selection.upgrades"), 0, TEXT("disabledText")).String.IsEmpty());
	TestTrue(TEXT("Its tooltip says what it does"),
		Panel.Item(TEXT("selection.upgrades"), 0, TEXT("description")).String.Contains(TEXT("Weapon damage x") + FText::AsNumber(1.1f).ToString()));

	// More slots, from config alone: all nine show and nothing overflows.
	URTSSettings* Settings = GetMutableDefault<URTSSettings>();
	Settings->SelectionUpgradeSlots = 12;
	TestEqual(TEXT("Twelve slots show all nine"), Panel.Count(TEXT("selection.upgrades")), 9);
	TestEqual(TEXT("with no overflow"), Panel.Count(TEXT("selection.upgradeMore")), 0);
	Settings->SelectionUpgradeSlots = Slots;

	// Switching, deselecting and a selection that is gone leave nothing behind.
	Controller->SelectUnits({ Other });
	TestEqual(TEXT("The other unit shows its own one"), Panel.Count(TEXT("selection.upgrades")), 1);
	TestEqual(TEXT("by name"), Panel.Item(TEXT("selection.upgrades"), 0, TEXT("name")).String, FString(TEXT("Upgrade 0")));
	Controller->SelectUnits({ Tank, Other });
	TestEqual(TEXT("Several units show no upgrades"), Panel.Count(TEXT("selection.upgrades")), 0);
	Controller->SelectUnits({});
	TestEqual(TEXT("Nothing selected, no upgrades"), Panel.Count(TEXT("selection.upgrades")), 0);
	TestFalse(TEXT("and no row"), Panel.Value(TEXT("selection.hasUpgrades")).bBool);
	Controller->SelectUnits({ Tank });
	Tank->Destroy();
	TestEqual(TEXT("A destroyed selection shows no upgrades"), Panel.Count(TEXT("selection.upgrades")), 0);
	TestEqual(TEXT("nor an overflow"), Panel.Count(TEXT("selection.upgradeMore")), 0);
	TestEqual(TEXT("nor art"), Panel.Value(TEXT("selection.heroArt")).String, FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSSelectionHeroTest, "WorldDivide.UI.Selection.Hero", TestFlags)

bool FRTSSelectionHeroTest::RunTest(const FString& Parameters)
{
	using namespace RTSSelectionTests;
	FRTSTestWorld TestWorld;
	TestWorld.SpawnGameState();
	ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 0);
	ARTSPlayerController* Controller = SpawnController(TestWorld, Player);
	const FSelectionPanel Panel(Controller);
	if (!TestNotNull(TEXT("A source serves the selection panel"), Panel.Source))
	{
		return false;
	}

	URTSUnitData* Hero = NewObject<URTSUnitData>();
	Hero->HeroArtSprite = TEXT("Art.Hero");
	Hero->IconSprite = TEXT("Icon.Hero");
	Hero->RoleText = FText::FromString(TEXT("Main battle tank"));
	Controller->SelectUnits({ TestWorld.SpawnUnit(Hero, 0, FVector::ZeroVector) });
	TestEqual(TEXT("Hero art shows when the data has it"), Panel.Value(TEXT("selection.heroArt")).String, FString(TEXT("Art.Hero")));
	TestTrue(TEXT("as art"), Panel.Value(TEXT("selection.hasHeroArt")).bBool);
	TestEqual(TEXT("with the role of the data"), Panel.Value(TEXT("selection.role")).String, FString(TEXT("Main battle tank")));
	TestEqual(TEXT("and room for it"), Panel.Value(TEXT("selection.detail")).String, FString(TEXT("None")));

	URTSUnitData* IconOnly = NewObject<URTSUnitData>();
	IconOnly->IconSprite = TEXT("Icon.Plain");
	Controller->SelectUnits({ TestWorld.SpawnUnit(IconOnly, 0, FVector(1000.f, 0.f, 0.f)) });
	TestEqual(TEXT("Without art the icon shows large"), Panel.Value(TEXT("selection.heroArt")).String, FString(TEXT("Icon.Plain")));
	TestFalse(TEXT("not as art"), Panel.Value(TEXT("selection.hasHeroArt")).bBool);
	TestEqual(TEXT("Without a role a unit says so"), Panel.Value(TEXT("selection.role")).String, FString(TEXT("Unit")));

	URTSUnitData* Bare = NewObject<URTSUnitData>();
	Controller->SelectUnits({ TestWorld.SpawnUnit(Bare, 0, FVector(2000.f, 0.f, 0.f)) });
	TestEqual(TEXT("Without art or icon the placeholder shows"), Panel.Value(TEXT("selection.heroArt")).String,
		FString(TEXT("Icon.Placeholder")));

	URTSAircraftData* Jet = NewObject<URTSAircraftData>();
	Controller->SelectUnits({ TestWorld.SpawnUnit(Jet, 0, FVector(3000.f, 0.f, 0.f)) });
	TestEqual(TEXT("An aircraft says so"), Panel.Value(TEXT("selection.role")).String, FString(TEXT("Aircraft")));
	TestEqual(TEXT("with its ammo below"), Panel.Value(TEXT("selection.detail")).String, FString(TEXT("Aircraft")));

	URTSBuildingData* Depot = NewObject<URTSBuildingData>();
	ARTSBuilding* Building = TestWorld.SpawnBuilding(Depot, 0, FVector(5000.f, 0.f, 0.f));
	TestTrue(TEXT("An own building is selectable"), Controller->SelectBuilding(Building));
	TestEqual(TEXT("A building says so"), Panel.Value(TEXT("selection.role")).String, FString(TEXT("Building")));
	TestEqual(TEXT("A site shows its progress below"), Panel.Value(TEXT("selection.detail")).String, FString(TEXT("Construction")));

	Controller->SelectUnits({});
	TestEqual(TEXT("Nothing selected: no art"), Panel.Value(TEXT("selection.heroArt")).String, FString());
	TestEqual(TEXT("and no role"), Panel.Value(TEXT("selection.role")).String, FString());
	return true;
}

#endif
