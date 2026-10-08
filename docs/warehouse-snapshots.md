# Warehouse snapshots, replay and branching

Commit #10 extends capacity changes to bays/workers/forklifts and adds snapshot and
recording schema 2 while preserving the v1 formats below. See the
[generalized commands and compatibility contract](scenario-comparison.md) for v2
history records, command identifiers and batch comparison. Forklift-only v1 data
remains byte-for-byte readable/writable.

The warehouse can now continue from a complete operational checkpoint. A branch
inherits shipment history, FIFO wait, active reservations, pending completions,
future arrivals, insertion sequences, clock time and accumulated metrics. It does
not start another day or restart jobs already in progress.

## Definitions and execution

| Concept | Meaning |
| --- | --- |
| Scenario definition | Validated immutable starting configuration, horizon, rate, periodic arrival and fixed processing duration; its canonical JSON is the exact compatibility identity |
| Runtime | Mutable shipments, reservations, FIFO, event heap, clock and integer accumulators |
| Snapshot | An independent logical value containing all state needed for continuation |
| Recorded commands | Accepted capacity adjustments with logical ticks and contiguous sequence numbers, wrapped with initial and expected final snapshots |
| Branch | A separately owned runtime constructed from a snapshot, optionally given different subsequent commands |
| Metrics | Read-only observations of actual work through the reached tick; derived ratios are not a replacement for operational state |

`WarehouseSimulation::advance_to(tick)` accepts `current <= tick <= H`;
`advance_by(count)` checks the remaining horizon before adding. Backward,
beyond-horizon and overflow-sized requests fail before changing state. The clock
is the existing private `SimulationClock`; its integer tick is the sole time
authority. The scheduler watermark follows that clock after each advance.
Rendering frame time, pause, scaling and wall time do not drive warehouse execution.
Only the clock's const interface is exposed; warehouse snapshots reconstruct its
fixed rate/tick state with zero remainder, unpaused and scale one.

Time skips directly to due events and then to the target. Continuous and chunked
execution preserve the existing ordering `(tick, completion-before-arrival,
insertion sequence)`. Arrivals occur in `[0,H)`, completions at H count, new work
never starts at H, and future completions remain pending. Advancing to the current
tick is idempotent after settlement. The initial constructor deliberately retains
arrival zero pending: its first `advance_to(0)` settles that arrival.

## Snapshot schema 1 and capture boundaries

The public value types are in `warehouse/warehouse_snapshot.hpp`:

- Version, canonical scenario identity, tick rate, horizon, processing ticks and
  current tick; `settled` distinguishes untouched construction from a reached boundary.
- Every arrived shipment's contiguous logical ID, status, arrival/start/completion
  ticks and next shipment ID. Optional timestamps distinguish unstarted and unfinished work.
- Exact unconsumed FIFO order and sorted active reservation IDs.
- Current capacities and usage of all three resource pools.
- Pending events in canonical dispatch order, with type, shipment, tick and insertion
  sequence; next sequence and the historical peak pending count.
- Started/completed counts, wait sum/maximum, completed processing ticks, occupied
  resource ticks, processed events and integrated available ticks for each pool.
- Ordered capacity history, retaining each command's tick, resource, new count and
  `started_before` count. That small additional value distinguishes reservations
  acquired between commands at the same tick.

Arrival count is the shipment vector size; waiting/active counts are the FIFO and
reservation sizes. These are exact derived counts rather than redundant counters.
Original configuration, interval, arrival limit and seed are included in the
canonical identity and checked against explicit timing fields. Reservation and
ever-scheduled ledgers are rebuilt from validated lifecycle/event records. Consumed
FIFO slots and heap layout are implementation details, not snapshot state.

Capture is const and deep-copies values. It works before initial settlement, after
any reached tick and at H, and remains valid after source mutation/destruction.
Public operations never expose a partially dispatched event. Every settled snapshot
has only events strictly after its current tick. The only allowed same-tick pending
event is arrival zero in an otherwise untouched constructor snapshot. Snapshots
exclude frame time, addresses, allocator capacity and resource-object identities.
Shipment IDs are local integer values: corresponding branches retain the same IDs
but own separate shipment storage. Successful restore replaces storage and invalidates
previous shipment views/references; rejected restore preserves them.

