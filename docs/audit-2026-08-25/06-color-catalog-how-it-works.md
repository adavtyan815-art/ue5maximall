# Color Catalog (RAL/NCS) — How It Works (2026-08-25)

## Files

| File | Role |
|---|---|
| `ColorCatalog/ColorCatalogTypes.h` | `FColorCatalogItem`, shade-category and catalog-type enums |
| `ColorCatalog/ColorCatalogSubsystem.h/.cpp` (43 + 268) | GameInstance subsystem: loads + parses the two JSON catalogs, filtering |
| `ColorCatalog/ColorCatalogItemObject.h` | UObject wrapper for `UTileView` items |
| `ColorCatalog/ColorCatalogWidget.h/.cpp` (214 + 312) | The catalog panel: tabs, category orbs, search, tile grid, selection |
| `ColorCatalog/ColorCatalogSwatchWidget.h/.cpp` | One tile entry (color box, code label, selection scaling) |

## Data loading (`UColorCatalogSubsystem::Initialize`)

Runs once per GameInstance. Two JSON files are loaded from `Content/Data/Colors/`:
`ral_classic.json` and `ncscolorguide_2052_ui_sorted.json`. `LoadColorJsonFile` tries ~12
candidate paths (relative to `BaseDir`, `ProjectContentDir`, `ProjectDir`, plus three
**hardcoded absolute Linux deployment paths** for the EC2/staged layouts). The files are staged
in packaged builds via `DefaultGame.ini`
(`+DirectoriesToAlwaysStageAsUFS/NonUFS=(Path="Data/Colors")`), which is why the multi-path
search exists.

Parsing produces `FColorCatalogItem` arrays: code, name, hex, `FLinearColor` (from sRGB bytes),
and a shade category. RAL entries map their `family` field through `MapRALFamilyToCategory`
(explicit family names, with White/Black families split by brightness into
White/Grey/Black). NCS entries use the `family` field when present, otherwise the `ncs_sort`
hue/blackness/chromaticness: neutral hue → White/Grey/Black by blackness thresholds; R/B/G
prefixes map directly; Y splits Yellow vs Brown by blackness ≥ 40 ∧ chromaticness ≤ 40.

`FilterColors(type, category, query)` filters the cached array by category and by a normalized
substring search (lowercased, spaces/dashes/underscores stripped) across code, name, and hex.

## UI flow

`UColorCatalogWidget` is opened by the configurator via the static helper
`OpenColorCatalogForWidget(CallingWidget, Class)`: collapses the calling widget
(WBP_PreviewWindow), creates the catalog, adds it to the viewport at Z-order 99. Closing
(`CloseColorCatalog` — Back button, or forced from `UConfiguratorMainWidget::RefreshSelections`
when the newly selected component doesn't allow the catalog) restores the caller's visibility,
broadcasts `OnCatalogClosed`, and removes the widget.

Inside the panel:
- RAL/NCS tab buttons and 13 optional category-orb buttons (bound in `BindCategoryButtons`)
  switch `CurrentCatalogType` / `CurrentCategory` and refresh; active states are shown by
  background color (tabs, "All") and render-scale (orbs). **Category buttons are pure UI
  filters** — they never touch scene materials (this was an explicit fix noted in the archived
  changelog).
- The search box refreshes on every keystroke.
- `RefreshGrid` clears the `UTileView`, creates one `UColorCatalogItemObject` per filtered
  item, and restores the visual selection if the previously chosen code is still in the list.
- Clicking a tile (`OnTileViewEntryClicked`) records the selection, updates the "Активный
  цвет: <code>" label, syncs `bIsSelected` + render scale across visible entry widgets, and
  broadcasts **`OnColorSelected(LinearColor, OverrideMaterial)`** — the widget's configurable
  `OverrideMaterial` (default null) rides along.

`UConfiguratorMainWidget::HandleColorSelected` receives that broadcast and routes it to
`RequestBoothCustomColorChange` (server-authoritative; see doc 02 for how the booth applies
custom colors). The catalog itself never modifies meshes.

`UColorCatalogSwatchWidget` (the `IUserObjectListEntry` tile) mirrors the item object's color/
code/selection into its widgets, scaling itself when selected. It also owns a
`OnColorSwatchClicked` delegate fired by an optional internal button — a second selection path
parallel to the TileView's item click (see Findings F-3).
