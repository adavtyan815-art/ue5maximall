// Copyright MaxiMall Project. All Rights Reserved.
// FurniturePreviewActor.cpp

#include "FurnitureConfigurator/Preview/FurniturePreviewActor.h"
#include "FurnitureConfigurator/Preview/StudioStageActor.h"
#include "FurnitureConfigurator/ShowroomBooth.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

AFurniturePreviewActor::AFurniturePreviewActor()
{
    // CRITICAL: Never replicate. Client-local only.
    bReplicates                   = false;
    bAlwaysRelevant               = false;
    // Fully event-driven; nothing per-frame here. The Studio Stage actor ticks
    // and pumps StudioTickUpdate for the studio camera orbit.
    PrimaryActorTick.bCanEverTick = false;

    // ── Root ──────────────────────────────────────────────────────────────
    PreviewRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PreviewRoot"));
    SetRootComponent(PreviewRoot);

    MeshRoot = CreateDefaultSubobject<USceneComponent>(TEXT("MeshRoot"));
    MeshRoot->SetupAttachment(PreviewRoot);

    // ── Furniture Meshes ──────────────────────────────────────────────────
    CabinetMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cabinet"));
    CabinetMesh->SetupAttachment(MeshRoot);

    DoorMeshSlot0 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorSlot0"));
    DoorMeshSlot0->SetupAttachment(CabinetMesh);

    DoorMeshSlot1 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorSlot1"));
    DoorMeshSlot1->SetupAttachment(CabinetMesh);

    CountertopMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Countertop"));
    CountertopMesh->SetupAttachment(CabinetMesh);

    SinkMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Sink"));
    SinkMesh->SetupAttachment(CabinetMesh);

    FaucetMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Faucet"));
    FaucetMesh->SetupAttachment(CabinetMesh);

    MirrorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mirror"));
    MirrorMesh->SetupAttachment(MeshRoot);

    ClosetMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Closet"));
    ClosetMesh->SetupAttachment(MeshRoot);

    ClosetDoorMeshSlot0 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ClosetDoorSlot0"));
    ClosetDoorMeshSlot0->SetupAttachment(ClosetMesh);

    ClosetDoorMeshSlot1 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ClosetDoorSlot1"));
    ClosetDoorMeshSlot1->SetupAttachment(ClosetMesh);

    // Configure every mesh: movable, no collision, lighting channel 0 only.
    ConfigureMesh(CabinetMesh.Get());
    ConfigureMesh(DoorMeshSlot0.Get());
    ConfigureMesh(DoorMeshSlot1.Get());
    ConfigureMesh(CountertopMesh.Get());
    ConfigureMesh(SinkMesh.Get());
    ConfigureMesh(FaucetMesh.Get());
    ConfigureMesh(MirrorMesh.Get());
    ConfigureMesh(ClosetMesh.Get());
    ConfigureMesh(ClosetDoorMeshSlot0.Get());
    ConfigureMesh(ClosetDoorMeshSlot1.Get());

    // ── Spring Arm & Camera ───────────────────────────────────────────────
    SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
    SpringArm->SetupAttachment(PreviewRoot);
    SpringArm->TargetArmLength       = 180.f; // studio framing overrides on focus
    SpringArm->bDoCollisionTest      = false;
    SpringArm->bUsePawnControlRotation = false;

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
    Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness = false;
    Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = false;

}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void AFurniturePreviewActor::BeginPlay()
{
    Super::BeginPlay();
}

// ─────────────────────────────────────────────────────────────────────────────
// STUDIO STAGE MODE — camera-orbit interaction ported from StudioViewerTest
// ─────────────────────────────────────────────────────────────────────────────

void AFurniturePreviewActor::SetStudioStageMode(AStudioStageActor* InStage)
{
    bStudioStageMode = IsValid(InStage);
    StudioStage = InStage;

    if (!bStudioStageMode)
    {
        return;
    }

    // The stage ticks and pumps our per-frame studio update.
    InStage->SetDrivenPreview(this);

    // The stage's environment capture is taken from inside the product bounds;
    // the subject must not photograph itself into its own environment.
    UStaticMeshComponent* AllMeshes[] =
    {
        CabinetMesh.Get(), DoorMeshSlot0.Get(), DoorMeshSlot1.Get(),
        CountertopMesh.Get(), SinkMesh.Get(), FaucetMesh.Get(), MirrorMesh.Get(),
        ClosetMesh.Get(), ClosetDoorMeshSlot0.Get(), ClosetDoorMeshSlot1.Get()
    };
    for (UStaticMeshComponent* Comp : AllMeshes)
    {
        if (IsValid(Comp))
        {
            Comp->bVisibleInReflectionCaptures = false;
            Comp->MarkRenderStateDirty();
        }
    }

    // NOTE: no 3D pivot marker is spawned anymore — the ViewmodeOverlayWidget
    // shows its Img_PivotMarker UI image (screen center) while panning.
}

bool AFurniturePreviewActor::GetStudioProductBox(FBox& OutBox) const
{
    // Product mesh components only — GetActorBounds would also count markers and
    // any other registered primitive, inflating the stage size. Visible-only:
    // after focus isolation this yields the FOCUSED group's box (at stage-build
    // time everything loaded is visible = the whole product).
    const UStaticMeshComponent* AllMeshes[] =
    {
        CabinetMesh.Get(), DoorMeshSlot0.Get(), DoorMeshSlot1.Get(),
        CountertopMesh.Get(), SinkMesh.Get(), FaucetMesh.Get(), MirrorMesh.Get(),
        ClosetMesh.Get(), ClosetDoorMeshSlot0.Get(), ClosetDoorMeshSlot1.Get()
    };

    FBox Combined(ForceInit);
    for (const UStaticMeshComponent* Comp : AllMeshes)
    {
        if (IsValid(Comp) && IsValid(Comp->GetStaticMesh()) && Comp->IsVisible())
        {
            Combined += Comp->Bounds.GetBox();
        }
    }

    OutBox = Combined;
    return Combined.IsValid != 0;
}

bool AFurniturePreviewActor::GetStudioProductBounds(FVector& OutOrigin, float& OutRadius) const
{
    FBox Combined(ForceInit);
    if (!GetStudioProductBox(Combined))
    {
        OutOrigin = GetActorLocation();
        OutRadius = 100.f;
        return false;
    }

    OutOrigin = Combined.GetCenter();
    OutRadius = FMath::Max(static_cast<float>(Combined.GetExtent().Size()), 25.f);
    return true;
}

