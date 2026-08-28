// Copyright MaxiMall Project. All Rights Reserved.
//
// STANDALONE STUDIO VIEWER TEST — isolated proof-of-concept, safe to delete.
// See StudioViewerTestActor.h for usage. No dependency on any project system.

#include "StudioViewerTest/StudioViewerTestActor.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkyLight.h"
#include "Engine/RectLight.h"
#include "Engine/Scene.h"
#include "Engine/TextureCube.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/PlayerController.h"
#include "Components/InputComponent.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "HAL/IConsoleManager.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    /** The studio lives far from the level content so nothing in the scene leaks into it. */
    const FVector GStudioOrigin(0.0f, 0.0f, 50000.0f);

    constexpr float GMaxOrbitPitch = 85.0f;

    /** Backdrop/panel meshes are taken from engine basic shapes (always available). */
    UStaticMesh* LoadEngineShape(const TCHAR* Path)
    {
        return LoadObject<UStaticMesh>(nullptr, Path);
    }
}

AStudioViewerTestActor::AStudioViewerTestActor()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AStudioViewerTestActor::BeginPlay()
{
    Super::BeginPlay();

    if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
    {
        EnableInput(PC);
        if (InputComponent)
        {
            // Raw key/axis bindings on this actor's OWN input component: no project
            // input mappings, no player-controller changes.
            InputComponent->Priority = 100;
            InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AStudioViewerTestActor::ToggleStudioView);
            InputComponent->BindKey(EKeys::R, IE_Pressed, this, &AStudioViewerTestActor::ResetFraming);

            InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed,  this, &AStudioViewerTestActor::OnOrbitPressed);
            InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AStudioViewerTestActor::OnOrbitReleased);
            InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed,  this, &AStudioViewerTestActor::OnPanPressed);
            InputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &AStudioViewerTestActor::OnPanReleased);
            InputComponent->BindKey(EKeys::MiddleMouseButton, IE_Pressed,  this, &AStudioViewerTestActor::OnPanPressed);
            InputComponent->BindKey(EKeys::MiddleMouseButton, IE_Released, this, &AStudioViewerTestActor::OnPanReleased);

            InputComponent->BindAxisKey(EKeys::MouseX, this, &AStudioViewerTestActor::OnMouseX);
            InputComponent->BindAxisKey(EKeys::MouseY, this, &AStudioViewerTestActor::OnMouseY);
            InputComponent->BindAxisKey(EKeys::MouseWheelAxis, this, &AStudioViewerTestActor::OnMouseWheel);
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("[StudioViewer] Ready. F = studio view on/off, left-drag = orbit, wheel = zoom, right/middle-drag = pan, R = reset."));
}

void AStudioViewerTestActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (!bStudioActive)
    {
        return;
    }

    // ── Fling: estimate angular velocity while dragging, coast after release ──
    if (bOrbiting)
    {
        if (DeltaSeconds > KINDA_SMALL_NUMBER)
        {
            // Smoothed over the last few frames so a pause before release kills the fling.
            const float Alpha = FMath::Clamp(DeltaSeconds * 20.0f, 0.0f, 1.0f);
            YawVelocity = FMath::Lerp(YawVelocity, FrameYawInput / DeltaSeconds, Alpha);
            PitchVelocity = FMath::Lerp(PitchVelocity, FramePitchInput / DeltaSeconds, Alpha);
        }
    }
    else if (bOrbitInertia && (FMath::Abs(YawVelocity) > 0.5f || FMath::Abs(PitchVelocity) > 0.5f))
    {
        OrbitYaw += YawVelocity * DeltaSeconds;
        OrbitPitch = FMath::Clamp(OrbitPitch + PitchVelocity * DeltaSeconds, -GMaxOrbitPitch, GMaxOrbitPitch);
        if (FMath::Abs(OrbitPitch) >= GMaxOrbitPitch)
        {
            PitchVelocity = 0.0f;
        }
        const float Decay = FMath::Exp(-OrbitInertiaDamping * DeltaSeconds);
        YawVelocity *= Decay;
        PitchVelocity *= Decay;
        IdleSeconds = 0.0f; // a live fling holds off the turntable
    }
    else
    {
        YawVelocity = 0.0f;
        PitchVelocity = 0.0f;
    }
    FrameYawInput = 0.0f;
    FramePitchInput = 0.0f;

    // ── Idle turntable ───────────────────────────────────────────────────────
    IdleSeconds += DeltaSeconds;
    if (bAutoRotateWhenIdle && !bOrbiting && !bPanning && IdleSeconds > AutoRotateIdleDelay)
    {
        OrbitYaw += AutoRotateSpeedDegPerSec * DeltaSeconds;
    }

    ApplyCameraTransform(/*bInstant=*/false);

    // Keep the pivot dot on the (eased) focus point at a constant screen size.
    if (PivotMarker)
    {
        PivotMarker->SetActorLocation(StudioFocus + CurrentPan);
        PivotMarker->SetActorScale3D(FVector(FMath::Max(CurrentDistance, 1.0f) * 0.0023f / 50.0f));
    }

    // Reassert the grab cursor every tick: the project's own PlayerController may
    // overwrite CurrentMouseCursor per frame, and we must win without touching it.
    if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
    {
        PC->CurrentMouseCursor = EMouseCursor::GrabHand;
    }
}

