#include "MaxiMallPreviewController.h"

#include <Net/Core/Connection/NetCloseResult.h>
#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"
#include "FurnitureConfigurator/ShowroomBooth.h"
#include "FurnitureConfigurator/Preview/FurniturePreviewActor.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/InputComponent.h"
#include "InputCoreTypes.h"
#include "EngineUtils.h"
#include "Engine/PostProcessVolume.h"
#include "Blueprint/UserWidget.h"
#include "FurnitureConfigurator/UI/ConfiguratorMainWidget.h"
#include "FurnitureConfigurator/UI/ViewmodeOverlayWidget.h"
#include "FurnitureConfigurator/UI/BIMInspectorWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "IPixelStreamingModule.h"
#include "PixelStreamingInputProtocol.h"
#include "PixelStreamingInputComponent.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderPipelineCache.h"
#include "DatasmithAssetUserData.h"
#include "DatasmithContentBlueprintLibrary.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"

AMaxiMallPreviewController::AMaxiMallPreviewController()
{
    // Initialize properties
    ActivePreviewActor = nullptr;
    CurrentTargetBooth = nullptr;
    CurrentTargetComponent = EFurnitureComponentType::None;
    HoveredComponent = nullptr;
    bIsClosingUI = false;
    LastClickTime = 0.f;
    DoubleClickThreshold = 0.5f;
    bRightMouseIsDragging = false;
    
    // Default preview actor class
    PreviewActorClass = AFurniturePreviewActor::StaticClass();

    MainWidgetClass = nullptr;
    ViewmodeOverlayClass = nullptr;
    MainWidgetInstance = nullptr;
    ViewmodeOverlayInstance = nullptr;



    // Create the Pixel Streaming Input component
    PixelStreamingInput = CreateDefaultSubobject<UPixelStreamingInput>(TEXT("PixelStreamingInputComponent"));
}

void AMaxiMallPreviewController::BeginPlay()
{
    Super::BeginPlay();

    // Force fast pre-warm batch mode for Vulkan shader pipeline compilation
    FShaderPipelineCache::SetBatchMode(FShaderPipelineCache::BatchMode::Fast);

    UE_LOG(LogTemp, Warning, TEXT("AMaxiMallPreviewController::BeginPlay - Player Controller Initialized. IsLocalController: %s"), IsLocalController() ? TEXT("TRUE") : TEXT("FALSE"));

    if (IsLocalController())
    {
        if (PixelStreamingInput)
        {
            UE_LOG(LogTemp, Warning, TEXT("[Clipboard] Binding dynamic OnPixelStreamingInput callback on local client."));
            PixelStreamingInput->OnInputEvent.AddDynamic(this, &AMaxiMallPreviewController::OnPixelStreamingInput);
        }

        // Force Epic quality scalability settings to prevent virtual server fallbacks
        if (UGameUserSettings* GameUserSettings = UGameUserSettings::GetGameUserSettings())
        {
            if (GameUserSettings->GetOverallScalabilityLevel() != 3)
            {
                UE_LOG(LogTemp, Warning, TEXT("AMaxiMallPreviewController::BeginPlay - Overriding overall scalability level to Epic (3)."));
                GameUserSettings->SetOverallScalabilityLevel(3); // 3 = Epic Quality
                GameUserSettings->ApplySettings(false);
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("AMaxiMallPreviewController::BeginPlay - GameUserSettings is NULL!"));
        }

        FInputModeGameAndUI InputMode;
        InputMode.SetHideCursorDuringCapture(true);
        InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        SetInputMode(InputMode);
        bShowMouseCursor = true;
        bEnableClickEvents = true;
        bEnableMouseOverEvents = true;
    }
}

void AMaxiMallPreviewController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);

    if (!IsLocalController())
    {
        return;
    }

    // Dynamically check for active Pixel Streaming Input components that are not our default subobject
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

    // Throttled clipboard monitoring: checks 5 times a second
    ClipboardCheckTimer += DeltaTime;
    if (ClipboardCheckTimer >= ClipboardCheckInterval)
    {
        ClipboardCheckTimer = 0.0f;

        FString CurrentClipboard;
        FPlatformApplicationMisc::ClipboardPaste(CurrentClipboard);

        if (CurrentClipboard != LastKnownClipboardContent)
        {
            LastKnownClipboardContent = CurrentClipboard;
            
            UPixelStreamingInput* TargetInput = (ActivePixelStreamingInput != nullptr) ? ActivePixelStreamingInput.Get() : PixelStreamingInput.Get();
            if (TargetInput)
            {
                FString Payload = FString::Printf(TEXT("MaxiMallClipboard %s"), *CurrentClipboard);
                TargetInput->SendPixelStreamingResponse(Payload);
            }
        }
    }

    if (!IsInputKeyDown(EKeys::RightMouseButton))
    {
        bRightMouseIsDragging = false;
    }

    UPrimitiveComponent* NewHoveredComp = nullptr;
    AShowroomBooth* HitBooth = nullptr;
    bool bHoveringShowroom = false;
    
    if (!ActivePreviewActor)
    {
        FHitResult HitResult;
        bool bHit = false;
        
        if (bShowMouseCursor)
        {
            bHit = GetHitResultUnderCursor(ECC_Visibility, false, HitResult);
        }
        else
        {
            FVector CameraLoc;
            FRotator CameraRot;
            GetPlayerViewPoint(CameraLoc, CameraRot);
            
            FVector Start = CameraLoc;
            FVector End = Start + (CameraRot.Vector() * 1000.f);
            
            FCollisionQueryParams Params;
            Params.AddIgnoredActor(GetPawn());
            
            bHit = GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, ECC_Visibility, Params);
        }
        
        if (bHit && HitResult.GetActor())
        {
            HitBooth = Cast<AShowroomBooth>(HitResult.GetActor());
            UPrimitiveComponent* HitComp = HitResult.GetComponent();

            bool bIsShowroomInteractable = HitBooth && HitComp && (
                HitComp == HitBooth->MainCabinet.Get() ||
                HitComp == HitBooth->ClosetMesh.Get() ||
                HitComp == HitBooth->DoorMeshSlot0.Get() ||
                HitComp == HitBooth->DoorMeshSlot1.Get() ||
                HitComp == HitBooth->ClosetDoorMeshSlot0.Get() ||
                HitComp == HitBooth->ClosetDoorMeshSlot1.Get() ||
                HitComp == HitBooth->CountertopMesh.Get() ||
                HitComp == HitBooth->SinkMesh.Get() ||
                HitComp == HitBooth->FaucetMesh.Get() ||
                HitComp == HitBooth->MirrorMesh.Get()
            );

            if (bIsShowroomInteractable)
            {
                bHoveringShowroom = true;
            }
            else if (HitComp && HasBIMMetadata(HitComp))
            {
                NewHoveredComp = HitComp;
            }
        }
    }

    UPrimitiveComponent* CurrentHovered = HoveredComponent.Get();
    if (CurrentHovered != NewHoveredComp)
    {
        if (CurrentHovered && CurrentHovered != SelectedComponent.Get())
        {
            CurrentHovered->SetRenderCustomDepth(false);
        }
        else if (CurrentHovered && CurrentHovered == SelectedComponent.Get())
        {
            CurrentHovered->SetRenderCustomDepth(true);
            CurrentHovered->SetCustomDepthStencilValue(2);
        }
        
        if (NewHoveredComp)
        {
            NewHoveredComp->SetRenderCustomDepth(true);
            if (NewHoveredComp != SelectedComponent.Get())
            {
                NewHoveredComp->SetCustomDepthStencilValue(1);
            }
        }
        
        HoveredComponent = NewHoveredComp;
    }

    const bool bIsAnyHovered = (NewHoveredComp != nullptr || bHoveringShowroom);
    if (bIsAnyHovered)
    {
        if (bShowMouseCursor)
        {
            CurrentMouseCursor = EMouseCursor::Hand;
        }
    }
    else
    {
        if (bShowMouseCursor)
        {
            CurrentMouseCursor = EMouseCursor::Default;
        }
    }

    // ── Pixel Streaming cursor data-channel broadcast ─────────────────────
    if (bIsAnyHovered != bWasHoveringInteractable)
    {
        BroadcastCursorState(bIsAnyHovered);
        bWasHoveringInteractable = bIsAnyHovered;
    }
}

