# PC1 Ecosystem Integration Guide — awsTutorial, maximall-web & maximall-pixel-config

> **Primary Audience**: PC1 Developer (Narek), Antigravity, and Claude Code agents working across the MaxiMall platform.  
> **Purpose**: Deep breakdown of cross-repository responsibilities, communication protocols, data schemas, and runtime contracts.  

---

## 1. System Responsibilities & Repository Ownership

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              maximall-web                                   │
│  (Node.js / Express Orchestrator on AWS EC2 t3.micro in eu-central-1b)      │
│  • Manages EC2 GPU instance standby pool (Start / Stop / Recycle)           │
│  • Reverse-proxies HTTP player assets & WebRTC signaling over private IPs  │
│  • Persists user room design saves (src/data/saves/)                        │
└──────────────────────┬───────────────────────────────┬──────────────────────┘
                       │                               │
                       │ Reverse-proxies WebRTC        │ Exchanges Save/Load
                       │ signaling & player assets     │ REST JSON payloads
                       ▼                               ▼
┌──────────────────────────────────────────────┐ ┌────────────────────────────┐
│            maximall-pixel-config             │ │        awsTutorial         │
│  (Wilbur Signaling Server & Frontend Player) │ │   (Unreal Engine 5 C++)    │
│  • Deployed on AWS GPU Instance (/home/ssm)  │ │  • Packaged for Linux on   │
│  • Wilbur signaling on port 8000 (HTTP/WS)   │ │    PC2 (UE 5.6)            │
│  • Compiled WebRTC player (player.html/js)   │ │  • Interactive 3D showroom │
│  • Connects to UE5 streamer on port 8888     │ │    and room constructor    │
└──────────────────────────────────────────────┘ └────────────────────────────┘
```

---

## 2. Unreal Engine ↔ Backend REST Contracts (`maximall-web`)

Unreal Engine communicates with `maximall-web` via HTTP REST calls dispatched from C++ (`HttpModule`) in `USaveSystemWidget`:

### 2.1 Get User Saves (`GET /api/saves/:username`)
- **Caller**: `USaveSystemWidget::RefreshSaveHistory()` in `awsTutorial`.
- **Backend Handler**: `app.get('/api/saves/:username', ...)` in `maximall-web`.
- **Response Format**:
  ```json
  [
    {
      "username": "narek",
      "saveId": "save_1724240000000",
      "saveName": "My Modern Showroom",
      "date": "21.08.2026",
      "thumbnail": "iVBORw0KGgoAAAANSUhEUgAAAQAAAAEACAYAAABccqhm...",
      "boothStates": [
        {
          "boothName": "BP_ShowroomBooth_C_1",
          "state": {
            "productID": "Vanity_Set_01",
            "activeSizeIndex": 0,
            "activeColorIndex": 2,
            "countertopSizeIndex": 0,
            "activeCountertopColorIndex": 1,
            "closetSizeIndex": 0,
            "closetColorIndex": 0,
            "sinkSizeIndex": 0,
            "sinkColorIndex": 0,
            "faucetSizeIndex": 0,
            "faucetColorIndex": 0,
            "mirrorSizeIndex": 0,
            "mirrorColorIndex": 0
          }
        }
      ]
    }
  ]
  ```

### 2.2 Upload Room Save (`POST /api/saves`)
- **Caller**: `USaveSystemWidget::ExecuteSaveGame()` in `awsTutorial`.
- **Backend Handler**: `app.post('/api/saves', ...)` in `maximall-web`.
- **Payload**: JSON payload with `username`, `saveId`, `saveName`, `date`, `thumbnail` (Base64 PNG screenshot), and `boothStates` array serializing `FShowroomBoothConfigState`.
- **Response**: `HTTP 200 OK` `{ "success": true, "id": "..." }`.

### 2.3 Delete Room Save (`DELETE /api/saves/:username/:saveId`)
- **Caller**: `USaveSystemWidget::HandleDeleteSaveItem()` in `awsTutorial`.
- **Backend Handler**: `app.delete('/api/saves/:username/:saveId', ...)` in `maximall-web`.
- **Response**: `HTTP 200 OK` `{ "success": true }`.

---

## 3. Unreal Engine ↔ Pixel Streaming Contracts (`maximall-pixel-config`)

### 3.1 Local Streamer WebRTC Connection
- The packaged Linux binary connects over localhost to Wilbur: `ws://127.0.0.1:8888`.
- Video frames captured from NVIDIA NVENC hardware encoder are streamed directly over WebRTC SRTP/SRTCP (UDP ports `49152–65535`).

### 3.2 Data Channel Event Hooks:
1. **URL Redirection (`open_url`)**:
   - Dispatched from `AAwsTutorial_PlayerController::SendOpenURLToBrowser(const FString& URL)`.
   - Transmits string payload over Pixel Streaming data channel: `open_url: https://external-store.com/item/123` (`open_url: %s` format with space).
   - Handled in `player.ts` via `window.open(url, '_blank')`.
2. **Cursor Synchronization**:
   - `player.ts` injects the `html.lmb-down` style to hide the browser cursor while the user rotates the 3D camera with LMB, preventing cursor flickering over DOM overlays.
3. **Hovering Mouse Mode**:
   - `player.ts` defaults to `HoveringMouse: true` so the user can freely interact with UI buttons and 3D showroom booths without locking the cursor.

---

## 4. Cross-Project Change Decision Matrix

| Change Type | What to Modify | Cross-Project Verification Required? |
|---|---|---|
| **C++ UI / Widget Styling** | `awsTutorial` (`Source/awsTutorial/FurnitureConfigurator/UI/`) | **No.** Local to Unreal Engine. |
| **New 3D Furniture / Materials** | `awsTutorial` (`Content/Meshes/`, `Content/Materials/`) | **No.** Local to Unreal Engine. |
| **Save/Load JSON Schema Changes** | `awsTutorial` (`USaveSystemWidget.cpp`) | **YES.** Must verify `maximall-web` (`src/app.ts` `/api/saves` handler). |
| **Pixel Streaming Custom Messages** | `awsTutorial` (`awsTutorial_PlayerController.cpp`) | **YES.** Must verify `maximall-pixel-config` (`Frontend/.../player.ts`). |
| **Instance Startup Parameters** | `maximall-web` (`src/services/ec2Service.ts`) | **YES.** Must verify AWS GPU AMI launch scripts. |

---

## 5. AWS Production Deployment Architecture

When the project is packaged on PC2 (UE 5.6) and deployed to the AWS GPU instance (`g4dn.2xlarge`):
1. **Headless Display**: The instance runs `Xvfb` on virtual display `:0` (`Environment=DISPLAY=:0` in systemd service) so Vulkan obtains a valid rendering surface.
2. **Signaling Server**: `SignallingWebServer` from `maximall-pixel-config` runs as a systemd service listening on port 8000.
3. **Unreal Application**: `awsTutorialClient` launches and connects to `ws://127.0.0.1:8888`.
4. **Reverse Proxy Routing**: `maximall-web` connects over the private IP (`http://172.31.x.x:8000`) to proxy WebRTC signaling and player assets to browser users.

---
*Document Version: 1.0.0 — PC1 Ecosystem Integration Guide*
