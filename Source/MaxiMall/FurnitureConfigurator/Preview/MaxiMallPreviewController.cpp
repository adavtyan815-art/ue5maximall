// Copyright MaxiMall Project. All Rights Reserved.
// MaxiMallPreviewController.cpp

#include "FurnitureConfigurator/Preview/MaxiMallPreviewController.h"

#include "FurnitureConfigurator/ShowroomBooth.h"
#include "FurnitureConfigurator/Preview/FurniturePreviewActor.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "Blueprint/UserWidget.h"
#include "FurnitureConfigurator/UI/ConfiguratorMainWidget.h"
#include "FurnitureConfigurator/UI/ViewmodeOverlayWidget.h"

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

    // Ensure we start in Game Only input mode with mouse cursor hidden for the local player controller
    if (IsLocalController())
    {
        FInputModeGameOnly InputMode;
        SetInputMode(InputMode);
        bShowMouseCursor = false;
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

    CurrentTargetComponent = FocusComponent;

    // Safety check: Ensure we have a possessed pawn before allowing preview mode
    if (!GetPawn())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PreviewController] OpenFurniturePreview called before possessing a pawn. Ignoring."));
        return;
    }

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

    // Isolate target booth by hiding all other booths in the level locally for this player only
    HiddenActors.Empty();
    for (TActorIterator<AShowroomBooth> It(World); It; ++It)
    {
        AShowroomBooth* Booth = *It;
        if (Booth && Booth != TargetBooth)
        {
            HiddenActors.Add(Booth);
        }
    }

    ActivePreviewActor = World->SpawnActor<AFurniturePreviewActor>(
        SpawnClass,
        PreviewStagingLocation,
        FRotator::ZeroRotator,
        SpawnParams);

    if (!ActivePreviewActor)
    {
        UE_LOG(LogTemp, Error, TEXT("[PreviewController] Failed to spawn AFurniturePreviewActor."));
        return;
    }

    // ── 4. Load the product snapshot into the preview actor ───────────────
    ActivePreviewActor->LoadProductPreview(ProductSnapshot, TargetBooth->ActiveState, TargetBooth);

    // Apply component-level visibility isolation inside the preview viewport
    if (CurrentTargetComponent != EFurnitureComponentType::None)
    {
        // Hide all meshes by default
        if (ActivePreviewActor->CabinetMesh) ActivePreviewActor->CabinetMesh->SetVisibility(false);
        if (ActivePreviewActor->DoorMeshSlot0) ActivePreviewActor->DoorMeshSlot0->SetVisibility(false);
        if (ActivePreviewActor->DoorMeshSlot1) ActivePreviewActor->DoorMeshSlot1->SetVisibility(false);
        if (ActivePreviewActor->CountertopMesh) ActivePreviewActor->CountertopMesh->SetVisibility(false);
        if (ActivePreviewActor->SinkMesh) ActivePreviewActor->SinkMesh->SetVisibility(false);
        if (ActivePreviewActor->FaucetMesh) ActivePreviewActor->FaucetMesh->SetVisibility(false);
        if (ActivePreviewActor->MirrorMesh) ActivePreviewActor->MirrorMesh->SetVisibility(false);
        if (ActivePreviewActor->ClosetMesh) ActivePreviewActor->ClosetMesh->SetVisibility(false);

        // Show only the selected component mesh
        switch (CurrentTargetComponent)
        {
        case EFurnitureComponentType::Cabinet:
            if (ActivePreviewActor->CabinetMesh) ActivePreviewActor->CabinetMesh->SetVisibility(true);
            break;
        case EFurnitureComponentType::Closet:
            if (ActivePreviewActor->ClosetMesh) ActivePreviewActor->ClosetMesh->SetVisibility(true);
            break;
        case EFurnitureComponentType::Doors:
            if (ProductSnapshot.DoorCount == EDoorCount::OneDoor)
            {
                if (ActivePreviewActor->DoorMeshSlot0) ActivePreviewActor->DoorMeshSlot0->SetVisibility(true);
            }
            else if (ProductSnapshot.DoorCount == EDoorCount::TwoDoors)
            {
                if (ActivePreviewActor->DoorMeshSlot0) ActivePreviewActor->DoorMeshSlot0->SetVisibility(true);
                if (ActivePreviewActor->DoorMeshSlot1) ActivePreviewActor->DoorMeshSlot1->SetVisibility(true);
            }
            break;
        case EFurnitureComponentType::Countertop:
            if (ActivePreviewActor->CountertopMesh) ActivePreviewActor->CountertopMesh->SetVisibility(true);
            break;
        case EFurnitureComponentType::Sink:
            if (ProductSnapshot.CountertopType == ECountertopType::SurfaceMounted)
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

        // Focus camera orbit on the isolated component
        ActivePreviewActor->SetFocusComponent(CurrentTargetComponent);
    }

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

    if (CurrentTargetBooth)
    {
        CurrentTargetBooth->OnProductChanged.RemoveAll(this);
        CurrentTargetBooth = nullptr;
    }
    CurrentTargetComponent = EFurnitureComponentType::None;

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

    // Remove viewmode overlay and restore main configurator UI widget on exit
    if (ViewmodeOverlayInstance)
    {
        ViewmodeOverlayInstance->RemoveFromParent();
    }

    if (CurrentTargetBooth && MainWidgetInstance)
    {
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
        InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        SetInputMode(InputMode);
        bShowMouseCursor = true;
    }
    else
    {
        // Restore the control rotation to prevent rotation drift on exit
        SetControlRotation(SavedControlRotation);

        FInputModeGameOnly InputMode;
        SetInputMode(InputMode);
        bShowMouseCursor = false;
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

    // Delegate entirely to the booth. The booth handles authority checks
    // and server RPC forwarding internally.
    TargetBooth->RequestProductChange(NewProductID);
}

void AMaxiMallPreviewController::RequestBoothDoorToggle(AShowroomBooth* TargetBooth,
                                                         int32 SlotIndex)
{
    if (!TargetBooth)
    {
        return;
    }

    TargetBooth->RequestDoorToggle(SlotIndex);
}

bool AMaxiMallPreviewController::TraceFurnitureComponent(AShowroomBooth*& OutBooth, EFurnitureComponentType& OutComponentType, UPrimitiveComponent*& OutHitComponent)
{
    OutBooth = nullptr;
    OutComponentType = EFurnitureComponentType::None;
    OutHitComponent = nullptr;

    FHitResult HitResult;
    // Trace under the mouse cursor using visibility channel
    if (GetHitResultUnderCursor(ECC_Visibility, false, HitResult))
    {
        AShowroomBooth* HitBooth = Cast<AShowroomBooth>(HitResult.GetActor());
        if (HitBooth)
        {
            OutBooth = HitBooth;
            OutHitComponent = HitResult.GetComponent();

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

    if (TraceFurnitureComponent(HitBooth, ComponentType, HitComponent))
    {
        if (ComponentType == EFurnitureComponentType::Doors)
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

            // Maintain component visibility isolation if active
            if (CurrentTargetComponent != EFurnitureComponentType::None)
            {
                // Hide all meshes by default
                if (ActivePreviewActor->CabinetMesh) ActivePreviewActor->CabinetMesh->SetVisibility(false);
                if (ActivePreviewActor->DoorMeshSlot0) ActivePreviewActor->DoorMeshSlot0->SetVisibility(false);
                if (ActivePreviewActor->DoorMeshSlot1) ActivePreviewActor->DoorMeshSlot1->SetVisibility(false);
                if (ActivePreviewActor->CountertopMesh) ActivePreviewActor->CountertopMesh->SetVisibility(false);
                if (ActivePreviewActor->SinkMesh) ActivePreviewActor->SinkMesh->SetVisibility(false);
                if (ActivePreviewActor->FaucetMesh) ActivePreviewActor->FaucetMesh->SetVisibility(false);
                if (ActivePreviewActor->MirrorMesh) ActivePreviewActor->MirrorMesh->SetVisibility(false);
                if (ActivePreviewActor->ClosetMesh) ActivePreviewActor->ClosetMesh->SetVisibility(false);

                // Show only the selected component mesh
                switch (CurrentTargetComponent)
                {
                case EFurnitureComponentType::Cabinet:
                    if (ActivePreviewActor->CabinetMesh) ActivePreviewActor->CabinetMesh->SetVisibility(true);
                    break;
                case EFurnitureComponentType::Closet:
                    if (ActivePreviewActor->ClosetMesh) ActivePreviewActor->ClosetMesh->SetVisibility(true);
                    break;
                case EFurnitureComponentType::Doors:
                    if (ProductSnapshot.DoorCount == EDoorCount::OneDoor)
                    {
                        if (ActivePreviewActor->DoorMeshSlot0) ActivePreviewActor->DoorMeshSlot0->SetVisibility(true);
                    }
                    else if (ProductSnapshot.DoorCount == EDoorCount::TwoDoors)
                    {
                        if (ActivePreviewActor->DoorMeshSlot0) ActivePreviewActor->DoorMeshSlot0->SetVisibility(true);
                        if (ActivePreviewActor->DoorMeshSlot1) ActivePreviewActor->DoorMeshSlot1->SetVisibility(true);
                    }
                    break;
                case EFurnitureComponentType::Countertop:
                    if (ActivePreviewActor->CountertopMesh) ActivePreviewActor->CountertopMesh->SetVisibility(true);
                    break;
                case EFurnitureComponentType::Sink:
                    if (ProductSnapshot.CountertopType == ECountertopType::SurfaceMounted)
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

                // Focus camera orbit on the isolated component
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
        if (!Booth) return;

        // If another configuration widget is open on a different booth, close it first
        if (MainWidgetInstance && CurrentTargetBooth && CurrentTargetBooth != Booth)
        {
            ToggleConfiguratorUI(CurrentTargetBooth, CurrentTargetComponent, false);
        }

        CurrentTargetBooth = Booth;
        CurrentTargetComponent = Component;

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
            InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
            SetInputMode(InputMode);
            bShowMouseCursor = true;
        }
    }
    else
    {
        if (MainWidgetInstance)
        {
            MainWidgetInstance->RemoveFromParent();
        }

        if (ViewmodeOverlayInstance)
        {
            ViewmodeOverlayInstance->RemoveFromParent();
        }

        CurrentTargetBooth = nullptr;
        CurrentTargetComponent = EFurnitureComponentType::None;

        FInputModeGameOnly InputMode;
        SetInputMode(InputMode);
        bShowMouseCursor = false;
    }
}
