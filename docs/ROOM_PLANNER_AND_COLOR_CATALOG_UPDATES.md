# MaxiMall Feature Updates & Technical Changelog

**Date:** 2026-08-24  
**Target Branch:** 
arek/current-dev *(Merged to dev only upon testing & approval)*

---

## Modified C++ Classes & Modules

| C++ Class | Header File | Source File | Key Responsibilities & Changes |
| :--- | :--- | :--- | :--- |
| **AAwsTutorial_PlayerController** | Source/awsTutorial/awsTutorial_PlayerController.h | Source/awsTutorial/awsTutorial_PlayerController.cpp | Removed auto-spawning of Room Planner in BeginPlay(); unlocked multi-mesh interaction in TraceFurnitureComponent() while configurator UI is open; refined IsWidgetHoveredGeometrically() to sidebar bounds; exposed RoomPlannerInstance as BlueprintReadWrite. |
| **URoomPlannerWidget** | Source/awsTutorial/FurnitureConfigurator/UI/RoomPlannerWidget.h | Source/awsTutorial/FurnitureConfigurator/UI/RoomPlannerWidget.cpp | Autonomous UI lifecycle (NativeConstruct / NativeDestruct); automated 2D Top-Down camera setup and smooth 3D camera restore; dynamic guidance hint engine (UpdateGuidanceHintText(), TxtGuidanceHint); toolbar tooltips. |
| **UConfiguratorMainWidget** | Source/awsTutorial/FurnitureConfigurator/UI/ConfiguratorMainWidget.h | Source/awsTutorial/FurnitureConfigurator/UI/ConfiguratorMainWidget.cpp | Parent class for WBP_PreviewWindow; integrated seamless component re-initialization (SetupWidget); tracks ActiveColorCatalogInstance and automatically closes it if an ineligible mesh is right-clicked; validates color application against DataTable rules. |
| **AShowroomBooth** | Source/awsTutorial/FurnitureConfigurator/ShowroomBooth.h | Source/awsTutorial/FurnitureConfigurator/ShowroomBooth.cpp | Added IsColorCatalogAllowedForComponent() query checking product DataTable flags and shared component options to authorize RAL/NCS color capabilities. Doors automatically inherit cabinet settings. |
| **UColorCatalogWidget** | Source/awsTutorial/ColorCatalog/ColorCatalogWidget.h | Source/awsTutorial/ColorCatalog/ColorCatalogWidget.cpp | Color catalog controller for RAL/NCS palettes; active tab styling (UpdateTabButtonStyles()); category orb scaling (UpdateCategoryButtonStyles(), ActiveCategoryScale = 1.25x); initial default selection (Red, RAL 3020); added CloseColorCatalog() for clean autonomous closure. |
| **UColorCatalogSwatchWidget** | Source/awsTutorial/ColorCatalog/ColorCatalogSwatchWidget.h | Source/awsTutorial/ColorCatalog/ColorCatalogSwatchWidget.cpp | Single color swatch item controller (WBP_ColorSwatchItem); uniform whole-item scaling on selection (ActiveScale = 1.20x, InactiveScale = 1.0x); automatic scale inheritance on spawn (NativeOnListItemObjectSet). |
| **AFurniturePreviewActor** | Source/awsTutorial/FurnitureConfigurator/Preview/FurniturePreviewActor.h | Source/awsTutorial/FurnitureConfigurator/Preview/FurniturePreviewActor.cpp | Subject fill lighting rig with level-matching calibration (MatchLevelLighting, MeasureWorldIlluminanceAt) ensuring accurate lighting and PBR material representation across levels. |
| **FurnitureTypes.h** | Source/awsTutorial/FurnitureConfigurator/Data/FurnitureTypes.h | Source/awsTutorial/FurnitureConfigurator/Data/FurnitureTypes.cpp | Clean unified Allow...ColorCatalog booleans in FFurnitureProductRow (AllowCabinetColorCatalog shared between cabinet and doors), FFurnitureModelOption, and shared DataTables. |

---

## 1. Room Planner Lifecycle & Manual Control

