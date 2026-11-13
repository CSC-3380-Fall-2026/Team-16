param(
    [Parameter(Mandatory)] [string]$Project,
    [string]$EngineRoot = $env:UE_ROOT,
    [switch]$FullCook,
    [switch]$ReuseCook,
    [switch]$Verify,
    [string]$ArchiveDirectory,
    [ValidatePattern('^[A-Za-z0-9_]+$')] [string]$MapName = 'PFTFirstFloorComplete'
)

$ErrorActionPreference = 'Stop'
if (!$EngineRoot) { $EngineRoot = 'C:\Program Files\Epic Games\UE_5.8' }
if ($FullCook -and $ReuseCook) { throw 'Choose either -FullCook or -ReuseCook.' }

$projectFile = Get-Item -LiteralPath $Project
if ($projectFile.PSIsContainer -or $projectFile.Extension -ne '.uproject') {
    throw 'Pass the path to PFTDigitalTwin.uproject with -Project.'
}

$projectRoot = $projectFile.DirectoryName
$map = "/Game/Level/$MapName"
$mapFile = Join-Path $projectRoot "Content\Level\$MapName.umap"
$defaultMap = '/Game/Level/PFTFirstFloorComplete'
$package = if ($map -eq $defaultMap) {
    Join-Path $projectRoot 'Saved\FirstFloorWindows\Windows'
} else {
    Join-Path $projectRoot "Saved\FirstFloorBuilds\Level\$MapName\WindowsDemo"
}
if ($ArchiveDirectory) {
    $archiveRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ArchiveDirectory)
    $package = Join-Path $archiveRoot 'Windows'
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

function Get-CombinedHash($entries) {
    $entries.Sort([StringComparer]::Ordinal)
    $bytes = [Text.Encoding]::UTF8.GetBytes($entries -join "`n")
    $stream = [IO.MemoryStream]::new($bytes)
    try { (Get-FileHash -InputStream $stream).Hash } finally { $stream.Dispose() }
}

function Get-CookInputs {
    $recursive = [IO.SearchOption]::AllDirectories
    $assetFolders = @("$projectRoot/Content")
    $settingFolders = @("$projectRoot/Config", "$projectRoot/Shaders")
    $sourceFolders = @("$projectRoot/Source")
    $descriptors = @($projectFile, (Get-Item -LiteralPath $mapFile))
    $engineVersion = Join-Path $EngineRoot 'Engine\Build\Build.version'
    if (Test-Path -LiteralPath $engineVersion -PathType Leaf) {
        $descriptors += Get-Item -LiteralPath $engineVersion
    }
    $plugins = Join-Path $projectRoot 'Plugins'
    if ([IO.Directory]::Exists($plugins)) {
        foreach ($plugin in [IO.DirectoryInfo]::new($plugins).EnumerateFiles('*.uplugin', $recursive)) {
            $descriptors += $plugin
            $assetFolders += "$($plugin.DirectoryName)/Content"
            $settingFolders += @("$($plugin.DirectoryName)/Config", "$($plugin.DirectoryName)/Shaders")
            $sourceFolders += "$($plugin.DirectoryName)/Source"
        }
    }

    # Keep the asset scan inexpensive for large Content folders.
    $assets = [Collections.Generic.List[string]]::new()
    foreach ($folder in $assetFolders | Where-Object { [IO.Directory]::Exists($_) }) {
        foreach ($file in [IO.DirectoryInfo]::new($folder).EnumerateFiles('*', $recursive)) {
            $assets.Add("$($file.FullName)|$($file.Length)|$($file.LastWriteTimeUtc.Ticks)")
        }
    }
    $settings = [Collections.Generic.List[string]]::new()
    foreach ($folder in $settingFolders | Where-Object { [IO.Directory]::Exists($_) }) {
        foreach ($file in [IO.DirectoryInfo]::new($folder).EnumerateFiles('*', $recursive)) {
            $settings.Add("$($file.FullName)|$((Get-FileHash -LiteralPath $file.FullName).Hash)")
        }
    }
    $source = [Collections.Generic.List[string]]::new()
    foreach ($folder in $sourceFolders | Where-Object { [IO.Directory]::Exists($_) }) {
        foreach ($file in [IO.DirectoryInfo]::new($folder).EnumerateFiles('*', $recursive)) {
            if ($file.Extension -in '.h', '.hpp', '.inl', '.cs') {
                $source.Add("$($file.FullName)|$((Get-FileHash -LiteralPath $file.FullName).Hash)")
            }
        }
    }
    $project = [Collections.Generic.List[string]]::new()
    foreach ($file in $descriptors) {
        $project.Add("$($file.FullName)|$((Get-FileHash -LiteralPath $file.FullName).Hash)")
    }
    @{
        Assets = Get-CombinedHash $assets
        Settings = Get-CombinedHash $settings
        HeadersAndBuildRules = Get-CombinedHash $source
        ProjectAndMap = Get-CombinedHash $project
    }
}

function Get-ChangedCookInputs($savedInputs, $currentInputs) {
    foreach ($name in ($currentInputs.Keys | Sort-Object)) {
        if ($savedInputs.$name -ne $currentInputs[$name]) { $name }
    }
}