void AMaxiMallPreviewController::SetupInputComponent()
{
    Super::SetupInputComponent();

    if (InputComponent)
    {
        InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AMaxiMallPreviewController::OnLeftMouseButtonPressed);
    }
}

void AMaxiMallPreviewController::AddYawInput(float Val)
{
    Super::AddYawInput(Val);

    if (Val != 0.f && IsInputKeyDown(EKeys::RightMouseButton))
    {
        bRightMouseIsDragging = true;
    }
}

void AMaxiMallPreviewController::AddPitchInput(float Val)
{
    Super::AddPitchInput(Val);

    if (Val != 0.f && IsInputKeyDown(EKeys::RightMouseButton))
    {
        bRightMouseIsDragging = true;
    }
}

// в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
// Pixel Streaming Cursor Broadcast
// в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ

void AMaxiMallPreviewController::BroadcastCursorState(bool bHovering)
{
    // Guard: Only runs on the local player controller (not server or remote).
    if (!IsLocalController())
    {
        return;
    }

#if WITH_ENGINE
    // Retrieve the active Pixel Streaming module. If the PixelStreaming plugin
    // is not loaded (e.g. Editor play without PS enabled, standalone desktop
    // build), IsAvailable() returns false and we safely do nothing.
    if (!IPixelStreamingModule::IsAvailable())
    {
        return;
    }
    IPixelStreamingModule& PSModule = IPixelStreamingModule::Get();

    // Build the payload: "MaxiMallCursor pointer" or "MaxiMallCursor default".
    // Epic's JS frontend matches by prefix (MaxiMallCursor) and strips it.
    const FString CursorValue = bHovering ? TEXT("pointer") : TEXT("default");
    const FString Payload = FString::Printf(TEXT("MaxiMallCursor %s"), *CursorValue);

    // Look up the "Response" message ID from the FromStreamer protocol map.
    // This is the canonical way Epic sends arbitrary JSON back to the browser
    // (identical approach to UPixelStreamingInput::SendPixelStreamingResponse).
    // Guard against the protocol entry being absent (should never happen in PS builds).
    const FPixelStreamingInputMessage* ResponseMsg = FPixelStreamingInputProtocol::FromStreamerProtocol.Find(TEXT("Response"));
    if (!ResponseMsg)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MaxiMall] BroadcastCursorState: 'Response' message type not registered in PS protocol."));
        return;
    }
    const uint8 ResponseTypeId = ResponseMsg->GetID();

    // Iterate all active streamers and send the cursor message.
    // In a single-user Pixel Streaming session there is exactly one streamer.
    PSModule.ForEachStreamer([ResponseTypeId, &Payload](TSharedPtr<IPixelStreamingStreamer> Streamer)
    {
        if (Streamer.IsValid())
        {
            Streamer->SendPlayerMessage(ResponseTypeId, Payload);
        }
    });
#endif
}

void AMaxiMallPreviewController::OnLeftMouseButtonPressed()
{
    float CurrentTime = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.f;

    bool bIsDoubleClick = (CurrentTime - LastClickTime < DoubleClickThreshold);
    LastClickTime = CurrentTime;

    if (bIsDoubleClick)
    {
        AShowroomBooth* HitBooth = nullptr;
        EFurnitureComponentType ComponentType = EFurnitureComponentType::None;
        UPrimitiveComponent* HitComponent = nullptr;

        if (TraceFurnitureComponent(HitBooth, ComponentType, HitComponent) &&
            (ComponentType == EFurnitureComponentType::Doors || ComponentType == EFurnitureComponentType::Closet))
        {
            HandleDoubleClickInteraction();
            return;
        }
    }

    // Single-click selection for BIM elements & furniture components
    if (!ActivePreviewActor)
    {
        FHitResult HitResult;
        bool bHit = false;
        
        if (bShowMouseCursor)
        {
            bHit = GetHitResultUnderCursor(ECC_Visibility, false, HitResult);
        }
        else
        {
            FVector CameraLoc;
            FRotator CameraRot;
            GetPlayerViewPoint(CameraLoc, CameraRot);
            
            FVector Start = CameraLoc;
            FVector End = Start + (CameraRot.Vector() * 1000.f);
            
            FCollisionQueryParams Params;
            Params.AddIgnoredActor(GetPawn());
            
            bHit = GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, ECC_Visibility, Params);
        }
        
        if (bHit && HitResult.GetComponent())
        {
            UPrimitiveComponent* HitComp = HitResult.GetComponent();
            AActor* HitActor = HitResult.GetActor();
            AShowroomBooth* HitBooth = Cast<AShowroomBooth>(HitActor);

            bool bIsShowroomActor = HitBooth || (HitActor && (
                HitActor->IsA(AShowroomBooth::StaticClass()) ||
                HitActor->GetName().Contains(TEXT("Showroom")) ||
                HitActor->GetClass()->GetName().Contains(TEXT("Showroom"))
            ));

            if (bIsShowroomActor)
            {
                // Showroom single left-click does NOT open UI or BIM selection.
                // Double-click interaction handles opening View Mode.
            }
            else if (HasBIMMetadata(HitComp))
            {
                // Toggle selection on BIM model: clicking selected mesh deselects it
                if (SelectedComponent.Get() == HitComp)
                {
                    SelectComponent(nullptr);
                }
                else
                {
                    SelectComponent(HitComp);
                }
            }
            else
            {
                SelectComponent(nullptr);
            }
        }
        else
        {
            SelectComponent(nullptr);
        }
    }
}

