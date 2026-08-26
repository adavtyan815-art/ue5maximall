# Showroom Booth & Furniture Configurator — How It Works (2026-08-25)

## Files

| File | Role |
|---|---|
| `FurnitureConfigurator/ShowroomBooth.h/.cpp` (547 + 2271) | The replicated booth actor: 10 mesh components, product/state application, doors, custom colors |
| `FurnitureConfigurator/Data/FurnitureTypes.h/.cpp` (734 + 84) | All configurator structs/enums, DataTable row types, editor dropdown helper |
| `FurnitureConfigurator/BoothInteractionInterface.h` | BP-implementable proximity/product events for the character |
| `FurnitureConfigurator/UI/ConfiguratorMainWidget.h/.cpp` (244 + 901) | The right-hand configuration panel (sizes, colors, metadata, Viewmode/ColorCatalog/tour buttons) |
| `FurnitureConfigurator/UI/FurnitureGridItemWidget`, `FurnitureSizePillWidget` | Small reusable tile/pill widgets (used from Blueprints) |

## Data model

A booth displays one **product** (row of `FFurnitureProductRow` in the `ProductCatalog`
DataTable, keyed by `ProductID`). A product defines:

- **CabinetOptions** — mesh per size (`Sizes[]` + `SizeNames[]`) and `Colors[]`
  (`FFurnitureColorOption`: per-slot material overrides + thumbnail + metadata + optional
  `SizeIndices` restricting which cabinet sizes a color applies to).
- **ClosetOptions** — `FFurnitureComponentOptions` (a `Models[]` list, each with its own colors).
- **DoorsConfig** — cabinet and closet door groups; each group is `NoDoors/OneDoor/TwoDoors`
  with per-slot configs (hinge yaw vs. drawer translation, offsets) and per-size/per-color data.
- **AllowedCountertopIDs / AllowedSinkIDs / AllowedFaucetIDs / AllowedMirrorIDs** — row names
  into four **shared catalogs** (`DT_SharedCountertops`, `DT_SharedSinks`,
  `AllowedFaucetIDs`(sic — on-disk name), `AllowedMirrorIDs`), which the booth constructor
  resolves via `ConstructorHelpers` with legacy-name fallbacks.
- `bAllow*ColorCatalog` flags at three levels (product row, options struct, per shared row) —
  `IsColorCatalogAllowedForComponent` ORs the relevant ones together.

**Replicated state** (`GetLifetimeReplicatedProps`): `ActiveState`
(`FShowroomBoothConfigState` — product ID + 12 size/color indices), `CustomColors`
(`FCustomColorOverride[]` — RAL/NCS component tints + optional override material),
`DoorStates` (4 entries: cabinet slots 0-1, closet slots 2-3), and
`bCountertopSizeFallbackActive`.

For shared components (countertop/sink/faucet/mirror) the "size index" is actually the **model
index** into the allowed-ID list; each allowed ID becomes one `Models[]` entry in
`GetResolvedComponentOptions`.

## Component tree

`BoothRoot` (scene) → `MainCabinet` → {`DoorMeshSlot0/1`, `CountertopMesh`, `SinkMesh`,
`FaucetMesh`}; `MirrorMesh`, `ClosetMesh` (→ `ClosetDoorMeshSlot0/1`) attach to the root.
`PostInitializeComponents` force-reattaches countertop/sink/faucet to `MainCabinet`
(overriding any stale Blueprint parenting) and captures **baseline transforms** — the
designer-authored relative transforms — once per game session
(`EnsureBaselineTransformsCaptured`, game worlds only). All later transform math applies
product deltas *on top of* these baselines.

## Runtime flow

**BeginPlay** (authority): `InitializeDefaultStateForProduct` (all indices → 0) +
`ApplyProductData`. Clients receive `ActiveState` via initial replication →
`OnRep_ActiveState` → same `ApplyProductData`. All ten components get `OnClicked` handlers
(`OnBoothComponentClicked`), which on right-click (requires RMB in `ClickEventKeys`, see
Findings 01/F-8) map the component to an `EFurnitureComponentType` and open the configurator UI.

