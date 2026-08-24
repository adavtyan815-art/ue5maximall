#include "awsTutorial_PlayerController.h"
#include "UObject/UObjectIterator.h"

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
#include "Blueprint/WidgetTree.h"
#include "FurnitureConfigurator/UI/ConfiguratorMainWidget.h"
#include "FurnitureConfigurator/UI/ViewmodeOverlayWidget.h"
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
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Constructor/RoomPlannerManager.h"
#include "FurnitureConfigurator/UI/RoomPlannerWidget.h"

AAwsTutorial_PlayerController::AAwsTutorial_PlayerController()
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

    PixelStreamingInput = CreateDefaultSubobject<UPixelStreamingInput>(TEXT("PixelStreamingInputComponent"));
}

// ─────────────────────────────────────────────────────────────────────────────
// SendDiag_PC — sends a DIAG diagnostic string to the browser via PS data channel.
// Works in Shipping builds (no UE_LOG dependency for browser visibility).
// ─────────────────────────────────────────────────────────────────────────────
static void SendDiag_PC(UPixelStreamingInput* PSInput, const FString& Message)
{
    if (!PSInput)
    {
        return;
    }
    const FString Payload = FString::Printf(TEXT("DIAG: %s"), *Message);
    PSInput->SendPixelStreamingResponse(Payload);
    UE_LOG(LogTemp, Warning, TEXT("[awsTutorial|PC|DIAG] %s"), *Message);
}

void AAwsTutorial_PlayerController::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogTemp, Warning, TEXT("[awsTutorial|PC] ===== BeginPlay START =====  IsLocalController: %s"),
        IsLocalController() ? TEXT("YES") : TEXT("NO"));

    if (IsLocalController())
    {
        ARoomPlannerManager::GetOrCreateInstance(GetWorld());
        // ─────────────────────────────────────────────────────────────
        // FIX 2 APPLIED: No CreateDefaultSubobject (removed from ctor).
        //
        // The PS plugin attaches UPixelStreamingInput to this controller ONLY
        // when a browser connects via WebRTC. This happens AFTER BeginPlay on
        // a persistent server — so GetComponentByClass may return null here.
        //
        // Strategy:
        //   1. Try once in BeginPlay (handles pre-connected / single-session builds).
        //   2. If not found, PlayerTick retries every tick until it is found.
        //      AddUniqueDynamic prevents duplicate bindings (the old per-tick scan
        //      used plain AddDynamic, which stacked duplicates every tick and caused
        //      the PS handshake to process messages twice → kickbacks).
        // ─────────────────────────────────────────────────────────────
        if (!PixelStreamingInput)
        {
            PixelStreamingInput = GetComponentByClass<UPixelStreamingInput>();
        }

        if (PixelStreamingInput)
        {
            PixelStreamingInput->OnInputEvent.AddUniqueDynamic(
                this, &AAwsTutorial_PlayerController::OnPixelStreamingInput);

            UE_LOG(LogTemp, Warning,
                TEXT("[awsTutorial|PC] PS Input component bound in BeginPlay."));

            SendDiag_PC(PixelStreamingInput,
                TEXT("[PC] BeginPlay OK"
                     " | FIX1: shader-pipeline Fast-batch removed — no game-thread stall"
                     " | FIX2: PS component bound in BeginPlay via GetComponentByClass"
                     " | Level transition should be < 5 s now."));
        }
        else
        {
            // Component not yet attached — PlayerTick will retry once per tick.
            UE_LOG(LogTemp, Warning,
                TEXT("[awsTutorial|PC] PS Input NOT found in BeginPlay — will retry in PlayerTick."));
        }

        // Force Epic quality scalability settings to prevent virtual server fallbacks
        if (UGameUserSettings* GameUserSettings = UGameUserSettings::GetGameUserSettings())
        {
            if (GameUserSettings->GetOverallScalabilityLevel() != 3)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[awsTutorial|PC] Overriding scalability level to Epic (3)."));
                GameUserSettings->SetOverallScalabilityLevel(3);
                GameUserSettings->ApplySettings(false);

                SendDiag_PC(PixelStreamingInput,
                    TEXT("[PC] Scalability set to Epic (3) — quality override applied."));
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[awsTutorial|PC] GameUserSettings is NULL!"));
        }

        FInputModeGameAndUI InputMode;
        InputMode.SetHideCursorDuringCapture(true);
        InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        SetInputMode(InputMode);
        bShowMouseCursor = true;
        bEnableClickEvents = true;
        bEnableMouseOverEvents = true;

        UE_LOG(LogTemp, Warning,
            TEXT("[awsTutorial|PC] ===== BeginPlay END — input mode set, cursor enabled. ====="));
    }
}

