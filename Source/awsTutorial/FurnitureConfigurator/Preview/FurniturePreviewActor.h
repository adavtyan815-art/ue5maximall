// Copyright MaxiMall Project. All Rights Reserved.
// FurniturePreviewActor.h
//
// AFurniturePreviewActor — the client-local Studio ViewMode preview subject.
//
// KEY GUARANTEE: bReplicates = false hard-coded. Spawned exclusively by
// AAwsTutorial_PlayerController on the owning client at the isolated studio spot.
//
// Architecture (Studio ViewMode):
//   - Mirrors the AShowroomBooth mesh layout from a local product snapshot; the
//     booth itself is never moved or modified.
//   - The MESH stays still; the CAMERA orbits (per-channel eased, fling inertia,
//     fixed-pivot pan, center-based multiplicative zoom, idle turntable). Per-frame
//     updates are pumped by the Studio Stage actor's Tick (StudioTickUpdate).
//   - Preview meshes live on LIGHTING CHANNEL 1, lit exclusively by the stage's
//     calibrated softbox rig + captured-scene IBL (see AStudioStageActor).
//   - Component isolation: only the focused mesh group is visible during preview.
//   - AR export reads GetMeshRootResetState for the neutral mesh state.
//
// Compatible: UE 5.3 → UE 5.6+

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FurnitureConfigurator/Data/FurnitureTypes.h"
#include "FurniturePreviewActor.generated.h"

class UStaticMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class ACharacter;
class AShowroomBooth;
class AStudioStageActor;

// ─────────────────────────────────────────────────────────────────────────────
// Per-Component Preview Configuration
// Assign one of these per furniture component type in BP_FurniturePreviewActor.
// ─────────────────────────────────────────────────────────────────────────────

USTRUCT(BlueprintType)
struct FPreviewComponentConfig
{
    GENERATED_BODY()

    // ── Camera & Framing ─────────────────────────────────────────────────────

    /** EV100 offset folded into the studio camera recipe (positive values brighten dark meshes). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera & Zoom",
              meta = (DisplayName = "Camera Exposure Offset (EV)", ClampMin = "-4.0", ClampMax = "4.0", UIMin = "-2.0", UIMax = "2.0"))
    float ExposureCompensation = 0.f;

    /**
     * Yaw offset (degrees) added to the entry view direction for THIS component.
     * The base entry view faces the booth's front (its forward axis). If a mesh is
     * authored or mounted facing a different local direction and enters Viewmode
     * showing its side, correct it here once — the offset is booth-relative, so it
     * holds for every placement and rotation of the booth in any bathroom.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera & Zoom",
              meta = (DisplayName = "Entry View Yaw Offset (deg)", ClampMin = "-180.0", ClampMax = "180.0"))
    float EntryYawOffsetDegrees = 0.f;

    /**
     * Studio camera distance multiplier for THIS component: scales the
     * automatic box-aware fit distance at entry and on reset. 1 = the auto
     * fit; 0.85 opens slightly closer, 1.2 slightly farther. The zoom range
     * and all interaction stay driven by the auto fit — this only nudges the
     * opening framing, universally for any mesh assigned to the component.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera & Zoom",
              meta = (DisplayName = "Studio Camera Distance Scale", ClampMin = "0.4", ClampMax = "2.5", UIMin = "0.6", UIMax = "1.8"))
    float StudioDistanceScale = 1.f;

    /**
     * Studio light intensity multiplier for THIS component: scales the whole
     * softbox rig AND the IBL ambient/reflections together (all light ratios
     * preserved), while the backdrop and exposure stay untouched — unlike the
     * EV offset, which brightens the entire frame including the background.
     * Applied on focus; costs a few SetIntensity calls, no sky recapture.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera & Zoom",
              meta = (DisplayName = "Studio Light Intensity Scale", ClampMin = "0.1", ClampMax = "4.0", UIMin = "0.5", UIMax = "2.0"))
    float StudioLightScale = 1.f;

};

// ─────────────────────────────────────────────────────────────────────────────
// AFurniturePreviewActor
// ─────────────────────────────────────────────────────────────────────────────

UCLASS(Blueprintable, BlueprintType, NotPlaceable,
       HideCategories = (Rendering, Physics, Collision, Lighting, HLOD, Navigation, Input,
                         ActorTick, ComponentTick, LOD, Cooking, Replication, Tags,
                         TextureStreaming, RayTracing, PathTracing, AssetUserData),
       meta = (DisplayName = "Furniture Preview Actor (Studio ViewMode)"))
class AWSTUTORIAL_API AFurniturePreviewActor : public AActor
{
    GENERATED_BODY()

public:

    AFurniturePreviewActor();

    // ── Lifecycle ──────────────────────────────────────────────────────────
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    // Tick is disabled (PrimaryActorTick.bCanEverTick = false).

    // ─────────────────────────────────────────────────────────────────────
    // VISUAL COMPONENTS
    // ─────────────────────────────────────────────────────────────────────

    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<USceneComponent> PreviewRoot;

    /** Pivot scene root parenting all furniture mesh components. */
    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<USceneComponent> MeshRoot;

