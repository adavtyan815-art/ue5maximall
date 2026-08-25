// Copyright MaxiMall Project. All Rights Reserved.

#include "ARExport/ARExportSubsystem.h"
#include "FurnitureConfigurator/ShowroomBooth.h"
#include "ARExport/QRCode/QRCodeTextureHelper.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Rendering/PositionVertexBuffer.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Async/Async.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

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

    // ── 1. Extract Geometry & Assembled Transforms on Game Thread ────────────
    TArray<FGLBPrimitive> Primitives;
    ExtractPrimitivesFromBooth(TargetBooth, Primitives);

    if (Primitives.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ARExportSubsystem] No geometry found in target booth."));
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

    // Compute WebAR Root URL (Short 26-char URL produces a bold Version 2 QR code that scans instantly)
    const FString LocalIP = GetLocalHostIPAddress();
    const FString ShortWebARURL = FString::Printf(TEXT("http://%s:%d/"), *LocalIP, LocalServerPort);
    const FString DirectModelURL = FString::Printf(TEXT("http://%s:%d/index.html?model=%s"), *LocalIP, LocalServerPort, *FileName);

    // ── 2. Run Heavy Serialization on Background Worker Thread ──────────────
    Async(EAsyncExecution::ThreadPool, [Primitives = MoveTemp(Primitives), FullFilePath, ShortWebARURL, DirectModelURL, OnFinished]()
    {
        TArray<uint8> GLBData;
        const bool bSerializeSuccess = FSimpleGLBWriter::SerializeToGLB(Primitives, GLBData);

        bool bWriteSuccess = false;
        if (bSerializeSuccess && GLBData.Num() > 0)
        {
            bWriteSuccess = FFileHelper::SaveArrayToFile(GLBData, *FullFilePath);
            UE_LOG(LogTemp, Log, TEXT("[ARExportSubsystem] Saved .glb file (%d bytes) to: %s"), GLBData.Num(), *FullFilePath);
        }

        // ── 3. Dispatch Back to Game Thread for QR Texture & UI Notification ─
        AsyncTask(ENamedThreads::GameThread, [bWriteSuccess, FullFilePath, ShortWebARURL, DirectModelURL, OnFinished]()
        {
            UTexture2D* QRTexture = nullptr;
            if (bWriteSuccess)
            {
                // Generates crisp high-contrast Version 2 QR code with exact integer module pixels
                QRTexture = FQRCodeTextureHelper::GenerateQRCodeTexture(ShortWebARURL, 512, 8);
            }

            OnFinished.ExecuteIfBound(bWriteSuccess, FullFilePath, DirectModelURL, QRTexture);
        });
    });
}

