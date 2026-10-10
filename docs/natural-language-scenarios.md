# Offline natural-language warehouse scenarios

SimForge interprets a bounded English grammar on the CPU. It produces a reviewable
configuration; simulation results come from the existing warehouse engine. There
is no model, hosted inference, network request, Python runtime, or new dependency.

## Tutorial

Open the warehouse workspace, select **Edit**, then **Describe Your Warehouse**.
Choose **Create** and enter:

> Create a warehouse with 4 loading bays, 12 workers and 3 forklifts. Shipments
> arrive every 5 minutes and take 15 minutes to process. Run the simulation for
> 8 hours.

Select **Interpret**. Review four bays, twelve workers, three forklifts, 300-second
arrivals, 900-second processing, and a 28,800-second horizon. Matched phrases and
inherited values are visible. Edit the suggested name or any proposal value, then
select **Validate Corrections**. Check **I approve these values**, then **Apply to
Editor**. Interpretation, corrections, and the checkbox alone change no draft or
run. **Discard** leaves the editor unchanged. Changed description/mode requires
another interpretation; changed review values require validation and approval again.

Select **Run Simulation**, then Play: a new custom run starts paused at tick zero.
The existing 3D layout and deterministic engine use the approved configuration.
Alternatively, **Add as candidate** and run the existing comparison workflow.
Reports contain measured simulation results. Interpretation and Apply preserve
existing run snapshots, clocks, cameras, and reports. Existing horizon/tick and
checkpoint comparison restrictions still apply.

Use **File > Save** (or Save As) to save a `.simforge` project, close the application,
then open that project from the File menu or `--project`. Approved draft/candidate
values and retained run state reopen through the existing persistence workflow.

For **Modify**, start from the baseline draft and enter:

> Add two forklifts and reduce processing time by 20 percent. Keep everything
> else unchanged.

This changes forklifts from two to four and processing from 900 to 720 seconds;
every other field and the name remain unchanged. Modify captures the validated
editor draft, including manual edits. Invalid drafts must be corrected first.
An intervening edit, Revert, scenario load, or project replacement makes a proposal
stale. Apply checks the runtime workspace/revision stamp and canonical Modify
source before updating the draft in one batch. Unchanged fields retain their raw
editor formatting.

Create always uses these bundled baseline defaults for unspecified fields and
relative changes: **3 bays, 10 workers, 2 forklifts, 300-second arrivals,
900-second processing, 28,800-second horizon, 60 Hz, seed 12345**. Defaults are
disclosed. The suggested name is `Described warehouse`. The review's Before column
shows the editor that Apply would replace.

## Supported fields and commands

| Field | Aliases and example phrases |
| --- | --- |
| Loading bays | bays/docks; `Use four loading bays`, `Add one dock` |
| Workers | workers/staff; `Set staff to 12`, `Remove two workers` |
| Forklifts | forklifts/lift trucks; `Use three lift trucks`, `Double forklifts` |
| Arrival interval | `Shipments arrive every five minutes`, `Set arrival interval to 300 seconds` |
| Arrival rate | `12 shipments per hour`, `Double arrival rate`, `Make shipments arrive twice as frequently` |
| Processing duration | `Each shipment takes fifteen minutes to process`, `Reduce processing time by 20 percent` |
| Simulation horizon | `Run the simulation for eight hours`, `An 8-hour shift`, `Extend duration by one hour` |
| Tick rate | `Set tick rate to 60 Hz` (absolute assignment) |
| Random seed | `Set random seed to 12345` (absolute assignment) |

Set/use/have/allocate express assignments. Add/increase/raise and
remove/subtract/decrease/reduce/lower/cut express relative changes. Entity-first
relative changes require `by`. Multiply/divide require a factor; double/halve
use two. Percentages accept `%`, `percent`, or `percentage`. These are focused
phrase patterns, not arbitrary interchangeable English words. Separate clauses
with commas, periods, semicolons, newlines, or `and`.

Digits, decimals with up to nine fractional digits, and bounded word-number forms
are accepted: zero through nineteen, tens, `twenty-five`, `thirty two`, and
`one hundred twenty five`. Negative values, slash fractions, exponent notation,
non-finite values, and non-ASCII input are rejected. Scenario names are edited in
review rather than inferred from prose.

