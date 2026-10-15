# Commit 30 delivery and validation

> Publication curation: additional captures cited in this engineering record remain in Commit #33. See [screenshot history and recovery](screenshot-gallery.md#historical-captures).

Validated on October 14, 2026 in `D:\Projects\SimForge`. Starting HEAD was `bbd6f865046d6a7394dc74d815e25d67fb7e4b62` and the working tree was clean. The requested local commit message is `feat(workloads): add demand calibration and holdout validation`. No push, publication, release or subsequent milestone is part of this change.

## Delivery checklist

| # | Requested item | Delivered behavior / evidence |
|---:|---|---|
| 1 | Commit SHA | Reported in the completion response; a commit cannot include its own SHA. |
| 2 | Working tree | Reviewed before the local commit; final status reported in the completion response. |
| 3 | Files changed | **58 files: 19 modified, 39 added**, including eleven PNGs. Core demand/worker/codec/native/CLI integration, tests/fixtures/generators/benchmarks and documentation. Exact paths are listed below. |
| 4 | Architecture | Immutable dataset snapshots → chronological partitions → training-only count/exposure rates → holdout metrics → immutable report → explicitly reviewed periodic scenario → normal engine. CPU-only fitting is independent of rendering. |
| 5 | CSV / timestamps | One `timestamp` column, strict UTC `YYYY-MM-DDTHH:MM:SSZ`, second resolution, Gregorian validation, 1970–2099 arrivals. Optional UTF-8 BOM, CRLF/LF and complete quoted fields. No ambiguous formats or automatic rounding. |
| 6 | Exposure | One user-asserted continuous `[start,end)` window, maximum 366 days; no inferred exposure. Unknown coverage, gaps/closures and disjoint windows unsupported. |
| 7 | Data quality | All-or-nothing row-numbered validation; out-of-order input stably normalized and reported; duplicates retained. Header-only zero observations valid. Size/record/field/column/time limits enforced. |
| 8 | Provenance | SHA-256 content digest with known vectors; deterministic dataset identity also includes import/exposure policy. Filename basename, schema/software versions, counts, ordering and policies retained. No private absolute import paths or raw datasets stored in reports. |
| 9 | Split | Explicit chronological boundary strictly inside exposure. Split/end ownership tested. Holdout never influences rates, intervals, bin definitions or scenario conversion. |
| 10 | Constant estimator | Training count / training exposure seconds; UI arrivals/hour, exports arrivals/second. |
| 11 | Piecewise estimator | Repeating UTC daily bins, widths 1800..86400 dividing 86400, actual training exposure aggregated across days/partial days. At least one full day of training, sparse warnings, at most 48 rates. |
| 12 | Uncertainty | Conservative marginal 95% Poisson Chernoff inversion, including zero and low counts. Nonnegative finite endpoints; independent root fixtures and enumerated probability coverage checks. Parameter intervals are not prediction intervals or simultaneous coverage. |
| 13 | Holdout metrics | Observed/expected count, signed and absolute errors, signed error/hour, bin residuals, joint bin-count Poisson deviance/log likelihood; positive observation with zero prediction exports null plus explicit status. Constant baseline uses identical partitions. |
| 14 | Selection | Manual only; repeatedly used holdout is labeled validation/model-selection data rather than independent final-test confirmation. |
| 15 | Statistical limits | Count-process assumptions are stated; burst/spike/variation checks are descriptive. No claim that total-count accuracy proves pattern fidelity or that calibration guarantees future operations. |
| 16 | Arrival generator | Existing generator v1 unchanged. Periodic constant/profile and seeded discrete-uniform semantics audited; no Poisson/exponential generator was added or mislabeled. |
| 17 | Quantization | Explicit periodic reciprocal-rate rounding to nearest integer second, ties up; rate differences and actual finite-horizon count previewed. Segment-start arrivals/phase resets retained; unsupported reciprocals or segment/event bounds reject conversion. |
| 18 | Scenario conversion | New named scenario, explicit seed/UTC alignment, validated by authoritative validator. Source horizon/tick rate/resources/service settings preserved. User must accept periodic surrogate semantics. Source/new definitions and fitted/represented rates retained. |
| 19 | Reproducibility | Normal deterministic workload/RNG streams and snapshots preserved. Native runtime snapshot/metrics compared to an independently executed same-seed headless simulation. Cross-platform SHA/PRNG integer operations are specified; cross-platform bitwise libm-derived calibration statistics are not newly claimed or tested. |
| 20 | Native UI | Analysis header opens Demand Calibration, with Import, Model & split, Validation, Scenario & exports views. Import and fitting are separate actions over an owned snapshot. |
| 21 | Charts / tables | Observed counts, fitted rates, holdout observed/expected and residual charts; exposure-proportional UTC axes for partial intervals, hover details, rate/interval tables and clipped numerical bins. No smoothing or fabricated events. |
| 22 | Study History | Shared immutable calibration evidence alongside existing study families; selected calibration and conversion lineage retained. Combined 32-study/192-MiB bounds and explicit deletion. History export includes calibration evidence. |
| 23 | Async / cancellation | One calibration worker, real phases and cooperative checkpoints; immutable owned requests/data. Failure/cancellation preserves previous successful evidence/simulations. Save shares captured evidence; later revisions remain dirty. Open defers publication and joins the replaced worker. |
| 24 | Headless | `--headless --calibrate-demand REQUEST.json`; request-relative CSV paths and strict schema. Optional reviewed periodic scenario export with source, seed, UTC alignment and explicit acceptance. Same core as GUI. |
| 25 | JSON / CSV | Versioned JSON includes complete evidence/provenance/assumptions/conversions. CSV parameter, interval and metric records carry IDs/digest and explicit units. Output paths must be exclusive new files; no missing metric becomes zero. |
| 26 | Persistence | v2 optional calibration feature with streamed per-report emission and bounded per-report reconstruction/discard. Derived values/conversions recomputed and canonical integrity checked; transactional loading/recovery and v1/v2 fixtures retained. No raw timestamp persistence or whole-project copies. |
| 27 | Numerical tests | Count/exposure units, short/long exposure, Poisson roots/coverage, independent error/deviance/likelihood fixtures, zero predictions, UTC calendars, daily/partial exposure and sparse bins. Maximum accepted million-record dataset and over-limit rejection tested. |
| 28 | Leakage tests | Constant and daily models invariant to changed holdout records; same rate/interval/edges/workload, distinct holdout metrics, nonoverlapping exposure and each-event ownership. |
| 29 | Generator tests | No new generator; unchanged workload baseline/streams/ordering/snapshot/paired tests plus converted periodic counts, zero gaps, rate rounding, UTC boundary alignment, service preservation and snapshot replay tests. |
| 30 | Debug totals | **643/643 passed**, 788.23 seconds including all 18 opt-in graphics tests. |
| 31 | Release totals | **643/643 passed**, 279.27 seconds including all 18 opt-in graphics tests. |
| 32 | Renderer-disabled totals | **605/605 passed**, 27.77 seconds. |
| 33 | Vulkan validation | Calibration Save/reopen walkthrough: **0 errors / 0 warnings**, including renderer shutdown/reinitialization. Complete Debug and Release graphics suites passed. |
| 34 | Performance | 10k/100k/1M core import/fit/holdout/serialization/Save/Open/simulation/cancellation measurements; maximum native-import frame measurements. [Core CSV](demand-calibration-benchmarks.csv), [native CSV](demand-calibration-native-benchmarks.csv). |
| 35 | Memory / project impact | Release process peak 49.0 MiB at maximum records; approximately 152–163.5 KiB project increase for 720-bin evidence and conversion. High-water process measurement includes previous cases. Raw timestamps excluded from projects. |
| 36 | Native screenshots | Eleven real, bounded PNGs below, inspected at 640, 1280, 1600 and 1920 pixel widths. Actual imported/fitted/runtime data; no fabricated results or image generation. |
| 37 | Known limits | Continuous UTC seconds only, maximum bounds, manual models, marginal assumption-dependent uncertainty, descriptive burst diagnostics, periodic surrogate conversion. No Poisson simulation, disjoint coverage, service calibration, forecasting guarantee or automatic optimization. |
| 38 | Recommended next milestone | Audit/add a seeded statistical count-process arrival generator and explicit observation windows, with deterministic resolution/distribution tests. This recommendation is not automatically begun. |

