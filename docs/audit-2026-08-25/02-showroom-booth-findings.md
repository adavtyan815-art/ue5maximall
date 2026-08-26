# Showroom Booth & Furniture Configurator — Findings (2026-08-25)

Severity legend: see [00-cross-cutting-findings.md](00-cross-cutting-findings.md).

---

## F-1 [PERF] `GetResolvedComponentOptions` is rebuilt from DataTables on every query, with nested re-resolution

[ShowroomBooth.cpp:577-864](../../Source/awsTutorial/FurnitureConfigurator/ShowroomBooth.cpp)
re-reads the shared catalogs and rebuilds the full `Models[]`/metadata arrays on **every call**,
with Warning-level logs per countertop entry. Call fan-out for a single
`Server_ApplyComponentSelection`:

- `ApplyProductData` resolves countertop, sink, faucet, mirror (4 full resolutions);
- `GetActiveCountertopType()` → resolves countertop *again*, and is called from
  `ApplyProductData`, `RecalculateDependentTransforms`, and — worst — **once per allowed faucet
  ID inside the faucet resolution loop** (line 802), each time performing the entire countertop
  resolution including the whole-catalog fallback scan;
- `RecalculateDependentTransforms` calls the four `GetActive*Offset()` helpers, each of which
  resolves its component again.

Net effect: one selection change ⇒ ~10–15 catalog resolutions plus log spam. At current data
sizes this is milliseconds, so it *works*, but it scales quadratically with catalog size and
floods the log. **Action (safe, mechanical)**: pass the already-resolved
`FFurnitureComponentOptions` down instead of re-resolving; compute `GetActiveCountertopType`
once per rebuild; drop the per-call Warning logs to Verbose.

## F-2 [PERF] Synchronous loads during product changes

All meshes/materials/thumbnails load via `LoadSynchronous`/`StaticLoadObject` on the game
thread (`ApplyComponentMeshAndMaterials`, `GetResolvedComponentOptions` fallbacks,
`RefreshSelections` thumbnails). First-time product switches hitch by design. Acceptable for
the current showroom scale; an async preload of the active product's soft references would
remove the hitch if it ever becomes visible on cloud GPUs.

## F-3 [CLEANUP] Stale header documentation

`ShowroomBooth.h` says `DoorStates` has "max 2 entries" (line ~208) and `RequestDoorToggle`
clamps to "[0,1]" (line ~259); the code uses **4** slots and clamps to [0,3]
(`ShowroomBooth.cpp:127, 554`). The class comment also says visuals are driven by "two
replicated variables" (there are four). Update the comments — they predate the closet doors.

## F-4 [CLEANUP] Dead data: `MirrorMaterialOverride` / `MirrorMaterialSlotIndex`

Defined on `FFurnitureModelOption` and `FFurnitureMirrorRow`
([FurnitureTypes.h:257-261, 359-363](../../Source/awsTutorial/FurnitureConfigurator/Data/FurnitureTypes.h)),
copied in `GetResolvedComponentOptions` (`ShowroomBooth.cpp:843-844`) — and **never read
anywhere** in the module (verified by search). The documented feature ("material override for
the mirror glass inside viewmode") was either never finished or removed. Delete the fields (and
any DataTable values) or implement the override in `AFurniturePreviewActor`.

## F-5 [LIKELY] `GetOptions` editor helper uses a wrong module path

