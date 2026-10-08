# Batch scenario comparison and bottleneck analysis

SimForge runs independent warehouse alternatives from one settled operational
checkpoint. Every branch retains prior shipments, waits, reservations, events,
sequence counters and metric history. Only future resource availability changes.
Conclusions come from actual event execution and explicit comparisons; there is
no language model, optimization solver, financial model or calibrated prediction.

## Architecture and API

`simforge_analysis` is a separate static library depending on `simforge_engine`.
The warehouse has no dependency on analysis and remains usable on its own.
`analysis/scenario_comparison.hpp` defines the small experiment/request/result API.
The comparison module orchestrates branches, calculates deltas and rankings, and
runs three controlled add-one probes. JSON/file adapters reuse the established
strict scenario parser and existing nlohmann-json dependency; CSV uses explicit
escaping and locale-independent numeric conversion.

```cpp
using namespace simforge::analysis;
auto definition = simforge::scenario::load_scenario("examples/warehouse-baseline.json");
auto request = prepare_request(definition, 7200ULL * definition.simulation.tick_rate,
    Objective::MaximizeCompleted, {
        {"Add forklift", {3,10,3}},
        {"Add worker", {3,11,2}},
        {"Add loading bay", {4,10,2}}
    });
auto report = run_comparison(request);
save_report("build/comparison.json", report);
save_csv("build/comparison.csv", report);
```

For an existing operational checkpoint, construct `ComparisonRequest` with its
exact `scenario_identity` and deep snapshot. That checkpoint may already contain
earlier capacity changes. It must be settled. `prepare_request` executes shared
history once and settles the requested tick, including initial arrival zero at
tick zero. It never reconstructs candidate runs from time zero. `run_comparison`
validates the entire request before executing any candidate, retains an unchanged
baseline, then independently restores every candidate from that same checkpoint.

The API separates runtime metrics from `MetricDelta`, resource-change summaries,
rank/eligibility and bottleneck inference. A report stores the shared checkpoint
once; final shipment histories are not copied into each result. Execution order
cannot affect another branch. Returned results and recorded request candidates are
sorted by exact name; rankings have their own objective order. Reversing input
candidates produces the same report.

## Experiment JSON schema 1

The checked-in [warehouse-comparison.json](../examples/warehouse-comparison.json)
is a reproducible example:

```json
{
  "schema_version": 1,
  "scenario": "warehouse-baseline.json",
  "checkpoint_seconds": 7200,
  "objective": "maximize_completed",
  "candidates": [
    {"name": "Add forklift", "loading_bays": 3, "workers": 10, "forklifts": 3},
    {"name": "Add worker", "loading_bays": 3, "workers": 11, "forklifts": 2},
    {"name": "Add loading bay", "loading_bays": 4, "workers": 10, "forklifts": 2}
  ]
}
```

`checkpoint_seconds` is optional and defaults to zero. Other fields are required.
Capacities describe the complete desired configuration; they do not depend on
previous candidates. Names follow the scenario name contract: 1..256 valid UTF-8
bytes without control characters. Names must be unique; `Baseline` is reserved.
Candidate count is 1..50. Combined capacity is at most 100000, each capacity may
be zero, and all three must cover current checkpoint usage. Existing warehouse
quotas remain: at most 100K arrivals and 10M horizon ticks. Checkpoints must be
within the configured horizon; no arithmetic wraps or silently clamps.

Experiment input is at most 64 KiB. Unknown/duplicate fields, unsupported versions/
objectives, malformed numbers, negative/fractional capacities, invalid names,
empty lists and incompatible snapshots fail before candidate execution. Each
candidate must fit the remaining 100K-entry capacity-history quota.

Scenario paths are local relative `.json` paths resolved against the experiment
file's directory, independent of the process working directory. Drive/UNC/rooted
paths, URLs, embedded NUL, parent traversal and other extensions are unsupported.
The resolved canonical path must be a regular file inside that directory, including
when symbolic links are involved. Missing/invalid files fail cleanly. The example
uses `warehouse-baseline.json`, not a second `examples/` prefix.

## Generalized capacity changes and compatibility

