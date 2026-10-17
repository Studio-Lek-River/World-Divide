#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RTSBenchGateCommandlet.generated.h"

/**
 * The full-roster performance gate's planning and judging (#171, ue.ps1 -Task gate; the logic is RTSBenchGate's):
 * -run=RTSBenchGate -Plan -Gate=<dir> -Changes=<file> [-Broad] [-Screens=<dir>] reads the roster (every URTSUnitData under
 * /Game/Data/Units with its packages and make-up) and the changed files, and writes plan.json: the sets to screen with the
 * screen method (#171: the long scene, 240 moving units, one 15 s sample) and to bench with the long method, and why.
 * -run=RTSBenchGate -Judge -Gate=<dir> -Baselines=<dir> -Stage=screen|final judges the runs ue.ps1 copied to
 * <Gate>/screen/<set> and <Gate>/long/<set> against <Baselines>/<method>/<set>: the screen writes screen.json with the sets
 * to escalate and to run again; the final stage writes gate.json and gate.md. Exit code 0 PASS, 1 FAIL or a bad command
 * line, 4 INCONCLUSIVE. Relative paths are taken from the project folder.
 */
UCLASS()
class WORLDDIVIDE_API URTSBenchGateCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	URTSBenchGateCommandlet();

	virtual int32 Main(const FString& Params) override;

	static constexpr int32 ExitPass = 0;
	static constexpr int32 ExitFail = 1;
	static constexpr int32 ExitInconclusive = 4;
};
