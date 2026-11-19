# Goldwind PlotJuggler 3.17.0 — v2 Optimization Plan

Baseline: keep the proven Windows x64 installer, CSV loader, 1,981 Chinese tag mappings, and original series IDs unchanged. Keep a working installer as a rollback release. Do not claim a feature is validated merely because CI compiles.

## Phase 1 — labels and startup
- Add a Chinese/raw/bilingual display-mode selector to the native Timeseries List; never change underlying raw IDs.
- Search Chinese displayed labels using the existing Timeseries filter. Extend matching to raw IDs as needed after testing.
- Disable the upstream startup splash image without removing unrelated UI images.
- Validate compile, silent install, CSV plugin presence, and startup. Manually verify switching and Chinese search on actual CSV.

## Phase 2 — grouping and batch plotting
- Design explicit category mapping (prefer a separate raw_tag -> category CSV, rather than guessing categories from Chinese names).
- Preserve duplicate Chinese descriptions as distinct raw series; no collapsing or alias-key collisions.
- Support multi-selection and batch plot action using native PlotJuggler plot APIs.
- Add GUI tests or a reproducible manual test script for selection and saved layouts.

## Phase 3 — performance and localization
- Benchmark representative CSV files at 100 MB, 500 MB, and 1 GB (time, peak RAM, UI responsiveness) before optimizing.
- Use background parsing/streaming or selective rendering only where upstream APIs safely support it.
- Translate common menu items, dialogs, and errors; avoid translating raw tag IDs or serialized data keys.
- Keep untranslated strings as fallback; check Chinese UTF-8 and Qt font rendering.

## Acceptance criteria
1. The last known-good installer remains available for rollback.
2. CSV import, plots, drag/drop, saved layouts, and raw data values are unchanged.
3. All 1,981 dictionary rows are packaged, and matched labels display correctly.
4. No startup splash image appears.
5. Mode switching and search work on a real Goldwind recording.
6. Category and batch functions are accepted only after manual/automated verification.