FString AMaxiMallPreviewController::GetRequestURL() const {
	UNetConnection* netConnection = GetNetConnection();
	if (netConnection == NULL) return FString();
	return netConnection->RequestURL;
}

TArray<FString> AMaxiMallPreviewController::GetRequestOptions() const {
	UNetConnection* netConnection = GetNetConnection();
	if (netConnection == NULL) return TArray<FString>();
	FURL InURL( NULL, *netConnection->RequestURL, TRAVEL_Absolute );
	return InURL.Op;
}

bool AMaxiMallPreviewController::HasRequestOption(const FString& key) const {
	UNetConnection* netConnection = GetNetConnection();
	if (netConnection == NULL) return false;
	FURL InURL( NULL, *netConnection->RequestURL, TRAVEL_Absolute );
	return InURL.HasOption(*key);
}

FString AMaxiMallPreviewController::GetRequestOption(const FString& key) const {
	UNetConnection* netConnection = GetNetConnection();
	if (netConnection == NULL) return FString("");
	FURL InURL( NULL, *netConnection->RequestURL, TRAVEL_Absolute );
	const TCHAR* o = InURL.GetOption(*key, NULL);
	if (o == NULL) return FString("");
	if (o[0] == '=') return FString(o + 1);
	return FString(o);
}

void AMaxiMallPreviewController::Kick_Implementation() {
	UNetConnection* netConnection = GetNetConnection();
	if (netConnection == NULL) return;

	netConnection->Close(UE::Net::FNetCloseResult());
}

// в”Ђв”Ђ CONFIGURATOR PREVIEW MANAGEMENT в”Ђв”Ђ

void AMaxiMallPreviewController::OpenFurniturePreview(AShowroomBooth* TargetBooth, EFurnitureComponentType FocusComponent)
{
    if (!IsLocalController())
    {
        return;
    }

    if (!TargetBooth)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PreviewController] OpenFurniturePreview called with null TargetBooth."));
        return;
    }

    // Clear any active BIM selection & stencil outline when entering furniture View Mode
    SelectComponent(nullptr);

    if (FocusComponent == EFurnitureComponentType::Doors)
    {
        FocusComponent = EFurnitureComponentType::Cabinet;
    }

    CurrentTargetComponent = FocusComponent;

    if (!GetPawn())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PreviewController] OpenFurniturePreview called before possessing a pawn. Ignoring."));
        return;
    }

    ResetIgnoreInputFlags();
    SetIgnoreLookInput(true);
    SetIgnoreMoveInput(true);

    if (MainWidgetInstance)
    {
        MainWidgetInstance->RemoveFromParent();
    }

    if (!ActivePreviewActor)
    {
        SavedControlRotation = GetControlRotation();
    }

    CloseFurniturePreview();

    FFurnitureProductRow ProductSnapshot;
    if (!TargetBooth->GetActiveProductData(ProductSnapshot))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[PreviewController] Booth '%s' has no valid active product. Cannot open preview."),
            *TargetBooth->GetName());
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = this;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    UClass* SpawnClass = PreviewActorClass;
    if (!SpawnClass || !SpawnClass->IsChildOf(AFurniturePreviewActor::StaticClass()))
    {
        SpawnClass = AFurniturePreviewActor::StaticClass();
    }

    UE_LOG(LogTemp, Log, TEXT("[PreviewController] OpenFurniturePreview spawning class: %s"), *SpawnClass->GetName());

    // Always spawn at the real-world booth location for WorldInPlace orbit.
    FRotator SpawnRotation = FRotator::ZeroRotator;
    if (TargetBooth)
    {
        SpawnRotation.Yaw = TargetBooth->GetActorRotation().Yaw;
    }
    const FVector TargetSpawnLocation = TargetBooth ? TargetBooth->GetActorLocation() : FVector::ZeroVector;

    // Disable world PostProcessVolume (and its M_PostProcessOutline) during View Mode
    for (TActorIterator<APostProcessVolume> It(World); It; ++It)
    {
        if (APostProcessVolume* PPVol = *It)
        {
            PPVol->bEnabled = false;
        }
    }

    ActivePreviewActor = Cast<AFurniturePreviewActor>(World->SpawnActor(
        SpawnClass,
        &TargetSpawnLocation,
        &SpawnRotation,
        SpawnParams));

    if (!ActivePreviewActor)
    {
        UE_LOG(LogTemp, Error, TEXT("[PreviewController] Failed to spawn AFurniturePreviewActor."));
        return;
    }

    ActivePreviewActor->LoadProductPreview(ProductSnapshot, TargetBooth->ActiveState, TargetBooth);

    // Component isolation and camera pivot are handled entirely inside SetFocusComponent.
    if (CurrentTargetComponent != EFurnitureComponentType::None)
    {
        ActivePreviewActor->SetFocusComponent(CurrentTargetComponent);
    }

    CurrentTargetBooth = TargetBooth;
    CurrentTargetBooth->OnProductChanged.AddDynamic(this, &AMaxiMallPreviewController::OnTargetBoothProductChanged);

    SetViewTargetWithBlend(ActivePreviewActor, 0.0f);

    if (!ViewmodeOverlayInstance && ViewmodeOverlayClass)
    {
        ViewmodeOverlayInstance = CreateWidget<UUserWidget>(this, ViewmodeOverlayClass);
    }
    if (ViewmodeOverlayInstance)
    {
        UViewmodeOverlayWidget* Overlay = Cast<UViewmodeOverlayWidget>(ViewmodeOverlayInstance);
        if (Overlay)
        {
            Overlay->SetOwningPC(this);
        }
        if (!ViewmodeOverlayInstance->IsInViewport())
        {
            ViewmodeOverlayInstance->AddToViewport();
        }

        FInputModeGameAndUI InputMode;
        InputMode.SetWidgetToFocus(ViewmodeOverlayInstance->TakeWidget());
        InputMode.SetHideCursorDuringCapture(true);
        InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        SetInputMode(InputMode);
        bShowMouseCursor = true;
    }

    OnPreviewOpened();
}

