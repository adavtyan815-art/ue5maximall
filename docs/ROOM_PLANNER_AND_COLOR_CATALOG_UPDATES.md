# Room Planner, Multi-Mesh Inspection & Color Catalog Updates

**Date:** 2026-08-22  
**Author:** AI Pair Programmer / MaxiMall Engineering  
**Target Branches:** 
arek/current-dev, dev

---

## 1. Room Planner Widget Lifecycle & Manual Control (URoomPlannerWidget)

### Problem
Previously, WBP_RoomPlannerWidget was automatically instantiated and added to the viewport inside AAwsTutorial_PlayerController::BeginPlay(), opening immediately on Play. Spawning it manually via Blueprints conflicted with controller initialization and camera blend logic.

### Solution
- **Removed Auto-Spawn:** Cleaned up BeginPlay() in wsTutorial_PlayerController.cpp to prevent automatic creation/viewport attachment.
- **Blueprint Accessibility:** Changed RoomPlannerInstance in AAwsTutorial_PlayerController.h from BlueprintReadOnly to BlueprintReadWrite so Blueprints can manage widget instances cleanly.
- **Self-Contained Lifecycle (NativeConstruct / NativeDestruct):**
  - NativeConstruct(): Automatically triggers 2D Top-Down camera mode (SetViewMode(ERoomPlannerViewMode::View2D)), activates DrawWall tool, and binds BtnClose / BtnBack buttons to ClosePlanner().
  - NativeDestruct() / ClosePlanner(): Unbinds all ARoomPlannerManager delegates, blends camera smoothly back behind CharPawn, restores player control rotation, and resets input mode to game defaults.

---

## 2. Contextual Guidance Hint System (TxtGuidanceHint)

### Overview
Added dynamic, state-aware on-screen hints to guide users step-by-step through 2D drafting and 3D inspection.

### Key Features
- Bound safely via meta = (BindWidgetOptional) to TxtGuidanceHint in URoomPlannerWidget.
- Contextual states supported:
  1. **Empty 2D Canvas:** Prompts user to drag LMB for first wall or pick 4x4m preset.
  2. **Wall Dragging:** Displays live wall length in meters and corner/angle snapping status.
  3. **2D Draw Mode (Existing Walls):** Prompts user to drag for new wall or snap to existing corners.
  4. **Select Mode (No selection):** Prompts user to click a wall or opening.
  5. **Select Mode (Wall selected):** Instructions to edit length in panel, insert doors/windows, or delete.
  6. **Select Mode (Opening selected):** Instructions to drag along wall to reposition, adjust dimensions, or delete.
  7. **Erase Mode:** Prompts user to click any wall to delete it.
  8. **3D View Mode:** Camera orbit (RMB), zoom (Scroll Wheel), and return to 2D instructions.
- Added descriptive tooltips to all toolbar buttons.

---

## 3. Multi-Mesh Right-Click Inspection (WBP_PreviewWindow)

### Problem
When WBP_PreviewWindow was open, right-clicking other showroom booth meshes (e.g. Mirror, Countertop, Cabinet) did not work because TraceFurnitureComponent blocked all traces when MainWidgetInstance was in the viewport, and the full-screen canvas was treated as hovered across the entire screen.

### Solution
- **Trace Unlocked:** Removed the hard if (MainWidgetInstance && MainWidgetInstance->IsInViewport()) return false; check from TraceFurnitureComponent.
- **Accurate Geometric Bounds:** Updated IsWidgetHoveredGeometrically to check only the active interactive child widgets of the left menu sidebar. Clicks over the 3D viewport area on the right pass straight through into 3D scene traces.
- **Seamless Switching:** Right-clicking another mesh while the configurator is open updates MainWidgetInstance->SetupWidget(...) for the newly clicked component without closing/re-opening the UI or losing input locks. Movement and look input remain disabled (SetIgnoreMoveInput(true), SetIgnoreLookInput(true)).

---

## 4. Color Catalog System (WBP_ColorCatalog & WBP_ColorCatalogSwatch)

### Key Improvements
- **RAL vs NCS Tab Switcher (UpdateTabButtonStyles):**
  - Active tab is highlighted with ActiveTabColor (default: #0A84FF Bright Accent Blue).
  - Inactive tab is styled with InactiveTabColor (default: #111C2E Slate Navy).
- **Color Category Grouping (UpdateCategoryButtonStyles):**
  - Active category orb scales up cleanly (ActiveCategoryScale = 1.25x), centered at (0.5, 0.5).
  - Default category on initial open is set to **Red** (DefaultCategory = EColorShadeCategory::Red).
  - "Все" (All) button dynamically highlights in accent color when active and dark slate when inactive.
- **Uniform Shade Scaling (WBP_ColorSwatchItem / UColorCatalogSwatchWidget):**
  - Selected shade scales the entire swatch item (color box + text code) up to ActiveSwatchScale = 1.20x with center pivot.
  - Default selection automatically selects and scales the first shade (RAL 3020) upon opening or switching category, eliminating initial layout jump on first click.
- **Customizable Properties (Details Panel):**
  - ActiveTabColor, InactiveTabColor, ActiveCategoryAllColor, InactiveCategoryAllColor
  - ActiveCategoryScale (default 1.25), InactiveCategoryScale (default 1.0)
  - ActiveSwatchScale (default 1.20), InactiveSwatchScale (default 1.0)
  - DefaultCategory (default Red), DefaultCatalogType (default RAL)
