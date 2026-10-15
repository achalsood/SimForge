# Checkpoint intervention studies

SimForge measures what changes when a capacity command is applied during a simulated warehouse shift. A study runs each seed from the source scenario to an exact checkpoint, forks that seed's authoritative state, and compares control and intervention continuations at the same final horizon. Extra capacity present from time zero is a different experiment.

## Inputs and commands

The owned request contains a complete validated warehouse scenario, integer checkpoint tick, one capacity action, an ordered optional source-command history, objective and replication plan. Worker execution never reads the Scenario Editor. The content-derived `checkpoint-fnv1a64` identity excludes the worker's monotonically allocated experiment number. Scenario/report/method versions and workload generator versions are explicit in exports.

One new action is supported at one checkpoint. Choose loading bays, workers or forklifts, then **set**, **increase** or **decrease** its capacity. Arithmetic is checked before conversion. Each capacity is 0..100000 and the combined capacity cannot exceed 100000. The action resolves to the existing `SetResourceCapacity` warehouse command and runs through `WarehouseInputRecorder::submit`; it never writes internal resource fields.

The authoritative runtime rejects reductions below active reservations. It preserves all active work. A reduction that fails for any seed fails the whole study; earlier completed evidence remains available. No partial set of successful seeds is published as a completed statistical study.

Advanced structured requests may include up to 32 ordered source commands. They are replayed in both arms. Commands on the intervention resource **at or after the checkpoint** are rejected to avoid overwriting/confounding the action. Other-resource commands retain their order; runtime reservation checks still apply. GUI source selection uses the validated loaded scenario or a retained editor candidate, with a fresh start for each seed. It does not infer a source history from the currently playing warehouse.

## Exact timing and state

Checkpoint ticks range from zero through the inclusive horizon. Tick rate belongs to the frozen scenario. No rounding or shift to an event boundary occurs.

`advance_to(tick)` processes **all events due at or before the target tick**, following the existing event scheduler's completion/arrival/sequence ordering, then moves the logical clock to that exact tick. Source commands due at that tick run in their recorded order. The checkpoint is captured after those events/commands. The intervention runs next, and can start newly eligible FIFO work immediately. At the closed final horizon no new service starts, although due completions have already been processed. Arrival eligibility remains the existing half-open shift interval.

At tick zero, deterministic arrival zero has already been processed where that workload supplies one. Poisson arrivals have no forced initial arrival. Tick-one, idle, arrival, completion, simultaneous-event, source-command, penultimate-tick and final-tick boundaries are tested using exact integers. Pause, seek and single-step playback reuse the same boundary policy.

Each seed constructs its own prefix exactly once. Existing warehouse snapshots retain clock tick, canonical source identity, capacities, shipment IDs/lifecycles, waiting queue, reservations, pending events and their dispatch sequence, next shipment/event identity, accumulated metric numerators and denominators, and ordered capacity history. Snapshot validation and restoration remain the existing tested implementation.

Workload algorithms are seed/identity based. Uniform arrival increments and processing requirements are reproducible from their independent streams; requirements are associated with shipment identity. Poisson tick realizations are regenerated from the original seed and generator version during restoration and checked against the snapshot. A separate opaque mutable PRNG state is therefore unnecessary. This study changes no generator or snapshot algorithm/version.

Two independently owned runtimes restore the same checkpoint. Their complete initial snapshots are compared before either continuation runs. The control receives no new intervention; both retain the optional source history. At the end, every shipment's ID, arrival tick and processing requirement is compared exactly between arms, including arrivals after checkpoint. Different queue/start/completion times are legitimate operational effects.

Only compact observations survive each seed: checkpoint/end metrics, resource-time sufficient statistics, resolved command, checkpoint capacity/event count and divergence digests. Full per-seed snapshots and 3D scenes are released before the next seed. FNV-1a digests help detect accidental reconstruction divergence; they are not cryptographic authenticity proofs.

## Outcomes and statistics

Eight outcome metrics are available: completed shipments, remaining waiting shipments, throughput/hour, mean started-shipment queue wait, maximum started-shipment wait, and bay/worker/forklift utilization. These are authoritative warehouse metrics.

Raw paired differences are always **intervention minus control**. Higher completed shipments/throughput and lower waiting/wait are favorable. Utilization is descriptive: neither higher nor lower is assigned favorable direction, and favorable fractions are unavailable for utilization.

