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
#include "Engine/OverlapResult.h"
#include "Components/RectLightComponent.h"
#include "CollisionQueryParams.h"
#include "EngineUtils.h"
#include "Engine/PostProcessVolume.h"
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
    PrimaryActorTick.bCanEverTick = false; // Fully event-driven; nothing per-frame.

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

    // ── Subject Fill Rig (LIGHTING CHANNEL 1, shadowless) ─────────────────
    // Two rect lights that illuminate ONLY the preview meshes (also channel 1).
    // The room keeps its own lights on channel 0, untouched; the subject gets
    // constant, even illumination through the full rotation, while Lumen GI and
    // reflections from the intact room preserve the level's material appearance.
    //
    // Both are children of the SpringArm ROOT (at the pivot), not the camera
    // socket: their distance to the subject never changes with zoom, and since
    // the camera does not move during mesh rotation, illumination is identical
    // from every viewing angle. Shadowless lights ignore occluders, so a light
    // position that lands inside nearby wall geometry still works correctly.
    //
    // Exact placement, intensity, color and size are configured per component in
    // SetFocusComponent from FPreviewComponentConfig.
    auto InitRigLight = [](URectLightComponent* Light)
    {
        Light->SetMobility(EComponentMobility::Movable);
        Light->SetIntensity(0.f);                 // configured on focus
        Light->SetLightColor(FLinearColor::White);
        Light->AttenuationRadius = 800.f;
        Light->bUseTemperature   = false;
        Light->SetCastShadows(false);             // evenness: never shadowed
        Light->SetVisibility(false);              // shown only during active preview
        Light->LightingChannels.bChannel0 = false;
        Light->LightingChannels.bChannel1 = true; // subject-only
    };

    PreviewKeyLight = CreateDefaultSubobject<URectLightComponent>(TEXT("PreviewKeyLight"));
    PreviewKeyLight->SetupAttachment(SpringArm);
    InitRigLight(PreviewKeyLight);

    PreviewFillLight = CreateDefaultSubobject<URectLightComponent>(TEXT("PreviewFillLight"));
    PreviewFillLight->SetupAttachment(SpringArm);
    InitRigLight(PreviewFillLight);

    // ── Default conflicting post-process materials ────────────────────────
    // The Room Planner's outline material is keyed on custom-depth stencil values;
    // the preview subject renders stencil 250 for its isolation dim, so the outline
    // would tint the previewed mesh. Suspended during preview, restored on exit.
    PostProcessMaterialsToSuspend.Add(TSoftObjectPtr<UMaterialInterface>(
        FSoftObjectPath(TEXT("/Game/M_PostProcessOutline.M_PostProcessOutline"))));
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

    // ── Restore world components hidden by the clearance fallback ──────────
    RestoreClearanceHiddenComponents();

    // ── Restore the suspended outline blendable(s) to their volumes ────────
    RestoreSuspendedPostProcessMaterials();

    // ── Hide preview lights ───────────────────────────────────────────────
    if (IsValid(PreviewKeyLight))  { PreviewKeyLight->SetVisibility(false); }
    if (IsValid(PreviewFillLight)) { PreviewFillLight->SetVisibility(false); }

    // NOTE: no other world state to restore. The level's DirectionalLights,
    // RectLights and SkyLights are never modified; PostProcessVolumes only ever
    // have the listed conflicting blendables temporarily pulled (restored above),
    // so entering/leaving Viewmode cannot alter how the level looks.

    Super::EndPlay(EndPlayReason);
}

void AFurniturePreviewActor::RestoreClearanceHiddenComponents()
{
    for (const TWeakObjectPtr<UPrimitiveComponent>& CompPtr : WIP_CachedHiddenWallComponents)
    {
        if (CompPtr.IsValid())
        {
            // Only components that were VISIBLE get hidden by the fallback,
            // so restoring visibility is sufficient and cannot clobber a
            // designer's deliberate hidden state.
            CompPtr->SetVisibility(true);
        }
    }
    WIP_CachedHiddenWallComponents.Empty();
}

// ─────────────────────────────────────────────────────────────────────────────
// Conflicting post-process material suspension
//
// The level PostProcessVolume carries M_PostProcessOutline for the Room Planner's
// selection outlines, driven by custom-depth stencil values. The preview subject
// must render custom depth (stencil 250) for its own isolation dim, so while the
// preview is open the outline material would tint the subject.
//
// Solution: pull ONLY the listed materials out of the volumes' blendable arrays
// for the duration of the preview, and put them back (same object, same weight,
// same volume) on exit. Every other volume setting — exposure, bloom, grading —
// keeps applying throughout, which level-accurate material appearance requires.
// The Room Planner is not usable while Viewmode is open (its UI is closed), so
// suspending its outline for exactly that window changes nothing it does.
// ─────────────────────────────────────────────────────────────────────────────

