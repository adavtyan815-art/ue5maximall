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
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "EngineUtils.h"

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

    PitchMin = -80.f;
    PitchMax = 80.f;
    DefaultCameraDistance = 250.f;
    CameraFOV = 65.f;
    ZoomMin = 100.f;
    ZoomMax = 500.f;
    CabinetFocusDistance = 250.f;
    ClosetFocusDistance = 250.f;
    DoorsFocusDistance = 200.f;
    CountertopFocusDistance = 200.f;
    SinkFocusDistance = 150.f;
    FaucetFocusDistance = 100.f;
    MirrorFocusDistance = 150.f;
    ActiveBaseFillIntensity = 10000.f;
    ReferenceZoomDistance = 250.f;
    CurrentZoomLength = DefaultCameraDistance;
    DefaultYaw = 0.f;
    DefaultPitch = -15.f;
    CurrentYaw = 0.f;
    CurrentPitch = -15.f;

    KeyLightColor = FLinearColor::White;
    FillLightColor = FLinearColor::White;
    RimLightColor = FLinearColor::White;

    bEnableKeyLight = false;
    bEnableFillLight = false;
    bEnableRimLight = false;
    PreviewDirectionalLightIntensityScale = 1.0f;

    // Profiles
    CabinetLighting.KeyLightIntensity = 80000.f;
    CabinetLighting.FillLightIntensity = 10000.f;
    CabinetLighting.KeyLightLocation = FVector(-300.f, -300.f, 300.f);

    ClosetLighting.KeyLightIntensity = 80000.f;
    ClosetLighting.FillLightIntensity = 10000.f;

    CountertopLighting.KeyLightIntensity = 60000.f;
    CountertopLighting.FillLightIntensity = 10000.f;

    SinkLighting.KeyLightIntensity = 50000.f;
    SinkLighting.FillLightIntensity = 10000.f;

    FaucetLighting.KeyLightIntensity = 40000.f;
    FaucetLighting.FillLightIntensity = 10000.f;

    MirrorLighting.KeyLightIntensity = 60000.f;
    MirrorLighting.FillLightIntensity = 10000.f;

    // ── Spring Arm & Camera ───────────────────────────────────────────────
    SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
    SpringArm->SetupAttachment(PreviewRoot);
    SpringArm->TargetArmLength = CurrentZoomLength;
    SpringArm->bDoCollisionTest = false;
    SpringArm->SetRelativeRotation(FRotator(-15.f, 0.f, 0.f));

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);

    // ── Instant Exposure Settings (Eliminates gradual light adaptation delay) ──
    Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
    Camera->PostProcessSettings.AutoExposureMinBrightness = 1.0f;

    Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
    Camera->PostProcessSettings.AutoExposureMaxBrightness = 1.0f;

    Camera->PostProcessSettings.bOverride_AutoExposureSpeedUp = true;
    Camera->PostProcessSettings.AutoExposureSpeedUp = 100.0f;

    Camera->PostProcessSettings.bOverride_AutoExposureSpeedDown = true;
    Camera->PostProcessSettings.AutoExposureSpeedDown = 100.0f;

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
    KeyLight->SetVisibility(bEnableKeyLight);
    KeyLight->SetLightColor(KeyLightColor);
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
    FillLight->SetVisibility(bEnableFillLight);
    FillLight->SetLightColor(FillLightColor);
    FillLight->bUseTemperature = false;
    FillLight->SetIntensity(ActiveBaseFillIntensity);
    FillLight->SetCastShadows(false);
    FillLight->LightingChannels.bChannel0 = true;

    // ── Rim Light (Placed behind model to separate from backdrop) ───────────
    RimLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("RimLight"));
    RimLight->SetupAttachment(PreviewRoot);
    RimLight->SetVisibility(bEnableRimLight);
    RimLight->SetLightColor(RimLightColor);
    RimLight->bUseTemperature = false;

    RimLight->SetRelativeLocation(FVector(200.f, 200.f, 250.f));
    FVector RimLookAtTarget = FVector(0.f, 0.f, 50.f) - RimLight->GetRelativeLocation();
    RimLight->SetRelativeRotation(RimLookAtTarget.Rotation());

    RimLight->InnerConeAngle = 30.f;
    RimLight->OuterConeAngle = 60.f;
    RimLight->SetIntensity(30000.f);
    RimLight->SetCastShadows(false);
    RimLight->LightingChannels.bChannel0 = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// BeginPlay
