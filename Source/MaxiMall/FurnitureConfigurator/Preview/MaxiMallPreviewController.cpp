// Copyright MaxiMall Project. All Rights Reserved.
// MaxiMallPreviewController.cpp

#include "FurnitureConfigurator/Preview/MaxiMallPreviewController.h"

#include "FurnitureConfigurator/ShowroomBooth.h"
#include "FurnitureConfigurator/Preview/FurniturePreviewActor.h"
#include "Engine/World.h"

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

AMaxiMallPreviewController::AMaxiMallPreviewController()
{
    // Player controllers replicate by default — that's fine, only the
    // ActivePreviewActor property is explicitly excluded from replication.
    ActivePreviewActor = nullptr;
    
    // Default to the C++ base class so it is not empty in the Editor by default
    PreviewActorClass = AFurniturePreviewActor::StaticClass();

    // Set default fallback asset paths (using the engine's built-in shape sphere)
    BackdropMeshAsset = FSoftObjectPath(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    // Default material fallback path (engine default material)
    BackdropMaterialAsset = FSoftObjectPath(TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Preview Management
// ─────────────────────────────────────────────────────────────────────────────

void AMaxiMallPreviewController::OpenFurniturePreview(AShowroomBooth* TargetBooth)
{
    if (!TargetBooth)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PreviewController] OpenFurniturePreview called with null TargetBooth."));
        return;
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

    // ── 4. Load the product snapshot into the preview actor ───────────────
    ActivePreviewActor->LoadProductPreview(ProductSnapshot);

    // Load and assign the backdrop static mesh and material dynamically
    UStaticMesh* LoadedBackdropMesh = BackdropMeshAsset.LoadSynchronous();
    UMaterialInterface* LoadedBackdropMat = BackdropMaterialAsset.LoadSynchronous();
    ActivePreviewActor->SetupBackdrop(LoadedBackdropMesh, LoadedBackdropMat);

    // ── 5. Switch viewport view target to the preview actor ────────────────
    SetViewTargetWithBlend(ActivePreviewActor, 0.0f);

    // ── 6. Fire the Blueprint hook so the widget can animate in ───────────
    OnPreviewOpened();
}

void AMaxiMallPreviewController::CloseFurniturePreview()
{
    if (!ActivePreviewActor)
    {
        return;
    }

    // Restore viewport view target to player pawn instantly
    SetViewTargetWithBlend(GetPawn(), 0.0f);

    // Restore the control rotation to prevent rotation drift on exit
    SetControlRotation(SavedControlRotation);

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

void AMaxiMallPreviewController::RequestBoothProductChange(AShowroomBooth* TargetBooth,
                                                            FName NewProductID)
{
    if (!TargetBooth)
    {
        return;
    }

    // Delegate entirely to the booth. The booth handles authority checks
    // and server RPC forwarding internally.
    TargetBooth->RequestProductChange(NewProductID);

    // If the preview is open, refresh it with the new product data.
    // This is purely a local visual update — no extra networking.
    if (ActivePreviewActor)
    {
        FFurnitureProductRow UpdatedSnapshot;
        if (TargetBooth->GetActiveProductData(UpdatedSnapshot))
        {
            ActivePreviewActor->LoadProductPreview(UpdatedSnapshot);
        }
    }
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
