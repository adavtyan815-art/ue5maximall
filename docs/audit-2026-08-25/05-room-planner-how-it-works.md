# Room Planner / Procedural Constructor — How It Works (2026-08-25)

## Files

| File | Role |
|---|---|
| `Constructor/RoomPlannerManager.h/.cpp` (267 + 2326) | Singleton actor: node/segment graph, room detection, floor/ceiling/baseboard meshes, tools, replication, PS command API |
| `Constructor/ProceduralWallActor.h/.cpp` (61 + 429) | One wall segment: procedural mesh with openings, selection highlights |
| `Constructor/RoomPlannerTypes.h` | `FWallNode`, `FWallSegment`, `FWallOpening`, `FRoomData`, tool/view enums |
| `FurnitureConfigurator/UI/RoomPlannerWidget.h/.cpp` (285 + 836) | The planner UI: tools, view modes, properties panel, character relocation |
| `awsTutorial_PlayerController.cpp` | 2D input loop (PlayerTick) + Server RPC forwarding + `ToggleRoomPlannerUI` |

## Data model & replication

The layout is a 2D graph: `Nodes` (id → position, connected segment ids) and `WallSegments`
(id → start/end node, thickness [20 cm], height [280 cm], `Openings[]` with type/width/height/
sill/distance-from-start). `Rooms` are derived, never edited. One `AProceduralWallActor` exists
per segment (`WallActors` map). The manager is spawned on demand by
`GetOrCreateInstance(World)` (first found via actor iterator; server-only spawn).

Replication is **state-snapshot via JSON**: after every server-side mutation the code sets
`ReplicatedRoomJSON = ExportLayoutToJSON()`; clients receive it and `OnRep_ReplicatedRoomJSON`
→ `ImportLayoutFromJSON` (full teardown + rebuild). The server RPC wrappers on the player
controller call `OnRep_…` manually so listen-server hosts rebuild too.

## Graph editing

- `GetOrCreateNodeAtPosition`: snap to an existing node within 25 cm; else if within
  (thickness/2 + 20 cm) of a wall centerline (≥20 cm from its endpoints), **split** that wall
  (`SplitWallSegment` — creates a junction node, divides the opening list by distance, spawns a
  second actor); else create a new node.
- `AddWall`: rejects self-loops and duplicate segments (returns the existing id — this
  idempotency matters, see Findings F-1), links connectivity, spawns the wall actor.
- `RemoveWall`: removes the segment, prunes now-orphaned nodes, clears mesh + collision before
  `Destroy()`, rebuilds, re-exports.
- Openings: `FindNonOverlappingOpeningDist` places a new door/window right of the rightmost
  opening, else left of the leftmost, else in the first sufficient gap, with 5 cm wall-end and
  15 cm neighbor margins; `UpdateOpeningPosition` clamps to the wall, snaps around neighbors
  (allowing side-swapping), keeps the `Openings` array sorted by distance, and re-resolves the
  selected index by `OpeningID` after sorting.
- `SetWallLength` moves the **end node** along the wall direction (connected walls at that node
  follow, since they share the node).

## Wall mesh generation (`RebuildAllWalls` + `AProceduralWallActor::RebuildWallMesh`)

For each segment the manager computes the four corner vertices. At nodes with exactly **two**
segments it builds a mitered corner: bisector direction of the two wall directions, offset
`halfThickness / sin(angle/2)` (clamped to ≤ 2.5 × thickness), choosing which side gets the
apex from the turn direction, and suppresses the end cap for a seamless seam. Nodes with 1 or
3+ connections get plain rectangular ends with caps.

`RebuildWallMesh` walks the wall lengthwise: solid sections (left/right/top quads) between
openings, then per opening the under-sill and above-lintel bands plus sill/lintel/jamb faces.
Openings are pre-sorted and overlap-filtered defensively. It also builds one hidden
**highlight box** procedural mesh per opening (index-matched to `WallData.Openings`, red
translucent material `M_OpeningSelection` loaded from two candidate paths) used for selection
feedback. A default material is applied if none set. Collision is complex-as-simple,
`WorldDynamic`, async-cooked.

