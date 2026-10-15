# Offline natural-language warehouse scenarios

> Publication curation: additional captures cited in this engineering record remain in Commit #33. See [screenshot history and recovery](screenshot-gallery.md#historical-captures).

SimForge interprets a bounded English grammar on the CPU. It produces a reviewable
configuration; simulation results come from the existing warehouse engine. There
is no model, hosted inference, network request, Python runtime, or new dependency.

## Expanded everyday-request coverage (2026-10-10)

The additional CPU-only coverage suite tests **3,609 prompts** through the real
interpreter: **3,417 exact positive cases and 192 required rejections**. It combines
22 request wrappers, nine resource aliases, numeric/word/hyphenated quantities,
addition versus absolute counts, removal, multipliers, percentages, duration and
frequency changes, punctuation, compound clauses, and edited Modify sources.
These are deterministic development regression combinations, not a claim of
accuracy on arbitrary English or independently sampled human prompts.

`I want to see 10 more bays` now emits **Add 10 loading bays**. On the bundled
three-bay baseline it proposes **13 bays**, preserving all other values; on an
edited seven-bay Modify source it proposes **17 bays**. `I want to see 10 bays`
sets an absolute capacity. Interpretation still requires review and approval before
Apply and never directly changes an active warehouse.

New request forms include `I'd like ten extra docks`, `We'd like to see ten more
bays`, `I would like you to add ten bays`, `Can I have ten more bays`, and
`Can you give me another two loading bays`. Optional `to` and `you to` are scoped
to explicit desire wrappers; `see` is consumed only inside such a wrapper. Bare
inspection requests such as `Show me ten bays` remain unsupported. Only anchored
`I/we would like` exempts `would` from the hypothetical guard. Negation, conditions,
other uses of `would`, vague quantities, conflicts and unsupported trailing material
continue to block the complete proposal. Exact courtesy endings (`for me`, `for us`,
`please`, `thanks`, `thank you`) may be consumed without dropping meaningful text.

| New test group | Cases | Result |
| --- | ---: | --- |
| Request wrappers, aliases and quantities | 3,171 | Exact operations and complete scenarios match |
| Arithmetic, contrasts and edited sources | 199 | Exact operations and complete scenarios match |
| Durations and arrival frequency | 35 | Exact operations and complete scenarios match |
| Compound clauses and source ownership | 12 | Exact operations, scenarios and nonoverlapping spans match |
| Ambiguous, negated, unsupported or invalid requests | 192 | All rejected; zero unsafe acceptances |

All **47 CPU language CTest cases** pass in Windows Debug, Release and the
renderer-disabled configuration. The original 134-case strict corpus remains
134/134 exact matches; development remains 119/119 positives and 55/55 rejections.
The unchanged frozen holdout scores **24/28 exact positives, 20/20 rejections**,
with zero unsafe acceptances or accepted silent omissions. Its four safe misses
remain measured misses; neither its text nor expectations were changed to improve
results, and new rules were developed from the user's example and the new suite.

Run `ctest --test-dir build/windows-release -R simforge_language -LE graphics
--output-on-failure`. The five `simforge_language_coverage_*` cases run the new
suite. Individual groups can also be run with
`build/windows-release/tests/simforge_scenario_language_coverage_test.exe requests`
(or `operations`, `timing`, `compound`, `rejections`). The existing frozen datasets
and approval, correction, stale-source and unchanged-runtime tests remain intact.

## Commit 23: conversational interpretation

All six user-reported failures were reproduced through the real interpreter API
before changing recognition. The six new regression tests failed; tokenization
succeeded in every case. The UI uses that same API directly.

| Reported phrase | Original recognition defect |
| --- | --- |
| `Put in 3 more bays` | Global uncertainty check rejected `more`; `put in` was absent from actions |
| `Get 3 more bays` | Global `more` rejection and missing `get` action |
| `We need 3 additional loading bays` | Prefix existed, but quantity parsing expected the entity immediately after `3` |
| `Get twice as many workers` | Missing conversational verb and multiplier relation |
| `We need twice as many workers` | Prefix existed; `twice as many` was not a supported factor expression |
| `Use 3 times as many forklifts` | Numeric recognition found `3`, but the entity matcher expected a resource immediately afterward |

