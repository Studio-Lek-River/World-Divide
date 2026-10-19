<#
.SYNOPSIS
Builds, tests, regenerates test content for or opens World Divide from the command line.

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task build
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task test -Filter WorldDivide.Commands
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task test -Filter WorldDivide.Commands,WorldDivide.UI
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task test -Tier fast
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task test -Rerun
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task test -Shards 1
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task test -TimeDilation 1
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task test -Filter WorldDivide.Smoke.AIvsAI -Repeat 10
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task test -Filter WorldDivide.Smoke.AIvsAI -Cvars rts.Nav.MoveRequestsPerFrame=6
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task setup
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task setup -Render
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task editor
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task uishots
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task uishots -Panel ResourceBar
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task unitshots -Unit DA_TestM20Guardian -Stress 240
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task unitshots -Unit DA_TestTank -Stress 240 -Trace
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task unitshots -Unit DA_TestM20Guardian -LODs
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task trackshots -Unit DA_TestM20Guardian
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task camerashots
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bench -Unit DA_TestM20Guardian
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bench -Unit DA_TestTank -Counts 240 -Trace
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bench -Unit DA_TestM20Guardian -Long
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bench -Unit DA_TestM20Guardian,DA_TestTank -Quick -Compare
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bench -Unit DA_TestM20Guardian,DA_TestTank -Long -SaveBaseline
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task gate
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task gate -Scope broad -Baseline 4a2a706
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task uibindings
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task uicheck -Path Saved/UIIntake/HUD_2026-10-14/staging/UI