## Room detection & floor/ceiling (`RebuildRooms`)

1. Build a node adjacency map; iteratively prune degree-<2 nodes (removes open walls, spurs,
   unclosed shapes) — only the closed "2-core" remains.
2. Build directed half-edges with polar angles, sorted CCW per node.
3. Trace faces with the **left-turn rule** (next edge = the one after the reverse edge in CCW
   order), each directed edge visited once, with a step cap. Faces are validated (simple cycle,
   ≥3 unique nodes, deduped vertices), and kept only when the shoelace signed area is positive
   (interior CCW faces; the outer perimeter is negative) and the bounding box ≥ 25 cm.
4. Per room: area (m²) recorded; **ear-clipping triangulation** (with a force-clip fallback if
   no ear is found); floor slab (1 cm, top+bottom+sides), ceiling at a fixed 280 cm, and a
   10 cm double-sided baseboard strip, each as one mesh section per room with a
   `UMaterialInstanceDynamic` of the engine default material (white / light grey / brown).

Ceiling visibility follows `bCeilingVisible && !b2DViewMode` (forced on when returning to 3D).

## View modes, tools, input

`SetViewMode(true)` saves the control rotation, zeroes it, ignores look input, spawns/reuses a
`TopDownCameraActor` at Z=1500-1600 looking straight down — **orthographic** (OrthoWidth 2500)
in DrawWall mode, perspective FOV 80 otherwise (also switched by `SetToolMode`) — and sets it as
view target; the camera follows the pawn's X/Y each manager `Tick`. `SetViewMode(false)`
restores everything.

The actual editing input lives in `AAwsTutorial_PlayerController::PlayerTick` (see doc 01):
mouse deprojected to Z=0, then per-tool: interactive drag (local preview via a collision-less
`PreviewWallActor`, hover snap hints, endpoint/centerline snapping at 30 cm, 45°-angle snapping
at 8° threshold when not endpoint-snapped) with the **commit sent to the server**
(`Server_CommitWall`); Select (click select wall/opening, drag opening, release commits
position); Erase (click deletes); Delete/Backspace deletes selection.

Selection state (`SelectedSegmentID`/`SelectedOpeningIndex`) is client-local on the manager;
visuals use CustomDepth stencil 2 on the wall mesh (drives `M_PostProcessOutline`) and the
per-opening highlight boxes.

## Web (Pixel Streaming) command API

`ProcessCommandJSON` accepts `{cmd: add_wall {x1,y1,x2,y2} | add_opening {segment_id, type,
dist, width?, height?, sill?} | clear | get_state}` and returns
`{status, cmd, state:<full layout JSON>}`; responses are prefixed `MaxiMallConstructor:` on the
data channel. Commands arrive via the player controller's PS handler **and** via the manager's
own `TObjectIterator`-bound handler (duplication — Findings F-1).

## Planner UI (`URoomPlannerWidget`)

Opened via `ToggleRoomPlannerUI(true)` (controller creates it, passes
`PlannerRelocationLocation`). On construct it caches the character transform and control
rotation and **teleports the character to the planner area** (default (-10000, 0, 0)); on
`ClosePlanner`/`NativeDestruct` it teleports back and restores rotation/input exactly, and the
destruct path also calls `PlannerManager->SetViewMode(false)`. The widget binds ~20 optional
buttons (with duplicate name variants), drives tool/view styling and Russian guidance text
every `NativeTick`, forwards all mutations through the controller's Server RPCs, and shows a
context properties panel (wall length in cm, or opening width/height/sill — a sill > 1 cm is
treated as "window"). `ParseLengthDimensionInput` accepts "250", "2.5m", "250cm"; bare numbers
> 10 are assumed to be centimeters.
