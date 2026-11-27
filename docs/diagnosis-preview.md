# Candidate evidence fusion: 3.17.0.24 preview

This is a further diagnostic-engine integration milestone, not a validated automatic root-cause system. Rule calibration with confirmed field outcomes is still required.

## Operator flow
1. Set the declared turbine vendor/model/controller, event center/window and SC time offset.
2. Open Condition Pack, configure/map the waveform conditions and run them.
3. Open Candidate and Event Fusion. Add a candidate, select its required waveform conditions and event-record requirements; all requirements use AND semantics. Supply source information and mark complete experimental definitions pending validation.
4. Confirm that the event logs belong to this turbine and that their time definition is correct. Separate SC/ActionList coverage assertions default to false. Only assert complete coverage after checking the actual source logs for this exact window.
5. Include pending definitions explicitly when trialling them, run fusion, inspect each predicate and matched record, and export the report. Save the condition pack to retain candidate definitions.

Old v1 packs without candidates still load. New candidate definitions use an optional candidates array in the same format. Older application versions reject the extension rather than silently discard it.

## Semantics and safeguards
- Exact event matching compares trimmed full original code text, case-sensitively.
- SC numeric-prefix matching retains leading zeros and requires either the whole numeric code or a following underscore. It does not match a longer numeric code.
- Action-token matching compares full semicolon-delimited action items, preserving r/f prefixes. Repeated identical tokens in one record count once.
- Only record_presence semantics are accepted. Operating state is not treated as a fault-active flag, and trigger/recovery or r/f meanings are not inferred.
- An event hit is evidence of a record, not proof of a fault remaining active. With no hit, the term is unknown unless the user confirmed identity/time and the corresponding log's complete window coverage.
- Waveform data/parameter/mapping failures and unexecuted dependencies are unknown, not normal. A failed numerical condition means the candidate's requirements were not jointly met; it is not an all-clear for the turbine.
- Pure log or pure waveform candidates are labelled accordingly. Joint support still requires field verification. No probability or confidence percentage is invented.
- Draft/disabled candidates never run. Pending candidates and dependencies require explicit trial inclusion. Imported validated status remains a source declaration, not software certification.
- Editing a condition demotes source-validated candidates that depend on it and clears their review declarations. Referenced condition IDs cannot be renamed or deleted until candidate references are updated.
- Candidate references, code modes, review/source metadata and size limits are strictly validated. There are no scripts or expressions.

## Compute and provenance
Candidate changes do not recompute existing waveform features when their numerical definitions are unchanged. Mapping, condition or context changes clear the numerical context. Every fusion run indexes only needed event predicates once, shared across candidates.

The report preserves the candidate/condition definitions, immutable numerical context and raw-data fingerprints, imported/executed package hashes, event identity and coverage assertions, event-snapshot hash, window and SC offset context, predicate truth/reason and matched source records. Event fingerprints hash canonical JSON records including original/aligned times, source identity and raw status, with the versioned bladescope-events-json-v1 prefix. They identify the normalized event snapshot, not the original file bytes.

CSV exports separate context, definitions, numerical evidence, predicates and matched-event rows instead of repeating the entire run in every candidate row. Spreadsheet formula-like scalar prefixes are escaped; the JSON report retains original values.

Each candidate exports at most 256 matched-event details, distributed across its event requirements; total hit counts remain complete and truncation is explicit. Cancellation publishes no partial output. Changes to confirmation/coverage flags invalidate the previous export without rereading waveform data.

Synthetic regression covers positive, absent/unknown and explicitly-covered-absent records, identity confirmation, leading-zero boundaries, action tokens, duplicate tokens, windows, scope, stale calculations, AND truth, lifecycle demotion, dangling references, unsupported active-fault semantics, bounded evidence and cancellation. Qt regression runs the real fusion dialog and verifies stale-export invalidation.

Remaining: manufacturer/case validation, reviewed legacy rule adaptation, batch/network tasks and final production acceptance. User raw recordings, original configuration tables and private inventory remain excluded from public source and binaries.
