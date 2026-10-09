#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "UI/RTSResourceBarWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using RTSTests::TestFlags;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSResourceBarFormattingTest, "WorldDivide.HUD.ResourceBar.Formatting", TestFlags)

bool FRTSResourceBarFormattingTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Credits"), URTSResourceBarWidget::FormatCredits(10000), FString(TEXT("Credits: 10000")));
	TestEqual(TEXT("No credits"), URTSResourceBarWidget::FormatCredits(0), FString(TEXT("Credits: 0")));

	TestEqual(TEXT("Power produced over consumed"), URTSResourceBarWidget::FormatPower(5, 3, false), FString(TEXT("Power: 5 / 3")));
	TestEqual(TEXT("No buildings yet"), URTSResourceBarWidget::FormatPower(0, 0, false), FString(TEXT("Power: 0 / 0")));
	TestEqual(TEXT("Low power is marked"), URTSResourceBarWidget::FormatPower(2, 5, true), FString(TEXT("Power: 2 / 5 (low)")));

	TestTrue(TEXT("Low power has its own colour"), URTSResourceBarWidget::GetPowerColor(true) != URTSResourceBarWidget::GetPowerColor(false));
	const FLinearColor Low = URTSResourceBarWidget::GetPowerColor(true);
	TestTrue(TEXT("Low power is red"), Low.R > Low.G && Low.R > Low.B);
	return true;
}

#endif
