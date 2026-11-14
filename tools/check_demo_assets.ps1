param([Parameter(Mandatory)] [string]$Log)

$ErrorActionPreference = 'Stop'
$patterns = @(
    'LogStreaming: (?:Warning|Error): .*Missing Dependency,',
    'LogStreaming: (?:Warning|Error): .*SkipPackage: .*does not exist on disk or in the loader',
    'LogMaterial: (?:Warning|Error): .*Default Material will be used in game'
)
$failures = @(Select-String -LiteralPath $Log -Pattern $patterns)
if ($failures.Count) {
    Write-Host "Found $($failures.Count) asset-loading failures in $Log."
    $failures | Select-Object -First 10 | ForEach-Object { Write-Host "Line $($_.LineNumber) $($_.Line)" }
    exit 1
}
