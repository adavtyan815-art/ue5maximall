// Copyright MaxiMall Project. All Rights Reserved.
// StudioStageActor.cpp — neutral Studio Stage environment for View Mode.
// A faithful port of the proven StudioViewerTest environment (actor-spawned
// backdrop / softboxes / panels / sky light). See StudioStageActor.h.

#include "FurnitureConfigurator/Preview/StudioStageActor.h"
#include "FurnitureConfigurator/Preview/FurniturePreviewActor.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/SkyLight.h"
#include "Engine/RectLight.h"
#include "Engine/Scene.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/PostProcessVolume.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

// Unity-build safety: uniquely named helper (this cpp can be merged with other
// module files, where a generic anonymous-namespace name could collide).
static UStaticMesh* StudioStage_LoadEngineShape(const TCHAR* Path)
{
    return LoadObject<UStaticMesh>(nullptr, Path);
}

AStudioStageActor::AStudioStageActor()
{
    // Client-local presentation only — never replicated.
    bReplicates = false;
    bAlwaysRelevant = false;

    // The stage is the per-frame driver of the studio interaction: a plain C++
    // actor that always ticks, independent of BP_FurniturePreviewActor settings.
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;

    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("StageRoot"));
}

void AStudioStageActor::SetDrivenPreview(AFurniturePreviewActor* InPreview)
{
    DrivenPreview = InPreview;
}

void AStudioStageActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (AFurniturePreviewActor* Preview = DrivenPreview.Get())
    {
        Preview->StudioTickUpdate(DeltaSeconds);
    }
}

void AStudioStageActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(PanelHideTimerHandle);
    }
    TearDownStage();
    Super::EndPlay(EndPlayReason);
}

float AStudioStageActor::ComputeExposureMultiplier() const
{
    // The recipe disables "Apply Physical Camera Exposure", so the renderer's
    // manual exposure reduces to exactly 2^Bias.
    return FMath::Pow(2.0f, LockedEV100);
}

UMaterialInstanceDynamic* AStudioStageActor::MakeUnlitColorMID(const FLinearColor& LinearColor)
{
    // GEngine->EmissiveMeshMaterial is an unlit emissive material driven by the
    // "Color" vector parameter (the engine itself uses it that way).
    UMaterialInterface* Base = GEngine ? GEngine->EmissiveMeshMaterial : nullptr;
    if (!Base)
    {
        UE_LOG(LogTemp, Warning, TEXT("[StudioStage] GEngine->EmissiveMeshMaterial unavailable — stage surfaces keep default materials."));
        return nullptr;
    }
    UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
    if (MID)
    {
        MID->SetVectorParameterValue(TEXT("Color"), LinearColor);
    }
    return MID;
}

