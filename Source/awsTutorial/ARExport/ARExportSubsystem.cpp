// Copyright MaxiMall Project. All Rights Reserved.

#include "ARExport/ARExportSubsystem.h"
#include "FurnitureConfigurator/ShowroomBooth.h"
#include "ARExport/QRCode/QRCodeTextureHelper.h"
#include "ARExport/GLB/SimpleGLBWriter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "StaticMeshResources.h"
#include "Rendering/PositionVertexBuffer.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Async/Async.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

namespace
{
    /** Holds UV Transformation parameters (Tiling, Offset, Rotation) extracted from Material Instances */
    struct FUVTransform
    {
        FVector2f Tiling = FVector2f(1.0f, 1.0f);
        FVector2f Offset = FVector2f(0.0f, 0.0f);
        float W_Rotation = 0.0f;
        FVector2f RotationPivot = FVector2f(0.0f, 0.0f);
        FVector2f TilingPivot = FVector2f(0.0f, 0.0f);
        bool bHasTransform = false;

        FVector2f TransformUV(const FVector2f& InUV) const
        {
            if (!bHasTransform)
            {
                return InUV;
            }

            FVector2f Out = InUV;

            // 1. Tiling around TilingPivot
            Out = (Out - TilingPivot) * Tiling + TilingPivot;

            // 2. Offset
            Out += Offset;

            // 3. Rotation around RotationPivot
            if (!FMath::IsNearlyZero(W_Rotation))
            {
                // In 3ds Max / Datasmith, W_Rotation is in turns (-0.25 = -90 degrees = -PI/2)
                const float AngleRad = W_Rotation * 2.0f * PI;
                const float CosA = FMath::Cos(AngleRad);
                const float SinA = FMath::Sin(AngleRad);

                const FVector2f P = Out - RotationPivot;
                Out.X = P.X * CosA - P.Y * SinA + RotationPivot.X;
                Out.Y = P.X * SinA + P.Y * CosA + RotationPivot.Y;
            }

            return Out;
        }
    };

    /** Extracted PBR parameter state for any dynamic or static material */
    struct FPBRParsedState
    {
        FLinearColor BaseColor = FLinearColor::White;
        float Metallic = 0.0f;
        float Roughness = 0.35f;
        UTexture2D* BaseColorTexture = nullptr;
        UTexture2D* NormalTexture = nullptr;
        UTexture2D* MetallicRoughnessTexture = nullptr;
        UTexture2D* RoughnessTexture = nullptr;
        UTexture2D* MetallicTexture = nullptr;
        FUVTransform UVTransform;
    };

