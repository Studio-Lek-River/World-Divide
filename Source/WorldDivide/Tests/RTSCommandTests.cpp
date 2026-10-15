#include "Commands/RTSCommandDispatch.h"
#include "Commands/RTSFormation.h"
#include "Data/RTSUnitData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitShape.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using RTSTests::TestFlags;
}

// Named rather than anonymous: unity builds merge anonymous namespaces, and other test files have helpers of the same names.
namespace RTSFormationTests
{
	const TCHAR* InfantryPath = TEXT("/Game/Data/Units/DA_TestInfantry.DA_TestInfantry");
	const TCHAR* TankPath = TEXT("/Game/Data/Units/DA_TestTank.DA_TestTank");
	const TCHAR* M20Path = TEXT("/Game/Data/Units/DA_TestM20Guardian.DA_TestM20Guardian");

	/** Plan's budget for 240 units, from #139's acceptance. */
	constexpr double MaxPlanMs240 = 5.0;

	/** A readable formation is neither a line nor a column: width over depth within these. */
	constexpr float MinAspect = 1.f / 3.f;
	constexpr float MaxAspect = 3.f;

	struct FShapes
	{
		FRTSUnitShape Infantry;
		FRTSUnitShape Tank;
		FRTSUnitShape M20;
	};

	bool LoadShapes(FAutomationTestBase& Test, FShapes& Out)
	{
		const URTSUnitData* Infantry = LoadObject<URTSUnitData>(nullptr, InfantryPath);
		const URTSUnitData* Tank = LoadObject<URTSUnitData>(nullptr, TankPath);
		const URTSUnitData* M20 = LoadObject<URTSUnitData>(nullptr, M20Path);
		if (!Infantry || !Tank || !M20)
		{
			Test.AddError(TEXT("The test unit data is missing; run ue.ps1 -Task setup"));
			return false;
		}
		Out.Infantry = RTSUnitShape::FromData(*Infantry);
		Out.Tank = RTSUnitShape::FromData(*Tank);
		Out.M20 = RTSUnitShape::FromData(*M20);
		return true;
	}

	/** Count units of Shapes in turn, scattered over a square block west of the origin, deterministically. */
	TArray<FRTSFormationMember> Scatter(const TArray<const FRTSUnitShape*>& Shapes, int32 Count, int32 Seed)
	{
		FRandomStream Random(Seed);
		const float Half = 300.f * FMath::Sqrt(static_cast<float>(Count));
		TArray<FRTSFormationMember> Members;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector Location(-10000.f + Random.FRandRange(-Half, Half), Random.FRandRange(-Half, Half), 0.f);
			Members.Emplace(Location, *Shapes[Index % Shapes.Num()]);
		}
		return Members;
	}

	/** The footprint a member would have standing in Slot, facing Yaw. */
	FRTSFootprint SlotFootprint(const FRTSFormationMember& Member, const FVector& Slot, float Yaw)
	{
		FRTSUnitShape Shape;
		Shape.Hull = FBox(-FVector(Member.Size.X, Member.Size.Y, 100.f) * 0.5f, FVector(Member.Size.X, Member.Size.Y, 100.f) * 0.5f);
		return RTSUnitShape::MakeFootprint(Shape, FTransform(FRotator(0.f, Yaw, 0.f), Slot));
	}

	/** The smallest gap between any two members' footprints in their slots; INF for one member. */
	float GetSmallestGap(TConstArrayView<FRTSFormationMember> Members, TConstArrayView<FVector> Slots, float Yaw)
	{
		TArray<FRTSFootprint> Footprints;
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			Footprints.Add(SlotFootprint(Members[Index], Slots[Index], Yaw));
		}
		float Smallest = TNumericLimits<float>::Max();
		for (int32 A = 0; A < Footprints.Num(); ++A)
		{
			for (int32 B = A + 1; B < Footprints.Num(); ++B)
			{
				Smallest = FMath::Min(Smallest, RTSUnitShape::GetGap(Footprints[A], Footprints[B]));
			}
		}
		return Smallest;
	}

	/** Width (across the facing) and depth of the hulls in their slots, for a formation facing +X. */
	FVector2D GetExtent(TConstArrayView<FRTSFormationMember> Members, TConstArrayView<FVector> Slots)
	{
		FBox2D Box(ForceInit);
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			const FVector2D Half = Members[Index].Size * 0.5f;
			Box += FVector2D(Slots[Index]) - Half;
			Box += FVector2D(Slots[Index]) + Half;
		}
		return FVector2D(Box.GetSize().Y, Box.GetSize().X);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSFormationUniqueSlotsTest, "WorldDivide.Commands.Formation.UniqueSlots", TestFlags)

