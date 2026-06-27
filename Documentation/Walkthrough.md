# Walkthrough - Countertop Isolation, Unified Cabinet/Door Colors & Dynamic Door Blueprint Layouts

All modifications requested in the implementation plans have been completed and verified with a clean compiler build.

---

## State Replication Matrix

The system replicates a single state structure, `FShowroomBoothConfigState`, which carries independent configuration indices for all modular components. This decouples the visual presentation layer from the network synchronization layer:

| Component Type | Size Index Property | Color/Material Index Property | UI/Redirection Behavior |
| :--- | :--- | :--- | :--- |
| **Cabinet** | `ActiveSizeIndex` (unified) | `ActiveColorIndex` (unified) | Right-click Cabinet opens Cabinet options. |
| **Doors** | `ActiveSizeIndex` (unified) | `ActiveColorIndex` (unified) | Right-click Doors redirects to Cabinet options. |
| **Countertop** | `ActiveSizeIndex` (unified) | `ActiveCountertopColorIndex` (isolated) | Right-click Countertop opens Countertop-only options. Size controls collapsed. |
| **Closet** | `ClosetSizeIndex` | `ClosetColorIndex` | Configured independently. |
| **Sink** | `SinkSizeIndex` | `SinkColorIndex` | Configured independently. |
| **Faucet** | `FaucetSizeIndex` | `FaucetColorIndex` | Configured independently. |
| **Mirror** | `MirrorSizeIndex` | `MirrorColorIndex` | Configured independently. |

---

## Changes Made

### 1. Data Structures and Schemas Updated
- **[FurnitureTypes.h](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Data/FurnitureTypes.h)**
  - Defined `FFurnitureDoorsConfig` to split door configurations into `CabinetDoors` (active for current context) and `ClosetDoors` (reserved for upcoming phase).
  - Defined `FFurnitureDoorGroup` containing a `DoorCount` enum, `FFurnitureSingleDoorConfig` (for single door setup), and `FFurnitureDoubleDoorsConfig` (for double doors setup) with conditional edit conditions (`meta = (EditCondition = ...)`).
  - Grouped `FDoorSlotConfig` (hinge logic) inside `SingleDoor.SlotConfig`, `DoubleDoors.Slot0Config`, and `DoubleDoors.Slot1Config` respectively.
  - Declared `FDoorSlotConfig` higher in the file to resolve circular type dependencies in UHT header parsing.
  - Replaced all door-related properties at the root of `FFurnitureProductRow` with `DoorsConfig` of type `FFurnitureDoorsConfig`.
  - Added new structs `FFurnitureMetadata` and `FFurnitureMetadataEntry` to support independent combination indices.
  - Updated `FFurnitureComponentOptions` to include a `CombinationsMetadata` array representing the Combinatorial Metadata Matrix.

### 2. Showroom Booth Refactoring & Closet Doors Integration
- **[ShowroomBooth.h](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.h)** & **[ShowroomBooth.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.cpp)**
  - Declared and initialized two new static mesh components: `ClosetDoorMeshSlot0` and `ClosetDoorMeshSlot1` (attached to `ClosetMesh`).
  - Added transient variables `BaselineClosetDoor0Transform` and `BaselineClosetDoor1Transform` and updated `EnsureBaselineTransformsCaptured` to record default editor relative transforms for the closet doors.
  - Refactored `ApplyDoorConfiguration` to dynamically apply `ClosetDoors` mesh and material assignments using the closet-specific configurations and active closet indices (`ClosetSizeIndex` and `ClosetColorIndex`).
  - Updated `ApplyDoorSlotVisual` to resolve baseline transforms for the closet door components during positioning.
  - Added safety initialization guards in `InitializeDefaultBooth` for the new closet door mesh components.

### 3. Studio Preview Visual Synchronization
- **[FurniturePreviewActor.h](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/FurniturePreviewActor.h)** & **[FurniturePreviewActor.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/FurniturePreviewActor.cpp)**
  - Declared `ClosetDoorMeshSlot0` and `ClosetDoorMeshSlot1` static mesh components.
  - Initialized, attached to `ClosetMesh`, and configured the new components to cast shadow, disable collision, and isolate to lighting channel 1 in the constructor.
  - Refactored `LoadProductPreview` to synchronize closet door transforms from the showroom booth or place them using their closed position offsets, and apply closet-specific size/color configuration indices dynamically.

### 4. UI Widget Configurator Clean Up & Metadata Display
- **[ConfiguratorMainWidget.h](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/UI/ConfiguratorMainWidget.h)** & **[ConfiguratorMainWidget.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/UI/ConfiguratorMainWidget.cpp)**
  - Removed all ComboBox widgets (such as `Combo_Size`, `Combo_Color`, and individual component selectors) and the legacy `Txt_ProductName` field from both declarations and code.
  - Added strictly bound widgets: `Txt_SKU` (UTextBlock), `Txt_ProductName_1` (UTextBlock), and `Txt_BtnURL` (UButton) mapping to the `WBP_PreviewWindow` blueprint.
  - Updated `RefreshSelections` to query active configuration metadata (Product Name and SKU) dynamically through the Player Controller (`GetActiveComponentMetadata`) and update the corresponding UTextBlock texts.
  - Implemented `OnURLButtonClicked` delegate linked to `Txt_BtnURL->OnClicked` to read the active configuration's URL and print it on screen (`AddOnScreenDebugMessage`) and in the logs.

