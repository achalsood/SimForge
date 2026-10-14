param([string]$Executable,[string]$SourceRoot)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Path (Join-Path $SourceRoot 'build/test-artifacts/logs') -Force|Out-Null
$root=Join-Path $SourceRoot ('build/test-artifacts/fixtures/failure-'+[guid]::NewGuid().ToString('N'))
& powershell -NoProfile -ExecutionPolicy Bypass -File "$SourceRoot/scripts/run-graphics-test.ps1" -Executable $Executable -ArtifactRoot $root -ForceFailure > "$SourceRoot/build/test-artifacts/logs/failure-test.log" 2>&1
if($LASTEXITCODE -ne 1) { throw 'Expected deliberate failed graphics run' }
Import-Module "$SourceRoot/scripts/ArtifactPaths.psm1" -Force
$runs=@(Get-ArtifactRuns $root Failures)
if($runs.Count -ne 1 -or -not (Test-Path -LiteralPath (Join-Path $runs[0].Path 'failure.png'))) { throw 'Expected owned failure run with direct PNG evidence' }
$log=Get-Content -LiteralPath (Join-Path $runs[0].Path 'console.log') -Raw
if($log -notmatch 'Intentional graphics artifact-retention test failure' -or $log -match 'Validation Error|Validation Warning') { throw 'Failure log was incomplete or Vulkan validation was not clean' }
# Delete only the test-created owned run, never pre-existing project artifacts.
if(-not (Remove-ArtifactRun $runs[0].Path $root)) { throw 'Cannot reclaim isolated failure fixture' }
foreach($path in @("$root/graphics/failures","$root/graphics/latest","$root/graphics",$root)) {
 Assert-ArtifactPath $path (Join-Path $SourceRoot 'build/test-artifacts/fixtures')
 Remove-Item -LiteralPath $path -Force
}
Write-Output 'Deliberate failure retained PNG and complete log; isolated fixture reclaimed'
