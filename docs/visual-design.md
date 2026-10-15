# SimForge visual design

> Publication curation: additional captures cited in this engineering record remain in Commit #33. See [screenshot history and recovery](screenshot-gallery.md#historical-captures).

Commit #18 gives the existing simulation and analysis workflow a distinct visual
identity. The reference dashboard informed the level of polish and scene presence;
SimForge uses its own composition, industrial geometry and interaction system.
No reference branding, assets or fabricated operational data are used.

## Design system

The interface uses quiet navy surfaces, cyan navigation, strong numeric hierarchy
and restrained rounded controls. Color carries meaning; text also states the
direction of a change so results do not depend on color perception alone.

| Role | Display color |
|---|---|
| Application shell | `#0B1220` |
| Panels | `#121C2B` |
| Elevated surfaces | `#1B293B` |
| Borders | `#2B3B50` |
| Primary / secondary text | `#F1F5F9` / `#94A3B8` |
| Navigation / processing / busy | `#38BDF8` |
| Completed / improvement | `#34D399` |
| Waiting | `#FBBF24` |
| Errors / regression | `#FB7185` |

`visualization/design_palette.hpp` stores the linear RGB equivalents. Both ImGui
and the scene draw into the sRGB attachment, which encodes once. Passing display
hex values directly would produce washed-out UI colors. The existing UNORM
fallback retains explicit scene output encoding; GUI color accuracy on a UNORM
surface has not been validated in this milestone.

The embedded, licensed Roboto font remains the only font dependency. Logical
body/label/heading/KPI sizes are 14/12/18/28 pixels. UI density follows actual
display scaling from 100% to 200%; framebuffer scaling is handled independently.
Spacing, padding, corners and the minimum 36-pixel control height live in the
CPU presentation header. Narrow windows use shorter navigation labels and
stacked forms with scrolling; controls remain reachable at 640×480 / 200%.

## Composition and modes

The persistent header identifies the scenario, simulation state and simulated
time. Its second row switches **Simulation / Scenario Editor / Analysis** and
offers the current context action when space permits. Switching pages preserves
the draft, candidate identities, result selection, cached report and warehouse.
It does not restart or implicitly pause a running simulation.

**Simulation** gives the 3D warehouse the remaining viewport, with operational KPI
cards, capacity/utilization bars, optional inspector and playback timeline. Scene
labels expose real loading usage and queue count. The view distinguishes the live
baseline, a draft run and a read-only final result. Camera orbit/pan/zoom, F to
frame, R to reset the camera, picking and plain-scenario playback remain available.
The same CPU camera handles actual drawable bounds and preserves manual poses.
An alternative view exposes **Return baseline** in the header, restoring the
preserved warehouse directly without navigating back through report details.

**Scenario Editor** occupies the full content area. Inputs are grouped into
scenario details, warehouse resources, operations and simulation. Units sit
beside labels; concise descriptions appear on hover. Wider pages put experiment
setup and actions beside the fields. Small pages stack them. Validation is tied
to individual fields, while dirty state remains visible. Candidates have readable
resource summaries and changed inputs; expanding a row exposes rename, duplicate,
edit, remove and restore operations. Existing discard confirmations remain.

**Analysis** also occupies the full content area. The best eligible tested
candidate, objective, baseline and four measured deltas lead the page. Controlled
add-one evidence appears near that summary. A seven-column primary table shows
configuration, resources, completed, throughput, mean wait, remaining queue and
rank; baseline stays first, followed by authoritative rank order and ineligible
candidates. Additional utilization, counts and input changes remain in details.
The table scrolls horizontally on small windows rather than shrinking text.

Paired metric bars use a common zero origin and maximum for each baseline/selected
pair. Labels show actual values outside the bars. Zero stays zero; undefined
metrics stay N/A. Bar lengths across different metrics are not comparable.
Detailed probe evidence and limitations remain expandable. Rankings are calculated
by the existing analysis library. UI code does not rerank or rerun experiments
while drawing; rank order is cached after a comparison.

## Warehouse and lighting

Eight stable built-in meshes still share one vertex buffer and one index buffer.
Objects reuse mesh ranges; scenario capacities never allocate individual meshes.