The arithmetic layer already supported multiplication correctly. The defect was
the mapping from everyday phrases to that typed operation, rather than numeric
conversion, simulation, or approval. Regression assertions were retained intact.

From the baseline, **Modify** `Get 3 more bays and twice as many workers` proposes
six bays and twenty workers. Review and explicitly approve before Apply. You can
then launch a custom run or add a candidate through the existing workflows.

| Expression | Operation |
| --- | --- |
| `Get 3 workers`, `We need 12 workers`, `Install 4 docks` | Set absolute capacity |
| `Put in 3 more bays`, `Hire 3 additional workers`, `Give us 2 extra docks` | Add the explicit quantity |
| `Get another two forklifts`, `Bring in 3 staff on top of the existing number` | Add the explicit quantity |
| `Use 3 fewer workers`, `Get rid of 2 forklifts`, `Take away one bay`, `Drop worker capacity by five` | Subtract |
| `Get twice as many workers`, `Use 3 times as many forklifts`, `Use 2x workers`, `Workers should be doubled` | Multiply by a factor |
| `Triple the loading bays`, `Make worker capacity four times its current value` | Multiply |
| `Use half as many forklifts`, `Cut processing time in half` | Multiply by 1/2 |
| `Halve processing time` | Existing divide-by-two operation |
| `Make processing twice as fast` | Divide processing duration by two |
| `Processing should take twice as long` | Multiply processing duration by two |
| `Shave five minutes off processing`, `Add ten minutes to processing time` | Subtract/add a duration |
| `Receive shipments twice as often`, `Have shipments arrive half as often` | Multiply arrival frequency; invert back to interval |
| `For forklifts, add 3` | Explicit topic binds that one command to forklifts |

Conversational verbs do not imply addition on their own: `Get 3 workers` sets
three, whereas `Get 3 more workers` adds three. Supported relative markers are
`more`, `additional`, `extra`, `another`, and `on top of the existing number`.
Staffing is also an alias for worker capacity. Singular/plural resource names and
existing aliases remain supported. Polite wrappers include Please, Could you,
Could we, Can we, I'd like to, We need to, We could use, and Let's. ASCII apostrophes
in contractions preserve their original byte spans; negation is never stripped.
`A couple of`, `several`, `some`, and `a bunch` are conservatively rejected.

These distinctions are covered by typed-operation and complete-state tests:

* `Increase workers by 2` adds two; `Increase workers to 2` sets two.
* `Increase workers by a factor of 2` multiplies by two.
* `Increase workers by 200%` increases ten workers to thirty; `Increase workers
  to 200% of the current count` makes twenty.
* Doubling frequency halves the stored interval; doubling the interval halves
  frequency. Total shipment counts and burst arrivals are not frequencies.
* Speed and duration use inverse factors for this fixed-work processing model.
  `Make processing faster` remains incomplete. Fractional final resources or
  seconds still require explicit correction; no rounding was added.

Negation (`don't`, `not`, `never`, and other `n't` contractions), conditions,
estimates, alternatives, retrospective decisions, and hypothetical requests block
approval. There is no exploratory hypothetical mode. `Set forklifts to 3 and 5`
now surfaces a conflict between explicit numeric alternatives. Multiple commands
for one physical field remain conflicts; `then` does not authorize sequencing.
Meaningful unmatched suffixes and unsupported clauses remain visible and blocking.

### Recognition architecture and clause ownership

`language_phrases.hpp/.cpp` owns shared entity/quantity types and focused rules for
request verbs, quantity modifiers, factors, current-value references, predicates,
arrival frequency, and processing duration. `language_recognition.cpp` owns
wrappers, clause boundaries, uncertainty protection, and one-operation emission.
The exact arithmetic/application library, public IR, simulation rules, renderer,
and project schema were not changed.

