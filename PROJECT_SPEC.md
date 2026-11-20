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
3. **Automatic Chinese CSV encoding detection (new requirement)**: support UTF-8 (with or without BOM) and GBK/GB18030 label-mapping files, detect encoding before parsing, preserve original raw tag IDs, warn on invalid/unrecognized input, and normalize saved mappings to UTF-8. Verify using Chinese sample labels and both encoding variants.\n4. Regression checks for existing 1,981 labels, Chinese search, system groups, label-mode toggle, CSV streaming and Windows EXE packaging.

**Removed from the roadmap at user request:** curve-rendering performance optimization. Do not implement or prioritize this work.

## Implementation status

The above two new priorities are **planned, not yet implemented**. Previous Windows CI builds succeeded for existing features; a newer tag-search optimization build is being checked separately. Full localization must be staged and verified rather than claimed complete.



## Next localization increment (2026-11-20)

- Continue Chinese localization of the main window: toolbar tooltips, recent-file/help text, zoom/playback and common action labels.
- Keep the previously user-verified GBK/GB18030 and UTF-8 mapping import behavior, full-replacement semantics, and user-level persistence unchanged.
- Do not implement curve-rendering performance optimization; it was explicitly removed from scope by the user.
- After CI succeeds, ask the user to verify translated tooltips/menu labels and regression-test mapping import.


## BladeScope branding and UI redesign (user-approved reference images, 2026-11-20)

1. Rename displayed product/window to **BladeScope 风机录波数据分析工具**; progressively rebrand installer, shortcuts, application icon and about dialog. Preserve Qt organization/application settings identity or migrate user AppData mapping safely before any identity change.
2. Replace app icon with the user's supplied visual reference: rounded-square dark blue to teal gradient, multicolor oscillation traces, white wind turbine; generate Windows .ico with multi-resolution layers and embed in executable and installer.
3. Restyle desktop UI inspired by the supplied second screenshot: light Chinese menu and icon toolbar; left searchable signal list with grouping; central multiple synchronized charts; right dockable curve properties (line width, color, markers, interpolation). Preserve PlotJuggler plotting/CSV/plugin functionality; implement in safe increments rather than claim a full redesign is done.
4. Regression: imported 1,981-tag mappings, UTF-8 BOM/plain, GBK/GB18030 detection, full replacement, 100-500 MB recorder CSV import, search, plotting and Windows packaging.
5. No curve rendering performance optimization (explicitly excluded).
