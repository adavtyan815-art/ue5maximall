// Copyright MaxiMall Project. All Rights Reserved.
//
// TEMPORARY PROTOTYPE — glTF Proxy Material proof for Wood_, safe to delete.
//
// Part A: creates a real Epic glTF proxy material for Wood_ (replicates
//   FGLTFMaterialProxyFactory::Create step-by-step using only exported plugin API,
//   with the same options the editor's "Create glTF Proxy Material" dialog uses by
//   default: Simple bake, 1024, trilinear, wrap). Proxy + baked textures are saved
//   as NEW assets under /Game/scena/Materials/GLTF/. The proxy link (AssetUserData
//   on Wood_) is applied IN MEMORY ONLY — the Wood_ asset on disk is not modified.
//
// Part B: exports SM_MERGED_StaticMeshActor_16 with BakeMaterialInputs=DISABLED,
//   which makes the editor bake path unable to produce any texture. A correct wood
//   appearance in the output can therefore only come from the pre-generated proxy —
//   this emulates exactly what a packaged (Linux) client has available.
//   antr (slot 0, NO proxy) acts as the in-file negative control: it must degrade
//   and log "Failed to export ..." warnings, proving no editor baking ran.
//
// Usage:
//   PIE / -game console:  ar.ProxyWoodTest
//   Headless auto-run:    UnrealEditor-Cmd <project> -game -ARGLTFProxyProto
//
// Output: Saved/AR_Exports/New folder/proto_PROXY_WOOD_TEST_<timestamp>.glb

#include "CoreMinimal.h"

#if WITH_EDITOR

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "UObject/SavePackage.h"
#include "ImageUtils.h"

#include "Builders/GLTFConvertBuilder.h"
#include "Options/GLTFExportOptions.h"
#include "Utilities/GLTFProxyMaterialUtilities.h"
#include "UserData/GLTFMaterialUserData.h"
#include "Exporters/GLTFExporter.h"

namespace
{
    constexpr const TCHAR* WoodMaterialPath = TEXT("/Game/scena/Materials/Wood_.Wood_");
    constexpr const TCHAR* TestMeshPath = TEXT("/Game/SM_MERGED_StaticMeshActor_16.SM_MERGED_StaticMeshActor_16");
    // Fixed-test assets live in a fresh folder so the first (defective) proxy assets stay untouched.
    constexpr const TCHAR* ProxyRootPath = TEXT("/Game/scena/Materials/GLTF_Fixed");

    struct FProtoImageData
    {
        FString Filename;
        bool bIgnoreAlpha = false;
        FIntPoint Size = FIntPoint::ZeroValue;
        TGLTFSharedArray<FColor> Pixels;
    };

    struct FProtoProxyState
    {
        TMap<FGLTFJsonTexture*, UTexture2D*> Textures;   // direct-reuse originals AND created assets
        TMap<FGLTFJsonImage*, FProtoImageData> Images;   // baked pixel payloads
        TArray<UPackage*> PackagesToSave;
        FString RootPath;                                // where new proxy assets are created
    };

    // Mirrors FGLTFMaterialProxyFactory's custom converters: intercept texture reuse
    // and baked image payloads instead of embedding them into a glTF file.
    class FProtoTexture2DConverter final : public IGLTFTexture2DConverter
    {
    public:
        FGLTFConvertBuilder& Builder;
        FProtoProxyState& State;
        FProtoTexture2DConverter(FGLTFConvertBuilder& InBuilder, FProtoProxyState& InState) : Builder(InBuilder), State(InState) {}
    protected:
        virtual void Sanitize(const UTexture2D*& Texture2D, bool& bToSRGB) override
        {
            bToSRGB = false; // ignore, same as engine factory
        }
        virtual FGLTFJsonTexture* Convert(const UTexture2D* Texture2D, bool bToSRGB) override
        {
            FGLTFJsonTexture* Texture = Builder.AddTexture();
            State.Textures.Add(Texture, const_cast<UTexture2D*>(Texture2D));
            return Texture;
        }
    };

    class FProtoImageConverter final : public IGLTFImageConverter
    {
    public:
        FGLTFConvertBuilder& Builder;
        FProtoProxyState& State;
        TSet<FString> UniqueFilenames;
        FProtoImageConverter(FGLTFConvertBuilder& InBuilder, FProtoProxyState& InState) : Builder(InBuilder), State(InState) {}
    protected:
        virtual FGLTFJsonImage* Convert(TGLTFSuperfluous<FString> Name, bool bIgnoreAlpha, FIntPoint Size, TGLTFSharedArray<FColor> Pixels) override
        {
            FString Filename = Name;
            int32 Suffix = 1;
            while (UniqueFilenames.Contains(Filename))
            {
                Filename = FString::Printf(TEXT("%s_%d"), *static_cast<const FString&>(Name), Suffix++);
            }
            UniqueFilenames.Add(Filename);

            FGLTFJsonImage* Image = Builder.AddImage();
            State.Images.Add(Image, { Filename, bIgnoreAlpha, Size, Pixels });
            return Image;
        }
    };

    TextureAddress ConvertWrap(EGLTFJsonTextureWrap Wrap)
    {
        switch (Wrap)
        {
            case EGLTFJsonTextureWrap::Repeat:         return TA_Wrap;
            case EGLTFJsonTextureWrap::MirroredRepeat: return TA_Mirror;
            case EGLTFJsonTextureWrap::ClampToEdge:    return TA_Clamp;
            default:                                   return TA_Wrap;
        }
    }

    TextureFilter ConvertFilter(EGLTFJsonTextureFilter Filter)
    {
        switch (Filter)
        {
            case EGLTFJsonTextureFilter::Nearest:
            case EGLTFJsonTextureFilter::NearestMipmapNearest:
            case EGLTFJsonTextureFilter::NearestMipmapLinear:  return TF_Nearest;
            case EGLTFJsonTextureFilter::Linear:
            case EGLTFJsonTextureFilter::LinearMipmapNearest:  return TF_Bilinear;
            case EGLTFJsonTextureFilter::LinearMipmapLinear:   return TF_Trilinear;
            default:                                           return TF_Default;
        }
    }