Each clause tries existing arrival, processing, and horizon patterns, then the
conversational rules, then existing generic commands. Factor relations and explicit
`to` targets are recognized before count requests. Each successful recognizer
returns one operation plus the consumed token endpoint. Unconsumed meaningful text
invalidates the proposal. Explicit topic clauses consume only their adjacent comma
and command; numeric alternatives inherit a resource field solely to report a
conflict. There is no nearest-number/entity heuristic or cross-clause pronoun guess.
Source spans are nonoverlapping original byte ranges. Existing v1 corpus spans and
recognition patterns remain exact matches.

The UI example now uses conversational addition/multiplication. Review shows the
operation type in both compact and table layouts. Missing-quantity diagnostics name
the supported field and request an exact count/factor. Interpretation still runs
only on explicit request; approval, correction validation, stale-source checks,
and unchanged active runs remain protected.

Native screenshot inspection caught an existing dialog-position issue when an
open review was resized from 1600x900 to 640x480 at 200% scale: the old modal
position left part of it outside the viewport. The dialog now recenters on resize
and reserves footer space using actual frame heights. Compact mode uses a single
toolbar row, smaller padding, and concise scrolling value/phrase rows. Native
assertions check the complete modal rectangle, approval-button bounds, and minimum
readable review height. Approval controls stay visible while the review scrolls.

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
addition/subtraction requires `by`; `to` specifies an absolute target. Multiply/divide require a factor; double/halve
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
processing, horizon, conversational, and generic entity/action patterns in that order. Explicit
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

## Evaluation and regression gates

The original **134-case corpus (74 supported, 60 rejected)** is unchanged. It
checks exact ordered operations, recognition patterns, matched source text, all
final fields, error-code sets, and acceptance. All 134 cases still match.

The development corpus adds **174 cases: 119 positive descriptions and 55 rejected
inputs**, including the reported failures, required conversational examples,
individually authored compound requests, word-order changes, mathematical
contrasts, negation, mixed unsupported material, and numeric/range failures.
Expected semantics were authored rather than captured from parser output. Exact
normalized-text overlap with the holdout was removed without changing the holdout.

The **48-case holdout (28 positive, 20 rejected)** was separately authored and
SHA-256 frozen before implementation. It was not evaluated during grammar tuning.
This is a disjoint evaluation set, not an independent human-author study. Its
first results and recognition-file hashes are retained in
[FREEZE.md](../tests/fixtures/scenario-language/holdout/FREEZE.md) and
[INITIAL-RESULTS.md](../tests/fixtures/scenario-language/holdout/INITIAL-RESULTS.md).
No grammar changes were made from holdout results. Before evaluation, its gate
was declared as at least 50% exact positive matches, all rejected cases rejected,
and zero unsafe acceptances or accepted omissions. Development requires 100%.

| Dataset | Exact supported operations | Exact final scenarios | Correct rejections | Unsafe acceptances | Accepted silent omissions |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original | 74/74; all 134 operation sequences exact | 74/74; all 134 computed outputs exact | 60/60 | 0 | 0 |
| Development | 119/119 | 119/119 | 55/55 | 0 | 0 |
| Frozen holdout | 24/28 (85.7%) | 24/28 (85.7%) | 20/20 | 0 | 0 |

The four holdout positive misses were safely rejected: `arrange a total of six docks`, `staff complement should become fifteen`, `scale the lift truck pool`,
and `shorten the service time`. They remain measured misses. They were not
relabeled as rejection examples or used to add new rules.

Version-2 development/holdout fixtures compare ordered field/action/rational/unit
semantics and the complete final scenario, including inherited fields. Rule
provenance, nonempty bounded spans, source order, and nonoverlap are checked;
focused association tests additionally check exact matched phrases. A supported
case accepted with wrong semantics is counted as unsafe, as is acceptance of a
rejection case. An accepted positive with fewer required operations, or acceptance
of a rejection tagged for unmatched material, counts as a silent omission. Safe
coverage misses lower holdout accuracy. Timing is separate from correctness.
Corpus versions describe evaluation files only; project schema remains version 1.

