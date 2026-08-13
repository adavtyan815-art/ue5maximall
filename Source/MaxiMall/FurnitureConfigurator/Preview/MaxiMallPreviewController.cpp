#include "MaxiMallPreviewController.h"
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
// NOTE: #include "ShaderPipelineCache.h" intentionally REMOVED (FIX 1).
// FShaderPipelineCache::SetBatchMode(Fast) was stalling the game thread for
// ~90-150 s on every level load while compiling all Vulkan PSOs synchronously.
// Background PSO compilation is now driven by DefaultEngine.ini:
//   r.ShaderPipelineCache.Enabled=1  |  BackgroundBatchSize=20  |  PrecompileBatchTime=0.01
#include "DatasmithAssetUserData.h"
#include "DatasmithContentBlueprintLibrary.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "RoomPlanner/RoomPlannerManager.h"

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

    PixelStreamingInput = CreateDefaultSubobject<UPixelStreamingInput>(TEXT("PixelStreamingInputComponent"));
}

// в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
// SendDiag_PC вЂ” sends a DIAG diagnostic string to the browser via PS data channel.
// Works in Shipping builds (no UE_LOG dependency for browser visibility).
// в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
static void SendDiag_PC(UPixelStreamingInput* PSInput, const FString& Message)
{
    if (!PSInput)
    {
        return;
    }
    const FString Payload = FString::Printf(TEXT("DIAG: %s"), *Message);
    PSInput->SendPixelStreamingResponse(Payload);
    UE_LOG(LogTemp, Warning, TEXT("[MaxiMall|PC|DIAG] %s"), *Message);
}

void AMaxiMallPreviewController::BeginPlay()
{
    Super::BeginPlay();

    // в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
    // FIX 1 APPLIED: FShaderPipelineCache::SetBatchMode(BatchMode::Fast)
    // has been REMOVED from BeginPlay().
    // Background PSO compilation driven by DefaultEngine.ini settings.
    // в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ

    UE_LOG(LogTemp, Warning, TEXT("[MaxiMall|PC] ===== BeginPlay START =====  IsLocalController: %s"),
        IsLocalController() ? TEXT("YES") : TEXT("NO"));

    if (IsLocalController())
    {
        ARoomPlannerManager::GetOrCreateInstance(GetWorld());
        // в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
        // FIX 2 APPLIED: No CreateDefaultSubobject (removed from ctor).
        //
        // The PS plugin attaches UPixelStreamingInput to this controller ONLY
        // when a browser connects via WebRTC. This happens AFTER BeginPlay on
        // a persistent server вЂ” so GetComponentByClass may return null here.
        //
        // Strategy:
        //   1. Try once in BeginPlay (handles pre-connected / single-session builds).
        //   2. If not found, PlayerTick retries every tick until it is found.
        //      AddUniqueDynamic prevents duplicate bindings (the old per-tick scan
        //      used plain AddDynamic, which stacked duplicates every tick and caused
        //      the PS handshake to process messages twice в†’ kickbacks).
        // в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
        if (!PixelStreamingInput)
        {
            PixelStreamingInput = GetComponentByClass<UPixelStreamingInput>();
        }

        if (PixelStreamingInput)
        {
            PixelStreamingInput->OnInputEvent.AddUniqueDynamic(
                this, &AMaxiMallPreviewController::OnPixelStreamingInput);

            UE_LOG(LogTemp, Warning,
                TEXT("[MaxiMall|PC] PS Input component bound in BeginPlay."));

            SendDiag_PC(PixelStreamingInput,
                TEXT("[PC] BeginPlay OK"
                     " | FIX1: shader-pipeline Fast-batch removed вЂ” no game-thread stall"
                     " | FIX2: PS component bound in BeginPlay via GetComponentByClass"
                     " | Level transition should be < 5 s now."));
        }
        else
        {
            // Component not yet attached вЂ” PlayerTick will retry once per tick.
            UE_LOG(LogTemp, Warning,
                TEXT("[MaxiMall|PC] PS Input NOT found in BeginPlay вЂ” will retry in PlayerTick."));
        }

        // Force Epic quality scalability settings to prevent virtual server fallbacks
        if (UGameUserSettings* GameUserSettings = UGameUserSettings::GetGameUserSettings())
        {
            if (GameUserSettings->GetOverallScalabilityLevel() != 3)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[MaxiMall|PC] Overriding scalability level to Epic (3)."));
                GameUserSettings->SetOverallScalabilityLevel(3);
                GameUserSettings->ApplySettings(false);

                SendDiag_PC(PixelStreamingInput,
                    TEXT("[PC] Scalability set to Epic (3) вЂ” quality override applied."));
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[MaxiMall|PC] GameUserSettings is NULL!"));
        }

        FInputModeGameAndUI InputMode;
        InputMode.SetHideCursorDuringCapture(true);
        InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        SetInputMode(InputMode);
        bShowMouseCursor = true;
        bEnableClickEvents = true;
        bEnableMouseOverEvents = true;

        if (RoomPlannerClass && !RoomPlannerInstance)
        {
            RoomPlannerInstance = CreateWidget<UUserWidget>(this, RoomPlannerClass);
            if (RoomPlannerInstance)
            {
                RoomPlannerInstance->AddToViewport();
            }
        }

        UE_LOG(LogTemp, Warning,
            TEXT("[MaxiMall|PC] ===== BeginPlay END вЂ” input mode set, cursor enabled. ====="));
    }
}

