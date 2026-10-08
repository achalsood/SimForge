# World snapshots and movement replay

Snapshots are deep copies of logical simulation state. `WorldSnapshot` contains
every slot (living, free or retired), generations, the generation ceiling, free
list links/head, living count, and Transform/Velocity records in observable dense
order. `ClockSnapshot` contains rate, tick count, fractional remainder, pause and
scale. `SimulationSnapshot` combines both. All three schemas currently use version
1. Capacity, sparse-array padding, ownership tokens, frame timing, and OS timestamps
are not simulation state and are not captured.

World methods capture/restore standalone ECS state. Free functions in
`simforge/simulation/simulation_snapshot.hpp` capture/restore a combined state,
serialize/deserialize it, run fixed movement ticks, and compare results. They
depend only on World, the simulation clock, and MovementSystem; there is no platform,
graphics, networking or serialization-library dependency.

## Handles and restoration

Public Entity handles contain index/generation and a private shared ownership
token. Each World has its own token. Default handles and two-integer construction
produce unowned, invalid handles. Equality includes ownership; matching logical
IDs in different Worlds are not equal public handles. All World handle operations
check ownership. Tokens are retained by external handles, so constructing another
World at a reused memory address cannot revive an old handle. There is no global
counter, random identifier or global mutable state.

Dense component arrays store compact logical `EntityId` pairs, not ownership
tokens. `each` creates an owned handle for its callback; copy the handle if retaining
it beyond that callback. `each_components` supplies values only, avoiding token
copies in systems such as movement. Both APIs preserve dense order, filtering,
const access and the structural mutation guard. Copies of public handles involve
reference counting; prefer const references when processing existing handle lists.

Every successful restore creates a fresh ownership token, **invalidating every
existing destination handle**, including handles whose old index/generation match
the snapshot. This prevents rollback from reviving a destroyed handle. All existing
component references/pointers are invalidated as well. Reacquire intentionally with
`world.handle_at(index)`, which throws `out_of_range` if that slot is not alive.
This returns the current generation; callers interpreting historical logical IDs
should also check the generation against their intended record. Never treat an
EntityId as an owned public handle.

Logical indices/generations, retirement and reuse order are preserved exactly.
Capture neither rotates the token nor changes the source. `clear()` retains
generation history as before and invalidates live handles through generation
advancement. Restore can roll history back, but the fresh token keeps all earlier
handles invalid. Even restoring an empty or identical snapshot rotates ownership.
Creating multiple Worlds from the same snapshot produces independent storage and
tokens while preserving equivalent logical allocation behavior.

## Validation and exception safety

Restoration validates before building replacement state. It checks versions,
positive generation ceilings, nonzero/in-range generations, living count, slot
flags, all free-list links and cycles, complete free-slot membership, and retirement
rules. A free slot may have the maximum generation: it gets one final lifetime
before retiring. Retired slots must have that maximum generation, no free link,
and cannot be alive. Free slots must have generation at least 2. Live slots have
no free link. Components must reference living slots with matching generations,
have unique ownership per type, and contain only finite doubles.

Replacement slots and sparse sets are built independently in snapshot dense order.
Only noexcept swaps commit them. Validation/allocation failure leaves destination
data, handles and component references unchanged. Combined restoration validates
the clock into a local replacement first, restores World, then commits the clock
with noexcept assignment. It therefore preserves both on failure. Restore is
rejected during active iteration. Capture may allocate and can fail on nonfinite
source values; it does not sanitize or modify them.

Capture/restore accept at most 1,000,000 slots. The normal ECS does not impose this
snapshot-specific limit. Serialized input/output is additionally capped at 64 MiB.
These are record/encoded-size limits, not a strict peak process-memory limit:
decoding and replacement state need additional bounded allocations. Out-of-memory
errors propagate. Capture at consistent system/tick boundaries; no synchronization,
concurrent snapshots, transactional movement or rollback-on-update-error is provided.

## Clock continuation and replay

Clock restoration preserves its integer numerical representation. Remainder is
in nanosecond-times-rate units modulo 1,000,000,000. The restored rate replaces the
destination rate; there is no rate setter otherwise. Elapsed simulated seconds
are derived as ticks/rate rather than serialized redundantly. Pause and finite
nonnegative scale are restored exactly. Single-step is immediate in this clock,
so there is no pending step queue. Wall/frame clocks remain separate.

`replay_fixed_ticks(world, clock, count)` executes MovementSystem once per tick,
then increments the simulation clock. It uses only the restored fixed step and
bypasses pause/scale, just like explicit `advance_ticks`. Tick overflow is checked
before any world updates. It records no events and introduces no command framework.
For elapsed-input replay, restore the combined state, feed the same nanosecond
inputs to `clock.advance`, and execute each returned batch in the same system order.
Pause/resume/scale changes and explicit steps must be replayed in their original order.

Identical initial logical state, ordered lifecycle/input/control operations, system
order, and supported execution environment reproduce equivalent finite final state
for MovementSystem. Tests compare actual component values exactly, including after
serialized restore. This is not a cross-compiler/hardware bitwise determinism promise.
Floating-point overflow, fast-math settings, new systems, concurrency, random seeds,
and external inputs need their own policies.