static bool IsWidgetHoveredGeometrically(UUserWidget* Widget)
{
    if (!Widget || !Widget->IsInViewport()) return false;

    if (FSlateApplication::IsInitialized())
    {
        FVector2D CursorPos = FSlateApplication::Get().GetCursorPos();

        // If it's a ConfiguratorMainWidget, check if the mouse is hovering its interactive panel/children
        if (UConfiguratorMainWidget* CfgWidget = Cast<UConfiguratorMainWidget>(Widget))
        {
            bool bChildHovered = false;
            if (CfgWidget->WidgetTree)
            {
                CfgWidget->WidgetTree->ForEachWidget([&](UWidget* Child)
                {
                    if (bChildHovered || !Child) return;
                    if (Child == CfgWidget->GetRootWidget()) return;

                    if (Child->GetVisibility() == ESlateVisibility::Visible ||
                        Child->GetVisibility() == ESlateVisibility::HitTestInvisible ||
                        Child->GetVisibility() == ESlateVisibility::SelfHitTestInvisible)
                    {
                        TSharedPtr<SWidget> CachedSWidget = Child->GetCachedWidget();
                        if (CachedSWidget.IsValid())
                        {
                            FGeometry ChildGeo = CachedSWidget->GetTickSpaceGeometry();
                            FVector2D LocalPos = ChildGeo.AbsoluteToLocal(CursorPos);
                            FVector2D LocalSize = ChildGeo.GetLocalSize();
                            if (LocalSize.X > 0.f && LocalSize.Y > 0.f &&
                                LocalSize.X < 900.f && // Exclude full-width root containers
                                LocalPos.X >= 0.f && LocalPos.X <= LocalSize.X &&
                                LocalPos.Y >= 0.f && LocalPos.Y <= LocalSize.Y)
                            {
                                bChildHovered = true;
                            }
                        }
                    }
                });
            }
            return bChildHovered;
        }

        if (Widget->IsHovered()) return true;

        FGeometry WidgetGeo = Widget->GetCachedGeometry();
        FVector2D LocalPos = WidgetGeo.AbsoluteToLocal(CursorPos);
        FVector2D LocalSize = WidgetGeo.GetLocalSize();

        if (LocalSize.X > 0.f && LocalSize.Y > 0.f)
        {
            if (LocalPos.X >= 0.f && LocalPos.X <= LocalSize.X &&
                LocalPos.Y >= 0.f && LocalPos.Y <= LocalSize.Y)
            {
                return true;
            }
        }
    }
    return false;
}

void AAwsTutorial_PlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);

    if (!IsLocalController())
    {
        return;
    }

    // в”Ђв”Ђ PS Input late-bind retry в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
    // If BeginPlay ran before the PS plugin attached the component (common on
    // persistent servers where the browser connects after level load), we retry
    // here. AddUniqueDynamic guarantees no duplicate bindings even if called
    // more than once. Once PixelStreamingInput is valid, this block is skipped
    // every subsequent tick (zero overhead).
    if (!PixelStreamingInput)
    {
        for (TObjectIterator<UPixelStreamingInput> It; It; ++It)
        {
            if (UPixelStreamingInput* PSInput = *It)
            {
                PixelStreamingInput = PSInput;
                PixelStreamingInput->OnInputEvent.AddUniqueDynamic(
                    this, &AAwsTutorial_PlayerController::OnPixelStreamingInput);
                UE_LOG(LogTemp, Warning,
                    TEXT("[awsTutorial|PC] PS Input component found via TObjectIterator and bound!"));
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
            
            // FIX 2: ActivePixelStreamingInput removed вЂ” single cached PixelStreamingInput.
            UPixelStreamingInput* TargetInput = PixelStreamingInput.Get();
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

    // в”Ђв”Ђ 2D Dynamic Drag-to-Draw Wall Handling в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
    ARoomPlannerManager* PlannerManager = ARoomPlannerManager::GetOrCreateInstance(GetWorld());
    if (PlannerManager && PlannerManager->Is2DModeActive())
    {
        FVector GroundPos;
        FVector WorldOrigin, WorldDirection;
        if (DeprojectMousePositionToWorld(WorldOrigin, WorldDirection) && !FMath::IsNearlyZero(WorldDirection.Z))
        {
            float t = -WorldOrigin.Z / WorldDirection.Z;
            if (t >= 0.f)
            {
                GroundPos = WorldOrigin + t * WorldDirection;
                GroundPos.Z = 0.f;

                if (PlannerManager->ActiveToolMode == EPlannerToolMode::DrawWall)
                {
                    if (IsInputKeyDown(EKeys::LeftMouseButton))
                    {
                        if (!bIs2DDrawingWall)
                        {
                            bIs2DDrawingWall = true;
                            PlannerManager->StartInteractiveWallDraw(GroundPos);
                        }
                        else
                        {
                            PlannerManager->UpdateInteractiveWallDraw(GroundPos);
                        }
                    }
                    else if (bIs2DDrawingWall)
                    {
                        bIs2DDrawingWall = false;
                        FVector2D P1(PlannerManager->GetDragStartPoint().X, PlannerManager->GetDragStartPoint().Y);
                        FVector2D P2(PlannerManager->GetDragCurrentPoint().X, PlannerManager->GetDragCurrentPoint().Y);
                        PlannerManager->CommitInteractiveWallDraw();

                        if (FVector2D::Distance(P1, P2) >= 10.f)
                        {
                            Server_CommitWall(P1, P2);
                        }
                    }
                    else
                    {
                        PlannerManager->CheckHoverSnapHint(GroundPos);
                    }
                }
                else if (PlannerManager->ActiveToolMode == EPlannerToolMode::Select)
                {
                    if (WasInputKeyJustPressed(EKeys::LeftMouseButton))
                    {
                        PlannerManager->SelectWallAtWorldPos(GroundPos);
                    }
                    else if (IsInputKeyDown(EKeys::LeftMouseButton) && PlannerManager->SelectedSegmentID != -1 && PlannerManager->SelectedOpeningIndex != -1)
                    {
                        PlannerManager->DragSelectedOpeningToWorldPos(GroundPos);
                    }
                    else if (WasInputKeyJustReleased(EKeys::LeftMouseButton) && PlannerManager->SelectedSegmentID != -1 && PlannerManager->SelectedOpeningIndex != -1)
                    {
                        float OpeningDist = 0.f;
                        if (PlannerManager->GetOpeningDistance(PlannerManager->SelectedSegmentID, PlannerManager->SelectedOpeningIndex, OpeningDist))
                        {
                            Server_UpdateOpeningPosition(PlannerManager->SelectedSegmentID, PlannerManager->SelectedOpeningIndex, OpeningDist);
                        }
                    }
                }
                else if (PlannerManager->ActiveToolMode == EPlannerToolMode::Erase)
                {
                    if (WasInputKeyJustPressed(EKeys::LeftMouseButton))
                    {
                        int32 TargetSeg = PlannerManager->SelectWallAtWorldPos(GroundPos);
                        if (TargetSeg != -1)
                        {
                            Server_DeleteWall(TargetSeg);
                        }
                    }
                }

                if (WasInputKeyJustPressed(EKeys::Delete) || WasInputKeyJustPressed(EKeys::BackSpace))
                {
                    if (PlannerManager->SelectedSegmentID != -1)
                    {
                        if (PlannerManager->SelectedOpeningIndex != -1)
                        {
                            Server_DeleteOpening(PlannerManager->SelectedSegmentID, PlannerManager->SelectedOpeningIndex);
                        }
                        else
                        {
                            Server_DeleteWall(PlannerManager->SelectedSegmentID);
                        }
                    }
                }
            }
        }
        return;
    }

    UPrimitiveComponent* NewHoveredComp = nullptr;
    AShowroomBooth* HitBooth = nullptr;
    bool bHoveringShowroom = false;
    
    bool bIsMouseOverUI = IsWidgetHoveredGeometrically(MainWidgetInstance);

    bool bIsMouseDown = IsInputKeyDown(EKeys::LeftMouseButton) || IsInputKeyDown(EKeys::RightMouseButton) || bRightMouseIsDragging;

    if (!ActivePreviewActor && !bIsMouseOverUI && !bIsMouseDown)
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
        }
    }

    const bool bIsAnyHovered = bHoveringShowroom;
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

    // в”Ђв”Ђ Pixel Streaming cursor data-channel broadcast в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
    if (bIsAnyHovered != bWasHoveringInteractable)
    {
        BroadcastCursorState(bIsAnyHovered);
        bWasHoveringInteractable = bIsAnyHovered;
    }
}

