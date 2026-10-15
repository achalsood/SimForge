# Asynchronous experiments

> Publication curation: additional captures cited in this engineering record remain in Commit #33. See [screenshot history and recovery](screenshot-gallery.md#historical-captures).

Commit #19 moves graphical comparisons off the SDL thread. Configure candidates,
choose the objective and fresh/shared-checkpoint mode, and select **Run comparison**.
Continue using the camera, playback controls or other application modes. Analysis
shows the submitted request, actual phase, completed candidates, run ticks and wall
time. Select **Cancel Experiment** or wait for **Completed**, review a result preview,
then export JSON/CSV. Draft edits and candidate changes apply to future requests.
The previous successful report remains available with its saved inputs.

## Ownership and execution

`ExperimentExecution` lives in the CPU-only `simforge_workspace` target, including
renderer-disabled builds. It owns at most one `std::jthread`, one progress snapshot,
one latest unsuccessful-attempt summary and one unpublished complete result. There is
no pending queue, polling idle thread,
thread pool, detached thread or callback into workspace/UI objects. Public commands
are serialized by the owning thread; progress reads are mutex protected and may
come from any thread. The optional observer is for controlled tests and measurement,
executes on the worker, and must have a lifetime covering the run.

`capture_request` copies validated baseline/candidate definitions, stable IDs,
cached differences/notes, objective, mode and optional checkpoint tick. A current
checkpoint is an owned snapshot captured on the main thread, preserving intervention
history. It contains no references to drafts, viewers or graphics objects. An explicit
checkpoint tick is prepared entirely on the worker. Initial comparisons start each
definition at tick zero. Historical and explicit checkpoint inputs are mutually exclusive.
Snapshot/capacity preflight uses references to avoid extra shipment copies.

`execute_experiment` contains the previous synchronous orchestration; `compare`
remains available for synchronous callers. The worker uses the same simulation,
capacity intervention ordering, ranking, rational wait comparisons, tie breaks,
probe reuse, deltas and report contracts. Every candidate has a private runtime.
Fresh runs retain independent authoritative reports, including their original probes.
Checkpoint runs retain one authoritative shared-history report. SDL3, Dear ImGui,
viewer controllers and Vulkan remain on the main thread.

Async computations additionally retain final baseline/candidate snapshots, in canonical
candidate-name order, so preview selection restores a verified result rather than
rerunning the experiment. On successful publication an older result preview retains
its owned snapshot and camera; retained custom playback is preserved, and preview
labels include their report ID. See [custom playback](custom-scenario-playback.md).
Preview construction/validation and scene creation still
occur on the main thread. Final snapshots are excluded from export payloads.

## Lifecycle and publication

| From | To | Cause |
|---|---|---|
| Idle / Completed / Cancelled / Failed | Preparing | Valid submission; prior thread joined |
| Preparing | Running | Worker enters computation checkpoints |
| Preparing / Running | Cancelling | Cooperative stop requested |
| Cancelling | Cancelled | Worker acknowledges stop and discards partial computation |
| Running | Completed | Entire report committed under publication lock |
| Preparing / Running / Cancelling | Failed | Worker exception (including observer failure) |
| Preparing | Failed | Worker startup failure |

Invalid submissions and overlapping starts throw a useful error without replacing
the current attempt or saved report. Accepted requests have increasing IDs; candidate
IDs are retained beside canonical definitions. A candidate rename/removal cannot
change a submitted request. Shutdown closes submissions permanently.

Cancellation and terminal publication use the same mutex. If cancel wins before
publication, no result is available. If publication wins first, later cancel is a
harmless no-op and the completed result remains successful. No partial rankings,
probes or previews are published. Exceptions are caught on the worker and converted
to Failed with error text; failure never becomes success. A terminal attempt may be
retried with a new identity. Programmer errors are exposed as failures.

The main loop moves a complete result into the workspace only for the service's
current request ID. Old IDs cannot retrieve a newer result. Scenario replacement
is rejected while running, and discards unconsumed earlier completion before changing
the baseline. A synchronous comparison also drains an older pending completion.
Candidate edits preserve reports; report labels identify saved inputs rather than
the current form. JSON/CSV retain their existing verified report schemas and scenario/
candidate names; session request and numeric candidate IDs are workspace provenance,
not new fields in the legacy export formats.

## Progress and cancellation

