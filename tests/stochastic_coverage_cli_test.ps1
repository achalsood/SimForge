param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$SourceRoot)
$ErrorActionPreference='Stop'
$dir=Join-Path ([IO.Path]::GetTempPath()) ('simforge-coverage-cli-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $dir|Out-Null
try {
  $request=Join-Path $SourceRoot 'examples/stochastic-coverage/multi-shift.json'
  $source=Join-Path $SourceRoot 'examples/stochastic-coverage/homogeneous.json'
  $report=Join-Path $dir 'report.json';$csv=Join-Path $dir 'report.csv';$scenario=Join-Path $dir 'scenario.json'
  & $Executable --headless --calibrate-demand $request --calibration-source $source --export-scenario $scenario --calibration-start '2026-02-02T08:00:00Z' --calibration-seed 54321 --poisson-realization --exclude-unestimated-hours --export-json $report --export-csv $csv
  if($LASTEXITCODE -ne 0){throw 'Seeded coverage conversion CLI failed'}
  $j=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
  if($j.calibration_report_version -ne 2 -or $j.training.observed -ne 138 -or $j.training.exposure_seconds -ne 54000 -or $j.holdout.expected -ne 72){throw 'Coverage counts/exposures differ'}
  if($j.rates[0].available -ne $false -or $null -ne $j.rates[0].rate_per_second){throw 'Unavailable rate exported as a number'}
  $c=$j.scenario_conversions[0]
  if($c.generator_version -ne 2 -or $c.explicit_seed -ne 54321 -or $c.expected_count -ne 66 -or $c.realized_count -ne 72 -or $c.periodic_approximation_accepted){throw 'Stochastic provenance differs'}
  if(($c.realized_counts_64_bins|Measure-Object -Sum).Sum -ne 72){throw 'Realization histogram lost events'}
  $rows=Import-Csv -LiteralPath $csv
  if(($rows[0].PSObject.Properties|Measure-Object).Count -ne 29 -or ($rows|Where-Object record_type -eq coverage_window).Count -ne 72){throw 'Coverage CSV schema differs'}
  $segments=@($rows|Where-Object record_type -eq arrival_intensity_segment)
  if($segments.Count -ne 8 -or [Math]::Abs(($segments|Measure-Object -Property expected_count -Sum).Sum-66) -gt 1e-9){throw 'CSV intensity segments lost integrated intensity'}
  if(@($segments|Where-Object {$_.generator_version -ne '2' -or $_.model -ne 'piecewise_poisson' -or $_.daily_bin_seconds -ne '3600'}).Count){throw 'CSV segment provenance differs'}
  & $Executable --headless --scenario $scenario
  if($LASTEXITCODE -ne 0){throw 'Exported Poisson scenario cannot execute'}
  $old=(Get-FileHash -LiteralPath $scenario -Algorithm SHA256).Hash
  $ErrorActionPreference='Continue'
  & $Executable --headless --calibrate-demand $request --calibration-source $source --export-scenario $scenario --calibration-start '2026-02-02T08:00:00Z' --calibration-seed 54321 --poisson-realization --exclude-unestimated-hours *> (Join-Path $dir 'exclusive.log')
  $status=$LASTEXITCODE;$ErrorActionPreference='Stop'
  if($status -eq 0 -or (Get-FileHash -LiteralPath $scenario -Algorithm SHA256).Hash -ne $old){throw 'Exclusive export changed an existing scenario'}
  $ErrorActionPreference='Continue'
  & $Executable --headless --calibrate-demand $request --calibration-source $source --export-scenario (Join-Path $dir 'rejected.json') --calibration-start '2026-02-02T08:00:00Z' --calibration-seed 54321 --poisson-realization *> (Join-Path $dir 'missing-policy.log')
  $status=$LASTEXITCODE;$ErrorActionPreference='Stop'
  if($status -eq 0 -or (Test-Path -LiteralPath (Join-Path $dir 'rejected.json'))){throw 'Unestimated hours were silently converted'}
} finally {
  $resolved=[IO.Path]::GetFullPath($dir);$temp=[IO.Path]::GetFullPath([IO.Path]::GetTempPath())
  if(-not $resolved.StartsWith($temp,[StringComparison]::OrdinalIgnoreCase) -or -not ([IO.Path]::GetFileName($resolved)).StartsWith('simforge-coverage-cli-')){throw 'Unexpected cleanup target'}
  Remove-Item -LiteralPath $resolved -Recurse -Force
}