void AAwsTutorial_PlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();

    if (InputComponent)
    {
        InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AAwsTutorial_PlayerController::OnLeftMouseButtonDown);
        InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AAwsTutorial_PlayerController::OnLeftMouseButtonReleased);
    }
}

void AAwsTutorial_PlayerController::OnLeftMouseButtonDown()
{
    LMBPressTime = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.f;
    if (FSlateApplication::IsInitialized())
    {
        LMBPressMousePos = FSlateApplication::Get().GetCursorPos();
    }
}

void AAwsTutorial_PlayerController::OnLeftMouseButtonReleased()
{
    float CurrentTime = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.f;
    float HeldDuration = CurrentTime - LMBPressTime;

    FVector2D CurrentMousePos = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCursorPos() : FVector2D::ZeroVector;
    float DragDistance = FVector2D::Distance(CurrentMousePos, LMBPressMousePos);

    if (HeldDuration <= 0.25f && DragDistance <= 8.f)
    {
        OnLeftMouseButtonClicked();
    }
}

void AAwsTutorial_PlayerController::AddYawInput(float Val)
{
    Super::AddYawInput(Val);

    if (Val != 0.f && IsInputKeyDown(EKeys::RightMouseButton))
    {
        bRightMouseIsDragging = true;
    }
}

void AAwsTutorial_PlayerController::AddPitchInput(float Val)
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

void AAwsTutorial_PlayerController::BroadcastCursorState(bool bHovering)
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

void AAwsTutorial_PlayerController::OnLeftMouseButtonClicked()
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

    bool bIsMouseOverUI = IsWidgetHoveredGeometrically(MainWidgetInstance);

    // Single-click selection for BIM elements & furniture components
    if (!ActivePreviewActor && !bIsMouseOverUI)
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
                // Showroom single left-click does NOT open UI or component selection.
                // Double-click interaction handles opening View Mode.
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

FString AAwsTutorial_PlayerController::GetRequestURL() const {
	UNetConnection* netConnection = GetNetConnection();
	if (netConnection == NULL) return FString();
	return netConnection->RequestURL;
}

