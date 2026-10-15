# Seeded stochastic arrivals and observation coverage

SimForge supports homogeneous Poisson and piecewise Poisson arrival **count processes on the engine tick grid**. Historical records fit transparent intensity estimates using explicit observed exposure. Native playback, headless execution, snapshots, paired studies and project persistence use the same CPU implementations. Existing generator-1 periodic/profile/uniform scenarios and continuous-exposure calibration reports remain compatible.

## Count semantics and algorithm choice

A homogeneous rate `lambda` in arrivals/second over duration `T` has expected count `lambda*T` and Poisson count variance `lambda*T`. Piecewise expected counts sum `lambda_i*(end_i-start_i)`. Counts in disjoint supported tick intervals are independent under the mathematical model. A zero rate has no arrivals; gaps between intensity segments are idle. A seed selects a realization, rather than fixing its count to the expectation. The count/increment properties and conditional uniform arrival construction are described in the [MIT Poisson-process textbook](https://web.mit.edu/urban_or_book/www/book/chapter2/2.12.1.html) and its [conditional-arrival derivation](https://web.mit.edu/urban_or_book/www/book/chapter2/2.12.3.html).

Two feasible constructions were evaluated:

| Construction | Advantages | Costs for this engine |
|---|---|---|
| Exponential waits and integrated hazard | Naturally chronological; can generate incrementally | Changing rates require hazard accounting, persistent sampler state and careful quantization at boundaries. `std::log` is not universally bit-identical. |
| Interval Poisson counts and conditional uniform locations | Exact segment ownership, naturally supports simultaneous ticks and zero gaps, bounded owned realization | Sorts the realized locations; stores at most 100,000 ticks. Floating-point count sampling still needs an honest toolchain contract. |

Generator **2** uses the second construction:

1. For a segment mean `m=lambda*T`, use `k=ceil(m/16)` equal submeans `m/k <=16`. Independent Poisson subcounts add to a Poisson count of mean `m`. Zero means bypass sampling.
2. Sample each subcount with a bounded Knuth product sampler. Draw uniforms strictly inside `(0,1)` using `(double(SplitMix64 >> 12)+0.5)*2^-52`. Multiply until the product is at most `exp(-m/k)`; each product that remains above the threshold contributes one event.
3. Assign each counted event a discrete uniform tick in its segment using the existing exact integer rejection sampler, with a location stream separated from count and service streams.
4. Sort locations within each segment and assign contiguous shipment IDs in that order. Ordered non-overlapping segments preserve global nondecreasing tick order.

Poisson splitting gives independent Poisson counts on disjoint tick sets when segment counts are Poisson and locations are conditionally independent uniforms. This preserves integrated intensity at engine resolution. It avoids rounding a sampled wait repeatedly or forcing an arrival at zero. The finite PRNG and floating-point implementation approximate ideal continuous random uniforms; they are not a claim of mathematically exact randomness.

## Grid, boundaries and event order

Resolution is `1/tick_rate` seconds, with tick rate 1..1000 Hz and at most 10,000,000 horizon ticks. Segment boundaries are integer simulated seconds and always `[start,end)`. A segment covers ticks `start*tick_rate .. end*tick_rate-1`. Continuous event timestamps are **not** retained or claimed. A tick cell has expected count `lambda/tick_rate`; multiple arrivals at a tick are valid. No old rate controls an event across a segment boundary.

The scheduler orders events by `(tick,event_type,sequence)`, with completions before arrivals at equal ticks. Arrival IDs follow sorted tick order. Equal-tick arrivals receive monotonically increasing scheduling sequence numbers. There is only one pending arrival event, plus active completions; the realized vector is not copied into the scheduler heap.

The 64-bin preview groups real generated ticks. Expected preview counts integrate intensity over the same integer tick cells, including horizons not divisible by 64. Teal bars are actual arrivals for the selected seed; amber lines are model expectations. These are separate from historical observations and parameter confidence intervals.

## PRNG and reproducibility contract

SplitMix64 uses defined unsigned 64-bit wraparound. Legacy arrival and shipment-service domains are unchanged. Poisson count domain is `0x8ebc6af09c88c6e3`; location domain begins with `0x589965cc75374cc3`. Count state is seeded from the explicit scenario seed and a mixed segment index. Location draws use mixed counters/domain keys independent of count draws. Processing remains keyed to stable shipment ID in the existing service domain, independent of runtime resource scheduling. There is no global RNG, wall-clock seed, standard-library random distribution or dependence on worker scheduling.

Same scenario workload, seed and generator version reproduce ticks, count and service requirements on the tested **Windows x64 MSVC compiler 19.51.36260 (toolset directory 14.51.36231)** toolchain in Debug, Release and renderer-disabled builds. The sampler uses `std::exp` and ordinary binary64 arithmetic. Other compiler, CRT, architecture or floating-point settings have not been certified for bitwise identity; universal cross-platform bit identity is **not** promised. Calibration continues to use the existing `log`/`lgamma` numerical contract. Do not replace algorithms while retaining generator version 2. Segment repartitioning changes keyed streams even when integrated intensity agrees; it is not an identity-preserving editing operation.

Golden fixture A: homogeneous `lambda=.2`, 120 seconds, 10 Hz, seed 42, generator 2. Count **24**; first ticks **47,48,125,209,273,293,328,331,346,365,373,464**.

Golden fixture B: `[0,30):.2`, `[30,60):0`, gap `[60,70)`, `[70,120):.6`, otherwise identical. Count **35**; first ticks **6,23,47,48,65,151,164,195,703,704,714,737**. Uniform service 2..8 seconds gives shipment IDs 0..5 requirements **49,75,63,30,80,59 ticks**, unchanged across both arrival modes.

The multi-shift historical example uses deterministic fixture records at 12 arrivals/hour in observed mornings and 6/hour in observed afternoons. It is a numerical calibration reference, not a claimed Poisson-generated dataset. Its generated simulation realization uses the actual seeded sampler and varies in count and timing.

## Isolation, snapshots and pairing

Each simulation owns its complete immutable realized tick vector, generated before engine state is published. It allocates shipment/resource capacity for the validated realized count. Snapshots retain canonical scenario identity, seed, snapshot version 3 and explicit arrival-generator version 2. Restore regenerates the vector from that identity/version and validates every consumed arrival, processing requirement and pending arrival against it. No mutable arrival PRNG cursor exists to advance twice or skip on restore. Existing snapshot versions 1/2 and generator-1 version-3 snapshots retain their compatibility rules.

Fresh starts, pause/resume, reset, seek, capacity commands, recordings and project restoration continue through this path. Snapshots, report validators and project Open do not trust externally supplied arrival timing. Corrupt evidence is rejected before replacing the current workspace.

Changing only forklifts, workers or bays preserves timestamps, IDs and per-ID service draws for each matched seed. Paired statistics, sensitivity, resource interaction and independent confirmation retain direct paired differences and their existing uncertainty methods. Distinct reserved confirmation seeds remain disjoint. Changing intensity, horizon, segment partition or arrival mode creates a different external workload and is not described as identical pairing. Analytical `replications.generator_version=1` and legacy study/confirmation generator provenance identify the existing replication/seed-plan contract; embedded scenario `workload.generator_version=2` identifies the Poisson arrival algorithm. Both survive export and Save/Open.

## Limits and failure behavior

| Limit | Contract |
|---|---|
| Intensity | Finite 0..1000 arrivals/second; negative, NaN and infinity rejected. Each nonzero segment mean must be at least 1e-12 to avoid collapse at binary64 sampler precision. |
| Segments | At most 64, ordered non-overlapping positive durations within horizon |
| Integrated expected count | At most 80,000; expectation is not a realization bound |
| Realized arrivals | At most 100,000; exceeding the budget throws explicitly, with no truncated vector published |
| Sampling work | At most 100,000 accepted draws plus at most 5,064 terminating subcount draws before success/failure; integer location rejection at most 64 draws/event |
| Scheduler | One pending arrival plus at most realized-count/concurrency active completions |
| Observations | 32 MiB CSV, 1,000,000 records, no truncation |
| Coverage | At most 256 windows across a span at most 366 days |
| Evidence | At most 1024 chronological diagnostic intervals, 48 daily rates, 8 conversions/report; 1 MiB report |
| History/project | Existing 32 studies, 192 MiB combined evidence, 256 MiB project and bounded per-evidence parsing |

Poisson tails are unbounded mathematically. Realizations exceeding the safety budget are failed operations, not conditional/truncated samples presented as ordinary Poisson realizations. A smaller explicit core realization budget is available for safety testing; normal scenarios use 100,000. Analytical workload guards use the conservative 100,000 cost bound for positive Poisson intensity, so large grids/replication sets may need fewer seeds/cells. Zero integrated intensity has bound zero.

Positive decoded binary64 means below 1e-12 are rejected rather than allowing `exp(-mean)` to round to one and silently producing an always-empty process. This is a numerical support limit, not a claim that smaller physical demand is zero. The positive lower bound is far below the smallest nonzero count/exposure estimate from a supported historical observation span.

## Coverage request and CSV contract

Arrival CSV remains one `timestamp` column, strict `YYYY-MM-DDTHH:MM:SSZ` UTC integer seconds, optional UTF-8 BOM, LF/CRLF. Duplicates are simultaneous arrivals and are retained. Invalid records reject the entire import. The time range remains 1970-01-01 through the exclusive arrival end 2100-01-01; offsets, local time, DST, fractional seconds and leap seconds are unsupported.

Structured request **version 2** supplies coverage. `continuous` is forbidden in this version; version 1 still requires its explicit continuous assertion.

```json
{
  "calibration_request_version": 2,
  "csv": "arrivals.csv",
  "timestamp_format": "iso8601_utc_seconds",
  "observation_start": "2026-02-02T08:00:00Z",
  "observation_end": "2026-02-02T14:00:00Z",
  "coverage_windows": [
    {"start":"2026-02-02T08:00:00Z","end":"2026-02-02T10:00:00Z","status":"observed"},
    {"start":"2026-02-02T10:00:00Z","end":"2026-02-02T12:00:00Z","status":"closed"},
    {"start":"2026-02-02T12:00:00Z","end":"2026-02-02T14:00:00Z","status":"observed"}
  ],
  "split": "2026-02-02T13:00:00Z",
  "model": "constant",
  "daily_bin_seconds": 3600,
  "diagnostic_bin_seconds": 3600
}
```

CSV paths resolve relative to the request directory. Coverage is sorted by start before validation and identity creation. Adjacent windows are retained separately rather than merged; normalized index gives stable evidence ordering. Windows must exactly partition the declared span. Missing gaps must be explicitly declared; they cannot imply zero demand. Overlap, duplicate/zero/reversed windows, unsupported status, excessive count/span and out-of-range arithmetic are rejected.

| Status | Meaning | Fitting exposure |
|---|---|---|
| `observed` | Collection active throughout, including zero-arrival intervals | Included |
| `closed` | Historical operation closed | Excluded; distinct operational provenance |
| `unobserved` | Collection unavailable or incomplete | Excluded |
| `interrupted` | Explicit known sensor/collection interruption | Excluded; retained separately |

Records are permitted only in observed windows. A start belongs to its window; an end belongs to the next window if that window is observed. Observation end is exclusive. Empty observed windows are valid evidence. A dataset with no observed training or holdout exposure can import, but cannot fit. File bytes, normalized coverage, timestamp/duplicate/ordering policies and algorithm version form the dataset fingerprint. Content SHA-256 alone remains the digest of the original arrival CSV. Coverage-order normalization does not infer coverage from arrival extrema.

## Exposure, fitting and holdout

Constant rate is total training count divided by **observed training seconds**. For each predefined UTC daily bin, sum observed training exposure intersecting that time-of-day bin over all days and divide its observed count by that sum. Partial hours, unequal shifts and days, and a split inside an observed window are partitioned exactly. Closed/unobserved/interrupted time contributes neither count nor exposure.

Zero count with positive exposure estimates zero intensity with the existing positive upper uncertainty bound. Zero exposure means **unavailable**, with null exported rate/interval endpoints, rather than zero intensity. Sparse supported bins retain the existing conservative marginal 95% Poisson Chernoff intervals and support warnings. Confidence intervals describe parameter uncertainty under Poisson assumptions, not prediction intervals or simultaneous coverage.

Chronological `[start,split)` records fit rates and intervals. Later `[split,end)` records only evaluate the fixed model and predefined widths. Diagnostic edges insert daily-bin, split, coverage and diagnostic boundaries; no event/exposure is duplicated. Expected holdout count is `fitted_rate * observed_holdout_exposure`. Excluded gaps are shaded and are not penalized as missing observed arrivals. The constant baseline uses identical exposure partitions.

If holdout exposure includes an unestimated daily rate, total fitted prediction, error, deviance and likelihood are exported unavailable. The report retains observed totals, unavailable count/exposure and available per-bin predictions; the constant baseline remains evaluable. Positive observations against an available zero prediction retain the legacy infinite-deviance/negative-infinite-likelihood null/status convention. Repeated model comparisons using holdout are validation/model selection, not untouched final-test evidence.

Legacy continuous requests/reports retain schema/algorithm 1, identities, estimates, uncertainty, diagnostics and periodic semantics unchanged. [The committed version-1 evidence fixture](../tests/fixtures/demand-report-v1.json) round-trips byte-for-byte. Explicit windows use report/import/algorithm **2**. Parsers reconstruct bounded count/exposure evidence and recompute derived rates, predictions, warnings and conversion metadata before accepting it.

## Conversion, UI and History

Open **Analysis → Demand Calibration → Import**, select **Use explicit coverage windows**, load the version-2 request, inspect/edit rows and import/validate the CSV. Fit the immutable imported snapshot in **Model & split**; later file edits do not silently change that snapshot. Validation shows exposure-adjusted counts, rates, uncertainty, residuals and unavailable support. Coverage chart colors distinguish observed counts, closed, unobserved and interrupted windows; unestimated rates are shaded and labeled in hover/table.

Save completed evidence in Study History. In **Scenario & exports**, choose **Seeded Poisson realization** or the legacy **Periodic surrogate**. Source processing, resources, horizon and tick rate are preserved; new scenario name, explicit seed and UTC profile alignment are recorded. Poisson conversion keeps rates in arrivals/second without reciprocal rounding. It rejects a simulated horizon containing an unestimated daily bin unless **Explicitly exclude unestimated UTC hours from simulated schedule** is selected. This is a deliberate simulated operating policy, not a transfer of historical sensor downtime. Estimated zero rates remain legitimate zero-rate segments.

Preview expected versus generated counts and the actual 64-bin realization, review the policy/seed, and create a normal independent playable run or export a validated scenario. The periodic alternative still requires explicit approximation acceptance, nearest integer-second reciprocal rounding (half up) and phase resets; its counts are not Poisson. Periodic conversion rejects unavailable fitted rates.

CSV reading, sorting, fitting and experiment execution use existing cancellable owned workers. Poisson conversion preview has a cancellable worker and publishes a complete owned result only on the UI thread. Failed/cancelled import, fit or generation preserves prior completed evidence, history, original scenario and playback. Preview workers are stopped/joined during shutdown; project replacement discards stale preview ownership. Ordinary playback constructor generation is bounded, and maximum supported generation is benchmarked rather than claimed free of frame cost.

## Headless examples and exports

```powershell
build/windows-release/apps/sandbox/simforge_headless.exe --headless --scenario examples/stochastic-coverage/homogeneous.json
build/windows-release/apps/sandbox/simforge_headless.exe --headless --scenario examples/stochastic-coverage/piecewise.json

build/windows-release/apps/sandbox/simforge_headless.exe --headless `
  --calibrate-demand examples/stochastic-coverage/multi-shift.json `
  --calibration-source examples/stochastic-coverage/homogeneous.json `
  --export-scenario build/calibrated-poisson.json `
  --calibration-start 2026-02-02T08:00:00Z --calibration-seed 54321 `
  --poisson-realization --exclude-unestimated-hours `
  --export-json build/coverage-report.json --export-csv build/coverage-report.csv
```

Conversion requires source, new output path, UTC alignment and explicit seed, plus exactly one of `--poisson-realization` or `--accept-periodic-approximation`. `--exclude-unestimated-hours` is allowed only for Poisson conversion. Existing output files are rejected; no implicit overwrite. Scenario seed is also explicit in scenario JSON. All supported workflows run with renderer disabled.

Poisson scenario workload JSON uses generator 2, arrival mode `poisson` with numeric `rate_per_second` or `piecewise_poisson` with `start_seconds`, `end_seconds`, `rate_per_second` segments. Processing remains constant/uniform. Version 1 is required for legacy modes; contradictory versions/inactive parameters are rejected.

JSON evidence includes coverage/status/exposure, split, content/dataset fingerprints, validation policy, algorithm/software versions, units, count/exposure uncertainty, warnings and source/output conversion definitions. Stochastic conversion adds PRNG/version/grid, seed, available-rate mask with null unestimated fitted rates, explicit unsupported-hours policy, expected/realized totals and real histogram counts. CSV retains legacy 19-column output for unchanged legacy evidence; explicit coverage or stochastic conversions use 29 columns with typed coverage/parameter/bin/metric/conversion/intensity-segment records and explicit model/bin widths, units in headers, algorithm and uncertainty metadata. Missing rates/predictions are blank, never substituted as estimates of zero.

Project v2 still streams per-run and per-study evidence, computes canonical CRC32 incrementally, bounds parsing and publishes staged Open transactionally. New features `poisson_arrivals_v2` and `coverage_windows_v2` are declared alongside legacy features when present. Generator identity, coverage evidence, conversions, analytical studies and playback persist; raw imported timestamp arrays do not. Existing v1/v2 fixtures continue to load. Pending unretained calibration evidence must be saved/discarded before project Save, as in Commit 30. Recovery, late Save completion and captured revisions retain existing rules.

## Validation and measurements

Tests use fixed seeds with justified acceptance tolerances: seven standard errors for means, eight for variance/covariance. Poisson variance-estimate asymptotic standard error is `sqrt((mu+2*mu^2)/(n-1))`; disjoint-count covariance standard error is `sqrt(mu_a*mu_b/n)`. These are broad deterministic regression regions, not significance claims about an empirical warehouse. Low-count tests also check zero-count frequency against `exp(-mu)`; high-rate tests include simultaneous tick arrivals. Golden vectors run in all three configurations.

The homogeneous 8,192-seed fixture expects mean/variance 24: observed **24.0509 / 24.1792**, half-count means **12.0125 / 12.0385**, covariance **-0.0388136**. Piecewise expects total 36: **36.0129 / 35.8502**, segment means **6.00281 / 30.0101**, covariance **-0.151414**. Numerical fixtures independently verify observed exposure, uneven daily support, split boundaries, unavailable rates and invariance to holdout changes. Coverage/scenario/report tampering, resource pairing, recording, restore, async cancellation, exclusive exports and old evidence compatibility are tested.

[Raw Release measurements](stochastic-coverage-benchmarks.csv) cover four seeds each at low/medium/high/max intensity, piecewise zero/gap profiles, 20-point/four-seed sensitivity, 64-cell/two-seed interaction, 256-window calibration with 10,000 and one million records, and project Save/Open. Simulation timings include constructor generation and event execution; throughput uses processed arrival/completion events. Generator throughput counts actual generated arrivals. Peak working set is cumulative process memory, not isolated case allocation. Tiny cases include timer/cold-start effects. These are measurements without a matched baseline or speedup claim.

At maximum expectation 80,000, realized counts were 79,559..80,071; generation **2.17..2.40 ms**, about **33.4..36.9 million arrivals/s**, and construction+simulation **5.19..5.38 ms** in this run. The one-million-record, 256-window imports took **160.8..171.1 ms**; fit **1.33..1.43 ms**, holdout **0.049..0.052 ms**, project size **120,081..125,397 bytes**, Save **5.73..5.89 ms**, Open **23.5..30.2 ms**. Whole-process peak reached approximately **41.1 MiB**. Sensitivity/grid execution took **3.65 / 4.27 ms**; retained projects were **181,638 / 421,419 bytes**.

[Native Debug maximum-import measurements](native-stochastic-coverage-benchmark.csv) use one million simultaneous records across a valid 72-window definition without a timing gate: **597 active frames**, mean **4.98 ms**, max **12.79 ms**, mean calibration UI construction **1.98 ms**, import **2.96 s**. Presentation synchronization and test-machine load are included; these are not GPU timings. Renderer: NVIDIA GeForce RTX 5080 Laptop GPU, Vulkan 1.4.351, loader 1.4.363, ImGui 1.92.9b. The opt-in walkthrough verifies the actual controls, same/different seeds, grid boundaries, fitting/conversion, parity, paired capacity streams, cancellation, Save/fresh-process Open, 640/1280/1920 resize and minimize/restore with zero validation errors/warnings. Curated captures are real framebuffer images.

## Remaining limits

This is a transparent plug-in Poisson intensity model, not proof that historical demand is Poisson or independent. No service-time calibration, automatic fitting/model search, prediction intervals, smoothing, local timezone/DST handling, subsecond observation input, warehouse operating/shift scheduler, live ingestion, forecast guarantee or optimal resource recommendation is added. Historical missing exposure cannot identify demand during the missing period. Unsupported simulated hours require a deliberate policy. Other compiler/platform bitwise reproducibility remains unverified.

See the [Commit 31 completion/validation report](commit-31-validation.md) for the full test totals and changed-file inventory. A useful next milestone is independently validated overdispersion diagnostics and prediction uncertainty for repeated simulated realizations; it has not been started.