- Forklifts have eight-sided tires, a distinct chassis, counterweight, operator
  compartment, overhead guard, mast members and forks.
- Workers retain separate limbs, vest, helmet brim and reflective bands.
- Racks use front/back beams, uprights, pallet bases, smaller varied cartons and
  straps at three levels.
- Docks have shutter details, platform edges, bumpers and alternating safety
  markings. Cargo has a slatted pallet and separated strapped packages.
- A thick floor plinth, subtle floor divisions, marked zones, open cutaway wall
  and rear structural frame establish a bounded industrial space.

Every vertex has a finite unit normal and an explicit operational-color weight.
Safety accents retain their material hue while other surfaces respond to busy,
idle or selection color. Materials are simple colors; there is no PBR, roughness
simulation, shadow map, texture asset system or ray tracing.

The fragment shader uses neutral ambient fill (0.48) and one fixed world-space
directional diffuse light (0.52). Per-object inverse-transpose normal matrices
handle rotation and nonuniform scaling. Matrix/tint data fit the guaranteed
128-byte push-constant budget. Shaders compile/embed at build time; the compatible
scene and UI pipelines are each created once and retained across normal resizing.

FIFO waiting cargo is amber, processing is cyan, completed cargo vacates its slot
and completion counts update from observations. Resource highlights indicate
aggregate capacity usage, not physical assignments. Racks are decorative storage;
the warehouse does not track rack inventory. Geometry, floor distances, animation,
labels and lighting never influence operational service timing or metrics.

## Interaction and exports

ImGui keyboard navigation is enabled. UI input capture prevents camera and
playback conflicts; full editor/analysis pages capture mouse input across the
covered scene. Disabled controls have concise explanations. Selection combines
state tint, a strong outline, real object identity and the inspector.

Analysis offers Windows **Save JSON report…** and **Save CSV summary…** with
Unicode native dialogs, format filters and suggested extensions. Dialogs preserve
the working directory, return cancellation without exporting, and surface errors.
An existing filename is explicitly rejected even after Windows' overwrite prompt:
the authoritative export API only creates new files. Exclusive creation remains
the final race-safe guard. Entering an explicit path remains available, including
on platforms without the Windows dialog. Dialog wall time is discarded from live
clock input. JSON retains replay-verifiable inputs; CSV remains a summary.

The automated graphical workflow exercises path entry, JSON and CSV buttons,
existing-file rejection, preserved report state and replay verification. The
native Save As modal itself requires interactive Windows testing.
Both editor and candidate comparison actions use the selected mode, objective and
checkpoint through one command builder. A GPU interaction check exercises the
editor action with a selected checkpoint, rather than injecting the comparison.

## Validation and measurements

All ordinary tests are CPU-only; the five GPU tests are opt-in and serial.
The new tests cover mode layouts, responsive bounds, state/draft/report preservation,
field descriptors/errors, authoritative best candidate and evidence, chart values,
zero/N/A/error cases, all built-in normals, winding, nonuniform normal transforms
and captured input. Existing geometry tests now audit triangle normals rather than
assuming every mesh is assembled solely from cubes.

The GPU walkthrough adds a neutral cube shading readback: three visible face
shades must be produced from white vertex materials. Existing depth, tint,
resize/minimize/restore, selection, camera, playback, mesh/pipeline reuse and
validation checks remain. It captures all five requested views at three sizes.

### CPU benchmarks

Release, same machine, existing benchmark executables. Layout measurements are
100-run means including allocation; workspace values are medians of three trials.
These are small local samples, not GPU timings or promises of a speedup.

