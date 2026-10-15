# Commit 32 completion and validation

Scope: frozen demand-model diagnostics and clearly separated predictive uncertainties. Baseline HEAD was clean at `be1bb002b7565d6cbe9afc054c31a1257a86dbc1`. Target local message: `feat(analysis): add demand diagnostics and predictive uncertainty`. The resulting SHA and post-commit clean-tree status are supplied in the delivery. No push/publication or next milestone is included.

| # | Requested item | Result/evidence |
|---|---|---|
| 1 | Commit SHA | Actual local SHA supplied in the delivery; this committed report cannot contain its own hash. |
| 2 | Working tree | Verified clean after the local commit. |
| 3 | Files changed | Exact inventory below: analysis/core codecs, existing worker/history/project integration, native/CLI controls, tests, fixtures, five examples, benchmarks, real captures and documentation. |
| 4 | Data availability/reimport | Raw timestamps are absent after Open. Exact coarsening of retained source counts is supported; finer boundaries require original-byte SHA-256, dataset/coverage and original-bin-count verification. Missing data is never simulated as historical evidence. |
| 5 | Typed architecture | Owned immutable source calibration, versioned settings, bins/pieces/predictions, numerical validity, per-replication seeds/statistics, reference summaries, warnings and content-derived diagnostic ID. Title/notes and elapsed timings do not change statistical identity. |
| 6 | Binning | Disjoint partition-anchored UTC bins with observed exposure, exact counts, integrated profile means, coverage support and explicit excluded/unestimated bins. |
| 7 | Dispersion | Positive-mean Pearson sum and descriptive residual ratio; holdout frozen-parameter denominator, training nominal fitted-parameter correction. Nonpositive degrees of freedom and zero-mean positive counts are explicit. No unqualified pooled VMR or chi-square test. |
| 8 | Zeros | Observed zero bins versus sum of each bin's own exp(-mean), with seeded reference quantiles/ranks. |
| 9 | Upper tails | Predefined inclusive integer threshold, per-bin Poisson tail expectation, actual tail-bin/maximum counts and simulation reference. Exploratory settings remain labeled. |
| 10 | Temporal dependence | Descriptive lag-one standardized residual correlation, requiring eight complete adjacent observed pairs and nonzero variance; gaps/partial/missing/zero-mean bins break adjacency. |
| 11 | Training/holdout | Frozen original training estimates; separate diagnostic partition. Training simulations are not refitted and never labeled a calibrated bootstrap. Repeated holdout inspection is not independent final confirmation. |
| 12 | Process intervals | Discrete equal-tail Poisson quantiles, mode-centered stable recurrence, no normal approximation/negative bounds; means 0..80000, levels .80.. .99. |
| 13 | Parameter-aware prediction | Separate conservative frequentist Chernoff confidence-set projection with explicit Bonferroni/process error allocation; constant and predefined profiles. No arbitrary widening or model-uncertainty claim. |
| 14 | Interval coverage | Eligible disjoint-bin evaluated/inside/below/above counts, observed fraction and mean width for each interval method. No-exposure/unestimated bins are excluded, not observed zeros. |
| 15 | Simulation checks | Unchanged generator 2 called on observed homogeneous intensity fragments at 1 Hz; explicit domain-separated root/fragment seed plan, actual aggregation, bounded count realization and exact replication count. |
| 16 | Monte Carlo meaning | Inclusive (1+ties)/(R+1) ranks, eligible sample counts, empirical reference quantiles, raw-fraction SE and approximate Wilson 95% precision interval. No exact/uniform p-value or automatic multiplicity-based rejection. |
| 17 | Statistical limitations | Conditional fitted-parameter checks, unrefitted training reference, finite counts/PRNG/numerics, shared parameter uncertainty, possible mean mismatch, unadjusted multiplicity and unverifiable predefinition assertions. |
| 18 | Native workflow | Source selection/frozen rate/confidence display, partition/bin/threshold/horizon/level/seed settings, verified reimport, Run/Cancel, source navigation and retained metadata. |
| 19 | Charts | Actual versus integrated expected counts, signed residuals/gaps, cached synthetic dispersion histogram with observed marker, process/parameter bounds against actual holdout counts. Explicit UTC/count/residual axes and legends; narrow scrolling view inspected. |
| 20 | Study History | Retain/select/title/notes/export/delete diagnostics alongside existing evidence; embedded source survives linked calibration deletion. Combined existing limits enforced. |
| 21 | Async/cancellation | Reuses calibration worker/ExecutionControl; truthful phases and completed/processed counters. Tests preserve previous report/history, active snapshot and saved bytes; active diagnostic overlaps Save/Open safely and never publishes into a replaced workspace. |
| 22 | Headless | --headless --diagnose-demand with version-1 structured request and the existing exclusive JSON/CSV output flags; no graphics creation. |
| 23 | Exports | Complete versioned JSON with frozen source, coverage/split/model/seed/method provenance and numerical validity. Typed 25-column CSV tables for observations/residuals/intervals/reference statistics/coverage/summary; undefined values blank/null. |
| 24 | Persistence | Streaming project v2 per-report collection and demand_diagnostics_v1 feature. Canonical CRC, transactional reconstruction, atomic Save, recovery and old v1/v2 fixtures preserved. No whole-history project DOM. |
| 25 | Numerical tests | Independent direct PMF sums/quantile boundaries through mean 80000; zero/small/moderate means, known intervals, heterogeneous means, sparse/zero/invalid bins, residual degrees of freedom, threshold expectation, correlation, projection and predictive coverage. |
| 26 | Simulation validation | Independent JavaScript BigInt/Knuth fixture with explicit counts/seeds; fixed-seed moments, same/different seed plans, correct rates/exposure, zero intensity, boundaries, exact counts, limits, cancellation and report round-trips. |
| 27 | Leakage | Holdout changes leave trained count/exposure/rate/confidence and training-defined settings unchanged; frozen training references unchanged. Wrong fingerprints/derived-source tampering reject; no holdout refitting. |
| 28 | Debug totals | All 741 cases passed across full-suite execution and documented isolated rechecks. Final full CPU 721/721; final native diagnostic walkthrough passed on the refined UI. See transparent run record below. |
| 29 | Release totals | All 741 cases passed: 721 CPU plus 20 graphics across full partition runs and documented isolated rechecks. Final native rechecks 3/3. |
| 30 | Renderer-disabled | Final full suite 701/701 passed. |
| 31 | Vulkan | Successful Debug/Release native workflows, fresh-process Open, resize/minimize/restore and shutdown require zero errors/warnings. Actual final Review logs report 0/0 twice. |
| 32 | Compute | Raw Release timings include 100/1000/1024 bins, 100/1000 replications where allowed, daily/gap models, quantiles, parsing/serialization, one-million-record verified reimport and cancellation. Measurements below. |
| 33 | Memory/project size | Cumulative core-benchmark peak 72.61 MiB, largest diagnostic 1,023,521 B and retained project 1,205,684 B; source-only growth and stage timings recorded. Native main-thread CPU and wall frame measurements are distinct. |
| 34 | Native screenshots | Eight unchanged actual framebuffer PNGs, maximum 240,477 B, visually inspected; six tutorial views, restored metadata and 640-pixel prediction view. |
| 35 | Known limitations | Conditional/descriptive references, no refitted training bootstrap/model uncertainty, conservative envelopes, UTC-second/fixed-profile assumptions, support limits, other platforms unverified. Two native startup timeouts were not reproduced; records below. |
| 36 | Next milestone | Refit-calibrated bootstrap diagnostics and an explicitly independent final-test partition. Recommended only; not started. |

