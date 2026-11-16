param(
    [Parameter(Mandatory)] [string]$Log,
    [string]$Scenario = 'Commons walkthrough'
)

$ErrorActionPreference = 'Stop'
$logFile = Get-Item -LiteralPath $Log
$capture = Select-String -LiteralPath $logFile.FullName -Pattern 'Capture Ended\. Writing CSV to file\s*:\s*(.+\.csv)\s*$' |
    Select-Object -Last 1
if (!$capture) { throw "No frame capture was recorded. Read $($logFile.FullName)." }

$captureName = [IO.Path]::GetFileName($capture.Matches[0].Groups[1].Value)
$csv = Join-Path $logFile.Directory.Parent.FullName "Profiling\CSV\$captureName"
$culture = [Globalization.CultureInfo]::InvariantCulture
$rows = @(Import-Csv -LiteralPath $csv)

function Get-Samples([string]$column) {
    foreach ($row in $rows) {
        $milliseconds = 0.0
        if ([double]::TryParse($row.$column, [Globalization.NumberStyles]::Float, $culture, [ref]$milliseconds) -and
            $milliseconds -gt 0 -and ![double]::IsInfinity($milliseconds)) {
            $milliseconds
        }
    }
}

$frames = @(Get-Samples 'FrameTime')
if (!$frames.Count) { throw "No valid frame times were found in $csv." }
[Array]::Sort($frames)

function Get-Percentile([double[]]$samples, [double]$percentile) {
    $index = [int][Math]::Ceiling($percentile * $samples.Count) - 1
    $samples[$index].ToString('0.00', $culture)
}

$report = @(
    "$Scenario frame pacing",
    "Frames: $($frames.Count)",
    "50th percentile: $(Get-Percentile $frames 0.50) ms",
    "95th percentile: $(Get-Percentile $frames 0.95) ms",
    "99th percentile: $(Get-Percentile $frames 0.99) ms",
    "Slowest frame: $(Get-Percentile $frames 1.0) ms",
    "Frames over 50 ms: $(@($frames | Where-Object { $_ -gt 50 }).Count)",
    "Frames over 100 ms: $(@($frames | Where-Object { $_ -gt 100 }).Count)"
)
$timings = [ordered]@{
    GameThreadTime = 'Game thread'
    RenderThreadTime = 'Render thread'
    RHIThreadTime = 'RHI thread'
    GPUTime = 'GPU'
}
foreach ($column in $timings.Keys) {
    $samples = @(Get-Samples $column)
    if (!$samples.Count) { continue }
    [Array]::Sort($samples)
    $report += "$($timings[$column]) 95th percentile $(Get-Percentile $samples 0.95) ms, maximum $(Get-Percentile $samples 1.0) ms"
}
$report += "Capture: $csv"
$reportPath = Join-Path $logFile.DirectoryName "$($logFile.BaseName)-FramePacing.txt"
$report | Set-Content -LiteralPath $reportPath -Encoding UTF8
$report
Write-Output "Report: $reportPath"
