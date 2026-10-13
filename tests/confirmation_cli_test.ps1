param([string]$Executable,[string]$SourceRoot)
$ErrorActionPreference='Stop'
$directory=Join-Path ([IO.Path]::GetTempPath()) ('simforge-confirmation-cli-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $directory | Out-Null
try {
 foreach($example in @('forklift','sensitivity','interaction','inconclusive')) {
  $inputFile=Join-Path $SourceRoot "examples/warehouse-confirmation-$example.json"
  $jsonFile=Join-Path $directory "$example.json"
  $csvFile=Join-Path $directory "$example.csv"
  $seedFile=Join-Path $directory "$example-seeds.csv"
  $output=& $Executable --headless --experiment $inputFile --export-json $jsonFile --export-csv $csvFile --export-seeds $seedFile 2>&1
  if($LASTEXITCODE -ne 0 -or ($output -join '') -notmatch 'Disjointness validated') {throw "Confirmation CLI failed: $output"}
  $report=Get-Content -LiteralPath $jsonFile -Raw | ConvertFrom-Json
  if($report.confirmation_report_version -ne 1 -or $report.paired.replications.Count -ne 30) {throw 'Missing independent paired results'}
  if((Get-Content -LiteralPath $seedFile -TotalCount 1) -notmatch '^study_id,source_study_id,selection_id,') {throw 'Missing stable CSV provenance'}
  if($example -eq 'inconclusive' -and (($output -join '') -notmatch 'interval includes zero')) {throw 'No-effect confirmation claimed improvement'}
 }
 $base=Get-Content -LiteralPath (Join-Path $SourceRoot 'examples/warehouse-confirmation-forklift.json') -Raw | ConvertFrom-Json
 $base.paired.replications.base_seed=1001
 $invalid=Join-Path $directory 'overlap.json';$base | ConvertTo-Json -Depth 50 | Set-Content -LiteralPath $invalid -Encoding ASCII
 $previous=$ErrorActionPreference;$ErrorActionPreference='Continue'
 $output=& $Executable --headless --experiment $invalid 2>&1
 $ErrorActionPreference=$previous
 if($LASTEXITCODE -eq 0 -or ($output -join '') -notmatch 'overlaps exploratory') {throw 'Headless overlap accepted'}
 # Export conflict must be rejected before execution and preserve existing evidence.
 $sentinel=Join-Path $directory 'sentinel.json';Set-Content -LiteralPath $sentinel -Value 'preserve' -Encoding ASCII
 $ErrorActionPreference='Continue'
 $output=& $Executable --headless --experiment (Join-Path $SourceRoot 'examples/warehouse-confirmation-forklift.json') --export-json $sentinel 2>&1
 $ErrorActionPreference=$previous
 if($LASTEXITCODE -eq 0 -or (Get-Content -LiteralPath $sentinel -Raw).Trim() -ne 'preserve') {throw 'Existing export was overwritten'}
} finally {
 $resolved=[IO.Path]::GetFullPath($directory)
 $boundary=[IO.Path]::GetFullPath([IO.Path]::GetTempPath())
 if(-not $resolved.StartsWith($boundary,[StringComparison]::OrdinalIgnoreCase) -or -not ([IO.Path]::GetFileName($resolved)).StartsWith('simforge-confirmation-cli-')) {throw 'Unsafe fixture cleanup'}
 Remove-Item -LiteralPath $resolved -Recurse -Force
}
