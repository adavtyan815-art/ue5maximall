# Room Planner / Procedural Constructor — Findings (2026-08-25)

Severity legend: see [00-cross-cutting-findings.md](00-cross-cutting-findings.md).

---

## F-1 [LIKELY] Duplicate Pixel Streaming command handling (shared with doc 01, F-3)

`ARoomPlannerManager::Tick` binds `OnPixelStreamingInputReceived` to the **first
`UPixelStreamingInput` found by `TObjectIterator`**
([RoomPlannerManager.cpp:89-101](../../Source/awsTutorial/Constructor/RoomPlannerManager.cpp))
with no `IsTemplate()` or world filter — it may grab the player controller's live component
(→ every constructor command runs **twice**: once via the PC's handler, once via the manager's)
or a class-default archetype (→ the manager's path never fires and is dead). Which one happens
is iteration-order luck. `add_wall` survives duplication thanks to `AddWall`'s dedupe;
**`add_opening` would create two openings**. Recommend deleting the manager's own PS binding
entirely — the player controller already forwards all four commands deterministically.

## F-2 [PERF] Full teardown/rebuild replication

Every mutation re-exports the entire layout to JSON and every client fully re-imports it
(destroy + respawn all wall actors, re-triangulate all rooms). At bathroom scale (≤ dozens of
walls) this is fine and keeps the code simple — worth knowing before anyone builds a large
floor plan with it. `RebuildAllWalls` also rebuilds **all** walls after any single-wall change
(needed for miter neighbors; correct but O(n) per edit).

## F-3 [FRAGILE] `(0,0)` sentinel for "no corner vertex provided"

`RebuildWallMesh` treats `IsNearlyZero()` corner inputs as "not provided, compute rectangular
corner" ([ProceduralWallActor.cpp:152-155](../../Source/awsTutorial/Constructor/ProceduralWallActor.cpp)).
A mitered corner that legitimately lands at world origin (wall pair crossing (0,0)) will be
silently replaced by a rectangular corner. Cosmetic, rare, but a classic sentinel bug —
pass `bStartCap/bEndCap`-style explicit flags (already half-present) or `TOptional`s instead.

## F-4 [INFO] Ceiling height hardcoded to 280 while wall height is per-segment

`FWallSegment.Height` is configurable (and `Server_CommitWall` accepts a height), but
`RebuildRooms` always builds ceilings at `CeilZ = 280.f`
([RoomPlannerManager.cpp:1088](../../Source/awsTutorial/Constructor/RoomPlannerManager.cpp)).
Rooms made of non-default-height walls get a mismatched ceiling. All current entry points pass
the default 280, so it's invisible today.

## F-5 [INFO] Opening positions skew slightly at mitered corners

Solid/opening sections are lerped along the **displaced** corner edge (`SL2D→EL2D`), while
opening distances are measured on the nominal centerline. Near a strongly mitered corner an
opening's rendered position can shift by up to the bisector offset. Cosmetic; nobody puts a
door 20 cm from a corner.

## F-6 [CLEANUP] Dead/vestigial code

- `ComputeMiterOffsetsAtNode` is an **empty function** with a comment saying the logic moved
  (`RoomPlannerManager.cpp:509-515`). Delete it and its header declaration.
- `bWasDraggingOpening` is set every Tick and never read.
- `EOpeningType::Archway` is never used, and `ExportLayoutToJSON` serializes any non-door type
  as `"window"`, so an archway wouldn't round-trip anyway.
- `UpdateActiveWallLength` / `EndWallDrawing` are declared in the header with no
  implementation/callers (checked: no definitions in the cpp) — remove the declarations.
- `SavedControlRotation` on the manager is a `UPROPERTY` but only used internally by
  `SetViewMode` — could be private.
- `FloorActor` / `CeilingActor` / `BaseboardActor` transient members are never assigned
  (superseded by the three components). Delete.
- Duplicate widget-name bindings in `RoomPlannerWidget` (`Btn2DView`/`Btn_2DView`,
  `Btn3DView`/`Btn_3DView`, three ceiling buttons, `EditableTxtOpeningWidth_1` etc.) — pick the
  real UMG names and drop the variants.

## F-7 [INFO] `GetOrCreateInstance` scans actors per call — including every controller tick

`AAwsTutorial_PlayerController::PlayerTick` calls `GetOrCreateInstance` each tick (line 280) to
check `Is2DModeActive`, which runs a `TActorIterator` scan every frame. Cheap with few actors,
but caching the pointer on the controller (invalidated on level change) is a one-line win.

## F-8 [INFO] UI heuristics that can misfire

- **Sill > 1 cm ⇒ "window"** in the properties panel (`RoomPlannerWidget.cpp:542`) — a window
  authored with sill 0 shows the door UI (and loses its sill field).
- **`ParseLengthDimensionInput`**: bare values > 10 are treated as centimeters, so typing
  `11` intending 11 m yields 0.11 m (then clamped by min length). Values ≤ 10 are meters. The
  "m"/"cm" suffix path is fine; the bare-number heuristic is guessy — consider always-cm to
  match the displayed unit.
- Commit-method conditions read `(CommitMethod == OnEnter || CommitMethod != Default)`, which
  is just `!= Default` — includes focus-loss commits; probably intended but the redundant
  clause suggests otherwise. Simplify to the intended set.

## F-9 [INFO] `ClearLayout` leaves selection state

`ProcessCommandJSON("clear")` and `Server_ClearLayout` call `ClearLayout`, which resets the
graph but **not** `SelectedSegmentID`/`SelectedOpeningIndex` and broadcasts no
`OnWallSelected(-1)`. Every consumer guards with `Contains()` so nothing crashes, but the UI's
properties panel can keep showing a stale selection until the next click. Add
`ClearWallSelection()` to `ClearLayout`.

## F-10 [FRAGILE] Shared studio coordinates with Viewmode

`PlannerRelocationLocation` (character) and `ViewModeRelocationLocation` (booth) both default
to (-10000, 0, 0). Today the UI flow prevents both being active at once; nothing else does.
See Viewmode findings F-1 (action item 4).

## F-11 [INFO] Hardcoded asset paths

`M_OpeningSelection` is loaded from two guessed content paths, floor/ceiling materials from the
engine default material, wall fallback from `/Engine/BasicShapes/BasicShapeMaterial`
(`ProceduralWallActor.cpp:288-292, 419`; `RoomPlannerManager.cpp:894`). Works, but these belong
in `EditAnywhere` soft references (they also sync-load mid-gesture the first time — cached by
the loader afterwards).

## F-12 [OK — leave alone]

- The 2-core prune + left-turn face trace in `RebuildRooms` is correct, handles open walls,
  spurs, multiple rooms, and non-convex shapes, and has sane caps. Don't rewrite it.
- The ear-clipping force-fallback (clip anyway when no ear found) is a deliberate
  robustness-over-beauty choice for degenerate polygons; bounded by the iteration cap.
- `AddWall` duplicate-segment dedupe and node snapping — they double as the guard that makes
  F-1 mostly harmless; keep them even after fixing F-1.
- Server-authoritative mutation + local interactive preview split (client draws the preview
  wall, server commits) — the right pattern.