bool FRTSFormationUniqueSlotsTest::RunTest(const FString& Parameters)
{
	using namespace RTSFormationTests;
	FShapes Shapes;
	if (!LoadShapes(*this, Shapes))
	{
		return false;
	}
	const FVector Target(2000.f, 500.f, 0.f);

	TestEqual(TEXT("No units, no slots"), RTSFormation::Plan(TArray<FRTSFormationMember>(), Target, 0.f).Num(), 0);
	const TArray<FVector> Single = RTSFormation::Plan(TArray<FRTSFormationMember>{ FRTSFormationMember(FVector::ZeroVector, Shapes.Tank) }, Target, 0.f);
	TestTrue(TEXT("A single unit goes to the exact target"), Single.Num() == 1 && Single[0].Equals(Target));

	for (const int32 Count : { 2, 5, 7, 16, 23, 120, 240 })
	{
		const TArray<FRTSFormationMember> Members = Scatter({ &Shapes.Infantry, &Shapes.Tank, &Shapes.M20 }, Count, Count);
		const TArray<FVector> Slots = RTSFormation::Plan(Members, Target, 0.f);
		TestEqual(FString::Printf(TEXT("%d units get %d slots"), Count, Count), Slots.Num(), Count);
		TestTrue(FString::Printf(TEXT("%d units: no two share a slot"), Count), GetSmallestGap(Members, Slots, 0.f) > 0.f);
		TestTrue(FString::Printf(TEXT("%d units: the same order gives the same slots"), Count), RTSFormation::Plan(Members, Target, 0.f) == Slots);

		// The same units listed in another order keep their slots.
		TArray<int32> Order;
		for (int32 Index = Count - 1; Index >= 0; --Index)
		{
			Order.Add(Index);
		}
		TArray<FRTSFormationMember> Reversed;
		for (const int32 Index : Order)
		{
			Reversed.Add(Members[Index]);
		}
		const TArray<FVector> ReversedSlots = RTSFormation::Plan(Reversed, Target, 0.f);
		bool bSame = true;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			bSame &= ReversedSlots[Index].Equals(Slots[Order[Index]]);
		}
		TestTrue(FString::Printf(TEXT("%d units: listed in reverse, each unit keeps its slot"), Count), bSame);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSFormationClearanceTest, "WorldDivide.Commands.Formation.FootprintClearance", TestFlags)

bool FRTSFormationClearanceTest::RunTest(const FString& Parameters)
{
	using namespace RTSFormationTests;
	FShapes Shapes;
	if (!LoadShapes(*this, Shapes))
	{
		return false;
	}

	for (const float Yaw : { 0.f, 37.f, 180.f })
	{
		const TArray<FRTSFormationMember> Members = Scatter({ &Shapes.Infantry, &Shapes.Tank, &Shapes.M20, &Shapes.Infantry }, 60, 7);
		const TArray<FVector> Slots = RTSFormation::Plan(Members, FVector::ZeroVector, Yaw);
		const float Gap = GetSmallestGap(Members, Slots, Yaw);
		TestTrue(FString::Printf(TEXT("Facing %.0f: every pair of footprints at least %.0f cm apart (closest %.1f cm)"), Yaw,
			RTSFormation::MinGap, Gap), Gap >= RTSFormation::MinGap - 0.5f);
	}

	// The gaps grow with the unit up to the approved vehicle clearances.
	TestEqual(TEXT("Infantry stands the minimum gap apart"), RTSFormation::GetSideGap(2.f * Shapes.Infantry.GetHalfWidth()), RTSFormation::MinGap);
	TestEqual(TEXT("Beside an M20 the gap is 2.5 m"), RTSFormation::GetSideGap(2.f * Shapes.M20.GetHalfWidth()), RTSFormation::MaxSideGap);
	TestEqual(TEXT("Behind an M20 the gap is 3 m"), RTSFormation::GetDepthGap(2.f * Shapes.M20.GetHalfLength()), RTSFormation::MaxDepthGap);

	// Infantry ahead of a few M20s keeps its own spacing: a big unit in the group does not spread every soldier.
	TArray<FRTSFormationMember> Members;
	for (int32 Index = 0; Index < 25; ++Index)
	{
		Members.Emplace(FVector(-2000.f + (Index / 5) * -150.f, (Index % 5) * 150.f, 0.f), Shapes.Infantry);
	}
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Members.Emplace(FVector(-5000.f, Index * 700.f, 0.f), Shapes.M20);
	}
	const TArray<FVector> Slots = RTSFormation::Plan(Members, FVector::ZeroVector, 0.f);
	const float InfantryPitch = 2.f * Shapes.Infantry.GetHalfWidth() + RTSFormation::GetSideGap(2.f * Shapes.Infantry.GetHalfWidth());
	// The front row is all infantry: as wide as its soldiers at their own pitch.
	float FrontX = -TNumericLimits<float>::Max();
	for (const FVector& Slot : Slots)
	{
		FrontX = FMath::Max(FrontX, static_cast<float>(Slot.X));
	}
	int32 FrontCount = 0;
	bool bFrontInfantry = true;
	FFloatInterval FrontY(TNumericLimits<float>::Max(), -TNumericLimits<float>::Max());
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		if (FMath::IsNearlyEqual(Slots[Index].X, FrontX, 1.f))
		{
			++FrontCount;
			bFrontInfantry &= Index < 25;
			FrontY.Include(Slots[Index].Y);
		}
	}
	TestTrue(TEXT("The infantry ahead takes the front row"), bFrontInfantry && FrontCount > 1);
	TestNearlyEqual(FString::Printf(TEXT("The front row's %d soldiers stand at their own pitch (%.0f cm)"), FrontCount, InfantryPitch),
		FrontY.Size(), (FrontCount - 1) * InfantryPitch, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSFormationFrontTest, "WorldDivide.Commands.Formation.FrontToFront", TestFlags)