```powershell
build/windows-release/tests/simforge_scenario_language_eval.exe
build/windows-release/tests/simforge_scenario_language_eval.exe tests/fixtures/scenario-language/development/corpus.json
build/windows-release/tests/simforge_scenario_language_eval.exe tests/fixtures/scenario-language/holdout/corpus.json --holdout
build/windows-release/benchmarks/simforge_scenario_language_benchmark.exe
ctest --preset windows-debug --output-on-failure
ctest --test-dir build/windows-release --output-on-failure
ctest --preset windows-headless --output-on-failure
```

The six initially failing regression tests exercise the real interpreter and
workspace approval/application path. Equivalence groups compare actual typed
operations and complete scenario state. Contrast pairs distinguish set/add,
by/to, percentage/factor, interval/frequency, and duration/speed. Dedicated tests
check clause span ownership, negation with no operations, conflicts, missing
quantities, fractional output rejection, dirty state, stale proposals, and real
custom-run capacity. Existing execution, persistence, replay, recovery, and async
comparison tests remain in the full matrix.

## Performance and native validation

Release measurements use 1000 iterations per case and report median/worst total
interpretation plus tokenize, recognize, apply, validate, and mean total time.
The total includes source validation/serialization and proposal construction.
Allocation instrumentation is confined to the benchmark. Requested bytes measure
cumulative allocation traffic, not live memory. Process peak working set includes
the executable, CRT, allocator caching, and cumulative high-water memory.
The maximum-input case is exactly 8192 bytes and exercises bounded rejection.

Current raw measurements are in
[scenario-language-conversation-benchmarks.csv](scenario-language-conversation-benchmarks.csv).
The earlier [Commit 22 measurements](scenario-language-benchmarks.csv) are retained
for comparison. Interpretation remains synchronous and runs only on request.

Final Release measurements on this development machine (microseconds):

| Case | Median total | Worst total | Mean total |
| --- | ---: | ---: | ---: |
| Simple assignment | 4.800 | 58.800 | 4.857 |
| Conversational addition | 5.400 | 77.900 | 5.705 |
| Conversational multiplication | 5.000 | 9.900 | 5.049 |
| Multi-clause request | 8.400 | 20.000 | 8.494 |
| Unsupported input | 5.800 | 8.400 | 5.889 |
| Maximum input | 146.900 | 316.500 | 145.038 |
| Original Create request | 9.300 | 54.100 | 9.449 |
| Original Modify request | 6.200 | 9.800 | 6.202 |

Maximum-input stage means were 44.214 tokenize, 81.855 recognize, 4.363 apply, and
0.245 validate microseconds. Mean requested allocation count/bytes were assignment
110/8,038, addition 112/8,878, multiplication 111/8,374, compound 119/13,398,
unsupported 118/9,534, and maximum input 501/313,399. Maximum process peak working
set was **4,902,912 bytes (4.68 MiB)**. Compared with Commit 22, original Create and
Modify means increased from 8.18/5.67 to 9.45/6.20 microseconds; maximum-input mean
increased from 107.20 to 145.04. These observed timings include scheduling and
measurement overhead and do not promise a hard upper bound.

All three configurations built without compiler warnings. Full test results:
**Windows Debug 365/365**, **Windows Release 365/365**, **renderer-disabled 336/336**.
One existing constrained-warehouse graphics test timed out in the initial Release
run at its unchanged 40-second limit. It passed unchanged in isolation (4.18 seconds)
and the complete Release rerun (4.20 seconds). Interrupted diagnostic artifacts
were preserved; no timeout or assertion was relaxed.