`WarehouseSimulation::set_resource_capacity(ResourceType, uint32_t)` supports
`LoadingBay=0`, `Worker=1`, and `Forklift=2`. The existing forklift convenience method
delegates to this implementation. Unknown resource types and invalid counts are
rejected before mutation. Commands require a settled logical boundary, execute
after all due events, preserve existing reservations and service completion ticks,
and start newly eligible FIFO jobs immediately except at the closed horizon.
All jobs still acquire one of each resource atomically.

Batch configurations apply **reductions first**, in bay/worker/forklift order,
followed by increases in that order. Each reduction must cover current usage.
This keeps intermediate total capacity within the 100K limit and prevents an early
increase from starting jobs that would invalidate a later reduction. Unchanged
counts generate no batch command. Equal-count direct commands remain accepted and
recorded. Multiple same-tick direct commands run in accepted sequence order.

Snapshots continue using the existing `SFWHSE\0\0` format and version field:

- Schema 1 remains readable and byte-for-byte writable for forklift-only history.
- Schema 2 adds a resource-type byte to each capacity-history record, after its
  tick and `started_before` count and before its uint32 capacity. All other fields,
  quotas, checksum and endian rules remain unchanged. Histories use 21 bytes per
  record instead of 20. Bay/worker changes select schema 2; a restored v2 runtime
  retains version 2. No automatic downgrade discards resource type information.
- The history audit now integrates all three capacity pools and checks historical
  reservations against each recorded change. Utilization remains busy ticks divided
  by actual historical available-capacity ticks, including partial work.
- Recording schema 1 and command identifier 0 still mean `set_forklift_capacity`;
  JSON retains its `forklifts` parameter. Schema 2 supports the additional identifier
  1, `set_resource_capacity`, with `resource` and `capacity` parameters. New records
  can contain both command types. Old identifiers are never reinterpreted.
- Schema-1 recordings require schema-1 snapshots and legacy commands. Schema-2
  recordings accept compatible v1/v2 checkpoints and ordered supported commands.
  Replay still requires exact initial state, executes in isolation, verifies final
  logical state and restores transactionally.

`tests/fixtures/warehouse-v1-inputs.json` was produced by the actual Commit #9
executable. Tests verify exact canonical JSON/binary round trip, replay, and subsequent
upgrade through a worker command. World/movement formats are unchanged. The C++
history/command parameter formerly named `forklifts` is now `capacity`; old aggregate
forklift construction and convenience methods retain their semantics.

## Objectives, deltas and deterministic ties

Supported objectives are:

| Objective | Operational ordering | Missing metric |
| --- | --- | --- |
| `maximize_completed` | More completed shipments first | Always available |
| `minimize_average_wait` | Lower mean started queue wait first | No started shipments makes candidate ineligible |
| `minimize_waiting` | Fewer waiting shipments at H first | Always available |

All outcomes use the same observation interval from simulation zero through H,
including common pre-checkpoint work. Mean wait is compared using exact integer
ratios `total_wait_ticks/started`; products are bounded to 1e17. This avoids using
rounded displayed doubles to break close operational ties. Completion/queue counts
are exact integers. Throughput is proportional to completions at the same horizon,
so a redundant throughput objective was not added.

Operational ties prefer fewer **positive resource additions** relative to checkpoint
capacity, then fewer total absolute resource changes, then lexicographic name.
This is a transparent resource proxy, not a monetary cost function. The baseline
remains an unranked reference. A best candidate that fails to improve on baseline
is explicitly reported; the API never claims global or financial optimality.

Absolute deltas include completions, final queue, throughput, mean wait, utilization
and each resource count. Completion percentage is `100*delta/baseline.completed`;
mean-wait percentage uses the baseline mean. A zero denominator yields null, even
for a zero absolute change. If either mean is unavailable, its absolute and percentage
changes are unavailable. No placeholder zero is invented. Signed deltas can be negative.

Mean/max wait retain the established **started shipments** semantics, including
unfinished processing and excluding still-waiting censored shipments. Minimizing
that mean alone can hide unprocessed queued work; reports state this limitation.
An analytical regression test adds a forklift at second 11 of a 12-second run:
final queue drops by one while mean started wait rises from 3 to 4.25 seconds.
The mean-wait objective correctly classifies this as no positive effect.

## Bottleneck evidence

