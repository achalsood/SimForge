# Live warehouse simulation visualization

> Publication curation: additional captures cited in this engineering record remain in Commit #33. See [screenshot history and recovery](screenshot-gallery.md#historical-captures).

This document records Commit #14. Commit #15 replaces the title-bar metrics and
Space-only playback with the [interactive dashboard and viewer](interactive-warehouse-viewer.md).
Its current capture suffixes and input controls are documented there.

`--live` connects the existing warehouse runtime to its procedural 3D scene.
Every operational cargo object represents a real shipment ID. The renderer does
not schedule work, invent events, calculate throughput, or change simulation rules.
Static mode and the original cube demo remain available.

```powershell
build/windows-release/apps/sandbox/simforge_sandbox.exe --scenario examples/warehouse-baseline.json --live --speed 300
```

Speed defaults to **300 simulated seconds per real second**, so the eight-hour
baseline lasts approximately 96 real seconds, excluding pauses/startup. CLI speeds
must be finite in **0.01..3600**; fractional speeds are supported. Space pauses or
resumes logical progression while presentation continues. At the horizon, the
final state stays visible until window close; nothing restarts automatically.
The title and console show actual time, counters and visible/hidden queue counts.
Structured console output includes throughput, average wait and all three
cumulative utilization percentages, updated about once per real second and on
pause/horizon changes. Undefined values display `unavailable`.

## Runtime and ownership boundary

```text
validated ScenarioDefinition
  -> WarehouseSimulation (owns shipments, reservations, events, SimulationClock)
  -> owned WarehouseObservation (bounded active records + exact metrics)
  -> LiveWarehouseScene (static prefix, capacity indicators, shipment targets)
  -> owned RenderFrameData
  -> existing Vulkan renderer (shared GPU assets, read-only drawing)
```

`warehouse/warehouse_observation.hpp` is a domain-only observation contract.
`capture_observation(waiting_limit, processing_limit)` is const and returns owned
values: tick/rate/horizon/reached flag, current capacities and usage, actual
`WarehouseMetrics`, a bounded FIFO waiting prefix, a bounded ordered processing
prefix, and hidden counts. Shipment records retain ID, actual arrival/start ticks,
status and optional actual completion tick. Completed history is omitted from
the active vectors and retained in aggregate metrics; no completion animation
keeps finished work in service.

Capture does not settle events, mutate state, serialize a full checkpoint, or copy
completed history. Untouched construction has tick zero with arrival zero pending;
the first advance, including `advance_elapsed(0)`, settles that real arrival.
FIFO starts with equal fixed processing durations imply completed and started
record prefixes. Processing extraction uses `[completed, started)`; waiting
extraction uses the unconsumed FIFO IDs. Thus work is O(copied visible records),
independent of historical arrival count. If variable-duration/out-of-order service
is introduced later, this prefix invariant must be replaced by an active index.
Each extraction limit is checked against the existing 100,000-shipment workload
bound before allocation. Zero limits and zero resources are safe.

The visualization adapter links core/domain plus renderer-neutral GLM scene math.
It links neither SDL nor Vulkan, including with the renderer disabled. The Vulkan
renderer has no warehouse dependency and receives no mutable domain references.
No Vulkan handles, visual IDs, animations or GPU packets enter persisted snapshots.

## Timing authority

The runtime's existing `SimulationClock` remains the sole authoritative logical
clock. `advance_elapsed(nanoseconds)` copies it temporarily to schedule a target,
checks arithmetic using the existing clock semantics, clamps the target to the
horizon, and calls `advance_to(target)` while the real clock still has its previous
tick. Events therefore execute at their own ticks with correct occupancy integrals.
Only after dispatch does the clock receive the scheduled fractional remainder and
control flags. There is no independently advancing warehouse float clock or second
application tick counter. Overflow-sized inputs fail before operational mutation;
negative elapsed values always fail. At the horizon, further positive elapsed
inputs leave state unchanged and discard leftover phase.

