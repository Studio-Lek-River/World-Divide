#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuilderData.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSUpgradeData.h"
#include "Misc/AutomationTest.h"
#include "Player/RTSCommandButton.h"
#include "Player/RTSPlayerController.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSUnit.h"

#if WITH_DEV_AUTOMATION_TESTS

// Named rather than anonymous: unity builds merge anonymous namespaces.
namespace RTSCommandPanelTests
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	template <typename T>
	T* MakeData(const TCHAR* Name, int32 Cost, const FKey& Hotkey = FKey())
	{
		T* Data = NewObject<T>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), T::StaticClass(), Name));
		Data->DisplayName = FText::FromString(Name);
		Data->MaxHealth = 100.f;
		Data->Cost = Cost;
		Data->BuildTime = 1.f;
		Data->Hotkey = Hotkey;
		return Data;
	}

	URTSBuildingData* MakeBuilding(const TCHAR* Name, int32 Cost, const FKey& Hotkey = FKey())
	{
		URTSBuildingData* Data = MakeData<URTSBuildingData>(Name, Cost, Hotkey);
		Data->Footprint = FIntPoint(3, 3);
		return Data;
	}

	ARTSBuilding* SpawnCompleted(const FRTSTestWorld& TestWorld, URTSBuildingData* Data, int32 TeamId, const FVector& Location)
	{
		ARTSBuilding* Building = TestWorld.SpawnBuilding(Data, TeamId, Location);
		Building->UpdateConstruction(Data->BuildTime);
		return Building;
	}

	/** A local controller for Player, without input or a HUD. */
	ARTSPlayerController* SpawnController(const FRTSTestWorld& TestWorld, ARTSPlayerState* Player)
	{
		ARTSPlayerController* Controller = TestWorld.World->SpawnActor<ARTSPlayerController>();
		Controller->PlayerState = Player;
		return Controller;
	}

	const FRTSCommandButton* Find(const TArray<FRTSCommandButton>& Buttons, FName Id)
	{
		return Buttons.FindByPredicate([Id](const FRTSCommandButton& Button) { return Button.Id == Id; });
	}

	FRTSCommandButton MakeButton(const TCHAR* Id, const FKey& Hotkey, ERTSCommandCategory Category = ERTSCommandCategory::Produce)
	{
		FRTSCommandButton Button;
		Button.Id = Id;
		Button.Hotkey = Hotkey;
		Button.Category = Category;
		return Button;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCommandPanelBuilderTest, "WorldDivide.UI.Commands.Builder", TestFlags)

	bool FRTSCommandPanelBuilderTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		TestWorld.SpawnGameState();
		ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 1000);
		ARTSPlayerController* Controller = SpawnController(TestWorld, Player);

		URTSBuildingData* Plant = MakeBuilding(TEXT("Plant"), 200, EKeys::P);
		URTSBuildingData* Depot = MakeBuilding(TEXT("Depot"), 200);
		Depot->MaxCount = 1;
		URTSBuildingData* Factory = MakeBuilding(TEXT("Factory"), 300);
		Factory->Prerequisites.Add(Plant);
		URTSBuildingData* Fortress = MakeBuilding(TEXT("Fortress"), 5000);
		URTSBuilderData* BuilderData = MakeData<URTSBuilderData>(TEXT("Builder"), 100);
		BuilderData->Buildings = { Plant, Depot, Factory, Fortress };

		SpawnCompleted(TestWorld, Depot, 0, FVector(3000.f, 0.f, 0.f));
		ARTSUnit* Builder = TestWorld.SpawnUnit(BuilderData, 0, FVector::ZeroVector);
		TestTrue(TEXT("Nothing selected, no commands"), Controller->GetCommandButtons().IsEmpty());
		Controller->SelectUnits({ Builder });

		const TArray<FRTSCommandButton> Buttons = Controller->GetCommandButtons();
		if (!TestEqual(TEXT("The builder's four buildings, then Stop and Attack-move"), Buttons.Num(), 6))
		{
			return false;
		}
		for (int32 Index = 0; Index < 4; ++Index)
		{
			TestTrue(FString::Printf(TEXT("Button %d is a building"), Index), Buttons[Index].Category == ERTSCommandCategory::Build);
			TestTrue(FString::Printf(TEXT("Button %d is the builder's building %d"), Index, Index),
				Buttons[Index].EntityData.Get() == BuilderData->Buildings[Index].Get());
		}
		TestTrue(TEXT("The plant is available"), Buttons[0].bEnabled);
		TestTrue(TEXT("with its own hotkey"), Buttons[0].Hotkey == EKeys::P);
		TestEqual(TEXT("and its cost"), Buttons[0].Cost, 200);
		TestTrue(TEXT("The depot's limit is reached"), Buttons[1].DisabledReason == ERTSCommandDisabledReason::BuildLimit);
		TestFalse(TEXT("so it is disabled"), Buttons[1].bEnabled);
		TestTrue(TEXT("and keeps its position key"), Buttons[1].Hotkey == EKeys::Two);
		TestTrue(TEXT("The factory needs the plant"), Buttons[2].DisabledReason == ERTSCommandDisabledReason::MissingPrerequisite);
		TestEqual(TEXT("which it names"), Buttons[2].DisabledText.ToString(), FString(TEXT("Needs Plant")));
		TestTrue(TEXT("The fortress is too dear"), Buttons[3].DisabledReason == ERTSCommandDisabledReason::CannotAfford);
		TestEqual(TEXT("Stop comes after the buildings"), Buttons[4].Id, RTSCommandPanel::StopId);
		TestEqual(TEXT("then Attack-move"), Buttons[5].Id, RTSCommandPanel::AttackMoveId);

		TestFalse(TEXT("A disabled building is not placed"), Controller->ExecuteCommandButton(Buttons[3]));
		TestNull(TEXT("no ghost"), Controller->GetPlacementData());
		TestTrue(TEXT("An available building is placed"), Controller->ExecuteCommandButton(Buttons[0]));
		TestTrue(TEXT("the plant's ghost"), Controller->GetPlacementData() == Plant);
		TestTrue(TEXT("Its button shows it armed"), Controller->GetCommandButtons()[0].bActive);
		Controller->CancelPlacement();
		TestTrue(TEXT("Its own hotkey places it too"), Controller->HandleCommandKey(EKeys::P));
		TestTrue(TEXT("the plant's ghost again"), Controller->GetPlacementData() == Plant);
		Controller->CancelPlacement();
		TestFalse(TEXT("A key no button has does nothing"), Controller->HandleCommandKey(EKeys::Nine));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCommandPanelProductionTest, "WorldDivide.UI.Commands.ProductionBuilding", TestFlags)

	bool FRTSCommandPanelProductionTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		TestWorld.SpawnGameState();
		ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 2000);
		ARTSPlayerController* Controller = SpawnController(TestWorld, Player);

		URTSBuildingData* Range = MakeBuilding(TEXT("Range"), 300);
		URTSUnitData* Rifle = MakeData<URTSUnitData>(TEXT("Rifle"), 100, EKeys::I);
		URTSUnitData* Grenadier = MakeData<URTSUnitData>(TEXT("Grenadier"), 150);
		Grenadier->Prerequisites.Add(Range);
		URTSUnitData* Commando = MakeData<URTSUnitData>(TEXT("Commando"), 5000);
		URTSUpgradeData* Training = MakeData<URTSUpgradeData>(TEXT("Training"), 400);
		URTSUpgradeData* Armour = MakeData<URTSUpgradeData>(TEXT("Armour"), 400);
		URTSBuildingData* BarracksData = MakeBuilding(TEXT("Barracks"), 500);
		BarracksData->Units = { Rifle, Grenadier, Commando };
		BarracksData->Upgrades = { Training, Armour };

		ARTSBuilding* Barracks = SpawnCompleted(TestWorld, BarracksData, 0, FVector::ZeroVector);
		Player->CompleteUpgrade(*Armour);
		if (!TestTrue(TEXT("The barracks is selected"), Controller->SelectBuilding(Barracks)))
		{
			return false;
		}

		TArray<FRTSCommandButton> Buttons = Controller->GetCommandButtons();
		if (!TestEqual(TEXT("Three units and two upgrades, no unit orders"), Buttons.Num(), 5))
		{
			return false;
		}
		TestTrue(TEXT("The rifle is produced"), Buttons[0].Category == ERTSCommandCategory::Produce);
		TestTrue(TEXT("and available"), Buttons[0].bEnabled);
		TestTrue(TEXT("with its own hotkey"), Buttons[0].Hotkey == EKeys::I);
		TestTrue(TEXT("The grenadier needs the range"), Buttons[1].DisabledReason == ERTSCommandDisabledReason::MissingPrerequisite);
		TestEqual(TEXT("which it names"), Buttons[1].DisabledText.ToString(), FString(TEXT("Needs Range")));
		TestTrue(TEXT("The commando is too dear"), Buttons[2].DisabledReason == ERTSCommandDisabledReason::CannotAfford);
		TestTrue(TEXT("Training is an upgrade"), Buttons[3].Category == ERTSCommandCategory::Upgrade);
		TestTrue(TEXT("still to research"), Buttons[3].bEnabled && !Buttons[3].bDone);
		TestTrue(TEXT("with its position key"), Buttons[3].Hotkey == EKeys::Four);
		TestTrue(TEXT("Armour is researched"), Buttons[4].bDone);
		TestFalse(TEXT("so it cannot be researched again"), Buttons[4].bEnabled);

		TestTrue(TEXT("The rifle's button queues it"), Controller->ExecuteCommandButton(Buttons[0]));
		URTSProductionComponent* Production = Barracks->GetProductionComponent();
		TestEqual(TEXT("one rifle queued"), Production->GetQueue().Num(), 1);
		TestTrue(TEXT("Its own hotkey queues another"), Controller->HandleCommandKey(EKeys::I));
		TestEqual(TEXT("two rifles queued"), Production->GetQueue().Num(), 2);
		TestTrue(TEXT("Training's position key researches it"), Controller->HandleCommandKey(EKeys::Four));
		Buttons = Controller->GetCommandButtons();
		TestFalse(TEXT("Training is not offered twice while researching"), Buttons[3].bEnabled);
		TestFalse(TEXT("and is not done yet"), Buttons[3].bDone);

		while (Production->GetQueue().Num() < URTSProductionComponent::MaxQueue)
		{
			Player->AddCredits(Rifle->Cost);
			Production->Enqueue(*Player, Rifle);
		}
		Buttons = Controller->GetCommandButtons();
		TestTrue(TEXT("A full queue disables the rifle"), Buttons[0].DisabledReason == ERTSCommandDisabledReason::QueueFull);
		TestFalse(TEXT("The disabled rifle is not queued"), Controller->ExecuteCommandButton(Buttons[0]));
		TestEqual(TEXT("the queue stays full"), Production->GetQueue().Num(), URTSProductionComponent::MaxQueue);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCommandPanelUnitsTest, "WorldDivide.UI.Commands.Units", TestFlags)

	bool FRTSCommandPanelUnitsTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		TestWorld.SpawnGameState();
		ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 1000);
		ARTSPlayerController* Controller = SpawnController(TestWorld, Player);
		URTSUnitData* TankData = MakeData<URTSUnitData>(TEXT("Tank"), 600);
		Controller->SelectUnits({ TestWorld.SpawnUnit(TankData, 0, FVector::ZeroVector), TestWorld.SpawnUnit(TankData, 0, FVector(300.f, 0.f, 0.f)) });

		TArray<FRTSCommandButton> Buttons = Controller->GetCommandButtons();
		if (!TestEqual(TEXT("Units get Stop and Attack-move only"), Buttons.Num(), 2))
		{
			return false;
		}
		TestEqual(TEXT("Stop"), Buttons[0].Id, RTSCommandPanel::StopId);
		TestTrue(TEXT("on X"), Buttons[0].Hotkey == EKeys::X);
		TestEqual(TEXT("Attack-move"), Buttons[1].Id, RTSCommandPanel::AttackMoveId);
		TestTrue(TEXT("on J"), Buttons[1].Hotkey == EKeys::J);
		for (const FRTSCommandButton& Button : Buttons)
		{
			TestTrue(TEXT("A unit order"), Button.Category == ERTSCommandCategory::Unit);
			TestTrue(TEXT("is free and available"), Button.bEnabled && Button.Cost == 0);
		}
		TestFalse(TEXT("Attack-move is not armed yet"), Buttons[1].bActive);
		TestFalse(TEXT("Unit orders are not looked up by key: X and J have their own input actions"), Controller->HandleCommandKey(EKeys::J));
		TestTrue(TEXT("The Attack-move button arms it"), Controller->ExecuteCommandButton(Buttons[1]));
		TestTrue(TEXT("armed"), Controller->IsAttackMoveArmed());
		TestTrue(TEXT("and its button shows it"), Find(Controller->GetCommandButtons(), RTSCommandPanel::AttackMoveId)->bActive);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCommandPanelHotkeysTest, "WorldDivide.UI.Commands.Hotkeys", TestFlags)

	bool FRTSCommandPanelHotkeysTest::RunTest(const FString& Parameters)
	{
		TArray<FRTSCommandButton> Buttons = {
			MakeButton(TEXT("Own"), EKeys::T),
			MakeButton(TEXT("Unset"), FKey()),
			MakeButton(TEXT("Duplicate"), EKeys::T),
			MakeButton(TEXT("Reserved"), EKeys::W),
			MakeButton(TEXT("Second"), EKeys::G),
			MakeButton(TEXT("Stop"), EKeys::X, ERTSCommandCategory::Unit),
		};
		TArray<FString> Warnings;
		RTSCommandPanel::ResolveHotkeys(Buttons, { EKeys::W, EKeys::X }, Warnings);
		TestTrue(TEXT("An own hotkey is kept"), Buttons[0].Hotkey == EKeys::T);
		TestTrue(TEXT("No hotkey gives the position key"), Buttons[1].Hotkey == EKeys::Two);
		TestTrue(TEXT("A duplicate falls back to its position key"), Buttons[2].Hotkey == EKeys::Three);
		TestTrue(TEXT("A reserved key falls back to its position key"), Buttons[3].Hotkey == EKeys::Four);
		TestTrue(TEXT("A later own hotkey is kept"), Buttons[4].Hotkey == EKeys::G);
		TestTrue(TEXT("A unit order keeps its reserved key"), Buttons[5].Hotkey == EKeys::X);
		if (TestEqual(TEXT("The duplicate and the reserved key warn"), Warnings.Num(), 2))
		{
			TestTrue(TEXT("naming the duplicate"), Warnings[0].Contains(TEXT("Duplicate")) && Warnings[0].Contains(TEXT("taken")));
			TestTrue(TEXT("and the reserved one"), Warnings[1].Contains(TEXT("Reserved")) && Warnings[1].Contains(TEXT("reserved")));
		}

		// Past the ninth button there is no position key left.
		TArray<FRTSCommandButton> Many;
		for (int32 Index = 0; Index < 11; ++Index)
		{
			Many.Add(MakeButton(*FString::Printf(TEXT("Item%d"), Index), FKey()));
		}
		Many[0].Hotkey = EKeys::Nine;
		Warnings.Reset();
		RTSCommandPanel::ResolveHotkeys(Many, {}, Warnings);
		TestTrue(TEXT("An own digit is kept"), Many[0].Hotkey == EKeys::Nine);
		TestTrue(TEXT("The eighth button gets 8"), Many[7].Hotkey == EKeys::Eight);
		TestFalse(TEXT("The ninth button's 9 is taken, so it has none"), Many[8].Hotkey.IsValid());
		TestFalse(TEXT("The tenth has no position key"), Many[9].Hotkey.IsValid());
		TestTrue(TEXT("Nothing clashed with an own key"), Warnings.IsEmpty());
		return true;
	}
}

#endif