void AMaxiMallPreviewController::CloseFurniturePreview()
{
    if (!IsLocalController())
    {
        return;
    }

    HiddenActors.Empty();

    AShowroomBooth* PreviousBooth = CurrentTargetBooth;
    if (CurrentTargetBooth)
    {
        CurrentTargetBooth->OnProductChanged.RemoveAll(this);
    }

    // Re-enable world PostProcessVolume when exiting View Mode
    if (UWorld* World = GetWorld())
    {
        for (TActorIterator<APostProcessVolume> It(World); It; ++It)
        {
            if (APostProcessVolume* PPVol = *It)
            {
                PPVol->bEnabled = true;
            }
        }
    }

    if (!ActivePreviewActor)
    {
        return;
    }

    if (ActivePreviewActor->Camera)
    {
        ActivePreviewActor->Camera->PostProcessBlendWeight = 0.f;
        ActivePreviewActor->Camera->PostProcessSettings = FPostProcessSettings();
    }

    SetViewTargetWithBlend(GetPawn(), 0.0f);

    SetControlRotation(SavedControlRotation);

    if (ViewmodeOverlayInstance)
    {
        ViewmodeOverlayInstance->RemoveFromParent();
    }

    if (PreviousBooth && MainWidgetInstance)
    {
        ResetIgnoreInputFlags();
        SetIgnoreLookInput(true);
        SetIgnoreMoveInput(true);

        CurrentTargetBooth = PreviousBooth;
        CurrentTargetBooth->OnProductChanged.AddUniqueDynamic(this, &AMaxiMallPreviewController::OnTargetBoothProductChanged);

        UConfiguratorMainWidget* MainWidget = Cast<UConfiguratorMainWidget>(MainWidgetInstance);
        if (MainWidget)
        {
            MainWidget->SetupWidget(this, CurrentTargetBooth, CurrentTargetComponent);
        }
        if (!MainWidgetInstance->IsInViewport())
        {
            MainWidgetInstance->AddToViewport();
        }

        FInputModeGameAndUI InputMode;
        InputMode.SetWidgetToFocus(MainWidgetInstance->TakeWidget());
        InputMode.SetHideCursorDuringCapture(true);
        InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        SetInputMode(InputMode);
        bShowMouseCursor = true;
    }
    else
    {
        CurrentTargetBooth = nullptr;
        CurrentTargetComponent = EFurnitureComponentType::None;

        bIsClosingUI = true;

        TWeakObjectPtr<AMaxiMallPreviewController> WeakThis(this);
        GetWorld()->GetTimerManager().SetTimerForNextTick([WeakThis]()
        {
            if (AMaxiMallPreviewController* StrongThis = WeakThis.Get())
            {
                StrongThis->ResetIgnoreInputFlags();
                StrongThis->SetIgnoreLookInput(false);
                StrongThis->SetIgnoreMoveInput(false);

                FInputModeGameAndUI InputMode;
                InputMode.SetHideCursorDuringCapture(true);
                InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
                StrongThis->SetInputMode(InputMode);
                StrongThis->bShowMouseCursor = true;

                if (FSlateApplication::IsInitialized())
                {
                    ULocalPlayer* LocalPlayer = StrongThis->GetLocalPlayer();
                    if (LocalPlayer)
                    {
                        FSlateApplication::Get().SetUserFocusToGameViewport(LocalPlayer->GetControllerId());
                    }
                }

                StrongThis->bIsClosingUI = false;
            }
        });
    }

    ActivePreviewActor->Destroy();
    ActivePreviewActor = nullptr;

    OnPreviewClosed();
}

void AMaxiMallPreviewController::HandlePreviewOrbitInput(float DeltaYaw, float DeltaPitch)
{
    if (!ActivePreviewActor)
    {
        return;
    }

    ActivePreviewActor->RotatePreview(
        DeltaYaw * OrbitSensitivity,
        DeltaPitch * OrbitSensitivity);
}

void AMaxiMallPreviewController::HandlePreviewZoomInput(float DeltaZoom)
{
    if (ActivePreviewActor)
    {
        ActivePreviewActor->ZoomPreview(DeltaZoom);
    }
}

void AMaxiMallPreviewController::ResetPreviewRotation()
{
    if (ActivePreviewActor)
    {
        ActivePreviewActor->ResetRotation();
    }
}

bool AMaxiMallPreviewController::IsPreviewActive() const
{
    return ActivePreviewActor != nullptr;
}

void AMaxiMallPreviewController::RequestBoothProductChange(AShowroomBooth* TargetBooth, FName NewProductID)
{
    if (!TargetBooth)
    {
        return;
    }

    if (GetLocalRole() == ROLE_Authority)
    {
        TargetBooth->RequestProductChange(NewProductID);
    }
    else
    {
        Server_RequestBoothProductChange(TargetBooth, NewProductID);
    }
}

void AMaxiMallPreviewController::RequestBoothDoorToggle(AShowroomBooth* TargetBooth, int32 SlotIndex)
{
    if (!TargetBooth)
    {
        return;
    }

    if (GetLocalRole() == ROLE_Authority)
    {
        TargetBooth->RequestDoorToggle(SlotIndex);
    }
    else
    {
        Server_RequestBoothDoorToggle(TargetBooth, SlotIndex);
    }
}

void AMaxiMallPreviewController::RequestBoothComponentSelection(AShowroomBooth* TargetBooth, EFurnitureComponentType ComponentType, int32 SizeIndex, int32 ColorIndex)
{
    if (!TargetBooth)
    {
        return;
    }

    if (GetLocalRole() == ROLE_Authority)
    {
        TargetBooth->RequestComponentSelection(ComponentType, SizeIndex, ColorIndex);
    }
    else
    {
        Server_RequestBoothComponentSelection(TargetBooth, ComponentType, SizeIndex, ColorIndex);
    }
}

void AMaxiMallPreviewController::Server_RequestBoothDoorToggle_Implementation(AShowroomBooth* TargetBooth, int32 SlotIndex)
{
    if (TargetBooth)
    {
        TargetBooth->RequestDoorToggle(SlotIndex);
    }
}

bool AMaxiMallPreviewController::Server_RequestBoothDoorToggle_Validate(AShowroomBooth* TargetBooth, int32 SlotIndex)
{
    return true;
}

void AMaxiMallPreviewController::Server_RequestBoothProductChange_Implementation(AShowroomBooth* TargetBooth, FName NewProductID)
{
    if (TargetBooth)
    {
        TargetBooth->RequestProductChange(NewProductID);
    }
}

bool AMaxiMallPreviewController::Server_RequestBoothProductChange_Validate(AShowroomBooth* TargetBooth, FName NewProductID)
{
    return true;
}

void AMaxiMallPreviewController::Server_RequestBoothComponentSelection_Implementation(AShowroomBooth* TargetBooth, EFurnitureComponentType ComponentType, int32 SizeIndex, int32 ColorIndex)
{
    if (TargetBooth)
    {
        TargetBooth->RequestComponentSelection(ComponentType, SizeIndex, ColorIndex);
    }
}

bool AMaxiMallPreviewController::Server_RequestBoothComponentSelection_Validate(AShowroomBooth* TargetBooth, EFurnitureComponentType ComponentType, int32 SizeIndex, int32 ColorIndex)
{
    return true;
}