function Get-CookStoreHash($directory) {
    $metadata = Join-Path $directory "$($projectFile.BaseName)\Metadata"
    $files = @(
        (Join-Path $directory 'ue.projectstore'),
        (Join-Path $metadata 'CookedIniVersion.txt'),
        (Join-Path $metadata 'CookMetadata.ucookmeta'),
        (Join-Path $metadata 'DevelopmentAssetRegistry.bin'),
        (Join-Path $metadata 'zenfs.manifest')
    )
    $entries = [Collections.Generic.List[string]]::new()
    foreach ($file in $files) {
        if (!(Test-Path -LiteralPath $file -PathType Leaf)) { return $null }
        $entries.Add("$file|$((Get-FileHash -LiteralPath $file).Hash)")
    }
    Get-CombinedHash $entries
}

$builds = Join-Path $projectRoot 'Saved\FirstFloorBuilds'
$mapBuild = Join-Path $builds "Level\$MapName"
$receipt = Join-Path $mapBuild 'WindowsCook.json'
$cookDirectory = Join-Path $projectRoot 'Saved\Cooked\Windows'
$arguments = @(
    'BuildCookRun', "-project=$($projectFile.FullName)", '-platform=Win64', '-clientconfig=Development',
    '-build', '-stage', '-pak', '-iostore', '-archive', "-archivedirectory=$package",
    "-map=$map", '-unattended', '-nop4', '-utf8output', '-ubtargs=-NoHotReloadFromIDE'
)
if ($map -ne $defaultMap) {
    # Keep the candidate's cooked packages and metadata separate from the standard demo.
    $cookDirectory = Join-Path $mapBuild 'Cooked\Windows'
    $cacheSuffix = $MapName.ToLowerInvariant()
    $arguments += @(
        "-CookOutputDir=$cookDirectory",
        "-ini:Engine:[ConsoleVariables]:ZenStoreWriter.OplogIdSuffix=.$cacheSuffix",
        "-ini:Engine:[/Script/EngineSettings.GameMapsSettings]:GameDefaultMap=$map.$MapName"
    )
}

$null = New-Item -ItemType Directory -Path $mapBuild -Force
# Standard and candidate builds use the same staging directory.
try {
    $lock = [IO.File]::Open((Join-Path $builds 'WindowsPackage.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
} catch [IO.IOException] {
    throw "Could not lock Windows packaging for $projectRoot. Check for another packaging command."
}
try {
    $inputs = Get-CookInputs
    $saved = $null
    if ($ReuseCook -and (Test-Path -LiteralPath $receipt -PathType Leaf)) {
        try { $saved = Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json } catch { $saved = $null }
    }
    $reuseIssue = $null
    $cookHash = $null
    if ($ReuseCook) {
        if (!$saved -or $saved.Map -ne $map -or !$saved.CookInputs) {
            $reuseIssue = 'No compatible cook receipt'
        } else {
            $changed = @(Get-ChangedCookInputs $saved.CookInputs $inputs)
            $cookHash = Get-CookStoreHash $cookDirectory
            if ($changed.Count) { $reuseIssue = "Cook inputs changed ($($changed -join ', '))" }
            elseif (!$cookHash -or $saved.Store -ne $cookHash) { $reuseIssue = 'Cook metadata is missing or changed' }
        }
    }
    $usingSavedCook = $ReuseCook -and !$reuseIssue
    if ($usingSavedCook) {
        Write-Host "Reusing the saved Windows cook for $map."
        $arguments += '-skipcook'
    } else {
        if ($ReuseCook) { Write-Host "$reuseIssue for $map. Cooking assets." }
        if (Test-Path -LiteralPath $receipt -PathType Leaf) { [IO.File]::WriteAllText($receipt, '') }
        $arguments += '-cook'
        if (!$FullCook) { $arguments += '-AdditionalCookerOptions=-CookIncremental' }
    }

    & $automationTool @arguments
    if ($LASTEXITCODE -ne 0) { throw "Unreal packaging failed with exit code $LASTEXITCODE." }

    if (!(Test-Path -LiteralPath (Join-Path $package 'PFTDigitalTwin.exe') -PathType Leaf)) {
        throw 'Packaging finished without producing the Windows demo executable.'
    }

    $finalInputs = Get-CookInputs
    $changed = @(Get-ChangedCookInputs $inputs $finalInputs)
    $finalCookHash = Get-CookStoreHash $cookDirectory
    if ($usingSavedCook -and ($changed.Count -or $finalCookHash -ne $cookHash)) {
        throw 'The saved cook or its inputs changed during packaging. Run again with a fresh cook.'
    }
    if (!$changed.Count -and $finalCookHash) {
        @{Map = $map; CookInputs = $finalInputs; Store = $finalCookHash} | ConvertTo-Json | Set-Content -LiteralPath $receipt
    }

    $reportTemplate = Join-Path $EngineRoot 'Engine\Content\Automation\Report-Template.html'
    if (Test-Path -LiteralPath $reportTemplate -PathType Leaf) {
        $reportFolder = Join-Path $package 'Engine\Content\Automation'
        $null = New-Item -ItemType Directory -Path $reportFolder -Force
        Copy-Item -LiteralPath $reportTemplate -Destination $reportFolder
    }

    $packageLauncher = Join-Path $package 'Play PFT Demo.cmd'
    [IO.File]::WriteAllText($packageLauncher, $launcherText.Replace($defaultMap, $map))
    if ($Verify) {
        & $packageLauncher verify
        if ($LASTEXITCODE -ne 0) {
            throw "Windows demo verification failed. Read the logs under $package\PFTDigitalTwin\Saved\Logs."
        }
    }
    Write-Output $packageLauncher
} finally {
    $lock.Dispose()
}