The graphical loop measures `steady_clock` elapsed nanoseconds, polls SDL, applies
pause controls, advances the warehouse, captures changed observations, updates
dynamic targets, advances presentation effects using real elapsed time, then
renders. Slow/minimized rendering does not discard elapsed time or events. Explicit
tick execution remains independent of pause/scaling, as in the original clock.
Scaling retains the existing per-call nanosecond rounding: identical inputs and
controls repeat in the supported environment; arbitrary non-unit frame partitions
need not produce identical intermediate ticks. Different tested pacing schedules
still reach identical final operational snapshots. Cross-hardware bitwise
simulation/graphics determinism is not promised.

## Shipment and resource presentation

Waiting shipments occupy existing cargo staging positions in FIFO prefix order.
The head has a warm gold tint; subsequent queued cargo is blue-gray. Processing
shipments are green and occupy deterministic dock slots in ascending active ID
order. Identity is the real shipment ID, preserved across transitions. A restrained
six-tone variation helps distinguish identity changes without inventing material
properties. Completed IDs immediately leave the active set.

The runtime models atomic reservations of one bay/worker/forklift per job and
aggregate capacities, not individually identified machines or bay assignments.
Accordingly, the first N capacity slots are busy, where N is actual current usage.
Busy dock structures turn green; busy vehicles and workers brighten; idle capacity
is dimmed. These are deterministic presentation slots, **not authoritative individual
assignments**. Current usage differs from cumulative utilization; both come directly
from runtime accounting and are labeled accordingly. Resource slots never release
before the reservation ledger does. Jobs unfinished at the closed horizon remain
visibly processing.

The static-mode staging pallets are removed in live mode. Rack cargo blocks remain
inert parts of the reusable rack mesh; they are not shipment objects or counted as
operational arrivals. Actual waiting/processing cargo is the only dynamic shipment
representation.

## Queue visibility and capacity changes

The viewport shows at most **128 waiting shipments**, additionally limited by the
layout's staging slots (`max(4, 2 * bays)`). All processing shipments fit when
captured using the adapter's bay limit and supported capacities (0..256 each).
The runtime retains the complete queue. Observation and display report both
visible and hidden counts; the title/console never imply all entries are visible.
Baseline has six staging slots: its final 32 waiting shipments display six and
explicitly report **26 hidden**. FIFO IDs are neither skipped nor fabricated.
Queue compaction and processing-slot reassignment are presentation decisions.

The adapter reads current capacities, not just initial scenario counts. On a
capacity change it rebuilds layout values once, preserves `(kind, ordinal)` keys
for retained capacity, reframes, and resets transient effects. GPU meshes and
pipelines remain shared. Unsupported capacities fail explicitly rather than
silently drawing old counts. CPU tests cover increases, valid reductions, zero
capacity after release, restored capacities, and rejection below active usage.
The CLI does **not** play recorded capacity-change timelines in this milestone;
headless recording/replay remains unchanged. The adapter handles current state
after API-driven commands/restoration/replay, with no interactive resource editor.

## Static/dynamic updates and transitions

The adapter builds floor, racks and markings once as a retained frame prefix.
Dock/worker/forklift templates are cached as a bounded dynamic suffix. It updates
that suffix and real shipment objects when tick, metrics or capacities change;
unchanged observations are skipped by the application. Static layout regeneration
occurs only for capacity changes, not every frame. RenderFrameData owns its values
and is reused, with no whole-frame copy per render call.

Newly observed cargo settles vertically by 0.30 units over **250 ms of real time**
with a simple quadratic ease. Horizontal targets, lifecycle status, metrics and
busy indicators change immediately. Processing/queue slot changes snap: the model
contains no travel duration or vehicle-to-shipment assignment, so no transport path
or operational delay is invented. Queued targets remain separated, and vertical
settling stays within a slot's XZ footprint. Effects finish even while logical
playback is paused. No completed-object animation history accumulates.

