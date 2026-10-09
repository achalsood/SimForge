# Project persistence

SimForge project format **1**, extension **.simforge**, saves a complete local warehouse workspace. It uses the existing scenario schema 1, warehouse snapshot versions 1/2, clock snapshot version 1 and analysis report schema 1. The compatibility identifier is `warehouse-2-analysis-1`. There are no migrations yet: unknown project, engine, scenario or snapshot versions fail explicitly. Standalone scenario JSON, snapshot and recording formats are unchanged.

## Saved data

A project owns a stable 128-bit hexadecimal identity, editable name, creation/save timestamps, an authoritative baseline definition, its canonical scenario identity, draft text and its draft origin, up to 50 ordered candidates and up to eight ordered custom runs. The workspace default playback speed is distinct from individual run speeds. Candidate IDs, notes, changed inputs and next-ID counters survive deletion/reopening without reusing IDs.

Invalid draft values remain text. Saving does not validate them into an executable scenario; explicit simulation/candidate actions still require existing scenario validation. Validation messages are reconstructed from the draft after loading. The editor's candidate selection, notes, tab, comparison objective/mode/checkpoint text, selected result, application mode and panel preferences are separate project preferences.

Each playable run embeds its validated initial definition, existing binary warehouse snapshot encoded as lowercase hexadecimal, complete clock snapshot, speed, paused state, camera pose and validated scene selection. Completion is derived from the horizon/tick. Snapshots retain every shipment, FIFO queue, processing reservation, scheduled event/sequence, resource capacity/usage, capacity command and operational total. Clock scheduling retains fractional remainder as well as tick/rate/scale. Initial reset snapshots are constructed from the validated definition; current state is restored transactionally from the saved snapshot, without replaying from tick zero.

Run ID 0 is always the original baseline. Custom IDs and ordering remain independent. Only the selected live run advances; static comparison previews remain separate controllers. Backward seek/reset retain a bounded capacity-command timeline; a saved future command tail is checked in a private runtime before restoration, and subsequently dispatched at its original event boundary. Complete snapshot equality tests cover original/restored continuation, event ordering, queues, reservations, totals and commands.

Vulkan resources, SDL windows, ImGui pointers and mesh data are never serialized. Loading creates CPU scene adapters and camera controllers against validated warehouse state, then uses the existing GPU pipelines and resources. Camera pose is preserved across viewport aspect changes; nonsensical poses are rejected and stale object/candidate selections are cleared.

## Container and integrity

The UTF-8 JSON envelope contains:

- `format: "SimForge"`, `version: 1`, `engine`, `scenario_schema`;
- the exact supported feature list (warehouse snapshots, clock phase, historical reports, independent runs);
- `payload_size` and `crc32` over the canonical compact JSON payload;
- `payload`: identity, baseline session, draft/origin, candidates, custom sessions, active/next identities, preferences, report and preview provenance.

JSON is inspectable; operational snapshots reuse the documented [warehouse binary format](warehouse-snapshots.md). Hex encoding costs two characters per binary byte but avoids a custom archive/dependency, ambiguous binary containers or extraction paths. CRC32 detects accidental damage; it is not authentication. Embedded snapshots also retain their existing integrity and structural checks. Timestamps and project IDs are metadata, outside authoritative simulation-state equality.

Total input/output is bounded at 256 MiB. Each binary warehouse snapshot is bounded at 32 MiB, scenario parsing at 4 MiB, draft fields/notes/checkpoint text at 4,096 bytes and project/candidate names at 256 bytes. Existing shipment, execution tick, event, report and command limits apply. JSON depth is bounded at 32 and parse events at four million. Counts are checked before reserving/constructing runs and candidates. Numeric types, booleans, enums, next IDs, duplicate identities, clock consistency, required/unknown fields and snapshot provenance are validated. No assets or external scenario references are followed from inside a project.

## Save and Save As

The File menu offers New Project, Open Project, Save Project and Save Project As. The launcher also offers New/Open Project; command-line restoration uses:

```powershell
.\build\windows-release\apps\sandbox\simforge_sandbox.exe --project "D:\Studies\warehouse.simforge"
```

Ordinary Save updates the associated path. The first Save opens the Windows native Save dialog. Save As requires a new filename, retains the project's stable identity and associates subsequent saves with the new path. It refuses an existing destination, including the currently associated file. Native dialogs use .simforge filters and default extensions. The project name/location/last save appear in File; `File *` indicates persisted changes. Success feedback is emitted only after replacement finishes.

New/Open/native close prompt for Save, Discard or Cancel when work is dirty or an experiment is active. Cancel keeps the workspace. Failed Save keeps the prompt and prevents replacement. Open validates its prospective replacement before cancelling/joining the old worker. An invalid file leaves runs, reports, edits and worker ownership intact.

