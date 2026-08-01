// Copyright MaxiMall Project. All Rights Reserved.
// FurniturePreviewActor.cpp

#include "FurnitureConfigurator/Preview/FurniturePreviewActor.h"
#include "FurnitureConfigurator/ShowroomBooth.h"

#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "Engine/OverlapResult.h"
#include "Engine/RectLight.h"
#include "Engine/DirectionalLight.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/DirectionalLightComponent.h"
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
    PrimaryActorTick.bCanEverTick = false; // Wall occlusion is one-shot at SetFocusComponent time.

    // ── Per-component zoom defaults ────────────────────────────────────────
    // These are the recommended starting values — designers override in the BP
    // Details panel under "Preview Config | Components".
    CabinetConfig.MinZoomDistance    = 60.f;  CabinetConfig.MaxZoomDistance    = 400.f;
    ClosetConfig.MinZoomDistance     = 60.f;  ClosetConfig.MaxZoomDistance     = 400.f;
    CountertopConfig.MinZoomDistance = 30.f;  CountertopConfig.MaxZoomDistance = 250.f;
    SinkConfig.MinZoomDistance       = 20.f;  SinkConfig.MaxZoomDistance       = 200.f;
    FaucetConfig.MinZoomDistance     = 15.f;  FaucetConfig.MaxZoomDistance     = 150.f;
    MirrorConfig.MinZoomDistance     = 30.f;  MirrorConfig.MaxZoomDistance     = 300.f;

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
    SpringArm->TargetArmLength       = CurrentZoomLength;
    SpringArm->bDoCollisionTest      = false;
    SpringArm->bUsePawnControlRotation = false;

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
    Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness = false;
    Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = false;

    // ── Preview Lighting Rig ──────────────────────────────────────────────
    // Key: attached to SpringArm at a FIXED −200cm offset along the arm direction.
    // This means the light orbits 1:1 with the camera (SpringArm rotation) but
    // its distance to the mesh pivot stays CONSTANT regardless of zoom level.
    // Attaching to Camera instead would move the light closer on zoom, causing the
    // attenuation radius boundary to cross the mesh surface (visible hard line).
    // Yaw=180° flips emission toward the mesh (pivot direction).
    PreviewKeyLight = CreateDefaultSubobject<URectLightComponent>(TEXT("PreviewKeyLight"));
    PreviewKeyLight->SetupAttachment(SpringArm);
    PreviewKeyLight->SetMobility(EComponentMobility::Movable);
    PreviewKeyLight->SetRelativeLocation(FVector(-200.f, 0.f, 0.f)); // 200cm toward camera from pivot
    PreviewKeyLight->SetRelativeRotation(FRotator(0.f, 180.f, 0.f)); // face toward mesh (pivot)
    PreviewKeyLight->SetIntensity(0.f);      // Off by default — Lumen handles lighting.
    PreviewKeyLight->SetLightColor(FLinearColor::White);
    PreviewKeyLight->SourceWidth  = 80.f;
    PreviewKeyLight->SourceHeight = 100.f;
    PreviewKeyLight->AttenuationRadius = 800.f;
    PreviewKeyLight->bUseTemperature = false;
    PreviewKeyLight->SetCastShadows(false);
    PreviewKeyLight->SetVisibility(false); // shown only during active preview

    // Fill: attached to SpringArm pivot (local Pitch=+35°, Yaw=170°).
    // Stays at the focus pivot and rotates as the SpringArm rotates,
    // so it provides backfill from below/behind the camera orbit.
    PreviewFillLight = CreateDefaultSubobject<URectLightComponent>(TEXT("PreviewFillLight"));
    PreviewFillLight->SetupAttachment(SpringArm);
    PreviewFillLight->SetMobility(EComponentMobility::Movable);
    PreviewFillLight->SetRelativeRotation(FRotator(35.f, 170.f, 0.f));
    PreviewFillLight->SetIntensity(320.f);
    PreviewFillLight->SetLightColor(FLinearColor::White);
    PreviewFillLight->SourceWidth  = 120.f;
    PreviewFillLight->SourceHeight = 150.f;
    PreviewFillLight->SetCastShadows(false);
    PreviewFillLight->SetVisibility(false);

    // Rim / Top: attached to SpringArm pivot, Pitch=−80° (pointing sharply downward).
    // Acts as a top-down edge/rim light regardless of camera orbit angle.
    PreviewRimLight = CreateDefaultSubobject<URectLightComponent>(TEXT("PreviewRimLight"));
    PreviewRimLight->SetupAttachment(SpringArm);
    PreviewRimLight->SetMobility(EComponentMobility::Movable);
    PreviewRimLight->SetRelativeRotation(FRotator(-80.f, 0.f, 0.f));
    PreviewRimLight->SetIntensity(200.f);
    PreviewRimLight->SetLightColor(FLinearColor::White);
    PreviewRimLight->SourceWidth  = 60.f;
    PreviewRimLight->SourceHeight = 60.f;
    PreviewRimLight->SetCastShadows(false);
    PreviewRimLight->SetVisibility(false);

    // ── Studio SkyLight ─────────────────────────────────────────────────────
    // One-shot captured scene, enabled only during active preview.
    // Provides true 360° diffuse ambient fill from all directions, solving the
    // pitch-black back-side artifact caused by WIP_UpdateWallOcclusion removing
    // Lumen bounce surfaces. bRealTimeCapture=false: zero per-frame overhead.
    PreviewSkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("PreviewSkyLight"));
    PreviewSkyLight->SetupAttachment(PreviewRoot);
    PreviewSkyLight->SetMobility(EComponentMobility::Movable);
    PreviewSkyLight->SourceType       = ESkyLightSourceType::SLS_CapturedScene;
    PreviewSkyLight->bRealTimeCapture = false;    // single RecaptureSky() at preview start
    PreviewSkyLight->SetIntensity(2.f);
    PreviewSkyLight->SetLightColor(FLinearColor::White);
    PreviewSkyLight->SetCastShadows(false);
    PreviewSkyLight->SetVisibility(false);        // hidden until preview is active

    // ── Studio Directional Key Light (Camera Headlight / View Light) ────────
    // Attached directly to Camera Component with a strict local rotation offset.
    // Moves and rotates 1:1 with camera location and view rotation, ensuring
    // whichever face the camera looks at (horizontal, from above, or from below)
    // is always illuminated with rich material highlights.
    PreviewDirectionalLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("PreviewDirectionalLight"));
    PreviewDirectionalLight->SetupAttachment(Camera);
    PreviewDirectionalLight->SetMobility(EComponentMobility::Movable);
    PreviewDirectionalLight->SetRelativeRotation(FRotator(-15.f, 15.f, 0.f)); // local offset relative to camera forward vector
    PreviewDirectionalLight->SetIntensity(8.f);
    PreviewDirectionalLight->SetLightColor(FLinearColor(1.f, 0.95f, 0.85f)); // warm sunlight tint
    PreviewDirectionalLight->SetCastShadows(false);
    PreviewDirectionalLight->SetVisibility(false);        // hidden until preview is active
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void AFurniturePreviewActor::BeginPlay()
{
    Super::BeginPlay();
    if (IsValid(SpringArm))
    {
        SpringArm->TargetArmLength = CurrentZoomLength;
    }
}

void AFurniturePreviewActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // ── Remove stencil isolation material from camera blendables ──────────
    if (IsValid(Camera))
    {
        Camera->PostProcessSettings.WeightedBlendables.Array.RemoveAll(
            [this](const FWeightedBlendable& B)
            {
                return B.Object == StencilIsolationMID || B.Object == StencilIsolationMaterialParent;
            });
    }

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

    // ── Restore all world components that were hidden during preview ───────
    for (const TWeakObjectPtr<UPrimitiveComponent>& CompPtr : WIP_CachedHiddenWallComponents)
    {
        if (CompPtr.IsValid())
        {
            CompPtr->SetVisibility(true);
            CompPtr->SetCastShadow(true);
        }
    }
    WIP_CachedHiddenWallComponents.Empty();

    // ── Clear custom depth and restore default CastShadow settings ────────
    auto RestoreState = [](UStaticMeshComponent* C, bool bDefaultShadow)
    {
        if (IsValid(C))
        {
            C->SetRenderCustomDepth(false);
            C->SetCastShadow(bDefaultShadow);
            C->SetCastHiddenShadow(false);
        }
    };
    RestoreState(CabinetMesh.Get(),          CabinetConfig.bCastShadow);
    RestoreState(DoorMeshSlot0.Get(),        CabinetConfig.bCastShadow);
    RestoreState(DoorMeshSlot1.Get(),        CabinetConfig.bCastShadow);
    RestoreState(CountertopMesh.Get(),       CountertopConfig.bCastShadow);
    RestoreState(SinkMesh.Get(),             SinkConfig.bCastShadow);
    RestoreState(FaucetMesh.Get(),           FaucetConfig.bCastShadow);
    RestoreState(MirrorMesh.Get(),           MirrorConfig.bCastShadow);
    RestoreState(ClosetMesh.Get(),           ClosetConfig.bCastShadow);
    RestoreState(ClosetDoorMeshSlot0.Get(),  ClosetConfig.bCastShadow);
    RestoreState(ClosetDoorMeshSlot1.Get(),  ClosetConfig.bCastShadow);

    // ── Hide preview lights ───────────────────────────────────────────────
    if (IsValid(PreviewKeyLight))          { PreviewKeyLight->SetVisibility(false); }
    if (IsValid(PreviewFillLight))         { PreviewFillLight->SetVisibility(false); }
    if (IsValid(PreviewRimLight))          { PreviewRimLight->SetVisibility(false); }
    if (IsValid(PreviewSkyLight))          { PreviewSkyLight->SetVisibility(false); }
    if (IsValid(PreviewDirectionalLight))  { PreviewDirectionalLight->SetVisibility(false); }

    // ── Restore hidden world Rect Light actors ────────────────────────────
    for (const TWeakObjectPtr<AActor>& LightPtr : WIP_CachedWorldRectLights)
    {
        if (LightPtr.IsValid())
        {
            LightPtr->SetActorHiddenInGame(false);
        }
    }
    WIP_CachedWorldRectLights.Empty();

    // ── Restore world ADirectionalLight intensities ────────────────────────
    for (const auto& Pair : WIP_CachedWorldDirLights)
    {
        if (Pair.Key.IsValid() && Pair.Key->GetLightComponent())
        {
            Pair.Key->GetLightComponent()->SetIntensity(Pair.Value);
        }
    }
    WIP_CachedWorldDirLights.Empty();

    Super::EndPlay(EndPlayReason);
}