float AStudioViewerTestActor::ComputeFitDistance() const
{
    // Box-based, aspect-aware fit: the subject's worst-case horizontal footprint
    // (any yaw) and its height must both fit in the frustum, plus the near face
    // sitting closer to the camera than the box center.
    const float HalfHFOVRad = FMath::DegreesToRadians(FMath::Clamp(CameraFOV, 5.0f, 170.0f) * 0.5f);

    float Aspect = 16.0f / 9.0f;
    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
    {
        const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
        if (Size.X > 0 && Size.Y > 0)
        {
            Aspect = static_cast<float>(Size.X) / static_cast<float>(Size.Y);
        }
    }
    // UE's FieldOfView is horizontal.
    const float HalfVFOVRad = FMath::Atan(FMath::Tan(HalfHFOVRad) / FMath::Max(Aspect, 0.1f));

    const float ExtX = static_cast<float>(SubjectBoxExtent.X);
    const float ExtY = static_cast<float>(SubjectBoxExtent.Y);
    const float HorizRadius = FMath::Max(FMath::Sqrt(ExtX * ExtX + ExtY * ExtY), 1.0f);
    const float VertRadius = FMath::Max(static_cast<float>(SubjectBoxExtent.Z), 1.0f);

    const float DistH = HorizRadius / FMath::Max(FMath::Tan(HalfHFOVRad), 0.01f);
    const float DistV = VertRadius / FMath::Max(FMath::Tan(HalfVFOVRad), 0.01f);

    // Half the footprint as depth allowance (full radius over-padded the frame
    // compared to the web viewer's ~105% fit).
    return (FMath::Max(DistH, DistV) + 0.5f * HorizRadius) * FMath::Max(FramingMargin, 1.0f);
}

float AStudioViewerTestActor::ComputeExposureMultiplier() const
{
    // The studio disables "Apply Physical Camera Exposure", so the renderer's manual
    // exposure reduces to exactly 2^Bias — scene units map ~1:1 to display range
    // (1 scene unit -> white), which is also how model-viewer's scene is scaled.
    return FMath::Pow(2.0f, LockedEV100);
}

void AStudioViewerTestActor::ApplyCameraTransform(bool bInstant)
{
    if (!StudioCamera)
    {
        return;
    }

    if (bInstant || CameraSmoothingSpeed <= 0.0f)
    {
        CurrentYaw = OrbitYaw;
        CurrentPitch = OrbitPitch;
        CurrentDistance = OrbitDistance;
        CurrentPan = PanOffset;
    }
    else
    {
        const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
        // Orbit/pan share one easing; zoom gets its own, snappier one.
        CurrentYaw = FMath::FInterpTo(CurrentYaw, OrbitYaw, Dt, CameraSmoothingSpeed);
        CurrentPitch = FMath::FInterpTo(CurrentPitch, OrbitPitch, Dt, CameraSmoothingSpeed);
        CurrentPan = FMath::VInterpTo(CurrentPan, PanOffset, Dt, CameraSmoothingSpeed);
        CurrentDistance = FMath::FInterpTo(CurrentDistance, OrbitDistance, Dt, ZoomSmoothingSpeed > 0.0f ? ZoomSmoothingSpeed : CameraSmoothingSpeed);
    }

    const FVector Focus = StudioFocus + CurrentPan;
    const FRotator Rot(CurrentPitch, CurrentYaw + 180.0f, 0.0f);
    StudioCamera->SetActorLocationAndRotation(Focus - Rot.Vector() * CurrentDistance, Rot);
}

void AStudioViewerTestActor::ClampPan()
{
    PanOffset = PanOffset.GetClampedToMaxSize(SubjectBoundsRadius * FMath::Max(PanRangeFactor, 0.0f));
}

void AStudioViewerTestActor::OnOrbitPressed()
{
    bOrbiting = true;
    YawVelocity = 0.0f;   // grabbing the model stops any running fling
    PitchVelocity = 0.0f;
    NotifyInteraction();
}

void AStudioViewerTestActor::OnOrbitReleased()
{
    bOrbiting = false;
    NotifyInteraction();
}

void AStudioViewerTestActor::OnPanPressed()
{
    bPanning = true;
    SetPivotMarkerVisible(true);
    NotifyInteraction();
}

void AStudioViewerTestActor::OnPanReleased()
{
    bPanning = false;
    SetPivotMarkerVisible(false);
    NotifyInteraction();
}

void AStudioViewerTestActor::SetPivotMarkerVisible(bool bVisible)
{
    if (PivotMarker && bStudioActive)
    {
        PivotMarker->SetActorHiddenInGame(!bVisible);
    }
}

