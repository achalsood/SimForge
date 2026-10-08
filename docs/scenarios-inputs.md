# Validated scenarios and reproducible inputs

Five different concepts are kept separate:

| Concept | Meaning |
| --- | --- |
| ScenarioDefinition | Validated configuration describing intended initial conditions and simulation budget |
| World | Mutable runtime ECS state, owned independently of rendering |
| SimulationSnapshot | Deep copy of logical World and clock state, including lifecycle history |
| InputRecording | Scenario identity, known initial snapshot, accepted typed inputs and expected final snapshot |
| Replay execution | Isolated reconstruction, ordered command application and exact final-state verification |

The `movement` adapter executes ECS movement, and `warehouse` executes a separate
operational model with periodic shipments, FIFO service, resource reservations and
measured metrics. See [warehouse semantics](warehouse-simulation.md). Input recordings
currently execute movement commands only. There is no AI, renderer, asynchronous
command queue or scripting interface.

## Scenario JSON schema v1

Every scenario requires `schema_version`, `name`, `domain`, and `simulation`.
`schema_version` must be integer 1. Name is 1..256 UTF-8 bytes without control
characters. Domains are exactly `warehouse` or `movement`.

`simulation` requires all three fields:

| Field | Type and limits |
| --- | --- |
| tick_rate | Integer 1..1000 Hz |
| duration_seconds | Integer 1..604800 seconds; duration times rate must be <=10,000,000 ticks |
| random_seed | Unsigned integer 0..UINT64_MAX; retained as identity/configuration, currently unused by movement |

A warehouse scenario requires `warehouse` and forbids `initial_world`.
`warehouse` requires integer `loading_bays`, `workers`, `forklifts`, each 0..100000
and their sum <=100000. `shipment_interval_seconds` is required, integer 1..604800.
Optional `processing_duration_seconds` is integer 1..604800, default 600 seconds
for legacy files; canonical output includes it explicitly. A warehouse execution
may contain at most 100000 arrivals strictly before its horizon. This new runtime
quota is validated before allocation; it deliberately rejects previously metadata-
only configurations with larger workloads.
See [warehouse example](../examples/warehouse-baseline.json).

A movement scenario requires `initial_world` and forbids `warehouse`.
`initial_world.entities` is a required array of 0..10000 records. Each has at least
one of `transform` or `velocity`. Transform requires `position`; optional rotation
defaults to `[0,0,0]` radians and scale to `[1,1,1]`. Velocity requires `linear`.
Every vector is exactly three finite numbers, each within +/-1e12. Optional
components stay absent; there is no implicit velocity or transform insertion.
Entity creation follows array order (index 0 onwards, generation 1). These are
logical IDs, not cross-world public handles. Empty movement scenarios are valid.
See [movement example](../examples/movement-baseline.json).

JSON scalar counters require integer syntax/types: `60.0`, booleans, numeric
strings, negative values and overflow are rejected for unsigned integer fields.
There are no JSON defaults for required simulation or warehouse fields. The optional
warehouse processing duration defaults to 600 seconds. The C++ builder's
SimulationConfig defaults are 60 Hz, 10 seconds and seed 0; name and domain data
still need validation. Domain definitions are ordinary value types, validated at
API boundaries and treated as configuration during execution.

