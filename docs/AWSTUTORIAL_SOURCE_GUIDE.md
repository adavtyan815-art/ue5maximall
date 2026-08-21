# awsTutorial C++ Source Code Architecture Guide

> **Module Root**: `Source/awsTutorial/` (42 functional C++ source files)  
> **Scope**: This document covers **exclusively** the C++ source code under `Source/awsTutorial/`.  

---

## Table of Contents
1. [Source Tree Structure](#1-source-tree-structure)
2. [Module Entry Point](#2-module-entry-point)
3. [Player Controller Subsystem](#3-player-controller-subsystem)
4. [Furniture Configurator Subsystem](#4-furniture-configurator-subsystem)
5. [Procedural Room Constructor Subsystem](#5-procedural-room-constructor-subsystem)
6. [Color & Material Catalog Subsystem](#6-color--material-catalog-subsystem)
7. [Save/Load & HTTP REST Subsystem](#7-saveload--http-rest-subsystem)
8. [External Contracts Implemented in C++](#8-external-contracts-implemented-in-c)
9. [C++ Class Guidelines: Modifying & Adding Code](#9-c-class-guidelines-modifying--adding-code)

---

## 1. Source Tree Structure

```
Source/awsTutorial/
├── awsTutorial.Build.cs                  # Module dependency declarations
├── awsTutorial.cpp / .h                  # Primary game module lifecycle
├── awsTutorial_PlayerController.cpp / .h  # Core interactive controller & Pixel Streaming hooks
├── awsTutorial_LoginPlayerController.cpp / .h # Authentication & landing controller
├── ColorCatalog/                         # Material & RAL palette customization subsystem
│   ├── ColorCatalogItemObject.h          # UObject wrapper for UMG ListView data bindings
│   ├── ColorCatalogSubsystem.cpp / .h    # GameInstance subsystem managing swatches & finishes
│   ├── ColorCatalogSwatchWidget.cpp / .h # Individual color swatch button widget
│   ├── ColorCatalogTypes.h               # Data structures & enums for material options
│   ├── ColorCatalogWidget.cpp / .h       # Primary color catalog UI panel
│   └── doc/ColorCatalogSystem.md         # Subsystem reference document
├── Constructor/                          # Procedural room planning subsystem
│   ├── ProceduralWallActor.cpp / .h      # Dynamic procedural mesh generation for walls
│   ├── RoomPlannerManager.cpp / .h       # 2D/3D grid planner, wall snapping & layout manager
│   └── RoomPlannerTypes.h                # Structs and enums for room geometry
└── FurnitureConfigurator/                # Showroom booth & interactive furniture preview
    ├── BoothInteractionInterface.h       # Interface for booth selection & raycast events
    ├── ShowroomBooth.cpp / .h            # Interactive showroom display booth actor
    ├── Data/
    │   └── FurnitureTypes.cpp / .h       # Data structures for booth configuration states
    ├── Preview/
    │   └── FurniturePreviewActor.cpp / .h # Isolated 3D studio preview actor
    └── UI/
        ├── ConfiguratorMainWidget.cpp / .h # Primary furniture customization UI
        ├── FurnitureGridItemWidget.cpp / .h # Catalog item tile widget
        ├── FurnitureSizePillWidget.cpp / .h # Dimension selector pill widget
        ├── RoomPlannerWidget.cpp / .h     # Room planner tool panel
        ├── SaveHistoryItemWidget.cpp / .h # Individual save history card widget
        ├── SaveSystemWidget.cpp / .h      # Save/Load REST dialog & screenshot manager
        └── ViewmodeOverlayWidget.cpp / .h # Viewmode camera navigation overlay
```

---

## 2. Module Entry Point

### `awsTutorial.Build.cs`
- **Module Type**: Primary runtime gameplay module (`awsTutorial`).
- **Dependencies**:
  - `Core`, `CoreUObject`, `Engine`, `InputCore`, `EnhancedInput` (Core Unreal runtime)
  - `UMG`, `Slate`, `SlateCore` (User interface)
  - `ProceduralMeshComponent` (Runtime mesh generation for walls)
  - `PixelStreaming`, `PixelStreamingInput` (WebRTC video streaming & data channels)
  - `HTTP`, `Json`, `JsonUtilities` (REST API communication and payload serialization)
  - `ImageCore` (Screenshot compression and Base64 thumbnail generation)

### `awsTutorial.cpp` / `awsTutorial.h`
- Implements `FDefaultGameModuleImpl` via `IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, awsTutorial, "awsTutorial")`.

---

## 3. Player Controller Subsystem

### `AAwsTutorial_PlayerController`
- **Responsibilities**:
  - Primary gameplay controller for camera movement, booth selection, and tool interaction.
  - Supports dual camera modes: 3D Perspective orbiting and 2D Orthographic top-down planning.
  - Manages Pixel Streaming input binding late-binding retries (`AddUniqueDynamic` in `PlayerTick`).
  - Emits Pixel Streaming responses over data channels.
- **Important Functions**:
  - `SendOpenURLToBrowser(const FString& URL)`: Dispatches `open_url: <URL>` message across the Pixel Streaming data channel to open external browser tabs.
  - `SelectComponent(UPrimitiveComponent* ComponentToSelect)`: Manages primitive component selection and outline highlights.
  - `SetupInputComponent()`: Binds enhanced input actions for pan, zoom, orbit, and click events.

### `AAwsTutorial_LoginPlayerController`
- **Responsibilities**:
  - Lightweight player controller for landing viewports and authentication overlays.

---

## 4. Furniture Configurator Subsystem

### `AShowroomBooth`
- **Responsibilities**:
  - Interactive booth actor placed in the 3D showroom.
  - Implements `IBoothInteractionInterface` for selection, highlight outline rendering, and theme swapping.
  - Tracks configuration state (`FShowroomBoothConfigState`):
    - `ProductID`: Identifier for the active furniture collection.
    - Component indices: `ActiveSizeIndex`, `ActiveColorIndex`, `CountertopSizeIndex`, `ActiveCountertopColorIndex`, `ClosetSizeIndex`, `ClosetColorIndex`, `SinkSizeIndex`, `SinkColorIndex`, `FaucetSizeIndex`, `FaucetColorIndex`, `MirrorSizeIndex`, `MirrorColorIndex`.
- **Important Functions**:
  - `ApplyConfigState(const FShowroomBoothConfigState& NewState)`: Updates meshes and material parameters across all subcomponents.
  - `OnBoothClicked()`: Triggers UI opening and camera focus.

### `AFurniturePreviewActor`
- **Responsibilities**:
  - Spawns isolated 3D furniture meshes in real-time within the studio preview stage.
  - Dynamically overrides Material Instance parameters (BaseColor, Roughness, Metallic, Normal strength).

### Configurator UI Widgets
- **`UConfiguratorMainWidget`**: Root customization panel managing category navigation, size selectors, and color pickers.
- **`UFurnitureGridItemWidget`**: Individual item tile displaying furniture icons and selection states.
- **`UFurnitureSizePillWidget`**: Dimension selector pill button (e.g. 60cm, 80cm, 100cm).
- **`UViewmodeOverlayWidget`**: UI overlay handling camera transitions and viewmode presets.

---

## 5. Procedural Room Constructor Subsystem

### `URoomPlannerManager`
- **Responsibilities**:
  - Manages 2D/3D grid coordinates, wall snapping, dimensional calculations, and furniture placement.
  - Parses JSON commands received via Pixel Streaming data channels (`ProcessCommandJSON`) and dispatches responses prefixed with `MaxiMallConstructor:`.
- **Important Functions**:
  - `GenerateRoom(const FRoomLayoutData& Layout)`: Calculates polygon contours and instantiates wall segments.
  - `SpawnWallActor(FVector Start, FVector End, float Height, float Thickness)`: Spawns `AProceduralWallActor` instances dynamically in the world.

### `AProceduralWallActor`
- **Responsibilities**:
  - Procedurally generates dynamic 3D wall geometry with custom thickness, height, cutouts for doors/windows, and material slot mapping using `UProceduralMeshComponent`.
- **Important Functions**:
  - `GenerateWallMesh()`: Builds vertices, triangles, normals, UVs, and tangents for front, back, and cap faces.

---

## 6. Color & Material Catalog Subsystem

### `UColorCatalogSubsystem`
- **Responsibilities**:
  - `UGameInstanceSubsystem` indexing available material swatch collections, RAL color palettes, and surface finish presets.
- **Important Functions**:
  - `GetSwatchesByCategory(FName Category)`: Returns arrays of color definitions and material instance references.

### `UColorCatalogWidget` & `UColorCatalogSwatchWidget`
- **Responsibilities**:
  - Interactive color catalog UI panel and individual swatch button items.
  - `UColorCatalogItemObject`: `UObject` wrapper for populating UMG `UListView` components.

---

## 7. Save/Load & HTTP REST Subsystem

### `USaveSystemWidget`
- **Responsibilities**:
  - Manages room design save/load dialogs, thumbnail capture, history scrolling, and backend REST communication.
- **Serialization Flow**:
  1. Gathers all `AShowroomBooth` actors in the world.
  2. Constructs JSON payload containing: `username`, `saveId`, `saveName`, `date`, `thumbnail`, and `boothStates` array.
- **Screenshot Capture Flow**:
  1. Dispatches `FScreenshotRequest::RequestScreenshot(false)`.
  2. Intercepts viewport frame in `OnScreenshotCapturedHandler`.
  3. Center-crops to $256 \times 256$ square, compresses PNG bytes via `FImageUtils::CompressImageArray`, and encodes to Base64 via `FBase64::Encode`.
- **HTTP REST Endpoints**:
  - `GET /api/saves/:username`: Retrieves array of saved configurations for the active user.
  - `POST /api/saves`: Uploads serialized configuration and Base64 thumbnail.
  - `DELETE /api/saves/:username/:saveId`: Deletes a specific save record.

---

## 8. External Contracts Implemented in C++

1. **Backend URL Routing**:
   - `USaveSystemWidget::GetBackendBaseURL()` parses `-BackendURL=<URL>` from `FCommandLine::Get()`, defaulting to local/configured backend.
2. **Pixel Streaming URL Navigation**:
   - `AAwsTutorial_PlayerController::SendOpenURLToBrowser(const FString& URL)` transmits `open_url: <URL>` over the WebRTC data channel.
3. **Room Constructor Data Channel Protocol**:
   - `AAwsTutorial_PlayerController` forwards incoming JSON commands to `URoomPlannerManager::ProcessCommandJSON` and sends responses formatted as `MaxiMallConstructor:<JSON>`.

---

## 9. C++ Class Guidelines: Modifying & Adding Code

### 9.1 Modifying Existing Classes
- Preserve public and Blueprint-callable API signatures (`UFUNCTION(BlueprintCallable)`).
- Ensure all pointer references are validated before dereferencing (`IsValid()`, null-checks).
- Use Unreal Engine standard logging macros (`UE_LOG(LogTemp, Warning, ...)`).

### 9.2 Adding New Classes
1. Place `.h` and `.cpp` in `Source/awsTutorial/` or the appropriate subsystem folder.
2. Ensure the class uses the `AWSTUTORIAL_API` export macro:
   ```cpp
   #pragma once
   #include "CoreMinimal.h"
   #include "GameFramework/Actor.h"
   #include "MyNewActor.generated.h"

   UCLASS()
   class AWSTUTORIAL_API AMyNewActor : public AActor
   {
       GENERATED_BODY()
   public:
       AMyNewActor();
   };
   ```

---
*Document Version: 1.0.0 — Canonical C++ Source Guide for Source/awsTutorial/*
