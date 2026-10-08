# ECS investigation and input benchmark results

Measured October 7, 2026, on Intel Core Ultra 9 275HX / Windows 11 Home x64
10.0.26300, MSVC 19.51.36260, C++23, CMake/Ninja Release `/O2 /Ob2 /DNDEBUG`.
The benchmark launcher was pinned to CPU 0, inherited by each child executable,
and restored its original affinity afterwards. Runs were serial and alternated
baseline/candidate. CPU frequency, thermal state and background OS load were not
fully controlled; this improves comparability but is not laboratory isolation.

## ECS regression investigation, before new features

Existing executable artifacts from `9ef028c` (pre-ownership) and `5c843c8`
(ownership/snapshots) were run in three alternating rounds. Each invocation used
one warmup and seven measured iterations, identical 100K entity/120-update work.
Median batch milliseconds per invocation:

| Round | Old create | Owned create | Old insert | Owned insert | Old 120 updates | Owned 120 updates | Old destroy | Owned destroy |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1.6268 | 1.9152 | 11.2707 | 12.7200 | 30.3959 | 30.6338 | 0.6184 | 1.2976 |
| 2 | 1.6568 | 1.9178 | 11.2858 | 12.4772 | 29.8718 | 29.8989 | 0.6285 | 1.2453 |
| 3 | 1.6393 | 1.8547 | 11.1837 | 12.6132 | 29.5481 | 29.7118 | 0.6087 | 1.2853 |

Destruction regression is reproducible (~2x). Creation/insertion increased in
these rounds. Movement remained close. The historical benchmark harness differs
in handling owned handle lists by const reference; that already avoids redundant
shared-token copies. Full causal attribution is not established by these results.

Code/disassembly inspection of the optimized World object found no successful-
destruction allocation or reference-count increment, no changed complexity, and
no snapshot validation in the path. Ownership adds a pointer comparison and a
larger public handle list (24 bytes versus 8 on this build). Generation, dense
swap cleanup and iteration guards remain checked. These are plausible contributors,
not isolated per-factor profiling evidence.

A capture-by-value cleanup-lambda experiment addressed an observed ID reload. In
three matched 100K rounds, current/candidate destruction medians were
1.2759/1.2872, 1.2942/1.2580, and 1.2836/1.2545 ms. Improvement was inconsistent,
so the experiment was discarded. There is no ECS optimization or claimed fix in
this commit. Ownership/generation/reference/snapshot guarantees are preserved.

Final before/after-scenario 100K medians under the same affinity:

| Version | Create | Insert both | 120 updates | Destroy |
| --- | ---: | ---: | ---: | ---: |
| Before scenarios (`5c843c8`) | 2.0510 | 12.7220 | 29.6624 | 1.2534 |
| Scenario implementation | 2.0485 | 12.5413 | 29.6181 | 1.2533 |

These differences are within observed variability, not a speedup claim. No new
processing is added to the normal movement loop. Controlled per-function profiling
is the next step if lifecycle cost needs optimization; a full ECS rewrite is not
justified by this investigation.

## Input benchmark

Run `build/windows-release/benchmarks/simforge_input_benchmark.exe`. It uses one
warmup plus seven measured runs, a movement scenario at 60 Hz for 600 ticks, one
velocity command per entity and pause/step/resume. Scenario creation, initial
snapshot capture, recorder construction, final recording capture and post-run
verification are outside the reported timing sections.

Acceptance measures the bulk submit loop and one single-step update. Full replay
includes compatibility/preflight validation, isolated state construction, command
dispatch, movement, exact comparison and destination commit. JSON write/read
include production validation and snapshot encoding/checksums. It is not valid
to interpret replay minus movement-only as pure command dispatch cost.

Median milliseconds, CPU 0:

| Entities / commands | Movement only | Accept | Verified replay | JSON write | JSON read | JSON bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 / 1,003 | 0.9240 | 0.0925 | 12.6524 | 12.7011 | 15.5455 | 741,048 |
| 10,000 / 10,003 | 10.3961 | 0.5210 | 244.6791 | 243.2788 | 424.3302 | 7,423,286 |

All expected state/count checks passed. Consumed checksums across seven runs were
627599.116667 and 6288849.700000. Bulk acceptance is inexpensive here, but JSON
construction/parsing, full preflight and exact comparison are substantial costs.
This implementation favors validated offline replay over low-latency interactive
loading. Future profiling may reduce boundary costs; JSON is never reparsed per
tick and commands are traversed once, not rescanned on every tick.

The measurements do not establish a latency guarantee or performance improvement.
There is no multithreading, command-stream compression or event-store framework.
