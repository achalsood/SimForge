# Streaming project persistence

Commit #29 keeps the existing v2 JSON schema, snapshot formats and authoritative analysis results. Save, Open and automatic recovery use one bounded background operation. The same engine implementation powers renderer-free verification, benchmarks and synchronous APIs.

## What a save contains

A save captures one coherent workspace revision when submitted. The main thread copies small metadata, preferences, study views and mutable run snapshots. It shares `shared_ptr<const Comparison>` evidence; it never copies every grid into the save snapshot. The worker owns everything it reads. An analysis already running continues with its private inputs; only completed evidence published before submission belongs to the save. Playback, camera changes, editor changes and newly completed studies after submission remain unsaved. Success associates the filename and records the captured marker. The UI says “Saved submitted revision. Newer changes remain unsaved.” when appropriate.

The File menu serializes conflicting project actions. Save permits ongoing editing and playback. Open keeps rendering and camera navigation available while pausing playback, structural editing and analysis publication. Save-before-New/Open/Close also pauses advancement and defers analysis publication until the submitted save is accounted for; later changes require confirmation again. Saved markers and recovery notices are fully allocated in staged state before publication, including the synchronous APIs. A validated replacement is published on the main thread; SDL, Vulkan and ImGui stay on that thread. Empty saved result selections remain empty on restoration, preventing an automatic UI default from making an opened project dirty.

## Writer and reader

The writer emits sorted canonical payload fields directly to a 64 KiB file buffer. It encodes one run or study at a time, inserts the existing canonical report representation, and updates a table-based IEEE CRC-32 incrementally. The envelope writes the payload before its size/checksum fields; JSON field ordering has never been part of the schema. No complete project DOM, complete payload string or complete output string exists in the real Save/recovery path. The convenience `Project::serialize()` API returns a string by definition and uses the same emitter; native persistence does not call it.

Open reads through a bounded 64 KiB stream buffer. A nlohmann JSON parse callback strictly tracks duplicate keys, depth, events and strings. It converts each completed study and run into independently owned CPU state and discards that JSON subtree. Metadata remains small. Every report passes the existing raw-observation, derived-statistic, execution-count and provenance validators. Confirmation lineage, frozen inputs and reserved seed sets are checked across the completed staged history. No simulation is rerun to restore evidence.

Canonical per-field CRCs and lengths are combined using the same IEEE polynomial, in sorted payload-key order. This verifies the original v1/v2 `payload.dump()` checksum without constructing that string. Reordered fields, indentation, escaped Unicode and equivalent JSON spellings remain compatible; the canonical representation defines integrity. Binary snapshots retain their independent integrity/structural checks. CRC detects accidental corruption and does not authenticate an untrusted file.

Small report JSON subtrees transfer ownership instead of repeatedly copying observation arrays. Deserialization accepts already parsed study values and avoids dumping/reparsing each analytical report. Interaction and sensitivity reconstruction aggregate once before comparison with the saved derived values. Statistical formulas, raw metrics, seed ordering, intervals, ranks and limitations are unchanged.

## Publication, cancellation and recovery

Save exclusively creates a randomly named temporary file beside the destination, writes/checks every chunk, flushes and closes it, then calls `MoveFileExW` with `MOVEFILE_WRITE_THROUGH`. Replacement is enabled only for an associated Save or the recovery slot. Save As cannot overwrite an existing file, including a file created after the dialog closes. Errors include the Windows error code. A failed or cancelled prepublication operation closes its handle and removes only its owned temporary file, leaving the old destination and dirty marker intact.

Microsoft documents the buffer flush and move flags in [FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers) and [MoveFileExW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw). This is a same-directory replacement after a complete flushed write; it is not a guarantee against every power-loss, device-cache, network-share or filesystem failure. POSIX flushes the temporary file and renames/links it; directory fsync remains unimplemented. OS locks, access denial, short writes, flush failures and replacement failures are surfaced without an in-place rewrite.

