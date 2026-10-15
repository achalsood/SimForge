param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$SourceRoot)
$ErrorActionPreference='Stop'
$boundary=[IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$taskPath=Join-Path $boundary ('simforge-checkpoint-cli-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskPath|Out-Null
function Invoke-Check([string[]]$Arguments,[bool]$Success){
 $previous=$ErrorActionPreference;$ErrorActionPreference='Continue'
 $text=& $Executable @Arguments 2>&1;$code=$LASTEXITCODE;$ErrorActionPreference=$previous
 if(($code -eq 0) -ne $Success){throw ('Unexpected checkpoint CLI exit '+$code+': '+($text|Out-String))}
}
try {
 $example=Join-Path $SourceRoot 'examples/checkpoint-interventions/forklift.json'
 $json=Join-Path $taskPath 'report.json';$csv=Join-Path $taskPath 'summary.csv';$seeds=Join-Path $taskPath 'seeds.csv'
 Invoke-Check @('--headless','--experiment',$example,'--export-json',$json,'--export-csv',$csv,'--export-seeds',$seeds) $true
 $report=Get-Content -LiteralPath $json -Raw -Encoding UTF8|ConvertFrom-Json
 if($report.checkpoint_report_version -ne 1 -or $report.outcomes.Count -ne 20 -or $report.prefix_executions -ne 20 -or $report.control_executions -ne 20 -or $report.intervention_executions -ne 20 -or $report.request.boundary -ne 'after_due_events_and_source_commands'){throw 'Incomplete checkpoint provenance'}
 $summary=Import-Csv $csv;$pairs=Import-Csv $seeds
 if($summary.Count -ne 16 -or $pairs.Count -ne 320 -or @($summary[0].PSObject.Properties).Count -ne 24){throw 'Invalid CSV shape'}
 if([double]$summary[0].difference -ne 36 -or $summary[0].scope -ne 'end_cumulative' -or $summary[0].resource -ne 'forklift'){throw 'Wrong measured checkpoint effect'}
 Invoke-Check @('--headless','--experiment',$example,'--export-json',$json) $false
 $collision=Join-Path $taskPath 'collision';Invoke-Check @('--headless','--experiment',$example,'--export-json',$collision,'--export-csv',$collision) $false
 if(Test-Path -LiteralPath $collision){throw 'Conflicting export created a file'}
 $request=Get-Content -LiteralPath $example -Raw -Encoding UTF8|ConvertFrom-Json
 $request.checkpoint_tick=288001;$bad=Join-Path $taskPath 'bad.json';$request|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $bad -Encoding UTF8
 $rejected=Join-Path $taskPath 'rejected.json';Invoke-Check @('--headless','--experiment',$bad,'--export-json',$rejected) $false
 if(Test-Path -LiteralPath $rejected){throw 'Invalid checkpoint published evidence'}
 $request.checkpoint_tick=72000;$request.intervention.action='route_forklift';$request|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $bad -Encoding UTF8;Invoke-Check @('--headless','--experiment',$bad) $false
 $raw=[IO.File]::ReadAllText($example).Replace('"checkpoint_tick":72000','"checkpoint_tick":72000,"checkpoint_tick":72001');[IO.File]::WriteAllText($bad,$raw,[Text.UTF8Encoding]::new($false));Invoke-Check @('--headless','--experiment',$bad) $false
} finally {
 $resolved=[IO.Path]::GetFullPath($taskPath)
 if(-not $resolved.StartsWith($boundary.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Cleanup escaped task boundary'}
 Remove-Item -LiteralPath $resolved -Recurse -Force
}
