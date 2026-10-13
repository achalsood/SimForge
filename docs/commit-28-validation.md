# Commit 28 implementation and validation report

Validated on 2026-10-13 in `D:\Projects\SimForge`. Starting HEAD was `be88345731fd94e75c94b033a740ff9929e51ab8`, with a clean working tree. The local commit message is `feat(analysis): add study history and independent confirmation`. No remote push, publication, release or subsequent milestone is part of this change.

## Requested delivery checklist

| # | Item | Result |
|---:|---|---|
| 1 | Commit and Git status | Local commit follows completed validation; its SHA is reported in the final response. Intended final working tree is clean. |
| 2 | Changed files | 49 files; inventory below. |
| 3 | Unified record | `StudyRecord` metadata and presentation around shared immutable `Comparison` evidence; one history collection. |
| 4 | Study types | Single-seed comparison, paired comparison, sensitivity, interaction, independent confirmation. |
| 5 | Stable identity | Project-scoped monotonic `study-N`, persisted counter, no ID reuse or array-position identity. |
| 6 | Capacity/retention | 32 studies; 32 MiB evidence each; 192 MiB history charge; 10,100 per-seed outcomes; 256 MiB project. No eviction. Pending evidence can be exported, explicitly retained or explicitly discarded. |
| 7 | Naming/notes/deletion | Editable metadata; deliberate deletion; source deletion blocked by dependants; reservation audit retained after confirmation deletion. |
| 8 | Publication | Simulation, aggregation, typed report validation, complete provenance and retention succeed before ownership publication. Failure preserves history. |
| 9 | Persistence/migration | Format 2 canonical history, selected ID, views and reservations. Format 1 single-report migration; unsupported future versions rejected. |
| 10 | Recovery | Multiple reports, unsaved metadata, deletion and cancelled reservations restore transactionally; no resumed worker or fabricated completion. Near-limit recovery cleanup also validated. |
| 11 | Eligibility | Fresh stochastic resource-capacity comparisons; resource sweeps/grids; reproducible matched single-seed sources. Workload factors, checkpoints and deterministic-only stochastic confirmation rejected. |
| 12 | Frozen ownership | Exact submitted baseline/candidate definitions owned by the request; source/editor/selection changes cannot rebind them. |
| 13 | Seeds | Checked `sequential_v1` or explicit order; no duplicates, overflow or exploratory overlap. Single-seed sources exclude all candidates' exploratory seeds. Reservations survive cancellation, failure, deletion and reopening. |
| 14 | Pairing | Existing paired engine and unchanged exogenous streams; arrival ticks and required processing durations checked against direct simulations. |
| 15 | Statistics | Existing paired Student-t kernel and NIST fixtures; candidate minus baseline; lower-is-better separately shown; nominal 95% caveats; missing statistics remain unavailable. |
| 16 | Repeated confirmation | At most 32 reserved attempts per source. First untouched attempt versus explicitly labelled additional analyses; no reservation reuse. |
| 17 | Estimate comparison | Actual exploratory/confirmation estimates, their difference, direction agreement and zero-including interval diagnostic. No pooling or guarantees. |
| 18 | History UI | Compact list, typed result restoration, title/notes, deletion popup, selected/history export, source and child navigation. |
| 19 | Confirmation UI | Freeze tested selection, count, deterministic/explicit seeds, overlap diagnostic, Run, frozen seeded playback and evidence interpretation. |
| 20 | Async/cancellation | Existing one-worker service, actual seed/phase/tick progress, cancellation and shutdown; no partial completed report. |
| 21 | Headless | Schema-5 inline frozen requests; shared validation/statistical execution; four real curated examples and CLI contract checks. |
| 22 | Exports | Study/history JSON; typed confirmation JSON; summary and per-seed CSV with source/selection identity and retained study ID where applicable. Exclusive destinations. |
| 23 | Numerical references | Existing independent Student-t fixtures, edges, quantiles, undefined/missing pairs, sign and favorable-direction checks remained green. No new statistical formula. |
| 24 | History/migration tests | Insertion, all types, repeated switching, identity, metadata, explicit deletion, limits/pending resolution, per-study views, migration, reordering, invalid projects and recovery. |
| 25 | Confirmation tests | Disjointness, duplicates, order/overflow, complete source seeds, frozen editor/selection, paired workloads, no leakage, deterministic reruns, cancellation, reservation persistence, lineage and corrupted inputs. |
| 26 | Debug | **533/533**, all 16 graphics checks included. Final local refinements additionally passed **47/47** targeted checks. |
| 27 | Release | **533/533**, all 16 graphics checks included; final native timing instrumentation additionally checked through the study graphics test. |
| 28 | Renderer-disabled | **497/497**. |
| 29 | Vulkan | **0 errors / 0 warnings** in both final review processes; validation and synchronization validation enabled. NVIDIA RTX 5080 Laptop GPU selected. |
| 30 | Performance | One/ten/moderate/multiple-large/near-limit histories; 20/100-seed confirmations; insertion, switching, Save/Open/recovery, deletion, submission/cancellation and real active viewer frames. CSV links below. |
| 31 | Memory/sizes | Near limit: 20 large grids, 201,181,771 project bytes (191.86 MiB), 201,183,117 charged history bytes, 1,866,362,880 peak working-set bytes (about 1.74 GiB). |
| 32 | Native screenshots | Eight bounded PNGs; inspected history/setup/overlap/results/lineage/playback/compact/restored views at 1600×1100 and 1280×720. Resize/minimize/restore also exercised. |
| 33 | Limitations | Large JSON histories allocate temporary DOM/validation memory and pause Save/Open. IDs/reservations are project scoped; standalone JSON import/history CLI manipulation and arbitrary workload coupling are deferred. |
| 34 | Next milestone | Measure streaming persistence and memory budgeting, then demand-model calibration. No next milestone was started. |

