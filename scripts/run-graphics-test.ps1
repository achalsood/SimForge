param(
 [Parameter(Mandatory=$true)][string]$Executable,
 [string]$Scenario,[switch]$Live,[switch]$ProjectRoundTrip,[ValidateRange(120,10000)][int]$Frames=180,
 [ValidateSet('Test','Review')][string]$Mode='Test',
 [ValidateRange(1,100)][int]$KeepRuns=3,[string]$ArtifactRoot,[switch]$ForceFailure
)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'ArtifactPaths.psm1') -Force
$root=Get-ArtifactRoot $ArtifactRoot
if(-not (Test-Path -LiteralPath $Executable -PathType Leaf)) { throw 'Graphics executable does not exist' }
if($Live -and -not $Scenario) { throw 'Live graphics requires a scenario' }
$id=[guid]::NewGuid().ToString('N'); $dir=Join-Path $root "graphics/latest/$id"
Assert-ArtifactPath $dir $root; New-Item -ItemType Directory -Path $dir|Out-Null
$lease=[IO.File]::Open((Join-Path $dir '.run.lock'),[IO.FileMode]::CreateNew,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
$marker=Join-Path $dir '.simforge-run.json'
$manifest=[ordered]@{owner='SimForge artifacts';schema=1;id=$id;category='Graphics';mode=$Mode;status='Running';createdUtc=[DateTime]::UtcNow.ToString('o')}
$manifest|ConvertTo-Json|Set-Content -LiteralPath $marker -Encoding UTF8
$arguments=@('--graphics-smoke',"$Frames")
if($Scenario) { $arguments+=@('--scenario',$Scenario) }; if($Live) { $arguments+='--live' }
if($Mode -eq 'Review') { $arguments+=@('--graphics-capture',(Join-Path $dir 'capture.png')) }
$oldProject=$env:SIMFORGE_PROJECT_TEST_PATH; $oldCapture=$env:SIMFORGE_FAILURE_CAPTURE; $oldFailure=$env:SIMFORGE_TEST_FAIL_AFTER_PRESENT; $oldMetrics=$env:SIMFORGE_CAPTURE_METRICS
$exitCode=1; $start=[Diagnostics.Stopwatch]::StartNew()
try {
 if($ProjectRoundTrip) { $env:SIMFORGE_PROJECT_TEST_PATH=Join-Path $dir "project.simforge" }
 $env:SIMFORGE_FAILURE_CAPTURE=Join-Path $dir 'failure.png'
 if($Mode -eq 'Review') { $env:SIMFORGE_CAPTURE_METRICS='1' }
 if($ForceFailure) { $env:SIMFORGE_TEST_FAIL_AFTER_PRESENT='1' } else { Remove-Item Env:SIMFORGE_TEST_FAIL_AFTER_PRESENT -ErrorAction SilentlyContinue }
 # Windows PowerShell represents native stderr as error records. Collect all
 # diagnostics without treating the first stderr line as a terminated pipeline.
 $ErrorActionPreference='Continue'
 & $Executable @arguments *> (Join-Path $dir 'console.log')
 $exitCode=$LASTEXITCODE
 if($ProjectRoundTrip -and $exitCode -eq 0) {
   $projectArgs=@('--project',(Join-Path $dir 'project.simforge'),'--graphics-smoke',"$Frames")
   if($Mode -eq 'Review') { $projectArgs+=@('--graphics-capture',(Join-Path $dir 'reopened.png')) }
   & $Executable @projectArgs *>> (Join-Path $dir 'console.log')
   $exitCode=$LASTEXITCODE
 }
 $ErrorActionPreference='Stop'
} catch { $_|Out-String|Add-Content -LiteralPath (Join-Path $dir 'console.log') } finally {
 $env:SIMFORGE_PROJECT_TEST_PATH=$oldProject; $env:SIMFORGE_FAILURE_CAPTURE=$oldCapture; $env:SIMFORGE_TEST_FAIL_AFTER_PRESENT=$oldFailure; $env:SIMFORGE_CAPTURE_METRICS=$oldMetrics
 $manifest.status=if($exitCode -eq 0){'Success'}else{'Failure'}
 $manifest['finishedUtc']=[DateTime]::UtcNow.ToString('o'); $manifest['elapsedMs']=$start.Elapsed.TotalMilliseconds
 $manifest|ConvertTo-Json|Set-Content -LiteralPath $marker -Encoding UTF8
 $lease.Dispose()
}
if($exitCode -ne 0) {
 $failureParent=Join-Path $root 'graphics/failures'; Assert-ArtifactPath $failureParent $root
 New-Item -ItemType Directory -Path $failureParent -Force|Out-Null
 $target=Join-Path $failureParent $id; Assert-ArtifactPath $target $root
 Move-Item -LiteralPath $dir -Destination $target
 Get-Content -LiteralPath (Join-Path $target 'console.log')
 Write-Output "Failed diagnostics retained: $target"
} elseif($Mode -eq 'Test') {
 $cleanup=[Diagnostics.Stopwatch]::StartNew()
 if(-not (Remove-ArtifactRun $dir $root)) { throw 'Successful test artifacts could not be reclaimed' }
 Write-Output ('Graphics passed; temporary run reclaimed (command {0:F1} ms, cleanup {1:F1} ms)' -f $manifest.elapsedMs,$cleanup.Elapsed.TotalMilliseconds)
} else {
 Limit-ArtifactRuns $root $KeepRuns
 Write-Output "Review PNGs and console log retained: $dir"
}
exit $exitCode
