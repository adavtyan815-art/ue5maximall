// Copyright MaxiMall Project. All Rights Reserved.
// MaxiMallPreviewController.cpp

#include "FurnitureConfigurator/Preview/MaxiMallPreviewController.h"

#include "Engine/Engine.h"
#include "FurnitureConfigurator/ShowroomBooth.h"
#include "FurnitureConfigurator/Preview/FurniturePreviewActor.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "Blueprint/UserWidget.h"
#include "FurnitureConfigurator/UI/ConfiguratorMainWidget.h"
#include "FurnitureConfigurator/UI/ViewmodeOverlayWidget.h"
#include "Framework/Application/SlateApplication.h"

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
}

void AMaxiMallPreviewController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);

    if (!IsLocalController())
    {
        return;
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

    // Disable character look/move inputs to prevent character movement/rotation during preview
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
    }

    // Focus camera orbit on the isolated component
    if (CurrentTargetComponent != EFurnitureComponentType::None)
    {
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

    // Restore character look/move inputs
    SetIgnoreLookInput(false);
    SetIgnoreMoveInput(false);

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

        FInputModeGameOnly InputMode;
        SetInputMode(InputMode);
        bShowMouseCursor = false;

        if (FSlateApplication::IsInitialized())
        {
            FSlateApplication::Get().SetAllUserFocusToGameViewport();
        }
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
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Cyan, FString::Printf(TEXT("ToggleConfiguratorUI entry. Local: %s, bOpen: %s"), 
            IsLocalController() ? TEXT("Yes") : TEXT("No"), 
            bOpen ? TEXT("True") : TEXT("False")));
    }

    if (!IsLocalController())
    {
        return;
    }

    if (bOpen)
    {
        if (!Booth) return;

        // Disable character look/move inputs to prevent character movement/rotation during configuration
        SetIgnoreLookInput(true);
        SetIgnoreMoveInput(true);

        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Yellow, FString::Printf(TEXT("ToggleConfiguratorUI: MainWidgetClass: %s"), 
                MainWidgetClass ? *MainWidgetClass->GetName() : TEXT("Null")));
        }

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
            if (GEngine)
            {
                GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Green, FString::Printf(TEXT("Created MainWidgetInstance: %s"), 
                    MainWidgetInstance ? TEXT("Success") : TEXT("Failed")));
            }
        }

        if (MainWidgetInstance)
        {
            UConfiguratorMainWidget* MainWidget = Cast<UConfiguratorMainWidget>(MainWidgetInstance);
            if (MainWidget)
            {
                MainWidget->SetupWidget(this, Booth, Component);
            }
            else
            {
                if (GEngine)
                {
                    GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red, TEXT("Failed to Cast MainWidgetInstance to UConfiguratorMainWidget!"));
                }
            }

            if (!MainWidgetInstance->IsInViewport())
            {
                MainWidgetInstance->AddToViewport();
                if (GEngine)
                {
                    GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Green, TEXT("Added MainWidgetInstance to Viewport!"));
                }
            }
            else
            {
                if (GEngine)
                {
                    GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Orange, TEXT("MainWidgetInstance was already in Viewport!"));
                }
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
            if (GEngine)
            {
                GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red, TEXT("ToggleConfiguratorUI failed: MainWidgetInstance is Null!"));
            }
        }
    }
    else
    {
        // Restore character look/move inputs
        SetIgnoreLookInput(false);
        SetIgnoreMoveInput(false);

        if (MainWidgetInstance)
        {
            MainWidgetInstance->RemoveFromParent();
        }

        if (ViewmodeOverlayInstance)
        {
            ViewmodeOverlayInstance->RemoveFromParent();
        }

        if (CurrentTargetBooth)
        {
            CurrentTargetBooth->OnProductChanged.RemoveAll(this);
        }
        CurrentTargetBooth = nullptr;
        CurrentTargetComponent = EFurnitureComponentType::None;

        FInputModeGameOnly InputMode;
        SetInputMode(InputMode);
        bShowMouseCursor = false;

        if (FSlateApplication::IsInitialized())
        {
            FSlateApplication::Get().SetAllUserFocusToGameViewport();
        }
    }
}
