# MaxiMall Constructor & Room Planner Implementation Summary

## Overview
This document summarizes the changes, fixes, and architecture implemented for the **MaxiMall Furniture Configurator & Room Planner** module (`Source/MaxiMall/Constructor/`).

---

## Key Features & Implementations

### 1. Procedural 3D Wall Mesh Generation (`AProceduralWallActor` & `ARoomPlannerManager`)
- **Mitered Corner Offsets**: `ARoomPlannerManager::ComputeMiterOffsetsAtNode` computes exact corner intersection offsets for connected walls to eliminate overlapping geometry at wall joints.
- **Dynamic Door & Window Openings**: Generates procedural quad sections around wall openings (doors, windows) with custom sill heights and dimensions.
- **Instant Collision Cleanup on Deletion**: `AProceduralWallActor::EndPlay` clears procedural mesh sections and sets `SetCollisionEnabled(ECollisionEnabled::NoCollision)` before actor destruction, ensuring physics colliders disappear instantly without lingering in the level.

### 2. Multi-Unit Opening Dimension Parser (`URoomPlannerWidget`)
- **Universal Dual-Unit Parser (`ParseLengthDimensionInput`)**:
  - Values entered as plain numbers $> 10$ (e.g., `30`, `50`, `90`, `120`) are automatically parsed as **centimeters** and converted to meters.
  - Values $\le 10$ or strings with explicit `"m"` suffixes (e.g., `0.9`, `2.1m`) are parsed as **meters**.
  - Ensures intuitive input handling for both door and window width, height, and sill height controls.

### 3. Room Cycle Detection & Floor Polygon Triangulation
- **Graph Cycle Detection (`RebuildRooms()`)**: Performs Depth-First Search (DFS) traversal over node adjacencies (`Nodes` & `WallSegments`) to detect closed room loops.
- **Centroid Angular Sorting**: Vertices of detected room polygons are sorted counter-clockwise around the room centroid (`FMath::Atan2(Y - Centroid.Y, X - Centroid.X)`) to guarantee clean fan triangulation without degenerate triangles.
- **Shoelace Area Calculation**: Computes exact floor surface area in $m^2$ and updates HUD readout (`TOTAL FLOOR AREA`).

### 4. Multiplayer Replication & RPC Fixes
- Added all required `_Implementation` and `_Validate` RPC functions on `ARoomPlannerManager`:
  - `Server_SetWallLength_Implementation` & `Server_SetWallLength_Validate`
  - `Server_RemoveWall_Implementation` & `Server_RemoveWall_Validate`
  - `Server_AddOpeningToWall_Implementation` & `Server_AddOpeningToWall_Validate`
  - `Server_UpdateOpeningDimensions_Implementation` & `Server_UpdateOpeningDimensions_Validate`
  - `Server_DeleteSelectedWall_Implementation` & `Server_DeleteSelectedWall_Validate`
- Implemented `SetWallLength` and `DeleteWallAtWorldPos` in `ARoomPlannerManager.cpp`, resolving all `unresolved external symbol` linker errors.

### 5. WebRTC / PixelStreaming Command Handler
- Supported `ProcessCommandJSON` for external remote control over PixelStreaming data channels (`add_wall`, `add_opening`, `clear`, `get_state`).

### 6. UI Dynamic Properties & View Mode Management (`URoomPlannerWidget`)
- **2D/3D State Separation**: Automatically collapses creation tools and dynamic property panels when switching to 3D Orbit view, and reveals them only in 2D Mode.
- **Dynamic Property Block**: Hides and shows contextual buttons (e.g., `BtnAddDoor`, `BtnApplyProperties`) alongside layout line dividers (`Image_1`, `Image_2`) based exactly on whether a wall or opening is currently selected.
- **State Initialization**: Correctly initializes UI visibilities in `NativeConstruct` to prevent layout bugs when the widget defaults to 3D mode.
