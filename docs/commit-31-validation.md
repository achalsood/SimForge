# Commit 31 completion and validation

Scope: seeded count-process arrivals and explicit observation coverage. Baseline was clean at `b88039e8b4cf9e74ddb43027796e06bafda2f0a9`. The local commit uses exactly `feat(workloads): add seeded count-process arrivals and coverage windows`. Its resulting SHA and final clean-tree status are included in the delivery message. No push, publication or subsequent milestone is part of this change.

| # | Requested completion item | Evidence/result |
|---|---|---|
| 1 | Commit SHA | Resulting local SHA is supplied with the final delivery; this committed document does not embed its own hash. |
| 2 | Working tree | Final status is verified after the local commit. All intended files are inventoried below. |
| 3 | Files changed | Core workloads/simulation/snapshots, calibration/coverage, bounded project codecs, native controls/diagnostics, CLI, tests, examples, benchmarks and documentation; exact inventory below. |
| 4 | Arrival architecture | Owned bounded realized tick vector, one pending arrival in the existing scheduler, contiguous stable shipment IDs. No parallel simulator. |
| 5 | Statistical semantics | Homogeneous and piecewise Poisson counts; mean/variance equal integrated intensity under the model. Tick-grid timestamps and simultaneous arrivals are explicit. |
| 6 | Sampler | Poisson additivity with submeans at most 16, bounded Knuth product sampling, independent-domain discrete-uniform locations and sorting. Compared against exponential-wait/hazard construction before implementation. |
| 7 | PRNG/version | Defined SplitMix64 unsigned transition, separate count/location/service domains, explicit seed. New arrival generator 2; legacy workloads retain generator 1. |
| 8 | Quantization | Uniform integer engine ticks inside each intensity segment. Tick-cell expected count is rate/tick-rate; not exact continuous timestamps. |
| 9 | Boundaries | Ordered `[start,end)` segments, no leakage into zero/gap intervals. Completion-before-arrival event ordering and stable sequence tie breaks. |
| 10 | Reproducibility | Golden ticks/count/service requirements verified across the supported Windows x64 Debug, Release and renderer-disabled configurations. No universal compiler/CRT bit-identity claim. |
| 11 | Stream isolation | Resource capacities do not alter external ticks, IDs or sampled service requirements. Service is keyed by ID, not dispatch order. |
| 12 | Snapshot/replay | Version-3 snapshot explicitly carries generator 2. Restore regenerates and validates the owned realization and pending cursor. Fresh run, pause, restore, seek, reset and command recording are tested. Old snapshots remain supported. |
| 13 | Limits | Finite rate 0..1000/s; nonzero segment mean at least 1e-12; at most 64 segments, 80,000 expected and 100,000 realized arrivals. Realization overflow fails explicitly. Existing tick/resource/project limits remain. |
| 14 | Coverage schema | Request 2 with normalized UTC windows, stable indices and observed/unobserved/closed/interrupted statuses. Legacy continuous request 1 remains unchanged. |
| 15 | Coverage validation | Positive durations, full explicit partition, sorted ownership, no overlap/duplicates/implicit gaps, at most 256 windows and 366-day span. Records must lie in observed time. |
| 16 | Estimators | Training count divided by observed exposure; excluded elapsed time is never denominator evidence. Constant and per-UTC-bin estimates reuse conservative marginal Chernoff uncertainty. |
| 17 | Daily profiles | Partial/unequal exposure is summed per predefined time-of-day bin. No exposure yields unavailable rate/interval, not zero. Supported sparse bins keep warnings and uncertainty. |
| 18 | Train/holdout | Exact chronological partition, including split inside windows. Holdout expectation uses observed holdout exposure. Missing trained rates produce explicit unavailable total prediction/error/likelihood. |
| 19 | Leakage | Changing only holdout records leaves training counts, exposure, rate parameters, interval endpoints, training predictions and predefined model/bin settings unchanged. |
| 20 | Conversion | Direct seeded Poisson conversion preserves rates and source service/resources/horizon/tick rate. Explicit UTC alignment and unsupported-hours exclusion policy retained. Legacy periodic approximation remains reviewed and distinct. |
| 21 | Native controls | Homogeneous/piecewise editor modes, decimal rate, seed, segments, expectation/version/grid labels; coverage request import/editor/table and observed/excluded exposure. |
| 22 | Charts | Status-colored observations/coverage, unavailable-aware intensity, exposure-adjusted holdout/residuals and actual seeded realization against expectation. Worker-built bounded counts; no synthetic stochastic curves. |
| 23 | Async/cancellation | Owned immutable import/fit/experiment results; cancellable realization preview with real phases/counts. Failure/cancel preserves prior reports, history, original scenario, active playback and saved project. Workers stop/join on shutdown. |
| 24 | Headless | Both Poisson modes, request-2 coverage fitting, explicit conversion seed/policy, JSON/CSV export and project verification use the same CPU core without creating a graphics window. |
| 25 | Provenance/export | JSON keeps coverage, fingerprints, split, observed/excluded exposure, uncertainty and derived evidence. Stochastic conversion records source/output scenario, generator/PRNG/grid/seed, policy, expected/actual totals and 64-bin evidence. New CSV has 29 columns including model/bin widths and explicit intensity segments. |
| 26 | Persistence | Streaming v2, per-run/study ownership, bounded parsing, canonical CRC, transactional Open and recovery retained. New feature declarations cover Poisson modes, retained inactive Poisson editor values and coverage windows. Old v1/v2 fixtures pass. |
| 27 | Golden vectors | Homogeneous seed-42 count 24 and piecewise count 35; first 12 ticks and six per-ID service requirements fixed independently of capacity and across builds. See method document for exact vectors. |
| 28 | Distribution tests | 8,192 seeds each for homogeneous/piecewise means, variances, per-segment/half counts and disjoint covariance; low-rate 4,096-seed zero frequency and high-rate 1,024-seed moments; broad justified fixed acceptance regions. |
| 29 | Calibration numerics | Continuous/separated/empty observed windows, closure, interruption, partial hour, unequal days, split inside coverage, start/end ownership, normalization, status/overlap/duplicate/zero exposure/overflow rejection, unavailable rates and tampering. Legacy report round-trips byte-for-byte. |
| 30 | Paired regressions | Direct external-state comparisons; paired statistical, sensitivity, interaction and independent confirmation execution/export; new statistical evidence and calibration coexist in project Save/Open. |
| 31 | Debug tests | Full suite 692/692 passed in 730.31 s, including 19 graphics tests. Final refinement checks are recorded below. |
| 32 | Release tests | All 692 tests passed: 673 CPU/headless tests plus 19 native graphics tests. Final results recorded below. |
| 33 | Renderer-disabled tests | 653/653 passed. Final refinement checks recorded below. |
| 34 | Vulkan validation | Debug and Release native workflows passed their required zero-error, zero-warning Vulkan validation checks, including fresh-process project reopen and shutdown. |
| 35 | Generator benchmarks | Four seeds each at low/medium/high/maximum intensity plus piecewise zero/gaps. Maximum expectation 80,000 generated approximately 79.6K..80.1K in 2.17..2.40 ms in recorded Release run. No speedup claim. |
| 36 | Memory/projects | Recorded cumulative process peak about 41.1 MiB. One-million-record/256-window calibration projects about 120..125 KB; 20-point/four-seed sensitivity 181,638 B and 64-cell/two-seed grid 421,419 B. CSVs contain stage timing and cancellation data. |
| 37 | Screenshots | Nine curated real framebuffer PNGs, listed below; setup, profile, coverage, intensity, holdout, actual realization, playback and restored evidence were visually inspected. Native tests also resize 640/1280/1920 and minimize/restore. |
| 38 | Limitations | Plug-in Poisson assumptions, fixed UTC bins, finite numeric/PRNG approximation and documented support bounds. No service calibration, model search, prediction intervals, shift scheduler, local-time/DST inference or universal cross-platform bit identity. |
| 39 | Next milestone | Independently validated overdispersion diagnostics and predictive uncertainty over repeated simulated realizations. Recommended only; not started. |

