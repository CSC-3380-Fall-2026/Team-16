param(
    [Parameter(Mandatory)] [string]$Project,
    [Parameter(Mandatory)] [ValidatePattern('^[A-Za-z0-9_]+$')] [string]$MapName,
    [string]$EngineRoot = $env:UE_ROOT,
    [switch]$Resume,
    [switch]$PackageDemo
)

$ErrorActionPreference = 'Stop'
if (!$EngineRoot) { $EngineRoot = 'C:\Program Files\Epic Games\UE_5.8' }

$projectFile = Get-Item -LiteralPath $Project
if ($projectFile.PSIsContainer -or $projectFile.Extension -ne '.uproject') {
    throw 'Pass the path to PFTDigitalTwin.uproject with -Project.'
}
if ($MapName -eq 'PatrickFTaylorHall') {
    throw 'Choose a new first-floor map name.'
}

$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (!(Test-Path -LiteralPath $editor -PathType Leaf)) {
    throw 'Unreal Editor is missing. Set UE_ROOT or pass -EngineRoot.'
}

$projectRoot = $projectFile.DirectoryName
$map = "/Game/Level/$MapName"
$mapFile = Join-Path $projectRoot "Content\Level\$MapName.umap"
$reports = Join-Path $projectRoot "Saved\FirstFloorBuilds\Level\$MapName"
if ($MapName -eq 'PFTFirstFloorComplete') { $reports = Join-Path $projectRoot 'Saved' }
$logs = Join-Path $reports 'RebuildLogs'
$null = New-Item -ItemType Directory -Path $logs -Force
$buildReport = Join-Path $reports 'FirstFloorBuild.json'
$verificationReport = Join-Path $reports 'FirstFloorVerification.json'
$runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'

function Invoke-MapStep($name, $python, $marker) {
    $log = Join-Path $logs "$runId-$name.log"
    Write-Host "$name -> $log"
    $arguments = @(
        ('"{0}"' -f $projectFile.FullName), $python, '-unattended', '-nullrhi',
        '-RenderOffscreen', '-nosplash', '-nop4', '-nosound', '-NoLiveCoding',
        ('-abslog="{0}"' -f $log)
    )
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $process.WaitForExit()
    if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $log -PathType Leaf) -or
        !(Select-String -LiteralPath $log -SimpleMatch $marker -Quiet)) {
        throw "$name failed. Read $log."
    }
}

$lock = [IO.File]::Open((Join-Path $logs 'build.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
try {
    $generator = (Join-Path $PSScriptRoot 'create_first_floor_level.py').Replace('\', '/')
    $verifier = (Join-Path $PSScriptRoot 'verify_first_floor_level.py').Replace('\', '/')
    $generate = '/Engine/Maps/Entry -ExecutePythonScript="{0} --output {1}"' -f $generator, $map
    $finish = '/Engine/Maps/Entry -ExecutePythonScript="{0} --output {1} --finish"' -f $generator, $map
    $verify = '-run=PythonScript -script="{0} --output {1}"' -f $verifier, $map

    if ($Resume) {
        if (!(Test-Path -LiteralPath $mapFile -PathType Leaf) -or
            !(Test-Path -LiteralPath $buildReport -PathType Leaf)) {
            throw 'Resume requires saved geometry and its build report.'
        }
    } else {
        if (Test-Path -LiteralPath $mapFile) { throw 'The map already exists. Choose a new name or use -Resume.' }
        Invoke-MapStep 'Geometry' $generate "FIRST_FLOOR_GEOMETRY_COMPLETE: $map;"
    }

    $build = Get-Content -LiteralPath $buildReport -Raw | ConvertFrom-Json
    if ($build.Output -ne $map -or $build.DryRun -ne $false -or $build.CopiedActors -le 0) {
        throw "The build report at $buildReport does not describe saved geometry for $map."
    }
    if (!$build.Complete) {
        Invoke-MapStep 'Finish' $finish "FIRST_FLOOR_BUILD_COMPLETE: $map,"
    }
    Invoke-MapStep 'Verify' $verify 'FIRST_FLOOR_VERIFIED:'
    $verified = Get-Content -LiteralPath $verificationReport -Raw | ConvertFrom-Json
    if ($verified.Map -ne $map -or $null -eq $verified.Errors -or $verified.Errors.Count -ne 0) {
        throw "Verification report does not confirm a passing result for $map."
    }
    Write-Output $mapFile
    Write-Output $verificationReport
    if ($PackageDemo) {
        & (Join-Path $PSScriptRoot 'package_first_floor_demo.ps1') -Project $projectFile.FullName `
            -EngineRoot $EngineRoot -MapName $MapName -ReuseCook -Verify
    }
} finally {
    $lock.Dispose()
}
