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
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureBase.h"
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
    /** Extracts an uncompressed PNG byte buffer from a UTexture2D for embedding into .glb */
    bool ExtractTexturePNG(UTexture2D* Tex, TArray<uint8>& OutPNG)
    {
        if (!Tex)
        {
            return false;
        }

        FTexturePlatformData* PlatformData = Tex->GetPlatformData();
        if (!PlatformData || PlatformData->Mips.Num() == 0)
        {
            return false;
        }

        FTexture2DMipMap& Mip0 = PlatformData->Mips[0];
        const int32 Width = Mip0.SizeX;
        const int32 Height = Mip0.SizeY;
        if (Width <= 0 || Height <= 0)
        {
            return false;
        }

        const void* RawData = Mip0.BulkData.LockReadOnly();
        if (!RawData)
        {
            return false;
        }

        TArray<FColor> Pixels;
        Pixels.SetNumUninitialized(Width * Height);

        if (PlatformData->PixelFormat == PF_B8G8R8A8)
        {
            FMemory::Memcpy(Pixels.GetData(), RawData, Width * Height * sizeof(FColor));
            Mip0.BulkData.Unlock();
        }
        else
        {
            Mip0.BulkData.Unlock();
#if WITH_EDITOR
            FImage SourceImage;
            if (FImageUtils::GetTexture2DSourceImage(Tex, SourceImage))
            {
                IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
                TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
                if (Wrapper.IsValid() && Wrapper->SetRaw(SourceImage.RawData.GetData(), SourceImage.RawData.Num(), SourceImage.SizeX, SourceImage.SizeY, ERGBFormat::BGRA, 8))
                {
                    OutPNG = Wrapper->GetCompressed(100);
                    return OutPNG.Num() > 0;
                }
            }
#endif
            return false;
        }

        IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
        TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
        if (ImageWrapper.IsValid() && ImageWrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Width, Height, ERGBFormat::BGRA, 8))
        {
            OutPNG = ImageWrapper->GetCompressed(100);
            return OutPNG.Num() > 0;
        }

        return false;
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

    // ── 1. Extract Geometry, PBR Parameters & Textures on Game Thread ────────
    TArray<FGLBPrimitive> Primitives;
    TMap<FString, TArray<uint8>> TextureCache;

    // Scan all mesh components on TargetBooth
    TArray<UStaticMeshComponent*> AllMeshComponents;
    TargetBooth->GetComponents<UStaticMeshComponent>(AllMeshComponents);

    for (UStaticMeshComponent* Comp : AllMeshComponents)
    {
        if (Comp && Comp->IsVisible() && Comp->GetStaticMesh())
        {
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

                FLinearColor ResolvedColor = FLinearColor::White;
                float ResolvedMetallic = 0.0f;
                float ResolvedRoughness = 0.5f;
                UTexture2D* FoundTexture = nullptr;

                if (SlotMat)
                {
                    // 1. Query Material Instance parameters
                    if (UMaterialInstance* InstMat = Cast<UMaterialInstance>(SlotMat))
                    {
                        TArray<FMaterialParameterInfo> ParamInfos;
                        TArray<FGuid> Guids;
                        InstMat->GetAllVectorParameterInfo(ParamInfos, Guids);

                        for (const FMaterialParameterInfo& Info : ParamInfos)
                        {
                            FString PName = Info.Name.ToString().ToLower();
                            FLinearColor PColor;
                            if (InstMat->GetVectorParameterValue(Info, PColor))
                            {
                                if (!PColor.Equals(FLinearColor::White, 0.001f) && !PColor.Equals(FLinearColor::Black, 0.001f))
                                {
                                    ResolvedColor = PColor;
                                    break;
                                }
                            }
                        }

                        float PVal = 0.5f;
                        if (InstMat->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Roughness")), PVal) ||
                            InstMat->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Roughness_Value")), PVal))
                        {
                            ResolvedRoughness = PVal;
                        }

                        if (InstMat->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Metallic")), PVal) ||
                            InstMat->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Metallic_Value")), PVal))
                        {
                            ResolvedMetallic = PVal;
                        }

                        // Query texture parameters
                        TArray<FMaterialParameterInfo> TexInfos;
                        InstMat->GetAllTextureParameterInfo(TexInfos, Guids);
                        for (const FMaterialParameterInfo& TInfo : TexInfos)
                        {
                            UTexture* Tex = nullptr;
                            if (InstMat->GetTextureParameterValue(TInfo, Tex) && Tex)
                            {
                                if (UTexture2D* Tex2D = Cast<UTexture2D>(Tex))
                                {
                                    FoundTexture = Tex2D;
                                    break;
                                }
                            }
                        }
                    }

                    // 2. If no texture in instance, query Base Material graph
                    if (!FoundTexture)
                    {
                        if (UMaterial* BaseMat = SlotMat->GetMaterial())
                        {
                            for (UMaterialExpression* Expr : BaseMat->GetExpressions())
                            {
                                if (UMaterialExpressionTextureSample* TS = Cast<UMaterialExpressionTextureSample>(Expr))
                                {
                                    if (UTexture2D* Tex2D = Cast<UTexture2D>(TS->Texture))
                                    {
                                        FoundTexture = Tex2D;
                                        break;
                                    }
                                }
                                else if (UMaterialExpressionConstant3Vector* C3 = Cast<UMaterialExpressionConstant3Vector>(Expr))
                                {
                                    if (!C3->Constant.Equals(FLinearColor::White, 0.001f) && !C3->Constant.Equals(FLinearColor::Black, 0.001f))
                                    {
                                        ResolvedColor = C3->Constant;
                                    }
                                }
                            }
                        }
                    }

                    // 3. Fallback color heuristics for specific materials if no parameter was found
                    const FString MatName = (SlotMat->GetPathName() + TEXT(" ") + SlotMat->GetName()).ToLower();
                    if (MatName.Contains(TEXT("red")) || MatName.Contains(TEXT("accent")) || MatName.Contains(TEXT("2147473129")))
                    {
                        ResolvedColor = FLinearColor(0.85f, 0.05f, 0.05f, 1.0f);
                        ResolvedRoughness = 0.35f;
                    }
                    else if (MatName.Contains(TEXT("dark")) || MatName.Contains(TEXT("black")) || MatName.Contains(TEXT("frame")) || MatName.Contains(TEXT("nero")))
                    {
                        ResolvedColor = FLinearColor(0.015f, 0.015f, 0.018f, 1.0f);
                        ResolvedMetallic = 0.85f;
                        ResolvedRoughness = 0.25f;
                    }
                    else if (MatName.Contains(TEXT("gold")) || MatName.Contains(TEXT("brass")) || MatName.Contains(TEXT("556")))
                    {
                        ResolvedColor = FLinearColor(0.85f, 0.65f, 0.22f, 1.0f);
                        ResolvedMetallic = 0.92f;
                        ResolvedRoughness = 0.18f;
                    }
                    else if (MatName.Contains(TEXT("mirror")) || MatName.Contains(TEXT("glass")) || MatName.Contains(TEXT("18")))
                    {
                        ResolvedColor = FLinearColor(0.95f, 0.97f, 1.0f, 1.0f);
                        ResolvedMetallic = 0.98f;
                        ResolvedRoughness = 0.02f;
                    }
                    else if (MatName.Contains(TEXT("fluted")) || MatName.Contains(TEXT("wood")) || MatName.Contains(TEXT("2147473128")))
                    {
                        if (!FoundTexture)
                        {
                            ResolvedColor = FLinearColor(0.035f, 0.022f, 0.015f, 1.0f);
                            ResolvedRoughness = 0.65f;
                        }
                    }
                }

                // Build Primitive
                FGLBPrimitive Prim;
                Prim.MeshName = FString::Printf(TEXT("%s_Slot%d"), *Comp->GetName(), MatSlotIndex);
                Prim.BaseColor = ResolvedColor;
                Prim.Metallic = ResolvedMetallic;
                Prim.Roughness = ResolvedRoughness;

                // If texture was found, extract and embed PNG
                if (FoundTexture)
                {
                    const FString TexPath = FoundTexture->GetPathName();
                    Prim.BaseColorTextureKey = TexPath;

                    if (TArray<uint8>* CachedPNG = TextureCache.Find(TexPath))
                    {
                        Prim.BaseColorTexturePNG = *CachedPNG;
                    }
                    else
                    {
                        TArray<uint8> PNGBytes;
                        if (ExtractTexturePNG(FoundTexture, PNGBytes) && PNGBytes.Num() > 0)
                        {
                            TextureCache.Add(TexPath, PNGBytes);
                            Prim.BaseColorTexturePNG = MoveTemp(PNGBytes);
                            Prim.BaseColor = FLinearColor::White;
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
                            V.UV = VertBuffer.GetVertexUV(OldVertIdx, 0);
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

    // ── 2. Run Heavy GLB Serialization on Background Worker Thread ───────────
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

        // ── 3. Dispatch Back to Game Thread for QR Texture & UI Callback ─────
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
