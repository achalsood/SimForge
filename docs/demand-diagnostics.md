# Demand model diagnostics and predictive uncertainty

Commit 32 assesses a **frozen fitted Poisson intensity**. It does not choose a new arrival family, refit from holdout data, change generator 2, or predict warehouse throughput. [Calibration](demand-calibration.md) and [coverage/count-process semantics](stochastic-arrivals-and-coverage.md) remain compatible.

## What evidence is available

Calibration reports retain UTC coverage, the chronological split, training count/exposure estimates and confidence limits, and bounded chronological aggregate counts. Project Save/Open does **not** retain every historical timestamp. Diagnostics can use those counts only when their new boundaries are an exact coarsening of retained calibration bins. They cannot subdivide a stored count.

For different boundaries, select the original CSV. The worker reapplies the source's original coverage, import policy and split, verifies the exact byte SHA-256 and coverage-sensitive dataset identity, and checks counts against all original calibration bins. Changing line endings or a BOM changes the fingerprint. New data is not silently fitted. An unavailable original file disables that finer analysis through an explicit error; the previous successful report remains.

A completed diagnostic stores its own bounded bin counts and exposure pieces, so its results/charts survive Open without reimport. Its embedded immutable calibration preserves lineage even if the separate calibration history record is deleted. The UI can navigate to a retained source; a deleted source remains inspectable in the diagnostic JSON.

## Three distinct uncertainties

1. **Process variation:** under a fixed intensity, Poisson count mean and variance both equal integrated intensity. The point estimate is treated as known.
2. **Parameter uncertainty:** finite training observations leave the intensity uncertain. Calibration's conservative 95% marginal rate confidence intervals describe rate parameters. Our separate future count envelope includes estimated-rate uncertainty.
3. **Model uncertainty:** independence, the intensity shape or the Poisson family may be wrong. Neither count interval accounts for unknown misspecification, future distribution shift or unmeasured covariates.