TArray<FString> AAwsTutorial_PlayerController::GetRequestOptions() const {
	UNetConnection* netConnection = GetNetConnection();
	if (netConnection == NULL) return TArray<FString>();
	FURL InURL( NULL, *netConnection->RequestURL, TRAVEL_Absolute );
	return InURL.Op;
}

bool AAwsTutorial_PlayerController::HasRequestOption(const FString& key) const {
	UNetConnection* netConnection = GetNetConnection();
	if (netConnection == NULL) return false;
	FURL InURL( NULL, *netConnection->RequestURL, TRAVEL_Absolute );
	return InURL.HasOption(*key);
}

FString AAwsTutorial_PlayerController::GetRequestOption(const FString& key) const {
	UNetConnection* netConnection = GetNetConnection();
	if (netConnection == NULL) return FString("");
	FURL InURL( NULL, *netConnection->RequestURL, TRAVEL_Absolute );
	const TCHAR* o = InURL.GetOption(*key, NULL);
	if (o == NULL) return FString("");
	if (o[0] == '=') return FString(o + 1);
	return FString(o);
}

void AAwsTutorial_PlayerController::Kick_Implementation() {
	UNetConnection* netConnection = GetNetConnection();
	if (netConnection == NULL) return;

	netConnection->Close(UE::Net::FNetCloseResult());
}

// РІвЂќР‚РІвЂќР‚ CONFIGURATOR PREVIEW MANAGEMENT РІвЂќР‚РІвЂќР‚

void AAwsTutorial_PlayerController::OpenFurniturePreview(AShowroomBooth* TargetBooth, EFurnitureComponentType FocusComponent)
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

    // NOTE: the level's PostProcessVolumes are deliberately left ENABLED during
    // View Mode. Exposure, bloom and grading on the previewed mesh must match the
    // level (this is most of what makes shiny materials read the same), and the
    // preview camera's own stencil-isolation blendable handles the outline/dim
    // without needing the world volume switched off.

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
    CurrentTargetBooth->OnProductChanged.AddDynamic(this, &AAwsTutorial_PlayerController::OnTargetBoothProductChanged);

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