void UARExportSubsystem::ExtractPrimitivesFromBooth(AShowroomBooth* Booth, TArray<FGLBPrimitive>& OutPrimitives)
{
    if (!Booth) return;

    // 1. Cabinet Body (Dark fluted walnut in linear space: sRGB ~45/32/22)
    ExtractComponentGeometry(Booth, Booth->MainCabinet.Get(), EFurnitureComponentType::Cabinet, TEXT("Cabinet"),
        FLinearColor(0.035f, 0.022f, 0.015f, 1.0f), 0.02f, 0.65f, OutPrimitives);

    // 2. Closet Body (White cabinet body)
    ExtractComponentGeometry(Booth, Booth->ClosetMesh.Get(), EFurnitureComponentType::Closet, TEXT("Closet"),
        FLinearColor(0.95f, 0.95f, 0.95f, 1.0f), 0.02f, 0.35f, OutPrimitives);

    // 3. Cabinet Doors (Dark fluted walnut in linear space)
    ExtractComponentGeometry(Booth, Booth->DoorMeshSlot0.Get(), EFurnitureComponentType::Doors, TEXT("CabinetDoor_0"),
        FLinearColor(0.035f, 0.022f, 0.015f, 1.0f), 0.02f, 0.65f, OutPrimitives);
    ExtractComponentGeometry(Booth, Booth->DoorMeshSlot1.Get(), EFurnitureComponentType::Doors, TEXT("CabinetDoor_1"),
        FLinearColor(0.035f, 0.022f, 0.015f, 1.0f), 0.02f, 0.65f, OutPrimitives);

    // 4. Closet Doors (White cabinet door with red accent strip on slot 1)
    ExtractComponentGeometry(Booth, Booth->ClosetDoorMeshSlot0.Get(), EFurnitureComponentType::Doors, TEXT("ClosetDoor_0"),
        FLinearColor(0.95f, 0.95f, 0.95f, 1.0f), 0.02f, 0.35f, OutPrimitives);
    ExtractComponentGeometry(Booth, Booth->ClosetDoorMeshSlot1.Get(), EFurnitureComponentType::Doors, TEXT("ClosetDoor_1"),
        FLinearColor(0.95f, 0.95f, 0.95f, 1.0f), 0.02f, 0.35f, OutPrimitives);

    // 5. Countertop (White Quartz finish)
    ExtractComponentGeometry(Booth, Booth->CountertopMesh.Get(), EFurnitureComponentType::Countertop, TEXT("Countertop"),
        FLinearColor(0.85f, 0.85f, 0.85f, 1.0f), 0.05f, 0.25f, OutPrimitives);

    // 6. Sink (Glossy white ceramic)
    ExtractComponentGeometry(Booth, Booth->SinkMesh.Get(), EFurnitureComponentType::Sink, TEXT("Sink"),
        FLinearColor(0.96f, 0.96f, 0.96f, 1.0f), 0.0f, 0.08f, OutPrimitives);

    // 7. Faucet (Polished Italian Brass / Gold)
    ExtractComponentGeometry(Booth, Booth->FaucetMesh.Get(), EFurnitureComponentType::Faucet, TEXT("Faucet"),
        FLinearColor(0.85f, 0.65f, 0.22f, 1.0f), 0.92f, 0.18f, OutPrimitives);

    // 8. Mirror (Reflective mirror glass + dark frame)
    ExtractComponentGeometry(Booth, Booth->MirrorMesh.Get(), EFurnitureComponentType::Mirror, TEXT("Mirror"),
        FLinearColor(0.95f, 0.97f, 1.0f, 1.0f), 0.98f, 0.02f, OutPrimitives);

    // 9. Extra Attached Static Mesh Components
    TArray<UStaticMeshComponent*> AllMeshComponents;
    Booth->GetComponents<UStaticMeshComponent>(AllMeshComponents);

    TSet<UStaticMeshComponent*> KnownComponents = {
        Booth->MainCabinet.Get(),
        Booth->ClosetMesh.Get(),
        Booth->DoorMeshSlot0.Get(),
        Booth->DoorMeshSlot1.Get(),
        Booth->ClosetDoorMeshSlot0.Get(),
        Booth->ClosetDoorMeshSlot1.Get(),
        Booth->CountertopMesh.Get(),
        Booth->SinkMesh.Get(),
        Booth->FaucetMesh.Get(),
        Booth->MirrorMesh.Get()
    };

    for (UStaticMeshComponent* ExtraComp : AllMeshComponents)
    {
        if (ExtraComp && !KnownComponents.Contains(ExtraComp) && ExtraComp->IsVisible() && ExtraComp->GetStaticMesh())
        {
            ExtractComponentGeometry(Booth, ExtraComp, EFurnitureComponentType::Cabinet, ExtraComp->GetName(),
                FLinearColor(0.035f, 0.022f, 0.015f, 1.0f), 0.02f, 0.65f, OutPrimitives);
        }
    }
}