Progress reports the filename, actual phase, processed bytes and completed studies. Serialization and writes interleave, so there is no invented percentage or time estimate. Cancel is cooperative at read/write chunks, parser checkpoints and study/run boundaries. Individual bounded report validation may finish before cancellation is observed. Before file replacement, cancellation and publication contend under one lock. Once replacement starts, Cancel is disabled; a successful publication is never reported as rolled back. A cancelled Open discards all staged state and leaves the active workspace unchanged, including its analysis worker, selections and dirty bookkeeping.

There is one persistence worker per Project and no operation queue. A second Save/Open is rejected while busy. Teardown cooperatively stops and joins it, accounts for any already completed file publication, then uses the optimized recovery path when required. The final shutdown flush can block after the window closes. Automatic recovery uses the same captured revision and streamed transactional writer, updates its marker only after success and waits until the project operation slot is free. Ownership checks also use bounded loading. Recovery Open restores an unsaved project without associating a primary filename.

## Bounds and compatibility

Input/output remains limited to 256 MiB, history to 192 MiB/32 studies, evidence to 32 MiB per study and snapshots to 32 MiB binary. The reader enforces a strict EOF, raw-NUL rejection, duplicate decoded-key rejection, depth 32, 36 million project events, four million events per study and at most 64 fields per object (all supported schemas fit). Unknown envelope/payload fields are rejected as their keys arrive. Existing scenario, numeric, boolean, enum, candidate, run, command, string, seed and identity limits still apply. Late CRC/EOF errors cannot publish partial state. No external assets or paths are followed from inside the project.

v1 migrates its retained report to deterministic `study-1`; v2 retains stable IDs, metadata, views and confirmation lineage. The checked-in v2 fixture wraps the existing v1 fixture's unchanged authoritative report in the established v2 history representation and uses an independently computed legacy bitwise CRC. Tests also compare every study type's canonical evidence exactly after asynchronous round trips.

## Measurement method

Windows Release, MSVC 14.51.36231, C++23 `/O2 /MD`, Intel Core Ultra 9 275HX, 24 reported logical processors and 33,752,997,888 bytes physical RAM. Native validation used an NVIDIA GeForce RTX 5080 Laptop GPU, Vulkan API 1.4.351/loader 1.4.363, SDL3 and ImGui 1.92.9b. Measurements are local CPU wall times, not GPU timers or a cross-machine guarantee. Filesystem/allocator caches and system load vary.

The original Release binary at `e08519bc074c7475ff3d6672a3e192bc822ee943` was retained before rebuilding. The near-limit fixture is exactly the earlier history benchmark: 20 copies of a 10×10 interaction grid with 100 sequential seeds starting at 1001, 120 seconds at 10 Hz, source seed 42, 12 bays/2 workers/1 forklift, uniform arrivals 1–3 seconds and processing 2–12 seconds. The next insertion exceeds the history budget. File byte differences of a few bytes come from timestamps and measured execution/report timings, not changed observations.

Baseline phase profiling measured serialization 11,620.2 ms, write/flush 153.458 ms, replacement 0.6412 ms and Save total 12,375 ms. Open parsed in 3,506.72 ms and validated/restored in 13,341.2 ms, for 17,066.2 ms total. The source working set was 256,176,128 bytes; Save's high-water mark reached 1,728,872,448 and Open reached 1,868,582,912 bytes. The main consumers were complete JSON history/envelope trees, input/output strings, the canonical payload copy, duplicated report DOMs and decoded state alive alongside all of them. Disk I/O was a small part of Save.

Three isolated process trials using the unchanged history benchmark produced:

| Near-limit metric | Original median (range) | Streaming median (range) | Target |
|---|---:|---:|---:|
| Save | 11.056 (10.926–11.286) s | 2.869 (2.813–2.875) s | <8 s |
| Open | 15.329 (15.285–16.033) s | 7.638 (7.588–7.652) s | <10 s |
| Recovery Save | 10.944 (10.869–12.808) s | 2.799 (2.775–2.850) s | — |
| Peak working set | 1,779.4 (1,778.8–1,781.2) MiB | 362.6 (362.3–363.2) MiB | <750 MiB |
| Project size | 201,181,771–772 bytes | 201,181,771–772 bytes | No material growth |

