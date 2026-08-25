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
        float Roughness = 0.5f;
        bool bTwoSided = false;
        UTexture2D* BaseColorTexture = nullptr;
        UTexture2D* NormalTexture = nullptr;
        FUVTransform UVTransform;
    };

    /** Recursively searches a material and its nested Material Functions for all referenced UTexture2D assets */
    void FindTexturesInMaterial(UMaterialInterface* Mat, TArray<UTexture2D*>& OutBaseColorTextures, TArray<UTexture2D*>& OutNormalTextures)
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

#if WITH_EDITORONLY_DATA
        // 2. Expressions in Base Material and all nested Material Functions
        // (expression graphs are editor-only data, stripped from cooked builds)
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
#else
        // Cooked builds: no expression graphs. Classify the textures actually in use -
        // color maps are sRGB, normal maps are identified by their compression settings.
        TArray<UTexture*> UsedTextures;
        Mat->GetUsedTextures(UsedTextures, EMaterialQualityLevel::Num, true, ERHIFeatureLevel::SM5, true);
        for (UTexture* UsedTex : UsedTextures)
        {
            if (UTexture2D* UsedTex2D = Cast<UTexture2D>(UsedTex))
            {
                if (UsedTex2D->CompressionSettings == TC_Normalmap)
                {
                    OutNormalTextures.AddUnique(UsedTex2D);
                }
                else if (UsedTex2D->SRGB)
                {
                    OutBaseColorTextures.AddUnique(UsedTex2D);
                }
            }
        }
