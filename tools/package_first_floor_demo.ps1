param(
    [Parameter(Mandatory)] [string]$Project,
    [string]$EngineRoot = $env:UE_ROOT,
    [switch]$FullCook,
    [string]$ArchiveDirectory
)

$ErrorActionPreference = 'Stop'
if (!$EngineRoot) { $EngineRoot = 'C:\Program Files\Epic Games\UE_5.8' }

$projectFile = Get-Item -LiteralPath $Project
if ($projectFile.PSIsContainer -or $projectFile.Extension -ne '.uproject') {
    throw 'Pass the path to PFTDigitalTwin.uproject with -Project.'
}

$projectRoot = $projectFile.DirectoryName
$mapFile = Join-Path $projectRoot 'Content\Level\PFTFirstFloorComplete.umap'
$archiveRoot = Join-Path $projectRoot 'Saved\FirstFloorWindows'
if ($ArchiveDirectory) {
    $archiveRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ArchiveDirectory)
}
$automationTool = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
$launcher = Join-Path $PSScriptRoot 'Play PFT Demo.cmd'
if (!(Test-Path -LiteralPath $mapFile -PathType Leaf)) {
    throw 'PFTFirstFloorComplete is missing. Apply the first-floor map or generate it before packaging.'
}
if (!(Test-Path -LiteralPath $automationTool -PathType Leaf)) {
    throw 'Unreal Automation Tool is missing. Set UE_ROOT or pass -EngineRoot.'
}
if (!(Test-Path -LiteralPath $launcher -PathType Leaf)) { throw 'Play PFT Demo.cmd must be beside this script.' }

$arguments = @(
    'BuildCookRun', "-project=$($projectFile.FullName)", '-platform=Win64', '-clientconfig=Development',
    '-build', '-cook', '-stage', '-pak', '-iostore', '-archive', "-archivedirectory=$archiveRoot",
    '-map=/Game/Level/PFTFirstFloorComplete', '-unattended', '-nop4', '-utf8output', '-ubtargs=-NoHotReloadFromIDE'
)
if (!$FullCook) { $arguments += '-AdditionalCookerOptions=-CookIncremental' }

& $automationTool @arguments
if ($LASTEXITCODE -ne 0) { throw "Unreal packaging failed with exit code $LASTEXITCODE." }

$package = Join-Path $archiveRoot 'Windows'
if (!(Test-Path -LiteralPath (Join-Path $package 'PFTDigitalTwin.exe') -PathType Leaf)) {
    throw 'Packaging finished without producing the Windows demo executable.'
}
Copy-Item -LiteralPath $launcher -Destination $package
Write-Output (Join-Path $package 'Play PFT Demo.cmd')