void AFurniturePreviewActor::SuspendConflictingPostProcessMaterials()
{
    UWorld* World = GetWorld();
    if (!World) { return; }

    // Resolve the configured materials. They are referenced by the level's volumes,
    // so they are already in memory; LoadSynchronous is effectively a lookup.
    TArray<UMaterialInterface*> MaterialsToSuspend;
    for (const TSoftObjectPtr<UMaterialInterface>& SoftMat : PostProcessMaterialsToSuspend)
    {
        if (UMaterialInterface* Mat = SoftMat.LoadSynchronous())
        {
            MaterialsToSuspend.Add(Mat);
        }
    }

    auto ShouldSuspend = [&MaterialsToSuspend](UObject* BlendableObject) -> bool
    {
        if (!IsValid(BlendableObject)) { return false; }

        // Name fallback: catches the outline material even if the asset was moved
        // or the configured soft reference failed to resolve (Content differs per
        // machine in this project).
        if (BlendableObject->GetName().Contains(TEXT("PostProcessOutline")))
        {
            return true;
        }

        UMaterialInterface* AsMaterial = Cast<UMaterialInterface>(BlendableObject);
        for (UMaterialInterface* Mat : MaterialsToSuspend)
        {
            if (BlendableObject == Mat)
            {
                return true;
            }
            // Also match dynamic/instanced versions of the listed material.
            if (AsMaterial && Mat && AsMaterial->GetMaterial() == Mat->GetMaterial())
            {
                return true;
            }
        }
        return false;
    };

    for (TActorIterator<APostProcessVolume> It(World); It; ++It)
    {
        APostProcessVolume* Volume = *It;
        if (!IsValid(Volume)) { continue; }

        TArray<FWeightedBlendable>& Blendables = Volume->Settings.WeightedBlendables.Array;
        for (int32 Index = Blendables.Num() - 1; Index >= 0; --Index)
        {
            if (ShouldSuspend(Blendables[Index].Object))
            {
                FSuspendedPPBlendable Record;
                Record.Volume          = Volume;
                Record.BlendableObject = Blendables[Index].Object;
                Record.Weight          = Blendables[Index].Weight;
                SuspendedPostProcessBlendables.Add(Record);

                Blendables.RemoveAt(Index);
            }
        }
    }

    if (SuspendedPostProcessBlendables.Num() > 0)
    {
        UE_LOG(LogTemp, Log, TEXT("[PreviewActor] Suspended %d conflicting post-process blendable(s) for the preview session."),
            SuspendedPostProcessBlendables.Num());
    }
}

void AFurniturePreviewActor::RestoreSuspendedPostProcessMaterials()
{
    for (const FSuspendedPPBlendable& Record : SuspendedPostProcessBlendables)
    {
        if (Record.Volume.IsValid() && Record.BlendableObject.IsValid())
        {
            Record.Volume->Settings.WeightedBlendables.Array.Add(
                FWeightedBlendable(Record.Weight, Record.BlendableObject.Get()));
        }
    }
    SuspendedPostProcessBlendables.Empty();
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

    // Pull ONLY the stencil-keyed outline material out of the level volumes for the
    // duration of the preview (see SuspendConflictingPostProcessMaterials). All other
    // volume settings keep applying. Restored exactly in EndPlay.
    SuspendConflictingPostProcessMaterials();

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
}

