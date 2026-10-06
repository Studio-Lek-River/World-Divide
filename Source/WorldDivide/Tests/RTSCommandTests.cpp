#include "Commands/RTSCommandDispatch.h"
#include "Commands/RTSFormation.h"
#include "Data/RTSUnitData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Units/RTSUnit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSFormationOffsetsTest, "WorldDivide.Commands.Formation.Offsets", TestFlags)

bool FRTSFormationOffsetsTest::RunTest(const FString& Parameters)
{
	constexpr float Spacing = 150.f;

	TestEqual(TEXT("Zero units give no offsets"), RTSFormation::ComputeOffsets(0, Spacing).Num(), 0);

	const TArray<FVector> Single = RTSFormation::ComputeOffsets(1, Spacing);
	TestEqual(TEXT("A single unit goes to the exact target"), Single[0], FVector::ZeroVector);

	for (const int32 Count : { 2, 5, 7, 16, 23 })
	{
		const TArray<FVector> Offsets = RTSFormation::ComputeOffsets(Count, Spacing);
		TestEqual(FString::Printf(TEXT("%d units get %d offsets"), Count, Count), Offsets.Num(), Count);

		FVector Sum = FVector::ZeroVector;
		for (int32 A = 0; A < Offsets.Num(); ++A)
		{
			Sum += Offsets[A];
			for (int32 B = A + 1; B < Offsets.Num(); ++B)
			{
				TestTrue(FString::Printf(TEXT("%d units: slots %d and %d keep their distance"), Count, A, B),
					FVector::Dist(Offsets[A], Offsets[B]) >= Spacing - KINDA_SMALL_NUMBER);
			}
		}
		// Rows are centred, so the group lands around the clicked point rather than beside it.
		TestTrue(FString::Printf(TEXT("%d units: formation centred on target"), Count),
			(Sum / Count).Size2D() <= Spacing * 0.5f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSFormationAssignTest, "WorldDivide.Commands.Formation.AssignSlots", TestFlags)

bool FRTSFormationAssignTest::RunTest(const FString& Parameters)
{
	// Two units side by side heading to two slots side by side should not swap and cross paths.
	const TArray<FVector> Units = { FVector(0.f, -100.f, 0.f), FVector(0.f, 100.f, 0.f) };
	const TArray<FVector> Slots = { FVector(1000.f, 100.f, 0.f), FVector(1000.f, -100.f, 0.f) };
	const TArray<int32> Assignment = RTSFormation::AssignSlots(Units, Slots);

	TestEqual(TEXT("Left unit takes left slot"), Assignment[0], 1);
	TestEqual(TEXT("Right unit takes right slot"), Assignment[1], 0);
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
