# Build footprint and artifact management

This maintenance change preserves simulation, rendering, Debug information,
warnings, validation and all previous tests. Measurements use file lengths in
**MiB (1,048,576 bytes)**, not filesystem allocated/compressed size.

## Initial audit

The repository measured **5,404.42 MiB** before implementation. Build output was
**5,393.09 MiB**: Debug **1,599.02**, Release **540.36**, headless **1,092.15**,
plus other build outputs. The previous estimate of 490 PPMs was out of date:
**755 direct `build/*.ppm` files occupied 2,156.87 MiB**.

The following categories partition the initial total. Dependency storage includes
its own objects, libraries, symbols and logs; those files are not counted again
in the other rows. Configuration totals above are an alternate view, not extra
storage to add to this table. Rounding can affect the last decimal.

| Category | MiB | Purpose / disposition |
|---|---:|---|
| Temporary build captures, PPM and PNG | 2,159.78 | Disposable after review; existing files require approval |
| Dependencies, including vcpkg intermediates | 1,377.27 | Installed packages required; retained staging/buildtrees reproducible |
| Project Debug symbols | 780.58 | Required for useful Debug stepping and stack traces |
| Incremental link state | 454.90 | Useful for app edit/relink; avoid for test/benchmark tools |
| Project compiler objects | 308.96 | Rebuildable; useful incremental compilation cache |
| Project static libraries | 184.57 | Required build inputs, reproducible |
| Project executables | 90.15 | Apps, tests and benchmarks |
| Copied runtime DLLs outside dependency roots | 24.98 | Required beside graphics executables |
| Other generated files | 9.71 | Font headers, CMake/Ninja metadata and reports |
| Git history | 5.30 | Preserve; no history rewrite justified |
| Documentation, including approved screenshots | 4.67 | Preserve curated assets |
| Source, tests, assets and project configuration | 1.36 | Preserve |
| Logs and text outputs outside dependency roots | 2.14 | Small, review-dependent diagnostics |
| Generated shaders | 0.03 | SPIR-V and embedded headers; negligible savings potential |

The largest twenty initial files are listed at the end of this guide. They are
mostly symbols and libraries, rather than source or graphics assets.

## Artifact structure

```text
build/
  windows-debug/            # Separate compiler/cache/package tree
  windows-release/
  windows-headless/
  test-artifacts/
    graphics/
      latest/<run-id>/      # Successful review runs; default limit 3
      failures/<run-id>/    # Failed tests/reviews; explicit cleanup only
    logs/<run-id>/          # Managed command logs; default limit 3 successes
    benchmarks/<run-id>/    # Managed benchmark output; same retention
    footprint/             # Read-only audit reports and task-owned measurements
    fixtures/              # Isolated cleanup/retention test fixtures
docs/artifacts/             # Curated PNGs; never an automatic cleanup target
```

Each managed run uses a new GUID directory and a `.simforge-run.json` ownership
manifest. A `.run.lock` lease prevents another process from reclaiming an active
run. Retention considers only completed successes; failures and running runs are
preserved. Cleanup validates ownership, category, identity, all path boundaries
and the complete tree. It rejects junctions/symlinks/reparse points. Deletion uses
literal leaf paths and nonrecursive directory removal. Manifests are removed last
so an interrupted cleanup can be retried.

The graphics scripts accept `-ArtifactRoot`; CTest exposes the CMake cache path
`SIMFORGE_ARTIFACT_ROOT`. Managed roots must remain under this repository's
`build/test-artifacts`, including subdirectories for separate CI jobs. This keeps
cleanup bounded; arbitrary external directories are rejected.

## Graphics captures and retention

Ordinary GPU tests use the existing in-memory Vulkan readbacks and image checks.
Successful test runs remove their own temporary log/manifest directory and write
no permanent captures. Failures retain the log and, when renderer readback is
still available, a `failure.png` produced during exception unwinding. Device/init
failures may have no framebuffer; logs remain available. Crashes or interrupted
scripts can leave a `Running` manifest, which is intentionally protected.