float AFurniturePreviewActor::ComputeStudioFitDistance() const
{
    // Box-based, aspect-aware fit (ported): the focused mesh's worst-case
    // horizontal footprint (any yaw) and its height must both fit the frustum.
    FVector BoxExtent(100.f);
    if (IsValid(CurrentFocusedComponent) && CurrentFocusedComponent->GetStaticMesh())
    {
        BoxExtent = CurrentFocusedComponent->Bounds.BoxExtent;
    }
    else
    {
        FVector Origin, Extent;
        GetActorBounds(/*bOnlyCollidingComponents=*/false, Origin, Extent, /*bIncludeFromChildActors=*/false);
        BoxExtent = Extent;
    }

    const float HalfHFOVRad = FMath::DegreesToRadians(FMath::Clamp(StudioCameraFOV, 5.f, 170.f) * 0.5f);

    float Aspect = 16.f / 9.f;
    // THIS world's viewport — in multi-client PIE, GEngine->GameViewport would
    // be a different instance's window.
    UGameViewportClient* ViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    if (ViewportClient && ViewportClient->Viewport)
    {
        const FIntPoint Size = ViewportClient->Viewport->GetSizeXY();
        if (Size.X > 0 && Size.Y > 0)
        {
            Aspect = static_cast<float>(Size.X) / static_cast<float>(Size.Y);
        }
    }
    // UE's FieldOfView is horizontal.
    const float HalfVFOVRad = FMath::Atan(FMath::Tan(HalfHFOVRad) / FMath::Max(Aspect, 0.1f));

    const float ExtX = static_cast<float>(BoxExtent.X);
    const float ExtY = static_cast<float>(BoxExtent.Y);
    const float HorizRadius = FMath::Max(FMath::Sqrt(ExtX * ExtX + ExtY * ExtY), 1.f);
    const float VertRadius  = FMath::Max(static_cast<float>(BoxExtent.Z), 1.f);

    const float DistH = HorizRadius / FMath::Max(FMath::Tan(HalfHFOVRad), 0.01f);
    const float DistV = VertRadius  / FMath::Max(FMath::Tan(HalfVFOVRad), 0.01f);

    return (FMath::Max(DistH, DistV) + 0.5f * HorizRadius) * FMath::Max(StudioFramingMargin, 1.f);
}

void AFurniturePreviewActor::SetupStudioFraming()
{
    StudioFitDistance = ComputeStudioFitDistance();
    // Zoom range: multiples of the auto-fit distance (0.35x .. 3.0x).
    StudioMinDist = StudioFitDistance * 0.35f;
    StudioMaxDist = StudioFitDistance * 3.0f;

    StudioYaw   = WIP_InitialOrbitRot.Yaw;   // booth-relative entry view + per-component offset
    StudioPitch = WIP_InitialOrbitRot.Pitch; // classic three-quarter tilt
    // Entry distance: auto fit scaled by the component's StudioDistanceScale,
    // clamped into the (auto-fit-based) zoom range.
    StudioDist  = FMath::Clamp(StudioFitDistance * FMath::Clamp(StudioActiveDistanceScale, 0.25f, 4.f),
                               StudioMinDist, StudioMaxDist);
    StudioPanOffset = FVector2D::ZeroVector;
    StudioCurPanOffset = FVector2D::ZeroVector;

    StudioYawVel = StudioPitchVel = StudioFrameYaw = StudioFramePitch = 0.f;
    StudioIdleSeconds = 0.f;

    // The studio camera starts EXACTLY on the arm boresight looking at the
    // pivot, like the test's bare camera. Legacy/BP compositions may configure
    // arm socket/target offsets or a camera-relative offset — a constant
    // sideways offset is invisible on a cabinet at 4 m but pushes a faucet at
    // 60 cm completely off-screen. Zero them all in studio mode (the preview
    // actor is destroyed on close, legacy sessions are unaffected). From here
    // on, SocketOffset is owned by ApplyStudioCameraTransform: it carries the
    // fixed-pivot pan (arm-local right/up), zero until the user pans.
    if (IsValid(SpringArm))
    {
        SpringArm->SocketOffset = FVector::ZeroVector;
        SpringArm->TargetOffset = FVector::ZeroVector;
        SpringArm->bEnableCameraLag = false;
        SpringArm->bEnableCameraRotationLag = false;
        SpringArm->bDoCollisionTest = false;
        SpringArm->bUsePawnControlRotation = false;
    }
    if (IsValid(Camera))
    {
        Camera->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
        Camera->SetFieldOfView(StudioCameraFOV);
    }

    ApplyStudioCameraTransform(/*bInstant=*/true);
    if (IsValid(SpringArm))
    {
        SpringArm->UpdateChildTransforms(); // camera lands on the boresight this frame
    }
}

void AFurniturePreviewActor::ApplyStudioCameraTransform(bool bInstant)
{
    if (!IsValid(SpringArm))
    {
        return;
    }

    if (bInstant || StudioOrbitSmoothing <= 0.f)
    {
        StudioCurYaw = StudioYaw;
        StudioCurPitch = StudioPitch;
        StudioCurDist = StudioDist;
        StudioCurPanOffset = StudioPanOffset;
    }
    else
    {
        const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
        // Orbit/pan share one easing; zoom gets its own, snappier one.
        StudioCurYaw   = FMath::FInterpTo(StudioCurYaw, StudioYaw, Dt, StudioOrbitSmoothing);
        StudioCurPitch = FMath::FInterpTo(StudioCurPitch, StudioPitch, Dt, StudioOrbitSmoothing);
        StudioCurPanOffset = FMath::Vector2DInterpTo(StudioCurPanOffset, StudioPanOffset, Dt, StudioOrbitSmoothing);
        StudioCurDist  = FMath::FInterpTo(StudioCurDist, StudioDist, Dt,
            StudioZoomSmoothing > 0.f ? StudioZoomSmoothing : StudioOrbitSmoothing);
    }

    // FIXED-PIVOT PAN: the arm root (orbit pivot) never leaves the product
    // center; pan rides in SocketOffset (arm-local: Y = right, Z = up), which
    // rotates with the orbit, so the panned framing carries around the product
    // while rotation still spins it about its own center.
    SpringArm->SetWorldLocation(WIP_FocusPivotWorld);
    SpringArm->SetWorldRotation(FRotator(StudioCurPitch, StudioCurYaw, 0.f));
    SpringArm->TargetArmLength = StudioCurDist;
    SpringArm->SocketOffset = FVector(0.f, StudioCurPanOffset.X, StudioCurPanOffset.Y);
}

void AFurniturePreviewActor::ClampStudioPan()
{
    const float MaxSize = WIP_MeshBoundsRadius * FMath::Max(StudioPanRangeFactor, 0.f);
    const float Size = static_cast<float>(StudioPanOffset.Size());
    if (Size > MaxSize && Size > KINDA_SMALL_NUMBER)
    {
        StudioPanOffset *= MaxSize / Size;
    }
}

