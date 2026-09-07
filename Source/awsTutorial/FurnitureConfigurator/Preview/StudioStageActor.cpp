// Copyright MaxiMall Project. All Rights Reserved.
// StudioStageActor.cpp — neutral Studio Stage environment for View Mode.
// GROUND-TRUTH ENVIRONMENT architecture with three reference-viewer
// presentation modes (Neutral / Studio / Lifestyle). See StudioStageActor.h.

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
#include "Engine/TextureCube.h"
#include "Engine/Texture2D.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "Misc/PackageName.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Math/Float16Color.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

// Unity-build safety: uniquely named helper.
static UStaticMesh* StudioStage_LoadEngineShape(const TCHAR* Path)
{
    return LoadObject<UStaticMesh>(nullptr, Path);
}

// The classic five-softbox placement, shared by the procedural environment
// (panel images -> reflections/ambient) and the real rig (key/rim -> shadows).
namespace StudioStageRig
{
    struct FBoxSpec { float Yaw; float Pitch; float Ratio; };
    static const FBoxSpec Boxes[] =
    {
        {  35.0f, -40.0f, 1.00f }, // key, high camera-left
        { -55.0f, -15.0f, 0.45f }, // fill, camera-right
        { 140.0f, -20.0f, 0.25f }, // back-left wrap
        { 200.0f, -35.0f, 0.60f }, // rim, behind
        {   0.0f, -89.0f, 0.50f }, // overhead soft top
    };
}

// Live switch for testing: `studio.Preset Neutral|Studio|Lifestyle`.
// Also becomes the default for stages opened afterwards in this session.
static int32 GStudioPresetOverride = -1;

AStudioStageActor::AStudioStageActor()
{
    // Client-local presentation only — never replicated.
    bReplicates = false;
    bAlwaysRelevant = false;

    // The stage is the per-frame driver of the studio interaction.
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;

    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("StageRoot"));

    // ── Mode looks: a 1:1 port of forma-webviewer's Environment.ts presets ──
    // NEUTRAL: RoomEnvironment-like procedural light; background is the
    // viewer's CSS radial gradient (#fafaf8 -> #e9e6e1 -> #d8d4cd) = flat
    // light warm-gray + vignette. No dome.
    NeutralLook.EnvironmentAssetPath = TEXT("");
    NeutralLook.EnvironmentIntensity = 1.0f;
    NeutralLook.bEnvironmentDome = false;
    NeutralLook.FlatColorSRGB = FColor(0xEF, 0xEC, 0xE7);
    NeutralLook.Vignette = 0.35f;

    // STUDIO: brown_photostudio_02 as light (1.0) and blurred background
    // (backgroundBlurriness 0.4, backgroundIntensity 0.55).
    StudioLook.EnvironmentAssetPath = TEXT("/Game/ViewMode/StudioEnvHDRI");
    StudioLook.EnvironmentIntensity = 1.0f;
    StudioLook.bEnvironmentDome = true;
    StudioLook.DomeIntensity = 0.55f;
    StudioLook.DomeRoughness = 0.55f;
    StudioLook.FlatColorSRGB = FColor(0x4E, 0x48, 0x44); // fallback: average of the blurred studio bg
    StudioLook.Vignette = 0.0f;

    // LIFESTYLE: lebombo_2k as light (0.95) and blurred background
    // (backgroundBlurriness 0.25, backgroundIntensity 0.85).
    LifestyleLook.EnvironmentAssetPath = TEXT("/Game/ViewMode/LifestyleEnvHDRI");
    LifestyleLook.EnvironmentIntensity = 0.95f;
    LifestyleLook.bEnvironmentDome = true;
    LifestyleLook.DomeIntensity = 0.85f;
    LifestyleLook.DomeRoughness = 0.40f;
    LifestyleLook.FlatColorSRGB = FColor(0xA0, 0x8A, 0x6E); // fallback: average of the blurred interior bg
    LifestyleLook.Vignette = 0.0f;
}

const FStudioPresentationLook& AStudioStageActor::GetActiveLook() const
{
    switch (PresentationMode)
    {
    case EStudioPresentation::Studio:    return StudioLook;
    case EStudioPresentation::Lifestyle: return LifestyleLook;
    default:                             return NeutralLook;
    }
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

        // Background dome follows the camera's FULL orientation (yaw AND
        // pitch): the visible background is a fixed window of the blurred
        // HDRI no matter how the view orbits or tilts — a stable backdrop, as
        // the reference viewer reads. (Yaw-only locking still swept the dome
        // across the HDRI's bright floor / dark walls when tilting — proven in
        // testing.) Visual only — the SkyLight (lighting) stays world-fixed.
        if (AActor* Dome = DomeActor.Get())
        {
            if (IsValid(Preview->Camera))
            {
                const FRotator CamRot = Preview->Camera->GetComponentRotation();
                Dome->SetActorRotation(FRotator(CamRot.Pitch, CamRot.Yaw + GetActiveLook().DomeYawOffset, 0.0f));
            }
        }
    }
}

void AStudioStageActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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

