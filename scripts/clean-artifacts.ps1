param(
 [ValidateSet('All','Graphics','Failures','Logs','Benchmarks','LegacyCaptures')][string]$Category='All',
 [ValidateRange(0,36500)][int]$OlderThanDays,
 [string]$ArtifactRoot,[string]$ReportPath,[switch]$Apply,[switch]$PassThru,[switch]$Quiet
)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'ArtifactPaths.psm1') -Force
$root=Get-ArtifactRoot $ArtifactRoot
$build=[IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) 'build'))
Assert-ArtifactPath $build $build
$before=Get-ArtifactBytes $build $build
$cutoff=if($PSBoundParameters.ContainsKey('OlderThanDays')){[DateTime]::UtcNow.AddDays(-$OlderThanDays)}else{[DateTime]::MaxValue}
$rows=@(); $runs=@(); $skipped=0
if($Category -eq 'LegacyCaptures') {
 if(Test-Path -LiteralPath $build) {
  foreach($file in Get-ChildItem -LiteralPath $build -File -Filter '*.ppm') {
   Assert-ArtifactPath $file.FullName $build
   if($file.LastWriteTimeUtc -le $cutoff) { $rows+=[pscustomobject]@{Path=$file.FullName;Bytes=$file.Length;Category='LegacyCaptures'} }
  }
 }
} else {
 foreach($run in Get-ArtifactRuns $root $Category) {
  if($run.Manifest.status -eq 'Running' -or [DateTime]::Parse($run.Manifest.createdUtc).ToUniversalTime() -gt $cutoff) { $skipped++; continue }
  $files=@(Get-ArtifactFiles $run.Path $root); $lease=Open-ArtifactLease $run.Path
  if(-not $lease) { $skipped++; continue }; $lease.Dispose()
  $runs+=$run
  foreach($file in $files) { $rows+=[pscustomobject]@{Path=$file.FullName;Bytes=$file.Length;Category=$run.Manifest.category} }
 }
}
if($ReportPath) {
 $report=[IO.Path]::GetFullPath($ReportPath); Assert-ArtifactPath $report $root
 if(Test-Path -LiteralPath $report) { throw 'Cleanup report must be a new file' }
 $stream=[IO.File]::Open($report,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
 try { $writer=[IO.StreamWriter]::new($stream,[Text.UTF8Encoding]::new($true)); $writer.Write(($rows|ConvertTo-Csv -NoTypeInformation) -join [Environment]::NewLine); $writer.Flush() } finally { $stream.Dispose() }
}
$bytes=($rows|Measure-Object Bytes -Sum).Sum
if(-not $Quiet) {
 Write-Output ("{0}: {1} eligible files, {2:F2} MiB reclaimable; {3} active/recent runs skipped" -f $(if($Apply){'Apply'}else{'DRY RUN'}),$rows.Count,($bytes/1MB),$skipped)
 foreach($row in $rows) { Write-Output ("{0} bytes  {1}" -f $row.Bytes,$row.Path) }
}
if($Apply) {
 if($Category -eq 'LegacyCaptures') {
  foreach($row in $rows) {
   Assert-ArtifactPath $row.Path $build
   if((Split-Path $row.Path -Parent) -ne $build -or [IO.Path]::GetExtension($row.Path) -ne '.ppm') { throw 'Legacy cleanup requires a direct build/*.ppm file' }
   Remove-Item -LiteralPath $row.Path -Force
  }
 } else { foreach($run in $runs) { if(-not (Remove-ArtifactRun $run.Path $root)) { $skipped++ } } }
}
$after=if($Apply){Get-ArtifactBytes $build $build}else{$before}
if(-not $Quiet) { Write-Output ('Build usage before/after: {0:F2} / {1:F2} MiB; estimated after explicit cleanup: {2:F2} MiB' -f ($before/1MB),($after/1MB),(($before-$bytes)/1MB)) }
if($PassThru) { [pscustomobject]@{Files=$rows.Count;Bytes=$bytes;BeforeBytes=$before;AfterBytes=$after;Skipped=$skipped;Rows=$rows;Applied=[bool]$Apply} }