```cpp
using namespace simforge::warehouse;
WarehouseSimulation baseline{definition};
baseline.advance_to(7200ULL * definition.simulation.tick_rate);
const auto checkpoint = baseline.capture_snapshot();
auto a = WarehouseSimulation::create_from(checkpoint);
auto b = WarehouseSimulation::create_from(checkpoint);
b.set_forklift_capacity(3);
a.run();
b.run();
```

`create_from` returns a noncopyable runtime by guaranteed C++ copy elision. No
runtime or snapshot shares mutable buffers with another branch.

## Validation and transactional restoration

`restore_snapshot` requires exact scenario identity, even when restoring an earlier
tick into an advanced runtime. It validates and builds a replacement, including
all reservations, scheduling markers and heap storage, before a noexcept swap.
Invalid input or allocation failure leaves the destination's complete logical state
unchanged. Branch construction follows the same validation path.

Validation uses runtime checks, including version/container limits, canonical
warehouse identity, rate/horizon/processing compatibility and bounded current time.
It checks expected arrival count and periodic ticks, unique contiguous shipment
IDs, timestamp/status consistency, FIFO start prefix and nondecreasing start ticks,
exact queue/reservation membership and pool usage, pending event membership/type/
payload/time/order, unique in-range sequences and exact next-ID counters. Completed
shipments cannot hold reservations, and every active shipment has exactly one
future completion. Settled waiting work must have a current resource bottleneck
unless the horizon is closed. Peak-pending statistics must fit historical occupancy
and event-count bounds.

Integer metric totals are independently derived from stored lifetimes. A sorted
audit of start/completion/capacity boundaries checks historical resource occupancy,
including reductions and equal-tick changes. It validates available-capacity
integrals and partial busy time without restarting service or re-executing historical
arrivals. Counts and times are bounded before arithmetic; at most 100K arrivals,
10M horizon ticks, 604800000 processing ticks and 100K combined resources keep all
accepted uint64 aggregates safe. Validation is O((shipments + changes) log(shipments
+ changes)) with bounded temporary storage, outside normal event dispatch.

Validation proves structural/lifecycle/resource/accounting consistency, not the
authorship of history. A deliberately edited but consistent snapshot is an authoritative
checkpoint; pending sequence values are checked for uniqueness/range rather than
authenticated against a retained log of every consumed event. A recording's exact
expected state additionally verifies its supported continuation inputs.

## Capacity changes and intermediate metrics

`set_resource_capacity(type, count)` and the retained `set_forklift_capacity(count)`
require a settled boundary. Call `advance_to(0)`
before changing an untouched runtime. Due events, including arrivals, settle before
the command. Existing reservations remain held; reducing below current usage or
exceeding the 100K combined-resource limit is rejected. Generalized commands change
one selected pool; the arrival schedule, horizon and service duration stay fixed.
There is no preemption or cancellation.

The command immediately starts eligible waiting jobs FIFO at its current tick,
except at H. Their original arrival/wait history is retained. Existing service
start/completion ticks stay unchanged. Newly scheduled completions use the existing
monotonic insertion counter. Multiple commands at the same tick run in acceptance
order; a later reduction sees work started by an earlier increase. Equal-capacity
commands are accepted and retained in history. Capacity history is bounded to 100K
entries. Heap reservation and history allocation precede logical mutation; the
subsequent starts use buffers reserved for the validated workload.

Metrics are constant-time, const and idempotent. Elapsed time is the reached tick,
not automatically H. Active jobs contribute busy time through that tick; only
completed jobs contribute completed-processing statistics. Started waiting mean/
maximum retain their established semantics, excluding still-waiting censored waits.
At zero elapsed time, throughput and utilization are unavailable. With a positive
observation interval, a positive available-capacity denominator and no work yield zero.

Pool utilization is `occupied_resource_ticks / integral(capacity dt)`. After a
forklift adjustment this uses actual capacity history, so earlier availability is
not retroactively changed. For fixed capacity it equals the previous `busy/(capacity*T)`
formula and preserves all original example results. The two conservation equations
`arrived = waiting + active + completed` and `started = active + completed` hold at
every valid boundary.

## Persistence

