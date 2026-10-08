# Initial ECS benchmark results

Measured October 7, 2026, on the local development machine:

- Intel Core Ultra 9 275HX, 24 cores / 24 logical processors.
- Windows 11 Home, 64-bit, version 10.0.26300.
- MSVC x64 19.51.36260 (`_MSC_FULL_VER=195136260`), C++23, Ninja, CMake Release.
- Release flags `/O2 /Ob2 /DNDEBUG`; single-threaded updates, no fast-math flag.
- Command: `build/windows-release/benchmarks/simforge_world_benchmark.exe`.

One warmup followed by seven measured runs per size, with 120 fixed updates at
60 Hz per run. Each cell is an independently computed median in milliseconds.
Insertion includes both Transform and Velocity. Creation/insertion include
World allocation growth, while result validation and checksum calculation are
excluded from update and destruction timings.

| Entities | Create | Insert both | 120 updates | Per update | Destroy |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 0.013700 | 0.112500 | 0.191200 | 0.001593 | 0.005600 |
| 10,000 | 0.246700 | 1.352900 | 2.035400 | 0.016962 | 0.058000 |
| 100,000 | 1.734500 | 11.694400 | 30.760100 | 0.256334 | 0.633500 |

All per-entity positions and destruction invariants passed validation, including
100K entities. Printed checksums across seven measured runs were 84,000, 840,000,
and 8,400,000 respectively. There were no correctness/scalability failures.

A separate Release headless sandbox run with 10K entities and 600 ticks reported
10 simulated seconds, checksum 100,000, setup 0.0003855 seconds, and simulation
updates 0.0102955 seconds. The sandbox pre-reserves storage and includes clock
accounting in update timing, so its numbers are not interchangeable with the
benchmark phases.

These results establish one local baseline. There is no earlier comparable
implementation or speedup claim. CPU frequency, thermal state, power policy,
background processes, and OS scheduling were not controlled; short measurements
are especially sensitive to noise. Reported decimal places describe observed
values, not accuracy guarantees. Re-run under controlled conditions before making
performance commitments.

Allocation growth contributes to insertion cost, and movement accesses a second
component through sparse lookups while writing 72-byte Transform values. Cache
traffic and component joining are candidates for future profiling, not established
causes of the timings. Profile before changing storage layout, and keep this
baseline comparable if a later milestone optimizes it.