The engine is taken from $env:UE_ROOT, falling back to D:\Program Files\Epic Games\UE_5.8.
Exits nonzero on any failure: 2 when the editor is running (build, editor), 3 when setup must rewrite read-only LFS assets.
The PIE smoke tests tick a fixed 1/30 game second per frame as fast as the CPU allows (-RTSTestFixedStep=30); -TimeDilation N
instead runs them in step with real time at N game seconds per real second, -TimeDilation 1 at real time.
-Repeat N runs the tests N times, each in a fresh editor, and passes only when every run does; it prints each run's result,
its wall-clock time and the tests' info lines that differ between runs, then the total (#179). -Cvars sets console variables
for the test run (-dpcvars, comma-separated name=value), such as another rts.Nav.MoveRequestsPerFrame to try a different
fixed-step trajectory (#181).
-Filter takes several comma-separated filters and runs them in one editor. -Tier fast leaves out the tests tagged [Slow] in
Config/Tests/Tags.ini (the whole suite in about 2 min instead of 8). -Rerun runs only the tests that failed in the last run
(Saved/Automation/LastFailed.txt). The whole suite at the full tier runs in two editors at once (about 5 min), the navigation
tests in one and the rest in the other; -Shards 1 keeps it in one editor. A watchdog kills the test editor after
-TimeoutMinutes (default 6 for fast, 15 for full), names the test it hung in and exits 5. Every run prints its slowest tests and names any test over 8 s without a [Slow] tag. The
editor's own output goes to Saved/Logs/AutomationRun*.log.stdout; the run prints only its result.
Setup runs without a renderer and draws the lobby's map previews; -Render gives it one (a GPU is needed) to render them.
Editor opens the editor with the Unreal MCP server at http://127.0.0.1:8000/mcp (docs/ui/AI_TEAM.md); test and setup run without it.
Uishots plays L_TestMap with a renderer (a GPU is needed) and saves the HUD in the UI test state for each faction look at
1920x1080, 2560x1440, 3440x1440 and 1680x1050 to Saved/UIShots/HUD, or with -Panel to Saved/UIShots/<Panel> with a crop of that panel;
-Panel <Panel>@<Scene> first selects a scene of RTSUITestState::ApplyScene (Selection@Hero3, Selection@Building, ...).
Unitshots plays L_TestMap with a renderer (a GPU is needed) at 1920x1080 and saves views of the first unit with the data
asset -Unit (rts.Debug.UnitShots): the RTS camera at its default, closest and farthest zoom, a close-up, the turret turned,
the gun's recoil at rest, at full stroke, with the hull rocked back and run out again (#218), the unit picked, selected and driving on a move order, driving into, along and round obstacles with its shapes drawn (the hull's
and barrel's overlap printed), and the default view with stat unit and stat rhi, to Saved/UnitShots/<Unit>; with -Stress N also N more of the unit in a
grid with the stats on. The frame time, game thread time, avoidance pass, draw calls and triangles of the stat views are
printed. -Trace also records an Unreal Insights capture (cpu, frame and region channels; each stat view is a region) to
Saved/Traces/UnitShots_<Unit>_<Stress>.utrace. -LODs takes the unit's LOD sheet instead (#149): each LOD forced from four sides,
the RTS camera at a zoom inside each LOD, the RTS lens just inside and outside each switch distance and the team row at each
LOD, with the LOD of every mesh of the unit printed per shot. -Cvars sets console variables for the run, as for trackshots
(rts.Render.GroupPartOcclusion=0 for the views before #214).
Camerashots plays L_TestMap with a renderer (a GPU is needed), stages a base and a mixed formation by Team0's start spot and
saves the camera comparison (rts.Debug.CameraShots): the camera before the real-scale retune and the camera now
at the closest, default and farthest zoom on a vehicle, the formation, the base and the battlefield, plus the base at
2560x1440 and 3440x1440, with metrics.csv, to Saved/CameraShots, then composes the labelled Sheet_*.jpg there (camera_sheet.ps1).
Bench plays L_Bench with a renderer (a GPU is needed) and measures -Unit (rts.Debug.Bench) at 1, 30, 120 and 240 units, or
the comma-separated -Counts, a mix of sizes when -Unit joins several with '+' (DA_TestM20Guardian+DA_TestTank+DA_TestInfantry),
standing in a grid and then driving on a move order, from one fixed camera at 1920x1080 with
dynamic resolution off and no frame rate cap. The table is printed and written with a CSV to Saved/Bench/<Unit>. -Trace
records an Unreal Insights capture to Saved/Traces/Bench_<Unit>.utrace, each scenario a region. -Long uses the long method
of #138: a 10 s warm-up, then 60 s samples (three at the largest count, one below it) with the game thread's 95th
percentile, while the units keep driving back and forth every 15 s, to a point 30 m west of a grid that stands
south-west of the middle, out of the enemy headquarters' range. -Long (alias -Full) is the performance gate. -Quick (#167) is
for iterating and never gate evidence: the long scene at 240 units (or -Counts), moving only, a 10 s warm-up and one 30 s
sample, the same columns. -Screen (#171) is the gate's first tier: the same with one 15 s sample. -Unit A,B runs several
sets one after another in one game session, each with its own warm-up,
samples and folder. A set named with the suffix @Solids (#172; -Unit DA_TestM20Guardian@Solids) benches among solids the
navmesh leaves out (trunks, boulders, low walls; RTSBenchScene), in its own folder Saved/Bench/<Unit>@Solids; @MixedTeams
(#150) spreads the units over four teams; @RigidTracks (#151) draws the tracks without the link shift and @AllTracks (#212)
animates every track at any distance, and every row counts the tracks' phase updates and the tracks animated on screen. Every row also counts the units' primitives, those drawn on screen, dynamic material
slots and materials. @ExactOcclusion, @NoOcclusion and @HZB (#214) change the occlusion queries, @Occluded roofs most of the
grid and @Close takes the camera to the RTS close zoom; -Map L_TestMap plays L_TestMap instead, for the sets with its viewpoints
(@TestMapOpen, @TestMapBase, @TestMapWall; RTSBenchScene). bench.json
beside the table records the method, scene, machine, commit and content hashes.
-SaveBaseline (clean worktree only) stores each set's run under Saved/Bench/Baselines/<commit>/<method>/<set>; -Compare reads
the run against the baseline of HEAD, or of the ancestor -Baseline <commit> (marked OUTDATED), refuses one of another
method, scene or machine, and writes compare.md. See docs/tech/scale/PERFORMANCE_RESULTS.md, "Bench workflow".
Every bench run checks itself (#171): bench.json's run section marks a sample cut short, too few frames, stalls or samples that
disagree, and ue.ps1 adds the CPU other processes used while each set ran (environment); -SaveBaseline refuses a run that is
no valid measurement. -Frames writes every measured frame to frames.csv and -SampleSeconds N changes the timed samples' length,
for method studies only (such a run never matches a baseline).
Gate (#171) is the performance gate for the full roster: it lists the files changed since the baseline commit (-Baseline, or
HEAD), plans with the RTSBenchGate commandlet which unit sets to screen (-Screen: the quick scene, one 15 s sample) and
which representatives to bench with the long method (-Scope broad: every unit), runs both in one session each, escalates suspicious screens to the long
method, and judges every set against Saved/Bench/Baselines/<commit> and the budgets: Saved/Bench/Gate/gate.md and gate.json,
exit 0 PASS, 1 FAIL, 4 INCONCLUSIVE. With -SaveBaseline (clean worktree) it also stores every valid run as HEAD's baseline.
See docs/tech/scale/PERFORMANCE_RESULTS.md, "Performance gate for a full roster".
Uibindings writes every binding key, list and action with its panels, and the widgets, colours, fonts and sprites a layout
may name, to -Out (default Saved/UIBriefs/bindings.json) for /ui-brief. Uicheck validates the layouts of the UI folder -Path
(an unpacked delivery, for /ui-intake) and prints each problem. -Out and -Path are relative to the repository root.
#>
param(
	[Parameter(Mandatory = $true)]
	[ValidateSet('build', 'test', 'setup', 'editor', 'uishots', 'unitshots', 'trackshots', 'camerashots', 'bench', 'gate', 'uibindings', 'uicheck')]
	[string]$Task,

	[string]$Filter = 'WorldDivide',

	[ValidateSet('full', 'fast')]
	[string]$Tier = 'full',

	[switch]$Rerun,

	[ValidateRange(0, 600)]
	[int]$TimeoutMinutes = 0,

	[ValidateRange(0, 2)]
	[int]$Shards = 0,

	[double]$TimeDilation = 0,

	[ValidateRange(1, 100)]
	[int]$Repeat = 1,

	[string]$Cvars = '',

	[switch]$Render,

	[string]$Panel = '',

	[string]$Unit = 'DA_TestM20Guardian',

	[int]$Stress = 0,

	[string]$Counts = '',

	[switch]$Trace,

	[Alias('Full')]
	[switch]$Long,

	[switch]$Quick,

	[switch]$Screen,

	[switch]$LODs,

	[switch]$SaveBaseline,

	[switch]$Compare,

	[string]$Baseline = '',

	[ValidateSet('auto', 'broad')]
	[string]$Scope = 'auto',

	[switch]$Frames,

	[double]$SampleSeconds = 0,

	[string]$Out = 'Saved/UIBriefs/bindings.json',

	[string]$Path = '',

	[ValidatePattern('^L_\w+$')]
	[string]$Map = 'L_Bench'
)

$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$Project = Join-Path $RepoRoot 'WorldDivide.uproject'

$UERoot = $env:UE_ROOT
if (-not $UERoot) { $UERoot = 'D:\Program Files\Epic Games\UE_5.8' }
if (-not (Test-Path (Join-Path $UERoot 'Engine'))) {
	Write-Error "Unreal Engine 5.8 not found at '$UERoot'. Set UE_ROOT to the engine install folder."
}

$BuildBat = Join-Path $UERoot 'Engine\Build\BatchFiles\Build.bat'
$EditorCmd = Join-Path $UERoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$EditorExe = Join-Path $UERoot 'Engine\Binaries\Win64\UnrealEditor.exe'

# Editor-only plugins of the AI team's MCP connection; the headless runs do not load them.
$NoMCPPlugins = '-DisablePlugins=ModelContextProtocol,EditorToolset,SlateInspectorToolset,WorldDivideMCP'
$MCPUrl = 'http://127.0.0.1:8000/mcp'

function Assert-EditorClosed([string]$Reason = 'Live Coding locks the module DLL and the build will fail.') {
	$Running = Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue
	if ($Running) {
		Write-Host "UnrealEditor is running. Close it first: $Reason"
		exit 2
	}
}

function Invoke-Build {
	Assert-EditorClosed
	& $BuildBat WorldDivideEditor Win64 Development "-Project=$Project" -WaitMutex
	if ($LASTEXITCODE -ne 0) {
		Write-Host "BUILD FAILED (exit $LASTEXITCODE)"
		exit $LASTEXITCODE
	}
	Write-Host 'BUILD SUCCEEDED'
}

function Invoke-Setup {
	$Script = Join-Path $RepoRoot 'Tools\Editor\setup_testmap.py'
	# Written by the script when assets it must rewrite are read-only (LFS lockable, not locked).
	$ReadOnlyReport = Join-Path $RepoRoot 'Saved\Setup\ReadOnlyAssets.txt'
	if (Test-Path $ReadOnlyReport) { Remove-Item -Force $ReadOnlyReport }

	$RenderArgs = if ($Render) { @('-AllowCommandletRendering', '-RenderOffscreen') } else { @('-nullrhi') }
	& $EditorCmd $Project -run=pythonscript "-script=$Script" $NoMCPPlugins -unattended -nosplash @RenderArgs -nosound -stdout -FullStdOutLogOutput
	if (Test-Path $ReadOnlyReport) {
		$Files = @(Get-Content $ReadOnlyReport | Where-Object { $_ })
		Write-Host ''
		Write-Host 'SETUP BLOCKED: these assets must be rewritten but are read-only (LFS lockable, not locked by you):'
		$Files | ForEach-Object { Write-Host "  $_" }
		Write-Host 'Nothing was written. Lock them, rerun setup, commit and push, then unlock:'
		$Files | ForEach-Object { Write-Host "  git lfs lock $_" }
		$Files | ForEach-Object { Write-Host "  git lfs unlock $_" }
		exit 3
	}
	if ($LASTEXITCODE -ne 0) {
		Write-Host "SETUP FAILED (exit $LASTEXITCODE)"
		exit $LASTEXITCODE
	}
	Write-Host 'SETUP SUCCEEDED'
}

# The tests -Tier fast leaves out: Config/Tests/Tags.ini, which the automation framework reads itself.
$TagsFile = Join-Path $RepoRoot 'Config\Tests\Tags.ini'
$LastFailedFile = Join-Path $RepoRoot 'Saved\Automation\LastFailed.txt'
# A test this long in a run that has no [Slow] tag is named after the run, so Tags.ini keeps up.
$SlowSeconds = 8.0

function Get-SlowTests {
	if (-not (Test-Path $TagsFile)) { return @() }
	return @(Get-Content $TagsFile | Where-Object { $_ -match '^([^;=\s][^=]*)=.*\[Slow\]' } | ForEach-Object { $Matches[1].Trim() })
}

# The RunTests argument: the failed tests of the last run with -Rerun, else -Filter, whose comma-separated entries become
# the '+' union of Automation RunTests (-ExecCmds splits its commands at commas).
function Get-TestExpression {
	if ($Rerun) {
		$Names = @(if (Test-Path $LastFailedFile) { Get-Content $LastFailedFile | Where-Object { $_ } })
		if ($Names.Count -eq 0) {
			Write-Host "Nothing to rerun: the last run left no failed tests ($LastFailedFile)."
			exit 1
		}
		Write-Host "Rerunning the $($Names.Count) failed test(s) of the last run."
		return ($Names | ForEach-Object { "^$_$" }) -join '+'
	}
	return (@($Filter -split '[,+]' | ForEach-Object { $_.Trim() } | Where-Object { $_ })) -join '+'
}

# Two shards split the whole suite: the navigation tests, about half its time, in one editor and every other area in a second.
function Get-TestShards([string]$Expression) {
	# By default the whole suite at the full tier runs in two shards (three parallel full runs green, 2026-10-18).
	$Count = if ($Shards -gt 0) { $Shards } elseif (-not $Rerun -and $Tier -eq 'full' -and $Expression -eq 'WorldDivide') { 2 } else { 1 }
	if ($Count -le 1) { return @(@{ Name = ''; Expression = $Expression }) }
	if ($Rerun -or $Expression -ne 'WorldDivide') {
		Write-Host 'TESTS FAILED: -Shards splits the whole WorldDivide suite; leave out -Filter and -Rerun.'
		exit 1
	}
	$Sources = Get-ChildItem (Join-Path $RepoRoot 'Source') -Recurse -Include '*.cpp', '*.h'
	$Areas = @($Sources | Select-String -Pattern '"WorldDivide\.(\w+)\.' -AllMatches | ForEach-Object { $_.Matches } |
		ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
	$Rest = @($Areas | Where-Object { $_ -ne 'Navigation' } | ForEach-Object { "^WorldDivide.$_." })
	return @(
		@{ Name = 'Navigation'; Expression = '^WorldDivide.Navigation.' },
		@{ Name = 'Rest'; Expression = $Rest -join '+' })
}

# One editor per shard, all at once; each is killed once the run has taken -TimeoutMinutes. Returns the merged report:
# tests, the failed and timed-out shards' names, and the wall clock.
function Invoke-TestRun {
	$Expression = Get-TestExpression
	$TagFilter = if ($Tier -eq 'fast') { 'SetTagFilter !Slow;' } else { '' }
	$Minutes = if ($TimeoutMinutes -gt 0) { $TimeoutMinutes } elseif ($Tier -eq 'fast') { 6 } else { 15 }
	# A fixed time step unless -TimeDilation asks for real-time ticking (RTSTests::ApplyTestTimeDilation).
	$TimeArg = if ($TimeDilation -gt 0) { "-RTSTestTimeDilation=$TimeDilation" } else { '-RTSTestFixedStep=30' }
	$CvarArgs = @(if ($Cvars) { "-dpcvars=$Cvars" })

	$Runs = @()
	$Clock = [Diagnostics.Stopwatch]::StartNew()
	foreach ($Shard in Get-TestShards $Expression) {
		$Suffix = if ($Shard.Name) { "-$($Shard.Name)" } else { '' }
		$ReportDir = Join-Path $RepoRoot "Saved\Automation\Reports$Suffix"
		$LogFile = Join-Path $RepoRoot "Saved\Logs\AutomationRun$Suffix.log"
		if (Test-Path $ReportDir) { Remove-Item -Recurse -Force $ReportDir }
		# The editor's own output repeats the log; it goes to a file so a run prints only its result.
		$Process = Start-Process -FilePath $EditorCmd -NoNewWindow -PassThru -RedirectStandardOutput "$LogFile.stdout" -ArgumentList (@(
			"`"$Project`"", "`"-ExecCmds=Automation $($TagFilter)RunTests $($Shard.Expression);Quit`"", '"-TestExit=Automation Test Queue Empty"',
			"`"-ReportExportPath=$ReportDir`"", "`"-abslog=$LogFile`"", $TimeArg) + $CvarArgs + @($NoMCPPlugins,
			'-unattended', '-nullrhi', '-nosplash', '-nosound'))
		# Holding the handle keeps the exit code readable once the process has gone.
		$null = $Process.Handle
		$Runs += @{ Name = $Shard.Name; Process = $Process; ReportDir = $ReportDir; LogFile = $LogFile }
		Write-Host "Running $(if ($Shard.Name) { "shard $($Shard.Name): " })$($Shard.Expression)$(if ($TagFilter) { ', without [Slow]' }); at most $Minutes min. Log: $LogFile"
	}

	$Deadline = [DateTime]::UtcNow.AddMinutes($Minutes)
	$Tests = @()
	$Broken = @()
	foreach ($Run in $Runs) {
		$Left = [Math]::Max(0, ($Deadline - [DateTime]::UtcNow).TotalMilliseconds)
		if (-not $Run.Process.WaitForExit([int]$Left)) {
			# Only the editor this run started, with its shader workers.
			& taskkill /PID $Run.Process.Id /T /F 2>&1 | Out-Null
			$null = $Run.Process.WaitForExit(30000)
			$Hung = Select-String -Path $Run.LogFile -Pattern 'Test Started\. .*Path=\{([^}]+)\}' | Select-Object -Last 1
			$Name = if ($Hung) { $Hung.Matches[0].Groups[1].Value } else { 'before the first test' }
			Write-Host "TIMED OUT after $Minutes min$(if ($Run.Name) { " in shard $($Run.Name)" }), in $Name. Log: $($Run.LogFile)"
			$Broken += "timeout:$Name"
			continue
		}
		$Index = Join-Path $Run.ReportDir 'index.json'
		if (-not (Test-Path $Index)) {
			Write-Host "No test report written$(if ($Run.Name) { " by shard $($Run.Name)" }) (editor exit $($Run.Process.ExitCode)). Log: $($Run.LogFile)"
			if (Test-Path $Run.LogFile) {
				Select-String -Path $Run.LogFile -Pattern 'Error:|Fatal|TEST COMPLETE' | Select-Object -Last 20 | ForEach-Object { Write-Host $_.Line }
			}
			$Broken += "noreport:$($Run.Name)"
			continue
		}
		# The report may carry a UTF-8 BOM; strip it before parsing.
		$Report = (Get-Content -Raw -Encoding UTF8 $Index).TrimStart([char]0xFEFF) | ConvertFrom-Json
		$Tests += @($Report.tests)
	}
	$Seconds = $Clock.Elapsed.TotalSeconds

	$Failed = @($Tests | Where-Object { $_.state -eq 'Fail' })
	foreach ($Test in $Tests) {
		Write-Host ("[{0}] {1}" -f $Test.state, $Test.fullTestPath)
	}
	foreach ($Test in $Failed) {
		Write-Host ''
		Write-Host "FAILED: $($Test.fullTestPath)"
		foreach ($Entry in $Test.entries) {
			if ($Entry.event.type -eq 'Error') { Write-Host "  $($Entry.event.message)" }
		}
	}
	# What -Rerun runs next: the failed tests, and a test a timeout cut off.
	New-Item -ItemType Directory -Force (Split-Path $LastFailedFile) | Out-Null
	$Again = @($Failed | ForEach-Object { $_.fullTestPath }) + @($Broken | Where-Object { $_ -like 'timeout:WorldDivide.*' } | ForEach-Object { $_.Substring(8) })
	[IO.File]::WriteAllLines($LastFailedFile, [string[]]$Again)

	$Slow = Get-SlowTests
	$Timed = @($Tests | Where-Object { $null -ne $_.duration } | Sort-Object { [double]$_.duration } -Descending)
	if ($Timed.Count -gt 0) {
		Write-Host ''
		Write-Host 'Slowest tests:'
		$Timed | Select-Object -First 5 | ForEach-Object { Write-Host ("  {0,6:N1} s  {1}" -f [double]$_.duration, $_.fullTestPath) }
		$Untagged = @($Timed | Where-Object { [double]$_.duration -gt $SlowSeconds -and $_.fullTestPath -notin $Slow })
		if ($Untagged.Count -gt 0) {
			Write-Host "Over $SlowSeconds s without a [Slow] tag in Config/Tests/Tags.ini (add them so -Tier fast stays fast):"
			$Untagged | ForEach-Object { Write-Host ("  {0}=[Slow]   ; {1:N1} s" -f $_.fullTestPath, [double]$_.duration) }
		}
	}

	$Count = { param($State) @($Tests | Where-Object { $_.state -eq $State }).Count }
	Write-Host ''
	Write-Host ("Succeeded: {0}  Failed: {1}  Not run: {2}  in {3:N0} s wall-clock ({4} tier{5})" -f (& $Count 'Success'), $Failed.Count,
		(& $Count 'NotRun'), $Seconds, $Tier, $(if ($Runs.Count -gt 1) { ", $($Runs.Count) shards" } else { '' }))
	return [pscustomobject]@{ Tests = $Tests; Failed = $Failed.Count; Broken = $Broken; Seconds = $Seconds }
}

function Test-RunPassed($Result) {
	return $Result.Broken.Count -eq 0 -and $Result.Tests.Count -gt 0 -and $Result.Failed -eq 0
}

function Invoke-Test {
	if ($Repeat -le 1) {
		$Result = Invoke-TestRun
		if (@($Result.Broken | Where-Object { $_ -like 'timeout:*' }).Count -gt 0) { exit 5 }
		if ($Result.Broken.Count -gt 0) { exit 1 }
		if ($Result.Tests.Count -eq 0) {
			Write-Host "No tests matched filter '$Filter'$(if ($Tier -eq 'fast') { ' outside the [Slow] tests' })."
			exit 1
		}
		if (-not (Test-RunPassed $Result)) { exit 1 }
		Write-Host 'TESTS PASSED'
		return
	}

	# Each run in its own editor, as a developer runs it; a run passes when it ran tests and none failed.
	$Runs = @()
	$Total = [Diagnostics.Stopwatch]::StartNew()
	for ($Run = 1; $Run -le $Repeat; $Run++) {
		Write-Host ''
		Write-Host "=== Run $Run of $Repeat ==="
		$Clock = [Diagnostics.Stopwatch]::StartNew()
		$Result = Invoke-TestRun
		$Seconds = $Clock.Elapsed.TotalSeconds
		$Passed = Test-RunPassed $Result
		$Info = @($Result.Tests | ForEach-Object { $_.entries } | Where-Object { $_.event.type -eq 'Info' } | ForEach-Object { $_.event.message })
		$Runs += [pscustomobject]@{ Run = $Run; Passed = $Passed; Seconds = $Seconds; Info = $Info }
	}

	Write-Host ''
	Write-Host "=== $Filter, $Repeat runs ==="
	foreach ($Entry in $Runs) {
		Write-Host ("Run {0,3}: {1}  {2,6:N0} s" -f $Entry.Run, $(if ($Entry.Passed) { 'PASS' } else { 'FAIL' }), $Entry.Seconds)
	}
	# Info lines that are not the same in every run: under the fixed step a deterministic test has none.
	$Varying = @($Runs | ForEach-Object { $_.Info } | Group-Object | Where-Object { $_.Count -ne $Repeat } | ForEach-Object { $_.Name })
	if ($Varying.Count -gt 0) {
		Write-Host 'Info lines that differ between runs:'
		$Varying | Select-Object -Unique | Select-Object -First 20 | ForEach-Object { Write-Host "  $_" }
	}
	else {
		Write-Host 'Every run reported the same info lines.'
	}
	$PassCount = @($Runs | Where-Object { $_.Passed }).Count
	Write-Host ("Passed {0} of {1} runs in {2:N0} s wall-clock" -f $PassCount, $Repeat, $Total.Elapsed.TotalSeconds)
	if ($PassCount -ne $Repeat) { exit 1 }
	Write-Host 'TESTS PASSED'
}

function Invoke-Editor {
	Assert-EditorClosed 'a second editor cannot serve MCP on the same port.'
	Start-Process -FilePath $EditorExe -ArgumentList @("`"$Project`"", '-ModelContextProtocolStartServer')
	Write-Host "Editor starting. The Unreal MCP server answers at $MCPUrl once the editor has loaded."
}

function Invoke-UIShots {
	Assert-EditorClosed 'the game run loads the same project.'
	$Folder = if ($Panel) { $Panel } else { 'HUD' }
	$ShotDir = Join-Path $RepoRoot "Saved\UIShots\$Folder"
	$LogFile = Join-Path $RepoRoot 'Saved\Logs\UIShots.log'
	if (Test-Path $ShotDir) { Remove-Item -Recurse -Force $ShotDir }

	# Windowed and offscreen, so no screen limits the window to its own size.
	$Process = Start-Process -FilePath $EditorExe -Wait -PassThru -ArgumentList @(
		"`"$Project`"", '/Game/Maps/L_TestMap', '-game', '-windowed', '-RenderOffscreen', '-ResX=1920', '-ResY=1080',
		"`"-ExecCmds=rts.UI.Screenshot $Panel`"", '-RTSUIShotsQuit', "`"-abslog=$LogFile`"", $NoMCPPlugins,
		'-unattended', '-nosplash', '-nosound')
	$GameExit = $Process.ExitCode

	# One shot per faction look and resolution, and with a panel a crop of each.
	$Themes = @(Get-ChildItem (Join-Path $RepoRoot 'UI\Themes') -Filter '*.theme.json' |
		ForEach-Object { $_.Name -replace '\.theme\.json$', '' } | Where-Object { $_ -ne 'Neutral' })
	$Resolutions = @(@(1920, 1080), @(2560, 1440), @(3440, 1440), @(1680, 1050))
	Add-Type -AssemblyName System.Drawing
	$Problems = @()
	foreach ($Theme in $Themes) {
		foreach ($Resolution in $Resolutions) {
			$Name = '{0}_{1}x{2}' -f $Theme, $Resolution[0], $Resolution[1]
			$Shot = Join-Path $ShotDir "$Name.png"
			if (-not (Test-Path $Shot)) {
				$Problems += "missing $Shot"
				continue
			}
			$Image = [System.Drawing.Image]::FromFile($Shot)
			if ($Image.Width -ne $Resolution[0] -or $Image.Height -ne $Resolution[1]) {
				$Problems += "$Shot is $($Image.Width)x$($Image.Height)"
			}
			$Image.Dispose()
			if ($Panel -and -not (Test-Path (Join-Path $ShotDir "$($Name)_crop.png"))) {
				$Problems += "missing the crop of $Name"
			}
		}
	}

	if ($GameExit -ne 0 -or $Problems.Count -gt 0) {
		$Problems | ForEach-Object { Write-Host "  $_" }
		if (Test-Path $LogFile) {
			Select-String -Path $LogFile -Pattern 'rts.UI.Screenshot|Error:|Fatal' | Select-Object -Last 20 | ForEach-Object { $_.Line }
		}
		Write-Host "UISHOTS FAILED (game exit $GameExit). Log: $LogFile"
		exit 1
	}
	Write-Host "UISHOTS SUCCEEDED: $ShotDir"
}

function Invoke-UnitShots {
	Assert-EditorClosed 'the game run loads the same project.'
	$ShotDir = Join-Path $RepoRoot "Saved\UnitShots\$Unit"
	if ($LODs) { $ShotDir = Join-Path $ShotDir 'LODs' }
	$LogFile = Join-Path $RepoRoot 'Saved\Logs\UnitShots.log'
	if (Test-Path $ShotDir) { Remove-Item -Recurse -Force $ShotDir }
	$TraceArgs = @()
	if ($Trace) {
		$TraceFile = Join-Path $RepoRoot "Saved\Traces\UnitShots_$($Unit)_$Stress.utrace"
		New-Item -ItemType Directory -Force (Split-Path $TraceFile) | Out-Null
		if (Test-Path $TraceFile) { Remove-Item -Force $TraceFile }
		$TraceArgs = @('-trace=cpu,frame,region', "`"-tracefile=$TraceFile`"")
	}

	$Process = Start-Process -FilePath $EditorExe -Wait -PassThru -ArgumentList (@(
		"`"$Project`"", '/Game/Maps/L_TestMap', '-game', '-windowed', '-RenderOffscreen', '-ResX=1920', '-ResY=1080',
		"`"-ExecCmds=rts.Debug.UnitShots $Unit $Stress$(if ($LODs) { ' lods' })`"", '-RTSUnitShotsQuit', "`"-abslog=$LogFile`"", $NoMCPPlugins,
		'-unattended', '-nosplash', '-nosound') + $TraceArgs + @(if ($Cvars) { "-dpcvars=$Cvars" }))
	$GameExit = $Process.ExitCode

	$Shots = @('rts_default', 'zoom_in_max', 'zoom_out_max', 'closeup', 'turret_rotated', 'driving', 'obstacle_block',
		'obstacle_along', 'obstacle_corner', 'obstacle_building', 'stats_one')
	if ($Stress -gt 0) { $Shots += "stress_$Stress" }
	if ($LODs) {
		$Shots = @(foreach ($Lod in 0..3) { foreach ($View in 'side', 'front', 'rear', 'three_quarter') { "lod$($Lod)_$View" } }) +
			@('auto_close', 'auto_tactical', 'auto_default', 'auto_max') +
			@(foreach ($Switch in 1..3) { "switch$($Switch)_before"; "switch$($Switch)_after" }) +
			@(foreach ($Lod in 0..3) { "teams_lod$Lod" })
	}
	$Problems = @($Shots | Where-Object { -not (Test-Path (Join-Path $ShotDir "$_.png")) } | ForEach-Object { "missing $_.png" })
	if (Test-Path $LogFile) {
		Select-String -Path $LogFile -Pattern 'rts.Debug.UnitShots: (\S+ over|picked|drove|\S+: the (hull|barrel) overlaps|\S+: no separate barrel|\S+: forced LOD)' | ForEach-Object { $_.Line -replace '^.*rts.Debug.UnitShots: ', '  ' }
	}
	if ($GameExit -ne 0 -or $Problems.Count -gt 0) {
		$Problems | ForEach-Object { Write-Host "  $_" }
		if (Test-Path $LogFile) {
			Select-String -Path $LogFile -Pattern 'rts.Debug|Error:|Fatal' | Select-Object -Last 20 | ForEach-Object { $_.Line }
		}
		Write-Host "UNITSHOTS FAILED (game exit $GameExit). Log: $LogFile"
		exit 1
	}
	if ($Trace) { Write-Host "Trace: $TraceFile" }
	Write-Host "UNITSHOTS SUCCEEDED: $ShotDir"
}

# Close-up frame sequences of a tracked unit's running gear (#151; rts.Debug.TrackShots) to Saved\TrackShots\<Unit>, with
# the gear's travel and each track's phase logged per frame. The motion_* sequences drive past a still camera at full speed,
# motion_*_reprojection with the previous frame's reprojection shown, for smearing under the temporal upscaler (#243).
function Invoke-TrackShots {
	Assert-EditorClosed 'the game run loads the same project.'
	$ShotDir = Join-Path $RepoRoot "Saved\TrackShots\$Unit"
	$LogFile = Join-Path $RepoRoot 'Saved\Logs\TrackShots.log'
	if (Test-Path $ShotDir) { Remove-Item -Recurse -Force $ShotDir }
	$Process = Start-Process -FilePath $EditorExe -Wait -PassThru -ArgumentList (@(
		"`"$Project`"", '/Game/Maps/L_TestMap', '-game', '-windowed', '-RenderOffscreen', '-ResX=1280', '-ResY=720',
		"`"-ExecCmds=rts.Debug.TrackShots $Unit`"", '-RTSTrackShotsQuit', "`"-abslog=$LogFile`"", $NoMCPPlugins,
		'-unattended', '-nosplash', '-nosound') + @(if ($Cvars) { "-dpcvars=$Cvars" }))
	if (Test-Path $LogFile) {
		Select-String -Path $LogFile -Pattern 'rts.Debug.TrackShots: \S+ \d\d:' | ForEach-Object { $_.Line -replace '^.*rts.Debug.TrackShots: ', '  ' }
	}
	if ($Process.ExitCode -ne 0) {
		if (Test-Path $LogFile) { Select-String -Path $LogFile -Pattern 'rts.Debug|Error:|Fatal' | Select-Object -Last 20 | ForEach-Object { $_.Line } }
		Write-Host "TRACKSHOTS FAILED (game exit $($Process.ExitCode)). Log: $LogFile"
		exit 1
	}
	Write-Host "TRACKSHOTS SUCCEEDED: $ShotDir"
}

function Invoke-CameraShots {
	Assert-EditorClosed 'the game run loads the same project.'
	$ShotDir = Join-Path $RepoRoot 'Saved\CameraShots'
	$LogFile = Join-Path $RepoRoot 'Saved\Logs\CameraShots.log'
	if (Test-Path $ShotDir) { Remove-Item -Recurse -Force $ShotDir }

	$Process = Start-Process -FilePath $EditorExe -Wait -PassThru -ArgumentList @(
		"`"$Project`"", '/Game/Maps/L_TestMap', '-game', '-windowed', '-RenderOffscreen', '-ResX=1920', '-ResY=1080',
		'"-ExecCmds=rts.Debug.CameraShots"', '-RTSCameraShotsQuit', "`"-abslog=$LogFile`"", $NoMCPPlugins,
		'-unattended', '-nosplash', '-nosound')
	$GameExit = $Process.ExitCode
	$Csv = Join-Path $ShotDir 'metrics.csv'
	if ($GameExit -ne 0 -or -not (Test-Path $Csv)) {
		if (Test-Path $LogFile) {
			Select-String -Path $LogFile -Pattern 'rts.Debug|Error:|Fatal' | Select-Object -Last 20 | ForEach-Object { $_.Line }
		}
		Write-Host "CAMERASHOTS FAILED (game exit $GameExit). Log: $LogFile"
		exit 1
	}
	& (Join-Path $PSScriptRoot 'camera_sheet.ps1') -ShotDir $ShotDir
	Write-Host "CAMERASHOTS SUCCEEDED: $ShotDir"
}

# The repository's git, quiet; returns its output trimmed.
function Invoke-Git {
	$ErrorActionPreference = 'Continue'
	$Output = & git -C $RepoRoot @args 2>$null
	if ($null -eq $Output) { return '' }
	return ($Output -join "`n").Trim()
}

function Read-Json([string]$File) { return Get-Content -Raw -Path $File | ConvertFrom-Json }

function Write-Json([string]$File, $Value) {
	[IO.File]::WriteAllText($File, ($Value | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding $false))
}

# The content files a unit is made of (meshes, materials, textures, animations), from the gate's last plan.json (#171).
function Get-UnitPackageFiles([string]$Name) {
	$PlanFile = Join-Path $RepoRoot 'Saved\Bench\Gate\plan.json'
	if (-not (Test-Path $PlanFile)) { return @() }
	$Entry = (Read-Json $PlanFile).roster | Where-Object { $_.name -eq $Name }
	if (-not $Entry) { return @() }
	return @($Entry.packages | ForEach-Object {
		$Base = 'Content/' + $_.Substring(6)
		if (Test-Path (Join-Path $RepoRoot "$Base.umap")) { "$Base.umap" } else { "$Base.uasset" }
	})
}

# What bench.json leaves to the repository: the commit, whether the tree had changes, and the hashes of the bench's code, its
# map, the set's unit data and, once a gate has planned, every content file the set's units are made of, so a comparison
# names what differs from its baseline.
function Get-BenchGitFacts([string]$Set) {
	$Hashes = [ordered]@{}
	$Units = @(($Set -replace '@Solids$', '') -split '\+')
	$Files = @('Source/WorldDivide/Units/Debug/RTSBenchCommand.cpp', 'Source/WorldDivide/Units/Debug/RTSBenchPlan.h',
		'Source/WorldDivide/Units/Debug/RTSBenchScene.cpp', 'Content/Maps/L_Bench.umap') +
		($Units | ForEach-Object { "Content/Data/Units/$_.uasset" })
	$Files = @($Files + ($Units | ForEach-Object { Get-UnitPackageFiles $_ }) | Select-Object -Unique)
	foreach ($File in $Files) {
		$Full = Join-Path $RepoRoot $File
		$Hashes[$File] = if (Test-Path $Full) { Invoke-Git hash-object $Full } else { 'missing' }
	}
	return [ordered]@{
		sha    = Invoke-Git rev-parse HEAD
		dirty  = [bool](Invoke-Git status --porcelain)
		hashes = $Hashes
	}
}

function Get-BaselineDir([string]$Sha, [string]$Method, [string]$Set) {
	return Join-Path $RepoRoot "Saved\Bench\Baselines\$Sha\$Method\$Set"
}

# The baseline a -Compare run is read against: the commit given by -Baseline, or HEAD, which the uncommitted change sits on.
# Returns @{ Sha; Behind } or exits: a commit that is not HEAD or one of its ancestors is refused.
function Resolve-BenchBaseline {
	$Head = Invoke-Git rev-parse HEAD
	if (-not $Baseline) { return @{ Sha = $Head; Behind = 0 } }
	$Sha = Invoke-Git rev-parse --verify --quiet "$Baseline^{commit}"
	if (-not $Sha) {
		Write-Host "BENCH FAILED: -Baseline $Baseline is no commit."
		exit 1
	}
	$ErrorActionPreference = 'Continue'
	& git -C $RepoRoot merge-base --is-ancestor $Sha $Head 2>$null
	if ($LASTEXITCODE -ne 0) {
		Write-Host "BENCH FAILED: -Baseline $Baseline ($($Sha.Substring(0, 7))) is not HEAD or an ancestor of it."
		exit 1
	}
	return @{ Sha = $Sha; Behind = [int](Invoke-Git rev-list --count "$Sha..$Head") }
}

# The fields of two bench.json sections that differ, as "section.field: baseline -> now".
function Compare-JsonSection([string]$Name, $Before, $Now) {
	$Differences = @()
	$Fields = @($Before.PSObject.Properties.Name) + @($Now.PSObject.Properties.Name) | Select-Object -Unique
	foreach ($Field in $Fields) {
		$B = $Before.$Field | ConvertTo-Json -Compress -Depth 4
		$N = $Now.$Field | ConvertTo-Json -Compress -Depth 4
		if ($B -ne $N) { $Differences += "${Name}.${Field}: $B -> $N" }
	}
	return $Differences
}

# Compares the run of Set in OutDir with its stored baseline and writes compare.md there; false when they are not comparable.
function Compare-BenchRun([string]$Set, [string]$OutDir, $Chosen, [string]$Method) {
	$BaseDir = Get-BaselineDir $Chosen.Sha $Method $Set
	$Before = Read-Json (Join-Path $BaseDir 'bench.json')
	$Now = Read-Json (Join-Path $OutDir 'bench.json')
	$Short = $Chosen.Sha.Substring(0, 7)

	# The method, the scene and the machine must be the same: a mismatch is never compared.
	$Hard = @()
	foreach ($Section in 'method', 'scene', 'machine') { $Hard += Compare-JsonSection $Section $Before.$Section $Now.$Section }
	if ($Hard.Count -gt 0) {
		Write-Host "INCOMPATIBLE BASELINE for $Set at $Short ($Method). Regenerate it with -SaveBaseline on a clean checkout:"
		$Hard | ForEach-Object { Write-Host "  $_" }
		return $false
	}

	$Lines = @("# $Set, $Method method: baseline $Short against $($Now.git.sha.Substring(0, 7))$(if ($Now.git.dirty) { ' with uncommitted changes' })", '')
	if ($Method -in 'quick', 'screen') { $Lines += "**$Method method: diagnostic, not gate evidence.** The gate is the long method (``-Long``)."; $Lines += '' }
	if ($Chosen.Behind -gt 0) {
		$Lines += "**OUTDATED baseline:** $Short is $($Chosen.Behind) commits behind HEAD; the difference includes those commits."
		$Lines += ''
	}
	# What differs in the repository is the change under test, named so it is never silent.
	$Soft = @()
	if ($Before.git.dirty) { $Soft += 'the baseline had uncommitted changes' }
	foreach ($File in $Now.git.hashes.PSObject.Properties.Name) {
		# A file the baseline did not hash (an older baseline, before the gate named the set's content) is not a change.
		if ($null -ne $Before.git.hashes.$File -and $Before.git.hashes.$File -ne $Now.git.hashes.$File) { $Soft += "$File changed" }
	}
	$Lines += "- Baseline: $($Before.date), now $($Now.date); $($Now.machine.cpu), $($Now.machine.gpu) $($Now.machine.driver), $($Now.machine.rhi), $($Now.machine.build)"
	$Lines += "- Changed since the baseline: $(if ($Soft) { $Soft -join '; ' } else { 'nothing the bench reads' })"
	$Lines += '- Read the game thread against the run-to-run spread of about 0.6 ms (marked **bold** when beyond it).'
	$Lines += ''

	$Columns = 'frame_ms', 'game_ms', 'p95_game_ms', 'max_game_ms', 'render_ms', 'gpu_ms', 'avoidance_ms', 'locomotion_ms', 'p95_locomotion_ms'
	$Lines += '| Units | Scenario | Sample | Frame ms | Game ms | Game p95 | Game max | Render ms | GPU ms | Avoidance ms | Locomotion ms | Locomotion p95 |'
	$Lines += '|---:|---|---:|---|---|---|---|---|---|---|---|---|'
	$BeforeRows = Import-Csv (Join-Path $BaseDir 'bench.csv')
	foreach ($Row in Import-Csv (Join-Path $OutDir 'bench.csv')) {
		$Match = $BeforeRows | Where-Object { $_.count -eq $Row.count -and $_.scenario -eq $Row.scenario -and $_.sample -eq $Row.sample }
		$Cells = foreach ($Column in $Columns) {
			if (-not $Match) { '-'; continue }
			$B = [double]$Match.$Column; $N = [double]$Row.$Column; $D = $N - $B
			$Cell = [string]::Format([Globalization.CultureInfo]::InvariantCulture, '{0:0.00} -> {1:0.00} ({2:+0.00;-0.00;0.00})', $B, $N, $D)
			if ($Column -eq 'game_ms' -and [Math]::Abs($D) -gt 0.6) { "**$Cell**" } else { $Cell }
		}
		$Lines += "| $($Row.count) | $($Row.scenario) | $($Row.sample) | $($Cells -join ' | ') |"
	}
	$File = Join-Path $OutDir 'compare.md'
	[IO.File]::WriteAllLines($File, [string[]]$Lines, (New-Object Text.UTF8Encoding $false))
	$Lines | ForEach-Object { Write-Host $_ }
	return $true
}

function Invoke-Bench {
	Assert-EditorClosed 'the game run loads the same project.'
	if (@($Quick, $Long, $Screen | Where-Object { $_ }).Count -gt 1) { Write-Host 'BENCH FAILED: -Quick, -Screen and -Long are methods; pick one.'; exit 1 }
	if ($SaveBaseline -and $Compare) { Write-Host 'BENCH FAILED: -SaveBaseline stores the run of a clean checkout; -Compare reads a change against it. Pick one.'; exit 1 }
	if ($Baseline -and -not $Compare) { Write-Host 'BENCH FAILED: -Baseline selects the baseline of -Compare.'; exit 1 }
	if ($Cvars -and $SaveBaseline) { Write-Host 'BENCH FAILED: -Cvars changes what is measured; such a run is never a baseline.'; exit 1 }
	$Method = if ($Quick) { 'quick' } elseif ($Screen) { 'screen' } elseif ($Long) { 'long' } else { 'short' }
	# Several unit sets are separated by ','; '+' joins the sizes of one mixed set.
	$Sets = @($Unit -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
	$LogFile = Join-Path $RepoRoot 'Saved\Logs\Bench.log'

	if ($SaveBaseline -and (Invoke-Git status --porcelain)) {
		Write-Host 'BENCH FAILED: -SaveBaseline needs a clean worktree, so the baseline is exactly its commit. Commit or set aside the changes:'
		Invoke-Git status --short | Write-Host
		exit 1
	}
	if ($Compare) {
		$Chosen = Resolve-BenchBaseline
		$Missing = @($Sets | Where-Object { -not (Test-Path (Join-Path (Get-BaselineDir $Chosen.Sha $Method $_) 'bench.json')) })
		if ($Missing) {
			$Short = $Chosen.Sha.Substring(0, 7)
			Write-Host "NO BASELINE at $Short for the $Method method of: $($Missing -join ', ')"
			$Stored = Join-Path $RepoRoot 'Saved\Bench\Baselines'
			if (Test-Path $Stored) {
				Write-Host 'Stored baselines (commit\method\set):'
				Get-ChildItem $Stored -Directory | ForEach-Object { Get-ChildItem $_.FullName -Directory | ForEach-Object { Get-ChildItem $_.FullName -Directory } } |
					ForEach-Object { Write-Host "  $($_.FullName.Substring($Stored.Length + 1))" }
			}
			$MethodArg = if ($Quick) { ' -Quick' } elseif ($Screen) { ' -Screen' } elseif ($Long) { ' -Long' } else { '' }
			$CountsArg = if ($Counts) { " -Counts $Counts" } else { '' }
			Write-Host "Make it on a clean checkout of $Short with: ue.ps1 -Task bench$MethodArg -Unit $($Missing -join ',')$CountsArg -SaveBaseline"
			Write-Host 'or name an older one with -Baseline <commit> (it is marked OUTDATED).'
			exit 1
		}
	}

	if ($SaveBaseline -and $SampleSeconds -gt 0) { Write-Host 'BENCH FAILED: -SampleSeconds is for method studies; such a run is never a baseline.'; exit 1 }

	# -ExecCmds splits its commands at commas: one rts.Debug.Bench per set, which the game queues, and the counts joined by '+'.
	$BenchCounts = ($Counts -split '[,\s+]+' | Where-Object { $_ }) -join '+'
	if (-not $BenchCounts) { $BenchCounts = if ($Quick -or $Screen) { '240' } else { '1+30+120+240' } }
	$TraceName = "Bench_$($Sets -join '_')"
	$Session = Invoke-BenchSession $Sets $Method $BenchCounts $TraceName
	if ($Session.Exit -ne 0 -or $Session.Failed) {
		if (Test-Path $Session.Log) {
			Select-String -Path $Session.Log -Pattern 'rts.Debug|Error:|Fatal' | Select-Object -Last 20 | ForEach-Object { $_.Line }
		}
		Write-Host "BENCH FAILED (game exit $($Session.Exit)$(if ($Session.Failed) { "; no results for $($Session.Failed -join ', ')" })). Log: $($Session.Log)"
		exit 1
	}

	$Incompatible = $false
	$Refused = @()
	foreach ($Set in $Sets) {
		$OutDir = Join-Path $RepoRoot "Saved\Bench\$Set"
		Get-Content (Join-Path $OutDir 'bench.md') | ForEach-Object { Write-Host $_ }
		Write-Host ''
		if ($SaveBaseline -and -not (Save-BenchBaseline $Set $Method)) { $Refused += $Set }
		if ($Compare -and -not (Compare-BenchRun $Set $OutDir $Chosen $Method)) { $Incompatible = $true }
	}
	if ($Session.Trace) { Write-Host "Trace: $($Session.Trace)" }
	Write-Host "Wall clock: $($Session.Seconds) s for $($Sets.Count) set(s), $Method method$(if ($Method -in 'quick', 'screen') { ' (diagnostic, not gate evidence)' })"
	if ($Refused) {
		Write-Host "BENCH FAILED: no baseline stored for $($Refused -join ', '): the run is not a valid measurement (see above). Run it again on an idle machine."
		exit 1
	}
	if ($Incompatible) {
		Write-Host 'BENCH FAILED: a baseline is incompatible (see above); its results were not compared.'
		exit 1
	}
	Write-Host "BENCH SUCCEEDED: $(($Sets | ForEach-Object { Join-Path $RepoRoot "Saved\Bench\$_" }) -join ', ')"
}

# Mean cores the other processes of the machine used, kept equal to RTSBenchGate::Validity::MaxOtherCores (#171).
$MaxOtherCores = 1.0

# The CPU seconds of every process but the game and this script, by id. Protected processes the user cannot read are left out.
function Get-OtherCpuSeconds([int]$GameId) {
	$Seconds = @{}
	foreach ($Process in Get-Process) {
		if ($Process.Id -in @(0, $GameId, $PID)) { continue }
		$Time = $Process.TotalProcessorTime
		if ($null -ne $Time) { $Seconds[$Process.Id] = $Time.TotalSeconds }
	}
	return $Seconds
}

function ConvertTo-UtcDate($Value) {
	if ($Value -is [DateTime]) { return $Value.ToUniversalTime() }
	return [DateTime]::Parse([string]$Value, [Globalization.CultureInfo]::InvariantCulture,
		[Globalization.DateTimeStyles]::AssumeUniversal -bor [Globalization.DateTimeStyles]::AdjustToUniversal)
}

# The machine's other load while one set ran: the samples of the session's timeline that overlap the set's run, weighted by
# the overlap. $null when no sample overlaps.
function Get-SetEnvironment($Run, $Timeline) {
	if (-not $Run -or -not $Run.start -or -not $Run.end) { return $null }
	$Start = ConvertTo-UtcDate $Run.start
	$End = ConvertTo-UtcDate $Run.end
	$Weighted = 0.0; $Total = 0.0; $Max = 0.0; $Shader = $false; $Count = 0
	foreach ($Sample in $Timeline) {
		$From = if ($Sample.Start -gt $Start) { $Sample.Start } else { $Start }
		$To = if ($Sample.End -lt $End) { $Sample.End } else { $End }
		$Overlap = ($To - $From).TotalSeconds
		if ($Overlap -le 0) { continue }
		$Weighted += $Sample.Cores * $Overlap; $Total += $Overlap; $Count++
		if ($Sample.Cores -gt $Max) { $Max = $Sample.Cores }
		if ($Sample.Shader) { $Shader = $true }
	}
	if ($Count -eq 0) { return $null }
	return [ordered]@{
		other_cores     = [Math]::Round($Weighted / $Total, 3)
		other_cores_max = [Math]::Round($Max, 3)
		shader_workers  = $Shader
		samples         = $Count
	}
}

# One game session on L_Bench that benches Sets one after another with Method, while the other processes' CPU is sampled
# every few seconds. Each set's bench.json gets the commit and hashes (git), the session, and the machine's other load while
# it ran (environment). Returns the exit code, the sets without results, the wall clock, the log and the trace file.
function Invoke-BenchSession([string[]]$Sets, [string]$Method, [string]$BenchCounts, [string]$TraceName) {
	$LogFile = Join-Path $RepoRoot 'Saved\Logs\Bench.log'
	foreach ($Set in $Sets) {
		$OutDir = Join-Path $RepoRoot "Saved\Bench\$Set"
		if (Test-Path $OutDir) { Remove-Item -Recurse -Force $OutDir }
	}
	$ExtraArgs = @()
	$TraceFile = ''
	if ($Trace) {
		$TraceFile = Join-Path $RepoRoot "Saved\Traces\$TraceName.utrace"
		New-Item -ItemType Directory -Force (Split-Path $TraceFile) | Out-Null
		if (Test-Path $TraceFile) { Remove-Item -Force $TraceFile }
		$ExtraArgs += @('-trace=cpu,frame,region,gpu', "`"-tracefile=$TraceFile`"")
	}
	# Console variables for a diagnostic run, such as rts.Vehicle.TrackAnimationDistance=0 (#151); never stored as a baseline.
	if ($Cvars) { $ExtraArgs += "-dpcvars=$Cvars" }
	if ($Frames) { $ExtraArgs += '-RTSBenchFrames' }
	if ($SampleSeconds -gt 0) { $ExtraArgs += [string]::Format([Globalization.CultureInfo]::InvariantCulture, '-RTSBenchSampleSeconds={0}', $SampleSeconds) }

	$BenchMethod = if ($Method -eq 'short') { '' } else { " $Method" }
	$ExecCmds = ($Sets | ForEach-Object { "rts.Debug.Bench $_ $BenchCounts$BenchMethod" }) -join ','
	$Clock = [Diagnostics.Stopwatch]::StartNew()
	$Process = Start-Process -FilePath $EditorExe -PassThru -ArgumentList (@(
		"`"$Project`"", "/Game/Maps/$Map", '-game', '-windowed', '-RenderOffscreen', '-ResX=1920', '-ResY=1080',
		"`"-ExecCmds=$ExecCmds`"", '-RTSBenchQuit', "`"-abslog=$LogFile`"", $NoMCPPlugins,
		'-unattended', '-nosplash', '-nosound') + $ExtraArgs)
	# Holding the handle keeps the exit code readable once the process has gone.
	$null = $Process.Handle
	$Timeline = @()
	$Last = Get-OtherCpuSeconds $Process.Id
	$LastTime = [DateTime]::UtcNow
	while (-not $Process.WaitForExit(5000)) {
		$Now = Get-OtherCpuSeconds $Process.Id
		$NowTime = [DateTime]::UtcNow
		$Busy = 0.0
		foreach ($Id in $Now.Keys) {
			$Before = if ($Last.ContainsKey($Id)) { $Last[$Id] } else { 0.0 }
			$Busy += [Math]::Max(0.0, $Now[$Id] - $Before)
		}
		$Timeline += [pscustomobject]@{
			Start  = $LastTime
			End    = $NowTime
			Cores  = $Busy / [Math]::Max(0.001, ($NowTime - $LastTime).TotalSeconds)
			Shader = [bool](Get-Process -Name 'ShaderCompileWorker' -ErrorAction SilentlyContinue)
		}
		$Last = $Now
		$LastTime = $NowTime
	}
	$Process.WaitForExit()
	$Clock.Stop()
	$Seconds = [Math]::Round($Clock.Elapsed.TotalSeconds)

	$Failed = @($Sets | Where-Object { -not (Test-Path (Join-Path $RepoRoot "Saved\Bench\$_\bench.md")) })
	foreach ($Set in $Sets) {
		$JsonFile = Join-Path $RepoRoot "Saved\Bench\$Set\bench.json"
		if (-not (Test-Path $JsonFile)) { continue }
		$Json = Read-Json $JsonFile
		$Json | Add-Member -NotePropertyName git -NotePropertyValue (Get-BenchGitFacts $Set) -Force
		$Json | Add-Member -NotePropertyName session -NotePropertyValue ([ordered]@{ sets = $Sets; wall_clock_s = $Seconds }) -Force
		$Environment = Get-SetEnvironment $Json.run $Timeline
		if ($Environment) { $Json | Add-Member -NotePropertyName environment -NotePropertyValue $Environment -Force }
		Write-Json $JsonFile $Json
	}
	return @{ Exit = $Process.ExitCode; Failed = $Failed; Seconds = $Seconds; Log = $LogFile; Trace = $TraceFile }
}

# Why the run of Set in Saved/Bench/<Set> is no valid measurement, from its own checks (run, #171) and the machine's other
# load while it ran (environment); empty when it is valid.
function Get-BenchInvalidity([string]$Set) {
	$Json = Read-Json (Join-Path $RepoRoot "Saved\Bench\$Set\bench.json")
	$Problems = @()
	if (-not $Json.run) { return @('no validity record') }
	if (-not $Json.run.valid) { $Problems += @($Json.run.flags) }
	if ($Json.run.trace) { $Problems += 'an Unreal Insights trace was recording' }
	if (-not $Json.environment) { $Problems += 'no record of the machine''s other load' }
	elseif ($Json.environment.other_cores -gt $MaxOtherCores) { $Problems += "other processes used $($Json.environment.other_cores) cores while it ran (at most $MaxOtherCores)" }
	if ($Json.environment.shader_workers) { $Problems += 'shaders were compiling while it ran' }
	return $Problems
}

# Stores the run of Set as the baseline of its commit, unless it is no valid measurement; false when refused.
function Save-BenchBaseline([string]$Set, [string]$Method) {
	$OutDir = Join-Path $RepoRoot "Saved\Bench\$Set"
	$Problems = @(Get-BenchInvalidity $Set)
	if ($Problems) {
		Write-Host "BASELINE REFUSED for ${Set}: not a valid measurement:"
		$Problems | ForEach-Object { Write-Host "  $_" }
		return $false
	}
	$Json = Read-Json (Join-Path $OutDir 'bench.json')
	$BaseDir = Get-BaselineDir $Json.git.sha $Method $Set
	if (Test-Path $BaseDir) { Remove-Item -Recurse -Force $BaseDir; Write-Host "Replacing the stored baseline $BaseDir" }
	New-Item -ItemType Directory -Force $BaseDir | Out-Null
	Copy-Item (Join-Path $OutDir 'bench.*') $BaseDir
	Write-Host "BASELINE STORED: $BaseDir"
	return $true
}

# The RTSBenchGate commandlet with Arguments; returns its exit code, its log in LogFile.
function Invoke-GateCommandlet([string[]]$Arguments, [string]$LogFile) {
	& $EditorCmd $Project -run=RTSBenchGate @Arguments "-abslog=$LogFile" $NoMCPPlugins -unattended -nullrhi -nosplash -nosound | Out-Null
	return $LASTEXITCODE
}

# Copies each set's run to the gate folder of its method, where the judge reads it.
function Copy-GateRuns([string[]]$Sets, [string]$Method, [string]$GateDir) {
	foreach ($Set in $Sets) {
		$Target = Join-Path $GateDir "$Method\$Set"
		if (Test-Path $Target) { Remove-Item -Recurse -Force $Target }
		New-Item -ItemType Directory -Force $Target | Out-Null
		$Source = Join-Path $RepoRoot "Saved\Bench\$Set"
		if (Test-Path $Source) { Copy-Item (Join-Path $Source '*') $Target }
	}
}

function Invoke-GateSession([string[]]$Sets, [string]$Method, [string]$GateDir) {
	if (-not $Sets) { return }
	$Counts = if ($Method -eq 'screen') { '240' } else { '1+30+120+240' }
	Write-Host "Gate: $Method method of $($Sets.Count) set(s): $($Sets -join ', ')"
	$Session = Invoke-BenchSession $Sets $Method $Counts "Gate_$Method"
	if ($Session.Exit -ne 0 -or $Session.Failed) {
		Write-Host "  the session ended with exit $($Session.Exit)$(if ($Session.Failed) { "; no results for $($Session.Failed -join ', ')" }); the judge reads them as incomplete. Log: $($Session.Log)"
	}
	Write-Host "  wall clock $($Session.Seconds) s"
	if ($SaveBaseline) {
		foreach ($Set in $Sets) {
			if (Test-Path (Join-Path $RepoRoot "Saved\Bench\$Set\bench.json")) { $null = Save-BenchBaseline $Set $Method }
		}
	}
	Copy-GateRuns $Sets $Method $GateDir
}

function Invoke-Gate {
	Assert-EditorClosed 'the game run loads the same project.'
	if ($Quick -or $Screen -or $Long -or $Compare -or $Trace -or $Frames -or $SampleSeconds -gt 0) {
		Write-Host 'GATE FAILED: the gate picks its own methods; -Quick, -Screen, -Long, -Compare, -Trace, -Frames and -SampleSeconds belong to -Task bench.'
		exit 1
	}
	if ($SaveBaseline -and (Invoke-Git status --porcelain)) {
		Write-Host 'GATE FAILED: -SaveBaseline needs a clean worktree, so each baseline is exactly its commit.'
		exit 1
	}
	$Chosen = Resolve-BenchBaseline
	$BaselineDir = Join-Path $RepoRoot "Saved\Bench\Baselines\$($Chosen.Sha)"
	$GateDir = Join-Path $RepoRoot 'Saved\Bench\Gate'
	$LogFile = Join-Path $RepoRoot 'Saved\Logs\BenchGate.log'
	if (Test-Path $GateDir) { Remove-Item -Recurse -Force $GateDir }
	New-Item -ItemType Directory -Force $GateDir | Out-Null
	$Clock = [Diagnostics.Stopwatch]::StartNew()

	# Everything that differs from the baseline commit: its descendants' commits and the uncommitted and new files.
	$ErrorActionPreference = 'Continue'
	$Changed = @(& git -C $RepoRoot diff --name-only $Chosen.Sha 2>$null) + @(& git -C $RepoRoot ls-files --others --exclude-standard 2>$null)
	$ErrorActionPreference = 'Stop'
	$ChangesFile = Join-Path $GateDir 'changes.txt'
	[IO.File]::WriteAllLines($ChangesFile, [string[]]@($Changed | Where-Object { $_ } | Select-Object -Unique), (New-Object Text.UTF8Encoding $false))

	$PlanArgs = @('-Plan', "-Gate=$GateDir", "-Changes=$ChangesFile", "-Screens=$(Join-Path $BaselineDir 'screen')", "-Longs=$(Join-Path $BaselineDir 'long')")
	if ($Scope -eq 'broad') { $PlanArgs += '-Broad' }
	if ((Invoke-GateCommandlet $PlanArgs $LogFile) -ne 0) {
		Select-String -Path $LogFile -Pattern 'RTSBenchGate|Error:|Fatal' | Select-Object -Last 20 | ForEach-Object { $_.Line }
		Write-Host "GATE FAILED: no plan. Log: $LogFile"
		exit 1
	}
	$Plan = Read-Json (Join-Path $GateDir 'plan.json')
	Write-Host "Gate against $($Chosen.Sha.Substring(0, 7))$(if ($Chosen.Behind -gt 0) { " (OUTDATED: $($Chosen.Behind) commits behind HEAD)" }), scope $(if ($Plan.broad) { 'broad' } else { 'the changed units' })"
	Write-Host "  representatives: $($Plan.representatives -join ', '); mixed set: $($Plan.mixed)"
	Write-Host "  screen: $(if ($Plan.screen) { $Plan.screen -join ', ' } else { 'none' })"
	Write-Host "  long: $(if ($Plan.long) { $Plan.long -join ', ' } else { 'none' })"

	$JudgeArgs = @('-Judge', "-Gate=$GateDir", "-Baselines=$BaselineDir")
	$Screens = @($Plan.screen)
	if ($Screens) {
		Invoke-GateSession $Screens 'screen' $GateDir
		$null = Invoke-GateCommandlet ($JudgeArgs + '-Stage=screen') $LogFile
		# A screen that was no valid measurement runs once more, in a session of its own.
		$Rerun = @((Read-Json (Join-Path $GateDir 'screen.json')).rerun)
		if ($Rerun) {
			Write-Host "Gate: running the screens that were no valid measurement once more: $($Rerun -join ', ')"
			Invoke-GateSession $Rerun 'screen' $GateDir
			$null = Invoke-GateCommandlet ($JudgeArgs + '-Stage=screen') $LogFile
		}
	}
	$Escalated = if (Test-Path (Join-Path $GateDir 'screen.json')) { @((Read-Json (Join-Path $GateDir 'screen.json')).escalate) } else { @() }
	if ($Escalated) { Write-Host "Gate: escalated to the long method: $($Escalated -join ', ')" }
	$Longs = @(@($Plan.long) + $Escalated | Where-Object { $_ } | Select-Object -Unique)
	Invoke-GateSession $Longs 'long' $GateDir

	$Verdict = Invoke-GateCommandlet ($JudgeArgs + '-Stage=final') $LogFile
	$Clock.Stop()
	$Report = Join-Path $GateDir 'gate.md'
	if (-not (Test-Path $Report)) {
		Select-String -Path $LogFile -Pattern 'RTSBenchGate|Error:|Fatal' | Select-Object -Last 20 | ForEach-Object { $_.Line }
		Write-Host "GATE FAILED: no verdict. Log: $LogFile"
		exit 1
	}
	Get-Content $Report | ForEach-Object { Write-Host $_ }
	Write-Host ''
	Write-Host "Wall clock: $([Math]::Round($Clock.Elapsed.TotalMinutes, 1)) min. Report: $Report and gate.json"
	switch ($Verdict) {
		0 { Write-Host 'GATE PASS' }
		4 { Write-Host 'GATE INCONCLUSIVE: not a pass; see Next above.' }
		default { Write-Host 'GATE FAIL' }
	}
	exit $Verdict
}

# The headless UI commandlet (URTSUICommandlet) with Argument; returns its exit code, its log in LogFile.
function Invoke-UICommandlet([string]$Argument, [string]$LogFile) {
	& $EditorCmd $Project -run=RTSUI $Argument "-abslog=$LogFile" $NoMCPPlugins -unattended -nullrhi -nosplash -nosound | Out-Null
	return $LASTEXITCODE
}

function Invoke-UIBindings {
	$LogFile = Join-Path $RepoRoot 'Saved\Logs\UIBindings.log'
	$File = Join-Path $RepoRoot $Out
	New-Item -ItemType Directory -Force (Split-Path $File) | Out-Null
	if (Test-Path $File) { Remove-Item -Force $File }
	$Exit = Invoke-UICommandlet "-Bindings=$Out" $LogFile
	if ($Exit -ne 0 -or -not (Test-Path $File)) {
		Select-String -Path $LogFile -Pattern 'RTSUI|Error:|Fatal' | Select-Object -Last 20 | ForEach-Object { $_.Line }
		Write-Host "UIBINDINGS FAILED (exit $Exit). Log: $LogFile"
		exit 1
	}
	Write-Host "UIBINDINGS SUCCEEDED: $File"
}

function Invoke-UICheck {
	if (-not $Path -or -not (Test-Path (Join-Path $RepoRoot $Path) -PathType Container)) {
		Write-Host "UICHECK FAILED: -Path must name a UI folder under the repository, got '$Path'."
		exit 1
	}
	$LogFile = Join-Path $RepoRoot 'Saved\Logs\UICheck.log'
	$Exit = Invoke-UICommandlet "-Validate=$Path" $LogFile
	$Problems = @(Select-String -Path $LogFile -Pattern 'UICHECK: (.*)$' | ForEach-Object { $_.Matches[0].Groups[1].Value })
	$Problems | ForEach-Object { Write-Host "UI/Layouts/$_" }
	if ($Exit -ne 0) {
		if ($Problems.Count -eq 0) {
			Select-String -Path $LogFile -Pattern 'RTSUI|Error:|Fatal' | Select-Object -Last 20 | ForEach-Object { $_.Line }
		}
		Write-Host "UICHECK FAILED (exit $Exit, $($Problems.Count) problem(s)). Log: $LogFile"
		exit 1
	}
	Write-Host 'UICHECK SUCCEEDED'
}

switch ($Task) {
	'build' { Invoke-Build }
	'test' { Invoke-Test }
	'setup' { Invoke-Setup }
	'editor' { Invoke-Editor }
	'uishots' { Invoke-UIShots }
	'unitshots' { Invoke-UnitShots }
	'trackshots' { Invoke-TrackShots }
	'camerashots' { Invoke-CameraShots }
	'bench' { Invoke-Bench }
	'gate' { Invoke-Gate }
	'uibindings' { Invoke-UIBindings }
	'uicheck' { Invoke-UICheck }
}