Duration units are seconds (`s`, `sec`, `secs`, `second`, `seconds`), minutes
(`min`, `mins`, `minute`, `minutes`), and hours (`h`, `hr`, `hrs`, `hour`, `hours`).
Durations require units; capacities are counts; tick rate requires Hz/hertz.
Checked rational integer arithmetic multiplies minutes by 60 and hours by 3600.
Rates convert to shipments/second and invert to the stored interval: 12/hour gives
300 seconds. Doubling a rate halves the interval; reducing it by 50% doubles the
interval. Arrivals remain evenly spaced individuals, not simultaneous batches.

Final capacities and durations must be whole counts and whole seconds. No rounding
occurs: increasing three forklifts by 20% gives 18/5 and blocks approval; review
can explicitly choose an integer. Seven shipments/hour also lacks an exact
whole-second interval. Authoritative validation enforces names, resource totals,
duration, ticks, and arrival limits. Existing 3D playback limits each resource
type to 256; the language layer does not change that limit.

## Errors and trust boundary

Every clause must be accounted for. A supported clause plus unsupported material
remains invalid. Diagnostics retain stage, code, message, and original byte span.

* `Use three forklifts and five forklifts` conflicts. Any two operations on the
  same physical field conflict, including interval versus rate. `Then` does not
  authorize ordering. Rewrite to one explicit instruction.
* `Make the warehouse faster`, alternatives, estimates, conditions, negation,
  ranges, and clock-time shifts need revised text with explicit quantities.
* `Add three autonomous robots` is unsupported. Conveyors, custom distributions,
  costs, optimization goals, and schedules are also outside the schema.
* `Process shipments every ten minutes` is ambiguous. Specify either processing
  duration or arrival interval explicitly.
* `Two shipments every five minutes` describes unsupported batches. An explicit
  evenly spaced rate is supported when that matches the intended model.

Recognized operations and computed values remain visible on invalid proposals.
They cannot be applied. Review can fix numeric precision/range errors, but cannot
erase recognition errors or conflicting original instructions. Revise the text
and interpret again. Apply always requires a valid current proposal and explicit
approval. The parser and review paths never execute a simulation.

## Architecture and persistence

The CPU-only `simforge_scenario_language` library links to the existing engine.
SDL/ImGui/Vulkan are used only by the viewer integration.

```text
description -> bounded tokens -> typed operations -> exact transformations
            -> authoritative validation -> proposal -> explicit approval
            -> existing editor -> run / candidate / project save
```

`Operation` contains `Field`, `Action`, reduced `Rational`, `Unit`, original byte
`Span`, `Origin`, and recognition `Pattern`. ArrivalRate maps to the physical
interval field. Origins distinguish rules, review edits, and structured input.
The read-only Proposal retains original source/identity, target stamp, text,
operations, explicit corrections, proposed definition, computed rational values,
diagnostics, validity, and timings.

The public API separates `tokenize`, `recognize`, `apply_operations`, `compile`,
`interpret`, and `revise`. A future local recognizer can supply the same typed
operations to `compile`; malformed enums, units, spans, diagnostics, and values
are checked. Scenario validation and execution remain independent of recognition.
Limits are 8192 input bytes, 1024 tokens, 128 clauses, 64 operations, and 128
diagnostics. Saturated diagnostic lists retain blocking errors. Interpretation
runs on an explicit button action, never every frame.

Recognition first rejects uncertainty within each clause, then tries arrival,
processing, horizon, and generic entity/action patterns in that order. Explicit
shipments-per-time rates precede one-shipment/every interval patterns. The exact
`take ... to process` continuation handles the shared shipment subject in the
tutorial. Generic parsing identifies the action before its quantity: `by 20%`
after reduce becomes a percentage reduction, never a minute assignment. Any
unconsumed suffix blocks the proposal. Multiple operations on a physical field
are conflicts rather than instructions whose order can resolve meaning.

