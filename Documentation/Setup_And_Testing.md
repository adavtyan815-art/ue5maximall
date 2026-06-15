# Technical Reference: Setup, Configuration, and Testing Guide

This document describes how to configure the Blueprint subclass, hook it up to the custom Player Controller, and verify the multi-user replication and camera orbit functionality at runtime.

---

## 1. Blueprint Subclassing & Registration

To allow design-time adjustments to the camera limits, backdrop meshes, materials, and lighting intensities, you must create a Blueprint subclass of the C++ preview actor and configure it on the Player Controller:

### A. Create the Blueprint Subclass
1. Open the Unreal Editor and navigate to the **Content Browser**.
2. Right-click and choose **Blueprint Class**.
3. Expand the **All Classes** section, search for `FurniturePreviewActor`, select it, and click **Select**.
4. Name the new Blueprint asset `BP_FurniturePreviewActor`.

### B. Configure Default Parameters
Double-click `BP_FurniturePreviewActor` to open the Blueprint Editor and modify the following defaults in the **Details Panel**:
* **Camera Clamping**:
  - `PitchMin`: `-80.f` (prevent camera from flipping upside down under the model).
  - `PitchMax`: `80.f` (prevent camera from flipping overhead).
* **Isolated Lighting Defaults**:
  - `BaseFillIntensity`: `40000.f` (starting headlight intensity in Lumens).
  - `ReferenceZoomDistance`: `250.f` (the distance baseline for headlight distance compensation).
* **Camera Post-Process Settings**:
  - Select the **Camera** component in the components hierarchy.
  - Locate **Post Process Settings** -> **Lens** -> **Exposure**.
  - To prevent adaptation flashes/delays (the 1-second dimming behavior), verify that `Min EV100` and `Max EV100` are set to identical values (e.g. `10.0` or `12.0` depending on the desired brightness of the studio scene).
  - *Note*: If you need to manually change exposure parameters, lock both min and max to the same value so that there is zero runtime adjustment delay.

### C. Register in the Player Controller
1. Find your project's custom player controller Blueprint, e.g., `BP_MaxiMallPlayerController` (which must subclass `AMaxiMallPreviewController`).
2. Open it and navigate to the **Class Defaults**.
3. Under the **MaxiMall | Preview Config** section, find the **Preview Actor Class** dropdown.
4. Select `BP_FurniturePreviewActor`.
5. Set the **Preview Staging Location** to `(0.0, 0.0, 10000.0)` or any location safe from main level visibility.
6. (Optional) Set the **Backdrop Static Mesh** and **Backdrop Material** assets to configure the studio backdrop.

---

## 2. Runtime Verification & Test Scenarios

Follow these scenarios to verify that the implementation is replication-safe and performs correctly under PIE (Play in Editor) multiplayer conditions.

### Scenario A: Multi-User Catalog Replication (Server/Client Sync)
1. In the Editor, set the Play Mode to **Play In Editor (PIE)** with **Number of Players = 2** and Net Mode set to **Play as Listen Server** or **Play as Client**.
2. Start the game.
3. Walk Player 1 up to a `AShowroomBooth` actor in the level.
4. Interact with the catalog menu and change the active product to a different ID.
5. **Verify**:
   - Player 1's local showroom booth immediately updates to display the new static meshes.
   - Player 2 (standing nearby) also immediately sees the showroom booth mesh configuration change.
   - Look at the **Output Log** to confirm the server accepted the request and replicated the change:
     ```
     [ShowroomBooth] Server_ChangeProductID called. ProductID = Cabinet_Modern_01
     [ShowroomBooth] OnRep_ActiveProductID updated on client. Rebuilding components...
     ```

### Scenario B: Isolated Local Preview Inspection
1. With Player 1, click the **Inspect/Preview** button (keyboard shortcut `F`) on a showroom booth to enter preview mode.
2. The UI widget showing the RenderTarget will open.
3. **Verify**:
   - The scene transition is instant, without a 1-second adaptation delay where the screen goes from bright white/blown-out to normal (due to the locked `AutoExposureMin/MaxBrightness` configurations).
   - Only Player 1 sees the preview scene. Player 2's screen and position are completely unaffected.
   - Walk Player 2 near Player 1's character. Verify Player 2 does *not* see any floating preview lights, backdrops, or extra cabinet meshes, as they are spawned with `bReplicates = false` far above the level.
   - Check the **Output Log** for the controller's spawn log:
     ```
     [PreviewController] OpenFurniturePreview spawning class: BP_FurniturePreviewActor_C
     ```

### Scenario C: Camera Orbit, Zoom, and Headlight Intensity Compensation
1. Inside the preview UI, click and drag with the mouse to orbit around the furniture model.
2. **Verify**:
   - The model rotates smoothly according to mouse movements, staying within the configured `PitchMin` and `PitchMax` boundaries.
3. Use the mouse wheel to zoom in close to the model and zoom far out.
4. **Verify**:
   - As you zoom in, the lighting intensity on the cabinet face does not blow out.
   - As you zoom out, the model does not go completely dark.
   - This indicates that the distance-compensation math formula $I_{\text{emitted}} = I_{\text{base}} \times (d / d_{\text{ref}})^2$ is correctly neutralizing the inverse-squared falloff of the headlight.