Reset, backward observations, capacity/layout changes, or jumps over 120 simulated
seconds snap to current targets. Large jumps may include arrivals and completions
between observed frames; such finished shipments are deliberately not replayed as
currently active. `set_state(observation, true)` explicitly resets effects even
when restored ticks/IDs match the prior frame.

Restoration reconstructs correct active IDs, FIFO, resource usage and metrics.
Warehouse persistence schemas are unchanged: restoring resets wall-time remainder,
pause and speed to their existing defaults. Applications can reapply playback
preferences. Animations are reconstructed/reset in the visualization layer and
never serialized. Tests compare restored and exactly replayed observations and
render targets, and ensure stale shipments disappear.

## Verification and captures

Debug passed **151/151** tests with five opt-in graphics cases; Release passed
**146/146** ordinary tests; renderer-disabled Debug passed **126/126**. The 25 new
CPU/CLI cases cover observation ownership/nonmutation, FIFO lifecycle, capacity
activity, 100,000-arrival overflow, multiple pacing subdivisions/speeds, pause,
large elapsed time, restoration, exact recording replay, transition reset,
intermediate/final metrics, invalid inputs and seeded randomized workloads.
Import inspection confirms the live test and isolated headless executable import
neither SDL3 nor Vulkan. Existing movement/warehouse snapshot and recording tests,
comparison rankings and JSON/CSV exports pass; a separate headless export CLI run
retains baseline/add-forklift branch outcomes of 63/87 completions and 32/8 waiting.

Live Debug and Release Vulkan runs verified real operational images, depth,
resize/minimize/restore, one layout generation, one pipeline, shared asset reuse,
safe close and renderer reinitialization with no stale shipments. All reported
**zero validation warnings and errors** through cleanup. A 600-frame Release run
presented 605 including diagnostic restoration/probes and compared its complete
final warehouse snapshot exactly with headless execution.

GPU phases use the real runtime at untouched tick zero, settled tick zero,
600 seconds and 900 seconds, then pace through the configured horizon. GPU pixels
change on actual arrivals, FIFO queue growth and completion/identity changes.
The opt-in smoke routes injected Space events through the actual SDL poll/consume
and application control path: logical time remains fixed for 250 ms, then resumes.
Host foreground-focus restrictions prevented guarded native keyboard automation,
so hardware-keyboard testing is not claimed. Normal live desktop output was
captured and verified separately, and native window close exited 0. Ordinary
Release defaults to validation off; all graphics smoke runs require it on.

These are actual GPU readback PNGs, converted losslessly from P6 RGB captures and
inspected, rather than conceptual images. Client size is 1280-by-720:

| Capture | Authoritative state |
| --- | --- |
| **Initial** (archived capture: `live-warehouse-initial.png`) | Tick 0 before initial settlement: no arrivals |
| **Processing** (archived capture: `live-warehouse-processing.png`) | Settled tick 0: shipment 0 processing, one busy slot per resource |
| **Queue buildup** (archived capture: `live-warehouse-queue.png`) | 600 s: three arrived, two processing, one waiting |
| **Final** (archived capture: `live-warehouse-final.png`) | 28,800 s: 96 arrived, 64 started, 63 completed, 32 waiting, one processing; six waiting shown, 26 hidden |

**Live queue buildup at 600 simulated seconds** (archived capture: `live-warehouse-queue.png`)

## Performance observations

Measured on 2026-10-08: MSVC x64, GLM 1.0.3, SDK/loader 1.4.363, NVIDIA RTX 5080
Laptop GPU API 1.4.351, BGRA8 sRGB/FIFO, three images, D32 depth. Release benchmark
uses 1,000 repeated bounded captures/target builds/frame updates, allocations
included. It measures API costs even for unchanged observations; the actual
application skips unchanged observations.