## Build/test run record

Windows Debug, Release and renderer-disabled builds all succeeded. No compiler errors or warnings appear in final build logs. Existing generator algorithms/versions, warehouse event/service/resource semantics, snapshots and paired/statistical/sensitivity/interaction/confirmation procedures were not changed.

| Run | Result | Elapsed |
|---|---|---:|
| Debug complete CTest suite | 740/741 initially; one existing forced-failure-artifact smoke timed out | 779.53 s |
| Debug identical failed-case retry | 1/1 passed, unchanged application/test | 3.18 s |
| Final Debug full CPU suite, isolated from other configurations | 721/721 passed | 23.13 s |
| Final Release full CPU suite | 721/721 passed | 6.60 s |
| Release complete graphics partition | 19/20 initially; existing higher-capacity smoke timed out | 275.37 s |
| Release identical failed-case retry | 1/1 passed, unchanged application/test | 4.30 s |
| Final Release native rechecks after final instrumentation | 3/3 passed: intentional artifact failure, diagnostic workflow, higher-capacity smoke | 19.75 s |
| Final renderer-disabled complete suite | 701/701 passed | 26.55 s |
| Latest Release native diagnostic walkthrough after calendar guard | 1/1 passed | 13.70 s |

The Debug/Release native timeouts each occurred once in existing smoke cases and were not reproduced in isolated reruns. Their original console output did not identify a code cause; no unrelated renderer or timeout change was made. Successful native exits enforce zero Vulkan errors/warnings. All 20 native cases in each renderer-enabled configuration have passed.