---

## Verification Plan

### Clean Compiler Build (Verified)
- Rebuilt the project via `UnrealBuildTool` in `Development` mode:
  ```powershell
  & "D:\Programs\Epic Games\UE_5.3\Engine\Build\BatchFiles\Build.bat" MaxiMallEditor Win64 Development -Project="C:\Users\Admin\Desktop\Aleg\UE5C++\MaxiMall\MaxiMall.uproject" -WaitMutex
  ```
  - Result: **Successful compilation** with **0 errors and 0 warnings**.

### Manual Verification
1. Play in Editor (PIE) and verify closet doors render and position themselves correctly on play (when a mesh is added inside doors -> closet doors).
2. Swap cabinet/closet size/color options and verify that the metadata (`Txt_ProductName_1` and `Txt_SKU`) updates instantly in real-time.
3. Click the URL Button (`Txt_BtnURL`) and verify that a Print String outputs the correct combination URL on the screen.

---

## Update: Closet Doors Viewmode and Interaction Fixes

We integrated the closet doors (`ClosetDoorMeshSlot0` and `ClosetDoorMeshSlot1`) into trace detection, double-click interactions, and viewmode isolation logic in the preview player controller:

### 1. Viewmode Isolation Visibility Updates
- **[MaxiMallPreviewController.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/MaxiMallPreviewController.cpp)**
  - Updated `OnTargetBoothProductChanged` to hide the closet door meshes (`ClosetDoorMeshSlot0` and `ClosetDoorMeshSlot1`) during isolation hiding, preventing them from floating in viewmode when other components are isolated.
  - Implemented logic in `case EFurnitureComponentType::Closet` inside `OnTargetBoothProductChanged` to set closet door visibility based on the active configuration's closet door count.
  - Implemented logic in `case EFurnitureComponentType::Doors` inside `OnTargetBoothProductChanged` to set closet door visibility based on the active configuration's closet door count, in addition to cabinet doors.

### 2. Interaction and Highlight Routing
- **[MaxiMallPreviewController.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/MaxiMallPreviewController.cpp)**
  - Appended `ClosetDoorMeshSlot0` and `ClosetDoorMeshSlot1` to the interactable hit component checks in `PlayerTick`, enabling Custom Depth highlighting and the hand cursor behavior on hover.
  - Included `ClosetDoorMeshSlot0` and `ClosetDoorMeshSlot1` in `TraceFurnitureComponent` to map their selection to `EFurnitureComponentType::Doors`.
  - Updated `HandleDoubleClickInteraction` to route double-click events on `ClosetDoorMeshSlot0` to slot index `2` and `ClosetDoorMeshSlot1` to slot index `3`, matching the animation slots on the showroom booth.

### 3. Compilation Verification
- Ran the Unreal Build Tool build:
  ```powershell
  & "D:\Programs\Epic Games\UE_5.3\Engine\Build\BatchFiles\Build.bat" MaxiMallEditor Win64 Development -Project="C:\Users\Admin\Desktop\Aleg\UE5C++\MaxiMall\MaxiMall.uproject" -WaitMutex
  ```
  - Result: **Successful compilation** (completed in 2.14 seconds).

---

## Update: Conditional Size UI & Viewmode Alignment Fixes

We implemented additional UI and preview alignment enhancements:

### 1. Conditional Size UI Visibility
- **[ConfiguratorMainWidget.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/UI/ConfiguratorMainWidget.cpp)**
  - Updated the size container generation logic: if `ActiveOpts.Sizes.Num() <= 1` (i.e. only 1 or no size is defined for the active product/component combination), we set the visibility of `Size_Container` to `ESlateVisibility::Collapsed` and clear its children, hiding it from the user.

### 2. Viewmode World Rotation Alignment
- **[MaxiMallPreviewController.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/MaxiMallPreviewController.cpp)**
  - Updated the spawning logic in `OpenFurniturePreview`: retrieved the target showroom booth's world Yaw rotation (`TargetBooth->GetActorRotation().Yaw`) and initialized the `AFurniturePreviewActor` with this rotation. This ensures all components in viewmode share the exact same orientation/rotation as they do in the level.

### 3. Compilation Verification
- Ran the Unreal Build Tool build:
  ```powershell
  & "D:\Programs\Epic Games\UE_5.3\Engine\Build\BatchFiles\Build.bat" MaxiMallEditor Win64 Development -Project="C:\Users\Admin\Desktop\Aleg\UE5C++\MaxiMall\MaxiMall.uproject" -WaitMutex
  ```
  - Result: **Successful compilation** (completed in 2.26 seconds).

### 4. Remote Git Branch dev Synchronization (Verified)
- Staged, committed, and pushed all local changes (including technical documentation, source code files, and assets) to the remote repository:
  - Branch: `dev`
  - Target URI: `https://github.com/adavtyan815-art/ue5maximall.git`
  - Commit ID: `744a637`


