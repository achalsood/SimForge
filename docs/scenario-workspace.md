# Scenario editing and comparison workspace

Commit #17 adds a graphical experiment workflow over the existing scenario,
warehouse, snapshot and analysis APIs. It introduces no new simulation rules,
renderer, domain, AI, physics or transport model.

## Start and experiment

Launch `simforge_sandbox.exe` without arguments. The scene demo now includes an
**Open warehouse workspace** panel. Its validated local JSON path defaults to the
bundled warehouse baseline; examples are copied beside the graphical executable,
so this also works when launched outside the repository directory. A different
warehouse can be opened by entering its full path. Invalid files leave the demo
running and show the error. No native file-picker dependency was added.

The existing `--scenario FILE --live` route remains supported. Click the fourth
header icon to open the workspace. The earlier metrics/focus/inspector controls,
keyboard shortcuts, timeline, static warehouse and cube graphics smoke remain.

1. Open `warehouse-baseline.json`. Pause if you want to choose an exact checkpoint.
2. In **Edit**, change Name to `Add forklift` and Forklifts from 2 to 3. Scroll the
   editor to reach other fields/actions; tabs remain visible.
3. **Validate**, then **Add as candidate**. This stores a validated configuration
   in the current workspace; it does not run or change the baseline. **Files** can
   also **Save as new scenario** to an explicit new filename.
4. In **Candidates**, choose **Run independent scenarios from start** and
   **Maximize completed shipments**, then **Run comparison**.
5. **Results** shows baseline 63 completed/32 waiting and this initial candidate
   94 completed/0 waiting over eight simulated hours. Throughput changes from
   7.875 to 11.75 shipments/hour; completion change is +31 (+49.2%). The UI rounds
   derived values to one decimal; exports retain full precision.
6. Expand **Controlled resource probes** for measured add-one baseline evidence.
   These probes change one capacity at a settled checkpoint (tick zero for the
   fresh baseline), independently of any multiple-variable candidate edits.
7. **View this final result** reconstructs the candidate's actual final state and
   verifies its metrics against the report. Its scene has three forklifts and the
   appropriate operational counts, rather than baseline metrics over new geometry.
   **Return to preserved baseline** restores the original viewer, camera, selection
   and simulation position.
8. Enter a new full destination path and choose **Export JSON** or **Export CSV**.
   Success/failure and destination appear in the workspace. Existing files are
   never overwritten. Change the filename before exporting the second format.

For the checkpoint workflow, choose **Compare from shared checkpoint**, uncheck
**Use preserved baseline's current time**, and select 7200 integer seconds. The
unchanged baseline remains 63 completed/32 waiting; adding a forklift then gives
87 completed/8 waiting, throughput 10.875/hour, completion change +24 (+38.1%).
Mean started queue wait changes from 4650 to about 1990.9 seconds. Adding one worker
or bay produces no completion improvement for this objective/checkpoint. These
values come from actual simulation execution, not a predefined recommendation.

## Architecture and ownership

```text
scenario validation -> ScenarioWorkspace -> WarehouseSimulation / simforge_analysis
                            |                          |
                       isolated draft            immutable comparison report
                            |                          |
                    preserved baseline viewer / reconstructed result preview
                            |
                    ImGui application widgets -> RenderFrameData -> existing Vulkan renderer
```

`simforge_workspace` is a CPU-only library depending on `simforge_viewer`,
`simforge_analysis` and the existing JSON support. No SDL, Vulkan or ImGui dependency
is introduced into simulation, warehouse, analysis or workspace. Workspace owns
application definitions, raw draft text, validation, stable candidate IDs, reports
and baseline/alternative controllers. Warehouse alone owns its rules and clock;
renderer owns GPU assets. `apps/sandbox/workspace_panel.*` reads this state and
queues typed actions for application processing before the next frame.

Comparisons operate on owned inputs and leave the live baseline untouched. The
application discards the synchronous action's old wall duration and the following
frame's duration, so experiment execution is not counted as baseline playback.
Only the selected viewer advances; a preserved baseline is frozen while viewing
an alternative. Returning to it resumes ordinary pacing without catching up time
spent in another view. No global workspace state or background simulation thread
exists. Vulkan and ImGui remain on the SDL main thread.

## Drafts and candidates

Editable fields are name, bays/workers/forklifts, integer arrival/processing seconds,
duration seconds, tick rate and uint64 seed. Text drafts retain invalid negative,
fractional, empty or overflowing inputs rather than silently clamping them.
Validation reports field-specific integer/range errors and authoritative schema
errors for combined constraints. No validation or simulation rerun occurs just
because ImGui redraws. Configuration differences are cached on candidate changes.