## Commands and numerical scope

Configured and built `windows-debug`, `windows-release` and the existing `windows-headless` preset using the repository's Visual Studio environment setup. Final tests use:

```powershell
ctest --preset windows-debug --output-on-failure -j 6
ctest --test-dir build/windows-release --output-on-failure -j 6
ctest --preset windows-headless --output-on-failure -j 6
```

All original tests remain enabled. The new family contains 52 core cases, four curated headless cases, one CLI contract test and one native walkthrough. Existing source-level checks continue to cover determinism, workload streams, replay, editor/validator, statistical comparisons/sweeps/interactions, independent confirmation, history, cancellation, legacy projects/streaming persistence, natural-language interpretation and graphics lifecycle.

The initial Debug run exposed three harness/layout issues: absent artifact-log directory, relative fixture lookup under the CTest working directory, and a new analysis row clipping existing native sensitivity/interaction controls. The fixture path/log setup and header placement were corrected. The four affected native cases passed together before the final complete runs. No original test was disabled or weakened.

## Recorded measurements

Windows x64, MSVC toolchain, Release CPU benchmark and NVIDIA GeForce RTX 5080 Laptop GPU native Vulkan walkthrough. Fixture generation is outside timed calibration. Times are wall measurements, not baseline comparisons.

| Records | Model | Import ms | Partition/fit ms | Holdout ms | Full workflow ms | Process peak MiB | Project increase KiB |
|---:|---|---:|---:|---:|---:|---:|---:|
| 10,000 | constant | 6.85 | 0.129 | 0.078 | 87.9 | 10.4 | 162.1 |
| 10,000 | UTC daily | 1.45 | 0.154 | 0.056 | 79.3 | 10.6 | 149.4 |
| 100,000 | constant | 28.9 | 0.454 | 0.043 | 155.6 | 12.0 | 162.7 |
| 100,000 | UTC daily | 18.7 | 0.345 | 0.037 | 119.1 | 12.2 | 151.7 |
| 1,000,000 | constant | 139.0 | 1.235 | 0.038 | 221.5 | 47.9 | 163.5 |
| 1,000,000 | UTC daily | 136.1 | 1.144 | 0.041 | 220.2 | 49.0 | 151.9 |