    // Package/asset names must be safe for object paths and Windows files (Cyrillic material
    // names like "cеrniy_mеtаl" are replaced character-wise).
    FString SanitizeAssetName(const FString& In)
    {
        FString Out;
        Out.Reserve(In.Len());
        for (TCHAR C : In)
        {
            Out.AppendChar((FChar::IsAlnum(C) && C < 128) || C == TEXT('_') ? C : TEXT('_'));
        }
        return Out;
    }

    UPackage* FindOrCreateProtoPackage(const FString& RawBaseName, FProtoProxyState& State)
    {
        const FString BaseName = SanitizeAssetName(RawBaseName);
        const FString Root = State.RootPath.IsEmpty() ? FString(ProxyRootPath) : State.RootPath;
        const FString PackageName = Root / BaseName;
        UPackage* Package = CreatePackage(*PackageName);
        Package->FullyLoad();
        Package->Modify();
        State.PackagesToSave.AddUnique(Package);
        return Package;
    }

    UTexture2D* FindOrCreateProxyTexture(FGLTFJsonTexture* JsonTexture, bool bSRGB, bool bNormalMap, FProtoProxyState& State)
    {
        if (JsonTexture == nullptr)
        {
            return nullptr;
        }
        if (UTexture2D** FoundPtr = State.Textures.Find(JsonTexture))
        {
            return *FoundPtr; // direct reuse of the original texture asset
        }
        const FProtoImageData* ImageData = State.Images.Find(JsonTexture->Source);
        if (ImageData == nullptr)
        {
            UE_LOG(LogTemp, Error, TEXT("[ProxyProto] No image payload for baked texture."));
            return nullptr;
        }

        FCreateTexture2DParameters TexParams;
        TexParams.bUseAlpha = !ImageData->bIgnoreAlpha;
        // FIX 2: TC_EditorIcon keeps the baked pixels uncompressed (B8G8R8A8) so the
        // runtime export readback is lossless instead of BC/DXT block-compressed.
        TexParams.CompressionSettings = bNormalMap ? TC_Normalmap : TC_EditorIcon;
        TexParams.bDeferCompression = true;
        TexParams.bSRGB = bSRGB;
        TexParams.TextureGroup = bNormalMap ? TEXTUREGROUP_WorldNormalMap : TEXTUREGROUP_World;
        TexParams.SourceGuidHash = FGuid();

        const FString BaseName = SanitizeAssetName(TEXT("T_GLTF_") + ImageData->Filename);
        UPackage* Package = FindOrCreateProtoPackage(BaseName, State);
        UTexture2D* Texture = FImageUtils::CreateTexture2D(ImageData->Size.X, ImageData->Size.Y, *ImageData->Pixels, Package, BaseName, RF_Public | RF_Standalone, TexParams);
        if (Texture && JsonTexture->Sampler)
        {
            Texture->Filter = ConvertFilter(JsonTexture->Sampler->MagFilter);
            Texture->AddressX = ConvertWrap(JsonTexture->Sampler->WrapS);
            Texture->AddressY = ConvertWrap(JsonTexture->Sampler->WrapT);
        }
        if (Texture)
        {
            State.Textures.Add(JsonTexture, Texture);
            UE_LOG(LogTemp, Warning, TEXT("[ProxyProto] Created baked proxy texture asset: %s (%dx%d)"), *Texture->GetPathName(), ImageData->Size.X, ImageData->Size.Y);
        }
        return Texture;
    }

    void SetProxyTextureParam(UMaterialInstanceConstant* Proxy, const FString& Name, const FGLTFJsonTextureInfo& Info, bool bSRGB, bool bNormalMap, FProtoProxyState& State)
    {
        UTexture2D* Texture = FindOrCreateProxyTexture(Info.Index, bSRGB, bNormalMap, State);
        if (Texture == nullptr)
        {
            return;
        }
        FGLTFProxyMaterialUtilities::SetParameterValue(Proxy, FHashedMaterialParameterInfo(*(Name + TEXT(" Texture"))), static_cast<UTexture*>(Texture), true);
        FGLTFProxyMaterialUtilities::SetParameterValue(Proxy, FHashedMaterialParameterInfo(*(Name + TEXT(" UV Index"))), static_cast<float>(Info.TexCoord), true);
        FGLTFProxyMaterialUtilities::SetParameterValue(Proxy, FHashedMaterialParameterInfo(*(Name + TEXT(" UV Offset"))), FLinearColor(Info.Transform.Offset.X, Info.Transform.Offset.Y, 0, 0), true);
        FGLTFProxyMaterialUtilities::SetParameterValue(Proxy, FHashedMaterialParameterInfo(*(Name + TEXT(" UV Scale"))), FLinearColor(Info.Transform.Scale.X, Info.Transform.Scale.Y, 0, 0), true);
        FGLTFProxyMaterialUtilities::SetParameterValue(Proxy, FHashedMaterialParameterInfo(*(Name + TEXT(" UV Rotation"))), Info.Transform.Rotation, true);
    }

