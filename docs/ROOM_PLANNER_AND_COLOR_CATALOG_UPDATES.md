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

---

## 3. Cinematic Auto-Tour Engine (Студийный 360° авто-облет)

### Classes: AAwsTutorial_PlayerController, UConfiguratorMainWidget
- **View Mode Integration (Studio Turntable):**
  - При переходе в режим изоляции (View Mode / `AFurniturePreviewActor`) тур **включается автоматически по умолчанию**.
  - Мебель плавно вращается на 360 градусов перед камерой на 60 FPS со студийным нейтральным освещением (канал 1) и затемненным фоном.
- **RMB Toggle (Управление ПКМ):**
  - Одиночный клик **ПКМ (Right Mouse Button)** внутри View Mode переключает тур: **Пауза ↔ Старт**.
- **Zero Idle Overhead (Оптимизация):**
  - Легковесный таймер (0.016s) активен строго во время тура. При выходе таймер очищается (`ClearTimer`), потребление ресурсов — 0.00%.

---

## 4. ShowroomBooth Studio Relocation & Exact Restoration

### Classes: AAwsTutorial_PlayerController
- **Configurable Relocation Coordinates:**
  - `ViewModeRelocationLocation` (по умолчанию `FVector(-10000.f, 0.f, 0.f)`) и `ViewModeRelocationRotation` (по умолчанию `FRotator(0.f, 90.f, 0.f)`) настраиваются в Details panel `AAwsTutorial_PlayerController`.
- **Первоочередное перемещение стенда (Relocation FIRST):**
  - В `OpenFurniturePreview()` перед спавном превью-актора и расчетом любых параметров `TargetBooth` сначала сохраняет свой исходный трансформ (`CachedOriginalBoothTransform`), а затем мгновенно перемещается в студийные координаты `(-10000, 0, 0)` с поворотом `(0, 90, 0)`.
  - Все последующие действия превью выполняются уже в чистой изолированной студийной позиции.
- **Точный возврат на место при выходе (`CloseFurniturePreview`):**
  - При закрытии режима просмотра (`Escape`, кнопка «Назад») исходный стенд мгновенно возвращается в свои оригинальные координаты и поворот в комнате через `SetActorTransform(CachedOriginalBoothTransform)`.