void AFurniturePreviewActor::StudioTickUpdate(float DeltaSeconds)
{
    if (!bStudioStageMode || DeltaSeconds <= 0.f)
    {
        return;
    }

    // ── Fling: estimate angular velocity while dragging, coast after release ──
    if (bStudioOrbiting)
    {
        if (DeltaSeconds > KINDA_SMALL_NUMBER)
        {
            // Smoothed over the last frames so a pause before release kills the fling.
            const float Alpha = FMath::Clamp(DeltaSeconds * 20.f, 0.f, 1.f);
            StudioYawVel   = FMath::Lerp(StudioYawVel, StudioFrameYaw / DeltaSeconds, Alpha);
            StudioPitchVel = FMath::Lerp(StudioPitchVel, StudioFramePitch / DeltaSeconds, Alpha);
        }
    }
    else if (bStudioOrbitInertia && (FMath::Abs(StudioYawVel) > 0.5f || FMath::Abs(StudioPitchVel) > 0.5f))
    {
        StudioYaw += StudioYawVel * DeltaSeconds;
        StudioPitch = FMath::Clamp(StudioPitch + StudioPitchVel * DeltaSeconds, -85.f, 85.f);
        if (FMath::Abs(StudioPitch) >= 85.f)
        {
            StudioPitchVel = 0.f;
        }
        const float Decay = FMath::Exp(-StudioInertiaDamping * DeltaSeconds);
        StudioYawVel *= Decay;
        StudioPitchVel *= Decay;
        StudioIdleSeconds = 0.f; // a live fling holds off the turntable
    }
    else
    {
        StudioYawVel = 0.f;
        StudioPitchVel = 0.f;
    }
    StudioFrameYaw = 0.f;
    StudioFramePitch = 0.f;

    // ── Idle turntable: starts after StudioAutoRotateDelay, any input stops it ──
    StudioIdleSeconds += DeltaSeconds;
    bStudioTurntableActive = bStudioAutoRotate && !bStudioOrbiting && !bStudioPanning
        && StudioIdleSeconds > StudioAutoRotateDelay;
    if (bStudioTurntableActive)
    {
        StudioYaw += StudioAutoRotateSpeedDegPerSec * DeltaSeconds;
    }

    ApplyStudioCameraTransform(/*bInstant=*/false);
}

void AFurniturePreviewActor::StudioOrbitDrag(float DeltaXPixels, float DeltaYPixelsUp)
{
    if (!bStudioStageMode)
    {
        return;
    }
    const float DYaw = DeltaXPixels * StudioOrbitDegPerPixel;
    const float DPitch = DeltaYPixelsUp * StudioOrbitDegPerPixel;
    StudioYaw += DYaw;
    StudioPitch = FMath::Clamp(StudioPitch + DPitch, -85.f, 85.f);
    StudioFrameYaw += DYaw;
    StudioFramePitch += DPitch;
    StudioNotifyInteraction();
}

void AFurniturePreviewActor::StudioPanDrag(float DeltaXPixels, float DeltaYPixelsUp)
{
    if (!bStudioStageMode)
    {
        return;
    }
    // Fixed pivot: pan moves the CAMERA laterally (arm-local right/up), the
    // orbit pivot stays on the product center. Dragging right moves the camera
    // left, so the product follows the mouse on screen — same feel as before.
    const float Scale = StudioPanPerPixel * FMath::Max(StudioCurDist, 1.f);
    StudioPanOffset.X -= DeltaXPixels * Scale;
    StudioPanOffset.Y -= DeltaYPixelsUp * Scale;
    ClampStudioPan();
    StudioNotifyInteraction();
}

void AFurniturePreviewActor::StudioZoom(float WheelNotches)
{
    if (!bStudioStageMode || FMath::IsNearlyZero(WheelNotches))
    {
        return;
    }
    StudioNotifyInteraction();

    // CENTER-BASED zoom: multiplicative distance change straight along the
    // boresight toward the fixed pivot (the focused component's center). The
    // cursor position never influences zoom — no drift of the model toward the
    // mouse; the pan offset is untouched, so the framing stays exactly put.
    StudioDist = FMath::Clamp(StudioDist * FMath::Exp(-WheelNotches * StudioZoomPerNotch),
                              StudioMinDist, StudioMaxDist);
}

void AFurniturePreviewActor::StudioSmartFocusAtCursor()
{
    if (!bStudioStageMode || !IsValid(Camera))
    {
        return;
    }
    StudioNotifyInteraction();

    // Where does the cursor ray meet the focal plane through the pivot?
    APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    FVector RayOrigin, RayDir;
    if (!PC || !PC->DeprojectMousePositionToWorld(RayOrigin, RayDir))
    {
        return;
    }
    const FVector CamFwd = Camera->GetForwardVector();
    const float Denom = static_cast<float>(FVector::DotProduct(RayDir, CamFwd));
    if (Denom <= KINDA_SMALL_NUMBER)
    {
        return;
    }
    const float T = static_cast<float>(FVector::DotProduct(WIP_FocusPivotWorld - RayOrigin, CamFwd)) / Denom;
    const FVector CursorPoint = RayOrigin + RayDir * T;

    // On (or near) the product: two wheel notches of eased, center-based zoom.
    // On empty background: glide back to the entry framing.
    const float LateralDist = static_cast<float>(FVector::Dist(CursorPoint, WIP_FocusPivotWorld));
    if (LateralDist <= WIP_MeshBoundsRadius * 1.15f)
    {
        StudioZoom(2.f);
    }
    else
    {
        ResetRotation();
    }
}

void AFurniturePreviewActor::StudioPinchZoom(float Ratio)
{
    if (!bStudioStageMode || Ratio <= KINDA_SMALL_NUMBER)
    {
        return;
    }
    // Spreading fingers (Ratio > 1) zooms in: distance divides by the ratio,
    // clamped to the same limits as wheel zoom. No cursor retargeting — the
    // accompanying two-finger midpoint pan handles framing.
    StudioDist = FMath::Clamp(StudioDist / Ratio, StudioMinDist, StudioMaxDist);
    StudioNotifyInteraction();
}

void AFurniturePreviewActor::StudioSetOrbiting(bool bOrbiting)
{
    if (!bStudioStageMode)
    {
        return;
    }
    bStudioOrbiting = bOrbiting;
    if (bOrbiting)
    {
        StudioYawVel = 0.f;   // grabbing the model stops any running fling
        StudioPitchVel = 0.f;
    }
    StudioNotifyInteraction();
}

void AFurniturePreviewActor::StudioSetPanning(bool bPanning)
{
    if (!bStudioStageMode)
    {
        return;
    }
    bStudioPanning = bPanning;
    StudioNotifyInteraction();
}

void AFurniturePreviewActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // ── Restore hidden character mesh ─────────────────────────────────────
    if (WIP_CachedCharacter.IsValid())
    {
        if (USkeletalMeshComponent* Mesh = WIP_CachedCharacter->GetMesh())
        {
            Mesh->SetVisibility(true);
        }
        WIP_CachedCharacter.Reset();
    }

    // ── Restore source showroom booth actor in level ───────────────────────
    if (WIP_CachedSourceBooth.IsValid())
    {
        WIP_CachedSourceBooth->SetActorHiddenInGame(false);
        WIP_CachedSourceBooth.Reset();
    }

    // NOTE: no other world state to restore. The level's lights, SkyLights and
    // PostProcessVolumes are never modified, so entering/leaving ViewMode
    // cannot alter how the level looks.

    Super::EndPlay(EndPlayReason);
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadProductPreview
// ─────────────────────────────────────────────────────────────────────────────

