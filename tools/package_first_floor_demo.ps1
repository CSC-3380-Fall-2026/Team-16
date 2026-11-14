param(
    [Parameter(Mandatory)] [string]$Project,
    [string]$EngineRoot = $env:UE_ROOT,
    [switch]$FullCook,
    [switch]$ReuseCook,
    [switch]$Verify,
    [string]$ArchiveDirectory,
    [ValidatePattern('^[A-Za-z0-9_]+$')] [string]$MapName = 'PFTFirstFloorComplete',
    [switch]$VerifyExtended
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
$package = [IO.Path]::GetFullPath($package)
$staging = [IO.Path]::GetFullPath((Join-Path $projectRoot 'Saved\StagedBuilds'))
if ($package.Equals($staging, [StringComparison]::OrdinalIgnoreCase) -or
    $package.StartsWith("$staging\", [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Choose an archive directory outside the Unreal staging folder.'
}
$packageParent = Split-Path -Parent $package
$packageName = Split-Path -Leaf $package
$buildId = [guid]::NewGuid().ToString('N')
$candidate = Join-Path $packageParent "$packageName.pending-$buildId"
$previous = Join-Path $packageParent "$packageName.previous-$buildId"
$automationTool = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
$launcher = Join-Path $PSScriptRoot 'Play PFT Demo.cmd'
$frameReport = Join-Path $PSScriptRoot 'measure_demo_frames.ps1'
$assetCheck = Join-Path $PSScriptRoot 'check_demo_assets.ps1'
$demoGuide = Join-Path $PSScriptRoot 'Read Me - PFT Demo.txt'
$navigatorCheck = Join-Path $PSScriptRoot 'install_navigator.py'
$prerequisite = 'Engine\Extras\Redist\en-us\vc_redist.x64.exe'
if (!(Test-Path -LiteralPath $mapFile -PathType Leaf)) {
    throw "Could not find $mapFile. Apply or generate the first-floor map before packaging."
}
if (!(Test-Path -LiteralPath $automationTool -PathType Leaf)) {
    throw 'Unreal Automation Tool is missing. Set UE_ROOT or pass -EngineRoot.'
}
if (!(Test-Path -LiteralPath $launcher -PathType Leaf)) { throw 'Play PFT Demo.cmd must be beside this script.' }
if (!(Test-Path -LiteralPath $frameReport -PathType Leaf)) { throw 'measure_demo_frames.ps1 must be beside this script.' }
if (!(Test-Path -LiteralPath $assetCheck -PathType Leaf)) { throw 'check_demo_assets.ps1 must be beside this script.' }
if (!(Test-Path -LiteralPath $demoGuide -PathType Leaf)) { throw 'Read Me - PFT Demo.txt must be beside this script.' }
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

function Confirm-DemoArchive($directory) {
    $existing = Get-Item -LiteralPath $directory -Force -ErrorAction SilentlyContinue
    if (!$existing) { return }
    if (!$existing.PSIsContainer -or ($existing.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
        !(Test-Path -LiteralPath (Join-Path $directory 'PFTDigitalTwin.exe') -PathType Leaf) -or
        !(Test-Path -LiteralPath (Join-Path $directory 'Manifest_UFSFiles_Win64.txt') -PathType Leaf)) {
        throw "The archive path is occupied by something other than a PFT Windows package: $directory"
    }
}

function Publish-DemoArchive {
    Confirm-DemoArchive $package
    $hasPrevious = [IO.Directory]::Exists($package)
    if ($hasPrevious) {
        Write-Host "Moving the previous demo to $previous."
        [IO.Directory]::Move($package, $previous)
    }
    try {
        [IO.Directory]::Move($candidate, $package)
    } catch {
        if ($hasPrevious -and ![IO.Directory]::Exists($package)) {
            try { [IO.Directory]::Move($previous, $package) }
            catch { throw "Could not restore the previous demo. It is available at $previous. New build: $candidate." }
        }
        throw
    }
}

function Get-ToolsRevision {
    $git = Get-Command git -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (!$git) { return $null }
    $files = @(
        'package_first_floor_demo.ps1', 'Play PFT Demo.cmd', 'measure_demo_frames.ps1',
        'check_demo_assets.ps1', 'Read Me - PFT Demo.txt', 'install_navigator.py', 'validate_data.py'
    )
    $tracked = & $git.Source -C $PSScriptRoot ls-files -- @files 2>$null
    if ($LASTEXITCODE -ne 0 -or @($tracked).Count -ne $files.Count) { return $null }
    $commit = & $git.Source -C $PSScriptRoot rev-parse HEAD 2>$null
    if ($LASTEXITCODE -ne 0) { return $null }
    $modified = & $git.Source -C $PSScriptRoot status --porcelain -- @files '../unreal/PFTNavigator' '../data' 2>$null
    if ($LASTEXITCODE -ne 0) { return $null }
    [ordered]@{ Commit = $commit; Modified = [bool]$modified }
}

function Confirm-NavigatorInstall {
    # Perforce-only copies of the packaging tools have no Git Navigator source to compare.
    $navigatorSource = Join-Path $PSScriptRoot '../unreal/PFTNavigator'
    if (!(Test-Path -LiteralPath $navigatorSource -PathType Container) -and
        !(Test-Path -LiteralPath $navigatorCheck -PathType Leaf)) { return }
    if (!(Test-Path -LiteralPath $navigatorCheck -PathType Leaf)) { throw 'Navigator installer is missing from the Git tools.' }
    if (!(Test-Path -LiteralPath $navigatorSource -PathType Container)) { throw 'Navigator source is missing from the Git checkout.' }
    $python = Get-Command python -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (!$python) { throw 'Python is required to verify the Navigator installed in the Unreal project.' }
    & $python.Source $navigatorCheck $projectFile.FullName --check
    if ($LASTEXITCODE -ne 0) {
        throw 'The installed Navigator could not be verified. See docs/navigator-plugin.md to install this checkout before packaging.'
    }
}

function Confirm-WalkingMap {
    if (!$Verify -and !$VerifyExtended) { return }
    $path = Join-Path $projectRoot 'Plugins/PFTNavigator/Content/Data/first_floor_routes.json'
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Verified packaging requires a walking graph for $map. Bake and install first_floor_routes.json before packaging."
    }
    try {
        $graph = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    } catch {
        throw "Could not read the walking graph at $path."
    }
    if ($graph.Map -cne $map) {
        throw "The walking graph is for '$($graph.Map)'. Bake and install the graph for $map before packaging."
    }
}

function Write-BuildRecord($mapHash, $cookMode, $verification, $toolsRevision, $tests) {
    $enginePath = Join-Path $EngineRoot 'Engine\Build\Build.version'
    $engine = if (Test-Path -LiteralPath $enginePath -PathType Leaf) {
        Get-Content -LiteralPath $enginePath -Raw | ConvertFrom-Json
    }
    [ordered]@{
        BuildId = $buildId
        PackagedAtUtc = [DateTime]::UtcNow.ToString('o')
        Map = $map
        MapSha256 = $mapHash
        Engine = $engine
        Tools = $toolsRevision
        CookMode = $cookMode
        VisualCppInstaller = $prerequisite.Replace('\', '/')
        Verification = $verification
        TestReports = $tests
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $candidate 'PFTDemoBuild.json') -Encoding UTF8
}

function Confirm-WalkthroughReport($path, $testPath) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Windows demo verification did not produce $path."
    }
    try {
        $report = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    } catch {
        throw "Windows demo verification produced an unreadable report at $path."
    }
    $results = @($report.tests)
    if ($results.Count -ne 1 -or $results[0].fullTestPath -cne $testPath -or
        $results[0].state -cne 'Success' -or $report.failed -ne 0 -or
        $report.notRun -ne 0 -or $report.inProcess -ne 0 -or
        ($report.succeeded + $report.succeededWithWarnings) -ne 1) {
        throw "Windows demo verification did not confirm a successful $testPath in $path."
    }
}

$builds = Join-Path $projectRoot 'Saved\FirstFloorBuilds'
$mapBuild = Join-Path $builds "Level\$MapName"
$receipt = Join-Path $mapBuild 'WindowsCook.json'
$cookDirectory = Join-Path $projectRoot 'Saved\Cooked\Windows'
$arguments = @(
    'BuildCookRun', "-project=$($projectFile.FullName)", '-platform=Win64', '-clientconfig=Development',
    '-build', '-stage', '-pak', '-iostore', '-archive', '-prereqs', "-archivedirectory=$candidate",
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
$archiveLock = $null
try {
    $lock = [IO.File]::Open((Join-Path $builds 'WindowsPackage.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
} catch [IO.IOException] {
    throw "Could not lock Windows packaging for $projectRoot. Check for another packaging command."
}
try {
    Confirm-NavigatorInstall
    Confirm-WalkingMap
    $null = New-Item -ItemType Directory -Path $packageParent -Force
    $archiveLock = [IO.File]::Open((Join-Path $packageParent "$packageName.package.lock"), 'OpenOrCreate', 'ReadWrite', 'None')
    Confirm-DemoArchive $package
    [IO.Directory]::CreateDirectory($candidate) > $null
    Write-Host "Building the replacement demo in $candidate."
    $mapHash = (Get-FileHash -LiteralPath $mapFile).Hash
    $toolsRevision = Get-ToolsRevision
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
    $cookMode = 'Incremental'
    if ($usingSavedCook) {
        $cookMode = 'Reused'
        Write-Host "Reusing the saved Windows cook for $map."
        $arguments += '-skipcook'
    } else {
        if ($FullCook) { $cookMode = 'Full' }
        if ($ReuseCook) { Write-Host "$reuseIssue for $map. Cooking assets." }
        if (Test-Path -LiteralPath $receipt -PathType Leaf) { [IO.File]::WriteAllText($receipt, '') }
        $arguments += '-cook'
        if (!$FullCook) { $arguments += '-AdditionalCookerOptions=-CookIncremental' }
    }

    & $automationTool @arguments
    if ($LASTEXITCODE -ne 0) { throw "Unreal packaging failed with exit code $LASTEXITCODE." }

    if (!(Test-Path -LiteralPath (Join-Path $candidate 'PFTDigitalTwin.exe') -PathType Leaf)) {
        throw 'Packaging finished without producing the Windows demo executable.'
    }
    Confirm-DemoArchive $candidate
    if (!(Test-Path -LiteralPath (Join-Path $candidate $prerequisite) -PathType Leaf)) {
        throw 'Packaging finished without the Windows Visual C++ runtime installer.'
    }

    $finalInputs = Get-CookInputs
    $changed = @(Get-ChangedCookInputs $inputs $finalInputs)
    $finalCookHash = Get-CookStoreHash $cookDirectory
    if ((Get-FileHash -LiteralPath $mapFile).Hash -ne $mapHash) {
        throw 'The selected map changed during packaging. Run again to package the updated map.'
    }
    if ($usingSavedCook -and ($changed.Count -or $finalCookHash -ne $cookHash)) {
        throw 'The saved cook or its inputs changed during packaging. Run again with a fresh cook.'
    }
    if (!$changed.Count -and $finalCookHash) {
        @{Map = $map; CookInputs = $finalInputs; Store = $finalCookHash} | ConvertTo-Json | Set-Content -LiteralPath $receipt
    }

    $reportTemplate = Join-Path $EngineRoot 'Engine\Content\Automation\Report-Template.html'
    if (Test-Path -LiteralPath $reportTemplate -PathType Leaf) {
        $reportFolder = Join-Path $candidate 'Engine\Content\Automation'
        $null = New-Item -ItemType Directory -Path $reportFolder -Force
        Copy-Item -LiteralPath $reportTemplate -Destination $reportFolder
    }

    $packageLauncher = Join-Path $candidate 'Play PFT Demo.cmd'
    [IO.File]::WriteAllText($packageLauncher, $launcherText.Replace($defaultMap, $map))
    Copy-Item -LiteralPath $frameReport -Destination $candidate
    Copy-Item -LiteralPath $assetCheck -Destination $candidate
    Copy-Item -LiteralPath $demoGuide -Destination $candidate
    $verification = 'Not requested'
    $tests = [ordered]@{}
    if ($Verify -or $VerifyExtended) {
        $mode = if ($VerifyExtended) { 'verifyextended' } else { 'verify' }
        & $packageLauncher $mode
        if ($LASTEXITCODE -ne 0) {
            throw "Windows demo verification failed. Read the logs under $candidate\PFTDigitalTwin\Saved\Logs."
        }
        $suites = [ordered]@{
            CommonsWalk = 'PFTFirstFloor.WalkToCommons'
            Navigator = 'PFTNavigator.Panel.AfterOnboarding'
            GuidedWalk = 'PFTFirstFloor.GuidedWalk'
        }
        if ($VerifyExtended) { $suites['C1007Walk'] = 'PFTFirstFloor.WalkToC1007' }
        foreach ($suite in $suites.Keys) {
            $report = "PFTDigitalTwin/Saved/Automation/$suite/index.json"
            Confirm-WalkthroughReport (Join-Path $candidate $report) $suites[$suite]
            $tests[$suite] = $report
        }
        $verification = 'Passed'
    }
    Confirm-NavigatorInstall
    Confirm-WalkingMap
    Write-BuildRecord $mapHash $cookMode $verification $toolsRevision $tests
    Publish-DemoArchive
    Write-Output (Join-Path $package 'Play PFT Demo.cmd')
} finally {
    if ($archiveLock) { $archiveLock.Dispose() }
    $lock.Dispose()
}