| Measurement | Before | After |
|---|---:|---:|
| Baseline layout construction | 2.212 µs | 2.278 µs |
| Baseline owned frame | 5.230 µs | 5.566 µs |
| Maximum layout construction | 211.849 µs | 184.381 µs |
| Maximum owned frame | 133.102 µs | 132.229 µs |
| Baseline camera update | 0.095 µs | 0.097 µs |
| Baseline ray + picking | 0.461 µs | 0.498 µs |
| Large scene ray + picking | 10.117 µs | 10.567 µs |
| Baseline paused frame update | 0.172 µs | 0.171 µs |
| Large scene paused frame update | 4.093 µs | 4.073 µs |
| 3-candidate fresh / checkpoint comparison | 9.390 / 4.812 ms | 10.190 / 4.789 ms |
| 10-candidate fresh / checkpoint comparison | 26.727 / 8.826 ms | 26.175 / 9.151 ms |
| 50-candidate fresh / checkpoint comparison | 123.520 / 28.570 ms | 119.595 / 26.723 ms |
| Result switching, 3 / 10 / 50 candidates | 1.018 / 0.986 / 1.041 ms | 1.035 / 0.982 / 0.908 ms |

The environment adds six reusable draws: live baseline 83→89 objects in the CPU
benchmark; large layout 1090→1096. Selectable counts stay 29/896. Two geometry
buffers and eight meshes remain. Vertex stride changes 24→40 bytes to carry
normals and the material state weight. No per-frame mesh uploads are added.

An initial 600-frame before run was interrupted after unusually slow presentation;
it is excluded. A complete 180-frame rerun produced UI construction 55.851 µs,
result UI 30.291 µs, record/submit/present 2.822 ms and total CPU frame 3.313 ms.
The final Release capture walkthrough produced UI construction 64.234 µs,
result UI 56.954 µs, record/submit/present 2.740 ms and total CPU frame 3.605 ms.
It submitted 497 frames, versus 400 in the before walkthrough, because capture,
navigation and export checks were extended. Result UI now builds summary/delta
cards, evidence and four paired bars; its additional ~27 µs is small in absolute
terms. Presentation includes GPU synchronization and display scheduling; these
measurements cannot establish a GPU speedup.

### Build, tests and graphical acceptance

Verified on Windows/MSVC x64, Vulkan SDK/loader 1.4.363 and NVIDIA RTX 5080 Laptop
GPU (device API 1.4.351), on 2026-10-09:

| Configuration | Result | Elapsed |
|---|---|---:|
| Windows Debug preset | Build succeeded; 230/230 CTest passed | 77.08 s |
| Windows Release preset | Build succeeded; 230/230 CTest passed | 33.96 s |
| Renderer-disabled Debug | Build succeeded; 205/205 CTest passed | 46.23 s |

These totals include all previous cases and eleven new CPU design cases. Both
graphics-enabled suites include the five serial Vulkan tests. A final caption
cleanup was additionally verified with the Debug live GPU test and the complete
Release capture/export walkthrough. Synchronization validation remained enabled:
**zero errors and zero warnings**, including shutdown/reinitialization. Scene/UI
pipeline generations stayed **1/1**, layout generation **1**, font texture creation
**1**; DPI/capture transitions caused 33 atlas updates, with no further creation
through steady frames. Core geometry uses two buffers; readback/depth resources
continue to follow swapchain lifetime tests.

The walkthrough launches the viewer, edits the forklift count via SDL text events,
checks a negative-input error, adds a candidate, compares from start, opens the
real final result, orbits/picks it, returns through the header action, runs a
selected checkpoint through the editor button, inspects probes, and exports both
formats through path controls. Baseline snapshots remain identical. The eight-hour
baseline is still 96 arrivals / 63 completed / 32 waiting / 1 processing. The
fresh extra-forklift candidate is 94 completed / 0 waiting; the two-hour checkpoint
branch is 87 completed / 8 waiting. Snapshot, recording/replay, comparison,
file-safety and headless CLI cases remain green. Separate headless invocations
confirmed the same baseline and checkpoint results without graphics initialization.

Native Windows WM_CLOSE was sent to owned launcher and live-viewer windows.
Both exited **0**, at window DPI **144 (150%)**. No desktop input or screenshot
capture of another application was used.

### Verified screenshots

All seventeen captures were visually inspected. The candidate page's redundant
large caption was removed after screenshot review; the header and viewport still
identify the read-only final result, and **Return baseline** is prominent.