    /** Recursively searches a material and its nested Material Functions for all referenced UTexture2D assets */
    void FindTexturesInMaterial(
        UMaterialInterface* Mat,
        TArray<UTexture2D*>& OutBaseColorTextures,
        TArray<UTexture2D*>& OutNormalTextures,
        TArray<UTexture2D*>& OutRoughnessTextures,
        TArray<UTexture2D*>& OutMetallicTextures,
        TArray<UTexture2D*>& OutORMTextures)
    {
        if (!Mat)
        {
            return;
        }

        auto ClassifyAndAddTexture = [&](UTexture2D* Tex, const FString& ParamName)
        {
            if (!Tex)
            {
                return;
            }

            const FString FullIdentifier = (ParamName + TEXT(" ") + Tex->GetPathName() + TEXT(" ") + Tex->GetName()).ToLower();
            if (FullIdentifier.Contains(TEXT("normal")) || FullIdentifier.Contains(TEXT("nrm")) ||
                FullIdentifier.Contains(TEXT("norm")) || FullIdentifier.Contains(TEXT("bump")))
            {
                OutNormalTextures.AddUnique(Tex);
            }
            else if (FullIdentifier.Contains(TEXT("orm")) || FullIdentifier.Contains(TEXT("arm")) || FullIdentifier.Contains(TEXT("mro")))
            {
                OutORMTextures.AddUnique(Tex);
            }
            else if (FullIdentifier.Contains(TEXT("rough")) || FullIdentifier.Contains(TEXT("gloss")) ||
                     FullIdentifier.Contains(TEXT("ref")) || FullIdentifier.Contains(TEXT("spec")))
            {
                OutRoughnessTextures.AddUnique(Tex);
            }
            else if (FullIdentifier.Contains(TEXT("metal")) || FullIdentifier.Contains(TEXT("metallic")))
            {
                OutMetallicTextures.AddUnique(Tex);
            }
            else
            {
                OutBaseColorTextures.AddUnique(Tex);
            }
        };

        // 1. Texture Parameters on Material Instance
        if (UMaterialInstance* Inst = Cast<UMaterialInstance>(Mat))
        {
            TArray<FMaterialParameterInfo> TexInfos;
            TArray<FGuid> Guids;
            Inst->GetAllTextureParameterInfo(TexInfos, Guids);
            for (const FMaterialParameterInfo& Info : TexInfos)
            {
                UTexture* Tex = nullptr;
                if (Inst->GetTextureParameterValue(Info, Tex) && Tex)
                {
                    if (UTexture2D* Tex2D = Cast<UTexture2D>(Tex))
                    {
                        ClassifyAndAddTexture(Tex2D, Info.Name.ToString());
                    }
                }
            }
        }

        // 2. Expressions in Base Material and all nested Material Functions
        UMaterial* BaseMat = Mat->GetMaterial();
        if (BaseMat)
        {
            TArray<UMaterialExpression*> ExpressionsToProcess;
            for (const TObjectPtr<UMaterialExpression>& ExprPtr : BaseMat->GetExpressions())
            {
                if (ExprPtr)
                {
                    ExpressionsToProcess.Add(ExprPtr.Get());
                }
            }

            TSet<UMaterialFunctionInterface*> ProcessedFunctions;

            for (int32 i = 0; i < ExpressionsToProcess.Num(); ++i)
            {
                UMaterialExpression* Expr = ExpressionsToProcess[i];
                if (!Expr)
                {
                    continue;
                }

                if (UMaterialExpressionTextureSample* TS = Cast<UMaterialExpressionTextureSample>(Expr))
                {
                    if (UTexture2D* Tex2D = Cast<UTexture2D>(TS->Texture))
                    {
                        ClassifyAndAddTexture(Tex2D, TS->GetName());
                    }
                }
                else if (UMaterialExpressionTextureBase* TB = Cast<UMaterialExpressionTextureBase>(Expr))
                {
                    if (UTexture2D* Tex2D = Cast<UTexture2D>(TB->Texture))
                    {
                        ClassifyAndAddTexture(Tex2D, TB->GetName());
                    }
                }
                else if (UMaterialExpressionMaterialFunctionCall* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expr))
                {
                    if (FuncCall->MaterialFunction && !ProcessedFunctions.Contains(FuncCall->MaterialFunction))
                    {
                        ProcessedFunctions.Add(FuncCall->MaterialFunction);
                        if (UMaterialFunction* MF = Cast<UMaterialFunction>(FuncCall->MaterialFunction))
                        {
                            for (const TObjectPtr<UMaterialExpression>& FuncExprPtr : MF->GetExpressions())
                            {
                                if (FuncExprPtr)
                                {
                                    ExpressionsToProcess.Add(FuncExprPtr.Get());
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    /** Extracts an uncompressed PNG byte buffer from a UTexture2D for embedding into .glb */
    bool ExtractTexturePNG(UTexture2D* Tex, TArray<uint8>& OutPNG)
    {
        if (!Tex)
        {
            return false;
        }

        // 1. Try ImageUtils Source Image (fastest, lossless, full resolution)
        FImage SourceImage;
        if (FImageUtils::GetTexture2DSourceImage(Tex, SourceImage))
        {
            TArray64<uint8> CompressedBytes;
            if (FImageUtils::CompressImage(CompressedBytes, TEXT("png"), SourceImage))
            {
                OutPNG.SetNumUninitialized(CompressedBytes.Num());
                FMemory::Memcpy(OutPNG.GetData(), CompressedBytes.GetData(), CompressedBytes.Num());
                return OutPNG.Num() > 0;
            }
        }

        // 2. Fallback: Read Mip 0 Platform Data
        FTexturePlatformData* PlatformData = Tex->GetPlatformData();
        if (PlatformData && PlatformData->Mips.Num() > 0)
        {
            FTexture2DMipMap& Mip0 = PlatformData->Mips[0];
            const int32 Width = Mip0.SizeX;
            const int32 Height = Mip0.SizeY;
            if (Width > 0 && Height > 0)
            {
                const void* RawData = Mip0.BulkData.LockReadOnly();
                if (RawData)
                {
                    if (PlatformData->PixelFormat == PF_B8G8R8A8)
                    {
                        IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
                        TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
                        if (ImageWrapper.IsValid() && ImageWrapper->SetRaw(RawData, Width * Height * 4, Width, Height, ERGBFormat::BGRA, 8))
                        {
                            OutPNG = ImageWrapper->GetCompressed(100);
                        }
                    }
                    Mip0.BulkData.Unlock();
                    return OutPNG.Num() > 0;
                }
            }
        }

        return false;
    }

    /** Extracts or combines Roughness and Metallic textures into a standard glTF 2.0 MetallicRoughness PNG (G=Roughness, B=Metallic) */
    bool ExtractMetallicRoughnessPNG(UTexture2D* InRoughnessTex, UTexture2D* InMetallicTex, UTexture2D* InORMTex, float FallbackMetallic, float FallbackRoughness, TArray<uint8>& OutPNG)
    {
        if (InORMTex)
        {
            return ExtractTexturePNG(InORMTex, OutPNG);
        }

        UTexture2D* SourceTex = InRoughnessTex ? InRoughnessTex : InMetallicTex;
        if (!SourceTex)
        {
            return false;
        }

        FImage SourceImg;
        if (!FImageUtils::GetTexture2DSourceImage(SourceTex, SourceImg) || SourceImg.RawData.Num() == 0)
        {
            return false;
        }

        const int32 Width = SourceImg.SizeX;
        const int32 Height = SourceImg.SizeY;
        TArray64<uint8> CombinedData;
        CombinedData.SetNumUninitialized(Width * Height * 4);

        const bool bIsInvertGloss = InRoughnessTex && (InRoughnessTex->GetName().ToLower().Contains(TEXT("gloss")) || 
                                                       InRoughnessTex->GetName().ToLower().Contains(TEXT("ref")));
        const uint8 MetallicByte = FMath::Clamp(FMath::RoundToInt(FallbackMetallic * 255.0f), 0, 255);
        const uint8 RoughnessByte = FMath::Clamp(FMath::RoundToInt(FallbackRoughness * 255.0f), 0, 255);

        for (int32 i = 0; i < Width * Height; ++i)
        {
            const int32 SrcOffset = i * 4;
            const int32 DstOffset = i * 4;

            uint8 R = 255; // Occlusion
            uint8 G = RoughnessByte; // Roughness
            uint8 B = MetallicByte; // Metallic
            uint8 A = 255;

            if (InRoughnessTex && SrcOffset < SourceImg.RawData.Num())
            {
                uint8 RawVal = SourceImg.RawData[SrcOffset];
                G = bIsInvertGloss ? (255 - RawVal) : RawVal;
            }

            if (InMetallicTex && SrcOffset < SourceImg.RawData.Num())
            {
                B = SourceImg.RawData[SrcOffset];
            }

            CombinedData[DstOffset + 0] = R;
            CombinedData[DstOffset + 1] = G;
            CombinedData[DstOffset + 2] = B;
            CombinedData[DstOffset + 3] = A;
        }

        FImage CombinedImage(Width, Height, ERawImageFormat::BGRA8, EGammaSpace::Linear);
        CombinedImage.RawData = MoveTemp(CombinedData);
        TArray64<uint8> CompressedBytes;
        if (FImageUtils::CompressImage(CompressedBytes, TEXT("png"), CombinedImage))
        {
            OutPNG.SetNumUninitialized(CompressedBytes.Num());
            FMemory::Memcpy(OutPNG.GetData(), CompressedBytes.GetData(), CompressedBytes.Num());
            return OutPNG.Num() > 0;
        }
        return false;
    }

    /**
     * 100% Material-Agnostic PBR Parameter Resolver:
     * Reads BaseColor, Metallic, Roughness, Normal, and UV Transforms directly
     * from any active UMaterialInterface / UMaterialInstance / Datasmith / CAD shader.
     */
    FPBRParsedState ResolveMaterialPBRState(UMaterialInterface* SlotMat)
    {
        FPBRParsedState State;
        if (!SlotMat)
        {
            return State;
        }

        bool bBaseColorFound = false;
        bool bMetallicFound = false;
        bool bRoughnessFound = false;
        FLinearColor ReflectionColor = FLinearColor::Black;
        float ReflectionLevel = 0.0f;
        float ReflectionGlossiness = 0.0f;

        // ── 1. Parse Vector Parameters on UMaterialInterface (Works for UMaterial, MIC, MID) ───
        TArray<FMaterialParameterInfo> VInfos;
        TArray<FGuid> Guids;
        SlotMat->GetAllVectorParameterInfo(VInfos, Guids);

        for (const FMaterialParameterInfo& VInfo : VInfos)
        {
            const FString LowerName = VInfo.Name.ToString().ToLower();
            FLinearColor Val;
            if (!SlotMat->GetVectorParameterValue(VInfo, Val))
            {
                continue;
            }

            // UV Scale / Offset / Pivots
            if (LowerName.Contains(TEXT("uv_tiling")) || LowerName.Contains(TEXT("tiling (")))
            {
                State.UVTransform.Tiling = FVector2f(Val.R, Val.G);
                State.UVTransform.bHasTransform = true;
            }
            else if (LowerName.Contains(TEXT("uv_offset")) || LowerName.Contains(TEXT("offset (")))
            {
                State.UVTransform.Offset = FVector2f(Val.R, Val.G);
                State.UVTransform.bHasTransform = true;
            }
            else if (LowerName.Contains(TEXT("rotation_pivot")))
            {
                State.UVTransform.RotationPivot = FVector2f(Val.R, Val.G);
            }
            else if (LowerName.Contains(TEXT("tiling_pivot")))
            {
                State.UVTransform.TilingPivot = FVector2f(Val.R, Val.G);
            }
            // Base Color / Diffuse
            else if (LowerName.Contains(TEXT("basecolor")) || LowerName.Contains(TEXT("base_color")) ||
                     LowerName.Contains(TEXT("albedo")) || LowerName.Contains(TEXT("diffuse")) ||
                     LowerName.Contains(TEXT("maincolor")) || LowerName.Contains(TEXT("tint")) ||
                     LowerName.Equals(TEXT("color")))
            {
                if (!bBaseColorFound)
                {
                    State.BaseColor = Val;
                    bBaseColorFound = true;
                }
            }
            else if (LowerName.Contains(TEXT("reflection")) && !LowerName.Contains(TEXT("gloss")))
            {
                ReflectionColor = Val;
            }
        }

        // ── 2. Parse Scalar Parameters on UMaterialInterface ─────────────────
        TArray<FMaterialParameterInfo> SInfos;
        SlotMat->GetAllScalarParameterInfo(SInfos, Guids);

        for (const FMaterialParameterInfo& SInfo : SInfos)
        {
            const FString LowerName = SInfo.Name.ToString().ToLower();
            float SVal = 0.0f;
            if (!SlotMat->GetScalarParameterValue(SInfo, SVal))
            {
                continue;
            }

            if (LowerName.Contains(TEXT("metallic")) || LowerName.Contains(TEXT("metalness")) || LowerName.Equals(TEXT("metal")))
            {
                State.Metallic = FMath::Clamp(SVal, 0.0f, 1.0f);
                bMetallicFound = true;
            }
            else if (LowerName.Contains(TEXT("roughness")) || LowerName.Contains(TEXT("rough")))
            {
                State.Roughness = FMath::Clamp(SVal, 0.0f, 1.0f);
                bRoughnessFound = true;
            }
            else if (LowerName.Contains(TEXT("reflection_glossiness")) || LowerName.Contains(TEXT("glossiness")) || LowerName.Equals(TEXT("gloss")))
            {
                ReflectionGlossiness = SVal;
                if (!bRoughnessFound)
                {
                    // In PBR, Roughness = 1.0 - Glossiness
                    State.Roughness = FMath::Clamp(1.0f - SVal, 0.0f, 1.0f);
                }
            }
            else if (LowerName.Contains(TEXT("reflection_level")) || LowerName.Contains(TEXT("reflection (")))
            {
                ReflectionLevel = SVal;
            }
            // Scalar UV parameters
            else if (LowerName.Contains(TEXT("w_rotation")) || LowerName.Contains(TEXT("rotation")))
            {
                State.UVTransform.W_Rotation = SVal;
                State.UVTransform.bHasTransform = true;
            }
            else if (LowerName.Contains(TEXT("tiling_u")) || LowerName.Contains(TEXT("tiling_x")))
            {
                State.UVTransform.Tiling.X = SVal;
                State.UVTransform.bHasTransform = true;
            }
            else if (LowerName.Contains(TEXT("tiling_v")) || LowerName.Contains(TEXT("tiling_y")))
            {
                State.UVTransform.Tiling.Y = SVal;
                State.UVTransform.bHasTransform = true;
            }
            else if (LowerName.Contains(TEXT("offset_u")) || LowerName.Contains(TEXT("offset_x")))
            {
                State.UVTransform.Offset.X = SVal;
                State.UVTransform.bHasTransform = true;
            }
            else if (LowerName.Contains(TEXT("offset_v")) || LowerName.Contains(TEXT("offset_y")))
            {
                State.UVTransform.Offset.Y = SVal;
                State.UVTransform.bHasTransform = true;
            }
        }

        // ── 3. V-Ray / Datasmith Specular Workflow Conversion ────────────
        // In V-Ray metals (e.g. gold, chrome, brass), diffuse is black and color is in Reflection.
        if (!bMetallicFound && ReflectionLevel > 0.5f)
        {
            const float MaxRefl = FMath::Max3(ReflectionColor.R, ReflectionColor.G, ReflectionColor.B);
            if (MaxRefl > 0.3f && State.BaseColor.GetLuminance() < 0.2f)
            {
                State.Metallic = FMath::Clamp(ReflectionLevel, 0.0f, 1.0f);
                State.BaseColor = ReflectionColor;
            }
        }

        // ── 4. Fallback for non-parameterized UMaterials ─────────────────────
        if (!bMetallicFound || !bRoughnessFound)
        {
            const FString MatName = SlotMat->GetName().ToLower();
            if (MatName.Contains(TEXT("mirror")) || MatName.Contains(TEXT("glass")))
            {
                if (!bMetallicFound) State.Metallic = 0.98f;
                if (!bRoughnessFound) State.Roughness = 0.02f;
                if (!bBaseColorFound) State.BaseColor = FLinearColor(0.95f, 0.97f, 1.0f, 1.0f);
            }
            else if (MatName.Contains(TEXT("chrome")) || MatName.Contains(TEXT("brass")) || MatName.Contains(TEXT("gold")) || MatName.Contains(TEXT("metal")) || MatName.Contains(TEXT("faucet")))
            {
                if (!bMetallicFound) State.Metallic = 0.95f;
                if (!bRoughnessFound) State.Roughness = 0.08f;
            }
            else if (MatName.Contains(TEXT("ceramic")) || MatName.Contains(TEXT("porcelain")) || MatName.Contains(TEXT("sink")) || MatName.Contains(TEXT("glaze")) || MatName.Contains(TEXT("enamel")))
            {
                if (!bMetallicFound) State.Metallic = 0.0f;
                if (!bRoughnessFound) State.Roughness = 0.08f;
            }
            else if (MatName.Contains(TEXT("frame")) || MatName.Contains(TEXT("dark")) || MatName.Contains(TEXT("black")) || MatName.Contains(TEXT("nero")))
            {
                if (!bMetallicFound) State.Metallic = 0.85f;
                if (!bRoughnessFound) State.Roughness = 0.25f;
                if (!bBaseColorFound) State.BaseColor = FLinearColor(0.015f, 0.015f, 0.018f, 1.0f);
            }
            else if (MatName.Contains(TEXT("countertop")) || MatName.Contains(TEXT("marble")) || MatName.Contains(TEXT("quartz")) || MatName.Contains(TEXT("granite")))
            {
                if (!bMetallicFound) State.Metallic = 0.0f;
                if (!bRoughnessFound) State.Roughness = 0.15f;
            }
        }

        // ── 5. Extract Textures ──────────────────────────────────────────────
        TArray<UTexture2D*> BaseTextures;
        TArray<UTexture2D*> NormalTextures;
        TArray<UTexture2D*> RoughnessTextures;
        TArray<UTexture2D*> MetallicTextures;
        TArray<UTexture2D*> ORMTextures;
        FindTexturesInMaterial(SlotMat, BaseTextures, NormalTextures, RoughnessTextures, MetallicTextures, ORMTextures);

        if (BaseTextures.Num() > 0)
        {
            State.BaseColorTexture = BaseTextures[0];
        }
        if (NormalTextures.Num() > 0)
        {
            State.NormalTexture = NormalTextures[0];
        }
        if (ORMTextures.Num() > 0)
        {
            State.MetallicRoughnessTexture = ORMTextures[0];
        }
        else if (RoughnessTextures.Num() > 0)
        {
            State.RoughnessTexture = RoughnessTextures[0];
        }
        if (MetallicTextures.Num() > 0)
        {
            State.MetallicTexture = MetallicTextures[0];
        }

        return State;
    }
}

void UARExportSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("[ARExportSubsystem] Initialized successfully."));
}

void UARExportSubsystem::Deinitialize()
{
    Super::Deinitialize();
}

FString UARExportSubsystem::GetLocalHostIPAddress() const
{
    bool bCanBindAll = false;
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

    if (SocketSubsystem)
    {
        TSharedRef<FInternetAddr> LocalAddr = SocketSubsystem->GetLocalHostAddr(*GLog, bCanBindAll);
        if (LocalAddr->IsValid())
        {
            FString IPStr = LocalAddr->ToString(false);
            if (!IPStr.IsEmpty() && !IPStr.StartsWith(TEXT("127.")))
            {
                return IPStr;
            }
        }
    }

    return TEXT("127.0.0.1");
}

void UARExportSubsystem::ExportBoothToAR(AShowroomBooth* TargetBooth, FOnARExportFinished OnFinished)
{
    if (!TargetBooth)
    {
        OnFinished.ExecuteIfBound(false, TEXT(""), TEXT(""), nullptr);
        return;
    }

    // ── 1. Extract Geometry, Dynamic PBR Parameters & Textures ───────────────
    TArray<FGLBPrimitive> Primitives;
    TMap<FString, TArray<uint8>> TextureCache;

    TArray<UStaticMeshComponent*> AllMeshComponents;
    TargetBooth->GetComponents<UStaticMeshComponent>(AllMeshComponents);

    for (UStaticMeshComponent* Comp : AllMeshComponents)
    {
        if (!Comp || !Comp->IsVisible() || !Comp->GetStaticMesh())
        {
            continue;
        }

        UStaticMesh* Mesh = Comp->GetStaticMesh();
        if (!Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.Num() == 0)
        {
            continue;
        }

        const FTransform ComponentToBoothTransform = Comp->GetComponentTransform().GetRelativeTransform(TargetBooth->GetActorTransform());
        const FStaticMeshLODResources& LOD = Mesh->GetRenderData()->LODResources[0];
        const FPositionVertexBuffer& PosBuffer = LOD.VertexBuffers.PositionVertexBuffer;
        const FStaticMeshVertexBuffer& VertBuffer = LOD.VertexBuffers.StaticMeshVertexBuffer;
        const FRawStaticIndexBuffer& IndexBuffer = LOD.IndexBuffer;

        TArray<uint32> AllIndices;
        IndexBuffer.GetCopy(AllIndices);

        const FString CompName = Comp->GetName();

        for (int32 SectionIdx = 0; SectionIdx < LOD.Sections.Num(); ++SectionIdx)
        {
            const FStaticMeshSection& Section = LOD.Sections[SectionIdx];
            const int32 MatSlotIndex = Section.MaterialIndex;
            const uint32 FirstIndex = Section.FirstIndex;
            const uint32 IndexCount = Section.NumTriangles * 3;

            if (IndexCount == 0 || FirstIndex >= (uint32)AllIndices.Num())
            {
                continue;
            }

            // Resolve Material assigned to this specific slot
            UMaterialInterface* SlotMat = Comp->GetMaterial(MatSlotIndex);
            if (!SlotMat && Mesh->GetStaticMaterials().IsValidIndex(MatSlotIndex))
            {
                SlotMat = Mesh->GetStaticMaterials()[MatSlotIndex].MaterialInterface;
            }
            if (!SlotMat)
            {
                SlotMat = Mesh->GetMaterial(MatSlotIndex);
            }

            // ── 2. Universal Material-Agnostic PBR Extraction ────────────────
            FPBRParsedState PBR = ResolveMaterialPBRState(SlotMat);

            // ── 3. Build GLB Primitive ───────────────────────────────────────
            FGLBPrimitive Prim;
            Prim.MeshName = FString::Printf(TEXT("%s_Slot%d"), *CompName, MatSlotIndex);
            Prim.BaseColor = PBR.BaseColor;
            Prim.Metallic = PBR.Metallic;
            Prim.Roughness = PBR.Roughness;

            // Embed BaseColor Texture (if present)
            if (PBR.BaseColorTexture)
            {
                const FString TexPath = PBR.BaseColorTexture->GetPathName();
                Prim.BaseColorTextureKey = TexPath;

                if (TArray<uint8>* CachedPNG = TextureCache.Find(TexPath))
                {
                    Prim.BaseColorTexturePNG = *CachedPNG;
                    Prim.BaseColor = FLinearColor::White;
                }
                else
                {
                    TArray<uint8> PNGBytes;
                    if (ExtractTexturePNG(PBR.BaseColorTexture, PNGBytes) && PNGBytes.Num() > 0)
                    {
                        UE_LOG(LogTemp, Log, TEXT("[ARExportSubsystem] Embedded Diffuse texture %s (%d bytes) for %s"), *TexPath, PNGBytes.Num(), *Prim.MeshName);
                        TextureCache.Add(TexPath, PNGBytes);
                        Prim.BaseColorTexturePNG = MoveTemp(PNGBytes);
                        Prim.BaseColor = FLinearColor::White;
                    }
                }
            }

            // Embed Normal Texture (if present)
            if (PBR.NormalTexture)
            {
                const FString TexPath = PBR.NormalTexture->GetPathName();
                Prim.NormalTextureKey = TexPath;

                if (TArray<uint8>* CachedPNG = TextureCache.Find(TexPath))
                {
                    Prim.NormalTexturePNG = *CachedPNG;
                }
                else
                {
                    TArray<uint8> PNGBytes;
                    if (ExtractTexturePNG(PBR.NormalTexture, PNGBytes) && PNGBytes.Num() > 0)
                    {
                        UE_LOG(LogTemp, Log, TEXT("[ARExportSubsystem] Embedded Normal texture %s (%d bytes) for %s"), *TexPath, PNGBytes.Num(), *Prim.MeshName);
                        TextureCache.Add(TexPath, PNGBytes);
                        Prim.NormalTexturePNG = MoveTemp(PNGBytes);
                    }
                }
            }

            // Embed Metallic-Roughness Texture (if present)
            if (PBR.MetallicRoughnessTexture || PBR.RoughnessTexture || PBR.MetallicTexture)
            {
                UTexture2D* MainMRTex = PBR.MetallicRoughnessTexture ? PBR.MetallicRoughnessTexture : (PBR.RoughnessTexture ? PBR.RoughnessTexture : PBR.MetallicTexture);
                const FString TexPath = MainMRTex->GetPathName() + TEXT("_MR");
                Prim.MetallicRoughnessTextureKey = TexPath;

                if (TArray<uint8>* CachedPNG = TextureCache.Find(TexPath))
                {
                    Prim.MetallicRoughnessTexturePNG = *CachedPNG;
                }
                else
                {
                    TArray<uint8> PNGBytes;
                    if (ExtractMetallicRoughnessPNG(PBR.RoughnessTexture, PBR.MetallicTexture, PBR.MetallicRoughnessTexture, PBR.Metallic, PBR.Roughness, PNGBytes) && PNGBytes.Num() > 0)
                    {
                        UE_LOG(LogTemp, Log, TEXT("[ARExportSubsystem] Embedded MetallicRoughness texture %s (%d bytes) for %s"), *TexPath, PNGBytes.Num(), *Prim.MeshName);
                        TextureCache.Add(TexPath, PNGBytes);
                        Prim.MetallicRoughnessTexturePNG = MoveTemp(PNGBytes);
                    }
                }
            }

            TMap<uint32, uint32> OldToNewIndexMap;
            const uint32 EndIndex = FMath::Min(FirstIndex + IndexCount, (uint32)AllIndices.Num());

            for (uint32 idx = FirstIndex; idx < EndIndex; ++idx)
            {
                const uint32 OldVertIdx = AllIndices[idx];
                uint32 NewVertIdx = 0;

                if (const uint32* Found = OldToNewIndexMap.Find(OldVertIdx))
                {
                    NewVertIdx = *Found;
                }
                else
                {
                    NewVertIdx = Prim.Vertices.Num();
                    OldToNewIndexMap.Add(OldVertIdx, NewVertIdx);

                    const FVector3f RawPos = PosBuffer.VertexPosition(OldVertIdx);
                    const FVector LocalPos = ComponentToBoothTransform.TransformPosition(FVector(RawPos));

                    FGLBVertex V;
                    V.Position.X = static_cast<float>(LocalPos.Y * 0.01);
                    V.Position.Y = static_cast<float>(LocalPos.Z * 0.01);
                    V.Position.Z = static_cast<float>(LocalPos.X * 0.01);

                    const FVector3f RawNormal = VertBuffer.VertexTangentZ(OldVertIdx);
                    const FVector LocalNormal = ComponentToBoothTransform.TransformVector(FVector(RawNormal)).GetSafeNormal();
                    V.Normal.X = static_cast<float>(LocalNormal.Y);
                    V.Normal.Y = static_cast<float>(LocalNormal.Z);
                    V.Normal.Z = static_cast<float>(LocalNormal.X);

                    if (VertBuffer.GetNumTexCoords() > 0)
                    {
                        const FVector2f RawUV = VertBuffer.GetVertexUV(OldVertIdx, 0);
                        V.UV = PBR.UVTransform.TransformUV(RawUV);
                    }
                    else
                    {
                        V.UV = FVector2f::ZeroVector;
                    }

                    Prim.Vertices.Add(V);
                }

                Prim.Indices.Add(NewVertIdx);
            }

            if (Prim.Vertices.Num() > 0 && Prim.Indices.Num() > 0)
            {
                Primitives.Add(MoveTemp(Prim));
            }
        }
    }

    if (Primitives.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ARExportSubsystem] No primitives extracted from booth."));
        OnFinished.ExecuteIfBound(false, TEXT(""), TEXT(""), nullptr);
        return;
    }

    // Determine destination path in Saved/AR_Exports/
    const FString ExportsDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AR_Exports"));
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*ExportsDir))
    {
        PlatformFile.CreateDirectoryTree(*ExportsDir);
    }

    const FString ProductID = TargetBooth->ActiveState.ProductID.ToString();
    const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    const FString FileName = FString::Printf(TEXT("export_%s_%s.glb"), *ProductID, *Timestamp);
    const FString FullFilePath = FPaths::Combine(ExportsDir, FileName);

    // Compute WebAR URLs
    const FString LocalIP = GetLocalHostIPAddress();
    const FString ShortWebARURL = FString::Printf(TEXT("http://%s:%d/"), *LocalIP, LocalServerPort);
    const FString DirectModelURL = FString::Printf(TEXT("http://%s:%d/index.html?model=%s"), *LocalIP, LocalServerPort, *FileName);

    // ── 4. Run Heavy GLB Serialization on Background Worker Thread ───────────
    Async(EAsyncExecution::ThreadPool, [Primitives = MoveTemp(Primitives), FullFilePath, ShortWebARURL, DirectModelURL, OnFinished]()
    {
        TArray<uint8> GLBData;
        const bool bSerializeSuccess = FSimpleGLBWriter::SerializeToGLB(Primitives, GLBData);

        bool bWriteSuccess = false;
        if (bSerializeSuccess && GLBData.Num() > 0)
        {
            bWriteSuccess = FFileHelper::SaveArrayToFile(GLBData, *FullFilePath);
            UE_LOG(LogTemp, Log, TEXT("[ARExportSubsystem] Successfully wrote GLB (%d bytes) to %s"), GLBData.Num(), *FullFilePath);
        }

        // ── 5. Dispatch Back to Game Thread for QR Texture & UI Callback ─────
        AsyncTask(ENamedThreads::GameThread, [bWriteSuccess, FullFilePath, ShortWebARURL, DirectModelURL, OnFinished]()
        {
            UTexture2D* QRTexture = nullptr;
            if (bWriteSuccess)
            {
                QRTexture = FQRCodeTextureHelper::GenerateQRCodeTexture(ShortWebARURL, 512, 8);
            }

            OnFinished.ExecuteIfBound(bWriteSuccess, FullFilePath, DirectModelURL, QRTexture);
        });
    });
}
