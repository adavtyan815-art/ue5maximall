# Clipboard Synchronization for Pixel Streaming (Ctrl+C / Ctrl+V)

This guide documents the production-grade approach for full bidirectional clipboard synchronization between a client browser and a headless Linux Unreal Engine 5 project over Pixel Streaming (WebRTC Data Channels).

---

## 1. Architectural Flow

```
PASTE FLOW (Browser -> Headless UE5 Server):
[Ctrl+V in Browser]
       │
       ▼
(Browser `paste` listener extracts text)
       │
       ▼
(emitUIInteraction JSON sent via Data Channel)
       │
       ▼
(UPixelStreamingInput Component intercepts in C++)
       │
       ▼
(Slate Key Character Injection bypasses headless OS limits)
       │
       ▼
(Characters inserted directly into active input widgets)


COPY FLOW (Headless UE5 Server -> Browser Client):
[Text Copied inside UE5 UI (Ctrl+C)]
       │
       ▼
(Text stored in Unreal Engine memory-fallback clipboard)
       │
       ▼
(C++ PlayerController polls clipboard at 5Hz in PlayerTick)
       │
       ▼
(SendPixelStreamingResponse sends "MaxiMallClipboard <text>")
       │
       ▼
(Browser addResponseEventListener intercepts payload)
       │
       ▼
(navigator.clipboard.writeText writes to user's physical clipboard)
```

---

## 2. Frontend Configuration (`player.ts`)

Add the following handlers inside your web client frontend initialization:

```typescript
// ─── Clipboard Sync ──────────────────────────────────────────────────────────
(function installClipboardSyncHandler() {
    // 1. Browser -> UE5 (Paste)
    window.addEventListener('paste', (event: ClipboardEvent) => {
        event.preventDefault();
        let pasteText = "";
        if (event.clipboardData) {
            pasteText = event.clipboardData.getData('text');
        }
        if (pasteText) {
            stream.emitUIInteraction({
                Cmd: "ClipboardPaste",
                Text: pasteText
            });
        }
    });

    // 2. UE5 -> Browser (Copy)
    stream.addResponseEventListener('MaxiMallClipboard', (rawData: string) => {
        const trimmedData = rawData.trim();
        if (trimmedData.startsWith('MaxiMallClipboard ')) {
            const textToCopy = trimmedData.substring('MaxiMallClipboard '.length).trim();
            if (textToCopy) {
                navigator.clipboard.writeText(textToCopy)
                    .catch(err => {
                        console.error('[MaxiMall] Failed to write to client clipboard: ', err);
                    });
            }
        }
    });
})();
```

---

## 3. Unreal Engine C++ Configurations

Unreal Engine Pixel Streaming dynamically attaches new `UPixelStreamingInput` components when a peer connects, rendering constructor-bound event delegates inactive. To handle this, we perform **dynamic component binding** inside `PlayerTick`, and inject characters **directly into Slate** to support headless systems.

### A. Game Level Controller (`MaxiMallPreviewController` / `awsTutorial_PlayerController`)

Implement both paste interception (via Slate character events) and copy polling (from the memory clipboard).

#### Header declarations (`.h`):
```cpp
protected:
    virtual void BeginPlay() override;
    virtual void PlayerTick(float DeltaTime) override;

private:
    UPROPERTY()
    TObjectPtr<UPixelStreamingInput> PixelStreamingInput;

    UPROPERTY()
    TObjectPtr<UPixelStreamingInput> ActivePixelStreamingInput;

    FString LastKnownClipboardContent;
    float ClipboardCheckInterval = 0.2f;
    float ClipboardCheckTimer = 0.0f;

    UFUNCTION()
    void OnPixelStreamingInput(const FString& Descriptor);
```

