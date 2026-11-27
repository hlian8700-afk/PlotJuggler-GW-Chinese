# Condition pack manager: 3.17.0.23 preview

This milestone adds versioned, scoped condition packs and shared-snapshot multi-condition runs. It remains in the diagnostic-engine integration phase. It does not convert the 131679-row draft inventory into validated root-cause rules.

## Operator workflow
1. In Fault Analysis → Event Parameters, declare the current vendor, model and optional controller/firmware identity, then choose the event window.
2. Open Condition Pack. New packs contain four empty draft templates, not manufacturer thresholds.
3. Edit a template: specify its canonical primary/reference role IDs, target unit, positive threshold, timing, source citation and optional original source version/hash/row. Mark it pending validation when complete.
4. On Role Mappings, choose the actual original signal, input unit, explicit additional scale and blade axis. Missing saved signals remain unmapped; they are never rebound to another channel.
5. Confirm mappings. Pending conditions run only when the explicit include-pending option is checked. Draft and disabled conditions never run.
6. Run once, inspect each result and its parameters, link the selected mapped signals back to the recording view, and export the JSON/CSV evidence report.

Pack JSON contains definitions and roles. Actual signal bindings are saved locally under a hash of vendor/model/controller/pack ID/version. The imported pack's vendor/model and, when present, controller must match the current declared context exactly. A controller-specific pack does not run when the controller context is unknown.

## Lifecycle and trust
The editor offers draft, pending-validation and disabled states. Editing a source-declared validated rule clears its reviewer/date/cases and demotes it. Imported validated conditions require nonempty reviewer, ISO date and case IDs; these are declarations supplied by the source, not software certification or a guarantee of correctness. All numerical results remain condition evidence rather than automatic root-cause verdicts.

Unknown JSON fields, duplicate IDs, incompatible role target units, invalid numbers, excessive sizes and missing review/source data are rejected. No expressions or scripts are evaluated. Limits are 2 MiB, 64 conditions and 32 roles. The schema is in schemas/condition-pack-v1.schema.json; additional ID and cross-role constraints are enforced by the native reader.

The audited legacy SQLite package remains a separate draft-ledger inspection function. It is not accepted as executable condition JSON. Unresolved legacy priorities and prose are not guessed.

## Units, data and compute
Supported explicit same-dimension conversions include rad/s → rpm, rad → deg, g → m/s2 and m/s → mm/s. Velocity and acceleration are not interchangeable. Conflicting units/scales/axes assigned to one actual signal are rejected for the affected conditions. Pitch mappings must name distinct signals on the same declared axis.

Each eligible source signal is copied once per run into an immutable original-data snapshot, shared by the conditions. There is no cross-run snapshot reuse that could conceal a source update. Rendering decimation and display filters are not used. Existing quality gates and the opt-in identical-duplicate coalescing operate on copies. Cancellation publishes no partial report.

Reports include the complete executed pack, lifecycle, mappings/units/scales/axes, window/event context, per-condition parameters and quality/matching/coalescing counts, imported and executed pack SHA256, and sample counts/fingerprints for each raw snapshot. The fingerprint format is little-endian IEEE binary64 time/value pairs with a versioned prefix. Optional original source file hashes are preserved as declared citations, not automatically verified against a local file.

Edits invalidate the previous result/export. Reports display missing/invalid data distinctly from a failed numerical condition. CSV rows retain both their condition/evidence and run context; JSON retains the complete structured run.

## Validation and remaining work
Synthetic tests cover scope and controller mismatch, lifecycle gating, missing and conflicting mappings, same-axis restrictions, all four families, unit dimensions, malformed packs, review declarations, fingerprint changes and cancellation. Qt tests cover the manager's draft defaults, asynchronous run and stale-export invalidation.

Real-case ground truth and manufacturer-specific rule validation remain necessary before promoting rules for operational diagnosis. Batch/network tasks and final end-to-end production acceptance remain subsequent phases. User recordings, original configuration tables and private inventory paths are not published with these builds.