The existing schema is authoritative: resources may be zero, combined capacity
is at most 100000; seconds 1..604800; rate 1..1000 Hz; at most 10M ticks and 100K
arrivals. Zero capacity blocks service under existing model semantics. Seed is
retained but this warehouse model has no random behavior. The graphical adapter
still supports at most 256 of each resource. Larger candidates can be analyzed
and saved; attempting a 3D run/preview fails explicitly and retains the current view.

Draft typing never changes the active runtime. **Run draft from start** prepares a
new independent runtime/layout/camera and switches only after successful preparation.
The loaded baseline is retained. Revert always restores the loaded baseline, not
the current candidate. Unsaved edits are marked; successful Save/Add/Update records
the accepted draft as its origin. Failed validation/file operations retain edits.
Opening another scenario requires confirmation when draft edits or candidates would
be discarded. Revert/Edit requires confirmation for a dirty draft. Cancelling keeps
the edit target and state. Opening successfully clears candidates/results.

Candidates have monotonically assigned workspace IDs, validated definitions, unique
names, optional notes and cached input differences. Add, rename, duplicate, edit,
update, remove and restore-to-baseline preserve the baseline. Rename retains the ID;
duplicate receives a new ID. `Baseline` is reserved. Names use the existing 1..256
UTF-8-byte contract; notes are bounded to 4096 bytes. Candidate count remains 1..50.
Candidate edits invalidate completed reports; raw unsaved draft typing does not alter
the sealed report's input definitions. Reports are recomputed only on explicit Run.
Changing UI objective/mode prepares the next request; a cached report continues to
show the actual objective/mode/checkpoint used to produce it.

## Two comparison contracts

**Initial scenarios:** Each validated definition executes independently from tick
zero. Arrival and processing intervals, capacities and seed metadata can differ.
Duration and tick rate must match, preserving the existing metric/rational-ranking
contracts and a common observation interval. Duration/rate edits can still be saved
or run independently; incompatible comparisons report an error rather than claiming
those inputs were applied. Names/changed parameters identify each alternative.
Multiple changes do not establish a single-resource causal explanation.

Fresh orchestration uses independent existing analysis executions, extracts their
authoritative baseline metrics, computes cross-scenario deltas with `compare_metrics`
and ranks with `rank_candidates`. These scenarios do not share a checkpoint. Each
execution includes controlled probes in its replayable report; the UI presents the
loaded baseline's probes. Fresh JSON stores those independent reports and a derived
CSV/ranking envelope; no existing shared-checkpoint schema is reinterpreted.

**Checkpoint:** Only bays/workers/forklifts may differ. Arrival/processing, duration,
tick rate and seed must remain compatible. A selected checkpoint is prepared through
the existing API; current-time mode clones the preserved baseline snapshot and
settles only the clone when necessary. `run_comparison` retains prior shipments,
reservations, event order, waits, utilization history and earlier capacity commands.
It creates independent branches and applies reductions before increases at the
defined tick. Reductions below active usage are rejected. It runs each branch to
the same horizon; it never substitutes a fresh restart for a checkpoint.

Supported objectives remain maximize completions, minimize average started queue
wait, and minimize final waiting count. Existing exact-rational average-wait ranking
and resource-addition/absolute-change/name tie breaks remain. N/A means a metric or
ratio is undefined; an unavailable wait objective makes the candidate ineligible.
The best candidate is explicitly **best among tested configurations**, not a global
or financial optimum. The metrics table freezes configuration identity and scrolls
horizontally; completions/waiting are prominent, while resources, arrivals,
throughput, mean wait and utilization remain available without shrinking typography.
Selected details show actual signed deltas and percentage changes, with explicit
Improved/Regressed/No change labels and corresponding colors. A best-tested candidate
that fails to improve baseline triggers a warning; unavailable objectives remain
inconclusive. Literal ##/### in user names are displayed as data, not widget IDs.

## Evidence and visualization

Controlled probes show tested resource/capacity, completion/queue/mean-wait deltas,
probe utilization, measured observation and likely/no-positive-effect/unavailable/
inconclusive classification. Classification depends on the selected objective and
tested checkpoint/horizon. High utilization alone is not treated as proof. Started
wait averages exclude shipments still waiting; lower means may hide unfinished work.
No costs, calibration or purchase/hiring justification are modeled.

Only one renderer/swapchain is active. Candidate switching prepares a new CPU scene
and reconstructed final runtime, validates matching metrics, clears stale selection
and fits updated camera bounds. It reuses the eight GPU meshes and compatible scene/
UI pipelines. Result previews are labelled, paused and have disabled playback/reset/
seek controls; they do not silently omit capacity interventions. Picking, inspector
and camera remain available. Close the workspace sidebar to expose the normal object
inspector; reopen it to return to comparison details. Returning to the preserved
baseline retains its camera/selection. Untouched cameras refit when workspace width
changes; manually adjusted cameras keep their pose. F explicitly reframes.