---

## Update: Viewmode Rotation Projection & Closet Door Routing Fixes

We implemented further enhancements to align the viewmode camera and properly route closet door right-clicks:

### 1. Viewmode Camera Angle Alignment (Relative Rotation Projection)
- **[FurniturePreviewActor.h](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/FurniturePreviewActor.h)** & **[FurniturePreviewActor.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/FurniturePreviewActor.cpp)**
  - Added `DefaultYaw` and `DefaultPitch` fields and the `SetInitialRotation` method to `AFurniturePreviewActor`.
  - Updated `ResetRotation` to restore camera angles to the configured `DefaultYaw` and `DefaultPitch` values instead of hardcoded defaults.
- **[MaxiMallPreviewController.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/MaxiMallPreviewController.cpp)**
  - In `OpenFurniturePreview`, we projected the player's level camera Yaw and Pitch directly onto the preview camera system:
    - `InitialYaw = NormalizeAxis(SavedControlRotation.Yaw - SpawnRotation.Yaw)`
    - `InitialPitch = NormalizeAxis(SavedControlRotation.Pitch)`
  - Setting this initial relative camera rotation ensures the model is viewed from the exact same angle (relative to the screen) as the player was looking at it in the level, completely resolving the 90-degree view offset.

### 2. Closet Door Right-Click & Focus Routing
- **[MaxiMallPreviewController.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/MaxiMallPreviewController.cpp)**
  - Refactored `TraceFurnitureComponent` to differentiate the door components:
    - Cabinet doors (`DoorMeshSlot0/1`) remain mapped to `EFurnitureComponentType::Doors` (which opens the Cabinet UI).
    - Closet doors (`ClosetDoorMeshSlot0/1`) are now mapped to `EFurnitureComponentType::Closet` (which opens the Closet options UI).
  - Updated `HandleDoubleClickInteraction` to allow double-click routing for both `Doors` and `Closet` component types, ensuring closet doors at slots 2 and 3 can still be double-clicked to animate open/closed.

### 3. Compilation Verification
- Ran the Unreal Build Tool build:
  ```powershell
  & "D:\Programs\Epic Games\UE_5.3\Engine\Build\BatchFiles\Build.bat" MaxiMallEditor Win64 Development -Project="C:\Users\Admin\Desktop\Aleg\UE5C++\MaxiMall\MaxiMall.uproject" -WaitMutex
  ```
  - Result: **Successful compilation** (completed in 2.96 seconds).

---

## Update: Decoupled Cabinet/Component Schema, Scrolling Models Grid & Model-Specific Colors

We implemented a decoupled catalog schema and updated the dynamic configuration UI to support scrolling grid panels, transparent size buttons, and nested color options:

### 1. Data Schema Refactoring
- **[FurnitureTypes.h](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Data/FurnitureTypes.h)**:
  - Created `FFurnitureCabinetOptions` retaining `Sizes` array and adding `SizeNames` string list.
  - Created `FFurnitureModelOption` containing `Mesh`, `Thumbnail`, and a nested `Colors` list.
  - Refactored `FFurnitureComponentOptions` to hold `Models` (with meta display name `"Models"`) and `CombinationsMetadata`.
  - Updated `FFurnitureProductRow` to split options between `CabinetOptions` (`FFurnitureCabinetOptions`) and other component options (`FFurnitureComponentOptions`).

### 2. Showroom Booth and Preview Actor Overloads
- **[ShowroomBooth.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.cpp)** & **[FurniturePreviewActor.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/FurniturePreviewActor.cpp)**:
  - Implemented overloaded versions of `ApplyComponentMeshAndMaterials` for both `FFurnitureComponentOptions` (non-cabinet components) and `FFurnitureCabinetOptions` (cabinet).
  - Overloads extract meshes and nested colors correctly from their respective struct layouts.

### 3. Dynamic UI Layout & Scrolling Grid
- **[ConfiguratorMainWidget.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/UI/ConfiguratorMainWidget.cpp)**:
  - **Cabinet Size Options**: Custom texts from `CabinetOptions.SizeNames` are rendered as transparent buttons using `SetBackgroundColor(FLinearColor::Transparent)`. Padding margins are applied via `UHorizontalBoxSlot` / `UWrapBoxSlot` casts.
  - **Component Model Options**: Non-cabinet model choices are rendered as thumbnails in a `UUniformGridPanel` nested within a `UScrollBox`. Buttons are placed in a fixed 2-column layout and sizing is enforced using `USizeBox` wrappers (120x120 pixels).
  - **Model-Specific Colors**: Active color arrays are queried nested under the active model. The color panel collapses dynamically if a model has 1 or fewer color options.
  - **Active Selection Routing**: Selectors reset the active color index to `0` when changing the model size.
  - **Player Controller Metadata Matrix Queries**: Updated `GetActiveComponentMetadata` in `MaxiMallPreviewController.cpp` to correctly search the cabinet-specific metadata array separately from the general component options metadata arrays.

