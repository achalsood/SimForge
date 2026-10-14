param([string]$Executable,[string]$SourceRoot)
$ErrorActionPreference='Stop'
$dir=Join-Path ([IO.Path]::GetTempPath()) ('simforge-demand-cli-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $dir|Out-Null
try {
    $request=Join-Path $SourceRoot 'examples/demand-calibration/morning-surge.json'
    $report=Join-Path $dir 'report.json';$csv=Join-Path $dir 'report.csv';$scenario=Join-Path $dir 'scenario.json'
    & $Executable --headless --calibrate-demand $request --export-json $report --export-csv $csv --export-scenario $scenario --calibration-source (Join-Path $SourceRoot 'examples/warehouse-baseline.json') --calibration-start '2026-01-05T08:00:00Z' --calibration-seed 42 --accept-periodic-approximation |Out-Null
    if($LASTEXITCODE -ne 0){throw 'Calibration export failed'}
    $json=Get-Content -Raw -LiteralPath $report|ConvertFrom-Json
    if($json.rates.Count -ne 24 -or $json.scenario_conversions.Count -ne 1 -or $json.holdout.observed -ne 240){throw 'Incorrect report fields'}
    $rows=Import-Csv -LiteralPath $csv
    if($rows.Count -ne 98 -or ($rows[0].PSObject.Properties|Measure-Object).Count -ne 19){throw 'CSV schema/row count mismatch'}
    & $Executable --headless --scenario $scenario|Out-Null
    if($LASTEXITCODE -ne 0){throw 'Generated scenario does not execute'}
    $before=[IO.File]::ReadAllText($report)
    $ErrorActionPreference='Continue'
    & $Executable --headless --calibrate-demand $request --export-json $report *> $null
    if($LASTEXITCODE -eq 0 -or [IO.File]::ReadAllText($report) -ne $before){throw 'Existing output was overwritten'}
    foreach($arguments in @(
        @('--calibrate-demand',$request),
        @('--headless','--calibrate-demand',$request,'--scenario',$scenario),
        @('--headless','--calibrate-demand',$request,'--entities','3'),
        @('--headless','--calibrate-demand',$request,'--export-scenario',(Join-Path $dir 'unapproved.json')),
        @('--headless','--calibrate-demand',$request,'--export-json',(Join-Path $dir 'same.json'),'--export-csv',(Join-Path $dir 'same.json')),
        @('--headless','--calibrate-demand',$request,'--calibrate-demand',$request),
        @('--headless','--export-scenario',(Join-Path $dir 'invalid.json')))) {
        & $Executable @arguments *> $null;if($LASTEXITCODE -eq 0){throw 'Invalid calibration CLI accepted'}
    }
    Write-Output 'Demand CLI: JSON/CSV, conversion, simulation, exclusive output and conflicting options verified'
} finally {
    $resolved=[IO.Path]::GetFullPath($dir);$temp=[IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if($resolved.StartsWith($temp,[StringComparison]::OrdinalIgnoreCase) -and (Split-Path $resolved -Leaf).StartsWith('simforge-demand-cli-')){Remove-Item -LiteralPath $resolved -Recurse -Force}
}