void AStudioViewerTestActor::OnMouseX(float Value)
{
    if (!bStudioActive || FMath::IsNearlyZero(Value))
    {
        return;
    }
    if (bOrbiting)
    {
        const float Delta = Value * OrbitSensitivity;
        OrbitYaw += Delta;
        FrameYawInput += Delta;
        NotifyInteraction();
    }
    else if (bPanning && StudioCamera)
    {
        PanOffset -= StudioCamera->GetActorRightVector() * (Value * PanSensitivity * OrbitDistance);
        ClampPan();
        NotifyInteraction();
    }
}

void AStudioViewerTestActor::OnMouseY(float Value)
{
    if (!bStudioActive || FMath::IsNearlyZero(Value))
    {
        return;
    }
    if (bOrbiting)
    {
        const float Delta = Value * OrbitSensitivity;
        OrbitPitch = FMath::Clamp(OrbitPitch + Delta, -GMaxOrbitPitch, GMaxOrbitPitch);
        FramePitchInput += Delta;
        NotifyInteraction();
    }
    else if (bPanning && StudioCamera)
    {
        PanOffset -= StudioCamera->GetActorUpVector() * (Value * PanSensitivity * OrbitDistance);
        ClampPan();
        NotifyInteraction();
    }
}

void AStudioViewerTestActor::OnMouseWheel(float Value)
{
    if (!bStudioActive || FMath::IsNearlyZero(Value))
    {
        return;
    }
    NotifyInteraction();

    // Multiplicative zoom feels linear on screen, like the web viewer.
    const float OldDistance = OrbitDistance;
    OrbitDistance = FMath::Clamp(OrbitDistance * FMath::Exp(-Value * ZoomSensitivity),
        FitDistance * MinZoomFactor, FitDistance * MaxZoomFactor);

    // Zoom toward the cursor: keep the point under the cursor (projected onto the
    // focal plane through the focus point) fixed on screen while the distance shrinks.
    if (bZoomToCursor && StudioCamera)
    {
        APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
        FVector RayOrigin, RayDir;
        if (PC && PC->DeprojectMousePositionToWorld(RayOrigin, RayDir))
        {
            const FVector CamFwd = StudioCamera->GetActorForwardVector();
            const FVector Focus = StudioFocus + PanOffset;
            const float Denom = static_cast<float>(FVector::DotProduct(RayDir, CamFwd));
            if (Denom > KINDA_SMALL_NUMBER)
            {
                const float T = static_cast<float>(FVector::DotProduct(Focus - RayOrigin, CamFwd)) / Denom;
                const FVector CursorPoint = RayOrigin + RayDir * T;
                const float Shrink = OrbitDistance / FMath::Max(OldDistance, 1.0f);
                PanOffset += (CursorPoint - Focus) * (1.0f - Shrink);
                ClampPan();
            }
        }
    }
}

void AStudioViewerTestActor::ResetFraming()
{
    if (!bStudioActive)
    {
        return;
    }
    FitDistance = ComputeFitDistance(); // viewport aspect may have changed
    OrbitYaw = StartYawDegrees;
    OrbitPitch = StartPitchDegrees;
    OrbitDistance = FitDistance;
    PanOffset = FVector::ZeroVector;
    YawVelocity = 0.0f;
    PitchVelocity = 0.0f;
    NotifyInteraction();
}

void AStudioViewerTestActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (bStudioActive)
    {
        ExitStudioView();
    }
    // The exit blend's deferred destruction can't run during teardown — flush it.
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(ExitCleanupTimerHandle);
        World->GetTimerManager().ClearTimer(PanelHideTimerHandle);
    }
    FinishExitCleanup();
    HideHintOverlay();
    Super::EndPlay(EndPlayReason);
}

void AStudioViewerTestActor::ToggleStudioView()
{
    if (bStudioActive)
    {
        ExitStudioView();
    }
    else
    {
        EnterStudioView();
    }
}

AStaticMeshActor* AStudioViewerTestActor::FindSubjectInLevel() const
{
    if (ExplicitSubject && ExplicitSubject->GetStaticMeshComponent() && ExplicitSubject->GetStaticMeshComponent()->GetStaticMesh())
    {
        return ExplicitSubject;
    }

    for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
    {
        AStaticMeshActor* Candidate = *It;
        if (Candidate && Candidate != SubjectCopy && Candidate->GetStaticMeshComponent() && Candidate->GetStaticMeshComponent()->GetStaticMesh())
        {
            // Skip anything we spawned for the studio itself (including a studio
            // still fading out from a previous exit).
            if (!StudioActors.Contains(Candidate) && !DyingStudioActors.Contains(Candidate))
            {
                return Candidate;
            }
        }
    }
    return nullptr;
}

UMaterialInstanceDynamic* AStudioViewerTestActor::MakeUnlitColorMID(const FLinearColor& LinearColor)
{
    // GEngine->EmissiveMeshMaterial is an unlit emissive material driven by the
    // "Color" vector parameter (the engine itself uses it that way).
    UMaterialInterface* Base = GEngine ? GEngine->EmissiveMeshMaterial : nullptr;
    if (!Base)
    {
        return nullptr;
    }
    UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
    if (MID)
    {
        MID->SetVectorParameterValue(TEXT("Color"), LinearColor);
    }
    return MID;
}

