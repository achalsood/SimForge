# Demand calibration and chronological validation

Demand Calibration connects local historical shipment arrivals to transparent count/exposure estimates, unseen-period diagnostics, and reproducible warehouse scenarios. The CPU module is independent of Vulkan, SDL and ImGui. The native UI and headless CLI use the same importer, partitioner, estimators, report validator and scenario converter.

## Workload semantics audit

Generator v1 is unchanged. Constant arrivals are periodic, starting at tick zero. A profile contains bounded, ordered `[start,end)` segments; the first arrival is at each segment start, subsequent arrivals advance by an integer-second interval, and gaps have no arrivals. Each new segment resets the periodic phase. Uniform interarrival workloads use an exact, seeded, discrete uniform sampler over **ticks**, with a guaranteed first arrival at zero. Processing is independently constant or uniform. SplitMix64 domain/index ownership separates arrival and service draws; changing resource capacities does not change matched workloads. Arrival ordering, snapshot validation/replay, CLI execution and paired-study compatibility continue through the existing authoritative engine.

None of these arrival generators is a Poisson process. This milestone retains Poisson **count-model evidence** and offers an explicitly reviewed periodic surrogate. It does not add exponential sampling or change existing RNG/snapshot versions. A periodic realization has no arrival-count variability across seeds. A selected seed still identifies a reproducible run and controls any existing stochastic service durations.

## Local CSV contract and coverage

```csv
timestamp
2026-01-05T08:03:12Z
2026-01-05T08:05:41Z
2026-01-05T08:05:41Z
```

Exactly one column is supported. Extra columns are rejected, including purported processing data. The header is `timestamp`; one-column quoted CSV fields, CRLF/LF, a final unterminated record, and an optional UTF-8 BOM are supported. CSV quotes mean double quotes around the complete field, as in `"2026-01-05T08:03:12Z"`; escaped/embedded quotes, multiline fields and alternate delimiters are unsupported. Header-only files represent zero observed arrivals. Empty files, blank records and truncated timestamps fail.

Timestamps must be exactly `YYYY-MM-DDTHH:MM:SSZ`. Observation resolution is one second; fractional seconds are rejected, never rounded. Calendar dates are validated. Arrivals are supported from 1970-01-01 through 2099-12-31 UTC; 2100-01-01T00:00:00Z is allowed as an exposure end. Offsets, local times, DST ambiguity, leap seconds and ambiguous date formats are rejected. Content after an optional BOM must be ASCII; UTF-16 and other encodings fail. Input ordering is stably normalized and reported. Simultaneous arrivals are legitimate and retained; duplicate count is the number of observations beyond the first at each identical timestamp.

Specify start, end, and `continuous: true`. Every observation must lie in `[start,end)`. An arrival at the split belongs to holdout; an arrival at the exposure end is rejected. The end cannot be inferred from the last event. There is one continuous observation window, limited to 366 days. Zero-arrival periods are observed exposure. Unknown coverage, collection interruptions, closure periods and disjoint shifts cannot be represented and must not be declared continuous. Coverage assertions are supplied by the user; timestamps cannot establish their truth.

Imports are all-or-nothing: a malformed row produces a row-numbered error and preserves the previous validated snapshot/report. No records are silently discarded. Limits are 32 MiB/file, 1,000,000 records, one column, 128 bytes/field, and 256 bytes/source basename. Core arithmetic uses bounded integer UTC seconds. Calibration stays local, makes no network requests, and does not log raw datasets.

## Provenance and split

Content is identified with portable FIPS 180-4 SHA-256, tested against empty, `abc`, and million-`a` reference vectors. Dataset identity additionally includes exposure, UTC-second interpretation, import schema and the sort/retain-duplicates policy. Reports retain the filename basename, digest, accepted/duplicate counts, reordered flag, import schema, validation policy and software version. Rejected count is zero on every successful report; failed imports do not publish partial evidence. Absolute source paths are not retained.

The split is a manually specified UTC boundary strictly inside the exposure. Training is `[start,split)` and holdout `[split,end)`. Their durations are known, nonempty and nonoverlapping. Bin widths are configured before fitting; the importer never chooses a split or bin width based on the observed holdout pattern. No normalization, fitted rates or confidence intervals use holdout observations. Leakage tests change holdout counts while asserting unchanged parameters, intervals, bin edges and converted workloads.

## Estimation and uncertainty