| Actual arrived / completed / waiting / processing | Visible waiting / processing | Hidden waiting | Objects / draws | Extraction | Dynamic construction | Frame update |
| --- | --- | --- | --- | --- | --- | --- |
| 12 / 7 / 4 / 1 | 4 / 1 | 0 | 80 | 0.137 us | 0.861 us | 0.032 us |
| 100,000 / 1,999 / 98,000 / 1 | 6 / 1 | 97,994 | 82 | 0.115 us | 0.897 us | 0.033 us |
| 1,000 / 577 / 360 / 63 | 128 / 63 | 232 | 703 | 1.175 us | 7.415 us | 0.319 us |

Synthetic 17 ms pacing at speeds 1/60/300/3600 took 7,059/118/24/2 update calls
and produced the same seven completions in the 120-second benchmark workload.
Measured mean CPU loop costs were 0.555/0.586/0.871/2.600 us respectively. These
are synthetic CPU tests, not elapsed demonstration durations or GPU throughput.

The live baseline has 75 initial render objects and 82 at its horizon, compared
with static mode's 81. Final drawing references 8,856 mesh vertices and submits
13,284 indices. Eight unique meshes still share two static GPU buffers; there is
no shader/pipeline regeneration, per-object GPU allocation or dynamic mesh upload.
Smoke alone adds four readback buffers across four swapchain/depth generations.
No batching/instancing optimization or speedup claim is introduced.

In one 600-frame Release live smoke (speed 300), 500 samples after frame 100 measured
mean CPU render/present 4.158 ms, standard deviation 0.108 ms, p50 4.158, p95 4.339,
min/max 3.721/4.740 ms. Samples while still advancing measured mean 4.075 ms and
standard deviation 0.682 ms (502 samples, including pause and resize phases).
Application means were extraction 0.689 us, dynamic targets 3.601 us, presentation
frame update 0.131 us, with one layout generation. Measurements include FIFO and
submission/fence waits and validation; they do not measure GPU execution or
establish a graphics bottleneck. The prior static baseline was approximately
4.163 ms; differing scenes/runs do not constitute a speedup comparison.

## Commands and remaining work

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
build/windows-debug/apps/sandbox/simforge_sandbox.exe --scenario examples/warehouse-baseline.json --live
# Bounded smoke uses documented synthetic elapsed inputs to reach the horizon:
build/windows-debug/apps/sandbox/simforge_sandbox.exe --scenario examples/warehouse-baseline.json --live --speed 300 --graphics-smoke 180
# New PPM output also saves sibling -initial/-processing/-queue/-completion files:
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-graphics-test.ps1 -Executable build/windows-debug/apps/sandbox/simforge_sandbox.exe -Mode Review -Frames 180 -Scenario examples/warehouse-baseline.json -Live
cmake --preset windows-debug -DSIMFORGE_GRAPHICS_TESTS=ON
ctest --preset windows-debug -L graphics --output-on-failure
cmake --preset windows-debug -DSIMFORGE_GRAPHICS_TESTS=OFF
cmake --preset windows-release
cmake --build --preset windows-release
ctest --test-dir build/windows-release --output-on-failure
build/windows-release/benchmarks/simforge_live_warehouse_benchmark.exe
```

Live mode requires a graphical warehouse scenario and conflicts with headless,
recording, replay, comparison, experiment and entity/tick override workflows.
`--speed` requires `--live`; duplicate, malformed, zero, negative, nonfinite and
out-of-range values fail before graphics initialization. Static commands and
all prior headless workflows remain supported.

Intel Graphics remains unsupported because the tested driver lacks required
maintenance1 presentation fences. No graphics compatibility guarantee was weakened.
Only NVIDIA was live-tested; other GPUs, split queues and operating systems were
not tested live. Native keyboard automation remained unavailable as described above.

No font/UI framework, transport animation, individual resource assignments,
recording-timeline playback, snapshot-loading CLI, camera controls, physics,
pathfinding or AI is added. Resource capacities retain the 256-per-category visual
limit; queue overflow is explicit. Recommended Commit #15: focused playback
controls and camera/selection interaction, with clear lifecycle/slot labels and
snapshot/replay control tests. Do not infer physical travel or change scheduling
to drive those controls; no Commit #15 implementation is included here.