Warehouse binary schema 1 uses the separate eight-byte magic `SFWHSE\0\0`.
The World/Simulation binary schema `SFSTATE\0` remains unchanged. All fields are
explicit little-endian unsigned integers, with booleans/enums as bytes and optional
timestamps as a 0/1 byte followed by uint64 only when present. No raw structs,
padding or C++ container memory are serialized. The warehouse file contains no
floating-point fields; presentation ratios are recomputed from integer totals.

The field order is:

1. Magic, uint32 version, uint32 UTF-8 identity byte length and canonical identity bytes.
2. uint32 rate; uint64 horizon, processing and current tick; settled byte; uint32 next shipment.
3. uint32 shipment count and records: uint32 ID, status byte, uint64 arrival, optional start and completion.
4. uint32 FIFO count/uint32 IDs; uint32 reservation count/uint32 IDs; two capacity/usage triples of uint32.
5. uint32 event count and records: uint64 tick, type byte, uint64 sequence, uint32 shipment.
6. uint64 next sequence and peak pending; uint32 history count and `(uint64 tick, uint64 started_before, uint32 forklifts)` records.
7. Ten uint64 totals in the declaration order of `WarehouseTotals`.
8. uint64 FNV-1a checksum over all preceding bytes.

The input quota is 32 MiB, with scenario identity at most 4 MiB, at most 100K
shipments/FIFO/reservations/history entries, and at most 100001 pending events
before tighter semantic validation. Lengths are checked against both quotas and
remaining bytes before allocation. Deserialization checks checksum, magic, version,
boolean representation, enum semantics, truncation, trailing bytes and full snapshot
validity. Correct-checksum malicious payloads are still validated. Encoding is
deterministic for equal logical values. FNV-1a detects accidental corruption; it is
not authentication. Schema 1 is portable between supported little/big-endian hosts
through explicit decoding, subject to scenario/model compatibility; future schema
changes require explicit migration. File helpers perform bounded binary reads and
exclusive creation of a new output file; they do not replace existing files.
Status byte values are waiting=0, in_progress=1 and completed=2; event types are
completion=0 and arrival=1. These values and field order are part of schema 1.

## Commands, replay and divergence

`warehouse/warehouse_input.hpp` follows the established input-recording conventions:
typed logical commands, contiguous sequence numbers starting at zero, canonical
scenario identity, embedded initial snapshot and exact expected final snapshot.
It uses the existing strict JSON/file infrastructure and the existing lowercase-hex
binary-snapshot convention. The warehouse envelope adds `domain: "warehouse"` and
uses warehouse binary snapshots; existing movement recording types/JSON stay unchanged.
There is no generalized scripting or domain-conversion framework.

Recording v1 supports `set_forklift_capacity`, with `tick`, `sequence` and `forklifts`.
Recording v2 also supports `set_resource_capacity` with `resource` and `capacity`,
without reinterpreting the legacy command. History parameter values use the C++
field `capacity` (formerly `forklifts`).
Commands apply after all due events at their tick, then in sequence order for equal
ticks. Accepted commands are recorded; rejected commands change neither runtime
nor log. The recorder's caller must exclusively mutate the runtime through its
advance/command methods until capture. Recordings are bounded to 64 MiB/100K commands.
Parsing rejects unknown/duplicate fields, unsupported commands, malformed numbers,
bad hex/snapshots, identity/time/history mismatch and incorrect ordering.

Replay requires the destination to equal the embedded initial logical state.
Restore that checkpoint explicitly before replaying into an advanced destination.
Replay executes in an isolated branch, verifies the full expected state and commits
through transactional restore. Wrong initial state/scenario, invalid commands,
failed allocations or divergent outcomes leave the destination unchanged. Arbitrary
nonzero initial checkpoints are authoritative validated states, matching the movement
recording concept; they need not be reconstructed from scenario tick zero.

`first_difference` compares every snapshot field exactly, including FIFO, event
order/counters, histories and raw metrics, with no checksum-only equality. It reports
the reached tick and first differing field/index; shipment status diagnostics include
expected/actual values. Container capacities, heap layout and addresses are ignored.
Pure continuation can be verified by restoring either an in-memory checkpoint or
its serialized round trip and comparing full horizon snapshots.

## CLI example and measured outcomes