    /**
     * AR export support: MeshRoot's neutral world state captured at focus time
     * (the mesh never moves in Studio ViewMode). Returns false when no state
     * has been captured yet (nothing to neutralize).
     */
    bool GetMeshRootResetState(FVector& OutLocation, FQuat& OutRotation) const
    {
        OutLocation = WIP_MeshRootLocAtReset;
        OutRotation = WIP_InitialMeshRootQuat;
        return !WIP_MeshRootLocAtReset.IsNearlyZero();
    }

    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> CabinetMesh;

    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> DoorMeshSlot0;

    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> DoorMeshSlot1;

    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> CountertopMesh;

    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> SinkMesh;

    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> FaucetMesh;

    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> MirrorMesh;

    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> ClosetMesh;

    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> ClosetDoorMeshSlot0;

    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> ClosetDoorMeshSlot1;

    /** SpringArm driving camera orbit around WIP_FocusPivotWorld. */
    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<USpringArmComponent> SpringArm;

    /** Camera attached to the SpringArm socket. */
    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UCameraComponent> Camera;

    // ─────────────────────────────────────────────────────────────────────
    // PREVIEW CONFIG
    // ─────────────────────────────────────────────────────────────────────

    /**
     * Downward tilt (degrees, negative = camera above looking down) of the entry
     * view for every component — the classic product-shot three-quarter angle.
     * 0 = perfectly level entry view.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config",
              meta = (DisplayName = "Entry View Pitch (deg)", ClampMin = "-60.0", ClampMax = "10.0"))
    float EntryPitchDegrees = -15.f;

    // ── Per-Component Configuration ────────────────────────────────────────
    // Entry framing yaw and exposure nudge per component type, set in the
    // BP_FurniturePreviewActor Details panel.

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config | Components",
              meta = (DisplayName = "Cabinet"))
    FPreviewComponentConfig CabinetConfig;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config | Components",
              meta = (DisplayName = "Closet"))
    FPreviewComponentConfig ClosetConfig;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config | Components",
              meta = (DisplayName = "Countertop"))
    FPreviewComponentConfig CountertopConfig;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config | Components",
              meta = (DisplayName = "Sink"))
    FPreviewComponentConfig SinkConfig;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config | Components",
              meta = (DisplayName = "Faucet"))
    FPreviewComponentConfig FaucetConfig;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config | Components",
              meta = (DisplayName = "Mirror"))
    FPreviewComponentConfig MirrorConfig;

    // ─────────────────────────────────────────────────────────────────────
    // PUBLIC API
    // ─────────────────────────────────────────────────────────────────────

    /** Applies a product snapshot locally. Rebuilds all mesh components. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control",
              meta = (DisplayName = "Load Product Preview"))
    void LoadProductPreview(const FFurnitureProductRow& ProductData,
                            const FShowroomBoothConfigState& ActiveState,
                            AShowroomBooth* SourceBooth);

    /**
     * Isolates and focuses on a single component group.
     * Handles:
     *   1. Visibility isolation (hides all other mesh groups).
     *   2. Pivot + bounds for the focused group; SpringArm placed at the pivot,
     *      entry view along the booth-relative forward axis.
     *   3. Studio camera recipe (per-component exposure nudge) + studio framing.
     */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control",
              meta = (DisplayName = "Set Focus Component"))
    void SetFocusComponent(EFurnitureComponentType ComponentType);

    /** Orbits the camera around the focused mesh. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control",
              meta = (DisplayName = "Rotate Preview"))
    void RotatePreview(float DeltaYaw, float DeltaPitch);

    /** Resets camera orbit to the initial facing angle and zoom. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control",
              meta = (DisplayName = "Reset Preview Rotation"))
    void ResetRotation();

    /** Adjusts the SpringArm TargetArmLength, clamped by the active component's config. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control",
              meta = (DisplayName = "Zoom Preview"))
    void ZoomPreview(float DeltaZoom);

    // ─────────────────────────────────────────────────────────────────────
    // STUDIO STAGE MODE — faithful port of the approved StudioViewerTest
    // presentation/interaction. Set by AAwsTutorial_PlayerController right
    // after spawn, BEFORE LoadProductPreview.
    //
    // In studio mode the preview renders on the neutral Studio Stage instead
    // of the real room, and the CAMERA orbits (per-channel eased, with fling
    // inertia, pan, multiplicative center-based zoom and an idle turntable) while
    // the mesh stays still. Room-dependent machinery is skipped: level-light
    // measurement, the channel-1 fill rig, stencil dim/custom depth, vignette,
    // clearance relocation and the zoom wall-clamp trace. Product assembly,
    // focus isolation, the public Rotate/Zoom/Reset API (PS + cinematic tour)
    // and AR export behave exactly as before.
    // ─────────────────────────────────────────────────────────────────────

    /** Enables studio mode and remembers the stage supplying lighting/backdrop/
        camera recipe. Also excludes preview meshes from reflection captures so
        the stage's environment capture never photographs the subject itself. */
    void SetStudioStageMode(AStudioStageActor* InStage);

    bool IsStudioStageMode() const { return bStudioStageMode; }

    // Raw drag/wheel input (pixel deltas, Y up-positive), called by the
    // ViewmodeOverlayWidget input layer. Desktop and Pixel Streaming mouse
    // input both arrive through that widget via Slate.
    void StudioOrbitDrag(float DeltaXPixels, float DeltaYPixelsUp);
    void StudioPanDrag(float DeltaXPixels, float DeltaYPixelsUp);
    void StudioZoom(float WheelNotches);
    void StudioSetOrbiting(bool bOrbiting);
    void StudioSetPanning(bool bPanning);

    /** Double-click smart focus: if the cursor points at (or near) the product,
        zoom in (center-based, like the wheel); if it points at empty
        background, glide back to the reset framing. */
    void StudioSmartFocusAtCursor();

    /** Touch pinch zoom: Ratio = current finger distance / previous finger
        distance (>1 = spread = zoom in). Distance-only — no cursor retargeting,
        the pinch midpoint pan is delivered separately via StudioPanDrag. */
    void StudioPinchZoom(float Ratio);

    /** True while the idle turntable is actually rotating the view (used by the
        overlay to show an optional auto-rotate indicator). */
    bool IsStudioAutoRotating() const { return bStudioTurntableActive; }

    /** Per-frame studio update: easing, inertia, idle turntable.
        Pumped every frame by the Studio Stage actor's Tick — a plain C++ ticker
        that is independent of the BP_FurniturePreviewActor subclass's settings. */
    void StudioTickUpdate(float DeltaSeconds);

    /** Combined bounds of the product mesh components only (ignores markers,
        empty slots and non-mesh components). Used to size the Studio Stage. */
    bool GetStudioProductBounds(FVector& OutOrigin, float& OutRadius) const;

    /** Combined world-space box of the currently VISIBLE product mesh
        components (after focus isolation, that is the focused group). Used to
        size the stage at build time. */
    bool GetStudioProductBox(FBox& OutBox) const;

    // ── Studio interaction tuning (values from the approved StudioViewerTest) ──

    /** Orbit speed in degrees per screen pixel of drag. */
    UPROPERTY(EditAnywhere, Category = "Preview Config | Studio", meta = (UIMin = "0.05", UIMax = "1.5"))
    float StudioOrbitDegPerPixel = 0.35f;

    /** Fraction of the current distance zoomed per wheel notch (multiplicative). */
    UPROPERTY(EditAnywhere, Category = "Preview Config | Studio", meta = (UIMin = "0.05", UIMax = "0.5"))
    float StudioZoomPerNotch = 0.25f;

    /** Pan speed as a fraction of the current distance per pixel. */
    UPROPERTY(EditAnywhere, Category = "Preview Config | Studio", meta = (UIMin = "0.0005", UIMax = "0.01"))
    float StudioPanPerPixel = 0.00175f;

    /** Orbit/pan easing (higher = snappier; 0 = instant). */
    UPROPERTY(EditAnywhere, Category = "Preview Config | Studio", meta = (UIMin = "0", UIMax = "30"))
    float StudioOrbitSmoothing = 14.f;

    /** Zoom easing, deliberately snappier than orbit. */
    UPROPERTY(EditAnywhere, Category = "Preview Config | Studio", meta = (UIMin = "0", UIMax = "40"))
    float StudioZoomSmoothing = 25.f;

    /** Carry orbit momentum after releasing the drag (fling). */
    UPROPERTY(EditAnywhere, Category = "Preview Config | Studio")
    bool bStudioOrbitInertia = true;

    /** How fast the fling dies out (per-second exponential decay). */
    UPROPERTY(EditAnywhere, Category = "Preview Config | Studio", meta = (UIMin = "0.5", UIMax = "12"))
    float StudioInertiaDamping = 4.f;

    /** Pan is clamped to this many focus bounding-sphere radii from center. */
    UPROPERTY(EditAnywhere, Category = "Preview Config | Studio", meta = (UIMin = "0.1", UIMax = "3"))
    float StudioPanRangeFactor = 1.f;

    /** Turntable auto-rotate after a few seconds without input; any interaction stops it. */
    UPROPERTY(EditAnywhere, Category = "Preview Config | Studio")
    bool bStudioAutoRotate = true;

    UPROPERTY(EditAnywhere, Category = "Preview Config | Studio", meta = (UIMin = "0.5", UIMax = "30"))
    float StudioAutoRotateDelay = 5.f;

    UPROPERTY(EditAnywhere, Category = "Preview Config | Studio", meta = (UIMin = "1", UIMax = "60"))
    float StudioAutoRotateSpeedDegPerSec = 10.f;

    /** Narrow product-viewer FOV (the web viewer uses ~30 deg). */
    UPROPERTY(EditAnywhere, Category = "Preview Config | Studio", meta = (UIMin = "20", UIMax = "90"))
    float StudioCameraFOV = 32.f;

    /** Framing padding at the entry distance (the web viewer opens at ~105%). */
    UPROPERTY(EditAnywhere, Category = "Preview Config | Studio", meta = (UIMin = "1.0", UIMax = "2.5"))
    float StudioFramingMargin = 1.1f;

