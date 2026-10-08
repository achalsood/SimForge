# Warehouse Release benchmark measurements

Measured October 7, 2026, on the development machine: Intel Core Ultra 9 275HX,
Windows 11 Home x64 10.0.26300, MSVC 19.51.36260, C++23, CMake/Ninja Release
(`/O2 /Ob2 /DNDEBUG`). The launch process was pinned to CPU 0 for serial runs and
restored its original affinity afterward. Thermal state/frequency/background load
were not fully controlled; short durations are sensitive to timer and OS noise.

Command: `build/windows-release/benchmarks/simforge_warehouse_benchmark.exe`.
Each size has one warmup and seven measured runs. Configuration is 60 Hz, resources
4 bays / 8 workers / 4 forklifts, one-second arrival interval and four-second
processing. Horizon in seconds equals arrival count, so there are exactly 1K,
10K, or 100K arrivals. All fit schema/runtime quotas without bypassing validation.

Initialization includes validation of an already constructed ScenarioDefinition,
runtime allocation and initial scheduling. File parsing/config creation are outside
timing. Event execution advances to H. Actual total covers initialization, events
and one metric query. Console output, validation traversal and teardown are excluded.

A single constant-time metric query was below clock resolution in initial trials.
The metric column therefore reports per-query cost from a separate 1000-query batch
with consumed checksum, excluded from total runtime. It includes loop/consumption
overhead and is not a claim of precisely measurable individual-query latency.
Cells are independently computed medians, so they need not sum to total.

| Arrivals | Initialization (ms) | Events (ms) | Metrics per query (ms) | Total (ms) | Owned payload bytes | Executed events |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 0.000600 | 0.044100 | 0.000008 | 0.044700 | 53,288 | 1,997 |
| 10,000 | 0.000700 | 0.436600 | 0.000008 | 0.438000 | 531,412 | 19,997 |
| 100,000 | 0.053100 | 4.907500 | 0.000008 | 4.948700 | 5,312,660 | 199,997 |

The payload estimate sums actual vector capacities for shipment history, FIFO,
reservation ledger, event heap and scheduling markers. Buffers are all reserved
during initialization; their sizes bound owned execution storage. This approximates
peak domain payload, not RSS/committed process memory: allocator overhead, object
headers, stack, library/config storage and lazy page commitment are excluded.

Every run validates closed-form counts/timestamps/occupied ticks outside timing.
For N arrivals, starts=N, completions=N-3, in-progress=3, queue wait=0, occupied
resource ticks=`(4*N-6)*60`, and executed events=`2*N-3`. Every shipment's arrival
and start is `id*60`, with completion `(id+4)*60` only if within H. Consumed checksums
across seven measured runs were 2105723459 / 58880126459 / 4368824156459, including
the batched metric-query consumption. All 100K correctness checks passed.

These are warehouse event-processing measurements, not extrapolations from ECS
movement benchmarks or a speedup claim. This small fixed-duration workload keeps
the live heap at five events; other concurrency/queue settings can have different
costs. Tests also exercise resource shortages and accumulating queues. The prior
ECS destruction regression is unchanged and remains documented separately; no ECS
rewrite or optimization is part of this milestone.