One additional parallel cross-configuration CPU verification hit the existing sensitivity persistence fixture's shared fixed temporary filename (`simforge-sensitivity-test.simforge`), causing the correct exclusive-save rejection (Windows error 183). The isolated case passed in .17 s, then the entire final Debug CPU suite passed 721/721 in isolation. This was a test invocation conflict; no save-overwrite policy or existing fixture behavior was weakened. New diagnostic fixtures use unique owned temporary directories.

The final numerical refinements included completed-report export admission, stronger cancellation/Save/Open evidence-preservation checks and a calendar-overflow admission guard. After the last guard, all 48 diagnostic CPU tests passed in each configuration: Debug (3.62 s), Release (.73 s) and renderer-disabled (3.60 s). The complete CPU suites above preceded only that guard; all other refinements are included in those suites. Native-only refinements (source navigation/rate display and main-thread CPU instrumentation) were rebuilt and exercised in the actual diagnostic walkthrough and final native rechecks. Unaffected native cases were already covered by complete partition runs.

Local logs: `build/diagnostics-full-debug-tests.log`, `build/diagnostics-debug-failure-recheck.log`, `build/diagnostics-final-debug-isolated-tests.log`, `build/diagnostics-final-release-cpu-tests.log`, `build/diagnostics-final-release-graphics-tests.log`, `build/diagnostics-release-graphics-recheck.log`, `build/diagnostics-final-native-rechecks.log`, `build/diagnostics-final-headless-tests.log`, `build/diagnostics-final-native-review.log`, `build/diagnostics-last-debug-tests.log`, `build/diagnostics-last-release-tests.log`, `build/diagnostics-last-headless-tests.log`, `build/diagnostics-last-native-tests.log`. Test selection uses CTest's graphics label; Release has no test preset and uses `--test-dir build/windows-release -C Release`.

## Statistical and reproducibility evidence

The [independent reference fixture](../tests/fixtures/demand-diagnostics-reference.json) retains 72 synthetic counts for each of three independently computed root streams. Base seed 42 yields roots 5589919048962539584, 11893985631523087576 and 3618112348595607274. Their Pearson sums are 66.8, 67.8 and 87.6; dispersion divides by 72; zero counts are 1/1/0 and threshold-10 tail-bin counts 0/0/4. The C++ implementation is checked against these external calculations.