void AStudioStageActor::SpawnSoftbox(const FVector& FocusPoint, float SubjectRadius, float YawDeg, float PitchDeg, float Lux, bool bCastShadows)
{
    UWorld* World = GetWorld();
    if (!World || Lux <= 0.0f)
    {
        return;
    }

    const float Distance = FMath::Max(SubjectRadius * LightDistanceFactor, 50.0f);
    const FRotator Dir(PitchDeg, YawDeg, 0.0f);
    const FVector Location = FocusPoint - Dir.Vector() * Distance;
    const float SizeCm = FMath::Max(SubjectRadius * SoftboxSizeFactor, 25.0f);

    // ── Rect light: lux-based, LIGHTING CHANNEL 1 (subject only) ─────────────
    ARectLight* Light = World->SpawnActor<ARectLight>(Location, Dir);
    if (Light)
    {
        if (URectLightComponent* Comp = Cast<URectLightComponent>(Light->GetLightComponent()))
        {
            Comp->SetMobility(EComponentMobility::Movable);
            Comp->SetSourceWidth(SizeCm);
            Comp->SetSourceHeight(SizeCm);
            // Fills/rim stay shadowless (like the web IBL); the key & top may cast
            // soft area shadows for form on diffuse materials (bKeyLightShadows).
            Comp->SetCastShadows(bCastShadows);
            Comp->SetIntensityUnits(ELightUnits::Candelas);
            // I[cd] = E[lux] * d[m]^2 -> the requested illuminance lands on the
            // subject. The global StudioLightIntensityScale multiplies every
            // light identically (ratios preserved).
            const float DistanceMeters = Distance * 0.01f;
            const float BaseIntensity = Lux * FMath::Max(StudioLightIntensityScale, 0.f) * DistanceMeters * DistanceMeters;
            Comp->SetIntensity(BaseIntensity);
            // Registered so SetSubjectLightScale can rescale per focused component.
            RigLightComponents.Add(Comp);
            RigLightBaseIntensities.Add(BaseIntensity);
            Comp->SetLightColor(FLinearColor::White);
            Comp->SetAttenuationRadius(Distance * 4.0f);
            // Subject-only: the preview meshes are on channel 1; the level (channel
            // 0) must never receive stage light. Also keep the rig out of any
            // dynamic GI — its intensities can be large in scene units.
            Comp->LightingChannels.bChannel0 = false;
            Comp->LightingChannels.bChannel1 = true;
            Comp->LightingChannels.bChannel2 = false;
            Comp->IndirectLightingIntensity = 0.0f;
            Comp->MarkRenderStateDirty();
        }
        StageActors.Add(Light);
    }

    // ── Emissive panel: the softbox as seen in reflections ───────────────────
    // Photographed into the sky capture, hidden right after (like the web HDRI:
    // it lights and reflects, but is never directly visible).
    if (UStaticMesh* PlaneMesh = StudioStage_LoadEngineShape(TEXT("/Engine/BasicShapes/Plane.Plane")))
    {
        AStaticMeshActor* Panel = World->SpawnActor<AStaticMeshActor>(Location, Dir + FRotator(90.0f, 0.0f, 0.0f));
        if (Panel)
        {
            Panel->SetMobility(EComponentMobility::Movable);
            UStaticMeshComponent* PanelComp = Panel->GetStaticMeshComponent();
            PanelComp->SetStaticMesh(PlaneMesh);
            PanelComp->SetWorldScale3D(FVector(SizeCm / 100.0f));
            PanelComp->SetCastShadow(false);
            PanelComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            PanelComp->bAffectDynamicIndirectLighting = false; // never feed the level's GI
            // Panel brightness tracks its light's share so reflections stay
            // consistent; the global scale applies here too, so the IBL's
            // ambient/specular follows the rect lights exactly.
            const float PanelLuminance = SoftboxPanelBrightness * FMath::Max(StudioLightIntensityScale, 0.f) *
                FMath::Clamp(Lux / FMath::Max(KeyIlluminanceLux, KINDA_SMALL_NUMBER), 0.05f, 1.0f);
            if (UMaterialInstanceDynamic* PanelMID = MakeUnlitColorMID(FLinearColor::White * PanelLuminance))
            {
                PanelComp->SetMaterial(0, PanelMID);
            }
            // NO-FLASH: hide the panel from the PLAYER'S view from frame one via
            // per-view hiding — the SkyLight capture builds its own views and
            // ignores this list, so the panel is still photographed into the
            // IBL. Without this, the bright fill panel (camera-right) was
            // visible through the opening fade until the post-capture hide —
            // the "flash on the right" when entering ViewMode.
            if (APlayerController* PC = World->GetFirstPlayerController())
            {
                PC->HiddenActors.Add(Panel);
            }
            StageActors.Add(Panel);
            PanelActors.Add(Panel);
        }
    }
}