// ─────────────────────────────────────────────────────────────────────────────
// Procedural neutral-studio environment cubemap (the NEUTRAL mode light).
//   L(D) = surround gradient (bottom..top, subtly warm..cool) + hot softbox
//   panels toward each rig light. Modeled on three.js RoomEnvironment: a
//   bright room with small, very bright light boxes.
// ─────────────────────────────────────────────────────────────────────────────
UTextureCube* AStudioStageActor::GetOrBuildProceduralEnvironment()
{
    if (ProceduralEnvCube)
    {
        return ProceduralEnvCube;
    }

    const int32 Size = FMath::Clamp(ProceduralEnvSize, 32, 1024);
    UTextureCube* Cube = UTextureCube::CreateTransient(Size, Size, PF_FloatRGBA, TEXT("StudioNeutralEnv"));
    if (!Cube)
    {
        UE_LOG(LogTemp, Error, TEXT("[StudioStage] Failed to create the procedural environment cubemap."));
        return nullptr;
    }
    Cube->SRGB = false;
    Cube->CompressionSettings = TC_HDR;

    struct FPanel { FVector ToLight; FVector T; FVector B; float Lum; };
    TArray<FPanel> Panels;
    // Panels are drawn SMALLER than the physical softboxes (reference-viewer
    // style: small, very hot sources -> metal sparkle, moderate diffuse share).
    const float TanHalf = (SoftboxSizeFactor * 0.5f) / FMath::Max(LightDistanceFactor, 0.1f)
                        * FMath::Clamp(EnvPanelAngularScale, 0.05f, 1.0f);
    const float EdgeStart = TanHalf * 0.55f;
    for (const StudioStageRig::FBoxSpec& Box : StudioStageRig::Boxes)
    {
        FPanel P;
        P.ToLight = -FRotator(Box.Pitch, Box.Yaw, 0.0f).Vector();
        P.Lum = EnvPanelLuminance * Box.Ratio;
        const FVector Ref = (FMath::Abs(P.ToLight.Z) > 0.95f) ? FVector::ForwardVector : FVector::UpVector;
        P.T = FVector::CrossProduct(Ref, P.ToLight).GetSafeNormal();
        P.B = FVector::CrossProduct(P.ToLight, P.T);
        Panels.Add(P);
    }

    const float GlobalScale = FMath::Max(StudioLightIntensityScale, 0.0f);
    // Subtle sky-cool / ground-warm tint (~5%): gives white ceramics a living
    // top-to-bottom gradient instead of a flat white field.
    const FLinearColor TopTint(0.955f, 0.985f, 1.045f);
    const FLinearColor BottomTint(1.045f, 1.0f, 0.950f);

    auto EvalRadiance = [&](const FVector& D) -> FLinearColor
    {
        const float UpBlend = FMath::Clamp(static_cast<float>(D.Z) * 0.5f + 0.5f, 0.0f, 1.0f);
        FLinearColor Col = FMath::Lerp(BottomTint * EnvBaseBottomLuminance, TopTint * EnvBaseTopLuminance, UpBlend);
        float PanelLum = 0.0f;
        for (const FPanel& P : Panels)
        {
            const float Along = static_cast<float>(FVector::DotProduct(D, P.ToLight));
            if (Along <= KINDA_SMALL_NUMBER)
            {
                continue;
            }
            const float TanU = FMath::Abs(static_cast<float>(FVector::DotProduct(D, P.T)) / Along);
            const float TanV = FMath::Abs(static_cast<float>(FVector::DotProduct(D, P.B)) / Along);
            if (TanU < TanHalf && TanV < TanHalf)
            {
                const float FadeU = 1.0f - FMath::SmoothStep(EdgeStart, TanHalf, TanU);
                const float FadeV = 1.0f - FMath::SmoothStep(EdgeStart, TanHalf, TanV);
                PanelLum += P.Lum * FadeU * FadeV;
            }
        }
        Col += FLinearColor(PanelLum, PanelLum, PanelLum);
        return Col * GlobalScale;
    };

    FTexturePlatformData* PlatformData = Cube->GetPlatformData();
    FTexture2DMipMap& Mip = PlatformData->Mips[0];
    FFloat16Color* Texels = static_cast<FFloat16Color*>(Mip.BulkData.Lock(LOCK_READ_WRITE));

    double IrradianceUp = 0.0;
    double IrrR = 0.0, IrrG = 0.0, IrrB = 0.0;
    const float Step = 2.0f / Size;

    for (int32 Face = 0; Face < 6; ++Face)
    {
        FFloat16Color* FaceTexels = Texels + static_cast<int64>(Face) * Size * Size;
        for (int32 y = 0; y < Size; ++y)
        {
            const float v = ((y + 0.5f) * Step) - 1.0f;
            for (int32 x = 0; x < Size; ++x)
            {
                const float u = ((x + 0.5f) * Step) - 1.0f;
                FVector Dir;
                switch (Face)
                {
                case 0: Dir = FVector( 1.0f,   -v,   -u); break; // +X
                case 1: Dir = FVector(-1.0f,   -v,    u); break; // -X
                case 2: Dir = FVector(    u, 1.0f,    v); break; // +Y
                case 3: Dir = FVector(    u,-1.0f,   -v); break; // -Y
                case 4: Dir = FVector(    u,   -v, 1.0f); break; // +Z
                default:Dir = FVector(   -u,   -v,-1.0f); break; // -Z
                }
                const float InvLen = 1.0f / FMath::Sqrt(1.0f + u * u + v * v);
                Dir *= InvLen;

                FLinearColor Col = EvalRadiance(Dir);
                Col.A = 1.0f;
                FaceTexels[y * Size + x] = FFloat16Color(Col);

                if (Dir.Z > 0.0f)
                {
                    const double dOmega = (Step * Step) * (InvLen * InvLen * InvLen);
                    const double Weight = Dir.Z * dOmega;
                    IrradianceUp += Col.GetLuminance() * Weight;
                    IrrR += Col.R * Weight; IrrG += Col.G * Weight; IrrB += Col.B * Weight;
                }
            }
        }
    }

    Mip.BulkData.Unlock();
    Cube->UpdateResource();

    ProceduralEnvIrradianceUp = FLinearColor(static_cast<float>(IrrR), static_cast<float>(IrrG), static_cast<float>(IrrB), 1.0f);

    const float Exposure = ComputeExposureMultiplier();
    UE_LOG(LogTemp, Log, TEXT("[StudioStage] Procedural env built (%dpx): E_up=%.2f, IBL-only white ~%.2f pre-tonemap (key adds ~%.2f)."),
        Size, IrradianceUp, 0.9f * static_cast<float>(IrradianceUp) / PI * Exposure,
        0.9f * KeyIlluminanceLux * GlobalScale / PI * Exposure);

    ProceduralEnvCube = Cube;
    return ProceduralEnvCube;
}