Normal launch first shows the existing static demo with a file-entry panel. Opening
the live workspace releases that demo's window/renderer before entering the existing
viewer path; candidate switches and later file opens do not reinitialize Vulkan.
Workspace tabs stay visible and each tab body scrolls. At compact/high-DPI sizes,
tab navigation can scroll horizontally and only a subset of content is visible.
At 640x480/200%, the workspace uses an expanded editor page below the header; the
header workspace toggle restores the scene/playback controls. The covered scene
does not receive mouse input while that page is open.
The existing logical/framebuffer mapping and UI capture prevent scene clicks/drags
through fields, panel scroll from zooming, and stale picking bounds after resizing.

## Export and reproducibility

File inputs are explicit paths; no filesystem browser, cloud integration or overwrite
dialog is introduced. Scenario saves and fresh report saves reuse the existing
exclusive output helper, which removes a newly created partial file on write failure.
Checkpoint exports call the existing `save_report`/`save_csv` directly and retain
analysis schema 1. Fresh JSON uses workspace schema 1 with mode/objective, independent
analysis reports containing their scenario identities, ranking, limitations and
derived comparison CSV. Each embedded report is verified with `replay_report`; the
workspace recomputes and compares the complete envelope. Timing/GUI state is excluded
from exports. Tampered metrics, missing/differing inputs and extra fields fail replay.

`ScenarioWorkspace::verify_export(json)` accepts both modes. Fresh CSV reuses the
analysis CSV columns/escaping/numeric conversion through a small shared serializer;
its aggregate deltas are checked against baseline. CSV is a human-readable summary,
not a standalone input timeline. Verification recreates it from the corresponding
replayed JSON; JSON preserves the full reproducible experiment. Reports survive failed
exports, including existing files and unwritable/missing directories. No snapshot or
recording persistence format changed. Fixed-step scheduling alone does not guarantee
bitwise simulation determinism across hardware/implementations.

## Validation and captures

One Debug cube graphics case hit its existing 40-second timeout once. Its isolated
rerun, three consecutive repeat runs and subsequent complete Debug suite passed
without relaxing timeout or validation checks; the stall was not reproduced.

Windows/MSVC x64 Debug and Release: 219/219 tests each, including five opt-in graphics
cases. Renderer-disabled Debug: 194/194 tests. Thirty-four CPU workspace cases cover
draft validation/lifecycle/file safety, candidate management/limits, independent and
checkpoint comparisons, compatibility/reductions, ordering/determinism/ranking,
undefined metrics/inconclusive evidence, caching, preview isolation, export/replay/
tampering and seeded conservation. Layout tests cover all five resolutions at
100/125/150/200% UI scale. All earlier tests remain, including snapshot/recording,
headless batch/JSON/CSV and static cube/depth/warehouse checks.

Extended live graphics smoke types draft edits and invalid values through SDL/ImGui,
clicks validation/candidate/comparison actions, verifies actual report-matched preview
metrics/camera/picking/inspector and preserved baseline, and captures both comparison
modes. It exercises compact workspace, existing multi-resolution/DPI playback,
resize/minimize/restore, shutdown/reinitialization and export JSON replay. NVIDIA RTX
5080 Laptop API 1.4.351, loader/SDK 1.4.363, BGRA8 sRGB/FIFO: zero validation and
synchronization warnings/errors, one scene/UI pipeline and one font texture. Actual
Windows scale is 150%; 100/125/200% are diagnostic UI overrides and CPU framebuffer
tests. Other GPUs/OSes/split queues remain unverified; tested Intel driver still lacks
required maintenance1 presentation fences. Input automation injects SDL events.
The cube smoke also verifies the launcher path/Open widget in actual Vulkan output
before its original depth/resize/minimize/lifecycle checks. Normal demo and live
viewer native WM_CLOSE requests exited with code 0. Hardware launcher clicking was
not verified because Windows refused the foreground change; desktop captures were
excluded from the evidence. The documented screenshots are renderer readbacks.

Visually inspected, actual Vulkan readbacks converted losslessly from PPM:

| Capture | Verified purpose |
| --- | --- |
| [Graphical launcher](artifacts/scenario-workspace-launcher.png) | Real path input/Open action over the preserved cube demo |
| [Editor](artifacts/scenario-workspace-editor.png) | Loaded initial values; baseline remains at its original final state |
| [Invalid draft](artifacts/scenario-workspace-invalid.png) | Negative forklift value and visible validation feedback |
| [Candidates](artifacts/scenario-workspace-candidates.png) | Explicit mode/objective and Run action |
| [Initial results](artifacts/scenario-workspace-results.png) | Baseline 63/32 versus initial candidate 94/0 completed/waiting |
| [Candidate preview](artifacts/scenario-workspace-candidate.png) | Actual candidate geometry, 94 completions and final-result label |
| [Candidate inspector](artifacts/scenario-workspace-inspector.png) | Real picking/inspection in the candidate scene |
| [Checkpoint results](artifacts/scenario-workspace-checkpoint.png) | Two-hour intervention comparison |
| [Probe evidence](artifacts/scenario-workspace-probes.png) | Forklift intervention produces measured +24 completions |
| [Compact workspace](artifacts/scenario-workspace-compact.png) | Scrollable workspace with essential playback/viewport retained |
| [640 at 200%](artifacts/scenario-workspace-dpi200.png) | Expanded editor page; header toggle restores playback |

