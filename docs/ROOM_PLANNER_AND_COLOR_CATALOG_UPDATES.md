# MaxiMall Feature Updates & Technical Changelog

**Date:** 2026-08-24  
**Target Branch:** 
arek/current-dev *(Merged to dev only upon testing & approval)*

---

## Modified C++ Classes & Modules

| C++ Class | Header File | Source File | Key Responsibilities & Changes |
| :--- | :--- | :--- | :--- |
| **URoomPlannerWidget** | Source/awsTutorial/FurnitureConfigurator/UI/RoomPlannerWidget.h | Source/awsTutorial/FurnitureConfigurator/UI/RoomPlannerWidget.cpp | Configurable spawn/relocation position (PlannerRelocationLocation = (-10000, 0, 0)); automatic character transform caching in NativeConstruct and exact restoration of position, rotation, and camera view target upon ClosePlanner / NativeDestruct. |
| **AAwsTutorial_PlayerController** | Source/awsTutorial/awsTutorial_PlayerController.h | Source/awsTutorial/awsTutorial_PlayerController.cpp | Added ToggleRoomPlannerUI(bool bOpen) and configurable RoomPlannerRelocationLocation; handles clean opening, closing, and camera/input restoration for Room Planner. |
| **USaveSystemWidget** | Source/awsTutorial/FurnitureConfigurator/UI/SaveSystemWidget.h | Source/awsTutorial/FurnitureConfigurator/UI/SaveSystemWidget.cpp | Full JSON serialization and deserialization of customColors (RAL/NCS LinearColor RGBA, component type, and override material) and doorStates (open/closed slots) for all booths in the level. |
| **AShowroomBooth** | Source/awsTutorial/FurnitureConfigurator/ShowroomBooth.h | Source/awsTutorial/FurnitureConfigurator/ShowroomBooth.cpp | Implemented LoadBoothFullState() ensuring loaded saves authoritatively apply product state, restore RAL/NCS CustomColors, and replicate door states cleanly to all clients. Also ensures ApplyProductData re-applies custom colors on any mesh change. |
| **UConfiguratorMainWidget** | Source/awsTutorial/FurnitureConfigurator/UI/ConfiguratorMainWidget.h | Source/awsTutorial/FurnitureConfigurator/UI/ConfiguratorMainWidget.cpp | Parent class for WBP_PreviewWindow; integrated seamless component re-initialization (SetupWidget); tracks ActiveColorCatalogInstance and automatically closes it if an ineligible mesh is right-clicked; validates color application against DataTable rules. |
| **UColorCatalogWidget** | Source/awsTutorial/ColorCatalog/ColorCatalogWidget.h | Source/awsTutorial/ColorCatalog/ColorCatalogWidget.cpp | Color catalog controller for RAL/NCS palettes; category buttons act strictly as UI grid filters without modifying scene mesh materials; swatch click triggers intentional application. |
| **FurnitureTypes.h** | Source/awsTutorial/FurnitureConfigurator/Data/FurnitureTypes.h | Source/awsTutorial/FurnitureConfigurator/Data/FurnitureTypes.cpp | Clean unified Allow...ColorCatalog booleans in FFurnitureProductRow (AllowCabinetColorCatalog shared between cabinet and doors), FFurnitureModelOption, and shared DataTables. |

---

## 1. Room Planner Character Relocation & Restoration

### Classes: URoomPlannerWidget, AAwsTutorial_PlayerController
- **Configurable Relocation Point:**
  - PlannerRelocationLocation (по умолчанию FVector(-10000.f, 0.f, 0.f)) настраивается в параметрах URoomPlannerWidget и AAwsTutorial_PlayerController (Details panel / Blueprint).
- **Автоматический захват и перемещение при открытии (NativeConstruct):**
  - При открытии виджета планировщика C++ запоминает исходные координаты персонажа (CachedOriginalPlayerLocation), поворот (CachedOriginalPlayerRotation) и угол обзора контроллера (CachedOriginalControlRotation).
  - Персонаж мгновенно и бесшовно телепортируется в зону планировщика (PlannerRelocationLocation), скорость сбрасывается (StopMovementImmediately()).
- **Точный возврат на исходное место при закрытии (ClosePlanner / NativeDestruct):**
  - При нажатии кнопок «Закрыть» / «Назад» или вызове ClosePlanner() персонаж телепортируется **в точности на то же самое место и с тем же направлением взгляда**, откуда был открыт планировщик.
  - Камера плавно переключается на персонажа (SetViewTargetWithBlend), управление и ввод полностью разблокируются (ResetIgnoreInputFlags()).

---

## 2. Save & Load System for RAL/NCS and Door States

### Classes: USaveSystemWidget, AAwsTutorial_PlayerController, AShowroomBooth
- **RAL / NCS Serialization (ExecuteSaveGame):**
  Кастомные цвета каждого стенда сохраняются в JSON с указанием типа компонента, точных значений LinearColor (RGBA) и путей к оверрайдам материалов.
- **Door State Serialization (doorStates):**
  Массив состояний открытых/закрытых створок сохраняется и восстанавливается через LoadBoothFullState().