#endif
    }

    /**
     * Extracts an uncompressed PNG byte buffer from a UTexture2D for embedding into .glb.
     * bFlipGreenChannel converts UE's DirectX-style normal maps (green down) to the
     * OpenGL-style convention (green up) that glTF mandates.
     */
    bool ExtractTexturePNG(UTexture2D* Tex, TArray<uint8>& OutPNG, bool bFlipGreenChannel = false)
    {
        if (!Tex)
        {
            return false;
        }

        // 1. Try ImageUtils Source Image (fastest, lossless, full resolution)
        FImage SourceImage;
        if (FImageUtils::GetTexture2DSourceImage(Tex, SourceImage))
        {
            if (bFlipGreenChannel && SourceImage.Format == ERawImageFormat::BGRA8)
            {
                for (int64 Idx = 1; Idx < SourceImage.RawData.Num(); Idx += 4)
                {
                    SourceImage.RawData[Idx] = 255 - SourceImage.RawData[Idx];
                }
            }
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

    /**
     * Builds a generic 2D coverage mask of texture pixels covered by non-degenerate UV triangles
     * of the mesh section (matching UE IMaterialBakingModule / FGLTFMaterialUtilities).
     */
    void BuildMeshUVCoverageMask(
        const FStaticMeshVertexBuffer& VertBuffer,
        const TArray<uint32>& AllIndices,
        uint32 FirstIndex,
        uint32 IndexCount,
        const FUVTransform& UVTransform,
        int32 TextureWidth,
        int32 TextureHeight,
        TArray<uint8>& OutCoverageMask)
    {
        OutCoverageMask.SetNumZeroed(TextureWidth * TextureHeight);
        if (TextureWidth <= 0 || TextureHeight <= 0 || VertBuffer.GetNumTexCoords() == 0 || IndexCount < 3)
        {
            return;
        }

        const uint32 EndIndex = FMath::Min(FirstIndex + IndexCount, (uint32)AllIndices.Num());
        const float Wf = static_cast<float>(TextureWidth);
        const float Hf = static_cast<float>(TextureHeight);

        auto EdgeFunc = [](float X0, float Y0, float X1, float Y1, float X2, float Y2) -> float
        {
            return (X2 - X0) * (Y1 - Y0) - (Y2 - Y0) * (X1 - X0);
        };

        for (uint32 i = FirstIndex; i + 2 < EndIndex; i += 3)
        {
            const uint32 I0 = AllIndices[i];
            const uint32 I1 = AllIndices[i + 1];
            const uint32 I2 = AllIndices[i + 2];

            const FVector2f UV0 = UVTransform.TransformUV(VertBuffer.GetVertexUV(I0, 0));
            const FVector2f UV1 = UVTransform.TransformUV(VertBuffer.GetVertexUV(I1, 0));
            const FVector2f UV2 = UVTransform.TransformUV(VertBuffer.GetVertexUV(I2, 0));

            const float U0 = UV0.X * Wf, V0 = UV0.Y * Hf;
            const float U1 = UV1.X * Wf, V1 = UV1.Y * Hf;
            const float U2 = UV2.X * Wf, V2 = UV2.Y * Hf;

            // Degenerate triangle check (cross-product in pixel space):
            const float DoubleArea = FMath::Abs((U1 - U0) * (V2 - V0) - (U2 - U0) * (V1 - V0));
            if (DoubleArea < 0.1f) // Skip collapsed 1D / zero-area UV projections
            {
                continue;
            }

            const int32 MinX = FMath::Clamp(FMath::FloorToInt32(FMath::Min3(U0, U1, U2)), 0, TextureWidth - 1);
            const int32 MaxX = FMath::Clamp(FMath::CeilToInt32(FMath::Max3(U0, U1, U2)), 0, TextureWidth - 1);
            const int32 MinY = FMath::Clamp(FMath::FloorToInt32(FMath::Min3(V0, V1, V2)), 0, TextureHeight - 1);
            const int32 MaxY = FMath::Clamp(FMath::CeilToInt32(FMath::Max3(V0, V1, V2)), 0, TextureHeight - 1);

            for (int32 Y = MinY; Y <= MaxY; ++Y)
            {
                const float Py = static_cast<float>(Y) + 0.5f;
                const int32 RowOffset = Y * TextureWidth;

                for (int32 X = MinX; X <= MaxX; ++X)
                {
                    const float Px = static_cast<float>(X) + 0.5f;

                    const float W0 = EdgeFunc(U1, V1, U2, V2, Px, Py);
                    const float W1 = EdgeFunc(U2, V2, U0, V0, Px, Py);
                    const float W2 = EdgeFunc(U0, V0, U1, V1, Px, Py);

                    if ((W0 >= 0.0f && W1 >= 0.0f && W2 >= 0.0f) || (W0 <= 0.0f && W1 <= 0.0f && W2 <= 0.0f))
                    {
                        OutCoverageMask[RowOffset + X] = 255;
                    }
                }
            }
        }
    }

    /**
     * GPU/Analytical Material BaseColor Baker:
     * Evaluates the material graph by multiplying the base color texture's sRGB texels
     * with the active runtime LinearColor parameter in Linear float space, then converting
     * back to sRGB. Pixels outside valid mesh UV triangles remain clean neutral zero (RGBA 0,0,0,0).
     */
    bool BakeTextureWithTintPNG(
        UTexture2D* Tex,
        const FLinearColor& TintColor,
        TArray<uint8>& OutPNG,
        const TArray<uint8>* OptionalUVCoverageMask = nullptr)
    {
        if (!Tex)
        {
            return false;
        }

        // If tint is pure white AND no UV masking requested, standard extract is faster
        if (TintColor.Equals(FLinearColor::White, 1e-3f) && (!OptionalUVCoverageMask || OptionalUVCoverageMask->Num() == 0))
        {
            return ExtractTexturePNG(Tex, OutPNG, false);
        }

        FImage SourceImage;
        if (FImageUtils::GetTexture2DSourceImage(Tex, SourceImage))
        {
            if (SourceImage.Format == ERawImageFormat::BGRA8 && SourceImage.RawData.Num() >= 4)
            {
                const int64 NumPixels = SourceImage.RawData.Num() / 4;
                const bool bUseMask = (OptionalUVCoverageMask != nullptr && OptionalUVCoverageMask->Num() == NumPixels);

                for (int64 i = 0; i < NumPixels; ++i)
                {
                    const int64 ByteIdx = i * 4;

                    if (bUseMask && (*OptionalUVCoverageMask)[i] == 0)
                    {
                        // Outside valid mesh UV triangles -> neutral transparent background (matching UE IMaterialBakingModule)
                        SourceImage.RawData[ByteIdx]     = 0;
                        SourceImage.RawData[ByteIdx + 1] = 0;
                        SourceImage.RawData[ByteIdx + 2] = 0;
                        SourceImage.RawData[ByteIdx + 3] = 0;
                        continue;
                    }

                    const uint8 B8 = SourceImage.RawData[ByteIdx];
                    const uint8 G8 = SourceImage.RawData[ByteIdx + 1];
                    const uint8 R8 = SourceImage.RawData[ByteIdx + 2];

                    // Material Graph: Desaturation (Luminance) -> Power(2.0) -> Multiply Tint
                    const FLinearColor RawLin = FLinearColor::FromSRGBColor(FColor(R8, G8, B8, 255));
                    const float Lum = 0.299f * RawLin.R + 0.587f * RawLin.G + 0.114f * RawLin.B;
                    const float LumSq = Lum * Lum;

                    FLinearColor Lin;
                    Lin.R = LumSq * TintColor.R;
                    Lin.G = LumSq * TintColor.G;
                    Lin.B = LumSq * TintColor.B;

                    // Convert Linear float back to sRGB byte
                    const FColor BakedColor = Lin.ToFColor(true);
                    SourceImage.RawData[ByteIdx]     = BakedColor.B;
                    SourceImage.RawData[ByteIdx + 1] = BakedColor.G;
                    SourceImage.RawData[ByteIdx + 2] = BakedColor.R;
                    SourceImage.RawData[ByteIdx + 3] = 0;
                }

                TArray64<uint8> CompressedBytes;
                if (FImageUtils::CompressImage(CompressedBytes, TEXT("png"), SourceImage))
                {
                    OutPNG.SetNumUninitialized(CompressedBytes.Num());
                    FMemory::Memcpy(OutPNG.GetData(), CompressedBytes.GetData(), CompressedBytes.Num());
                    return OutPNG.Num() > 0;
                }
            }
        }

        // Fallback: extract raw texture if source image format cannot be modified
        return ExtractTexturePNG(Tex, OutPNG, false);
    }

    /**
     * Material Parameter Resolver:
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

        State.bTwoSided = SlotMat->IsTwoSided();
        bool bBaseColorFound = false;

        // V-Ray/Datasmith channels: metals carry their visible color in Reflection with a
        // near-black diffuse, and roughness is expressed as Reflection_Glossiness.
        bool bHasReflection = false;
        FLinearColor ReflectionColor = FLinearColor::Black;
        bool bMetallicExplicit = false, bRoughnessExplicit = false;
        bool bHasReflGloss = false, bHasGenericGloss = false;
        float ReflGloss = 0.0f, GenericGloss = 0.0f;

        // ── 1. Parse Vector Parameters on UMaterialInterface ─────────────────
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
            // V-Ray reflection color (the visible surface color of Datasmith metals)
            else if (LowerName.Contains(TEXT("reflection")) && !LowerName.Contains(TEXT("gloss")))
            {
                if (!bHasReflection)
                {
                    bHasReflection = true;
                    ReflectionColor = Val;
                }
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
                bMetallicExplicit = true;
            }
            else if (LowerName.Contains(TEXT("roughness")) || LowerName.Contains(TEXT("rough")))
            {
                State.Roughness = FMath::Clamp(SVal, 0.0f, 1.0f);
                bRoughnessExplicit = true;
            }
            // V-Ray glossiness (prefer the reflection-specific one; refraction gloss is not surface roughness)
            else if (LowerName.Contains(TEXT("gloss")))
            {
                if (LowerName.Contains(TEXT("reflection")) && !bHasReflGloss)
                {
                    bHasReflGloss = true;
                    ReflGloss = SVal;
                }
                else if (!LowerName.Contains(TEXT("refract")) && !bHasGenericGloss)
                {
                    bHasGenericGloss = true;
                    GenericGloss = SVal;
                }
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

        // ── 3. Extract Textures ──────────────────────────────────────────────
        TArray<UTexture2D*> BaseTextures;
        TArray<UTexture2D*> NormalTextures;
        FindTexturesInMaterial(SlotMat, BaseTextures, NormalTextures);

        if (BaseTextures.Num() > 0)
        {
            State.BaseColorTexture = BaseTextures[0];
        }
        if (NormalTextures.Num() > 0)
        {
            State.NormalTexture = NormalTextures[0];
        }

        // ── 4. V-Ray -> glTF PBR Conversion ──────────────────────────────────
        // Roughness: 1 - glossiness when no explicit roughness parameter exists.
        const bool bHasGloss = bHasReflGloss || bHasGenericGloss;
        const float GlossValue = bHasReflGloss ? ReflGloss : GenericGloss;
        if (!bRoughnessExplicit && bHasGloss)
        {
            State.Roughness = FMath::Clamp(1.0f - GlossValue, 0.03f, 1.0f);
        }

        // Metals: a COLORED reflection over a dark diffuse (gold/brass), or a true mirror
        // (strong neutral reflection, dark diffuse, high glossiness). V-Ray dielectrics also
        // carry bright NEUTRAL reflection as their specular term - that must stay dielectric.
        // Texture-driven slots keep their texture; this only applies to untextured metals.
        if (!State.BaseColorTexture && !bMetallicExplicit && bHasReflection)
        {
            const float ReflMax = FMath::Max3(ReflectionColor.R, ReflectionColor.G, ReflectionColor.B);
            const float ReflMin = FMath::Min3(ReflectionColor.R, ReflectionColor.G, ReflectionColor.B);
            const bool bColoredReflection = ReflMax > 0.35f && (ReflMax - ReflMin) > 0.08f;
            const bool bDarkBase = !bBaseColorFound || State.BaseColor.GetLuminance() < 0.08f;
            const float MirrorGloss = bHasGloss ? GlossValue : 0.9f;
            const bool bMirrorLike = ReflMax >= 0.5f && MirrorGloss >= 0.8f && bDarkBase;

            if ((bColoredReflection && bDarkBase) || bMirrorLike)
            {
                State.BaseColor = ReflectionColor;
                State.Metallic = 1.0f;
                if (!bRoughnessExplicit)
                {
                    State.Roughness = FMath::Clamp(1.0f - MirrorGloss, 0.03f, 1.0f);
                }
            }
        }

        State.BaseColor.A = 1.0f;   // opaque; a stray parameter alpha would ghost the mesh

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
            // The UE->glTF axis conversion below flips triangle winding, so single-sided
            // materials would render inside-out (culled faces) in glTF viewers. Keep all
            // primitives double-sided until winding is reversed during export.
            Prim.bDoubleSided = true;

            UE_LOG(LogTemp, Log, TEXT("[ARExportSubsystem] %s: mat=%s rgb=(%.3f, %.3f, %.3f) metal=%.2f rough=%.2f tex=%s normal=%s"),
                *Prim.MeshName, SlotMat ? *SlotMat->GetName() : TEXT("<none>"),
                Prim.BaseColor.R, Prim.BaseColor.G, Prim.BaseColor.B, Prim.Metallic, Prim.Roughness,
                PBR.BaseColorTexture ? *PBR.BaseColorTexture->GetName() : TEXT("<none>"),
                PBR.NormalTexture ? *PBR.NormalTexture->GetName() : TEXT("<none>"));

            // Embed Baked BaseColor Texture (if present)
            if (PBR.BaseColorTexture)
            {
                // Build generic mesh-aware UV coverage mask for this section
                TArray<uint8> UVCoverageMask;
                const int32 TexW = PBR.BaseColorTexture->GetSizeX();
                const int32 TexH = PBR.BaseColorTexture->GetSizeY();
                BuildMeshUVCoverageMask(VertBuffer, AllIndices, FirstIndex, IndexCount, PBR.UVTransform, TexW, TexH, UVCoverageMask);

                const FString TexKey = FString::Printf(TEXT("%s_Baked_%s_Sec%d"), *PBR.BaseColorTexture->GetPathName(), *PBR.BaseColor.ToString(), SectionIdx);
                Prim.BaseColorTextureKey = TexKey;

                if (TArray<uint8>* CachedPNG = TextureCache.Find(TexKey))
                {
                    Prim.BaseColorTexturePNG = *CachedPNG;
                    Prim.BaseColor = FLinearColor::White;
                }
                else
                {
                    TArray<uint8> PNGBytes;
                    if (BakeTextureWithTintPNG(PBR.BaseColorTexture, PBR.BaseColor, PNGBytes, &UVCoverageMask) && PNGBytes.Num() > 0)
                    {
                        UE_LOG(LogTemp, Log, TEXT("[ARExportSubsystem] Baked BaseColor texture %s (%d bytes) for %s"), *TexKey, PNGBytes.Num(), *Prim.MeshName);
                        TextureCache.Add(TexKey, PNGBytes);
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
                    if (ExtractTexturePNG(PBR.NormalTexture, PNGBytes, /*bFlipGreenChannel=*/true) && PNGBytes.Num() > 0)
                    {
                        UE_LOG(LogTemp, Log, TEXT("[ARExportSubsystem] Embedded Normal texture %s (%d bytes) for %s"), *TexPath, PNGBytes.Num(), *Prim.MeshName);
                        TextureCache.Add(TexPath, PNGBytes);
                        Prim.NormalTexturePNG = MoveTemp(PNGBytes);
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

    // Compute the WebAR viewer URL that loads this exact export
    const FString LocalIP = GetLocalHostIPAddress();
    const FString DirectModelURL = FString::Printf(TEXT("http://%s:%d/index.html?model=%s"), *LocalIP, LocalServerPort, *FileName);

    // ── 4. Run Heavy GLB Serialization on Background Worker Thread ───────────
    Async(EAsyncExecution::ThreadPool, [Primitives = MoveTemp(Primitives), FullFilePath, DirectModelURL, OnFinished]()
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
        AsyncTask(ENamedThreads::GameThread, [bWriteSuccess, FullFilePath, DirectModelURL, OnFinished]()
        {
            UTexture2D* QRTexture = nullptr;
            if (bWriteSuccess)
            {
                // Encode the direct model URL so a scan opens this exact export
                QRTexture = FQRCodeTextureHelper::GenerateQRCodeTexture(DirectModelURL, 512, 8);
            }

            OnFinished.ExecuteIfBound(bWriteSuccess, FullFilePath, DirectModelURL, QRTexture);
        });
    });
}