A workspace generation tracks editor/candidate/report changes. A small in-memory marker observes run clocks, operational counters, camera/selection, run ordering and preferences; it does not serialize shipment arrays or inspect the filesystem on every frame. Simulation changes, including arrival-zero settlement and fractional clock advance, count as unsaved work. Drawing/validation alone does not.

## Transactional write and load

Save serializes the complete consistent main-thread workspace before touching the destination. It creates an exclusively owned, randomly named temporary file in the same directory. On Windows, it checks WriteFile, FlushFileBuffers and CloseHandle, then uses MoveFileExW with MOVEFILE_WRITE_THROUGH and, only for associated Save/recovery, MOVEFILE_REPLACE_EXISTING. A new/Save As destination omits replacement, so a file appearing between dialog and rename cannot be overwritten. Failure removes only the temporary file owned by that save. There is no in-place rewrite.

See Microsoft's [MoveFileExW contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw) and [file replacement overview](https://learn.microsoft.com/en-us/windows/win32/fileio/moving-and-replacing-files). Flushing the payload and requesting replacement improves failure safety; this implementation does **not** claim power-loss atomicity or directory/storage-device durability on all filesystems, removable drives or network shares. POSIX uses a flushed temporary file and rename; directory fsync is not implemented.

Loading reads with a size bound, parses bounded JSON, checks compatibility/features/CRC/length, validates definitions/identities/preferences/reports and restores every session into temporary controllers. Only after the complete workspace succeeds does it cancel/join the prior experiment and move the replacement into the application. Failures leave the active project unchanged. No GPU resource is part of the transaction.

## Reports and async work

The last published successful comparison persists with its exact submitted baseline, candidate IDs/definitions, objective, mode, checkpoint snapshot/tick/horizon, schema versions, executions, bottleneck evidence, ranking, warnings, limitations and completed execution/report timings. Report parsing validates operational ratios, deltas, eligibility/ranking and structural/provenance relationships using the same compatible analysis implementation; it never starts a simulation experiment. Duplicate export copies of execution/checkpoint payloads are omitted.

Editing or deleting a current candidate does not relabel historical report inputs. The Analysis page identifies the original report request and explicitly separates submitted inputs from current edits. Future async request IDs start above the restored report ID.

Saving during a comparison snapshots the main-thread inputs and last published report. It neither reads shared mutable worker simulations nor waits for the active comparison. Threads, progress and incomplete results are not restored. Restored users can submit a new comparison. New/Open cancellation is followed by joining, never detaching.

Cached final snapshots are validated against definition, capacity, completion and report metrics. A preview associated with the saved report can restore directly from that cache. Missing caches leave the selected live run visible with an explanation; selecting a result later explicitly rebuilds the preview. A preview from a superseded report is cleared with a message, rather than attached to the latest result or converted to playback.

## Recovery

A conservative single recovery slot lives at SDL's per-user SimForge preference directory, `session.recovery`. It contains the same versioned, validated project format and project identity. Once per 60 seconds, only a dirty workspace changed since its previous recovery is saved. The last explicit project save is never the recovery destination. Exceptions become concise recovery-failure feedback and are retried on a later interval.

Launch offers Restore recovery, Discard recovery or Later. Restore opens an unsaved project and requires Save As to establish a primary path; it does not automatically write any path embedded in the recovery. Discard removes only the .recovery slot. Later retains it and suspends new autosaves until it is explicitly resolved from File. Clearing recovery on clean/discarded shutdown checks ownership, so another project's recovery is preserved.

Recovery is synchronous at a safe main-thread point. It is a bounded, local last-recovery mechanism, not a complete crash journal: changes since the last interval can be lost on abrupt termination; multiple concurrent application instances do not have isolated recovery slots. Explicit saves remain the reliable persistence boundary. There is no background scheduler, cloud service or account.

## Measurements

Measured with the Release benchmark on the development Windows machine, single samples with filesystem/cache variability; 96, 10,000 and 100,000 shipments, rate 1 Hz, snapshots at independent intermediate times. Save timings below update an existing associated project. Times are milliseconds; sizes are bytes.

