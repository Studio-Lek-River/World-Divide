#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSProductionComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Commands/RTSCommandDispatch.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "Data/RTSUnitData.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSUnit.h"

#if WITH_DEV_AUTOMATION_TESTS

// Named rather than anonymous: unity builds merge anonymous namespaces, and the power tests have helpers of the same names.
namespace RTSProductionTests
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	constexpr float Step = 0.1f;

	URTSUnitData* MakeUnit(int32 Cost, float BuildTime)
	{
		URTSUnitData* Data = NewObject<URTSUnitData>();
		Data->MaxHealth = 100.f;
		Data->Cost = Cost;
		Data->BuildTime = BuildTime;
		return Data;
	}

	URTSBuildingData* MakeBuilding(int32 PowerOutput = 0, int32 PowerDraw = 0, const TArray<URTSUnitData*>& Units = {})
	{
		URTSBuildingData* Data = NewObject<URTSBuildingData>();
		Data->Footprint = FIntPoint(3, 3);
		Data->MaxHealth = 500.f;
		Data->BuildTime = 2.f;
		Data->PowerOutput = PowerOutput;
		Data->PowerDraw = PowerDraw;
		Data->Units.Append(Units);
		return Data;
	}

	ARTSBuilding* SpawnCompleted(const FRTSTestWorld& TestWorld, URTSBuildingData* Data, int32 TeamId, const FVector& Location)
	{
		ARTSBuilding* Building = TestWorld.SpawnBuilding(Data, TeamId, Location);
		Building->UpdateConstruction(Data->BuildTime);
		return Building;
	}

	void Kill(ARTSBuilding& Building)
	{
		Building.GetHealthComponent()->ApplyDamage(Building.GetHealthComponent()->GetMaxHealth(), nullptr);
	}

	/** A unit Building produced, and how many seconds into the run it came out. */
	struct FProduced
	{
		ARTSUnit* Unit = nullptr;
		float Seconds = 0.f;
	};

	/** Steps Building's production for Seconds and returns the units it produced, in order. */
	TArray<FProduced> RunProduction(UWorld& World, ARTSBuilding& Building, float Seconds)
	{
		TSet<ARTSUnit*> Known;
		for (TActorIterator<ARTSUnit> It(&World); It; ++It)
		{
			Known.Add(*It);
		}
		TArray<FProduced> Produced;
		for (float Time = Step; Time < Seconds + KINDA_SMALL_NUMBER; Time += Step)
		{
			Building.GetProductionComponent()->UpdateProduction(Step);
			for (TActorIterator<ARTSUnit> It(&World); It; ++It)
			{
				if (!Known.Contains(*It))
				{
					Known.Add(*It);
					Produced.Add({ *It, Time });
				}
			}
		}
		return Produced;
	}

	/** Progress Building's front entry makes in one second at the current power. */
	float ProgressPerSecond(ARTSBuilding& Building)
	{
		URTSProductionComponent* Production = Building.GetProductionComponent();
		const float Before = Production->GetProgress();
		for (float Time = 0.f; Time < 1.f - KINDA_SMALL_NUMBER; Time += Step)
		{
			Production->UpdateProduction(Step);
		}
		return Production->GetProgress() - Before;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSProductionQueueTest, "WorldDivide.Production.Queue", TestFlags)

	bool FRTSProductionQueueTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 1000);
		ARTSPlayerState* Enemy = TestWorld.SpawnPlayer(1, 1000);
		URTSUnitData* Rifle = MakeUnit(100, 2.f);
		URTSUnitData* Truck = MakeUnit(300, 3.f);
		URTSUnitData* Foreign = MakeUnit(50, 1.f);
		URTSBuildingData* BarracksData = MakeBuilding(0, 0, { Rifle, Truck });

		ARTSBuilding* Site = TestWorld.SpawnBuilding(BarracksData, 0, FVector(0.f, 3000.f, 0.f));
		TestFalse(TEXT("A construction site cannot produce"), Site->GetProductionComponent()->CanProduce());
		TestFalse(TEXT("A construction site refuses to queue"), Site->GetProductionComponent()->Enqueue(*Player, Rifle));

		ARTSBuilding* Barracks = SpawnCompleted(TestWorld, BarracksData, 0, FVector::ZeroVector);
		URTSProductionComponent* Production = Barracks->GetProductionComponent();
		TestTrue(TEXT("A completed barracks can produce"), Production->CanProduce());
		TestFalse(TEXT("Another team cannot queue at it"), Production->Enqueue(*Enemy, Rifle));
		TestFalse(TEXT("A unit not on its list is refused"), Production->Enqueue(*Player, Foreign));
		TestFalse(TEXT("Nothing is refused"), Production->Enqueue(*Player, nullptr));
		TestEqual(TEXT("Refusals charge nothing"), Player->GetCredits(), 1000);
		TestEqual(TEXT("Refusals charge the other team nothing"), Enemy->GetCredits(), 1000);
		TestEqual(TEXT("Refusals queue nothing"), Production->GetQueue().Num(), 0);

		TestTrue(TEXT("Queue a rifleman"), Production->Enqueue(*Player, Rifle));
		TestEqual(TEXT("Queueing charges the cost"), Player->GetCredits(), 900);
		TestTrue(TEXT("Queue a truck"), Production->Enqueue(*Player, Truck));
		TestTrue(TEXT("Queue a second rifleman"), Production->Enqueue(*Player, Rifle));
		TestEqual(TEXT("Three entries charged"), Player->GetCredits(), 500);

		// Filling up: MaxQueue fit, one more is refused. The extra riflemen are cancelled again.
		Player->AddCredits(1000);
		for (int32 Index = Production->GetQueue().Num(); Index < URTSProductionComponent::MaxQueue; ++Index)
		{
			TestTrue(FString::Printf(TEXT("Queue entry %d"), Index + 1), Production->Enqueue(*Player, Rifle));
		}
		const int32 CreditsWhenFull = Player->GetCredits();
		TestFalse(TEXT("One more does not fit"), Production->Enqueue(*Player, Rifle));
		TestEqual(TEXT("The full queue charged nothing more"), Player->GetCredits(), CreditsWhenFull);
		while (Production->GetQueue().Num() > 3)
		{
			Production->CancelLast(*Player, Rifle);
		}

		Player->AddCredits(50 - Player->GetCredits());
		TestFalse(TEXT("A unit the player cannot afford is refused"), Production->Enqueue(*Player, Truck));
		TestEqual(TEXT("It charges nothing"), Player->GetCredits(), 50);
		TestEqual(TEXT("The queue holds the three entries"), Production->GetQueue().Num(), 3);

		// The rifleman takes 2 s, the truck 3 s, the second rifleman 2 s, each started once the one before is out.
		const TArray<FProduced> Produced = RunProduction(*TestWorld.World, *Barracks, 8.f);
		const TArray<URTSUnitData*> Expected = { Rifle, Truck, Rifle };
		if (TestEqual(TEXT("Three units come out"), Produced.Num(), Expected.Num()))
		{
			const float ExitX = BarracksData->Footprint.X * 50.f;
			for (int32 Index = 0; Index < Produced.Num(); ++Index)
			{
				const ARTSUnit* Unit = Produced[Index].Unit;
				TestTrue(FString::Printf(TEXT("Unit %d is the one queued %d"), Index, Index), Unit->GetUnitData() == Expected[Index]);
				const float Took = Produced[Index].Seconds - (Index > 0 ? Produced[Index - 1].Seconds : 0.f);
				TestTrue(FString::Printf(TEXT("Unit %d comes out after its build time (%.1f s)"), Index, Took),
					FMath::IsNearlyEqual(Took, Expected[Index]->BuildTime, Step * 1.5f));
				TestEqual(FString::Printf(TEXT("Unit %d is the player's"), Index), Unit->GetTeamId(), 0);
				TestTrue(FString::Printf(TEXT("Unit %d comes out beyond the front edge"), Index), Unit->GetActorLocation().X > ExitX);
			}
		}
		TestEqual(TEXT("The queue is empty"), Production->GetQueue().Num(), 0);

		// A destroyed barracks takes no more orders.
		Player->AddCredits(1000);
		Kill(*Barracks);
		FRTSCommand Produce;
		Produce.Type = ERTSCommandType::Produce;
		Produce.TargetActor = Barracks;
		Produce.EntityData = Rifle;
		TestFalse(TEXT("A destroyed barracks is gone"), IsValid(Barracks));
		TestFalse(TEXT("A destroyed barracks refuses to queue"), RTSCommands::Produce(*Player, Produce));
		TestEqual(TEXT("It charges nothing"), Player->GetCredits(), 1050);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSProductionPrerequisitesTest, "WorldDivide.Production.Prerequisites", TestFlags)

	bool FRTSProductionPrerequisitesTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 5000);
		TestWorld.SpawnPlayer(1, 0);
		URTSBuildingData* DepotData = MakeBuilding();
		URTSUnitData* Truck = MakeUnit(300, 3.f);
		Truck->Prerequisites.Add(DepotData);
		URTSUnitData* Rifle = MakeUnit(100, 2.f);
		ARTSBuilding* Factory = SpawnCompleted(TestWorld, MakeBuilding(0, 0, { Truck, Rifle }), 0, FVector::ZeroVector);
		URTSProductionComponent* Production = Factory->GetProductionComponent();

		TestTrue(TEXT("A unit without prerequisites is available"),
			RTSProduction::FindMissingPrerequisite(*TestWorld.World, 0, *Rifle) == nullptr);
		TestTrue(TEXT("Without a depot the depot is missing"), RTSProduction::FindMissingPrerequisite(*TestWorld.World, 0, *Truck) == DepotData);
		TestFalse(TEXT("Without a depot the truck is refused"), Production->Enqueue(*Player, Truck));

		ARTSBuilding* Site = TestWorld.SpawnBuilding(DepotData, 0, FVector(3000.f, 0.f, 0.f));
		TestFalse(TEXT("A depot under construction does not count"), Production->Enqueue(*Player, Truck));
		SpawnCompleted(TestWorld, DepotData, 1, FVector(6000.f, 0.f, 0.f));
		TestFalse(TEXT("The enemy's depot does not count"), Production->Enqueue(*Player, Truck));
		TestEqual(TEXT("Refusals charge nothing"), Player->GetCredits(), 5000);

		Site->UpdateConstruction(DepotData->BuildTime);
		TestTrue(TEXT("Once the depot completes the truck is accepted"), Production->Enqueue(*Player, Truck));

		Kill(*Site);
		TestFalse(TEXT("Once the depot is destroyed the truck is refused again"), Production->Enqueue(*Player, Truck));
		TestTrue(TEXT("Other units are still accepted"), Production->Enqueue(*Player, Rifle));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSProductionIsAvailableTest, "WorldDivide.Production.IsAvailable", TestFlags)

	bool FRTSProductionIsAvailableTest::RunTest(const FString& Parameters)
	{
		URTSBuildingData* Barracks = MakeBuilding();
		URTSBuildingData* Plant = MakeBuilding(5, 0);
		URTSBuildingData* Factory = MakeBuilding();
		Factory->Prerequisites.Add(Barracks);
		Factory->Prerequisites.Add(Plant);
		URTSBuildingData* Depot = MakeBuilding();
		Depot->Prerequisites.Add(nullptr);

		TSet<const URTSEntityData*> Owned;
		TestTrue(TEXT("Nothing needed is available with nothing owned"), RTSProduction::IsAvailable(Owned, *Barracks));
		TestTrue(TEXT("An empty prerequisite slot is ignored"), RTSProduction::IsAvailable(Owned, *Depot));
		TestFalse(TEXT("With nothing owned the factory is unavailable"), RTSProduction::IsAvailable(Owned, *Factory));
		TestTrue(TEXT("The first missing prerequisite is named"), RTSProduction::FindMissingPrerequisite(Owned, *Factory) == Barracks);

		Owned.Add(Barracks);
		TestFalse(TEXT("One of two prerequisites is not enough"), RTSProduction::IsAvailable(Owned, *Factory));
		TestTrue(TEXT("The one still missing is named"), RTSProduction::FindMissingPrerequisite(Owned, *Factory) == Plant);

		Owned.Add(Plant);
		TestTrue(TEXT("With both owned the factory is available"), RTSProduction::IsAvailable(Owned, *Factory));
		TestNull(TEXT("Nothing is missing"), RTSProduction::FindMissingPrerequisite(Owned, *Factory));

		Owned.Remove(Barracks);
		TestFalse(TEXT("Losing one makes it unavailable again"), RTSProduction::IsAvailable(Owned, *Factory));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSProductionCancelTest, "WorldDivide.Production.Cancel", TestFlags)

	bool FRTSProductionCancelTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 1000);
		ARTSPlayerState* Enemy = TestWorld.SpawnPlayer(1, 1000);
		URTSUnitData* Rifle = MakeUnit(100, 4.f);
		URTSUnitData* Truck = MakeUnit(300, 4.f);
		ARTSBuilding* Barracks = SpawnCompleted(TestWorld, MakeBuilding(0, 0, { Rifle, Truck }), 0, FVector::ZeroVector);
		URTSProductionComponent* Production = Barracks->GetProductionComponent();

		Production->Enqueue(*Player, Rifle);
		Production->Enqueue(*Player, Rifle);
		Production->Enqueue(*Player, Truck);
		TestEqual(TEXT("Three entries charged"), Player->GetCredits(), 500);
		RunProduction(*TestWorld.World, *Barracks, 1.f);
		TestTrue(TEXT("The front rifleman is a quarter done"), FMath::IsNearlyEqual(Production->GetProgress(), 0.25f, 0.01f));

		TestTrue(TEXT("Cancel a rifleman"), Production->CancelLast(*Player, Rifle));
		TestEqual(TEXT("Its cost is refunded"), Player->GetCredits(), 600);
		TestEqual(TEXT("The last rifleman left the queue"), Production->GetQueue().Num(), 2);
		TestTrue(TEXT("The front rifleman keeps its progress"), FMath::IsNearlyEqual(Production->GetProgress(), 0.25f, 0.01f));

		TestTrue(TEXT("Cancel the other rifleman"), Production->CancelLast(*Player, Rifle));
		TestEqual(TEXT("Its cost is refunded in full, progress or not"), Player->GetCredits(), 700);
		TestEqual(TEXT("Cancelling the front entry resets progress"), Production->GetProgress(), 0.f);
		TestTrue(TEXT("The truck moved to the front"), Production->GetQueue().Num() == 1 && Production->GetQueue()[0] == Truck);

		TestFalse(TEXT("No rifleman is left to cancel"), Production->CancelLast(*Player, Rifle));
		TestFalse(TEXT("Another team cannot cancel the truck"), Production->CancelLast(*Enemy, Truck));
		TestEqual(TEXT("Refused cancels refund nothing"), Player->GetCredits(), 700);
		TestEqual(TEXT("Nor to the other team"), Enemy->GetCredits(), 1000);

		TestTrue(TEXT("Cancel the truck"), Production->CancelLast(*Player, Truck));
		TestEqual(TEXT("Everything is refunded"), Player->GetCredits(), 1000);
		TestEqual(TEXT("The queue is empty"), Production->GetQueue().Num(), 0);
		TestEqual(TEXT("Nothing was produced"), RunProduction(*TestWorld.World, *Barracks, 5.f).Num(), 0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSProductionCancelAtTest, "WorldDivide.Production.CancelAt", TestFlags)

	bool FRTSProductionCancelAtTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 1000);
		ARTSPlayerState* Enemy = TestWorld.SpawnPlayer(1, 1000);
		URTSUnitData* Tank = MakeUnit(200, 4.f);
		URTSUnitData* Rifle = MakeUnit(100, 4.f);
		ARTSBuilding* Barracks = SpawnCompleted(TestWorld, MakeBuilding(0, 0, { Tank, Rifle }), 0, FVector::ZeroVector);
		URTSProductionComponent* Production = Barracks->GetProductionComponent();

		// Tank, rifleman, tank: the clicked entry goes, not the last of its kind.
		Production->Enqueue(*Player, Tank);
		Production->Enqueue(*Player, Rifle);
		Production->Enqueue(*Player, Tank);
		TestEqual(TEXT("Three entries charged"), Player->GetCredits(), 500);
		RunProduction(*TestWorld.World, *Barracks, 1.f);

		TestFalse(TEXT("Another team cannot cancel the front tank"), Production->CancelAt(*Enemy, 0, Tank));
		TestFalse(TEXT("An entry of another kind is not cancelled"), Production->CancelAt(*Player, 1, Tank));
		TestFalse(TEXT("Nor one past the queue"), Production->CancelAt(*Player, 3, Tank));
		TestEqual(TEXT("Refused cancels leave the queue"), Production->GetQueue().Num(), 3);
		TestEqual(TEXT("and refund nothing"), Player->GetCredits(), 500);

		TestTrue(TEXT("Cancel the front tank"), Production->CancelAt(*Player, 0, Tank));
		TestEqual(TEXT("Its cost is refunded"), Player->GetCredits(), 700);
		TestEqual(TEXT("Cancelling the front entry resets progress"), Production->GetProgress(), 0.f);
		TestTrue(TEXT("The rifleman and the last tank stay, in order"),
			Production->GetQueue().Num() == 2 && Production->GetQueue()[0] == Rifle && Production->GetQueue()[1] == Tank);

		// A click on slot 1 that arrives after the queue moved on (it now holds the tank at 1, but the click meant the rifleman).
		TestFalse(TEXT("A stale index cancels nothing"), Production->CancelAt(*Player, 1, Rifle));
		TestEqual(TEXT("The queue is unchanged"), Production->GetQueue().Num(), 2);

		// Through the command: with an index exactly that entry, without one the last of its kind.
		RunProduction(*TestWorld.World, *Barracks, 1.f);
		FRTSCommand Command;
		Command.Type = ERTSCommandType::CancelProduction;
		Command.TargetActor = Barracks;
		Command.EntityData = Rifle;
		Command.QueueIndex = 1;
		TestFalse(TEXT("The command refuses a stale index"), RTSCommands::CancelProduction(*Player, Command));
		Command.EntityData = Tank;
		TestTrue(TEXT("The command cancels the waiting tank"), RTSCommands::CancelProduction(*Player, Command));
		TestTrue(TEXT("The rifleman keeps its progress"), Production->GetProgress() > 0.f);
		Command.EntityData = Rifle;
		Command.QueueIndex = INDEX_NONE;
		TestTrue(TEXT("Without an index the last rifleman goes"), RTSCommands::CancelProduction(*Player, Command));
		TestEqual(TEXT("Everything is refunded"), Player->GetCredits(), 1000);
		TestEqual(TEXT("The queue is empty"), Production->GetQueue().Num(), 0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSProductionLowPowerTest, "WorldDivide.Production.LowPower", TestFlags)

	bool FRTSProductionLowPowerTest::RunTest(const FString& Parameters)
	{
		constexpr float BuildTime = 10.f;

		const FRTSTestWorld TestWorld;
		ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 1000);
		URTSFactionData* Faction = NewObject<URTSFactionData>();
		Faction->bUsesPower = true;
		Player->SetFaction(Faction);

		URTSUnitData* Rifle = MakeUnit(100, BuildTime);
		URTSBuildingData* PlantData = MakeBuilding(5, 0);
		ARTSBuilding* Plant = SpawnCompleted(TestWorld, PlantData, 0, FVector(0.f, -3000.f, 0.f));
		ARTSBuilding* Barracks = SpawnCompleted(TestWorld, MakeBuilding(0, 2, { Rifle }), 0, FVector::ZeroVector);
		Barracks->GetProductionComponent()->Enqueue(*Player, Rifle);

		TestFalse(TEXT("The plant covers the barracks"), Player->IsLowPower());
		TestTrue(TEXT("Full power produces at the build time"), FMath::IsNearlyEqual(ProgressPerSecond(*Barracks), 1.f / BuildTime, 0.005f));

		Kill(*Plant);
		TestTrue(TEXT("Without the plant the player is low on power"), Player->IsLowPower());
		const float LowProgress = ProgressPerSecond(*Barracks);
		TestTrue(FString::Printf(TEXT("Low power produces at two thirds of the speed (%.3f per second)"), LowProgress),
			FMath::IsNearlyEqual(LowProgress, ARTSPlayerState::LowPowerProductionRate / BuildTime, 0.005f));

		ARTSBuilding* NewPlant = TestWorld.SpawnBuilding(PlantData, 0, FVector(0.f, -3000.f, 0.f));
		NewPlant->UpdateConstruction(PlantData->BuildTime * 2.f);
		TestFalse(TEXT("A new plant restores power"), Player->IsLowPower());
		TestTrue(TEXT("Production is back to full speed"), FMath::IsNearlyEqual(ProgressPerSecond(*Barracks), 1.f / BuildTime, 0.005f));
		return true;
	}
}

#endif
