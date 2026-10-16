<#
.SYNOPSIS
Composes the camera comparison sheets from the pictures and metrics.csv of rts.Debug.CameraShots (ue.ps1 -Task camerashots).

.DESCRIPTION
One sheet per scene (vehicle, formation, base, battlefield) at 1920x1080: a row per camera (before the real-scale
retune and now), a column per zoom (closest, default, farthest), each picture captioned with its field of view, arm, pitch, the ground across and deep, and
the M20, tank and soldier's broadside width. A last sheet has the base at 2560x1440 and 3440x1440. The sheets are JPEGs
in the same folder: Sheet_<scene>.jpg and Sheet_screens.jpg.
#>
param(
	[string]$ShotDir = (Join-Path $PSScriptRoot '..\..\Saved\CameraShots')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$Metrics = Import-Csv (Join-Path $ShotDir 'metrics.csv')
$ThumbWidth = 640
$LabelWidth = 170
$CaptionHeight = 74
$HeaderHeight = 48
$Font = New-Object System.Drawing.Font('Segoe UI', 10)
$BoldFont = New-Object System.Drawing.Font('Segoe UI', 13, [System.Drawing.FontStyle]::Bold)
$Ink = [System.Drawing.Brushes]::White
$Back = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(24, 26, 30))

function Format-Width($Px, $Pct) {
	if ([double]$Px -lt 0) { return 'off screen' }
	return "$Px px ($Pct %)"
}

function Get-Caption($Row) {
	$Across = if ([double]$Row.across_m -lt 0) { 'to the horizon' } else { "$($Row.across_m) m" }
	$Deep = if ([double]$Row.deep_m -lt 0) { 'to the horizon' } else { "$($Row.deep_m) m" }
	return @(
		"FOV $($Row.fov_h16x9) (camera $($Row.camera_fov))  arm $($Row.arm_cm) cm  pitch $($Row.pitch)  pan $($Row.pan_cm_s) cm/s",
		"ground across $Across, top to bottom $Deep",
		"M20 $(Format-Width $Row.DA_TestM20Guardian_px $Row.DA_TestM20Guardian_pct)  tank $(Format-Width $Row.DA_TestTank_px $Row.DA_TestTank_pct)  soldier $(Format-Width $Row.DA_TestInfantry_px $Row.DA_TestInfantry_pct)"
	) -join "`n"
}

# Draws the grid of Cells (rows of metrics rows, $null for an empty cell) under Title with RowLabels on the left.
function Write-Sheet($Path, $Title, $ColumnLabels, $RowLabels, $Cells) {
	$Columns = ($Cells | ForEach-Object { $_.Count } | Measure-Object -Maximum).Maximum
	$RowHeights = @()
	foreach ($Row in $Cells) {
		$Height = 0
		foreach ($Cell in $Row) {
			if ($Cell) {
				$Size = $Cell.screen -split 'x'
				$Height = [Math]::Max($Height, [int]($ThumbWidth * [double]$Size[1] / [double]$Size[0]))
			}
		}
		$RowHeights += $Height + $CaptionHeight
	}
	$Width = $LabelWidth + $Columns * $ThumbWidth
	$Height = $HeaderHeight * 2 + ($RowHeights | Measure-Object -Sum).Sum
	$Sheet = [System.Drawing.Bitmap]::new([int]$Width, [int]$Height)
	$Graphics = [System.Drawing.Graphics]::FromImage($Sheet)
	$Graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
	$Graphics.FillRectangle($Back, 0, 0, $Width, $Height)
	$Graphics.DrawString($Title, $BoldFont, $Ink, 10, 12)
	for ($Column = 0; $Column -lt $ColumnLabels.Count; ++$Column) {
		$Graphics.DrawString($ColumnLabels[$Column], $BoldFont, $Ink, $LabelWidth + $Column * $ThumbWidth + 10, $HeaderHeight + 10)
	}
	$Y = $HeaderHeight * 2
	for ($RowIndex = 0; $RowIndex -lt $Cells.Count; ++$RowIndex) {
		$Graphics.DrawString($RowLabels[$RowIndex], $BoldFont, $Ink, 10, $Y + 10)
		for ($Column = 0; $Column -lt $Cells[$RowIndex].Count; ++$Column) {
			$Cell = $Cells[$RowIndex][$Column]
			if (-not $Cell) { continue }
			$Image = [System.Drawing.Image]::FromFile((Join-Path $ShotDir "$($Cell.shot).png"))
			$ThumbHeight = [int]($ThumbWidth * $Image.Height / $Image.Width)
			$X = $LabelWidth + $Column * $ThumbWidth
			$Graphics.DrawImage($Image, $X + 2, $Y, $ThumbWidth - 4, $ThumbHeight)
			$Image.Dispose()
			$Graphics.DrawString((Get-Caption $Cell), $Font, $Ink, $X + 4, $Y + $ThumbHeight + 4)
		}
		$Y += $RowHeights[$RowIndex]
	}
	$Graphics.Dispose()
	$Codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq 'image/jpeg' }
	$Params = New-Object System.Drawing.Imaging.EncoderParameters(1)
	$Params.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter([System.Drawing.Imaging.Encoder]::Quality, [long]88)
	$Sheet.Save($Path, $Codec, $Params)
	$Sheet.Dispose()
	Write-Host "  $Path"
}

function Find-Shot($Screen, $Profile, $Scene, $Zoom) {
	return $Metrics | Where-Object { $_.screen -eq $Screen -and $_.profile -eq $Profile -and $_.scene -eq $Scene -and $_.zoom -eq $Zoom } | Select-Object -First 1
}

$Profiles = @('Previous', 'Final')
$Zooms = @('min', 'default', 'max')
foreach ($Scene in @('vehicle', 'formation', 'base', 'battlefield')) {
	$Cells = @(foreach ($Profile in $Profiles) { , @(foreach ($Zoom in $Zooms) { Find-Shot '1920x1080' $Profile $Scene $Zoom }) })
	Write-Sheet (Join-Path $ShotDir "Sheet_$Scene.jpg") "Camera comparison: $Scene at 1920x1080" @('Closest zoom', 'Default zoom', 'Farthest zoom') $Profiles $Cells
}

$Cells = @(
	, @(foreach ($Profile in $Profiles) { Find-Shot '2560x1440' $Profile 'base' 'default' })
	, @(foreach ($Profile in @('Previous', 'Final', 'FinalHorFOV')) { Find-Shot '3440x1440' $Profile 'base' 'default' })
	, @(foreach ($Profile in @('Previous', 'Final', 'FinalHorFOV')) { Find-Shot '3440x1440' $Profile 'base' 'max' })
)
Write-Sheet (Join-Path $ShotDir 'Sheet_screens.jpg') 'Camera comparison: the base on other screens' @('Previous', 'Final', 'Final, horizontal FOV kept') `
	@('1440p default', '21:9 default', '21:9 max') $Cells