`first_difference(expected, actual)` validates snapshots and compares allocation
history, dense component ownership/order/values, and clock state. It returns no
value for equality, otherwise the first path with expected/actual values and the
expected tick. For example: `tick 600, world.Transform[dense=0,entity=0].position.x`.
It uses exact finite numeric equality, where signed zeros are equal. It ignores
addresses, tokens and capacity. No hash is used as proof of state equality.

Serialization preserves signed-zero bits, so logical equality does not imply
byte-identical encoding. Identical supported field values/bit patterns always
encode in the same order. The file checksum is an integrity aid, not an equality
proof or security boundary. For comparisons across systems, schema/version and
floating-point compatibility still matter.

## Binary schema version 1

The initial format is a small fixed schema without a serialization dependency.
All unsigned integers are explicitly little-endian. Doubles are IEEE-754 binary64,
encoded through their integer bit representation in little-endian order. Hosts
must provide 8-byte IEC 559 doubles (compile-time checked). No object layouts,
pointers, container memory, or padding are written. Booleans are one byte, exactly
0 or 1. Counts are u32. All fields below are required and occur in this order:

| Section | Ordered fields |
| --- | --- |
| Header | Eight bytes `SFSTATE` followed by NUL; simulation version u32 |
| World header | World version u32, generation limit u32, free head u32, live count u32, slot count u32 |
| Slots | Exactly slot-count records: generation u32, next-free u32, alive u8, retired u8 (10 bytes each) |
| Transform header | Transform count u32 |
| Transforms | Logical index u32, generation u32; position, rotation, scale, each x/y/z f64 (80 bytes each) |
| Velocity header | Velocity count u32 |
| Velocities | Logical index u32, generation u32; linear x/y/z f64 (32 bytes each) |
| Clock | Clock version u32, rate u32, ticks u64, remainder u64, paused u8, scale f64 (33 bytes) |
| Trailer | FNV-1a-64 checksum u64 over every preceding encoded byte |

Empty snapshots are 81 bytes. Total bytes are
`81 + 10 * slots + 80 * transforms + 32 * velocities`. The no-slot sentinel is
`0xffffffff`. Array order is slot index order and actual component dense order.
There is no optional field, implicit default or extension section in v1; missing,
truncated, extra fields and unsupported versions are rejected.

FNV-1a starts at 14695981039346656037, XORs each unsigned byte, then multiplies by
1099511628211 modulo 2^64. The checksum covers versions, counts, fields and magic;
it excludes its own trailer. It detects accidental corruption with possible hash
collisions and supplies no authenticity or malicious-tampering protection.

The decoder checks input size/header length and checksum before allocation. Each
record count is checked against the slot limit and remaining encoded bytes before
reserve/iteration. It then applies the same semantic validation as in-memory
restore. It returns a new snapshot; parsing does not mutate a World. File I/O,
JSON interoperability, compression, schema migration and component registration
are outside this milestone.

## Branching example

```cpp
auto baseline = simforge::simulation::capture_snapshot(world, clock);
simforge::world::World branch_a, branch_b;
simforge::simulation::SimulationClock clock_a, clock_b;
simforge::simulation::restore_snapshot(branch_a, clock_a, baseline);
simforge::simulation::restore_snapshot(branch_b, clock_b, baseline);
auto handle_a = branch_a.handle_at(0); // Only if logical slot 0 is alive.
// branch_b.valid(handle_a) is false.
branch_a.get<simforge::world::Velocity>(handle_a).linear.x = 2;
simforge::simulation::replay_fixed_ticks(branch_a, clock_a, 120);
simforge::simulation::replay_fixed_ticks(branch_b, clock_b, 120);
auto mismatch = simforge::simulation::first_difference(
    simforge::simulation::capture_snapshot(branch_a, clock_a),
    simforge::simulation::capture_snapshot(branch_b, clock_b));
```

## Verification and benchmarks

The seven new CTest cases cover capture/lifecycle/ownership, rejected restores,
clock continuation, serialization/corruption, replay/divergence/branches, a seeded
random ECS reference model, and a 100K-entity round trip. The model uses seed
`0x5F062026` for 3,000 operations and prints seed/operation on failure. It tracks
slots, a simple free stack, optional components and dense swap removal independently.
Corruption tests repair checksums to verify deeper validation, and include every
truncated prefix, invalid numeric fields/counts, and the 64 MiB input limit.

```powershell
ctest --preset windows-debug --output-on-failure
ctest --test-dir build/windows-release --output-on-failure
build/windows-release/benchmarks/simforge_world_benchmark.exe
build/windows-release/benchmarks/simforge_snapshot_benchmark.exe
```

Benchmarks use one warmup and seven measured runs at 1K/10K/100K, reporting separate
median capture, restoration, serialization and deserialization times. Setup and
final exact comparisons are outside timing; production API validation/checksum
costs and release of old state during restore are included. See
[measured results and regression comparison](snapshot-benchmark-results.md).