void AStudioStageActor::SpawnRigLight(const FVector& FocusPoint, float SubjectRadius, float YawDeg, float PitchDeg, float Lux, bool bCastShadows)
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

    ARectLight* Light = World->SpawnActor<ARectLight>(Location, Dir);
    if (!Light)
    {
        return;
    }
    if (URectLightComponent* Comp = Cast<URectLightComponent>(Light->GetLightComponent()))
    {
        Comp->SetMobility(EComponentMobility::Movable);
        Comp->SetSourceWidth(SizeCm);
        Comp->SetSourceHeight(SizeCm);
        Comp->SetCastShadows(bCastShadows);
        Comp->SetIntensityUnits(ELightUnits::Candelas);
        // I[cd] = E[lux] * d[m]^2 -> the requested illuminance lands on the subject.
        const float DistanceMeters = Distance * 0.01f;
        const float BaseIntensity = Lux * FMath::Max(StudioLightIntensityScale, 0.f) * DistanceMeters * DistanceMeters;
        Comp->SetIntensity(BaseIntensity);
        RigLightComponents.Add(Comp);
        RigLightBaseIntensities.Add(BaseIntensity);
        Comp->SetLightColor(FLinearColor::White);
        Comp->SetAttenuationRadius(Distance * 4.0f);
        // Subject-only: the preview meshes are on channel 1; the level (channel
        // 0) and the background dome (channel 2) never receive rig light.
        Comp->LightingChannels.bChannel0 = false;
        Comp->LightingChannels.bChannel1 = true;
        Comp->LightingChannels.bChannel2 = false;
        Comp->IndirectLightingIntensity = 0.0f;
        Comp->MarkRenderStateDirty();
    }
    StageActors.Add(Light);
}

void AStudioStageActor::SpawnFlatBackdrop(const FVector& FocusPoint, float SubjectRadius, const FColor& ColorSRGB)
{
    UWorld* World = GetWorld();
    UStaticMesh* PlaneMesh = StudioStage_LoadEngineShape(TEXT("/Engine/BasicShapes/Plane.Plane"));
    if (!World || !PlaneMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("[StudioStage] Backdrop: engine Plane mesh could not be loaded."));
        return;
    }

    // Enclosing box of six inward-facing UNLIT planes: constant color in every
    // direction, so corners/edges are invisible — a seamless flat field.
    const float BackdropHalfSize = FMath::Max(FMath::Min(SubjectRadius * 40.0f, 6000.0f), SubjectRadius * 14.0f);

    FLinearColor BackdropLinear = FLinearColor::FromSRGBColor(ColorSRGB);
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
    for (const FVector& Dir : FaceDirs)
    {
        const FVector FaceLoc = FocusPoint + Dir * BackdropHalfSize;
        const FRotator FaceRot = FRotationMatrix::MakeFromZ(-Dir).Rotator();
        AStaticMeshActor* Face = World->SpawnActor<AStaticMeshActor>(FaceLoc, FaceRot);
        if (!Face)
        {
            continue;
        }
        Face->SetMobility(EComponentMobility::Movable);
        UStaticMeshComponent* FaceComp = Face->GetStaticMeshComponent();
        FaceComp->SetStaticMesh(PlaneMesh);
        FaceComp->SetWorldScale3D(FVector(BackdropHalfSize / 50.0f, BackdropHalfSize / 50.0f, 1.0f));
        FaceComp->SetCastShadow(false);
        FaceComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        FaceComp->bAffectDynamicIndirectLighting = false;
        if (BackdropMID)
        {
            FaceComp->SetMaterial(0, BackdropMID);
        }
        StageActors.Add(Face);
    }
}