### 4. Compilation & Remote Repository Sync
- **Clean Compiler Build**: Rebuilt successfully via `UnrealBuildTool` in development mode.
- **Git Sync**: Staged, committed, and pushed all updates to the remote branch `dev`.
  - Remote: `https://github.com/adavtyan815-art/ue5maximall.git`
  - Commit ID: `aa57741`

---

## Update: Decoupled Shared Data Catalog Migration (Option A), Size Pill button styling & Grid limits

We migrated the catalog architecture to a decoupled shared data lookup system, styled the transparent size text buttons as clickable pills, and wrapped the models scroll list inside a height constraint box:

### 1. Decoupled Shared Data Migration (Option A)
- **[FurnitureTypes.h](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Data/FurnitureTypes.h)**:
  - Created `FFurnitureModelRow` inheriting from `FTableRowBase` to represent shared catalog rows.
  - Refactored `FFurnitureProductRow` by replacing countertop, sink, faucet, and mirror `FFurnitureComponentOptions` with `TArray<FName>` IDs and separate combinations metadata arrays (`CountertopCombinationsMetadata`, etc.).
- **[ShowroomBooth.h](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.h)** & **[ShowroomBooth.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.cpp)**:
  - Declared lookup DataTables `SharedCountertopsCatalog`, `SharedSinksCatalog`, `SharedFaucetsCatalog`, and `SharedMirrorsCatalog`.
  - Implemented `GetResolvedComponentOptions` to resolve shared models at runtime into memory-managed `FFurnitureComponentOptions` structs.
  - Updated `ApplyProductData` to query and resolve options on the fly.
- **[FurniturePreviewActor.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/FurniturePreviewActor.cpp)**:
  - Updated `LoadProductPreview` to query `GetResolvedComponentOptions` from the source booth actor to synchronize models and color properties.
- **[MaxiMallPreviewController.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/MaxiMallPreviewController.cpp)**:
  - Updated `GetActiveComponentMetadata` to dynamically resolve catalog details for decoupled components.

### 2. Glassmorphism Size Pill styling
- **[ConfiguratorMainWidget.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/UI/ConfiguratorMainWidget.cpp)**:
  - Customized size button `FButtonStyle` tints using `GetStyle()` to apply glass capsule shapes:
    - Selected: Translucent light blue (`FLinearColor(0.2f, 0.6f, 1.0f, 0.3f)`).
    - Unselected: Faint translucent white (`FLinearColor(1.f, 1.f, 1.f, 0.05f)`).
    - Added corresponding hover and press highlights.

### 3. Models Grid Scroll limits
- **[ConfiguratorMainWidget.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/UI/ConfiguratorMainWidget.cpp)**:
  - Configured `UScrollBox` to use `ESlateVisibility::Visible` for scrollbar rendering.
  - Wrapped `UScrollBox` inside a `USizeBox` with `SetMaxDesiredHeight(255.f)` to restrict the space to exactly 4 items (2 rows) before scrolling.

### 4. Verification & Sync
- **Clean Compiler Build**: Rebuilt successfully via `UnrealBuildTool` in development mode.
- **Git Synchronization**: Staged and pushed all updates (including C++ changes and updated catalog assets) to branch `dev`.

---

## Update: Relocating Offsets & Metadata and Editor Dropdown Menus

We migrated the countertop structural type (`CountertopType`), placement offsets (`RelativeOffset` replacing `SinkOffset`/`FaucetOffset`), and combinatorial metadata (product name, SKU, and URL) out of the main booth catalog row (`FFurnitureProductRow`) and directly into the shared catalog structure (`FFurnitureModelRow` / `FFurnitureColorOption`). We also introduced native Unreal Editor dropdown pickers for allowed ID list properties in the DataTable editor.

### 1. Data Schema & Relocation Changes
- **[FurnitureTypes.h](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Data/FurnitureTypes.h)**:
  - Added `ProductName`, `SKU`, and `URL` properties directly to `FFurnitureColorOption`.
  - Added `CountertopType` and a unified `FFurniturePlacementOffset` (`RelativeOffset`) property directly to `FFurnitureModelRow`.
  - Cleaned up `FFurnitureProductRow` by removing countertop types, offsets, and all component-specific combinations metadata matrices.
  - Decorated countertop, sink, faucet, and mirror `AllowedIDs` arrays in `FFurnitureProductRow` with `GetOptions` metadata specifiers pointing to a static editor helper.
  - Declared helper `UFurnitureEditorHelper` class containing static options generators.
- **[FurnitureTypes.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Data/FurnitureTypes.cpp) [NEW]**:
  - Implemented static string array generators for `UFurnitureEditorHelper` that load the respective shared catalog DataTables from fixed Game folder paths and extract their row names for display.