static bool IsWidgetHoveredGeometrically(UUserWidget* Widget)
{
    if (!Widget || !Widget->IsInViewport()) return false;

    if (UBIMInspectorWidget* BIMWidget = Cast<UBIMInspectorWidget>(Widget))
    {
        return BIMWidget->IsMouseOverMainPanel();
    }

    if (Widget->IsHovered()) return true;

    if (FSlateApplication::IsInitialized())
    {
        FGeometry WidgetGeo = Widget->GetCachedGeometry();
        FVector2D CursorPos = FSlateApplication::Get().GetCursorPos();
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

void AMaxiMallPreviewController::PlayerTick(float DeltaTime)
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
                    this, &AMaxiMallPreviewController::OnPixelStreamingInput);
                UE_LOG(LogTemp, Warning,
                    TEXT("[MaxiMall|PC] PS Input component found via TObjectIterator and bound!"));
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

                        if (!HasAuthority())
                        {
                            if (FVector2D::Distance(P1, P2) >= 10.f)
                            {
                                Server_CommitWall(P1, P2);
                            }
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
                }
                else if (PlannerManager->ActiveToolMode == EPlannerToolMode::Erase)
                {
                    if (WasInputKeyJustPressed(EKeys::LeftMouseButton))
                    {
                        PlannerManager->DeleteWallAtWorldPos(GroundPos);
                    }
                }

                if (WasInputKeyJustPressed(EKeys::Delete) || WasInputKeyJustPressed(EKeys::BackSpace))
                {
                    if (PlannerManager->SelectedSegmentID != -1)
                    {
                        PlannerManager->DeleteSelectedWall();
                    }
                }
            }
        }
        return;
    }

    UPrimitiveComponent* NewHoveredComp = nullptr;
    AShowroomBooth* HitBooth = nullptr;
    bool bHoveringShowroom = false;
    
    bool bIsMouseOverUI = IsWidgetHoveredGeometrically(BIMInspectorInstance) || IsWidgetHoveredGeometrically(MainWidgetInstance);

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
            // If another player shared this component, restore stencil 3 instead
            // of turning off custom depth entirely.
            if (SharedHighlights.Contains(CurrentHovered))
            {
                CurrentHovered->SetRenderCustomDepth(true);
                CurrentHovered->SetCustomDepthStencilValue(3);
            }
            else
            {
                CurrentHovered->SetRenderCustomDepth(false);
            }
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

    // в”Ђв”Ђ Pixel Streaming cursor data-channel broadcast в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
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
        InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AMaxiMallPreviewController::OnLeftMouseButtonDown);
        InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AMaxiMallPreviewController::OnLeftMouseButtonReleased);
    }
}

void AMaxiMallPreviewController::OnLeftMouseButtonDown()
{
    LMBPressTime = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.f;
    if (FSlateApplication::IsInitialized())
    {
        LMBPressMousePos = FSlateApplication::Get().GetCursorPos();
    }
}

void AMaxiMallPreviewController::OnLeftMouseButtonReleased()
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

void AMaxiMallPreviewController::OnLeftMouseButtonClicked()
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

    bool bIsMouseOverUI = IsWidgetHoveredGeometrically(BIMInspectorInstance) || IsWidgetHoveredGeometrically(MainWidgetInstance);

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

// РІвЂќР‚РІвЂќР‚ CONFIGURATOR PREVIEW MANAGEMENT РІвЂќР‚РІвЂќР‚

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