    UMaterialInterface* CreateWoodProxy()
    {
        UMaterialInterface* Wood = LoadObject<UMaterialInterface>(nullptr, WoodMaterialPath);
        if (!Wood)
        {
            UE_LOG(LogTemp, Error, TEXT("[ProxyProto] Wood_ material not found at %s"), WoodMaterialPath);
            return nullptr;
        }

        // Same effective options as the editor dialog's defaults (see
        // FGLTFMaterialProxyFactory::CreateExportOptions + UGLTFProxyOptions::ResetToDefault).
        UGLTFExportOptions* ExportOptions = NewObject<UGLTFExportOptions>();
        ExportOptions->ResetToDefault();
        ExportOptions->bExportProxyMaterials = false;
        ExportOptions->BakeMaterialInputs = EGLTFMaterialBakeMode::Simple;
        ExportOptions->DefaultMaterialBakeSize = EGLTFMaterialBakeSizePOT::POT_1024;
        ExportOptions->DefaultMaterialBakeFilter = TF_Trilinear;
        ExportOptions->DefaultMaterialBakeTiling = TA_Wrap;
        ExportOptions->bAdjustNormalmaps = false;

        FProtoProxyState State;
        FGLTFConvertBuilder Builder(TEXT(""), ExportOptions);
        Builder.Texture2DConverter = MakeUnique<FProtoTexture2DConverter>(Builder, State);
        Builder.ImageConverter = MakeUnique<FProtoImageConverter>(Builder, State);

        FGLTFJsonMaterial* JsonMaterial = Builder.AddUniqueMaterial(Wood);
        if (JsonMaterial == nullptr)
        {
            UE_LOG(LogTemp, Error, TEXT("[ProxyProto] AddUniqueMaterial failed for Wood_"));
            return nullptr;
        }
        Builder.ProcessSlowTasks();

        // Create the proxy instance exactly like the engine factory (MI_GLTF_<name>).
        const FString ProxyBaseName = TEXT("MI_GLTF_") + Wood->GetName();
        UPackage* ProxyPackage = FindOrCreateProtoPackage(ProxyBaseName, State);
        UMaterialInstanceConstant* Proxy = FGLTFProxyMaterialUtilities::CreateProxyMaterial(JsonMaterial->ShadingModel, ProxyPackage, *ProxyBaseName, RF_Public | RF_Standalone);
        if (Proxy == nullptr)
        {
            UE_LOG(LogTemp, Error, TEXT("[ProxyProto] CreateProxyMaterial failed"));
            return nullptr;
        }

        // Base property overrides (two-sided / blend mode / cutoff), as in SetBaseProperties.
        const UMaterial* BaseMaterial = Proxy->GetMaterial();
        if (Wood->IsTwoSided() != BaseMaterial->IsTwoSided())
        {
            Proxy->BasePropertyOverrides.bOverride_TwoSided = true;
            Proxy->BasePropertyOverrides.TwoSided = Wood->IsTwoSided();
        }
        if (Wood->GetBlendMode() != BaseMaterial->GetBlendMode())
        {
            Proxy->BasePropertyOverrides.bOverride_BlendMode = true;
            Proxy->BasePropertyOverrides.BlendMode = Wood->GetBlendMode();
        }
        if (Wood->GetOpacityMaskClipValue() != BaseMaterial->GetOpacityMaskClipValue())
        {
            Proxy->BasePropertyOverrides.bOverride_OpacityMaskClipValue = true;
            Proxy->BasePropertyOverrides.OpacityMaskClipValue = Wood->GetOpacityMaskClipValue();
        }

        // Proxy parameters, as in SetProxyParameters (Default shading model set).
        const FGLTFJsonPBRMetallicRoughness& PBR = JsonMaterial->PBRMetallicRoughness;
        SetProxyTextureParam(Proxy, TEXT("Base Color"), PBR.BaseColorTexture, /*sRGB*/true, /*normal*/false, State);
        FGLTFProxyMaterialUtilities::SetParameterValue(Proxy, FHashedMaterialParameterInfo(TEXT("Base Color Factor")), FLinearColor(PBR.BaseColorFactor.R, PBR.BaseColorFactor.G, PBR.BaseColorFactor.B, PBR.BaseColorFactor.A), true);
        if (JsonMaterial->ShadingModel == EGLTFJsonShadingModel::Default || JsonMaterial->ShadingModel == EGLTFJsonShadingModel::ClearCoat)
        {
            SetProxyTextureParam(Proxy, TEXT("Emissive"), JsonMaterial->EmissiveTexture, /*sRGB*/true, /*normal*/false, State);
            FGLTFProxyMaterialUtilities::SetParameterValue(Proxy, FHashedMaterialParameterInfo(TEXT("Emissive Factor")), FLinearColor(JsonMaterial->EmissiveFactor.R, JsonMaterial->EmissiveFactor.G, JsonMaterial->EmissiveFactor.B), true);
            FGLTFProxyMaterialUtilities::SetParameterValue(Proxy, FHashedMaterialParameterInfo(TEXT("Emissive Strength")), JsonMaterial->EmissiveStrength, true);
            SetProxyTextureParam(Proxy, TEXT("Metallic Roughness"), PBR.MetallicRoughnessTexture, /*sRGB*/false, /*normal*/false, State);
            FGLTFProxyMaterialUtilities::SetParameterValue(Proxy, FHashedMaterialParameterInfo(TEXT("Metallic Factor")), PBR.MetallicFactor, true);
            FGLTFProxyMaterialUtilities::SetParameterValue(Proxy, FHashedMaterialParameterInfo(TEXT("Roughness Factor")), PBR.RoughnessFactor, true);
            SetProxyTextureParam(Proxy, TEXT("Normal"), JsonMaterial->NormalTexture, /*sRGB*/false, /*normal*/true, State);
            FGLTFProxyMaterialUtilities::SetParameterValue(Proxy, FHashedMaterialParameterInfo(TEXT("Normal Scale")), JsonMaterial->NormalTexture.Scale, true);
            SetProxyTextureParam(Proxy, TEXT("Occlusion"), JsonMaterial->OcclusionTexture, /*sRGB*/false, /*normal*/false, State);
            FGLTFProxyMaterialUtilities::SetParameterValue(Proxy, FHashedMaterialParameterInfo(TEXT("Occlusion Strength")), JsonMaterial->OcclusionTexture.Strength, true);
        }

        // FIX 1: a freshly constructed UMaterialInstance caches ShadingModels = MSM_Unlit
        // (MaterialInstance.cpp:715) and CreateProxyMaterial assigns Parent directly without
        // refreshing that cache. Force the refresh so the proxy reports its parent's
        // DefaultLit shading model (same state a disk load / packaged client always gets).
        Proxy->PreEditChange(nullptr);
        Proxy->PostEditChange();
        UE_LOG(LogTemp, Warning, TEXT("[ProxyProto] After cache refresh: proxy first shading model = %d (MSM_Unlit=0, MSM_DefaultLit=1)"),
            static_cast<int32>(Proxy->GetShadingModels().GetFirstShadingModel()));

        // Attach the proxy to Wood_ via AssetUserData — IN MEMORY ONLY (Wood_ is never saved).
        FGLTFProxyMaterialUtilities::SetProxyMaterial(Wood, Proxy);

        // Persist ONLY the new proxy/texture assets as new files.
        for (UPackage* Package : State.PackagesToSave)
        {
            const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
            FSavePackageArgs SaveArgs;
            SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
            const bool bSaved = UPackage::SavePackage(Package, nullptr, *FileName, SaveArgs);
            UE_LOG(LogTemp, Warning, TEXT("[ProxyProto] Save %s -> %s"), *Package->GetName(), bSaved ? TEXT("OK") : TEXT("FAILED"));
        }

        UE_LOG(LogTemp, Warning, TEXT("[ProxyProto] Proxy created: %s (shading model %d), attached to %s in-memory."),
            *Proxy->GetPathName(), static_cast<int32>(JsonMaterial->ShadingModel), *Wood->GetName());
        return Proxy;
    }

