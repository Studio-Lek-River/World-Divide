#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Units/Debug/RTSBenchPlan.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using RTSTests::TestFlags;
}

/**
 * The bench methods measure what PERFORMANCE_RESULTS.md says they do (#167): the long method is the #138 gate, unchanged; the
 * short method is the step 0 method; the quick method is 240 moving units, a 10 s warm-up and one 30 s sample, in the long scene.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBenchPlanTest, "WorldDivide.Debug.Bench.Plan", TestFlags)

bool FRTSBenchPlanTest::RunTest(const FString& Parameters)
{
	const TArray<int32> GateCounts = { 1, 30, 120, 240 };

	const FRTSBenchPlan Long = FRTSBenchPlan::Make(ERTSBenchMethod::Long, {});
	TestTrue(TEXT("long: counts"), Long.Counts == GateCounts);
	TestTrue(TEXT("long: measures idle"), Long.bIdle);
	TestEqual(TEXT("long: idle warm-up"), Long.IdleWarmupSeconds, 10.0);
	TestEqual(TEXT("long: move warm-up"), Long.MoveWarmupSeconds, 10.0);
	TestEqual(TEXT("long: sample length"), Long.SampleSeconds, 60.0);
	TestEqual(TEXT("long: samples at 240"), Long.GetSamples(240), 3);
	TestEqual(TEXT("long: samples at 120"), Long.GetSamples(120), 1);
	TestEqual(TEXT("long: samples at 1"), Long.GetSamples(1), 1);
	TestTrue(TEXT("long: long scene"), Long.bLongScene);

	const FRTSBenchPlan Short = FRTSBenchPlan::Make(ERTSBenchMethod::Short, {});
	TestTrue(TEXT("short: counts"), Short.Counts == GateCounts);
	TestTrue(TEXT("short: measures idle"), Short.bIdle);
	TestEqual(TEXT("short: settle"), Short.IdleWarmupSeconds, 3.0);
	TestEqual(TEXT("short: move lead"), Short.MoveWarmupSeconds, 1.0);
	TestFalse(TEXT("short: frame sample"), Short.IsTimed());
	TestEqual(TEXT("short: frames"), Short.SampleFrames, 120);
	TestFalse(TEXT("short: step 0 scene"), Short.bLongScene);

	const FRTSBenchPlan Quick = FRTSBenchPlan::Make(ERTSBenchMethod::Quick, {});
	TestTrue(TEXT("quick: counts"), Quick.Counts == TArray<int32>{ 240 });
	TestFalse(TEXT("quick: moving only"), Quick.bIdle);
	TestEqual(TEXT("quick: move warm-up"), Quick.MoveWarmupSeconds, 10.0);
	TestEqual(TEXT("quick: sample length"), Quick.SampleSeconds, 30.0);
	TestEqual(TEXT("quick: one sample"), Quick.GetSamples(240), 1);
	TestTrue(TEXT("quick: long scene, so it compares with the gate's move rows"), Quick.bLongScene);

	const FRTSBenchPlan Screen = FRTSBenchPlan::Make(ERTSBenchMethod::Screen, {});
	TestTrue(TEXT("screen: counts"), Screen.Counts == TArray<int32>{ 240 });
	TestFalse(TEXT("screen: moving only"), Screen.bIdle);
	TestEqual(TEXT("screen: move warm-up"), Screen.MoveWarmupSeconds, 10.0);
	TestEqual(TEXT("screen: sample length (#171)"), Screen.SampleSeconds, 15.0);
	TestEqual(TEXT("screen: one sample"), Screen.GetSamples(240), 1);
	TestTrue(TEXT("screen: the long scene"), Screen.bLongScene);

	const FRTSBenchPlan QuickCounts = FRTSBenchPlan::Make(ERTSBenchMethod::Quick, { 30, 120 });
	TestTrue(TEXT("quick: given counts"), QuickCounts.Counts == TArray<int32>{ 30, 120 });
	TestEqual(TEXT("quick: one sample at the largest given count"), QuickCounts.GetSamples(120), 1);
	return true;
}

/** rts.Debug.Bench's arguments: counts joined by ',' or '+', and the method words. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSBenchArgsTest, "WorldDivide.Debug.Bench.Args", TestFlags)

bool FRTSBenchArgsTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("counts with '+'"), FRTSBenchPlan::ParseCounts(TEXT("1+30+120+240")) == TArray<int32>{ 1, 30, 120, 240 });
	TestTrue(TEXT("counts with ','"), FRTSBenchPlan::ParseCounts(TEXT("240,30")) == TArray<int32>{ 240, 30 });
	TestTrue(TEXT("0 is no count"), FRTSBenchPlan::ParseCounts(TEXT("0")).IsEmpty());
	TestTrue(TEXT("words are no counts"), FRTSBenchPlan::ParseCounts(TEXT("quick")).IsEmpty());

	for (const ERTSBenchMethod Method : { ERTSBenchMethod::Short, ERTSBenchMethod::Long, ERTSBenchMethod::Quick, ERTSBenchMethod::Screen })
	{
		ERTSBenchMethod Parsed = ERTSBenchMethod::Short;
		TestTrue(FString::Printf(TEXT("%s parses"), FRTSBenchPlan::GetMethodName(Method)),
			FRTSBenchPlan::ParseMethod(FRTSBenchPlan::GetMethodName(Method), Parsed) && Parsed == Method);
	}
	ERTSBenchMethod Unknown = ERTSBenchMethod::Short;
	TestFalse(TEXT("an unknown method is refused"), FRTSBenchPlan::ParseMethod(TEXT("fast"), Unknown));
	return true;
}

#endif
