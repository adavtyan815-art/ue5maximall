# Technical Reference: MaxiMall Furniture Configurator Architecture

This document provides a technical overview of the data-driven showroom booth, C++ widget bindings, real-time replication model, and isolated viewmode system.

---

## 1. Dynamic Catalog & Data-Driven Meshes

The configurator is fully data-driven, storing all furniture products inside a central DataTable (`DT_FurnitureCatalog`) mapped to the `FFurnitureProductRow` structure (defined in [FurnitureTypes.h](file:///c:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Data/FurnitureTypes.h)).

Each furniture product consists of up to seven individual visual subcomponents applied to corresponding `UStaticMeshComponent` slots at runtime:
1. **Cabinet Body** (base static mesh + size/material configurations)
2. **Door Slot 0** (left/single door, relative offsets, and material overrides)
3. **Door Slot 1** (right door, relative offsets, and material overrides)
4. **Countertop**
5. **Sink** (visibility depends on Countertop selection type)
6. **Faucet**
7. **Mirror** (optional subcomponent)
8. **Closet** (optional subcomponent)

### Dynamic Component Mesh Gating
Showroom booths (`AShowroomBooth`) are designed to be visually flexible and dynamic. A booth class variant is NOT required to have all subcomponents compiled together. If optional subcomponents (like the `MirrorMesh` or `ClosetMesh`) lack a static mesh (or are null) on the targeted booth, the C++ UI backend automatically collapsing their respective rows/options:
* **Gating Method**: `UConfiguratorMainWidget::IsComponentMeshValid` checks the mesh component's validity and calls `GetStaticMesh()`.
* **Visibility Collapse**: If invalid, the corresponding combo box selectors or row layout visibility is updated to `ESlateVisibility::Collapsed` in C++, hiding them cleanly from the designer's UI layout.

---

## 2. C++ Widget Bindings & Layout Integration

We use the C++ UMG Widget Binding workflow to bypass manual Blueprint graph event wiring and property polling:

* **Strict Bindings (`meta = (BindWidget)`)**: Core UI elements are marked strict, ensuring compilation fails if these elements are missing in the Blueprint Widget.
  * `Txt_ProductName` (UTextBlock) - Displays the active product name.
  * `Btn_Viewmode` (UButton) - Transition to isolated viewmode.
  * `Btn_CloseUI` (UButton) - Close configurator UI and return to Game input mode.
  * `Btn_Back` (UButton, on Widget 2) - Exit isolated viewmode and return to main UI.
* **Optional Bindings (`meta = (BindWidgetOptional)`)**: Selector combo boxes are marked optional, giving designers visual layout flexibility.
  * Generic selectors: `Combo_Size` and `Combo_Color` for the active right-clicked component.
  * Component-specific selectors: `Combo_Cabinet_Size`, `Combo_Cabinet_Color`, `Combo_Closet_Size`, `Combo_Closet_Color`, etc., which drive a full configuration table.

These base classes are implemented in:
* **Widget 1 (Main Configurator UI)**: [ConfiguratorMainWidget.h](file:///c:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/UI/ConfiguratorMainWidget.h) / [ConfiguratorMainWidget.cpp](file:///c:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/UI/ConfiguratorMainWidget.cpp)
* **Widget 2 (Isolated Viewmode Overlay)**: [ViewmodeOverlayWidget.h](file:///c:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/UI/ViewmodeOverlayWidget.h) / [ViewmodeOverlayWidget.cpp](file:///c:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/UI/ViewmodeOverlayWidget.cpp)

---

## 3. Interaction Flow & Input Models

We support two distinct interaction modes to drive booth configurations and state changes:

1. **Main World Interaction (Real-Time Configuration)**:
   * **Trigger**: The player approaches a booth and right-clicks on a specific subcomponent (such as the Faucet or Sink).
   * **Behavior**:
     * `AMaxiMallPreviewController::TraceFurnitureComponent` determines which booth and component were clicked.
     * The player controller opens **Widget 1 (Main Configurator UI)** in the main world.
     * Selecting sizes/colors in Widget 1 instantly fires authoritative server RPCs to update the booth state in real-time, replicating the visual changes to all players.
2. **Door/Drawer Toggle**:
   * **Trigger**: Double-clicking on a door or drawer component in the main world.
   * **Input Binding**: Handled natively in C++ inside `AMaxiMallPreviewController::SetupInputComponent` by binding the `Left Mouse Button` pressed state. Click delta time is monitored to filter out double-clicks, bypassing potential Blueprint mouse-click consumption blocks.
   * **Behavior**: Fires `Server_ToggleDoor` RPC to open/close the door or drawer mesh.
   * **Visual Movement & Animation**:
     * Visuals are driven by DataTable configuration (`bIsRotation` for swinging hinge doors vs `false` for sliding translational drawers).
     * Smooth movement is animated using an optimized repeating timer loop (`FTimerManager`) running at 60 FPS in C++ (`AShowroomBooth::UpdateDoorAnimation`). This avoids Unreal's class default object (CDO) serialization bugs where `bCanEverTick` from C++ constructors fails to propagate to pre-existing Blueprints.
     * The door/drawer interpolates relative location/rotation over approximately **1 second** using `FMath::VInterpTo` and `FMath::RInterpTo` with an interpolation speed of `5.0f`.
     * Once all active animations complete, the timer automatically clears itself to conserve CPU cycles.

---

## 4. Dual-Widget Flow & True Isolated Viewmode

When the user enters the isolated inspection environment, the UI and viewport transition through a structured lifecycle:

1. **Entering Viewmode**:
   * **Trigger**: Clicking the "Viewmode" button inside Widget 1.
   * **UI Transition**: Widget 1 is removed from the screen, and **Widget 2 (Viewmode Overlay)** is added. Widget 2 displays only a "Back" button, with no color/size selectors.
   * **View Isolation**:
     * Spawns a client-side `AFurniturePreviewActor` at an off-camera staging location (`(0, 0, 10000)`) and copies the active product configuration onto it.
     * Restricts the viewport target to the private preview camera (`SetViewTargetWithBlend`).
     * Hides all other global showroom booths locally for the inspecting client only using `APlayerController::HiddenActors`. This ensures booths remain fully visible for other players in the session.
2. **Exiting Viewmode**:
   * **Trigger**: Clicking the "Back" button inside Widget 2.
   * **UI Restoration**: Widget 2 is removed. The player controller restores **Widget 1** to the viewport, and calls `SetupWidget` to re-bind it to the active booth without losing target configuration state.
   * **Resetting State**:
     * Restores view target to player pawn.
     * Clears `HiddenActors` to restore visibility of all showroom booths.
     * Restores mouse cursor focus and Game/UI input mode.
     * Destroys the private `AFurniturePreviewActor`.

---

## 5. Multiplayer Replication Model

Replication is handled in C++ inside the `AShowroomBooth` class ([ShowroomBooth.h](file:///c:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.h) / [ShowroomBooth.cpp](file:///c:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.cpp)):

* **Authoritative Replicated State**:
  ```cpp
  UPROPERTY(ReplicatedUsing = OnRep_ActiveState, BlueprintReadOnly, Category = "Booth | State")
  FShowroomBoothConfigState ActiveState;

  UPROPERTY(ReplicatedUsing = OnRep_DoorStates, BlueprintReadOnly, Category = "Booth | State")
  TArray<EDoorSlotState> DoorStates;
  ```
* **Server Authority RPCs**: When client UI interactions occur, they invoke authoritative Server RPCs:
  * `Server_ApplyProductChange(FName NewProductID)`
  * `Server_ApplyComponentSelection(EFurnitureComponentType ComponentType, FName SizeID, FName ColorID)`
  * `Server_ToggleDoor(int32 SlotIndex)`
* **Replication Sync (RepNotify)**:
  * Updating properties on the server replicates them down to all clients, triggering `OnRep_ActiveState()` or `OnRep_DoorStates()`.
  * Clients apply the static meshes, offsets, and materials locally (`ApplyProductData()`).
  * The local player controller listens to `OnProductChanged` events, auto-refreshing the active configurator combo selections in real-time.