bool AStudioStageActor::SpawnBackdropDome(const FVector& FocusPoint, float SubjectRadius, const FStudioPresentationLook& Look, UTextureCube* EnvCube)
{
    UWorld* World = GetWorld();
    UStaticMesh* SphereMesh = StudioStage_LoadEngineShape(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    UMaterialInterface* DomeBase = BackdropDomeMaterial;
    if (!DomeBase)
    {
        DomeBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ViewMode/M_ViewModeBackdrop.M_ViewModeBackdrop"));
    }
    if (!World || !SphereMesh || !DomeBase || !EnvCube)
    {
        UE_LOG(LogTemp, Warning, TEXT("[StudioStage] Background dome needs /Game/ViewMode/M_ViewModeBackdrop (two-sided UNLIT skybox material with 'EnvCube', 'Tint', 'BlurMip' parameters) and the mode's HDRI — using the flat surround color instead."));
        return false;
    }

    // Same distance rule as the flat box; the engine sphere has radius 50.
    const float DomeRadius = FMath::Max(FMath::Min(SubjectRadius * 40.0f, 6000.0f), SubjectRadius * 14.0f);
    AStaticMeshActor* Dome = World->SpawnActor<AStaticMeshActor>(FocusPoint, FRotator::ZeroRotator);
    if (!Dome)
    {
        return false;
    }
    Dome->SetMobility(EComponentMobility::Movable);
    UStaticMeshComponent* DomeComp = Dome->GetStaticMeshComponent();
    DomeComp->SetStaticMesh(SphereMesh);
    DomeComp->SetWorldScale3D(FVector(DomeRadius / 50.0f));
    DomeComp->SetCastShadow(false);
    DomeComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DomeComp->bAffectDynamicIndirectLighting = false;
    DomeComp->bVisibleInReflectionCaptures = false;
    // Channel 2 only: no rig light ever reaches it (the material is unlit
    // anyway — a SKYBOX: each pixel shows the cubemap in its own view
    // direction, exactly like three.js scene.background, so orbiting only
    // pans a very blurred image. A lit/metallic dome is a mirror ball seen
    // from inside and swings with the camera — proven wrong in testing).
    DomeComp->LightingChannels.bChannel0 = false;
    DomeComp->LightingChannels.bChannel1 = false;
    DomeComp->LightingChannels.bChannel2 = true;

    if (UMaterialInstanceDynamic* DomeMID = UMaterialInstanceDynamic::Create(DomeBase, this))
    {
        const float I = FMath::Max(Look.DomeIntensity, 0.0f);
        // Blur: absolute mip level of the cubemap sample. three.js
        // backgroundBlurriness is a PMREM roughness — even 0.25 is a very wide
        // lobe — so it maps onto the SMALLEST mips of the chain: 0 -> ~45% of
        // the mips, 1 -> the 2px/face mip. (The old "blur x 10" left far too
        // much structure — proven in testing.)
        const int32 NumMips = FMath::Max(EnvCube->GetNumMips(), 1);
        const float MaxMip = FMath::Max(static_cast<float>(NumMips - 2), 0.0f); // 2px/face
        const float BlurMip = FMath::Clamp((0.45f + FMath::Clamp(Look.DomeRoughness, 0.0f, 1.0f)) * MaxMip, 0.0f, MaxMip);
        DomeMID->SetTextureParameterValue(TEXT("EnvCube"), EnvCube);
        DomeMID->SetVectorParameterValue(TEXT("Tint"), FLinearColor(I, I, I, 1.0f));
        DomeMID->SetScalarParameterValue(TEXT("BlurMip"), BlurMip);
        DomeComp->SetMaterial(0, DomeMID);
    }
    StageActors.Add(Dome);
    DomeActor = Dome;
    return true;
}

UTexture2D* AStudioStageActor::GetOrBuildContactShadowTexture()
{
    if (ContactShadowTexture)
    {
        return ContactShadowTexture;
    }
    const int32 Size = 256;
    UTexture2D* Tex = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8, TEXT("StudioContactShadow"));
    if (!Tex)
    {
        return nullptr;
    }
    Tex->SRGB = false;
    Tex->CompressionSettings = TC_EditorIcon; // uncompressed RGBA, alpha kept
    Tex->Filter = TF_Bilinear;
    Tex->AddressX = TA_Clamp;
    Tex->AddressY = TA_Clamp;

    FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
    FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
    for (int32 y = 0; y < Size; ++y)
    {
        for (int32 x = 0; x < Size; ++x)
        {
            const float u = (x + 0.5f) / Size * 2.0f - 1.0f;
            const float v = (y + 0.5f) / Size * 2.0f - 1.0f;
            const float R = FMath::Clamp(FMath::Sqrt(u * u + v * v), 0.0f, 1.0f);
            // The viewer's radial blob: 1.0 at the center -> 0.38 at 55% of the
            // radius -> 0 at the rim (normalized; strength is applied as tint).
            float Falloff;
            if (R < 0.55f)
            {
                Falloff = FMath::Lerp(1.0f, 0.38f, FMath::SmoothStep(0.0f, 0.55f, R));
            }
            else
            {
                Falloff = FMath::Lerp(0.38f, 0.0f, FMath::SmoothStep(0.55f, 1.0f, R));
            }
            const uint8 A = static_cast<uint8>(FMath::RoundToInt(255.0f * Falloff));
            Pixels[y * Size + x] = FColor(0, 0, 0, A);
        }
    }
    Mip.BulkData.Unlock();
    Tex->UpdateResource();
    ContactShadowTexture = Tex;
    return ContactShadowTexture;
}

