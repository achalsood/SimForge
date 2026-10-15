param(
 [Parameter(Mandatory=$true)][string]$Executable,
 [string]$Scenario,[switch]$Live,[switch]$ProjectRoundTrip,[switch]$Language,[switch]$Workload,[switch]$Statistical,[switch]$Sensitivity,[switch]$Interaction,[switch]$StudyHistory,[switch]$Persistence,[switch]$Calibration,[switch]$StochasticCoverage,[switch]$DemandDiagnostics,[ValidateRange(120,10000)][int]$Frames=180,
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
$oldLanguage=$env:SIMFORGE_LANGUAGE_TEST_PATH; $oldProject=$env:SIMFORGE_PROJECT_TEST_PATH; $oldCapture=$env:SIMFORGE_FAILURE_CAPTURE; $oldFailure=$env:SIMFORGE_TEST_FAIL_AFTER_PRESENT; $oldMetrics=$env:SIMFORGE_CAPTURE_METRICS
$oldWorkload=$env:SIMFORGE_WORKLOAD_TEST_PATH
$oldStatistical=$env:SIMFORGE_STATISTICAL_TEST_PATH
$oldSensitivity=$env:SIMFORGE_SENSITIVITY_TEST_PATH
$oldInteraction=$env:SIMFORGE_INTERACTION_TEST_PATH
$oldStudy=$env:SIMFORGE_STUDY_TEST_PATH
$oldPersistence=$env:SIMFORGE_PERSISTENCE_TEST_PATH
$oldCalibration=$env:SIMFORGE_CALIBRATION_TEST_PATH
$oldStochasticCoverage=$env:SIMFORGE_STOCHASTIC_COVERAGE_TEST_PATH
$oldDemandDiagnostics=$env:SIMFORGE_DEMAND_DIAGNOSTICS_TEST_PATH
$exitCode=1; $start=[Diagnostics.Stopwatch]::StartNew()
try {
 if($Language) { $env:SIMFORGE_LANGUAGE_TEST_PATH=Join-Path $dir "language.simforge" }
 if($Workload) { $env:SIMFORGE_WORKLOAD_TEST_PATH=Join-Path $dir "workload.simforge" }
 if($Statistical) { $env:SIMFORGE_STATISTICAL_TEST_PATH=Join-Path $dir "statistical.simforge" }
 if($Sensitivity) { $env:SIMFORGE_SENSITIVITY_TEST_PATH=Join-Path $dir "sensitivity.simforge" }
 if($StudyHistory) { $env:SIMFORGE_STUDY_TEST_PATH=Join-Path $dir "study.simforge" }
 if($Persistence) { $env:SIMFORGE_PERSISTENCE_TEST_PATH=Join-Path $dir "persistence.simforge" }
 if($Calibration) { $env:SIMFORGE_CALIBRATION_TEST_PATH=Join-Path $dir "calibration.simforge" }
 if($StochasticCoverage) { $env:SIMFORGE_STOCHASTIC_COVERAGE_TEST_PATH=Join-Path $dir "stochastic-coverage.simforge" }
 if($DemandDiagnostics) { $env:SIMFORGE_DEMAND_DIAGNOSTICS_TEST_PATH=Join-Path $dir "demand-diagnostics.simforge" }
 if($Interaction) { $env:SIMFORGE_INTERACTION_TEST_PATH=Join-Path $dir "interaction.simforge" }
 if($ProjectRoundTrip) { $env:SIMFORGE_PROJECT_TEST_PATH=Join-Path $dir "project.simforge" }
 $env:SIMFORGE_FAILURE_CAPTURE=Join-Path $dir 'failure.png'
 if($Mode -eq 'Review') { $env:SIMFORGE_CAPTURE_METRICS='1' }
 if($ForceFailure) { $env:SIMFORGE_TEST_FAIL_AFTER_PRESENT='1' } else { Remove-Item Env:SIMFORGE_TEST_FAIL_AFTER_PRESENT -ErrorAction SilentlyContinue }
 # Windows PowerShell represents native stderr as error records. Collect all
 # diagnostics without treating the first stderr line as a terminated pipeline.
 $ErrorActionPreference='Continue'
 & $Executable @arguments *> (Join-Path $dir 'console.log')
 $exitCode=$LASTEXITCODE
 if(($ProjectRoundTrip -or $Language -or $Workload -or $Statistical -or $Sensitivity -or $Interaction -or $StudyHistory -or $Persistence -or $Calibration -or $StochasticCoverage -or $DemandDiagnostics) -and $exitCode -eq 0) {
   $projectFile=if($DemandDiagnostics){'demand-diagnostics.simforge'}elseif($StochasticCoverage){'stochastic-coverage.simforge'}elseif($Calibration){'calibration.simforge'}elseif($Persistence){'persistence.simforge'}elseif($StudyHistory){'study.simforge'}elseif($Interaction){'interaction.simforge'}elseif($Sensitivity){'sensitivity.simforge'}elseif($Statistical){'statistical.simforge'}elseif($Workload){'workload.simforge'}elseif($Language){'language.simforge'}else{'project.simforge'}
   $projectArgs=@('--project',(Join-Path $dir $projectFile),'--graphics-smoke',"$Frames")
   if($Mode -eq 'Review') { $projectArgs+=@('--graphics-capture',(Join-Path $dir 'reopened.png')) }
   & $Executable @projectArgs *>> (Join-Path $dir 'console.log')
   $exitCode=$LASTEXITCODE
 }
 $ErrorActionPreference='Stop'
} catch { $_|Out-String|Add-Content -LiteralPath (Join-Path $dir 'console.log') } finally {
 $env:SIMFORGE_LANGUAGE_TEST_PATH=$oldLanguage; $env:SIMFORGE_PROJECT_TEST_PATH=$oldProject; $env:SIMFORGE_FAILURE_CAPTURE=$oldCapture; $env:SIMFORGE_TEST_FAIL_AFTER_PRESENT=$oldFailure; $env:SIMFORGE_CAPTURE_METRICS=$oldMetrics
 $env:SIMFORGE_WORKLOAD_TEST_PATH=$oldWorkload
 $env:SIMFORGE_STATISTICAL_TEST_PATH=$oldStatistical
 $env:SIMFORGE_SENSITIVITY_TEST_PATH=$oldSensitivity
 $env:SIMFORGE_INTERACTION_TEST_PATH=$oldInteraction
 $env:SIMFORGE_STUDY_TEST_PATH=$oldStudy
 $env:SIMFORGE_PERSISTENCE_TEST_PATH=$oldPersistence
 $env:SIMFORGE_CALIBRATION_TEST_PATH=$oldCalibration
 $env:SIMFORGE_STOCHASTIC_COVERAGE_TEST_PATH=$oldStochasticCoverage
 $env:SIMFORGE_DEMAND_DIAGNOSTICS_TEST_PATH=$oldDemandDiagnostics
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
