# Room-Scoped Save/Load System Design: Phase 1 & 2

This document details the architectural recommendations, user experience (UX) optimizations, and feature roadmap for integrating save/load capabilities into the multiplayer, room-based (Public/Private) setup of the MaxiMall configurator.

---

## 1. Saving Gating: Public vs. Private Rooms

### The Problem with Public Saves
Allowing players to save or load layout configurations inside a **Public Room** causes significant issues:
1. **Visual Interference**: If User A loads a saved kitchen layout in a public space, the showroom booths will instantly rebuild, suddenly changing the environment for User B and User C who are shopping nearby.
2. **State Pollution**: A player might click "Save" in a public room and inadvertently capture modifications made by other concurrent players.

### Recommendation: Private Room Gating (Owner Authoritative)
* **Gated Saves (100% Agreed)**: Saving and loading operations should be strictly disabled in Public Rooms. The UI button to Save/Load must be hidden or disabled with a tooltip indicating: *"Saving is only available in Private Rooms."*
* **Room-Owner Authority**: In Private Rooms, only the **Room Creator** (the player hosting the room who generated the code) should be authorized to execute `Save` and `Load` commands. If guests in the room were allowed to load states, they could overwrite the host's ongoing configurations without warning.
* **Server Verification Gating**: The server must enforce this in the C++ RPC validation:
  ```cpp
  bool AAwsTutorial_PlayerController::Server_RequestSave_Validate(const FString& SaveName)
  {
      // 1. Verify player is in a Private Room
      // 2. Verify player is the Room Owner (Host)
      return IsInPrivateRoom() && IsRoomOwner();
  }
  ```

---

## 2. Phase 1: Save/Load UI & UX Recommendations

### A. UI Button vs. Keyboard Shortcut
* **Recommendation: Dedicated UMG Widget Buttons**:
  In Pixel Streaming, **avoid relying on standard keyboard shortcuts** (like `Ctrl+S` or `Ctrl+L`) for critical UI actions:
  1. **Browser Conflicts**: `Ctrl+S` will trigger the browser's native "Save Webpage As..." dialog, and `Ctrl+O`/`Ctrl+L` will trigger page/address bar overrides.
  2. **Mobile Compatibility**: Pixel Streaming is frequently loaded on tablets and mobile phones which do not have physical keyboards.
* **UX Solution**: Include a clean, premium floating **Menu Bar** in your HUD (e.g. a gear or folder icon in the top corner) that expands to show a **"Save Design"** and **"Load Design"** button.

### B. Optimized Save/Load Flow

To avoid typing collisions and case-sensitivity errors (e.g. a user writing `save_1` then `Save_1` and creating duplicates), implement a **Save Slot Card System**:

```
+-------------------------------------------------------------+
|                       SAVE / LOAD DESIGNS                   |
+-------------------------------------------------------------+
|  [ + Save As New Design ] <--- Opens Text Input Dialog      |
|                                                             |
|  EXISTING SAVES:                                            |
|  +-------------------------------------------------------+  |
|  | Modern Black Loft     | Updated: 02/07/26 | [Load]    |  |
|  | SKU Count: 14 items   | Slot: 1           | [Save]    |  |
|  +-------------------------------------------------------+  |
|  | Scandinavian Kitchen  | Updated: 30/06/26 | [Load]    |  |
|  | SKU Count: 8 items    | Slot: 2           | [Save]    |  |
|  +-------------------------------------------------------+  |
+-------------------------------------------------------------+
```

1. **Saving Flow**:
   * Player clicks the **"Save Design"** button.
   * A dialog opens with two sections:
     * **Save New**: A text field to type a name, e.g. `Black Kitchen`, and a `[Create]` button.
     * **Overwrite Existing**: A list of their current saves. Clicking `[Save]` next to an existing slot overwrites the configuration in that slot.
2. **Loading Flow**:
   * Player clicks **"Load Design"**.
   * The UE Server queries the database via HTTP for the player's saved configurations.
   * A scrollbox lists all saved slots, including the **Design Name**, **Creation Date**, and a **Total Item Count** (derived from the configuration array).
   * Clicking a slot's card triggers the server-side load RPC.

## 3. Phase 2: Advanced Feature & Optimization Roadmaps

Once Phase 1's core level-wide serialization is functional, Phase 2 should focus on data footprint optimization, modular reusability, and layout cataloging.

### A. Modular Assembly Presets (Individual Booth Saves)
* **The Concept**: Instead of forcing a user to save and load the entire showroom level, allow them to save a *single specific booth* layout (e.g., "Charcoal Vanity with Gold Accent") as a reusable template.
* **Benefits & Implementation**:
  * **UX Reusability**: The player can configure a layout they like on Booth 1, save it, and then load/duplicate it onto Booth 3 to compare setups side-by-side or populate their custom room quickly.
  * **Zero Latency & Tiny Footprint**: The payload is just the `FShowroomBoothConfigState` of one booth (a few bytes), avoiding level-wide database writes.
  * **Favorites Library**: Users can manage a personal tray of "Booth Templates" and drag-and-drop them to any booth.

### B. Delta-Only Sparse Global Saving (Database Optimization)
* **The Concept**: Optimize level-wide saving by only serializing booths that the player has actually modified.
* **Benefits & Implementation**:
  * **Avoid Monolithic Bloat**: Currently, if there are 20 booths but the user only changed 1 kitchen, Phase 1 serializes all 20 default states.
  * **Implementation**: Add a boolean `bIsModified` to the booth class, set to `true` whenever an RPC modifies it. When saving the level, only include entries where `bIsModified == true`.
  * **Loading**: Any booth omitted in the load payload is automatically reset to its level-default catalog state.

### C. Component & Finish "Favorites" List
* **The Concept**: Add a "Favorite" star button next to specific materials, colors, or faucet models within the configurator UMG panel.
* **Benefits & Implementation**:
  * **Quick Styling**: If a designer is configuring multiple cabinets, they can quickly apply their favorite preset wood-finish or sizing without digging through catalogs.
  * **Data Layout**: Very lightweight structure keying `UserID` to a simple map of favorite product rows or color IDs.

### D. Visual Thumbnail Generation
* **The Concept**: Show a rendered picture card of the design in the load menu rather than a generic text name.
* **Implementation**: Hide HUD widgets temporarily during save, trigger a viewport screenshot callback, encode the image to base64 or write to S3, and link the URL to the save record.

### E. Collaborative Co-Editing
* **The Concept**: Allow the private room host to grant edit permissions to guests.
* **Implementation**: The host toggles an "Allow Guest Edits" flag in the UI, enabling RPC modifications from other clients in the private room while keeping save/load slot permissions exclusively locked to the host's account.