The report runs three interventions from the identical checkpoint: add one bay,
add one worker, and add one forklift, holding the other capacities constant. An
identical already-run candidate's measured metrics can be reused for that probe.
Otherwise the probe gets an independent restored branch. Evidence includes actual
raw metrics, capacities, completion/throughput/wait/queue/utilization deltas and a
deterministic summary generated from the measured counts.

Classification is objective-specific: a strict improvement supports
`likely_bottleneck_under_tested_conditions`; a tested non-improvement is
`no_positive_objective_effect`; missing objective metrics give `inconclusive`; an
intervention exceeding resource/history quotas is `intervention_unavailable` with
an explicit warning. Integer counts and exact wait ratios determine improvement;
no significance claim from noisy wall-time measurements is involved.

High utilization alone is never used to infer causality. When all probes have zero
effect, the report remains cautious: capacity might already suffice, multiple
resources may jointly constrain starts, the horizon may hide benefits, or the
checkpoint may already be at H. Single-resource experiments cannot distinguish or
exclude those explanations. A resource can cease to be limiting after an increase;
the result applies only to this tested state and horizon. Monetary purchasing advice
requires cost inputs and model calibration that this milestone does not provide.

## JSON and CSV reports

Report JSON schema 1 contains `request`, `checkpoint`, `units`, `baseline`,
`candidates`, `ranking`, `bottlenecks`, `warnings`, and `model_limitations`.
The request records exact scenario identity, a single lowercase-hex binary checkpoint,
objective and complete candidate inputs. This preserves reproducibility even when
the checkpoint contains earlier interventions. It is a summary with one shared
state, rather than N duplicated final histories.

Raw `metrics` are separate from `delta` and inference fields. Time values use seconds,
rates use shipments/hour, percentage changes use percent, and utilization uses
fractions of historical available capacity. Null means unavailable. Serialization
is deterministic, rejects nonfinite numbers, validates counts/ratios/intervals/
history-derived utilization/ranks, and limits output to 64 MiB. There are no wall
timestamps, runtime pointers, padding or allocator capacities in the report.

`replay_report(json)` strictly parses bounded/versioned input, reconstructs the
recorded request, reruns the experiment and compares every exported field with the
provided report. Invalid metadata, numerical outcomes, ordering, summaries or units
are rejected. It does not merely trust loaded metrics. This can be expensive because
it intentionally verifies the operational experiment. It authenticates neither
the author nor the chosen authoritative checkpoint; changing both inputs and expected
results is not prevented by the corruption checksum.

An abbreviated excerpt from the actual example's candidate row:

```json
{
  "candidate": {"name": "Add forklift", "loading_bays": 3, "workers": 10, "forklifts": 3},
  "metrics": {"completed": 87, "waiting": 8, "throughput_per_hour": 10.875, "average_wait_seconds": 1990.909090909091},
  "delta": {"completed": 24, "completed_percent": 38.095238095238095, "waiting": -24},
  "rank": 1
}
```

CSV is UTF-8 without a BOM, with CRLF rows, stable headers, baseline first then
canonical candidate order, and 29 columns. Fields include resource configuration,
counts, throughput, mean/max wait, utilization, completed processing, baseline
deltas, resource additions/changes and nullable rank. Unavailable values are empty
cells. Commas/quotes/CR/LF are escaped using quoted fields and doubled quotes;
the current name contract rejects control characters. Numeric formatting uses
`to_chars` with round-trip precision and decimal `.` independent of process locale.

The complete CSV header is emitted in the file. Its beginning is:

```csv
candidate,baseline,loading_bays,workers,forklifts,arrived,started,completed,waiting,in_progress,throughput_per_hour,average_wait_seconds
Baseline,true,3,10,2,96,64,63,32,1,7.875,4650
Add forklift,false,3,10,3,96,88,87,8,1,10.875,1990.909090909091
```

Exports exclusively create new files; they never silently replace an existing target.
Each output is serialized/validated before opening its file. On write failure a
newly created partial file is removed where possible. JSON/CSV files are independent
writes, not a multi-file filesystem transaction; if the second write fails the
first successful file remains. CLI checks existing/same output paths before running.

## CLI and actual results

```powershell
build/windows-release/apps/sandbox/simforge_sandbox.exe --headless --experiment examples/warehouse-comparison.json --export-json build/comparison.json --export-csv build/comparison.csv
```

