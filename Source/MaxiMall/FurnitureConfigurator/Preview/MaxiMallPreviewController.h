// Copyright MaxiMall Project. All Rights Reserved.
// MaxiMallPreviewController.h
//
// AMaxiMallPreviewController — PlayerController subclass providing:
//
//  1. LOOSE COUPLING BRIDGE
//     The controller knows about AShowroomBooth and AFurniturePreviewActor,
//     but NEVER about the specific Character class used in the destination
//     UE 5.6 project. The existing Blueprint Character simply calls
//     "Get Player Controller → Cast → Open Preview" without any C++ coupling.
//     The controller also exposes a BlueprintImplementableEvent so Blueprint
//     character logic can hook in without any C++ modifications.
//
//  2. PREVIEW LIFECYCLE MANAGEMENT
//     OpenFurniturePreview() spawns AFurniturePreviewActor in an off-camera
//     staging position, pipes the active booth's product data into it, and
//     returns the render target for the widget to display.
//     CloseFurniturePreview() destroys the preview actor cleanly.
//
//  3. ORBIT INPUT FORWARDING
//     HandlePreviewOrbitInput() is called by the widget (via Blueprint) and
//     passes delta values directly to AFurniturePreviewActor::RotatePreview().
//     No Server RPCs are involved. No networking overhead.
//
// Compatible: UE 5.3 → UE 5.6+

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FurnitureConfigurator/Data/FurnitureTypes.h"
#include "FurnitureConfigurator/Preview/FurniturePreviewActor.h"
#include "MaxiMallPreviewController.generated.h"

class AShowroomBooth;

// ─────────────────────────────────────────────────────────────────────────────
// AMaxiMallPreviewController
// ─────────────────────────────────────────────────────────────────────────────

UCLASS(ClassGroup = (MaxiMall),
       meta = (DisplayName = "MaxiMall Preview Player Controller"))
class MAXIMALL_API AMaxiMallPreviewController : public APlayerController
{
    GENERATED_BODY()

public:

    AMaxiMallPreviewController();

    // ─────────────────────────────────────────────────────────────────────
    // PREVIEW MANAGEMENT
    // ─────────────────────────────────────────────────────────────────────