bool FRTSFormationFrontTest::RunTest(const FString& Parameters)
{
	using namespace RTSFormationTests;
	FShapes Shapes;
	if (!LoadShapes(*this, Shapes))
	{
		return false;
	}

	// Two units side by side heading to two slots side by side do not swap and cross paths.
	{
		const TArray<FRTSFormationMember> Members = { FRTSFormationMember(FVector(0.f, -100.f, 0.f), Shapes.Tank),
			FRTSFormationMember(FVector(0.f, 100.f, 0.f), Shapes.Tank) };
		const TArray<FVector> Slots = RTSFormation::Plan(Members, FVector(5000.f, 0.f, 0.f), 0.f);
		TestTrue(TEXT("The left unit takes the left slot"), Slots[0].Y < Slots[1].Y);
	}

	// A column of tanks driving east: those ahead take the front rows, and no unit behind gets a slot ahead of one in front.
	TArray<FRTSFormationMember> Members;
	for (int32 Index = 0; Index < 24; ++Index)
	{
		Members.Emplace(FVector(-Index * 400.f, (Index % 3 - 1) * 80.f, 0.f), Shapes.Tank);
	}
	for (const float Yaw : { 0.f, 90.f })
	{
		const FVector Forward = FRotator(0.f, Yaw, 0.f).Vector();
		TArray<FRTSFormationMember> Turned = Members;
		for (FRTSFormationMember& Member : Turned)
		{
			Member.Location = FRotator(0.f, Yaw, 0.f).RotateVector(Member.Location);
		}
		const TArray<FVector> Slots = RTSFormation::Plan(Turned, Forward * 20000.f, Yaw);
		int32 Inversions = 0;
		for (int32 A = 0; A < Turned.Num(); ++A)
		{
			for (int32 B = A + 1; B < Turned.Num(); ++B)
			{
				// A is ahead of B (lower index); its slot must not be behind B's.
				Inversions += FVector::DotProduct(Slots[A] - Slots[B], Forward) < -1.f ? 1 : 0;
			}
		}
		TestEqual(FString::Printf(TEXT("Facing %.0f: front units take front slots"), Yaw), Inversions, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSFormationScenariosTest, "WorldDivide.Commands.Formation.Scenarios", TestFlags)

bool FRTSFormationScenariosTest::RunTest(const FString& Parameters)
{
	using namespace RTSFormationTests;
	FShapes Shapes;
	if (!LoadShapes(*this, Shapes))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("Hulls (L x W): infantry %.0f x %.0f, tank %.0f x %.0f, M20 %.0f x %.0f cm"),
		2.f * Shapes.Infantry.GetHalfLength(), 2.f * Shapes.Infantry.GetHalfWidth(), 2.f * Shapes.Tank.GetHalfLength(),
		2.f * Shapes.Tank.GetHalfWidth(), 2.f * Shapes.M20.GetHalfLength(), 2.f * Shapes.M20.GetHalfWidth()));

	struct FScenario
	{
		const TCHAR* Name;
		TArray<const FRTSUnitShape*> Shapes;
		int32 Count;
	};
	const FScenario Scenarios[] = {
		{ TEXT("30 M20"), { &Shapes.M20 }, 30 },
		{ TEXT("30 infantry"), { &Shapes.Infantry }, 30 },
		{ TEXT("30 tanks"), { &Shapes.Tank }, 30 },
		{ TEXT("30 mixed (infantry, tank, M20)"), { &Shapes.Infantry, &Shapes.Infantry, &Shapes.Tank, &Shapes.Tank, &Shapes.M20 }, 30 },
		{ TEXT("120 tanks"), { &Shapes.Tank }, 120 },
		{ TEXT("120 mixed"), { &Shapes.Infantry, &Shapes.Infantry, &Shapes.Tank, &Shapes.Tank, &Shapes.M20 }, 120 },
		{ TEXT("240 tanks"), { &Shapes.Tank }, 240 },
		{ TEXT("240 M20"), { &Shapes.M20 }, 240 },
		{ TEXT("240 mixed"), { &Shapes.Infantry, &Shapes.Infantry, &Shapes.Tank, &Shapes.Tank, &Shapes.M20 }, 240 },
	};
	for (const FScenario& Scenario : Scenarios)
	{
		const TArray<FRTSFormationMember> Members = Scatter(Scenario.Shapes, Scenario.Count, Scenario.Count + 1);
		const TArray<FVector> Slots = RTSFormation::Plan(Members, FVector::ZeroVector, 0.f);
		const FVector2D Extent = GetExtent(Members, Slots);
		const float Aspect = Extent.X / FMath::Max(Extent.Y, 1.0);
		AddInfo(FString::Printf(TEXT("%s: %.0f m wide, %.0f m deep"), Scenario.Name, Extent.X / 100.0, Extent.Y / 100.0));
		TestTrue(FString::Printf(TEXT("%s: readable, %.2f wide per deep"), Scenario.Name, Aspect), Aspect >= MinAspect && Aspect <= MaxAspect);
		TestTrue(FString::Printf(TEXT("%s: footprints clear"), Scenario.Name), GetSmallestGap(Members, Slots, 0.f) >= RTSFormation::MinGap - 0.5f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSFormationPerformanceTest, "WorldDivide.Commands.Formation.Performance240", TestFlags)

bool FRTSFormationPerformanceTest::RunTest(const FString& Parameters)
{
	using namespace RTSFormationTests;
	FShapes Shapes;
	if (!LoadShapes(*this, Shapes))
	{
		return false;
	}
	const TArray<FRTSFormationMember> Members = Scatter({ &Shapes.Infantry, &Shapes.Tank, &Shapes.M20 }, 240, 240);

	// The median of several runs, so one descheduled run does not decide it.
	TArray<double> Times;
	for (int32 Run = 0; Run < 21; ++Run)
	{
		const double Start = FPlatformTime::Seconds();
		const TArray<FVector> Slots = RTSFormation::Plan(Members, FVector(3000.f, 0.f, 0.f), 0.f);
		Times.Add((FPlatformTime::Seconds() - Start) * 1000.0);
		check(Slots.Num() == Members.Num());
	}
	Times.Sort();
	const double Median = Times[Times.Num() / 2];
	AddInfo(FString::Printf(TEXT("Formation for 240 units: %.3f ms median, %.3f ms slowest of %d runs"), Median, Times.Last(), Times.Num()));
	TestTrue(FString::Printf(TEXT("Formation for 240 units under %.0f ms"), MaxPlanMs240), Median < MaxPlanMs240);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCommandOwnershipTest, "WorldDivide.Commands.Ownership", TestFlags)

bool FRTSCommandOwnershipTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);

	URTSUnitData* Data = NewObject<URTSUnitData>();

	auto SpawnUnit = [World, Data](int32 TeamId)
	{
		ARTSUnit* Unit = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), FTransform::Identity);
		Unit->InitFromData(Data, TeamId);
		Unit->FinishSpawning(FTransform::Identity);
		return Unit;
	};

	ARTSUnit* Own = SpawnUnit(0);
	ARTSUnit* OwnToo = SpawnUnit(0);
	ARTSUnit* Enemy = SpawnUnit(1);

	const TArray<ARTSUnit*> Requested = { Own, Enemy, nullptr, OwnToo, Own };
	const TArray<ARTSUnit*> Filtered = RTSCommands::FilterOwnedUnits(Requested, 0);

	TestEqual(TEXT("Only own units survive, once each"), Filtered.Num(), 2);
	TestTrue(TEXT("First own unit kept"), Filtered.Contains(Own));
	TestTrue(TEXT("Second own unit kept"), Filtered.Contains(OwnToo));
	TestFalse(TEXT("Enemy unit dropped"), Filtered.Contains(Enemy));

	TestEqual(TEXT("Team 1 may only order its own unit"), RTSCommands::FilterOwnedUnits(Requested, 1).Num(), 1);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