The native test enters `Get 3 more bays`, reviews ADD and six bays, approves, then
enters `Get twice as many workers`, reviews MULTIPLY and twenty workers, and
approves. A compound request adds one forklift and halves processing without
cross-field association. Vague and negated requests disable approval and Apply.
A conversational Create proposal launches real **6 bays / 20 workers / 3 forklifts**
with 300-second arrivals and 900-second processing. Playback snapshots and
comparison metrics match authoritative headless execution. A second process
reopens the approved draft, retained runs, report, camera, and clock. The compact
640x480 review at 200% scale is captured for visual inspection. Existing bounded
PNG retention and native shutdown checks remain active.

After the screenshot-driven layout correction, the Debug native language test
passed again (29.16 seconds), and the Release review plus fresh-process reopen
passed. Both Release processes reported **zero Vulkan validation errors/warnings**
with synchronization validation enabled and clean shutdown/reinitialization.
The final native Interpret action measured **31.8 microseconds**. Inspection
confirmed readable before/proposed values and matched phrases at 640x480/200%,
visible approval controls, and actual six-bay/twenty-worker/three-forklift playback.

## Native captures

These are real application captures, not generated mockups:

* **Conversational addition** (archived capture: `scenario-language-conversation-addition.png`)
* **Conversational multiplication** (archived capture: `scenario-language-conversation-multiplication.png`)
* **Compound association** (archived capture: `scenario-language-conversation-compound.png`)
* **Missing-quantity diagnostic** (archived capture: `scenario-language-conversation-ambiguous.png`)
* **Negated request** (archived capture: `scenario-language-conversation-negated.png`)
* **Approved custom playback** (archived capture: `scenario-language-conversation-playback.png`)
* **Compact high-DPI review** (archived capture: `scenario-language-conversation-compact.png`)

Earlier Commit 22 captures remain available: **input** (archived capture: `scenario-language-input.png`),
**review** (archived capture: `scenario-language-review.png`), **error** (archived capture: `scenario-language-error.png`),
**approved editor** (archived capture: `scenario-language-applied.png`), and
**playback** (archived capture: `scenario-language-playback.png`).

## Known limitations and next milestone

This remains bounded deterministic English recognition, not general language
understanding. Unfamiliar synonyms, general pronouns, hypothetical exploration,
clock schedules, estimates, unquantified goals, and colloquial vague counts are
rejected. Only ASCII contractions are supported; smart apostrophes remain
unsupported characters. Final capacities and seconds must be integers. Existing
3D resource limits and comparison compatibility rules apply. Project proposals
remain transient; there is no new inference dependency or project migration.

Recommended next milestone: collect descriptions from independent human authors,
freeze a new untouched holdout, and evaluate coverage before making further grammar
changes or assessing whether a small local recognizer adds value. Preserve these
first holdout results. New simulation resource types, schedules, stochastic arrivals,
and optimization objectives require deliberate model/schema work first.

## Developer extension guide

1. Author a failing input with explicit intended field/action/value/unit and
   resulting state. Add an adversarial or one-word contrast, not only synonyms.
2. Put entity aliases, count modifiers, factor relations, or focused phrase rules
   in `engine/src/scenario/language_phrases.*`. Keep clause/wrapper/uncertainty
   handling in `language_recognition.cpp`. Do not change arithmetic for wording.
3. Return one `Parsed` operation with its consumed endpoint and original byte
   span. Keep semantic words (`by`, `to`, `more`, `per`, `times`, `every`) intact.
   Unmatched material must remain diagnostic. Do not guess nearest entities.
4. Keep legacy v1 fixture expectations intact. Add version-2 development cases
   with a complete validated source, mode, expected semantic operations, expected
   field overrides, and acceptance classification. Expected values must be authored
   independently of observed parser output.
5. Test typed operations, the complete scenario, rejection/approval, and source
   ownership. Run original/development corpora and renderer-disabled tests.
6. Freeze a new disjoint holdout and its gates before evaluation. Do not tune
   against this milestone's holdout or rewrite missed positives as rejections.
7. For UI changes, run the native language graphics test, inspect actual PNGs,
   and verify real playback/comparison, Save/Open, and Vulkan validation.