A fixed 1000-replication moment fixture compares mean reference dispersion with one using a seven-standard-error region from Poisson squared-residual moments. Distribution regressions from Commit 31 remain included. Constant/profile/gap/zero/sparse fixtures cover exposure and boundary ownership; no outcome-driven seed search or uncontrolled test randomness is used.

All five example requests produced identical complete statistical evidence across Debug, Release and renderer-disabled builds (15 executions, elapsed timings excluded). Content-derived diagnostic IDs also matched. The owned temporary cross-build exports were reclaimed. A directory-local Git attribute preserves LF bytes in the five original example CSVs so Windows checkout conversion cannot invalidate their recorded SHA-256 fingerprints; a forced Windows-style checkout was hash-verified.

The five [curated example requests and numerical reports](../examples/demand-diagnostics) record actual results. Poisson-generating A's unusual dispersion is retained rather than replaced; D demonstrates intensity-aware criticism, and E distinguishes missing exposure from later distribution shift. See the [method document](demand-diagnostics.md) for exact observed values, mathematical assumptions and primary references.

## Measurements

[Raw Release CSV](demand-diagnostics-benchmarks.csv) records the implemented Release workloads. Preparation at 100 bins was 1.51..1.91 ms; at 1000 bins 14.93..15.24 ms; at 1024 bins about 15.0 ms. Total diagnostic execution was 18.18 ms for 100 bins × 100 replications, 102.96 ms for 100 × 1000, 798.82 ms for 1000 × 1000, and 153.89 ms for 1024 × 100. The maximum bins × 1000 case is rejected by the documented one-million-fragment-invocation bound.

The six-parameter varying-rate example with 1000 replications took 183.33 ms total; the 70-window gap example took 100.91 ms. A separate one-million-record verified reimport took 135.10 ms; full 1024-bin/100-replication analysis including its reimport took 303.95 ms. One hundred process intervals at mean 80000 took 5.98 ms. No matched baseline or speedup claim is made.

Largest measured diagnostic JSON was 1,023,521 bytes; its project was 1,205,684 bytes, increasing source-only project size by 1,023,670 bytes. That case saved/opened in 51.35/115.29 ms. Cumulative process peak reached 76,136,448 bytes (72.61 MiB), including million-record input/reimport buffers; this is not isolated stage allocation or GPU memory. Cooperative cancellation measured at the synthetic-reference boundary ranged .0164.. .0868 ms; codec/I/O/generator safe-boundary latency is not universally bounded by that measurement.

[Native Debug measurements](demand-diagnostics-native-benchmarks.csv) came from 155 actual nongated active frames while checking 210 historical observations, eight valid holdout bins and 1000 replications. Mean/max wall frame time was 4.117/6.947 ms, including presentation/scheduling; mean diagnostic UI construction was 53.11 microseconds. Actual owning-thread kernel+user CPU time averaged 2.016 ms/frame, measured with Windows GetThreadTimes, excluding worker CPU and GPU/presentation waits. Playback time remains fixed during the diagnostic parity walkthrough. Hardware is the selected NVIDIA GeForce RTX 5080 Laptop GPU; supported Windows/MSVC generator reproducibility remains as documented in Commit 31.

## Actual native evidence

Final Review source: `8ebb166139134f1a8e46f76de75709df`. Its 28-step SDL workflow covers source selection/configuration, Run/Cancel/progress, counts/residuals/reference/prediction views, CPU parity, source navigation, report retention/metadata, JSON/CSV export, wrong-fingerprint rejection, project Save/Open and fresh-process reopen, 640/1280/1920 resize, minimize/restore and shutdown. Both successful processes report Vulkan validation 0/0.

