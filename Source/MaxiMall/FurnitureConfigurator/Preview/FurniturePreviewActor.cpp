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

// Default render target dimensions (pixels).
static constexpr int32 DefaultRTWidth  = 512;
static constexpr int32 DefaultRTHeight = 512;

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

    // ── Furniture Meshes ──────────────────────────────────────────────────
    CabinetMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cabinet"));
    CabinetMesh->SetupAttachment(PreviewRoot);

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
    MirrorMesh->SetupAttachment(PreviewRoot);

    // Enable self-shadowing but disable collision on all preview meshes, and isolate them to lighting channel 1.
    auto ConfigurePreviewMesh = [](UStaticMeshComponent* Comp)
    {
        if (Comp)
        {
            Comp->SetCastShadow(true);
            Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Comp->LightingChannels.bChannel0 = false;
            Comp->LightingChannels.bChannel1 = true;
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
    ClosetMesh->SetupAttachment(PreviewRoot);
    ConfigurePreviewMesh(ClosetMesh.Get());

    // ── Closet Doors ──────────────────────────────────────────────────────
    ClosetDoorMeshSlot0 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ClosetDoorSlot0"));
    ClosetDoorMeshSlot0->SetupAttachment(ClosetMesh);
    ConfigurePreviewMesh(ClosetDoorMeshSlot0.Get());

    ClosetDoorMeshSlot1 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ClosetDoorSlot1"));
    ClosetDoorMeshSlot1->SetupAttachment(ClosetMesh);
    ConfigurePreviewMesh(ClosetDoorMeshSlot1.Get());

    // Set default parameter values explicitly in constructor body for CDO persistence
    PitchMin = -80.f;
    PitchMax = 80.f;
    BaseFillIntensity = 40000.f;
    ReferenceZoomDistance = 250.f;
    CurrentZoomLength = 250.f;
    DefaultYaw = 0.f;
    DefaultPitch = -15.f;
    CurrentYaw = 0.f;
    CurrentPitch = -15.f;

    // ── Spring Arm & Camera ───────────────────────────────────────────────
    SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
    SpringArm->SetupAttachment(PreviewRoot);
    SpringArm->TargetArmLength = CurrentZoomLength;
    SpringArm->bDoCollisionTest = false;
    // Position spring arm rotation to look slightly down at the furniture
    SpringArm->SetRelativeRotation(FRotator(-15.f, 0.f, 0.f));

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);

    // Lock auto-exposure on the camera by default to prevent transition flashes (customizable in BP details)
    Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
    Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
    Camera->PostProcessSettings.AutoExposureMinBrightness = 1.0f;
    Camera->PostProcessSettings.AutoExposureMaxBrightness = 1.0f;

    // ── Studio Backdrop ───────────────────────────────────────────────────
    BackdropMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BackdropMesh"));
    BackdropMesh->SetupAttachment(PreviewRoot);
    BackdropMesh->SetCastShadow(false);
    BackdropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    // Scale it huge so it encapsulates the camera orbit range (SpringArm TargetArmLength up to 500)
    BackdropMesh->SetRelativeScale3D(FVector(15.f, 15.f, 15.f));

    // ── Studio Lights (Isolated to Channel 1) ─────────────────────────────
    KeyLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("KeyLight"));
    KeyLight->SetupAttachment(PreviewRoot);
    
    // Position KeyLight offset and point it down at the furniture
    KeyLight->SetRelativeLocation(FVector(-300.f, -300.f, 300.f));
    FVector LookAtTarget = FVector(0.f, 0.f, 50.f) - KeyLight->GetRelativeLocation();
    KeyLight->SetRelativeRotation(LookAtTarget.Rotation());
    
    // Configure Spotlight cones and intensity (high Lumen value for studio lighting)
    KeyLight->InnerConeAngle = 30.f;
    KeyLight->OuterConeAngle = 50.f;
    KeyLight->SetIntensity(80000.f);
    KeyLight->SetCastShadows(true);
    KeyLight->LightingChannels.bChannel0 = false;
    KeyLight->LightingChannels.bChannel1 = true;
    KeyLight->LightingChannels.bChannel2 = false;

    // Camera-mounted Fill Light (Headlight)
    FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
    FillLight->SetupAttachment(Camera);
    FillLight->SetIntensity(BaseFillIntensity); // Initialized using base intensity
    FillLight->SetCastShadows(false);
    FillLight->LightingChannels.bChannel0 = false;
    FillLight->LightingChannels.bChannel1 = true;
    FillLight->LightingChannels.bChannel2 = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// BeginPlay
