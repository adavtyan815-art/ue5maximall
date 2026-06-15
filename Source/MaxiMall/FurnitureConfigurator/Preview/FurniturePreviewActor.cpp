// Copyright MaxiMall Project. All Rights Reserved.
// FurniturePreviewActor.cpp

#include "FurnitureConfigurator/Preview/FurniturePreviewActor.h"

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
    SinkMesh->SetupAttachment(CountertopMesh);

    FaucetMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Faucet"));
    FaucetMesh->SetupAttachment(CountertopMesh);

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

    // Set default parameter values explicitly in constructor body for CDO persistence
    PitchMin = -80.f;
    PitchMax = 80.f;
    BaseFillIntensity = 40000.f;
    ReferenceZoomDistance = 250.f;
    CurrentZoomLength = 250.f;

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
    Camera->PostProcessSettings.AutoExposureMinBrightness = 3.0f;
    Camera->PostProcessSettings.AutoExposureMaxBrightness = 3.0f;

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

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void AFurniturePreviewActor::LoadProductPreview(const FFurnitureProductRow& ProductData, const FShowroomBoothConfigState& ActiveState)
{
    // ── Cabinet ───────────────────────────────────────────────────────────
    ApplyComponentMeshAndMaterials(CabinetMesh.Get(), ProductData.CabinetOptions, ActiveState.CabinetState);

    // ── Closet ────────────────────────────────────────────────────────────
    ApplyComponentMeshAndMaterials(ClosetMesh.Get(), ProductData.ClosetOptions, ActiveState.ClosetState);

    // ── Doors ─────────────────────────────────────────────────────────────
    ApplyComponentMeshAndMaterials(DoorMeshSlot0.Get(), ProductData.DoorOptions, ActiveState.DoorState);
    ApplyComponentMeshAndMaterials(DoorMeshSlot1.Get(), ProductData.DoorOptions, ActiveState.DoorState);

    switch (ProductData.DoorCount)
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
        if (ProductData.DoorSlots.IsValidIndex(0))
        {
            DoorMeshSlot0->SetRelativeLocation(ProductData.DoorSlots[0].ClosedPositionOffset);
        }
        DoorMeshSlot1->SetVisibility(false);
        break;
    case EDoorCount::TwoDoors:
        if (DoorMeshSlot0->GetStaticMesh())
        {
            DoorMeshSlot0->SetVisibility(true);
        }
        if (ProductData.DoorSlots.IsValidIndex(0))
        {
            DoorMeshSlot0->SetRelativeLocation(ProductData.DoorSlots[0].ClosedPositionOffset);
        }
        if (DoorMeshSlot1->GetStaticMesh())
        {
            DoorMeshSlot1->SetVisibility(true);
        }
        if (ProductData.DoorSlots.IsValidIndex(1))
        {
            DoorMeshSlot1->SetRelativeLocation(ProductData.DoorSlots[1].ClosedPositionOffset);
        }
        break;
    }

    // ── Countertop ────────────────────────────────────────────────────────
    ApplyComponentMeshAndMaterials(CountertopMesh.Get(), ProductData.CountertopOptions, ActiveState.CountertopState);

    // ── Sink ──────────────────────────────────────────────────────────────
    if (ProductData.CountertopType == ECountertopType::SurfaceMounted)
    {
        ApplyComponentMeshAndMaterials(SinkMesh.Get(), ProductData.SinkOptions, ActiveState.SinkState);

        const FSinkPlacementOffset& SO = ProductData.SinkOffset;
        SinkMesh->SetRelativeLocation(SO.RelativeLocation);
        SinkMesh->SetRelativeRotation(SO.RelativeRotation);
        SinkMesh->SetRelativeScale3D(SO.RelativeScale);
    }
    else
    {
        SinkMesh->SetStaticMesh(nullptr);
        SinkMesh->SetVisibility(false);
        SinkMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    // ── Faucet ────────────────────────────────────────────────────────────
    ApplyComponentMeshAndMaterials(FaucetMesh.Get(), ProductData.FaucetOptions, ActiveState.FaucetState);
    {
        const FFaucetPlacementOffset& FO = ProductData.FaucetOffset;
        FaucetMesh->SetRelativeLocation(FO.RelativeLocation);
        FaucetMesh->SetRelativeRotation(FO.RelativeRotation);
        FaucetMesh->SetRelativeScale3D(FO.RelativeScale);
    }

    // ── Mirror ────────────────────────────────────────────────────────────
    ApplyComponentMeshAndMaterials(MirrorMesh.Get(), ProductData.MirrorOptions, ActiveState.MirrorState);
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
    CurrentYaw   = 0.f;
    CurrentPitch = -15.f;
    CurrentZoomLength = 250.f;

    if (SpringArm)
    {
        SpringArm->SetRelativeRotation(FRotator(CurrentPitch, CurrentYaw, 0.f));
        SpringArm->TargetArmLength = CurrentZoomLength;
    }
    UpdateLightIntensityForZoom();
}

// ─────────────────────────────────────────────────────────────────────────────
// Private Helpers
// ─────────────────────────────────────────────────────────────────────────────

void AFurniturePreviewActor::ApplyComponentMeshAndMaterials(UStaticMeshComponent* Target,
                                                            const FFurnitureComponentOptions& Options,
                                                            const FFurnitureComponentState& State)
{
    if (!Target)
    {
        return;
    }

    const FFurnitureSizeOption* SelectedSize = nullptr;
    if (Options.Sizes.Num() > 0)
    {
        for (const FFurnitureSizeOption& SizeOpt : Options.Sizes)
        {
            if (SizeOpt.SizeID == State.SelectedSizeID)
            {
                SelectedSize = &SizeOpt;
                break;
            }
        }
        if (!SelectedSize)
        {
            SelectedSize = &Options.Sizes[0];
        }
    }

    const FFurnitureColorOption* SelectedColor = nullptr;
    if (Options.Colors.Num() > 0)
    {
        for (const FFurnitureColorOption& ColorOpt : Options.Colors)
        {
            if (ColorOpt.ColorID == State.SelectedColorID)
            {
                SelectedColor = &ColorOpt;
                break;
            }
        }
        if (!SelectedColor)
        {
            SelectedColor = &Options.Colors[0];
        }
    }

    if (!SelectedSize || SelectedSize->Mesh.IsNull() || SelectedSize->Mesh.ToSoftObjectPath().ToString().IsEmpty())
    {
        Target->SetStaticMesh(nullptr);
        Target->SetVisibility(false);
        Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        return;
    }

    UStaticMesh* LoadedMesh = SelectedSize->Mesh.LoadSynchronous();
    if (LoadedMesh)
    {
        Target->SetStaticMesh(LoadedMesh);
        Target->SetVisibility(true);
        Target->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

        const int32 NumMaterials = Target->GetNumMaterials();
        for (int32 i = 0; i < NumMaterials; ++i)
        {
            Target->SetMaterial(i, nullptr);
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

void AFurniturePreviewActor::UpdateLightIntensityForZoom()
{
    if (FillLight && ReferenceZoomDistance > 0.f)
    {
        float ScaleFactor = FMath::Square(CurrentZoomLength / ReferenceZoomDistance);
        FillLight->SetIntensity(BaseFillIntensity * ScaleFactor);
    }
}
