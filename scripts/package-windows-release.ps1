[CmdletBinding()]
param(
    [ValidatePattern('^0\.[0-9]+\.[0-9]+$')][string]$Version = '0.1.0',
    [string]$BuildDirectory = 'build/windows-release',
    [string]$OutputDirectory = 'dist/v0.1.0',
    [string]$Dumpbin = 'dumpbin.exe'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build = (Resolve-Path $BuildDirectory).Path
$destination = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
$name = "SimForge-v$Version-windows-x64"
# Never erase or reuse a directory, even after a failed attempt.
if (Test-Path -LiteralPath $destination) { throw 'Output directory exists. Choose a new empty output path; nothing was overwritten.' }
$head = (& git -C $repo rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot read Git HEAD' }
$changes = & git -C $repo status --porcelain --untracked-files=normal
if ($LASTEXITCODE -ne 0 -or $changes) { throw 'Release packaging requires a clean committed source tree.' }
$metadata = Get-Content -Raw -LiteralPath (Join-Path $build 'release-build.json') | ConvertFrom-Json
if ($metadata.version -ne $Version -or $metadata.sourceCommit -ne $head -or $metadata.sourceDirty -ne $false -or
    $metadata.configuration -ne 'Release' -or $metadata.renderer -ne $true) {
    throw 'Version, Release renderer configuration or clean source identity mismatch. Reconfigure and rebuild from HEAD.'
}
$cache = Get-Content -Raw -LiteralPath (Join-Path $build 'CMakeCache.txt')
if ($cache -notmatch '(?m)^CMAKE_BUILD_TYPE:STRING=Release\r?$' -or $cache -notmatch '(?m)^VCPKG_TARGET_TRIPLET:STRING=x64-windows\r?$') {
    throw 'Only the audited Release x64-windows dependency configuration is supported.'
}
$commands = Get-Content -Raw -LiteralPath (Join-Path $build 'compile_commands.json')
if ($commands -notmatch '(?:-|/)MD\s' -or $commands -match '(?:-|/)MDd\s|(?:-|/)MTd?\s' -or $commands -notmatch '/DNDEBUG') {
    throw 'Unexpected runtime model or debug compilation flags.'
}
$tool = (Get-Command $Dumpbin -ErrorAction Stop).Source
$binary = Join-Path $build 'apps/sandbox'
$installed = Join-Path $build 'vcpkg_installed/x64-windows'
function Read-Imports([string]$File) {
    $lines = & $tool /dependents $File
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect imports: $File" }
    @($lines | ForEach-Object { if ($_ -match '^\s+([A-Za-z0-9_.-]+\.dll)\s*$') { $matches[1] } } | Sort-Object -Unique)
}
$dependencies = @()
foreach ($file in @('simforge_sandbox.exe','simforge_headless.exe','SDL3.dll')) {
    $path = Join-Path $binary $file
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing runtime: $file" }
    $headers = (& $tool /headers $path) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $headers -notmatch '8664 machine') { throw "Not an x64 PE: $file" }
    $imports = @(Read-Imports $path)
    if (-not $imports.Count) { throw "No import evidence for $file" }
    foreach ($import in $imports) {
        $policy = if ($import -ieq 'SDL3.dll') { 'bundled' }
            elseif ($import -ieq 'vulkan-1.dll') { 'external: graphics driver Vulkan runtime' }
            elseif ($import -match '^(MSVCP140(_ATOMIC_WAIT)?|VCRUNTIME140(_1)?)\.dll$') { 'external: Microsoft x64 v14 Redistributable >=14.51.36231' }
            elseif ($import -match '^api-ms-win-crt-[a-z0-9-]+\.dll$|^(KERNEL32|USER32|SHELL32|COMDLG32|ole32|OLEAUT32|IMM32|GDI32|WINMM|VERSION|ADVAPI32|SETUPAPI)\.dll$') { 'Windows system component' }
            else { throw "Unreviewed runtime dependency $import in $file" }
        $dependencies += [ordered]@{binary=$file;module=$import;policy=$policy}
    }
}
foreach ($exe in @('simforge_sandbox.exe','simforge_headless.exe')) {
    $identity = (& (Join-Path $binary $exe) --version) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $identity -notlike "SimForge v$Version | source $head | dirty false*") { throw "Embedded identity mismatch: $exe" }
}
$shaderInventory = @()
foreach ($stage in @('vert','frag')) {
    $shader = Join-Path $build "engine/generated/scene.$stage.spv"
    $bytes = [IO.File]::ReadAllBytes($shader)
    if ($bytes.Length -lt 20 -or [BitConverter]::ToUInt32($bytes,0) -ne 0x07230203) { throw "Invalid compiled SPIR-V: $stage" }
    $shaderInventory += [ordered]@{name="scene.$stage.spv";deployment='embedded in simforge_sandbox.exe';bytes=$bytes.Length;sha256=(Get-FileHash $shader -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$font=Join-Path $repo 'assets/fonts/Roboto-Regular.ttf'
$fontHash=(Get-FileHash -LiteralPath $font -Algorithm SHA256).Hash.ToLowerInvariant()
if ($fontHash -ne '56a45233d29f11b4dfb86d248e921939d115778f87325e7ae8cc108383d6664d' -or
    -not (Test-Path -LiteralPath (Join-Path $build 'engine/generated/viewer_font.hpp'))) { throw 'Embedded font/provenance mismatch' }
$libraries=@()
foreach ($package in @('sdl3','imgui','glm','nlohmann-json','vulkan-headers')) {
    $spdx=Get-Content -Raw -LiteralPath (Join-Path $installed "share/$package/vcpkg.spdx.json") | ConvertFrom-Json
    $deployment=switch ($package) { 'sdl3' {'SDL3.dll'} 'imgui' {'statically linked'} default {'headers compiled into application'} }
    $libraries += [ordered]@{package=$package;identity=$spdx.name;deployment=$deployment;notice="licenses/$package.txt"}
}
$stbNotices="Dear ImGui's embedded stb components - MIT license alternative selected.`n"
foreach ($header in @('imstb_rectpack.h','imstb_textedit.h','imstb_truetype.h')) {
    $text=[IO.File]::ReadAllText((Join-Path $installed "include/$header"))
    $license=[regex]::Match($text,'(?s)ALTERNATIVE A - MIT License\r?\n(.*?)\r?\n-+\r?\nALTERNATIVE B')
    if (-not $license.Success) { throw "Missing embedded stb license: $header" }
    $stbNotices += "`n================ $header ================`n"+$license.Groups[1].Value+"`n"
}
$jsonCopyright=@(Get-ChildItem -LiteralPath (Join-Path $installed 'include/nlohmann') -Recurse -Filter '*.hpp' | ForEach-Object {
    $text=[IO.File]::ReadAllText($_.FullName)
    foreach ($license in [regex]::Matches($text,'(?m)^// SPDX-License-Identifier: (.+)')) {
        if ($license.Groups[1].Value.Trim() -ne 'MIT') { throw 'Unreviewed nlohmann component license' }
    }
    foreach ($credit in [regex]::Matches($text,'(?m)^// SPDX-FileCopyrightText: (.+)')) { 'SPDX-FileCopyrightText: '+$credit.Groups[1].Value.Trim() }
} | Sort-Object -Unique)
if ($jsonCopyright.Count -lt 5) { throw 'Missing nlohmann component attribution evidence' }
$jsonNotices="nlohmann/json component copyright attributions from installed headers (MIT):`n`n"+($jsonCopyright -join "`n")+"`n`n"+
    [IO.File]::ReadAllText((Join-Path $installed 'share/nlohmann-json/copyright'))
$files = [ordered]@{
    'simforge_sandbox.exe' = (Join-Path $binary 'simforge_sandbox.exe')
    'simforge_headless.exe' = (Join-Path $binary 'simforge_headless.exe')
    'SDL3.dll' = (Join-Path $binary 'SDL3.dll')
    'build-info.json' = (Join-Path $build 'release-build.json')
    'LICENSE' = (Join-Path $repo 'LICENSE')
    'NOTICE' = (Join-Path $repo 'NOTICE')
    'THIRD_PARTY.md' = (Join-Path $repo 'THIRD_PARTY.md')
    'README.txt' = (Join-Path $repo 'docs/releases/portable-README.txt')
    'assets/fonts/Roboto-LICENSE.txt' = (Join-Path $repo 'assets/fonts/Roboto-LICENSE.txt')
    'assets/fonts/README.md' = (Join-Path $repo 'assets/fonts/README.md')
}
foreach ($example in @('warehouse-baseline.json','warehouse-constrained.json','stochastic-coverage/homogeneous.json','checkpoint-interventions/forklift.json')) {
    $files["examples/$example"] = Join-Path $repo "examples/$example"
}
foreach ($doc in @('windows-release.md','releases/v0.1.0.md','checkpoint-intervention-studies.md','stochastic-arrivals-and-coverage.md')) {
    $files["docs/$doc"] = Join-Path $repo "docs/$doc"
}
foreach ($package in @('sdl3','imgui','glm','nlohmann-json','vulkan-headers')) {
    $files["licenses/$package.txt"] = Join-Path $installed "share/$package/copyright"
}
foreach ($source in $files.Values) { if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Required input missing: $source" } }
$stageRoot = Join-Path $destination $name
New-Item -ItemType Directory -Path $stageRoot | Out-Null
foreach ($relative in $files.Keys) {
    $target = Join-Path $stageRoot $relative
    New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
    Copy-Item -LiteralPath $files[$relative] -Destination $target
}
# Keep the compact offline guide intact; link optional, omitted documentation
# and figures to the exact source revision instead of shipping broken links.
foreach ($relative in @($files.Keys | Where-Object { $_ -like '*.md' })) {
    $target=Join-Path $stageRoot $relative
    $text=[IO.File]::ReadAllText($target)
    $sourceDirectory=Split-Path $files[$relative]
    $stagedDirectory=Split-Path $target
    $text=[regex]::Replace($text,'(!?\[[^\]]*\]\()([^\s)]+)(\))',{
        param($match)
        $link=$match.Groups[2].Value
        if ($link -match '^[a-zA-Z][a-zA-Z0-9+.-]*:|^#') { return $match.Value }
        $parts=$link.Split('#',2)
        if (Test-Path -LiteralPath (Join-Path $stagedDirectory $parts[0]) -PathType Leaf) { return $match.Value }
        $sourcePath=[IO.Path]::GetFullPath((Join-Path $sourceDirectory $parts[0]))
        if (-not $sourcePath.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $sourcePath)) { throw "Unresolved packaged documentation link: $relative -> $link" }
        $repoPath=$sourcePath.Substring($repo.Length+1).Replace('\','/')
        $suffix=if ($parts.Count -eq 2) { '#'+$parts[1] } else { '' }
        $kind=if (Test-Path -LiteralPath $sourcePath -PathType Container) { 'tree' } else { 'blob' }
        $url=if ($match.Groups[1].Value.StartsWith('!')) { "https://raw.githubusercontent.com/achalsood/SimForge/$head/$repoPath" } else { "https://github.com/achalsood/SimForge/$kind/$head/$repoPath" }
        return $match.Groups[1].Value+$url+$suffix+$match.Groups[3].Value
    })
    [IO.File]::WriteAllText($target,$text,[Text.UTF8Encoding]::new($false))
}
$notices = "SimForge v$Version third-party notices`n`nThe project LICENSE covers SimForge. Dependencies retain their own licenses.`nThe complete installed notices follow, including SDL component notices.`nRoboto is embedded; its complete license and provenance are in assets/fonts/.`nVulkan loader/driver and Microsoft runtimes are external prerequisites, not bundled.`n"
[IO.File]::WriteAllText((Join-Path $stageRoot 'licenses/imgui-stb.txt'),$stbNotices,[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $stageRoot 'licenses/nlohmann-components.txt'),$jsonNotices,[Text.UTF8Encoding]::new($false))
foreach ($package in @('sdl3','imgui','glm','nlohmann-json','vulkan-headers')) {
    $notices += "`n================ $package ================`n" + [IO.File]::ReadAllText((Join-Path $stageRoot "licenses/$package.txt")) + "`n"
}
$notices += "`n"+$stbNotices
$notices += "`n"+$jsonNotices
[IO.File]::WriteAllText((Join-Path $stageRoot 'THIRD_PARTY_NOTICES.txt'),$notices,[Text.UTF8Encoding]::new($false))
$inventory = @(Get-ChildItem -LiteralPath $stageRoot -File -Recurse | Sort-Object FullName | ForEach-Object {
    [ordered]@{path=$_.FullName.Substring($stageRoot.Length+1).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
$manifest = [ordered]@{schema=1;version=$Version;sourceCommit=$head;build=$metadata;dependencies=$dependencies;libraries=$libraries;shaders=$shaderInventory;assets=@([ordered]@{name='Roboto-Regular.ttf';deployment='embedded in simforge_sandbox.exe';sha256=$fontHash},[ordered]@{name='warehouse geometry';deployment='procedural built-in meshes'});files=$inventory;inventoryNote='package-inventory.json excludes itself; the external ZIP checksum covers the complete archive.'}
$manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $stageRoot 'package-inventory.json') -Encoding UTF8
& (Join-Path $PSScriptRoot 'test-windows-package.ps1') -PackageDirectory $stageRoot -Version $Version -SourceCommit $head
if ($LASTEXITCODE -ne 0) { throw 'Staging audit failed' }
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = Join-Path $destination "$name.zip"
$commitDate = (& git -C $repo show -s --format=%cI HEAD).Trim()
$timestamp = [DateTimeOffset]::Parse($commitDate).ToUniversalTime()
if ($timestamp.Year -lt 1980 -or $timestamp.Year -gt 2107) { throw 'Commit timestamp cannot be represented in ZIP' }
$stream = [IO.File]::Open($zip,[IO.FileMode]::CreateNew)
try {
    $archive = [IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create,$true)
    try {
        foreach ($file in (Get-ChildItem -LiteralPath $stageRoot -Recurse -File | Sort-Object FullName)) {
            $relative = $file.FullName.Substring($stageRoot.Length+1).Replace('\','/')
            $entry = $archive.CreateEntry("$name/$relative",[IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = $timestamp
            $inputStream = [IO.File]::OpenRead($file.FullName); $outputStream = $entry.Open()
            try { $inputStream.CopyTo($outputStream) } finally { $inputStream.Dispose(); $outputStream.Dispose() }
        }
    } finally { $archive.Dispose() }
} finally { $stream.Dispose() }
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $destination "SimForge-v$Version-SHA256.txt"),"$hash  $name.zip`n",[Text.UTF8Encoding]::new($false))
& (Join-Path $PSScriptRoot 'test-windows-package.ps1') -ZipPath $zip -Version $Version -SourceCommit $head
if ($LASTEXITCODE -ne 0) { throw 'Archive audit failed' }
$receipt = [ordered]@{version=$Version;sourceCommit=$head;zip="$name.zip";bytes=(Get-Item -LiteralPath $zip).Length;sha256=$hash;files=$inventory;publication='not approved; no tag, push, upload or release performed'}
$receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $destination 'release-receipt.json') -Encoding UTF8
$notes = [IO.File]::ReadAllText((Join-Path $repo 'docs/releases/v0.1.0.md'))
$notes += "`n### Artifact identity`n`nSource: https://github.com/achalsood/SimForge/commit/$head`n`nZIP SHA-256: ``$hash```n`nSize: $($receipt.bytes) bytes.`n"
[IO.File]::WriteAllText((Join-Path $destination 'github-release-body.md'),$notes,[Text.UTF8Encoding]::new($false))
Write-Output "ZIP: $zip ($($receipt.bytes) bytes)`nSHA-256: $hash`nInventory and release body: $destination"
