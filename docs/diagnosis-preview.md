# Single-event rule trial: 3.17.0.22 preview

This milestone provides four transparent experimental conditions. It does not convert the legacy draft inventory into validated root-cause rules and does not assign diagnostic probabilities.

## Entry and workflow
Analysis → Fault analysis (preview) → Single-event rule trial. Set the event center/window and maximum allowed sampling gap in Event Parameters first. Choose vendor/model, original primary and optional command signals, target units, explicit positive conversion gains and a positive threshold. Confirm signal definitions; pitch actual/command must come from the same declared blade axis. No turbine thresholds are prefilled.

Mappings and parameters are stored locally per vendor/model. Changing the family clears the threshold and resets unit gains. Signal/unit/value changes clear confirmation. All parameter changes invalidate the previous export; a fresh trial is required.

## Conditions
- Overspeed: converted speed strictly above the specified threshold for the requested duration.
- Pitch following: absolute actual-command difference strictly above threshold for the requested duration. Both signals require complete valid coverage; nearest-neighbor matching uses an explicit maximum time difference. Any unmatched point returns unknown.
- Vibration: sample RMS of the demeaned signal; selectable acceleration or velocity units must correspond to the selected sensor and conversion gain. This is not a band-limited vibration diagnosis.
- Speed fluctuation: sample RMS after removing the least-squares linear trend, with a minimum number of complete cycles estimated between hysteretic zero crossings (deadband 5% of the RMS threshold). A ramp alone does not qualify. Frequency-band and mechanical-fault attribution are not implemented.

## Data and numerical contract
The main thread copies the selected original source windows into immutable vectors before background computation. Rendering decimation and plot condition filters are not used. Both primary and reference are checked for missing endpoints, invalid values, duplicate/reversed time and excessive sampling gaps. These produce unknown, not normal.

An opt-in setting merges only adjacent points with exactly identical timestamps AND finite values, in the analysis copies. Conflicting duplicate values remain invalid. Original series are not modified. Counts of merged primary/reference points are reported. The original private example retained all 6000 source rows while its single identical duplicate was coalesced into 5999 analysis rows in a local test.

Sustained conditions use actual elapsed milliseconds, not point counts. Nonunique millisecond timestamps are rejected for duration calculations. RMS and trend fitting use centered long-double accumulators; converted-value or RMS overflow returns unknown. Trial results are scoped to an explicit vendor/model, and pitch mappings require matching axis declarations.

## Result and export
Outcomes: experimental condition supported, condition not supported, insufficient data/quality, not applicable, invalid configuration. Every result remains pending validation. The report and UTF-8 CSV export include feature/threshold, conversion units/gains, event UTC window and SC offset context, mappings, axis, sample and matching counts, quality, merge counts, cycle/duration metrics and algorithm definition.

The UI prevents stale results from being exported after edits. Cancellation does not publish a result. Parameters/cases remain local; no raw user files, inventory paths or credentials are included in public builds.

## Validation and remaining scope
Pure C++ regression covers the four condition families, wrong scope, units, same-axis restrictions, reference mismatch/tolerance, duplicate handling, truncation, linear ramps and cancellation. Qt regression exercises real trial dialog entry, asynchronous computation and stale-export invalidation. Windows repeats the standalone tests before the full build and installed-UI acceptance.

Validated rule-package execution, automatic root-cause attribution, reviewed legacy threshold import, batch/network modes and frequency-band vibration/oscillation diagnosis remain future milestones. Case ground truth is still needed to validate turbine-specific rules.