## Final validation record

All three configurations built successfully. The complete prior 643/643 Debug/Release and 605/605 renderer-disabled test sets remain included in the larger suites.

| Configuration/check | Passed | Elapsed |
|---|---:|---:|
| Debug full suite | 692/692 | 730.31 s |
| Final Debug CPU suite | 673/673 | 27.21 s |
| Final Debug affected graphics workflows | 2/2 | 37.69 s |
| Final Release CPU suite | 673/673 | 9.54 s |
| Final Release graphics suite | 19/19 | 226.56 s |
| Final renderer-disabled full suite | 653/653 | 27.05 s |

Builds used `windows-debug`, `windows-release` and `windows-headless`. CPU tests ran through CTest with graphics excluded in renderer-enabled builds; native tests ran serially using the `graphics` label. Release CPU and graphics partitions together cover all 692 registered tests. Logs are retained locally under `build/poisson-full-debug-tests.log`, `build/poisson-final-debug-cpu-tests.log`, `build/poisson-final-debug-ui-tests.log`, `build/poisson-final-release-cpu-tests.log`, `build/poisson-final-release-graphics-tests.log` and `build/poisson-final-headless-tests.log`. No compiler errors or warnings were found in the final build logs. `git diff --check` passed, and the README/method/report local links resolve.

The initial full Debug run was followed by the final numerical admission guard, unavailable hover/CLI label corrections, explicit CSV model/segment metadata, real preview progress and inactive-editor feature-declaration regression. Final CPU suites and affected native workflows were rerun on these changes. Unaffected native tests needed no second Debug run. Release graphics ran serially after Debug graphics.

## Statistical evidence and independent references

| Fixture | Seeds | Expected mean/variance | Observed mean | Observed variance | Disjoint covariance |
|---|---:|---:|---:|---:|---:|
| Homogeneous, two equal halves | 8192 | 24 | 24.0509 | 24.1792 | -0.0388136 |
| Piecewise, means 6 and 30 | 8192 | 36 | 36.0129 | 35.8502 | -0.151414 |
| Low mean, one tick | 4096 | 0.1 | 0.0991211 | 0.0981091 | not applicable |
| High mean, one tick | 1024 | 1000 | 999.675 | 1002.83 | not applicable |