void AMaxiMallPreviewController::Server_LoadBoothState_Implementation(AShowroomBooth* TargetBooth, FShowroomBoothConfigState State)
{
    if (TargetBooth)
    {
        UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][Server] Loading booth state for '%s' (Product: '%s')"), 
            *TargetBooth->GetName(), *State.ProductID.ToString());

        TargetBooth->ActiveState = State;
        TargetBooth->RebuildBoothVisuals();
    }
}

bool AMaxiMallPreviewController::Server_LoadBoothState_Validate(AShowroomBooth* TargetBooth, FShowroomBoothConfigState State)
{
    return true;
}

bool AMaxiMallPreviewController::TraceFurnitureComponent(AShowroomBooth*& OutBooth, EFurnitureComponentType& OutComponentType, UPrimitiveComponent*& OutHitComponent)
{
    OutBooth = nullptr;
    OutComponentType = EFurnitureComponentType::None;
    OutHitComponent = nullptr;

    if (bIsClosingUI)
    {
        return false;
    }
    if (IsPreviewActive())
    {
        return false;
    }
    if (MainWidgetInstance && MainWidgetInstance->IsInViewport())
    {
        return false;
    }

    FHitResult HitResult;
    const bool bHit = GetHitResultUnderCursor(ECC_Visibility, false, HitResult);

    if (bHit)
    {
        AActor* HitActor = HitResult.GetActor();
        UPrimitiveComponent* HitComp = HitResult.GetComponent();

        AShowroomBooth* HitBooth = Cast<AShowroomBooth>(HitActor);
        if (HitBooth)
        {
            OutBooth = HitBooth;
            OutHitComponent = HitComp;

            CurrentTargetBooth = HitBooth;

            if (OutHitComponent == HitBooth->MainCabinet.Get())
            {
                OutComponentType = EFurnitureComponentType::Cabinet;
            }
            else if (OutHitComponent == HitBooth->ClosetMesh.Get())
            {
                OutComponentType = EFurnitureComponentType::Closet;
            }
            else if (OutHitComponent == HitBooth->DoorMeshSlot0.Get() || OutHitComponent == HitBooth->DoorMeshSlot1.Get())
            {
                OutComponentType = EFurnitureComponentType::Doors;
            }
            else if (OutHitComponent == HitBooth->ClosetDoorMeshSlot0.Get() || OutHitComponent == HitBooth->ClosetDoorMeshSlot1.Get())
            {
                OutComponentType = EFurnitureComponentType::Closet;
            }
            else if (OutHitComponent == HitBooth->CountertopMesh.Get())
            {
                OutComponentType = EFurnitureComponentType::Countertop;
            }
            else if (OutHitComponent == HitBooth->SinkMesh.Get())
            {
                OutComponentType = EFurnitureComponentType::Sink;
            }
            else if (OutHitComponent == HitBooth->FaucetMesh.Get())
            {
                OutComponentType = EFurnitureComponentType::Faucet;
            }
            else if (OutHitComponent == HitBooth->MirrorMesh.Get())
            {
                OutComponentType = EFurnitureComponentType::Mirror;
            }

            CurrentTargetComponent = OutComponentType;
            return (OutComponentType != EFurnitureComponentType::None);
        }
    }

    return false;
}

void AMaxiMallPreviewController::HandleDoubleClickInteraction()
{
    AShowroomBooth* HitBooth = nullptr;
    EFurnitureComponentType ComponentType = EFurnitureComponentType::None;
    UPrimitiveComponent* HitComponent = nullptr;

    const bool bSuccess = TraceFurnitureComponent(HitBooth, ComponentType, HitComponent);

    if (bSuccess)
    {
        if (ComponentType == EFurnitureComponentType::Doors || ComponentType == EFurnitureComponentType::Closet)
        {
            int32 SlotIndex = -1;
            if (HitComponent == HitBooth->DoorMeshSlot0.Get())
            {
                SlotIndex = 0;
            }
            else if (HitComponent == HitBooth->DoorMeshSlot1.Get())
            {
                SlotIndex = 1;
            }
            else if (HitComponent == HitBooth->ClosetDoorMeshSlot0.Get())
            {
                SlotIndex = 2;
            }
            else if (HitComponent == HitBooth->ClosetDoorMeshSlot1.Get())
            {
                SlotIndex = 3;
            }

            if (SlotIndex != -1)
            {
                RequestBoothDoorToggle(HitBooth, SlotIndex);
            }
        }
    }
}

void AMaxiMallPreviewController::FocusPreviewOnComponent(EFurnitureComponentType ComponentType)
{
    if (ActivePreviewActor)
    {
        ActivePreviewActor->SetFocusComponent(ComponentType);
    }
}

void AMaxiMallPreviewController::OnTargetBoothProductChanged(AShowroomBooth* Booth, FName NewProductID)
{
    if (!IsLocalController())
    {
        return;
    }

    if (ActivePreviewActor && Booth && Booth == CurrentTargetBooth)
    {
        FFurnitureProductRow ProductSnapshot;
        if (Booth->GetActiveProductData(ProductSnapshot))
        {
            ActivePreviewActor->LoadProductPreview(ProductSnapshot, Booth->ActiveState, Booth);

            // Isolation and camera pivot handled inside SetFocusComponent.
            if (CurrentTargetComponent != EFurnitureComponentType::None)
            {
                ActivePreviewActor->SetFocusComponent(CurrentTargetComponent);
            }
        }
    }

    if (MainWidgetInstance && MainWidgetInstance->IsInViewport() && Booth == CurrentTargetBooth)
    {
        UConfiguratorMainWidget* MainWidget = Cast<UConfiguratorMainWidget>(MainWidgetInstance);
        if (MainWidget)
        {
            MainWidget->RefreshSelections();
        }
    }
}

