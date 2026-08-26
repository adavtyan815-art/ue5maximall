# Color Catalog (RAL/NCS) — Findings (2026-08-25)

Severity legend: see [00-cross-cutting-findings.md](00-cross-cutting-findings.md).

---

## F-1 [BUG (cosmetic)] Corrupted Russian display names in `EColorShadeCategory`

[ColorCatalogTypes.h:15-27](../../Source/awsTutorial/ColorCatalog/ColorCatalogTypes.h) —
all 13 `UMETA(DisplayName = …)` values are mojibake (`Р’СЃРµ` = double-encoded «Все»). Invisible
in-game because the category buttons are separate UMG widgets, but visible in the editor
(details panels, any `GetDisplayValueAsText` use). Re-save the file as proper UTF-8 with the
intended Cyrillic strings. (Same corruption class as Cross-cutting X-1.)

## F-2 [CLEANUP] Dead target members

`UColorCatalogWidget` declares `TargetActor`, `TargetComponent`, `TargetComponents`
([ColorCatalogWidget.h:174-181](../../Source/awsTutorial/ColorCatalog/ColorCatalogWidget.h)) —
never written or read; the class comment ("Handles target mesh tinting") describes an older
design where the catalog tinted meshes directly. Likewise
`UConfiguratorMainWidget::OnColorCatalogClicked` computes a `TargetMesh` switch
([ConfiguratorMainWidget.cpp:731-742](../../Source/awsTutorial/FurnitureConfigurator/UI/ConfiguratorMainWidget.cpp))
and then never uses it. Delete both; today's flow is delegate-only (correct).

## F-3 [INFO] Two parallel selection paths on a swatch

Selection normally flows through `UTileView::OnItemClicked` → `OnTileViewEntryClicked`. The
swatch widget *also* has an internal `Button_SwatchClick` → `HandleSwatchClicked` →
`OnColorSwatchClicked` broadcast, which sets `bIsSelected = true` on its own item **without
clearing the previous selection or notifying the catalog** — nothing in C++ binds
`OnColorSwatchClicked`. If the UMG asset contains that button and a Blueprint binds the
delegate, the two paths can disagree (two tiles rendered selected, label not updated). If the
button doesn't exist in the UMG, the whole path is dead. Verify which is true; either remove
`Button_SwatchClick`/`HandleSwatchClicked`/`OnColorSwatchClicked` or make it forward to the
catalog's canonical selection routine.

## F-4 [FRAGILE] Hardcoded absolute deployment paths in the JSON search list

[ColorCatalogSubsystem.cpp:33-35](../../Source/awsTutorial/ColorCatalog/ColorCatalogSubsystem.cpp)
bakes `/home/ssm-user/…`, `/home/ubuntu/…`, `/local/game/…` into the client. They paper over
staging differences that `DefaultGame.ini`'s stage rules should already solve. Harmless
fallbacks, but any infra path change requires a client rebuild; consider a
`-ColorDataDir=` launch arg instead, and log at `Error` only after all paths fail (currently a
Warning-level success log fires every boot — fine, but see X-2).

## F-5 [INFO] Catalog load failure is silent for the user

If both JSON files fail to load, the subsystem logs an error and the catalog UI simply shows an
empty grid; nothing tells the user why. A one-line "catalog unavailable" state in `RefreshGrid`
(when both source arrays are empty) would make field diagnosis faster.

## F-6 [PERF] Per-keystroke full rebuild

`OnSearchTextChanged` → `RefreshGrid` recreates every `UColorCatalogItemObject` (NewObject per
item, ~2k for RAL+NCS "All") on each keystroke. `UTileView` virtualizes the widgets, so this is
allocation churn only — measured scale is fine; noted in case the catalog grows.

## F-7 [INFO] NCS family reuses the RAL mapper with a dummy color number

`LoadNCSCatalog` passes `ColorNumber = 0` into `MapRALFamilyToCategory` when an NCS entry has a
`family` field (`ColorCatalogSubsystem.cpp:150`) — the parameter is unused by the mapper, so
this works, but the signature invites a future bug. Drop the unused `ColorNumber` parameter.

## F-8 [OK — leave alone]

- Category buttons as pure UI filters (never touching materials) — deliberate fix, keep.
- The normalized fuzzy search (strip spaces/dashes) — simple and effective for RAL/NCS codes.
- `FLinearColor::FromSRGBColor` for the swatch colors — correct color-space handling.
