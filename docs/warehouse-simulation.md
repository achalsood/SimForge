# Operational warehouse simulation

This is a small operational queueing model, not a calibrated predictor of a real
warehouse. Shipments arrive periodically, wait FIFO, reserve one loading bay,
one worker and one forklift together, and occupy all three for a fixed processing
duration. Completion releases the reservation and permits the next waiting
shipment to start. There is no travel, layout, routing, inventory, picking, shift
schedule, failure, shipment variation, preemption, or resource priority policy.

`simforge::warehouse::WarehouseSimulation` owns the domain state: shipment records,
queue, EventScheduler, ResourceManager, SimulationClock, and integer metric totals.
It accepts the validated ScenarioDefinition. It has no Engine, SDL, Vulkan,
networking, UI or AI dependency. Abstract shipments/events/resources are not ECS
entities. World still owns only its existing component model; warehouse state is
not implicitly included in existing World snapshots or movement recordings.

## Schema v1 compatibility and limits

Warehouse retains its required `loading_bays`, `workers`, `forklifts`, and
`shipment_interval_seconds` fields. New optional `processing_duration_seconds`
defaults to **600 seconds** for files that omit it. It is an integer in 1..604800.
Canonical output includes the explicit processing duration. The schema version
remains 1; existing files remain readable. Older executables with strict field
validation will not understand the new field. Movement schema/recording identities
and binary snapshot formats are unchanged.

The checked-in baseline explicitly uses **900 seconds** to demonstrate resource
constraints. This differs from the legacy omission default deliberately; loading
a legacy file does not silently adopt the example's processing duration.

All scenario times remain integer seconds, tick rates integer 1..1000 Hz, and
duration positive 1..604800 seconds with at most 10M horizon ticks. Multiplication
by the tick rate is exact. Fractional seconds, zero-duration processing, and
fractional/zero arrival intervals are rejected rather than rounded. Every accepted
duration therefore aligns exactly to ticks; no rounding rule or same-tick infinite
loop is needed. The existing schema rejects a zero-length simulation horizon.

Resources can be zero, with total capacity at most 100000. Arrivals are additionally
limited to **100000**, computed as `1 + (duration_seconds - 1) / interval_seconds`.
Configurations previously accepted only as metadata that exceed this new runtime
quota are rejected explicitly. The existing 4 MiB JSON input cap and strict
numeric/field/UTF-8 rules still apply. Parsing is separate from execution.

## Exact event and horizon semantics

- First arrival is at tick zero. Later arrivals are at integer multiples of the
  interval, strictly **before** the horizon: `[0,H)`.
- Completion at exactly H is processed and counted; events after H are not executed.
- No new shipment begins service at H, including an older waiting shipment freed
  by a horizon completion. It remains waiting. This avoids starting new work after
  the observation interval closes.
- Scheduler order is `(tick, type precedence, insertion sequence)`: completion
  before arrival, then stable sequence order within each type.
- Arrival enqueues its ID, schedules the next arrival if before H, then attempts
  to start waiting work. Completion releases resources, records completion, then
  attempts waiting work before the next due event.
- Shipment IDs are contiguous arrival-order integers local to the runtime.
  Completion order can differ from arrival order; service starts remain FIFO.
- A service starting at t has completion tick `t + processing_ticks`. Future
  completion events remain pending after H, and their shipments remain in progress.
  Queued/in-progress work is not drained beyond the horizon or reported completed.

EventScheduler is a bounded min-heap with monotonically increasing sequence IDs.
It validates event type/ID, rejects past scheduling, and retains a per-shipment/type
scheduled bit so neither duplicate scheduling nor rescheduling a processed event
is accepted. Sequence/marker state is unchanged by rejected scheduling. Its watermark
advances to the popped event tick, or the requested limit when nothing is due.
The domain executes through `advance_to(tick)` only for `current <= tick <= H`.
The initial zero-time arrival is processed on the first advance (including
`advance_to(0)`), not during construction. `run()` advances to H and is idempotent
after completion. `advance_by(count)` checks the remaining horizon before addition.
Use a new runtime for a clean run, or restore a [warehouse checkpoint](warehouse-snapshots.md)
to continue the full operational state from a reached boundary.