AActor* AStudioStageActor::SpawnShadowBlob(const FBox& ProductBox, float Strength)
{
    UWorld* World = GetWorld();
    UStaticMesh* PlaneMesh = StudioStage_LoadEngineShape(TEXT("/Engine/BasicShapes/Plane.Plane"));
    UMaterialInterface* BlobBase = ContactShadowMaterial;
    if (!BlobBase)
    {
        BlobBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Translucent.Widget3DPassThrough_Translucent"));
    }
    UTexture2D* BlobTex = GetOrBuildContactShadowTexture();
    if (!World || !PlaneMesh || !BlobBase || !BlobTex)
    {
        UE_LOG(LogTemp, Warning, TEXT("[StudioStage] Shadow blob unavailable (plane/material/texture missing) — product will float."));
        return nullptr;
    }

    AStaticMeshActor* Blob = World->SpawnActor<AStaticMeshActor>(ProductBox.GetCenter(), FRotator::ZeroRotator);
    if (!Blob)
    {
        return nullptr;
    }
    Blob->SetMobility(EComponentMobility::Movable);
    UStaticMeshComponent* BlobComp = Blob->GetStaticMeshComponent();
    BlobComp->SetStaticMesh(PlaneMesh);
    BlobComp->SetCastShadow(false);
    BlobComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BlobComp->bAffectDynamicIndirectLighting = false;
    BlobComp->bVisibleInReflectionCaptures = false;
    BlobComp->SetTranslucentSortPriority(-10); // always behind the product

    if (UMaterialInstanceDynamic* BlobMID = UMaterialInstanceDynamic::Create(BlobBase, this))
    {
        BlobMID->SetTextureParameterValue(TEXT("SlateUI"), BlobTex);
        BlobMID->SetVectorParameterValue(TEXT("TintColorAndOpacity"), FLinearColor(0.0f, 0.0f, 0.0f, FMath::Clamp(Strength, 0.0f, 1.0f)));
        BlobMID->SetScalarParameterValue(TEXT("OpacityFromTexture"), 1.0f);
        BlobComp->SetMaterial(0, BlobMID);
    }
    StageActors.Add(Blob);
    return Blob;
}