private:

    // ── Focus/framing state ────────────────────────────────────────────────
    FVector  WIP_FocusPivotWorld  = FVector::ZeroVector;
    FRotator WIP_InitialOrbitRot  = FRotator::ZeroRotator;
    float    WIP_MeshBoundsRadius = 100.f;

    /** Active component's StudioDistanceScale (applied at entry and on reset). */
    float    StudioActiveDistanceScale = 1.f;

    // ── Scene restore ──────────────────────────────────────────────────────
    TWeakObjectPtr<ACharacter>      WIP_CachedCharacter;
    TWeakObjectPtr<AShowroomBooth>  WIP_CachedSourceBooth;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> CurrentFocusedComponent;

    // ── Studio mode state ─────────────────────────────────────────────────
    bool bStudioStageMode = false;
    TWeakObjectPtr<AStudioStageActor> StudioStage;

    // Orbit targets (input writes these; the camera eases toward them).
    float   StudioYaw = 0.f;
    float   StudioPitch = 0.f;
    float   StudioDist = 180.f;

    /** FIXED-PIVOT PAN: 2D camera offset in arm-local space (X = right, Y = up),
        applied via SpringArm->SocketOffset so it rotates with the orbit. The arm
        root — the orbit pivot — stays permanently at WIP_FocusPivotWorld, so
        orbiting after a pan still spins the product about its own center (the
        web-viewer behavior) instead of swinging it around a displaced pivot. */
    FVector2D StudioPanOffset = FVector2D::ZeroVector;

    // Smoothed values actually applied to the SpringArm (per-channel easing).
    float     StudioCurYaw = 0.f;
    float     StudioCurPitch = 0.f;
    float     StudioCurDist = 180.f;
    FVector2D StudioCurPanOffset = FVector2D::ZeroVector;

    // Fling state (deg/s), fed while dragging, consumed after release.
    float StudioYawVel = 0.f;
    float StudioPitchVel = 0.f;
    float StudioFrameYaw = 0.f;
    float StudioFramePitch = 0.f;

    float StudioIdleSeconds = 0.f;
    float StudioFitDistance = 180.f;
    float StudioMinDist = 30.f;
    float StudioMaxDist = 400.f;
    bool  bStudioOrbiting = false;
    bool  bStudioPanning = false;
    bool  bStudioTurntableActive = false;

    // NOTE: the pan indicator is no longer a spawned 3D actor — the
    // ViewmodeOverlayWidget shows its own Img_PivotMarker (pinned to the
    // viewport center) while RMB/MMB panning.

    float ComputeStudioFitDistance() const;
    void  ApplyStudioCameraTransform(bool bInstant);
    void  ClampStudioPan();
    void  SetupStudioFraming();
    void  StudioNotifyInteraction() { StudioIdleSeconds = 0.f; }

    /** Neutral MeshRoot state captured at focus time — read by AR export via
        GetMeshRootResetState (the mesh never moves in Studio ViewMode). */
    FVector WIP_MeshRootLocAtReset  = FVector::ZeroVector;
    FQuat   WIP_InitialMeshRootQuat = FQuat::Identity;

    // ── Private helpers ────────────────────────────────────────────────────
    FVector WIP_GetFocusPivotWorld() const;
    void    ConfigureMesh(UStaticMeshComponent* Comp) const;

    void ApplyComponentMeshAndMaterials(UStaticMeshComponent* Target,
                                        const FFurnitureComponentOptions& Options,
                                        int32 SizeIndex,
                                        int32 ColorIndex);

    void ApplyComponentMeshAndMaterials(UStaticMeshComponent* Target,
                                        const FFurnitureCabinetOptions& Options,
                                        int32 SizeIndex,
                                        int32 ColorIndex);

    void ApplyDoorMeshAndMaterials(UStaticMeshComponent* Target,
                                   const FFurnitureDoorGroup& DoorGroup,
                                   int32 SizeIndex,
                                   int32 ColorIndex,
                                   int32 SlotIndex);
};