// ─────────────────────────────────────────────────────────────────────────────

void AFurniturePreviewActor::BeginPlay()
{
    Super::BeginPlay();

    // No runtime overrides: respects the Camera component PostProcessSettings configured in Blueprint

    UpdateLightIntensityForZoom();
}

void AFurniturePreviewActor::PostInitializeComponents()
{
    Super::PostInitializeComponents();

    // Force flat attachment hierarchy at runtime to match C++ constructor design and override BP saved hierarchy
    if (SinkMesh && CabinetMesh)
    {
        SinkMesh->AttachToComponent(CabinetMesh, FAttachmentTransformRules::KeepWorldTransform);
    }
    if (FaucetMesh && CabinetMesh)
    {
        FaucetMesh->AttachToComponent(CabinetMesh, FAttachmentTransformRules::KeepWorldTransform);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void AFurniturePreviewActor::LoadProductPreview(const FFurnitureProductRow& ProductData, const FShowroomBoothConfigState& ActiveState, AShowroomBooth* SourceBooth)
{
    if (SourceBooth)
    {
        if (CabinetMesh && SourceBooth->MainCabinet)
        {
            CabinetMesh->SetRelativeTransform(SourceBooth->MainCabinet->GetRelativeTransform());
        }
        if (ClosetMesh && SourceBooth->ClosetMesh)
        {
            ClosetMesh->SetRelativeTransform(SourceBooth->ClosetMesh->GetRelativeTransform());
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
        if (MirrorMesh && SourceBooth->MirrorMesh)
        {
            MirrorMesh->SetRelativeTransform(SourceBooth->MirrorMesh->GetRelativeTransform());
        }
    }

    // ── Cabinet ───────────────────────────────────────────────────────────
    if (!SourceBooth || (SourceBooth->MainCabinet && SourceBooth->MainCabinet->GetStaticMesh() != nullptr))
    {
        ApplyComponentMeshAndMaterials(CabinetMesh.Get(), ProductData.CabinetOptions, ActiveState.ActiveSizeIndex, ActiveState.ActiveColorIndex);
    }
    else
    {
        CabinetMesh->SetStaticMesh(nullptr);
        CabinetMesh->SetVisibility(false);
        CabinetMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    // ── Closet ────────────────────────────────────────────────────────────
    if (!SourceBooth || (SourceBooth->ClosetMesh && SourceBooth->ClosetMesh->GetStaticMesh() != nullptr))
    {
        ApplyComponentMeshAndMaterials(ClosetMesh.Get(), ProductData.ClosetOptions, ActiveState.ClosetSizeIndex, ActiveState.ClosetColorIndex);
    }
    else
    {
        ClosetMesh->SetStaticMesh(nullptr);
        ClosetMesh->SetVisibility(false);
        ClosetMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    // ── Doors ─────────────────────────────────────────────────────────────
    if (!SourceBooth || (SourceBooth->DoorMeshSlot0 && SourceBooth->DoorMeshSlot0->GetStaticMesh() != nullptr))
    {
        const FFurnitureDoorGroup& CabDoors = ProductData.DoorsConfig.CabinetDoors;

        ApplyDoorMeshAndMaterials(DoorMeshSlot0.Get(), CabDoors, ActiveState.ActiveSizeIndex, ActiveState.ActiveColorIndex, 0);
        ApplyDoorMeshAndMaterials(DoorMeshSlot1.Get(), CabDoors, ActiveState.ActiveSizeIndex, ActiveState.ActiveColorIndex, 1);

        switch (CabDoors.DoorCount)
        {
        case EDoorCount::NoDoors:
            DoorMeshSlot0->SetVisibility(false);
            DoorMeshSlot1->SetVisibility(false);
            break;
        case EDoorCount::OneDoor:
            if (DoorMeshSlot0->GetStaticMesh())
            {
                DoorMeshSlot0->SetVisibility(true);
            }
            if (SourceBooth && SourceBooth->DoorMeshSlot0)
            {
                DoorMeshSlot0->SetRelativeTransform(SourceBooth->DoorMeshSlot0->GetRelativeTransform());
            }
            else
            {
                DoorMeshSlot0->SetRelativeLocation(CabDoors.SingleDoor.SlotConfig.ClosedPositionOffset);
            }
            DoorMeshSlot1->SetVisibility(false);
            break;
        case EDoorCount::TwoDoors:
            if (DoorMeshSlot0->GetStaticMesh())
            {
                DoorMeshSlot0->SetVisibility(true);
            }
            if (SourceBooth && SourceBooth->DoorMeshSlot0)
            {
                DoorMeshSlot0->SetRelativeTransform(SourceBooth->DoorMeshSlot0->GetRelativeTransform());
            }
            else
            {
                DoorMeshSlot0->SetRelativeLocation(CabDoors.DoubleDoors.Slot0Config.ClosedPositionOffset);
            }
            if (DoorMeshSlot1->GetStaticMesh())
            {
                DoorMeshSlot1->SetVisibility(true);
            }
            if (SourceBooth && SourceBooth->DoorMeshSlot1)
            {
                DoorMeshSlot1->SetRelativeTransform(SourceBooth->DoorMeshSlot1->GetRelativeTransform());
            }
            else
            {
                DoorMeshSlot1->SetRelativeLocation(CabDoors.DoubleDoors.Slot1Config.ClosedPositionOffset);
            }
            break;
        }
    }
    else
    {
        DoorMeshSlot0->SetStaticMesh(nullptr);
        DoorMeshSlot0->SetVisibility(false);
        DoorMeshSlot0->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        DoorMeshSlot1->SetStaticMesh(nullptr);
        DoorMeshSlot1->SetVisibility(false);
        DoorMeshSlot1->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    // ── Closet Doors ──────────────────────────────────────────────────────
    if (!SourceBooth || (SourceBooth->ClosetDoorMeshSlot0 && SourceBooth->ClosetDoorMeshSlot0->GetStaticMesh() != nullptr))
    {
        const FFurnitureDoorGroup& ClosetDoors = ProductData.DoorsConfig.ClosetDoors;

        ApplyDoorMeshAndMaterials(ClosetDoorMeshSlot0.Get(), ClosetDoors, ActiveState.ClosetSizeIndex, ActiveState.ClosetColorIndex, 0);
        ApplyDoorMeshAndMaterials(ClosetDoorMeshSlot1.Get(), ClosetDoors, ActiveState.ClosetSizeIndex, ActiveState.ClosetColorIndex, 1);

        switch (ClosetDoors.DoorCount)
        {
        case EDoorCount::NoDoors:
            ClosetDoorMeshSlot0->SetVisibility(false);
            ClosetDoorMeshSlot1->SetVisibility(false);
            break;
        case EDoorCount::OneDoor:
            if (ClosetDoorMeshSlot0->GetStaticMesh())
            {
                ClosetDoorMeshSlot0->SetVisibility(true);
            }
            if (SourceBooth && SourceBooth->ClosetDoorMeshSlot0)
            {
                ClosetDoorMeshSlot0->SetRelativeTransform(SourceBooth->ClosetDoorMeshSlot0->GetRelativeTransform());
            }
            else
            {
                ClosetDoorMeshSlot0->SetRelativeLocation(ClosetDoors.SingleDoor.SlotConfig.ClosedPositionOffset);
            }
            ClosetDoorMeshSlot1->SetVisibility(false);
            break;
        case EDoorCount::TwoDoors:
            if (ClosetDoorMeshSlot0->GetStaticMesh())
            {
                ClosetDoorMeshSlot0->SetVisibility(true);
            }
            if (SourceBooth && SourceBooth->ClosetDoorMeshSlot0)
            {
                ClosetDoorMeshSlot0->SetRelativeTransform(SourceBooth->ClosetDoorMeshSlot0->GetRelativeTransform());
            }
            else
            {
                ClosetDoorMeshSlot0->SetRelativeLocation(ClosetDoors.DoubleDoors.Slot0Config.ClosedPositionOffset);
            }
            if (ClosetDoorMeshSlot1->GetStaticMesh())
            {
                ClosetDoorMeshSlot1->SetVisibility(true);
            }
            if (SourceBooth && SourceBooth->ClosetDoorMeshSlot1)
            {
                ClosetDoorMeshSlot1->SetRelativeTransform(SourceBooth->ClosetDoorMeshSlot1->GetRelativeTransform());
            }
            else
            {
                ClosetDoorMeshSlot1->SetRelativeLocation(ClosetDoors.DoubleDoors.Slot1Config.ClosedPositionOffset);
            }
            break;
        }
    }
    else
    {
        ClosetDoorMeshSlot0->SetStaticMesh(nullptr);
        ClosetDoorMeshSlot0->SetVisibility(false);
        ClosetDoorMeshSlot0->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        ClosetDoorMeshSlot1->SetStaticMesh(nullptr);
        ClosetDoorMeshSlot1->SetVisibility(false);
        ClosetDoorMeshSlot1->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    // ── Countertop ────────────────────────────────────────────────────────
    if (!SourceBooth || (SourceBooth->CountertopMesh && SourceBooth->CountertopMesh->GetStaticMesh() != nullptr))
    {
        FFurnitureComponentOptions ResolvedCountertop;
        if (SourceBooth)
        {
            SourceBooth->GetResolvedComponentOptions(EFurnitureComponentType::Countertop, ResolvedCountertop);
        }
        ApplyComponentMeshAndMaterials(CountertopMesh.Get(), ResolvedCountertop, ActiveState.CountertopSizeIndex, ActiveState.ActiveCountertopColorIndex);

        FFurniturePlacementOffset CO = SourceBooth ? SourceBooth->GetActiveCountertopOffset() : FFurniturePlacementOffset();
        FTransform ProductDelta;
        ProductDelta.SetLocation(CO.RelativeLocation);
        ProductDelta.SetRotation(CO.RelativeRotation.Quaternion());
        ProductDelta.SetScale3D(CO.RelativeScale);

        if (SourceBooth && SourceBooth->GetActiveCountertopType() == ECountertopType::BuiltIn)
        {
            // Integrated countertops align perfectly relative to the cabinet base with no baseline offset multiplier needed
            CountertopMesh->SetRelativeTransform(ProductDelta);
        }
        else
        {
            const FTransform BaselineCountertop = SourceBooth ? SourceBooth->GetBaselineCountertopTransform() : FTransform::Identity;
            const FTransform FinalCountertopTransform = ProductDelta * BaselineCountertop;
            CountertopMesh->SetRelativeTransform(FinalCountertopTransform);
        }
    }
    else
    {
        CountertopMesh->SetStaticMesh(nullptr);
        CountertopMesh->SetVisibility(false);
        CountertopMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    // ── Sink ──────────────────────────────────────────────────────────────
    ECountertopType ActiveCountertopType = SourceBooth ? SourceBooth->GetActiveCountertopType() : ECountertopType::SurfaceMounted;
    if (ActiveCountertopType == ECountertopType::SurfaceMounted && 
        (!SourceBooth || (SourceBooth->SinkMesh && SourceBooth->SinkMesh->GetStaticMesh() != nullptr)))
    {
        FFurnitureComponentOptions ResolvedSink;
        if (SourceBooth)
        {
            SourceBooth->GetResolvedComponentOptions(EFurnitureComponentType::Sink, ResolvedSink);
        }
        ApplyComponentMeshAndMaterials(SinkMesh.Get(), ResolvedSink, ActiveState.SinkSizeIndex, ActiveState.SinkColorIndex);

        FFurniturePlacementOffset SO = SourceBooth ? SourceBooth->GetActiveSinkOffset() : FFurniturePlacementOffset();
        FTransform ProductDelta;
        ProductDelta.SetLocation(SO.RelativeLocation);
        ProductDelta.SetRotation(SO.RelativeRotation.Quaternion());
        ProductDelta.SetScale3D(SO.RelativeScale);

        const FTransform BaselineSink = SourceBooth ? SourceBooth->GetBaselineSinkTransform() : FTransform::Identity;
        const FTransform FinalSinkTransform = ProductDelta * BaselineSink;
        SinkMesh->SetRelativeTransform(FinalSinkTransform);
    }
    else
    {
        SinkMesh->SetStaticMesh(nullptr);
        SinkMesh->SetVisibility(false);
        SinkMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    // ── Faucet ────────────────────────────────────────────────────────────
    if (!SourceBooth || (SourceBooth->FaucetMesh && SourceBooth->FaucetMesh->GetStaticMesh() != nullptr))
    {
        FFurnitureComponentOptions ResolvedFaucet;
        if (SourceBooth)
        {
            SourceBooth->GetResolvedComponentOptions(EFurnitureComponentType::Faucet, ResolvedFaucet);
        }
        ApplyComponentMeshAndMaterials(FaucetMesh.Get(), ResolvedFaucet, ActiveState.FaucetSizeIndex, ActiveState.FaucetColorIndex);
        {
            FFurniturePlacementOffset FO = SourceBooth ? SourceBooth->GetActiveFaucetOffset() : FFurniturePlacementOffset();
            FTransform ProductDelta;
            ProductDelta.SetLocation(FO.RelativeLocation);
            ProductDelta.SetRotation(FO.RelativeRotation.Quaternion());
            ProductDelta.SetScale3D(FO.RelativeScale);

            if (SourceBooth && SourceBooth->GetActiveCountertopType() == ECountertopType::BuiltIn)
            {
                // Integrated faucets align perfectly relative to the cabinet base with no baseline offset multiplier needed
                FaucetMesh->SetRelativeTransform(ProductDelta);
            }
            else
            {
                const FTransform BaselineFaucet = SourceBooth ? SourceBooth->GetBaselineFaucetTransform() : FTransform::Identity;
                const FTransform FinalFaucetTransform = ProductDelta * BaselineFaucet;
                FaucetMesh->SetRelativeTransform(FinalFaucetTransform);
            }
        }
    }
    else
    {
        FaucetMesh->SetStaticMesh(nullptr);
        FaucetMesh->SetVisibility(false);
        FaucetMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    // ── Mirror ────────────────────────────────────────────────────────────
    if (!SourceBooth || (SourceBooth->MirrorMesh && SourceBooth->MirrorMesh->GetStaticMesh() != nullptr))
    {
        FFurnitureComponentOptions ResolvedMirror;
        if (SourceBooth)
        {
            SourceBooth->GetResolvedComponentOptions(EFurnitureComponentType::Mirror, ResolvedMirror);
        }
        ApplyComponentMeshAndMaterials(MirrorMesh.Get(), ResolvedMirror, ActiveState.MirrorSizeIndex, ActiveState.MirrorColorIndex);
        {
            FFurniturePlacementOffset MO = SourceBooth ? SourceBooth->GetActiveMirrorOffset() : FFurniturePlacementOffset();
            FTransform ProductDelta;
            ProductDelta.SetLocation(MO.RelativeLocation);
            ProductDelta.SetRotation(MO.RelativeRotation.Quaternion());
            ProductDelta.SetScale3D(MO.RelativeScale);

            const FTransform BaselineMirror = SourceBooth ? SourceBooth->GetBaselineMirrorTransform() : FTransform::Identity;
            const FTransform FinalMirrorTransform = ProductDelta * BaselineMirror;
            MirrorMesh->SetRelativeTransform(FinalMirrorTransform);
        }
    }
    else
    {
        MirrorMesh->SetStaticMesh(nullptr);
        MirrorMesh->SetVisibility(false);
        MirrorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}

void AFurniturePreviewActor::SetFocusComponent(EFurnitureComponentType ComponentType)
{
    UStaticMeshComponent* TargetComponent = nullptr;
    float DefaultZoomDistance = 250.f;

    switch (ComponentType)
    {
    case EFurnitureComponentType::Cabinet:
        TargetComponent = CabinetMesh.Get();
        DefaultZoomDistance = 250.f;
        break;
    case EFurnitureComponentType::Closet:
        TargetComponent = ClosetMesh.Get();
        DefaultZoomDistance = 250.f;
        break;
    case EFurnitureComponentType::Doors:
        TargetComponent = DoorMeshSlot0.Get();
        DefaultZoomDistance = 200.f;
        break;
    case EFurnitureComponentType::Countertop:
        TargetComponent = CountertopMesh.Get();
        DefaultZoomDistance = 200.f;
        break;
    case EFurnitureComponentType::Sink:
        TargetComponent = SinkMesh.Get();
        DefaultZoomDistance = 150.f;
        break;
    case EFurnitureComponentType::Faucet:
        TargetComponent = FaucetMesh.Get();
        DefaultZoomDistance = 100.f;
        break;
    case EFurnitureComponentType::Mirror:
        TargetComponent = MirrorMesh.Get();
        DefaultZoomDistance = 150.f;
        break;
    case EFurnitureComponentType::None:
    default:
        break;
    }

    if (SpringArm)
    {
        if (TargetComponent && TargetComponent->GetVisibleFlag() && TargetComponent->GetStaticMesh())
        {
            FVector WorldLoc = TargetComponent->GetComponentLocation();
            FVector LocalFocusLoc = GetActorTransform().InverseTransformPosition(WorldLoc);
            SpringArm->SetRelativeLocation(LocalFocusLoc);
            SpringArm->TargetArmLength = DefaultZoomDistance;
            CurrentZoomLength = DefaultZoomDistance;
        }
        else
        {
            SpringArm->SetRelativeLocation(FVector::ZeroVector);
            SpringArm->TargetArmLength = 250.f;
            CurrentZoomLength = 250.f;
        }
        UpdateLightIntensityForZoom();
    }
}

void AFurniturePreviewActor::SetupBackdrop(UStaticMesh* InMesh, UMaterialInterface* InMaterial)
{
    if (!BackdropMesh)
    {
        return;
    }

    if (InMesh)
    {
        BackdropMesh->SetStaticMesh(InMesh);
    }

    if (InMaterial)
    {
        BackdropMesh->SetMaterial(0, InMaterial);
    }
}

void AFurniturePreviewActor::RotatePreview(float DeltaYaw, float DeltaPitch)
{
    CurrentYaw   += DeltaYaw;
    CurrentPitch  = FMath::Clamp(CurrentPitch + DeltaPitch, PitchMin, PitchMax);

    if (SpringArm)
    {
        SpringArm->SetRelativeRotation(FRotator(CurrentPitch, CurrentYaw, 0.f));
    }
}

void AFurniturePreviewActor::ZoomPreview(float DeltaZoom)
{
    CurrentZoomLength = FMath::Clamp(CurrentZoomLength + DeltaZoom, ZoomMin, ZoomMax);
    if (SpringArm)
    {
        SpringArm->TargetArmLength = CurrentZoomLength;
    }
    UpdateLightIntensityForZoom();
}

void AFurniturePreviewActor::ResetRotation()
{
    CurrentYaw   = DefaultYaw;
    CurrentPitch = DefaultPitch;
    CurrentZoomLength = 250.f;

    if (SpringArm)
    {
        SpringArm->SetRelativeRotation(FRotator(CurrentPitch, CurrentYaw, 0.f));
        SpringArm->TargetArmLength = CurrentZoomLength;
    }
    UpdateLightIntensityForZoom();
}

void AFurniturePreviewActor::SetInitialRotation(float InYaw, float InPitch)
{
    DefaultYaw = InYaw;
    DefaultPitch = FMath::Clamp(InPitch, PitchMin, PitchMax);
    CurrentYaw = DefaultYaw;
    CurrentPitch = DefaultPitch;

    if (SpringArm)
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
    if (!Target)
    {
        return;
    }

    TSoftObjectPtr<UStaticMesh> TargetMeshPtr;
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
        Target->SetStaticMesh(nullptr);
        Target->SetVisibility(false);
        Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        return;
    }

    UStaticMesh* LoadedMesh = TargetMeshPtr.LoadSynchronous();
    if (LoadedMesh)
    {
        Target->SetStaticMesh(LoadedMesh);
        Target->SetVisibility(true);
        Target->SetCollisionEnabled(ECollisionEnabled::NoCollision); // Keep no collision in preview

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
                if (LoadedMat && Target->GetNumMaterials() > SlotOverride.SlotIndex)
                {
                    Target->SetMaterial(SlotOverride.SlotIndex, LoadedMat);
                }
            }
        }
    }
    else
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
    if (!Target)
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
        Target->SetStaticMesh(nullptr);
        Target->SetVisibility(false);
        Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        return;
    }

    UStaticMesh* LoadedMesh = TargetMeshPtr.LoadSynchronous();
    if (LoadedMesh)
    {
        Target->SetStaticMesh(LoadedMesh);
        Target->SetVisibility(true);
        Target->SetCollisionEnabled(ECollisionEnabled::NoCollision); // Keep no collision in preview

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
                if (LoadedMat && Target->GetNumMaterials() > SlotOverride.SlotIndex)
                {
                    Target->SetMaterial(SlotOverride.SlotIndex, LoadedMat);
                }
            }
        }
    }
    else
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
    if (!Target)
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
        Target->SetStaticMesh(nullptr);
        Target->SetVisibility(false);
        Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        return;
    }

    UStaticMesh* LoadedMesh = TargetMeshPtr.LoadSynchronous();
    if (LoadedMesh)
    {
        Target->SetStaticMesh(LoadedMesh);
        Target->SetVisibility(true);
        Target->SetCollisionEnabled(ECollisionEnabled::NoCollision); // Keep no collision in preview

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
                if (LoadedMat && Target->GetNumMaterials() > SlotOverride.SlotIndex)
                {
                    Target->SetMaterial(SlotOverride.SlotIndex, LoadedMat);
                }
            }
        }
    }
    else
    {
        Target->SetStaticMesh(nullptr);
        Target->SetVisibility(false);
        Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}

void AFurniturePreviewActor::UpdateLightIntensityForZoom()
{
    if (FillLight && ReferenceZoomDistance > 0.f)
    {
        float ScaleFactor = FMath::Square(CurrentZoomLength / ReferenceZoomDistance);
        FillLight->SetIntensity(BaseFillIntensity * ScaleFactor);
    }
}
