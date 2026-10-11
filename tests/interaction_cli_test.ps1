param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$SourceRoot)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $SourceRoot 'scripts/ArtifactPaths.psm1') -Force
$boundary=Get-ArtifactRoot
$directory=Join-Path $boundary ('interaction-cli/'+[guid]::NewGuid().ToString('N'))
Assert-ArtifactPath $directory $boundary
New-Item -ItemType Directory -Path $directory -Force|Out-Null
function Invoke-Batch([string[]]$Arguments,[int]$Expected) {
 $previous=$ErrorActionPreference
 try {$ErrorActionPreference='Continue'; & $Executable @Arguments *> (Join-Path $directory 'console.log');$code=$LASTEXITCODE}finally{$ErrorActionPreference=$previous}
 if($code -ne $Expected){Get-Content (Join-Path $directory 'console.log');throw "Expected exit $Expected, received $code"}
}
try {
 Copy-Item -LiteralPath (Join-Path $SourceRoot 'examples/warehouse-statistical-workload.json') -Destination (Join-Path $directory 'warehouse-statistical-workload.json')
 $request=Get-Content -LiteralPath (Join-Path $SourceRoot 'examples/warehouse-interaction-forklifts-workers.json') -Raw|ConvertFrom-Json
 $request.replications.count=3
 $spec=Join-Path $directory 'experiment.json'
 $request|ConvertTo-Json -Depth 20|Set-Content -LiteralPath $spec -Encoding ascii
 $json=Join-Path $directory 'report.json';$summary=Join-Path $directory 'grid.csv';$seeds=Join-Path $directory 'cells.csv';$interactions=Join-Path $directory 'interactions.csv';$contrasts=Join-Path $directory 'contrasts.csv'
 Invoke-Batch @('--headless','--experiment',$spec,'--export-json',$json,'--export-csv',$summary,'--export-seeds',$seeds,'--export-interactions',$interactions,'--export-interaction-seeds',$contrasts) 0
 $report=Get-Content -LiteralPath $json -Raw|ConvertFrom-Json
 if($report.interaction_report_version -ne 1 -or $report.cells.Count -ne 9 -or $report.replications.Count -ne 3 -or $report.interactions.Count -ne 4 -or $report.simulation_executions -ne 30){throw 'Wrong interaction dimensions or baseline reuse count'}
 $rows=@(Import-Csv -LiteralPath $summary);$seedRows=@(Import-Csv -LiteralPath $seeds);$effectRows=@(Import-Csv -LiteralPath $interactions);$contrastRows=@(Import-Csv -LiteralPath $contrasts)
 if($rows.Count -ne 45 -or $seedRows.Count -ne 135 -or $effectRows.Count -ne 20 -or $contrastRows.Count -ne 60){throw 'Wrong CSV shapes'}
 foreach($cell in $report.cells){$row=$rows|Where-Object {$_.cell_id -eq $cell.id -and $_.metric -eq 'completed'};if([math]::Abs([double]$row.mean_tested-$cell.means.completed) -gt 1e-9){throw 'Summary numerical mismatch'}}
 foreach($row in $contrastRows){if($row.metric -eq 'completed'){$expected=[double]$row.value11-[double]$row.value10-[double]$row.value01+[double]$row.value00;if([math]::Abs($expected-[double]$row.contrast) -gt 1e-9){throw 'Per-seed four-cell CSV contrast mismatch'}}}
 $before=Get-Content -LiteralPath $json -Raw
 Invoke-Batch @('--headless','--experiment',$spec,'--export-json',$json) 1
 if((Get-Content -LiteralPath $json -Raw) -ne $before){throw 'Existing report overwritten'}
 Invoke-Batch @('--headless','--experiment',$spec,'--export-json',(Join-Path $directory 'same.csv'),'--export-interactions',(Join-Path $directory 'same.csv')) 1
 $request.y_parameter=$request.x_parameter;$request|ConvertTo-Json -Depth 20|Set-Content -LiteralPath $spec -Encoding ascii
 Invoke-Batch @('--headless','--experiment',$spec) 2
 $request.y_parameter='workers';$request.x_values=@(1.5,2);$request|ConvertTo-Json -Depth 20|Set-Content -LiteralPath $spec -Encoding ascii
 Invoke-Batch @('--headless','--experiment',$spec) 2
 $request.x_values=@(1,2,3);$request.x_parameter='shipment_interval_seconds';$request|ConvertTo-Json -Depth 20|Set-Content -LiteralPath $spec -Encoding ascii
 Invoke-Batch @('--headless','--experiment',$spec) 2
 $request.mode='unsupported';$request|ConvertTo-Json -Depth 20|Set-Content -LiteralPath $spec -Encoding ascii
 Invoke-Batch @('--headless','--experiment',$spec) 3
 Write-Output 'Interaction CLI canonical JSON/CSV, four-cell raw values, limits and exclusive output checks passed'
}finally{Assert-ArtifactPath $directory $boundary;Remove-Item -LiteralPath $directory -Recurse -Force}
