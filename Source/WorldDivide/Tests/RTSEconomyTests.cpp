#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Commands/RTSCommandDispatch.h"
#include "Core/RTSPlayerState.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSHarvesterData.h"
#include "Economy/RTSHarvesterComponent.h"
#include "Economy/RTSSupplySource.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSUnit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	URTSHarvesterData* MakeHarvesterData(int32 CarryAmount, float GatherSeconds, float SearchRadius)
	{
		URTSHarvesterData* Data = NewObject<URTSHarvesterData>();
		Data->MaxHealth = 100.f;
		Data->SelectionRadius = 70.f;
		Data->CarryAmount = CarryAmount;
		Data->GatherSeconds = GatherSeconds;
		Data->SearchRadius = SearchRadius;
		return Data;
	}

	URTSBuildingData* MakeDepotData(URTSUnitData* SpawnOnComplete)
	{
		URTSBuildingData* Data = NewObject<URTSBuildingData>();
		Data->Footprint = FIntPoint(2, 2);
		Data->MaxHealth = 500.f;
		Data->BuildTime = 2.f;
		Data->SpawnOnComplete = SpawnOnComplete;
		Data->bAcceptsSupply = true;
		return Data;
	}

	TArray<ARTSUnit*> GetUnits(UWorld* World)
	{
		TArray<ARTSUnit*> Units;
		for (TActorIterator<ARTSUnit> It(World); It; ++It)
		{
			Units.Add(*It);
		}
		return Units;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCreditsTest, "WorldDivide.Economy.Credits", TestFlags)