void AStudioStageActor::UpdateContactShadow(const FBox& VisibleProductBox)
{
    if (!VisibleProductBox.IsValid)
    {
        return;
    }
    const FVector Extent = VisibleProductBox.GetExtent();
    const FVector Center = VisibleProductBox.GetCenter();
    const float HalfX = FMath::Max(static_cast<float>(Extent.X), 5.0f);
    const float HalfY = FMath::Max(static_cast<float>(Extent.Y), 5.0f);
    const float MeanHalf = 0.5f * (HalfX + HalfY);
    const float GroundZ = VisibleProductBox.Min.Z;

    // Shadow direction: away from the key light (rig yaw 35, above the product).
    const float KeyYawRad = FMath::DegreesToRadians(StudioStageRig::Boxes[0].Yaw);
    const FVector2D ToKey(-FMath::Cos(KeyYawRad), -FMath::Sin(KeyYawRad));
    const FVector2D ShadowDir = -ToKey;
    const float YawDeg = FMath::RadiansToDegrees(FMath::Atan2(ShadowDir.Y, ShadowDir.X));

    // Tight contact blob: the footprint's own ellipse, centered.
    if (AActor* Contact = ContactBlobActor.Get())
    {
        const float S = FMath::Max(ContactShadowScale, 0.1f);
        Contact->SetActorLocation(FVector(Center.X, Center.Y, GroundZ + 0.8f));
        Contact->SetActorRotation(FRotator::ZeroRotator);
        Contact->SetActorScale3D(FVector(HalfX * S / 50.0f, HalfY * S / 50.0f, 1.0f));
    }
    // Directional shadow: a longer, softer ellipse pushed along the shadow
    // direction (the viewer's key-light shadow through its invisible catcher).
    if (AActor* Directional = DirectionalBlobActor.Get())
    {
        const FVector2D Offset = ShadowDir * (MeanHalf * FMath::Max(DirectionalShadowOffset, 0.0f));
        Directional->SetActorLocation(FVector(Center.X + Offset.X, Center.Y + Offset.Y, GroundZ + 0.6f));
        Directional->SetActorRotation(FRotator(0.0f, YawDeg, 0.0f));
        Directional->SetActorScale3D(FVector(MeanHalf * FMath::Max(DirectionalShadowLength, 1.0f) / 50.0f, MeanHalf * 1.15f / 50.0f, 1.0f));
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
    TearDownStage();
    RigLightComponents.Reset();
    RigLightBaseIntensities.Reset();

    SetActorLocation(FocusPoint);
    ActiveSubjectRadius = FMath::Clamp(SubjectRadius, 25.0f, 2000.0f);
    const float Radius = ActiveSubjectRadius;

    // Remember the build for live presentation switches; honor a preset chosen
    // with `studio.Preset` earlier in this session.
    LastBuildFocusPoint = FocusPoint;
    LastBuildSubjectRadius = SubjectRadius;
    bHasBuiltOnce = true;
    if (GStudioPresetOverride >= 0)
    {
        PresentationMode = static_cast<EStudioPresentation>(GStudioPresetOverride);
    }
    const FStudioPresentationLook& Look = GetActiveLook();

    // ── 0. Suspend the level's atmosphere for full isolation ─────────────────
    // Height fog recolors distant unlit surfaces (the proven backdrop bug);
    // clouds/atmosphere could show through any seam. Restored on teardown.
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

    // ── 1. Environment resolution (the light; also the dome background) ──────
    UTextureCube* EnvCube = EnvironmentCubemap.Get();
    FString EnvSource = TEXT("explicit EnvironmentCubemap");
    if (!EnvCube && !Look.EnvironmentAssetPath.IsEmpty())
    {
        const FString ObjectPath = Look.EnvironmentAssetPath + TEXT(".") + FPackageName::GetShortName(Look.EnvironmentAssetPath);
        EnvCube = LoadObject<UTextureCube>(nullptr, *ObjectPath);
        EnvSource = Look.EnvironmentAssetPath;
        if (!EnvCube)
        {
            UE_LOG(LogTemp, Warning, TEXT("[StudioStage] Mode HDRI '%s' not found — import the .hdr at that path. Falling back to the procedural environment."), *Look.EnvironmentAssetPath);
        }
    }
    const bool bProceduralEnv = (EnvCube == nullptr);
    if (bProceduralEnv)
    {
        EnvCube = GetOrBuildProceduralEnvironment();
        EnvSource = TEXT("procedural neutral studio");
    }

    // ── 2. Background: blurred-HDRI dome (Studio/Lifestyle) or flat surround ─
    bool bDomeBuilt = false;
    if (Look.bEnvironmentDome && !bProceduralEnv)
    {
        bDomeBuilt = SpawnBackdropDome(FocusPoint, Radius, Look, EnvCube);
    }
    if (!bDomeBuilt)
    {
        SpawnFlatBackdrop(FocusPoint, Radius, Look.FlatColorSRGB);
    }

    // ── 3. Grounding: contact blob + directional shadow ellipse ──────────────
    ContactBlobActor = nullptr;
    DirectionalBlobActor = nullptr;
    if (bContactShadow)
    {
        FBox ProductBox(ForceInit);
        AFurniturePreviewActor* Preview = DrivenPreview.Get();
        if (!Preview || !Preview->GetStudioProductBox(ProductBox))
        {
            ProductBox = FBox(FocusPoint - FVector(Radius), FocusPoint + FVector(Radius));
        }
        DirectionalBlobActor = SpawnShadowBlob(ProductBox, DirectionalShadowStrength);
        ContactBlobActor = SpawnShadowBlob(ProductBox, ContactShadowStrength);
        if (AActor* D = DirectionalBlobActor.Get())
        {
            if (UStaticMeshComponent* C = Cast<AStaticMeshActor>(D)->GetStaticMeshComponent()) { C->SetTranslucentSortPriority(-11); }
        }
        UpdateContactShadow(ProductBox);
    }

    // ── 4. Direct rig: shadow-casting key + optional shadowless rim ──────────
    SpawnRigLight(FocusPoint, Radius, StudioStageRig::Boxes[0].Yaw, StudioStageRig::Boxes[0].Pitch,
                  KeyIlluminanceLux, bKeyLightShadows);
    if (RimRatio > 0.0f)
    {
        SpawnRigLight(FocusPoint, Radius, StudioStageRig::Boxes[3].Yaw, StudioStageRig::Boxes[3].Pitch,
                      KeyIlluminanceLux * RimRatio, false);
    }

    // ── 5. SkyLight: the explicit environment (ambient + reflections) ────────
    ActiveEnvironmentIntensity = FMath::Max(Look.EnvironmentIntensity, 0.0f);
    if (ASkyLight* Sky = World->SpawnActor<ASkyLight>(FocusPoint, FRotator::ZeroRotator))
    {
        if (USkyLightComponent* SkyComp = Sky->GetLightComponent())
        {
            SkyComp->SetMobility(EComponentMobility::Movable);
            SkyComp->SourceType = SLS_SpecifiedCubemap;
            SkyComp->bRealTimeCapture = false;
            SkyComp->SetCastShadows(false);
            SkyComp->bLowerHemisphereIsBlack = false;
            SkyComp->CubemapResolution = 512;
            SkyComp->SetIntensity(SkyLightIntensity * ActiveEnvironmentIntensity);
            if (EnvCube)
            {
                SkyComp->SetCubemap(EnvCube);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("[StudioStage] No environment cubemap available — the stage will be ambient-less."));
            }
            SkyComp->SetSourceCubemapAngle(EnvironmentCubemapAngle);
            StageSkyComp = SkyComp;
        }
        StageActors.Add(Sky);
    }

    // ── 6. Product-view rendering stability (restored on teardown) ───────────
    ApplyStudioCVars();

    bBuildingStage = false;

    // Whole-level view isolation: nothing outside the studio may ever render
    // for this player while the stage exists.
    ApplyWorldIsolation();

    ShowHintOverlay();

    const TCHAR* ModeName = PresentationMode == EStudioPresentation::Studio ? TEXT("Studio")
                          : PresentationMode == EStudioPresentation::Lifestyle ? TEXT("Lifestyle") : TEXT("Neutral");
    UE_LOG(LogTemp, Log, TEXT("[StudioStage] Built at %s, radius %.1f cm — preset=%s, env=%s, background=%s (%d stage actors)."),
        *FocusPoint.ToCompactString(), Radius, ModeName, *EnvSource,
        bDomeBuilt ? TEXT("blurred HDRI dome") : TEXT("flat color"), StageActors.Num());
}