The [NIST Poisson reference](https://itl.nist.gov/div898/handbook/eda/section3/eda366j.htm) gives the defining PMF/CDF and model moments. [R's distribution documentation](https://stat.ethz.ch/R-manual/R-devel/library/stats/html/Poisson.html) describes discrete quantile semantics. The implementation below uses its own bounded recurrence, not R or a plotting/statistics dependency.

## Exposure-aware diagnostic bins

Choose a training or holdout partition and a fixed width in integer seconds. Boundaries are anchored at that partition's UTC start; the last bin may be shorter. All intervals are disjoint `[start,end)`. Training and holdout never share a diagnostic bin. Width changes are manual, and exploratory settings are labeled.

Each bin retains start/end, actual observed seconds, count, valid prediction status, complete-observation status and ordered observed intensity pieces. Pieces stop at coverage and UTC profile boundaries. Constant intensity integrates as `rate * observed_seconds`; a daily profile sums its own rate over each observed piece. Closed, unobserved and interrupted periods remain distinct in the embedded coverage definition and contribute no exposure. A bin requiring an unestimated daily rate has an unavailable prediction. No exposure is not an observed zero.

The observed/expected chart uses integrated counts, not raw elapsed time. Residual stems do not connect across gaps; unavailable bins are shaded and partial exposure is tinted. Tooltips give exact UTC ownership and exposure.

## Dispersion, zero counts, tails and dependence

For each available positive-mean bin, `r_i=(n_i-mu_i)/sqrt(mu_i)` and `X²=sum(r_i²)`. We never pool heterogeneous raw count variances into an unqualified variance-to-mean ratio.

Holdout uses the frozen model, with descriptive ratio `X² / positive_mean_bins`. Training reports a nominal residual denominator subtracting the number of contributing positive fitted rate parameters. This is not a universal design-matrix-rank calculation, especially for coarse bins mixing profile strata. Nonpositive degrees of freedom make the ratio unavailable. No chi-square p-value is claimed. Simulation applies exactly the same denominator, so the reference comparison is equivalent to comparing Pearson sums at fixed settings.

Zero mean with positive observations is explicit model/point-estimate mismatch; Pearson is unavailable rather than an invented finite value. Zero mean and zero count contributes its correct zero probability of one, but no Pearson division.

The zero-count statistic is the number of available bins containing zero arrivals; its model expectation is `sum(exp(-mu_i))`. The upper-tail statistic counts bins at or above the request's predefined integer threshold, with expectation `sum(1-F_Poisson(threshold-1;mu_i))`. Maximum count is also retained. Low-rate zeros are normal. High aggregate counts do not by themselves establish individual-arrival clustering.

Lag-one Pearson residual correlation uses only ordered, fully observed, positive-mean adjacent bins with touching boundaries. At least eight eligible pairs and positive residual variance on both sides are required. Partial exposure, missing rate support, zero intensity and gaps break adjacency. It is descriptive; its absolute value is compared with synthetic references, without a calibrated serial-dependence hypothesis test.

## Fixed-parameter simulation references

The immutable request freezes source calibration, binning, partition, threshold, prediction settings, replication count and explicit base seed. These four count/residual checks form the version-1 diagnostic bundle.

Each replication derives a root seed with defined SplitMix64 domain `0x7463e0ef3b19a821`; observed intensity fragments derive seeds in domain `0x199acb4470bbdc21`. Every fragment calls the **unchanged generator 2**, with homogeneous intensity, its actual observed duration and tick rate 1 Hz. Conditional integer tick locations and counts therefore honor the importer’s UTC-second boundaries. Only observed exposure is generated. Disjoint pieces use separate seeded streams and aggregate into the requested bins. Closed/unobserved intervals are not generated as validation observations. Service/resource/event/snapshot streams are unchanged.

Reports retain every root seed and each replication's Pearson/dispersion, zero/tail/maximum counts and supported lag statistic. They retain reference quantiles at the requested central level, inclusive lower/upper tail ranks and the number of eligible replications. Lag references can have fewer eligible samples when synthetic residual variance is zero.

Ranks use `(1 + number of inclusive ties in the tail)/(R + 1)`. Both ranks can be large with ties; they are not complements. They are finite Monte Carlo descriptions, **not exact or uniformly distributed p-values** for an estimated model. An estimated standard error and approximate Wilson 95% interval for the raw upper-tail fraction show sampling precision, including boundary fractions with estimated standard error zero. Resolution is limited by the actual eligible replication count.

Training simulations are **not refitted**. Their reference is descriptive, not a calibrated training goodness-of-fit bootstrap. Holdout checks condition on the frozen estimates and do not include estimated-rate uncertainty. Repeated holdout inspection is not independent final-test confirmation. A requester can assert that settings preceded inspection; the software cannot verify that assertion. Multiple checks are unadjusted and correlated; an unusual statistic does not automatically invalidate the model. No aggregate accuracy score is produced.

Same settings, source, evidence and seed plan reproduce statistical results on the supported Windows x64/MSVC toolchain from Commit 31. Wall-clock timings are excluded from diagnostic identity. No universal compiler/CRT bit-identity guarantee is added.

## Process-only predictive count intervals

For mean `mu`, the interval is `[Q((1-level)/2), Q(1-(1-level)/2)]`, where `Q(p)` is the smallest nonnegative integer whose Poisson CDF reaches `p`. Nominal levels .80 through .99 are supported. Achievable coverage is discrete and generally exceeds the nominal level. Zero mean returns `[0,0]`.

The numerical method builds relative PMF weights about `floor(mu)` using downward/upward recurrences, then normalizes their cumulative sum. This avoids `exp(-mu)` underflow at large means. Bounds are `max(0,floor(mu-14*sqrt(mu)-100))` through `ceil(mu+14*sqrt(mu)+100)`; omitted-tail Chernoff bounds are below 1e-30 over the admitted means. Binary64 accumulation error remains. No symmetric normal approximation is used. Public quantile probabilities are limited to `[1e-8,1-1e-8]`; admitted means are finite, 0..80,000.

Tests derive an independent direct PMF sum with logarithms and `lgamma`, without using the recurrence/CDF implementation under test. Examples include .1 -> `[0,1]`, 1 -> `[0,3]`, 10 -> `[4,17]`, 100 -> `[81,120]` at 95%, and independent boundary checks through mean 80,000.

## Parameter-aware predictive envelope

This is a frequentist conservative projection, **not a posterior mixture or arbitrary widening factor**. Let `alpha=1-level` and `K` be the number of available trained rate parameters, including estimated-zero rates.

Allocate half of alpha to the family of rate estimates. Invert each Poisson count's two Chernoff tails at `alpha/(4*K)` using the same count/exposure estimator as calibration. Sum the resulting lower/upper rates times the future exposure pieces to obtain `mu_L,mu_U`. Then return `[Q_Poisson(mu_L,alpha/4),Q_Poisson(mu_U,1-alpha/4)]`.

Bonferroni bounds the probability that any true rate leaves its confidence set by alpha/2. On that set, Poisson stochastic monotonicity and the two process tails bound future failure by alpha/2. The union bound gives at least nominal coverage **under independent Poisson training counts, future count independence and an unchanged true intensity profile**, subject to numerical approximation. It applies to supported constant and predefined daily-profile models. It is often wide, and its coverage is marginal per future interval, not simultaneous coverage of all future bins.

A future horizon must start at or after the original complete observation end, including holdout, and be 1..604800 seconds in supported UTC. Historical missing exposure is not copied into a future operating schedule. Any required unestimated profile rate makes the prediction unavailable. If the upper integrated confidence bound exceeds numerical mean support, the parameter-aware envelope is unavailable with a warning; the process-only interval can remain available.

The native multi-shift example has expected future count **9.2**, process-only interval **[4,16]**, and parameter-aware envelope **[2,20]**. These are arrival-count intervals, not rate confidence intervals or throughput guarantees.

## Empirical interval coverage

For eligible disjoint holdout bins, report nominal level, evaluated count, inside/below/above counts, observed fraction and mean integer interval width. Unavailable bins are excluded explicitly. Training coverage is labeled a descriptive fit check. A small sample need not achieve exactly the nominal fraction; shared estimated parameters can induce dependence in unconditional interval membership. No unsupported binomial confidence guarantee or overlap-based sample inflation is added.

## Limits and execution

- At most 1024 diagnostic bins and 4096 observed intensity pieces.
- 1..1000 replications; at most one million fragment invocations. Thus 1000 bins × 1000 replications is admitted; 1024 × 1000 is rejected.
- At most 80,000 integrated expected arrivals per replication and five million across the request; generator support 0..1000 arrivals/second and nonzero fragment expectation at least 1e-12.
- At most 100,000 actual synthetic arrivals per replication, with explicit failure rather than truncation. Across 1000 replications the absolute admitted realization budget is bounded by 100 million; the separate expected-work limit remains five million.
- At most 8 MiB per diagnostic JSON/CSV, below the existing 32 MiB per-study limit; combined history remains 32 studies/192 MiB and project limit 256 MiB.

Use 100 replications for routine checks; 1000 is an extended setting subject to work limits. Fewer than 100 or fewer than eight positive bins yields an inconclusive dispersion summary. Quantile computation, residual aggregation and parsing are bounded CPU work. No GPU computation or unrestricted worker pool is added.

The existing calibration worker owns the request/reimport and reports validation, preparation, current/completed replications, aggregation and construction. SDL/Vulkan/ImGui remain on the owner thread. Cancellation checks occur at safe generator/bin/replication boundaries; result publication checks cancellation again. Failed/cancelled work never replaces a previous completed report, history or active playback. Workers join on shutdown. Save captures immutable completed evidence; pending unretained diagnostics must be retained or explicitly discarded. Open publishes a fully staged validated workspace.

## Native workflow and Study History

Open/retain a calibration, inspect its fitted model and parameter confidence intervals, and enter **Demand Diagnostics**. Configure partition, bins, threshold, nominal level, future horizon, replications and explicit seed. Choose exploratory labeling and verified CSV reimport when needed. Run/cancel through the existing worker controls.

Counts/residuals, reference checks and predictive uncertainty have separate compact views. Summary shows numerical validity, conservative assessment, source identity/fingerprint and limitations. Save the completed diagnostic to History; rename/add notes, reopen, export or explicitly delete it. Frozen source evidence survives deletion of its separate lineage record. Narrow windows use wrapping controls and vertical scrolling; plots are drawn from immutable bounded evidence and the reference histogram is cached once per report.

## Headless requests and exports

```powershell
build/windows-release/apps/sandbox/simforge_headless.exe --headless `
  --diagnose-demand examples/demand-diagnostics/time-varying.json `
  --export-json build/demand-diagnostics.json `
  --export-csv build/demand-diagnostics.csv
```

Request version 1 contains a relative/absolute `source_calibration` report filename, optional original `csv` path, and `settings`: partition, bin_seconds, replications, tail_threshold, seed, nominal_coverage, prediction_start UTC, prediction_seconds and exploratory flag. Paths in a request resolve relative to that request. Output paths use the existing exclusive `--export-json`/`--export-csv` contract and must differ; workflow conflicts are rejected before execution. GUI and CLI call the same implementation.

JSON report version/method 1 embeds source calibration algorithm/import versions, model parameters, SHA-256/coverage/split, UTC bins/pieces/counts, residuals, interval methods/levels, predictive coverage, all reference seeds/statistics, eligible reference counts/ranks/precision, validity, warnings and timings. Generator 2 and PRNG/grid identities are explicit; legacy source calibration generator metadata is preserved as legacy evidence.

The machine-readable 25-column CSV has typed tables for bin counts/residuals/process and parameter intervals, replication statistics, process/parameter coverage and summary, with stable IDs, partition, units, seed and method version. Undefined values are blank; JSON uses null. Coverage rows explicitly label how evaluated/inside/fraction/below/above/width map into the shared columns. JSON is the complete provenance representation.

Streaming project v2 adds `demand_diagnostics_v1` and a per-report collection. Each report is parsed/released individually; no whole-history project DOM is added. Derived exposures, predictions, residuals, observed statistics, interval coverage, seed plans and reference summaries are revalidated without rerunning Monte Carlo. Canonical CRC, atomic Save, transactional Open, recovery, captured revisions and v1/v2 compatibility remain.

## Five reproducible examples

[Generator source](../scripts/generate-demand-diagnostics.cpp) builds fourteen UTC days starting 2026-03-02, splits after seven days, and fixes reference seed 320032, 1000 replications and tail threshold 15 before diagnostic execution. A/D/E historical observations call unchanged generator 2 at 1 Hz in two seven-day blocks, with seeds `310032 + 100*example_index + week`. B/C are deterministic criticism fixtures. No seed was changed after observing a diagnostic.

| Example | Historical construction | Holdout dispersion | Process coverage | Parameter envelope coverage |
|---|---|---:|---:|---:|
| [Poisson-generating family](../examples/demand-diagnostics/poisson-compatible.json) | Homogeneous 5/hour; 781 training, 805 holdout arrivals | 1.32796 | .946429 | 1 |
| [Overdispersed](../examples/demand-diagnostics/overdispersed.json) | Alternating hourly 0/20 counts; matched fitted mean 10/hour | 10 | 0 | 0 |
| [Underdispersed](../examples/demand-diagnostics/underdispersed.json) | Exactly five periodic arrivals/hour | 0 | 1 | 1 |
| [Time-varying](../examples/demand-diagnostics/time-varying.json) | 1/hour before 08:00, 20/hour 08:00..12:00, 3/hour after noon; predefined four-hour fitted strata | 1.04276 | .976190 | 1 |
| [Gaps and shift](../examples/demand-diagnostics/gaps-and-shift.json) | 70 explicit windows, observed 08:00..12:00/13:00..18:00; historical 5/hour training, 15/hour holdout | 20.7399 | .126984 | .317460 |

The Poisson-generating example's dispersion exceeds its fixed-fitted reference central range [.794974,1.231625], with inclusive upper rank .003996. This realization is retained honestly: a correctly specified generating family can yield an unusual check, and frozen estimated rates add conditioning limitations. Process coverage is still near its nominal level; the parameter envelope covers all these bins. This is not proof that its known generating family failed.

The varying-rate example has 27 zeros versus 26.139 expected, and a dispersion within its intensity-aware reference [.788199,1.258694]. Pooling its morning/quiet raw counts would mix different means. The shifted example has 308 training and 905 holdout arrivals over 63 observed hours each; its excess Pearson sum also reflects mean mismatch, not just variance inflation. All CSVs, source calibration reports, requests and bounded numerical diagnostic reports are retained beside the examples.

## Measurements and limitations

[Release benchmark CSV](demand-diagnostics-benchmarks.csv) records 100/1000/1024-bin preparation and residual aggregation, 1/100/1000 replications where allowed, six-parameter daily profiles, 70-window gaps, verified 10K/one-million-record reimport, quantiles, serialization/parsing, project Save/Open, report/project growth, cumulative process peak and cooperative cancellation. [Native CSV](demand-diagnostics-native-benchmarks.csv) records actual active viewer frames during a nongated 1000-replication job. Cancellation tests use gates only to check publication safety; routine frame measurements have no gate.

The benchmark includes report construction/validation in total time; stage preparation/simulation counters exclude the subsequent codec validation. Peak memory is cumulative process working set, not per-stage allocation. Quantile rows time 100 interval calculations. Load/scheduling/cold-start effects are present; no matched-baseline speedup is claimed. Final measured ranges and screenshots are collected in the [completion report](commit-32-validation.md).

No service calibration, new stochastic family, automatic model search, smoothing, model-uncertainty distribution, refitted training bootstrap, exact plug-in hypothesis test, untouched final-test split, timezone/DST inference, live database ingestion, shift scheduling or warehouse optimization is added. Recommended next milestone: refit-calibrated bootstrap checks and an explicitly independent final-test partition. It has not been started.