bool FRTSCreditsTest::RunTest(const FString& Parameters)
{
	const FRTSTestWorld TestWorld;
	ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 1000);
	ARTSPlayerState* Other = TestWorld.SpawnPlayer(1, 0);

	TestEqual(TEXT("Starts with the given credits"), Player->GetCredits(), 1000);
	TestTrue(TEXT("Spending what the player has succeeds"), Player->TrySpendCredits(600));
	TestEqual(TEXT("Spending lowers the credits"), Player->GetCredits(), 400);
	TestFalse(TEXT("Spending more than the player has fails"), Player->TrySpendCredits(401));
	TestEqual(TEXT("A failed spend changes nothing"), Player->GetCredits(), 400);
	TestFalse(TEXT("A negative spend is refused"), Player->TrySpendCredits(-100));
	TestTrue(TEXT("Spending exactly everything succeeds"), Player->TrySpendCredits(400));
	TestEqual(TEXT("Nothing left"), Player->GetCredits(), 0);

	Player->AddCredits(250);
	TestEqual(TEXT("Deposits add up"), Player->GetCredits(), 250);
	Player->AddCredits(-1000);
	TestEqual(TEXT("Credits never drop below zero"), Player->GetCredits(), 0);

	TestTrue(TEXT("Each team finds its own player"),
		ARTSPlayerState::FindForTeam(*TestWorld.World, 0) == Player && ARTSPlayerState::FindForTeam(*TestWorld.World, 1) == Other);
	TestNull(TEXT("A team without a player finds nobody"), ARTSPlayerState::FindForTeam(*TestWorld.World, 2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSSupplySourceTest, "WorldDivide.Economy.SupplySource", TestFlags)

bool FRTSSupplySourceTest::RunTest(const FString& Parameters)
{
	const FRTSTestWorld TestWorld;
	ARTSSupplySource* Source = TestWorld.SpawnSupplySource(250, FIntPoint(3, 3), FVector::ZeroVector);

	TestEqual(TEXT("Starts full"), Source->GetRemaining(), 250);
	TestEqual(TEXT("A full load is taken"), Source->Take(100), 100);
	TestEqual(TEXT("A second full load is taken"), Source->Take(100), 100);
	TestFalse(TEXT("Not empty yet"), Source->IsDepleted());
	TestEqual(TEXT("The last load is what is left"), Source->Take(100), 50);
	TestTrue(TEXT("Empty"), Source->IsDepleted());
	TestEqual(TEXT("An empty source gives nothing"), Source->Take(100), 0);

	// Footprint edges lie 150 cm from the centre.
	TestEqual(TEXT("Closest point lies on the footprint edge"),
		Source->GetClosestFootprintPoint(FVector(500.f, 20.f, 0.f)), FVector(150.f, 20.f, 0.f));

	ARTSSupplySource* Near = TestWorld.SpawnSupplySource(500, FIntPoint(3, 3), FVector(1000.f, 0.f, 0.f));
	ARTSSupplySource* Far = TestWorld.SpawnSupplySource(500, FIntPoint(3, 3), FVector(4000.f, 0.f, 0.f));
	const FVector From(-200.f, 0.f, 0.f);
	TestTrue(TEXT("The empty source is skipped for the nearest one with supplies"),
		ARTSSupplySource::FindNearest(*TestWorld.World, From, 10000.f) == Near);
	TestNull(TEXT("Nothing beyond the search distance"), ARTSSupplySource::FindNearest(*TestWorld.World, From, 1000.f));
	const TWeakObjectPtr<ARTSSupplySource> Skipped[] = { Near };
	TestTrue(TEXT("An excluded source is skipped for the next nearest"),
		ARTSSupplySource::FindNearest(*TestWorld.World, From, 10000.f, Skipped) == Far);
	Near->Take(500);
	TestTrue(TEXT("Once the near one is empty the far one is next"),
		ARTSSupplySource::FindNearest(*TestWorld.World, From, 10000.f) == Far);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSDepotSpawnsHarvesterTest, "WorldDivide.Economy.DepotSpawnsHarvester", TestFlags)

bool FRTSDepotSpawnsHarvesterTest::RunTest(const FString& Parameters)
{
	URTSHarvesterData* Harvester = MakeHarvesterData(100, 1.f, 5000.f);
	URTSBuildingData* DepotData = MakeDepotData(Harvester);

	const FRTSTestWorld TestWorld;
	ARTSBuilding* Depot = TestWorld.SpawnBuilding(DepotData, 1, FVector::ZeroVector);
	ARTSBuilding* EnemyDepot = TestWorld.SpawnBuilding(MakeDepotData(nullptr), 0, FVector(3000.f, 0.f, 0.f));
	EnemyDepot->UpdateConstruction(DepotData->BuildTime);

	TestEqual(TEXT("Nothing spawns while under construction"), GetUnits(TestWorld.World).Num(), 0);
	TestNull(TEXT("An unfinished depot takes no deliveries"),
		URTSHarvesterComponent::FindNearestDepot(*TestWorld.World, 1, FVector::ZeroVector));

	Depot->UpdateConstruction(DepotData->BuildTime);
	const TArray<ARTSUnit*> Units = GetUnits(TestWorld.World);
	if (!TestEqual(TEXT("Completion spawns one unit"), Units.Num(), 1))
	{
		return true;
	}
	const ARTSUnit* Spawned = Units[0];
	TestTrue(TEXT("It is the depot's harvester"), Spawned->GetUnitData() == Harvester);
	TestEqual(TEXT("It belongs to the depot's team"), Spawned->GetTeamId(), 1);
	TestTrue(TEXT("It can harvest"), Spawned->GetHarvesterComponent()->CanHarvest());
	TestTrue(FString::Printf(TEXT("It stands outside the footprint (%s)"), *Spawned->GetActorLocation().ToCompactString()),
		Spawned->GetActorLocation().X - Spawned->GetSelectionRadius() > 100.f);

	Depot->UpdateConstruction(1.f);
	TestEqual(TEXT("A completed depot spawns nothing more"), GetUnits(TestWorld.World).Num(), 1);
	TestTrue(TEXT("The completed depot takes its team's deliveries"),
		URTSHarvesterComponent::FindNearestDepot(*TestWorld.World, 1, FVector(2500.f, 0.f, 0.f)) == Depot);

	ARTSBuilding* FarDepot = TestWorld.SpawnBuilding(MakeDepotData(nullptr), 1, FVector(-3000.f, 0.f, 0.f));
	FarDepot->UpdateConstruction(DepotData->BuildTime);
	const TWeakObjectPtr<ARTSBuilding> Skipped[] = { Depot };
	TestTrue(TEXT("An excluded depot is skipped for the next nearest of the team"),
		URTSHarvesterComponent::FindNearestDepot(*TestWorld.World, 1, FVector(2500.f, 0.f, 0.f), Skipped) == FarDepot);
	const TWeakObjectPtr<ARTSBuilding> SkippedBoth[] = { Depot, FarDepot };
	TestNull(TEXT("With every own depot excluded there is none, even with an enemy depot nearby"),
		URTSHarvesterComponent::FindNearestDepot(*TestWorld.World, 1, FVector(2500.f, 0.f, 0.f), SkippedBoth));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSHarvestCycleTest, "WorldDivide.Economy.HarvestCycle", TestFlags)

bool FRTSHarvestCycleTest::RunTest(const FString& Parameters)
{
	constexpr int32 CarryAmount = 100;
	constexpr float GatherSeconds = 2.f;
	constexpr float Step = 0.1f;

	const FRTSTestWorld TestWorld;
	ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 0);
	ARTSPlayerState* Enemy = TestWorld.SpawnPlayer(1, 0);

	// The harvester stands between a 2x2 depot and a 3x3 source, docked at both, so no movement is needed.
	ARTSBuilding* Depot = TestWorld.SpawnBuilding(MakeDepotData(nullptr), 0, FVector::ZeroVector);
	Depot->UpdateConstruction(Depot->GetBuildingData()->BuildTime);
	ARTSSupplySource* Source = TestWorld.SpawnSupplySource(250, FIntPoint(3, 3), FVector(450.f, 0.f, 0.f));
	// In reach but out of dock range, and one beyond the search radius.
	ARTSSupplySource* Next = TestWorld.SpawnSupplySource(500, FIntPoint(3, 3), FVector(200.f, 1500.f, 0.f));
	ARTSSupplySource* OutOfReach = TestWorld.SpawnSupplySource(500, FIntPoint(3, 3), FVector(200.f, -9000.f, 0.f));
	ARTSUnit* Unit = TestWorld.SpawnUnit(MakeHarvesterData(CarryAmount, GatherSeconds, 3000.f), 0, FVector(200.f, 0.f, 0.f));
	URTSHarvesterComponent* Harvester = Unit->GetHarvesterComponent();

	TestTrue(TEXT("The run starts at the nearest source"), Harvester->StartHarvest(nullptr) && Harvester->GetSource() == Source);

	// Records the time of every delivery.
	TArray<float> DepositTimes;
	TArray<int32> Deposits;
	int32 LastCredits = Player->GetCredits();
	float Elapsed = 0.f;
	while (Harvester->GetSource() == Source && Elapsed < 30.f)
	{
		Harvester->UpdateHarvest(Step);
		Elapsed += Step;
		if (Player->GetCredits() != LastCredits)
		{
			DepositTimes.Add(Elapsed);
			Deposits.Add(Player->GetCredits() - LastCredits);
			LastCredits = Player->GetCredits();
		}
	}

	if (!TestEqual(TEXT("Three deliveries emptied the source"), Deposits.Num(), 3))
	{
		return true;
	}
	TestEqual(TEXT("First delivery is a full load"), Deposits[0], CarryAmount);
	TestEqual(TEXT("Second delivery is a full load"), Deposits[1], CarryAmount);
	TestEqual(TEXT("The last delivery is what was left"), Deposits[2], 50);
	TestEqual(TEXT("Credits rose by everything the source held"), Player->GetCredits(), 250);
	TestEqual(TEXT("Nothing went to the other team"), Enemy->GetCredits(), 0);
	// Docked at both ends, a cycle is the gather plus a few ticks of switching between source and depot.
	const float FirstCycle = DepositTimes[1] - DepositTimes[0];
	const float SecondCycle = DepositTimes[2] - DepositTimes[1];
	TestTrue(FString::Printf(TEXT("Steady cycles (%.2f s, %.2f s)"), FirstCycle, SecondCycle),
		FMath::IsNearlyEqual(FirstCycle, SecondCycle, Step * 0.5f));
	TestTrue(FString::Printf(TEXT("A cycle takes about one gather (%.2f s for %.2f s)"), FirstCycle, GatherSeconds),
		FirstCycle >= GatherSeconds - Step * 0.5f && FirstCycle <= GatherSeconds + Step * 3.5f);
	TestTrue(TEXT("The source is empty"), Source->IsDepleted());

	TestTrue(TEXT("The harvester moved on to the other source in reach"), Harvester->GetSource() == Next);
	TestEqual(TEXT("It heads there"), Harvester->GetState(), ERTSHarvestState::ToSource);
	TestEqual(TEXT("Nothing left on board"), Harvester->GetCarried(), 0);

	// Once that one is gone too, only a source beyond the search radius remains: the run ends.
	Next->Take(500);
	TestFalse(TEXT("The run ends with no source in reach"), Harvester->UpdateHarvest(Step));
	TestEqual(TEXT("The harvester is idle"), Harvester->GetState(), ERTSHarvestState::Idle);
	TestFalse(TEXT("The far source was never picked"), OutOfReach->IsDepleted() || Harvester->GetSource() == OutOfReach);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSConstructionRefundTest, "WorldDivide.Economy.ConstructionRefund", TestFlags)

