<#
.SYNOPSIS
Builds, tests or regenerates test content for World Divide from the command line.

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task build
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task test -Filter WorldDivide.Commands
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task test -TimeDilation 1
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task setup

The engine is taken from $env:UE_ROOT, falling back to G:\Program Files\Epic Games\UE_5.6.
Exits nonzero on any failure: 2 when the editor is running (build), 3 when setup must rewrite read-only LFS assets.
The PIE smoke tests run TimeDilation game seconds per real second (default 4); -TimeDilation 1 runs them at real time.
#>
param(
	[Parameter(Mandatory = $true)]
	[ValidateSet('build', 'test', 'setup')]
	[string]$Task,

	[string]$Filter = 'WorldDivide',

	[double]$TimeDilation = 4
)

$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$Project = Join-Path $RepoRoot 'WorldDivide.uproject'

$UERoot = $env:UE_ROOT
if (-not $UERoot) { $UERoot = 'G:\Program Files\Epic Games\UE_5.6' }
if (-not (Test-Path (Join-Path $UERoot 'Engine'))) {
	Write-Error "Unreal Engine 5.6 not found at '$UERoot'. Set UE_ROOT to the engine install folder."
}

$BuildBat = Join-Path $UERoot 'Engine\Build\BatchFiles\Build.bat'
$EditorCmd = Join-Path $UERoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'

function Assert-EditorClosed {
	$Running = Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue
	if ($Running) {
		Write-Host 'UnrealEditor is running. Close it first: Live Coding locks the module DLL and the build will fail.'
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

	& $EditorCmd $Project -run=pythonscript "-script=$Script" -unattended -nosplash -nullrhi -nosound -stdout -FullStdOutLogOutput
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

function Invoke-Test {
	$ReportDir = Join-Path $RepoRoot 'Saved\Automation\Reports'
	$LogFile = Join-Path $RepoRoot 'Saved\Logs\AutomationRun.log'
	if (Test-Path $ReportDir) { Remove-Item -Recurse -Force $ReportDir }

	& $EditorCmd $Project "-ExecCmds=Automation RunTests $Filter;Quit" '-TestExit=Automation Test Queue Empty' `
		"-ReportExportPath=$ReportDir" "-abslog=$LogFile" "-RTSTestTimeDilation=$TimeDilation" -unattended -nullrhi -nosplash -nosound
	$EditorExit = $LASTEXITCODE

	$Index = Join-Path $ReportDir 'index.json'
	if (-not (Test-Path $Index)) {
		Write-Host "No test report written (editor exit $EditorExit). Log: $LogFile"
		if (Test-Path $LogFile) {
			Select-String -Path $LogFile -Pattern 'Error:|Fatal|TEST COMPLETE' | Select-Object -Last 20 | ForEach-Object { $_.Line }
		}
		exit 1
	}

	# The report may carry a UTF-8 BOM; strip it before parsing.
	$Report = (Get-Content -Raw -Encoding UTF8 $Index).TrimStart([char]0xFEFF) | ConvertFrom-Json
	$Failed = @($Report.tests | Where-Object { $_.state -eq 'Fail' })

	foreach ($Test in $Report.tests) {
		Write-Host ("[{0}] {1}" -f $Test.state, $Test.fullTestPath)
	}
	foreach ($Test in $Failed) {
		Write-Host ''
		Write-Host "FAILED: $($Test.fullTestPath)"
		foreach ($Entry in $Test.entries) {
			if ($Entry.event.type -eq 'Error') { Write-Host "  $($Entry.event.message)" }
		}
	}

	Write-Host ''
	Write-Host ("Succeeded: {0}  With warnings: {1}  Failed: {2}  Not run: {3}" -f $Report.succeeded, $Report.succeededWithWarnings, $Report.failed, $Report.notRun)
	Write-Host "Log: $LogFile"

	if ($Report.tests.Count -eq 0) {
		Write-Host "No tests matched filter '$Filter'."
		exit 1
	}
	if ($Failed.Count -gt 0 -or $Report.failed -gt 0) { exit 1 }
	Write-Host 'TESTS PASSED'
}

switch ($Task) {
	'build' { Invoke-Build }
	'test' { Invoke-Test }
	'setup' { Invoke-Setup }
}