Unknown fields, duplicate keys (including escaped equivalents), missing fields,
unsupported versions, invalid UTF-8, malformed/trailing JSON, raw NUL, excessive
nesting and oversized input are rejected. Errors identify paths such as
`warehouse.forklifts` or `initial_world.entities[0].velocity.linear`. Parsing uses
nlohmann-json 3.12.0 through the existing pinned vcpkg manifest; it is a private
implementation dependency, not exposed in public headers. Its
[parser callbacks](https://json.nlohmann.me/api/basic_json/parser_callback_t/)
enforce decoded duplicate keys and depth/event limits.

`parse_scenario`, `validate`, `serialize_scenario`, `load_scenario`, and
`save_scenario` are in `simforge/scenario/scenario.hpp`. Scenario input/output is
bounded to 4 MiB; JSON depth is bounded to 32, parser events to 4 million.
File reading is chunked and bounded even when a file grows. Saving uses exclusive
creation and refuses to overwrite existing output. Write failures can leave a
partial new file; no atomic filesystem transaction is promised.

Canonical serialization uses sorted object keys, compact JSON, explicit vector
defaults, normalized numeric types and positive zero for signed-zero vectors.
Whitespace, original field order and optional default spelling do not change
canonical identity. Entity array order does. This is schema-specific normalization,
not Unicode normalization or a claim to implement RFC 8785. Snapshot binary
serialization remains unchanged and separate.

`initial_state(definition)` validates and constructs an initial movement snapshot.
Restore that into a World/clock using the existing atomic restoration API. This
function remains movement-only because warehouse state is owned by WarehouseSimulation,
rather than translating warehouse resources into the movement demo. Construct that
domain runtime with the same validated warehouse definition.

## Supported typed commands

`InputCommand` carries execution `tick`, contiguous `sequence`, and one variant:

| Type / JSON type | Parameters and behavior |
| --- | --- |
| SetEntityVelocity / set_entity_velocity | Logical target `{index,generation}` and finite `linear` vector within +/-1e12. Target must be alive, match generation, and own Velocity. Replaces linear velocity. |
| PauseSimulation / pause_simulation | No parameters. Pauses; rejected if already paused. |
| ResumeSimulation / resume_simulation | No parameters. Resumes; rejected if not paused. |
| StepSimulation / step_simulation | No parameters. Requires pause; executes MovementSystem once, then clock single-step. Advances one tick and remains paused. |

Commands apply at boundary `t`, before automatic movement from `t` to `t+1`.
Sequences start at zero, are contiguous and encode accepted order. Same-tick
commands use sequence order; later velocity writes win. Steps immediately change
the tick, so the next command must use the resulting boundary. Commands at the
final boundary may change values/control state without another automatic update.
There are no warehouse resource commands or arbitrary serialized code.

`InputRecorder(definition, world, clock)` captures the initial state and retains
its normalized identity and budget. World/clock must outlive it. Submit a payload
to assign current tick/next sequence automatically, or supply an InputCommand
with exactly those values. Future scheduling and past submissions are rejected;
advance the session to a future tick before submitting there. Validate parameters,
target and controls before reserving log space or mutating state. Only accepted
commands enter the log; allocation failure leaves state unchanged.

`advance_to(tick)` runs explicit fixed updates. It honors pause and bypasses time
scale, following the existing explicit tick contract. It rejects past/out-of-budget
advancement. A paused clock cannot reach a future command: resume or step at its
current tick. This avoids using UI/wall time as an implicit ordering source. No
JSON parsing, whole-record scanning, validation hashing or command logic is added
to ordinary MovementSystem updates.

The caller owns exclusive access, submits between system updates, and makes no
unrecorded World/clock edits. Under that contract accepted commands have a
prevalidated nonallocating mutation path. Movement itself is not a transactional
system and floating-point overflow is not checked per entity per tick. Capture
rejects nonfinite resulting state. `capture_recording()` makes a deep copy with
the observed expected final state and validates the complete log.

## Recording JSON schema v1

Required fields, with no unknown fields allowed:

| Field | Meaning |
| --- | --- |
| schema_version | Integer 1 |
| scenario_identity | Exact canonical scenario JSON stored as a string; metadata/name/seed are included, with no hash collision ambiguity |
| initial_tick / end_tick | Integer boundaries within the scenario budget, end >= initial |
| initial_snapshot / expected_final | Lowercase hexadecimal encodings of the existing binary SimulationSnapshot v1, including its integrity checksum |
| commands | Ordered array, max 100000 records, with required tick/sequence/type |

Velocity records also require `target.index`, `target.generation`, and `linear`.
Control records forbid these parameters. The full recording is limited to 32 MiB,
with the same JSON depth/event bounds. Embedding snapshots as opaque hex keeps
runtime lifecycle state in the established binary schema. It increases file size;
some otherwise valid large combinations exceed the recording cap and are rejected.
Snapshots' existing finite/metadata/lifecycle checks and byte caps still apply.

Scenario identity must itself be canonical, valid, executable movement config.
Initial snapshot tick and rate must match recording/config. At tick zero its
logical state must equal the scenario adapter's initial state. Nonzero checkpoints
are accepted as the authoritative captured runtime base; this validates compatibility,
not proof of the checkpoint's earlier history. Targets are checked against that base
because this command set does not create/destroy entities or add/remove components.
Clock pause/step reachability and expected final clock state are preflighted.

## Replay and ownership

`replay_recording(definition, recording, destination, clock)` checks exact scenario
identity, preflights the log, restores the embedded base into an isolated World,
and walks the command array once. Each command runs when due, with the cursor
processing O(due commands) per boundary. Elapsed intervals use fixed steps;
recording JSON is never reparsed inside execution loops.

The resulting World/clock are captured and compared with `expected_final` through
the existing exact logical-state comparator. Divergence includes tick, entity,
component field and expected/actual values. Only a verified result is committed
through atomic snapshot restoration. Incompatible, malformed, unavailable-target,
divergent or allocation-failed replay leaves the destination unchanged, including
its handles/references. Successful commit invalidates all old destination handles.

Commands use logical index/generation pairs. Applying them reacquires a handle in
the current restored World and checks its generation and Velocity ownership.
Transient ownership tokens are never recorded; cross-world validation and restore
invalidation stay intact. Replay uses the existing same-environment floating-point
assumptions; it makes no universal hardware/compiler bitwise guarantee. Expected
snapshots detect accidental outcome changes but do not authenticate an author or
protect against someone deliberately replacing both inputs and expected outcomes.

## CLI and examples

```powershell
build/windows-debug/apps/sandbox/simforge_sandbox.exe --headless --scenario examples/warehouse-baseline.json
build/windows-debug/apps/sandbox/simforge_sandbox.exe --headless --scenario examples/movement-baseline.json
build/windows-debug/apps/sandbox/simforge_sandbox.exe --headless --scenario examples/movement-baseline.json --record build/my-movement-inputs.json
build/windows-debug/apps/sandbox/simforge_sandbox.exe --headless --replay build/my-movement-inputs.json
build/windows-debug/apps/sandbox/simforge_sandbox.exe --headless --replay examples/movement-inputs.json
```

The warehouse path executes the operational warehouse model and prints actual
event-derived metrics. The movement path executes its declared duration. `--record` generates
a small, verified demonstration: set velocity, pause, step, resume, advance halfway,
change velocity, finish. It needs a Velocity target and a new output path. The
checked-in [recording example](../examples/movement-inputs.json) was generated by
that demonstration and is exercised by CTest. `--replay` uses its embedded identity
or verifies an optional `--scenario` file. `--record`/`--replay` are exclusive and
scenario workflows conflict with `--entities`/`--ticks`. The old headless demo and
argument-free SDL window path remain supported.

Tests cover scenarios, canonical files, inputs, replay/atomic failures and seeded
random operations (`0x5F072026`, 1000 operations, seed/operation printed on failure).
The command reference model independently tracks position, velocity and pause.
CLI fixtures exercise output creation, replay and cleanup. All run without SDL or
Vulkan initialization. See [measured performance](input-performance.md).

Warehouse snapshot/command integration, domain RNG state, scenario migrations,
large event streams and asynchronous input processing remain future work.