## Performance

Release on the same machine. Median of three 10K-arrival trials, 60 Hz, 10000-second
horizon, baseline resources 4/8/2 and checkpoint at 5000 seconds. No file I/O/GPU.

| Candidates | Validate draft | Create candidate | Fresh comparison | Final rank construction | Checkpoint comparison | Fresh JSON / CSV construction | Result view switch |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 3 | 0.528 us | 2.233 us | 9.827 ms | 1.600 us | 4.829 ms | 1.100 ms / 7.800 us | 1.007 ms |
| 10 | 0.425 us | 1.480 us | 25.841 ms | 2.900 us | 9.019 ms | 2.806 ms / 15.800 us | 0.982 ms |
| 50 | 0.427 us | 1.498 us | 120.078 ms | 16.100 us | 28.081 ms | 12.701 ms / 62.900 us | 1.011 ms |

Creation includes explicit validation and cached differences. Fresh comparison includes
independent analysis executions and their controlled probes; final rank construction
is only the final ranking phase, not all report work. Checkpoint comparison includes
snapshot clone, preflight, branches and derived evidence. Exports are measured
separately. The earlier analysis benchmark measured batches at 4.021/6.855/22.692 ms
for 3/10/50 candidates before this milestone; these differing orchestrations are not
a like-for-like speedup comparison. The synchronous approach is retained after these
measurements. Near-limit workloads can block longer; there is no cancellation or
pretend asynchronous progress. Cached results are never rerun during UI rendering.

Baseline Release 600-frame graphics before/after: UI build 44.064/57.473 us,
dynamic frame update 0.633/0.838 us, record/submit/present 3.486/3.349 ms, total
CPU frame 3.863/3.769 ms. Result-tab construction averaged 36.174 us during the
after diagnostic. Before submitted 716 frames; after 820 due to extra workspace
probes. Both finish at 1280x720; scene draws remain 82, UI draws 7/9. One scene/UI
pipeline is retained; warm frames create no further font texture. These means include
diagnostic interaction and FIFO/fence waits, so they do not establish GPU speedups.
CPU picking baseline/large before was 0.462/9.943 us, after 0.469/10.029 us; paused
frame construction 0.174/4.085 us before, 0.174/4.054 us after.

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
cmake --preset windows-release
cmake --build --preset windows-release
ctest --test-dir build/windows-release --output-on-failure
cmake --preset windows-headless
cmake --build --preset windows-headless
ctest --preset windows-headless --output-on-failure
build/windows-release/benchmarks/simforge_workspace_benchmark.exe
# Opt-in graphical tests, supported GPU/display/validation required:
cmake --preset windows-debug -DSIMFORGE_GRAPHICS_TESTS=ON
build/windows-debug/apps/sandbox/simforge_sandbox.exe --scenario examples/warehouse-baseline.json --live --graphics-smoke 180
```

## Limits and next milestone

Added files: `engine/include/simforge/workspace/scenario_workspace.hpp`,
`engine/src/workspace/scenario_workspace.cpp`, sandbox `workspace_panel.hpp/.cpp`,
`tests/workspace_test.cpp`, `benchmarks/workspace_benchmark.cpp`, this guide and eleven
`docs/artifacts/scenario-workspace-*.png` captures. Modified: engine/sandbox/tests/
benchmarks CMake, dashboard/viewer/graphics application sources, viewer controller/
presentation headers/sources, analysis CSV declaration/serializer, README and the
viewer/Vulkan guides. Warehouse, clock, World/ECS, snapshot and recording implementations,
vcpkg dependencies, Vulkan source and shaders remain unchanged.

Only the warehouse scenario schema is editable. No arbitrary live intervention,
graphical recording playback, history browser, synchronized side-by-side viewport,
workspace persistence, cancellation/worker thread, native file picker, calibration,
financial optimization or AI integration is added. Result previews deliberately have
no seek/reset timeline. Session candidate notes/IDs are not a persisted workspace
document; save configurations individually and export the sealed report. Full keyboard/
screen-reader accessibility and actual multi-monitor DPI transitions need further work.

For the next milestone, prioritize measured long-run cancellation/background execution
and a versioned saved-workspace format, or command-aware recorded result playback.
Those need separate authorization; none is started here.