| Shipments | Custom runs | File bytes | Serialize | Write/flush | Replace | Save total | Parse | Validate | Restore | Scene | Open total | Recovery |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 96 | 0 | 4,414 | 0.123 | 0.585 | 0.340 | 1.073 | 0.055 | 0.124 | 0.041 | 0.027 | 4.911 | 0.998 |
| 96 | 3 | 13,284 | 0.314 | 0.490 | 0.334 | 1.177 | 0.102 | 0.303 | 0.127 | 0.077 | 4.194 | 1.296 |
| 96 | 8 | 23,978 | 0.557 | 0.556 | 0.372 | 1.552 | 0.147 | 0.544 | 0.265 | 0.177 | 4.596 | 1.655 |
| 10,000 | 0 | 200,530 | 3.120 | 0.747 | 0.366 | 4.283 | 0.891 | 2.808 | 0.164 | 0.030 | 7.665 | 4.833 |
| 10,000 | 3 | 634,316 | 9.822 | 1.199 | 0.377 | 11.533 | 1.759 | 7.898 | 0.562 | 0.079 | 14.828 | 11.425 |
| 10,000 | 8 | 937,505 | 13.944 | 1.336 | 0.494 | 15.906 | 2.656 | 11.628 | 0.848 | 0.196 | 20.169 | 16.248 |
| 100,000 | 0 | 1,982,537 | 30.611 | 1.878 | 0.470 | 33.094 | 7.890 | 24.402 | 1.312 | 0.071 | 43.340 | 32.703 |
| 100,000 | 3 | 6,277,336 | 96.628 | 8.341 | 0.834 | 106.217 | 17.940 | 78.339 | 4.071 | 0.199 | 115.366 | 102.102 |
| 100,000 | 8 | 9,237,986 | 142.970 | 8.357 | 1.198 | 153.460 | 24.939 | 117.163 | 6.318 | 0.602 | 166.206 | 159.753 |

One completed comparison containing 1/3/8 candidate executions occupied 20,827 / 38,261 / 81,554 bytes, saved in 2.497 / 3.813 / 7.367 ms and opened in 5.960 / 8.598 / 11.278 ms. Only the latest successful comparison is retained. Several historical executions within that report are preserved; this milestone does not introduce a report-history archive.

Peak process working set during the combined save/load benchmark reached 70,529,024 bytes (67.3 MiB). This is the cumulative Windows process high-water mark, including runtime/scene construction and allocator caching, not a per-stage allocation profiler. Parse/restoration/scene timings are instrumented CPU phases; total Open also includes disk read, canonical integrity checking and workspace commit. Scene timing measures CPU adapter construction, not GPU presentation.

Normal projects save in about 1–8 ms here. The largest tested nine-session workload pauses the main thread for about 150–165 ms. Serialization and validation dominate; do not infer responsiveness at the 256 MiB hard limit. A background save system/compression is deferred until larger real workloads justify its ownership and dependency costs.

The [full CSV measurements](project-persistence-benchmarks.csv) retain all instrumented stages and process memory samples. Reproduce with `build/windows-release/benchmarks/simforge_project_benchmark.exe`. It uses a temporary directory and cleans its outputs.

## Validation and curated captures

49 CPU persistence cases cover format/types/version/CRC/size/depth, identities/paths, invalid drafts, candidates/preferences, exact independent run restoration, clock phase, seek/reset/commands, historical reports, preview invalidation, failure injection, active-worker isolation and recovery. Existing engine, snapshot, replay, comparison, async/cancellation, playback, export and artifact tests remain enabled.

The opt-in `simforge_project_graphics` test drives File actions with injected SDL input and deterministic paths, verifies Save/Discard/Cancel and failed-save blocking, exits the saving process and launches a fresh `--project` process. It verifies camera/time/geometry/metrics/candidates/reports, complete deterministic continuation, clean Vulkan validation and native GPU/UI shutdown. Ordinary use uses Windows native dialogs; automated tests bypass dialog interaction through test-only paths. The artifact wrapper retains bounded diagnostic failures/reviews and removes successful temporary project files.

Validation results: Windows Debug **322/322**, Windows Release **322/322**, renderer-disabled **294/294**; the final transaction-specific follow-up also passed all **49/49** persistence cases in each configuration. Both graphics processes reported **0 Vulkan validation errors / 0 warnings**, kept scene/UI pipeline generations at **1/1**, and completed native shutdown/reinitialization. An initial Release deliberate-failure artifact case hit its 40-second timeout; its unchanged isolated rerun passed in 3 seconds and the complete rerun passed. The interrupted owned fixture was reclaimed after verifying its lease was released.

Curated real Vulkan readbacks:

- [File menu and project location](artifacts/project-file-menu.png)
- [Restored custom run](artifacts/project-restored.png)
- [Continued restored simulation](artifacts/project-continued.png)

Known limits: one current report, no recent-project list, one recovery slot, synchronous large saves, no power-loss guarantee or older-format migration. Preview camera/scroll positions are temporary; per-playable-run cameras and editor/panel selections are persistent. No external asset/GPU data, database, AI integration or publishing is added.
