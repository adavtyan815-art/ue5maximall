# Player Controller & Pixel Streaming Glue — How It Works (2026-08-25)

## Files

| File | Role |
|---|---|
| `Source/awsTutorial/awsTutorial_PlayerController.h/.cpp` (321 + 2037 lines) | Main gameplay controller: input, hover/click routing, Viewmode lifecycle, cinematic tour, Room Planner glue, Pixel Streaming data channel |
| `Source/awsTutorial/awsTutorial_LoginPlayerController.h/.cpp` | Minimal controller for the login map: clipboard paste injection only |
| `Source/awsTutorial/awsTutorial.Build.cs` | Module deps: EnhancedInput, UMG, HTTP/Json, ProceduralMeshComponent, PixelStreaming(+Input), ImageWrapper |
| `Source/awsTutorial.Target.cs` etc. | Standard Game/Editor/Client/Server targets |

`Config/DefaultEngine.ini` maps `/Game/MaxiMall` as the default map with
`BP_FirstPersonGameMode`. The controllers are subclassed/configured in Blueprints (widget
classes, relocation coordinates, preview actor class are all `EditAnywhere` properties).

## Lifecycle (`AAwsTutorial_PlayerController`)

**Constructor** creates a `UPixelStreamingInput` default subobject and sets
`PreviewActorClass = AFurniturePreviewActor::StaticClass()` as a fallback.

**BeginPlay** (local controller only):
1. `ARoomPlannerManager::GetOrCreateInstance(World)` — ensures the planner singleton exists.
2. Binds `OnPixelStreamingInput` to the PS input component (`AddUniqueDynamic`). Because the
   constructor always creates the component, the `GetComponentByClass` fallback and the
   `PlayerTick` `TObjectIterator` retry never actually run (see Findings F-1).
3. Forces overall scalability to Epic (3) — prevents low-quality fallbacks on cloud GPUs.
4. Sets `FInputModeGameAndUI`, shows the cursor, enables click/mouse-over events.

**PlayerTick** does, in order:
1. PS input late-bind retry (dead in practice — F-1).
2. **Clipboard monitor**: every 0.2 s reads the OS clipboard; when it changes, sends
   `MaxiMallClipboard <content>` over the PS data channel so the browser can mirror
   copy operations from the streamed app.
3. Clears `bRightMouseIsDragging` when RMB is up (the flag is set by `AddYawInput` /
   `AddPitchInput` overrides while RMB is held — this is how "click" vs "camera drag" is
   disambiguated everywhere).
4. **2D Room Planner branch**: when `ARoomPlannerManager::Is2DModeActive()`, deprojects the
   mouse to the Z=0 plane and drives the active tool: interactive wall drag
   (start/update/commit → `Server_CommitWall`), select/drag openings
   (`Server_UpdateOpeningPosition` on release), erase (`Server_DeleteWall`), and
   Delete/Backspace deletion. The function **returns early** here, so booth hover logic is
   suspended in planner mode.
5. **Booth hover**: cursor raycast (`ECC_Visibility`); if the hit component is one of the ten
   interactable `AShowroomBooth` mesh components, the cursor becomes a hand and a
   `MaxiMallCursor pointer|default` message is broadcast to the browser via
   `BroadcastCursorState` (sent only on state *change*, via `IPixelStreamingModule::ForEachStreamer`).

## Click handling

Raw key bindings (not Enhanced Input) for LMB/RMB press/release:

- **LMB release** within 0.25 s and ≤8 px of the press point counts as a click
  (`OnLeftMouseButtonClicked`). Two clicks within `DoubleClickThreshold` (0.5 s) →
  `HandleDoubleClickInteraction`, which re-traces and, for Doors/Closet components, resolves
  the slot index (0/1 cabinet, 2/3 closet) and calls `RequestBoothDoorToggle`.
  A single click on a non-Showroom actor clears the selection (`SelectComponent(nullptr)`).
- **RMB release** within 0.35 s / 10 px *while a preview is active* toggles the cinematic tour.
- Right-clicking a booth component to open the configurator UI is **not** handled here — it
  goes through `UPrimitiveComponent::OnClicked` on the booth itself
  (see `AShowroomBooth::OnBoothComponentClicked`), which requires RMB to be present in the
  controller's `ClickEventKeys` (Blueprint-configured — see Findings F-8).
