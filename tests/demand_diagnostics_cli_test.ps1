param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$SourceRoot)
$ErrorActionPreference='Stop'
$taskBoundary=[IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$taskPath=Join-Path $taskBoundary ('simforge-diagnostic-cli-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskPath|Out-Null
function Invoke-Check([string[]]$Arguments,[bool]$Success) {
 $previousPreference=$ErrorActionPreference;$ErrorActionPreference='Continue'
 $text=& $Executable @Arguments 2>&1; $code=$LASTEXITCODE;$ErrorActionPreference=$previousPreference
 if(($code -eq 0) -ne $Success){throw ('Unexpected CLI status: '+($text|Out-String))}
}
try {
 $examples=Join-Path $SourceRoot 'examples/demand-diagnostics'
 $original=Get-Content (Join-Path $examples 'poisson-compatible.json') -Raw -Encoding UTF8|ConvertFrom-Json
 $original.source_calibration=Join-Path $examples 'poisson-compatible-calibration.json'
 $original.csv=Join-Path $examples 'poisson-compatible.csv';$original.settings.replications=10
 $request=Join-Path $taskPath 'request.json';$original|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $request -Encoding UTF8
 $json=Join-Path $taskPath 'report.json';$csv=Join-Path $taskPath 'report.csv'
 Invoke-Check @('--headless','--diagnose-demand',$request,'--export-json',$json,'--export-csv',$csv) $true
 $report=Get-Content $json -Raw -Encoding UTF8|ConvertFrom-Json
 if($report.diagnostic_report_version -ne 1 -or $report.method_version -ne 1 -or $report.arrival_generator_version -ne 2 -or $report.replications.Count -ne 10 -or $report.bins.Count -ne 168){throw 'Missing diagnostic reproducibility evidence'}
 if(-not $report.future_prediction.process_only -or -not $report.future_prediction.parameter_aware_envelope){throw 'Missing distinguished predictive uncertainty'}
 $rows=Import-Csv $csv;if(-not ($rows|Where-Object table -eq 'reference_statistics') -or -not ($rows|Where-Object table -eq 'parameter_aware_coverage')){throw 'Missing typed CSV tables'}
 Invoke-Check @('--diagnose-demand',$request) $false
 Invoke-Check @('--headless','--diagnose-demand',$request,'--calibrate-demand',(Join-Path $SourceRoot 'examples/stochastic-coverage/multi-shift.json')) $false
 Invoke-Check @('--headless','--diagnose-demand',$request,'--export-json',$json) $false
 $collision=Join-Path $taskPath 'collision.json';Invoke-Check @('--headless','--diagnose-demand',$request,'--export-json',$collision,'--export-csv',$collision) $false
 if(Test-Path $collision){throw 'Invalid output request published a file'}
 $wrong=Join-Path $taskPath 'wrong.csv';'timestamp'|Set-Content -LiteralPath $wrong -Encoding ASCII;$original.csv=$wrong;$original|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $request -Encoding UTF8
 $rejected=Join-Path $taskPath 'rejected.json';Invoke-Check @('--headless','--diagnose-demand',$request,'--export-json',$rejected) $false;if(Test-Path $rejected){throw 'Fingerprint rejection published evidence'}
} finally {
 $resolved=[IO.Path]::GetFullPath($taskPath)
 if(-not $resolved.StartsWith($taskBoundary.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Cleanup path escaped task boundary'}
 Remove-Item -LiteralPath $resolved -Recurse -Force
}