ARectLight* AStudioViewerTestActor::SpawnSoftbox(const FVector& FocusPoint, float SubjectRadius, float YawDeg, float PitchDeg, float Lux, bool bCastShadows)
{
    UWorld* World = GetWorld();
    if (!World || Lux <= 0.0f)
    {
        return nullptr;
    }

    const float Distance = FMath::Max(SubjectRadius * LightDistanceFactor, 50.0f);
    const FRotator Dir(PitchDeg, YawDeg, 0.0f);
    const FVector Location = FocusPoint - Dir.Vector() * Distance;

    ARectLight* Light = World->SpawnActor<ARectLight>(Location, Dir);
    if (!Light)
    {
        return nullptr;
    }

    URectLightComponent* Comp = Cast<URectLightComponent>(Light->GetLightComponent());
    if (Comp)
    {
        const float SizeCm = FMath::Max(SubjectRadius * SoftboxSizeFactor, 25.0f);
        Comp->SetMobility(EComponentMobility::Movable);
        Comp->SetSourceWidth(SizeCm);
        Comp->SetSourceHeight(SizeCm);
        // Fills/rim stay shadowless (like the web IBL); the key & top may cast soft
        // area shadows for form on diffuse materials (see bKeyLightShadows).
        Comp->SetCastShadows(bCastShadows);
        Comp->SetIntensityUnits(ELightUnits::Candelas);
        // I[cd] = E[lux] * d[m]^2 -> the requested illuminance lands on the subject.
        const float DistanceMeters = Distance * 0.01f;
        Comp->SetIntensity(Lux * DistanceMeters * DistanceMeters);
        Comp->SetLightColor(FLinearColor::White);
        Comp->SetAttenuationRadius(Distance * 4.0f);
    }

    StudioActors.Add(Light);

    // Emissive panel at the same place, so metals reflect real softbox shapes — this
    // is what the web viewer's "neutral" HDRI provides. The panel exists only for the
    // environment capture and is hidden right after it (an invisible HDRI source).
    if (UStaticMesh* PlaneMesh = LoadEngineShape(TEXT("/Engine/BasicShapes/Plane.Plane")))
    {
        const float SizeCm = FMath::Max(SubjectRadius * SoftboxSizeFactor, 25.0f);
        AStaticMeshActor* Panel = World->SpawnActor<AStaticMeshActor>(Location, Dir + FRotator(90.0f, 0.0f, 0.0f));
        if (Panel)
        {
            Panel->SetMobility(EComponentMobility::Movable);
            UStaticMeshComponent* PanelComp = Panel->GetStaticMeshComponent();
            PanelComp->SetStaticMesh(PlaneMesh);
            PanelComp->SetWorldScale3D(FVector(SizeCm / 100.0f));
            PanelComp->SetCastShadow(false);
            // Panel brightness tracks its light's share so reflections stay consistent.
            const float PanelLuminance = SoftboxPanelBrightness * FMath::Clamp(Lux / FMath::Max(KeyIlluminanceLux, KINDA_SMALL_NUMBER), 0.05f, 1.0f);
            if (UMaterialInstanceDynamic* PanelMID = MakeUnlitColorMID(FLinearColor::White * PanelLuminance))
            {
                PanelComp->SetMaterial(0, PanelMID);
            }
            StudioActors.Add(Panel);
            EnvironmentPanels.Add(Panel);
        }
    }

    return Light;
}