void AAwsTutorial_PlayerController::CloseFurniturePreview()
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

    // (PostProcessVolumes are no longer touched on entry, so there is nothing to
    // re-enable here — the level's post processing ran untouched throughout.)

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
        CurrentTargetBooth->OnProductChanged.AddUniqueDynamic(this, &AAwsTutorial_PlayerController::OnTargetBoothProductChanged);

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

        TWeakObjectPtr<AAwsTutorial_PlayerController> WeakThis(this);
        GetWorld()->GetTimerManager().SetTimerForNextTick([WeakThis]()
        {
            if (AAwsTutorial_PlayerController* StrongThis = WeakThis.Get())
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

void AAwsTutorial_PlayerController::HandlePreviewOrbitInput(float DeltaYaw, float DeltaPitch)
{
    if (!ActivePreviewActor)
    {
        return;
    }

    ActivePreviewActor->RotatePreview(
        DeltaYaw * OrbitSensitivity,
        DeltaPitch * OrbitSensitivity);
}

void AAwsTutorial_PlayerController::HandlePreviewZoomInput(float DeltaZoom)
{
    if (ActivePreviewActor)
    {
        ActivePreviewActor->ZoomPreview(DeltaZoom);
    }
}

void AAwsTutorial_PlayerController::ResetPreviewRotation()
{
    if (ActivePreviewActor)
    {
        ActivePreviewActor->ResetRotation();
    }
}

bool AAwsTutorial_PlayerController::IsPreviewActive() const
{
    return ActivePreviewActor != nullptr;
}

void AAwsTutorial_PlayerController::RequestBoothProductChange(AShowroomBooth* TargetBooth, FName NewProductID)
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

void AAwsTutorial_PlayerController::RequestBoothDoorToggle(AShowroomBooth* TargetBooth, int32 SlotIndex)
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

void AAwsTutorial_PlayerController::RequestBoothComponentSelection(AShowroomBooth* TargetBooth, EFurnitureComponentType ComponentType, int32 SizeIndex, int32 ColorIndex)
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

void AAwsTutorial_PlayerController::RequestBoothCustomColorChange(AShowroomBooth* TargetBooth, EFurnitureComponentType ComponentType, FLinearColor Color, UMaterialInterface* OverrideMaterial)
{
    if (!TargetBooth)
    {
        return;
    }

    if (GetLocalRole() == ROLE_Authority)
    {
        TargetBooth->RequestCustomColorChange(ComponentType, Color, OverrideMaterial);
    }
    else
    {
        Server_RequestBoothCustomColorChange(TargetBooth, ComponentType, Color, OverrideMaterial);
    }
}

void AAwsTutorial_PlayerController::Server_RequestBoothDoorToggle_Implementation(AShowroomBooth* TargetBooth, int32 SlotIndex)
{
    if (TargetBooth)
    {
        TargetBooth->RequestDoorToggle(SlotIndex);
    }
}

bool AAwsTutorial_PlayerController::Server_RequestBoothDoorToggle_Validate(AShowroomBooth* TargetBooth, int32 SlotIndex)
{
    return true;
}

void AAwsTutorial_PlayerController::Server_RequestBoothProductChange_Implementation(AShowroomBooth* TargetBooth, FName NewProductID)
{
    if (TargetBooth)
    {
        TargetBooth->RequestProductChange(NewProductID);
    }
}

bool AAwsTutorial_PlayerController::Server_RequestBoothProductChange_Validate(AShowroomBooth* TargetBooth, FName NewProductID)
{
    return true;
}

void AAwsTutorial_PlayerController::Server_RequestBoothComponentSelection_Implementation(AShowroomBooth* TargetBooth, EFurnitureComponentType ComponentType, int32 SizeIndex, int32 ColorIndex)
{
    if (TargetBooth)
    {
        TargetBooth->RequestComponentSelection(ComponentType, SizeIndex, ColorIndex);
    }
}

bool AAwsTutorial_PlayerController::Server_RequestBoothComponentSelection_Validate(AShowroomBooth* TargetBooth, EFurnitureComponentType ComponentType, int32 SizeIndex, int32 ColorIndex)
{
    return true;
}

void AAwsTutorial_PlayerController::Server_RequestBoothCustomColorChange_Implementation(AShowroomBooth* TargetBooth, EFurnitureComponentType ComponentType, FLinearColor Color, UMaterialInterface* OverrideMaterial)
{
    if (TargetBooth)
    {
        TargetBooth->RequestCustomColorChange(ComponentType, Color, OverrideMaterial);
    }
}

bool AAwsTutorial_PlayerController::Server_RequestBoothCustomColorChange_Validate(AShowroomBooth* TargetBooth, EFurnitureComponentType ComponentType, FLinearColor Color, UMaterialInterface* OverrideMaterial)
{
    return true;
}

void AAwsTutorial_PlayerController::Server_LoadBoothState_Implementation(AShowroomBooth* TargetBooth, FShowroomBoothConfigState State, const TArray<FCustomColorOverride>& InCustomColors, const TArray<EDoorSlotState>& InDoorStates)
{
    if (TargetBooth)
    {
        UE_LOG(LogTemp, Warning, TEXT("[SaveSystem][Server] Loading booth state for '%s' (Product: '%s', CustomColors: %d, DoorStates: %d)"), 
            *TargetBooth->GetName(), *State.ProductID.ToString(), InCustomColors.Num(), InDoorStates.Num());

        TargetBooth->LoadBoothFullState(State, InCustomColors, InDoorStates);
    }
}

bool AAwsTutorial_PlayerController::Server_LoadBoothState_Validate(AShowroomBooth* TargetBooth, FShowroomBoothConfigState State, const TArray<FCustomColorOverride>& InCustomColors, const TArray<EDoorSlotState>& InDoorStates)
{
    return true;
}

bool AAwsTutorial_PlayerController::TraceFurnitureComponent(AShowroomBooth*& OutBooth, EFurnitureComponentType& OutComponentType, UPrimitiveComponent*& OutHitComponent)
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
    if (IsWidgetHoveredGeometrically(MainWidgetInstance))
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

void AAwsTutorial_PlayerController::HandleDoubleClickInteraction()
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

void AAwsTutorial_PlayerController::FocusPreviewOnComponent(EFurnitureComponentType ComponentType)
{
    if (ActivePreviewActor)
    {
        ActivePreviewActor->SetFocusComponent(ComponentType);
    }
}

void AAwsTutorial_PlayerController::OnTargetBoothProductChanged(AShowroomBooth* Booth, FName NewProductID)
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

void AAwsTutorial_PlayerController::ToggleConfiguratorUI(AShowroomBooth* Booth, EFurnitureComponentType Component, bool bOpen)
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

        CurrentTargetBooth->OnProductChanged.AddUniqueDynamic(this, &AAwsTutorial_PlayerController::OnTargetBoothProductChanged);

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

        TWeakObjectPtr<AAwsTutorial_PlayerController> WeakThis(this);
        GetWorld()->GetTimerManager().SetTimerForNextTick([WeakThis]()
        {
            if (AAwsTutorial_PlayerController* StrongThis = WeakThis.Get())
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

void AAwsTutorial_PlayerController::ToggleRoomPlannerUI(bool bOpen)
{
    if (!IsLocalController())
    {
        return;
    }

    if (bOpen)
    {
        if (!RoomPlannerInstance && RoomPlannerClass)
        {
            RoomPlannerInstance = CreateWidget<UUserWidget>(this, RoomPlannerClass);
        }

        if (RoomPlannerInstance)
        {
            if (URoomPlannerWidget* PlannerWidget = Cast<URoomPlannerWidget>(RoomPlannerInstance))
            {
                PlannerWidget->PlannerRelocationLocation = RoomPlannerRelocationLocation;
            }

            if (!RoomPlannerInstance->IsInViewport())
            {
                RoomPlannerInstance->AddToViewport(10);
            }
        }
    }
    else
    {
        if (RoomPlannerInstance)
        {
            if (URoomPlannerWidget* PlannerWidget = Cast<URoomPlannerWidget>(RoomPlannerInstance))
            {
                PlannerWidget->ClosePlanner();
            }
            else
            {
                RoomPlannerInstance->RemoveFromParent();
            }
            RoomPlannerInstance = nullptr;
        }
    }
}

bool AAwsTutorial_PlayerController::GetActiveComponentMetadata(EFurnitureComponentType ComponentType, FText& OutProductName, FString& OutSKU, FString& OutURL) const
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

void AAwsTutorial_PlayerController::OnPixelStreamingInput(const FString& Descriptor)
{
    UE_LOG(LogTemp, Warning,
        TEXT("[awsTutorial|PC] OnPixelStreamingInput fired. Descriptor length: %d"), Descriptor.Len());

    // в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
    // LEGACY FORMAT (backward-compat): the old player.js sends a plain-text
    // string starting with "MaxiMallPaste " followed by clipboard text.
    // We must handle this BEFORE attempting JSON deserialization so we do NOT
    // break existing paste functionality when player.js has not been updated.
    // в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
    static const FString LegacyPrefix = TEXT("MaxiMallPaste ");
    if (Descriptor.StartsWith(LegacyPrefix))
    {
        FString PasteText = Descriptor.Mid(LegacyPrefix.Len());
        PasteText.ReplaceInline(TEXT("\0"), TEXT(""));

        UE_LOG(LogTemp, Warning,
            TEXT("[awsTutorial|PC] [LEGACY] ClipboardPaste (plain-text). Character count: %d"), PasteText.Len());

        FPlatformApplicationMisc::ClipboardCopy(*PasteText);
        LastKnownClipboardContent = PasteText;

        if (FSlateApplication::IsInitialized())
        {
            FSlateApplication& SlateApp = FSlateApplication::Get();
            for (int32 i = 0; i < PasteText.Len(); ++i)
            {
                FCharacterEvent CharEvent(PasteText[i], FModifierKeysState(), 0, false);
                SlateApp.ProcessKeyCharEvent(CharEvent);
            }
            UE_LOG(LogTemp, Warning,
                TEXT("[awsTutorial|PC] [LEGACY] Injected %d chars via Slate."), PasteText.Len());
        }
        return;
    }

    // в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
    // JSON FORMAT: new browser side sends {"Cmd":"ClipboardPaste","Text":"..."}
    // в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
    UE_LOG(LogTemp, Warning, TEXT("[MaxiMallConstructor] OnPixelStreamingInput raw Descriptor: %s"), *Descriptor);

    FString CleanDescriptor = Descriptor;
    CleanDescriptor.ReplaceInline(TEXT("\0"), TEXT(""));
    if (CleanDescriptor.StartsWith(TEXT("UIInteraction:")))
    {
        CleanDescriptor = CleanDescriptor.Mid(14).TrimStart();
    }
    else if (CleanDescriptor.StartsWith(TEXT("UIInteraction")))
    {
        CleanDescriptor = CleanDescriptor.Mid(13).TrimStart();
    }

    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CleanDescriptor);

    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        // Not legacy format, not valid JSON вЂ” log but do NOT crash.
        UE_LOG(LogTemp, Warning,
            TEXT("[awsTutorial|PC] OnPixelStreamingInput: unrecognised message format. Descriptor: %s"), *Descriptor);
        return;
    }

    // Unwrap PixelStreaming UIInteraction descriptor field if present
    FString EffectiveJSON = CleanDescriptor;
    if (JsonObject->HasField(TEXT("descriptor")))
    {
        EffectiveJSON = JsonObject->GetStringField(TEXT("descriptor"));
        TSharedPtr<FJsonObject> InnerObject;
        TSharedRef<TJsonReader<>> InnerReader = TJsonReaderFactory<>::Create(EffectiveJSON);
        if (FJsonSerializer::Deserialize(InnerReader, InnerObject) && InnerObject.IsValid())
        {
            JsonObject = InnerObject;
        }
    }
    else if (JsonObject->HasField(TEXT("Descriptor")))
    {
        EffectiveJSON = JsonObject->GetStringField(TEXT("Descriptor"));
        TSharedPtr<FJsonObject> InnerObject;
        TSharedRef<TJsonReader<>> InnerReader = TJsonReaderFactory<>::Create(EffectiveJSON);
        if (FJsonSerializer::Deserialize(InnerReader, InnerObject) && InnerObject.IsValid())
        {
            JsonObject = InnerObject;
        }
    }

    // в”Ђв”Ђ ClipboardPaste command в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
    if (JsonObject->HasField(TEXT("Cmd")) &&
        JsonObject->GetStringField(TEXT("Cmd")) == TEXT("ClipboardPaste"))
    {
        FString PasteText = JsonObject->GetStringField(TEXT("Text"));

        // Strip embedded null terminators (common from some browser clipboard APIs)
        PasteText.ReplaceInline(TEXT("\0"), TEXT(""));

        UE_LOG(LogTemp, Warning,
            TEXT("[awsTutorial|PC] ClipboardPaste received. Character count: %d"), PasteText.Len());

        // 1. Sync to OS-level clipboard as a fallback (e.g. for non-Slate paths)
        FPlatformApplicationMisc::ClipboardCopy(*PasteText);
        LastKnownClipboardContent = PasteText;

        // 2. Direct injection via Slate Character Events.
        //    Bypasses the headless-OS clipboard limitation on Linux EC2 servers.
        if (FSlateApplication::IsInitialized())
        {
            FSlateApplication& SlateApp = FSlateApplication::Get();
            for (int32 i = 0; i < PasteText.Len(); ++i)
            {
                FCharacterEvent CharEvent(PasteText[i], FModifierKeysState(), 0, false);
                SlateApp.ProcessKeyCharEvent(CharEvent);
            }

            UE_LOG(LogTemp, Warning,
                TEXT("[awsTutorial|PC] ClipboardPaste: injected %d chars via Slate CharacterEvent."), PasteText.Len());

            SendDiag_PC(PixelStreamingInput.Get(),
                FString::Printf(
                    TEXT("[PC] ClipboardPaste OK вЂ” injected %d chars via Slate."), PasteText.Len()));
        }
        else
        {
            UE_LOG(LogTemp, Error,
                TEXT("[awsTutorial|PC] ClipboardPaste FAILED: SlateApplication not initialized."));
            SendDiag_PC(PixelStreamingInput.Get(),
                TEXT("[PC] ERROR: ClipboardPaste failed вЂ” Slate not initialized."));
        }
    }
    // в”Ђв”Ђ Constructor commands в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
    else if (JsonObject->HasField(TEXT("cmd")) || JsonObject->HasField(TEXT("Cmd")))
    {
        FString CmdVal = JsonObject->HasField(TEXT("cmd")) ? JsonObject->GetStringField(TEXT("cmd")) : JsonObject->GetStringField(TEXT("Cmd"));
        if (CmdVal.StartsWith(TEXT("add_wall")) || CmdVal.StartsWith(TEXT("add_opening")) || CmdVal.StartsWith(TEXT("clear")) || CmdVal.StartsWith(TEXT("get_state")))
        {
            ARoomPlannerManager* Planner = ARoomPlannerManager::GetOrCreateInstance(GetWorld());
            if (Planner)
            {
                FString Response = Planner->ProcessCommandJSON(EffectiveJSON);
                if (PixelStreamingInput)
                {
                    PixelStreamingInput->SendPixelStreamingResponse(FString::Printf(TEXT("MaxiMallConstructor:%s"), *Response));
                }
            }
        }
    }
}





void AAwsTutorial_PlayerController::SendOpenURLToBrowser(const FString& URL)
{
    // FIX 2: ActivePixelStreamingInput removed вЂ” use single cached PixelStreamingInput.
    UPixelStreamingInput* TargetInput = PixelStreamingInput.Get();
    if (TargetInput)
    {
        FString Message = FString::Printf(TEXT("open_url: %s"), *URL);
        UE_LOG(LogTemp, Warning,
            TEXT("[awsTutorial|PC] Sending open_url to browser data channel: %s"), *Message);
        TargetInput->SendPixelStreamingResponse(Message);

        // Browser diagnostic: confirms URL command was sent.
        SendDiag_PC(TargetInput,
            FString::Printf(TEXT("[PC] SendOpenURLToBrowser fired вЂ” URL: %s"), *URL));
    }
    else
    {
        UE_LOG(LogTemp, Error,
            TEXT("[awsTutorial|PC] SendOpenURLToBrowser FAILED: PixelStreamingInput is null (PS not active)."));
    }
}

void AAwsTutorial_PlayerController::SelectComponent(UPrimitiveComponent* ComponentToSelect)
{
    // Never allow component selection while inside Viewmode
    if (ActivePreviewActor != nullptr && ComponentToSelect != nullptr)
    {
        return;
    }

    UPrimitiveComponent* PrevSelected = SelectedComponent.Get();
    if (PrevSelected && PrevSelected != ComponentToSelect)
    {
        PrevSelected->SetRenderCustomDepth(false);
    }

    SelectedComponent = ComponentToSelect;

    OnComponentSelected(ComponentToSelect);
    OnComponentSelectedDelegate.Broadcast(ComponentToSelect);
}

UPrimitiveComponent* AAwsTutorial_PlayerController::GetSelectedComponent() const
{
    return SelectedComponent.Get();
}

void AAwsTutorial_PlayerController::Server_CommitWall_Implementation(FVector2D StartPos, FVector2D EndPos, float Thickness, float Height)
{
	if (ARoomPlannerManager* Manager = ARoomPlannerManager::GetOrCreateInstance(GetWorld()))
	{
		int32 N1 = Manager->AddNode(StartPos);
		int32 N2 = Manager->AddNode(EndPos);
		Manager->AddWall(N1, N2, Thickness, Height);
		Manager->ReplicatedRoomJSON = Manager->ExportLayoutToJSON();
		Manager->OnRep_ReplicatedRoomJSON();
	}
}

bool AAwsTutorial_PlayerController::Server_CommitWall_Validate(FVector2D StartPos, FVector2D EndPos, float Thickness, float Height)
{
	return true;
}

void AAwsTutorial_PlayerController::Server_ClearLayout_Implementation()
{
	if (ARoomPlannerManager* Manager = ARoomPlannerManager::GetOrCreateInstance(GetWorld()))
	{
		Manager->ClearLayout();
		Manager->ReplicatedRoomJSON = Manager->ExportLayoutToJSON();
		Manager->OnRep_ReplicatedRoomJSON();
	}
}

bool AAwsTutorial_PlayerController::Server_ClearLayout_Validate()
{
	return true;
}

void AAwsTutorial_PlayerController::Server_BuildPreset4x4mRoom_Implementation()
{
	if (ARoomPlannerManager* Manager = ARoomPlannerManager::GetOrCreateInstance(GetWorld()))
	{
		Manager->BuildPreset4x4mRoom();
		Manager->OnRep_ReplicatedRoomJSON();
	}
}

bool AAwsTutorial_PlayerController::Server_BuildPreset4x4mRoom_Validate()
{
	return true;
}

void AAwsTutorial_PlayerController::Server_SetWallLength_Implementation(int32 SegmentID, float NewLengthMeters)
{
	if (ARoomPlannerManager* Manager = ARoomPlannerManager::GetOrCreateInstance(GetWorld()))
	{
		Manager->SetWallLength(SegmentID, NewLengthMeters);
		Manager->OnRep_ReplicatedRoomJSON();
	}
}

bool AAwsTutorial_PlayerController::Server_SetWallLength_Validate(int32 SegmentID, float NewLengthMeters)
{
	return true;
}

void AAwsTutorial_PlayerController::Server_DeleteWall_Implementation(int32 SegmentID)
{
	if (ARoomPlannerManager* Manager = ARoomPlannerManager::GetOrCreateInstance(GetWorld()))
	{
		Manager->RemoveWall(SegmentID);
		Manager->OnRep_ReplicatedRoomJSON();
	}
}

bool AAwsTutorial_PlayerController::Server_DeleteWall_Validate(int32 SegmentID)
{
	return true;
}

void AAwsTutorial_PlayerController::Server_DeleteOpening_Implementation(int32 SegmentID, int32 OpeningIndex)
{
	if (ARoomPlannerManager* Manager = ARoomPlannerManager::GetOrCreateInstance(GetWorld()))
	{
		Manager->DeleteOpening(SegmentID, OpeningIndex);
		Manager->OnRep_ReplicatedRoomJSON();
	}
}

bool AAwsTutorial_PlayerController::Server_DeleteOpening_Validate(int32 SegmentID, int32 OpeningIndex)
{
	return true;
}

void AAwsTutorial_PlayerController::Server_AddDoor_Implementation(int32 SegmentID, float WidthMeters, float HeightMeters, float DistFromStartCm)
{
	if (ARoomPlannerManager* Manager = ARoomPlannerManager::GetOrCreateInstance(GetWorld()))
	{
		Manager->AddDoorToWall(SegmentID, WidthMeters, HeightMeters, DistFromStartCm);
		Manager->OnRep_ReplicatedRoomJSON();
	}
}

bool AAwsTutorial_PlayerController::Server_AddDoor_Validate(int32 SegmentID, float WidthMeters, float HeightMeters, float DistFromStartCm)
{
	return true;
}

void AAwsTutorial_PlayerController::Server_AddWindow_Implementation(int32 SegmentID, float WidthMeters, float HeightMeters, float SillHeightMeters, float DistFromStartCm)
{
	if (ARoomPlannerManager* Manager = ARoomPlannerManager::GetOrCreateInstance(GetWorld()))
	{
		Manager->AddWindowToWall(SegmentID, WidthMeters, HeightMeters, SillHeightMeters, DistFromStartCm);
		Manager->OnRep_ReplicatedRoomJSON();
	}
}

bool AAwsTutorial_PlayerController::Server_AddWindow_Validate(int32 SegmentID, float WidthMeters, float HeightMeters, float SillHeightMeters, float DistFromStartCm)
{
	return true;
}

void AAwsTutorial_PlayerController::Server_UpdateOpeningDimensions_Implementation(int32 SegmentID, int32 OpeningIndex, float WidthMeters, float HeightMeters, float SillHeightMeters)
{
	if (ARoomPlannerManager* Manager = ARoomPlannerManager::GetOrCreateInstance(GetWorld()))
	{
		Manager->UpdateOpeningDimensions(SegmentID, OpeningIndex, WidthMeters, HeightMeters, SillHeightMeters);
		Manager->OnRep_ReplicatedRoomJSON();
	}
}

bool AAwsTutorial_PlayerController::Server_UpdateOpeningDimensions_Validate(int32 SegmentID, int32 OpeningIndex, float WidthMeters, float HeightMeters, float SillHeightMeters)
{
	return true;
}

void AAwsTutorial_PlayerController::Server_UpdateOpeningPosition_Implementation(int32 SegmentID, int32 OpeningIndex, float NewDistFromStartCm)
{
	if (ARoomPlannerManager* Manager = ARoomPlannerManager::GetOrCreateInstance(GetWorld()))
	{
		Manager->UpdateOpeningPosition(SegmentID, OpeningIndex, NewDistFromStartCm);
		Manager->OnRep_ReplicatedRoomJSON();
	}
}

bool AAwsTutorial_PlayerController::Server_UpdateOpeningPosition_Validate(int32 SegmentID, int32 OpeningIndex, float NewDistFromStartCm)
{
	return true;
}

