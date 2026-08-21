# Color Catalog System Architecture

This directory contains the core logic for the awsTutorial Color Catalog system, entirely separated from the Room Planner.

## Architecture Overview

The system consists of three main C++ pillars, backed by UI Blueprints in the `Content/ColorCatalog` directory:

1. **`UColorCatalogSubsystem` (GameInstance Subsystem)**
   - Responsible for loading, parsing, and caching JSON files containing color data (`ral_classic.json` and `ncscolorguide_2052_ui_sorted.json`).
   - These JSON files must be located in `Content/Data/Colors/`.
   - The parsing logic correctly converts RGB values into Unreal's `FLinearColor` by using `FLinearColor::FromSRGBColor(FColor(R, G, B, 255))`, ensuring proper sRGB-to-Linear space conversion before the color reaches any material parameters.
   - Provides robust filtering mechanisms by catalog type (RAL vs NCS), color family (Red, Blue, Green, etc.), and text search query.
   - Categorization is based on predefined string mappings mapped to `EColorShadeCategory`.

2. **`UColorCatalogWidget` (Main Catalog UI Controller)**
   - Exposes `OpenColorCatalogForWidget` to handle seamless UI overlay logic (collapsing parent menus, pushing itself to the viewport, and restoring the parent on exit).
   - Dynamically binds UI elements via `UPROPERTY(meta = (BindWidgetOptional))`.
   - Listens to text input from `EditableText_Search` to filter colors via the subsystem.
   - Populates a `UTileView` grid with `UColorCatalogItemObject` items.
   - When a user clicks a swatch (`OnTileViewEntryClicked`), it immediately applies a "Live Preview" of the chosen color by searching for the target mesh's `UMaterialInstanceDynamic` and updating the Vector Parameter (defaults to `BaseColor` or `Color`).

3. **`UColorCatalogSwatchWidget` (Individual Color Swatch Item)**
   - Represents a single tile in the `UTileView` grid.
   - Receives data from `UColorCatalogItemObject` via `IUserObjectListEntry`.
   - Applies the specific HEX tint to a Slate Brush or UI Material so the user can visually identify the color.
   - Handles the active selection visual states.

## Data Flow (End-to-End)
1. **Init**: The GameInstance starts, `UColorCatalogSubsystem` initializes and reads the JSON files.
2. **Open UI**: A parent widget calls `OpenColorCatalogForWidget`, passing in a `TargetMesh` (a 3D primitive on the screen).
3. **Populate UI**: `UColorCatalogWidget` calls `FilterColors` on the subsystem, builds `UColorCatalogItemObject` instances, and feeds them into `TileView_Swatches`.
4. **Interact**: User clicks a swatch. The swatch triggers the parent `UColorCatalogWidget`'s callback.
5. **Apply**: `UColorCatalogWidget` converts the color to an sRGB `FLinearColor`, loops through all materials on `TargetMesh`, creates Dynamic Material Instances if needed, and calls `SetVectorParameterValue("BaseColor", SelectedColor)`.

## Known Caveats & Rules
- The C++ UI class has been completely cleaned of useless "Apply" button logic (`Button_Apply`), as the user workflow prefers instant application ("Live Preview").
- Do NOT use plain division (`R / 255.f`) for color conversion here; standard JSON HEX/RGB values are authored in sRGB space.
- The `Constructor` folder was deprecated. Do not refer to this system as "RoomPlannerColor"; it is the "ColorCatalog" module.