Save is approximately 3.9× faster, Open 2.0× faster and peak working set 79.6% lower on this fixture. Commit #28's originally published single sample was 12.74 s Save/18.26 s Open/approximately 1.74 GiB peak; the repeated reproduction above is the direct same-machine comparison. [Raw before/after trials](persistence-before-after.csv) retain every measurement rather than substituting the published sample.

`persistence_benchmark` additionally samples the current Windows working set every 2 ms at operation boundaries and records the process high-water mark. These measure resident process memory, including allocator reservations, rather than exact live allocations. Deletion/switch tests also prove evidence ownership is released; a resident allocator cache is not classified as a leak. Three isolated trials per additional fixture, with the 2 ms sampler enabled:

| Fixture | Studies | File bytes | Save median (range), ms | Open median (range), ms | Peak median, MiB |
|---|---:|---:|---:|---:|---:|
| small | 0 | 3,007 | 2.15 (1.81–2.43) | 5.84 (4.31–5.93) | 6.6 |
| medium | 10 | 787,842 | 14.29 (14.03–15.31) | 36.11 (35.90–38.72) | 9.5 |
| large | 4 | 20,254,352 | 333.74 (325.13–336.74) | 854.55 (849.45–859.11) | 155.3 |
| near | 20 | 201,181,772 | 2885.67 (2873.89–2893.46) | 7970.24 (7950.00–8012.08) | 362.3 |
| maximum | 20 | 268,435,456 | 2885.84 (2882.16–2943.79) | 8247.82 (8173.48–8392.33) | 361.5 |
| count_limit | 32 | 2,087,319 | 30.39 (29.90–33.45) | 83.35 (73.08–86.83) | 12.1 |

Small contains no studies. Medium alternates ten paired statistical/sensitivity studies. Large contains a paired study, its independent confirmation and two 100-seed 10-by-10 grids. Near contains twenty grids and exercises the history-byte limit. Count-limit contains 32 paired studies. Maximum reads the near file with legal whitespace padding to exactly 268,435,456 bytes; its Save column measures the original unpadded file. These are fixture-specific timings, not different measurements of the unchanged direct-comparison binary above. Instrumented near-limit Open is about 0.3 seconds slower with phase sampling enabled.

Malformed trailing JSON and 256 MiB + 1 byte input were rejected in all three trials; the active workspace stayed intact. Invalid-case CSV Save/Open rows refer to the small valid setup project before corruption. Trailing-JSON rejection took 3.65–4.53 ms; oversized rejection took 4.95–6.94 ms. Raw [operation timings](persistence-benchmarks.csv) and [phase/lifecycle resident-memory samples](persistence-memory.csv) retain the three trials for every case.

Near-limit phase medians:

| Phase | Save, ms | Open, ms |
|---|---:|---:|
| Main-thread snapshot capture | 0.105 | — |
| Serialization | 2,513.945 | — |
| Read, parse and run decoding | — | 3,506.977 |
| Evidence/workspace validation | — | 3,418.520 |
| Canonicalization/CRC | 234.491 | 1,046.931 |
| Write and flush | 136.194 | included in read/parse |
| Replace | 0.447 | — |
| Snapshot restoration | — | 0.050 |
| CPU scene reconstruction | — | 0.043 |
| Main-thread publication | 0.0005 | 0.005 |
| Total | 2,885.669 | 7,970.241 |

Read and parse share one streaming phase. Study canonicalization/CRC is separated from report validation; run canonicalization and binary decoding remain within read/parse. Snapshot restoration and scene fields count their CPU construction only. Totals include setup/worker dispatch and cannot be reconstructed by summing independently selected medians. Recovery Save/Open medians were 2,911.819/8,027.392 ms; Recovery Open publication (which destroys its old loaded evidence) was 17.753 ms.

Median working sets were 4.8 MiB idle, 142.2 MiB after fixture construction, 153.9 MiB while serializing studies, 150.7 MiB while writing, 138.6 MiB at replacement, 229.9 MiB while reading/parsing, 362.3 MiB while rebuilding a study, and 322.1 MiB after validation/before publication. Each phase sample is a current resident-memory maximum, not an allocation total; adjacent phases can share the same allocations. The Open peak contains the source workspace, staged typed observations and a single report DOM/canonical string.