Visual review is explicit:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-graphics-test.ps1 `
  -Executable build/windows-release/apps/sandbox/simforge_sandbox.exe `
  -Scenario examples/warehouse-baseline.json -Live -Mode Review -KeepRuns 3
```

Each review run contains its PNGs, JSON/CSV verification reports and console log.
The existing scenario, camera, selection, depth, resize, minimize/restore,
workspace and Vulkan validation checks remain. Screenshots cannot collide across
runs. Documentation capture generation still includes the five requested views
at 1280×720, 1600×900 and 1920×1080. Copy selected verified PNGs to
`docs/artifacts/` before their review run ages out. Existing approved PNGs are
untouched. Direct-encoder examples verified in this task:

- [Simulation](artifacts/artifact-management-simulation.png)
- [Analysis](artifacts/artifact-management-analysis.png)

The explicit low-level `--graphics-capture NEW_FILE.png` and `.ppm` exports remain
available for compatibility and caller-owned destinations. Managed testing and
documented review commands never put temporary images directly in `build/`.
Avoid using the low-level export repeatedly with unbounded unique root filenames.

Image writing is centralized in `apps/sandbox/capture.cpp`. Windows Imaging
Component performs lossless PNG encoding, with no downloaded image library. The
encoder retains RGBA8 pixels (including alpha), checks dimensions/storage, and
uses exclusive file creation. Both primary and diagnostic sibling images use
the requested extension. PPM remains supported as an explicit raw export; it is
not retained alongside PNG automatically. Non-Windows builds retain PPM export
and direct in-memory graphics tests; PNG currently requires Windows.

## Logs and benchmarks

Use the generic managed runner for new command output rather than accumulating
root-level logs:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-artifact-command.ps1 `
  -Category Benchmarks -Executable build/windows-release/benchmarks/simforge_workspace_benchmark.exe
```

For native commands with arguments, call the script from PowerShell with an array:

```powershell
& ./scripts/run-artifact-command.ps1 -Category Logs -Executable cmake.exe `
  -Arguments @('--build', 'build/windows-debug')
```

Failures remain; successful command output keeps three recent runs by default.
CTest's own `Testing/Temporary` logs retain their existing CTest behavior. Manually
written audit logs/reports without ownership manifests are not automatically
eligible for deletion. Existing reports are never inferred disposable from their
extension or filename.

## Cleanup preview and explicit application

