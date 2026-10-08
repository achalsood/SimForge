# World and ECS foundation

`simforge::world::World` owns entity slots and typed component storage. It has no
SDL, Vulkan, rendering, wall-clock or AI dependency. The sandbox composes World,
Engine's SimulationClock, and MovementSystem; Engine does not own a World or choose
which systems run. Tests and benchmarks link only `simforge_engine`.

## Usage

```cpp
#include <simforge/world/world.hpp>
#include <simforge/simulation/movement_system.hpp>
#include <simforge/simulation/simulation_clock.hpp>

simforge::world::World world;
simforge::simulation::SimulationClock clock;
const auto entity = world.create_entity();
world.emplace<simforge::world::Transform>(entity);
world.emplace<simforge::world::Velocity>(entity, simforge::world::Vec3{1, 0, 0});

for (int tick = 0; tick < 60; ++tick)
{
    simforge::simulation::MovementSystem::update(world, clock.fixed_step_seconds());
    clock.advance_ticks(1);
}
const auto position = world.get<simforge::world::Transform>(entity).position;
world.destroy_entity(entity);
// world.valid(entity) is now false; position is a value copy.
```

For real-time scheduling, use the count returned by `clock.advance(elapsed)` and
call MovementSystem once per returned tick. Paused `advance` returns zero.
After `clock.single_step()` call the system exactly once. Scaling changes the
number of updates, never the fixed timestep. Explicit `advance_ticks` bypasses
pause and scale for headless drivers, as documented in [simulation timing](simulation-clock.md).
The clock counts scheduled ticks, not completed callbacks; it does not roll back
world state or tick counts if application updates fail.

## Entity identity and lifetime

An Entity is an index, a 32-bit generation and a private World ownership token.
Value equality includes ownership. Generation
zero and the default handle are invalid. Indices `0` through `UINT32_MAX - 1` are
available; `UINT32_MAX` is the free-list sentinel. `valid` and `has<T>` return
false for invalid/stale handles. Mutating operations and `get<T>` throw
`std::invalid_argument` for those handles. Double destruction is rejected.

Slots start at generation 1. Destroy removes every owned component, marks the
slot dead, and increments its generation before adding it to a LIFO free list.
Subsequent creation can reuse the index, but never the destroyed handle. Creation
and destruction are amortized O(1). A slot at its maximum generation is permanently
retired on destruction. Generations never wrap. The constructor's optional
generation ceiling defaults to `UINT32_MAX`; a lower ceiling can bound reuse and
lets tests exercise the real exhaustion path without billions of iterations.

`clear()` invalidates all live handles, clears components, and retains slot
generation history, retired slots, and allocated capacities. It does not reset
generations or free history. It is O(slot count + component count) and can be
called repeatedly. Releasing all generation history requires destruction or
snapshot restoration, which replaces ownership identity.

Handles are checked against their originating World's current ownership token.
Matching index/generation pairs from another World are rejected. Successful
snapshot restoration invalidates all previous handles and component references;
reacquire intentionally using `handle_at(index)`. World is noncopyable and nonmovable.
`entity_count()` reports only living entities. `reserve(count)` can preallocate
slots and all currently supported component arrays. See
[snapshots and replay](snapshots-replay.md) for token lifetimes and restore semantics.

## Component storage and references

The initial components are Transform (position, Euler rotation in radians, scale)
and Velocity (linear velocity). Vec3 uses three doubles without a math dependency.
Defaults are zero position/rotation/velocity and unit scale. No rotational motion,
physics, hierarchy, or rendering semantics are implemented.

A reusable `detail::SparseSet<T>` backs each type. A sparse index maps entity slots
to packed component and entity arrays. Lookup/has/remove are O(1), insertion is
amortized O(1), and iteration is linear in the first component's dense count.
Removal uses swap-and-pop and updates the moved entity's sparse index. The World
owns a fixed tuple of Transform/Velocity storages; adding a new supported type
requires a compile-time tuple/API change. There is no runtime registration,
reflection, archetype graph, or third-party ECS dependency. Types must have
nothrow move construction, move assignment, and destruction for safe relocation.

`emplace<T>` returns a component reference and rejects duplicates with
`std::logic_error`, preserving the existing value. `get<T>` throws
`std::out_of_range` for a missing component on a living entity. `remove<T>` returns
false for a missing component, true when removed. `component_count<T>()` reports
the dense count. Construction/allocation failures do not create partial components
or change entity liveness, but successful intermediate capacity changes can
invalidate existing references even when an operation later throws.