The near-limit cancellation probe stops at the first study reconstruction boundary and then measures cooperative completion: median 24.076 ms. Working set after normal operations was 175.3 MiB, after cancellation 210.6 MiB, after deleting one source history entry 205.8 MiB and after switching the loaded project to empty 71.7 MiB. Deletion median was 0.412 ms; empty-project switch was 21.913 ms. The source fixture and its shared immutable grid remain alive throughout, and the allocator retains freed blocks. These measurements demonstrate bounded ownership and release, not return of every freed page to the OS.

## Verification and native responsiveness

Final Windows Debug passed **585/585** (668.39 s), Windows Release passed **585/585** (204.95 s), and renderer-disabled passed **548/548** (20.72 s). The Debug near-limit native workflow passed in 471.62 s. All prior tests remain enabled and green. The targeted persistence suite has 51 CPU/CLI cases, including exact single-seed, paired multi-seed, interaction, sensitivity and independent-confirmation round trips; unchanged request/seed/raw observations/derived statistics; v1 migration and an independently sealed v2 fixture; field reordering/escaped Unicode; duplicate keys, corruption and adversarial nested run arrays; injected write/flush/prepublication failures; allocation failure during reconstruction and publication preparation; real Windows sharing locks and Save As races; cancellation at each supported boundary; later edits, deletion and active analysis during capture; shutdown/destruction and recovery.

The original unmodified v2 20-grid project (201,181,772 bytes) also passed the new renderer-free verifier. No simulations were rerun by that verification.

The reviewed native near-limit workflow uses real File-menu operations with no artificial delays. It cancels Save/Open, keeps the previous workspace visible, edits/seeks during Save, moves the camera during Open, resizes from 640 to 1600 pixels, minimizes/restores, opens successfully and preserves the restored workspace after corrupt Open. It compares retained history, live snapshots, selections and dirty state, then exercises explicit-seed playback and a fresh-process restoration.

| Native operation | Rendered frames | Mean frame time | p95 | Maximum |
|---|---:|---:|---:|---:|
| Release Save (review) | 640 | 4.155 ms | 4.483 ms | 12.865 ms |
| Release Open (review) | 1,960 | 4.168 ms | 4.679 ms | 7.098 ms |
| Debug Save (final full suite) | 6,707 | 4.170 ms | 4.477 ms | 22.555 ms |
| Debug Open (final full suite) | 33,825 | 4.171 ms | 4.440 ms | 28.197 ms |

These are measured rendered-frame CPU times during the persistence interval, including normal frame work; OS suspension/minimization is exercised separately. They are local samples, not an end-to-end latency guarantee. Fresh-process rendering averaged 4.063 ms over 182 frames. Both native processes reported **0 Vulkan validation errors / 0 warnings**, scene/UI pipeline generations **1/1**, and successful shutdown/reinitialization.

Five inspected PNGs total 852,777 bytes; the existing artifact manager bounds reviewed runs and reclaims successful routine diagnostics:

- [Save progress](artifacts/persistence-save-progress.png)
- [Open progress and cancellation](artifacts/persistence-open-progress.png)
- [Cancel Save while later playback remains visible](artifacts/persistence-cancel-save.png)
- [Restored workspace](artifacts/persistence-restored.png)
- [Corrupt Open preserves the workspace](artifacts/persistence-corrupt-preserved.png)

## Reproduce and verify

```powershell
cmake --build --preset windows-release
.\build\windows-release\benchmarks\simforge_study_history_benchmark.exe --near
.\build\windows-release\benchmarks\simforge_persistence_benchmark.exe near
# Additional cases: small, medium, large, count_limit, maximum, malformed, oversized
.\build\windows-release\apps\sandbox\simforge_headless.exe --headless --verify-project "study.simforge"
ctest --test-dir build/windows-release --output-on-failure
powershell -File scripts/run-graphics-test.ps1 -Executable build/windows-release/apps/sandbox/simforge_sandbox.exe -Scenario examples/warehouse-statistical-workload.json -Live -Persistence -Mode Review
```

