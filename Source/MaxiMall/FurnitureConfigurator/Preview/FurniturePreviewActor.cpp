// Copyright MaxiMall Project. All Rights Reserved.
// FurniturePreviewActor.cpp

#include "FurnitureConfigurator/Preview/FurniturePreviewActor.h"
#include "FurnitureConfigurator/ShowroomBooth.h"

#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Components/SpotLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/TextureCube.h"
#include "EngineUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

AFurniturePreviewActor::AFurniturePreviewActor()
{
    // ── CRITICAL: This actor must NEVER replicate ─────────────────────────
    bReplicates          = false;
    bAlwaysRelevant      = false;
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

    auto ConfigurePreviewMesh = [](UStaticMeshComponent* Comp)
    {
        if (IsValid(Comp))
        {
            Comp->SetMobility(EComponentMobility::Movable);
            Comp->SetCastShadow(true);
            Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Comp->LightingChannels.bChannel0 = true;
            Comp->LightingChannels.bChannel1 = false;
            Comp->LightingChannels.bChannel2 = false;
        }
    };

    ConfigurePreviewMesh(CabinetMesh.Get());
    ConfigurePreviewMesh(DoorMeshSlot0.Get());
    ConfigurePreviewMesh(DoorMeshSlot1.Get());
    ConfigurePreviewMesh(CountertopMesh.Get());
    ConfigurePreviewMesh(SinkMesh.Get());
    ConfigurePreviewMesh(FaucetMesh.Get());
    ConfigurePreviewMesh(MirrorMesh.Get());

    // ── Closet ────────────────────────────────────────────────────────────
    ClosetMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Closet"));
    ClosetMesh->SetupAttachment(MeshRoot);
    ConfigurePreviewMesh(ClosetMesh.Get());

    ClosetDoorMeshSlot0 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ClosetDoorSlot0"));
    ClosetDoorMeshSlot0->SetupAttachment(ClosetMesh);
    ConfigurePreviewMesh(ClosetDoorMeshSlot0.Get());

    ClosetDoorMeshSlot1 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ClosetDoorSlot1"));
    ClosetDoorMeshSlot1->SetupAttachment(ClosetMesh);
    ConfigurePreviewMesh(ClosetDoorMeshSlot1.Get());

    ActiveBaseFillIntensity = 10000.f;
    ReferenceZoomDistance = 250.f;
    DefaultYaw = 0.f;
    DefaultPitch = -15.f;
    CurrentYaw = 0.f;
    CurrentPitch = -15.f;

    // Profiles Defaults
    CabinetLighting.FocusDistance = 250.f;
    CabinetLighting.CameraFOV = 65.f;
    CabinetLighting.Pitch = -15.f;
    CabinetLighting.Yaw = 0.f;
    CabinetLighting.PitchMin = -80.f;
    CabinetLighting.PitchMax = 80.f;
    CabinetLighting.ZoomMin = 100.f;
    CabinetLighting.ZoomMax = 500.f;
    CabinetLighting.bCastShadow = true;
    CabinetLighting.bEnableKeyLight = true;
    CabinetLighting.bEnableFillLight = true;
    CabinetLighting.bEnableRimLight = true;
    CabinetLighting.KeyLightIntensity = 80000.f;
    CabinetLighting.FillLightIntensity = 10000.f;
    CabinetLighting.RimLightIntensity = 30000.f;
    CabinetLighting.DirectionalLightScale = 1.0f;
    CabinetLighting.MasterLightIntensityScale = 1.0f;
    CabinetLighting.KeyLightColor = FLinearColor::White;
    CabinetLighting.FillLightColor = FLinearColor::White;
    CabinetLighting.RimLightColor = FLinearColor::White;
    CabinetLighting.KeyLightLocation = FVector(-300.f, -300.f, 300.f);

    ClosetLighting = CabinetLighting;

    CountertopLighting = CabinetLighting;
    CountertopLighting.FocusDistance = 200.f;

    SinkLighting = CabinetLighting;
    SinkLighting.FocusDistance = 150.f;
    SinkLighting.bCastShadow = false;

    FaucetLighting = CabinetLighting;
    FaucetLighting.FocusDistance = 100.f;

    MirrorLighting = CabinetLighting;
    MirrorLighting.FocusDistance = 150.f;

    ActiveConfig = CabinetLighting;
    CurrentZoomLength = ActiveConfig.FocusDistance;

    // ── Spring Arm & Camera ───────────────────────────────────────────────
    SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
    SpringArm->SetupAttachment(PreviewRoot);
    SpringArm->TargetArmLength = CurrentZoomLength;
    SpringArm->bDoCollisionTest = false;
    SpringArm->SetRelativeRotation(FRotator(-15.f, 0.f, 0.f));

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);

    // ── PostProcess Settings (Inherits tone-mapping and color grading from world) ──
    Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness = false;
    Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = false;

    // ── Studio Backdrop ───────────────────────────────────────────────────
    BackdropMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BackdropMesh"));
    BackdropMesh->SetupAttachment(PreviewRoot);
    BackdropMesh->SetMobility(EComponentMobility::Movable);
    BackdropMesh->SetCastShadow(false);
    BackdropMesh->SetAffectDynamicIndirectLighting(false);
    BackdropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BackdropMesh->LightingChannels.bChannel0 = true;
    BackdropMesh->LightingChannels.bChannel1 = false;
    BackdropMesh->LightingChannels.bChannel2 = false;
    BackdropMesh->SetRelativeScale3D(FVector(100.f, 100.f, 100.f));

    // ── Key Light (Attached to SpringArm so it orbits WITH camera) ────────
    KeyLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("KeyLight"));
    KeyLight->SetupAttachment(SpringArm);
    KeyLight->SetVisibility(ActiveConfig.bEnableKeyLight);
    KeyLight->SetLightColor(ActiveConfig.KeyLightColor);
    KeyLight->bUseTemperature = false;
    
    KeyLight->SetRelativeLocation(FVector(-300.f, -300.f, 300.f));
    FVector LookAtTarget = FVector(0.f, 0.f, 50.f) - KeyLight->GetRelativeLocation();
    KeyLight->SetRelativeRotation(LookAtTarget.Rotation());
    
    KeyLight->InnerConeAngle = 30.f;
    KeyLight->OuterConeAngle = 50.f;
    KeyLight->SetIntensity(80000.f);
    KeyLight->SetCastShadows(true);
    KeyLight->LightingChannels.bChannel0 = true;

    // ── Fill Light (Mounted on Camera, intensity scales dynamically with zoom) ──
    FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
    FillLight->SetupAttachment(Camera);
    FillLight->SetVisibility(ActiveConfig.bEnableFillLight);
    FillLight->SetLightColor(ActiveConfig.FillLightColor);
    FillLight->bUseTemperature = false;
    FillLight->SetIntensity(ActiveBaseFillIntensity);
    FillLight->SetCastShadows(false);
    FillLight->LightingChannels.bChannel0 = true;

    // ── Rim Light (Placed behind model to separate from backdrop) ───────────
    RimLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("RimLight"));
    RimLight->SetupAttachment(PreviewRoot);
    RimLight->SetVisibility(ActiveConfig.bEnableRimLight);
    RimLight->SetLightColor(ActiveConfig.RimLightColor);
    RimLight->bUseTemperature = false;

    RimLight->SetRelativeLocation(FVector(200.f, 200.f, 250.f));
    FVector RimLookAtTarget = FVector(0.f, 0.f, 50.f) - RimLight->GetRelativeLocation();
    RimLight->SetRelativeRotation(RimLookAtTarget.Rotation());

    RimLight->InnerConeAngle = 30.f;
    RimLight->OuterConeAngle = 60.f;
    RimLight->SetIntensity(30000.f);
    RimLight->SetCastShadows(false);
    RimLight->LightingChannels.bChannel0 = true;

    // ── Sky Light (Studio Ambient HDRI Reflections for Gold/Chrome/Metals) ──
    SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
    SkyLight->SetupAttachment(PreviewRoot);
    SkyLight->SetMobility(EComponentMobility::Movable);
    SkyLight->SourceType = ESkyLightSourceType::SLS_CapturedScene;
    SkyLight->SetIntensity(ActiveConfig.SkyLightIntensity);
    SkyLight->SetLightColor(ActiveConfig.SkyLightColor);
}

