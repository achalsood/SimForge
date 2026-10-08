# Snapshot and replay benchmark results

Measured October 7, 2026, on the same development machine as the
[ECS baseline](ecs-benchmark-results.md): Intel Core Ultra 9 275HX (24 cores/logical
processors), Windows 11 Home x64 10.0.26300, MSVC 19.51.36260
(`_MSC_FULL_VER=195136260`), C++23, CMake/Ninja Release with `/O2 /Ob2 /DNDEBUG`.
All workloads are single threaded. Background load, frequency and thermal state
are not controlled, so small differences are not accuracy/speedup claims.

Each size has one warmup and seven measured runs. Snapshot setup creates both
components, with zero positions and velocity `(1,2,3)`, and runs the ECS benchmark's
120 movement ticks at 60 Hz. Setup is outside snapshot timing. Capture is combined
World + clock; restore targets a populated World and includes old-state destruction.
Serialization/deserialization include validation and FNV-1a integrity checks. Exact
state comparisons and final checksum traversal are outside all timed phases.
Every run validates final logical state and handle invalidation.

```powershell
build/windows-release/benchmarks/simforge_snapshot_benchmark.exe
```

Median milliseconds:

| Entities | Capture | Restore | Serialize | Deserialize | Encoded bytes |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 0.043000 | 0.053500 | 0.222500 | 0.147000 | 122,081 |
| 10,000 | 0.347000 | 0.423000 | 2.268200 | 1.435900 | 1,220,081 |
| 100,000 | 3.832300 | 4.318300 | 23.936600 | 15.487200 | 12,200,081 |

Checksums across seven measured runs were 84,000 / 840,000 / 8,400,000. All sizes
passed exact comparison and lifecycle checks. No 100K correctness failures occurred.

## Normal ECS comparison

Before rebuilding Release, the existing executable from commit `9ef028c` was
copied to the ignored build directory and rerun. The same benchmark workload and
compiler configuration were then run with this milestone. The new harness uses
const references when processing handle lists to avoid redundant ownership-token
copies; the old handles were trivial index/generation values. No World reservation
was introduced. Growth allocations remain included in creation and insertion.

| Entities | Old 120 updates (ms) | New 120 updates (ms) | New create (ms) | New insert (ms) | Old destroy (ms) | New destroy (ms) |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 0.186500 | 0.183900 | 0.021000 | 0.144500 | 0.005600 | 0.011200 |
| 10,000 | 2.012000 | 2.040800 | 0.211000 | 0.813800 | 0.057400 | 0.118300 |
| 100,000 | 30.414400 | 30.275600 | 2.055500 | 12.627200 | 0.624900 | 1.353500 |

At 100K, movement was about 0.5% faster in the final run (0.252297 ms/tick versus
0.253453 ms/tick). An earlier run with the same compact storage measured 30.948700
ms for 120 updates, about 1.8% slower than baseline. These small differences are
within the observed variability of uncontrolled measurements, not a speedup claim
or proof of no regression. Dense storage retains compact IDs, and movement
constructs no owned handles or snapshot validation state per tick. An initial
owned-handle dense layout showed about a 27% movement regression and was replaced
after measurement. That intermediate layout is not the committed architecture.

Destruction is about 2.2 times the baseline batch duration at 100K, an observed
regression associated with the new public ownership checks and larger handle
lists. Old/new 100K creation was 1.774800/2.055500 ms; insertion of both components
was 11.366300/12.627200 ms. Smaller insertion measurements varied enough that no
speedup claim is made. Ownership safety adds lifetime/copy costs; profile in a
controlled environment before further optimization. Public handles keep identity
tokens alive, but compact dense IDs avoid per-component token storage.

Potential future profiling targets are snapshot copying/validation allocation,
field-by-field encoding and checksum traversal, and ownership-aware lifecycle
access. No hashing or snapshot validation is added to ordinary movement ticks.
Serialization is bounded/uncompressed, so large fully componentized Worlds may
hit the 64 MiB format cap before the 1M-slot cap. Neither multithreading nor a
general serialization framework was introduced.