void UARExportSubsystem::ExtractComponentGeometry(
    AShowroomBooth* Booth,
    UStaticMeshComponent* Comp,
    EFurnitureComponentType CompType,
    const FString& MeshName,
    const FLinearColor& FallbackColor,
    float FallbackMetallic,
    float FallbackRoughness,
    TArray<FGLBPrimitive>& OutPrimitives)
{
    if (!Booth || !Comp || !Comp->IsVisible() || !Comp->GetStaticMesh())
    {
        return;
    }

    UStaticMesh* Mesh = Comp->GetStaticMesh();
    if (!Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.Num() == 0)
    {
        return;
    }

    // ── 1. Calculate Unified Assembly Transform Relative to Booth Root ───────
    const FTransform ComponentToBoothTransform = Comp->GetComponentTransform().GetRelativeTransform(Booth->GetActorTransform());

    const FStaticMeshLODResources& LOD = Mesh->GetRenderData()->LODResources[0];
    const FPositionVertexBuffer& PosBuffer = LOD.VertexBuffers.PositionVertexBuffer;
    const FStaticMeshVertexBuffer& VertBuffer = LOD.VertexBuffers.StaticMeshVertexBuffer;
    const FRawStaticIndexBuffer& IndexBuffer = LOD.IndexBuffer;

    TArray<uint32> AllIndices;
    IndexBuffer.GetCopy(AllIndices);

    const int32 NumSections = LOD.Sections.Num();
    if (NumSections == 0 || AllIndices.Num() == 0)
    {
        return;
    }

    // ── 2. Extract Each Material Section As Its Own GLB Primitive ────────────
    for (int32 SectionIdx = 0; SectionIdx < NumSections; ++SectionIdx)
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

        FLinearColor ResolvedColor = FallbackColor;
        float ResolvedMetallic = FallbackMetallic;
        float ResolvedRoughness = FallbackRoughness;

        // Check custom RAL/NCS override on Booth
        bool bHasCustomColor = false;
        for (const FCustomColorOverride& Override : Booth->CustomColors)
        {
            if (Override.ComponentType == CompType)
            {
                if (MatSlotIndex == 0)
                {
                    ResolvedColor = Override.CustomColor;
                    bHasCustomColor = true;
                }
                break;
            }
        }

        // Read active parameters from material instance or base material graph
        if (!bHasCustomColor && SlotMat)
        {
            UMaterialInstance* InstMat = Cast<UMaterialInstance>(SlotMat);
            if (InstMat)
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
                        // Avoid default neutral white tint (1,1,1) overriding true model colors
                        if (!PColor.Equals(FLinearColor::White, 0.001f))
                        {
                            if (PName.Contains(TEXT("base")) || PName.Contains(TEXT("color")) || PName.Contains(TEXT("tint")) ||
                                PName.Contains(TEXT("albedo")) || PName.Contains(TEXT("diffuse")) || PName.Contains(TEXT("wood")) ||
                                PName.Contains(TEXT("accent")) || PName.Contains(TEXT("strip")) || PName.Contains(TEXT("handle")))
                            {
                                ResolvedColor = PColor;
                                break;
                            }
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
            }
            else if (UMaterial* BaseMat = SlotMat->GetMaterial())
            {
                // Inspect constant vectors in the base material graph
                for (UMaterialExpression* Expr : BaseMat->GetExpressions())
                {
                    if (UMaterialExpressionConstant3Vector* C3 = Cast<UMaterialExpressionConstant3Vector>(Expr))
                    {
                        if (!C3->Constant.Equals(FLinearColor::White, 0.001f) && !C3->Constant.Equals(FLinearColor::Black, 0.001f))
                        {
                            ResolvedColor = C3->Constant;
                            break;
                        }
                    }
                }
            }

            // Asset name / path heuristics for CAD/FBX materials
            const FString MatName = (SlotMat->GetPathName() + TEXT(" ") + SlotMat->GetName()).ToLower();
            if (MatName.Contains(TEXT("red")) || MatName.Contains(TEXT("accent")) || MatName.Contains(TEXT("strip")) || MatName.Contains(TEXT("rosso")) || MatName.Contains(TEXT("2147473129")))
            {
                ResolvedColor = FLinearColor(0.85f, 0.05f, 0.05f, 1.0f); // Bright Red Accent Strip
                ResolvedMetallic = 0.02f;
                ResolvedRoughness = 0.35f;
            }
            else if (MatName.Contains(TEXT("dark")) || MatName.Contains(TEXT("black")) || MatName.Contains(TEXT("frame")) || MatName.Contains(TEXT("nero")) || MatName.Contains(TEXT("graphite")))
            {
                ResolvedColor = FLinearColor(0.015f, 0.015f, 0.018f, 1.0f); // Dark Black Metal Frame
                ResolvedMetallic = 0.85f;
                ResolvedRoughness = 0.25f;
            }
            else if (MatName.Contains(TEXT("gold")) || MatName.Contains(TEXT("brass")) || MatName.Contains(TEXT("oro")) || MatName.Contains(TEXT("ottone")))
            {
                ResolvedColor = FLinearColor(0.85f, 0.65f, 0.22f, 1.0f); // Polished Italian Brass
                ResolvedMetallic = 0.92f;
                ResolvedRoughness = 0.18f;
            }
            else if (MatName.Contains(TEXT("mirror")) || MatName.Contains(TEXT("specchio")) || MatName.Contains(TEXT("glass")) || MatName.Contains(TEXT("vetro")))
            {
                ResolvedColor = FLinearColor(0.95f, 0.97f, 1.0f, 1.0f); // Mirror Glass
                ResolvedMetallic = 0.98f;
                ResolvedRoughness = 0.02f;
            }
        }

        // Component-specific multi-material slot guarantees:
        if (CompType == EFurnitureComponentType::Mirror)
        {
            if (MatSlotIndex == 0)
            {
                ResolvedColor = FLinearColor(0.95f, 0.97f, 1.0f, 1.0f);
                ResolvedMetallic = 0.98f;
                ResolvedRoughness = 0.02f;
            }
            else if (MatSlotIndex >= 1)
            {
                // Slot 1+ is the dark black mirror frame
                ResolvedColor = FLinearColor(0.015f, 0.015f, 0.018f, 1.0f);
                ResolvedMetallic = 0.85f;
                ResolvedRoughness = 0.25f;
            }
        }
        else if (CompType == EFurnitureComponentType::Closet || (CompType == EFurnitureComponentType::Doors && MeshName.Contains(TEXT("Closet"))))
        {
            if (MatSlotIndex == 1)
            {
                // Slot 1 on Tall Closet Door/Body is the Red Accent Handle Strip
                ResolvedColor = FLinearColor(0.85f, 0.05f, 0.05f, 1.0f);
                ResolvedMetallic = 0.02f;
                ResolvedRoughness = 0.35f;
            }
        }

        // ── 3. Extract & Transform Section Geometry ───────────────────────────
        FGLBPrimitive Prim;
        Prim.MeshName = FString::Printf(TEXT("%s_Slot%d"), *MeshName, MatSlotIndex);
        Prim.BaseColor = ResolvedColor;
        Prim.Metallic = ResolvedMetallic;
        Prim.Roughness = ResolvedRoughness;

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
            OutPrimitives.Add(MoveTemp(Prim));
        }
    }
}