void AStudioViewerTestActor::EnterStudioView()
{
    UWorld* World = GetWorld();
    APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
    if (!World || !PC)
    {
        return;
    }

    // A previous exit may still be blending out — retire its studio immediately.
    World->GetTimerManager().ClearTimer(ExitCleanupTimerHandle);
    World->GetTimerManager().ClearTimer(PanelHideTimerHandle);
    FinishExitCleanup();
    EnvironmentPanels.Reset();

    AStaticMeshActor* Subject = FindSubjectInLevel();
    if (!Subject)
    {
        UE_LOG(LogTemp, Warning, TEXT("[StudioViewer] No StaticMeshActor found in the level to display."));
        return;
    }

    UStaticMeshComponent* SourceComp = Subject->GetStaticMeshComponent();
    UStaticMesh* Mesh = SourceComp->GetStaticMesh();

    // ── 1. Copy of the subject, centered at the studio origin ────────────────
    SubjectCopy = World->SpawnActor<AStaticMeshActor>(GStudioOrigin, FRotator::ZeroRotator);
    if (!SubjectCopy)
    {
        return;
    }
    SubjectCopy->SetMobility(EComponentMobility::Movable);
    UStaticMeshComponent* CopyComp = SubjectCopy->GetStaticMeshComponent();
    CopyComp->SetStaticMesh(Mesh);
    CopyComp->SetWorldScale3D(SourceComp->GetComponentScale());
    for (int32 i = 0; i < SourceComp->GetNumMaterials(); ++i)
    {
        CopyComp->SetMaterial(i, SourceComp->GetMaterial(i)); // current runtime materials, untouched
    }
    // The subject must not appear in its own environment capture.
    CopyComp->bVisibleInReflectionCaptures = false;
    CopyComp->MarkRenderStateDirty();
    StudioActors.Add(SubjectCopy);

    // Recenter so the mesh's bounds sit exactly on the studio origin.
    const FBoxSphereBounds Bounds = CopyComp->Bounds;
    const float SubjectRadius = FMath::Max(static_cast<float>(Bounds.SphereRadius), 10.0f);
    SubjectCopy->SetActorLocation(GStudioOrigin + (GStudioOrigin - Bounds.Origin));
    const FVector FocusPoint = GStudioOrigin;

    SubjectBoundsRadius = SubjectRadius;
    SubjectBoxExtent = Bounds.BoxExtent;

    const float ExposureMultiplier = ComputeExposureMultiplier();

    // ── 2. Backdrop: large unlit sphere in the viewer's flat background color ─
    if (UStaticMesh* SphereMesh = LoadEngineShape(TEXT("/Engine/BasicShapes/Sphere.Sphere")))
    {
        AStaticMeshActor* Backdrop = World->SpawnActor<AStaticMeshActor>(FocusPoint, FRotator::ZeroRotator);
        if (Backdrop)
        {
            Backdrop->SetMobility(EComponentMobility::Movable);
            UStaticMeshComponent* BackdropComp = Backdrop->GetStaticMeshComponent();
            BackdropComp->SetStaticMesh(SphereMesh);
            // Inside-out huge sphere: scale negative on X so we see its interior.
            const float Radius = SubjectRadius * 40.0f;
            BackdropComp->SetWorldScale3D(FVector(-Radius / 50.0f, Radius / 50.0f, Radius / 50.0f));
            BackdropComp->SetCastShadow(false);

            // The backdrop stays in the captured environment: metals then reflect a
            // gray surround plus the bright panels, exactly like the web viewer's
            // neutral HDRI. At unit exposure its emissive is dim, so its lighting
            // contribution is negligible.
            FLinearColor BackdropLinear = FLinearColor::FromSRGBColor(BackdropColorSRGB);
            if (bCompensateBackdropExposure && ExposureMultiplier > KINDA_SMALL_NUMBER)
            {
                // Inverse of the exposure the tonemapper will apply -> the backdrop
                // lands on screen at exactly BackdropColorSRGB, for any LockedEV100.
                BackdropLinear *= 1.0f / ExposureMultiplier;
            }
            if (UMaterialInstanceDynamic* BackdropMID = MakeUnlitColorMID(BackdropLinear))
            {
                BackdropComp->SetMaterial(0, BackdropMID);
            }
            StudioActors.Add(Backdrop);
        }
    }

    // ── 3. Soft-box rig (approximates the "neutral" studio env) ──────────────
    SpawnSoftbox(FocusPoint, SubjectRadius,  35.0f, -40.0f, KeyIlluminanceLux, bKeyLightShadows);          // key, high camera-left
    SpawnSoftbox(FocusPoint, SubjectRadius, -55.0f, -15.0f, KeyIlluminanceLux * FillLeftRatio);            // fill, camera-right
    SpawnSoftbox(FocusPoint, SubjectRadius, 140.0f, -20.0f, KeyIlluminanceLux * FillRightRatio);           // back-left wrap
    SpawnSoftbox(FocusPoint, SubjectRadius, 200.0f, -35.0f, KeyIlluminanceLux * RimRatio);                 // rim, behind
    SpawnSoftbox(FocusPoint, SubjectRadius,   0.0f, -89.0f, KeyIlluminanceLux * 0.5f, bKeyLightShadows);   // overhead soft top

    // ── 4. Ambient + reflections (captured studio or reference HDRI) ─────────
    const bool bUseReferenceCubemap = EnvironmentMode == EStudioEnvironmentMode::ReferenceCubemap && ReferenceCubemap != nullptr;
    if (EnvironmentMode == EStudioEnvironmentMode::ReferenceCubemap && !ReferenceCubemap)
    {
        UE_LOG(LogTemp, Warning, TEXT("[StudioViewer] EnvironmentMode is ReferenceCubemap but no cubemap is assigned — falling back to the captured studio."));
    }

    if (ASkyLight* Sky = World->SpawnActor<ASkyLight>(FocusPoint, FRotator::ZeroRotator))
    {
        if (USkyLightComponent* SkyComp = Sky->GetLightComponent())
        {
            SkyComp->SetMobility(EComponentMobility::Movable);
            SkyComp->bRealTimeCapture = false;
            SkyComp->SetCastShadows(false);
            // Everything counts as "sky": the default 1.5 km threshold would exclude
            // the entire studio from the capture (-> black environment, dull metals).
            SkyComp->SkyDistanceThreshold = 1.0f;
            // The web HDRI lights from below too; a black lower hemisphere kills it.
            SkyComp->bLowerHemisphereIsBlack = false;
            SkyComp->CubemapResolution = 512; // crisp panel shapes in glossy metal
            if (bUseReferenceCubemap)
            {
                SkyComp->SourceType = SLS_SpecifiedCubemap;
                SkyComp->SetCubemap(ReferenceCubemap);
                SkyComp->SetIntensity(ReferenceCubemapIntensity);
            }
            else
            {
                SkyComp->SourceType = SLS_CapturedScene;
                SkyComp->SetIntensity(1.0f);
                // Capture is deferred one tick (DeferredEnvironmentCapture) so the
                // whole studio exists before the environment is photographed.
            }
            StudioSkyComp = SkyComp;
        }
        StudioActors.Add(Sky);
    }
    // No reflection-capture actor: the SkyLight cubemap is the single reflection
    // source (pure IBL, like model-viewer), which also avoids the runtime
    // "REFLECTION CAPTURES NEED TO BE REBUILT" screen warning.

    World->GetTimerManager().SetTimerForNextTick(this, &AStudioViewerTestActor::DeferredEnvironmentCapture);

    // ── 5. Product camera with the measured "viewer" post recipe ─────────────
    StudioFocus = FocusPoint;
    FitDistance = ComputeFitDistance();
    OrbitYaw = StartYawDegrees;
    OrbitPitch = StartPitchDegrees;
    OrbitDistance = FitDistance;
    PanOffset = FVector::ZeroVector;
    YawVelocity = 0.0f;
    PitchVelocity = 0.0f;
    FrameYawInput = 0.0f;
    FramePitchInput = 0.0f;
    IdleSeconds = 0.0f;
    bOrbiting = false;
    bPanning = false;

    const FRotator CamRot(OrbitPitch, OrbitYaw + 180.0f, 0.0f);
    const FVector CamLoc = FocusPoint - CamRot.Vector() * OrbitDistance;

    StudioCamera = World->SpawnActor<ACameraActor>(CamLoc, CamRot);
    if (StudioCamera)
    {
        UCameraComponent* Cam = StudioCamera->GetCameraComponent();
        Cam->SetFieldOfView(CameraFOV);

        FPostProcessSettings& PP = Cam->PostProcessSettings;

        // Fixed exposure (no auto-exposure drift), like the viewer's exposure value.
        // Physical camera exposure is OFF, so the multiplier is exactly 2^LockedEV100
        // and scene units map ~1:1 to display range (matches ComputeExposureMultiplier).
        PP.bOverride_AutoExposureMethod = true;   PP.AutoExposureMethod = AEM_Manual;
        PP.bOverride_AutoExposureBias = true;     PP.AutoExposureBias = LockedEV100;
        PP.bOverride_AutoExposureMinBrightness = true; PP.AutoExposureMinBrightness = 1.0f;
        PP.bOverride_AutoExposureMaxBrightness = true; PP.AutoExposureMaxBrightness = 1.0f;
        PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true; PP.AutoExposureApplyPhysicalCameraExposure = false;

        // Deterministic, model-viewer-style pure IBL: no Lumen GI/reflections and no
        // SSR for this camera — the SkyLight cubemap is the only environment source.
        // (The project globally runs Lumen; this override is per-view and temporary.)
        PP.bOverride_DynamicGlobalIlluminationMethod = true;
        PP.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
        PP.bOverride_ReflectionMethod = true;
        PP.ReflectionMethod = EReflectionMethod::None;

        // Neutral tone response: 0 = linear->sRGB (color-true, like the viewer's
        // Neutral/Linear modes) instead of UE's filmic look.
        PP.bOverride_ToneCurveAmount = true;      PP.ToneCurveAmount = ToneCurveAmount;
        PP.bOverride_ExpandGamut = true;          PP.ExpandGamut = 0.0f;
        PP.bOverride_BlueCorrection = true;       PP.BlueCorrection = 0.0f;
        PP.bOverride_FilmSlope = true;            PP.FilmSlope = 0.88f;
        PP.bOverride_FilmToe = true;              PP.FilmToe = 0.55f;
        PP.bOverride_FilmShoulder = true;         PP.FilmShoulder = 0.26f;

        // No camera/lens effects — the web viewer has none of these.
        PP.bOverride_BloomIntensity = true;             PP.BloomIntensity = 0.0f;
        PP.bOverride_VignetteIntensity = true;          PP.VignetteIntensity = 0.0f;
        PP.bOverride_SceneFringeIntensity = true;       PP.SceneFringeIntensity = 0.0f;
        PP.bOverride_FilmGrainIntensity = true;         PP.FilmGrainIntensity = 0.0f;
        PP.bOverride_MotionBlurAmount = true;           PP.MotionBlurAmount = 0.0f;
        PP.bOverride_LensFlareIntensity = true;         PP.LensFlareIntensity = 0.0f;
        // Mild SSAO grounds diffuse materials (wood/fabric); the web viewer has none.
        PP.bOverride_AmbientOcclusionIntensity = true;  PP.AmbientOcclusionIntensity = FMath::Clamp(AmbientOcclusionAmount, 0.0f, 1.0f);

        // Optional Khronos PBR Neutral (or any) tone-mapping blendable.
        UMaterialInterface* ToneMap = ToneMapperMaterial;
        if (!ToneMap)
        {
            ToneMap = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/StudioViewerTest/M_PBRNeutral.M_PBRNeutral"));
        }
        if (ToneMap)
        {
            PP.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, ToneMap));
            UE_LOG(LogTemp, Warning, TEXT("[StudioViewer] Tone-mapper blendable active: %s"), *ToneMap->GetName());
        }

        StudioActors.Add(StudioCamera);

        // Land the camera on its start transform, then blend the view over.
        ApplyCameraTransform(/*bInstant=*/true);

        SavedViewTarget = PC->GetViewTarget();
        PC->SetViewTargetWithBlend(StudioCamera, FMath::Max(EnterBlendSeconds, 0.0f), VTBlend_EaseInOut, 2.0f);

        // Editor-viewport cursor behavior: cursor stays visible, and is hidden only
        // while a mouse button is held (drag capture). No project input logic involved.
        bSavedShowMouseCursor = PC->bShowMouseCursor;
        SavedMouseCursor = static_cast<int32>(PC->CurrentMouseCursor.GetValue());
        SavedDefaultMouseCursor = static_cast<int32>(PC->DefaultMouseCursor.GetValue());
        PC->bShowMouseCursor = true;
        // Web-viewer style grab affordance. Both cursors are set (and Tick reasserts
        // the current one) so a project PlayerController resetting the cursor from
        // its default every frame still ends up showing the grab hand.
        PC->CurrentMouseCursor = EMouseCursor::GrabHand;
        PC->DefaultMouseCursor = EMouseCursor::GrabHand;
        FInputModeGameAndUI InputMode;
        InputMode.SetHideCursorDuringCapture(true);
        InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        InputMode.SetWidgetToFocus(nullptr);
        PC->SetInputMode(InputMode);
    }

    // ── 6. Pivot marker: small dot at the orbit/pan center, shown while panning ─
    if (UStaticMesh* MarkerMesh = LoadEngineShape(TEXT("/Engine/BasicShapes/Sphere.Sphere")))
    {
        PivotMarker = World->SpawnActor<AStaticMeshActor>(FocusPoint, FRotator::ZeroRotator);
        if (PivotMarker)
        {
            PivotMarker->SetMobility(EComponentMobility::Movable);
            UStaticMeshComponent* MarkerComp = PivotMarker->GetStaticMeshComponent();
            MarkerComp->SetStaticMesh(MarkerMesh);
            MarkerComp->SetCastShadow(false);
            MarkerComp->bVisibleInReflectionCaptures = false;
            MarkerComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            if (UMaterialInstanceDynamic* MarkerMID = MakeUnlitColorMID(FLinearColor(1.5f, 1.5f, 1.5f)))
            {
                MarkerComp->SetMaterial(0, MarkerMID);
            }
            PivotMarker->SetActorHiddenInGame(true);
            StudioActors.Add(PivotMarker);
        }
    }

    ApplyStudioCVars();
    ShowHintOverlay();

    bStudioActive = true;
    UE_LOG(LogTemp, Warning, TEXT("[StudioViewer] Studio view ON — subject '%s' (radius %.1f cm). Press F to exit."),
        *Subject->GetName(), SubjectRadius);
}

