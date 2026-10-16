[CmdletBinding(DefaultParameterSetName='Directory')]
param(
    [Parameter(Mandatory,ParameterSetName='Directory')][string]$PackageDirectory,
    [Parameter(Mandatory,ParameterSetName='Zip')][string]$ZipPath,
    [string]$Version='0.1.0',
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{40}$')][string]$SourceCommit
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression.FileSystem
if ($PSCmdlet.ParameterSetName -eq 'Zip') {
    $zip = (Resolve-Path -LiteralPath $ZipPath).Path
    $name = "SimForge-v$Version-windows-x64"
    $archive = [IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $seen = @{}
        foreach ($entry in $archive.Entries) {
            if ($entry.FullName -notlike "$name/*" -or $entry.FullName -match '(^|[/\\])\.\.([/\\]|$)|:|\\|^/' -or $seen.ContainsKey($entry.FullName)) { throw 'Unsafe or duplicate ZIP entry' }
            $seen[$entry.FullName] = $true
        }
    } finally { $archive.Dispose() }
    $extraction = Join-Path (Split-Path $zip) ('audit-extraction-'+[guid]::NewGuid().ToString('N'))
    [IO.Compression.ZipFile]::ExtractToDirectory($zip,$extraction)
    $PackageDirectory = Join-Path $extraction $name
    $checksum = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    $expected = [IO.File]::ReadAllText((Join-Path (Split-Path $zip) "SimForge-v$Version-SHA256.txt")).Trim()
    if ($expected -cne "$checksum  $name.zip") { throw 'ZIP checksum mismatch' }
}
$root = (Resolve-Path -LiteralPath $PackageDirectory).Path
$manifest = Get-Content -Raw -LiteralPath (Join-Path $root 'package-inventory.json') | ConvertFrom-Json
if ($manifest.version -ne $Version -or $manifest.sourceCommit -ne $SourceCommit -or $manifest.build.sourceDirty -ne $false -or $manifest.build.configuration -ne 'Release') { throw 'Package metadata mismatch' }
$required = @('simforge_sandbox.exe','simforge_headless.exe','SDL3.dll','LICENSE','NOTICE','THIRD_PARTY.md','THIRD_PARTY_NOTICES.txt','README.txt','build-info.json','docs/windows-release.md','docs/releases/v0.1.0.md','examples/warehouse-baseline.json','examples/warehouse-constrained.json','examples/stochastic-coverage/homogeneous.json','examples/checkpoint-interventions/forklift.json','assets/fonts/Roboto-LICENSE.txt','assets/fonts/README.md')
foreach ($package in @('sdl3','imgui','glm','nlohmann-json','vulkan-headers')) { $required += "licenses/$package.txt" }
$required += 'licenses/imgui-stb.txt'
$required += 'licenses/nlohmann-components.txt'
foreach ($file in $required) { if (-not (Test-Path -LiteralPath (Join-Path $root $file) -PathType Leaf)) { throw "Missing packaged file: $file" } }
$seen = @{}
foreach ($file in $manifest.files) {
    if ($file.path -match '(^|[/\\])\.\.([/\\]|$)|:|\\|^/' -or $seen.ContainsKey($file.path)) { throw 'Unsafe/duplicate inventory path' }
    $seen[$file.path]=$true
    $path=Join-Path $root $file.path
    if ((Get-Item -LiteralPath $path).Length -ne $file.bytes -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $file.sha256) { throw "Inventory mismatch: $($file.path)" }
}
$actual = @(Get-ChildItem -LiteralPath $root -File -Recurse)
if ($actual.Count -ne $manifest.files.Count+1) { throw 'Unexpected package files' }
foreach ($file in $actual) {
    $relative=$file.FullName.Substring($root.Length+1).Replace('\','/')
    if ($relative -ne 'package-inventory.json' -and -not $seen.ContainsKey($relative)) { throw 'Uninventoried file' }
    if ($relative -match '(?i)(^|/)(\.git|\.aws|\.ssh|\.env|CMakeFiles|node_modules|Testing)(/|$)|\.(pdb|obj|lib|ilk|key|pem|pfx|p12|cpp|hpp)$|CMakeCache|compile_commands|vulkan-1\.dll|ucrtbased\.dll|vcruntime\d+d\.dll|msvcp\d+d\.dll') { throw "Forbidden package file: $relative" }
    if ($file.Extension -in @('.json','.txt','.md') -and $file.Name -ne 'THIRD_PARTY_NOTICES.txt' -and $relative -notlike 'licenses/*') {
        $text=[IO.File]::ReadAllText($file.FullName)
        if ($file.Extension -eq '.json' -and $text -match '[A-Za-z]:[/\\]|D:/Projects/SimForge') { throw "Absolute configuration path: $relative" }
        if ($text -match '-----BEGIN (RSA |OPENSSH |EC )?PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}|AKIA[0-9A-Z]{16}') { throw "Possible credential in $relative" }
    }
}
if ($manifest.shaders.Count -ne 2 -or @($manifest.shaders | Where-Object { $_.deployment -ne 'embedded in simforge_sandbox.exe' -or $_.bytes -le 20 }).Count) { throw 'Missing embedded shader evidence' }
if ($manifest.assets.Count -ne 2 -or $manifest.assets[0].sha256 -ne '56a45233d29f11b4dfb86d248e921939d115778f87325e7ae8cc108383d6664d' -or $manifest.libraries.Count -ne 5) { throw 'Missing font/library evidence' }
$build = Get-Content -Raw -LiteralPath (Join-Path $root 'build-info.json') | ConvertFrom-Json
if ($build.version -ne $Version -or $build.sourceCommit -ne $SourceCommit -or $build.sourceDirty -ne $false -or $build.configuration -ne 'Release') { throw 'Build identity mismatch' }
foreach ($file in @('README.txt','docs/releases/v0.1.0.md','docs/windows-release.md')) {
    if (-not [IO.File]::ReadAllText((Join-Path $root $file)).Contains("SimForge v$Version")) { throw "Documentation version mismatch: $file" }
}
foreach ($exe in @('simforge_sandbox.exe','simforge_headless.exe')) {
    $identity=(& (Join-Path $root $exe) --version) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $identity -notlike "SimForge v$Version | source $SourceCommit | dirty false*") { throw 'Executable identity mismatch' }
}
Write-Output "Package audit passed: $($actual.Count) files, v$Version, $SourceCommit; $root"
