param([string]$Destination=(Join-Path $PSScriptRoot '../examples/stochastic-coverage'))
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$target=[IO.Path]::GetFullPath($Destination)
if(-not $target.StartsWith($workspace+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Fixture destination must be inside this workspace'}
New-Item -ItemType Directory -Path $target -Force|Out-Null
$utf8=[Text.UTF8Encoding]::new($false)
$start=[DateTimeOffset]::Parse('2026-02-02T00:00:00Z')
function Stamp($time){$time.ToString("yyyy-MM-dd'T'HH:mm:ss'Z'")}
$windows=[Collections.Generic.List[object]]::new()
$rows=[Collections.Generic.List[string]]::new();$rows.Add('timestamp')
for($day=0;$day -lt 3;$day++){
  $date=$start.AddDays($day)
  for($hour=0;$hour -lt 24;$hour++){
    $status=if($hour -lt 8 -or $hour -eq 12){'closed'}elseif($hour -ge 17){'unobserved'}elseif($day -eq 1 -and $hour -eq 14){'interrupted'}else{'observed'}
    $a=$date.AddHours($hour);$b=$a.AddHours(1)
    # Retain adjacent windows separately: stable normalized row ordering is evidence.
    $windows.Add([ordered]@{start=(Stamp $a);end=(Stamp $b);status=$status})
    if($status -eq 'observed'){
      $count=if($hour -lt 12){12}else{6}
      for($i=0;$i -lt $count;$i++){$rows.Add((Stamp $a.AddSeconds([int](($i+0.5)*3600/$count))))}
    }
  }
}
[IO.File]::WriteAllText((Join-Path $target 'multi-shift.csv'),(($rows -join "`n")+"`n"),$utf8)
$request=[ordered]@{calibration_request_version=2;csv='multi-shift.csv';timestamp_format='iso8601_utc_seconds';observation_start=(Stamp $start);observation_end=(Stamp $start.AddDays(3));coverage_windows=$windows.ToArray();split=(Stamp $start.AddDays(2));model='daily_profile';daily_bin_seconds=3600;diagnostic_bin_seconds=3600}
[IO.File]::WriteAllText((Join-Path $target 'multi-shift.json'),(($request|ConvertTo-Json -Depth 8)+"`n"),$utf8)
$request.model='constant'
[IO.File]::WriteAllText((Join-Path $target 'multi-shift-constant.json'),(($request|ConvertTo-Json -Depth 8)+"`n"),$utf8)
$scenario=[ordered]@{schema_version=1;name='Seeded homogeneous Poisson arrivals';domain='warehouse';simulation=[ordered]@{tick_rate=60;duration_seconds=28800;random_seed=42};warehouse=[ordered]@{loading_bays=3;workers=10;forklifts=2;workload=[ordered]@{generator_version=2;arrivals=[ordered]@{mode='poisson';rate_per_second=0.016666666666666666};processing=[ordered]@{mode='uniform';minimum_seconds=600;maximum_seconds=1200}}}}
[IO.File]::WriteAllText((Join-Path $target 'homogeneous.json'),(($scenario|ConvertTo-Json -Depth 8)+"`n"),$utf8)
$scenario.name='Seeded piecewise Poisson arrivals'
$scenario.warehouse.workload.arrivals=[ordered]@{mode='piecewise_poisson';segments=@([ordered]@{start_seconds=0;end_seconds=7200;rate_per_second=0.008333333333333333},[ordered]@{start_seconds=7200;end_seconds=14400;rate_per_second=0},[ordered]@{start_seconds=18000;end_seconds=28800;rate_per_second=0.025})}
[IO.File]::WriteAllText((Join-Path $target 'piecewise.json'),(($scenario|ConvertTo-Json -Depth 8)+"`n"),$utf8)
Write-Output "Wrote explicit coverage and seeded workload examples to $target"