void AMaxiMallPreviewController::ToggleConfiguratorUI(AShowroomBooth* Booth, EFurnitureComponentType Component, bool bOpen)
{
    if (!IsLocalController())
    {
        return;
    }

    if (bOpen)
    {
        if (bRightMouseIsDragging)
        {
            return;
        }

        if (!Booth) return;

        if (Component == EFurnitureComponentType::Doors)
        {
            Component = EFurnitureComponentType::Cabinet;
        }

        ResetIgnoreInputFlags();
        SetIgnoreLookInput(true);
        SetIgnoreMoveInput(true);

        if (!MainWidgetClass)
        {
            UE_LOG(LogTemp, Error, TEXT("[PreviewController] ToggleConfiguratorUI failed: MainWidgetClass is null!"));
        }

        if (MainWidgetInstance && CurrentTargetBooth && CurrentTargetBooth != Booth)
        {
            ToggleConfiguratorUI(CurrentTargetBooth, CurrentTargetComponent, false);
        }

        CurrentTargetBooth = Booth;
        CurrentTargetComponent = Component;

        CurrentTargetBooth->OnProductChanged.AddUniqueDynamic(this, &AMaxiMallPreviewController::OnTargetBoothProductChanged);

        if (!MainWidgetInstance && MainWidgetClass)
        {
            MainWidgetInstance = CreateWidget<UUserWidget>(this, MainWidgetClass);
        }

        if (MainWidgetInstance)
        {
            UConfiguratorMainWidget* MainWidget = Cast<UConfiguratorMainWidget>(MainWidgetInstance);
            if (MainWidget)
            {
                MainWidget->SetupWidget(this, Booth, Component);
            }

            if (!MainWidgetInstance->IsInViewport())
            {
                MainWidgetInstance->AddToViewport();
            }

            FInputModeGameAndUI InputMode;
            InputMode.SetWidgetToFocus(MainWidgetInstance->TakeWidget());
            InputMode.SetHideCursorDuringCapture(true);
            InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
            SetInputMode(InputMode);
            bShowMouseCursor = true;
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[PreviewController] ToggleConfiguratorUI failed to create main widget instance."));
        }
    }
    else
    {
        if (CurrentTargetBooth)
        {
            CurrentTargetBooth->OnProductChanged.RemoveAll(this);
        }
        CurrentTargetBooth = nullptr;
        CurrentTargetComponent = EFurnitureComponentType::None;

        bIsClosingUI = true;

        TWeakObjectPtr<AMaxiMallPreviewController> WeakThis(this);
        GetWorld()->GetTimerManager().SetTimerForNextTick([WeakThis]()
        {
            if (AMaxiMallPreviewController* StrongThis = WeakThis.Get())
            {
                StrongThis->ResetIgnoreInputFlags();
                StrongThis->SetIgnoreLookInput(false);
                StrongThis->SetIgnoreMoveInput(false);

                if (StrongThis->MainWidgetInstance)
                {
                    StrongThis->MainWidgetInstance->RemoveFromParent();
                }

                if (StrongThis->ViewmodeOverlayInstance)
                {
                    StrongThis->ViewmodeOverlayInstance->RemoveFromParent();
                }

                FInputModeGameAndUI InputMode;
                InputMode.SetHideCursorDuringCapture(true);
                InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
                StrongThis->SetInputMode(InputMode);
                StrongThis->bShowMouseCursor = true;

                if (FSlateApplication::IsInitialized())
                {
                    ULocalPlayer* LocalPlayer = StrongThis->GetLocalPlayer();
                    if (LocalPlayer)
                    {
                        FSlateApplication::Get().SetUserFocusToGameViewport(LocalPlayer->GetControllerId());
                    }
                }

                StrongThis->bIsClosingUI = false;
            }
        });
    }
}