```powershell
build/windows-release/apps/sandbox/simforge_sandbox.exe --headless --scenario examples/warehouse-baseline.json --compare-forklifts 2,3 --checkpoint-seconds 7200
build/windows-release/apps/sandbox/simforge_sandbox.exe --headless --scenario examples/warehouse-baseline.json --compare-forklifts 2,3 --checkpoint-seconds 7200 --record build/warehouse-inputs.json
build/windows-release/apps/sandbox/simforge_sandbox.exe --headless --warehouse-replay build/warehouse-inputs.json
```

Both comparison options must be supplied together with a headless warehouse
scenario. The first count must retain that scenario's baseline forklifts; the
second is applied at the checkpoint after due events. Checkpoint seconds are an
unsigned integer in `0..duration`. Comparison conflicts with replay and entity/tick
overrides. Warehouse `--record` requires the comparison demonstration;
`--warehouse-replay` avoids ambiguity with the existing movement `--replay` format
and optionally verifies `--scenario` identity. Output files must be new.

Actual Release output on October 8, 2026, with the checked-in eight-hour baseline:

| Observation | Arrived | Started | Completed | Waiting | Active | Completed/hour | Mean/max started wait (minutes) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Shared checkpoint at 2 hours, 2 forklifts | 25 | 17 | 15 | 8 | 2 | 7.500 | 18.824 / 40 |
| Branch A, retain 2 | 96 | 64 | 63 | 32 | 1 | 7.875 | 77.500 / 155 |
| Branch B, change to 3 at 2 hours | 96 | 88 | 87 | 8 | 1 | 10.875 | 33.182 / 40 |

Branch B minus A: **24 additional completions, 24 fewer waiting shipments**.
Completed processing is 56700 / 78300 seconds. Bay utilization is 66.319% / 91.319%,
worker utilization 19.896% / 27.396%, and forklift utilization over capacity history
99.479% / 99.621%. Branch B's denominator is `2*7200 + 3*21600 = 79200` forklift-seconds.
The CLI verifies serialized baseline continuation and capacity-command replay exactly.
These are counterfactual outcomes of the implemented fixed-duration FIFO model,
not calibrated real-world predictions. Adding a forklift at two hours gives 87
completions; starting the original higher-capacity example with three gives 94.

## Tests, performance and limits

Commit #9 ran 68 CTest cases in both Debug and Release: all original 48 plus nine warehouse
snapshot cases, branch CLI, six invalid comparison cases and four recording fixtures.
Commit #10 retains those cases and extends both suites to 84 with batch analysis tests.
New coverage includes constructor/settled zero/intermediate/horizon capture,
analytical occupancy/utilization, large pending sets and queues, transactional
invalid restores, every truncation/one-byte corruption of a representative binary,
correct-checksum malformed payloads, file round trips, exact serialized continuation,
same-tick commands, isolated replay failure and branch ownership.

Seed `0x5F092026` drives 150 configurations with 80 bounded operations each,
covering advances/capture/restore/branch/capacity/serialization. Every operation
checks invariants and chunked versus continuous continuation. Failures print seed,
iteration, operation and complete scenario. The original independent queueing oracle
and 100K-arrival tests remain active. All automated execution is headless, without
SDL/Vulkan initialization. The SDL target builds; interactive window testing was
not performed for this milestone.

Normal events remain preallocated and event-driven; snapshot validation/serialization
are never run in dispatch. Capacity changes may allocate for heap growth/history,
but preserve existing service. Capture is O(shipments + queue + pending events),
plus canonical sorting of pending events; restoration/branching additionally audit
history. Temporary snapshot/persistence buffers are separate from runtime storage.
See [measured before/after and checkpoint benchmarks](warehouse-benchmarks.md).

Determinism assumes identical validated logical state, ordered accepted inputs,
model rules and supported execution environment. Integer warehouse state and
ordering are exact; displayed floating-point ratios inherit environment assumptions.
Fixed-step timing alone does not make other simulation systems deterministic across
hardware. No multithreading, domain RNG, preemption, horizon
extension, schema migrations, general policy scripts or calibrated real-world
prediction were introduced. The earlier ECS destruction regression is outside scope.
An appropriate next milestone is structured batch comparison/export built on these
verified branches, with an explicitly agreed metric/output contract.