### Classes: AAwsTutorial_PlayerController, URoomPlannerWidget
- **Removed Auto-Spawn:** BeginPlay() in AAwsTutorial_PlayerController no longer instantiates or displays WBP_RoomPlannerWidget on game startup.
- **Blueprint Access:** RoomPlannerInstance changed to BlueprintReadWrite so Blueprints can instantiate, show, hide, or destroy the widget on demand.
- **Autonomous Lifecycle:**
  - URoomPlannerWidget::NativeConstruct() switches view mode to 2D Top-Down (SetViewMode(ERoomPlannerViewMode::View2D)), activates DrawWall tool, and binds close buttons.
  - URoomPlannerWidget::NativeDestruct() / ClosePlanner() unbinds delegates, blends camera smoothly back to CharPawn, and restores player rotation and input.

---

## 2. Dynamic Contextual Guidance Hint System

### Classes: URoomPlannerWidget
- Bound via meta = (BindWidgetOptional) to TxtGuidanceHint.
- UpdateGuidanceHintText() dynamically updates hints across 8 contextual states:
  1. Empty 2D Canvas (draw first wall prompt).
  2. Live wall drag (distance in meters, angle & corner snap status).
  3. 2D Draw Mode with existing walls.
  4. Select Mode without selection.
  5. Select Mode with wall selected (length edit, add opening, delete).
  6. Select Mode with opening selected (slide along wall, adjust dimensions, delete).
  7. Erase Mode (click wall to delete).
  8. 3D Inspection Mode (RMB orbit, zoom, return to 2D).
- Descriptive tooltips added to all toolbar action buttons.

---

## 3. Multi-Mesh Right-Click Inspection & Security Gating

### Classes: AAwsTutorial_PlayerController, UConfiguratorMainWidget
- TraceFurnitureComponent() operates freely when MainWidgetInstance (WBP_PreviewWindow) is open in the viewport.
- IsWidgetHoveredGeometrically() strictly checks the active left sidebar panel. Clicks in the 3D scene area pass through to line traces.
- Right-clicking another mesh (e.g. Mirror, Countertop, Cabinet) reconfigures MainWidgetInstance->SetupWidget(...) for the target component without closing/reopening the widget. Look and movement inputs remain disabled during inspection.
- **Anti-Exploit Auto-Close:** If WBP_ColorCatalog is currently open and the user right-clicks an element where color catalog is disallowed, WBP_ColorCatalog is closed immediately and cleanly, preventing unauthorized color modifications.

---

## 4. Color Catalog (RAL & NCS System)

### Classes: UColorCatalogWidget, UColorCatalogSwatchWidget
- **RAL / NCS Switcher (UpdateTabButtonStyles):** Active tab styled with ActiveTabColor (#0A84FF), inactive styled with InactiveTabColor (#111C2E).
- **Color Categories (UpdateCategoryButtonStyles):** Active category orb scales to ActiveCategoryScale = 1.25x (with Red active by default).
- **Uniform Swatch Scaling:** The entire WBP_ColorSwatchItem (color box + text) scales up to ActiveSwatchScale = 1.20x upon selection.
- **Default Selection:** First shade (RAL 3020) is selected by default on open and category switch, eliminating initial layout jumps.
- **Clean Lifecycle Method:** Added CloseColorCatalog() to guarantee clean destruction and restoring the parent widget.

---

## 5. Data-Driven Color Catalog Accessibility (DataTable Booleans)

### Classes: FurnitureTypes.h, AShowroomBooth, UConfiguratorMainWidget
- **Product DataTable Booleans (FFurnitureProductRow):** Clean unified boolean toggles under **Product | Color Catalog**:
  - AllowCabinetColorCatalog (Controls both Cabinet body and Doors)
  - AllowClosetColorCatalog
  - AllowCountertopColorCatalog
  - AllowSinkColorCatalog
  - AllowFaucetColorCatalog
  - AllowMirrorColorCatalog
- **Component & Shared Models Booleans:** Added AllowColorCatalog to FFurnitureCabinetOptions, FFurnitureDoorGroup, FFurnitureModelOption, FFurnitureComponentOptions, FFurnitureCountertopRow, FFurnitureSinkRow, FFurnitureFaucetRow, and FFurnitureMirrorRow.
- **Inherited Door Logic:** Doors automatically inherit the allowance from AllowCabinetColorCatalog / CabinetOptions.bAllowColorCatalog.
- **Strict Backend Gating:** HandleColorSelected() validates permissions against IsColorCatalogAllowedForComponent().