Approved values use existing **project schema version 1**. Drafts, candidates,
custom runs, reports, cameras, and playback settings follow existing persistence.
Proposals, diagnostics, and approval are transient and cleared on restoration.
A genuine pre-language Commit #21 fixture verifies loading three runs, one
candidate, a report, and a paused active run at tick 36000/speed 2 without migration.

## Evaluation, tests, and performance

The authored corpus contains **134 cases: 74 supported and 60 rejected**, covering
compound descriptions, aliases, word numbers, decimals, units/rates, relative
changes, inheritance, conflicts, ambiguity, unsupported requests, precision,
overflow, and limits. Each case checks ordered typed operations including source
text/pattern, every final field, exact error codes, and acceptance. This measures
documented grammar regression coverage, not general-English accuracy.

Release evaluation achieved **134/134 exact operation matches**, **134/134 exact
full value matches**, **zero unsafe false acceptances**, and zero failures.
Unsupported-code detections: 13; ambiguity-code detections: 15. Other rejections
exercise conflict, numeric, source, and limit checks. Corpus mean/worst timing was
7.279/38.6 microseconds on this development machine.

```powershell
build/windows-release/tests/simforge_scenario_language_eval.exe
build/windows-release/benchmarks/simforge_scenario_language_benchmark.exe
ctest --preset windows-debug --output-on-failure
ctest --test-dir build/windows-release --output-on-failure
ctest --preset windows-headless --output-on-failure
```

Release benchmark, 1000 iterations per case, microseconds:

| Case | Tokenize | Recognize | Apply | Validate | Total mean | Total worst |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Create, 164 bytes | 1.159 | 2.401 | 0.464 | 0.221 | 8.183 | 43.500 |
| Modify, 56 bytes | 0.460 | 0.958 | 0.407 | 0.193 | 5.673 | 29.700 |
| Unsupported, 55 bytes | 0.303 | 0.917 | 0.234 | 0.189 | 5.195 | 55.300 |
| Maximum input, 8192 bytes | 41.542 | 46.088 | 4.212 | 0.223 | 107.204 | 321.000 |

Total includes source validation/serialization, proposal construction, and timing
overhead beyond listed stages. Maximum input exercises bounded rejection. A
native Debug Interpret action measured 205.4 microseconds. These results support
synchronous interpretation on demand.

The final Release native review measured 25.5 microseconds for its Interpret action.

Benchmark-only allocation instrumentation measured mean allocation count/requested
bytes: Create 122/16,046; Modify 114/9,806; unsupported 118/9,534; maximum input
501/313,399. Requested bytes represent cumulative allocation traffic per parse,
not live memory. Peak process working set ranged from 4,882,432 to 5,505,024 bytes
(about 5.25 MiB maximum), including CRT/process/allocator caching. It is a cumulative
process high-water measurement, not parser-exclusive memory. See the
[raw benchmark CSV](scenario-language-benchmarks.csv).

CPU tests cover parser stages, malformed typed inputs, approval/isolation, dirty
state, stale sources, determinism, actual execution/comparison, project round trips,
and legacy compatibility. The native walkthrough enters text and clicks controls,
checks approval/corrections/staleness, launches 3D playback, compares exact state
and measured results with headless execution, saves, and reopens in a second
process. It exercises 640x480 at 200% UI scale and checks Vulkan validation.

All configurations built without compiler warnings. Full results: **Debug
352/352**, **Release 352/352**, and **renderer-disabled 323/323** tests passed.
Both graphical suites include nine graphics tests. The final native review and
fresh-process reopen each reported **zero Vulkan validation errors and warnings**,
with synchronization validation enabled and clean shutdown/reinitialization.

## Real application captures

* [Description input](artifacts/scenario-language-input.png)
* [Proposal review](artifacts/scenario-language-review.png)
* [Unsupported request](artifacts/scenario-language-error.png)
* [Approved editor](artifacts/scenario-language-applied.png)
* [Measured 3D playback](artifacts/scenario-language-playback.png)

## Recommended next milestone

Expand the independently authored corpus and refine the documented grammar from
observed warehouse descriptions before evaluating a specialized local recognizer.
Retain typed operations, authoritative validation, explicit review, and actual
simulation execution. New resource types, schedules, stochastic arrivals, and
optimization objectives need deliberate simulation/schema changes first.