void AStudioStageActor::SetPresentationMode(EStudioPresentation NewMode)
{
    if (PresentationMode == NewMode)
    {
        return;
    }
    PresentationMode = NewMode;
    if (!bHasBuiltOnce)
    {
        return;
    }
    BuildStage(LastBuildFocusPoint, LastBuildSubjectRadius);
    if (AFurniturePreviewActor* Preview = DrivenPreview.Get())
    {
        // The recipe carries the mode's vignette; keep the per-component EV.
        ApplyCameraRecipe(Preview->Camera, LastExtraExposureEV);
        FBox VisibleBox(ForceInit);
        if (Preview->GetStudioProductBox(VisibleBox))
        {
            UpdateContactShadow(VisibleBox);
        }
    }
}

static FAutoConsoleCommand GStudioPresetCmd(
    TEXT("studio.Preset"),
    TEXT("Switch the ViewMode presentation: Neutral | Studio | Lifestyle"),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        if (Args.Num() < 1)
        {
            UE_LOG(LogTemp, Warning, TEXT("[StudioStage] Usage: studio.Preset Neutral|Studio|Lifestyle"));
            return;
        }
        EStudioPresentation Mode;
        if (Args[0].Equals(TEXT("Studio"), ESearchCase::IgnoreCase))         { Mode = EStudioPresentation::Studio; }
        else if (Args[0].Equals(TEXT("Lifestyle"), ESearchCase::IgnoreCase)) { Mode = EStudioPresentation::Lifestyle; }
        else if (Args[0].Equals(TEXT("Neutral"), ESearchCase::IgnoreCase))   { Mode = EStudioPresentation::Neutral; }
        else { UE_LOG(LogTemp, Warning, TEXT("[StudioStage] Unknown preset '%s' (Neutral|Studio|Lifestyle)"), *Args[0]); return; }

        GStudioPresetOverride = static_cast<int32>(Mode);
        if (!GEngine) { return; }
        for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
        {
            UWorld* World = Ctx.World();
            if (!World || (Ctx.WorldType != EWorldType::PIE && Ctx.WorldType != EWorldType::Game))
            {
                continue;
            }
            for (TActorIterator<AStudioStageActor> It(World); It; ++It)
            {
                It->SetPresentationMode(Mode);
            }
        }
        UE_LOG(LogTemp, Log, TEXT("[StudioStage] Presentation preset -> %s"), *Args[0]);
    }));

// Live tuning of WHICH region of the HDRI sits behind the product in the
// active mode: `studio.DomeYaw <degrees>` (applies next frame, no rebuild).
// Make it permanent by setting that mode's look DomeYawOffset.
static FAutoConsoleCommand GStudioDomeYawCmd(
    TEXT("studio.DomeYaw"),
    TEXT("Set the active presentation's DomeYawOffset (degrees) on live stages"),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        if (Args.Num() < 1 || !GEngine)
        {
            UE_LOG(LogTemp, Warning, TEXT("[StudioStage] Usage: studio.DomeYaw <degrees>"));
            return;
        }
        const float Yaw = FCString::Atof(*Args[0]);
        for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
        {
            UWorld* World = Ctx.World();
            if (!World || (Ctx.WorldType != EWorldType::PIE && Ctx.WorldType != EWorldType::Game))
            {
                continue;
            }
            for (TActorIterator<AStudioStageActor> It(World); It; ++It)
            {
                switch (It->PresentationMode)
                {
                case EStudioPresentation::Studio:    It->StudioLook.DomeYawOffset = Yaw;    break;
                case EStudioPresentation::Lifestyle: It->LifestyleLook.DomeYawOffset = Yaw; break;
                default:                             It->NeutralLook.DomeYawOffset = Yaw;   break;
                }
            }
        }
        UE_LOG(LogTemp, Log, TEXT("[StudioStage] DomeYawOffset -> %.1f"), Yaw);
    }));

