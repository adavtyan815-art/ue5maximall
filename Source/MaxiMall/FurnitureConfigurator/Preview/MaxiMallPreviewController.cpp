// Copyright MaxiMall Project. All Rights Reserved.
// MaxiMallPreviewController.cpp

#include "FurnitureConfigurator/Preview/MaxiMallPreviewController.h"

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
#include "Blueprint/UserWidget.h"
#include "FurnitureConfigurator/UI/ConfiguratorMainWidget.h"
#include "FurnitureConfigurator/UI/ViewmodeOverlayWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

AMaxiMallPreviewController::AMaxiMallPreviewController()
{
    // Player controllers replicate by default — that's fine, only the
    // ActivePreviewActor property is explicitly excluded from replication.
    ActivePreviewActor = nullptr;
    CurrentTargetBooth = nullptr;
    CurrentTargetComponent = EFurnitureComponentType::None;
    HoveredComponent = nullptr;
    bIsClosingUI = false;
    LastClickTime = 0.f;
    
    // Default to the C++ base class so it is not empty in the Editor by default
    PreviewActorClass = AFurniturePreviewActor::StaticClass();

    MainWidgetClass = nullptr;
    ViewmodeOverlayClass = nullptr;
    MainWidgetInstance = nullptr;
    ViewmodeOverlayInstance = nullptr;

    // Set default fallback asset paths (using the engine's built-in shape sphere)
    BackdropMeshAsset = FSoftObjectPath(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    // Default material fallback path (engine default material)
    BackdropMaterialAsset = FSoftObjectPath(TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
}

void AMaxiMallPreviewController::BeginPlay()
{
    Super::BeginPlay();

    // Ensure we start with the consistent mouse cursor behavior (visible by default, hides when clicking/capturing)
    if (IsLocalController())
    {
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

    // Reset drag flag as soon as RMB is released.
    // The flag itself is set in AddYawInput/AddPitchInput whenever the camera
    // actually rotates while RMB is held — that is the only reliable signal.
    if (!IsInputKeyDown(EKeys::RightMouseButton))
    {
        bRightMouseIsDragging = false;
    }

    UPrimitiveComponent* NewHoveredComp = nullptr;
    AShowroomBooth* HitBooth = nullptr;
    
    // We only highlight if we are NOT in viewmode (viewmode staging is isolated)
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
            // Trace from camera look direction
            FVector CameraLoc;
            FRotator CameraRot;
            GetPlayerViewPoint(CameraLoc, CameraRot);
            
            FVector Start = CameraLoc;
            FVector End = Start + (CameraRot.Vector() * 1000.f); // 10 meters range
            
            FCollisionQueryParams Params;
            Params.AddIgnoredActor(GetPawn());
            
            bHit = GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, ECC_Visibility, Params);
        }
        
        if (bHit && HitResult.GetActor())
        {
            HitBooth = Cast<AShowroomBooth>(HitResult.GetActor());
            if (HitBooth)
            {
                UPrimitiveComponent* HitComp = HitResult.GetComponent();
                if (HitComp && (
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
                ))
                {
                    NewHoveredComp = HitComp;
                }
            }
        }
    }

    // Update highlights and cursor
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
            NewHoveredComp->SetCustomDepthStencilValue(1); // Default highlight index
            
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
        
        HoveredComponent = NewHoveredComp;
    }
}

void AMaxiMallPreviewController::SetupInputComponent()
{
    Super::SetupInputComponent();

    if (InputComponent)
    {
        InputComponent->BindKey(EKeys::LeftMouseButton,  IE_Pressed, this, &AMaxiMallPreviewController::OnLeftMouseButtonPressed);
        InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AMaxiMallPreviewController::OnRightMouseButtonPressed);
    }
}

void AMaxiMallPreviewController::OnLeftMouseButtonPressed()
{
    float CurrentTime = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.f;

    if (CurrentTime - LastClickTime < 0.25f)
    {
        HandleDoubleClickInteraction();
    }
    
    LastClickTime = CurrentTime;
}

void AMaxiMallPreviewController::OnRightMouseButtonPressed()
{
    // Reset drag flag on every new press so a clean click starts fresh.
    bRightMouseIsDragging = false;
}

void AMaxiMallPreviewController::AddYawInput(float Val)
{
    Super::AddYawInput(Val);
    // UE calls this whenever mouse X movement rotates the camera.
    // If RMB is held at the same time, the user is doing a camera drag, not a click.
    if (Val != 0.f && IsInputKeyDown(EKeys::RightMouseButton))
    {
        bRightMouseIsDragging = true;
    }
}