void AMaxiMallPreviewController::RequestBoothCustomColorChange(AShowroomBooth* TargetBooth, EFurnitureComponentType ComponentType, FLinearColor Color, UMaterialInterface* OverrideMaterial)
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

void AMaxiMallPreviewController::Server_RequestBoothCustomColorChange_Implementation(AShowroomBooth* TargetBooth, EFurnitureComponentType ComponentType, FLinearColor Color, UMaterialInterface* OverrideMaterial)
{
    if (TargetBooth)
    {
        TargetBooth->RequestCustomColorChange(ComponentType, Color, OverrideMaterial);
    }
}

bool AMaxiMallPreviewController::Server_RequestBoothCustomColorChange_Validate(AShowroomBooth* TargetBooth, EFurnitureComponentType ComponentType, FLinearColor Color, UMaterialInterface* OverrideMaterial)
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
    UE_LOG(LogTemp, Warning,
        TEXT("[MaxiMall|PC] OnPixelStreamingInput fired. Descriptor length: %d"), Descriptor.Len());

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
            TEXT("[MaxiMall|PC] [LEGACY] ClipboardPaste (plain-text). Character count: %d"), PasteText.Len());

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
                TEXT("[MaxiMall|PC] [LEGACY] Injected %d chars via Slate."), PasteText.Len());
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
            TEXT("[MaxiMall|PC] OnPixelStreamingInput: unrecognised message format. Descriptor: %s"), *Descriptor);
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
            TEXT("[MaxiMall|PC] ClipboardPaste received. Character count: %d"), PasteText.Len());

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
                TEXT("[MaxiMall|PC] ClipboardPaste: injected %d chars via Slate CharacterEvent."), PasteText.Len());

            SendDiag_PC(PixelStreamingInput.Get(),
                FString::Printf(
                    TEXT("[PC] ClipboardPaste OK вЂ” injected %d chars via Slate."), PasteText.Len()));
        }
        else
        {
            UE_LOG(LogTemp, Error,
                TEXT("[MaxiMall|PC] ClipboardPaste FAILED: SlateApplication not initialized."));
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

// в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
// ToggleSharedSelection вЂ” called from WBP_BIMInspector Share button.
// в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
bool AMaxiMallPreviewController::ToggleSharedSelection()
{
    if (bSharedSelectionActive)
    {
        // в”Ђв”Ђ DEACTIVATE: stop sharing the currently broadcast component в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
        if (SharedSelectionComponent.IsValid())
        {
            UPrimitiveComponent* Comp = SharedSelectionComponent.Get();
            Server_SetSharedSelection(Comp->GetOwner(), Comp->GetName(), false);
            UE_LOG(LogTemp, Warning,
                TEXT("[MaxiMall|PC] ToggleSharedSelection: deactivating broadcast for '%s'."),
                *Comp->GetName());
        }
        bSharedSelectionActive = false;
        SharedSelectionComponent.Reset();
    }
    else
    {
        // в”Ђв”Ђ ACTIVATE: broadcast the currently selected component to all players в”Ђв”Ђ
        UPrimitiveComponent* Comp = SelectedComponent.Get();
        if (!Comp || !Comp->GetOwner() || !HasBIMMetadata(Comp))
        {
            // Nothing valid to share.
            UE_LOG(LogTemp, Warning,
                TEXT("[MaxiMall|PC] ToggleSharedSelection: no valid BIM component selected вЂ” nothing to share."));
            return false;
        }

        Server_SetSharedSelection(Comp->GetOwner(), Comp->GetName(), true);
        bSharedSelectionActive = true;
        SharedSelectionComponent = Comp;
        UE_LOG(LogTemp, Warning,
            TEXT("[MaxiMall|PC] ToggleSharedSelection: broadcasting '%s' owned by '%s'."),
            *Comp->GetName(), *Comp->GetOwner()->GetName());
    }

    // Automatically update BIM Inspector UI text (e.g. TextBlock_93)
    if (BIMInspectorInstance)
    {
        if (UBIMInspectorWidget* BIMWidget = Cast<UBIMInspectorWidget>(BIMInspectorInstance))
        {
            BIMWidget->UpdateShareButtonText(bSharedSelectionActive);
        }
    }

    // Notify browser so it can update the button label/color.
    SendDiag_PC(PixelStreamingInput.Get(),
        FString::Printf(TEXT("[PC] SharedSelection %s."),
            bSharedSelectionActive ? TEXT("ACTIVATED") : TEXT("DEACTIVATED")));

    return bSharedSelectionActive;
}

// в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
// Server_SetSharedSelection вЂ” validates and calls the NetMulticast.
// в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
bool AMaxiMallPreviewController::Server_SetSharedSelection_Validate(
    AActor* ComponentOwner, const FString& ComponentName, bool bActivate)
{
    return IsValid(ComponentOwner);
}

void AMaxiMallPreviewController::Server_SetSharedSelection_Implementation(
    AActor* ComponentOwner, const FString& ComponentName, bool bActivate)
{
    UE_LOG(LogTemp, Warning,
        TEXT("[MaxiMall|Server] Server_SetSharedSelection: owner='%s' comp='%s' activate=%s"),
        *ComponentOwner->GetName(), *ComponentName,
        bActivate ? TEXT("true") : TEXT("false"));

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    // Broadcast Client_ApplySharedSelection to EVERY connected PlayerController on the server!
    // This guarantees all connected clients (Client 1, Client 2, etc.) receive the stencil change.
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        if (AMaxiMallPreviewController* PC = Cast<AMaxiMallPreviewController>(It->Get()))
        {
            PC->Client_ApplySharedSelection(ComponentOwner, ComponentName, bActivate);
        }
    }
}