bool FRTSConstructionRefundTest::RunTest(const FString& Parameters)
{
	constexpr int32 Paid = 400;

	const FRTSTestWorld TestWorld;
	ARTSPlayerState* Player = TestWorld.SpawnPlayer(0, 0);
	ARTSPlayerState* Enemy = TestWorld.SpawnPlayer(1, 0);
	URTSBuildingData* Data = MakeDepotData(nullptr);

	auto Cancel = [](ARTSPlayerState& By, ARTSBuilding* Building)
	{
		FRTSCommand Command;
		Command.Type = ERTSCommandType::CancelConstruction;
		Command.TargetActor = Building;
		return RTSCommands::CancelConstruction(By, Command);
	};
	auto SpawnSite = [&](const FVector& Location, int32 PaidCost, float Progress)
	{
		ARTSBuilding* Building = TestWorld.SpawnBuilding(Data, 0, Location);
		Building->SetPaidCost(PaidCost);
		Building->UpdateConstruction(Data->BuildTime * Progress);
		return Building;
	};

	// A quarter built: cancelling returns the three quarters still to build.
	ARTSBuilding* Site = SpawnSite(FVector::ZeroVector, Paid, 0.25f);
	TestEqual(TEXT("The refund is the unbuilt share"), Site->GetCancelRefund(), 300);
	TestEqual(TEXT("Another team cannot cancel it"), Cancel(*Enemy, Site), INDEX_NONE);
	TestTrue(TEXT("The refused cancel left the site standing"), IsValid(Site));
	TestEqual(TEXT("Cancelling returns the unbuilt share"), Cancel(*Player, Site), 300);
	TestEqual(TEXT("The owner got the refund"), Player->GetCredits(), 300);
	TestEqual(TEXT("Nothing went to the other team"), Enemy->GetCredits(), 0);
	TestFalse(TEXT("The cancelled site is gone"), IsValid(Site));
	TestEqual(TEXT("A cancelled site cannot be cancelled again"), Cancel(*Player, Site), INDEX_NONE);

	// A completed building is no construction site any more.
	ARTSBuilding* Done = SpawnSite(FVector(1000.f, 0.f, 0.f), Paid, 1.f);
	TestEqual(TEXT("A completed building has nothing to refund"), Done->GetCancelRefund(), 0);
	TestEqual(TEXT("A completed building cannot be cancelled"), Cancel(*Player, Done), INDEX_NONE);
	TestTrue(TEXT("The completed building stands"), IsValid(Done));

	// A construction site the enemy destroys is lost with everything paid for it.
	ARTSBuilding* Lost = SpawnSite(FVector(2000.f, 0.f, 0.f), Paid, 0.5f);
	Lost->GetHealthComponent()->ApplyDamage(Data->MaxHealth, nullptr);
	TestFalse(TEXT("The destroyed site is gone"), IsValid(Lost));
	TestEqual(TEXT("A destroyed site refunds nothing"), Player->GetCredits(), 300);

	// A site placed for free, such as by the map, returns nothing.
	ARTSBuilding* Free = SpawnSite(FVector(3000.f, 0.f, 0.f), 0, 0.f);
	TestEqual(TEXT("A free site cancels for nothing"), Cancel(*Player, Free), 0);
	TestFalse(TEXT("The free site is gone"), IsValid(Free));
	TestEqual(TEXT("Credits are unchanged"), Player->GetCredits(), 300);
	return true;
}

#endif