## Validation commands and evidence

Visual Studio x64 environment initialization was required for the cached MSVC compiler to find standard headers. The repository presets were retained:

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
```

The Debug full run took 254.29 s, Release 180.13 s and the final renderer-disabled run 81.14 s. The new coverage adds 39 core study cases, five CLI cases and one opt-in native graphics workflow. Existing simulation, replay, statistical, sensitivity, interaction, language, persistence, custom playback and artifact tests remain green. Full suites were followed by focused checks for final refinements and the native timing diagnostic; their logs are retained locally under `build/commit28-*.log`.

The final native review retained three exploratory types, switched through each result renderer, renamed/annotated a record, froze a grid cell, rejected overlap, displayed actual progress, cancelled, reserved fresh seeds, completed confirmation, compared paired intervals, ran a measured seed, saved, reopened in a fresh process, and exercised dependency-aware deletion and resize/shutdown. Both processes reported validation counters 0/0. The ungated confirmation had **38 active frames averaging 4.09148 ms CPU including presentation**. Whole-workflow and reopened averages were 6.41728 ms and 4.09863 ms. These are CPU timings, not GPU timers.

## Storage and execution measurements

[Measured CSV](study-history-benchmarks.csv), [near-limit CSV](study-history-near-limit.csv), and [architecture, interpretation and limitations](study-history-and-confirmation.md) contain the complete context. Prior benchmark files are retained and no unmatched comparison is used to claim a performance improvement or regression.

| Case | Project size | Insertion/submission | Save/execution | Load | Recovery | Deletion |
|---|---:|---:|---:|---:|---:|---:|
| One paired report | 68,580 B | — | 4.59 ms | 10.50 ms | 3.84 ms | 0.015 ms |
| Ten moderate paired reports | 654,655 B | 43.24 ms total additional insertion | 28.84 ms | 46.27 ms | 27.43 ms | 0.023 ms |
| Two large interaction grids | 20,121,293 B | 1,061.92 ms additional insertion | 1,280.80 ms | 1,936.31 ms | 1,327.54 ms | 0.583 ms |
| Near history limit: 20 large grids | 201,181,771 B | 21,215.30 ms total additional insertion | 12,743.08 ms | 18,259.66 ms | 12,858.98 ms | 0.714 ms |
| 20-seed confirmation, short scenario | — | 0.315 ms | 15.14 ms | — | — | — |
| 100-seed confirmation, short scenario | — | 0.355 ms | 58.61 ms | — | — | — |

Switching immutable shared evidence measured below 0.0001 ms display precision over 10,000 switches. Cancellation measured 0.071/0.025 ms in the short scenario runs and published no partial report. Near-limit recovery was 201,181,797 bytes. The next large history insertion failed without eviction. Peak memory is a cumulative process high-water mark; the CSV history-byte column measures serialized storage charge, not compact heap ownership. Temporary JSON processing accounts for much of the high-water memory.

## Changed-file inventory

* Engine (11): `engine/CMakeLists.txt`; `engine/include/simforge/analysis/confirmation.hpp`; `engine/include/simforge/workspace/{project,scenario_workspace}.hpp`; `engine/src/analysis/confirmation.cpp`; `engine/src/scenario/json_io.{cpp,hpp}`; `engine/src/workspace/{experiment_execution,project,scenario_workspace,study_history}.cpp`.
* Desktop/CLI (8): `apps/sandbox/CMakeLists.txt`, `main.cpp`, `statistical_panel.cpp`, `viewer.cpp`, `workspace_panel.{cpp,hpp}`, `study_history_panel.{cpp,hpp}`.
* Benchmarks (2): `benchmarks/CMakeLists.txt`, `benchmarks/study_history_benchmark.cpp`.
* Tests (10): `tests/CMakeLists.txt`, `async_experiment_test.cpp`, `interaction_test.cpp`, `playable_run_test.cpp`, `project_test.cpp`, `sensitivity_test.cpp`, `statistical_test.cpp`, `workspace_test.cpp`, `study_history_test.cpp`, `confirmation_cli_test.ps1`.
* Native runner (1): `scripts/run-graphics-test.ps1`.
* Examples (4): `examples/warehouse-confirmation-{forklift,sensitivity,interaction,inconclusive}.json`.
* Documentation/artifacts (13): `README.md`, this report, `docs/study-history-and-confirmation.md`, the two CSVs, and `docs/artifacts/study-{history,confirmation-setup,seed-overlap,confirmation-progress,confirmation-results,playback,compact,restored}.png`.
