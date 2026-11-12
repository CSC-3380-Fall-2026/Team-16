param(
    [Parameter(Mandatory)] [string]$Project,
    [string]$EngineRoot = $env:UE_ROOT,
    [switch]$FullCook,
    [string]$ArchiveDirectory,
    [ValidatePattern('^[A-Za-z0-9_]+$')] [string]$MapName = 'PFTFirstFloorComplete'
)

$ErrorActionPreference = 'Stop'
if (!$EngineRoot) { $EngineRoot = 'C:\Program Files\Epic Games\UE_5.8' }

$projectFile = Get-Item -LiteralPath $Project
if ($projectFile.PSIsContainer -or $projectFile.Extension -ne '.uproject') {
    throw 'Pass the path to PFTDigitalTwin.uproject with -Project.'
}

$projectRoot = $projectFile.DirectoryName
$map = "/Game/Level/$MapName"
$mapFile = Join-Path $projectRoot "Content\Level\$MapName.umap"
$defaultMap = '/Game/Level/PFTFirstFloorComplete'
$archiveRoot = if ($map -eq $defaultMap) {
    Join-Path $projectRoot 'Saved\FirstFloorWindows'
} else {
    Join-Path $projectRoot "Saved\FirstFloorBuilds\Level\$MapName\WindowsDemo"
}
if ($ArchiveDirectory) {
    $archiveRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ArchiveDirectory)
}
$automationTool = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
$launcher = Join-Path $PSScriptRoot 'Play PFT Demo.cmd'
if (!(Test-Path -LiteralPath $mapFile -PathType Leaf)) {
    throw "Could not find $mapFile. Apply or generate the first-floor map before packaging."
}
if (!(Test-Path -LiteralPath $automationTool -PathType Leaf)) {
    throw 'Unreal Automation Tool is missing. Set UE_ROOT or pass -EngineRoot.'
}
if (!(Test-Path -LiteralPath $launcher -PathType Leaf)) { throw 'Play PFT Demo.cmd must be beside this script.' }
$launcherText = Get-Content -LiteralPath $launcher -Raw
if (!$launcherText.Contains($defaultMap)) { throw 'The launcher is missing its first-floor map setting.' }

$arguments = @(
    'BuildCookRun', "-project=$($projectFile.FullName)", '-platform=Win64', '-clientconfig=Development',
    '-build', '-cook', '-stage', '-pak', '-iostore', '-archive', "-archivedirectory=$archiveRoot",
    "-map=$map", '-unattended', '-nop4', '-utf8output', '-ubtargs=-NoHotReloadFromIDE'
)
if (!$FullCook) { $arguments += '-AdditionalCookerOptions=-CookIncremental' }
if ($map -ne $defaultMap) {
    # Cook the review map as the game default for this build.
    $arguments += "-ini:Engine:[/Script/EngineSettings.GameMapsSettings]:GameDefaultMap=$map.$MapName"
}

& $automationTool @arguments
if ($LASTEXITCODE -ne 0) { throw "Unreal packaging failed with exit code $LASTEXITCODE." }

$package = Join-Path $archiveRoot 'Windows'
if (!(Test-Path -LiteralPath (Join-Path $package 'PFTDigitalTwin.exe') -PathType Leaf)) {
    throw 'Packaging finished without producing the Windows demo executable.'
}
$packageLauncher = Join-Path $package 'Play PFT Demo.cmd'
[IO.File]::WriteAllText($packageLauncher, $launcherText.Replace($defaultMap, $map))
Write-Output $packageLauncher