The benchmark creates, saves, reloads and validates its fixtures, restores recovery, cancels a staged load, deletes evidence and switches to an empty project. `maximum` pads a valid near-limit JSON project with legal trailing whitespace to exactly 256 MiB; it tests the maximum accepted input boundary, rather than claiming a typical generated file is that large. `oversized` verifies rejection at 256 MiB + 1 byte. All benchmark projects are temporary and reclaimed. Native diagnostics retain a bounded reviewed PNG set through the existing artifact manager.

## Remaining limits and next work

Snapshot capture copies mutable live state on the main thread and scales with its size. Report insertion/export and explicitly requested seeded playback still have their existing CPU costs; study switching shares immutable evidence and does not reparse it. Each study has a bounded temporary DOM and canonical string. Open needs the old and staged typed workspaces concurrently. Final publication joins the previous analysis worker and releases its workspace; cancellation of an unusually expensive in-flight analysis may add publication latency. Cancellation cannot interrupt a blocking OS call or roll back an already published file. No compression, journal/database, new binary format or external dependency was introduced.

Recommended next milestone: demand-model calibration with explicit fit/holdout provenance and uncertainty validation, after defining its data contract. Do not infer that it has started from this persistence work.

## Changed files

- [README.md](../README.md)
- [apps/sandbox/CMakeLists.txt](../apps/sandbox/CMakeLists.txt)
- [apps/sandbox/graphics.cpp](../apps/sandbox/graphics.cpp)
- [apps/sandbox/main.cpp](../apps/sandbox/main.cpp)
- [apps/sandbox/persistence_diagnostic.inl](../apps/sandbox/persistence_diagnostic.inl)
- [apps/sandbox/project_panel.cpp](../apps/sandbox/project_panel.cpp)
- [apps/sandbox/project_panel.hpp](../apps/sandbox/project_panel.hpp)
- [apps/sandbox/viewer.cpp](../apps/sandbox/viewer.cpp)
- [benchmarks/CMakeLists.txt](../benchmarks/CMakeLists.txt)
- [benchmarks/persistence_benchmark.cpp](../benchmarks/persistence_benchmark.cpp)
- [docs/artifacts/persistence-cancel-save.png](artifacts/persistence-cancel-save.png)
- [docs/artifacts/persistence-corrupt-preserved.png](artifacts/persistence-corrupt-preserved.png)
- [docs/artifacts/persistence-open-progress.png](artifacts/persistence-open-progress.png)
- [docs/artifacts/persistence-restored.png](artifacts/persistence-restored.png)
- [docs/artifacts/persistence-save-progress.png](artifacts/persistence-save-progress.png)
- [docs/persistence-before-after.csv](persistence-before-after.csv)
- [docs/persistence-benchmarks.csv](persistence-benchmarks.csv)
- [docs/persistence-memory.csv](persistence-memory.csv)
- [docs/persistence-performance.md](persistence-performance.md)
- [docs/project-persistence.md](project-persistence.md)
- [docs/study-history-and-confirmation.md](study-history-and-confirmation.md)
- [engine/include/simforge/workspace/project.hpp](../engine/include/simforge/workspace/project.hpp)
- [engine/src/analysis/confirmation.cpp](../engine/src/analysis/confirmation.cpp)
- [engine/src/analysis/interaction_json.cpp](../engine/src/analysis/interaction_json.cpp)
- [engine/src/analysis/report_value.hpp](../engine/src/analysis/report_value.hpp)
- [engine/src/analysis/sensitivity_json.cpp](../engine/src/analysis/sensitivity_json.cpp)
- [engine/src/analysis/statistical_json.cpp](../engine/src/analysis/statistical_json.cpp)
- [engine/src/workspace/persistence_stream.hpp](../engine/src/workspace/persistence_stream.hpp)
- [engine/src/workspace/project.cpp](../engine/src/workspace/project.cpp)
- [engine/src/workspace/project_io.inl](../engine/src/workspace/project_io.inl)
- [engine/src/workspace/project_methods.inl](../engine/src/workspace/project_methods.inl)
- [scripts/run-graphics-test.ps1](../scripts/run-graphics-test.ps1)
- [tests/CMakeLists.txt](../tests/CMakeLists.txt)
- [tests/fixtures/project-v2.simforge](../tests/fixtures/project-v2.simforge)
- [tests/persistence_stream_test.cpp](../tests/persistence_stream_test.cpp)