void AStudioStageActor::BuildStage(const FVector& FocusPoint, float SubjectRadius)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    // Rebuild-safe: product reloads re-size the stage. The guard keeps the
    // OnActorSpawned isolation hook from hiding the stage's own spawns.
    bBuildingStage = true;
    World->GetTimerManager().ClearTimer(PanelHideTimerHandle);
    TearDownStage();
    RigLightComponents.Reset();
    RigLightBaseIntensities.Reset();

    SetActorLocation(FocusPoint);
    ActiveSubjectRadius = FMath::Clamp(SubjectRadius, 25.0f, 2000.0f);
    const float Radius = ActiveSubjectRadius;

    // ── 0. Suspend the level's atmosphere for full isolation ─────────────────
    // Height fog recolors distant unlit surfaces (the proven backdrop bug);
    // clouds/atmosphere could show through any seam. All three are hidden while
    // the stage exists and restored exactly on teardown. See header comment.
    int32 SuspendedCount = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* WorldActor = *It;
        if (!IsValid(WorldActor) || WorldActor->IsHidden() || StageActors.Contains(WorldActor))
        {
            continue;
        }
        const FString ClassName = WorldActor->GetClass()->GetName();
        if (ClassName.Contains(TEXT("ExponentialHeightFog")) ||
            ClassName.Contains(TEXT("VolumetricCloud")) ||
            ClassName.Contains(TEXT("SkyAtmosphere")))
        {
            WorldActor->SetActorHiddenInGame(true);
            SuspendedEnvironmentActors.Add(WorldActor);
            ++SuspendedCount;
        }
    }
    UE_LOG(LogTemp, Log, TEXT("[StudioStage] Suspended %d atmosphere actor(s) for the studio session."), SuspendedCount);

    // ── 1. Backdrop: enclosing box of six inward-facing unlit gray planes ────
    // The original StudioViewerTest used a negative-X-scaled "inside-out" sphere.
    // Diagnostics proved that sphere never renders in the production PIE-client
    // context even though it spawns with a valid mesh and material (the level's
    // sky stayed visible), while POSITIVE-scaled plane actors with the identical
    // material pipeline (the softbox panels) render reliably. So the backdrop is
    // built from that proven combination instead: six one-sided planes facing the
    // center, forming a closed box. Visually identical to the sphere — a flat,
    // unlit #3a3a3a in every direction — and it keeps the same distance rules:
    // close enough (<60 m) that sky-atmosphere aerial perspective (starts ~100 m)
    // contributes nothing and volumetric clouds are occluded, yet always beyond
    // the 3x-fit max zoom distance (floor of 14x the subject radius).
    if (UStaticMesh* PlaneMesh = StudioStage_LoadEngineShape(TEXT("/Engine/BasicShapes/Plane.Plane")))
    {
        const float BackdropHalfSize = FMath::Max(FMath::Min(Radius * 40.0f, 6000.0f), Radius * 14.0f);

        // Compensate the emissive by the inverse exposure so the backdrop lands
        // on-screen at exactly BackdropColorSRGB. It stays in the captured
        // environment AT THE COMPENSATED radiance — and that is the physically
        // consistent choice, not an artifact: captured radiance x exposure ==
        // on-screen backdrop, so a white diffuse surface lit by the IBL alone
        // reads exactly as environment gray (the model-viewer neutral-HDRI
        // behavior). Do not "fix" the capture to the uncompensated color; that
        // would dim the ambient below the environment the user actually sees.
        FLinearColor BackdropLinear = FLinearColor::FromSRGBColor(BackdropColorSRGB);
        const float ExposureMultiplier = ComputeExposureMultiplier();
        if (bCompensateBackdropExposure && ExposureMultiplier > KINDA_SMALL_NUMBER)
        {
            BackdropLinear *= 1.0f / ExposureMultiplier;
        }
        UMaterialInstanceDynamic* BackdropMID = MakeUnlitColorMID(BackdropLinear);

        const FVector FaceDirs[6] =
        {
            FVector( 1, 0, 0), FVector(-1, 0, 0),
            FVector( 0, 1, 0), FVector( 0,-1, 0),
            FVector( 0, 0, 1), FVector( 0, 0,-1)
        };

        int32 SpawnedFaces = 0;
        for (const FVector& Dir : FaceDirs)
        {
            const FVector FaceLoc = FocusPoint + Dir * BackdropHalfSize;
            // The engine plane's face normal is its local +Z: aim it at the center.
            const FRotator FaceRot = FRotationMatrix::MakeFromZ(-Dir).Rotator();
            AStaticMeshActor* Face = World->SpawnActor<AStaticMeshActor>(FaceLoc, FaceRot);
            if (!Face)
            {
                continue;
            }
            Face->SetMobility(EComponentMobility::Movable);
            UStaticMeshComponent* FaceComp = Face->GetStaticMeshComponent();
            FaceComp->SetStaticMesh(PlaneMesh);
            // Plane mesh is 100x100 cm: side length 2 * half-size closes the box.
            FaceComp->SetWorldScale3D(FVector(BackdropHalfSize / 50.0f, BackdropHalfSize / 50.0f, 1.0f));
            FaceComp->SetCastShadow(false);
            FaceComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            FaceComp->bAffectDynamicIndirectLighting = false;
            if (BackdropMID)
            {
                FaceComp->SetMaterial(0, BackdropMID);
            }
            StageActors.Add(Face);
            ++SpawnedFaces;
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[StudioStage] Backdrop: engine Plane mesh could not be loaded."));
    }

    // ── 2. Softbox rig (calibrated against the approved StudioViewerTest) ────
    struct FSoftboxSpec { float Yaw; float Pitch; float Lux; bool bShadows; };
    const FSoftboxSpec Rig[] =
    {
        { 35.0f, -40.0f, KeyIlluminanceLux,                  bKeyLightShadows }, // key, high camera-left
        {-55.0f, -15.0f, KeyIlluminanceLux * FillLeftRatio,  false            }, // fill, camera-right
        {140.0f, -20.0f, KeyIlluminanceLux * FillRightRatio, false            }, // back-left wrap
        {200.0f, -35.0f, KeyIlluminanceLux * RimRatio,       false            }, // rim, behind
        {  0.0f, -89.0f, KeyIlluminanceLux * TopRatio,       bKeyLightShadows }, // overhead soft top
    };
    for (const FSoftboxSpec& Box : Rig)
    {
        SpawnSoftbox(FocusPoint, Radius, Box.Yaw, Box.Pitch, Box.Lux, Box.bShadows);
    }

    // ── 3. SkyLight: the stage IBL (ambient + the only reflection source) ────
    // NOTE: sky lights are scene-global (no lighting channels); while this one is
    // registered it overrides the level's sky light on the scene stack and is
    // restored automatically on destroy. The player only sees the enclosed studio.
    if (ASkyLight* Sky = World->SpawnActor<ASkyLight>(FocusPoint, FRotator::ZeroRotator))
    {
        if (USkyLightComponent* SkyComp = Sky->GetLightComponent())
        {
            SkyComp->SetMobility(EComponentMobility::Movable);
            SkyComp->SourceType = SLS_CapturedScene;
            SkyComp->bRealTimeCapture = false;
            SkyComp->SetCastShadows(false);
            // Everything counts as "sky": the default 1.5 km threshold (used as
            // the capture near plane) would exclude the entire studio.
            SkyComp->SkyDistanceThreshold = 1.0f;
            // The web HDRI lights from below too; a black lower hemisphere kills it.
            SkyComp->bLowerHemisphereIsBlack = false;
            // 1024: sharp panel shapes in chrome/faucet/mirror reflections. The
            // capture runs once per stage build (not per frame), so the only
            // recurring cost is cubemap memory — safe for Pixel Streaming.
            SkyComp->CubemapResolution = 1024;
            SkyComp->SetIntensity(1.0f);
            StageSkyComp = SkyComp;
        }
        StageActors.Add(Sky);
    }

    // Capture one tick later so the whole studio exists before it is photographed.
    World->GetTimerManager().SetTimerForNextTick(this, &AStudioStageActor::DeferredEnvironmentCapture);

    bBuildingStage = false;

    // Whole-level view isolation: nothing outside the studio may ever render
    // for this player while the stage exists (see header).
    ApplyWorldIsolation();

    ShowHintOverlay();

    UE_LOG(LogTemp, Log, TEXT("[StudioStage] Built at %s, subject radius %.1f cm (%d stage actors)."),
        *FocusPoint.ToCompactString(), Radius, StageActors.Num());
}