void AMaxiMallPreviewController::AddPitchInput(float Val)
{
    Super::AddPitchInput(Val);
    // Same as above for vertical (pitch) rotation.
    if (Val != 0.f && IsInputKeyDown(EKeys::RightMouseButton))
    {
        bRightMouseIsDragging = true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Preview Management
// ─────────────────────────────────────────────────────────────────────────────

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

    if (FocusComponent == EFurnitureComponentType::Doors)
    {
        FocusComponent = EFurnitureComponentType::Cabinet;
    }

    CurrentTargetComponent = FocusComponent;

    // Safety check: Ensure we have a possessed pawn before allowing preview mode
    if (!GetPawn())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PreviewController] OpenFurniturePreview called before possessing a pawn. Ignoring."));
        return;
    }

    // Disable character look/move inputs to prevent character movement/rotation during preview
    ResetIgnoreInputFlags();
    SetIgnoreLookInput(true);
    SetIgnoreMoveInput(true);

    // Hide the main configurator UI widget when entering viewmode
    if (MainWidgetInstance)
    {
        MainWidgetInstance->RemoveFromParent();
    }

    // Capture the current control rotation before entering preview mode (if not already in preview)
    if (!ActivePreviewActor)
    {
        SavedControlRotation = GetControlRotation();
    }

    // ── 1. Close any existing preview first ───────────────────────────────
    CloseFurniturePreview();

    // ── 2. Read the active product data from the booth ────────────────────
    FFurnitureProductRow ProductSnapshot;
    if (!TargetBooth->GetActiveProductData(ProductSnapshot))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[PreviewController] Booth '%s' has no valid active product. Cannot open preview."),
            *TargetBooth->GetName());
        return;
    }

    // ── 3. Spawn the preview actor (client-local, not replicated) ─────────
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner              = this;
    SpawnParams.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    // SpawnActor will call the constructor which sets bReplicates = false.
    UClass* SpawnClass = PreviewActorClass;
    if (!SpawnClass)
    {
        SpawnClass = AFurniturePreviewActor::StaticClass();
    }

    // Print runtime spawn class details for debugging/exposure validation
    UE_LOG(LogTemp, Log, TEXT("[PreviewController] OpenFurniturePreview spawning class: %s"), *SpawnClass->GetName());

    FRotator SpawnRotation = FRotator::ZeroRotator;
    if (TargetBooth)
    {
        SpawnRotation.Yaw = TargetBooth->GetActorRotation().Yaw;
    }

    ActivePreviewActor = World->SpawnActor<AFurniturePreviewActor>(
        SpawnClass,
        PreviewStagingLocation,
        SpawnRotation,
        SpawnParams);

    if (!ActivePreviewActor)
    {
        UE_LOG(LogTemp, Error, TEXT("[PreviewController] Failed to spawn AFurniturePreviewActor."));
        return;
    }

    // Isolate view by hiding ALL other actors in the level locally for this player only
    HiddenActors.Empty();
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (Actor && Actor != this && Actor != GetPawn() && Actor != ActivePreviewActor)
        {
            HiddenActors.Add(Actor);
        }
    }

    // ── 4. Load the product snapshot into the preview actor ───────────────
    ActivePreviewActor->LoadProductPreview(ProductSnapshot, TargetBooth->ActiveState, TargetBooth);

    // Isolate target component in the preview actor by hiding others
    if (CurrentTargetComponent != EFurnitureComponentType::None)
    {
        if (ActivePreviewActor->CabinetMesh) ActivePreviewActor->CabinetMesh->SetVisibility(false);
        if (ActivePreviewActor->DoorMeshSlot0) ActivePreviewActor->DoorMeshSlot0->SetVisibility(false);
        if (ActivePreviewActor->DoorMeshSlot1) ActivePreviewActor->DoorMeshSlot1->SetVisibility(false);
        if (ActivePreviewActor->CountertopMesh) ActivePreviewActor->CountertopMesh->SetVisibility(false);
        if (ActivePreviewActor->SinkMesh) ActivePreviewActor->SinkMesh->SetVisibility(false);
        if (ActivePreviewActor->FaucetMesh) ActivePreviewActor->FaucetMesh->SetVisibility(false);
        if (ActivePreviewActor->MirrorMesh) ActivePreviewActor->MirrorMesh->SetVisibility(false);
        if (ActivePreviewActor->ClosetMesh) ActivePreviewActor->ClosetMesh->SetVisibility(false);
        if (ActivePreviewActor->ClosetDoorMeshSlot0) ActivePreviewActor->ClosetDoorMeshSlot0->SetVisibility(false);
        if (ActivePreviewActor->ClosetDoorMeshSlot1) ActivePreviewActor->ClosetDoorMeshSlot1->SetVisibility(false);

        // Show only the selected component mesh
        switch (CurrentTargetComponent)
        {
        case EFurnitureComponentType::Cabinet:
            if (ActivePreviewActor->CabinetMesh) ActivePreviewActor->CabinetMesh->SetVisibility(true);
            if (ProductSnapshot.DoorsConfig.CabinetDoors.DoorCount == EDoorCount::OneDoor)
            {
                if (ActivePreviewActor->DoorMeshSlot0) ActivePreviewActor->DoorMeshSlot0->SetVisibility(true);
            }
            else if (ProductSnapshot.DoorsConfig.CabinetDoors.DoorCount == EDoorCount::TwoDoors)
            {
                if (ActivePreviewActor->DoorMeshSlot0) ActivePreviewActor->DoorMeshSlot0->SetVisibility(true);
                if (ActivePreviewActor->DoorMeshSlot1) ActivePreviewActor->DoorMeshSlot1->SetVisibility(true);
            }
            break;
        case EFurnitureComponentType::Closet:
            if (ActivePreviewActor->ClosetMesh) ActivePreviewActor->ClosetMesh->SetVisibility(true);
            if (ProductSnapshot.DoorsConfig.ClosetDoors.DoorCount == EDoorCount::OneDoor)
            {
                if (ActivePreviewActor->ClosetDoorMeshSlot0) ActivePreviewActor->ClosetDoorMeshSlot0->SetVisibility(true);
            }
            else if (ProductSnapshot.DoorsConfig.ClosetDoors.DoorCount == EDoorCount::TwoDoors)
            {
                if (ActivePreviewActor->ClosetDoorMeshSlot0) ActivePreviewActor->ClosetDoorMeshSlot0->SetVisibility(true);
                if (ActivePreviewActor->ClosetDoorMeshSlot1) ActivePreviewActor->ClosetDoorMeshSlot1->SetVisibility(true);
            }
            break;
        case EFurnitureComponentType::Doors:
            if (ProductSnapshot.DoorsConfig.CabinetDoors.DoorCount == EDoorCount::OneDoor)
            {
                if (ActivePreviewActor->DoorMeshSlot0) ActivePreviewActor->DoorMeshSlot0->SetVisibility(true);
            }
            else if (ProductSnapshot.DoorsConfig.CabinetDoors.DoorCount == EDoorCount::TwoDoors)
            {
                if (ActivePreviewActor->DoorMeshSlot0) ActivePreviewActor->DoorMeshSlot0->SetVisibility(true);
                if (ActivePreviewActor->DoorMeshSlot1) ActivePreviewActor->DoorMeshSlot1->SetVisibility(true);
            }
            if (ProductSnapshot.DoorsConfig.ClosetDoors.DoorCount == EDoorCount::OneDoor)
            {
                if (ActivePreviewActor->ClosetDoorMeshSlot0) ActivePreviewActor->ClosetDoorMeshSlot0->SetVisibility(true);
            }
            else if (ProductSnapshot.DoorsConfig.ClosetDoors.DoorCount == EDoorCount::TwoDoors)
            {
                if (ActivePreviewActor->ClosetDoorMeshSlot0) ActivePreviewActor->ClosetDoorMeshSlot0->SetVisibility(true);
                if (ActivePreviewActor->ClosetDoorMeshSlot1) ActivePreviewActor->ClosetDoorMeshSlot1->SetVisibility(true);
            }
            break;
        case EFurnitureComponentType::Countertop:
            if (ActivePreviewActor->CountertopMesh) ActivePreviewActor->CountertopMesh->SetVisibility(true);
            break;
        case EFurnitureComponentType::Sink:
            if (CurrentTargetBooth && CurrentTargetBooth->GetActiveCountertopType() == ECountertopType::SurfaceMounted)
            {
                if (ActivePreviewActor->SinkMesh) ActivePreviewActor->SinkMesh->SetVisibility(true);
            }
            break;
        case EFurnitureComponentType::Faucet:
            if (ActivePreviewActor->FaucetMesh) ActivePreviewActor->FaucetMesh->SetVisibility(true);
            break;
        case EFurnitureComponentType::Mirror:
            if (ActivePreviewActor->MirrorMesh) ActivePreviewActor->MirrorMesh->SetVisibility(true);
            break;
        default:
            break;
        }
    }

    // Focus camera orbit on the isolated component
    if (CurrentTargetComponent != EFurnitureComponentType::None)
    {
        ActivePreviewActor->SetFocusComponent(CurrentTargetComponent);
    }

    // Calculate and apply starting preview camera angles to match player's view relative to the booth
    float InitialYaw = FRotator::NormalizeAxis(SavedControlRotation.Yaw - SpawnRotation.Yaw);
    float InitialPitch = FRotator::NormalizeAxis(SavedControlRotation.Pitch);
    ActivePreviewActor->SetInitialRotation(InitialYaw, InitialPitch);

    // Bind to the booth's product change delegate to keep the preview actor in sync dynamically
    CurrentTargetBooth = TargetBooth;
    CurrentTargetBooth->OnProductChanged.AddDynamic(this, &AMaxiMallPreviewController::OnTargetBoothProductChanged);

    // Load and assign the backdrop static mesh and material dynamically
    UStaticMesh* LoadedBackdropMesh = BackdropMeshAsset.LoadSynchronous();
    UMaterialInterface* LoadedBackdropMat = BackdropMaterialAsset.LoadSynchronous();
    ActivePreviewActor->SetupBackdrop(LoadedBackdropMesh, LoadedBackdropMat);

    // ── 5. Switch viewport view target to the preview actor ────────────────
    SetViewTargetWithBlend(ActivePreviewActor, 0.0f);

    // Show the viewmode overlay UI widget
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

    // ── 6. Fire the Blueprint hook so the widget can animate in ───────────
    OnPreviewOpened();
}
void AMaxiMallPreviewController::CloseFurniturePreview()
{
    if (!IsLocalController())
    {
        return;
    }

    // Restore visibility of all showroom booths in the level for the local player locally
    HiddenActors.Empty();

    AShowroomBooth* PreviousBooth = CurrentTargetBooth;
    if (CurrentTargetBooth)
    {
        CurrentTargetBooth->OnProductChanged.RemoveAll(this);
    }

    if (!ActivePreviewActor)
    {
        return;
    }

    // Disable exposure/post-processing overrides on the preview camera before destroying it
    // to prevent exposure settings from leaking/persisting on the player's camera manager
    if (ActivePreviewActor->Camera)
    {
        ActivePreviewActor->Camera->PostProcessBlendWeight = 0.f;
        ActivePreviewActor->Camera->PostProcessSettings = FPostProcessSettings();
    }

    // Restore viewport view target to player pawn instantly
    SetViewTargetWithBlend(GetPawn(), 0.0f);

    // Restore the control rotation to prevent rotation drift on exit
    SetControlRotation(SavedControlRotation);

    // Remove viewmode overlay and restore main configurator UI widget on exit
    if (ViewmodeOverlayInstance)
    {
        ViewmodeOverlayInstance->RemoveFromParent();
    }

    if (PreviousBooth && MainWidgetInstance)
    {
        // Maintain ignore look/move inputs when returning to the main configurator UI
        ResetIgnoreInputFlags();
        SetIgnoreLookInput(true);
        SetIgnoreMoveInput(true);

        CurrentTargetBooth = PreviousBooth;
        // Re-bind the delegate since we removed it above
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

        // Defer input mode and focus restoration to the next tick to prevent Slate capture stealing focus
        TWeakObjectPtr<AMaxiMallPreviewController> WeakThis(this);
        GetWorld()->GetTimerManager().SetTimerForNextTick([WeakThis]()
        {
            if (AMaxiMallPreviewController* StrongThis = WeakThis.Get())
            {
                // Restore character look/move inputs
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

    // Destroy the local actor. This is a local operation — no RPC needed.
    ActivePreviewActor->Destroy();
    ActivePreviewActor = nullptr;

    // Fire the Blueprint hook so the widget can animate out.
    OnPreviewClosed();
}

void AMaxiMallPreviewController::HandlePreviewOrbitInput(float DeltaYaw, float DeltaPitch)
{
    if (!ActivePreviewActor)
    {
        return;
    }

    // Apply sensitivity scaling and forward to the preview actor.
    ActivePreviewActor->RotatePreview(
        DeltaYaw   * OrbitSensitivity,
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

// ─────────────────────────────────────────────────────────────────────────────
// Booth Interaction Wrappers
// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────
// Server RPC Implementations for Booth Interactions
// ─────────────────────────────────────────────────────────────────────────────

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

bool AMaxiMallPreviewController::TraceFurnitureComponent(AShowroomBooth*& OutBooth, EFurnitureComponentType& OutComponentType, UPrimitiveComponent*& OutHitComponent)
{
    OutBooth = nullptr;
    OutComponentType = EFurnitureComponentType::None;
    OutHitComponent = nullptr;

    // Block interaction if UI is currently closing, if a preview is active, or if the configurator UI is already open on the viewport
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
    // Trace under the mouse cursor using visibility channel
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

            // Lock in the targeted showroom booth
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

            // Isolate target component in the preview actor by hiding others
            if (CurrentTargetComponent != EFurnitureComponentType::None)
            {
                if (ActivePreviewActor->CabinetMesh) ActivePreviewActor->CabinetMesh->SetVisibility(false);
                if (ActivePreviewActor->DoorMeshSlot0) ActivePreviewActor->DoorMeshSlot0->SetVisibility(false);
                if (ActivePreviewActor->DoorMeshSlot1) ActivePreviewActor->DoorMeshSlot1->SetVisibility(false);
                if (ActivePreviewActor->CountertopMesh) ActivePreviewActor->CountertopMesh->SetVisibility(false);
                if (ActivePreviewActor->SinkMesh) ActivePreviewActor->SinkMesh->SetVisibility(false);
                if (ActivePreviewActor->FaucetMesh) ActivePreviewActor->FaucetMesh->SetVisibility(false);
                if (ActivePreviewActor->MirrorMesh) ActivePreviewActor->MirrorMesh->SetVisibility(false);
                if (ActivePreviewActor->ClosetMesh) ActivePreviewActor->ClosetMesh->SetVisibility(false);
                if (ActivePreviewActor->ClosetDoorMeshSlot0) ActivePreviewActor->ClosetDoorMeshSlot0->SetVisibility(false);
                if (ActivePreviewActor->ClosetDoorMeshSlot1) ActivePreviewActor->ClosetDoorMeshSlot1->SetVisibility(false);

                // Show only the selected component mesh
                switch (CurrentTargetComponent)
                {
                case EFurnitureComponentType::Cabinet:
                    if (ActivePreviewActor->CabinetMesh) ActivePreviewActor->CabinetMesh->SetVisibility(true);
                    if (ProductSnapshot.DoorsConfig.CabinetDoors.DoorCount == EDoorCount::OneDoor)
                    {
                        if (ActivePreviewActor->DoorMeshSlot0) ActivePreviewActor->DoorMeshSlot0->SetVisibility(true);
                    }
                    else if (ProductSnapshot.DoorsConfig.CabinetDoors.DoorCount == EDoorCount::TwoDoors)
                    {
                        if (ActivePreviewActor->DoorMeshSlot0) ActivePreviewActor->DoorMeshSlot0->SetVisibility(true);
                        if (ActivePreviewActor->DoorMeshSlot1) ActivePreviewActor->DoorMeshSlot1->SetVisibility(true);
                    }
                    break;
                case EFurnitureComponentType::Closet:
                    if (ActivePreviewActor->ClosetMesh) ActivePreviewActor->ClosetMesh->SetVisibility(true);
                    if (ProductSnapshot.DoorsConfig.ClosetDoors.DoorCount == EDoorCount::OneDoor)
                    {
                        if (ActivePreviewActor->ClosetDoorMeshSlot0) ActivePreviewActor->ClosetDoorMeshSlot0->SetVisibility(true);
                    }
                    else if (ProductSnapshot.DoorsConfig.ClosetDoors.DoorCount == EDoorCount::TwoDoors)
                    {
                        if (ActivePreviewActor->ClosetDoorMeshSlot0) ActivePreviewActor->ClosetDoorMeshSlot0->SetVisibility(true);
                        if (ActivePreviewActor->ClosetDoorMeshSlot1) ActivePreviewActor->ClosetDoorMeshSlot1->SetVisibility(true);
                    }
                    break;
                case EFurnitureComponentType::Doors:
                    if (ProductSnapshot.DoorsConfig.CabinetDoors.DoorCount == EDoorCount::OneDoor)
                    {
                        if (ActivePreviewActor->DoorMeshSlot0) ActivePreviewActor->DoorMeshSlot0->SetVisibility(true);
                    }
                    else if (ProductSnapshot.DoorsConfig.CabinetDoors.DoorCount == EDoorCount::TwoDoors)
                    {
                        if (ActivePreviewActor->DoorMeshSlot0) ActivePreviewActor->DoorMeshSlot0->SetVisibility(true);
                        if (ActivePreviewActor->DoorMeshSlot1) ActivePreviewActor->DoorMeshSlot1->SetVisibility(true);
                    }
                    if (ProductSnapshot.DoorsConfig.ClosetDoors.DoorCount == EDoorCount::OneDoor)
                    {
                        if (ActivePreviewActor->ClosetDoorMeshSlot0) ActivePreviewActor->ClosetDoorMeshSlot0->SetVisibility(true);
                    }
                    else if (ProductSnapshot.DoorsConfig.ClosetDoors.DoorCount == EDoorCount::TwoDoors)
                    {
                        if (ActivePreviewActor->ClosetDoorMeshSlot0) ActivePreviewActor->ClosetDoorMeshSlot0->SetVisibility(true);
                        if (ActivePreviewActor->ClosetDoorMeshSlot1) ActivePreviewActor->ClosetDoorMeshSlot1->SetVisibility(true);
                    }
                    break;
                case EFurnitureComponentType::Countertop:
                    if (ActivePreviewActor->CountertopMesh) ActivePreviewActor->CountertopMesh->SetVisibility(true);
                    break;
                case EFurnitureComponentType::Sink:
                    if (CurrentTargetBooth && CurrentTargetBooth->GetActiveCountertopType() == ECountertopType::SurfaceMounted)
                    {
                        if (ActivePreviewActor->SinkMesh) ActivePreviewActor->SinkMesh->SetVisibility(true);
                    }
                    break;
                case EFurnitureComponentType::Faucet:
                    if (ActivePreviewActor->FaucetMesh) ActivePreviewActor->FaucetMesh->SetVisibility(true);
                    break;
                case EFurnitureComponentType::Mirror:
                    if (ActivePreviewActor->MirrorMesh) ActivePreviewActor->MirrorMesh->SetVisibility(true);
                    break;
                default:
                    break;
                }
            }

            // Maintain camera focus on the isolated component
            if (CurrentTargetComponent != EFurnitureComponentType::None)
            {
                ActivePreviewActor->SetFocusComponent(CurrentTargetComponent);
            }
        }
    }

    // Refresh main UI combo selections if open
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
        // Block if the player is rotating the camera with RMB.
        // NOTE: Do NOT add IsInputKeyDown here — the Blueprint fires this function
        // on the Pressed event, so RMB is always down at the call site. Only the
        // drag flag (set via raw mouse axis in PlayerTick) reliably distinguishes
        // a camera rotation from a genuine click.
        if (bRightMouseIsDragging)
        {
            return;
        }

        if (!Booth) return;


        if (Component == EFurnitureComponentType::Doors)
        {
            Component = EFurnitureComponentType::Cabinet;
        }

        // Disable character look/move inputs to prevent character movement/rotation during configuration
        ResetIgnoreInputFlags();
        SetIgnoreLookInput(true);
        SetIgnoreMoveInput(true);

        if (!MainWidgetClass)
        {
            UE_LOG(LogTemp, Error, TEXT("[PreviewController] ToggleConfiguratorUI failed: MainWidgetClass is null! Please configure MainWidgetClass in your Player Controller defaults."));
        }

        // If another configuration widget is open on a different booth, close it first
        if (MainWidgetInstance && CurrentTargetBooth && CurrentTargetBooth != Booth)
        {
            ToggleConfiguratorUI(CurrentTargetBooth, CurrentTargetComponent, false);
        }

        CurrentTargetBooth = Booth;
        CurrentTargetComponent = Component;

        // Listen for booth configuration updates dynamically while UI is open
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
            UE_LOG(LogTemp, Error, TEXT("[PreviewController] ToggleConfiguratorUI failed: MainWidgetInstance could not be created/found."));
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

        // Defer input mode change, widget removal, and viewport focus restoration to next tick
        // to prevent Slate mouse capture lockup when clicked from a button.
        TWeakObjectPtr<AMaxiMallPreviewController> WeakThis(this);
        GetWorld()->GetTimerManager().SetTimerForNextTick([WeakThis]()
        {
            if (AMaxiMallPreviewController* StrongThis = WeakThis.Get())
            {
                // Restore character look/move inputs
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

    // Since Cabinet (and Doors, which mapped to Cabinet) uses FFurnitureCabinetOptions,
    // and other components use FFurnitureComponentOptions, we need to handle them separately.

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

    // Search for combination match in the component metadata matrix
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

    // Fallback: If no combination is found, try to use the first combination
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
