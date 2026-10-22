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
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task setup -Lock
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bakemap -Map /Game/Maps/L_RiverFord -Render
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task rivercrossing
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task rivercrossing -Rebuild -Render
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task editor
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task uishots
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task uishots -Panel ResourceBar
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task unitshots -Unit DA_TestM20Guardian -Stress 240
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task unitshots -Unit DA_TestTank -Stress 240 -Trace
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task unitshots -Unit DA_TestM20Guardian -LODs
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task trackshots -Unit DA_TestM20Guardian
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task camerashots
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task camerashots -Map /Game/Maps/WD_RiverCrossing_01
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bench -Unit DA_TestM20Guardian
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bench -Unit DA_TestTank -Counts 240 -Trace
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bench -Unit DA_TestM20Guardian -Long
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bench -Unit DA_TestM20Guardian,DA_TestTank -Quick -Compare
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bench -Unit DA_TestM20Guardian,DA_TestTank -Long -SaveBaseline
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task setup -BenchHQ
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task bench -Unit DA_TestM20Guardian,DA_TestM20Guardian@NaniteHQ,DA_TestM20Guardian@LodHQ -Quick -Passes -Shots
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task buildingshots -Building DA_BenchHQ_Nanite
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task buildingshots -Building DA_BenchHQ_Nanite -Cvars r.Nanite=0
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task gate
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task packaudit
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task surfaces -Path D:\Downloads\textures
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
Setup stops before its slow imports when an asset it must rewrite is read-only and lists them all in
Saved/Setup/ReadOnlyAssets.txt (#350). -Lock (setup, bakemap, rivercrossing) then git lfs locks the listed files, clears a
read-only flag the lock left and runs once more; unlock them after the push. -BenchHQ also builds the bench's dense HQ
(#349) under the git-ignored Content/Bench/USAHQ: DA_BenchHQ_Nanite, the HQ's static shell as delivered with Nanite; about
10 minutes the first time.
Bakemap bakes the level -Map (/Game/Maps/<Level> or <Level>) into its map data asset DA_Map<Name>, or -DataAsset DA_<Name>:
its preview (rendered with -Render, drawn without), the area the preview covers and its start spots (Tools/Editor/bake_map.py,
docs/world/maps/AUTHORING_A_MAP.md). It exits 3 like setup when the asset or the preview texture is read-only.
Rivercrossing builds and saves the navmesh of /Game/Maps/WD_RiverCrossing_01, first creating its scripted greybox when the
level does not exist yet (Tools/Editor/setup_rivercrossing.py, #154); bake it afterwards with bakemap. It exits 3 like setup
when an actor file it must rewrite is read-only. -Rebuild deletes the level and its actor files first, so the greybox is
built anew (#155); it exits 3 too when one of them is read-only, and with -Lock locks them first. The greybox's river
carves the landscape on the GPU, so building it needs -Render.
Editor opens the editor with the Unreal MCP server at http://127.0.0.1:8000/mcp (docs/ui/AI_TEAM.md); test and setup run without it.
Uishots plays L_TestMap with a renderer (a GPU is needed) and saves the HUD in the UI test state for each faction look at
1920x1080, 2560x1440, 3440x1440 and 1680x1050 to Saved/UIShots/HUD, or with -Panel to Saved/UIShots/<Panel> with a crop of that panel;
-Panel <Panel>@<Scene> first selects a scene of RTSUITestState::ApplyScene (Selection@Hero3, Selection@Building, ...).
Unitshots plays L_TestMap with a renderer (a GPU is needed) at 1920x1080 and saves views of the first unit with the data
asset -Unit (rts.Debug.UnitShots): the RTS camera at its default, closest and farthest zoom, a close-up, the turret turned,
the gun's recoil at rest, at full stroke and run out again (#218), the hull at rest and rocked back from the front and the side with the gun at
0, 90 and 180 degrees (recoil_rest_*/recoil_rock_*, #219), the firing held 0, 30, 80 and 150 ms after the shot, close up and from the RTS camera, by
day and at night (muzzle_flash_<day|night>_<ms>[_rts], #220, #294), the muzzle smoke just after a shot and left behind as the turret turns (muzzle_smoke_*, #221), held 0.15 to 3 s after the shot (muzzle_smoke_<ms>, #355) and played again from a reused component (muzzle_smoke_repeat_*), the head- and rear lights by day and at night (vehicle_lights_<day|night>, #223), the unit picked, selected and driving on a move order, driving into, along and round obstacles with its shapes drawn (the hull's
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
grid and @Close takes the camera to the RTS close zoom, @Near (#149) to 60 m and @Far to its farthest, 450 m; -Map L_TestMap plays L_TestMap instead, for the sets with its viewpoints
(@TestMapOpen, @TestMapBase, @TestMapWall; RTSBenchScene). @NaniteHQ and @LodHQ (#349) stand four HQs beside the grid
(@NaniteHQ the DA_BenchHQ_Nanite setup -BenchHQ builds, @LodHQ the production DA_TestHeadquarters at LOD0 + LOD1, #358),
against the same set without them, and add a table
of their meshes, triangles and memory, and the meshes' disk size; @VSM turns virtual shadow maps on while the set runs (the
project draws conventional shadow maps otherwise).
-Passes records a CSV profile of every sample (r.GPUCsvStatsEnabled and the VSM category) and adds to bench.md and passes.json
the mean of the GPU passes (Nanite, base pass, shadow depths, ...) and the virtual shadow map pages; -Shots saves the view near
the end of the move's warm-up as view_<count>.png. Neither run is ever a baseline. bench.json
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
Buildingshots plays L_Bench with a renderer (a GPU is needed) at 1920x1080 and pictures one finished building of the data
asset -Building (rts.Debug.BuildingShots, #349; the bench HQs or any of Content/Data/Buildings) at the RTS camera's pitch from
12, 150 and 450 m and at 249.6 and 270.4 m, 4 % either side of the bench's LOD1 switch, with every part's LOD printed, to
Saved/BuildingShots/<Building>. -Cvars runs it with console variables into a folder named after them (r.Nanite=0 into
r_Nanite_0, what hardware without Nanite draws).
Uibindings writes every binding key, list and action with its panels, and the widgets, colours, fonts and sprites a layout
may name, to -Out (default Saved/UIBriefs/bindings.json) for /ui-brief. Uicheck validates the layouts of the UI folder -Path
(an unpacked delivery, for /ui-intake) and prints each problem. -Out and -Path are relative to the repository root.
Packaudit reads the local marketplace packs in Content/ into Saved/PackAudit/ and builds the git-ignored scale level
/Game/Developers/PackAudit/L_PackScale (Tools/Editor/audit_packs.py, #153); it fails when that level misses a reference.
It also caps every pack texture over 4K at 4K (#330).
Surfaces (#330) takes the scanned surface ZIPs in the folder -Path (Megascans raw or _ue_raw/_ue_high, or any set whose maps
end in BaseColor/Albedo/Color/diff, Normal/nor_gl/NormalDX, Roughness/rough, AO, Metalness or ORM/arm): it skips snow and
imperfection sets, keeps one set per Megascans id (an _ue variant first), scales the maps to 2K with a packed ORM into
Saved/Surfaces/, and imports them with Tools/Editor/import_surfaces.py into the git-ignored, never cooked /Game/Surfaces/<Name>/
as T_<Name>_BC/_N/_ORM and MI_<Name> of M_ScannedSurface (world-aligned, tiled at the JSON's scanArea).
#>
param(
	[Parameter(Mandatory = $true)]
	[ValidateSet('build', 'test', 'setup', 'editor', 'uishots', 'unitshots', 'trackshots', 'camerashots', 'buildingshots', 'bench', 'gate', 'uibindings', 'uicheck', 'packaudit', 'surfaces', 'bakemap', 'rivercrossing')]
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

	[switch]$Lock,

	[switch]$Rebuild,

	[switch]$BenchHQ,

	[ValidatePattern('^DA_\w+$')]
	[string]$Building = 'DA_BenchHQ_Nanite',

	[switch]$Passes,

	[switch]$Shots,

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

	[ValidatePattern('^(L_\w+|/Game(/\w+)+)$')]
	[string]$Map = 'L_Bench',

	[ValidatePattern('^(DA_\w+)?$')]
	[string]$DataAsset = ''
)

# Whether -Map was given; bakemap has no default level (bench's L_Bench is not one to bake by accident).
$MapGiven = $PSBoundParameters.ContainsKey('Map')
# Whether -Filter was given; trackshots takes it as scenario prefixes, and its default is the tests'.
$FilterGiven = $PSBoundParameters.ContainsKey('Filter')

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

# Empties a shot folder of an earlier run but keeps the folders, which Explorer or a viewer may hold open (#323). A file
# that is in use stays, with a warning; the run writes over it.
function Clear-ShotDir([string]$Dir) {
	if (-not (Test-Path $Dir)) { return }
	Get-ChildItem -Path $Dir -Recurse -File | ForEach-Object {
		try { Remove-Item -Force -LiteralPath $_.FullName -ErrorAction Stop }
		catch { Write-Host "  could not clear $($_.TargetObject): $($_.Exception.Message)" }
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

# Runs an editor Python script as a commandlet, as setup does. $Name is the task's name in its result line. Exits 3 when the
# script lists assets it must rewrite but cannot because they are read-only (LFS lockable, not locked). With -Lock it locks
# them instead, clears a read-only flag the lock left behind and runs the script once more (#350).
function Invoke-EditorScript([string]$Script, [string]$Name) {
	# Written by the script when assets it must rewrite are read-only (LFS lockable, not locked).
	$ReadOnlyReport = Join-Path $RepoRoot 'Saved\Setup\ReadOnlyAssets.txt'
	$RenderArgs = if ($Render) { @('-AllowCommandletRendering', '-RenderOffscreen') } else { @('-nullrhi') }
	$Locked = @()
	foreach ($Attempt in 1..2) {
		if (Test-Path $ReadOnlyReport) { Remove-Item -Force $ReadOnlyReport }
		& $EditorCmd $Project -run=pythonscript "-script=$Script" $NoMCPPlugins -unattended -nosplash @RenderArgs -nosound -stdout -FullStdOutLogOutput
		if (-not (Test-Path $ReadOnlyReport)) { break }

		$Files = @(Get-Content $ReadOnlyReport | Where-Object { $_ })
		Write-Host ''
		Write-Host "$Name BLOCKED: these assets must be rewritten but are read-only (LFS lockable, not locked by you):"
		$Files | ForEach-Object { Write-Host "  $_" }
		if (-not $Lock -or $Attempt -eq 2) {
			Write-Host "Nothing was written. Rerun with -Lock to lock them and run again, or lock them yourself, rerun"
			Write-Host "$($Name.ToLower()), commit and push, then unlock:"
			$Files | ForEach-Object { Write-Host "  git lfs lock $_" }
			$Files | ForEach-Object { Write-Host "  git lfs unlock $_" }
			Write-Host "From bash: git lfs lock `$(cat Saved/Setup/ReadOnlyAssets.txt). A file still read-only after its lock:"
			Write-Host "  attrib -R <file> (cmd) or chmod u+w <file> (bash)"
			exit 3
		}

		Write-Host "Nothing was written. Locking them (-Lock):"
		foreach ($File in $Files) {
			& git -C $RepoRoot lfs lock $File
			if ($LASTEXITCODE -ne 0) {
				Write-Host "$Name BLOCKED: could not lock $File (git lfs locks shows who holds it)."
				exit 3
			}
			$Locked += $File
			$Item = Get-Item -LiteralPath (Join-Path $RepoRoot $File)
			if ($Item.IsReadOnly) { $Item.IsReadOnly = $false }
		}
		Write-Host "Locked. Running $($Name.ToLower()) again."
	}
	if ($Locked) {
		Write-Host "Locked by this run; unlock them after the push:"
		$Locked | ForEach-Object { Write-Host "  git lfs unlock $_" }
	}
	if ($LASTEXITCODE -ne 0) {
		Write-Host "$Name FAILED (exit $LASTEXITCODE)"
		exit $LASTEXITCODE
	}
	Write-Host "$Name SUCCEEDED"
}

function Invoke-Setup {
	# The bench's HQ shells (#349) only when asked: setup_testmap.py reads WD_SETUP_BENCH_HQ.
	if ($BenchHQ) { $env:WD_SETUP_BENCH_HQ = '1' }
	try {
		Invoke-EditorScript (Join-Path $RepoRoot 'Tools\Editor\setup_testmap.py') 'SETUP'
	}
	finally {
		Remove-Item Env:WD_SETUP_BENCH_HQ -ErrorAction SilentlyContinue
	}
}

# Bakes the preview, preview area and start spots of the level -Map (L_Name or a /Game path) into its map data asset
# (Tools/Editor/bake_map.py, #289).
function Invoke-BakeMap {
	if (-not $MapGiven) {
		Write-Host 'BAKEMAP FAILED: name the level with -Map /Game/Maps/<Level> (or -Map <Level>).'
		exit 1
	}
	$LevelPath = if ($Map.StartsWith('/')) { $Map } else { "/Game/Maps/$Map" }
	$env:WD_BAKE_MAP = $LevelPath
	$env:WD_BAKE_MAP_DATA = $DataAsset
	try {
		Invoke-EditorScript (Join-Path $RepoRoot 'Tools\Editor\bake_map.py') 'BAKEMAP'
	}
	finally {
		Remove-Item Env:WD_BAKE_MAP, Env:WD_BAKE_MAP_DATA -ErrorAction SilentlyContinue
	}
}

# Builds and saves WD_RiverCrossing_01's navmesh, creating its greybox first when the level is missing (#154). -Rebuild
# deletes the level and its actor files first, which must be locked like any file the run rewrites (#155).
function Invoke-RiverCrossing {
	if ($Rebuild) {
		$LevelFiles = @(Get-Item -LiteralPath (Join-Path $RepoRoot 'Content\Maps\WD_RiverCrossing_01.umap') -ErrorAction SilentlyContinue)
		$ActorDir = Join-Path $RepoRoot 'Content\__ExternalActors__\Maps\WD_RiverCrossing_01'
		if (Test-Path $ActorDir) { $LevelFiles += @(Get-ChildItem -LiteralPath $ActorDir -Recurse -File) }
		$ReadOnly = @($LevelFiles | Where-Object { $_.IsReadOnly } | ForEach-Object {
			$_.FullName.Substring($RepoRoot.Length + 1).Replace([char]92, [char]47) })
		if ($ReadOnly.Count -gt 0) {
			if (-not $Lock) {
				Write-Host "RIVERCROSSING BLOCKED: -Rebuild deletes $($ReadOnly.Count) read-only level files (LFS lockable, not locked by you)."
				Write-Host 'Nothing was deleted. Rerun with -Lock to lock them first; unlock them after the push.'
				exit 3
			}
			Write-Host "Locking the $($ReadOnly.Count) level files -Rebuild deletes (-Lock):"
			foreach ($File in $ReadOnly) {
				& git -C $RepoRoot lfs lock $File | Out-Null
				if ($LASTEXITCODE -ne 0) {
					Write-Host "RIVERCROSSING BLOCKED: could not lock $File (git lfs locks shows who holds it)."
					exit 3
				}
			}
			Write-Host 'Locked; unlock them after the push (git lfs unlock <file> for each, or git lfs locks to list them).'
		}
		$LevelFiles | ForEach-Object { Remove-Item -Force -LiteralPath $_.FullName }
		Write-Host "Deleted the level and $($LevelFiles.Count - 1) actor files; building the greybox anew."
	}
	Invoke-EditorScript (Join-Path $RepoRoot 'Tools\Editor\setup_rivercrossing.py') 'RIVERCROSSING'
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
	Clear-ShotDir $ShotDir

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
	Clear-ShotDir $ShotDir
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
# motion_*_reprojection with the previous frame's reprojection shown, for smearing under the temporal upscaler (#243). The
# fire_* sequences stand and fire, through the whole of the hull's rock, with the hull's rock logged per frame (#219). The
# dust_<ground>_* sequences stand, drive slowly and fast and pivot, from behind and above, on L_TestMap's dirt and on each of
# its strips of grass, road, sand, mud and water (#336), with the dust's rate, the ground's surface type and the emitters
# logged per frame (#224); a wheeled unit (DA_TestFlakTruck, spawned when the map has none) takes those only.
function Invoke-TrackShots {
	Assert-EditorClosed 'the game run loads the same project.'
	$ShotDir = Join-Path $RepoRoot "Saved\TrackShots\$Unit"
	$LogFile = Join-Path $RepoRoot 'Saved\Logs\TrackShots.log'
	Clear-ShotDir $ShotDir
	# L_TestMap by default; -Map another level with the same open strip and ground strips (L_TestMapNight, #336). -Filter,
	# scenario name prefixes joined by + (dust_dirt+dust_sand), takes only those.
	$ShotMap = if (-not $MapGiven) { '/Game/Maps/L_TestMap' } elseif ($Map.StartsWith('/Game')) { $Map } else { "/Game/Maps/$Map" }
	$Only = if ($FilterGiven) { " $Filter" } else { '' }
	$Process = Start-Process -FilePath $EditorExe -Wait -PassThru -ArgumentList (@(
		"`"$Project`"", $ShotMap, '-game', '-windowed', '-RenderOffscreen', '-ResX=1280', '-ResY=720',
		"`"-ExecCmds=rts.Debug.TrackShots $Unit$Only`"", '-RTSTrackShotsQuit', "`"-abslog=$LogFile`"", $NoMCPPlugins,
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

# One finished building pictured from the RTS camera at 12, 150 and 450 m and either side of the bench HQ's 260 m LOD switch
# (#349; rts.Debug.BuildingShots), to Saved\BuildingShots\<Building>, or with -Cvars to a folder named after them.
function Invoke-BuildingShots {
	Assert-EditorClosed 'the game run loads the same project.'
	$Folder = if ($Cvars) { $Cvars -replace '[^A-Za-z0-9]+', '_' } else { '' }
	$ShotDir = Join-Path $RepoRoot "Saved\BuildingShots\$Building"
	if ($Folder) { $ShotDir = Join-Path $ShotDir $Folder }
	$LogFile = Join-Path $RepoRoot 'Saved\Logs\BuildingShots.log'
	Clear-ShotDir $ShotDir
	$Process = Start-Process -FilePath $EditorExe -Wait -PassThru -ArgumentList (@(
		"`"$Project`"", '/Game/Maps/L_Bench', '-game', '-windowed', '-RenderOffscreen', '-ResX=1920', '-ResY=1080',
		"`"-ExecCmds=rts.Debug.BuildingShots $Building $Folder`"", '-RTSBuildingShotsQuit', "`"-abslog=$LogFile`"", $NoMCPPlugins,
		'-unattended', '-nosplash', '-nosound') + @(if ($Cvars) { "-dpcvars=$Cvars" }))
	$Names = @('close_12m', 'default_150m', 'far_450m', 'lod_before_249.6m', 'lod_after_270.4m')
	$Problems = @($Names | Where-Object { -not (Test-Path (Join-Path $ShotDir "$_.png")) } | ForEach-Object { "missing $_.png" })
	if (Test-Path $LogFile) {
		Select-String -Path $LogFile -Pattern 'rts.Debug.BuildingShots: \S+ (from|at) ' | ForEach-Object { $_.Line -replace '^.*rts.Debug.BuildingShots: ', '  ' }
	}
	if ($Process.ExitCode -ne 0 -or $Problems.Count -gt 0) {
		$Problems | ForEach-Object { Write-Host "  $_" }
		if (Test-Path $LogFile) { Select-String -Path $LogFile -Pattern 'rts.Debug|Error:|Fatal' | Select-Object -Last 20 | ForEach-Object { $_.Line } }
		Write-Host "BUILDINGSHOTS FAILED (game exit $($Process.ExitCode)). Log: $LogFile"
		exit 1
	}
	Write-Host "BUILDINGSHOTS SUCCEEDED: $ShotDir"
}

function Invoke-CameraShots {
	Assert-EditorClosed 'the game run loads the same project.'
	$ShotDir = Join-Path $RepoRoot 'Saved\CameraShots'
	$LogFile = Join-Path $RepoRoot 'Saved\Logs\CameraShots.log'
	Clear-ShotDir $ShotDir
	# L_TestMap's staged base and formation by default; -Map pictures another level's landscape (WD_RiverCrossing_01, #295).
	$ShotMap = if (-not $MapGiven) { '/Game/Maps/L_TestMap' } elseif ($Map.StartsWith('/Game')) { $Map } else { "/Game/Maps/$Map" }

	$Process = Start-Process -FilePath $EditorExe -Wait -PassThru -ArgumentList @(
		"`"$Project`"", $ShotMap, '-game', '-windowed', '-RenderOffscreen', '-ResX=1920', '-ResY=1080',
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
	if ($SaveBaseline -and ($Passes -or $Shots)) { Write-Host 'BENCH FAILED: -Passes and -Shots add their own cost to the frames; such a run is never a baseline.'; exit 1 }

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
		Add-BenchPasses $Set
		Add-BenchHQDisk $Set
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

# The GPU passes and virtual shadow map pages of each sample's CSV profile (-Passes, #349): the means of every GPU/ and VSM/
# column and of the frame's own times, to passes.json, and to bench.md the passes over 0.05 ms and the pages.
function Add-BenchPasses([string]$Set) {
	$OutDir = Join-Path $RepoRoot "Saved\Bench\$Set"
	$Files = @(Get-ChildItem (Join-Path $OutDir 'passes') -Filter '*.csv' -ErrorAction SilentlyContinue | Sort-Object Name)
	if (-not $Files) { return }
	$Culture = [Globalization.CultureInfo]::InvariantCulture
	$Samples = [ordered]@{}
	foreach ($File in $Files) {
		$Lines = [IO.File]::ReadAllLines($File.FullName)
		$Header = $Lines[0].Split(',')
		$Wanted = @(for ($Index = 0; $Index -lt $Header.Count; $Index++) {
			$Name = $Header[$Index]
			if ($Name -like 'GPU/*' -or $Name -like 'VSM/*' -or $Name -in 'FrameTime', 'GameThreadTime', 'RenderThreadTime', 'RHIThreadTime', 'GPUTime') { $Index }
		})
		$Sums = @{}; $Seen = @{}
		$Rows = 0
		foreach ($Line in $Lines[1..($Lines.Count - 1)]) {
			if ($Line.StartsWith('[') -or $Line -eq $Lines[0]) { break }
			$Cells = $Line.Split(',')
			$Rows++
			foreach ($Index in $Wanted) {
				$Value = 0.0
				if ($Index -lt $Cells.Count -and [double]::TryParse($Cells[$Index], [Globalization.NumberStyles]::Float, $Culture, [ref]$Value)) {
					$Sums[$Index] = $Sums[$Index] + $Value
					$Seen[$Index] = $Seen[$Index] + 1
				}
			}
		}
		$Means = [ordered]@{ frames = $Rows }
		foreach ($Index in $Wanted) {
			if ($Seen[$Index]) { $Means[$Header[$Index]] = [Math]::Round($Sums[$Index] / $Seen[$Index], 4) }
		}
		$Samples[[IO.Path]::GetFileNameWithoutExtension($File.Name)] = $Means
	}
	Write-Json (Join-Path $OutDir 'passes.json') $Samples
	$Lines = @('', 'GPU passes and virtual shadow map pages (-Passes, #349), the mean over each sample''s CSV profile; passes over 0.05 ms:', '')
	foreach ($Sample in $Samples.Keys) {
		$Means = $Samples[$Sample]
		$Lines += "**$Sample** ($($Means.frames) frames): frame $($Means.FrameTime) ms, GPU $($Means.GPUTime) ms, render thread $($Means.RenderThreadTime) ms"
		$Lines += ''
		$Lines += '| GPU pass | ms |'
		$Lines += '|---|---:|'
		$Gpu = @($Means.Keys | Where-Object { $_ -like 'GPU/*' -and $Means[$_] -ge 0.05 } | Sort-Object { -[double]$Means[$_] })
		$Gpu | ForEach-Object { $Lines += "| $($_.Substring(4)) | $($Means[$_].ToString('0.000', $Culture)) |" }
		$Vsm = @($Means.Keys | Where-Object { $_ -like 'VSM/*' })
		if ($Vsm) {
			$Lines += ''
			$Lines += '| VSM | mean |'
			$Lines += '|---|---:|'
			$Vsm | ForEach-Object { $Lines += "| $($_.Substring(4)) | $($Means[$_].ToString('0.#', $Culture)) |" }
		}
		$Lines += ''
	}
	[IO.File]::AppendAllLines((Join-Path $OutDir 'bench.md'), [string[]]$Lines, (New-Object Text.UTF8Encoding $false))
}

# The disk size of the HQ shell an @NaniteHQ or @LodHQ set stood (#349): the bench's SM_BenchHQ_Nanite_* packages, or the
# production HQ's meshes (#358), to bench.md.
function Add-BenchHQDisk([string]$Set) {
	if ($Set -like '*@NaniteHQ*') { $Kind = 'Nanite'; $Folder = 'Content\Bench\USAHQ\Meshes'; $Filter = 'SM_BenchHQ_Nanite_*.uasset' }
	elseif ($Set -like '*@LodHQ*') { $Kind = 'Lod'; $Folder = 'Content\Art\USA\Buildings\USAHQ\Meshes'; $Filter = '*.uasset' }
	else { return }
	$Files = @(Get-ChildItem (Join-Path $RepoRoot $Folder) -Filter $Filter -ErrorAction SilentlyContinue)
	$Bytes = ($Files | Measure-Object -Property Length -Sum).Sum
	$Line = [string]::Format([Globalization.CultureInfo]::InvariantCulture, 'Disk (#349): the {0} HQ''s {1} mesh packages take {2:0.0} MB in {3}.', $Kind, $Files.Count, $Bytes / 1MB, $Folder.Replace('\', '/'))
	[IO.File]::AppendAllLines((Join-Path $RepoRoot "Saved\Bench\$Set\bench.md"), [string[]]@('', $Line), (New-Object Text.UTF8Encoding $false))
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
	if ($Passes) { $ExtraArgs += '-RTSBenchPasses' }
	if ($Shots) { $ExtraArgs += '-RTSBenchShots' }
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

function Invoke-PackAudit {
	$Script = Join-Path $RepoRoot 'Tools\Editor\audit_packs.py'
	$Summary = Join-Path $RepoRoot 'Saved\PackAudit\summary.md'
	if (Test-Path $Summary) { Remove-Item -Force $Summary }
	& $EditorCmd $Project -run=pythonscript "-script=$Script" $NoMCPPlugins -unattended -nosplash -nullrhi -nosound -stdout -FullStdOutLogOutput | Out-Null
	if (-not (Test-Path $Summary)) {
		Write-Host "PACKAUDIT FAILED (exit $LASTEXITCODE): no summary written. See Saved\Logs\WorldDivide.log."
		exit 1
	}
	$Missing = Select-String -Path $Summary -Pattern '^- Missing references: (\d+)' | ForEach-Object { [int]$_.Matches[0].Groups[1].Value }
	if ($Missing -ne 0) {
		Write-Host "PACKAUDIT FAILED: L_PackScale misses $Missing reference(s). See $Summary"
		exit 1
	}
	Write-Host "PACKAUDIT SUCCEEDED: $Summary"
}

# Scales the scanned maps and packs the ORM with GDI+, which every Windows has, so the surfaces need no image library.
$SurfaceImageSource = @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;

public static class WDSurfaceImage
{
	static Bitmap Scale(Image Source, int MaxSize)
	{
		double Factor = Math.Min(1.0, (double)MaxSize / Math.Max(Source.Width, Source.Height));
		Bitmap Result = new Bitmap(Math.Max(1, (int)Math.Round(Source.Width * Factor)), Math.Max(1, (int)Math.Round(Source.Height * Factor)), PixelFormat.Format24bppRgb);
		using (Graphics Canvas = Graphics.FromImage(Result))
		using (ImageAttributes Wrap = new ImageAttributes())
		{
			Canvas.InterpolationMode = InterpolationMode.HighQualityBicubic;
			Canvas.PixelOffsetMode = PixelOffsetMode.HighQuality;
			Canvas.CompositingMode = CompositingMode.SourceCopy;
			// Tiling sets: sample across the edges, so the scaled map still tiles.
			Wrap.SetWrapMode(WrapMode.TileFlipXY);
			Canvas.DrawImage(Source, new Rectangle(0, 0, Result.Width, Result.Height), 0, 0, Source.Width, Source.Height, GraphicsUnit.Pixel, Wrap);
		}
		return Result;
	}

	public static void Resize(Stream Source, string Target, int MaxSize)
	{
		using (Image Image = Image.FromStream(Source))
		using (Bitmap Scaled = Scale(Image, MaxSize))
		{
			Scaled.Save(Target, ImageFormat.Png);
		}
	}

	static byte[] RedChannel(Stream Source, int Width, int Height, byte Fallback)
	{
		byte[] Channel = new byte[Width * Height];
		if (Source == null)
		{
			for (int Index = 0; Index < Channel.Length; ++Index) { Channel[Index] = Fallback; }
			return Channel;
		}
		using (Image Image = Image.FromStream(Source))
		using (Bitmap Scaled = Scale(Image, Math.Max(Width, Height)))
		{
			if (Scaled.Width != Width || Scaled.Height != Height)
			{
				throw new InvalidDataException("The ORM maps differ in aspect");
			}
			BitmapData Data = Scaled.LockBits(new Rectangle(0, 0, Width, Height), ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
			byte[] Row = new byte[Data.Stride];
			for (int Y = 0; Y < Height; ++Y)
			{
				Marshal.Copy(Data.Scan0 + Y * Data.Stride, Row, 0, Data.Stride);
				// 24bpp is stored B, G, R; the maps are grey, red is as good as any.
				for (int X = 0; X < Width; ++X) { Channel[Y * Width + X] = Row[X * 3 + 2]; }
			}
			Scaled.UnlockBits(Data);
		}
		return Channel;
	}

	/** AO into red, roughness into green, metalness into blue; a missing map is the fallback value. */
	public static void PackOrm(Stream Ao, Stream Roughness, Stream Metal, Stream Reference, string Target, int MaxSize)
	{
		int Width, Height;
		using (Image Image = Image.FromStream(Reference))
		{
			double Factor = Math.Min(1.0, (double)MaxSize / Math.Max(Image.Width, Image.Height));
			Width = Math.Max(1, (int)Math.Round(Image.Width * Factor));
			Height = Math.Max(1, (int)Math.Round(Image.Height * Factor));
		}
		byte[] Red = RedChannel(Ao, Width, Height, 255);
		byte[] Green = RedChannel(Roughness, Width, Height, 204);
		byte[] Blue = RedChannel(Metal, Width, Height, 0);
		using (Bitmap Result = new Bitmap(Width, Height, PixelFormat.Format24bppRgb))
		{
			BitmapData Data = Result.LockBits(new Rectangle(0, 0, Width, Height), ImageLockMode.WriteOnly, PixelFormat.Format24bppRgb);
			byte[] Row = new byte[Data.Stride];
			for (int Y = 0; Y < Height; ++Y)
			{
				for (int X = 0; X < Width; ++X)
				{
					int Index = Y * Width + X;
					Row[X * 3] = Blue[Index];
					Row[X * 3 + 1] = Green[Index];
					Row[X * 3 + 2] = Red[Index];
				}
				Marshal.Copy(Row, 0, Data.Scan0 + Y * Data.Stride, Data.Stride);
			}
			Result.UnlockBits(Data);
			Result.Save(Target, ImageFormat.Png);
		}
	}
}
'@

# The map kind of a file in a surface ZIP, from the end of its name (Megascans, ambientCG, Poly Haven), or ''.
function Get-SurfaceMapKind([string]$FileName) {
	$Extension = [IO.Path]::GetExtension($FileName).ToLowerInvariant()
	if ($Extension -notin '.jpg', '.jpeg', '.png', '.tga', '.tif', '.tiff', '.bmp') { return '' }
	$Stem = [IO.Path]::GetFileNameWithoutExtension($FileName).ToLowerInvariant()
	# Resolution tags sit before or after the kind: Lush_Grass_8K_Normal, rock_nor_gl_2k.
	$Stem = $Stem -replace '[_-]\d+k$', ''
	if ($Stem -match '(^|[_-])(orm|arm)$') { return 'ORM' }
	if ($Stem -match '(^|[_-])(basecolor|albedo|diffuse|diff|color|col)$') { return 'BaseColor' }
	if ($Stem -match '(^|[_-])(normaldx|nor_dx)$') { return 'NormalDX' }
	if ($Stem -match '(^|[_-])(normal|normalgl|nor_gl)$') { return 'Normal' }
	if ($Stem -match '(^|[_-])(roughness|rough)$') { return 'Roughness' }
	if ($Stem -match '(^|[_-])(ao|ambientocclusion)$') { return 'AO' }
	if ($Stem -match '(^|[_-])(metalness|metallic|metal)$') { return 'Metal' }
	return ''
}

function Read-ZipEntry($Entry) {
	$Memory = New-Object IO.MemoryStream
	$Stream = $Entry.Open()
	try { $Stream.CopyTo($Memory) } finally { $Stream.Dispose() }
	$Memory.Position = 0
	return , $Memory
}

function ConvertTo-SurfaceName([string]$Text) {
	return (($Text -split '[^A-Za-z0-9]+' | Where-Object { $_ } | ForEach-Object { $_.Substring(0, 1).ToUpperInvariant() + $_.Substring(1) }) -join '')
}

function Invoke-Surfaces {
	if (-not $Path -or -not (Test-Path $Path -PathType Container)) {
		Write-Host "SURFACES FAILED: -Path must name a folder of surface ZIPs, got '$Path'."
		exit 1
	}
	Assert-EditorClosed 'the import writes /Game/Surfaces, which the open editor may hold.'
	Add-Type -AssemblyName System.IO.Compression.FileSystem
	if (-not ('WDSurfaceImage' -as [type])) { Add-Type -TypeDefinition $SurfaceImageSource -ReferencedAssemblies System.Drawing }
	$Staging = Join-Path $RepoRoot 'Saved\Surfaces'
	if (Test-Path $Staging) { Remove-Item -Recurse -Force $Staging }
	New-Item -ItemType Directory -Force $Staging | Out-Null

	# Read every ZIP's description first, so a set found twice keeps its best variant.
	$Candidates = @()
	$Skipped = @()
	foreach ($Zip in Get-ChildItem -Path $Path -Filter '*.zip' | Sort-Object Name) {
		$Archive = [IO.Compression.ZipFile]::OpenRead($Zip.FullName)
		try {
			$Maps = @{}
			$Info = $null
			foreach ($Entry in $Archive.Entries) {
				if ($Entry.Name -match '\.json$' -and -not $Info) {
					$Reader = New-Object IO.StreamReader($Entry.Open())
					try { $Info = $Reader.ReadToEnd() | ConvertFrom-Json } catch { $Info = $null } finally { $Reader.Dispose() }
					continue
				}
				$Kind = Get-SurfaceMapKind $Entry.Name
				if ($Kind -and -not $Maps.ContainsKey($Kind)) { $Maps[$Kind] = $Entry.FullName }
			}
			$Stem = $Zip.BaseName -replace '_ue_(raw|high)', '' -replace '_\d+k$', ''
			$Id = if ($Info -and $Info.id) { [string]$Info.id } else { $Stem }
			$Title = if ($Info -and $Info.name) { [string]$Info.name } else { $Stem -replace '_texture_pack$', '' }
			# Some Megascans names end in their id already ('Military Trenches Ground Dirt Fine 01 yd0kabg').
			$Title = ($Title -replace "[\s_-]*$([regex]::Escape($Id))$", '').Trim()
			# Megascans: the tags and categories name the kind of set; snow does not fit the late-summer map (D3).
			$Words = @($Title)
			if ($Info) {
				$Words += @($Info.categories) + @($Info.tags)
				if ($Info.semanticTags) { $Words += @($Info.semanticTags.asset_type) + @($Info.semanticTags.contains) }
			}
			$Words = ($Words | Where-Object { $_ } | ForEach-Object { "$_".ToLowerInvariant() }) -join ' '
			$Reason = ''
			if ($Words -match 'imperfection') { $Reason = 'imperfection' }
			elseif ($Words -match '\bsnow') { $Reason = 'snow' }
			elseif (-not $Maps.ContainsKey('BaseColor') -or -not ($Maps.ContainsKey('Normal') -or $Maps.ContainsKey('NormalDX'))) {
				$Reason = 'no base colour or normal map it can read'
			}
			if ($Reason) {
				$Skipped += "$($Zip.Name) ($Reason)"
				continue
			}
			$TileM = 2.0
			if ($Info -and $Info.meta) {
				$Scan = $Info.meta | Where-Object { $_.key -eq 'scanArea' } | Select-Object -First 1
				if ($Scan -and "$($Scan.value)" -match '([\d.]+)') { $TileM = [double]$Matches[1] }
			}
			$Resolution = if ($Zip.BaseName -match '_(\d+)k($|_)') { [int]$Matches[1] } else { 0 }
			$Candidates += [pscustomobject]@{
				Zip = $Zip.FullName; Id = $Id; Name = (ConvertTo-SurfaceName $Title) + $(if ($Info -and $Info.id) { "_$Id" } else { '' })
				TileM = $TileM; Maps = $Maps; UE = [bool]($Zip.BaseName -match '_ue_(raw|high)'); Resolution = $Resolution
			}
		} finally { $Archive.Dispose() }
	}

	$Sets = @()
	foreach ($Group in $Candidates | Group-Object Id) {
		$Best = $Group.Group | Sort-Object @{ Expression = 'UE'; Descending = $true }, @{ Expression = 'Resolution'; Descending = $true } | Select-Object -First 1
		foreach ($Other in $Group.Group | Where-Object { $_ -ne $Best }) { $Skipped += "$(Split-Path -Leaf $Other.Zip) (duplicate of $(Split-Path -Leaf $Best.Zip))" }
		$Sets += $Best
	}

	$Manifest = @()
	foreach ($Set in $Sets | Sort-Object Name) {
		Write-Host "SURFACES: $($Set.Name) from $(Split-Path -Leaf $Set.Zip)"
		$Folder = Join-Path $Staging $Set.Name
		New-Item -ItemType Directory -Force $Folder | Out-Null
		$Archive = [IO.Compression.ZipFile]::OpenRead($Set.Zip)
		try {
			$Open = { param($Kind) if ($Set.Maps.ContainsKey($Kind)) { Read-ZipEntry $Archive.GetEntry($Set.Maps[$Kind]) } else { $null } }
			$Files = @{}
			foreach ($Kind in 'BaseColor', 'Normal') {
				$Source = & $Open $(if ($Kind -eq 'Normal' -and -not $Set.Maps.ContainsKey('Normal')) { 'NormalDX' } else { $Kind })
				$Target = Join-Path $Folder "$Kind.png"
				try { [WDSurfaceImage]::Resize($Source, $Target, 2048) } finally { $Source.Dispose() }
				$Files[$Kind] = $Target
			}
			$Target = Join-Path $Folder 'ORM.png'
			if ($Set.Maps.ContainsKey('ORM')) {
				$Source = & $Open 'ORM'
				try { [WDSurfaceImage]::Resize($Source, $Target, 2048) } finally { $Source.Dispose() }
			} else {
				$Streams = @{}
				foreach ($Kind in 'AO', 'Roughness', 'Metal', 'BaseColor') { $Streams[$Kind] = & $Open $Kind }
				try { [WDSurfaceImage]::PackOrm($Streams.AO, $Streams.Roughness, $Streams.Metal, $Streams.BaseColor, $Target, 2048) }
				finally { $Streams.Values | Where-Object { $_ } | ForEach-Object { $_.Dispose() } }
			}
			$Files['ORM'] = $Target
		} finally { $Archive.Dispose() }
		# Raw Megascans, ambientCG NormalGL and Poly Haven nor_gl are OpenGL (green up); Unreal wants DirectX. _ue sets are
		# exported for Unreal already.
		$Manifest += [ordered]@{
			name = $Set.Name; id = $Set.Id; source = (Split-Path -Leaf $Set.Zip); tile_m = $Set.TileM
			flip_green = ($Set.Maps.ContainsKey('Normal') -and -not $Set.UE); textures = $Files
		}
	}
	ConvertTo-Json @{ sets = $Manifest; skipped = $Skipped } -Depth 5 | Set-Content -Encoding utf8 (Join-Path $Staging 'manifest.json')
	$Skipped | ForEach-Object { Write-Host "SURFACES: skipped $_" }

	$Script = Join-Path $RepoRoot 'Tools\Editor\import_surfaces.py'
	$Report = Join-Path $Staging 'imported.md'
	& $EditorCmd $Project -run=pythonscript "-script=$Script" $NoMCPPlugins -unattended -nosplash -nullrhi -nosound -stdout -FullStdOutLogOutput | Out-Null
	if (-not (Test-Path $Report)) {
		Write-Host "SURFACES FAILED (exit $LASTEXITCODE): no report written. See Saved\Logs\WorldDivide.log."
		exit 1
	}
	Write-Host "SURFACES SUCCEEDED: $($Manifest.Count) set(s) imported, $($Skipped.Count) skipped. $Report"
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
	'buildingshots' { Invoke-BuildingShots }
	'bench' { Invoke-Bench }
	'gate' { Invoke-Gate }
	'uibindings' { Invoke-UIBindings }
	'uicheck' { Invoke-UICheck }
	'packaudit' { Invoke-PackAudit }
	'surfaces' { Invoke-Surfaces }
	'bakemap' { Invoke-BakeMap }
	'rivercrossing' { Invoke-RiverCrossing }
}