bool AMaxiMallPreviewController::GetActiveComponentMetadata(EFurnitureComponentType ComponentType, FText& OutProductName, FString& OutSKU, FString& OutURL) const
{
    OutProductName = FText::GetEmpty();
    OutSKU = FString();
    OutURL = FString();

    if (!CurrentTargetBooth)
    {
        return false;
    }

    const FFurnitureProductRow* Row = CurrentTargetBooth->FindProductRow(CurrentTargetBooth->ActiveState.ProductID);
    if (!Row)
    {
        return false;
    }

    if (ComponentType == EFurnitureComponentType::Cabinet || ComponentType == EFurnitureComponentType::Doors)
    {
        int32 TargetSizeIndex = CurrentTargetBooth->ActiveState.ActiveSizeIndex;
        int32 TargetColorIndex = CurrentTargetBooth->ActiveState.ActiveColorIndex;
        
        // 1. Try to fetch metadata directly from the filtered color option
        TArray<FFurnitureColorOption> FilteredColors;
        for (const FFurnitureColorOption& ColorOpt : Row->CabinetOptions.Colors)
        {
            if (ColorOpt.SizeIndices.Num() == 0 || ColorOpt.SizeIndices.Contains(TargetSizeIndex))
            {
                FilteredColors.Add(ColorOpt);
            }
        }
        
        if (FilteredColors.IsValidIndex(TargetColorIndex))
        {
            const FFurnitureColorOption& SelectedColor = FilteredColors[TargetColorIndex];
            if (!SelectedColor.ProductName.IsEmpty() || !SelectedColor.SKU.IsEmpty() || !SelectedColor.URL.IsEmpty())
            {
                OutProductName = SelectedColor.ProductName;
                OutSKU = SelectedColor.SKU;
                OutURL = SelectedColor.URL;
                return true;
            }
        }
        
        // 2. Fall back to legacy CombinationsMetadata mapping
        for (const FFurnitureMetadataEntry& Entry : Row->CabinetOptions.CombinationsMetadata)
        {
            if (Entry.SizeIndex == TargetSizeIndex && Entry.ColorIndex == TargetColorIndex)
            {
                OutProductName = Entry.Metadata.ProductName;
                OutSKU = Entry.Metadata.SKU;
                OutURL = Entry.Metadata.URL;
                return true;
            }
        }
        
        if (Row->CabinetOptions.CombinationsMetadata.Num() > 0)
        {
            const FFurnitureMetadata& Fallback = Row->CabinetOptions.CombinationsMetadata[0].Metadata;
            OutProductName = Fallback.ProductName;
            OutSKU = Fallback.SKU;
            OutURL = Fallback.URL;
            return true;
        }
        
        return false;
    }

    FFurnitureComponentOptions ResolvedOptions;
    const FFurnitureComponentOptions* TargetOptions = nullptr;
    int32 TargetSizeIndex = 0;
    int32 TargetColorIndex = 0;

    if (ComponentType == EFurnitureComponentType::Closet)
    {
        TargetOptions = &Row->ClosetOptions;
        TargetSizeIndex = CurrentTargetBooth->ActiveState.ClosetSizeIndex;
        TargetColorIndex = CurrentTargetBooth->ActiveState.ClosetColorIndex;
    }
    else
    {
        if (CurrentTargetBooth->GetResolvedComponentOptions(ComponentType, ResolvedOptions))
        {
            TargetOptions = &ResolvedOptions;
        }

        switch (ComponentType)
        {
        case EFurnitureComponentType::Countertop:
            TargetSizeIndex = CurrentTargetBooth->ActiveState.CountertopSizeIndex;
            TargetColorIndex = CurrentTargetBooth->ActiveState.ActiveCountertopColorIndex;
            break;
        case EFurnitureComponentType::Sink:
            TargetSizeIndex = CurrentTargetBooth->ActiveState.SinkSizeIndex;
            TargetColorIndex = CurrentTargetBooth->ActiveState.SinkColorIndex;
            break;
        case EFurnitureComponentType::Faucet:
            TargetSizeIndex = CurrentTargetBooth->ActiveState.FaucetSizeIndex;
            TargetColorIndex = CurrentTargetBooth->ActiveState.FaucetColorIndex;
            break;
        case EFurnitureComponentType::Mirror:
            TargetSizeIndex = CurrentTargetBooth->ActiveState.MirrorSizeIndex;
            TargetColorIndex = CurrentTargetBooth->ActiveState.MirrorColorIndex;
            break;
        default:
            return false;
        }
    }

    if (!TargetOptions)
    {
        return false;
    }

    for (const FFurnitureMetadataEntry& Entry : TargetOptions->CombinationsMetadata)
    {
        if (Entry.SizeIndex == TargetSizeIndex && Entry.ColorIndex == TargetColorIndex)
        {
            OutProductName = Entry.Metadata.ProductName;
            OutSKU = Entry.Metadata.SKU;
            OutURL = Entry.Metadata.URL;
            return true;
        }
    }

    if (TargetOptions->CombinationsMetadata.Num() > 0)
    {
        const FFurnitureMetadata& Fallback = TargetOptions->CombinationsMetadata[0].Metadata;
        OutProductName = Fallback.ProductName;
        OutSKU = Fallback.SKU;
        OutURL = Fallback.URL;
        return true;
    }

    return false;
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
            
            // Remove null terminators
            PasteText.ReplaceInline(TEXT("\0"), TEXT(""));

            // 1. Set the OS-level clipboard (fallback)
            FPlatformApplicationMisc::ClipboardCopy(*PasteText);
            LastKnownClipboardContent = PasteText;

            // 2. Direct injection via Slate Character Events (bypasses headless OS clipboard limits)
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

void AMaxiMallPreviewController::SendOpenURLToBrowser(const FString& URL)
{
    UPixelStreamingInput* TargetInput = (ActivePixelStreamingInput != nullptr) ? ActivePixelStreamingInput.Get() : PixelStreamingInput.Get();
    if (TargetInput)
    {
        FString Message = FString::Printf(TEXT("open_url: %s"), *URL);
        UE_LOG(LogTemp, Warning, TEXT("[PixelStreaming] Sending open_url command to browser data channel: %s"), *Message);
        TargetInput->SendPixelStreamingResponse(Message);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[PixelStreaming] Cannot open URL. PixelStreamingInput component is null!"));
    }
}

bool AMaxiMallPreviewController::HasBIMMetadata(UPrimitiveComponent* Component)
{
    if (!Component)
    {
        return false;
    }

    if (Component->ComponentTags.Contains(TEXT("IgnoreBIM")))
    {
        return false;
    }

    if (AActor* OwnerActor = Component->GetOwner())
    {
        if (OwnerActor->ActorHasTag(TEXT("IgnoreBIM")) || OwnerActor->Tags.Contains(TEXT("IgnoreBIM")))
        {
            return false;
        }

        if (Cast<AShowroomBooth>(OwnerActor) || OwnerActor->IsA(AShowroomBooth::StaticClass()) || 
            OwnerActor->GetName().Contains(TEXT("Showroom")) || OwnerActor->GetClass()->GetName().Contains(TEXT("Showroom")))
        {
            return false;
        }
    }

    // 1. Check Component Asset User Data
    if (UDatasmithAssetUserData* AssetUserData = UDatasmithContentBlueprintLibrary::GetDatasmithUserData(Component))
    {
        return true;
    }

    // 2. Check Static Mesh Asset User Data
    if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Component))
    {
        if (UStaticMesh* MeshAsset = SMC->GetStaticMesh())
        {
            if (UDatasmithAssetUserData* MeshUserData = UDatasmithContentBlueprintLibrary::GetDatasmithUserData(MeshAsset))
            {
                return true;
            }
        }
    }

    // 3. Check Parent Actor Asset User Data
    if (AActor* OwnerActor = Component->GetOwner())
    {
        if (UDatasmithAssetUserData* ActorUserData = UDatasmithContentBlueprintLibrary::GetDatasmithUserData(OwnerActor))
        {
            return true;
        }
    }

    // Fallback: If component is part of a Datasmith imported mesh/actor
    if (AActor* OwnerActor = Component->GetOwner())
    {
        if (OwnerActor->GetName().Contains(TEXT("Datasmith")) || OwnerActor->GetClass()->GetName().Contains(TEXT("Datasmith")))
        {
            return true;
        }
    }

    return false;
}

void AMaxiMallPreviewController::SelectComponent(UPrimitiveComponent* ComponentToSelect)
{
    // Never allow BIM selection or BIM events while inside Viewmode
    if (ActivePreviewActor != nullptr && ComponentToSelect != nullptr)
    {
        return;
    }

    UPrimitiveComponent* PrevSelected = SelectedComponent.Get();

    // Clear custom depth from previously selected component if it's no longer hovered
    if (PrevSelected && PrevSelected != ComponentToSelect && PrevSelected != HoveredComponent.Get())
    {
        PrevSelected->SetRenderCustomDepth(false);
    }
    else if (PrevSelected && PrevSelected != ComponentToSelect && PrevSelected == HoveredComponent.Get())
    {
        // Revert to hover stencil (1)
        PrevSelected->SetRenderCustomDepth(true);
        PrevSelected->SetCustomDepthStencilValue(1);
    }

    SelectedComponent = ComponentToSelect;

    if (ComponentToSelect && HasBIMMetadata(ComponentToSelect))
    {
        // Apply persistent selection stencil (2)
        ComponentToSelect->SetRenderCustomDepth(true);
        ComponentToSelect->SetCustomDepthStencilValue(2);

        if (!BIMInspectorInstance && BIMInspectorClass)
        {
            BIMInspectorInstance = CreateWidget<UUserWidget>(this, BIMInspectorClass);
        }
        if (BIMInspectorInstance)
        {
            if (!BIMInspectorInstance->IsInViewport())
            {
                BIMInspectorInstance->AddToViewport();
            }
            if (UBIMInspectorWidget* BIMWidget = Cast<UBIMInspectorWidget>(BIMInspectorInstance))
            {
                BIMWidget->RefreshBIMData(ComponentToSelect);
            }

            bShowMouseCursor = true;
            FInputModeGameAndUI InputMode;
            InputMode.SetWidgetToFocus(BIMInspectorInstance->TakeWidget());
            InputMode.SetHideCursorDuringCapture(true);
            InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
            SetInputMode(InputMode);
        }
    }
    else
    {
        if (BIMInspectorInstance && BIMInspectorInstance->IsInViewport())
        {
            BIMInspectorInstance->RemoveFromParent();

            FInputModeGameAndUI InputMode;
            InputMode.SetHideCursorDuringCapture(true);
            InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
            SetInputMode(InputMode);
        }
    }

    OnComponentSelected(ComponentToSelect);
    OnComponentSelectedDelegate.Broadcast(ComponentToSelect);
}

UPrimitiveComponent* AMaxiMallPreviewController::GetSelectedComponent() const
{
    return SelectedComponent.Get();
}

