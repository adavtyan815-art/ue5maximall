# Save/Load & HTTP Backend — How It Works (2026-08-25)

## Files

| File | Role |
|---|---|
| `FurnitureConfigurator/UI/SaveSystemWidget.h/.cpp` (140 + 754) | The save/load dialog: REST calls, JSON (de)serialization, screenshot thumbnails, history grid |
| `FurnitureConfigurator/UI/SaveHistoryItemWidget.h/.cpp` | One history card (name, date, thumbnail, Load/Delete buttons → delegates with `SaveId`) |
| `awsTutorial_PlayerController.cpp` (`Server_LoadBoothState`) | Server RPC applying a loaded state to a booth |
| `FurnitureConfigurator/ShowroomBooth.cpp` (`LoadBoothFullState`) | Authoritative full-state application |

## Backend contract

Base URL resolution (`GetBackendBaseURL`), in priority order:
1. `-BackendURL=<url>` launch argument (trailing slash stripped);
2. host extracted from `-PixelStreamingURL=ws(s)://host[:port]` → `http://<host>:3000`;
3. hardcoded fallback `https://18-185-5-251.nip.io`.

Endpoints (JSON, no authentication headers; username in the path):
- `GET  /api/saves/:username` → array of save records;
- `POST /api/saves` → upsert by `saveId`;
- `DELETE /api/saves/:username/:saveId`.

All requests use a **5 s timeout** and refresh the history grid on completion.

Save record shape: `{ username, saveId (GUID), saveName, date ("dd.MM.yyyy"), thumbnail
(base64 PNG), boothStates: [ { boothName, state {productID + 12 indices}, customColors
[{componentType, color{r,g,b,a}, overrideMaterial (asset path)}], doorStates [int×4] } ] }`.

The username comes from `GetActiveUserName()`: a **reflection lookup of an `FString` property
named `UserName` on the GameInstance** (set by the Blueprint Cognito login flow), falling back
to `"guest_tester"`.

## Save flow (`OnSaveClicked`)

1. Requires a non-empty name; generates a fresh GUID as `PendingSaveId`.
2. **Immediately** POSTs the full state with an empty thumbnail (`ExecuteSaveGame`), so the
   save exists even if the screenshot never arrives.
3. Requests a viewport screenshot (`FScreenshotRequest::RequestScreenshot(false)` — no UI) and
   binds `OnScreenshotCaptured`.
4. `OnScreenshotCapturedHandler`: unbinds, center-crops the frame to a square, nearest-neighbor
   resizes to 256×256 (alpha forced opaque), PNG-compresses (`FImageUtils::CompressImageArray`),
   base64-encodes, and POSTs the **same `saveId` again** with the thumbnail — the backend
   upserts, updating the record.

`ExecuteSaveGame` gathers every `AShowroomBooth` in the world via `GetAllActorsOfClass` and
serializes `ActiveState`, `CustomColors` (linear RGBA + material asset path), and all four
`DoorStates` per booth, keyed by **actor name** (`Booth->GetName()`).

## Load flow

`RefreshSaveHistory` (on construct and after every mutation) GETs the user's saves, parses them
into `LoadedSaves`, updates the "Last Save" panel from the **last array element**, decodes each
thumbnail (`FBase64::Decode` → `FImageUtils::ImportBufferAsTexture2D`, sRGB), and builds the
history grid (SizeBox-wrapped `USaveHistoryItemWidget`s in a UniformGrid inside the ScrollBox;
per-item Load/Delete delegates).

`HandleLoadSaveItem(SaveId)` finds the record, then for each serialized booth: matches a level
booth **by actor name**, reconstructs `FShowroomBoothConfigState`, `FCustomColorOverride[]`
(override materials resolved via `StaticLoadObject` by path), and `EDoorSlotState[]`, and calls
`AwsPC->Server_LoadBoothState(...)`. On the server, `LoadBoothFullState` overwrites the three
replicated state blocks and rebuilds visuals; clients converge through the normal RepNotify
path. Booths present in the level but absent from the save keep their current state (the
`saves` endpoint's commit `7aba0f8` "preserve custom boothStates level layout" relates to the
backend keeping unknown booths).

`HandleDeleteSaveItem` fires the DELETE immediately (no confirmation dialog) and refreshes.

## UI state

`UpdateUIVisibility(SaveCount)` toggles three optional containers: LastSave + History when
saves exist, `FirstTimeWelcomeMessage` otherwise. All containers start collapsed to avoid
flicker before the first GET completes.
