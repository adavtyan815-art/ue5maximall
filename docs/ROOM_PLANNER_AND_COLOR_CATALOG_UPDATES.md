# MaxiMall Feature Updates & Technical Changelog

**Date:** 2026-08-24  
**Target Branch:** 
arek/current-dev *(Merged to dev only upon testing & approval)*

---

## Modified C++ Classes & Modules

| C++ Class | Header File | Source File | Key Responsibilities & Changes |
| :--- | :--- | :--- | :--- |
| **USaveSystemWidget** | Source/awsTutorial/FurnitureConfigurator/UI/SaveSystemWidget.h | Source/awsTutorial/FurnitureConfigurator/UI/SaveSystemWidget.cpp | Full JSON serialization and deserialization of customColors (RAL/NCS LinearColor RGBA, component type, and override material) and doorStates (open/closed slots) for all booths in the level. |
| **AAwsTutorial_PlayerController** | Source/awsTutorial/awsTutorial_PlayerController.h | Source/awsTutorial/awsTutorial_PlayerController.cpp | Extended Server_LoadBoothState to pass InCustomColors and InDoorStates to authoritative showroom booths. |
| **AShowroomBooth** | Source/awsTutorial/FurnitureConfigurator/ShowroomBooth.h | Source/awsTutorial/FurnitureConfigurator/ShowroomBooth.cpp | Implemented LoadBoothFullState() ensuring loaded saves authoritatively apply product state, restore RAL/NCS CustomColors, and replicate door states cleanly to all clients. Also ensures ApplyProductData re-applies custom colors on any mesh change. |
| **URoomPlannerWidget** | Source/awsTutorial/FurnitureConfigurator/UI/RoomPlannerWidget.h | Source/awsTutorial/FurnitureConfigurator/UI/RoomPlannerWidget.cpp | Autonomous UI lifecycle (NativeConstruct / NativeDestruct); automated 2D Top-Down camera setup and smooth 3D camera restore; dynamic guidance hint engine (UpdateGuidanceHintText(), TxtGuidanceHint); toolbar tooltips. |
| **UConfiguratorMainWidget** | Source/awsTutorial/FurnitureConfigurator/UI/ConfiguratorMainWidget.h | Source/awsTutorial/FurnitureConfigurator/UI/ConfiguratorMainWidget.cpp | Parent class for WBP_PreviewWindow; integrated seamless component re-initialization (SetupWidget); tracks ActiveColorCatalogInstance and automatically closes it if an ineligible mesh is right-clicked; validates color application against DataTable rules. |
| **UColorCatalogWidget** | Source/awsTutorial/ColorCatalog/ColorCatalogWidget.h | Source/awsTutorial/ColorCatalog/ColorCatalogWidget.cpp | Color catalog controller for RAL/NCS palettes; active tab styling (UpdateTabButtonStyles()); category orb scaling (UpdateCategoryButtonStyles(), ActiveCategoryScale = 1.25x); initial default selection (Red, RAL 3020); added CloseColorCatalog() for clean autonomous closure. |
| **UColorCatalogSwatchWidget** | Source/awsTutorial/ColorCatalog/ColorCatalogSwatchWidget.h | Source/awsTutorial/ColorCatalog/ColorCatalogSwatchWidget.cpp | Single color swatch item controller (WBP_ColorSwatchItem); uniform whole-item scaling on selection (ActiveScale = 1.20x, InactiveScale = 1.0x); automatic scale inheritance on spawn (NativeOnListItemObjectSet). |
| **FurnitureTypes.h** | Source/awsTutorial/FurnitureConfigurator/Data/FurnitureTypes.h | Source/awsTutorial/FurnitureConfigurator/Data/FurnitureTypes.cpp | Clean unified Allow...ColorCatalog booleans in FFurnitureProductRow (AllowCabinetColorCatalog shared between cabinet and doors), FFurnitureModelOption, and shared DataTables. |

---

## 1. Save & Load System for RAL/NCS and Door States

### Classes: USaveSystemWidget, AAwsTutorial_PlayerController, AShowroomBooth
- **RAL / NCS Serialization (ExecuteSaveGame):**
  Each booth's CustomColors array is converted to JSON objects containing:
  - componentType (Cabinet, Sink, Faucet, etc.)
  - color (, g, ,  floats)
  - overrideMaterial (asset object path)
- **Door State Serialization (doorStates):**
  The open/closed status of each door slot is saved as an integer array in JSON.
- **Authoritative Full Load (LoadBoothFullState):**
  When loading a save via HandleLoadSaveItem:
  - Parses CustomColors and DoorStates alongside standard component indices.
  - Calls AwsPC->Server_LoadBoothState(TargetBooth, State, LoadedCustomColors, LoadedDoorStates).
  - Re-evaluates booth geometry (RebuildBoothVisuals), immediately re-applies custom colors (OnRep_CustomColors), and restores door transforms (OnRep_DoorStates).
  - Guarantees no stale or mismatched colors remain from previous sessions.

---

## 2. Persistent RAL/NCS Colors Across Model Changes

### Classes: AShowroomBooth
- ApplyProductData(Data) now executes OnRep_CustomColors() at the end of mesh setup.
- Changing a component model (e.g. changing Faucet model or Cabinet size) automatically re-applies custom colors to the new meshes.
- Custom colors are only cleared when the user explicitly chooses a standard catalog material preset in the UI.

---

## 3. Dynamic Contextual Guidance Hint System & Room Planner

### Classes: URoomPlannerWidget, AAwsTutorial_PlayerController
- TxtGuidanceHint provides dynamic contextual hints across all 8 editing states (Empty canvas, Live wall drag, Draw mode, Select mode, Opening editing, Erase mode, 3D inspection).
- Manual Blueprint-driven opening and closing of Room Planner.

---

## 4. Multi-Mesh Right-Click Inspection & Anti-Exploit Security

### Classes: AAwsTutorial_PlayerController, UConfiguratorMainWidget, AShowroomBooth
- Seamless right-click inspection across 3D meshes while configurator UI is open.
- Auto-closing of WBP_ColorCatalog if switching to an element where color catalog is disallowed.
- Hard security checks in HandleColorSelected() validating permissions via IsColorCatalogAllowedForComponent().