Dry-run is the default and lists every eligible path and its size:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/clean-artifacts.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/clean-artifacts.ps1 -Category Graphics -OlderThanDays 7
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/clean-artifacts.ps1 -Category LegacyCaptures
```

`All` includes owned managed runs, including failures, but deliberately excludes
legacy root captures. `LegacyCaptures` examines **only direct `build/*.ppm`**;
it does not recursively search/delete PPMs and never selects PNGs or reports.
Age filtering uses UTC creation time for managed runs and last-write time for
legacy files. Active leases and running manifests are skipped. Usage accounting
does not follow reparse points; every deletion candidate has stricter validation.

After reviewing the preview and explicitly approving deletion:

```powershell
# Only after approval; not run on existing artifacts during this task.
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/clean-artifacts.ps1 -Category LegacyCaptures -Apply
```

Optional `-ReportPath build/test-artifacts/logs/NEW_NAME.csv` writes the complete
candidate list to a new file. Reports cannot overwrite existing files or escape
artifact boundaries. `-PassThru -Quiet` provides structured counts, byte totals,
paths and before/after usage for tests or automation.

The legacy preview identifies **755 files / 2,156.87 MiB (2.11 GiB)** recoverable.
No destructive legacy cleanup was run. Existing build directories, raw captures,
source, user scenarios, reports, approved documentation, assets and Git history
remain preserved. Only fresh test-created fixtures and new managed successes were
reclaimed automatically. Build directories are not cleanup categories.

## Build storage changes

- Debug/headless presets no longer build benchmarks by default. Release keeps
  them. All tests remain enabled; opt into Debug benchmarks with
  `-DSIMFORGE_BUILD_BENCHMARKS=ON` when needed.
- MSVC Debug test/benchmark executables link with `/INCREMENTAL:NO`, retaining
  full `/DEBUG` information and existing compiler warning settings. This avoids
  tool `.ilk` databases; test relinks may take longer. App executables keep normal
  incremental linking for interactive development.
- Presets pass vcpkg `--clean-buildtrees-after-build` and
  `--clean-packages-after-build`. Installed libraries, headers and symbols remain;
  completed dependency staging/buildtrees are not kept indefinitely. Downloads
  and binary caches are not deleted. Rebuilding an uncached port may require
  source extraction again.
- Dependency installations remain isolated per configuration. No shared compiler
  output or incompatible Debug/Release directory is introduced. Existing obsolete
  benchmark files, `.ilk` files and historical vcpkg staging data are not manually
  deleted; configuration changes do not promise to reclaim them retroactively.
- Shader intermediates are tiny, and font/shader embedding remains reproducible.
  Required runtime DLLs are kept; no speculative removal of runtime dependencies.

Fresh before/after builds were made in task-owned isolated directories with the
same pinned vcpkg packages and MSVC x64 compiler. Before used the original defaults
(benchmarks on); after used the new preset choices and link/cleanup policies.
The dependency binary cache was warm. These totals include installed dependencies
once per configuration; global vcpkg caches/SDK storage are outside the repository.

| Fresh build | Before MiB | After MiB | Saved MiB |
|---|---:|---:|---:|
| Debug | 1,013.46 | 655.20 | 358.26 (35.4%) |
| Release | 336.81 | 228.14 | 108.66 (32.3%) |
| Renderer-disabled Debug | 786.24 | 501.36 | 284.88 (36.2%) |
| Three configurations | 2,136.50 | 1,384.70 | 751.81 |

Existing directories may remain larger because they contain historical files and
cold dependency buildtrees. The isolated measurement builds were reclaimed after
recording results; original configurations were never removed. Fresh rebuilds can
be made in a new binary directory using `cmake --preset windows-debug -B NEW_DIR`
and a separate `-DVCPKG_INSTALLED_DIR=NEW_DIR/vcpkg_installed`. Do not remove an
existing configuration without reviewing its contents and obtaining approval.

## Effectiveness and correctness

One complete review retained **45 PNGs / 5.80 MiB** versus approximately
**140.22 MiB** for equivalent RGB PPM frames: **95.9% less image storage**.
Release encoded the 45 images in **547.47 ms** and wrote them in **43.12 ms**.
These costs are outside normal in-memory testing/rendering and are not GPU timings.

Two standard cube graphics runs each left **zero persistent files**; their native
commands took 3,843.6 / 3,838.5 ms and cleanup took 72.3 / 71.8 ms. The full
Debug/Release graphics suites also reclaimed successful runs. Four real review
runs retained exactly three successes; the oldest review disappeared, while its
curated documentation copies survived. Tests also exercise synthetic retention
and leases without requiring a GPU.

Final validation: **244/244 Debug**, **244/244 Release**, **218/218 renderer-disabled**.
All three preset configurations built successfully. The corrected final suites
took 83.82 / 43.36 / 47.78 seconds respectively. Debug and Release each include six
serial opt-in GPU tests, with zero Vulkan validation errors/warnings. Native
WM_CLOSE succeeded for the owned launcher and live-viewer windows, exit 0 at
window DPI 144 (150%). Headless execution still produced baseline 96 arrivals /
63 completed / 32 waiting / 1 processing, and checkpoint extra-forklift 87 completed
/ 8 waiting. Snapshot/replay, comparison/export and prior tests remain green.

There are thirteen new ordinary tests (four image cases and nine cleanup cases),
plus one deliberate GPU failure-retention test. Ordinary tests cover
lossless PNG/PPM output, dimensions/errors, exclusive creation, cleanup dry runs,
literal filenames, legacy boundaries, age, active locks, manifest validation,
retention, junction rejection and interrupted cleanup retry. A dedicated opt-in
GPU fault test verifies real failure PNG/log retention. Graphical scenario
preflight fixtures now have unique per-invocation directories, preventing
concurrent tests from reusing or deleting each other's filenames.

The final read-only audit, after reclaiming only task-created measurement fixtures,
measured **5,120.18 MiB** for the repository. Existing Debug/Release/headless
directories measured 1,439.48 / 541.75 / 953.62 MiB respectively. This in-place result includes old
captures and old configurations and should not be confused with the fresh-build
table. The full dry-run path list is saved under
`build/test-artifacts/logs/legacy-captures-approved-scope-preview.csv` and `.txt`.
All 755 original PPMs were verified present with unchanged lengths and last-write
times at the audit JSON's one-millisecond precision.

## Troubleshooting and remaining limits

Run `scripts/measure-footprint.ps1` for a fresh read-only categorized audit, top
twenty files and per-configuration totals; `-Json NEW_REPORT.json` records it.
When a build is unexpectedly large, inspect PDB/ILK growth, stale Debug benchmarks
and `vcpkg_installed/vcpkg/{blds,pkgs}` separately from installed dependencies.
Repeated incremental application relinks can grow PDB/ILK state even without
source-code growth. Avoid conflating an aged directory with a fresh-build result.

Failures are intentionally not subject to automatic age/size eviction. Inspect
and explicitly clean them when their evidence is no longer useful. Interrupted
runner processes may leave protected running manifests. Direct caller-owned
exports and unmarked manual logs remain the caller's responsibility. Headless
configuration still builds the existing platform library/SDL dependency, although
headless executables do not initialize or link SDL/Vulkan; removing that unused
build dependency is a separate architectural change. Windows PNG uses system
codecs; non-Windows PNG encoding is not added in this task.

## Twenty largest initial files

| MiB | Repository-relative path |
|---:|---|
| 47.96 | `build/windows-debug/apps/sandbox/simforge_sandbox.pdb` |
| 35.48 | `build/windows-headless/engine/simforge_engine.lib` |
| 35.48 | `build/windows-debug/engine/simforge_engine.lib` |
| 34.58 | `build/windows-debug/apps/sandbox/simforge_sandbox.ilk` |
| 27.32 | `build/windows-debug/tests/simforge_workspace_test.pdb` |
| 27.08 | `build/windows-headless/tests/simforge_workspace_test.pdb` |
| 24.79 | `build/windows-debug/tests/simforge_analysis_test.pdb` |
| 23.96 | `build/windows-headless/tests/simforge_analysis_test.pdb` |
| 23.03 | `build/windows-headless/tests/simforge_visual_design_test.pdb` |
| 21.71 | `build/windows-headless/apps/sandbox/simforge_sandbox.pdb` |
| 21.70 | `build/windows-headless/apps/sandbox/simforge_headless.pdb` |
| 21.32 | `build/windows-debug/tests/simforge_scenario_input_test.pdb` |
| 21.28 | `build/windows-headless/tests/simforge_visual_design_test.ilk` |
| 20.84 | `build/windows-headless/tests/simforge_workspace_test.ilk` |
| 20.61 | `build/windows-headless/tests/simforge_analysis_test.ilk` |
| 20.61 | `build/windows-debug/tests/simforge_analysis_test.ilk` |
| 20.47 | `build/windows-debug/tests/simforge_warehouse_snapshot_test.pdb` |
| 19.88 | `build/windows-debug/apps/sandbox/simforge_headless.pdb` |
| 19.74 | `build/windows-headless/tests/simforge_live_warehouse_test.pdb` |
| 19.68 | `build/windows-debug/apps/sandbox/simforge_headless.ilk` |