// ─────────────────────────────────────────────────────────────────────────────
// SetFocusComponent
//
// Unified entry point for:
//   1. Component isolation   — hide all groups, show only the focused one.
//   2. Per-component config  — zoom limits, exposure and fill rig from FPreviewComponentConfig.
//   3. Stencil-250 isolation — apply CustomDepth on the focused group.
//   4. Swept clearance       — relocate the pivot into free space so 360-degree
//                              rotation cannot clip walls/floor (ResolveClearPivot).
//   5. SpringArm placement   — entry view along booth forward; max zoom clamped
//                              once against the wall behind the camera.
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

    // ── 0b. Undo any previous clearance fallback before re-evaluating ─────
    // Switching focus recomputes the swept clearance from scratch; components
    // hidden for the previous focus must come back first.
    RestoreClearanceHiddenComponents();

    // ── 1. Component isolation ────────────────────────────────────────────
    // Preview meshes never cast shadows (they are lit by the shadowless channel-1
    // rig only), so isolation is purely a visibility concern.
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

    // ── 6. Compute focus pivot and swept radius ───────────────────────────
    FVector FocusPivot = WIP_GetFocusPivotWorld();
    float MeshRadius = 80.f;
    if (IsValid(TargetComp) && TargetComp->GetStaticMesh())
    {
        MeshRadius = FMath::Max(15.f, TargetComp->Bounds.SphereRadius);
    }
    WIP_MeshBoundsRadius = MeshRadius;

    // ── 6b. Swept-clearance pivot relocation ("pick it off the shelf") ────
    // Booths stand against bathroom walls, so a full 360-degree rotation (yaw AND
    // pitch) sweeps a sphere of MeshRadius that would clip through the wall behind
    // the booth and, when pitched, the floor below. Relocate the pivot to the
    // nearest free spot instead of hiding the room - the room must stay intact
    // because it is what Lumen reflections and GI on the subject come from.
    const FVector ClearPivot = ResolveClearPivot(FocusPivot, MeshRadius);
    if (!ClearPivot.Equals(FocusPivot, 0.1f) && IsValid(MeshRoot))
    {
        MeshRoot->AddWorldOffset(ClearPivot - FocusPivot);
        FocusPivot = ClearPivot;
    }
    WIP_FocusPivotWorld = FocusPivot;

    // Cache initial MeshRoot state and pivot for exact rotation around bounds center
    if (IsValid(MeshRoot))
    {
        WIP_MeshRootLocAtReset  = MeshRoot->GetComponentLocation();
        WIP_InitialMeshRootQuat = MeshRoot->GetComponentQuat();
        WIP_MeshPivotWorld      = FocusPivot;
    }

    // ── 7. Entry view: consistent front view for every component ──────────
    // Base direction: the booth's facing axis (booths face into the open side of
    // the bathroom), so the entry view is booth-relative and therefore identical
    // for any placement or rotation of the booth in any level. Two corrections
    // make the view a natural product-shot front view for ALL components:
    //   - Per-component EntryYawOffsetDegrees fixes meshes whose authored front
    //     does not align with the booth's forward axis (the "enters showing its
    //     side" problem) — set once in BP_FurniturePreviewActor.
    //   - Actor-wide EntryPitchDegrees tilts the camera slightly above the mesh
    //     for the classic three-quarter presentation.
    // The channel-1 rig is camera-relative, so the subject is lit identically
    // regardless of the chosen entry direction.
    const float EntryYawOffset = Config ? Config->EntryYawOffsetDegrees : 0.f;
    const float EntryYaw       = GetActorRotation().Yaw + 180.f + EntryYawOffset;
    WIP_InitialOrbitRot        = FRotator(EntryPitchDegrees, EntryYaw, 0.f);

    // ── 7b. Clamp max zoom against the wall behind the camera ─────────────
    // The camera never moves during rotation, so the only way it can end up inside
    // a wall is by zooming out. One entry-time trace fixes that permanently:
    // never auto-adjust distance afterwards (stable-viewing-distance requirement).
    ActiveMaxZoom = FMath::Max(ActiveMaxZoom, ActiveMinZoom + 10.f);
    if (UWorld* World = GetWorld())
    {
        const FVector TowardCamera = -WIP_InitialOrbitRot.Vector(); // pivot -> camera
        FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(PreviewZoomClamp), /*bTraceComplex*/ false);
        TraceParams.AddIgnoredActor(this);
        if (WIP_CachedSourceBooth.IsValid()) { TraceParams.AddIgnoredActor(WIP_CachedSourceBooth.Get()); }
        if (ACharacter* Char = UGameplayStatics::GetPlayerCharacter(World, 0)) { TraceParams.AddIgnoredActor(Char); }

        FHitResult ZoomHit;
        if (World->LineTraceSingleByChannel(ZoomHit, FocusPivot,
                FocusPivot + TowardCamera * (ActiveMaxZoom + 60.f), ECC_Visibility, TraceParams))
        {
            const float CameraWallMarginCm = 30.f;
            ActiveMaxZoom = FMath::Clamp(ZoomHit.Distance - CameraWallMarginCm,
                                         ActiveMinZoom + 10.f, ActiveMaxZoom);
        }
    }

    // Adaptive initial distance: 2.5x the mesh radius, clamped to per-component limits.
    const float AdaptiveDist = FMath::Clamp(MeshRadius * 2.5f, ActiveMinZoom, ActiveMaxZoom);
    WIP_CurrentViewDist = AdaptiveDist;
    WIP_InitialViewDist = AdaptiveDist;
    CurrentZoomLength   = AdaptiveDist;

    // ── 8. Position SpringArm at pivot ───────────────────────────────────
    if (IsValid(SpringArm))
    {
        // Collision test stays OFF by design: the camera position is validated once
        // above; a live collision test would change the viewing distance whenever
        // geometry brushed the arm, violating the stable-distance requirement.
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

    // ── 10. Configure the channel-1 subject fill rig ──────────────────────
    // Both lights are children of the SpringArm root at the pivot; the arm's
    // rotation is fixed during mesh rotation, so the rig is screen-stable: the
    // subject is lit identically at every rotation angle. Shadows are always off
    // (evenness requirement), so nearby walls cannot block or shadow the rig.
    if (IsValid(PreviewKeyLight) && IsValid(PreviewFillLight))
    {
        const float KeyIntensity   = Config ? Config->PreviewKeyIntensity        : 800.f;
        const float FillMult       = Config ? Config->FillRimMultiplier          : 0.4f;
        const FLinearColor LColor  = Config ? Config->LightColor                 : FLinearColor::White;
        const float SrcW           = Config ? Config->LightSourceWidth           : 100.f;
        const float SrcH           = Config ? Config->LightSourceHeight          : 120.f;
        const float RigOffset      = Config ? Config->KeyLightOffset             : 200.f;
        const float AttenuRadius   = Config ? Config->KeyLightAttenuationRadius  : 800.f;

        // Soft key: camera side, raised and offset left of the view axis, aimed at
        // the pivot. Large source area = broad gentle speculars, not hard glints.
        const FVector KeyLoc(-RigOffset, -RigOffset * 0.45f, RigOffset * 0.55f);
        PreviewKeyLight->SetRelativeLocation(KeyLoc);
        PreviewKeyLight->SetRelativeRotation((-KeyLoc).Rotation());
        PreviewKeyLight->AttenuationRadius = AttenuRadius;
        PreviewKeyLight->SetIntensity(KeyIntensity);
        PreviewKeyLight->SetLightColor(LColor);
        PreviewKeyLight->SourceWidth  = SrcW;
        PreviewKeyLight->SourceHeight = SrcH;
        PreviewKeyLight->SetCastShadows(false);
        PreviewKeyLight->MarkRenderStateDirty();
        PreviewKeyLight->SetVisibility(KeyIntensity > 0.f);

        // Wrap fill: opposite side, slightly below, so the far side of the subject
        // never reads as a dark half while it rotates.
        const FVector FillLoc(RigOffset * 0.8f, RigOffset * 0.5f, -RigOffset * 0.15f);
        PreviewFillLight->SetRelativeLocation(FillLoc);
        PreviewFillLight->SetRelativeRotation((-FillLoc).Rotation());
        PreviewFillLight->AttenuationRadius = AttenuRadius;
        PreviewFillLight->SetIntensity(KeyIntensity * FillMult);
        PreviewFillLight->SetLightColor(LColor);
        PreviewFillLight->SourceWidth  = SrcW * 1.5f;
        PreviewFillLight->SourceHeight = SrcH * 1.5f;
        PreviewFillLight->SetCastShadows(false);
        PreviewFillLight->MarkRenderStateDirty();
        PreviewFillLight->SetVisibility(KeyIntensity * FillMult > 0.f);
    }

    // ── 11. Per-component camera exposure compensation ────────────────────
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

    // ── 12. Stencil isolation post-process material ───────────────────────
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

    // Optional soft vignette for room-subject separation. At 0 the level's own
    // vignette (from its PostProcessVolume) applies unchanged.
    if (PreviewVignetteIntensity > KINDA_SMALL_NUMBER)
    {
        PP.bOverride_VignetteIntensity = true;
        PP.VignetteIntensity           = PreviewVignetteIntensity;
    }
    else
    {
        PP.bOverride_VignetteIntensity = false;
    }

    // NOTE: deliberately NO reflection-method or Lumen-quality overrides here.
    // Reflections on the subject must render exactly as the level renders them.
}