- `IsWidgetHoveredGeometrically` (static helper) suppresses world interaction while the cursor
  is over the configurator UI. For `UConfiguratorMainWidget` it walks the whole widget tree and
  tests every visible child < 900 px wide against the cursor position.

## Viewmode lifecycle (`OpenFurniturePreview` / `CloseFurniturePreview`)

Open, in order:
1. Guard clauses (local controller, valid booth, possessed pawn). `Doors` focus is coerced to
   `Cabinet`. Clears BIM selection; removes the main configurator widget from the viewport.
2. Saves `SavedControlRotation` (only if no preview already active), then calls
   `CloseFurniturePreview()` to tear down any previous preview.
3. **Relocates the booth**: caches its transform, teleports it to
   `ViewModeRelocationLocation` (default **(-10000, 0, 0)**, rotation (0, 90, 0)). Everything
   after this — preview spawn, lighting calibration, clearance search — happens at the studio
   location, not in the showroom (see Findings 03, F-1).
4. Snapshots the product row; spawns `AFurniturePreviewActor` (class from `PreviewActorClass`)
   at the relocated booth's position/yaw; calls `LoadProductPreview` + `SetFocusComponent`.
5. Subscribes to the booth's `OnProductChanged` (so live product changes re-load the preview),
   sets the preview actor as view target, shows `ViewmodeOverlayWidget` (Back button), and
   **auto-starts the cinematic tour**.

Close restores the booth transform, view target, control rotation, and either re-opens the
configurator main widget (when it existed) or restores free input next tick via a
`SetTimerForNextTick` lambda. The level's PostProcessVolumes are deliberately never touched.

## Cinematic tour

A 0.016 s looping timer (`UpdateCinematicTourStep`) rotates the preview
(`CinematicTourOrbitSpeed` deg/s yaw + a slow sine pitch wobble). Auto-started on preview open;
toggled by RMB-click in Viewmode or the UI button; always stopped on close. The timer only
exists while the tour runs (zero idle cost).

## Pixel Streaming message protocol (`OnPixelStreamingInput`)

Incoming descriptors are processed in this order:
1. **Legacy plain text**: `MaxiMallPaste <text>` → copies to OS clipboard, injects each
   character as a Slate `FCharacterEvent` (works on headless Linux where the OS clipboard
   doesn't reach Slate).
2. Otherwise strips an optional `UIInteraction:`/`UIInteraction` prefix, parses JSON, and
   unwraps a nested `descriptor`/`Descriptor` field if present.
3. `{"Cmd":"ClipboardPaste","Text":…}` → same clipboard + Slate injection path.
4. `{"cmd":"add_wall|add_opening|clear|get_state", …}` → forwarded to
   `ARoomPlannerManager::ProcessCommandJSON`; the response is sent back as
   `MaxiMallConstructor:<json>`. (The planner *also* binds its own handler for the same
   commands — see Findings F-3.)

Outbound messages: `MaxiMallCursor …`, `MaxiMallClipboard …`, `MaxiMallConstructor:…`,
`open_url: <URL>` (`SendOpenURLToBrowser`), and `DIAG: …` diagnostics.

## Networking

The controller exposes thin Server RPCs that forward to `ARoomPlannerManager` (wall/opening
CRUD, each followed by `ReplicatedRoomJSON = ExportLayoutToJSON()` + a manual
`OnRep_ReplicatedRoomJSON()` for the listen-server/host path) and to `AShowroomBooth`
(product/selection/color/door/full-state). `GetRequestURL`/`GetRequestOption*` parse the
connection URL for login options; `Kick()` closes the net connection.

## Login controller

`AAwsTutorial_LoginPlayerController` creates its own `UPixelStreamingInput` subobject, binds
paste handling in BeginPlay, and *additionally* scans each tick for any other
`UPixelStreamingInput` component on itself and binds that too (see Findings F-2). Its only job
is `ClipboardPaste` JSON → Slate character injection for the Cognito login form.