The default scope is cumulative end-of-horizon metrics. Pre-checkpoint completions and busy time remain part of those cumulative values. The alternative post-checkpoint scope uses sufficient statistics:

| Outcome | Post-checkpoint definition |
|---|---|
| Completed | Final completed counter minus checkpoint completed counter |
| Throughput/hour | That completion difference divided by the remaining simulated duration, in hours |
| Mean queue wait | Difference of started-shipment wait sums divided by difference of started counts and tick rate |
| Utilization | Difference of occupied resource ticks divided by the corresponding difference of available resource ticks |
| Waiting stock | Unavailable as an interval aggregate; inspect the final waiting stock |
| Maximum wait | Unavailable; subtracting cumulative maxima is invalid |

Post-checkpoint mean wait describes shipments started after the checkpoint state was captured, including starts caused immediately by the command. Their waiting can have begun before the checkpoint. It excludes shipments still waiting, so changing the measured population can increase that mean even while more shipments complete. Zero duration or denominator produces null/blank, never an invented zero. Resource-time denominators incorporate all actual capacity changes.

The existing `summarize_pairs` implementation computes means, sample standard deviation of paired differences, standard error, extrema, median, positive/negative/tied counts, and descriptive favorable fraction. Nominal 95% Student-t intervals use the existing NIST table rounded to three decimal places with n−1 degrees of freedom. They require at least three complete stochastic pairs. Two pairs can define an SD/SE; one cannot. Deterministic workloads suppress uncertainty intervals because seeds repeat the same physical experiment. Zero observed variance gives a degenerate model interval with an explicit warning, not a guarantee.

Undefined metrics prevent that metric's aggregate/interval rather than silently selecting complete cases. Nonfinite observations are rejected. Failed simulations abort the study. No independent-sample interval or pooling of different seeds' checkpoints is used.

Results are conditional on source assumptions, demand/workload model, seed plan, checkpoint, resources and horizon. Distinct PRNG seeds do not establish independence of real-world observations. Small samples, skew and discrete outcomes can invalidate approximate paired-t coverage. Multiple metrics or explored checkpoint choices are unadjusted. An observed positive effect does not establish the optimal intervention time, real-world benefit or universal superiority.

## GUI, history and explicit-seed playback

1. Open **Analysis**, then **Checkpoint Intervention Study**.
2. Select a validated source, exact tick, resource/action/value, objective and seed plan. At 60 Hz, two hours is 432000 ticks; at 10 Hz it is 72000.
3. Run the study. Progress reports seed index/count and actual prefix, branch construction, control, intervention and aggregation phases. Tick progress comes from runtime callbacks. Cancel requests stop the existing bounded worker cooperatively.
4. Review signed per-seed effect stems, mean/nominal interval, control/intervention means and the numerical paired-outcome table. All charts use retained measured observations; no continuous trajectory is fabricated.
5. Select an explicit seed and open control or intervention playback. The submitted seed is reconstructed and checked against the entire retained outcome before publication. Playback is paused at the checkpoint and supports seek, step, reset and resume through the existing recorded-command timeline. The title identifies arm, seed and checkpoint; resource counts and metrics are live authoritative values.
6. Study History retains the immutable evidence, title/notes and selected metric/seed. Rename, export and confirmed deletion use existing controls and limits. Switching studies does not refit or rerun them.

Setup and results share a scrollable child, including the 640×480 layout. Playback changes are queued and applied by the owning thread before rendering the new controller. The background experiment never mutates the active runtime, renderer or UI. Failure/cancellation preserves prior evidence and active playback.

## Headless requests and exports

The existing `--experiment` entry point accepts a version-1 embedded-source request with mode `paired_checkpoint_intervention`. [Five complete request examples](../examples/checkpoint-interventions) show exact fields. Inline scenarios remove path ambiguity and freeze all workload parameters. Unknown fields, duplicate keys, invalid commands and incompatible method/boundary/generator versions reject.

```powershell
build/windows-release/apps/sandbox/simforge_headless.exe --headless `
  --experiment examples/checkpoint-interventions/forklift.json `
  --export-json build/new-checkpoint-report.json `
  --export-csv build/new-checkpoint-summary.csv `
  --export-seeds build/new-checkpoint-seeds.csv
```

