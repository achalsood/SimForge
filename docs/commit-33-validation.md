# Commit 33 completion and validation

Baseline HEAD was clean at `b08bbefe365b2cb5c2fa1f0fc2b711470afdf8ec`. Local target message: `feat(experiments): add paired multi-seed checkpoint interventions`. The final commit SHA and clean-tree verification are supplied in the delivery because a committed file cannot contain its own hash. No push/publication, release or next milestone is included.

## Requested completion items

| # | Item | Implementation and evidence |
|---|---|---|
| 1 | Commit SHA | Actual local SHA supplied with delivery. |
| 2 | Working tree | Post-commit status checked for a clean tree. |
| 3 | Files changed | Exact inventory below; core request/runner/codecs, existing workspace/worker/history/persistence, native/CLI, tests/examples, benchmarks and documentation. |
| 4 | Architecture | Owned validated typed request; one independent per-seed prefix and two restored continuations. Immutable compact report. No new warehouse engine or shared representative checkpoint. |
| 5 | Interventions | One loading-bay/worker/forklift set/increase/decrease action, resolved to the authoritative recorder command. Arithmetic, total capacity, enums, bounds and source conflicts validate. |
| 6 | Timing | Exact integer tick after all due runtime events and source commands at that tick. Intervention follows; no event-time rounding. Start/end supported. |
| 7 | Reconstruction | Fresh frozen source with each planned seed; original seed-specific workload, prefix once, validated checkpoint, independent branches. |
| 8 | Snapshot completeness | Existing tick/source, shipments, queue, reservations, event scheduler/sequence, counters, available resource time and command history. Generator realizations regenerate from seed/version; shipment requirements are identity based. |
| 9 | Independence | Complete restored initial states compare equal before branch mutation. Independent owned runtime containers; tests mutate one branch and verify the other remains unchanged. |
| 10 | External demand | Every final shipment's ID, arrival tick and processing requirement compared exactly across control and intervention. Future arrivals remain matched. |
| 11 | Random streams | Existing independent arrival and processing streams and generator versions unchanged; fixed/profile/uniform/homogeneous/piecewise cases tested with variable processing. |
| 12 | Control | No new intervention. Ordered source commands retained in both arms, including future commands on nonconflicting resources. |
| 13 | Execution | Existing WarehouseInputRecorder submit/set-resource runtime APIs. Reductions below active reservations fail; active work is never destroyed. |
| 14 | End outcomes | Authoritative completed, waiting, throughput, mean/max started wait and all three utilization metrics; cumulative scope explicit. |
| 15 | Post outcomes | Completion/throughput counter differences; wait-sum/started-count differences; occupied/available resource-time differences. Post waiting stock/max wait and zero denominators remain unavailable. |
| 16 | Aggregation | Existing paired summarizer, intervention minus control, SD/SE, per-seed observations, raw signs/ties and favorable direction. Utilization is descriptive without favorable fraction. |
| 17 | Confidence intervals | Nominal 95% paired Student-t with existing three-decimal NIST table, n−1 df and at least three complete stochastic pairs. Deterministic intervals suppressed; zero variance explicitly qualified. |
| 18 | Edge cases | Empty/invalid seed plans, overflow, unsupported commands/versions, missing/nonfinite metrics, small samples, deterministic/no-op cases, closed horizon and invalid reductions. No partial successful-seed report. |
| 19 | Analysis UI | Source, exact tick, action/resource/value, objective, sequential/explicit seeds, Run/Cancel and truthful phase/tick progress. Shared scrolling setup/results at narrow widths. |
| 20 | Charts/details | Signed per-seed effects, measured mean/interval, control/intervention numerical comparison, explicit timeline and per-seed checkpoint/provenance. No fabricated continuous trajectory. |
| 21 | Playback | Explicit measured seed and arm reconstruct, compare entire retained outcome, then existing command-aware seek/reset/step/resume playback. Native checks pre-checkpoint equality, command timing/count and exact final metrics. |
| 22 | Async/cancellation | Existing single jthread/ExecutionControl, lock-protected progress/publication. Cancellation during prefix/control/intervention and between seeds/aggregation; earlier evidence and active runtime preserved. |
| 23 | History | New immutable Checkpoint Intervention Study type; existing metadata/export/confirmed deletion/pending retention limits; selected metric/seed stored in study view. |
| 24 | Headless | Existing --headless --experiment with embedded-source paired_checkpoint_intervention request; same core runner. Five example smoke cases and strict CLI contract. |
| 25 | Exports | Versioned full JSON; 24-column summary/per-seed CSV with stable identity, ticks, action, units, scopes and blank/null undefined values. Exclusive distinct output paths. |
| 26 | Persistence | Existing streaming project v2 evidence collection with checkpoint_intervention_v1 feature; bounded report parsers rederive summaries/ratios/provenance. CRC/atomic Save/transactional Open/recovery/old fixtures retained. |
| 27 | Deterministic tests | Exact boundary fixtures, control versus uninterrupted run, fresh-start distinction, branch isolation, snapshot serialization/restore and corruption, recording replay, command validation and no-op outcomes. |
| 28 | Stochastic pairing | Exact IDs/ticks/requirements for every supported workload mode, including both Poisson modes and uniform processing; reproducible compact reports and restored playback. |
| 29 | Numerical references | Independent paired differences {4,6,5,7,3}: mean 5, variance 2.5, SE sqrt(.5), interval 5 ± 2.776 sqrt(.5). Mixed signs, negative mean, missing pairs, nonfinite values, 1/2/3 pairs and deterministic suppression. |
| 30 | Debug | All 795 cases passed across the complete run and documented native rechecks; latest selected checkpoint CPU 59/59. The initial run had two fixed UI regressions and one unexplained existing smoke timeout; no first-pass all-green claim. |
| 31 | Release | Final complete suite 795/795 passed, including all 21 graphics tests (278.89 s). |
| 32 | Renderer-disabled | Final complete suite 754/754 passed (132.45 s). |
| 33 | Vulkan | Actual successful native walkthrough and fresh-process reconstruction report zero warnings/errors, including shutdown/reinitialization. Full run details below. |
| 34 | Performance | Six core Release workloads include 10 short seeds, 20 eight-hour seeds and 100 seeds at the maximum admitted fixed-arrival workload; stage times and N/N/N execution counts retained. |
| 35 | Memory/storage | Raw report/project sizes, source-only project increase, Save/Open and cumulative process peak measured; details below. No full per-seed snapshot retention. |
| 36 | Screenshots | Curated unchanged actual framebuffer PNGs for setup/timeline, progress, paired effects, interval, seed inspection, both playback arms and compact layout. Visually inspected. |
| 37 | Limitations | Conditional model inference, approximate small-sample intervals, no multiplicity correction, deterministic/zero-variance caveats, conservative bounds and on-demand reconstruction latency. Windows/MSVC verified; other platforms unverified. |
| 38 | Next milestone | Independent-seed confirmation of a frozen checkpoint intervention, preserving exact boundary and paired workload. Recommendation only; not started. |

