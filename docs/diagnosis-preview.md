# Real sample compatibility and diagnostic preflight: 3.17.0.21 preview

Stable baseline remains 3.17.0.18. This milestone completes event-format compatibility and adds an explicit raw-data quality gate before subsequent rule execution.

## Verified locally, without publishing samples
- StatusCode input: UTF-8, 7 metadata lines, real header containing stTriggerTimeStamp, 23 twelve-column records and a terminal //END.
- ActionList input: UTF-8, 4 metadata lines, NO column header, 18 four-column records and //END. Variable-width date/time components require native Qt-compatible normalization.
- Recording: 6000 sample rows spanning 59.990 seconds. A relevant SC event is inside that range. No ActionList event is in that recording range.
- One duplicate timestamp, zero backward timestamps. The production pure C++ quality inspector was run locally on the original recording's numeric samples and rejected readiness because of the duplicate.
- All event timestamps were checked locally using Qt date/regex validation and standard CSV shape checks. Original uploaded files have not yet been exercised through the installed Windows UI. Public CI uses invented examples with the same layout, never the original rows or paths.

## Product behavior
Automatic SC header matching accepts stTriggerTimeStamp. Headerless ActionList recognition is enabled only for ActionList imports in automatic-header mode and requires four vendor metadata keys plus a strict four-column structure. The inferred names and mapping are shown for confirmation: SeqNo, TimeStamp, OperationMode, ActionList. The action list is preserved verbatim; no r/f semantics or operational state are inferred.

Importing SC selects an event within the loaded recording range when available. Importing subsequent ActionList records preserves the current event center. Loaded-record counts remain separate from current-window counts, so out-of-window actions are not reported as failed imports.

The diagnostic preflight button inspects selected original samples, not the 50k rendering cache. It reports finite/invalid counts, duplicate/backward timestamps, largest adjacent gap and whether the requested window is covered. The user sets the maximum allowed sampling gap (default 100 ms, not a turbine fault threshold). Any duplicate, reversal, invalid sample, excessive gap or missing window coverage blocks readiness. Passing this gate does not prove absence of a fault.

Four-family root-cause algorithms, validated rule execution and batch/network diagnostics remain subsequent milestones. No license/authentication functionality is added.