bool AStudioStageActor::IsStageWhitelisted(const AActor* Actor) const
{
    if (!Actor)
    {
        return false;
    }
    if (Actor == this || StageActors.Contains(Actor))
    {
        return true;
    }
    if (const AFurniturePreviewActor* Preview = DrivenPreview.Get())
    {
        if (Actor == Preview)
        {
            return true;
        }
    }
    return false;
}

void AStudioStageActor::ApplyWorldIsolation()
{
    UWorld* World = GetWorld();
    APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
    if (!World || !PC)
    {
        return;
    }

    int32 NewlyHidden = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* WorldActor = *It;
        if (!IsValid(WorldActor) || IsStageWhitelisted(WorldActor))
        {
            continue;
        }
        // Only actors that can render need to be view-hidden.
        if (!WorldActor->FindComponentByClass<UPrimitiveComponent>())
        {
            continue;
        }
        if (PC->HiddenActors.Contains(WorldActor))
        {
            continue;
        }
        PC->HiddenActors.Add(WorldActor);
        HiddenWorldActors.Add(WorldActor);
        ++NewlyHidden;
    }

    // Anything spawned while the studio is open gets hidden as it appears —
    // future level content never needs to be known in advance.
    if (!ActorSpawnedHandle.IsValid())
    {
        ActorSpawnedHandle = World->AddOnActorSpawnedHandler(
            FOnActorSpawned::FDelegate::CreateUObject(this, &AStudioStageActor::OnWorldActorSpawned));
    }

    UE_LOG(LogTemp, Log, TEXT("[StudioStage] World isolation: view-hid %d level actor(s) for the local player."), NewlyHidden);
}