## Run record

All three configured Windows builds succeeded. Final build logs contain no compiler warnings/errors. Existing simulation, generator, snapshot and paired-statistical algorithms/versions remain unchanged.

| Run | Result | Elapsed |
|---|---|---:|
| Debug full CTest suite | 792/795 initially; two UI regressions plus one existing custom-live timeout | 878.46 s |
| Debug native rechecks after selector fix | 6/6: checkpoint, statistical, sensitivity, interaction, Study History and custom-live | 75.57 s |
| Latest Debug checkpoint CPU selection | 59/59, including six existing checkpoint cases, 47 new core cases and six CLI cases | 3.13 s |
| Release full CPU partition before final source-count guard | 774/774 | 25.44 s |
| Final renderer-disabled full suite | 754/754 | 132.45 s |
| Final Release full suite | 795/795 passed, all 21 graphics cases included | 278.89 s |

The native selector regression was reproducible from the captured clipped controls, fixed by preserving the previous wide-panel row height, and verified by the affected native workflows. The custom-live graphics smoke timed out once at 40.04 s with an empty console and no surviving sandbox process; its identical isolated case passed in 18.57 s. There was no identifying diagnostic for that timeout, so its cause remains unresolved. No renderer, timeout or simulator change was made to accommodate it.

The final core export/parser guard also verifies retained arrival counts against the frozen seeded generator. An adversarial report with self-consistent but invented arrivals rejects. All five curated full report JSONs remained byte-identical across all three final builds. The final Debug checkpoint selection, renderer-disabled suite and final Release suite include this guard and the stronger reduction/view-restoration fixtures. Unaffected Debug cases were covered in the complete run.

Local logs: `build/checkpoint-full-debug-tests.log`, `build/checkpoint-debug-native-rechecks.log`, `build/checkpoint-final-debug-cpu-tests.log`, `build/checkpoint-release-cpu-tests.log`, `build/checkpoint-full-headless-tests.log`, `build/checkpoint-full-release-tests.log`, `build/checkpoint-final-native-review.log` and `build/checkpoint-final-build-windows-{debug,release,headless}.log`.

