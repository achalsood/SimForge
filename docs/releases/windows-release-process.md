# Manual Windows release procedure

GitHub Actions is prohibited. Run builds and tests locally, using the commands in the root README with graphics tests explicitly enabled. Never create workflows or automated publication jobs.

Use the existing dynamic `/MD`, `x64-windows` dependency configuration. Do not change runtime models without rebuilding dependencies. Inspect actual PE imports, including delay imports: SDL3 is bundled; the MSVC x64 v14 runtime and graphics-driver Vulkan loader are prerequisites; Windows system DLLs are not copied. Shaders (optimized Vulkan 1.1 SPIR-V), Roboto and procedural geometry are embedded, so no development path or shader tool is needed at runtime.

After implementation tests pass, commit locally, verify a clean tree, then **reconfigure and rebuild from that commit**. CMake records the configure-time Git identity; every source commit requires reconfiguration before packaging. Both executables expose `--version`. The packager compares their embedded version/commit/dirty status to the configured manifest and current HEAD, so an old executable cannot be packaged with fresh metadata.

From the repository root in an x64 Visual Studio developer PowerShell:

```powershell
cmake --preset windows-release -DSIMFORGE_GRAPHICS_TESTS=ON
cmake --build --preset windows-release
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package-windows-release.ps1 -Version 0.1.0 -OutputDirectory dist/v0.1.0
```

`dumpbin` must be available; `-Dumpbin` accepts its explicit path. The script supports the audited Release x64-windows configuration. It rejects uncommitted sources, metadata/version mismatches, unreviewed DLL imports, unexpected runtime flags, missing dependencies/notices/shader products, and existing output directories. A failed attempt leaves its own output for diagnosis: choose a fresh output directory for another attempt. No cleanup command removes unrelated files.

The package is an explicit allowlist, not a recursive build-directory copy. It includes the graphical and headless executables, SDL3, four example inputs, two technical guides, quick-start/release notes, licenses and metadata. Complete installed copyright texts for SDL3 (including component licenses), ImGui, GLM, JSON and Vulkan headers are copied and combined in THIRD_PARTY_NOTICES.txt. ImGui's embedded stb components and JSON's header-level contributors receive additional full MIT notices extracted from the installed headers; the packager rejects missing/unreviewed license evidence. The embedded Roboto license/provenance and root Apache LICENSE/NOTICE are preserved. The external Vulkan loader, Microsoft runtimes, SDK tools, tests and benchmark libraries are not distributed.

The script writes a stable root name, sorts archive entries and sets ZIP timestamps to the source commit's UTC timestamp. File inventory paths are relative; inventory excludes itself and the outer archive checksum covers all bytes. It audits staging, creates the ZIP with CreateNew semantics, extracts it into a fresh audit directory, rechecks every hash, runs both `--version` commands and verifies the checksum file. Audits/extractions are retained under ignored dist/. Run an audit again with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-windows-package.ps1 -ZipPath dist/v0.1.0/SimForge-v0.1.0-windows-x64.zip -SourceCommit YOUR_40_CHARACTER_COMMIT
```

Stable input files, entry order, timestamps and metadata make repeat packaging comparable. This is not a promise of bitwise-identical compiler output across independent toolchain builds.

Test a copy of only the ZIP outside the checkout, in a directory with spaces, with an unrelated working directory and a PATH containing only Windows system paths. Verify loaded SDL comes from the package and Vulkan/MSVC from system runtime installations. Run normal launcher, samples, playback, graphics validation, Save/fresh-process Open and clean exit. Compare normal startup time to first responsive window, process memory and viewer frame measurements against the same local Release build. An automated native desktop walkthrough is not a human usability test. A development machine with restricted PATH is not a clean machine. Record unavailable VM/GPU coverage explicitly and never claim it happened.

On the compatible development host with installed Khronos validation layers, the portable runner performs those checks, including three interleaved packaged/local normal startup comparisons. It retains captures, logs, projects and metrics under a fresh child of build/test-artifacts and leaves the temporary extraction for inspection:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-windows-portable.ps1 -ZipPath dist/v0.1.0/SimForge-v0.1.0-windows-x64.zip -SourceCommit YOUR_40_CHARACTER_COMMIT -EvidenceDirectory build/test-artifacts/release-v010-portable
```

The tracked validation document records the test evidence before the final preparation commit. Its own containing commit and archive checksum cannot be written into that commit without changing its identity. The generated receipt and release body provide final identity/checksum values, and the final artifact validation supplement alongside them records checks on the exact committed package. Review both.

**Stop for owner approval** after local commit/build/package/validation. Do not push the source commit, create/push the tag, upload binaries or create/publish the GitHub release before approval. The requested user gate is mandatory. No push or tag is part of either script.

After explicit approval, verify remote master ancestry and absence of an existing v0.1.0 tag/release, push without force, create/push an annotated tag at the validated source commit, verify the remote target, then manually create the release and upload the ZIP/checksum. Use the generated release body, updating its preparation-only wording only after publication. Verify downloadable assets and the downloaded checksum, tag target and Actions enabled:false. Report partial publication if any step fails. Do not silently replace existing tags/releases/assets.

Only after successful publication, add this proposed README section (the asset URL must first be verified):

```markdown
## Download

[SimForge v0.1.0 for Windows x64](https://github.com/achalsood/SimForge/releases/tag/v0.1.0)
is available as a portable ZIP. Extract it and launch simforge_sandbox.exe.
See [requirements and quick start](docs/windows-release.md),
[release notes](docs/releases/v0.1.0.md), and the build-from-source instructions below.
```