`FFurnitureProductRow`'s allowed-ID arrays use
`meta = (GetOptions = "MaxiMall.FurnitureEditorHelper.GetCountertopOptions")`
([FurnitureTypes.h:669-688](../../Source/awsTutorial/FurnitureConfigurator/Data/FurnitureTypes.h)),
but `UFurnitureEditorHelper` lives in module **awsTutorial** — the correct form is
`"/Script/awsTutorial.FurnitureEditorHelper.GetCountertopOptions"` (or just the bare function
name if the helper were on the row's own class). If the dropdowns in the DataTable editor
currently show no suggestions, this is why. Editor-only; verify in the editor and fix the path.

## F-6 [FRAGILE] Countertop "size index" vs. cabinet size index coupling

Shared-catalog color filtering (`FilterColors` in `GetResolvedComponentOptions`, line 648) and
the countertop mesh choice both key off **`ActiveState.ActiveSizeIndex` (the cabinet size)** —
by design, countertop meshes are per-cabinet-size (`FFurnitureCountertopRow.Sizes`), while
`CountertopSizeIndex` selects the countertop *model*. This double meaning of "size index" is
correct but highly confusing; `IsColorCatalogAllowedForComponent` indexes
`AllowedCountertopIDs[CountertopSizeIndex]` (a model index into an ID list — correct, but reads
like an off-by-concept). Rename (`ModelIndex` vs `CabinetSizeIndex`) or add comments at the use
sites; every future maintainer will trip here.

## F-7 [INFO] Custom colors overwrite *all* material slots

`OnRep_CustomColors` applies the override material / tint to every slot of every targeted mesh
(`ShowroomBooth.cpp:1446-1483`). For a mesh with mixed slots (e.g. wood body + glass front in
one static mesh), the glass gets tinted/replaced too. All current products apparently use
per-component meshes where this is the intended "paint the whole component" behavior — if a
multi-material product is ever added, this needs a slot mask.

## F-8 [CLEANUP] Redundant work in state application

- `LoadBoothFullState` calls `RebuildBoothVisuals` (which already ends in
  `OnRep_CustomColors`) and then calls `OnRep_CustomColors` again, plus `OnRep_DoorStates`
  (`ApplyProductData` inside the rebuild also snapped door visuals already). Idempotent but
  triple work.
- `Server_ApplyComponentSelection` ends with `ApplyProductData` (which re-applies custom
  colors) and then `OnRep_CustomColors()` again.
- Cabinet vs Doors cases in `OnRep_CustomColors` have identical bodies (lines 1424-1433).
- The BuiltIn-countertop fallback search duplicates the same loop twice (allowed IDs, then all
  rows; lines 703-738) — extract a lambda.

## F-9 [INFO] Booth vs. preview color filtering divergence

The booth's cabinet-material application filters `Colors` by `SizeIndices` before indexing
(`ShowroomBooth.cpp:1733-1750`), but `AFurniturePreviewActor::ApplyComponentMeshAndMaterials`
(cabinet variant) indexes `Options.Colors` **unfiltered**
([FurniturePreviewActor.cpp:1440-1442](../../Source/awsTutorial/FurnitureConfigurator/Preview/FurniturePreviewActor.cpp)).
With products that use `SizeIndices` restrictions and a color index > 0, booth and preview can
resolve **different colors** for the same state. Masked today because
`LoadProductPreview` afterwards copies the booth's live materials over the preview meshes
(lines 699-728), which effectively wins. Consolidating the helpers (Cross-cutting X-4) removes
the trap.

## F-10 [INFO] Per-frame lookups during door animation

`AnimateDoorSlot` calls `FindProductRow` (a DataTable `FindRow`) for every animating slot every
0.016 s tick. Trivial cost, but caching the row for the animation duration is a one-liner.

## F-11 [INFO] `FindProductRow` warns on missing rows in validation paths

`FindProductRow(..., bWarnIfNotFound=true)` is used by `IsValidProductID`, which is the
`Server_ApplyProductChange_Validate` — malformed client input produces engine warnings rather
than being silently rejected. Cosmetic.

## F-12 [INFO] URL button does not open the URL

`UConfiguratorMainWidget::OnURLButtonClicked` only logs and shows an on-screen debug message
with the product URL (`ConfiguratorMainWidget.cpp:772-791`); it never calls
`SendOpenURLToBrowser`. If "open product page in browser" is expected from that button, the
Blueprint must be doing it — otherwise this is an unfinished feature. Verify which is true and
either wire it (`OwningPC->SendOpenURLToBrowser(URL)`) or delete the button binding.

## F-13 [CLEANUP] Duplicate cinematic-tour button bindings

`Btn_CinematicTour` **and** `BtnCinematicTour` (both `BindWidgetOptional`) exist to tolerate
two widget naming conventions (`ConfiguratorMainWidget.h:73-77`); same pattern as the Room
Planner's duplicated buttons. Once the UMG names are settled, delete the unused variant.

## F-14 [INFO] Booth actor relocation is client-side against a replicated actor

`OpenFurniturePreview` teleports the (replicated) booth locally. Safe today because the booth
does not replicate movement, so the server never corrects it — but if `bReplicateMovement` is
ever enabled on booths, previews will fight the server and snap back mid-view. Leave as is;
noted so the constraint is known.