The first native walkthrough exposed a real layout defect: setup occupied the Analysis window and clipped its results child, preventing the intervention button from being reached. The layout was corrected to one scrollable study workspace; both actual playback controls then passed. A subsequent native benchmark request with 100 Poisson seeds was correctly rejected by the existing conservative work budget. The harness now measures 50 permitted seeds; no product bound was weakened. A PowerShell CSV contract assertion initially counted each property's member instead of the property collection; the assertion was corrected and the 24-column export itself passed C++ row-width checks.

Final Review source: `5625771d0b584e13b4780b6dee6bd671`. Its actual initial process and fresh-process reopen both report Vulkan validation 0 errors/0 warnings, including shutdown/reinitialization. The walkthrough verifies real Run/Cancel/playback controls, actual mid-continuation tick progress, previous-evidence/active-runtime preservation, headless/core equality, exact pre-checkpoint equality, exactly one intervention command, final metric equality, metadata/exports, project Save/Open and 640/1280/1920 resize/minimize/restore. No aggregate warehouse state is constructed.

## Measured examples

These [five complete requests/reports](../examples/checkpoint-interventions) each use a frozen eight-hour source, checkpoint at two hours, 20 predefined sequential seeds and actual paired simulation outputs. All raw observations and other metrics remain in JSON.

| Example | Control mean completed | Intervention mean completed | Mean paired difference | Nominal 95% interval | Mean waiting-stock difference | Mean started-wait difference (s) |
|---|---:|---:|---:|---|---:|---:|
| Add third forklift | 93.95 | 129.95 | +36 | [36,36] | −36 | −1229.611 |
| Add two workers | 93.95 | 165.95 | +72 | [72,72] | −72 | −2885.639 |
| Add one bay | 47 | 83 | +36 | [36,36] | −36 | −607.627 |
| No-benefit extra forklift | 47 | 47 | 0 | [0,0] | 0 | 0 |
| Piecewise Poisson surge | 84.4 | 119.6 | +35.2 | [35.007933,35.392067] | −36 | −31.450 |

The first four completion effects have zero observed variance, explicitly marked as degenerate model intervals. Their demand is still stochastic; sustained congestion can make the capacity-driven completion difference constant across the chosen seeds. The no-benefit case honestly retains its zero effect. No seed search or effect-based seed selection was used.

All five full example report JSONs matched byte-for-byte across Debug, Release and renderer-disabled execution and the committed curated reference (15 requests). Timings are separate from report evidence. The owned temporary cross-build export run was reclaimed.

## Measurements and artifact review

[Raw Release CSV](checkpoint-intervention-benchmarks.csv), measured with no other build/test job active:

| Workload | Seeds | Total (ms) | Prefixes / control / intervention |
|---|---:|---:|---|
| Short Poisson, 600 s | 10 | 2.0943 | 10 / 10 / 10 |
| Eight-hour Poisson | 20 | 5.5463 | 20 / 20 / 20 |
| Maximum admitted 100-seed fixed workload, 66666 arrivals/seed | 100 | 1561.1686 | 100 / 100 / 100 |
| Eight-hour deterministic | 20 | 3.0521 | 20 / 20 / 20 |
| Eight-hour homogeneous Poisson | 50 | 13.1138 | 50 / 50 / 50 |
| Eight-hour piecewise Poisson | 20 | 7.3736 | 20 / 20 / 20 |

The maximum case used 86.0719 ms for prefixes, 218.6835 ms for checkpoint capture/validation/encoding/digest, 201.7252 ms for branch construction/restoration/equality checks, 270.4435 ms for control continuations, 282.6081 ms for intervention continuations and .1428 ms for aggregation. Totals also include matched-workload verification and bounded orchestration; the component columns do not exhaust total time. Snapshot validation may regenerate demand but never replays the prefix's warehouse events. No matched speedup claim is made.

The largest measured report/project was 317983/320938 bytes; retaining it increased the source-only project by 318388 bytes. Serialization/parsing took 3.7096/7.8049 ms and Save/Open 6.5384/24.5112 ms. Cumulative process peak working set was 30900224 bytes (29.47 MiB), including the maximum-workload temporary runtime/snapshot buffers. This is not isolated per-stage allocation or GPU memory. Cooperative cancellation at the control-continuation boundary measured .0178.. .3935 ms, including stack unwinding; other safe-boundary/codec latency is not universally bounded by those measurements.