- [Setup/frozen rate/confidence](artifacts/diagnostics-setup.png)
- [Observed/expected counts](artifacts/diagnostics-counts.png)
- [Signed residuals and gaps](artifacts/diagnostics-residuals.png)
- [Actual seeded reference distribution](artifacts/diagnostics-reference.png)
- [Distinct predictive uncertainties](artifacts/diagnostics-prediction.png)
- [Conservative summary/provenance](artifacts/diagnostics-summary.png)
- [Fresh-process restored metadata/evidence](artifacts/diagnostics-restored.png)
- [640-pixel prediction view with scrolling](artifacts/diagnostics-compact.png)

PNG files are copied unchanged from real framebuffer captures. Test-mode temporary runs are reclaimed using ownership/path checks; bounded Review evidence remains locally. Only curated captures, raw benchmark CSVs and bounded analytical reference/example reports are committed. No generated full projects, million-row inputs or cross-build temporary exports are committed.

## Exact changed-file inventory

17 modified files and 44 added files (61 total).

```text
apps/sandbox/calibration_panel.cpp
apps/sandbox/calibration_panel.hpp
apps/sandbox/CMakeLists.txt
apps/sandbox/demand_diagnostics_diagnostic.inl
apps/sandbox/diagnostics_panel.cpp
apps/sandbox/diagnostics_panel.hpp
apps/sandbox/main.cpp
apps/sandbox/study_history_panel.cpp
apps/sandbox/viewer.cpp
benchmarks/CMakeLists.txt
benchmarks/demand_diagnostics_benchmark.cpp
docs/artifacts/diagnostics-compact.png
docs/artifacts/diagnostics-counts.png
docs/artifacts/diagnostics-prediction.png
docs/artifacts/diagnostics-reference.png
docs/artifacts/diagnostics-residuals.png
docs/artifacts/diagnostics-restored.png
docs/artifacts/diagnostics-setup.png
docs/artifacts/diagnostics-summary.png
docs/commit-32-validation.md
docs/demand-diagnostics.md
docs/demand-diagnostics-benchmarks.csv
docs/demand-diagnostics-native-benchmarks.csv
engine/CMakeLists.txt
engine/include/simforge/analysis/demand_diagnostics.hpp
engine/include/simforge/workspace/calibration_execution.hpp
engine/include/simforge/workspace/scenario_workspace.hpp
engine/src/analysis/demand_diagnostics.cpp
engine/src/analysis/demand_diagnostics_json.cpp
engine/src/workspace/calibration_execution.cpp
engine/src/workspace/project.cpp
engine/src/workspace/project_io.inl
engine/src/workspace/study_history.cpp
examples/demand-diagnostics/gaps-and-shift.csv
examples/demand-diagnostics/gaps-and-shift.json
examples/demand-diagnostics/.gitattributes
examples/demand-diagnostics/gaps-and-shift-calibration.json
examples/demand-diagnostics/gaps-and-shift-diagnostics.json
examples/demand-diagnostics/overdispersed.csv
examples/demand-diagnostics/overdispersed.json
examples/demand-diagnostics/overdispersed-calibration.json
examples/demand-diagnostics/overdispersed-diagnostics.json
examples/demand-diagnostics/poisson-compatible.csv
examples/demand-diagnostics/poisson-compatible.json
examples/demand-diagnostics/poisson-compatible-calibration.json
examples/demand-diagnostics/poisson-compatible-diagnostics.json
examples/demand-diagnostics/time-varying.csv
examples/demand-diagnostics/time-varying.json
examples/demand-diagnostics/time-varying-calibration.json
examples/demand-diagnostics/time-varying-diagnostics.json
examples/demand-diagnostics/underdispersed.csv
examples/demand-diagnostics/underdispersed.json
examples/demand-diagnostics/underdispersed-calibration.json
examples/demand-diagnostics/underdispersed-diagnostics.json
README.md
scripts/generate-demand-diagnostics.cpp
scripts/run-graphics-test.ps1
tests/CMakeLists.txt
tests/demand_diagnostics_cli_test.ps1
tests/demand_diagnostics_test.cpp
tests/fixtures/demand-diagnostics-reference.json
```