// ─────────────────────────────────────────────────────────────────────────────
// DeferredHideWorldLights
// Tick N+1 callback scheduled by LoadProductPreview.
// At this point the GPU has already executed the SkyLight cubemap capture from
// the fully-lit Tick N scene. Now it is safe to hide world actors / lights.
// ─────────────────────────────────────────────────────────────────────────────

void AFurniturePreviewActor::DeferredHideWorldLights()
{
    // ── Hide SourceBooth (no longer needed visually; preview meshes take its place) ──
    if (AShowroomBooth* Booth = WIP_DeferredSourceBooth.Get())
    {
        WIP_CachedSourceBooth = Booth;
        Booth->SetActorHiddenInGame(true);
    }
    WIP_DeferredSourceBooth.Reset();

    // ── Hide all world ARectLight actors ──────────────────────────────────────
    // Prevents world Rect Lights from double-lighting the focused mesh.
    WIP_CachedWorldRectLights.Empty();
    if (UWorld* W = GetWorld())
    {
        for (TActorIterator<ARectLight> It(W); It; ++It)
        {
            ARectLight* RLActor = *It;
            if (IsValid(RLActor) && !RLActor->IsHidden())
            {
                WIP_CachedWorldRectLights.Add(RLActor);
                RLActor->SetActorHiddenInGame(true);
            }
        }
    }

    // ── Zero world directional light intensities ──────────────────────────────
    // Cached properties were already read in LoadProductPreview (Tick N).
    // We only zero the intensity here so our PreviewDirectionalLight takes over.
    for (const auto& Pair : WIP_CachedWorldDirLights)
    {
        if (Pair.Key.IsValid() && Pair.Key->GetLightComponent())
        {
            Pair.Key->GetLightComponent()->SetIntensity(0.f);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[PreviewActor] DeferredHideWorldLights executed on Tick N+1. "
        "SkyLight cubemap was captured from warm scene on Tick N."));
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadProductPreview
// ─────────────────────────────────────────────────────────────────────────────

void AFurniturePreviewActor::LoadProductPreview(const FFurnitureProductRow& ProductData,
                                                const FShowroomBoothConfigState& ActiveState,
                                                AShowroomBooth* SourceBooth)
{
    // ── TWO-TICK SKYLIGHT CAPTURE FIX ──────────────────────────────────────
    //
    // RecaptureSky() does NOT capture immediately. It only sets a dirty flag:
    //     bCaptureDirty = true;  MarkRenderStateDirty();
    // The actual GPU cubemap generation happens at the END OF THE RENDER PASS,
    // which runs AFTER all C++ code in this game tick has finished.
    //
    // Previous approach (all hiding synchronous): every operation after
    // RecaptureSky() still ran in the SAME tick, so the GPU always saw a
    // stripped scene (booth hidden, rect lights off, sun zeroed) — a grey void.
    //
    // Correct approach (two ticks):
    //   Tick N  (this function): RecaptureSky() → GPU captures fully-lit scene
    //   Tick N+1 (deferred):     Hide booth, disable rect lights, zero world sun
    //
    // This guarantees the SkyLight cubemap is built from the warm unmodified
    // room, then the scene is cleaned up for preview on the following tick.

    // TICK N — Step 1: Position SkyLight at the furniture's world location so it
    // captures the correct indoor environment (not an unrelated part of the level).
    if (IsValid(SourceBooth) && IsValid(PreviewSkyLight))
    {
        PreviewSkyLight->SetWorldLocation(SourceBooth->GetActorLocation());
    }

    // TICK N — Step 2: Cache world sun properties NOW (before zeroing on next tick)
    // so SetFocusComponent() can read them correctly when it runs.
    WIP_CachedWorldDirLights.Empty();
    WIP_CachedWorldSunRotation  = FRotator(-46.f, -46.f, 0.f);
    WIP_CachedWorldSunIntensity = 8.f;
    WIP_CachedWorldSunColor     = FLinearColor(1.f, 0.95f, 0.85f);
    WIP_CachedWorldSunUseTemp   = false;
    WIP_CachedWorldSunTemp      = 6500.f;
    WIP_CachedWorldSunIndirect  = 1.0f;
    if (UWorld* W = GetWorld())
    {
        for (TActorIterator<ADirectionalLight> It(W); It; ++It)
        {
            ADirectionalLight* DLActor = *It;
            if (IsValid(DLActor) && !DLActor->IsHidden() && DLActor->GetLightComponent())
            {
                ULightComponent* LightComp = DLActor->GetLightComponent();
                WIP_CachedWorldDirLights.Emplace(DLActor, LightComp->Intensity);
                WIP_CachedWorldSunRotation  = DLActor->GetActorRotation();
                WIP_CachedWorldSunIntensity = LightComp->Intensity;
                WIP_CachedWorldSunColor     = LightComp->GetLightColor();
                WIP_CachedWorldSunUseTemp   = LightComp->bUseTemperature;
                WIP_CachedWorldSunTemp      = LightComp->Temperature;
                WIP_CachedWorldSunIndirect  = LightComp->IndirectLightingIntensity;
                // NOTE: Do NOT zero intensity here — that happens in DeferredHideWorldLights.
            }
        }
    }

    // TICK N — Step 3: Schedule the GPU capture (executes at end of this tick's render pass).
    if (IsValid(PreviewSkyLight))
    {
        PreviewSkyLight->SetVisibility(true);
        PreviewSkyLight->RecaptureSky(); // GPU captures AFTER this tick ends, BEFORE next tick begins
    }

    // TICK N — Step 4: Store SourceBooth for DeferredHideWorldLights (runs next tick).
    WIP_DeferredSourceBooth = SourceBooth;
    WIP_CachedWorldRectLights.Empty(); // Will be populated in deferred step

    // Schedule all hiding for Tick N+1 so the GPU capture sees the clean scene.
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().SetTimerForNextTick(this, &AFurniturePreviewActor::DeferredHideWorldLights);
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
}

// ─────────────────────────────────────────────────────────────────────────────
// SetFocusComponent
//
// Unified entry point for:
//   1. Component isolation  — hide all groups, show only the focused one.
//   2. Per-component config — pull zoom limits and cast shadow from FPreviewComponentConfig.
//   3. Stencil-250 isolation — apply CustomDepth on the focused group.
//   4. SpringArm pivot      — move to the focused mesh's bounds center.
//   5. Wall occlusion       — one-shot sphere overlap, hides everything in orbit volume.
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
    ActiveMinZoom = Config ? Config->MinZoomDistance : 30.f;
    ActiveMaxZoom = Config ? Config->MaxZoomDistance : 500.f;

    // ── 1. Component isolation and CastShadow configuration ─────────────
    // Hidden meshes MUST have SetCastShadow(false) so they don't cast shadow artifacts onto active preview objects.
    auto ApplyMeshState = [](UStaticMeshComponent* C, bool bShow, bool bCastShadow)
    {
        if (IsValid(C))
        {
            const bool bHasMesh = IsValid(C->GetStaticMesh());
            const bool bFinalVisibility = bShow && bHasMesh;
            C->SetVisibility(bFinalVisibility);
            C->SetCastShadow(bFinalVisibility ? bCastShadow : false);
            C->SetCastHiddenShadow(false);
        }
    };

    const bool bShowAll = (TargetType == EFurnitureComponentType::None);

    const bool bCabinetShadow    = CabinetConfig.bCastShadow;
    const bool bClosetShadow     = ClosetConfig.bCastShadow;
    const bool bCountertopShadow = CountertopConfig.bCastShadow;
    const bool bSinkShadow       = SinkConfig.bCastShadow;
    const bool bFaucetShadow     = FaucetConfig.bCastShadow;
    const bool bMirrorShadow     = MirrorConfig.bCastShadow;

    // First hide & disable shadows for all preview meshes.
    ApplyMeshState(CabinetMesh.Get(),          bShowAll, bCabinetShadow);
    ApplyMeshState(DoorMeshSlot0.Get(),        bShowAll, bCabinetShadow);
    ApplyMeshState(DoorMeshSlot1.Get(),        bShowAll, bCabinetShadow);
    ApplyMeshState(CountertopMesh.Get(),       bShowAll, bCountertopShadow);
    ApplyMeshState(SinkMesh.Get(),             bShowAll, bSinkShadow);
    ApplyMeshState(FaucetMesh.Get(),           bShowAll, bFaucetShadow);
    ApplyMeshState(MirrorMesh.Get(),           bShowAll, bMirrorShadow);
    ApplyMeshState(ClosetMesh.Get(),           bShowAll, bClosetShadow);
    ApplyMeshState(ClosetDoorMeshSlot0.Get(),  bShowAll, bClosetShadow);
    ApplyMeshState(ClosetDoorMeshSlot1.Get(),  bShowAll, bClosetShadow);

    // Reveal and enable shadows ONLY for the active focus group.
    if (!bShowAll)
    {
        switch (TargetType)
        {
        case EFurnitureComponentType::Cabinet:
            ApplyMeshState(CabinetMesh.Get(),   true, bCabinetShadow);
            ApplyMeshState(DoorMeshSlot0.Get(), true, bCabinetShadow);
            ApplyMeshState(DoorMeshSlot1.Get(), true, bCabinetShadow);
            break;
        case EFurnitureComponentType::Closet:
            ApplyMeshState(ClosetMesh.Get(),          true, bClosetShadow);
            ApplyMeshState(ClosetDoorMeshSlot0.Get(), true, bClosetShadow);
            ApplyMeshState(ClosetDoorMeshSlot1.Get(), true, bClosetShadow);
            break;
        case EFurnitureComponentType::Countertop:
            ApplyMeshState(CountertopMesh.Get(), true, bCountertopShadow);
            break;
        case EFurnitureComponentType::Sink:
            ApplyMeshState(SinkMesh.Get(), true, bSinkShadow);
            break;
        case EFurnitureComponentType::Faucet:
            ApplyMeshState(FaucetMesh.Get(), true, bFaucetShadow);
            break;
        case EFurnitureComponentType::Mirror:
            ApplyMeshState(MirrorMesh.Get(), true, bMirrorShadow);
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

    // ── 4. Stencil-250 isolation: clear all, then set focused group ───────
    auto SetDepth = [](UStaticMeshComponent* C, bool bEnable, int32 Val = 250)
    {
        if (IsValid(C) && C->GetVisibleFlag() && IsValid(C->GetStaticMesh()))
        {
            C->SetRenderCustomDepth(bEnable);
            if (bEnable) { C->SetCustomDepthStencilValue(Val); }
        }
    };
    SetDepth(CabinetMesh.Get(),          false);
    SetDepth(ClosetMesh.Get(),           false);
    SetDepth(CountertopMesh.Get(),       false);
    SetDepth(SinkMesh.Get(),             false);
    SetDepth(FaucetMesh.Get(),           false);
    SetDepth(MirrorMesh.Get(),           false);
    SetDepth(DoorMeshSlot0.Get(),        false);
    SetDepth(DoorMeshSlot1.Get(),        false);
    SetDepth(ClosetDoorMeshSlot0.Get(),  false);
    SetDepth(ClosetDoorMeshSlot1.Get(),  false);

    if (IsValid(TargetComp))
    {
        SetDepth(TargetComp, true, 250);

        if (TargetType == EFurnitureComponentType::Cabinet)
        {
            SetDepth(DoorMeshSlot0.Get(),  true, 250);
            SetDepth(DoorMeshSlot1.Get(),  true, 250);
        }
        else if (TargetType == EFurnitureComponentType::Closet)
        {
            SetDepth(ClosetDoorMeshSlot0.Get(), true, 250);
            SetDepth(ClosetDoorMeshSlot1.Get(), true, 250);
        }
    }

    // ── 5. Reset orbit accumulators ───────────────────────────────────────
    WorldInPlaceYaw   = 0.f;
    WorldInPlacePitch = 0.f;

    if (IsValid(MeshRoot))
    {
        MeshRoot->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
    }

    // ── 6. Compute focus pivot and adaptive initial view distance ─────────
    FVector FocusPivot = WIP_GetFocusPivotWorld();
    float MeshRadius = 80.f;
    if (IsValid(TargetComp) && TargetComp->GetStaticMesh())
    {
        MeshRadius = FMath::Max(15.f, TargetComp->Bounds.SphereRadius);
    }
    WIP_MeshBoundsRadius = MeshRadius;
    WIP_FocusPivotWorld  = FocusPivot;

    // Cache initial MeshRoot state and pivot for exact rotation around bounds center
    if (IsValid(MeshRoot))
    {
        MeshRoot->SetRelativeRotation(FRotator::ZeroRotator);
        WIP_MeshRootLocAtReset  = MeshRoot->GetComponentLocation();
        WIP_InitialMeshRootQuat = MeshRoot->GetComponentQuat();
        WIP_MeshPivotWorld      = FocusPivot;
    }

    // Adaptive initial distance: 2.5× the mesh radius, clamped to per-component limits.
    const float AdaptiveDist = FMath::Clamp(MeshRadius * 2.5f, ActiveMinZoom, ActiveMaxZoom);
    WIP_CurrentViewDist = AdaptiveDist;
    WIP_InitialViewDist = AdaptiveDist;
    CurrentZoomLength   = AdaptiveDist;

    // ── 7. FIX 2: Canonical initial orbit rotation (player-position-independent)
    // Previously derived from the player camera's live world rotation, which caused
    // the PreviewDirectionalLight (camera-attached) to point in a different world
    // direction from every player position — producing color shifts and random
    // shower shadows depending on where the client stood in the room.
    //
    // We now force a fixed canonical orientation: the camera always enters preview
    // facing the mesh from world -Y (Yaw=0, looking toward +Y / "north" in UE coords).
    // This means:
    //   • PreviewDirectionalLight world direction = canonical + DLRelRot = identical every time.
    //   • Color appearance of the cabinet is the same regardless of player position.
    //   • Shower / room shadows are fully deterministic and can be tuned once.
    //
    // The user rotates the MESH (not the camera) during preview, so this fixed
    // starting angle does not restrict 360° inspection.
    WIP_InitialOrbitRot = FRotator(0.f, 0.f, 0.f); // Camera looks toward world +Y

    // ── 8. Position SpringArm at pivot ───────────────────────────────────
    if (IsValid(SpringArm))
    {
        SpringArm->bDoCollisionTest        = false;
        SpringArm->bUsePawnControlRotation = false;
        SpringArm->SetWorldLocation(FocusPivot);
        SpringArm->SetWorldRotation(WIP_InitialOrbitRot);
        SpringArm->TargetArmLength         = AdaptiveDist;
        SpringArm->UpdateChildTransforms();
    }

    // ── 9. Hide character mesh ────────────────────────────────────────────
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

    // ── 10. Wall occlusion (one-shot sphere overlap) ──────────────────────
    // Must be called AFTER WIP_FocusPivotWorld and ActiveMaxZoom are set.
    WIP_UpdateWallOcclusion();

    // ── 11. Apply per-component preview lighting config ───────────────────
    if (IsValid(PreviewKeyLight) && IsValid(PreviewFillLight) && IsValid(PreviewRimLight))
    {
        const float KeyIntensity   = Config ? Config->KeyLightIntensity          : 0.f;
        const float FillRimMult    = Config ? Config->FillRimMultiplier          : 0.4f;
        const FLinearColor LColor  = Config ? Config->LightColor                 : FLinearColor::White;
        const float SrcW           = Config ? Config->LightSourceWidth           : 80.f;
        const float SrcH           = Config ? Config->LightSourceHeight          : 100.f;
        const float KeyOffset      = Config ? Config->KeyLightOffset             : 200.f;
        const float AttenuRadius   = Config ? Config->KeyLightAttenuationRadius  : 800.f;
        const bool bCastShadows    = Config ? Config->bPreviewLightCastShadows   : false;

        // Key Light: fixed orbit offset + user-configured attenuation radius.
        PreviewKeyLight->SetRelativeLocation(FVector(-KeyOffset, 0.f, 0.f));
        PreviewKeyLight->AttenuationRadius = AttenuRadius;
        PreviewKeyLight->MarkRenderStateDirty();
        PreviewKeyLight->SetIntensity(KeyIntensity);
        PreviewKeyLight->SetLightColor(LColor);
        PreviewKeyLight->SourceWidth  = SrcW;
        PreviewKeyLight->SourceHeight = SrcH;
        PreviewKeyLight->SetCastShadows(bCastShadows);
        PreviewKeyLight->SetVisibility(KeyIntensity > 0.f);

        // Fill Light: scaled fraction of key, always shadow-free.
        PreviewFillLight->SetIntensity(KeyIntensity * FillRimMult);
        PreviewFillLight->SetLightColor(LColor);
        PreviewFillLight->SourceWidth  = SrcW * 1.5f;
        PreviewFillLight->SourceHeight = SrcH * 1.5f;
        PreviewFillLight->SetCastShadows(false);
        PreviewFillLight->SetVisibility(KeyIntensity > 0.f);

        // Rim / Top Light: scaled fraction, always shadow-free.
        PreviewRimLight->SetIntensity(KeyIntensity * FillRimMult * 0.6f);
        PreviewRimLight->SetLightColor(LColor);
        PreviewRimLight->SourceWidth  = SrcW * 0.75f;
        PreviewRimLight->SourceHeight = SrcH * 0.75f;
        PreviewRimLight->SetCastShadows(false);
        PreviewRimLight->SetVisibility(KeyIntensity > 0.f);
    }

    // ── 11b. Per-component camera exposure compensation ───────────────────
    // Non-destructive brightness control: uses existing Lumen GI, preserves
    // AO and normal map depth. Preferred over Rect Lights for most meshes.
    if (IsValid(Camera))
    {
        const float ExpComp = Config ? Config->ExposureCompensation : 0.f;
        FPostProcessSettings& PP = Camera->PostProcessSettings;
        if (FMath::Abs(ExpComp) > KINDA_SMALL_NUMBER)
        {
            PP.bOverride_AutoExposureBias = true;
            PP.AutoExposureBias           = ExpComp;
        }
        else
        {
            // Restore auto-exposure to level defaults.
            PP.bOverride_AutoExposureBias = false;
            PP.AutoExposureBias           = 0.f;
        }
    }

    // ── 12. Studio SkyLight per-component config ──────────────────────────
    // Intensity and color are set here so each component can tune the ambient
    // fill independently. The SkyLight was already activated and recaptured in
    // LoadProductPreview; we only update its parameters now.
    if (IsValid(PreviewSkyLight))
    {
        const float SLIntensity    = Config ? Config->SkyLightIntensity : 2.f;
        const FLinearColor SLColor = Config ? Config->SkyLightColor     : FLinearColor::White;
        PreviewSkyLight->SetIntensity(SLIntensity);
        PreviewSkyLight->SetLightColor(SLColor);
        PreviewSkyLight->SetVisibility(SLIntensity > 0.f);
    }

    // ── 13. Studio Directional Key Light per-component config ─────────────
    if (IsValid(PreviewDirectionalLight))
    {
        PreviewDirectionalLight->SetMobility(EComponentMobility::Movable);

        const bool bUseWorldDefaults = Config ? Config->bUseWorldSunDefaults : true;

        // When bUseWorldSunDefaults is true: inherit ALL settings from the level's main world sun.
        // When false: use initial world sun values as base, but apply user overrides from Config.
        const float DLIntensity     = (Config && !bUseWorldDefaults) ? Config->DirectionalLightIntensity        : WIP_CachedWorldSunIntensity;
        const FLinearColor DLColor  = (Config && !bUseWorldDefaults) ? Config->DirectionalLightColor            : WIP_CachedWorldSunColor;
        const FRotator DLRelRot     = (Config && !bUseWorldDefaults) ? Config->DirectionalLightRelativeRotation : FRotator::ZeroRotator;
        const bool bDLShadows       = (Config && !bUseWorldDefaults) ? Config->bDirectionalLightCastShadows     : false;

        // Pure Camera Origin Attachment: Attached to Camera Component with DLRelRot
        if (IsValid(Camera))
        {
            PreviewDirectionalLight->AttachToComponent(Camera, FAttachmentTransformRules::SnapToTargetIncludingScale);
            PreviewDirectionalLight->SetRelativeLocation(FVector::ZeroVector);
            PreviewDirectionalLight->SetRelativeRotation(DLRelRot);
        }

        PreviewDirectionalLight->SetIntensity(DLIntensity);
        PreviewDirectionalLight->SetLightColor(DLColor);
        if (bUseWorldDefaults)
        {
            PreviewDirectionalLight->SetUseTemperature(WIP_CachedWorldSunUseTemp);
            PreviewDirectionalLight->SetTemperature(WIP_CachedWorldSunTemp);
            PreviewDirectionalLight->SetIndirectLightingIntensity(WIP_CachedWorldSunIndirect);
        }
        PreviewDirectionalLight->SetCastShadows(bDLShadows);
        PreviewDirectionalLight->SetVisibility(DLIntensity > 0.f);

        UE_LOG(LogTemp, Warning, TEXT("[PreviewDirLight SETUP] UseWorldDefaults=%d | CachedWorldIntensity=%.2f | ConfigIntensity=%.2f | FinalIntensity=%.2f | CastShadows=%d | RelRot=%s | WorldRot=%s | ForwardDir=%s"),
            bUseWorldDefaults ? 1 : 0,
            WIP_CachedWorldSunIntensity,
            Config ? Config->DirectionalLightIntensity : -1.f,
            DLIntensity,
            bDLShadows ? 1 : 0,
            *PreviewDirectionalLight->GetRelativeRotation().ToString(),
            *PreviewDirectionalLight->GetComponentRotation().ToString(),
            *PreviewDirectionalLight->GetForwardVector().ToString());

        if (IsValid(PreviewSkyLight))
        {
            UE_LOG(LogTemp, Warning, TEXT("[PreviewSkyLight SETUP] Mobility=%d | Intensity=%.2f | Visibility=%d"),
                (int32)PreviewSkyLight->Mobility, PreviewSkyLight->Intensity, PreviewSkyLight->IsVisible() ? 1 : 0);
        }

        if (IsValid(CurrentFocusedComponent) && CurrentFocusedComponent->GetStaticMesh())
        {
            UStaticMesh* MeshAsset = CurrentFocusedComponent->GetStaticMesh();
            UE_LOG(LogTemp, Warning, TEXT("[PreviewMesh DIAG] FocusedMesh=%s | NumMaterials=%d | NumLODs=%d"),
                *MeshAsset->GetName(), CurrentFocusedComponent->GetNumMaterials(), MeshAsset->GetNumLODs());
            for (int32 MatIdx = 0; MatIdx < CurrentFocusedComponent->GetNumMaterials(); ++MatIdx)
            {
                UMaterialInterface* Mat = CurrentFocusedComponent->GetMaterial(MatIdx);
                if (Mat)
                {
                    UE_LOG(LogTemp, Warning, TEXT("[PreviewMesh DIAG] Mat[%d]=%s | TwoSided=%d"),
                        MatIdx, *Mat->GetName(), Mat->IsTwoSided() ? 1 : 0);
                }
            }
        }
    }

    // ── 14. Stencil isolation post-process material ───────────────────────
    WIP_ApplyStencilIsolation();
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera orbit and zoom
// ─────────────────────────────────────────────────────────────────────────────

void AFurniturePreviewActor::RotatePreview(float DeltaYaw, float DeltaPitch)
{
    if (!IsValid(MeshRoot) || !IsValid(Camera)) { return; }

    WorldInPlaceYaw   -= DeltaYaw;
    WorldInPlacePitch  = FMath::Clamp(WorldInPlacePitch + DeltaPitch, -80.f, 80.f);

    // Compute Camera Right vector (flattened to Z=0 plane to prevent roll)
    FVector CamRight = Camera->GetRightVector();
    CamRight.Z = 0.f;
    CamRight.Normalize();
    if (CamRight.IsNearlyZero())
    {
        CamRight = FVector::RightVector;
    }

    // Yaw (left/right drag) around World Z (UpVector)
    // Pitch (up/down drag) around Camera Right vector
    FQuat YawQ        = FQuat(FVector::UpVector, FMath::DegreesToRadians(WorldInPlaceYaw));
    FQuat PitchQ      = FQuat(CamRight,           FMath::DegreesToRadians(WorldInPlacePitch));
    FQuat TotalDeltaQ = PitchQ * YawQ;

    // Combine delta rotation with initial MeshRoot orientation
    FQuat NewQuat = TotalDeltaQ * WIP_InitialMeshRootQuat;

    // Rotate MeshRoot location around WIP_MeshPivotWorld (the mesh's actual visual center)
    FVector NewLoc = WIP_MeshPivotWorld + TotalDeltaQ.RotateVector(WIP_MeshRootLocAtReset - WIP_MeshPivotWorld);
    MeshRoot->SetWorldLocationAndRotation(NewLoc, NewQuat);
}

void AFurniturePreviewActor::ResetRotation()
{
    WorldInPlaceYaw     = 0.f;
    WorldInPlacePitch   = 0.f;
    WIP_CurrentViewDist = WIP_InitialViewDist;
    CurrentZoomLength   = WIP_InitialViewDist;

    if (IsValid(MeshRoot))
    {
        MeshRoot->SetWorldLocationAndRotation(WIP_MeshRootLocAtReset, WIP_InitialMeshRootQuat);
    }

    if (IsValid(SpringArm))
    {
        SpringArm->SetWorldLocation(WIP_FocusPivotWorld);
        SpringArm->SetWorldRotation(WIP_InitialOrbitRot);
        SpringArm->TargetArmLength = WIP_InitialViewDist;
    }

    WIP_ApplyStencilIsolation();
}

void AFurniturePreviewActor::ZoomPreview(float DeltaZoom)
{
    if (!IsValid(SpringArm)) { return; }

    // Clamp against the ACTIVE component's configured limits.
    CurrentZoomLength   = FMath::Clamp(CurrentZoomLength + DeltaZoom, ActiveMinZoom, ActiveMaxZoom);
    WIP_CurrentViewDist = CurrentZoomLength;
    SpringArm->TargetArmLength = CurrentZoomLength;
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

void AFurniturePreviewActor::WIP_ApplyStencilIsolation()
{
    if (!IsValid(Camera)) { return; }

    // Lazy-create the dynamic material instance.
    if (IsValid(StencilIsolationMaterialParent) && !IsValid(StencilIsolationMID))
    {
        StencilIsolationMID = UMaterialInstanceDynamic::Create(StencilIsolationMaterialParent, this);
    }
    if (IsValid(StencilIsolationMID))
    {
        StencilIsolationMID->SetScalarParameterValue(TEXT("IsolationFade"),  0.8f);
        StencilIsolationMID->SetScalarParameterValue(TEXT("TargetStencil"), 250.f);
    }

    UObject* MatToBind = IsValid(StencilIsolationMID)
        ? static_cast<UObject*>(StencilIsolationMID)
        : static_cast<UObject*>(StencilIsolationMaterialParent);
    if (!IsValid(MatToBind)) { return; }

    FPostProcessSettings& PP = Camera->PostProcessSettings;
    PP.WeightedBlendables.Array.RemoveAll([this, MatToBind](const FWeightedBlendable& B)
    {
        return B.Object == MatToBind
            || B.Object == StencilIsolationMID
            || B.Object == StencilIsolationMaterialParent;
    });
    PP.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, MatToBind));

    // Disable physical DoF — the focused mesh must remain 100% sharp.
    PP.bOverride_DepthOfFieldFocalDistance = false;
    PP.bOverride_DepthOfFieldFocalRegion   = false;
    PP.bOverride_DepthOfFieldFstop         = false;
    PP.bOverride_DepthOfFieldNearBlurSize  = false;
    PP.bOverride_DepthOfFieldFarBlurSize   = false;

    // Soft vignette for a natural room-booth separation.
    PP.bOverride_VignetteIntensity = true;
    PP.VignetteIntensity           = 0.8f;

    PP.bOverride_ReflectionMethod       = true;
    PP.bOverride_LumenReflectionQuality = true;
    PP.LumenReflectionQuality           = 2.f;
}

// ─────────────────────────────────────────────────────────────────────────────
// WIP_UpdateWallOcclusion — ONE-SHOT sphere overlap
//
// Called ONCE from SetFocusComponent after the pivot and ActiveMaxZoom are set.
// Sphere radius = ActiveMaxZoom + 100cm buffer, so ANY camera position the user
// can reach by rotating or zooming will never see hidden geometry through a wall.
//
// Components belonging to the preview actor itself or the player character
// are ignored. Everything else within the sphere is hidden (shadows kept via
// SetCastHiddenShadow), and cached for restoration in EndPlay.
// ─────────────────────────────────────────────────────────────────────────────

void AFurniturePreviewActor::WIP_UpdateWallOcclusion()
{
    if (!GetWorld()) { return; }

    const float SphereRadius = ActiveMaxZoom + 100.f; // 100 cm beyond farthest zoom

    TArray<FOverlapResult> Overlaps;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(this);
    if (WIP_CachedCharacter.IsValid())
    {
        Params.AddIgnoredActor(WIP_CachedCharacter.Get());
    }

    GetWorld()->OverlapMultiByChannel(
        Overlaps,
        WIP_FocusPivotWorld,
        FQuat::Identity,
        ECC_WorldStatic,
        FCollisionShape::MakeSphere(SphereRadius),
        Params
    );

    for (const FOverlapResult& Overlap : Overlaps)
    {
        UPrimitiveComponent* Comp = Overlap.GetComponent();
        if (!IsValid(Comp)) { continue; }

        // Skip our own preview furniture meshes — they're already handled by
        // component isolation above.
        if (Comp == CabinetMesh.Get()          || Comp == ClosetMesh.Get()             ||
            Comp == CountertopMesh.Get()        || Comp == SinkMesh.Get()               ||
            Comp == FaucetMesh.Get()            || Comp == MirrorMesh.Get()             ||
            Comp == DoorMeshSlot0.Get()         || Comp == DoorMeshSlot1.Get()          ||
            Comp == ClosetDoorMeshSlot0.Get()   || Comp == ClosetDoorMeshSlot1.Get())
        {
            continue;
        }

        Comp->SetCastHiddenShadow(false);
        Comp->SetCastShadow(false);
        Comp->SetVisibility(false);
        WIP_CachedHiddenWallComponents.AddUnique(Comp);
    }
}

void AFurniturePreviewActor::ConfigureMesh(UStaticMeshComponent* Comp) const
{
    if (!IsValid(Comp)) { return; }
    Comp->SetMobility(EComponentMobility::Movable);
    Comp->SetCastShadow(true);
    Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Comp->LightingChannels.bChannel0 = true;
    Comp->LightingChannels.bChannel1 = false;
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