    /**
     * Opens the isolated furniture preview for the specified booth.
     *
     * Workflow:
     *  1. Reads the active product row from the target booth.
     *  2. Spawns AFurniturePreviewActor at the designated staging location.
     *  3. Calls LoadProductPreview() on the preview actor.
     *  4. Returns the RenderTarget for the widget to bind to a UImage.
     *
     * Call this from the Blueprint Character's interaction logic when the
     * player activates the "Inspect" UI. No Character type knowledge needed.
     *
     * @param TargetBooth  The showroom booth currently being inspected.
     * @return The RenderTarget to bind to the preview widget's UImage, or null on failure.
     */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Preview",
              meta = (DisplayName = "Open Furniture Preview"))
    void OpenFurniturePreview(AShowroomBooth* TargetBooth);

    /**
     * Closes and cleans up the isolated preview.
     * Destroys the preview actor. Safe to call even if no preview is active.
     */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Preview",
              meta = (DisplayName = "Close Furniture Preview"))
    void CloseFurniturePreview();

    /**
     * Forwards orbit drag input to the active preview actor.
     * Intended to be called from the widget's mouse-drag event every frame.
     *
     * If no preview is active, this is a safe no-op.
     *
     * @param DeltaYaw    Horizontal drag in degrees (scaled by sensitivity).
     * @param DeltaPitch  Vertical drag in degrees (clamped inside PreviewActor).
     */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Preview",
              meta = (DisplayName = "Handle Preview Orbit Input"))
    void HandlePreviewOrbitInput(float DeltaYaw, float DeltaPitch);

    /**
     * Resets the preview rotation to the default angle.
     */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Preview",
              meta = (DisplayName = "Reset Preview Rotation"))
    void ResetPreviewRotation();

    /**
     * Returns true if a preview is currently active.
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MaxiMall | Preview",
              meta = (DisplayName = "Is Preview Active"))
    bool IsPreviewActive() const;

    /**
     * Forwards zoom input to the active preview actor.
     *
     * @param DeltaZoom  Distance to zoom (positive zooms out, negative zooms in).
     */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Preview",
              meta = (DisplayName = "Handle Preview Zoom Input"))
    void HandlePreviewZoomInput(float DeltaZoom);

    /**
     * Traces from the mouse position under the cursor, finding if a showroom booth was hit
     * and which subcomponent was hit.
     */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Interaction",
              meta = (DisplayName = "Trace Furniture Component"))
    bool TraceFurnitureComponent(AShowroomBooth*& OutBooth, EFurnitureComponentType& OutComponentType, UPrimitiveComponent*& OutHitComponent);

    /**
     * Handles double-click mouse interactions for doors/drawers.
     */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Interaction",
              meta = (DisplayName = "Handle Double-Click Interaction"))
    void HandleDoubleClickInteraction();

    /**
     * Sets the local preview focus on a specific subcomponent.
     */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Preview",
              meta = (DisplayName = "Focus Preview On Component"))
    void FocusPreviewOnComponent(EFurnitureComponentType ComponentType);

    // ─────────────────────────────────────────────────────────────────────
    // BOOTH INTERACTION API
    // These are convenience wrappers callable from any Blueprint without
    // needing a direct reference to AShowroomBooth's C++ class.
    // ─────────────────────────────────────────────────────────────────────

    /**
     * Requests a product change on the given booth.
     * Internally calls AShowroomBooth::RequestProductChange(), which handles
     * the server RPC if running on a client.
     *
     * This wrapper exists so the Blueprint Character can call it on the
     * controller without needing an explicit AShowroomBooth Blueprint cast.
     *
     * @param TargetBooth  The booth to modify.
     * @param NewProductID RowName of the desired product in the DataTable.
     */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Booth",
              meta = (DisplayName = "Request Booth Product Change"))
    void RequestBoothProductChange(AShowroomBooth* TargetBooth, FName NewProductID);

    /**
     * Requests a door toggle on the given booth.
     *
     * @param TargetBooth  The booth containing the door.
     * @param SlotIndex    0 = left/single, 1 = right.
     */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Booth",
              meta = (DisplayName = "Request Booth Door Toggle"))
    void RequestBoothDoorToggle(AShowroomBooth* TargetBooth, int32 SlotIndex);

    // ─────────────────────────────────────────────────────────────────────
    // BLUEPRINT EVENTS (Loose Coupling Hooks)
    // These can be overridden in the Blueprint child of this controller
    // (or the Blueprint Character via Event Dispatcher) without any C++ change.
    // ─────────────────────────────────────────────────────────────────────

    /**
     * Called when the preview is successfully opened.
     * Override in Blueprint to animate the widget in, set input mode, etc.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "MaxiMall | Preview Events",
              meta = (DisplayName = "On Preview Opened"))
    void OnPreviewOpened();

    /**
     * Called when the preview is closed.
     * Override in Blueprint to animate the widget out, restore input mode, etc.
     */
    UFUNCTION(BlueprintImplementableEvent, Category = "MaxiMall | Preview Events",
              meta = (DisplayName = "On Preview Closed"))
    void OnPreviewClosed();

    // ─────────────────────────────────────────────────────────────────────
    // CONFIG
    // ─────────────────────────────────────────────────────────────────────

    /** The Blueprint subclass of AFurniturePreviewActor to spawn for the preview. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | Preview Config",
              meta = (DisplayName = "Preview Actor Class"))
    TSubclassOf<AFurniturePreviewActor> PreviewActorClass;

    /**
     * World-space location where the preview actor is spawned.
     * Place this far from the main level so it is outside camera view
     * but still within the streaming distance for the capture component.
     * Default: 10000 units up from the world origin.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | Preview Config",
              meta = (DisplayName = "Preview Staging Location"))
    FVector PreviewStagingLocation = FVector(0.f, 0.f, 10000.f);

    /**
     * Mouse/touch drag sensitivity multiplier for orbit input.
     * Increase to rotate faster, decrease for finer control.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | Preview Config",
              meta = (DisplayName = "Orbit Sensitivity", ClampMin = "0.1", ClampMax = "10.0"))
    float OrbitSensitivity = 1.f;

    /** Static mesh asset used for the studio backdrop (e.g. an inverted sphere). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | Preview Config",
              meta = (DisplayName = "Backdrop Static Mesh"))
    TSoftObjectPtr<UStaticMesh> BackdropMeshAsset;

    /** Material asset used for the studio backdrop (e.g. unlit two-sided white). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | Preview Config",
              meta = (DisplayName = "Backdrop Material"))
    TSoftObjectPtr<UMaterialInterface> BackdropMaterialAsset;

private:

    /**
     * The currently active preview actor. Null when no preview is open.
     * Explicitly not replicated — local to this machine's controller only.
     */
    UPROPERTY()
    TObjectPtr<AFurniturePreviewActor> ActivePreviewActor;

    /** Caches the player's control rotation before entering preview mode to restore it on exit. */
    FRotator SavedControlRotation;

    /** The showroom booth currently being inspected/previewed. */
    UPROPERTY()
    TObjectPtr<AShowroomBooth> CurrentTargetBooth;

    /** Delegate callback called when the target showroom booth's configuration changes. */
    UFUNCTION()
    void OnTargetBoothProductChanged(AShowroomBooth* Booth, FName NewProductID);
};
