# Technical Reference: Setup, Configuration, and Testing Guide

This document describes how to configure the Blueprint widgets, connect them to our Player Controller, and verify the multi-user replication, UI gating, and isolated Viewmode camera orbit functionality at runtime.

---

## 1. Blueprint Subclassing & Widget Binding Reparenting

To eliminate manual Blueprint graph event wiring and property polling, you must reparent the Blueprint Widgets and register them with the custom Player Controller:

### A. Reparent Widget Blueprints
1. Locate your main configurator User Widget Blueprint (e.g. `WBP_PreviewWindow`) in the **Content Browser**.
2. Double-click to open it. Go to **File** -> **Reparent Blueprint**.
3. Choose `ConfiguratorMainWidget` as the new parent class.
4. Reparent the Viewmode overlay User Widget Blueprint (e.g. `WBP_ViewmodeOverlay`) to `ViewmodeOverlayWidget`.

### B. Visual Component Naming (BindWidget Mapping)
Verify that the visual components (buttons, combo boxes, text blocks) inside the designer match the C++ variable names **exactly** (case-sensitive) to bind automatically:
* **`WBP_PreviewWindow` (inheriting from `UConfiguratorMainWidget`)**:
  * `Btn_Viewmode` (Button)
  * `Btn_CloseUI` (Button)
  * `Txt_BtnURL` (Button)
  * `Size_Container` (PanelWidget)
  * `Color_Container` (PanelWidget)
  * `Txt_SKU` (TextBlock)
  * `Txt_ProductName_1` (TextBlock)
  * `Txt_Warning` (TextBlock, optional)
* **`WBP_ViewmodeOverlay` (inheriting from `UViewmodeOverlayWidget`)**:
  * `Btn_Back` (Button)

### C. Register Widgets in the Player Controller
1. Find your project's custom player controller Blueprint, e.g., `BP_MaxiMallPlayerController` (which must subclass `AMaxiMallPreviewController`).
2. Open it and navigate to the **Class Defaults**.
3. Locate the **MaxiMall | UI Config** section.
4. Assign:
   * **Main Widget Class**: `WBP_PreviewWindow`
   * **Viewmode Overlay Class**: `WBP_ViewmodeOverlay`
5. Locate the **MaxiMall | Preview Config** section.
6. Set:
   * **Preview Actor Class**: `BP_FurniturePreviewActor`
   * **Preview Staging Location**: `(0.0, 0.0, 10000.0)`
   * **Backdrop Static Mesh** and **Backdrop Material** assets to configure the studio backdrop.

### D. Clean up Blueprint Input Bindings
1. Open the blueprint [BP_MaxiMallPlayerController](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Content/BP_MaxiMallPlayerController.uasset).
2. Go to the **Event Graph**.
3. **Delete or disconnect** the `Left Mouse Button` event node. Double-click detection is now handled automatically by the native C++ parent class `AMaxiMallPreviewController`, so removing this blueprint node prevents input consumption conflicts.
4. Compile and Save the Blueprint.

---

## 2. Runtime Verification & Test Scenarios

Follow these scenarios to verify that the implementation is replication-safe, handles dynamic layout gating correctly, and performs cleanly under PIE (Play in Editor) multiplayer conditions.

### Scenario A: Main World Right-Click & Real-Time Replication
1. Run a multiplayer session with **2 players** (e.g. NetMode: Client 1 and Server).
2. Walk Player 1 up to a showroom booth in the level.
3. **Right-Click** on a specific subcomponent (such as the Cabinet).
4. **Verify**:
   * **Widget 1 (Main Configurator UI)** opens immediately on Player 1's screen.
   * Mouse cursor becomes visible, and input mode focuses on the UI.
5. In the UI combo boxes, select a different color or size.
6. **Verify**:
   * Player 1's showroom booth immediately updates to display the new meshes/materials.
   * Player 2 (standing nearby) also immediately sees the showroom booth mesh configuration change.
   * No staging buffers, local cache, or "Commit" button is involved. Updates replicate instantly.

### Scenario B: Dynamic Component Gating (Optional Meshes Collapse)
1. Interact with a showroom booth in the level that has optional components missing or set to null static meshes (for example, a booth without a `MirrorMesh` component).
2. **Verify**:
   * In **Widget 1**, the Mirror color and size options/rows are automatically set to `ESlateVisibility::Collapsed` (hidden).
3. Now interact with a showroom booth that includes a valid `MirrorMesh`.
4. **Verify**:
   * The Mirror option size/color dropdowns are fully visible (`ESlateVisibility::Visible`) and populated.

### Scenario C: Door/Drawer Double-Click Toggle
1. Approach a showroom booth that has doors (e.g., `OneDoor` or `TwoDoors` layout) or drawers.
2. **Double-Click** on one of the door/drawer components.
3. **Verify**:
   * The door/drawer component animates open or closed smoothly over approximately 1 second.
   * Hinge doors rotate open (up to `OpenYawDelta` degrees), while drawers slide open translationally (displacing by `OpenTranslationOffset` units) based on their catalog configuration.
   * The updated state replicates to other clients, showing the animated state on Player 2's screen.

### Scenario D: True Isolated Viewmode & UI Transition
1. Open the main configurator UI with Player 1.
2. Click the **Viewmode** button.
3. **Verify**:
   * Player 1's camera transitions instantly to the isolated staging studio (`SetViewTargetWithBlend`).
   * **Widget 1** is removed from Player 1's screen, and **Widget 2 (Viewmode Overlay)** is displayed.
   * **Widget 2** contains no configuration UI (no colors, sizes), just a clean "Back" overlay.
   * Only the targeted booth component (e.g. Cabinet) is visible in the preview scene. All other booths are hidden locally for Player 1.
   * Player 2's screen is completely unaffected. All booths remain fully visible on Player 2's screen.
4. Click the **Back** button on Widget 2.
5. **Verify**:
   * Player 1's camera returns to the character's main world position.
   * Widget 2 is removed, and **Widget 1 (Main Configurator UI)** is re-opened, restoring focus and cursor state without losing the active booth target.
   * All other showroom booths in the level become visible again on Player 1's screen.

### Scenario E: Countertop Dynamic Resizing and Fallback Warning
1. Open the DataTable `DT_SharedCountertops` and configure an integrated (`BuiltIn`) countertop model to only have meshes for size 100 (index 1) and size 120 (index 2), leaving size 80 (index 0) empty. Ensure a standard (`SurfaceMounted`) countertop model contains a valid mesh for size 80.
2. In the level, interact with a showroom booth configured with this integrated countertop model.
3. Switch the Cabinet size to 100 or 120.
   * **Verify**: The countertop updates to its integrated model mesh, hiding the standalone sink and faucet. No warning is displayed.
4. Now, switch the Cabinet size to 80.
   * **Verify**:
     * The booth automatically falls back to the standard countertop model's size 80 mesh.
     * The standalone sink and faucet are rendered at their correct baseline transforms.
     * The Russian warning text block `Txt_Warning` pops up on the screen, stating `"Для этой модели нет встроенной столешницы соответствующего размера"`.
5. Switch the Cabinet size back to 100 or 120.
   * **Verify**: The warning message disappears, and the integrated countertop is restored.

