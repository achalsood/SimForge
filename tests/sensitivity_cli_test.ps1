param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$SourceRoot)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $SourceRoot 'scripts/ArtifactPaths.psm1') -Force
$boundary=Get-ArtifactRoot
$directory=Join-Path $boundary ('sensitivity-cli/'+[guid]::NewGuid().ToString('N'))
Assert-ArtifactPath $directory $boundary
New-Item -ItemType Directory -Path $directory -Force|Out-Null
function Invoke-Batch([string[]]$Arguments,[int]$Expected) {
 $previous=$ErrorActionPreference
 try { $ErrorActionPreference='Continue'; & $Executable @Arguments *> (Join-Path $directory 'console.log'); $code=$LASTEXITCODE }
 finally { $ErrorActionPreference=$previous }
 if($code -ne $Expected) { Get-Content (Join-Path $directory 'console.log'); throw "Expected exit $Expected, received $code" }
}
try {
 Copy-Item -LiteralPath (Join-Path $SourceRoot 'examples/warehouse-statistical-workload.json') -Destination (Join-Path $directory 'warehouse-statistical-workload.json')
 $request=Get-Content -LiteralPath (Join-Path $SourceRoot 'examples/warehouse-sensitivity-stochastic.json') -Raw|ConvertFrom-Json
 $request.replications.count=3
 $spec=Join-Path $directory 'experiment.json'
 $request|ConvertTo-Json -Depth 20|Set-Content -LiteralPath $spec -Encoding ascii
 $json=Join-Path $directory 'report.json';$summary=Join-Path $directory 'summary.csv';$seeds=Join-Path $directory 'seeds.csv';$marginal=Join-Path $directory 'marginal.csv'
 Invoke-Batch @('--headless','--experiment',$spec,'--export-json',$json,'--export-csv',$summary,'--export-seeds',$seeds,'--export-marginal',$marginal) 0
 $report=Get-Content -LiteralPath $json -Raw|ConvertFrom-Json
 if($report.sensitivity_report_version -ne 1 -or $report.replications.Count -ne 3 -or $report.points.Count -ne 6 -or $report.simulation_executions -ne 24) {throw 'Wrong report dimensions/reuse count'}
 $rows=@(Import-Csv -LiteralPath $summary);$seedRows=@(Import-Csv -LiteralPath $seeds);$marginalRows=@(Import-Csv -LiteralPath $marginal)
 if($rows.Count -ne 30 -or $seedRows.Count -ne 90 -or $marginalRows.Count -ne 25) {throw 'Wrong CSV dimensions'}
 foreach($point in $report.points) {
   $row=$rows|Where-Object {$_.value -eq $point.value -and $_.metric -eq 'completed'}
   if([math]::Abs([double]$row.mean_tested-$point.baseline_effects.completed.candidate_mean) -gt 1e-9) {throw 'Summary export changed numerical value'}
 }
 $before=Get-Content -LiteralPath $json -Raw
 Invoke-Batch @('--headless','--experiment',$spec,'--export-json',$json) 1
 if((Get-Content -LiteralPath $json -Raw) -ne $before) {throw 'Existing report overwritten'}
 Invoke-Batch @('--headless','--experiment',$spec,'--export-json',(Join-Path $directory 'same.csv'),'--export-marginal',(Join-Path $directory 'same.csv')) 1
 $request.range.step=0;$request|ConvertTo-Json -Depth 20|Set-Content -LiteralPath $spec -Encoding ascii
 Invoke-Batch @('--headless','--experiment',$spec) 2
 $request.range.step=1;$request.parameter='shipment_interval_seconds';$request|ConvertTo-Json -Depth 20|Set-Content -LiteralPath $spec -Encoding ascii
 Invoke-Batch @('--headless','--experiment',$spec) 2
 $request.mode='unsupported';$request|ConvertTo-Json -Depth 20|Set-Content -LiteralPath $spec -Encoding ascii
 Invoke-Batch @('--headless','--experiment',$spec) 3
 Write-Output 'Sensitivity CLI canonical exports, bounds, compatibility and exclusive output checks passed'
} finally { Assert-ArtifactPath $directory $boundary;Remove-Item -LiteralPath $directory -Recurse -Force }
