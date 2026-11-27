# Vendor event format preview (3.17.0.20)

Stable product baseline remains 3.17.0.18. This preview completes the shared event-import and evidence-navigation milestone, not the four automatic diagnostic algorithms.

## Import workflow
1. Analysis → Fault analysis (preview); choose SC or ActionList.
2. Auto detection checks UTF-8/BOM, GB18030 and BOM/structured UTF-16, plus comma, tab and semicolon CSV structure. A successful character decode alone is not accepted as a valid import.
3. Inspect the first 20 parsed rows in the bounded dialog. If necessary change encoding, delimiter or the 1-based **nonempty CSV record** containing the header, then regenerate the preview.
4. Select distinct time and code/action columns; state and description are optional. Choose numeric seconds/milliseconds, timezone and custom text format explicitly. Confirmation requires a parseable preview event.
5. Strict import is the default. The opt-in valid-only mode retains rejection reasons and original nonempty record numbers; malformed quotes always abort. Only surplus empty trailing fields may be trimmed. Nonempty extra fields are never merged.
6. SC and ActionList keep separate local mapping preferences. Reimporting the same source and kind replaces its records; importing another file preserves existing sources.

No actual user data or source paths are stored in the repository. Synthetic tests cover both dialogs and background event publication, encodings, delimiters, multiline quoted fields, invalid shape, wrong UTF-16, preamble records and rejection accounting.

## Evidence navigation
Previous/next event buttons use aligned event times. Evidence selections persist when revisiting the page. The recording view shows the selected center as a neutral marker, not as an inferred trigger or root cause. A collapsible coverage summary counts finite original samples, invalid values and largest adjacent valid-sample gaps within the requested window. Endpoint coverage is descriptive and does not establish uninterrupted coverage or sensor validity.

## Remaining limitations
Actual uploaded SC, ActionList and TR sample compatibility has not been verified because the execution environment is unavailable. Four-family automatic diagnosis, validated rule execution, automatic trigger/recovery interpretation, batch/network jobs and full immutable diagnostic snapshots are subsequent milestones. The event reader retains the 64 MiB file limit. User recordings/configuration tables are not included in CI or releases.

Use the portable preview to compare against stable 3.17.0.18. The installer shares its application ID and installation directory with prior versions.

## Vendor CSV compatibility update
The header selector now defaults to 0 (automatic). A bounded scan recognizes explicit time and code/action column names. Vendor preamble lines are skipped **before** CSV quote parsing. Manual values identify the nonempty physical header line, so even metadata containing non-CSV quotes is supported. Multiline data fields still use strict CSV parsing.

A terminal standalone //END marker is counted separately and not imported. Nonblank content after it is rejected; //END inside a quoted multiline field remains ordinary data. The preview shows the detected preamble and footer counts.

IEC DT# and DATE_AND_TIME# timestamps accept a date/clock hyphen, single-digit date/hour fields, explicit zones and grouped fractional seconds such as .123_000_000. Only values representable exactly in milliseconds are admitted; finer nonzero precision produces a rejection instead of silent truncation. The selected timezone applies to zone-less text. Original time text is retained in the result and CSV export.

Clicking an event displays its source, record number, original/UTC/aligned timestamps and unmodified status/description. This is evidence inspection, not an automatic root-cause verdict. Regression cases use invented metadata and events; the uploaded real files have not been byte-validated while the execution environment is unavailable.