void AStudioViewerTestActor::DeferredEnvironmentCapture()
{
    if (!bStudioActive)
    {
        return;
    }
    bool bCapturePending = false;
    if (USkyLightComponent* SkyComp = StudioSkyComp.Get())
    {
        if (SkyComp->SourceType == SLS_CapturedScene)
        {
            SkyComp->RecaptureSky();
            bCapturePending = true;
        }
    }

    // The emissive panels only exist to be photographed into the environment
    // cubemap; once the capture has been processed they are hidden so the camera
    // never sees floating bright rectangles (the web HDRI is invisible too).
    if (bCapturePending)
    {
        GetWorldTimerManager().SetTimer(PanelHideTimerHandle, this, &AStudioViewerTestActor::HideEnvironmentPanels, 0.25f, false);
    }
    else
    {
        HideEnvironmentPanels();
    }
}

void AStudioViewerTestActor::HideEnvironmentPanels()
{
    for (const TWeakObjectPtr<AActor>& Panel : EnvironmentPanels)
    {
        if (AActor* PanelActor = Panel.Get())
        {
            PanelActor->SetActorHiddenInGame(true);
        }
    }
    EnvironmentPanels.Reset();
}

void AStudioViewerTestActor::ExitStudioView()
{
    UWorld* World = GetWorld();
    if (World)
    {
        if (APlayerController* PC = World->GetFirstPlayerController())
        {
            if (SavedViewTarget.IsValid())
            {
                PC->SetViewTargetWithBlend(SavedViewTarget.Get(), FMath::Max(EnterBlendSeconds, 0.0f), VTBlend_EaseInOut, 2.0f);
            }
            PC->bShowMouseCursor = bSavedShowMouseCursor;
            PC->CurrentMouseCursor = static_cast<EMouseCursor::Type>(SavedMouseCursor);
            PC->DefaultMouseCursor = static_cast<EMouseCursor::Type>(SavedDefaultMouseCursor);
            if (bSavedShowMouseCursor)
            {
                PC->SetInputMode(FInputModeGameAndUI());
            }
            else
            {
                PC->SetInputMode(FInputModeGameOnly());
            }
        }
    }

    RestoreStudioCVars();
    HideHintOverlay();

    // Keep the studio alive while the view blends back, then destroy it.
    DyingStudioActors.Append(StudioActors);
    StudioActors.Reset();
    if (World && EnterBlendSeconds > 0.0f)
    {
        World->GetTimerManager().SetTimer(ExitCleanupTimerHandle, this, &AStudioViewerTestActor::FinishExitCleanup, EnterBlendSeconds + 0.1f, false);
    }
    else
    {
        FinishExitCleanup();
    }

    SubjectCopy = nullptr;
    StudioCamera = nullptr;
    PivotMarker = nullptr;
    StudioSkyComp = nullptr;
    SavedViewTarget = nullptr;

    bStudioActive = false;
    bOrbiting = false;
    bPanning = false;
    YawVelocity = 0.0f;
    PitchVelocity = 0.0f;
    UE_LOG(LogTemp, Warning, TEXT("[StudioViewer] Studio view OFF."));
}