void AFurniturePreviewActor::LoadProductPreview(const FFurnitureProductRow& ProductData,
                                                const FShowroomBoothConfigState& ActiveState,
                                                AShowroomBooth* SourceBooth)
{
    // ── Take the real booth's place ────────────────────────────────────────
    // The preview duplicates the booth at its exact world transform, so the real
    // booth is hidden to avoid z-fighting (restored in EndPlay). This is the only
    // world actor the preview hides: the level's lights, SkyLights and
    // PostProcessVolumes are deliberately left untouched so Lumen GI, reflections
    // and exposure on the subject come from the ACTUAL room.
    if (IsValid(SourceBooth))
    {
        WIP_CachedSourceBooth = SourceBooth;
        SourceBooth->SetActorHiddenInGame(true);
    }

    if (IsValid(MeshRoot))
    {
        MeshRoot->SetRelativeTransform(FTransform::Identity);
    }

    // ── Copy SourceBooth component relative transforms ─────────────────────
    if (IsValid(SourceBooth))
    {
        auto CopyTransform = [](UStaticMeshComponent* Dst, UStaticMeshComponent* Src)
        {
            if (IsValid(Dst) && IsValid(Src))
            {
                Dst->SetRelativeTransform(Src->GetRelativeTransform());
            }
        };
        CopyTransform(CabinetMesh.Get(),         SourceBooth->MainCabinet.Get());
        CopyTransform(ClosetMesh.Get(),           SourceBooth->ClosetMesh.Get());
        CopyTransform(DoorMeshSlot0.Get(),        SourceBooth->DoorMeshSlot0.Get());
        CopyTransform(DoorMeshSlot1.Get(),        SourceBooth->DoorMeshSlot1.Get());
        CopyTransform(ClosetDoorMeshSlot0.Get(),  SourceBooth->ClosetDoorMeshSlot0.Get());
        CopyTransform(ClosetDoorMeshSlot1.Get(),  SourceBooth->ClosetDoorMeshSlot1.Get());
        CopyTransform(CountertopMesh.Get(),       SourceBooth->CountertopMesh.Get());
        CopyTransform(SinkMesh.Get(),             SourceBooth->SinkMesh.Get());
        CopyTransform(FaucetMesh.Get(),           SourceBooth->FaucetMesh.Get());
        CopyTransform(MirrorMesh.Get(),           SourceBooth->MirrorMesh.Get());
    }

    // ── Cabinet ───────────────────────────────────────────────────────────
    if (!IsValid(SourceBooth) || (IsValid(SourceBooth->MainCabinet) && SourceBooth->MainCabinet->GetStaticMesh()))
    {
        ApplyComponentMeshAndMaterials(CabinetMesh.Get(), ProductData.CabinetOptions,
                                       ActiveState.ActiveSizeIndex, ActiveState.ActiveColorIndex);
    }
    else if (IsValid(CabinetMesh)) { CabinetMesh->SetStaticMesh(nullptr); CabinetMesh->SetVisibility(false); }

    // ── Closet ────────────────────────────────────────────────────────────
    if (!IsValid(SourceBooth) || (IsValid(SourceBooth->ClosetMesh) && SourceBooth->ClosetMesh->GetStaticMesh()))
    {
        ApplyComponentMeshAndMaterials(ClosetMesh.Get(), ProductData.ClosetOptions,
                                       ActiveState.ClosetSizeIndex, ActiveState.ClosetColorIndex);
    }
    else if (IsValid(ClosetMesh)) { ClosetMesh->SetStaticMesh(nullptr); ClosetMesh->SetVisibility(false); }

    // ── Cabinet Doors ─────────────────────────────────────────────────────
    ApplyDoorMeshAndMaterials(DoorMeshSlot0.Get(), ProductData.DoorsConfig.CabinetDoors,
                              ActiveState.ActiveSizeIndex, ActiveState.ActiveColorIndex, 0);
    ApplyDoorMeshAndMaterials(DoorMeshSlot1.Get(), ProductData.DoorsConfig.CabinetDoors,
                              ActiveState.ActiveSizeIndex, ActiveState.ActiveColorIndex, 1);

    // ── Closet Doors ──────────────────────────────────────────────────────
    ApplyDoorMeshAndMaterials(ClosetDoorMeshSlot0.Get(), ProductData.DoorsConfig.ClosetDoors,
                              ActiveState.ClosetSizeIndex, ActiveState.ClosetColorIndex, 0);
    ApplyDoorMeshAndMaterials(ClosetDoorMeshSlot1.Get(), ProductData.DoorsConfig.ClosetDoors,
                              ActiveState.ClosetSizeIndex, ActiveState.ClosetColorIndex, 1);

    // ── Countertop ────────────────────────────────────────────────────────
    if (!IsValid(SourceBooth) || (IsValid(SourceBooth->CountertopMesh) && SourceBooth->CountertopMesh->GetStaticMesh()))
    {
        FFurnitureComponentOptions ResolvedCountertop;
        if (IsValid(SourceBooth)) { SourceBooth->GetResolvedComponentOptions(EFurnitureComponentType::Countertop, ResolvedCountertop); }
        ApplyComponentMeshAndMaterials(CountertopMesh.Get(), ResolvedCountertop,
                                       ActiveState.CountertopSizeIndex, ActiveState.ActiveCountertopColorIndex);

        FFurniturePlacementOffset CO = IsValid(SourceBooth) ? SourceBooth->GetActiveCountertopOffset() : FFurniturePlacementOffset();
        if (IsValid(SourceBooth) && SourceBooth->GetActiveCountertopType() == ECountertopType::BuiltIn)
        {
            const FTransform Baseline = SourceBooth->GetBaselineCountertopTransform();
            if (IsValid(CountertopMesh))
            {
                CountertopMesh->SetRelativeLocationAndRotation(
                    FVector(0.f, 0.f, Baseline.GetLocation().Z) + CO.RelativeLocation, CO.RelativeRotation);
                CountertopMesh->SetRelativeScale3D(CO.RelativeScale * Baseline.GetScale3D());
            }
        }
        else if (IsValid(CountertopMesh))
        {
            FTransform Delta;
            Delta.SetLocation(CO.RelativeLocation); Delta.SetRotation(CO.RelativeRotation.Quaternion()); Delta.SetScale3D(CO.RelativeScale);
            CountertopMesh->SetRelativeTransform(Delta * (IsValid(SourceBooth) ? SourceBooth->GetBaselineCountertopTransform() : FTransform::Identity));
        }
    }
    else if (IsValid(CountertopMesh)) { CountertopMesh->SetStaticMesh(nullptr); CountertopMesh->SetVisibility(false); }

    // ── Sink ──────────────────────────────────────────────────────────────
    const ECountertopType ActiveCTType = IsValid(SourceBooth) ? SourceBooth->GetActiveCountertopType() : ECountertopType::SurfaceMounted;
    if (ActiveCTType == ECountertopType::SurfaceMounted &&
        (!IsValid(SourceBooth) || (IsValid(SourceBooth->SinkMesh) && SourceBooth->SinkMesh->GetStaticMesh())))
    {
        FFurnitureComponentOptions ResolvedSink;
        if (IsValid(SourceBooth)) { SourceBooth->GetResolvedComponentOptions(EFurnitureComponentType::Sink, ResolvedSink); }
        ApplyComponentMeshAndMaterials(SinkMesh.Get(), ResolvedSink, ActiveState.SinkSizeIndex, ActiveState.SinkColorIndex);

        FFurniturePlacementOffset SO = IsValid(SourceBooth) ? SourceBooth->GetActiveSinkOffset() : FFurniturePlacementOffset();
        FTransform Delta; Delta.SetLocation(SO.RelativeLocation); Delta.SetRotation(SO.RelativeRotation.Quaternion()); Delta.SetScale3D(SO.RelativeScale);
        if (IsValid(SinkMesh))
        {
            SinkMesh->SetRelativeTransform(Delta * (IsValid(SourceBooth) ? SourceBooth->GetBaselineSinkTransform() : FTransform::Identity));
        }
    }
    else if (IsValid(SinkMesh)) { SinkMesh->SetStaticMesh(nullptr); SinkMesh->SetVisibility(false); }

    // ── Faucet ────────────────────────────────────────────────────────────
    if (!IsValid(SourceBooth) || (IsValid(SourceBooth->FaucetMesh) && SourceBooth->FaucetMesh->GetStaticMesh()))
    {
        FFurnitureComponentOptions ResolvedFaucet;
        if (IsValid(SourceBooth)) { SourceBooth->GetResolvedComponentOptions(EFurnitureComponentType::Faucet, ResolvedFaucet); }
        ApplyComponentMeshAndMaterials(FaucetMesh.Get(), ResolvedFaucet, ActiveState.FaucetSizeIndex, ActiveState.FaucetColorIndex);

        FFurniturePlacementOffset FO = IsValid(SourceBooth) ? SourceBooth->GetActiveFaucetOffset() : FFurniturePlacementOffset();
        if (IsValid(SourceBooth) && SourceBooth->GetActiveCountertopType() == ECountertopType::BuiltIn)
        {
            const FTransform Baseline = SourceBooth->GetBaselineFaucetTransform();
            if (IsValid(FaucetMesh))
            {
                FaucetMesh->SetRelativeLocationAndRotation(
                    FVector(0.f, 0.f, Baseline.GetLocation().Z) + FO.RelativeLocation, FO.RelativeRotation);
                FaucetMesh->SetRelativeScale3D(FO.RelativeScale * Baseline.GetScale3D());
            }
        }
        else if (IsValid(FaucetMesh))
        {
            FTransform Delta; Delta.SetLocation(FO.RelativeLocation); Delta.SetRotation(FO.RelativeRotation.Quaternion()); Delta.SetScale3D(FO.RelativeScale);
            FaucetMesh->SetRelativeTransform(Delta * (IsValid(SourceBooth) ? SourceBooth->GetBaselineFaucetTransform() : FTransform::Identity));
        }
    }
    else if (IsValid(FaucetMesh)) { FaucetMesh->SetStaticMesh(nullptr); FaucetMesh->SetVisibility(false); }

    // ── Mirror ────────────────────────────────────────────────────────────
    if (!IsValid(SourceBooth) || (IsValid(SourceBooth->MirrorMesh) && SourceBooth->MirrorMesh->GetStaticMesh()))
    {
        FFurnitureComponentOptions ResolvedMirror;
        if (IsValid(SourceBooth)) { SourceBooth->GetResolvedComponentOptions(EFurnitureComponentType::Mirror, ResolvedMirror); }
        ApplyComponentMeshAndMaterials(MirrorMesh.Get(), ResolvedMirror, ActiveState.MirrorSizeIndex, ActiveState.MirrorColorIndex);

        FFurniturePlacementOffset MO = IsValid(SourceBooth) ? SourceBooth->GetActiveMirrorOffset() : FFurniturePlacementOffset();
        FTransform Delta; Delta.SetLocation(MO.RelativeLocation); Delta.SetRotation(MO.RelativeRotation.Quaternion()); Delta.SetScale3D(MO.RelativeScale);
        if (IsValid(MirrorMesh))
        {
            MirrorMesh->SetRelativeTransform(Delta * (IsValid(SourceBooth) ? SourceBooth->GetBaselineMirrorTransform() : FTransform::Identity));
        }
    }
    else if (IsValid(MirrorMesh)) { MirrorMesh->SetStaticMesh(nullptr); MirrorMesh->SetVisibility(false); }

    // ── 5. Override Materials from SourceBooth ────────────────────────────
    // This ensures any custom MIDs (like custom colors from the catalog) are copied into the preview.
    if (IsValid(SourceBooth))
    {
        auto CopyMaterials = [](UStaticMeshComponent* Dst, UStaticMeshComponent* Src)
        {
            if (IsValid(Dst) && IsValid(Src))
            {
                const int32 NumMats = Src->GetNumMaterials();
                for (int32 MatIdx = 0; MatIdx < NumMats; ++MatIdx)
                {
                    if (UMaterialInterface* Mat = Src->GetMaterial(MatIdx))
                    {
                        Dst->SetMaterial(MatIdx, Mat);
                    }
                }
            }
        };

        CopyMaterials(CabinetMesh.Get(), SourceBooth->MainCabinet.Get());
        CopyMaterials(ClosetMesh.Get(), SourceBooth->ClosetMesh.Get());
        CopyMaterials(DoorMeshSlot0.Get(), SourceBooth->DoorMeshSlot0.Get());
        CopyMaterials(DoorMeshSlot1.Get(), SourceBooth->DoorMeshSlot1.Get());
        CopyMaterials(ClosetDoorMeshSlot0.Get(), SourceBooth->ClosetDoorMeshSlot0.Get());
        CopyMaterials(ClosetDoorMeshSlot1.Get(), SourceBooth->ClosetDoorMeshSlot1.Get());
        CopyMaterials(CountertopMesh.Get(), SourceBooth->CountertopMesh.Get());
        CopyMaterials(SinkMesh.Get(), SourceBooth->SinkMesh.Get());
        CopyMaterials(FaucetMesh.Get(), SourceBooth->FaucetMesh.Get());
        CopyMaterials(MirrorMesh.Get(), SourceBooth->MirrorMesh.Get());
    }

    // ── 6. ViewMode-only mirror glass override (MirrorMesh only) ──────────
    // Runs AFTER the booth material copy so it wins over catalog/custom-color
    // materials, and re-applies automatically on every product reload.
    ApplyStudioMirrorGlass();
}

