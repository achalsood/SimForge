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

## Commit 9: controlled comparison and checkpoint costs

Measured October 8, 2026, with the same machine/compiler/Release flags and CPU 0
affinity. Before editing, the commit-8 benchmark executable was preserved in the
ignored build directory and measured again. Five subsequent alternating old/new
launch pairs each performed the original one warmup plus seven trials per size.
The following table is the median of those five within-launch medians. Thermal,
frequency and OS noise remain uncontrolled; this is a local comparison, not a
precise throughput guarantee. There is no serialization or snapshot validation
inside the timed event workload.

| Arrivals | Before events (ms) | After events (ms) | Change | Before total (ms) | After total (ms) |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 0.043900 | 0.044600 | +1.6% | 0.044800 | 0.049000 |
| 10,000 | 0.442300 | 0.451100 | +2.0% | 0.443000 | 0.455500 |
| 100,000 | 5.027300 | 5.016600 | -0.2% | 5.078900 | 5.070600 |

Initial standalone before-edit medians were 0.043300 / 0.442600 / 4.930600 ms for
events and 0.043900 / 0.443400 / 4.978800 ms for totals. The first new 10K launch
showed 0.496200 ms, prompting the alternating comparison above; later pairs reduced
that difference to approximately 2%. No material event-processing regression was
observed. The three extra capacity-integral accumulators add bounded arithmetic at
clock jumps. Checksums and executed-event counts match the original benchmark exactly.

Constructor initialization now includes canonical scenario identity creation for
snapshot compatibility. Paired medians increased from 0.000600 to 0.004300 ms at
1K, 0.000700 to 0.004800 ms at 10K and 0.052100 to 0.057300 ms at 100K. That few-microsecond
cost raises the small 1K total by about 9.4%, while event time increases only 1.6%.
Identity storage adds 316 payload bytes for this benchmark's scenario. Reported
owned payload totals are 53604 / 531728 / 5312976 bytes; capacity history adds storage
only when commands are accepted. Allocation/page commitment varies between launches.

The new `simforge_warehouse_snapshot_benchmark` uses one warmup and seven measured
trials per size, also in Release on CPU 0. Continuous and 100-chunk runs use the
original 4/8/4, one-second arrival/four-second processing workload. Source checkpoint
preparation is outside measured snapshot phases; capture is at H/2, **after** its
due arrival, so it contains N/2+1 shipment records. Restoration is into an advanced
runtime. Capacity-command timing uses a separately prepared 4/8/2 checkpoint and
increases forklifts to 4, actually starting two waiting jobs. It includes history/
heap allocation; setup and snapshot comparison are outside the command timer.

| Scenario arrivals | Checkpoint records | Continuous (ms) | 100 chunks (ms) | Capture (ms) | Restore (ms) | Serialize (ms) | Deserialize (ms) | Branch (ms) | Capacity change (ms) | Binary bytes |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 501 | 0.045500 | 0.054600 | 0.008600 | 0.058400 | 0.060300 | 0.052500 | 0.053000 | 0.000200 | 16,087 |
| 10,000 | 5,001 | 0.499600 | 0.525200 | 0.080900 | 0.330400 | 0.489600 | 0.387700 | 0.272300 | 0.000300 | 155,588 |
| 100,000 | 50,001 | 4.895600 | 5.116200 | 0.623000 | 2.800900 | 5.432100 | 3.774500 | 2.523600 | 0.000500 | 1,550,589 |

Each phase reports an independently computed median. The new benchmark's timing
order/allocation context differs from the original executable, so compare continuous
and chunked columns within this table and old/new original workloads in the preceding
table. The 1K chunk result is timer/cache sensitive. At 100K, this final launch's
chunked median was 4.5% slower; the preceding launch measured 4.982600 ms continuous
and 4.945900 ms chunked (0.7% faster). That variation prevents a precise incremental
overhead estimate. Both execute identical events and states; the added work consists
of 100 stop-boundary checks/integrations, without a per-tick loop. Capture copies full historical records,
including optional timestamps. Restore/branch include bounded allocation, scenario
parsing and historical interval validation. Serialization includes validation and
checksum generation; deserialization includes checksum, bounded decoding and
validation. These costs belong to explicit checkpoints, not normal event execution.

Every trial independently checks the original closed-form totals, exact continuous/
chunked/continued snapshots, byte round-trip state and capacity-change reservations.
Results and bytes are consumed in checksums outside timing; seven-run totals were
109532549 / 10550210958 / 1050559636133. Measurements do not include process startup,
console output, source preparation, external verification or final object teardown,
except replacement-state cleanup within restore. Snapshots can allocate O(history)
temporary storage; the binary quota is 32 MiB. No ECS destruction change was made.