// ─────────────────────────────────────────────────────────────────────────────
// BeginPlay
// ─────────────────────────────────────────────────────────────────────────────

void AFurniturePreviewActor::BeginPlay()
{
    Super::BeginPlay();

    ActiveConfig = CabinetLighting;
    CurrentZoomLength = ActiveConfig.FocusDistance;
    DefaultYaw = ActiveConfig.Yaw;
    DefaultPitch = ActiveConfig.Pitch;
    CurrentYaw = DefaultYaw;
    CurrentPitch = DefaultPitch;

    if (IsValid(SpringArm))
    {
        SpringArm->TargetArmLength = CurrentZoomLength;
        SpringArm->SetRelativeRotation(FRotator(CurrentPitch, CurrentYaw, 0.f));
    }
    if (IsValid(Camera))
    {
        Camera->FieldOfView = ActiveConfig.CameraFOV;
    }

    UpdateLightIntensityForZoom();
    EnforceLightingSettings();
    ApplyDirectionalLightScale();
    ApplyWorldPostProcessSettings();
}

void AFurniturePreviewActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);
    RestoreDirectionalLight();
}

void AFurniturePreviewActor::PostInitializeComponents()
{
    Super::PostInitializeComponents();

    ActiveConfig = CabinetLighting;
    CurrentZoomLength = ActiveConfig.FocusDistance;
    if (IsValid(SpringArm))
    {
        SpringArm->TargetArmLength = CurrentZoomLength;
    }
    if (IsValid(Camera))
    {
        Camera->FieldOfView = ActiveConfig.CameraFOV;
    }

    EnforceLightingSettings();

    if (IsValid(CountertopMesh) && IsValid(CabinetMesh))
    {
        CountertopMesh->AttachToComponent(CabinetMesh, FAttachmentTransformRules::KeepWorldTransform);
    }
    if (IsValid(SinkMesh) && IsValid(CabinetMesh))
    {
        SinkMesh->AttachToComponent(CabinetMesh, FAttachmentTransformRules::KeepWorldTransform);
    }
    if (IsValid(FaucetMesh) && IsValid(CabinetMesh))
    {
        FaucetMesh->AttachToComponent(CabinetMesh, FAttachmentTransformRules::KeepWorldTransform);
    }
}

void AFurniturePreviewActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    
    ActiveConfig = CabinetLighting;
    CurrentZoomLength = ActiveConfig.FocusDistance;
    if (IsValid(SpringArm))
    {
        SpringArm->TargetArmLength = CurrentZoomLength;
    }
    if (IsValid(Camera))
    {
        Camera->FieldOfView = ActiveConfig.CameraFOV;
    }

    EnforceLightingSettings();
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void AFurniturePreviewActor::LoadProductPreview(const FFurnitureProductRow& ProductData,
                                                const FShowroomBoothConfigState& ActiveState,
                                                AShowroomBooth* SourceBooth)
{
    // Reset MeshRoot transform
    if (IsValid(MeshRoot))
    {
        MeshRoot->SetRelativeTransform(FTransform::Identity);
    }

    // ── Apply SourceBooth Transforms if available ─────────────────────────
    if (IsValid(SourceBooth))
    {
        if (CabinetMesh && SourceBooth->MainCabinet)
        {
            CabinetMesh->SetRelativeTransform(SourceBooth->MainCabinet->GetRelativeTransform());
        }
        if (ClosetMesh && SourceBooth->ClosetMesh)
        {
            ClosetMesh->SetRelativeTransform(SourceBooth->ClosetMesh->GetRelativeTransform());
        }
        if (DoorMeshSlot0 && SourceBooth->DoorMeshSlot0)
        {
            DoorMeshSlot0->SetRelativeTransform(SourceBooth->DoorMeshSlot0->GetRelativeTransform());
        }
        if (DoorMeshSlot1 && SourceBooth->DoorMeshSlot1)
        {
            DoorMeshSlot1->SetRelativeTransform(SourceBooth->DoorMeshSlot1->GetRelativeTransform());
        }
        if (ClosetDoorMeshSlot0 && SourceBooth->ClosetDoorMeshSlot0)
        {
            ClosetDoorMeshSlot0->SetRelativeTransform(SourceBooth->ClosetDoorMeshSlot0->GetRelativeTransform());
        }
        if (ClosetDoorMeshSlot1 && SourceBooth->ClosetDoorMeshSlot1)
        {
            ClosetDoorMeshSlot1->SetRelativeTransform(SourceBooth->ClosetDoorMeshSlot1->GetRelativeTransform());
        }
        if (CountertopMesh && SourceBooth->CountertopMesh)
        {
            CountertopMesh->SetRelativeTransform(SourceBooth->CountertopMesh->GetRelativeTransform());
        }
        if (SinkMesh && SourceBooth->SinkMesh)
        {
            SinkMesh->SetRelativeTransform(SourceBooth->SinkMesh->GetRelativeTransform());
        }
        if (FaucetMesh && SourceBooth->FaucetMesh)
        {
            FaucetMesh->SetRelativeTransform(SourceBooth->FaucetMesh->GetRelativeTransform());
        }
        if (MirrorMesh && SourceBooth->MirrorMesh)
        {
            MirrorMesh->SetRelativeTransform(SourceBooth->MirrorMesh->GetRelativeTransform());
        }
    }

    // ── Cabinet ───────────────────────────────────────────────────────────
    if (!IsValid(SourceBooth) || (IsValid(SourceBooth->MainCabinet) && SourceBooth->MainCabinet->GetStaticMesh() != nullptr))
    {
        ApplyComponentMeshAndMaterials(CabinetMesh.Get(), ProductData.CabinetOptions, ActiveState.ActiveSizeIndex, ActiveState.ActiveColorIndex);
    }
    else if (IsValid(CabinetMesh))
    {
        CabinetMesh->SetStaticMesh(nullptr);
        CabinetMesh->SetVisibility(false);
        CabinetMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    // ── Closet ────────────────────────────────────────────────────────────
    if (!IsValid(SourceBooth) || (IsValid(SourceBooth->ClosetMesh) && SourceBooth->ClosetMesh->GetStaticMesh() != nullptr))
    {
        ApplyComponentMeshAndMaterials(ClosetMesh.Get(), ProductData.ClosetOptions, ActiveState.ClosetSizeIndex, ActiveState.ClosetColorIndex);
    }
    else if (IsValid(ClosetMesh))
    {
        ClosetMesh->SetStaticMesh(nullptr);
        ClosetMesh->SetVisibility(false);
        ClosetMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    // ── Doors ─────────────────────────────────────────────────────────────
    if (!IsValid(SourceBooth) || (IsValid(SourceBooth->DoorMeshSlot0) && SourceBooth->DoorMeshSlot0->GetStaticMesh() != nullptr))
    {
        const FFurnitureDoorGroup& CabDoors = ProductData.DoorsConfig.CabinetDoors;

        ApplyDoorMeshAndMaterials(DoorMeshSlot0.Get(), CabDoors, ActiveState.ActiveSizeIndex, ActiveState.ActiveColorIndex, 0);
        ApplyDoorMeshAndMaterials(DoorMeshSlot1.Get(), CabDoors, ActiveState.ActiveSizeIndex, ActiveState.ActiveColorIndex, 1);

        switch (CabDoors.DoorCount)
        {
        case EDoorCount::NoDoors:
            if (IsValid(DoorMeshSlot0)) DoorMeshSlot0->SetVisibility(false);
            if (IsValid(DoorMeshSlot1)) DoorMeshSlot1->SetVisibility(false);
            break;
        case EDoorCount::OneDoor:
            if (IsValid(DoorMeshSlot0) && DoorMeshSlot0->GetStaticMesh())
            {
                DoorMeshSlot0->SetVisibility(true);
            }
            if (IsValid(SourceBooth) && IsValid(SourceBooth->DoorMeshSlot0))
            {
                if (IsValid(DoorMeshSlot0)) DoorMeshSlot0->SetRelativeTransform(SourceBooth->DoorMeshSlot0->GetRelativeTransform());
            }
            else if (IsValid(DoorMeshSlot0))
            {
                DoorMeshSlot0->SetRelativeLocation(CabDoors.SingleDoor.SlotConfig.ClosedPositionOffset);
            }
            if (IsValid(DoorMeshSlot1)) DoorMeshSlot1->SetVisibility(false);
            break;
        case EDoorCount::TwoDoors:
            if (IsValid(DoorMeshSlot0) && DoorMeshSlot0->GetStaticMesh())
            {
                DoorMeshSlot0->SetVisibility(true);
            }
            if (IsValid(SourceBooth) && IsValid(SourceBooth->DoorMeshSlot0))
            {
                if (IsValid(DoorMeshSlot0)) DoorMeshSlot0->SetRelativeTransform(SourceBooth->DoorMeshSlot0->GetRelativeTransform());
            }
            else if (IsValid(DoorMeshSlot0))
            {
                DoorMeshSlot0->SetRelativeLocation(CabDoors.DoubleDoors.Slot0Config.ClosedPositionOffset);
            }
            if (IsValid(DoorMeshSlot1) && DoorMeshSlot1->GetStaticMesh())
            {
                DoorMeshSlot1->SetVisibility(true);
            }
            if (IsValid(SourceBooth) && IsValid(SourceBooth->DoorMeshSlot1))
            {
                if (IsValid(DoorMeshSlot1)) DoorMeshSlot1->SetRelativeTransform(SourceBooth->DoorMeshSlot1->GetRelativeTransform());
            }
            else if (IsValid(DoorMeshSlot1))
            {
                DoorMeshSlot1->SetRelativeLocation(CabDoors.DoubleDoors.Slot1Config.ClosedPositionOffset);
            }
            break;
        }
    }
    else
    {
        if (IsValid(DoorMeshSlot0))
        {
            DoorMeshSlot0->SetStaticMesh(nullptr);
            DoorMeshSlot0->SetVisibility(false);
            DoorMeshSlot0->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
        if (IsValid(DoorMeshSlot1))
        {
            DoorMeshSlot1->SetStaticMesh(nullptr);
            DoorMeshSlot1->SetVisibility(false);
            DoorMeshSlot1->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
    }

    // ── Closet Doors ──────────────────────────────────────────────────────
    if (!IsValid(SourceBooth) || (IsValid(SourceBooth->ClosetDoorMeshSlot0) && SourceBooth->ClosetDoorMeshSlot0->GetStaticMesh() != nullptr))
    {
        const FFurnitureDoorGroup& ClosetDoors = ProductData.DoorsConfig.ClosetDoors;

        ApplyDoorMeshAndMaterials(ClosetDoorMeshSlot0.Get(), ClosetDoors, ActiveState.ClosetSizeIndex, ActiveState.ClosetColorIndex, 0);
        ApplyDoorMeshAndMaterials(ClosetDoorMeshSlot1.Get(), ClosetDoors, ActiveState.ClosetSizeIndex, ActiveState.ClosetColorIndex, 1);

        switch (ClosetDoors.DoorCount)
        {
        case EDoorCount::NoDoors:
            if (IsValid(ClosetDoorMeshSlot0)) ClosetDoorMeshSlot0->SetVisibility(false);
            if (IsValid(ClosetDoorMeshSlot1)) ClosetDoorMeshSlot1->SetVisibility(false);
            break;
        case EDoorCount::OneDoor:
            if (IsValid(ClosetDoorMeshSlot0) && ClosetDoorMeshSlot0->GetStaticMesh())
            {
                ClosetDoorMeshSlot0->SetVisibility(true);
            }
            if (IsValid(SourceBooth) && IsValid(SourceBooth->ClosetDoorMeshSlot0))
            {
                if (IsValid(ClosetDoorMeshSlot0)) ClosetDoorMeshSlot0->SetRelativeTransform(SourceBooth->ClosetDoorMeshSlot0->GetRelativeTransform());
            }
            else if (IsValid(ClosetDoorMeshSlot0))
            {
                ClosetDoorMeshSlot0->SetRelativeLocation(ClosetDoors.SingleDoor.SlotConfig.ClosedPositionOffset);
            }
            if (IsValid(ClosetDoorMeshSlot1)) ClosetDoorMeshSlot1->SetVisibility(false);
            break;
        case EDoorCount::TwoDoors:
            if (IsValid(ClosetDoorMeshSlot0) && ClosetDoorMeshSlot0->GetStaticMesh())
            {
                ClosetDoorMeshSlot0->SetVisibility(true);
            }
            if (IsValid(SourceBooth) && IsValid(SourceBooth->ClosetDoorMeshSlot0))
            {
                if (IsValid(ClosetDoorMeshSlot0)) ClosetDoorMeshSlot0->SetRelativeTransform(SourceBooth->ClosetDoorMeshSlot0->GetRelativeTransform());
            }
            else if (IsValid(ClosetDoorMeshSlot0))
            {
                ClosetDoorMeshSlot0->SetRelativeLocation(ClosetDoors.DoubleDoors.Slot0Config.ClosedPositionOffset);
            }
            if (IsValid(ClosetDoorMeshSlot1) && ClosetDoorMeshSlot1->GetStaticMesh())
            {
                ClosetDoorMeshSlot1->SetVisibility(true);
            }
            if (IsValid(SourceBooth) && IsValid(SourceBooth->ClosetDoorMeshSlot1))
            {
                if (IsValid(ClosetDoorMeshSlot1)) ClosetDoorMeshSlot1->SetRelativeTransform(SourceBooth->ClosetDoorMeshSlot1->GetRelativeTransform());
            }
            else if (IsValid(ClosetDoorMeshSlot1))
            {
                ClosetDoorMeshSlot1->SetRelativeLocation(ClosetDoors.DoubleDoors.Slot1Config.ClosedPositionOffset);
            }
            break;
        }
    }
    else
    {
        if (IsValid(ClosetDoorMeshSlot0))
        {
            ClosetDoorMeshSlot0->SetStaticMesh(nullptr);
            ClosetDoorMeshSlot0->SetVisibility(false);
            ClosetDoorMeshSlot0->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
        if (IsValid(ClosetDoorMeshSlot1))
        {
            ClosetDoorMeshSlot1->SetStaticMesh(nullptr);
            ClosetDoorMeshSlot1->SetVisibility(false);
            ClosetDoorMeshSlot1->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
    }

    // ── Countertop ────────────────────────────────────────────────────────
    if (!IsValid(SourceBooth) || (IsValid(SourceBooth->CountertopMesh) && SourceBooth->CountertopMesh->GetStaticMesh() != nullptr))
    {
        FFurnitureComponentOptions ResolvedCountertop;
        if (IsValid(SourceBooth))
        {
            SourceBooth->GetResolvedComponentOptions(EFurnitureComponentType::Countertop, ResolvedCountertop);
        }
        ApplyComponentMeshAndMaterials(CountertopMesh.Get(), ResolvedCountertop, ActiveState.CountertopSizeIndex, ActiveState.ActiveCountertopColorIndex);

        FFurniturePlacementOffset CO = IsValid(SourceBooth) ? SourceBooth->GetActiveCountertopOffset() : FFurniturePlacementOffset();
        if (IsValid(SourceBooth) && SourceBooth->GetActiveCountertopType() == ECountertopType::BuiltIn)
        {
            const FTransform BaselineCountertop = SourceBooth->GetBaselineCountertopTransform();
            FVector TargetLocation = FVector(0.f, 0.f, BaselineCountertop.GetLocation().Z) + CO.RelativeLocation;
            FRotator TargetRotation = CO.RelativeRotation;
            FVector TargetScale = CO.RelativeScale * BaselineCountertop.GetScale3D();
            if (IsValid(CountertopMesh))
            {
                CountertopMesh->SetRelativeLocationAndRotation(TargetLocation, TargetRotation);
                CountertopMesh->SetRelativeScale3D(TargetScale);
            }
        }
        else if (IsValid(CountertopMesh))
        {
            FTransform ProductDelta;
            ProductDelta.SetLocation(CO.RelativeLocation);
            ProductDelta.SetRotation(CO.RelativeRotation.Quaternion());
            ProductDelta.SetScale3D(CO.RelativeScale);

            const FTransform BaselineCountertop = IsValid(SourceBooth) ? SourceBooth->GetBaselineCountertopTransform() : FTransform::Identity;
            const FTransform FinalCountertopTransform = ProductDelta * BaselineCountertop;
            CountertopMesh->SetRelativeTransform(FinalCountertopTransform);
        }
    }
    else if (IsValid(CountertopMesh))
    {
        CountertopMesh->SetStaticMesh(nullptr);
        CountertopMesh->SetVisibility(false);
        CountertopMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    // ── Sink ──────────────────────────────────────────────────────────────
    ECountertopType ActiveCountertopType = IsValid(SourceBooth) ? SourceBooth->GetActiveCountertopType() : ECountertopType::SurfaceMounted;
    if (ActiveCountertopType == ECountertopType::SurfaceMounted && 
        (!IsValid(SourceBooth) || (IsValid(SourceBooth->SinkMesh) && SourceBooth->SinkMesh->GetStaticMesh() != nullptr)))
    {
        FFurnitureComponentOptions ResolvedSink;
        if (IsValid(SourceBooth))
        {
            SourceBooth->GetResolvedComponentOptions(EFurnitureComponentType::Sink, ResolvedSink);
        }
        ApplyComponentMeshAndMaterials(SinkMesh.Get(), ResolvedSink, ActiveState.SinkSizeIndex, ActiveState.SinkColorIndex);

        FFurniturePlacementOffset SO = IsValid(SourceBooth) ? SourceBooth->GetActiveSinkOffset() : FFurniturePlacementOffset();
        FTransform ProductDelta;
        ProductDelta.SetLocation(SO.RelativeLocation);
        ProductDelta.SetRotation(SO.RelativeRotation.Quaternion());
        ProductDelta.SetScale3D(SO.RelativeScale);

        const FTransform BaselineSink = IsValid(SourceBooth) ? SourceBooth->GetBaselineSinkTransform() : FTransform::Identity;
        const FTransform FinalSinkTransform = ProductDelta * BaselineSink;
        if (IsValid(SinkMesh))
        {
            SinkMesh->SetRelativeTransform(FinalSinkTransform);
        }
    }
    else if (IsValid(SinkMesh))
    {
        SinkMesh->SetStaticMesh(nullptr);
        SinkMesh->SetVisibility(false);
        SinkMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    // ── Faucet ────────────────────────────────────────────────────────────
    if (!IsValid(SourceBooth) || (IsValid(SourceBooth->FaucetMesh) && SourceBooth->FaucetMesh->GetStaticMesh() != nullptr))
    {
        FFurnitureComponentOptions ResolvedFaucet;
        if (IsValid(SourceBooth))
        {
            SourceBooth->GetResolvedComponentOptions(EFurnitureComponentType::Faucet, ResolvedFaucet);
        }
        ApplyComponentMeshAndMaterials(FaucetMesh.Get(), ResolvedFaucet, ActiveState.FaucetSizeIndex, ActiveState.FaucetColorIndex);
        {
            FFurniturePlacementOffset FO = IsValid(SourceBooth) ? SourceBooth->GetActiveFaucetOffset() : FFurniturePlacementOffset();
            if (IsValid(SourceBooth) && SourceBooth->GetActiveCountertopType() == ECountertopType::BuiltIn)
            {
                const FTransform BaselineFaucet = SourceBooth->GetBaselineFaucetTransform();
                FVector TargetLocation = FVector(0.f, 0.f, BaselineFaucet.GetLocation().Z) + FO.RelativeLocation;
                FRotator TargetRotation = FO.RelativeRotation;
                FVector TargetScale = FO.RelativeScale * BaselineFaucet.GetScale3D();
                if (IsValid(FaucetMesh))
                {
                    FaucetMesh->SetRelativeLocationAndRotation(TargetLocation, TargetRotation);
                    FaucetMesh->SetRelativeScale3D(TargetScale);
                }
            }
            else if (IsValid(FaucetMesh))
            {
                FTransform ProductDelta;
                ProductDelta.SetLocation(FO.RelativeLocation);
                ProductDelta.SetRotation(FO.RelativeRotation.Quaternion());
                ProductDelta.SetScale3D(FO.RelativeScale);

                const FTransform BaselineFaucet = IsValid(SourceBooth) ? SourceBooth->GetBaselineFaucetTransform() : FTransform::Identity;
                const FTransform FinalFaucetTransform = ProductDelta * BaselineFaucet;
                FaucetMesh->SetRelativeTransform(FinalFaucetTransform);
            }
        }
    }
    else if (IsValid(FaucetMesh))
    {
        FaucetMesh->SetStaticMesh(nullptr);
        FaucetMesh->SetVisibility(false);
        FaucetMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    // ── Mirror ────────────────────────────────────────────────────────────
    if (!IsValid(SourceBooth) || (IsValid(SourceBooth->MirrorMesh) && SourceBooth->MirrorMesh->GetStaticMesh() != nullptr))
    {
        FFurnitureComponentOptions ResolvedMirror;
        if (IsValid(SourceBooth))
        {
            SourceBooth->GetResolvedComponentOptions(EFurnitureComponentType::Mirror, ResolvedMirror);
        }
        ApplyComponentMeshAndMaterials(MirrorMesh.Get(), ResolvedMirror, ActiveState.MirrorSizeIndex, ActiveState.MirrorColorIndex);
        {
            FFurniturePlacementOffset MO = IsValid(SourceBooth) ? SourceBooth->GetActiveMirrorOffset() : FFurniturePlacementOffset();
            FTransform ProductDelta;
            ProductDelta.SetLocation(MO.RelativeLocation);
            ProductDelta.SetRotation(MO.RelativeRotation.Quaternion());
            ProductDelta.SetScale3D(MO.RelativeScale);

            const FTransform BaselineMirror = IsValid(SourceBooth) ? SourceBooth->GetBaselineMirrorTransform() : FTransform::Identity;
            const FTransform FinalMirrorTransform = ProductDelta * BaselineMirror;
            if (IsValid(MirrorMesh))
            {
                MirrorMesh->SetRelativeTransform(FinalMirrorTransform);
            }
        }
    }
    else if (IsValid(MirrorMesh))
    {
        MirrorMesh->SetStaticMesh(nullptr);
        MirrorMesh->SetVisibility(false);
        MirrorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    EnforceLightingSettings();
}

// ─────────────────────────────────────────────────────────────────────────────
// WorldInPlace View Mode
//
// Architecture:
//   PreviewRoot  — spawns at the booth world location with booth Yaw.
//   MeshRoot     — child of PreviewRoot. All furniture meshes are children of MeshRoot.
//                  On activation: RelativeLocation = Zero, RelativeRotation = Zero
//                  (meshes sit exactly where they are in the real booth).
//                  During rotation: SetWorldLocationAndRotation() pivots around the
//                  focused mesh's bounding box center (Bounds.Origin).
//   SpringArm    — child of PreviewRoot.
//                  WORLD Location = focused mesh Bounds.Origin.
//                  WORLD Rotation = computed from character position so camera is
//                  always on the same side as the standing character.
//                  TargetArmLength = zoom distance AND DoF focal distance.
//   Camera       — child of SpringArm socket, faces the pivot naturally.
//
// Rotation:
//   Left/Right → MeshRoot rotates around WORLD Z axis (always vertical).
//   Up/Down    → MeshRoot rotates around CAMERA RIGHT vector (horizontal, ⊥ to view).
//   Both axes pivot on the focused mesh's Bounds.Origin — true self-rotation.
//   Roll = 0 is guaranteed because Z and CameraRight are always perpendicular.
//
// Zoom:   SpringArm.TargetArmLength only. Camera moves strictly along view axis.
// DoF:    DepthOfFieldFocalDistance = TargetArmLength (exact, always correct).
// ─────────────────────────────────────────────────────────────────────────────

// Returns the current world-space bounding box center of the focused mesh.
FVector AFurniturePreviewActor::WIP_GetFocusPivotWorld() const
{
    if (IsValid(CurrentFocusedComponent) && CurrentFocusedComponent->GetStaticMesh())
        return CurrentFocusedComponent->Bounds.Origin;
    if (IsValid(CabinetMesh) && CabinetMesh->GetStaticMesh())
        return CabinetMesh->Bounds.Origin;
    if (IsValid(MeshRoot))
        return MeshRoot->GetComponentLocation();
    return GetActorLocation();
}

void AFurniturePreviewActor::SetFocusComponent(EFurnitureComponentType TargetType)
{
    // ── 1. Resolve target component ───────────────────────────────────────
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

    if (ViewportMode == EPreviewViewportMode::WorldInPlace)
    {
        // ── 2. Reset rotation accumulators ───────────────────────────────
        WorldInPlaceYaw   = 0.f;
        WorldInPlacePitch = 0.f;

        // ── 3. Reset MeshRoot to identity ────────────────────────────────
        // All meshes return to their real-world booth positions.
        if (IsValid(MeshRoot))
        {
            MeshRoot->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
        }

        // ── 4. Compute SpringArm position and direction ───────────────────
        // Pivot = world-space bounding box center of the focused mesh.
        FVector MeshCenter = WIP_GetFocusPivotWorld();

        // Get the character camera's world position to determine
        // which side the player is standing on.
        FVector CharCamLoc  = GetActorLocation(); // safe fallback
        FRotator CharCamRot = FRotator::ZeroRotator;
        if (GetWorld())
        {
            if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
            {
                PC->GetPlayerViewPoint(CharCamLoc, CharCamRot);
            }
        }

        // Horizontal direction FROM mesh center TOWARDS character camera.
        FVector DirMeshToChar = FVector(CharCamLoc.X - MeshCenter.X,
                                        CharCamLoc.Y - MeshCenter.Y,
                                        0.f).GetSafeNormal();
        if (DirMeshToChar.IsNearlyZero())
        {
            // Fallback: opposite of booth forward (player should be facing booth)
            DirMeshToChar = -GetActorForwardVector();
            DirMeshToChar.Z = 0.f;
            DirMeshToChar = DirMeshToChar.GetSafeNormal();
        }

        // SpringArm math:
        //   Camera socket = SpringArmPivot + (-ArmLength) * SpringArm.ForwardVector
        //   We want Camera on the SAME SIDE as the character:
        //     Camera = MeshCenter + DirMeshToChar * ArmLength
        //   Solving: SpringArm.ForwardVector = -DirMeshToChar
        //   SpringArmWorldYaw = Yaw(-DirMeshToChar) = DirMeshToChar.Yaw + 180°
        float CharDirYaw = FMath::RadiansToDegrees(FMath::Atan2(DirMeshToChar.Y, DirMeshToChar.X));
        float SpringArmWorldYaw = FRotator::NormalizeAxis(CharDirYaw + 180.f);

        // ── 5. Set arm length based on component type ─────────────────────
        float ArmLength = 180.0f;
        switch (TargetType)
        {
        case EFurnitureComponentType::Faucet:     ArmLength =  80.0f; break;
        case EFurnitureComponentType::Sink:        ArmLength = 120.0f; break;
        case EFurnitureComponentType::Mirror:      ArmLength = 140.0f; break;
        case EFurnitureComponentType::Countertop:  ArmLength = 160.0f; break;
        default:                                   ArmLength = 180.0f; break;
        }
        CurrentZoomLength = ArmLength;

        // ── 6. Place the SpringArm in world space ─────────────────────────
        if (IsValid(SpringArm))
        {
            SpringArm->SetWorldLocation(MeshCenter);
            SpringArm->SetWorldRotation(FRotator(0.f, SpringArmWorldYaw, 0.f));
            SpringArm->TargetArmLength = CurrentZoomLength;
        }

        // ── 7. Store initial state so ResetRotation can restore ───────────
        WIP_InitialSpringArmWorldLoc = MeshCenter;
        WIP_InitialSpringArmWorldRot = FRotator(0.f, SpringArmWorldYaw, 0.f);

        // ── 8. Apply Depth of Field ───────────────────────────────────────
        WIP_ApplyDoF();
    }
    else
    {
        // IsolatedStudio mode — orbit camera around focused component.
        FVector LocalFocusLoc = FVector::ZeroVector;
        if (IsValid(TargetComp) && TargetComp->GetVisibleFlag() && TargetComp->GetStaticMesh())
        {
            FVector WorldLoc = TargetComp->GetComponentLocation();
            LocalFocusLoc = GetActorTransform().InverseTransformPosition(WorldLoc);
        }

        if (IsValid(SpringArm))
        {
            SpringArm->SetRelativeLocation(LocalFocusLoc);
            SpringArm->SetRelativeRotation(FRotator(ActiveConfig.Pitch, ActiveConfig.Yaw, 0.f));
            SpringArm->TargetArmLength = ActiveConfig.FocusDistance;
            CurrentZoomLength = ActiveConfig.FocusDistance;
        }
        UpdateLightIntensityForZoom();
    }

    EnforceLightingSettings();
}

// ── WorldInPlace: Apply Depth of Field using SpringArm arm length as focal distance. ──
// The camera IS the SpringArm camera — arm length = exact distance from lens to pivot = model center.
// So focal distance = arm length always keeps the model pin-sharp.
void AFurniturePreviewActor::WIP_ApplyDoF()
{
    if (!IsValid(Camera)) return;

    // focal distance = SpringArm length (distance from camera to model center)
    float FocalDist = IsValid(SpringArm) ? SpringArm->TargetArmLength : 150.0f;

    // focal region = depth of the mesh bounding box so the whole model stays sharp
    float FocalRegion = 300.0f;
    UStaticMeshComponent* TargetComp = IsValid(CurrentFocusedComponent) ? CurrentFocusedComponent.Get() : CabinetMesh.Get();
    if (IsValid(TargetComp) && TargetComp->GetVisibleFlag() && TargetComp->GetStaticMesh())
    {
        // Full diagonal extent of the bounding box
        FocalRegion = FMath::Max(150.0f, TargetComp->Bounds.BoxExtent.Size() * 2.0f + 50.0f);
    }

    FPostProcessSettings& PP = Camera->PostProcessSettings;
    PP.bOverride_DepthOfFieldFocalDistance  = true;
    PP.DepthOfFieldFocalDistance            = FocalDist;
    PP.bOverride_DepthOfFieldFocalRegion    = true;
    PP.DepthOfFieldFocalRegion              = FocalRegion;
    PP.bOverride_DepthOfFieldFstop          = true;
    PP.DepthOfFieldFstop                    = WorldInPlaceBackgroundBlurFstop;
    PP.bOverride_DepthOfFieldSensorWidth    = true;
    PP.DepthOfFieldSensorWidth              = 35.0f;
    PP.bOverride_DepthOfFieldNearBlurSize   = true;
    PP.DepthOfFieldNearBlurSize             = 0.0f; // ZERO near blur — model is 100% sharp!
}

void AFurniturePreviewActor::SetViewportMode(EPreviewViewportMode NewMode)
{
    ViewportMode = NewMode;
    EnforceLightingSettings();
}

void AFurniturePreviewActor::SetupBackdrop(UStaticMesh* InMesh, UMaterialInterface* InMaterial)
{
    if (IsValid(BackdropMesh))
    {
        if (IsValid(InMesh))     BackdropMesh->SetStaticMesh(InMesh);
        if (IsValid(InMaterial)) BackdropMesh->SetMaterial(0, InMaterial);
    }
}

void AFurniturePreviewActor::UpdateWorldInPlaceModelPosition()
{
    // DEPRECATED — kept for compatibility. Position is now managed by MeshRoot.
}

void AFurniturePreviewActor::UpdateWorldInPlaceDOF()
{
    if (ViewportMode == EPreviewViewportMode::WorldInPlace)
    {
        WIP_ApplyDoF();
    }
}

void AFurniturePreviewActor::RotatePreview(float DeltaYaw, float DeltaPitch)
{
    if (ViewportMode == EPreviewViewportMode::WorldInPlace && IsValid(MeshRoot))
    {
        // ── Clamp pitch to ±45° ───────────────────────────────────────────
        float NewPitch  = FMath::Clamp(WorldInPlacePitch + (-DeltaPitch), -45.f, 45.f);
        float ActualDP  = NewPitch - WorldInPlacePitch;
        WorldInPlacePitch = NewPitch;
        WorldInPlaceYaw  += (-DeltaYaw);

        // ── Rotation pivot = bounding box center of the focused mesh ──────
        // (Updates dynamically as MeshRoot moves, giving true self-rotation.)
        FVector Pivot = WIP_GetFocusPivotWorld();

        // ── Left / Right: rotate around WORLD Z axis (always vertical) ────
        // Left input → model turns left. Right input → model turns right.
        // Roll = 0 always because we rotate around the straight-up world axis.
        if (!FMath::IsNearlyZero(DeltaYaw))
        {
            FQuat YawQ(FVector::UpVector, FMath::DegreesToRadians(-DeltaYaw));
            FVector NewLoc = Pivot + YawQ.RotateVector(MeshRoot->GetComponentLocation() - Pivot);
            MeshRoot->SetWorldLocationAndRotation(NewLoc, YawQ * MeshRoot->GetComponentQuat());
        }

        // ── Up / Down: rotate around CAMERA RIGHT vector ──────────────────
        // Camera right = SpringArm's world-space Y axis.
        // This is always horizontal and perpendicular to the camera view direction,
        // so "up" drag always tilts the top of the model towards the camera,
        // regardless of which direction the character is facing.
        if (!FMath::IsNearlyZero(ActualDP) && IsValid(SpringArm))
        {
            FVector CameraRight = FRotationMatrix(SpringArm->GetComponentRotation()).GetScaledAxis(EAxis::Y);
            FQuat PitchQ(CameraRight, FMath::DegreesToRadians(-ActualDP));
            FVector NewLoc = Pivot + PitchQ.RotateVector(MeshRoot->GetComponentLocation() - Pivot);
            MeshRoot->SetWorldLocationAndRotation(NewLoc, PitchQ * MeshRoot->GetComponentQuat());
        }
        // SpringArm and Camera are NEVER touched — camera stays perpendicular.
    }
    else
    {
        CurrentYaw   += DeltaYaw;
        CurrentPitch = FMath::Clamp(CurrentPitch + DeltaPitch, ActiveConfig.PitchMin, ActiveConfig.PitchMax);
        if (IsValid(SpringArm))
        {
            SpringArm->SetRelativeRotation(FRotator(CurrentPitch, CurrentYaw, 0.f));
        }
    }
}

void AFurniturePreviewActor::ResetRotation()
{
    if (ViewportMode == EPreviewViewportMode::WorldInPlace)
    {
        WorldInPlaceYaw   = 0.f;
        WorldInPlacePitch = 0.f;

        // Restore MeshRoot to identity — meshes return to real booth positions.
        if (IsValid(MeshRoot))
        {
            MeshRoot->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
        }

        // Restore SpringArm to the state captured at SetFocusComponent time.
        if (IsValid(SpringArm))
        {
            SpringArm->SetWorldLocation(WIP_InitialSpringArmWorldLoc);
            SpringArm->SetWorldRotation(WIP_InitialSpringArmWorldRot);
            SpringArm->TargetArmLength = CurrentZoomLength;
        }

        WIP_ApplyDoF();
    }
    else
    {
        CurrentYaw        = ActiveConfig.Yaw;
        CurrentPitch      = ActiveConfig.Pitch;
        CurrentZoomLength = ActiveConfig.FocusDistance;

        if (IsValid(SpringArm))
        {
            SpringArm->SetRelativeRotation(FRotator(ActiveConfig.Pitch, ActiveConfig.Yaw, 0.f));
            SpringArm->TargetArmLength = ActiveConfig.FocusDistance;
        }
        if (IsValid(Camera))
        {
            Camera->FieldOfView = ActiveConfig.CameraFOV;
        }
        UpdateLightIntensityForZoom();
    }
}

void AFurniturePreviewActor::SetInitialRotation(float InYaw, float InPitch)
{
    DefaultYaw   = InYaw;
    DefaultPitch = FMath::Clamp(InPitch, ActiveConfig.PitchMin, ActiveConfig.PitchMax);
    CurrentYaw   = DefaultYaw;
    CurrentPitch = DefaultPitch;

    WorldInPlaceYaw   = 0.f;
    WorldInPlacePitch = 0.f;

    if (IsValid(MeshRoot))
    {
        MeshRoot->SetRelativeRotation(FRotator::ZeroRotator);
    }

    if (IsValid(SpringArm) && ViewportMode != EPreviewViewportMode::WorldInPlace)
    {
        SpringArm->SetRelativeRotation(FRotator(CurrentPitch, CurrentYaw, 0.f));
        SpringArm->TargetArmLength = CurrentZoomLength;
    }
}

void AFurniturePreviewActor::ZoomPreview(float DeltaZoom)
{
    if (ViewportMode == EPreviewViewportMode::WorldInPlace)
    {
        // Zoom = change arm length.
        // Min = 30 cm (before clipping into mesh), Max = 400 cm.
        CurrentZoomLength = FMath::Clamp(CurrentZoomLength + DeltaZoom, 30.0f, 400.0f);
        if (IsValid(SpringArm))
        {
            SpringArm->TargetArmLength = CurrentZoomLength;
        }
        // Update DoF so focus tracks the new distance
        WIP_ApplyDoF();
    }
    else
    {
        CurrentZoomLength = FMath::Clamp(CurrentZoomLength + DeltaZoom, ActiveConfig.ZoomMin, ActiveConfig.ZoomMax);
        if (IsValid(SpringArm))
        {
            SpringArm->TargetArmLength = CurrentZoomLength;
        }
        UpdateLightIntensityForZoom();
    }
}

void AFurniturePreviewActor::SetKeyLightEnabled(bool bEnable)
{
    ActiveConfig.bEnableKeyLight = bEnable;
    EnforceLightingSettings();
}

void AFurniturePreviewActor::SetFillLightEnabled(bool bEnable)
{
    ActiveConfig.bEnableFillLight = bEnable;
    EnforceLightingSettings();
}

void AFurniturePreviewActor::SetRimLightEnabled(bool bEnable)
{
    ActiveConfig.bEnableRimLight = bEnable;
    EnforceLightingSettings();
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

void AFurniturePreviewActor::ApplyComponentMeshAndMaterials(UStaticMeshComponent* Target,
                                                            const FFurnitureComponentOptions& Options,
                                                            int32 SizeIndex,
                                                            int32 ColorIndex)
{
    if (!IsValid(Target) || Target->IsUnreachable())
    {
        return;
    }

    TSoftObjectPtr<UStaticMesh> TargetMeshPtr = nullptr;
    if (Options.Models.IsValidIndex(SizeIndex))
    {
        TargetMeshPtr = Options.Models[SizeIndex].Mesh;
    }
    else if (Options.Models.Num() > 0)
    {
        TargetMeshPtr = Options.Models[0].Mesh;
    }

    if (TargetMeshPtr.IsNull() || TargetMeshPtr.ToSoftObjectPath().ToString().IsEmpty())
    {
        if (IsValid(Target))
        {
            Target->SetStaticMesh(nullptr);
            Target->SetVisibility(false);
            Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
        return;
    }

    UStaticMesh* LoadedMesh = TargetMeshPtr.LoadSynchronous();
    if (IsValid(LoadedMesh) && IsValid(Target))
    {
        Target->SetStaticMesh(LoadedMesh);
        Target->SetVisibility(true);
        Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);

        const int32 NumMaterials = Target->GetNumMaterials();
        for (int32 i = 0; i < NumMaterials; ++i)
        {
            Target->SetMaterial(i, nullptr);
        }

        const FFurnitureColorOption* SelectedColor = nullptr;
        if (Options.Models.IsValidIndex(SizeIndex) && Options.Models[SizeIndex].Colors.IsValidIndex(ColorIndex))
        {
            SelectedColor = &Options.Models[SizeIndex].Colors[ColorIndex];
        }

        if (SelectedColor)
        {
            for (const FFurnitureMaterialSlot& SlotOverride : SelectedColor->MaterialOverrides)
            {
                UMaterialInterface* LoadedMat = SlotOverride.Material.LoadSynchronous();
                if (IsValid(LoadedMat) && IsValid(Target) && Target->GetNumMaterials() > SlotOverride.SlotIndex)
                {
                    Target->SetMaterial(SlotOverride.SlotIndex, LoadedMat);
                }
            }
        }
    }
    else if (IsValid(Target))
    {
        Target->SetStaticMesh(nullptr);
        Target->SetVisibility(false);
        Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}

void AFurniturePreviewActor::ApplyComponentMeshAndMaterials(UStaticMeshComponent* Target,
                                                            const FFurnitureCabinetOptions& Options,
                                                            int32 SizeIndex,
                                                            int32 ColorIndex)
{
    if (!IsValid(Target) || Target->IsUnreachable())
    {
        return;
    }

    TSoftObjectPtr<UStaticMesh> TargetMeshPtr = nullptr;
    if (Options.Sizes.IsValidIndex(SizeIndex))
    {
        TargetMeshPtr = Options.Sizes[SizeIndex];
    }
    else if (Options.Sizes.Num() > 0)
    {
        TargetMeshPtr = Options.Sizes[0];
    }

    if (TargetMeshPtr.IsNull() || TargetMeshPtr.ToSoftObjectPath().ToString().IsEmpty())
    {
        if (IsValid(Target))
        {
            Target->SetStaticMesh(nullptr);
            Target->SetVisibility(false);
            Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
        return;
    }

    UStaticMesh* LoadedMesh = TargetMeshPtr.LoadSynchronous();
    if (IsValid(LoadedMesh) && IsValid(Target))
    {
        Target->SetStaticMesh(LoadedMesh);
        Target->SetVisibility(true);
        Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);

        const int32 NumMaterials = Target->GetNumMaterials();
        for (int32 i = 0; i < NumMaterials; ++i)
        {
            Target->SetMaterial(i, nullptr);
        }

        const FFurnitureColorOption* SelectedColor = nullptr;
        if (Options.Colors.IsValidIndex(ColorIndex))
        {
            SelectedColor = &Options.Colors[ColorIndex];
        }
        else if (Options.Colors.Num() > 0)
        {
            SelectedColor = &Options.Colors[0];
        }

        if (SelectedColor)
        {
            for (const FFurnitureMaterialSlot& SlotOverride : SelectedColor->MaterialOverrides)
            {
                UMaterialInterface* LoadedMat = SlotOverride.Material.LoadSynchronous();
                if (IsValid(LoadedMat) && IsValid(Target) && Target->GetNumMaterials() > SlotOverride.SlotIndex)
                {
                    Target->SetMaterial(SlotOverride.SlotIndex, LoadedMat);
                }
            }
        }
    }
    else if (IsValid(Target))
    {
        Target->SetStaticMesh(nullptr);
        Target->SetVisibility(false);
        Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}

void AFurniturePreviewActor::ApplyDoorMeshAndMaterials(UStaticMeshComponent* Target,
                                                       const FFurnitureDoorGroup& DoorGroup,
                                                       int32 SizeIndex,
                                                       int32 ColorIndex,
                                                       int32 SlotIndex)
{
    if (!IsValid(Target) || Target->IsUnreachable())
    {
        return;
    }

    TSoftObjectPtr<UStaticMesh> TargetMeshPtr = nullptr;
    TArray<FFurnitureMaterialSlot> MaterialOverrides;
    bool bHasSelectedColor = false;

    if (DoorGroup.DoorCount == EDoorCount::OneDoor)
    {
        const FFurnitureSingleDoorConfig& SingleCfg = DoorGroup.SingleDoor;
        if (SingleCfg.Sizes.IsValidIndex(SizeIndex))
        {
            TargetMeshPtr = SingleCfg.Sizes[SizeIndex];
        }
        else if (SingleCfg.Sizes.Num() > 0)
        {
            TargetMeshPtr = SingleCfg.Sizes[0];
        }

        const FFurnitureDoorColorOption* SelectedColor = nullptr;
        if (SingleCfg.Colors.IsValidIndex(ColorIndex))
        {
            SelectedColor = &SingleCfg.Colors[ColorIndex];
        }
        else if (SingleCfg.Colors.Num() > 0)
        {
            SelectedColor = &SingleCfg.Colors[0];
        }

        if (SelectedColor)
        {
            MaterialOverrides = SelectedColor->MaterialOverrides;
            bHasSelectedColor = true;
        }
    }
    else if (DoorGroup.DoorCount == EDoorCount::TwoDoors)
    {
        const FFurnitureDoubleDoorsConfig& DoubleCfg = DoorGroup.DoubleDoors;
        if (DoubleCfg.Sizes.IsValidIndex(SizeIndex))
        {
            TargetMeshPtr = (SlotIndex == 0) ? DoubleCfg.Sizes[SizeIndex].Slot0Mesh : DoubleCfg.Sizes[SizeIndex].Slot1Mesh;
        }
        else if (DoubleCfg.Sizes.Num() > 0)
        {
            TargetMeshPtr = (SlotIndex == 0) ? DoubleCfg.Sizes[0].Slot0Mesh : DoubleCfg.Sizes[0].Slot1Mesh;
        }

        const FFurnitureDoubleDoorsColorOption* SelectedColor = nullptr;
        if (DoubleCfg.Colors.IsValidIndex(ColorIndex))
        {
            SelectedColor = &DoubleCfg.Colors[ColorIndex];
        }
        else if (DoubleCfg.Colors.Num() > 0)
        {
            SelectedColor = &DoubleCfg.Colors[0];
        }

        if (SelectedColor)
        {
            MaterialOverrides = (SlotIndex == 0) ? SelectedColor->Slot0MaterialOverrides : SelectedColor->Slot1MaterialOverrides;
            bHasSelectedColor = true;
        }
    }

    if (TargetMeshPtr.IsNull() || TargetMeshPtr.ToSoftObjectPath().ToString().IsEmpty())
    {
        if (IsValid(Target))
        {
            Target->SetStaticMesh(nullptr);
            Target->SetVisibility(false);
            Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
        return;
    }

    UStaticMesh* LoadedMesh = TargetMeshPtr.LoadSynchronous();
    if (IsValid(LoadedMesh) && IsValid(Target))
    {
        Target->SetStaticMesh(LoadedMesh);
        Target->SetVisibility(true);
        Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);

        const int32 NumMaterials = Target->GetNumMaterials();
        for (int32 i = 0; i < NumMaterials; ++i)
        {
            Target->SetMaterial(i, nullptr);
        }

        if (bHasSelectedColor)
        {
            for (const FFurnitureMaterialSlot& SlotOverride : MaterialOverrides)
            {
                UMaterialInterface* LoadedMat = SlotOverride.Material.LoadSynchronous();
                if (IsValid(LoadedMat) && IsValid(Target) && Target->GetNumMaterials() > SlotOverride.SlotIndex)
                {
                    Target->SetMaterial(SlotOverride.SlotIndex, LoadedMat);
                }
            }
        }
    }
    else if (IsValid(Target))
    {
        Target->SetStaticMesh(nullptr);
        Target->SetVisibility(false);
        Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}

void AFurniturePreviewActor::UpdateLightIntensityForZoom()
{
    if (IsValid(FillLight) && ReferenceZoomDistance > 0.f)
    {
        float ZoomRatio = CurrentZoomLength / ReferenceZoomDistance;
        FillLight->SetIntensity(ActiveBaseFillIntensity * ZoomRatio * ZoomRatio * ActiveConfig.MasterLightIntensityScale);
    }
}

void AFurniturePreviewActor::EnforceLightingSettings()
{
    auto ForceConfigureMesh = [](UStaticMeshComponent* Comp, bool bCastShadow)
    {
        if (IsValid(Comp))
        {
            Comp->SetMobility(EComponentMobility::Movable);
            Comp->SetCastShadow(bCastShadow);
            Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Comp->LightingChannels.bChannel0 = true;
            Comp->LightingChannels.bChannel1 = false;
            Comp->LightingChannels.bChannel2 = false;
        }
    };

    ForceConfigureMesh(CabinetMesh.Get(), CabinetLighting.bCastShadow);
    ForceConfigureMesh(DoorMeshSlot0.Get(), CabinetLighting.bCastShadow);
    ForceConfigureMesh(DoorMeshSlot1.Get(), CabinetLighting.bCastShadow);
    ForceConfigureMesh(ClosetMesh.Get(), ClosetLighting.bCastShadow);
    ForceConfigureMesh(ClosetDoorMeshSlot0.Get(), ClosetLighting.bCastShadow);
    ForceConfigureMesh(ClosetDoorMeshSlot1.Get(), ClosetLighting.bCastShadow);
    ForceConfigureMesh(CountertopMesh.Get(), CountertopLighting.bCastShadow);
    ForceConfigureMesh(SinkMesh.Get(), SinkLighting.bCastShadow);
    ForceConfigureMesh(FaucetMesh.Get(), FaucetLighting.bCastShadow);
    ForceConfigureMesh(MirrorMesh.Get(), MirrorLighting.bCastShadow);
    ForceConfigureMesh(BackdropMesh.Get(), false);

    if (IsValid(BackdropMesh))
    {
        BackdropMesh->SetAffectDynamicIndirectLighting(false);
        BackdropMesh->SetVisibility(ViewportMode == EPreviewViewportMode::IsolatedStudio);
    }

    if (ViewportMode == EPreviewViewportMode::WorldInPlace && IsValid(Camera))
    {
        // DoF is applied via WIP_ApplyDoF() which uses SpringArm arm length as focal distance.
        // SpringArm arm length = exact distance from camera lens to model center at all times.
        WIP_ApplyDoF();
    }
    else if (IsValid(Camera))
    {
        Camera->PostProcessSettings.bOverride_DepthOfFieldFstop = false;
    }

    ApplyLightingConfig(ActiveConfig);
}

void AFurniturePreviewActor::ApplyLightingConfig(const FFurniturePreviewLightingConfig& Config)
{
    ActiveConfig = Config;

    if (IsValid(KeyLight))
    {
        KeyLight->SetLightColor(Config.KeyLightColor);
        KeyLight->SetVisibility(Config.bEnableKeyLight);
        KeyLight->SetIntensity(Config.KeyLightIntensity * Config.MasterLightIntensityScale);
        KeyLight->SetRelativeLocation(Config.KeyLightLocation);
        KeyLight->InnerConeAngle = Config.KeyLightInnerConeAngle;
        KeyLight->OuterConeAngle = Config.KeyLightOuterConeAngle;
        KeyLight->SetAttenuationRadius(Config.AttenuationRadius);
        KeyLight->ShadowBias = Config.ShadowBias;
        KeyLight->ShadowSlopeBias = Config.ShadowSlopeBias;
        KeyLight->ContactShadowLength = Config.ContactShadowLength;

        FVector LookAtTarget = FVector(0.f, 0.f, 50.f) - KeyLight->GetRelativeLocation();
        KeyLight->SetRelativeRotation(LookAtTarget.Rotation());
    }

    if (IsValid(FillLight))
    {
        FillLight->SetLightColor(Config.FillLightColor);
        FillLight->SetVisibility(Config.bEnableFillLight);
        FillLight->SetAttenuationRadius(Config.AttenuationRadius);
        ActiveBaseFillIntensity = Config.FillLightIntensity;
        UpdateLightIntensityForZoom();
    }

    if (IsValid(RimLight))
    {
        RimLight->SetLightColor(Config.RimLightColor);
        RimLight->SetVisibility(Config.bEnableRimLight);
        RimLight->SetIntensity(Config.RimLightIntensity * Config.MasterLightIntensityScale);
        RimLight->SetAttenuationRadius(Config.AttenuationRadius);
    }

    if (IsValid(SkyLight))
    {
        SkyLight->SetLightColor(Config.SkyLightColor);
        SkyLight->SetIntensity(Config.SkyLightIntensity * Config.MasterLightIntensityScale);

        UTextureCube* TargetCubemap = Config.StudioCubemap.LoadSynchronous();
        if (IsValid(TargetCubemap))
        {
            SkyLight->SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;
            SkyLight->Cubemap = TargetCubemap;
        }
        else
        {
            SkyLight->SourceType = ESkyLightSourceType::SLS_CapturedScene;
        }
        SkyLight->RecaptureSky();
    }

    if (IsValid(Camera))
    {
        Camera->FieldOfView = Config.CameraFOV;
    }

    ApplyDirectionalLightScale();
    ApplyWorldPostProcessSettings();
}

void AFurniturePreviewActor::ApplyDirectionalLightScale()
{
    if (!GetWorld())
    {
        return;
    }

    for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
    {
        ADirectionalLight* DirLight = *It;
        if (IsValid(DirLight) && IsValid(DirLight->GetLightComponent()))
        {
            if (SavedDirectionalLightIntensity < 0.f)
            {
                SavedDirectionalLightIntensity = DirLight->GetLightComponent()->Intensity;
            }
            DirLight->GetLightComponent()->SetIntensity(SavedDirectionalLightIntensity * ActiveConfig.DirectionalLightScale);
            CachedDirectionalLight = DirLight;
            break;
        }
    }
}

void AFurniturePreviewActor::RestoreDirectionalLight()
{
    if (ADirectionalLight* DirLight = CachedDirectionalLight.Get())
    {
        if (IsValid(DirLight) && IsValid(DirLight->GetLightComponent()) && SavedDirectionalLightIntensity >= 0.f)
        {
            DirLight->GetLightComponent()->SetIntensity(SavedDirectionalLightIntensity);
        }
    }
    SavedDirectionalLightIntensity = -1.f;
    CachedDirectionalLight = nullptr;
}

void AFurniturePreviewActor::ApplyWorldPostProcessSettings()
{
    if (!GetWorld() || !IsValid(Camera))
    {
        return;
    }

    for (TActorIterator<APostProcessVolume> It(GetWorld()); It; ++It)
    {
        APostProcessVolume* PPVolume = *It;
        if (IsValid(PPVolume))
        {
            if (PPVolume->bUnbound)
            {
                Camera->PostProcessSettings = PPVolume->Settings;
                break;
            }
            else if (USceneComponent* RootComp = PPVolume->GetRootComponent())
            {
                if (RootComp->Bounds.GetBox().IsInside(GetActorLocation()))
                {
                    Camera->PostProcessSettings = PPVolume->Settings;
                    break;
                }
            }
        }
    }
}