void AStudioStageActor::ApplyStudioCVars()
{
    if (SavedCVarValues.Num() > 0)
    {
        return; // already applied for this stage's lifetime
    }
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
            UE_LOG(LogTemp, Warning, TEXT("[StudioStage] Unknown console variable '%s' — skipped."), *Name);
            continue;
        }
        SavedCVarValues.Emplace(Name, CVar->GetString());
        CVar->Set(*Value, ECVF_SetByConsole);
    }
}

void AStudioStageActor::RestoreStudioCVars()
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

void AStudioStageActor::ApplyCameraRecipe(UCameraComponent* Camera, float ExtraExposureEV) const
{
    if (!IsValid(Camera))
    {
        return;
    }
    LastExtraExposureEV = ExtraExposureEV;

    Camera->PostProcessBlendWeight = 1.0f;
    FPostProcessSettings& PP = Camera->PostProcessSettings;

    // Fixed exposure, like the web viewer's exposure value. Physical camera
    // exposure is OFF, so the multiplier is exactly 2^(LockedEV100 + ExtraEV).
    PP.bOverride_AutoExposureMethod = true;   PP.AutoExposureMethod = AEM_Manual;
    PP.bOverride_AutoExposureBias = true;     PP.AutoExposureBias = LockedEV100 + ExtraExposureEV;
    PP.bOverride_AutoExposureMinBrightness = true; PP.AutoExposureMinBrightness = 1.0f;
    PP.bOverride_AutoExposureMaxBrightness = true; PP.AutoExposureMaxBrightness = 1.0f;
    PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true; PP.AutoExposureApplyPhysicalCameraExposure = false;

    // Pure IBL for this camera: no Lumen GI/reflections and no SSR — the stage
    // SkyLight cubemap is the single ambient/reflection source.
    PP.bOverride_DynamicGlobalIlluminationMethod = true;
    PP.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
    PP.bOverride_ReflectionMethod = true;
    PP.ReflectionMethod = EReflectionMethod::None;

    // FULLY-SPECIFIED TONE RESPONSE at ENGINE-DEFAULT values — the Static Mesh
    // Editor's response; every input pinned so level PostProcessVolumes can
    // never tint the product.
    PP.bOverride_ToneCurveAmount = true;      PP.ToneCurveAmount = ToneCurveAmount;
    PP.bOverride_ExpandGamut = true;          PP.ExpandGamut = 1.0f;   // engine default
    PP.bOverride_BlueCorrection = true;       PP.BlueCorrection = 0.6f; // engine default
    PP.bOverride_FilmSlope = true;            PP.FilmSlope = 0.88f;
    PP.bOverride_FilmToe = true;              PP.FilmToe = 0.55f;
    PP.bOverride_FilmShoulder = true;         PP.FilmShoulder = 0.26f;

    // Bloom pinned to the studio's own value; the mode's vignette (Neutral uses
    // it for the viewer's radial background gradient); other lens effects off.
    PP.bOverride_BloomIntensity = true;             PP.BloomIntensity = FMath::Max(StudioBloomIntensity, 0.0f);
    PP.bOverride_VignetteIntensity = true;          PP.VignetteIntensity = FMath::Clamp(GetActiveLook().Vignette, 0.0f, 1.0f);
    PP.bOverride_SceneFringeIntensity = true;       PP.SceneFringeIntensity = 0.0f;
    PP.bOverride_FilmGrainIntensity = true;         PP.FilmGrainIntensity = 0.0f;
    PP.bOverride_MotionBlurAmount = true;           PP.MotionBlurAmount = 0.0f;
    PP.bOverride_LensFlareIntensity = true;         PP.LensFlareIntensity = 0.0f;

    PP.bOverride_AmbientOcclusionIntensity = true;
    PP.AmbientOcclusionIntensity = FMath::Clamp(AmbientOcclusionAmount, 0.0f, 1.0f);

    // Optional tone-mapper blendable; reset before adding to avoid stacking.
    PP.WeightedBlendables.Array.Reset();
    if (ToneMapperMaterial)
    {
        PP.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, ToneMapperMaterial));
    }
}

void AStudioStageActor::ShowHintOverlay()
{
    // NOTE: use THIS world's viewport, not GEngine->GameViewport — in multi-client
    // PIE the latter is the first instance's window.
    UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    if (!bShowHintOverlay || HintWidget.IsValid() || !Viewport)
    {
        return;
    }

    const FText HintText = FText::FromString(TEXT(
        "ЛКМ — вращение"
        "      ПКМ/СКМ — перемещение"
        "      Колесо — масштаб"));

    const TSharedRef<SOverlay> Overlay =
        SNew(SOverlay)
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
    if (USkyLightComponent* Sky = StageSkyComp.Get())
    {
        Sky->SetIntensity(SkyLightIntensity * ActiveEnvironmentIntensity * S);
    }
}

void AStudioStageActor::TearDownStage()
{
    HideHintOverlay();
    RemoveWorldIsolation();
    RestoreStudioCVars();

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
    StageSkyComp = nullptr;
    ContactBlobActor = nullptr;
    DirectionalBlobActor = nullptr;
    DomeActor = nullptr;
    // The procedural cubemap and blob texture are kept (cheap, reused on
    // rebuild); they die with this actor via GC.
}