### 2. Showroom Booth & Preview Actor Adaptations
- **[ShowroomBooth.h](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.h)** & **[ShowroomBooth.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.cpp)**:
  - Declared and captured baseline relative transforms for the countertop (`BaselineCountertopTransform`) and mirror (`BaselineMirrorTransform`) in addition to sink/faucet.
  - Implemented dynamic lookup getter functions (`GetActiveCountertopType()`, `GetActiveSinkOffset()`, etc.) that query the active component catalog row to determine its type and relative offsets.
  - Updated options resolution in `GetResolvedComponentOptions` to construct combinations metadata dynamically on the fly from the nested color metadata in the shared model rows.
  - Updated visual application (`ApplyProductData`) and transform recalculation (`RecalculateDependentTransforms`) to apply relocated offsets.
- **[FurniturePreviewActor.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/FurniturePreviewActor.cpp)**:
  - Updated preview actor rendering to pull active offsets and countertop types from the booth getters and apply them to the preview countertop, sink, faucet, and mirror static mesh components.

### 3. Controller & UI Compatibility
- **[MaxiMallPreviewController.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/MaxiMallPreviewController.cpp)**:
  - Updated visibility toggles in isolation mode to query countertop types via `GetActiveCountertopType()` from the booth.
- **[ConfiguratorMainWidget.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/UI/ConfiguratorMainWidget.cpp)**:
  - Simplified sink mesh visibility check to query countertop type from the booth actor getter.

### 4. Compilation Verification
- **Clean Compiler Build**: Rebuilt successfully via `UnrealBuildTool` in development mode.
  - Result: **Successful compilation** with **0 errors and 0 warnings**.

---

## Update: Component-Specific Row Structures, Countertop Selection Decoupling, and Faucet Filtering

We migrated the catalog architecture to use component-specific shared DataTable row structures and decoupled countertop selection from cabinet size, allowing users to choose countertop models independently while applying dynamic component dependency and faucet type filtering rules:

### 1. Component-Specific Row Structures
- **[FurnitureTypes.h](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Data/FurnitureTypes.h)**:
  - Defined component-specific row structures: `FFurnitureCountertopRow`, `FFurnitureSinkRow`, `FFurnitureFaucetRow`, and `FFurnitureMirrorRow` (replacing the single shared `FFurnitureModelRow` which polluted unrelated assets with properties like `CountertopType`).
  - Added `CountertopSizeIndex` to `FShowroomBoothConfigState` to track countertop model selection independently from cabinet size.
  - Added `EFaucetType` enum (Standard vs Integrated) and added `FaucetType` to `FFurnitureFaucetRow` and `CountertopType` to `FFurnitureCountertopRow`.
  - Added `CountertopType` and `RelativeOffset` properties directly to `FFurnitureModelOption` runtime struct.

### 2. Showroom Booth Resolution and Clamp Guards
- **[ShowroomBooth.h](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.h)**:
  - Updated DataTable property metadata tags to check for specific row structures (`FurnitureCountertopRow`, `FurnitureSinkRow`, etc.).
- **[ShowroomBooth.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.cpp)**:
  - **Dynamic Faucet & Countertop Options Resolution**:
    - In `GetResolvedComponentOptions` for Countertop, filtered countertop models to only include rows matching the active cabinet size (`CabinetSizeIndex == ActiveState.ActiveSizeIndex`).
    - In `GetResolvedComponentOptions` for Faucet, filtered faucets to only include integrated faucets if the countertop type is `BuiltIn` (Integrated), or standard faucets if the countertop type is `SurfaceMounted` (Standard).
  - **Getters Simplification**: Simplified offset/type getters (`GetActiveCountertopType`, `GetActiveSinkOffset`, etc.) to query resolved options list via `GetResolvedComponentOptions` and read from the pre-resolved model arrays instead of performing raw DataTable lookups.
  - **Selection Safety Guards**:
    - Clamped/reset `CountertopSizeIndex` and `FaucetSizeIndex` to `0` when the Cabinet size index changes.
    - Clamped/reset `FaucetSizeIndex` to `0` when the Countertop model changes.
  - **Visual Application Adaptation**:
    - In `ApplyProductData`, applied the countertop mesh and materials using `ActiveState.CountertopSizeIndex` instead of cabinet size index.

### 3. Widget UI & Preview Actor Adjustments
- **[ConfiguratorMainWidget.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/UI/ConfiguratorMainWidget.cpp)**:
  - Updated size selection index to query and set `Booth->ActiveState.CountertopSizeIndex` for countertops.
  - Removed the countertop-specific layout collapse check for `Size_Container` so countertop model options display in the UniformGridPanel.
- **[FurniturePreviewActor.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/FurniturePreviewActor.cpp)**:
  - Updated countertop mesh application in `LoadProductPreview` to use `ActiveState.CountertopSizeIndex`.
- **[MaxiMallPreviewController.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/MaxiMallPreviewController.cpp)**:
  - Updated `GetActiveComponentMetadata` to query countertop metadata using `CountertopSizeIndex`.

### 4. Compilation Verification
- **Clean Compiler Build**: Rebuilt successfully via `UnrealBuildTool` in development mode.
  - Result: **Successful compilation** with **0 errors and 0 warnings**.

---

## Update: Catalog Option Dropdowns and Component Attachment Hierarchy Alignment

We resolved two critical catalog loading and alignment issues:

### 1. Catalog Dropdown Names Compatibility
- **[FurnitureTypes.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Data/FurnitureTypes.cpp)**:
  - Updated `GetFaucetOptions()` and `GetMirrorOptions()` static helper functions to fallback to loading `AllowedFaucetIDs.AllowedFaucetIDs` and `AllowedMirrorIDs.AllowedMirrorIDs` respectively. This populates editor Details dropdown menu options correctly when the shared DataTables on disk are named `AllowedFaucetIDs` and `AllowedMirrorIDs` instead of `DT_SharedFaucets` and `DT_SharedMirrors`.
- **[ShowroomBooth.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.cpp)**:
  - Updated `GetResolvedComponentOptions()` fallback path to load `AllowedFaucetIDs` and `AllowedMirrorIDs` respectively if default paths are missing, resolving runtime options correctly.

### 2. Flat Component Attachment Hierarchy Alignment
- **[ShowroomBooth.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.cpp)**:
  - Modified the constructor attachment setup of `SinkMesh` and `FaucetMesh` to call `SetupAttachment(MainCabinet)` instead of `SetupAttachment(CountertopMesh)`. This aligns with the modular asset design (where meshes share the same default origin relative to the cabinet or room floor) and prevents child components from inheriting countertop scale/translation or offset shifts.
- **[FurniturePreviewActor.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/FurniturePreviewActor.cpp)**:
  - Synchronized `AFurniturePreviewActor` constructor attachment hierarchy by calling `SetupAttachment(CabinetMesh)` for `SinkMesh` and `FaucetMesh` (matching the new flat showroom booth hierarchy).

### 3. Compilation Verification
- **Clean Compiler Build**: Rebuilt successfully via `UnrealBuildTool` in development mode.
  - Result: **Successful compilation** with **0 errors and 0 warnings**.

---

## Update: Corrected Object Paths for StaticLoadObject Calls

We corrected all relative path loaders in `ShowroomBooth.cpp` to use the standard package name suffix format for `StaticLoadObject`:

### 1. Suffix Format on StaticLoadObject Paths
- **[ShowroomBooth.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.cpp)**:
  - Appended the `.Name` suffix format to package path strings in `StaticLoadObject` calls for countertops, sinks, faucets, and mirrors (e.g. `/Game/DT/DT_SharedCountertops.DT_SharedCountertops`). This ensures that if the catalog properties on the client instance are uninitialized or null, the backup loaders can resolve the assets successfully from disk, enabling the Faucet options list to correctly filter between standard and integrated models when the countertop type changes.

### 2. Compilation Verification
- **Clean Compiler Build**: Rebuilt successfully via `UnrealBuildTool` in development mode.
  - Result: **Successful compilation** with **0 errors and 0 warnings**.

---

## Update: Integrated Countertop and Faucet Visual Alignment

We resolved visual misalignments, unnatural rotations, and location offsets of integrated countertops and faucets:

### 1. Flat Component Attachment Enforcement
- **[ShowroomBooth.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.cpp)**:
  - Overrode `PostInitializeComponents` to force attachment of `SinkMesh` and `FaucetMesh` directly to `MainCabinet` keeping the world transform before capturing baseline transforms. This overrides any Blueprint-serialized parenting overrides and ensures a flat hierarchy.
- **[FurniturePreviewActor.h](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/FurniturePreviewActor.h)** & **[FurniturePreviewActor.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/FurniturePreviewActor.cpp)**:
  - Declared and implemented the `PostInitializeComponents` override to force attachment of `SinkMesh` and `FaucetMesh` directly to `CabinetMesh` keeping the world transform, maintaining symmetry between the showroom booth and preview actor.

### 2. Integrated Type Transform Recalculation (Baseline Location/Scale Anchor Retention)
- **[ShowroomBooth.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.cpp)**:
  - Updated `RecalculateDependentTransforms()`: when the countertop type is `BuiltIn` (Integrated), both `CountertopMesh` and `FaucetMesh` are placed using their baseline locations and scales (to correctly anchor to the top surface of the cabinet body and maintain XY alignment) but their baseline rotations are bypassed to use their catalog `RelativeRotation` (defaulting to identity).
- **[FurniturePreviewActor.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/FurniturePreviewActor.cpp)**:
  - Updated `LoadProductPreview()`: synchronized countertop and faucet transform matching to similarly retain baseline location/scale while bypassing baseline rotation for `BuiltIn` countertop types.

### 3. Compilation Verification
- **Clean Compiler Build**: Rebuilt successfully via `UnrealBuildTool` in development mode.
  - Result: **Successful compilation** with **0 errors and 0 warnings**.

---

## Update: Integrated Countertop and Faucet Centering and Offset Correction

We corrected the centering, location offsets, and visual shifts of the integrated (`BuiltIn`) countertops and their attached faucets:

### 1. Resetting Baseline X/Y Location Offsets
- **[ShowroomBooth.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.cpp)**:
  - In `RecalculateDependentTransforms()`, for the `BuiltIn` (integrated) countertop and faucet, we reset the baseline's X/Y translation offsets while preserving only the baseline Z-height. This prevents the standard countertop's pivot/centering offsets from polluting the built-in assets, which are designed to share a common pivot origin (X=0, Y=0) with the cabinet base.
  - The relative location is set to `(0, 0, Z) + RelativeOffset`, where `Z` is the baseline's height and `RelativeOffset` is the catalog-specified offset.
