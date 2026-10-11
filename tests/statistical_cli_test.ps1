param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$SourceRoot)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $SourceRoot 'scripts/ArtifactPaths.psm1') -Force
$boundary=Get-ArtifactRoot
$directory=Join-Path $boundary ('statistical-cli/'+[guid]::NewGuid().ToString('N'))
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
 $request=Get-Content -LiteralPath (Join-Path $SourceRoot 'examples/warehouse-statistical-forklift.json') -Raw|ConvertFrom-Json
 $request.replications.count=3
 $spec=Join-Path $directory 'experiment.json'
 [IO.File]::WriteAllText($spec,($request|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
 $json=Join-Path $directory 'report.json';$summary=Join-Path $directory 'summary.csv';$raw=Join-Path $directory 'raw.csv'
 Invoke-Batch @('--headless','--experiment',$spec,'--export-json',$json,'--export-csv',$summary,'--export-seeds',$raw) 0
 $report=Get-Content -LiteralPath $json -Raw|ConvertFrom-Json
 if($report.replications.Count -ne 3 -or $report.method.confidence -ne 0.95 -or $report.request.candidates.Count -ne 1) {throw 'Statistical JSON lost paired provenance'}
 $rows=@(Import-Csv -LiteralPath $raw)
 if($rows.Count -ne 15) {throw 'Per-seed CSV lost metrics or seeds'}
 foreach($row in $rows) {
  if($row.experiment_identity -ne $report.experiment_identity) {throw 'CSV identity differs from JSON'}
  $replication=$report.replications|Where-Object {[string]$_.seed -eq $row.seed}
  $baseline=$replication.baseline.($row.metric);$candidate=$replication.candidates[0].($row.metric)
  # Windows PowerShell's JSON/CSV decimal conversion can differ by one ULP;
  # the C++ canonical replay tests compare the actual report values exactly.
  if([Math]::Abs([double]$row.baseline-[double]$baseline) -gt 1e-9 -or [Math]::Abs([double]$row.candidate_value-[double]$candidate) -gt 1e-9 -or [Math]::Abs([double]$row.paired_difference-([double]$candidate-[double]$baseline)) -gt 1e-9) {throw "CSV pair mismatch: metric=$($row.metric), baseline=$($row.baseline)/$baseline, candidate=$($row.candidate_value)/$candidate, delta=$($row.paired_difference)"}
 }
 $request.replications.count=0
 [IO.File]::WriteAllText($spec,($request|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
 Invoke-Batch @('--headless','--experiment',$spec) 2
 $request.replications.count=3;$request.mode='checkpoint_statistical'
 [IO.File]::WriteAllText($spec,($request|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
 Invoke-Batch @('--headless','--experiment',$spec) 3
 $request.mode='paired_statistical'
 [IO.File]::WriteAllText($spec,($request|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
 Invoke-Batch @('--headless','--experiment',$spec,'--export-csv',(Join-Path $directory 'missing/summary.csv')) 4
 $request.schema_version=1e300
 [IO.File]::WriteAllText($spec,($request|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
 Invoke-Batch @('--headless','--experiment',$spec) 1
 Write-Output 'Statistical CLI: exact raw CSV/JSON pairs, provenance, validation/unsupported/execution return codes verified'
} finally {
 # This unique test-owned path is checked before recursive native deletion.
 Assert-ArtifactPath $directory $boundary
 Remove-Item -LiteralPath $directory -Recurse -Force
}