Constant intensity is `training_count / training_exposure_seconds`, expressed as arrivals/second. UI rates multiply by 3,600 for arrivals/hour. A daily profile has predefined equal UTC clock bins anchored at midnight. Width must divide 86,400 and lie between 1,800 and 86,400 seconds, giving at most 48 parameters. It requires at least 24 hours of continuous training exposure, so every clock bin has observed training support. Counts and **actual exposure** aggregate across days. Partial days contribute only their observed seconds. A rate is count divided by that bin's training exposure. Zero-count bins have zero estimated intensity and positive upper uncertainty bounds. Bins with fewer than five training arrivals warn about sparse evidence; they are not advertised as precise.

Rate uncertainty uses conservative two-sided Poisson Chernoff inversion. Let `n` be the training count, `m` its unknown Poisson mean, `T` exposure, and `c = log(40)`. For `n > 0`, solve

```text
m - n + n log(n/m) = c
```

on each side of `n`, and divide the roots by `T`. For `n = 0`, return `[0, log(40)/T]`. Each of the two excluded tails has Chernoff bound at most 0.025, so coverage is **at least 95% under the Poisson assumptions**, including low counts. These are conservative concentration intervals, not exact Garwood intervals or asymptotic Wald intervals. Independent Newton fixtures and direct Poisson probability sums check the numerical implementation. The count-tail argument follows [CMU's Chernoff bound for Poisson tails](https://www.cs.cmu.edu/~harchol/Probability/chapters/chpt18.pdf). SHA-256 follows [NIST FIPS 180-4](https://csrc.nist.gov/pubs/fips/180-4/upd1/final).

Intervals describe uncertainty in the rate parameter under independent Poisson counts. They do not predict future-count variability, validate the structural model, or provide simultaneous coverage across multiple bins. Bursty arrivals or serial dependence can violate these assumptions. No paired Student-t comparison code is reused for rate intervals.

## Holdout diagnostics and manual comparison

Diagnostic boundaries include the configured duration blocks, UTC rate-bin edges, the split, and exposure end. There are at most 1,024 intervals. Every interval has known exposure; increase widths or shorten the observation window if the limit is exceeded. Boundaries are fixed before inspecting observations. Expected counts integrate the **training** rate across actual holdout exposure. The constant baseline uses the identical split and diagnostic intervals.

Reports include observed and expected totals, signed error `observed - expected`, absolute total-count error, signed error/hour, and per-interval observed counts, predictions, constant-baseline predictions and residuals. Joint bin-count Poisson log likelihood and deviance are included for training and holdout. For a bin with observation `n` and expectation `mu`, deviance is `2[n log(n/mu) - (n-mu)]`; zero-count contribution is `2mu`. Log likelihood is `n log(mu) - mu - log(n!)`. A positive count with zero expectation has infinite deviance and negative-infinite likelihood; JSON uses `null` and `likelihood_status: zero_prediction_positive_count`, CSV leaves those numbers empty, and the UI explains the limit. Missing quantities are never exported as zero.

Sparse bins, zero observed intervals, total under/overprediction, a descriptive spike threshold, and large descriptive Pearson residual sums produce measured warnings. These are conservative descriptive checks, not formal burstiness/significance tests. No short-timescale burst claim is made beyond the configured diagnostic resolution. Equal predicted and observed totals do not establish a correct temporal pattern: the morning-surge fixture has the same total error for both models, while its constant baseline misses the hourly surge and has larger bin deviance.

Selection is manual. A complex profile is not automatically recommended. If a model is chosen after holdout comparison, that holdout is validation/model-selection data, not untouched independent final-test confirmation. No resource optimization or service-time estimation occurs.

## Native workflow, charts and playback

Open **Analysis → Demand Calibration**. Import a local CSV after declaring continuous coverage. Inspect accepted counts, duplicates, ordering, exposure, fingerprint and the worker-built 64-block observed-count overview. The imported immutable snapshot owns the observations; editing UI buffers or the source file does not alter that snapshot. Re-import to change it.

In **Model & split**, set an explicit split and predefined widths, choose constant or daily intensity, and fit. In **Validation**, inspect training, holdout and constant-baseline numbers. Charts show observed counts, fitted intensity, holdout observed versus expected counts, and residuals. UTC charts use exposure-proportional widths even for partial intervals; hover shows exact interval ownership/counts/expectation/errors. The fitted chart is a bar profile, without fabricated smoothing. Rates and marginal intervals have a numerical table; all interval diagnostics use a clipped bounded table. Scroll to see the full evidence on smaller windows. Gaps are rejected, so every displayed interval has known continuous coverage.

In **Scenario & exports**, save the immutable calibration study, export JSON/CSV, and preview a scenario using the current validated source draft. Name the new scenario, provide a seed, and choose UTC start for daily-bin alignment. Review every fitted versus represented rate and the actual periodic horizon count. Explicitly accept the periodic approximation before export or creation of a playable run. The original source is preserved; service durations/distributions, resource counts, horizon and tick rate are preserved.

Conversion rounds `1/rate` to the nearest integer **second**, ties upward, with explicit displayed rate differences. Nonzero reciprocals outside 1..604,800 seconds are rejected rather than clamped. Zero-rate bins become profile gaps; zero overall intensity becomes an empty profile. UTC alignment expands the repeating model into ordered horizon segments; more than 64 nonzero segments or more than 100,000 possible arrivals is rejected by conversion/authoritative validation. Each segment begins with an arrival, so finite-horizon count is not generally `rate × horizon`; the preview displays that actual count. Tick timing is the represented integer-second schedule multiplied by the source tick rate. There is no exponential quantization claim or zero-duration sampling loop.

Converted scenarios run through normal Start/Pause/Resume/Step/Speed/Seek/Reset, authoritative metrics and viewport state. The native walkthrough compares its completed runtime snapshot against a separate headless run at the same seed. Conversion evidence retains source/new scenario definitions, fitted/represented rates, UTC alignment, seed, algorithm/generator versions, explicit acceptance and semantics, with at most eight conversions per study.

## Workers and project persistence

One bounded calibration worker owns its copied file request or immutable dataset snapshot. Phases are Reading, Parsing, Validating, Partitioning, Fitting, Holdout evaluation, Report construction and Publication. Progress states describe real work; there is no invented percentage. Cancellation checks occur between I/O chunks, hash blocks, rows, sort operations and diagnostic bins. A failed/cancelled attempt leaves the previous dataset/report and simulations available. Vulkan/ImGui calls and report publication remain on the main thread. Calibration publication is deferred during Open and Save-before-transition. Successful project replacement destroys/joins the old owned worker; it cannot publish into the replacement workspace.

Save captures completed, explicitly retained reports as `shared_ptr<const Report>`. Running jobs are not waited for or implicitly saved; subsequently published evidence remains dirty. Save/discard a completed unretained calibration before project Save. Study History shares the existing 32-study and 192-MiB combined limits. Each calibration is at most 1 MiB. Explicit deletion is required at capacity. Aggregate evidence is persisted; raw timestamps are not. Reopen restores models, diagnostics, conversion provenance and playable scenarios without rereading CSVs or refitting raw observations. Re-import is required to change split/bin choices after reopening.

Project format stays **v2**, with optional `demand_calibrations`, `selected_calibration`, and an explicit `demand_calibration_v1` feature. Existing v1/v2 projects remain readable. Older binaries do not understand this optional feature and reject projects that contain it. The streaming writer emits one immutable calibration report at a time. The bounded reader reconstructs/discards each report subtree, verifies the canonical project integrity digest, and recomputes all derived calibration statistics from retained count/exposure evidence. Unsupported versions, mismatched values, duplicate IDs, overlapping partitions and inconsistent conversion provenance reject the staged load transactionally. Project integrity checks accidental corruption, not authenticity or scientific representativeness.

## Headless requests and exports

```powershell
build/windows-release/apps/sandbox/simforge_headless.exe --headless `
  --calibrate-demand examples/demand-calibration/morning-surge.json `
  --export-json build/demand-report.json --export-csv build/demand-report.csv
```

Request v1 contains `calibration_request_version`, `csv`, `timestamp_format: iso8601_utc_seconds`, `observation_start`, `observation_end`, `continuous`, `split`, `model: constant | daily_profile`, `daily_bin_seconds`, and `diagnostic_bin_seconds`. CSV paths are relative to the request file. Unknown, duplicate or contradictory fields are rejected. See the four example requests for complete files.

Optional explicit periodic scenario conversion:

```powershell
build/windows-release/apps/sandbox/simforge_headless.exe --headless `
  --calibrate-demand examples/demand-calibration/morning-surge.json `
  --export-json build/converted-evidence.json --export-scenario build/calibrated.json `
  --calibration-source examples/warehouse-baseline.json `
  --calibration-start 2026-01-05T08:00:00Z --calibration-seed 54321 `
  --accept-periodic-approximation
build/windows-release/apps/sandbox/simforge_headless.exe --headless --scenario build/calibrated.json
```

Outputs must be different, new files; existing files are never implicitly replaced. Export JSON includes provenance, import policies, exposures, split, units, rates/intervals, all metrics/bins, assumptions/warnings, model/generator versions, timings and any scenario conversion evidence. CSV has stable `record_type` rows (`parameter`, `observed_bin`, `metrics`), calibration/dataset IDs and fingerprint on every row, explicit `_seconds`, `_per_second`, `_count` fields, UTC endpoints, partition ownership and likelihood availability. Full metadata and assumptions reside in JSON. Study History export includes calibration evidence alongside analytical studies.

## Curated examples

All four fixtures cover Jan 5–8, 2026 UTC, with Jan 5–6 training and Jan 7 validation. They are evenly spaced deterministic descriptive datasets, **not generated Poisson realizations**. Both committed generators produce the documented truth without choosing a favorable split.

| Fixture | Training truth | Holdout truth | Interpretation |
|---|---|---|---|
| constant | 4 arrivals/hour; 192 total | 96 | Constant baseline is reasonable for this count pattern. |
| morning-surge | 30/hour 08:00–12:00, 6/hour otherwise; 480 total | 240 | Daily model captures hourly pattern; constant predicts the same total but misses the surge. |
| sparse | 1/day at 08:00; 2 total | 1 | Few events produce wide intervals and sparse-evidence warnings. |
| distribution-shift | 6/hour; 288 total | 18/hour; 432 total | Constant holdout prediction 144, underprediction 288; no claim of generalization. |

`malformed.csv` contains an invalid calendar date for failure preservation checks. Run `scripts/generate-demand-fixtures.ps1` or the equivalent Python generator to reproduce valid fixtures.

## Validation and measurements

Numerical fixtures cover timestamp/calendar boundaries, rate units, zero/low/large counts, short/long exposure, unequal partial-day exposure, Poisson intervals and deterministic coverage sums, independent deviance/log-likelihood values, and zero-prediction failures. Input tests include quotes, BOM, encoding, sorting, duplicates, extra columns, bad dates, endpoints, overflow and maximum accepted/rejected sizes. Leakage tests compare both models under changed holdout records. Existing workload RNG, ordering, paired matching and snapshots are unchanged and retain their full suites. Calibration adds conversion/service preservation, replay, report tampering, exclusive CLI exports, project round-trip, streaming ownership/revision, async Save/Open, cancellation and combined-retention tests.

Opt-in `simforge_demand_graphics` drives native controls and inspects actual datasets, rates, charts, scenarios and runtime snapshots; it saves/reopens the project, rejects malformed input, cancels a gated real phase, resizes/minimizes/restores, and requires zero Vulkan warnings/errors. Review PNGs are real frame captures. See [delivery validation](commit-30-validation.md) for test totals and curated screenshots.

Release measurements are in [demand-calibration-benchmarks.csv](demand-calibration-benchmarks.csv). The benchmark imports 10,000, 100,000 and the maximum 1,000,000 records, continuously observed over 30 days with a 20-day training split, 24 daily rate bins, and 720 chronological diagnostics. It measures import, partition/fitting, holdout evaluation, serialization, Save/Open, simulation, cancellation and full end-to-end wall time. Fixture generation is excluded. End-to-end time includes dataset checks, conversion/report validation and persistence; it is not the sum of just fitting and evaluation. Working-set peaks are process high-water marks including prior cases and calibration/simulation libraries, not per-stage allocation measurements. Gate-based cancellation latency measures a real cooperative phase boundary, not every possible I/O/storage stall. Native frame/UI measurements include presentation synchronization and are not GPU timings. There is no performance baseline or speedup claim.

In the recorded Windows x64 Release run, maximum-size import took 136–139 ms, partition/fitting 1.14–1.23 ms, holdout evaluation 0.038–0.041 ms, and the full workflow 220–222 ms. Process peak working set reached 49.0 MiB; the retained report/conversion increased project size by approximately 152–163.5 KiB. A separate [native maximum-import measurement](demand-calibration-native-benchmarks.csv), with the RTX 5080 Vulkan renderer active, observed 59 real frames during import: mean 3.97 ms, maximum 8.48 ms, calibration-panel build mean 62.7 microseconds, full import 232 ms. This native stress fixture contains one million legitimate same-second records; it measures import behavior rather than claiming a representative demand process. Its owned temporary CSV is deleted after measurement.

## Current limits and next work

There is no Poisson simulation generator, coverage-gap support, fixed-offset/local-time input, fractional-second observations, prediction interval, formal burst test, service calibration, automatic model recommendation, future forecast guarantee or resource optimization. Maximum-rate/segment constraints may make a fitted model ineligible for periodic conversion; evidence and exports remain available. A recommended subsequent milestone is an independently audited seeded count-process generator and explicit observation windows, with deterministic sampling and time-resolution tests. It is not automatically started by this change.