void AStudioViewerTestActor::FinishExitCleanup()
{
    for (AActor* Actor : DyingStudioActors)
    {
        if (IsValid(Actor))
        {
            Actor->Destroy();
        }
    }
    DyingStudioActors.Reset();
}

void AStudioViewerTestActor::ApplyStudioCVars()
{
    SavedCVarValues.Reset();
    for (const FString& Entry : StudioConsoleVariables)
    {
        FString Name, Value;
        if (!Entry.TrimStartAndEnd().Split(TEXT(" "), &Name, &Value))
        {
            continue;
        }
        Name.TrimStartAndEndInline();
        Value.TrimStartAndEndInline();
        IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name);
        if (!CVar)
        {
            UE_LOG(LogTemp, Warning, TEXT("[StudioViewer] Unknown console variable '%s' — skipped."), *Name);
            continue;
        }
        SavedCVarValues.Emplace(Name, CVar->GetString());
        CVar->Set(*Value, ECVF_SetByConsole);
    }
}

void AStudioViewerTestActor::RestoreStudioCVars()
{
    for (const TPair<FString, FString>& Saved : SavedCVarValues)
    {
        if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Saved.Key))
        {
            CVar->Set(*Saved.Value, ECVF_SetByConsole);
        }
    }
    SavedCVarValues.Reset();
}

