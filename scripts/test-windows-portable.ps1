[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ZipPath,
    [Parameter(Mandatory)][string]$SourceCommit,
    [string]$BuildExecutable='build/windows-release/apps/sandbox/simforge_sandbox.exe',
    [string]$EvidenceDirectory='build/test-artifacts/release-v010-portable'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$zip=(Resolve-Path -LiteralPath $ZipPath).Path
$buildExe=(Resolve-Path -LiteralPath $BuildExecutable).Path
$evidence=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($EvidenceDirectory)
$allowedEvidence=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../build/test-artifacts'))
if (-not $evidence.StartsWith($allowedEvidence+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Evidence must be in a fresh child of build/test-artifacts for the existing artifact safety policy.' }
if (Test-Path -LiteralPath $evidence) { throw 'Evidence directory already exists; choose a new one.' }
New-Item -ItemType Directory -Path $evidence | Out-Null
$isolation=Join-Path ([IO.Path]::GetTempPath()) ('SimForge release with spaces '+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $isolation | Out-Null
Copy-Item -LiteralPath $zip -Destination $isolation
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::ExtractToDirectory((Join-Path $isolation (Split-Path $zip -Leaf)),$isolation)
$package=Join-Path $isolation 'SimForge-v0.1.0-windows-x64'
$exe=Join-Path $package 'simforge_sandbox.exe'
$before=@(Get-ChildItem -LiteralPath $package -File -Recurse | ForEach-Object { "$($_.FullName)|$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash)" })
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class SimForgeReleaseWindow {
  public delegate bool EnumProc(IntPtr hwnd, IntPtr parameter);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int count);
  [DllImport("user32.dll")] public static extern bool IsHungAppWindow(IntPtr hwnd);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr w, IntPtr l);
  public static IntPtr Find(uint processId) {
    IntPtr result=IntPtr.Zero;
    EnumWindows((hwnd,p) => {uint id; GetWindowThreadProcessId(hwnd,out id); var text=new StringBuilder(512); GetWindowText(hwnd,text,512);
      if(id==processId && text.ToString().StartsWith("SimForge v0.1.0 |")) {result=hwnd;return false;} return true;},IntPtr.Zero);
    return result;
  }
}
'@
$results=@()
$savedEnvironment=@{}
foreach ($variable in @('PATH','VULKAN_SDK','VCPKG_ROOT','VK_LAYER_PATH','VK_ADD_LAYER_PATH','VK_INSTANCE_LAYERS','VK_ICD_FILENAMES','VK_DRIVER_FILES')) {
    $savedEnvironment[$variable]=[Environment]::GetEnvironmentVariable($variable,'Process')
}
function Measure-Launch([string]$Application,[string]$WorkingDirectory,[string]$Label) {
    $stdout=Join-Path $evidence "$Label-stdout.log"; $stderr=Join-Path $evidence "$Label-stderr.log"
    $start=[Diagnostics.Stopwatch]::StartNew()
    $process=Start-Process -FilePath $Application -WorkingDirectory $WorkingDirectory -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    try {
        $window=[IntPtr]::Zero
        while ($start.Elapsed.TotalSeconds -lt 30 -and -not $process.HasExited) {
            $window=[SimForgeReleaseWindow]::Find([uint32]$process.Id)
            $rendererReady=(Test-Path -LiteralPath $stdout) -and ([IO.File]::ReadAllText($stdout) -match 'Pipeline creation:')
            if ($window -ne [IntPtr]::Zero -and -not [SimForgeReleaseWindow]::IsHungAppWindow($window) -and $rendererReady) { break }
            Start-Sleep -Milliseconds 20
        }
        $startup=$start.Elapsed.TotalMilliseconds
        if ($window -eq [IntPtr]::Zero -or $process.HasExited -or -not $rendererReady) { throw "No initialized responsive application window: $Label" }
        Start-Sleep -Milliseconds 1200
        $process.Refresh()
        $modules=@($process.Modules | ForEach-Object { $_.FileName })
        $applicationRoot=Split-Path $Application
        foreach ($module in $modules) {
            if (-not $module.StartsWith($applicationRoot+'\',[StringComparison]::OrdinalIgnoreCase) -and
                -not $module.StartsWith($env:WINDIR+'\',[StringComparison]::OrdinalIgnoreCase)) { throw "Unexpected module outside package/system: $module" }
        }
        if (-not ($modules | Where-Object { $_ -ieq (Join-Path $applicationRoot 'SDL3.dll') })) { throw 'SDL did not load from executable directory' }
        if (-not ($modules | Where-Object { $_ -ieq (Join-Path $env:WINDIR 'System32/vulkan-1.dll') })) { throw 'Vulkan did not load from system runtime' }
        $record=[ordered]@{label=$Label;rendererReadyAndResponsiveWindowMs=$startup;workingSetBytes=$process.WorkingSet64;peakWorkingSetBytes=$process.PeakWorkingSet64;modules=$modules;hung=[SimForgeReleaseWindow]::IsHungAppWindow($window)}
        [void][SimForgeReleaseWindow]::PostMessage($window,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
        if (-not $process.WaitForExit(15000)) { throw "Normal close did not exit: $Label" }
        $process.WaitForExit()
        $record['exitCode']=$process.ExitCode
        if ($process.ExitCode -ne 0) { throw "Abnormal exit: $Label" }
        return $record
    } finally { if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }; $process.Dispose() }
}
try {
    $env:PATH="$env:WINDIR\System32;$env:WINDIR"
    foreach ($variable in $savedEnvironment.Keys | Where-Object { $_ -ne 'PATH' }) { [Environment]::SetEnvironmentVariable($variable,$null,'Process') }
    $results+=Measure-Launch $exe $package 'package-own-directory'
    $results+=Measure-Launch $exe $isolation 'package-unrelated-directory'
    $results+=Measure-Launch $buildExe $isolation 'local-release-unrelated-directory'
    foreach ($scenario in @('warehouse-baseline.json','warehouse-constrained.json','stochastic-coverage/homogeneous.json')) {
        & (Join-Path $package 'simforge_headless.exe') --headless --scenario (Join-Path $package "examples/$scenario") *> (Join-Path $evidence (($scenario.Replace('/','-'))+'.log'))
        if ($LASTEXITCODE -ne 0) { throw "Packaged scenario failed: $scenario" }
    }
    & (Join-Path $package 'simforge_headless.exe') --headless --experiment (Join-Path $package 'examples/checkpoint-interventions/forklift.json') *> (Join-Path $evidence 'checkpoint-request.log')
    if ($LASTEXITCODE -ne 0) { throw 'Packaged checkpoint request failed' }
} finally {
    foreach ($variable in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($variable,$savedEnvironment[$variable],'Process') }
    $results | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $evidence 'normal-startup.json') -Encoding UTF8
}
# These explicit developer diagnostics require installed validation layers. They
# are separate from the sanitized normal startup checks and are not clean-VM evidence.
Push-Location $isolation
try {
    foreach ($case in @('launcher','baseline','constrained','poisson','checkpoint','local-release')) {
        $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'run-graphics-test.ps1'),'-Executable',$exe,'-Mode','Review','-ArtifactRoot',$evidence,'-KeepRuns','20')
        if ($case -ne 'launcher') {
            $scenario=switch ($case) {
                'constrained' { 'warehouse-constrained.json' }
                'poisson' { 'stochastic-coverage/homogeneous.json' }
                default { 'warehouse-baseline.json' }
            }
            $arguments+=@('-Scenario',(Join-Path $package "examples/$scenario"),'-Live')
            if ($case -eq 'checkpoint') { $arguments+='-Checkpoint' }
            elseif ($case -eq 'baseline') { $arguments+='-ProjectRoundTrip' }
            if ($case -eq 'local-release') { $arguments[$arguments.IndexOf($exe)]=$buildExe }
        }
        & powershell.exe @arguments *> (Join-Path $evidence "$case-graphics.log")
        if ($LASTEXITCODE -ne 0) { throw "Native package diagnostic failed: $case" }
    }
} finally { Pop-Location }
$after=@(Get-ChildItem -LiteralPath $package -File -Recurse | ForEach-Object { "$($_.FullName)|$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash)" })
if (Compare-Object $before $after) { throw 'Application resources changed during portable tests' }
$warnings=@(Get-ChildItem -LiteralPath $evidence -Filter console.log -Recurse | Select-String 'Vulkan validation (error|warning):')
if ($warnings.Count) { throw 'Vulkan validation diagnostics found' }
$identity=Get-Content -Raw -LiteralPath (Join-Path $package 'build-info.json') | ConvertFrom-Json
if ($identity.sourceCommit -ne $SourceCommit -or $identity.sourceDirty -ne $false) { throw 'Unexpected tested source identity' }
[ordered]@{sourceCommit=$SourceCommit;package=$package;evidence=$evidence;normalLaunches=$results;vulkanErrors=0;vulkanWarnings=0;resourcesUnchanged=$true;cleanMachine=$false;environment='Development host; ZIP-only copy to temporary directory with spaces; normal runs use Windows-only PATH and no SDK/vcpkg/driver override variables; validation walkthroughs use installed developer layers.'} |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $evidence 'portable-validation.json') -Encoding UTF8
Write-Output "Portable extraction/startup/scenario/playback/project/validation checks passed. Evidence: $evidence; extraction retained: $isolation"