    void RunProxyWoodExport(UWorld* World)
    {
        if (!World)
        {
            UE_LOG(LogTemp, Error, TEXT("[ProxyProto] No world."));
            return;
        }

        UMaterialInterface* Proxy = CreateWoodProxy();
        if (!Proxy)
        {
            return;
        }

        UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TestMeshPath);
        if (!Mesh)
        {
            UE_LOG(LogTemp, Error, TEXT("[ProxyProto] Test mesh not found: %s"), TestMeshPath);
            return;
        }
        AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator);
        Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);

        // Prove per-slot what the exporter will resolve.
        UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent();
        for (int32 i = 0; i < Comp->GetNumMaterials(); ++i)
        {
            const UMaterialInterface* SlotMat = Comp->GetMaterial(i);
            const UMaterialInterface* Resolved = UGLTFMaterialExportOptions::ResolveProxy(SlotMat);
            UE_LOG(LogTemp, Warning, TEXT("[ProxyProto] Slot %d: %s -> ResolveProxy -> %s (IsProxyMaterial=%s)"),
                i,
                SlotMat ? *SlotMat->GetName() : TEXT("<null>"),
                Resolved ? *Resolved->GetName() : TEXT("<null>"),
                (Resolved && FGLTFProxyMaterialUtilities::IsProxyMaterial(Resolved)) ? TEXT("YES") : TEXT("no"));
        }

        // CRITICAL: BakeMaterialInputs=Disabled forbids the editor bake path entirely.
        // Whatever material data reaches the GLB can only come from proxies (or trivial
        // constant/texture expressions). This emulates the packaged-client situation.
        UGLTFExportOptions* Options = NewObject<UGLTFExportOptions>();
        Options->ResetToDefault();
        Options->BakeMaterialInputs = EGLTFMaterialBakeMode::Disabled;
        Options->TextureImageFormat = EGLTFTextureImageFormat::PNG;
        UE_LOG(LogTemp, Warning, TEXT("[ProxyProto] Export options: BakeMaterialInputs=DISABLED, bExportProxyMaterials=%s"),
            Options->bExportProxyMaterials ? TEXT("true") : TEXT("false"));

        const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AR_Exports"), TEXT("New folder"));
        IFileManager::Get().MakeDirectory(*Dir, true);
        const FString FilePath = FPaths::Combine(Dir, FString::Printf(TEXT("proto_PROXY_WOOD_TEST_FIXED_%s.glb"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));

        FGLTFExportMessages Messages;
        TSet<AActor*> Selected;
        Selected.Add(Actor);
        const double StartTime = FPlatformTime::Seconds();
        const bool bOK = UGLTFExporter::ExportToGLTF(World, FilePath, Options, Selected, Messages);
        const double Elapsed = FPlatformTime::Seconds() - StartTime;

        for (const FString& Msg : Messages.Errors)      { UE_LOG(LogTemp, Error,   TEXT("[ProxyProto] EXPORT ERROR: %s"), *Msg); }
        for (const FString& Msg : Messages.Warnings)    { UE_LOG(LogTemp, Warning, TEXT("[ProxyProto] EXPORT WARNING: %s"), *Msg); }
        for (const FString& Msg : Messages.Suggestions) { UE_LOG(LogTemp, Log,     TEXT("[ProxyProto] EXPORT SUGGESTION: %s"), *Msg); }

        UE_LOG(LogTemp, Warning, TEXT("[ProxyProto] Export %s (%.1fs, %lld bytes) -> %s"),
            bOK ? TEXT("SUCCEEDED") : TEXT("FAILED"), Elapsed, IFileManager::Get().FileSize(*FilePath), *FilePath);

        Actor->Destroy();
        UE_LOG(LogTemp, Warning, TEXT("[ProxyProto] Done."));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Generic proxy creation for the RAL / METAL / GLASS tests.
    // Same flow as the (passed) Wood test incl. FIX 1 (shading cache refresh) and
    // FIX 2 (uncompressed bake textures). Reuses an existing proxy when one is
    // already attached (same session) or already saved on disk (fresh session —
    // the production-faithful load path).
    // ─────────────────────────────────────────────────────────────────────────
    UMaterialInterface* CreateProxyForMaterialGeneric(UMaterialInterface* Mat)
    {
        if (!Mat)
        {
            return nullptr;
        }

        if (UMaterialInterface* Existing = FGLTFProxyMaterialUtilities::GetProxyMaterial(Mat))
        {
            UE_LOG(LogTemp, Warning, TEXT("[ProxyProto] Reusing already-attached proxy %s for %s"), *Existing->GetName(), *Mat->GetName());
            return Existing;
        }

        // Proxies live next to their source material (Epic's own convention:
        // "<material folder>/GLTF"). The physical location is otherwise arbitrary -
        // the association is the AssetUserData link, not the folder.
        const FString MaterialFolder = FPackageName::GetLongPackagePath(Mat->GetOutermost()->GetName());
        const FString RootPath = MaterialFolder / TEXT("GLTF");

        const FString ProxyBaseName = SanitizeAssetName(TEXT("MI_GLTF_") + Mat->GetName());
        for (const FString& Candidate : { RootPath / ProxyBaseName + TEXT(".") + ProxyBaseName,
                                          FString(ProxyRootPath) / ProxyBaseName + TEXT(".") + ProxyBaseName })
        {
            if (UMaterialInstanceConstant* FromDisk = LoadObject<UMaterialInstanceConstant>(nullptr, *Candidate))
            {
                FGLTFProxyMaterialUtilities::SetProxyMaterial(Mat, FromDisk);
                Mat->MarkPackageDirty(); // let the user persist the association by saving the material
                UE_LOG(LogTemp, Warning, TEXT("[ProxyProto] Loaded existing proxy from disk: %s for %s"), *FromDisk->GetPathName(), *Mat->GetName());
                return FromDisk;
            }
        }

        UGLTFExportOptions* ExportOptions = NewObject<UGLTFExportOptions>();
        ExportOptions->ResetToDefault();
        ExportOptions->bExportProxyMaterials = false;
        ExportOptions->BakeMaterialInputs = EGLTFMaterialBakeMode::Simple;
        ExportOptions->DefaultMaterialBakeSize = EGLTFMaterialBakeSizePOT::POT_1024;
        ExportOptions->DefaultMaterialBakeFilter = TF_Trilinear;
        ExportOptions->DefaultMaterialBakeTiling = TA_Wrap;
        ExportOptions->bAdjustNormalmaps = false;

        FProtoProxyState State;
        State.RootPath = RootPath;
        FGLTFConvertBuilder Builder(TEXT(""), ExportOptions);
        Builder.Texture2DConverter = MakeUnique<FProtoTexture2DConverter>(Builder, State);
        Builder.ImageConverter = MakeUnique<FProtoImageConverter>(Builder, State);

        FGLTFJsonMaterial* JsonMaterial = Builder.AddUniqueMaterial(Mat);
        if (JsonMaterial == nullptr)
        {
            UE_LOG(LogTemp, Error, TEXT("[ProxyProto] AddUniqueMaterial failed for %s"), *Mat->GetName());
            return nullptr;
        }
        Builder.ProcessSlowTasks();

        UPackage* ProxyPackage = FindOrCreateProtoPackage(ProxyBaseName, State);
        UMaterialInstanceConstant* Proxy = FGLTFProxyMaterialUtilities::CreateProxyMaterial(JsonMaterial->ShadingModel, ProxyPackage, *ProxyBaseName, RF_Public | RF_Standalone);
        if (Proxy == nullptr)
        {
            UE_LOG(LogTemp, Error, TEXT("[ProxyProto] CreateProxyMaterial failed for %s"), *Mat->GetName());
            return nullptr;
        }

        const UMaterial* BaseMaterial = Proxy->GetMaterial();
        if (Mat->IsTwoSided() != BaseMaterial->IsTwoSided())
        {
            Proxy->BasePropertyOverrides.bOverride_TwoSided = true;
            Proxy->BasePropertyOverrides.TwoSided = Mat->IsTwoSided();
        }
        if (Mat->GetBlendMode() != BaseMaterial->GetBlendMode())
        {
            Proxy->BasePropertyOverrides.bOverride_BlendMode = true;
            Proxy->BasePropertyOverrides.BlendMode = Mat->GetBlendMode();
        }
        if (Mat->GetOpacityMaskClipValue() != BaseMaterial->GetOpacityMaskClipValue())
        {
            Proxy->BasePropertyOverrides.bOverride_OpacityMaskClipValue = true;
            Proxy->BasePropertyOverrides.OpacityMaskClipValue = Mat->GetOpacityMaskClipValue();
        }

        const FGLTFJsonPBRMetallicRoughness& PBR = JsonMaterial->PBRMetallicRoughness;
        SetProxyTextureParam(Proxy, TEXT("Base Color"), PBR.BaseColorTexture, /*sRGB*/true, /*normal*/false, State);
        FGLTFProxyMaterialUtilities::SetParameterValue(Proxy, FHashedMaterialParameterInfo(TEXT("Base Color Factor")), FLinearColor(PBR.BaseColorFactor.R, PBR.BaseColorFactor.G, PBR.BaseColorFactor.B, PBR.BaseColorFactor.A), true);
        if (JsonMaterial->ShadingModel == EGLTFJsonShadingModel::Default || JsonMaterial->ShadingModel == EGLTFJsonShadingModel::ClearCoat)
        {
            SetProxyTextureParam(Proxy, TEXT("Emissive"), JsonMaterial->EmissiveTexture, /*sRGB*/true, /*normal*/false, State);
            FGLTFProxyMaterialUtilities::SetParameterValue(Proxy, FHashedMaterialParameterInfo(TEXT("Emissive Factor")), FLinearColor(JsonMaterial->EmissiveFactor.R, JsonMaterial->EmissiveFactor.G, JsonMaterial->EmissiveFactor.B), true);
            FGLTFProxyMaterialUtilities::SetParameterValue(Proxy, FHashedMaterialParameterInfo(TEXT("Emissive Strength")), JsonMaterial->EmissiveStrength, true);
            SetProxyTextureParam(Proxy, TEXT("Metallic Roughness"), PBR.MetallicRoughnessTexture, /*sRGB*/false, /*normal*/false, State);
            FGLTFProxyMaterialUtilities::SetParameterValue(Proxy, FHashedMaterialParameterInfo(TEXT("Metallic Factor")), PBR.MetallicFactor, true);
            FGLTFProxyMaterialUtilities::SetParameterValue(Proxy, FHashedMaterialParameterInfo(TEXT("Roughness Factor")), PBR.RoughnessFactor, true);
            SetProxyTextureParam(Proxy, TEXT("Normal"), JsonMaterial->NormalTexture, /*sRGB*/false, /*normal*/true, State);
            FGLTFProxyMaterialUtilities::SetParameterValue(Proxy, FHashedMaterialParameterInfo(TEXT("Normal Scale")), JsonMaterial->NormalTexture.Scale, true);
            SetProxyTextureParam(Proxy, TEXT("Occlusion"), JsonMaterial->OcclusionTexture, /*sRGB*/false, /*normal*/false, State);
            FGLTFProxyMaterialUtilities::SetParameterValue(Proxy, FHashedMaterialParameterInfo(TEXT("Occlusion Strength")), JsonMaterial->OcclusionTexture.Strength, true);
        }

        // FIX 1: refresh cached shading state (fresh MICs default to MSM_Unlit).
        Proxy->PreEditChange(nullptr);
        Proxy->PostEditChange();
        UE_LOG(LogTemp, Warning, TEXT("[ProxyProto] Proxy %s for %s: shading model after refresh = %d (0=Unlit? must be lit), blend=%d"),
            *Proxy->GetName(), *Mat->GetName(),
            static_cast<int32>(Proxy->GetShadingModels().GetFirstShadingModel()),
            static_cast<int32>(Proxy->GetBlendMode()));

        // Attach the association via Epic's AssetUserData mechanism and mark the
        // material dirty so the user can persist the link with a normal save.
        FGLTFProxyMaterialUtilities::SetProxyMaterial(Mat, Proxy);
        Mat->MarkPackageDirty();

        for (UPackage* Package : State.PackagesToSave)
        {
            const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
            FSavePackageArgs SaveArgs;
            SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
            const bool bSaved = UPackage::SavePackage(Package, nullptr, *FileName, SaveArgs);
            UE_LOG(LogTemp, Warning, TEXT("[ProxyProto] Save %s -> %s"), *Package->GetName(), bSaved ? TEXT("OK") : TEXT("FAILED"));
        }

        return Proxy;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Generic runtime-color bridge for one-proxy-per-material-family (RAL/NCS):
    // The exporter reads proxy parameters with NonDefaultOnly=true, and for a MID
    // the "default" is its parent's resolved value — so a MID created directly from
    // the family proxy hides every inherited parameter (proven by the first RAL
    // test: metallic/roughness fell back to glTF defaults). The generic bridge
    // therefore creates the MID from the BASE proxy material and copies the family
    // proxy's resolved values onto it, then overrides Base Color Factor with the
    // runtime-selected catalog color. No per-color or per-family logic involved.
    // ─────────────────────────────────────────────────────────────────────────
    void CopyProxyParamsToBridgeMID(const UMaterialInterface* FamilyProxy, UMaterialInstanceDynamic* MID)
    {
        static const TCHAR* ScalarParams[] = {
            TEXT("Metallic Factor"), TEXT("Roughness Factor"), TEXT("Emissive Strength"),
            TEXT("Occlusion Strength"), TEXT("Normal Scale") };
        static const TCHAR* VectorParams[] = { TEXT("Base Color Factor"), TEXT("Emissive Factor") };
        static const TCHAR* TextureGroups[] = {
            TEXT("Base Color"), TEXT("Metallic Roughness"), TEXT("Normal"), TEXT("Emissive"), TEXT("Occlusion") };

        for (const TCHAR* Name : ScalarParams)
        {
            float V;
            if (FamilyProxy->GetScalarParameterValue(FMaterialParameterInfo(Name), V))
            {
                MID->SetScalarParameterValue(FName(Name), V);
            }
        }
        for (const TCHAR* Name : VectorParams)
        {
            FLinearColor V;
            if (FamilyProxy->GetVectorParameterValue(FMaterialParameterInfo(Name), V))
            {
                MID->SetVectorParameterValue(FName(Name), V);
            }
        }
        for (const TCHAR* Group : TextureGroups)
        {
            UTexture* Tex = nullptr;
            if (FamilyProxy->GetTextureParameterValue(FMaterialParameterInfo(*(FString(Group) + TEXT(" Texture"))), Tex) && Tex)
            {
                MID->SetTextureParameterValue(FName(*(FString(Group) + TEXT(" Texture"))), Tex);
                static const TCHAR* ScalarSubs[] = { TEXT(" UV Index"), TEXT(" UV Rotation") };
                static const TCHAR* VectorSubs[] = { TEXT(" UV Offset"), TEXT(" UV Scale") };
                for (const TCHAR* Sub : ScalarSubs)
                {
                    float V;
                    if (FamilyProxy->GetScalarParameterValue(FMaterialParameterInfo(*(FString(Group) + Sub)), V))
                    {
                        MID->SetScalarParameterValue(FName(*(FString(Group) + Sub)), V);
                    }
                }
                for (const TCHAR* Sub : VectorSubs)
                {
                    FLinearColor V;
                    if (FamilyProxy->GetVectorParameterValue(FMaterialParameterInfo(*(FString(Group) + Sub)), V))
                    {
                        MID->SetVectorParameterValue(FName(*(FString(Group) + Sub)), V);
                    }
                }
            }
        }
    }

    UMaterialInstanceDynamic* MakeRuntimeColorBridgeMID(UMaterialInterface* FamilyMaterial, const FLinearColor& CatalogColor, UObject* Outer)
    {
        UMaterialInterface* FamilyProxy = CreateProxyForMaterialGeneric(FamilyMaterial);
        if (!FamilyProxy)
        {
            return nullptr;
        }
        UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(FamilyProxy->GetMaterial(), Outer);
        CopyProxyParamsToBridgeMID(FamilyProxy, MID);
        MID->SetVectorParameterValue(FName(TEXT("Base Color Factor")), CatalogColor);
        return MID;
    }

    void ExportActorProxyOnly(UWorld* World, AActor* Actor, UStaticMeshComponent* Comp, const TCHAR* TestName)
    {
        for (int32 i = 0; i < Comp->GetNumMaterials(); ++i)
        {
            const UMaterialInterface* SlotMat = Comp->GetMaterial(i);
            const UMaterialInterface* Resolved = UGLTFMaterialExportOptions::ResolveProxy(SlotMat);
            UE_LOG(LogTemp, Warning, TEXT("[ProxyProto][%s] Slot %d: %s -> ResolveProxy -> %s (IsProxyMaterial=%s)"),
                TestName, i,
                SlotMat ? *SlotMat->GetName() : TEXT("<null>"),
                Resolved ? *Resolved->GetName() : TEXT("<null>"),
                (Resolved && FGLTFProxyMaterialUtilities::IsProxyMaterial(Resolved)) ? TEXT("YES") : TEXT("no"));
        }

        UGLTFExportOptions* Options = NewObject<UGLTFExportOptions>();
        Options->ResetToDefault();
        Options->BakeMaterialInputs = EGLTFMaterialBakeMode::Disabled; // editor baking forbidden
        Options->TextureImageFormat = EGLTFTextureImageFormat::PNG;

        const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AR_Exports"), TEXT("New folder"));
        IFileManager::Get().MakeDirectory(*Dir, true);
        const FString FilePath = FPaths::Combine(Dir, FString::Printf(TEXT("proto_PROXY_%s_%s.glb"), TestName, *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));

        FGLTFExportMessages Messages;
        TSet<AActor*> Selected;
        Selected.Add(Actor);
        const bool bOK = UGLTFExporter::ExportToGLTF(World, FilePath, Options, Selected, Messages);

        for (const FString& Msg : Messages.Errors)   { UE_LOG(LogTemp, Error,   TEXT("[ProxyProto][%s] EXPORT ERROR: %s"), TestName, *Msg); }
        for (const FString& Msg : Messages.Warnings) { UE_LOG(LogTemp, Warning, TEXT("[ProxyProto][%s] EXPORT WARNING: %s"), TestName, *Msg); }
        UE_LOG(LogTemp, Warning, TEXT("[ProxyProto][%s] Export %s (%lld bytes) -> %s"),
            TestName, bOK ? TEXT("SUCCEEDED") : TEXT("FAILED"), IFileManager::Get().FileSize(*FilePath), *FilePath);
    }

    AStaticMeshActor* SpawnTestMesh(UWorld* World, const TCHAR* MeshPath)
    {
        UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath);
        if (!Mesh)
        {
            UE_LOG(LogTemp, Error, TEXT("[ProxyProto] Mesh not found: %s"), MeshPath);
            return nullptr;
        }
        AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator);
        Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
        return Actor;
    }

    void RunAllProxyTests(UWorld* World)
    {
        if (!World)
        {
            UE_LOG(LogTemp, Error, TEXT("[ProxyProto] No world."));
            return;
        }

        // ── TEST 1: RAL/NCS — antr proxy (static) + a REAL runtime MID recolor to RAL 3020 ──
        if (AStaticMeshActor* Actor = SpawnTestMesh(World, TEXT("/Game/SM_MERGED_StaticMeshActor_16.SM_MERGED_StaticMeshActor_16")))
        {
            UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent();
            for (int32 i = 0; i < Comp->GetNumMaterials(); ++i)
            {
                CreateProxyForMaterialGeneric(Comp->GetMaterial(i));
            }
            // Runtime MID color change on slot 1: NewMaterial1 (the material the accepted
            // FINAL_RAL3020 reference recolored) -> proxy -> live MID with RAL 3020 factor.
            if (UMaterialInterface* NewMat1 = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/scena/Materials/NewMaterial1.NewMaterial1")))
            {
                const FLinearColor RAL3020(0.497f, 0.014f, 0.006f, 1.0f); // test input value (from accepted FINAL_RAL3020 reference)
                if (UMaterialInstanceDynamic* MID = MakeRuntimeColorBridgeMID(NewMat1, RAL3020, Comp))
                {
                    Comp->SetMaterial(1, MID);
                    UE_LOG(LogTemp, Warning, TEXT("[ProxyProto][RAL_TEST] Slot 1 set to generic bridge MID (family proxy of NewMaterial1) with catalog color (0.497, 0.014, 0.006)"));
                }
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("[ProxyProto][RAL_TEST] NewMaterial1 not found - MID recolor leg skipped"));
            }
            ExportActorProxyOnly(World, Actor, Comp, TEXT("RAL_TEST"));
            Actor->Destroy();
        }

        // ── TEST 2: METAL — faucet RILIEVO_59093 with white_Inst (V-Ray gold) ──
        if (AStaticMeshActor* Actor = SpawnTestMesh(World, TEXT("/Game/Models/Faucet/Fauset/RILIEVO_59093.RILIEVO_59093")))
        {
            UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent();
            for (int32 i = 0; i < Comp->GetNumMaterials(); ++i)
            {
                CreateProxyForMaterialGeneric(Comp->GetMaterial(i));
            }
            ExportActorProxyOnly(World, Actor, Comp, TEXT("METAL_TEST"));
            Actor->Destroy();
        }

        // ── TEST 3: GLASS — closet door with аttik / cеrniy_mеtаl ──
        if (AStaticMeshActor* Actor = SpawnTestMesh(World, TEXT("/Game/Models/NewFolder/Closet_1_door.Closet_1_door")))
        {
            UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent();
            for (int32 i = 0; i < Comp->GetNumMaterials(); ++i)
            {
                CreateProxyForMaterialGeneric(Comp->GetMaterial(i));
            }
            ExportActorProxyOnly(World, Actor, Comp, TEXT("GLASS_TEST"));
            Actor->Destroy();
        }

        UE_LOG(LogTemp, Warning, TEXT("[ProxyProto] All three proxy tests done."));
    }

    // Genericity proof for the one-proxy-per-family RAL/NCS model: same mesh, same
    // single family proxy, three DIFFERENT catalog colors (values taken from
    // Content/Data/Colors/ral_classic.json). Pass = each GLB's baseColorFactor equals
    // its input color exactly and metallic/roughness carry the family's true values.
    void RunRALGenericColorTests(UWorld* World)
    {
        if (!World)
        {
            UE_LOG(LogTemp, Error, TEXT("[ProxyProto] No world."));
            return;
        }
        UMaterialInterface* NewMat1 = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/scena/Materials/NewMaterial1.NewMaterial1"));
        if (!NewMat1)
        {
            UE_LOG(LogTemp, Error, TEXT("[ProxyProto] NewMaterial1 not found"));
            return;
        }

        struct FColorCase { const TCHAR* Label; const TCHAR* Hex; FLinearColor Linear; };
        FColorCase Cases[] = {
            { TEXT("RAL3020"), TEXT("CC0605"), FLinearColor(0.497f, 0.014f, 0.006f) }, // accepted reference value
            { TEXT("RAL6018"), TEXT("60993B"), FLinearColor(FColor::FromHex(TEXT("60993B"))) }, // Yellow green, from catalog json
            { TEXT("RAL5015"), TEXT("007CAF"), FLinearColor(FColor::FromHex(TEXT("007CAF"))) }, // Sky blue, from catalog json
        };

        for (const FColorCase& Case : Cases)
        {
            if (AStaticMeshActor* Actor = SpawnTestMesh(World, TEXT("/Game/SM_MERGED_StaticMeshActor_16.SM_MERGED_StaticMeshActor_16")))
            {
                UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent();
                for (int32 i = 0; i < Comp->GetNumMaterials(); ++i)
                {
                    CreateProxyForMaterialGeneric(Comp->GetMaterial(i));
                }
                if (UMaterialInstanceDynamic* MID = MakeRuntimeColorBridgeMID(NewMat1, Case.Linear, Comp))
                {
                    Comp->SetMaterial(1, MID);
                }
                UE_LOG(LogTemp, Warning, TEXT("[ProxyProto][RAL_COLOR_%s] Catalog color #%s -> linear (%.4f, %.4f, %.4f)"),
                    Case.Label, Case.Hex, Case.Linear.R, Case.Linear.G, Case.Linear.B);
                ExportActorProxyOnly(World, Actor, Comp, *FString::Printf(TEXT("RAL_COLOR_%s"), Case.Label));
                Actor->Destroy();
            }
        }
        UE_LOG(LogTemp, Warning, TEXT("[ProxyProto] RAL generic color tests done."));
    }

    FAutoConsoleCommand GProxyRALGenericCmd(
        TEXT("ar.ProxyRALGenericTest"),
        TEXT("TEMP PROTOTYPE: prove one-proxy-per-family RAL/NCS genericity with 3 different catalog colors."),
        FConsoleCommandDelegate::CreateLambda([]()
        {
            UWorld* World = nullptr;
            if (GEngine)
            {
                for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
                {
                    if (Ctx.World() && (Ctx.WorldType == EWorldType::PIE || Ctx.WorldType == EWorldType::Game))
                    {
                        World = Ctx.World();
                        break;
                    }
                }
            }
            RunRALGenericColorTests(World);
        }));

    FAutoConsoleCommand GProxyAllTestsCmd(
        TEXT("ar.ProxyAllTests"),
        TEXT("TEMP PROTOTYPE: run RAL/METAL/GLASS proxy-only export tests (editor baking disabled)."),
        FConsoleCommandDelegate::CreateLambda([]()
        {
            UWorld* World = nullptr;
            if (GEngine)
            {
                for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
                {
                    if (Ctx.World() && (Ctx.WorldType == EWorldType::PIE || Ctx.WorldType == EWorldType::Game))
                    {
                        World = Ctx.World();
                        break;
                    }
                }
            }
            RunAllProxyTests(World);
        }));

    FAutoConsoleCommand GProxyWoodTestCmd(
        TEXT("ar.ProxyWoodTest"),
        TEXT("TEMP PROTOTYPE: create Epic glTF proxy for Wood_ and export test mesh with editor baking DISABLED."),
        FConsoleCommandDelegate::CreateLambda([]()
        {
            UWorld* World = nullptr;
            if (GEngine)
            {
                for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
                {
                    if (Ctx.World() && (Ctx.WorldType == EWorldType::PIE || Ctx.WorldType == EWorldType::Game))
                    {
                        World = Ctx.World();
                        break;
                    }
                }
            }
            RunProxyWoodExport(World);
        }));

    // Headless auto-run: UnrealEditor-Cmd <project> -game -ARGLTFProxyProto
    struct FARGLTFProxyProtoAutoRun
    {
        FARGLTFProxyProtoAutoRun()
        {
            FCoreUObjectDelegates::PostLoadMapWithWorld.AddLambda([](UWorld* World)
            {
                static bool bScheduled = false;
                const bool bWoodFlag = FParse::Param(FCommandLine::Get(), TEXT("ARGLTFProxyProto"));
                const bool bAllFlag = FParse::Param(FCommandLine::Get(), TEXT("ARGLTFProxyAllTests"));
                if (bScheduled || !World || (!bWoodFlag && !bAllFlag))
                {
                    return;
                }
                bScheduled = true;

                FTimerHandle Handle;
                World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([World, bAllFlag]()
                {
                    if (bAllFlag)
                    {
                        RunAllProxyTests(World);
                    }
                    else
                    {
                        RunProxyWoodExport(World);
                    }
                    UE_LOG(LogTemp, Warning, TEXT("[ProxyProto] Auto-run complete, quitting."));
                    if (GEngine)
                    {
                        GEngine->Exec(World, TEXT("QUIT"));
                    }
                }), 8.0f, false);
            });
        }
    };
    static FARGLTFProxyProtoAutoRun GARGLTFProxyProtoAutoRun;
}

#endif // WITH_EDITOR