Reference/pointer lifetime follows packed-vector storage:

- Growth or reserve that reallocates invalidates all references to that type.
- Insertion without reallocation preserves existing component references.
- Removing a component invalidates its reference and the old reference to the
  last dense element. The removed element's address may now contain a different
  entity's component. Other elements remain in place.
- Destroy has the same effect on every attached type; clear invalidates all
  component references. Always reacquire by Entity after structural mutation.

`each<First, Rest...>(callback)` supplies an owned Entity and component references for
entities possessing every requested type. Mutable and const overloads are
provided. The callback walks First's dense order; insertion appends, removal moves
the last entry into the removed entry's position. Identical ordered lifecycle
operations give identical iteration order, but order is not sorted by entity ID.
Copy the callback handle if retaining it after the callback. `each_components`
supplies only the component references and avoids creating ownership-bearing
handles; MovementSystem uses this path. Internal storage uses compact logical IDs.

A scoped iteration guard rejects create/destroy/emplace/remove/clear/reserve/restore
during any active iteration, including mutations through aliases during const or
nested iteration. Value edits and nested reads are allowed. Exceptions release
the guard, so later structural edits remain possible. This prevents storage
relocation while callbacks hold references. Callbacks must not destroy the World
itself or retain component references across later structural mutations. There
are no concurrent access guarantees; callers synchronize externally.

## Movement and determinism

`MovementSystem::update(World&, double)` executes one allocation-free traversal
of Velocity storage, probing Transform. It applies `position += linear * dt` to
each matching entity. Entity cleanup ensures dead entities are absent. Unmatched
entities are skipped. The timestep must be finite and nonnegative; zero is a
no-op. Component numerical values are application-owned and are not sanitized;
overflow or nonfinite component values can propagate through floating-point math.

For identical initial data, ordered lifecycle operations, and fixed tick inputs,
the same supported execution environment produces identical results. Floating-
point state is not guaranteed bitwise identical across compilers, CPU architectures,
or compiler settings. Record input/control operations for replay. World state
serialization and movement replay are implemented with documented ownership and
versioned state. Domain random-seed policies and transactional updates remain future
work. See [snapshot design](snapshots-replay.md) for the exact current guarantees.

## Running and testing

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
build/windows-debug/apps/sandbox/simforge_sandbox.exe --headless --entities 10000 --ticks 600
```

Headless defaults are 10,000 entities and 600 ticks at 60 Hz. It creates transforms
and velocities `(1, 0.5, -0.25)`, runs every requested update, prints counts,
simulation seconds, separate setup and update wall durations, and consumes final
positions via a checksum. Checksum traversal and output are outside update timing.
Neither SDL nor Vulkan is initialized on this path. The executable still links
the platform library and the full build requires SDL. Running without arguments
retains the SDL event/window loop and also advances a one-entity movement world.

Counts must be unsigned decimal integers without trailing text; duplicates,
unknown flags, missing values, overflow, and entity/tick flags without headless
mode fail with a clear diagnostic. Zero entities/ticks are supported. Memory
allocation limits still apply to large valid counts. No hard workload cap is
imposed. Setup pre-reserves capacity; reported update time includes clock tick
accounting and MovementSystem calls, not setup or teardown.

Tests cover lifecycle/reuse/retirement/reset, invalid handles, storage growth and
swap removal, component cleanup, const access, iteration guards/exception recovery,
movement and clock controls, repeated results, a 100K-entity stress run, and CLI
validation. Tests make no assertions about real-world execution duration.

## Benchmarks

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
ctest --test-dir build/windows-release --output-on-failure
build/windows-release/benchmarks/simforge_world_benchmark.exe
build/windows-release/benchmarks/simforge_snapshot_benchmark.exe
```

The independent benchmark refuses Debug execution. It performs one warmup and
seven measured runs at each size. It reports separate median batch costs for
creation, insertion of both components, 120 movement ticks, and destruction.
World growth allocations are included; no World reserve is used. Harness handle
memory is reserved outside timing, though writing handles is part of creation.
Every final position is validated outside timing and checksums are printed to
consume results. Destruction cleanup and stale handles are also validated.
Do not compare the pre-reserved sandbox setup against benchmark insertion.

See [initial measured results](ecs-benchmark-results.md) and
[snapshot benchmarks and ownership costs](snapshot-benchmark-results.md).
Profiling and validated scenario input are reasonable next milestones.