Low-mean zero count frequency was 3707/4096; tolerances use seven mean/zero-frequency standard errors and eight variance/covariance standard errors. Tests use fixed seeds and do not rely on exact equality of random count to its expectation. The [method document](stochastic-arrivals-and-coverage.md) links the primary mathematical sources and states the finite implementation/reproducibility limitations.

The multi-shift reference has 138 training arrivals / 54,000 observed seconds and 72 holdout arrivals / 28,800 observed seconds. Constant estimate is 9.2/hour, predicting 73.6 holdout arrivals. Predefined daily fitting recovers 12/hour mornings and 6/hour supported afternoons, predicts 72 holdout arrivals, and leaves unobserved hours unavailable. The historical fixture is deterministic reference data. Converted seed 54321 over aligned 08:00..16:00 expects 66 and generates 72 arrivals; actual native/headless playback completes 62, leaves eight waiting and two in progress.

## Benchmarks and actual screenshots

- [Release raw benchmark CSV](stochastic-coverage-benchmarks.csv): generator/throughput, multiple seeds, 20-point sensitivity, 64-cell interaction, maximum observations and coverage, Save/Open, cumulative peak memory and cooperative cancellation.
- [Native maximum-import CSV](native-stochastic-coverage-benchmark.csv): actual frames during one-million-record import across 72 explicit windows, without a timing gate. Detailed interpretation and measured ranges are in the method document.
- Screens: [setup](artifacts/stochastic-setup.png), [piecewise profile](artifacts/stochastic-piecewise.png), [coverage definition](artifacts/stochastic-coverage-definition.png), [observations/coverage](artifacts/stochastic-coverage.png), [intensity](artifacts/stochastic-intensity.png), [holdout](artifacts/stochastic-holdout.png), [actual realization](artifacts/stochastic-realization.png), [3D playback](artifacts/stochastic-simulation.png), [restored project](artifacts/stochastic-restored.png).

Captured on NVIDIA GeForce RTX 5080 Laptop GPU with Vulkan synchronization validation enabled; source Review run `653f8d3585614feebe235eadd190c535`. Images are copied unchanged from actual framebuffer captures. Each is at most 226,942 bytes. Large generated CSVs and successful Test-mode project/capture directories are reclaimed through existing ownership checks; no full projects or million-row inputs are committed.

## Exact changed-file inventory

54 files: 30 modified and 24 added.

```text
apps/sandbox/calibration_panel.cpp
apps/sandbox/calibration_panel.hpp
apps/sandbox/dashboard.cpp
apps/sandbox/main.cpp
apps/sandbox/stochastic_coverage_diagnostic.inl
apps/sandbox/viewer.cpp
apps/sandbox/workspace_panel.cpp
apps/sandbox/workspace_panel.hpp
benchmarks/CMakeLists.txt
benchmarks/stochastic_coverage_benchmark.cpp
docs/artifacts/stochastic-coverage.png
docs/artifacts/stochastic-coverage-definition.png
docs/artifacts/stochastic-holdout.png
docs/artifacts/stochastic-intensity.png
docs/artifacts/stochastic-piecewise.png
docs/artifacts/stochastic-realization.png
docs/artifacts/stochastic-restored.png
docs/artifacts/stochastic-setup.png
docs/artifacts/stochastic-simulation.png
docs/commit-31-validation.md
docs/demand-calibration.md
docs/native-stochastic-coverage-benchmark.csv
docs/stochastic-arrivals-and-coverage.md
docs/stochastic-coverage-benchmarks.csv
docs/workload-modeling.md
engine/include/simforge/analysis/demand_calibration.hpp
engine/include/simforge/scenario/workload.hpp
engine/include/simforge/warehouse/warehouse_simulation.hpp
engine/include/simforge/workspace/scenario_workspace.hpp
engine/src/analysis/comparison.cpp
engine/src/analysis/demand_calibration.cpp
engine/src/analysis/demand_json.cpp
engine/src/analysis/evaluation.hpp
engine/src/analysis/statistical_experiment.cpp
engine/src/scenario/workload.cpp
engine/src/scenario/workload_json.hpp
engine/src/warehouse/warehouse_simulation.cpp
engine/src/warehouse/warehouse_snapshot.cpp
engine/src/warehouse/warehouse_state.cpp
engine/src/workspace/project.cpp
engine/src/workspace/project_io.inl
engine/src/workspace/workload_draft.cpp
examples/stochastic-coverage/homogeneous.json
examples/stochastic-coverage/multi-shift.csv
examples/stochastic-coverage/multi-shift.json
examples/stochastic-coverage/multi-shift-constant.json
examples/stochastic-coverage/piecewise.json
README.md
scripts/generate-stochastic-coverage-fixtures.ps1
scripts/run-graphics-test.ps1
tests/CMakeLists.txt
tests/fixtures/demand-report-v1.json
tests/stochastic_coverage_cli_test.ps1
tests/stochastic_coverage_test.cpp
```