Every row has 30 days of continuous exposure, 20 days training and 720 chronological diagnostic bins. Constant has one parameter; UTC daily has 24. Cancellation plus join at a real checkpoint took 0.15–0.80 ms in this run, not a bound on every I/O stall. Serialization took 3.2–5.3 ms, Save 6.4–15.5 ms, Open 25.6–39.5 ms, and simulation 0.011–0.021 ms for the bounded 600-second benchmark scenario. Native maximum import: 59 active frames, 3.97-ms mean / 8.48-ms maximum, 62.7-microsecond mean calibration-panel build, 232-ms import. None of these values claims a speedup or GPU computation acceleration.

## Curated native captures

- **Import** (archived capture: `demand-import.png`), **coverage/data quality and counts** (archived capture: `demand-quality.png`), **model/split** (archived capture: `demand-model.png`).
- [Holdout counts/residuals](artifacts/demand-holdout.png), **fitted daily profile** (archived capture: `demand-profile.png`), **reviewed periodic conversion** (archived capture: `demand-conversion.png`).
- **Actual calibrated simulation** (archived capture: `demand-simulation.png`), **restored project evidence** (archived capture: `demand-restored.png`).
- **640-wide compact view** (archived capture: `demand-compact.png`), **1280-wide validation** (archived capture: `demand-validation-1280.png`), **1920-wide validation** (archived capture: `demand-validation-1920.png`).

Each PNG is below 222 KiB. Managed test runs reclaim owned temporary outputs; review runs are bounded by the existing artifact manager. The owned million-row native performance fixture is deleted after import. Only curated documentation PNGs and compact measured CSVs are versioned.

## File inventory

```text
apps/sandbox/calibration_diagnostic.inl
apps/sandbox/calibration_panel.cpp
apps/sandbox/calibration_panel.hpp
apps/sandbox/CMakeLists.txt
apps/sandbox/main.cpp
apps/sandbox/save_dialog.cpp
apps/sandbox/save_dialog.hpp
apps/sandbox/study_history_panel.cpp
apps/sandbox/viewer.cpp
apps/sandbox/workspace_panel.cpp
apps/sandbox/workspace_panel.hpp
benchmarks/CMakeLists.txt
benchmarks/demand_calibration_benchmark.cpp
docs/artifacts/demand-compact.png
docs/artifacts/demand-conversion.png
docs/artifacts/demand-holdout.png
docs/artifacts/demand-import.png
docs/artifacts/demand-model.png
docs/artifacts/demand-profile.png
docs/artifacts/demand-quality.png
docs/artifacts/demand-restored.png
docs/artifacts/demand-simulation.png
docs/artifacts/demand-validation-1280.png
docs/artifacts/demand-validation-1920.png
docs/commit-30-validation.md
docs/demand-calibration.md
docs/demand-calibration-benchmarks.csv
docs/demand-calibration-native-benchmarks.csv
engine/CMakeLists.txt
engine/include/simforge/analysis/demand_calibration.hpp
engine/include/simforge/workspace/calibration_execution.hpp
engine/include/simforge/workspace/scenario_workspace.hpp
engine/src/analysis/demand_calibration.cpp
engine/src/analysis/demand_hash.cpp
engine/src/analysis/demand_internal.hpp
engine/src/analysis/demand_json.cpp
engine/src/workspace/calibration_execution.cpp
engine/src/workspace/project.cpp
engine/src/workspace/project_io.inl
engine/src/workspace/scenario_workspace.cpp
engine/src/workspace/study_history.cpp
examples/demand-calibration/constant.csv
examples/demand-calibration/constant.json
examples/demand-calibration/distribution-shift.csv
examples/demand-calibration/distribution-shift.json
examples/demand-calibration/malformed.csv
examples/demand-calibration/morning-surge.csv
examples/demand-calibration/morning-surge.json
examples/demand-calibration/sparse.csv
examples/demand-calibration/sparse.json
README.md
scripts/generate-demand-fixtures.ps1
scripts/generate-demand-fixtures.py
scripts/run-graphics-test.ps1
tests/artifact_failure_test.ps1
tests/CMakeLists.txt
tests/demand_calibration_cli_test.ps1
tests/demand_calibration_test.cpp
```