void AFurniturePreviewActor::ApplyStudioMirrorGlass()
{
    // Studio ViewMode only; the booth in the configurator flow is never touched
    // (this actor exists only between Btn_Viewmode and Btn_Back).
    if (!bStudioStageMode || !IsValid(StudioMirrorGlassMaterial) ||
        !IsValid(MirrorMesh) || !IsValid(MirrorMesh->GetStaticMesh()))
    {
        return;
    }

    const int32 NumSlots = MirrorMesh->GetNumMaterials();
    if (NumSlots <= 0)
    {
        return;
    }

    // A single-slot mirror mesh IS its glass (frameless mirrors), regardless of
    // how its material happens to be named.
    if (NumSlots == 1)
    {
        MirrorMesh->SetMaterial(0, StudioMirrorGlassMaterial);
        return;
    }

    auto NameMatchesKeyword = [this](const FString& Name) -> bool
    {
        for (const FString& Keyword : StudioMirrorGlassKeywords)
        {
            if (!Keyword.IsEmpty() && Name.Contains(Keyword)) // case-insensitive
            {
                return true;
            }
        }
        return false;
    };

    const TArray<FStaticMaterial>& StaticMats = MirrorMesh->GetStaticMesh()->GetStaticMaterials();
    bool bSwappedAny = false;
    for (int32 Slot = 0; Slot < NumSlots; ++Slot)
    {
        // 1. The mesh's authored material slot name.
        bool bIsGlass = StaticMats.IsValidIndex(Slot) &&
                        NameMatchesKeyword(StaticMats[Slot].MaterialSlotName.ToString());

        // 2. The assigned material's name, walking up the instance parent chain
        //    (a runtime custom-color MID matches through its 'zerkalo' parent).
        for (UMaterialInterface* Mat = MirrorMesh->GetMaterial(Slot); !bIsGlass && Mat; )
        {
            if (NameMatchesKeyword(Mat->GetName()))
            {
                bIsGlass = true;
                break;
            }
            UMaterialInstance* AsInstance = Cast<UMaterialInstance>(Mat);
            Mat = AsInstance ? AsInstance->Parent.Get() : nullptr;
        }

        if (bIsGlass)
        {
            MirrorMesh->SetMaterial(Slot, StudioMirrorGlassMaterial);
            bSwappedAny = true;
        }
    }

    if (!bSwappedAny)
    {
        // Safe no-op: never guess on a multi-slot mesh. The log lists the names
        // so a new product only needs a keyword added in BP_FurniturePreviewActor.
        FString SlotDump;
        for (int32 Slot = 0; Slot < NumSlots; ++Slot)
        {
            SlotDump += FString::Printf(TEXT("[%d] slot='%s' mat='%s' "),
                Slot,
                StaticMats.IsValidIndex(Slot) ? *StaticMats[Slot].MaterialSlotName.ToString() : TEXT("?"),
                *GetNameSafe(MirrorMesh->GetMaterial(Slot)));
        }
        UE_LOG(LogTemp, Warning,
            TEXT("[PreviewActor] Studio mirror glass: no slot matched the keywords on '%s' — nothing swapped. Slots: %s"),
            *GetNameSafe(MirrorMesh->GetStaticMesh()), *SlotDump);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SetFocusComponent
//
// Unified entry point for:
//   1. Component isolation  — hide all groups, show only the focused one.
//   2. Pivot + bounds       — the fixed studio orbit pivot at the group's center.
//   3. Entry view           — booth-relative yaw (+ per-component offset), pitch.
//   4. Camera recipe/framing — stage recipe with the component's exposure nudge,
//                              then the box-aware studio fit.
// ─────────────────────────────────────────────────────────────────────────────

void AFurniturePreviewActor::SetFocusComponent(EFurnitureComponentType TargetType)
{
    // ── 0. Resolve per-component config ───────────────────────────────────
    const FPreviewComponentConfig* Config = nullptr;
    switch (TargetType)
    {
    case EFurnitureComponentType::Cabinet:    Config = &CabinetConfig;    break;
    case EFurnitureComponentType::Closet:     Config = &ClosetConfig;     break;
    case EFurnitureComponentType::Countertop: Config = &CountertopConfig; break;
    case EFurnitureComponentType::Sink:       Config = &SinkConfig;       break;
    case EFurnitureComponentType::Faucet:     Config = &FaucetConfig;     break;
    case EFurnitureComponentType::Mirror:     Config = &MirrorConfig;     break;
    default: break;
    }

    // ── 1. Component isolation ────────────────────────────────────────────
    auto ApplyMeshState = [](UStaticMeshComponent* C, bool bShow)
    {
        if (IsValid(C))
        {
            const bool bHasMesh = IsValid(C->GetStaticMesh());
            C->SetVisibility(bShow && bHasMesh);
        }
    };

    const bool bShowAll = (TargetType == EFurnitureComponentType::None);

    // First hide all preview meshes.
    ApplyMeshState(CabinetMesh.Get(),          bShowAll);
    ApplyMeshState(DoorMeshSlot0.Get(),        bShowAll);
    ApplyMeshState(DoorMeshSlot1.Get(),        bShowAll);
    ApplyMeshState(CountertopMesh.Get(),       bShowAll);
    ApplyMeshState(SinkMesh.Get(),             bShowAll);
    ApplyMeshState(FaucetMesh.Get(),           bShowAll);
    ApplyMeshState(MirrorMesh.Get(),           bShowAll);
    ApplyMeshState(ClosetMesh.Get(),           bShowAll);
    ApplyMeshState(ClosetDoorMeshSlot0.Get(),  bShowAll);
    ApplyMeshState(ClosetDoorMeshSlot1.Get(),  bShowAll);

    // Reveal ONLY the active focus group.
    if (!bShowAll)
    {
        switch (TargetType)
        {
        case EFurnitureComponentType::Cabinet:
            ApplyMeshState(CabinetMesh.Get(),   true);
            ApplyMeshState(DoorMeshSlot0.Get(), true);
            ApplyMeshState(DoorMeshSlot1.Get(), true);
            break;
        case EFurnitureComponentType::Closet:
            ApplyMeshState(ClosetMesh.Get(),          true);
            ApplyMeshState(ClosetDoorMeshSlot0.Get(), true);
            ApplyMeshState(ClosetDoorMeshSlot1.Get(), true);
            break;
        case EFurnitureComponentType::Countertop:
            ApplyMeshState(CountertopMesh.Get(), true);
            break;
        case EFurnitureComponentType::Sink:
            ApplyMeshState(SinkMesh.Get(), true);
            break;
        case EFurnitureComponentType::Faucet:
            ApplyMeshState(FaucetMesh.Get(), true);
            break;
        case EFurnitureComponentType::Mirror:
            ApplyMeshState(MirrorMesh.Get(), true);
            break;
        default: break;
        }
    }

    // ── 2. Resolve focused UStaticMeshComponent ───────────────────────────
    UStaticMeshComponent* TargetComp = nullptr;
    switch (TargetType)
    {
    case EFurnitureComponentType::Cabinet:    TargetComp = CabinetMesh.Get();    break;
    case EFurnitureComponentType::Closet:     TargetComp = ClosetMesh.Get();     break;
    case EFurnitureComponentType::Countertop: TargetComp = CountertopMesh.Get(); break;
    case EFurnitureComponentType::Sink:       TargetComp = SinkMesh.Get();       break;
    case EFurnitureComponentType::Faucet:     TargetComp = FaucetMesh.Get();     break;
    case EFurnitureComponentType::Mirror:     TargetComp = MirrorMesh.Get();     break;
    default: break;
    }
    CurrentFocusedComponent = TargetComp;

    // ── 3. Neutral mesh state ─────────────────────────────────────────────
    if (IsValid(MeshRoot))
    {
        MeshRoot->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
    }

    // ── 4. Compute focus pivot and bounds radius ──────────────────────────
    // The pivot stays exactly at the focused group's bounds center — the studio
    // void has no geometry to clip against, and the fixed-pivot camera orbits
    // around this point permanently.
    FVector FocusPivot = WIP_GetFocusPivotWorld();
    float MeshRadius = 80.f;
    if (IsValid(TargetComp) && TargetComp->GetStaticMesh())
    {
        MeshRadius = FMath::Max(15.f, TargetComp->Bounds.SphereRadius);
    }
    WIP_MeshBoundsRadius = MeshRadius;
    WIP_FocusPivotWorld  = FocusPivot;

    // Neutral MeshRoot state for AR export (the mesh never moves in studio mode).
    if (IsValid(MeshRoot))
    {
        WIP_MeshRootLocAtReset  = MeshRoot->GetComponentLocation();
        WIP_InitialMeshRootQuat = MeshRoot->GetComponentQuat();
    }

    // ── 5. Entry view: consistent front view for every component ──────────
    // Base direction: the booth's facing axis (booths face into the open side of
    // the bathroom), so the entry view is booth-relative and therefore identical
    // for any placement or rotation of the booth in any level. Two corrections
    // make the view a natural product-shot front view for ALL components:
    //   - Per-component EntryYawOffsetDegrees fixes meshes whose authored front
    //     does not align with the booth's forward axis (the "enters showing its
    //     side" problem) — set once in BP_FurniturePreviewActor.
    //   - Actor-wide EntryPitchDegrees tilts the camera slightly above the mesh
    //     for the classic three-quarter presentation.
    const float EntryYawOffset = Config ? Config->EntryYawOffsetDegrees : 0.f;
    const float EntryYaw       = GetActorRotation().Yaw + 180.f + EntryYawOffset;
    WIP_InitialOrbitRot        = FRotator(EntryPitchDegrees, EntryYaw, 0.f);

    // ── 6. Position SpringArm at pivot ───────────────────────────────────
    // (Distance/framing is set by SetupStudioFraming below — box-aware fit.)
    if (IsValid(SpringArm))
    {
        SpringArm->bDoCollisionTest        = false;
        SpringArm->bUsePawnControlRotation = false;
        SpringArm->SetWorldLocation(FocusPivot);
        SpringArm->SetWorldRotation(WIP_InitialOrbitRot);
        SpringArm->UpdateChildTransforms();
    }

    // ── 7. Hide character mesh ────────────────────────────────────────────
    {
        ACharacter* Char = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
        if (IsValid(Char))
        {
            if (USkeletalMeshComponent* CharMesh = Char->GetMesh())
            {
                CharMesh->SetVisibility(false);
                WIP_CachedCharacter = Char;
            }
        }
    }

    // ── 8. Studio camera recipe + framing ─────────────────────────────────
    // The stage's calibrated recipe (exposure, tone response, pure IBL) with
    // this component's exposure nudge, then the studio framing: FOV 32,
    // box-aware ~105% fit scaled by the component's StudioDistanceScale,
    // entry view, eased orbit.
    StudioActiveDistanceScale = Config ? Config->StudioDistanceScale : 1.f;
    if (AStudioStageActor* Stage = StudioStage.Get())
    {
        Stage->ApplyCameraRecipe(Camera, Config ? Config->ExposureCompensation : 0.f);
    }
    SetupStudioFraming();

    // ── 9. Per-component subject light scale ──────────────────────────────
    if (AStudioStageActor* Stage = StudioStage.Get())
    {
        Stage->SetSubjectLightScale(Config ? Config->StudioLightScale : 1.f);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera orbit and zoom
// ─────────────────────────────────────────────────────────────────────────────

void AFurniturePreviewActor::RotatePreview(float DeltaYaw, float DeltaPitch)
{
    // Public orbit API (BP/Pixel Streaming callers): drives the eased studio
    // camera orbit. ZERO-GUARD: BP graphs may poll this every frame with (0,0);
    // that must not count as interaction (it would suppress the turntable).
    if (!bStudioStageMode)
    {
        return;
    }
    if (FMath::Abs(DeltaYaw) <= KINDA_SMALL_NUMBER && FMath::Abs(DeltaPitch) <= KINDA_SMALL_NUMBER)
    {
        return;
    }
    StudioYaw += DeltaYaw;
    StudioPitch = FMath::Clamp(StudioPitch - DeltaPitch, -85.f, 85.f);
    StudioNotifyInteraction();
}

void AFurniturePreviewActor::ResetRotation()
{
    // Reset = recompute the box-aware fit (the viewport aspect may have
    // changed) and glide back to the entry framing; velocities cleared.
    if (!bStudioStageMode)
    {
        return;
    }
    StudioFitDistance = ComputeStudioFitDistance();
    StudioMinDist = StudioFitDistance * 0.35f;
    StudioMaxDist = StudioFitDistance * 3.0f;
    StudioYaw = WIP_InitialOrbitRot.Yaw;
    StudioPitch = WIP_InitialOrbitRot.Pitch;
    StudioDist = FMath::Clamp(StudioFitDistance * FMath::Clamp(StudioActiveDistanceScale, 0.25f, 4.f),
                              StudioMinDist, StudioMaxDist);
    StudioPanOffset = FVector2D::ZeroVector;
    StudioYawVel = StudioPitchVel = 0.f;
    StudioNotifyInteraction();
}

void AFurniturePreviewActor::ZoomPreview(float DeltaZoom)
{
    // Public zoom API (BP/Pixel Streaming callers pass an additive cm delta,
    // positive = out): mapped onto one multiplicative wheel notch.
    // CRITICAL ZERO-GUARD: BP graphs may POLL this every frame with delta 0.0;
    // without the guard that would classify as one zoom notch per frame.
    if (bStudioStageMode && FMath::Abs(DeltaZoom) > KINDA_SMALL_NUMBER)
    {
        StudioZoom(DeltaZoom > 0.f ? -1.f : 1.f);
    }
}



// ─────────────────────────────────────────────────────────────────────────────
// Private Helpers
// ─────────────────────────────────────────────────────────────────────────────

FVector AFurniturePreviewActor::WIP_GetFocusPivotWorld() const
{
    if (IsValid(CurrentFocusedComponent) && CurrentFocusedComponent->GetStaticMesh())
    {
        return CurrentFocusedComponent->Bounds.Origin;
    }
    if (IsValid(MeshRoot)) { return MeshRoot->GetComponentLocation(); }
    return GetActorLocation();
}

void AFurniturePreviewActor::ConfigureMesh(UStaticMeshComponent* Comp) const
{
    if (!IsValid(Comp)) { return; }
    Comp->SetMobility(EComponentMobility::Movable);
    Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // Shadows off: the subject is lit by the shadowless channel-1 rig, so its own
    // shadow casting would only produce artifacts against channel-0 world lighting.
    Comp->SetCastShadow(false);
    Comp->SetCastHiddenShadow(false);

    // LIGHTING CHANNEL 1: world lights (channel 0) do not light the preview meshes
    // directly — this is what keeps illumination even through a full rotation.
    // Lumen GI and reflections are not channel-filtered, so the intact room still
    // provides realistic ambient light and reflections on the subject.
    Comp->LightingChannels.bChannel0 = false;
    Comp->LightingChannels.bChannel1 = true;
    Comp->LightingChannels.bChannel2 = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Mesh + Material application helpers
// ─────────────────────────────────────────────────────────────────────────────

void AFurniturePreviewActor::ApplyComponentMeshAndMaterials(UStaticMeshComponent* Target,
                                                            const FFurnitureComponentOptions& Options,
                                                            int32 SizeIndex,
                                                            int32 ColorIndex)
{
    if (!IsValid(Target) || Target->IsUnreachable()) { return; }

    TSoftObjectPtr<UStaticMesh> MeshPtr;
    if (Options.Models.IsValidIndex(SizeIndex))  { MeshPtr = Options.Models[SizeIndex].Mesh; }
    else if (Options.Models.Num() > 0)           { MeshPtr = Options.Models[0].Mesh; }

    if (MeshPtr.IsNull()) { Target->SetStaticMesh(nullptr); Target->SetVisibility(false); return; }

    UStaticMesh* Loaded = MeshPtr.LoadSynchronous();
    if (!IsValid(Loaded)) { Target->SetStaticMesh(nullptr); Target->SetVisibility(false); return; }

    Target->SetStaticMesh(Loaded);
    Target->SetVisibility(true);
    Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    for (int32 i = 0; i < Target->GetNumMaterials(); ++i) { Target->SetMaterial(i, nullptr); }

    const FFurnitureColorOption* Color = nullptr;
    if (Options.Models.IsValidIndex(SizeIndex) && Options.Models[SizeIndex].Colors.IsValidIndex(ColorIndex))
    {
        Color = &Options.Models[SizeIndex].Colors[ColorIndex];
    }
    if (Color)
    {
        for (const FFurnitureMaterialSlot& Slot : Color->MaterialOverrides)
        {
            UMaterialInterface* Mat = Slot.Material.LoadSynchronous();
            if (IsValid(Mat) && Target->GetNumMaterials() > Slot.SlotIndex)
            {
                Target->SetMaterial(Slot.SlotIndex, Mat);
            }
        }
    }
}

void AFurniturePreviewActor::ApplyComponentMeshAndMaterials(UStaticMeshComponent* Target,
                                                            const FFurnitureCabinetOptions& Options,
                                                            int32 SizeIndex,
                                                            int32 ColorIndex)
{
    if (!IsValid(Target) || Target->IsUnreachable()) { return; }

    TSoftObjectPtr<UStaticMesh> MeshPtr;
    if (Options.Sizes.IsValidIndex(SizeIndex)) { MeshPtr = Options.Sizes[SizeIndex]; }
    else if (Options.Sizes.Num() > 0)          { MeshPtr = Options.Sizes[0]; }

    if (MeshPtr.IsNull()) { Target->SetStaticMesh(nullptr); Target->SetVisibility(false); return; }

    UStaticMesh* Loaded = MeshPtr.LoadSynchronous();
    if (!IsValid(Loaded)) { Target->SetStaticMesh(nullptr); Target->SetVisibility(false); return; }

    Target->SetStaticMesh(Loaded);
    Target->SetVisibility(true);
    Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    for (int32 i = 0; i < Target->GetNumMaterials(); ++i) { Target->SetMaterial(i, nullptr); }

    const FFurnitureColorOption* Color = nullptr;
    if (Options.Colors.IsValidIndex(ColorIndex))  { Color = &Options.Colors[ColorIndex]; }
    else if (Options.Colors.Num() > 0)            { Color = &Options.Colors[0]; }

    if (Color)
    {
        for (const FFurnitureMaterialSlot& Slot : Color->MaterialOverrides)
        {
            UMaterialInterface* Mat = Slot.Material.LoadSynchronous();
            if (IsValid(Mat) && Target->GetNumMaterials() > Slot.SlotIndex)
            {
                Target->SetMaterial(Slot.SlotIndex, Mat);
            }
        }
    }
}

void AFurniturePreviewActor::ApplyDoorMeshAndMaterials(UStaticMeshComponent* Target,
                                                       const FFurnitureDoorGroup& DoorGroup,
                                                       int32 SizeIndex,
                                                       int32 ColorIndex,
                                                       int32 SlotIndex)
{
    if (!IsValid(Target) || Target->IsUnreachable()) { return; }

    if (DoorGroup.DoorCount == EDoorCount::NoDoors)
    {
        Target->SetStaticMesh(nullptr);
        Target->SetVisibility(false);
        return;
    }

    TSoftObjectPtr<UStaticMesh> MeshPtr;
    TArray<FFurnitureMaterialSlot> Materials;

    if (DoorGroup.DoorCount == EDoorCount::OneDoor)
    {
        if (SlotIndex != 0)
        {
            Target->SetStaticMesh(nullptr);
            Target->SetVisibility(false);
            return;
        }

        const FFurnitureSingleDoorConfig& Cfg = DoorGroup.SingleDoor;
        MeshPtr = Cfg.Sizes.IsValidIndex(SizeIndex) ? Cfg.Sizes[SizeIndex]
                : Cfg.Sizes.Num() > 0               ? Cfg.Sizes[0]
                : TSoftObjectPtr<UStaticMesh>();

        const FFurnitureDoorColorOption* Col = Cfg.Colors.IsValidIndex(ColorIndex) ? &Cfg.Colors[ColorIndex]
                                             : Cfg.Colors.Num() > 0                ? &Cfg.Colors[0]
                                             : nullptr;
        if (Col) { Materials = Col->MaterialOverrides; }
    }
    else if (DoorGroup.DoorCount == EDoorCount::TwoDoors)
    {
        const FFurnitureDoubleDoorsConfig& Cfg = DoorGroup.DoubleDoors;
        if (Cfg.Sizes.IsValidIndex(SizeIndex))
        {
            MeshPtr = (SlotIndex == 0) ? Cfg.Sizes[SizeIndex].Slot0Mesh : Cfg.Sizes[SizeIndex].Slot1Mesh;
        }
        else if (Cfg.Sizes.Num() > 0)
        {
            MeshPtr = (SlotIndex == 0) ? Cfg.Sizes[0].Slot0Mesh : Cfg.Sizes[0].Slot1Mesh;
        }

        const FFurnitureDoubleDoorsColorOption* Col = Cfg.Colors.IsValidIndex(ColorIndex) ? &Cfg.Colors[ColorIndex]
                                                    : Cfg.Colors.Num() > 0                ? &Cfg.Colors[0]
                                                    : nullptr;
        if (Col) { Materials = (SlotIndex == 0) ? Col->Slot0MaterialOverrides : Col->Slot1MaterialOverrides; }
    }

    if (MeshPtr.IsNull()) { Target->SetStaticMesh(nullptr); Target->SetVisibility(false); return; }

    UStaticMesh* Loaded = MeshPtr.LoadSynchronous();
    if (!IsValid(Loaded)) { Target->SetStaticMesh(nullptr); Target->SetVisibility(false); return; }

    Target->SetStaticMesh(Loaded);
    Target->SetVisibility(true);
    Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    for (int32 i = 0; i < Target->GetNumMaterials(); ++i) { Target->SetMaterial(i, nullptr); }
    for (const FFurnitureMaterialSlot& Slot : Materials)
    {
        UMaterialInterface* Mat = Slot.Material.LoadSynchronous();
        if (IsValid(Mat) && Target->GetNumMaterials() > Slot.SlotIndex)
        {
            Target->SetMaterial(Slot.SlotIndex, Mat);
        }
    }
}
