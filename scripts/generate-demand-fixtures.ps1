# Equivalent Windows fixture generator; see generate-demand-fixtures.py for truth.
$root=Join-Path $PSScriptRoot '../examples/demand-calibration'
New-Item -ItemType Directory -Path $root -Force|Out-Null
$start=[DateTimeOffset]::Parse('2026-01-05T00:00:00Z')
foreach($name in @('constant','morning-surge','sparse','distribution-shift')) {
    $rows=[Collections.Generic.List[string]]::new();$rows.Add('timestamp')
    for($day=0;$day -lt 3;$day++) {for($hour=0;$hour -lt 24;$hour++) {
        $count=switch($name) {
            'constant' {4}
            'morning-surge' {if($hour -ge 8 -and $hour -lt 12){30}else{6}}
            'sparse' {if($hour -eq 8){1}else{0}}
            'distribution-shift' {if($day -lt 2){6}else{18}}
        }
        for($i=0;$i -lt $count;$i++) {$rows.Add($start.AddDays($day).AddHours($hour).AddSeconds([Math]::Floor($i*3600/$count)).ToString("yyyy-MM-dd'T'HH:mm:ss'Z'"))}
    }}
    [IO.File]::WriteAllText((Join-Path $root "$name.csv"),($rows -join "`n")+"`n",[Text.Encoding]::ASCII)
    $request=[ordered]@{calibration_request_version=1;csv="$name.csv";timestamp_format='iso8601_utc_seconds';observation_start='2026-01-05T00:00:00Z';observation_end='2026-01-08T00:00:00Z';continuous=$true;split='2026-01-07T00:00:00Z';model=$(if($name -eq 'morning-surge'){'daily_profile'}else{'constant'});daily_bin_seconds=3600;diagnostic_bin_seconds=3600}
    [IO.File]::WriteAllText((Join-Path $root "$name.json"),($request|ConvertTo-Json)+"`n",[Text.Encoding]::ASCII)
}
[IO.File]::WriteAllText((Join-Path $root 'malformed.csv'),"timestamp`n2026-02-30T00:00:00Z`n",[Text.Encoding]::ASCII)
