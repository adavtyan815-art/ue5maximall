# MaxiMall C++ Source Audit — 2026-08-25

Full end-to-end audit of `Source/awsTutorial/` (45 files, ~16,000 lines). Every source file was
read in full; claims below are backed by file/line references against the code as of this date.
Existing docs (`archive/AWSTUTORIAL_SOURCE_GUIDE.md`, `archive/VIEWMODE_SYSTEM.md`,
`archive/ROOM_PLANNER_AND_COLOR_CATALOG_UPDATES.md`) were used as context only and are
**superseded by this set** — they contain statements that no longer match the code (details in
the findings docs). `AI_ACCESS_AND_PERMISSIONS.md` and `UE5_GIT_WORKFLOW.md` are unrelated and
untouched.

## Documents

Each subsystem has two documents: **how it works** (current architecture and runtime flow) and
**findings** (bugs, risks, dead code, debt).

| # | Subsystem | How it works | Findings |
|---|-----------|--------------|----------|
| 1 | Player Controller & Pixel Streaming glue | [01-player-controller-how-it-works.md](01-player-controller-how-it-works.md) | [01-player-controller-findings.md](01-player-controller-findings.md) |
| 2 | Showroom Booth & Furniture Configurator | [02-showroom-booth-how-it-works.md](02-showroom-booth-how-it-works.md) | [02-showroom-booth-findings.md](02-showroom-booth-findings.md) |
| 3 | Viewmode / Furniture Preview | [03-viewmode-preview-how-it-works.md](03-viewmode-preview-how-it-works.md) | [03-viewmode-preview-findings.md](03-viewmode-preview-findings.md) |
| 4 | Save/Load & HTTP backend | [04-save-system-how-it-works.md](04-save-system-how-it-works.md) | [04-save-system-findings.md](04-save-system-findings.md) |
| 5 | Room Planner / Procedural Constructor | [05-room-planner-how-it-works.md](05-room-planner-how-it-works.md) | [05-room-planner-findings.md](05-room-planner-findings.md) |
| 6 | Color Catalog (RAL/NCS) | [06-color-catalog-how-it-works.md](06-color-catalog-how-it-works.md) | [06-color-catalog-findings.md](06-color-catalog-findings.md) |

Cross-cutting issues that don't belong to one subsystem: [00-cross-cutting-findings.md](00-cross-cutting-findings.md).

## Executive summary

The codebase is functional and, per extensive manual testing, currently behaves correctly. The
audit found **no evidence of a defect in the primary happy paths**. What it did find falls into
four buckets:

### Confirmed problems (code demonstrably contradicts itself or does nothing)

1. **Stale "FIX 2" state** — `awsTutorial_PlayerController.cpp` constructor still creates the
   `UPixelStreamingInput` default subobject that comments (and the `BeginPlay`/`PlayerTick`
   retry logic) claim was removed. Two retry code paths are dead, and the comments describe a
   codebase that no longer exists. (Findings 01, F-1)
2. **Duplicate Pixel Streaming command pipeline** — constructor commands (`add_wall`,
   `add_opening`, `clear`, `get_state`) are handled by *both* the player controller and
   `ARoomPlannerManager`'s own binding. Depending on which component the planner's
   unfiltered `TObjectIterator` scan latches onto, a single browser `add_opening` command can be
   executed twice. (Findings 01, F-3 / Findings 05, F-1)
3. **`ReplaceInline(TEXT("\0"), …)` is a no-op** in all 5 occurrences — the "null terminator
   stripping" never strips anything. (Findings 01, F-4)
4. **Dead data fields** — `MirrorMaterialOverride` / `MirrorMaterialSlotIndex` are defined,
   documented, and copied around, but no code ever reads them. (Findings 02, F-4)
5. **Corrupted text** — mojibake in `EColorShadeCategory` Russian display names and in comment
   art across ~10 files (double-encoded UTF-8). (Findings 00 / 06)
6. **Viewmode documentation contradiction** — the booth is now relocated to a studio location
   at (-10000, 0, 0) *before* the preview spawns, which invalidates the central premise of the
   archived `VIEWMODE_SYSTEM.md` ("preview at the real booth position, lit/reflected by the
   real room"). The lighting-calibration and clearance systems now measure the studio location.
   The code works today only because of level content / Blueprint configuration that C++ cannot
   see. (Findings 03, F-1 — the single most fragile point in the project.)

### Likely issues that need a targeted test

- Login screen paste may inject characters twice (double delegate binding pattern that "FIX 2"
  removed from the main controller still exists in `awsTutorial_LoginPlayerController`).
- Save-thumbnail race when the Save button is clicked twice within one screenshot round-trip.
- Save/load matches booths **by actor name** — renaming a booth actor or PIE name suffixes
  silently orphan saved state.
- `GetOptions` editor meta references module path `MaxiMall.FurnitureEditorHelper…` but the
  module is named `awsTutorial` — the editor dropdowns for allowed IDs may silently not work.
- Right-click on booth components requires `RightMouseButton` in the PlayerController's
  `ClickEventKeys` (configured only in Blueprint — undocumented hidden dependency).

### Safe cleanup candidates

Dead retry paths, obsolete screenshot-timeout members, unused color-catalog target members,
duplicate widget-name bindings, "Mock" naming, no-op string replaces, duplicated
`ApplyComponentMeshAndMaterials` variants. Each is listed with exact locations in the findings
docs. None of them change behavior when removed.

### Leave alone (correct and tested — do not "fix")

- The Viewmode rotation/zoom model (mesh rotates, camera fixed; `bDoCollisionTest=false` is
  **deliberate**), the level-match light calibration honoring ~0 lux measurements, and the
  targeted post-process-blendable suspension. These encode hard-won lessons documented in the
  archived viewmode doc; several look wrong at first glance but are intentional.
- The `AddWall` node-snap + dedupe logic (it is what accidentally makes the duplicate command
  pipeline mostly harmless for `add_wall`).
- The door slot state machine (4 fixed slots, no dynamic components).
- The room face-tracing algorithm (2-core prune + left-turn face walk) — correct and robust.

## Statistics

| Metric | Value |
|---|---|
| C++ files audited | 45 (~16.2k lines) |
| Confirmed problems | 14 |
| Likely issues needing testing | 9 |
| Safe cleanup candidates | 18 |
| Performance notes | 7 |