- **[FurniturePreviewActor.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/FurniturePreviewActor.cpp)**:
  - In `LoadProductPreview()`, synchronized the countertop and faucet transform matching logic to reset baseline X/Y offsets for `BuiltIn` types, matching the showroom booth behavior.

### 2. Compilation Verification
- Rebuilt the project successfully via `UnrealBuildTool` in development mode:
  ```powershell
  & "D:\Programs\Epic Games\UE_5.3\Engine\Build\BatchFiles\Build.bat" MaxiMallEditor Win64 Development -Project="C:\Users\Admin\Desktop\Aleg\UE5C++\MaxiMall\MaxiMall.uproject" -WaitMutex
  ```
  - Result: **Successful compilation** (completed in 3.39 seconds).

---

## Update: Rollback of CDO Baseline and Construction Script Changes

Due to initialization failures, CDO serialization limitations, and components falling apart/floating in the level, we rolled back the source code changes to the previous stable runtime alignment state:

### 1. Rollback Details
- **[ShowroomBooth.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.cpp)** & **[FurniturePreviewActor.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/FurniturePreviewActor.cpp)**:
  - Reverted `EnsureBaselineTransformsCaptured()` to capture baseline transforms at runtime start from the duplicated component transforms rather than querying the uninitialized/empty CDO defaults.
  - Restored early return in `RecalculateDependentTransforms()` for `!IsGameWorld()` to prevent editor construction script execution from writing uninitialized baseline data.
  - Removed countertop-specific manual attachments from `PostInitializeComponents()` and `OnConstruction()`.

### 2. Status
- Reverted code compiles clean.
- The project is restored to the state of commit `2b5a4d55d57d1b9324f7f17aa297251aea93322e`.

---

## Update: Countertop Hierarchy Enforcement

To ensure standard and integrated countertops correctly inherit the cabinet's parent coordinate space (and do not get shifted/rotated relative to the cabinet due to Blueprint serialized hierarchy overrides), we enforced flat parenting at initialization:

### 1. Attachment Changes
- **[ShowroomBooth.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.cpp)** & **[FurniturePreviewActor.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/FurniturePreviewActor.cpp)**:
  - Added `CountertopMesh` to the runtime force-attachment lists in `PostInitializeComponents()`. It attaches directly to `MainCabinet`/`CabinetMesh` keeping the world transform, aligning the countertop component hierarchy perfectly relative to the cabinet base before baseline transforms are captured.

---

## Update: Dynamic Countertop Resizing and Size Fallback Warning

We implemented dynamic countertop mesh selection based on the active cabinet size, fallback to a standard countertop mesh if an integrated model lacks the matching size, and a Russian warning popup on screen in this fallback scenario:

### 1. Data Schema Refactoring
- **[FurnitureTypes.h](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Data/FurnitureTypes.h)**:
  - Replaced `TSoftObjectPtr<UStaticMesh> Mesh` and `int32 CabinetSizeIndex` inside `FFurnitureCountertopRow` with `TArray<TSoftObjectPtr<UStaticMesh>> Sizes`. This allows designers to specify countertop meshes directly per size index (e.g. index 0 for size 80, 1 for 100, 2 for 120).

### 2. Runtime Countertop Sizing & Fallback Verification
- **[ShowroomBooth.h](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.h)** & **[ShowroomBooth.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.cpp)**:
  - Declared and replicated `bCountertopSizeFallbackActive` to track when an integrated countertop falls back to a standard countertop asset.
  - Updated `GetResolvedComponentOptions()` for countertops:
    - Queries the mesh matching `ActiveState.ActiveSizeIndex` from the countertop's `Sizes` array.
    - If the active countertop is integrated (`BuiltIn`) and lacks a mesh at `ActiveSizeIndex`, it automatically finds the first allowed standard (`SurfaceMounted`) countertop model containing a valid mesh at `ActiveSizeIndex`. It falls back to it, copying its mesh, standard `SurfaceMounted` type, relative offsets, and color options.
  - Updated `ApplyProductData()`:
    - Compares the raw catalog type with the resolved type to set `bCountertopSizeFallbackActive` dynamically.
    - If the fallback is active, since the type overrides to `SurfaceMounted`, the system automatically shows the standard sink and faucet components.
  - Updated `Server_ApplyComponentSelection_Implementation()`:
    - Prevented resetting `CountertopSizeIndex` to `0` when the cabinet size changes (`bCabinetSizeChanged == true`), preserving the active countertop model selection.

### 3. UI Warning popup binding
- **[ConfiguratorMainWidget.h](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/UI/ConfiguratorMainWidget.h)** & **[ConfiguratorMainWidget.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/UI/ConfiguratorMainWidget.cpp)**:
  - Added optional `UTextBlock` binding `Txt_Warning`.
  - Updated `RefreshSelections()`: when the booth's `bCountertopSizeFallbackActive` is true, displays the Russian warning text `"Для этой модели нет встроенной столешницы соответствующего размера"` and sets the visibility to `Visible`. Otherwise, collapses the text block.