`ExecutionControl` is optional in simulation and analysis APIs. It checks a stop token
before/after observer calls, at preparation/candidate/probe/report boundaries, and
after each batch of 256 simulation events. The final tick is also published. A single
latest snapshot replaces prior progress; no tick history accumulates. Synchronization
occurs per batch rather than per event. Tick progress is local to each run and may
restart for a different candidate or probe; candidate-completion counters never decrease.

The candidate bar measures **completed candidate evaluations**, not a weighted estimate
of total time. Baseline preparation, probes and report construction are indeterminate.
Fresh candidate completion includes its entire authoritative analysis. A report-ready
100% bar appears only after transactional completion. Elapsed time uses a steady clock
and stops at a terminal state. No artificial delays or fake timer progress exist.

Cancellation cannot mutate the live baseline: interrupted private branches are
discarded. Snapshot validation/restoration/capture, capacity dispatch, allocation,
ranking and small report construction sections are bounded but not interruptible
internally. A dispatch can start up to the validated shipment limit at one event.
Cancellation therefore has no universal wall-time deadline; scheduling and allocator
behavior also affect latency. The next control checkpoint checks stop before publication.

Normal application shutdown requests cancellation and joins before renderer teardown.
The service destructor also cancels/joins; worker-owned inputs and state survive until
join. No worker accesses objects destroyed during exception unwinding. Test observers
can intentionally block: they must be released before joining, and diagnostics use
bounded timeout gates. Production supplies no blocking observer.

## Resource bounds and restrictions

There is one worker, at most 50 candidates, one saved successful workspace report,
one new computation, one latest progress snapshot and one latest unsuccessful-attempt summary. Existing scenario, snapshot,
shipment, resource and report limits remain authoritative. Retaining up to 51 final
snapshots increases memory compared with legacy synchronous comparisons; the bound
scales with 51 times the validated per-snapshot payload limit, and old/new snapshots
may coexist until replacement. Large 50-candidate requests can use substantially more
memory than the benchmark below. Export serialization and snapshot preview restoration
are synchronous; their latency is separate from experiment execution. No persistent
job history, resumable jobs, parallel candidates or background export is introduced.

PNG capture and existing managed artifact retention remain unchanged. Diagnostic gates
and substantial workloads run only in opt-in graphics smoke mode. Normal simulation
execution is never intentionally slowed.

## Validation and measurements

CPU tests exercise immutable submitted inputs, stable IDs, owned history, invalid
requests, overlap rejection, monotonic candidate counts, exact synchronous equivalence
for all objectives and both modes, conservation, repeated requests, immediate stop,
stop during a candidate/probe, stop before publication, failure/retry, stale completion,
previous-report retention and destruction while running. Gates synchronize with actual
worker checkpoints; fixed-duration sleeps do not decide lifecycle correctness.

The Vulkan walkthrough starts work through graphical controls, verifies candidate
previews, holds real work at a checkpoint to exercise camera/mode/cancel controls,
then measures eight presented frames during an actual 100K-arrival/50-candidate run.
It cancels, preserves the report, retries, and tests native close with an active worker.
Final CPU/graphics suites: Debug 254/254, Release 254/254, renderer-disabled
228/228 (ten new CPU test cases), followed by affected workspace/async and graphical
reruns after final preview/publication fixes. The complete suites include existing snapshot,
replay, scenario validation, synchronous comparison, exports and artifact-retention
checks. Graphics validation requires zero errors and warnings on every successful
walkthrough. Thread safety was audited using ownership/locking and synchronized tests;
no ThreadSanitizer run is claimed on this MSVC configuration.

### Release orchestration benchmark

Measured on this Windows workstation, MSVC 19.51, C++23 Release, three trials and
median times, 60 Hz, one-second arrivals, four-second processing, baseline capacities
4/8/2 and seed 42. No GPU or file I/O participates in the CPU benchmark. This is an
orchestration/responsiveness measurement; it establishes no speedup.

| Arrivals | Candidates | Legacy sync ms | Sync + snapshots ms | Async observed ms | Submission ms | Snapshot + callback ms | Worker updates | Progress read us |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 10K | 3 | 9.775 | 11.451 | 13.967 | 0.049 | 10.799 | 1,410 | 0.025 |
| 10K | 10 | 26.117 | 29.996 | 46.981 | 0.064 | 29.567 | 3,999 | 0.021 |
| 10K | 50 | 119.006 | 126.318 | 132.070 | 0.074 | 125.815 | 18,979 | 0.020 |
| 100K | 3 | 93.844 | 104.968 | 111.607 | 0.076 | 104.595 | 13,534 | 0.026 |

