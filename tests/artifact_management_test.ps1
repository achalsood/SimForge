param([Parameter(Mandatory=$true)][string]$Case,[Parameter(Mandatory=$true)][string]$SourceRoot)
$ErrorActionPreference='Stop'
$fixture=Join-Path $SourceRoot ('build/test-artifacts/fixtures/'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path "$fixture/scripts" -Force|Out-Null
Copy-Item -LiteralPath "$SourceRoot/scripts/ArtifactPaths.psm1","$SourceRoot/scripts/clean-artifacts.ps1" -Destination "$fixture/scripts"
$owned=Join-Path $fixture 'owned-fixture.txt'; [IO.File]::WriteAllText($owned,'SimForge cleanup test fixture')
$root=Join-Path $fixture 'build/test-artifacts'; New-Item -ItemType Directory -Path $root -Force|Out-Null
Import-Module "$fixture/scripts/ArtifactPaths.psm1" -Force
function Check($value) { if(-not $value) { throw "Artifact test failed: $Case" } }
function Rejects([scriptblock]$action) { $rejected=$false; try { & $action } catch { $rejected=$true }; Check $rejected }
function Run([string]$status='Success',[int]$age=0,[string]$category='Graphics') {
 $id=[guid]::NewGuid().ToString('N'); $folder=if($status -eq 'Failure'){'failures'}else{'latest'}
 $relative=if($category -eq 'Graphics'){"graphics/$folder/$id"}else{$category.ToLowerInvariant()+"/$id"}
 $dir=Join-Path $root $relative; New-Item -ItemType Directory -Path $dir -Force|Out-Null
 @{owner='SimForge artifacts';schema=1;id=$id;category=$category;status=$status;mode='Review';createdUtc=[DateTime]::UtcNow.AddDays(-$age).ToString('o')}|ConvertTo-Json|Set-Content "$dir/.simforge-run.json"
 [IO.File]::WriteAllText("$dir/.run.lock",''); [IO.File]::WriteAllText("$dir/image[1].png",'fixture pixels'); [IO.File]::WriteAllText("$dir/console.log",'fixture log'); return $dir
}
function Clean([string]$category='All',[switch]$Apply,[int]$Age=-1) {
 $options=@{ArtifactRoot=$root;Category=$category;PassThru=$true;Quiet=$true;Apply=$Apply}
 if($Age -ge 0) { $options.OlderThanDays=$Age }
 & "$fixture/scripts/clean-artifacts.ps1" @options
}
try {
 if($Case -eq 'dry') {
  $dir=Run; $before=@(Get-ArtifactFiles $fixture $fixture).Count; $result=Clean
  Check ($result.Files -eq 4 -and $result.Bytes -gt 0 -and -not $result.Applied -and $result.BeforeBytes -eq $result.AfterBytes)
  Check ((@(Get-ArtifactFiles $fixture $fixture).Count) -eq $before -and (Test-Path -LiteralPath "$dir/image[1].png"))
 } elseif($Case -eq 'legacy') {
  New-Item -ItemType Directory -Path "$fixture/build/nested","$fixture/docs/artifacts" -Force|Out-Null
  foreach($p in @('build/old[1].ppm','build/nested/keep.ppm','build/report.json','docs/artifacts/approved.png')) { [IO.File]::WriteAllText((Join-Path $fixture $p),'preserve') }
  $preview=Clean LegacyCaptures; Check ($preview.Files -eq 1)
  $result=Clean LegacyCaptures -Apply; Check ($result.BeforeBytes-$result.AfterBytes -eq $preview.Bytes)
  Check (-not (Test-Path -LiteralPath "$fixture/build/old[1].ppm"))
  foreach($p in @('build/nested/keep.ppm','build/report.json','docs/artifacts/approved.png')) { Check (Test-Path -LiteralPath (Join-Path $fixture $p)) }
 } elseif($Case -eq 'apply') {
  $dir=Run; $failed=Run Failure; $logs=Run Success 0 Logs; $benchmark=Run Success 0 Benchmarks; $result=Clean Graphics -Apply
  Check ($result.Files -eq 4 -and -not (Test-Path -LiteralPath $dir) -and (Test-Path -LiteralPath $failed))
  [void](Clean Logs -Apply); Check (-not (Test-Path -LiteralPath $logs) -and (Test-Path -LiteralPath $benchmark))
  [void](Clean Benchmarks -Apply); Check (-not (Test-Path -LiteralPath $benchmark) -and (Test-Path -LiteralPath $failed))
 } elseif($Case -eq 'age') {
  $old=Run Success 10; $recent=Run Success; $result=Clean Graphics -Apply -Age 7
  Check (-not (Test-Path -LiteralPath $old) -and (Test-Path -LiteralPath $recent) -and $result.Files -eq 4)
 } elseif($Case -eq 'outside') {
  Rejects { Get-ArtifactRoot "$fixture/docs" }; Rejects { Get-ArtifactRoot "$root/../../../../" }
  $dir=Run; '{"owner":"wrong","schema":1}'|Set-Content "$dir/.simforge-run.json"
  Rejects { Clean Graphics -Apply }; Check (Test-Path -LiteralPath "$dir/image[1].png")
 } elseif($Case -eq 'active') {
  $dir=Run; $running=Run Running; $lease=[IO.File]::Open("$dir/.run.lock",'Open','ReadWrite','None')
  try { $result=Clean Graphics -Apply; Check ($result.Files -eq 0 -and $result.Skipped -eq 2 -and (Test-Path -LiteralPath $dir)) } finally { $lease.Dispose() }
  Check (Test-Path -LiteralPath $running)
 } elseif($Case -eq 'retention') {
  $dirs=@(1..5|ForEach-Object{Run Success $_}); $failed=Run Failure 20; $running=Run Running 20
  Limit-ArtifactRuns $root 3
  Check ((@(Get-ArtifactRuns $root Graphics|Where-Object{$_.Manifest.status -eq 'Success'}).Count) -eq 3)
  Check ((Test-Path -LiteralPath $failed) -and (Test-Path -LiteralPath $running))
 } elseif($Case -eq 'partial') {
  $dir=Run; $image=[IO.File]::Open("$dir/image[1].png",'Open','ReadWrite','None')
  try { Rejects { Clean Graphics -Apply }; Check (Test-Path -LiteralPath "$dir/.simforge-run.json") } finally { $image.Dispose() }
  [void](Clean Graphics -Apply); Check (-not (Test-Path -LiteralPath $dir))
 } elseif($Case -eq 'reparse') {
  $dir=Run; $outside=Join-Path $fixture 'outside'; New-Item -ItemType Directory -Path $outside|Out-Null; [IO.File]::WriteAllText("$outside/keep.txt",'preserve')
  $link=Join-Path $dir 'redirect'; New-Item -ItemType Junction -Path $link -Target $outside|Out-Null
  try { Rejects { Clean Graphics -Apply }; Check ((Get-Content "$outside/keep.txt" -Raw) -eq 'preserve') } finally { [IO.Directory]::Delete($link) }
 } else { throw 'Unknown cleanup case' }
 Write-Output "Artifact management $Case passed"
} finally {
 # Only the fresh fixture created by this process can be removed. A junction is
 # explicitly removed above; validate every descendant before recursive removal.
 if([IO.File]::ReadAllText($owned) -ne 'SimForge cleanup test fixture' -or -not $fixture.StartsWith((Join-Path $SourceRoot 'build/test-artifacts/fixtures')+'\')) { throw 'Fixture ownership check failed' }
 [void]@(Get-ArtifactFiles $fixture $fixture)
 Remove-Item -LiteralPath $fixture -Recurse -Force
}
