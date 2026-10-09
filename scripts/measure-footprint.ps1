param([string]$Root=(Split-Path $PSScriptRoot -Parent),[string]$Json)
$ErrorActionPreference='Stop'
$rootPath=(Resolve-Path -LiteralPath $Root).Path.TrimEnd('\','/')
$files=@(Get-ChildItem -LiteralPath $rootPath -File -Recurse -Force)
$rows=@($files | ForEach-Object {
 $relative=$_.FullName.Substring($rootPath.Length+1)
 $category=if($relative.StartsWith('.git\')){'Git history'}
 elseif($relative -match '^build\\') {
  if($relative -match '\\vcpkg_installed\\'){'Dependencies'}
  elseif($_.Extension -in '.obj','.o'){'Compiler objects'}
  elseif($_.Extension -eq '.pdb'){'Debug symbols'}
  elseif($_.Extension -eq '.ilk'){'Incremental link state'}
  elseif($_.Extension -in '.ppm','.png'){'Graphics captures'}
  elseif($_.Extension -eq '.lib'){'Static libraries'}
  elseif($_.Extension -eq '.exe'){'Executables'}
  elseif($_.Extension -eq '.dll'){'Runtime libraries'}
  elseif($_.Extension -eq '.spv' -or $_.Name -match '^scene_.*\.hpp$'){'Shaders'}
  elseif($_.Extension -in '.log','.csv','.txt'){'Logs and text outputs'}
  else{'Other generated files'}
 } elseif($relative.StartsWith('docs\')){'Documentation'} else{'Source, tests and assets'}
 [pscustomobject]@{Path=$relative;Bytes=$_.Length;Category=$category;Top=$relative.Split('\')[0]}
})
$categories=@($rows|Group-Object Category|ForEach-Object{[pscustomobject]@{Category=$_.Name;Bytes=($_.Group|Measure-Object Bytes -Sum).Sum;Files=$_.Count}})
$top=@($rows|Group-Object Top|ForEach-Object{[pscustomobject]@{Directory=$_.Name;Bytes=($_.Group|Measure-Object Bytes -Sum).Sum;Files=$_.Count}})
$builds=@(foreach($name in @('windows-debug','windows-release','windows-headless')) {
 $part=@($rows|Where-Object{$_.Path.StartsWith("build\$name\")})
 [pscustomobject]@{Configuration=$name;Bytes=($part|Measure-Object Bytes -Sum).Sum;Files=$part.Count}
})
$result=[pscustomobject]@{Root=$rootPath;Utc=[DateTime]::UtcNow.ToString('o');Bytes=($rows|Measure-Object Bytes -Sum).Sum;Files=$rows.Count;Categories=$categories;Directories=$top;Builds=$builds;Largest=@($rows|Sort-Object Bytes -Descending|Select-Object -First 20)}
if($Json) {
 $stream=[IO.File]::Open([IO.Path]::GetFullPath($Json),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
 try { $writer=[IO.StreamWriter]::new($stream,[Text.UTF8Encoding]::new($false)); $writer.Write(($result|ConvertTo-Json -Depth 6)); $writer.Flush() } finally { $stream.Dispose() }
}
Write-Output ('Repository: {0:F2} MiB / {1} files' -f ($result.Bytes/1MB),$result.Files)
foreach($r in $categories|Sort-Object Bytes -Descending) { Write-Output ('{0}: {1:F2} MiB / {2} files' -f $r.Category,($r.Bytes/1MB),$r.Files) }
foreach($r in $builds) { Write-Output ('{0}: {1:F2} MiB' -f $r.Configuration,($r.Bytes/1MB)) }
Write-Output 'Largest 20 files (categories above partition the total; directory/build totals are alternate views):'
foreach($r in $result.Largest) { Write-Output ('{0:F2} MiB {1}' -f ($r.Bytes/1MB),$r.Path) }