`--experiment` requires headless mode and conflicts with scenario, record/replay,
checkpoint-demo and entity/tick flags. Export flags require an experiment and distinct
new paths. Existing warehouse scenarios, the two-branch forklift checkpoint demo,
movement and recording/replay commands remain supported. There are only three new
flags: experiment, JSON output and CSV output.

Actual Release results on October 8, 2026, at a two-hour checkpoint of the eight-hour
baseline scenario (all candidates share 25 arrivals, 17 starts and 15 completions
already reached at the checkpoint):

| Configuration | Resources bays/workers/forklifts | Completed | Waiting | Shipments/hour | Mean/max started wait (seconds) | Bay/worker/forklift utilization (%) | Rank |
| --- | --- | ---: | ---: | ---: | --- | --- | --- |
| Unchanged baseline | 3/10/2 | 63 | 32 | 7.875 | 4650 / 9300 | 66.319 / 19.896 / 99.479 | reference |
| Add forklift | 3/10/3 | 87 | 8 | 10.875 | 1990.909 / 2400 | 91.319 / 27.396 / 99.621 | 1 |
| Add loading bay | 4/10/2 | 63 | 32 | 7.875 | 4650 / 9300 | 53.056 / 19.896 / 99.479 | 2 |
| Add worker | 3/11/2 | 63 | 32 | 7.875 | 4650 / 9300 | 66.319 / 18.508 / 99.479 | 3 |

The third forklift adds 24 completions (+38.095%), reduces final queue by 24 and
mean started wait by 2659.091 seconds (-57.185%). One extra bay or worker has zero
effect on these operational objectives under this model; their utilization drops
because available capacity rises. They tie on outcome and added-resource count,
so name orders bay before worker. Controlled probes therefore support forklifts as
a **likely bottleneck under the tested completion objective and conditions**.
These are reproducible counterfactual model outcomes, not a purchasing recommendation.

## Tests, benchmarks and renderer follow-up

The milestone adds ten analysis CTest cases and six CLI cases to the original 68.
Debug and Release run 84 cases, all headless. Coverage includes all resource types,
v1 fixtures and v2 replay, historical utilization, same-tick FIFO starts, preflight
failures, candidate order/isolation/repeatability, all objectives/ties/nulls, limiting
bay/worker/forklift cases, joint constraints, short horizons and unavailable probes.
Export tests check UTF-8, quotes/commas, column count, nulls, locale-independent
formatting, existing/missing outputs and tampered report replay.

Seed `0x5F102026` drives 100 bounded experiments with randomized capacities,
checkpoints, candidates and objectives. Tests reorder candidates, preserve the
source snapshot, verify conservation, independently apply/record interventions,
compare raw metrics and replay exact final operational state. Failures log seed,
iteration, checkpoint and scenario. Existing 100K warehouse stress, ECS, World
snapshots and movement recordings remain active. SDL builds; interactive testing
was not performed.

The Release comparison benchmark uses 3/10/50 candidates over 10K arrivals, a
shared H/2 checkpoint, one warmup and seven trials. It separates validation,
checkpoint creation, branch creation, execution, metrics, derived deltas/ranking,
JSON/CSV serialization, batch cost and end-to-end cost. Controlled probes and
evidence generation are included in batch cost; validation/persistence stay outside
normal event dispatch. File I/O and console output are excluded. See
[comparison benchmark measurements](comparison-benchmarks.md) for actual values,
before/after warehouse performance and checkpoint validation overhead.

The implementation is serial and bounded, with no optimization search, monetary
costs, domain RNG, schema auto-downgrade or new operational scheduling policies.
Reproducibility assumes identical authoritative state, inputs, rules and supported
execution environment. Ratios are displayed as doubles; fixed-step timing alone
does not guarantee all simulation systems' cross-hardware determinism. The earlier
ECS destruction regression is unchanged and outside this milestone.

For the custom Vulkan renderer milestone, first agree a read-only visualization
contract for simulation time, shipments/queue/resources and selected branch metrics.
Keep renderer resources and frame timing out of warehouse snapshots and operational
ordering. Preserve headless simulation/CTest as a required path, then add a minimal
SDL/Vulkan visualization with explicit GPU ownership and render interpolation.
No renderer implementation is included here.