void AStudioStageActor::OnWorldActorSpawned(AActor* SpawnedActor)
{
    if (bBuildingStage || !IsValid(SpawnedActor) || IsStageWhitelisted(SpawnedActor))
    {
        return;
    }
    UWorld* World = GetWorld();
    APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
    if (!PC || PC->HiddenActors.Contains(SpawnedActor))
    {
        return;
    }
    PC->HiddenActors.Add(SpawnedActor);
    HiddenWorldActors.Add(SpawnedActor);
}

void AStudioStageActor::RemoveWorldIsolation()
{
    UWorld* World = GetWorld();
    if (World && ActorSpawnedHandle.IsValid())
    {
        World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
        ActorSpawnedHandle.Reset();
    }

    APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
    if (PC)
    {
        for (const TWeakObjectPtr<AActor>& Hidden : HiddenWorldActors)
        {
            if (AActor* HiddenActor = Hidden.Get())
            {
                PC->HiddenActors.Remove(HiddenActor);
            }
        }
    }
    HiddenWorldActors.Reset();
}

void AStudioStageActor::DeferredEnvironmentCapture()
{
    if (USkyLightComponent* SkyComp = StageSkyComp.Get())
    {
        SkyComp->RecaptureSky();
        // The emissive panels only exist to be photographed into the environment
        // cubemap; hide them once the capture has been processed so the camera
        // never sees floating bright rectangles.
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().SetTimer(PanelHideTimerHandle, this,
                &AStudioStageActor::HideEnvironmentPanels, 0.3f, false);
        }
    }
}

void AStudioStageActor::HideEnvironmentPanels()
{
    APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    for (const TWeakObjectPtr<AActor>& Panel : PanelActors)
    {
        if (AActor* PanelActor = Panel.Get())
        {
            PanelActor->SetActorHiddenInGame(true);
            // Drop the per-view entry now that the actor is scene-hidden.
            if (PC)
            {
                PC->HiddenActors.Remove(PanelActor);
            }
        }
    }
    PanelActors.Reset();
}

void AStudioStageActor::ApplyCameraRecipe(UCameraComponent* Camera, float ExtraExposureEV) const
{
    if (!IsValid(Camera))
    {
        return;
    }

    Camera->PostProcessBlendWeight = 1.0f;
    FPostProcessSettings& PP = Camera->PostProcessSettings;

    // Fixed exposure, like the web viewer's exposure value. Physical camera
    // exposure is OFF, so the multiplier is exactly 2^(LockedEV100 + ExtraEV) —
    // ExtraEV carries the per-component ExposureCompensation from the preview
    // config, preserving that existing tuning knob.
    PP.bOverride_AutoExposureMethod = true;   PP.AutoExposureMethod = AEM_Manual;
    PP.bOverride_AutoExposureBias = true;     PP.AutoExposureBias = LockedEV100 + ExtraExposureEV;
    PP.bOverride_AutoExposureMinBrightness = true; PP.AutoExposureMinBrightness = 1.0f;
    PP.bOverride_AutoExposureMaxBrightness = true; PP.AutoExposureMaxBrightness = 1.0f;
    PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true; PP.AutoExposureApplyPhysicalCameraExposure = false;

    // Deterministic, model-viewer-style pure IBL for this camera only: no Lumen
    // GI/reflections and no SSR — the stage SkyLight cubemap is the single
    // environment source. Per-view and temporary; the level itself is untouched.
    PP.bOverride_DynamicGlobalIlluminationMethod = true;
    PP.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
    PP.bOverride_ReflectionMethod = true;
    PP.ReflectionMethod = EReflectionMethod::None;

    // TONAL PARITY WITH THE LEVEL VIEW: the default 1.0 runs UE's standard
    // filmic curve — the same response the normal level view uses — so the
    // authored materials keep the tonal character (highlight shoulder, midtone
    // contrast) the user sees in the room. ExpandGamut / BlueCorrection / Bloom
    // are deliberately NOT overridden: the level's PostProcessVolumes stay
    // enabled during ViewMode, so those inherit exactly what the level view
    // renders with. (ToneCurveAmount 0 = web-neutral linear->sRGB, kept as an
    // opt-in knob.)
    PP.bOverride_ToneCurveAmount = true;      PP.ToneCurveAmount = ToneCurveAmount;

    // Lens effects that would differ from a product shot stay off; vignette is
    // the one optional presentation knob.
    PP.bOverride_VignetteIntensity = true;          PP.VignetteIntensity = FMath::Clamp(StudioVignetteIntensity, 0.0f, 1.0f);
    PP.bOverride_SceneFringeIntensity = true;       PP.SceneFringeIntensity = 0.0f;
    PP.bOverride_FilmGrainIntensity = true;         PP.FilmGrainIntensity = 0.0f;
    PP.bOverride_MotionBlurAmount = true;           PP.MotionBlurAmount = 0.0f;
    PP.bOverride_LensFlareIntensity = true;         PP.LensFlareIntensity = 0.0f;

    // Mild SSAO grounds diffuse materials (wood/fabric); the web viewer has none.
    PP.bOverride_AmbientOcclusionIntensity = true;
    PP.AmbientOcclusionIntensity = FMath::Clamp(AmbientOcclusionAmount, 0.0f, 1.0f);
}

