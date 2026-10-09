param([Parameter(Mandatory=$true)][string]$Executable,[string[]]$Arguments=@(),
 [ValidateSet('Logs','Benchmarks')][string]$Category='Logs',
 [ValidateRange(1,100)][int]$KeepRuns=3,[string]$ArtifactRoot)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'ArtifactPaths.psm1') -Force
$root=Get-ArtifactRoot $ArtifactRoot
if(-not (Test-Path -LiteralPath $Executable -PathType Leaf)) { throw 'Executable does not exist' }
$id=[guid]::NewGuid().ToString('N'); $dir=Join-Path $root ($Category.ToLowerInvariant()+'/'+$id)
Assert-ArtifactPath $dir $root; New-Item -ItemType Directory -Path $dir|Out-Null
$lease=[IO.File]::Open((Join-Path $dir '.run.lock'),'CreateNew','ReadWrite','None')
$marker=Join-Path $dir '.simforge-run.json'
$manifest=[ordered]@{owner='SimForge artifacts';schema=1;id=$id;category=$Category;mode='Review';status='Running';createdUtc=[DateTime]::UtcNow.ToString('o')}
$manifest|ConvertTo-Json|Set-Content -LiteralPath $marker -Encoding UTF8
$code=1
try {
 $ErrorActionPreference='Continue'; & $Executable @Arguments *> (Join-Path $dir 'console.log'); $code=$LASTEXITCODE; $ErrorActionPreference='Stop'
} catch { $_|Out-String|Add-Content -LiteralPath (Join-Path $dir 'console.log') } finally {
 $manifest.status=if($code -eq 0){'Success'}else{'Failure'}
 $manifest['finishedUtc']=[DateTime]::UtcNow.ToString('o')
 $manifest|ConvertTo-Json|Set-Content -LiteralPath $marker -Encoding UTF8; $lease.Dispose()
}
Limit-ArtifactRuns $root $KeepRuns $Category
Get-Content -LiteralPath (Join-Path $dir 'console.log'); Write-Output "Diagnostics retained: $dir"
exit $code