void AStudioViewerTestActor::ShowHintOverlay()
{
    if (!bShowHintOverlay || HintWidget.IsValid() || !GEngine || !GEngine->GameViewport)
    {
        return;
    }

    const TSharedRef<SOverlay> Overlay =
        SNew(SOverlay)
        // Never intercept clicks/wheel meant for the viewport.
        .Visibility(EVisibility::HitTestInvisible)
        + SOverlay::Slot()
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Bottom)
        .Padding(0.0f, 0.0f, 0.0f, 24.0f)
        [
            SNew(SBorder)
            .BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.4f))
            .Padding(FMargin(14.0f, 6.0f))
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("StudioViewer", "ControlsHint", "Left-drag: orbit     Right/Middle-drag: pan     Wheel: zoom     R: reset     F: exit"))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                .ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.85f))
            ]
        ];

    GEngine->GameViewport->AddViewportWidgetContent(Overlay, 1000);
    HintWidget = Overlay;
}

void AStudioViewerTestActor::HideHintOverlay()
{
    if (HintWidget.IsValid() && GEngine && GEngine->GameViewport)
    {
        GEngine->GameViewport->RemoveViewportWidgetContent(HintWidget.ToSharedRef());
    }
    HintWidget.Reset();
}

// Console fallback in case the F key is consumed by another input mapping.
static FAutoConsoleCommand GStudioToggleCmd(
    TEXT("studio.Toggle"),
    TEXT("Toggles the standalone Studio Viewer test view."),
    FConsoleCommandDelegate::CreateLambda([]()
    {
        if (!GEngine)
        {
            return;
        }
        for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
        {
            UWorld* World = Ctx.World();
            if (!World || (Ctx.WorldType != EWorldType::PIE && Ctx.WorldType != EWorldType::Game))
            {
                continue;
            }
            for (TActorIterator<AStudioViewerTestActor> It(World); It; ++It)
            {
                It->ToggleStudioView();
                return;
            }
        }
    }));