void AStudioStageActor::ShowHintOverlay()
{
    // NOTE: use THIS world's viewport, not GEngine->GameViewport — in multi-client
    // PIE the latter is the first instance's window, and the hint would land there.
    UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    if (!bShowHintOverlay || HintWidget.IsValid() || !Viewport)
    {
        return;
    }

    // Russian-only controls hint, bottom-center (compiled with /utf-8).
    // Reset moved from the R key to the overlay's Btn_ResetView UI button.
    const FText HintText = FText::FromString(TEXT(
        "ЛКМ — вращение"
        "      ПКМ/СКМ — перемещение"
        "      Колесо — масштаб"));

    const TSharedRef<SOverlay> Overlay =
        SNew(SOverlay)
        // Never intercept clicks/wheel meant for the viewport or overlay UI.
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
                .Text(HintText)
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                .ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.85f))
            ]
        ];

    Viewport->AddViewportWidgetContent(Overlay, 1000);
    HintWidget = Overlay;
}

void AStudioStageActor::HideHintOverlay()
{
    if (HintWidget.IsValid())
    {
        if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
        {
            Viewport->RemoveViewportWidgetContent(HintWidget.ToSharedRef());
        }
    }
    HintWidget.Reset();
}

void AStudioStageActor::DisableHintOverlay()
{
    // The WBP overlay renders the controls hint itself — the code-Slate fallback
    // must neither show now nor reappear on a stage rebuild.
    bShowHintOverlay = false;
    HideHintOverlay();
}

void AStudioStageActor::SetSubjectLightScale(float Scale)
{
    const float S = FMath::Clamp(Scale, 0.05f, 8.0f);
    for (int32 Index = 0; Index < RigLightComponents.Num(); ++Index)
    {
        if (URectLightComponent* Light = RigLightComponents[Index].Get())
        {
            if (RigLightBaseIntensities.IsValidIndex(Index))
            {
                Light->SetIntensity(RigLightBaseIntensities[Index] * S);
            }
        }
    }
    // The SkyLight scales the captured IBL (ambient + reflections) without any
    // recapture, so the whole subject lighting moves as one.
    if (USkyLightComponent* Sky = StageSkyComp.Get())
    {
        Sky->SetIntensity(S);
    }
}

void AStudioStageActor::TearDownStage()
{
    HideHintOverlay();
    RemoveWorldIsolation();

    // Restore the level's atmosphere exactly as it was.
    for (const TWeakObjectPtr<AActor>& EnvActor : SuspendedEnvironmentActors)
    {
        if (AActor* Restored = EnvActor.Get())
        {
            Restored->SetActorHiddenInGame(false);
        }
    }
    SuspendedEnvironmentActors.Reset();

    for (AActor* Actor : StageActors)
    {
        if (IsValid(Actor))
        {
            Actor->Destroy(); // sky light unregister restores the level's sky
        }
    }
    StageActors.Reset();
    PanelActors.Reset();
    StageSkyComp = nullptr;
}