// ─────────────────────────────────────────────────────────────────────────────
// ResolveClearPivot — swept-clearance pivot relocation
//
// A rotating mesh sweeps a sphere of its bounds radius around the pivot. Booths
// stand against bathroom walls, so at the raw pivot that sphere usually
// intersects the wall behind the booth and (once pitch rotation is involved)
// the floor. Rather than hiding the room — which would gut the Lumen
// reflections and GI this whole preview design exists to keep — the pivot is
// moved to the nearest position where the sphere is free:
//
//   1. Lift above the floor so a pitched (tumbling) mesh cannot sweep into it.
//   2. Walk forward along the booth's facing direction (then right, then left)
//      in small steps until an overlap test comes back clean.
//   3. Only if no free spot exists within MaxPivotSearchDistanceCm (very small
//      bathrooms) and the fallback is allowed: hide just the components that
//      intersect the swept sphere at the best candidate, cached for restore.
//
// Entry-time only — nothing here runs during rotation or zoom.
// ─────────────────────────────────────────────────────────────────────────────

FVector AFurniturePreviewActor::ResolveClearPivot(const FVector& DesiredPivot, float SweptRadius)
{
    UWorld* World = GetWorld();
    if (!World) { return DesiredPivot; }

    const float Clearance = SweptRadius + FMath::Max(0.f, PivotClearanceMarginCm);

    FCollisionQueryParams Params(SCENE_QUERY_STAT(PreviewPivotClearance), /*bTraceComplex*/ false);
    Params.AddIgnoredActor(this);
    if (WIP_CachedSourceBooth.IsValid()) { Params.AddIgnoredActor(WIP_CachedSourceBooth.Get()); }
    if (ACharacter* Char = UGameplayStatics::GetPlayerCharacter(World, 0)) { Params.AddIgnoredActor(Char); }

    const FCollisionShape Sphere = FCollisionShape::MakeSphere(Clearance);

    auto IsFree = [&](const FVector& Candidate) -> bool
    {
        return !World->OverlapBlockingTestByChannel(Candidate, FQuat::Identity, ECC_WorldStatic, Sphere, Params);
    };

    // 1. Floor clearance: the actor sits at the booth's root (floor level), so a
    //    pivot lower than the swept radius would sweep a pitched mesh into the floor.
    FVector Base = DesiredPivot;
    const float FloorZ = GetActorLocation().Z;
    Base.Z = FMath::Max(Base.Z, FloorZ + Clearance + 2.f);

    // 2. Search order: straight out of the booth first (into the open room), then
    //    diagonals, then sideways. Flattened to the horizontal plane.
    const FVector Fwd   = GetActorForwardVector().GetSafeNormal2D();
    const FVector Right = GetActorRightVector().GetSafeNormal2D();
    const FVector SearchDirs[] =
    {
        Fwd,
        (Fwd + Right * 0.5f).GetSafeNormal(),
        (Fwd - Right * 0.5f).GetSafeNormal(),
        Right,
        -Right
    };

    const float StepCm  = 20.f;
    const float MaxDist = FMath::Max(0.f, MaxPivotSearchDistanceCm);

    if (IsFree(Base))
    {
        return Base;
    }

    for (const FVector& Dir : SearchDirs)
    {
        for (float Dist = StepCm; Dist <= MaxDist; Dist += StepCm)
        {
            const FVector Candidate = Base + Dir * Dist;
            if (IsFree(Candidate))
            {
                return Candidate;
            }
        }
    }

    // 3. No free spot exists in this room. Fall back to the least-blocked candidate
    //    (as far into the room as allowed) and, if permitted, hide only the world
    //    components that actually intersect the swept sphere there.
    const FVector BestEffort = Base + Fwd * (MaxDist * 0.5f);

    if (bAllowGeometryHideFallback)
    {
        TArray<FOverlapResult> Overlaps;
        World->OverlapMultiByChannel(Overlaps, BestEffort, FQuat::Identity,
                                     ECC_WorldStatic, Sphere, Params);

        for (const FOverlapResult& Overlap : Overlaps)
        {
            UPrimitiveComponent* Comp = Overlap.GetComponent();
            if (!IsValid(Comp) || !Comp->IsVisible()) { continue; }
            if (Comp->GetOwner() == this) { continue; }

            Comp->SetVisibility(false);
            WIP_CachedHiddenWallComponents.AddUnique(Comp);
        }

        UE_LOG(LogTemp, Log,
            TEXT("[PreviewActor] Clearance fallback: no free pivot within %.0fcm; hid %d blocking component(s)."),
            MaxDist, WIP_CachedHiddenWallComponents.Num());
    }

    return BestEffort;
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
