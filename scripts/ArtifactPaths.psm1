$ErrorActionPreference='Stop'
function Get-ArtifactRoot([string]$Root) {
 $repo=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
 $known=Join-Path $repo 'build/test-artifacts'
 if(-not $Root) { $Root=$known }
 $full=[IO.Path]::GetFullPath($Root).TrimEnd('\','/')
 if($full -ne $known -and -not $full.StartsWith($known+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Artifact root must be inside this repository build/test-artifacts' }
 Assert-ArtifactPath $full (Join-Path $repo 'build')
 return $full
}
function Assert-ArtifactPath([string]$Path,[string]$Boundary) {
 $full=[IO.Path]::GetFullPath($Path).TrimEnd('\','/')
 $base=[IO.Path]::GetFullPath($Boundary).TrimEnd('\','/')
 if($full -ne $base -and -not $full.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)) { throw "Path outside artifact boundary: $full" }
 $cursor=$full
 while($cursor) {
  $item=Get-Item -LiteralPath $cursor -Force -ErrorAction SilentlyContinue
  if($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Reparse points are not eligible: $cursor" }
  if($cursor -eq $base) { break }
  $cursor=Split-Path $cursor -Parent
 }
 # Also guard the boundary's repository ancestors against redirected build roots.
 $cursor=$base
 while($cursor) {
  $item=Get-Item -LiteralPath $cursor -Force -ErrorAction SilentlyContinue
  if($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Redirected ancestor rejected: $cursor" }
  $parent=Split-Path $cursor -Parent; if($parent -eq $cursor) { break }; $cursor=$parent
 }
}
function Get-ArtifactFiles([string]$Path,[string]$Boundary) {
 Assert-ArtifactPath $Path $Boundary
 if(-not (Test-Path -LiteralPath $Path)) { return }
 foreach($item in Get-ChildItem -LiteralPath $Path -Force) {
  Assert-ArtifactPath $item.FullName $Boundary
  if($item.PSIsContainer) { Get-ArtifactFiles $item.FullName $Boundary } else { $item }
 }
}
function Get-ArtifactBytes([string]$Path,[string]$Boundary) {
 Assert-ArtifactPath $Path $Boundary
 if(-not (Test-Path -LiteralPath $Path)) { return 0 }
 $pending=[Collections.Generic.Stack[string]]::new(); $pending.Push($Path); [long]$bytes=0
 while($pending.Count) {
  foreach($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
   # Usage accounting never follows links. Strict ownership/path checks remain
   # mandatory for every file eligible for deletion.
   if($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
   if($item.PSIsContainer) { $pending.Push($item.FullName) } else { $bytes+=$item.Length }
  }
 }
 return $bytes
}
function Read-ArtifactRun([string]$Directory,[string]$Root) {
 Assert-ArtifactPath $Directory $Root
 $marker=Join-Path $Directory '.simforge-run.json'
 if(-not (Test-Path -LiteralPath $marker)) { return }
 Assert-ArtifactPath $marker $Root
 try { $m=Get-Content -LiteralPath $marker -Raw|ConvertFrom-Json } catch { throw "Invalid artifact manifest: $marker" }
 if($m.owner -ne 'SimForge artifacts' -or $m.schema -ne 1 -or $m.id -notmatch '^[0-9a-f]{32}$' -or (Split-Path $Directory -Leaf) -ne $m.id) { throw "Unrecognized artifact ownership: $Directory" }
 if($m.category -notin @('Graphics','Logs','Benchmarks') -or $m.status -notin @('Running','Success','Failure')) { throw "Invalid artifact state: $Directory" }
 [void][DateTime]::Parse($m.createdUtc,[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::RoundtripKind)
 [pscustomobject]@{Path=$Directory;Manifest=$m}
}
function Get-ArtifactRuns([string]$Root,[string]$Category='All') {
 $locations=@{Graphics='graphics/latest';Failures='graphics/failures';Logs='logs';Benchmarks='benchmarks'}
 foreach($key in $locations.Keys) {
  if($Category -ne 'All' -and $Category -ne $key) { continue }
  $parent=Join-Path $Root $locations[$key]; Assert-ArtifactPath $parent $Root
  if(-not (Test-Path -LiteralPath $parent)) { continue }
  foreach($dir in Get-ChildItem -LiteralPath $parent -Directory -Force) {
   $run=Read-ArtifactRun $dir.FullName $Root
   if($run) {
    $expected=if($key -eq 'Failures'){'Graphics'}else{$key}
    if($run.Manifest.category -ne $expected) { throw 'Artifact category does not match its directory' }
    if(($key -eq 'Failures' -and $run.Manifest.status -ne 'Failure') -or ($key -eq 'Graphics' -and $run.Manifest.status -eq 'Failure')) { throw 'Artifact status does not match its directory' }
    $run
   }
  }
 }
}
function Open-ArtifactLease([string]$Directory) {
 try { return [IO.File]::Open((Join-Path $Directory '.run.lock'),[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None) }
 catch [IO.IOException] { return $null }
}
function Remove-ArtifactRun([string]$Directory,[string]$Root) {
 $run=Read-ArtifactRun $Directory $Root
 if(-not $run -or $run.Manifest.status -eq 'Running') { return $false }
 # Validate the complete tree before deleting any file, then keep the lease while
 # deleting literal leaf paths. Never recursively delete a computed directory.
 $files=@(Get-ArtifactFiles $Directory $Root)
 $lease=Open-ArtifactLease $Directory; if(-not $lease) { return $false }
 try {
  $dirs=@(Get-ChildItem -LiteralPath $Directory -Directory -Recurse -Force|Sort-Object {$_.FullName.Length} -Descending)
  foreach($file in $files) {
   if($file.Name -eq '.run.lock' -and $file.DirectoryName -eq $Directory) { continue }
   if($file.Name -eq '.simforge-run.json' -and $file.DirectoryName -eq $Directory) { continue }
   Assert-ArtifactPath $file.FullName $Root; Remove-Item -LiteralPath $file.FullName -Force
  }
  foreach($dir in $dirs) { Assert-ArtifactPath $dir.FullName $Root; Remove-Item -LiteralPath $dir.FullName -Force }
  # Keep provenance until all payloads are removed, so an interrupted/failed
  # cleanup can be retried rather than stranding an unowned directory.
  Remove-Item -LiteralPath (Join-Path $Directory '.simforge-run.json') -Force
 } finally { $lease.Dispose() }
 Assert-ArtifactPath $Directory $Root
 Remove-Item -LiteralPath (Join-Path $Directory '.run.lock') -Force
 Remove-Item -LiteralPath $Directory -Force
 return $true
}
function Limit-ArtifactRuns([string]$Root,[int]$Keep=3,[string]$Category='Graphics') {
 if($Keep -lt 1) { throw 'Retention requires at least one review run' }
 $runs=@(Get-ArtifactRuns $Root $Category|Where-Object{$_.Manifest.status -eq 'Success' -and $_.Manifest.mode -eq 'Review'}|Sort-Object {$_.Manifest.createdUtc} -Descending)
 foreach($run in $runs|Select-Object -Skip $Keep) { [void](Remove-ArtifactRun $run.Path $Root) }
}
Export-ModuleMember -Function Get-ArtifactRoot,Assert-ArtifactPath,Get-ArtifactFiles,Get-ArtifactBytes,Read-ArtifactRun,Get-ArtifactRuns,Open-ArtifactLease,Remove-ArtifactRun,Limit-ArtifactRuns