**`ApplyProductData`** is the deterministic master rebuild (runs on server and on every client
via RepNotify): recomputes the countertop fallback flag, applies cabinet + closet meshes and
materials, door configuration (meshes, visibility, collision, and — on authority — resets
`DoorStates` to Closed/NotPresent per product), resolves and applies countertop / sink / faucet
/ mirror from the shared catalogs, recalculates dependent transforms, and finally re-applies
`CustomColors` so RAL/NCS tints survive any mesh change.

Key sub-behaviors:

- **BuiltIn vs SurfaceMounted countertops** (`ECountertopType`): BuiltIn hides the standalone
  sink entirely and aligns countertop/faucet using only the baseline Z (X/Y and rotation
  reset); SurfaceMounted composes `product delta × baseline`. Faucets are filtered by
  compatibility (`Integrated` faucets only with BuiltIn countertops, `Standard` only with
  SurfaceMounted).
- **Countertop size fallback**: if the active BuiltIn countertop has no mesh for the current
  cabinet size, `GetResolvedComponentOptions` searches the allowed IDs, then the whole shared
  catalog, for a SurfaceMounted model with that size; `bCountertopSizeFallbackActive`
  (replicated) drives a warning label in the UI ("no built-in countertop for this size").
- **Doors**: fixed 4-slot state machine, no dynamic components. `Server_ToggleDoor` flips
  Closed↔Open, applies visuals with animation on the server; clients animate via
  `OnRep_DoorStates`. Animation is a 0.016 s timer (`UpdateDoorAnimation`) interpolating
  location/rotation to baseline (closed) or baseline+delta (open) with `FMath::V/RInterpTo`
  (speed 5), self-stopping at target.
- **Custom colors** (`OnRep_CustomColors`): for each override, targets the component group
  (Cabinet and Doors are unified — coloring either applies to `MainCabinet` + both door
  slots), and on **every material slot** either creates a MID from the override material or
  wraps the existing material in a MID, then sets both `BaseColor` and `Color` vector params.
  Selecting a standard preset color in the UI removes the custom override for that group
  (`Server_ApplyComponentSelection`).
- **Selection changes** (`Server_ApplyComponentSelection`): updates the relevant index pair;
  a cabinet-size or countertop-model change resets `FaucetSizeIndex` to 0 (compatibility);
  explicit color choice clears the matching `CustomColors` entry; then full `ApplyProductData`.

All request entry points (`RequestProductChange`, `RequestComponentSelection`,
`RequestCustomColorChange`, `RequestDoorToggle`, `LoadBoothFullState` via the controller RPC)
run directly on authority or forward through Server RPCs from clients.

## Configurator UI (`UConfiguratorMainWidget`)

Created/owned by the player controller (`ToggleConfiguratorUI`). `SetupWidget(PC, Booth,
Component)` + `RefreshSelections()` rebuilds the panel from live booth state:

- Size selectors: text pills for cabinet sizes; thumbnail grid (ScrollBox → UniformGrid of
  SizeBox→Button→ScaleBox→Image, all constructed in C++) for shared-component models. Scroll
  offset is preserved across rebuilds. Extensive styling knobs are `EditAnywhere` properties.
- Color selectors: same grid pattern from the active model's colors (cabinet colors filtered by
  `SizeIndices` against the active cabinet size).
- Click routing uses per-button `UFurnitureOptionListener` objects →
  `HandleOptionSelected` → `RequestBoothComponentSelection` (size change resets color to 0).
- Metadata (`Txt_ProductName_1`, `Txt_SKU`) comes from
  `AAwsTutorial_PlayerController::GetActiveComponentMetadata`, which prefers per-color metadata
  and falls back to `CombinationsMetadata` (exact size+color match, then entry 0).
- Buttons: Viewmode (`OpenFurniturePreview`), Close, Color Catalog (visibility gated by
  `IsColorCatalogAllowedForComponent`; opens `UColorCatalogWidget` and binds
  `HandleColorSelected` → `RequestBoothCustomColorChange`), Cinematic Tour toggle (two
  alternate widget names bound to the same handler), URL (currently logs/debug-prints the
  product URL — the browser-opening path `SendOpenURLToBrowser` is not called from here).
- `RefreshSelections` is re-run on every product change via the controller's
  `OnTargetBoothProductChanged` subscription.
