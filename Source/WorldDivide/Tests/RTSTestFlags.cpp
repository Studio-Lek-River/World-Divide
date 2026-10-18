#include "Tests/RTSTestFlags.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/DelayedAutoRegister.h"

namespace
{
	/**
	 * With -RTSTestFixedStep every PIE session ticks at the fixed step from its first frame, before its worlds begin play:
	 * frames at the editor's real-time step before the test applied it would start the match a CPU-dependent fraction of a
	 * second later on one run than on another (#179).
	 */
	FDelayedAutoRegisterHelper GRTSFixedStepAtPIEStart(EDelayedRegisterRunPhase::EndOfEngineInit, []()
	{
		FEditorDelegates::PreBeginPIE.AddLambda([](bool)
		{
			const double Rate = RTSTests::GetTestFixedStepRate();
			if (Rate > 0.0)
			{
				RTSTests::ApplyTestFixedStep(Rate);
			}
		});
	});
}

#endif