The clock fast-forwards to event ticks using explicit integer `advance_ticks`.
Between events, busy time is integrated as `active_reservations * delta_ticks`.
There is no per-tick scan, frame timing, or wall-clock dependency. Pause/time scaling
are not exposed as warehouse controls in this milestone.

## Reservations, queue, memory and safety

ResourceManager's maximum concurrency is `min(bays, workers, forklifts)`. Each
successful reservation increments one shared active-job counter: usage of each
resource pool equals that counter because every job requires one of each. Failed
acquisition leaves all usage unchanged. A per-shipment reservation ledger checks
duplicate acquisition, missing/wrong-ID release, and double release before changing
counters. No identities for individual resources are needed by this model.

If any capacity is zero, concurrency is zero. Every arrival queues and none start;
the model does not substitute a positive default. Reservations never exceed any
pool's capacity and cannot become negative.

The FIFO is a pre-reserved vector of shipment IDs with a monotonically advancing
head. This straightforward container retains consumed entries until destruction,
but avoids deque allocation during execution. Only the head is tested; if it
cannot acquire all resources, service attempts stop without scanning later entries.

Construction reserves all shipment/queue records, reservation/scheduling ledgers,
and at most `min(capacities, arrivals) + 1` pending heap entries (active completions
plus the next arrival). Valid event execution allocates no memory. Storage is
O(arrivals + concurrency), execution O(events * log(concurrency + 1)). Counters and
durations are bounded before construction: H <=10M, arrivals <=100K, interval and
processing ticks <=604800000. Scheduling additions are also checked explicitly.
Integer busy/wait/processing aggregates fit uint64 under these limits.

Invalid advance requests are rejected before mutation. Constructor/validation
failure exposes no partial runtime. Internal invariant failures indicate a bug;
there is no general transactional event rollback API. No concurrent mutation is
supported. `shipments()` returns a read-only view of live domain records, including
started/completed timestamps; it belongs to the runtime's lifetime and reflects
later advances. Records are preallocated, but a saved span's size does not grow
when new shipments arrive. IDs are not World Entity handles.

## Metrics and denominators

Metrics are queried from actual event records and maintained integer totals. At
intermediate advances they describe the observed elapsed tick interval; after
`run()` that is the configured horizon H. Let T be observed elapsed ticks and R
the tick rate. Fractions below are converted to doubles only for presentation.

| Metric | Definition |
| --- | --- |
| Arrived / started / completed | Actual arrival, reservation-start, completion counts |
| Waiting / in progress | Unstarted queue length / active reservations |
| Throughput per hour | `completed * 3600 * R / T`; unavailable at T=0 |
| Mean queue wait | `sum(start - arrival) / started / R`; all started shipments, including unfinished processing |
| Maximum queue wait | Maximum `start - arrival` among started shipments, divided by R |
| Total completed processing | `sum(completion - start)` for completed shipments only, divided by R |
| Mean completed processing | Total completed processing seconds / completed count |
| Pool utilization | `occupied_resource_ticks / integral(pool_capacity dt)`; equals `busy/(capacity*T)` for fixed capacity; a fraction, CLI displays percent |

Waiting mean/maximum are unavailable when no shipment started, and exclude still
waiting shipments (their waits are censored). Completed-processing mean is unavailable
without completions. Total completed processing is then zero. Pool utilization is
unavailable for zero capacity or T=0; a positive-capacity but unused pool correctly
has 0% over a positive interval. Busy totals include partial time through T for
unfinished jobs; they never include queued work. The common occupied numerator
applies separately to each pool because each reservation occupies one of each.

Conservation is checked by metric finalization:
`arrived = waiting + in_progress + completed` and
`started = in_progress + completed`. Resource usage and queued service do not
generate invented operational metrics. Normalized hourly throughput over a short
observation interval is not a steady-state real-world throughput prediction.

## Independently solvable example

[warehouse-analytical.json](../examples/warehouse-analytical.json) has H=10 seconds,
arrivals every 2 seconds, processing for 3 seconds, and capacities 1/1/1:

| Shipment | Arrival (s) | Start (s) | Completion within horizon (s) | Wait (s) |
| ---: | ---: | ---: | ---: | ---: |
| 0 | 0 | 0 | 3 | 0 |
| 1 | 2 | 3 | 6 | 1 |
| 2 | 4 | 6 | 9 | 2 |
| 3 | 6 | 9 | Pending at 12 | 3 |
| 4 | 8 | Waiting | None | Not yet measured |

Therefore arrivals=5, started=4, completed=3, waiting=1, in-progress=1. Mean started
wait is `(0+1+2+3)/4 = 1.5` seconds, max wait 3 seconds. Completed processing totals
9 seconds with mean 3. Reservations occupy the whole `[0,10)` interval, so each
pool has 100% utilization, including one unfinished service's final second.
Normalized throughput is `3 / (10/3600) = 1080` shipments/hour. No arrival at 10
or completion at 12 is executed.

## Eight-hour resource comparisons

All three examples use 60 Hz, H=28800 seconds, arrivals every 300 seconds and
processing for 900 seconds. Bays/workers remain 3/10; forklifts change from 1 to 3.
Every result below was executed by the Release CLI:

| Scenario | Arrived | Started | Completed | Waiting | In progress | Completed/hour | Mean/max started wait (minutes) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| [Constrained](../examples/warehouse-constrained.json), 1 forklift | 96 | 32 | 32 | 64 | 0 | 4.000 | 155 / 310 |
| [Baseline](../examples/warehouse-baseline.json), 2 forklifts | 96 | 64 | 63 | 32 | 1 | 7.875 | 77.5 / 155 |
| [Higher capacity](../examples/warehouse-higher-capacity.json), 3 forklifts | 96 | 96 | 94 | 0 | 2 | 11.750 | 0 / 0 |

Utilization (bays / workers / forklifts): constrained 33.333% / 10% / 100%;
baseline 66.319% / 19.896% / 99.479%; higher 98.958% / 29.688% / 98.958%.
Completed processing totals are 28800 / 56700 / 84600 seconds respectively; mean
completed processing is 900 seconds in all three cases.

Forklifts are a real constraint here, so their increase improves completions and
waiting. More resources do not always improve throughput: increasing forklifts
while a one-bay bottleneck remains cannot change shipment service times. Tests
exercise bay, worker and forklift bottlenecks separately and non-bottleneck increases.

## Running, tests and determinism

```powershell
build/windows-release/apps/sandbox/simforge_sandbox.exe --headless --scenario examples/warehouse-baseline.json
build/windows-release/apps/sandbox/simforge_sandbox.exe --headless --scenario examples/warehouse-constrained.json
build/windows-release/apps/sandbox/simforge_sandbox.exe --headless --scenario examples/warehouse-higher-capacity.json
build/windows-release/apps/sandbox/simforge_sandbox.exe --headless --scenario examples/warehouse-analytical.json
build/windows-release/benchmarks/simforge_warehouse_benchmark.exe
```

CLI prints actual counts, measured waits/processing/utilization, and separate
initialization/execution/metric wall durations. Those durations do not drive
simulation. Movement, recording/replay and argument-free SDL mode remain available.
Warehouse snapshots, incremental continuation, resource-capacity commands and
ordered command recordings are described in [warehouse snapshots and branching](warehouse-snapshots.md).
The existing movement recording workflow remains unchanged. Continuation beyond
the configured horizon is not supported.

Integer scheduling/state/aggregates have stable ordering for identical validated
configuration and clean initialization. Tests verify repeated logical records and
metrics, including different supported rates. Presented double ratios still follow
the existing supported-environment assumptions; no universal floating-point bitwise
or real-warehouse accuracy claim is made. Random seed is retained but unused by this
fully deterministic domain model.

The eight domain CTest cases cover scheduling, resources, analytical metrics, horizon,
capacity, schema/limits, 300 seeded property scenarios (`0x5F082026`), and 100K stress.
The randomized oracle independently assigns FIFO jobs to the earliest available
server with `start=max(arrival, earliest_release)`, instead of implementing another
event heap. It checks full timestamps/statuses, aggregates, event counts and resource
bounds. Failures print seed, iteration and complete scenario parameters. Three new
CLI cases check constrained/higher/analytical results. Existing regression tests
remain headless. See [benchmark measurements](warehouse-benchmarks.md).