| View | 1280×720 | 1600×900 | 1920×1080 |
|---|---|---|---|
| Simulation overview at 2 h | [PNG](artifacts/visual-design-simulation-1280.png) | **PNG** (archived capture: `visual-design-simulation-1600.png`) | **PNG** (archived capture: `visual-design-simulation-1920.png`) |
| Scenario editor | [PNG](artifacts/visual-design-editor-1280.png) | **PNG** (archived capture: `visual-design-editor-1600.png`) | **PNG** (archived capture: `visual-design-editor-1920.png`) |
| Analysis results | **PNG** (archived capture: `visual-design-analysis-1280.png`) | **PNG** (archived capture: `visual-design-analysis-1600.png`) | **PNG** (archived capture: `visual-design-analysis-1920.png`) |
| Candidate final preview | **PNG** (archived capture: `visual-design-candidate-1280.png`) | **PNG** (archived capture: `visual-design-candidate-1600.png`) | **PNG** (archived capture: `visual-design-candidate-1920.png`) |
| Selected shipment | **PNG** (archived capture: `visual-design-shipment-1280.png`) | **PNG** (archived capture: `visual-design-shipment-1600.png`) | **PNG** (archived capture: `visual-design-shipment-1920.png`) |

Additional **invalid input** (archived capture: `visual-design-invalid.png`) and
**640×480 / 200%** (archived capture: `visual-design-dpi200.png`) captures demonstrate validation
and small-window scrolling. At 1280×720 / 150%, the editor scrolls to advanced
simulation fields and lower actions; all primary analysis columns remain readable.

### Architecture and file inventory

SimulationClock, warehouse events, resource reservations, snapshots, recorded
commands, ranking and report schemas are unchanged. CPU layout/design utilities
remain in `simforge_viewer`; chart/summary adapters are in `simforge_workspace`.
The renderer receives only owned scene values. ImGui widgets and Windows dialogs
remain in the graphical sandbox; the headless executables do not link them.

New source files:

- `engine/include/simforge/visualization/design_palette.hpp`
- `engine/include/simforge/workspace/presentation.hpp`
- `engine/src/workspace/presentation.cpp`
- `apps/sandbox/save_dialog.hpp` and `save_dialog.cpp`
- `tests/visual_design_test.cpp`

Modified presentation and integration files:

- `apps/sandbox/dashboard.hpp`, `dashboard.cpp`, `workspace_panel.hpp`,
  `workspace_panel.cpp`, `viewer.cpp` and `CMakeLists.txt`
- `engine/include/simforge/viewer/presentation.hpp`, `engine/src/viewer/presentation.cpp`
- `engine/include/simforge/visualization/visual_palette.hpp`,
  `engine/src/visualization/warehouse_layout.cpp`
- `engine/include/simforge/renderer/geometry.hpp`, `render_frame_data.hpp`,
  `engine/src/renderer/geometry.cpp`, `industrial_geometry.cpp`, `vulkan_scene.cpp`,
  `renderer.cpp` and `engine/shaders/scene.vert`, `scene.frag`
- `engine/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/warehouse_layout_test.cpp`
- `README.md`, this guide, and current-presentation notices in
  `docs/scenario-workspace.md`, `interactive-warehouse-viewer.md`, `vulkan-renderer.md`

Verified PNGs under `docs/artifacts/` are actual Vulkan readbacks, converted
losslessly from PPM. No desktop screenshots or generated mockups are used.

## Limits and next work

This is a styled low-poly cutaway presentation, with a small lighting model.
There are no cast shadows, skeletal animation, routes or physical resource IDs.
Capacity visualization still supports 256 of each resource, and the visible FIFO
queue is bounded with a truthful hidden count. Reports and final previews retain
their original semantics, including equal comparison horizons/tick rates and
capacity-only checkpoint interventions. Final previews have no playback timeline.

Comparisons remain synchronous and bounded; large scenarios may briefly block the
UI. Form/results pages scroll when content exceeds available height. Native modal
dialog behavior is not driven by the GPU test harness. Optional on-page 3D previews
are not added; **Preview result** opens the dedicated simulation viewport.

A useful next milestone would be cancellable background experiment execution and
progress reporting, while preserving immutable inputs and reproducible exports.
That work has not been started.
