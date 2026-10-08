# Comparison Release benchmark measurements

Measured October 8, 2026, on Intel Core Ultra 9 275HX / Windows 11 Home x64
10.0.26300, MSVC 19.51.36260, C++23, Ninja/CMake Release (`/O2 /Ob2 /DNDEBUG`).
Launches were serial and pinned to CPU 0, restoring affinity afterward. Thermal
state, frequency and background load were not completely controlled. Timings are
local observations, not universal performance guarantees.

## Warehouse before/after

Actual Commit #9 event and checkpoint benchmark executables were copied into the
ignored build directory before implementation and measured before editing. The
checkpoint executables were then alternated old/new for three launch pairs. Each
launch used one warmup and seven measured trials per size; table values are medians
of those three within-launch medians. Workloads and correctness checks are unchanged.

| Arrivals | Old continuous (ms) | New continuous (ms) | Change | Old capture (ms) | New capture (ms) | Old restore (ms) | New restore (ms) | Old branch (ms) | New branch (ms) |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 0.044900 | 0.045600 | +1.6% | 0.009300 | 0.009400 | 0.057400 | 0.059400 | 0.050900 | 0.052800 |
| 10,000 | 0.512500 | 0.499600 | -2.5% | 0.068100 | 0.069500 | 0.254100 | 0.311900 | 0.273400 | 0.294900 |
| 100,000 | 4.964500 | 4.931200 | -0.7% | 0.627400 | 0.630200 | 2.785100 | 2.920600 | 2.518700 | 2.754900 |

No material event-execution regression was observed; normal dispatch was unchanged.
Checksums and owned payload estimates are unchanged. The separate event benchmark
before implementation measured 0.043900 / 0.498700 / 4.898300 ms; its final launch
measured 0.044800 / 0.445500 / 4.923600 ms. The 10K difference between launch contexts
illustrates why a single measurement should not be treated as a speedup claim.

Generalized history validation adds checkpoint work. At approximately 50K records,
restore rose 4.9% (0.1355 ms) and branch construction 9.4% (0.2362 ms). The 10K restore
median rose about 22.7% (0.0578 ms); short allocation/cache-sensitive phases vary.
Initial unpaired 100K restore was 3.1728 ms versus 2.8486 ms old, prompting inspection.
The audit now computes concurrency once per capacity change instead of repeating
the three-pool minimum at every historical boundary. Its transition layout remains
24 bytes. Full resource/timestamp/occupancy/utilization validation was preserved.
Remaining validation overhead is reported rather than removed from correctness checks.

Capture deep-copies about 50K records in 0.63 ms. Schema-1 sizes/checksums remain
identical: 16087 / 155588 / 1550589 bytes for 501 / 5001 / 50001 checkpoint records.
Seven-trial checksum totals remain 109532549 / 10550210958 / 1050559636133. The earlier
ECS destruction regression was not modified.

## Batch methodology

Command: `build/windows-release/benchmarks/simforge_comparison_benchmark.exe`.
Each 3/10/50-candidate size has one warmup and seven measured trials. The scenario
uses 10K arrivals, 60 Hz, one-second interval, four-second processing, resources
4/8/2 and checkpoint at 5000 seconds. Candidates vary bays 4..7, workers 8..12 and
forklifts 2..5 with distinct names.

Validation measures preflight. Checkpoint creation includes initialization, shared
advancement/capture and preflight from an already constructed scenario. Branch
creation includes validated restoration. Execution includes capacity commands and
continuation; metrics are timed separately. Branch/execution/metric columns sum all
requested candidates, excluding baseline/probes. Derived analysis measures baseline
deltas and exact ranking; controlled probe simulation and evidence generation are
included in batch cost. JSON includes one shared checkpoint and validation; CSV
includes summary validation and all rows.

Batch cost starts from a prepared checkpoint and includes baseline, candidates,
probes and analysis. End-to-end is measured separately from shared checkpoint
creation through batch and both serializations. Scenario construction, file parsing/
I/O, console output, external checks and final teardown are excluded. Per-candidate
batch cost includes shared baseline/probe overhead. Cells are independent medians.

| Candidates | Validate (ms) | Checkpoint (ms) | Branch sum (ms) | Execute sum (ms) | Metrics sum (ms) | Derived analysis (ms) | JSON (ms) | CSV (ms) | Batch (ms) | End-to-end (ms) | Batch/candidate (ms) |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 3 | 0.143300 | 0.391200 | 0.579700 | 0.712700 | 0.000100 | 0.000500 | 1.642000 | 0.215600 | 4.052700 | 6.486000 | 1.350900 |
| 10 | 0.141100 | 0.393600 | 1.161500 | 2.220600 | 0.000300 | 0.001000 | 1.715300 | 0.234300 | 6.899000 | 8.946300 | 0.689900 |
| 50 | 0.150200 | 0.422600 | 5.375400 | 11.201100 | 0.001500 | 0.005300 | 2.624700 | 0.649600 | 21.885900 | 26.050100 | 0.437718 |

No timings are serialized into logical results. Metric extraction is close to clock
resolution, so its sum is approximate, not a precise individual-query latency.
Every trial checks independently measured branches/deltas/ranks against batch
results, reruns recorded report inputs, and compares the separately measured
end-to-end report and both encodings. Seven-trial checksums were 33523840 / 113808275 /
578630577. Verification is outside timers. The workload is practical, without a
50-by-100K CI performance test, timing thresholds or analysis inside event dispatch.