bool AMaxiMallPreviewController::GetBIMElementData(UPrimitiveComponent* Component, FBIMElementData& OutData)
{
    OutData = FBIMElementData();

    if (!Component)
    {
        return false;
    }

    OutData.ElementName = Component->GetName();
    if (AActor* Owner = Component->GetOwner())
    {
        OutData.ElementName = Owner->GetName();
    }

    TMap<FName, FString> RawMap;

    auto ExtractMetaDataFromObj = [&RawMap](UObject* Obj)
    {
        if (!Obj) return;
        if (UDatasmithAssetUserData* CompData = UDatasmithContentBlueprintLibrary::GetDatasmithUserData(Obj))
        {
            RawMap.Append(CompData->MetaData);
        }
        if (IInterface_AssetUserData* Interface = Cast<IInterface_AssetUserData>(Obj))
        {
            if (const TArray<UAssetUserData*>* UserDataArray = Interface->GetAssetUserDataArray())
            {
                for (UAssetUserData* UserData : *UserDataArray)
                {
                    if (UDatasmithAssetUserData* DatasmithData = Cast<UDatasmithAssetUserData>(UserData))
                    {
                        RawMap.Append(DatasmithData->MetaData);
                    }
                }
            }
        }
    };

    ExtractMetaDataFromObj(Component);

    if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Component))
    {
        ExtractMetaDataFromObj(SMC->GetStaticMesh());
    }

    ExtractMetaDataFromObj(Component->GetOwner());

    for (const TPair<FName, FString>& Pair : RawMap)
    {
        FString KeyStr = Pair.Key.ToString();
        FString ValStr = Pair.Value;

        FBIMMetadataPair RawPair;
        RawPair.Key = KeyStr;
        RawPair.Value = ValStr;
        OutData.RawMetadata.Add(RawPair);

        FString CleanKey = KeyStr;
        if (CleanKey.StartsWith(TEXT("Element=")))
        {
            CleanKey.RightChopInline(8);
        }
        else if (CleanKey.StartsWith(TEXT("Type=")))
        {
            CleanKey.RightChopInline(5);
        }

        FBIMMetadataPair CleanPair;
        CleanPair.Key = CleanKey;
        CleanPair.Value = ValStr;

        if (CleanKey.Equals(TEXT("Category"), ESearchCase::IgnoreCase) || CleanKey.Contains(TEXT("Category")))
        {
            OutData.Category = ValStr;
        }
        else if (CleanKey.Equals(TEXT("Family"), ESearchCase::IgnoreCase) || CleanKey.Contains(TEXT("Family")))
        {
            OutData.FamilyName = ValStr;
        }
        else if (CleanKey.Equals(TEXT("Type"), ESearchCase::IgnoreCase))
        {
            OutData.TypeName = ValStr;
        }
        else if (CleanKey.Equals(TEXT("IfcGUID"), ESearchCase::IgnoreCase))
        {
            OutData.IfcGUID = ValStr;
        }
        else if (CleanKey.Contains(TEXT("Area")) || CleanKey.Contains(TEXT("Height")) || CleanKey.Contains(TEXT("Length")) || CleanKey.Contains(TEXT("Width")) || CleanKey.Contains(TEXT("breedte")))
        {
            OutData.Dimensions.Add(CleanPair);
        }
        else
        {
            OutData.Specifications.Add(CleanPair);
        }
    }

    // Fail-safe fallbacks: Ensure Dimensions and Title are NEVER empty
    if (OutData.Dimensions.Num() == 0)
    {
        OutData.Dimensions = OutData.RawMetadata;
    }

    if (OutData.FamilyName.IsEmpty())
    {
        OutData.FamilyName = OutData.ElementName;
    }
    if (OutData.Category.IsEmpty())
    {
        OutData.Category = TEXT("BIM Element");
    }
    if (OutData.TypeName.IsEmpty())
    {
        OutData.TypeName = OutData.ElementName;
    }

    return true;
}

int32 AMaxiMallPreviewController::SelectAllComponentsOfCategory(const FString& CategoryName)
{
    SelectComponent(nullptr);

    for (int32 i = MultiSelectedComponents.Num() - 1; i >= 0; --i)
    {
        if (UPrimitiveComponent* Comp = MultiSelectedComponents[i].Get())
        {
            Comp->SetRenderCustomDepth(false);
        }
    }
    MultiSelectedComponents.Empty();

    if (CategoryName.IsEmpty() || !GetWorld())
    {
        return 0;
    }

    int32 Count = 0;
    for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
    {
        UPrimitiveComponent* Comp = *It;
        if (Comp && Comp->GetWorld() == GetWorld() && !Comp->IsBeingDestroyed())
        {
            FBIMElementData Data;
            if (GetBIMElementData(Comp, Data))
            {
                if (Data.Category.Equals(CategoryName, ESearchCase::IgnoreCase))
                {
                    Comp->SetRenderCustomDepth(true);
                    Comp->SetCustomDepthStencilValue(2);
                    MultiSelectedComponents.Add(Comp);
                    Count++;
                }
            }
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("[BIM Category Selection] Highlighted %d elements for category '%s'"), Count, *CategoryName);
    return Count;
}

void AMaxiMallPreviewController::CalculateSelectedQuantity(float& OutTotalAreaM2, float& OutTotalLengthM) const
{
    OutTotalAreaM2 = 0.f;
    OutTotalLengthM = 0.f;

    TArray<UPrimitiveComponent*> Targets;
    if (SelectedComponent.IsValid())
    {
        Targets.Add(SelectedComponent.Get());
    }
    for (const TObjectPtr<UPrimitiveComponent>& CompObj : MultiSelectedComponents)
    {
        if (CompObj && !Targets.Contains(CompObj.Get()))
        {
            Targets.Add(CompObj.Get());
        }
    }

    for (UPrimitiveComponent* Comp : Targets)
    {
        FBIMElementData Data;
        if (GetBIMElementData(Comp, Data))
        {
            for (const FBIMMetadataPair& Dim : Data.Dimensions)
            {
                if (Dim.Key.Contains(TEXT("Area")))
                {
                    FString RawVal = Dim.Value;
                    RawVal.ReplaceInline(TEXT("m²"), TEXT(""));
                    RawVal.ReplaceInline(TEXT("m2"), TEXT(""));
                    RawVal.TrimStartAndEndInline();
                    OutTotalAreaM2 += FCString::Atof(*RawVal);
                }
                else if (Dim.Key.Contains(TEXT("Length")))
                {
                    FString RawVal = Dim.Value;
                    RawVal.TrimStartAndEndInline();
                    float Val = FCString::Atof(*RawVal);
                    if (Val > 100.f)
                    {
                        Val /= 1000.f;
                    }
                    OutTotalLengthM += Val;
                }
            }
        }
    }
}