// ─────────────────────────────────────────────────────────────────────────────

void AFurniturePreviewActor::BeginPlay()
{
    Super::BeginPlay();

    CurrentZoomLength = DefaultCameraDistance;
    if (IsValid(SpringArm))
    {
        SpringArm->TargetArmLength = CurrentZoomLength;
    }
    if (IsValid(Camera))
    {
        Camera->FieldOfView = CameraFOV;
    }

    UpdateLightIntensityForZoom();
    EnforceLightingSettings();
    ApplyDirectionalLightScale();
}

void AFurniturePreviewActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);
    RestoreDirectionalLight();
}

void AFurniturePreviewActor::PostInitializeComponents()
{
    Super::PostInitializeComponents();

    CurrentZoomLength = DefaultCameraDistance;
    if (IsValid(SpringArm))
    {
        SpringArm->TargetArmLength = CurrentZoomLength;
    }
    if (IsValid(Camera))
    {
        Camera->FieldOfView = CameraFOV;
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
    
    CurrentZoomLength = DefaultCameraDistance;
    if (IsValid(SpringArm))
    {
        SpringArm->TargetArmLength = CurrentZoomLength;
    }
    if (IsValid(Camera))
    {
        Camera->FieldOfView = CameraFOV;
    }

    EnforceLightingSettings();
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void AFurniturePreviewActor::LoadProductPreview(const FFurnitureProductRow& ProductData, const FShowroomBoothConfigState& ActiveState, AShowroomBooth* SourceBooth)
{
    if (IsValid(SourceBooth))
    {
        if (IsValid(CabinetMesh) && IsValid(SourceBooth->MainCabinet))
        {
            CabinetMesh->SetRelativeTransform(SourceBooth->MainCabinet->GetRelativeTransform());
        }
        if (IsValid(ClosetMesh) && IsValid(SourceBooth->ClosetMesh))
        {
            ClosetMesh->SetRelativeTransform(SourceBooth->ClosetMesh->GetRelativeTransform());
        }
        if (IsValid(ClosetDoorMeshSlot0) && IsValid(SourceBooth->ClosetDoorMeshSlot0))
        {
            ClosetDoorMeshSlot0->SetRelativeTransform(SourceBooth->ClosetDoorMeshSlot0->GetRelativeTransform());
        }
        if (IsValid(ClosetDoorMeshSlot1) && IsValid(SourceBooth->ClosetDoorMeshSlot1))
        {
            ClosetDoorMeshSlot1->SetRelativeTransform(SourceBooth->ClosetDoorMeshSlot1->GetRelativeTransform());
        }
        if (IsValid(CountertopMesh) && IsValid(SourceBooth->CountertopMesh))
        {
            CountertopMesh->SetRelativeTransform(SourceBooth->CountertopMesh->GetRelativeTransform());
        }
        if (IsValid(MirrorMesh) && IsValid(SourceBooth->MirrorMesh))
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

void AFurniturePreviewActor::SetFocusComponent(EFurnitureComponentType ComponentType)
{
    UStaticMeshComponent* TargetComponent = nullptr;
    float DefaultZoomDistance = DefaultCameraDistance;

    FFurniturePreviewLightingConfig SelectedConfig;

    switch (ComponentType)
    {
    case EFurnitureComponentType::Cabinet:
        TargetComponent = CabinetMesh.Get();
        DefaultZoomDistance = (CabinetFocusDistance == 250.f) ? DefaultCameraDistance : CabinetFocusDistance;
        SelectedConfig = CabinetLighting;
        break;
    case EFurnitureComponentType::Closet:
        TargetComponent = ClosetMesh.Get();
        DefaultZoomDistance = (ClosetFocusDistance == 250.f) ? DefaultCameraDistance : ClosetFocusDistance;
        SelectedConfig = ClosetLighting;
        break;
    case EFurnitureComponentType::Doors:
        TargetComponent = DoorMeshSlot0.Get();
        DefaultZoomDistance = DoorsFocusDistance;
        SelectedConfig = CabinetLighting;
        break;
    case EFurnitureComponentType::Countertop:
        TargetComponent = CountertopMesh.Get();
        DefaultZoomDistance = CountertopFocusDistance;
        SelectedConfig = CountertopLighting;
        break;
    case EFurnitureComponentType::Sink:
        TargetComponent = SinkMesh.Get();
        DefaultZoomDistance = SinkFocusDistance;
        SelectedConfig = SinkLighting;
        break;
    case EFurnitureComponentType::Faucet:
        TargetComponent = FaucetMesh.Get();
        DefaultZoomDistance = FaucetFocusDistance;
        SelectedConfig = FaucetLighting;
        break;
    case EFurnitureComponentType::Mirror:
        TargetComponent = MirrorMesh.Get();
        DefaultZoomDistance = MirrorFocusDistance;
        SelectedConfig = MirrorLighting;
        break;
    case EFurnitureComponentType::None:
    default:
        SelectedConfig = CabinetLighting;
        break;
    }

    ApplyLightingConfig(SelectedConfig);

    CurrentFocusedComponent = TargetComponent;

    FVector LocalFocusLoc = FVector::ZeroVector;
    if (IsValid(TargetComponent) && TargetComponent->GetVisibleFlag() && TargetComponent->GetStaticMesh())
    {
        FVector WorldLoc = TargetComponent->GetComponentLocation();
        LocalFocusLoc = GetActorTransform().InverseTransformPosition(WorldLoc);
    }

    if (IsValid(MeshRoot))
    {
        MeshRoot->SetRelativeRotation(FRotator::ZeroRotator);
    }

    if (IsValid(SpringArm))
    {
        SpringArm->SetRelativeRotation(FRotator(-15.f, 0.f, 0.f));
        if (IsValid(TargetComponent) && TargetComponent->GetVisibleFlag() && TargetComponent->GetStaticMesh())
        {
            SpringArm->SetRelativeLocation(LocalFocusLoc);
            SpringArm->TargetArmLength = DefaultZoomDistance;
            CurrentZoomLength = DefaultZoomDistance;
        }
        else
        {
            SpringArm->SetRelativeLocation(FVector::ZeroVector);
            SpringArm->TargetArmLength = DefaultCameraDistance;
            CurrentZoomLength = DefaultCameraDistance;
        }
        UpdateLightIntensityForZoom();
    }
}

void AFurniturePreviewActor::SetupBackdrop(UStaticMesh* InMesh, UMaterialInterface* InMaterial)
{
    if (!IsValid(BackdropMesh))
    {
        return;
    }

    if (IsValid(InMesh))
    {
        BackdropMesh->SetStaticMesh(InMesh);
    }

    if (IsValid(InMaterial))
    {
        BackdropMesh->SetMaterial(0, InMaterial);
    }
}

void AFurniturePreviewActor::RotatePreview(float DeltaYaw, float DeltaPitch)
{
    CurrentYaw   += DeltaYaw;
    CurrentPitch  = FMath::Clamp(CurrentPitch + DeltaPitch, PitchMin, PitchMax);

    if (IsValid(SpringArm))
    {
        SpringArm->SetRelativeRotation(FRotator(CurrentPitch, CurrentYaw, 0.f));
    }
}

void AFurniturePreviewActor::ZoomPreview(float DeltaZoom)
{
    CurrentZoomLength = FMath::Clamp(CurrentZoomLength + DeltaZoom, ZoomMin, ZoomMax);
    if (IsValid(SpringArm))
    {
        SpringArm->TargetArmLength = CurrentZoomLength;
    }
    UpdateLightIntensityForZoom();
}

void AFurniturePreviewActor::SetKeyLightEnabled(bool bEnable)
{
    bEnableKeyLight = bEnable;
    EnforceLightingSettings();
}

void AFurniturePreviewActor::SetFillLightEnabled(bool bEnable)
{
    bEnableFillLight = bEnable;
    EnforceLightingSettings();
}

void AFurniturePreviewActor::SetRimLightEnabled(bool bEnable)
{
    bEnableRimLight = bEnable;
    EnforceLightingSettings();
}

void AFurniturePreviewActor::ResetRotation()
{
    CurrentYaw   = DefaultYaw;
    CurrentPitch = DefaultPitch;
    CurrentZoomLength = DefaultCameraDistance;

    if (IsValid(MeshRoot))
    {
        MeshRoot->SetRelativeRotation(FRotator::ZeroRotator);
    }

    if (IsValid(SpringArm))
    {
        SpringArm->SetRelativeRotation(FRotator(CurrentPitch, CurrentYaw, 0.f));
        SpringArm->TargetArmLength = CurrentZoomLength;
    }
    if (IsValid(Camera))
    {
        Camera->FieldOfView = CameraFOV;
    }
    UpdateLightIntensityForZoom();
}

void AFurniturePreviewActor::SetInitialRotation(float InYaw, float InPitch)
{
    DefaultYaw = InYaw;
    DefaultPitch = FMath::Clamp(InPitch, PitchMin, PitchMax);
    CurrentYaw = DefaultYaw;
    CurrentPitch = DefaultPitch;

    if (IsValid(MeshRoot))
    {
        MeshRoot->SetRelativeRotation(FRotator::ZeroRotator);
    }

    if (IsValid(SpringArm))
    {
        SpringArm->SetRelativeRotation(FRotator(CurrentPitch, CurrentYaw, 0.f));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Private Helpers
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

    TSoftObjectPtr<UStaticMesh> TargetMeshPtr;
    int32 ActiveModelIndex = -1;
    if (Options.Models.IsValidIndex(SizeIndex))
    {
        TargetMeshPtr = Options.Models[SizeIndex].Mesh;
        ActiveModelIndex = SizeIndex;
    }
    else if (Options.Models.Num() > 0)
    {
        TargetMeshPtr = Options.Models[0].Mesh;
        ActiveModelIndex = 0;
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
        if (Options.Models.IsValidIndex(SizeIndex))
        {
            const TArray<FFurnitureColorOption>& Colors = Options.Models[SizeIndex].Colors;
            if (Colors.IsValidIndex(ColorIndex))
            {
                SelectedColor = &Colors[ColorIndex];
            }
            else if (Colors.Num() > 0)
            {
                SelectedColor = &Colors[0];
            }
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

        if (Target == MirrorMesh.Get() && Options.Models.IsValidIndex(ActiveModelIndex))
        {
            const FFurnitureModelOption& ActiveOption = Options.Models[ActiveModelIndex];
            if (!ActiveOption.MirrorMaterialOverride.IsNull())
            {
                UMaterialInterface* MatteMat = ActiveOption.MirrorMaterialOverride.LoadSynchronous();
                int32 SlotIdx = ActiveOption.MirrorMaterialSlotIndex;
                if (IsValid(MatteMat) && IsValid(Target) && SlotIdx >= 0 && SlotIdx < NumMaterials)
                {
                    Target->SetMaterial(SlotIdx, MatteMat);
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

    TSoftObjectPtr<UStaticMesh> TargetMeshPtr;
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

        TArray<FFurnitureColorOption> FilteredColors;
        for (const FFurnitureColorOption& ColorOpt : Options.Colors)
        {
            if (ColorOpt.SizeIndices.Num() == 0 || ColorOpt.SizeIndices.Contains(SizeIndex))
            {
                FilteredColors.Add(ColorOpt);
            }
        }

        const FFurnitureColorOption* SelectedColor = nullptr;
        if (FilteredColors.IsValidIndex(ColorIndex))
        {
            SelectedColor = &FilteredColors[ColorIndex];
        }
        else if (FilteredColors.Num() > 0)
        {
            SelectedColor = &FilteredColors[0];
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
        FillLight->SetIntensity(ActiveBaseFillIntensity * ZoomRatio * ZoomRatio);
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

    ForceConfigureMesh(CabinetMesh.Get(), bCabinetCastShadow);
    ForceConfigureMesh(DoorMeshSlot0.Get(), bCabinetCastShadow);
    ForceConfigureMesh(DoorMeshSlot1.Get(), bCabinetCastShadow);
    ForceConfigureMesh(ClosetMesh.Get(), bClosetCastShadow);
    ForceConfigureMesh(ClosetDoorMeshSlot0.Get(), bClosetCastShadow);
    ForceConfigureMesh(ClosetDoorMeshSlot1.Get(), bClosetCastShadow);
    ForceConfigureMesh(CountertopMesh.Get(), bCountertopCastShadow);
    ForceConfigureMesh(SinkMesh.Get(), bSinkCastShadow);
    ForceConfigureMesh(FaucetMesh.Get(), bFaucetCastShadow);
    ForceConfigureMesh(MirrorMesh.Get(), bMirrorCastShadow);
    ForceConfigureMesh(BackdropMesh.Get(), false);

    if (IsValid(BackdropMesh))
    {
        BackdropMesh->SetAffectDynamicIndirectLighting(false);
    }

    if (IsValid(KeyLight))
    {
        KeyLight->SetLightColor(KeyLightColor);
        KeyLight->bUseTemperature = false;
        KeyLight->SetVisibility(bEnableKeyLight);
        KeyLight->LightingChannels.bChannel0 = true;
    }

    if (IsValid(FillLight))
    {
        FillLight->SetLightColor(FillLightColor);
        FillLight->bUseTemperature = false;
        FillLight->SetVisibility(bEnableFillLight);
        FillLight->LightingChannels.bChannel0 = true;
        UpdateLightIntensityForZoom();
    }

    if (IsValid(RimLight))
    {
        RimLight->SetLightColor(RimLightColor);
        RimLight->bUseTemperature = false;
        RimLight->SetVisibility(bEnableRimLight);
        RimLight->LightingChannels.bChannel0 = true;
    }
}

void AFurniturePreviewActor::ApplyLightingConfig(const FFurniturePreviewLightingConfig& Config)
{
    if (!IsValid(KeyLight) || !IsValid(FillLight))
    {
        return;
    }

    KeyLight->SetIntensity(Config.KeyLightIntensity);
    FillLight->SetIntensity(Config.FillLightIntensity);
    KeyLight->SetRelativeLocation(Config.KeyLightLocation);
    KeyLight->InnerConeAngle = Config.KeyLightInnerConeAngle;
    KeyLight->OuterConeAngle = Config.KeyLightOuterConeAngle;
    KeyLight->SetAttenuationRadius(Config.AttenuationRadius);
    FillLight->SetAttenuationRadius(Config.AttenuationRadius);
    KeyLight->ShadowBias = Config.ShadowBias;
    KeyLight->ShadowSlopeBias = Config.ShadowSlopeBias;
    KeyLight->ContactShadowLength = Config.ContactShadowLength;

    FVector LookAtTarget = FVector(0.f, 0.f, 50.f) - KeyLight->GetRelativeLocation();
    KeyLight->SetRelativeRotation(LookAtTarget.Rotation());

    ActiveBaseFillIntensity = Config.FillLightIntensity;
    UpdateLightIntensityForZoom();
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
            SavedDirectionalLightIntensity = DirLight->GetLightComponent()->Intensity;
            DirLight->GetLightComponent()->SetIntensity(SavedDirectionalLightIntensity * PreviewDirectionalLightIntensityScale);
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