// в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
// Client_ApplySharedSelection вЂ” executed on every connected client.
// Finds the named component on ComponentOwner and applies / removes stencil 3.
// Local hover (1) and local selection (2) take priority: this function restores
// them if they were previously set, avoiding visual conflict.
// в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
void AMaxiMallPreviewController::Client_ApplySharedSelection_Implementation(
    AActor* ComponentOwner, const FString& ComponentName, bool bActivate)
{
    if (!IsValid(ComponentOwner))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[MaxiMall|Client] Client_ApplySharedSelection: ComponentOwner is null, skipping."));
        return;
    }

    // Find the named PrimitiveComponent on the actor.
    // Component names are deterministic and identical across all clients.
    UPrimitiveComponent* TargetComp = nullptr;
    TArray<UPrimitiveComponent*> PrimComps;
    ComponentOwner->GetComponents<UPrimitiveComponent>(PrimComps);
    for (UPrimitiveComponent* Comp : PrimComps)
    {
        if (Comp && Comp->GetName() == ComponentName)
        {
            TargetComp = Comp;
            break;
        }
    }

    if (!TargetComp)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[MaxiMall|Client] Multicast_ApplySharedSelection: component '%s' not found on '%s'."),
            *ComponentName, *ComponentOwner->GetName());
        return;
    }

    if (bActivate)
    {
        // в”Ђв”Ђ Add to our local SharedHighlights tracking set в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
        SharedHighlights.Add(TargetComp);

        // Apply stencil 3 ONLY if local player is not hovering (1) or selecting (2)
        // this component right now. If they are, their local stencil takes priority,
        // and PlayerTick will restore stencil 3 when hover/select leaves.
        const bool bIsLocallyHovered  = (HoveredComponent.Get()  == TargetComp);
        const bool bIsLocallySelected = (SelectedComponent.Get() == TargetComp);

        if (!bIsLocallyHovered && !bIsLocallySelected)
        {
            TargetComp->SetRenderCustomDepth(true);
            TargetComp->SetCustomDepthStencilValue(3);
        }
        // If hovering/selecting: we leave the current stencil (1 or 2) intact.
        // When hover/select ends, PlayerTick will restore stencil 3 via SharedHighlights.

        UE_LOG(LogTemp, Warning,
            TEXT("[MaxiMall|Client] SharedSelection ACTIVATED on '%s' (localHovered=%s, localSelected=%s)."),
            *ComponentName,
            bIsLocallyHovered  ? TEXT("yes") : TEXT("no"),
            bIsLocallySelected ? TEXT("yes") : TEXT("no"));
    }
    else
    {
        // в”Ђв”Ђ Remove from tracking set в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
        SharedHighlights.Remove(TargetComp);

        // Restore the correct local stencil for this client.
        const bool bIsLocallySelected = (SelectedComponent.Get() == TargetComp);
        const bool bIsLocallyHovered  = (HoveredComponent.Get()  == TargetComp);

        if (bIsLocallySelected)
        {
            // Keep stencil 2 (locally selected вЂ” still clicked on by this player)
            TargetComp->SetRenderCustomDepth(true);
            TargetComp->SetCustomDepthStencilValue(2);
        }
        else if (bIsLocallyHovered)
        {
            // Keep stencil 1 (currently hovered by this player)
            TargetComp->SetRenderCustomDepth(true);
            TargetComp->SetCustomDepthStencilValue(1);
        }
        else
        {
            // Not hovered or selected locally вЂ” clear completely
            TargetComp->SetRenderCustomDepth(false);
        }

        UE_LOG(LogTemp, Warning,
            TEXT("[MaxiMall|Client] SharedSelection DEACTIVATED on '%s' вЂ” restored to %s."),
            *ComponentName,
            bIsLocallySelected ? TEXT("stencil 2") :
            bIsLocallyHovered  ? TEXT("stencil 1") : TEXT("off"));
    }
}