### 4. Compilation Verification
- Rebuilt the project successfully via `UnrealBuildTool` in development mode:
  - Result: **Successful compilation** with **0 errors and 0 warnings**.

---

## Update: Robust Fallback & Warning Evaluation

We updated the fallback and warning detection logic to evaluate each countertop size index independently and handle missing sizes (either explicit `None` elements or out-of-bounds size indices) robustly:

### 1. Independent Evaluation & Out-of-Bounds Guards
- **[ShowroomBooth.h](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.h)** & **[ShowroomBooth.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.cpp)**:
  - Implemented the helper function `CalculateCountertopFallbackActive()`. It evaluates whether the raw countertop model is `BuiltIn` (Integrated) and its `Sizes` array lacks a valid mesh for `ActiveState.ActiveSizeIndex` (by checking if the active size index is out of bounds or explicitly set to `None`/empty).
  - Updated `ApplyProductData()` to calculate and replicate `bCountertopSizeFallbackActive` at the very beginning of the function, ensuring all dependent components (e.g. Faucet and Sink options resolution) utilize the up-to-date fallback state.
  - Updated `GetActiveCountertopType()` to immediately return `ECountertopType::SurfaceMounted` when `bCountertopSizeFallbackActive` is active, forcing standalone sink and faucet meshes to resolve and display correctly.

### 2. Compilation Verification
- Rebuilt the project successfully via `UnrealBuildTool` in development mode:
  - Result: **Successful compilation** (completed in 4.32 seconds).

---

## Update: Lighting Profiles, Mirror Overrides, RMB Drag UI Protection & The Golden Rule

We have implemented dynamic category-specific lighting profiles, per-row mirror overrides, right-mouse-button drag UI protection, and verified complete compilation stability.

### The Golden Rule
> [!IMPORTANT]
> **THE GOLDEN RULE**: The `awsTutorial` project currently compiles and opens perfectly on the target production computer. We must **ONLY** inject our tested `MaxiMall` code into `awsTutorial` without breaking its existing dependencies, class names, or structures, so that the code runs with zero errors on both machines. We push only the `MaxiMall` state to Git.

### 1. Preview Actor Cleanup & Optimization
- **[FurniturePreviewActor.h](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/FurniturePreviewActor.h)** & **[FurniturePreviewActor.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/FurniturePreviewActor.cpp)**:
  - Defined the `FFurniturePreviewLightingConfig` struct exposing light parameters (Intensity, Location, Cone Angles, Attenuation, Shadow Bias, Contact Shadow Length, KeyLightSourceRadius).
  - Created 6 separate categories (`CabinetLighting`, `ClosetLighting`, `CountertopLighting`, `SinkLighting`, `FaucetLighting`, `MirrorLighting`) under `"Preview Config"` in the Details panel.
  - Added the `PreviewSkyLight` (`USkyLightComponent`) to resolve black reflections on metallic/mirror surfaces in isolated viewmode.
  - Configured all components, lights, and backdrops to utilize **Lighting Channel 2** for clean studio isolation.
  - Updated the default `SinkLighting.KeyLightLocation` to `FVector(0.f, 0.f, 300.f)` to shine directly overhead, preventing inner lip crescent shadows.

### 2. Mirror Material Override
- **[FurnitureTypes.h](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Data/FurnitureTypes.h)**:
  - Added `MirrorMaterialOverride` and `MirrorMaterialSlotIndex` directly to `FFurnitureModelOption` and `FFurnitureMirrorRow`.
- **[ShowroomBooth.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.cpp)**:
  - Updated `GetResolvedComponentOptions()` for mirror type to copy override properties.
- **[FurniturePreviewActor.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/FurniturePreviewActor.cpp)**:
  - Updated `ApplyComponentMeshAndMaterials()` to dynamically apply per-row mirror material overrides to the mirror mesh.

### 3. RMB Drag UI Protection & Click Gating
- **[MaxiMallPreviewController.h](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/MaxiMallPreviewController.h)** & **[MaxiMallPreviewController.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Preview/MaxiMallPreviewController.cpp)**:
  - Overrode `AddYawInput` and `AddPitchInput` to set `bRightMouseIsDragging = true` when camera movement is registered.
  - Blocked opening of configurator UI in `ToggleConfiguratorUI` if `bRightMouseIsDragging` is active.
- **[ShowroomBooth.h](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.h)** & **[ShowroomBooth.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.cpp)**:
  - Bound components `OnClicked` delegate inside `BeginPlay()`.
  - Implemented `OnBoothComponentClicked()` to restrict UI opening strictly to `RightMouseButton` click, ignoring events when `IsRightMouseDragging()` is true. This keeps left-click interactions (like door double-clicks) working cleanly without opening the configurator UI.

### 4. Compilation Verification
- Rebuilt the project successfully via `UnrealBuildTool` in development mode:
  - Result: **Successful compilation** (completed with 0 errors/warnings).

