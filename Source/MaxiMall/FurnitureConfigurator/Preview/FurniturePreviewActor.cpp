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

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void AFurniturePreviewActor::LoadProductPreview(const FFurnitureProductRow& ProductData)
{
    // ── Cabinet ───────────────────────────────────────────────────────────
    ApplyMeshAndMaterials(CabinetMesh.Get(), ProductData.CabinetBody);

    // ── Doors ─────────────────────────────────────────────────────────────
    ApplyMeshAndMaterials(DoorMeshSlot0.Get(), ProductData.DoorMesh);
    ApplyMeshAndMaterials(DoorMeshSlot1.Get(), ProductData.DoorMesh);

    switch (ProductData.DoorCount)
    {
    case EDoorCount::NoDoors:
        DoorMeshSlot0->SetVisibility(false);
        DoorMeshSlot1->SetVisibility(false);
        break;
    case EDoorCount::OneDoor:
        DoorMeshSlot0->SetVisibility(true);
        if (ProductData.DoorSlots.IsValidIndex(0))
        {
            DoorMeshSlot0->SetRelativeLocation(ProductData.DoorSlots[0].ClosedPositionOffset);
        }
        DoorMeshSlot1->SetVisibility(false);
        break;
    case EDoorCount::TwoDoors:
        DoorMeshSlot0->SetVisibility(true);
        if (ProductData.DoorSlots.IsValidIndex(0))
        {
            DoorMeshSlot0->SetRelativeLocation(ProductData.DoorSlots[0].ClosedPositionOffset);
        }
        DoorMeshSlot1->SetVisibility(true);
        if (ProductData.DoorSlots.IsValidIndex(1))
        {
            DoorMeshSlot1->SetRelativeLocation(ProductData.DoorSlots[1].ClosedPositionOffset);
        }
        break;
    }

    // ── Countertop ────────────────────────────────────────────────────────
    ApplyMeshAndMaterials(CountertopMesh.Get(), ProductData.CountertopMesh);

    // ── Sink ──────────────────────────────────────────────────────────────
    if (ProductData.CountertopType == ECountertopType::SurfaceMounted)
    {
        ApplyMeshAndMaterials(SinkMesh.Get(), ProductData.SinkMesh);
        SinkMesh->SetVisibility(true);

        const FSinkPlacementOffset& SO = ProductData.SinkOffset;
        SinkMesh->SetRelativeLocation(SO.RelativeLocation);
        SinkMesh->SetRelativeRotation(SO.RelativeRotation);
        SinkMesh->SetRelativeScale3D(SO.RelativeScale);
    }
    else
    {
        SinkMesh->SetVisibility(false);
    }

    // ── Faucet ────────────────────────────────────────────────────────────
    ApplyMeshAndMaterials(FaucetMesh.Get(), ProductData.FaucetMesh);
    {
        const FFaucetPlacementOffset& FO = ProductData.FaucetOffset;
        FaucetMesh->SetRelativeLocation(FO.RelativeLocation);
        FaucetMesh->SetRelativeRotation(FO.RelativeRotation);
        FaucetMesh->SetRelativeScale3D(FO.RelativeScale);
    }

    // ── Mirror ────────────────────────────────────────────────────────────
    ApplyMeshAndMaterials(MirrorMesh.Get(), ProductData.MirrorMesh);
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

void AFurniturePreviewActor::ApplyMeshAndMaterials(UStaticMeshComponent* Target,
                                                    const FFurnitureMeshMaterials& Config)
{
    if (!Target)
    {
        return;
    }

    if (UStaticMesh* LoadedMesh = Config.Mesh.Get())
    {
        Target->SetStaticMesh(LoadedMesh);
    }

    for (const FFurnitureMaterialSlot& SlotOverride : Config.MaterialOverrides)
    {
        UMaterialInterface* LoadedMat = SlotOverride.Material.Get();
        if (LoadedMat && Target->GetNumMaterials() > SlotOverride.SlotIndex)
        {
            Target->SetMaterial(SlotOverride.SlotIndex, LoadedMat);
        }
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