The legacy sync path omits final preview snapshots; the second column of timings
isolates that cost. The callback timing uses a counting callback and the same retained
snapshots. Differences smaller than a few milliseconds are noisy across these three
trials; the callback sample is not an isolated measure of the service mutex cost.
Async observed time includes worker startup, publication, retained snapshots and
main-thread polling. Waiting sleeps for 1 ms, whose Windows scheduling granularity
can add several milliseconds; the service itself does not sleep while executing.

Five actual 100K-arrival stop trials each, synchronized with candidate/probe work:

| Stop location | Min ms | Median ms | Max ms |
|---|---:|---:|---:|
| Fresh candidate | 6.336 | 8.159 | 8.760 |
| Bottleneck probe | 2.345 | 4.657 | 22.434 |
| Historical checkpoint candidate (50K arrivals in history) | 13.007 | 13.678 | 14.653 |
| Join during fresh candidate (shutdown) | 0.188 | 0.193 | 0.227 |

Cancellation figures include the observer notification and polling interval. Shutdown
measures synchronous stop/join and avoids that polling delay. These samples are not
a universal cancellation bound. Non-cancellable 100K-shipment snapshot capture,
validation and restore measured 1.423/2.496/3.979 ms; one command dispatching 33,333
waiting shipments measured 0.490 ms. A 200 ms idle interval accrued 0.000 ms process
CPU at Windows process-time resolution. Benchmark peak process working set was
34.520 MiB, covering the workloads above rather than a maximum-size 50x100K report.

Reproduce with `build/windows-release/benchmarks/simforge_async_experiment_benchmark.exe`.
The opt-in Vulkan smoke log separately records mean/max presented frame time during
real background computation and observed cancellation including rendering. PNG
captures and build footprint are linked below with the final graphics review.

### Graphics review and storage

Final Release review on NVIDIA GeForce RTX 5080 Laptop GPU, Vulkan 1.4.351, FIFO
present mode: eight frames during real 100K-arrival/50-candidate computation averaged
4.158 ms (maximum 4.364 ms). Cancellation observed with rendering included was
3.985 ms. Whole walkthrough CPU frame mean was 3.581 ms, UI build 66.456 us and
record/submit/present 2.802 ms; these include synchronization and are not GPU timings.
The run presented 546 frames, maintained one scene/UI pipeline generation each,
validated report exports/replay and native active-worker close/reinitialization,
and recorded **0 Vulkan errors / 0 warnings**.

Fixed documentation PNGs: **running** (archived capture: `async-experiment-running.png`) and
**cancelled with prior report** (archived capture: `async-experiment-cancelled.png`).
The complete managed review is under
`build/test-artifacts/graphics/latest/9ff5ad245bf44a5faf15d3bc726d4e93/`.
Review retention keeps three successful runs; successful Test runs reclaim their
PNGs/logs. Failed diagnostics retain the existing failure policy. No PPM files were
found under build, and no cleanup-script changes or dependencies were added.

Post-implementation footprint (before the final Git commit): repository **1745.55 MiB**;
Debug **832.18 MiB**, Release **232.03 MiB**, renderer-disabled **628.58 MiB**.
New test executables use the existing non-incremental linker policy and create no
new `.ilk` files. Existing application `.ilk` files were preserved. Debug symbols
remain intact. The two committed status PNGs total approximately 253 KiB; measured
build captures include bounded successful review runs and preserved failed diagnostics.

### Changed files

- Execution: `engine/include/simforge/simulation/execution_control.hpp`,
  `engine/include/simforge/workspace/experiment_execution.hpp`,
  `engine/src/workspace/experiment_execution.cpp`.
- Computation/workspace: warehouse simulation header/source, scenario comparison
  header/`comparison.cpp`, scenario workspace header/source, `engine/CMakeLists.txt`.
- Main thread/UI: `apps/sandbox/viewer.cpp`, `workspace_panel.hpp`, `workspace_panel.cpp`.
- Verification: `tests/async_experiment_test.cpp`, `tests/workspace_test.cpp`,
  `tests/CMakeLists.txt`, `benchmarks/async_experiment_benchmark.cpp`,
  `benchmarks/CMakeLists.txt`.
- Guides/captures: `README.md`, `docs/scenario-workspace.md`, this guide and two PNGs above.

A suitable next milestone is validated workload distributions and sensitivity analysis,
with explicit uncertainty and calibration evidence. This milestone does not begin that work.
