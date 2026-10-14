<#
.SYNOPSIS
Builds, tests, regenerates test content for or opens World Divide from the command line.

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task build
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task test -Filter WorldDivide.Commands
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task test -TimeDilation 1
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task setup
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task setup -Render
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task editor
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task uishots
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Scripts/ue.ps1 -Task uishots -Panel ResourceBar

The engine is taken from $env:UE_ROOT, falling back to D:\Program Files\Epic Games\UE_5.8.
Exits nonzero on any failure: 2 when the editor is running (build, editor), 3 when setup must rewrite read-only LFS assets.
The PIE smoke tests run TimeDilation game seconds per real second (default 4); -TimeDilation 1 runs them at real time.
Setup runs without a renderer and draws the lobby's map previews; -Render gives it one (a GPU is needed) to render them.
Editor opens the editor with the Unreal MCP server at http://127.0.0.1:8000/mcp (docs/ui/AI_TEAM.md); test and setup run without it.
Uishots plays L_TestMap with a renderer (a GPU is needed) and saves the HUD in the UI test state for each faction look at
1920x1080, 2560x1440 and 3440x1440 to Saved/UIShots/HUD, or with -Panel to Saved/UIShots/<Panel> with a crop of that panel.
#>
param(
	[Parameter(Mandatory = $true)]
	[ValidateSet('build', 'test', 'setup', 'editor', 'uishots')]
	[string]$Task,

	[string]$Filter = 'WorldDivide',

	[double]$TimeDilation = 4,

	[switch]$Render,

	[string]$Panel = ''
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

function Invoke-Test {
	$ReportDir = Join-Path $RepoRoot 'Saved\Automation\Reports'
	$LogFile = Join-Path $RepoRoot 'Saved\Logs\AutomationRun.log'
	if (Test-Path $ReportDir) { Remove-Item -Recurse -Force $ReportDir }

	& $EditorCmd $Project "-ExecCmds=Automation RunTests $Filter;Quit" '-TestExit=Automation Test Queue Empty' `
		"-ReportExportPath=$ReportDir" "-abslog=$LogFile" "-RTSTestTimeDilation=$TimeDilation" $NoMCPPlugins -unattended -nullrhi -nosplash -nosound
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
	$Resolutions = @(@(1920, 1080), @(2560, 1440), @(3440, 1440))
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

switch ($Task) {
	'build' { Invoke-Build }
	'test' { Invoke-Test }
	'setup' { Invoke-Setup }
	'editor' { Invoke-Editor }
	'uishots' { Invoke-UIShots }
}