void AMaxiMallPreviewController::SendOpenURLToBrowser(const FString& URL)
{
    // FIX 2: ActivePixelStreamingInput removed вЂ” use single cached PixelStreamingInput.
    UPixelStreamingInput* TargetInput = PixelStreamingInput.Get();
    if (TargetInput)
    {
        FString Message = FString::Printf(TEXT("open_url: %s"), *URL);
        UE_LOG(LogTemp, Warning,
            TEXT("[MaxiMall|PC] Sending open_url to browser data channel: %s"), *Message);
        TargetInput->SendPixelStreamingResponse(Message);

        // Browser diagnostic: confirms URL command was sent.
        SendDiag_PC(TargetInput,
            FString::Printf(TEXT("[PC] SendOpenURLToBrowser fired вЂ” URL: %s"), *URL));
    }
    else
    {
        UE_LOG(LogTemp, Error,
            TEXT("[MaxiMall|PC] SendOpenURLToBrowser FAILED: PixelStreamingInput is null (PS not active)."));
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

    // в”Ђв”Ђ Auto-deactivate shared selection on any selection change в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
    // Covers both: closing BIM Inspector (ComponentToSelect == nullptr) and
    // clicking a different mesh while sharing is active.
    if (bSharedSelectionActive && SharedSelectionComponent.IsValid() &&
        SharedSelectionComponent.Get() != ComponentToSelect)
    {
        UPrimitiveComponent* Shared = SharedSelectionComponent.Get();
        Server_SetSharedSelection(Shared->GetOwner(), Shared->GetName(), false);
        bSharedSelectionActive = false;
        SharedSelectionComponent.Reset();
        UE_LOG(LogTemp, Warning, TEXT("[MaxiMall|PC] SharedSelection auto-deactivated (selection changed)."));
    }

    UPrimitiveComponent* PrevSelected = SelectedComponent.Get();

    // Clear custom depth from previously selected component if it's no longer hovered
    if (PrevSelected && PrevSelected != ComponentToSelect && PrevSelected != HoveredComponent.Get())
    {
        // Restore shared stencil if another player still has it shared
        if (SharedHighlights.Contains(PrevSelected))
        {
            PrevSelected->SetRenderCustomDepth(true);
            PrevSelected->SetCustomDepthStencilValue(3);
        }
        else
        {
            PrevSelected->SetRenderCustomDepth(false);
        }
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
    TMap<FString, TArray<FBIMMetadataPair>> GroupMap;

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
        if (CleanKey.StartsWith(TEXT("Element*")) || CleanKey.StartsWith(TEXT("Element=")) || CleanKey.StartsWith(TEXT("Element.")) || CleanKey.StartsWith(TEXT("Element_")) || CleanKey.StartsWith(TEXT("Element:")) || CleanKey.StartsWith(TEXT("Element-")))
        {
            CleanKey.RightChopInline(8);
        }
        else if (CleanKey.StartsWith(TEXT("Type*")) || CleanKey.StartsWith(TEXT("Type=")) || CleanKey.StartsWith(TEXT("Type.")) || CleanKey.StartsWith(TEXT("Type_")) || CleanKey.StartsWith(TEXT("Type:")) || CleanKey.StartsWith(TEXT("Type-")))
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

        // Categorize into standard BIM property groups
        FString GroupName = TEXT("Identity Data");
        FString PropName = CleanKey;

        int32 DotIdx = INDEX_NONE;
        if (CleanKey.FindChar(TEXT('.'), DotIdx))
        {
            GroupName = CleanKey.Left(DotIdx);
            PropName = CleanKey.Mid(DotIdx + 1);
        }
        else
        {
            if (CleanKey.Contains(TEXT("Constraint")) || CleanKey.Contains(TEXT("Offset")) || CleanKey.Contains(TEXT("Location Line")) || CleanKey.Contains(TEXT("Unconnected Height")) || CleanKey.Contains(TEXT("Attached")))
            {
                GroupName = TEXT("Constraints");
            }
            else if (CleanKey.Contains(TEXT("Area")) || CleanKey.Contains(TEXT("Length")) || CleanKey.Contains(TEXT("Volume")) || CleanKey.Contains(TEXT("Thickness")) || CleanKey.Contains(TEXT("Width")) || CleanKey.Contains(TEXT("Height")))
            {
                GroupName = TEXT("Dimensions");
            }
            else if (CleanKey.Contains(TEXT("Phase")))
            {
                GroupName = TEXT("Phasing");
            }
            else if (CleanKey.Contains(TEXT("Structural")) || CleanKey.Contains(TEXT("Analytical")))
            {
                GroupName = TEXT("Structural");
            }
            else if (CleanKey.Contains(TEXT("Cross-Section")) || CleanKey.Contains(TEXT("Export to IFC")) || CleanKey.Contains(TEXT("Function")))
            {
                GroupName = TEXT("Construction");
            }
            else if (CleanKey.Contains(TEXT("Material")) || CleanKey.Contains(TEXT("Absorptance")))
            {
                GroupName = TEXT("Materials and Finishes");
            }
            else if (CleanKey.Contains(TEXT("Room Bounding")) || CleanKey.Contains(TEXT("Mass")))
            {
                GroupName = TEXT("Model Properties");
            }
        }

        if (PropName.StartsWith(TEXT("Element*")) || PropName.StartsWith(TEXT("Element=")) || PropName.StartsWith(TEXT("Element.")) || PropName.StartsWith(TEXT("Element_")))
        {
            PropName.RightChopInline(8);
        }
        else if (PropName.StartsWith(TEXT("Type*")) || PropName.StartsWith(TEXT("Type=")) || PropName.StartsWith(TEXT("Type.")) || PropName.StartsWith(TEXT("Type_")))
        {
            PropName.RightChopInline(5);
        }

        FBIMMetadataPair GroupPair;
        GroupPair.Key = PropName;
        GroupPair.Value = ValStr;
        GroupMap.FindOrAdd(GroupName).Add(GroupPair);
    }

    // Convert GroupMap to CategorizedMetadata array
    for (const TPair<FString, TArray<FBIMMetadataPair>>& GroupPair : GroupMap)
    {
        FBIMCategoryGroup CatGroup;
        CatGroup.CategoryName = GroupPair.Key;
        CatGroup.Pairs = GroupPair.Value;
        OutData.CategorizedMetadata.Add(CatGroup);
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
                    RawVal.ReplaceInline(TEXT("mВІ"), TEXT(""));
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

void AMaxiMallPreviewController::Server_CommitWall_Implementation(FVector2D StartPos, FVector2D EndPos, float Thickness, float Height)
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

bool AMaxiMallPreviewController::Server_CommitWall_Validate(FVector2D StartPos, FVector2D EndPos, float Thickness, float Height)
{
	return true;
}

void AMaxiMallPreviewController::Server_ClearLayout_Implementation()
{
	if (ARoomPlannerManager* Manager = ARoomPlannerManager::GetOrCreateInstance(GetWorld()))
	{
		Manager->ClearLayout();
		Manager->ReplicatedRoomJSON = Manager->ExportLayoutToJSON();
		Manager->OnRep_ReplicatedRoomJSON();
	}
}

bool AMaxiMallPreviewController::Server_ClearLayout_Validate()
{
	return true;
}

void AMaxiMallPreviewController::Server_BuildPreset4x4mRoom_Implementation()
{
	if (ARoomPlannerManager* Manager = ARoomPlannerManager::GetOrCreateInstance(GetWorld()))
	{
		Manager->ClearLayout();
		int32 N1 = Manager->AddNode(FVector2D(-200.f, -200.f));
		int32 N2 = Manager->AddNode(FVector2D(200.f, -200.f));
		int32 N3 = Manager->AddNode(FVector2D(200.f, 200.f));
		int32 N4 = Manager->AddNode(FVector2D(-200.f, 200.f));
		Manager->AddWall(N1, N2);
		Manager->AddWall(N2, N3);
		Manager->AddWall(N3, N4);
		Manager->AddWall(N4, N1);
		Manager->RebuildRooms();
		Manager->ReplicatedRoomJSON = Manager->ExportLayoutToJSON();
		Manager->OnRep_ReplicatedRoomJSON();
	}
}

bool AMaxiMallPreviewController::Server_BuildPreset4x4mRoom_Validate()
{
	return true;
}

