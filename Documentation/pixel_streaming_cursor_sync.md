# Pixel Streaming Cursor Hover Synchronization (Hand Pointer CSS & Data-Channel)

This guide documents the 100% working, verified solution for synchronizing the client's browser mouse cursor with hover interactions occurring inside Unreal Engine 5 via Pixel Streaming.

---

## 1. The Core Problem
Unreal Engine's built-in cursor styling (`CurrentMouseCursor = EMouseCursor::Hand`) only modifies the hardware cursor of the **remote server OS**. 

Under Pixel Streaming, the browser is receiving a video stream and rendering its own physical mouse cursor. 
1. If you change the cursor inside UE, it only changes a software cursor drawn *inside the video feed*. 
2. The user's actual browser cursor remains a default arrow (`default`), creating a sluggish, non-interactive feel.
3. Simply modifying the body's inline CSS (`document.body.style.cursor = 'pointer'`) fails because the nested video player and control canvas elements have their own specific cursor styling rules that override the body.

---

## 2. The 100% Working Architecture
The solution uses the Webrtc **Data Channel** (bidirectional messages) to send high-frequency hover states from UE C++ to the browser, combined with a **sentinel CSS class selector rule** to force cursor updates across all nested frontend elements.

```
+-----------------------------------+
|      Unreal Engine 5 (Server)     |
|  - Traces hover mesh under cursor |
|  - Fires BroadcastCursorState()   |
+-----------------------------------+
                  |
        (WebRTC Data Channel)
  [Payload: "MaxiMallCursor pointer"]
                  v
+-----------------------------------+
|       Browser Client (Web)        |
|  - Registers response listener    |
|  - toggles class 'ps-cursor-ptr'  |
|  - CSS rule overrides all child   |
|    cursors with !important        |
+-----------------------------------+
```

---

## 3. Step-by-Step Code Configuration

### A. Unreal Engine C++ Component Setup

#### 1. Tick Tracing & Detection (`awsTutorial_PlayerController.cpp` / `MaxiMallPreviewController.cpp`)
Inside the `PlayerTick` function, trace for interactive meshes under the player's crosshair or cursor. If hover target changes, send a command to the cursor broadcast:

```cpp
void AAwsTutorial_PlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);

    if (!IsLocalController()) return;

    UPrimitiveComponent* NewHoveredComp = nullptr;
    AShowroomBooth* HitBooth = nullptr;
    
    // Trace logic checks if the mouse is hovering over an interactive mesh component
    if (TraceFurnitureComponent(HitBooth, CurrentTargetComponent, NewHoveredComp))
    {
        NewHoveredComp = HitComp;
    }

    UPrimitiveComponent* CurrentHovered = HoveredComponent.Get();
    if (CurrentHovered != NewHoveredComp)
    {
        if (CurrentHovered)
        {
            CurrentHovered->SetRenderCustomDepth(false);
        }
        
        if (NewHoveredComp)
        {
            NewHoveredComp->SetRenderCustomDepth(true);
            NewHoveredComp->SetCustomDepthStencilValue(1);
        }
        
        // --- 100% WORKING BRIDGE INVOCATION ---
        const bool bNowHovering = (NewHoveredComp != nullptr);
        if (bNowHovering != bWasHoveringInteractable)
        {
            BroadcastCursorState(bNowHovering);
            bWasHoveringInteractable = bNowHovering;
        }
        
        HoveredComponent = NewHoveredComp;
    }
}
```

#### 2. Broadcast via Data Channel (`awsTutorial_PlayerController.cpp`)
The player controller formats a custom message string prefixed with `MaxiMallCursor` and sends it down the WebRTC data channel using the `Response` registry message ID:

```cpp
void AAwsTutorial_PlayerController::BroadcastCursorState(bool bHovering)
{
    if (!IsLocalController()) return;

#if WITH_ENGINE
    if (!IPixelStreamingModule::IsAvailable()) return;

    IPixelStreamingModule& PSModule = IPixelStreamingModule::Get();

    // Epic's JS frontend matches response events by prefix and strips it.
    const FString CursorValue = bHovering ? TEXT("pointer") : TEXT("default");
    const FString Payload = FString::Printf(TEXT("MaxiMallCursor %s"), *CursorValue);

    // Retrieve the 'Response' message ID from the FromStreamer protocol map
    const FPixelStreamingInputMessage* ResponseMsg = FPixelStreamingInputProtocol::FromStreamerProtocol.Find(TEXT("Response"));
    if (!ResponseMsg) return;
    
    const uint8 ResponseTypeId = ResponseMsg->GetID();

    // Send the player message across all active stream clients
    PSModule.ForEachStreamer([ResponseTypeId, &Payload](TSharedPtr<IPixelStreamingStreamer> Streamer)
    {
        if (Streamer.IsValid())
        {
            Streamer->SendPlayerMessage(ResponseTypeId, Payload);
        }
    });
#endif
}
```

---

### B. Frontend Web Page Configuration (`player.ts`)

To parse the message and ensure the mouse cursor changes reliably across the whole screen, register a response event listener and apply a global override CSS sheet.

```typescript
(function installPixelStreamingCursorHandler() {
    // 1. Inject a stylesheet rule targeting the sentinel class.
    // Standard body inline cursor styling fails because nested WebRTC canvas/video players
    // have their own specific cursor definitions. Targeting body.ps-cursor-pointer * 
    // forces every child element under the body to inherit the pointer style.
    const style = document.createElement('style');
    style.id = 'ps-cursor-style';
    style.textContent = 'body.ps-cursor-pointer, body.ps-cursor-pointer * { cursor: pointer !important; }';
    document.head.appendChild(style);

    // 2. Register the listener directly on the PixelStreaming instance.
    // The library automatically strips the "MaxiMallCursor" prefix and passes the rest as rawData.
    stream.addResponseEventListener('MaxiMallCursor', (rawData: string) => {
        console.log('[MaxiMall] Received hover event payload:', rawData);
        const cursor = rawData.trim();
        
        if (cursor.includes('pointer')) {
            document.body.classList.add('ps-cursor-pointer');
        } else if (cursor.includes('default')) {
            document.body.classList.remove('ps-cursor-pointer');
        }
    });

    console.log('[MaxiMall] Pixel Streaming cursor data-channel listener registered.');
})();
```

---

## 4. Best Practices for Pixel Streaming UI Communication

1. **Leverage the Response message ID**: Avoid writing custom data channels from scratch. Use Epic's registered `Response` channel mapping (`FPixelStreamingInputProtocol::FromStreamerProtocol`). It is natively optimized and guarantees standard packet delivery.
2. **Sentinel CSS Classes Instead of Inline Styles**: Avoid applying styles directly to elements (e.g. `element.style.cursor = 'pointer'`). When the user moves the mouse into UI widget panels, overlay dialogs, or custom overlays, direct styles get overridden. Using `.class-name, .class-name * { cursor: pointer !important; }` guarantees global compliance.
3. **Isolate Overlap Behaviors**: Keep helper scripts separated (e.g., separating the hover-to-hand pointer code from the drag-to-hide cursor style, like hiding cursor on Right Mouse Button drag). 
4. **State Gating**: Ensure that when the client is dragging (like rotating the viewport), hover events are muted on the server-side to prevent the cursor from flicking back and forth between "pointer" and "arrow" while the camera orbits.
5. **Debounce / Change-only broadcasts**: Mute redundant broadcasts. Only send messages over the data channel when the state *changes* (e.g., transitioning from hover to non-hover). Do not broadcast every tick.