Outputs use existing exclusive-new-file semantics and must have distinct paths. JSON includes full source identity/request, exact boundary policy, seed plan, versions, raw checkpoint/control/intervention metrics, resolved commands, resource-time counters, signed effects, aggregate summaries, validity/nulls and limitations. The 24-column summary/per-seed CSVs use stable identifiers, checkpoint/horizon ticks, action provenance, scope, metric and units. Undefined values are blank; summary CI columns are deliberately blank in per-seed rows. Execution timing measurements are separate from reproducible statistical evidence.

## Persistence, ownership and limits

Streaming project format v2 stores checkpoint studies in the existing bounded evidence collection, with the explicit `checkpoint_intervention_v1` feature. Each study is serialized/parsed and released separately; there is no full-project evidence DOM. Parsers rederive summary statistics, signed effects, sufficient-statistic ratios, command resolution, available resource time and method/provenance consistency without running a statistical experiment. Playback performs a separate exact reconstruction check on demand.

Existing CRC integrity, atomic Save, transactional Open, recovery, async Save/Open, captured revisions, old v1/v2 projects, custom runs, calibration, diagnostics and confirmation lineage remain intact. No full branch snapshot for every seed is persisted. Playback preview caches are reconstructed rather than saved as aggregate/representative states.

Replication plans retain the existing 1..100 distinct-seed, sequential-or-explicit policy, overflow checks, 95% confidence and method version. The existing conservative workload budget is 20 million arrival executions, counting N prefixes and 2N continuations. The warehouse's positive-intensity Poisson bound is 100000 arrivals, so this conservative preflight admits **at most 66 positive-intensity Poisson seeds** even for a small expected intensity. This is a deliberate bounded-execution restriction, not a simulated count cap. Other workloads may reach 100 seeds when their conservative bound permits it. Per-report size is 4 MiB, with the existing 32-study, 32 MiB per-study, 192 MiB history and 256 MiB project limits also enforced. Storage-limit failures leave a pending result for explicit retention/export/discard; no earlier evidence is silently evicted.

## Measured examples and validation

All five examples use 20 predefined sequential seeds and eight-hour horizons. They keep actual numerical outcomes, including zero or degenerate effects. The [completion report](commit-33-validation.md) records their control/intervention means, paired effects, uncertainty, test totals and validation limitations. In these saturated examples, adding a forklift, workers or a bay can produce an identical completion difference across the selected seeds; that does not imply demand itself is deterministic.

The CPU suite covers independent arithmetic reference values, exact boundaries, snapshot/replay equivalence and corruption, all resource/actions, source-command ordering/conflicts, fixed/profile/uniform/homogeneous/piecewise workload pairing with variable processing, no-benefit behavior, compact round-trips/tampering, retention, failure, cancellation during each simulation arm, publication preservation, and fresh playback after Save/Open. The SDL walkthrough uses real controls for execution/cancel and both playback arms, compares core/native evidence, and checks zero Vulkan warnings/errors through reopen and shutdown.

[Core Release benchmark CSV](checkpoint-intervention-benchmarks.csv) separates prefix simulation, snapshot capture/validation, branch construction/restoration, both continuations, aggregation, serialization/parsing, Save/Open, bytes, cumulative peak working set and cooperative cancellation. It includes 10 short seeds, 20 eight-hour seeds, a 100-seed maximum-admitted fixed workload (66666 arrivals per seed), deterministic, homogeneous and piecewise Poisson workloads. Counts are N prefix executions and N continuations per arm; snapshot validation/regeneration costs remain included in measured stages. No matched speedup claim is made.

[Native Debug benchmark CSV](checkpoint-intervention-native-benchmarks.csv) separates owning-thread kernel+user CPU measured with `GetThreadTimes` from wall frame time including scheduling/presentation. It excludes worker-thread CPU from the main-thread measure. Native measurements and core Release timings are distinct workloads/configurations. Peak working set is process-wide and cumulative, not isolated allocation per stage or GPU memory. Cancellation is measured at a cooperative safe boundary, not a universal latency bound.

This milestone does not optimize timing or staffing, implement arbitrary intervention schedules, fit demand, or add a new simulation engine. A useful next milestone is independent-seed confirmation of a frozen checkpoint intervention, retaining the same boundary and workload-pairing contract.