#### Source implementation (`.cpp`):
```cpp
#include "Framework/Application/SlateApplication.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformApplicationMisc.h"

// In Constructor:
// PixelStreamingInput = CreateDefaultSubobject<UPixelStreamingInput>(TEXT("PixelStreamingInputComponent"));

void AMaxiMallPreviewController::BeginPlay()
{
    Super::BeginPlay();

    if (IsLocalController() && PixelStreamingInput)
    {
        PixelStreamingInput->OnInputEvent.AddDynamic(this, &AMaxiMallPreviewController::OnPixelStreamingInput);
    }
}

void AMaxiMallPreviewController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);

    if (!IsLocalController()) return;

    // 1. Dynamic Binding: Detect dynamically attached peer input components
    if (ActivePixelStreamingInput == nullptr)
    {
        TArray<UPixelStreamingInput*> InputComponents;
        GetComponents<UPixelStreamingInput>(InputComponents);
        for (UPixelStreamingInput* PSInput : InputComponents)
        {
            if (PSInput && PSInput != PixelStreamingInput)
            {
                ActivePixelStreamingInput = PSInput;
                ActivePixelStreamingInput->OnInputEvent.AddDynamic(this, &AMaxiMallPreviewController::OnPixelStreamingInput);
                break;
            }
        }
    }

    // 2. UE to Web copy polling: check clipboard 5 times a second
    ClipboardCheckTimer += DeltaTime;
    if (ClipboardCheckTimer >= ClipboardCheckInterval)
    {
        ClipboardCheckTimer = 0.0f;

        FString CurrentClipboard;
        FPlatformApplicationMisc::ClipboardPaste(CurrentClipboard);

        if (!CurrentClipboard.IsEmpty() && CurrentClipboard != LastKnownClipboardContent)
        {
            LastKnownClipboardContent = CurrentClipboard;
            
            UPixelStreamingInput* TargetInput = ActivePixelStreamingInput.IsValid() ? ActivePixelStreamingInput.Get() : PixelStreamingInput.Get();
            if (TargetInput)
            {
                FString Payload = FString::Printf(TEXT("MaxiMallClipboard %s"), *CurrentClipboard);
                TargetInput->SendPixelStreamingResponse(Payload);
            }
        }
    }
}

void AMaxiMallPreviewController::OnPixelStreamingInput(const FString& Descriptor)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Descriptor);

    if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
    {
        if (JsonObject->HasField(TEXT("Cmd")) && JsonObject->GetStringField(TEXT("Cmd")) == TEXT("ClipboardPaste"))
        {
            FString PasteText = JsonObject->GetStringField(TEXT("Text"));
            PasteText.ReplaceInline(TEXT("\0"), TEXT(""));

            // Populate OS clipboard fallback
            FPlatformApplicationMisc::ClipboardCopy(*PasteText);
            LastKnownClipboardContent = PasteText;

            // Direct slate injection (works seamlessly on headless Linux)
            if (FSlateApplication::IsInitialized())
            {
                FSlateApplication& SlateApp = FSlateApplication::Get();
                for (int32 i = 0; i < PasteText.Len(); ++i)
                {
                    FCharacterEvent CharEvent(PasteText[i], FModifierKeysState(), 0, false);
                    SlateApp.ProcessKeyCharEvent(CharEvent);
                }
            }
        }
    }
}
```

---

### B. Login Level Controller (`MaxiMallLoginPlayerController` / `awsTutorial_LoginPlayerController`)

To support copy-paste on the credentials login screen without loading heavy 3D scene dependencies, assign this lightweight class as the base for your Login Player Controller Blueprint.

#### Header declarations (`.h`):
```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "PixelStreamingInput.h"
#include "MaxiMallLoginPlayerController.generated.h"

UCLASS()
class MAXIMALL_API AMaxiMallLoginPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    AMaxiMallLoginPlayerController();

protected:
    virtual void BeginPlay() override;
    virtual void PlayerTick(float DeltaTime) override;

private:
    UPROPERTY()
    TObjectPtr<UPixelStreamingInput> PixelStreamingInput;

    UPROPERTY()
    TObjectPtr<UPixelStreamingInput> ActivePixelStreamingInput;

    UFUNCTION()
    void OnPixelStreamingInput(const FString& Descriptor);
};
```

#### Source implementation (`.cpp`):
```cpp
#include "MaxiMallLoginPlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformApplicationMisc.h"

AMaxiMallLoginPlayerController::AMaxiMallLoginPlayerController()
{
    PrimaryActorTick.bCanEverTick = true;
    PixelStreamingInput = CreateDefaultSubobject<UPixelStreamingInput>(TEXT("PixelStreamingInputComponent"));
}

void AMaxiMallLoginPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (IsLocalController() && PixelStreamingInput)
    {
        PixelStreamingInput->OnInputEvent.AddDynamic(this, &AMaxiMallLoginPlayerController::OnPixelStreamingInput);
    }
}

void AMaxiMallLoginPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    if (!IsLocalController()) return;

    if (ActivePixelStreamingInput == nullptr)
    {
        TArray<UPixelStreamingInput*> InputComponents;
        GetComponents<UPixelStreamingInput>(InputComponents);
        for (UPixelStreamingInput* PSInput : InputComponents)
        {
            if (PSInput && PSInput != PixelStreamingInput)
            {
                ActivePixelStreamingInput = PSInput;
                ActivePixelStreamingInput->OnInputEvent.AddDynamic(this, &AMaxiMallLoginPlayerController::OnPixelStreamingInput);
                break;
            }
        }
    }
}

void AMaxiMallLoginPlayerController::OnPixelStreamingInput(const FString& Descriptor)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Descriptor);

    if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
    {
        if (JsonObject->HasField(TEXT("Cmd")) && JsonObject->GetStringField(TEXT("Cmd")) == TEXT("ClipboardPaste"))
        {
            FString PasteText = JsonObject->GetStringField(TEXT("Text"));
            PasteText.ReplaceInline(TEXT("\0"), TEXT(""));

            FPlatformApplicationMisc::ClipboardCopy(*PasteText);

            if (FSlateApplication::IsInitialized())
            {
                FSlateApplication& SlateApp = FSlateApplication::Get();
                for (int32 i = 0; i < PasteText.Len(); ++i)
                {
                    FCharacterEvent CharEvent(PasteText[i], FModifierKeysState(), 0, false);
                    SlateApp.ProcessKeyCharEvent(CharEvent);
                }
            }
        }
    }
}
```
