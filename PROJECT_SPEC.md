# PlotJuggler 3.17.0 — Goldwind Chinese labels

This is the private working repository for a Windows x64 custom build based on the upstream PlotJuggler **3.17.0** release.

## Required behavior

- In the native Timeseries List, display the Chinese description for each exact raw series tag found in the supplied Goldwind mapping.
- Keep the original raw tag as the series identity used by the data store, drag/drop, plots, transforms, and layout persistence.
- If a tag is not in the mapping, display its original English/raw name unchanged.
- Do not rewrite, rename, or otherwise modify source recording files.
- Include all 1,981 mapping entries supplied by the user.
- Deliver a Windows x64 installer only after a real CI build and smoke test.

## Implementation constraints

This is an application UI change, not a Toolbox-only side panel or a standalone dictionary viewer. The label must be a presentation-layer alias; internal series keys and serialized layout references must remain raw names.

## Build target

- Upstream: PlotJuggler 3.17.0 (exact tag; do not silently upgrade to 3.17.1/3.17.2 or 4.x)
- Platform: Windows x64
- Delivery: installer executable plus build/test logs

## Validation checklist

- [ ] Build the exact upstream 3.17.0 source with the UI patch.
- [ ] Load the full 1,981-row mapping and verify it parses correctly.
- [ ] Confirm mapped tags show Chinese labels in the native Timeseries List.
- [ ] Confirm unknown tags remain unchanged.
- [ ] Confirm raw series identity is preserved for plotting, drag/drop, and saved layouts.
- [ ] Confirm source recording files are not modified.
- [ ] Run a Windows x64 startup/load smoke test.
- [ ] Publish the installer and build provenance.

## Current priorities (updated 2026-11-20)

1. **Full main-interface Chinese localization**: menus, dialogs, tooltips, import status and common error messages. Preserve raw tag IDs and application behavior.
2. **Import new label mappings**: add a UI action to select a UTF-8 CSV mapping file with columns `raw_tag,cn_name`, validate rows and duplicates, store a user-writable copy, reload mappings without reinstalling, and refresh the native series list and Chinese search.
3. Regression checks for existing 1,981 labels, Chinese search, system groups, label-mode toggle, CSV streaming and Windows EXE packaging.

**Removed from the roadmap at user request:** curve-rendering performance optimization. Do not implement or prioritize this work.

## Implementation status

The above two new priorities are **planned, not yet implemented**. Previous Windows CI builds succeeded for existing features; a newer tag-search optimization build is being checked separately. Full localization must be staged and verified rather than claimed complete.