[Native Debug CSV](checkpoint-intervention-native-benchmarks.csv): 50 permitted dense-demand paired seeds, 125 actual nongated active frames; mean/max wall frame time 4.12469/5.2388 ms; owning-thread kernel+user CPU averaged 2.5 ms/frame via Windows GetThreadTimes. The main-thread value excludes worker CPU and presentation waits; wall timing includes scheduling/presentation. These captures use a separate dense-demand fixture from the five curated requests. GPU: NVIDIA GeForce RTX 5080 Laptop GPU. Other platforms and unmatched speedups are not claimed.

Nine unchanged framebuffer PNGs were visually inspected, maximum 257993 bytes. The 640×480 capture shows the actual interval and control mean reachable by scrolling. These are native application outputs, not mockups or edited images.

- [Setup and checkpoint timeline](artifacts/checkpoint-setup.png)
- [Actual seed/phase/tick progress](artifacts/checkpoint-progress.png)
- [Signed per-seed paired effects](artifacts/checkpoint-paired-effects.png)
- [Mean effect and nominal interval](artifacts/checkpoint-confidence.png)
- [Explicit seed and paired outcomes](artifacts/checkpoint-seed-inspection.png)
- [Actual control playback](artifacts/checkpoint-control-playback.png)
- [Actual intervention playback](artifacts/checkpoint-intervention-playback.png)
- [Fresh-process restored study](artifacts/checkpoint-restored.png)
- [640-pixel interval view](artifacts/checkpoint-compact.png)

Only curated PNGs, raw benchmark CSVs and bounded example requests/reports are committed. Generated full projects, benchmark workloads and temporary exports remain outside tracked source. Native Review artifacts use the existing owned-path/lease checks and bounded successful-run retention. Failed exploratory artifacts are removed only after ownership and inactive lease verification.

## Exact file inventory

18 modified files and 35 added files (53 total).

```text
apps/sandbox/checkpoint_diagnostic.inl
apps/sandbox/checkpoint_panel.cpp
apps/sandbox/checkpoint_panel.hpp
apps/sandbox/CMakeLists.txt
apps/sandbox/dashboard.cpp
apps/sandbox/main.cpp
apps/sandbox/study_history_panel.cpp
apps/sandbox/viewer.cpp
apps/sandbox/workspace_panel.cpp
apps/sandbox/workspace_panel.hpp
benchmarks/checkpoint_intervention_benchmark.cpp
benchmarks/CMakeLists.txt
docs/artifacts/checkpoint-compact.png
docs/artifacts/checkpoint-confidence.png
docs/artifacts/checkpoint-control-playback.png
docs/artifacts/checkpoint-intervention-playback.png
docs/artifacts/checkpoint-paired-effects.png
docs/artifacts/checkpoint-progress.png
docs/artifacts/checkpoint-restored.png
docs/artifacts/checkpoint-seed-inspection.png
docs/artifacts/checkpoint-setup.png
docs/checkpoint-intervention-benchmarks.csv
docs/checkpoint-intervention-native-benchmarks.csv
docs/checkpoint-intervention-studies.md
docs/commit-33-validation.md
engine/CMakeLists.txt
engine/include/simforge/analysis/checkpoint_intervention.hpp
engine/include/simforge/workspace/scenario_workspace.hpp
engine/src/analysis/checkpoint_intervention.cpp
engine/src/analysis/checkpoint_intervention_json.cpp
engine/src/workspace/checkpoint_study.cpp
engine/src/workspace/experiment_execution.cpp
engine/src/workspace/project.cpp
engine/src/workspace/project_io.inl
engine/src/workspace/scenario_workspace.cpp
engine/src/workspace/study_history.cpp
examples/checkpoint-interventions/forklift.json
examples/checkpoint-interventions/forklift-report.json
examples/checkpoint-interventions/loading-bay.json
examples/checkpoint-interventions/loading-bay-report.json
examples/checkpoint-interventions/no-benefit.json
examples/checkpoint-interventions/no-benefit-report.json
examples/checkpoint-interventions/surge.json
examples/checkpoint-interventions/surge-report.json
examples/checkpoint-interventions/workers.json
examples/checkpoint-interventions/workers-report.json
README.md
scripts/checkpoint-examples.hpp
scripts/generate-checkpoint-examples.cpp
scripts/run-graphics-test.ps1
tests/checkpoint_cli_test.ps1
tests/checkpoint_intervention_test.cpp
tests/CMakeLists.txt
```
