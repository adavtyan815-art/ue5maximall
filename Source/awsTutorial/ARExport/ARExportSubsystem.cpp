// Copyright MaxiMall Project. All Rights Reserved.

#include "ARExport/ARExportSubsystem.h"
#include "FurnitureConfigurator/ShowroomBooth.h"
#include "ARExport/QRCode/QRCodeTextureHelper.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Rendering/PositionVertexBuffer.h"
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
    TArray<TSharedPtr<FInternetAddr>> LocalAddresses;
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

    if (SocketSubsystem)
    {
        SocketSubsystem->GetLocalHostAddresses(LocalAddresses, bCanBindAll);

        for (const TSharedPtr<FInternetAddr>& Addr : LocalAddresses)
        {
            if (Addr.IsValid())
            {
                FString IPStr = Addr->ToString(false);
                // Prefer LAN private addresses (192.168.x.x, 10.x.x.x, 172.16.x.x)
                if (IPStr.StartsWith(TEXT("192.168.")) || IPStr.StartsWith(TEXT("10.")) || IPStr.StartsWith(TEXT("172.")))
                {
                    return IPStr;
                }
            }
        }

        for (const TSharedPtr<FInternetAddr>& Addr : LocalAddresses)
        {
            if (Addr.IsValid())
            {
                FString IPStr = Addr->ToString(false);
                if (!IPStr.StartsWith(TEXT("127.")) && !IPStr.IsEmpty())
                {
                    return IPStr;
                }
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

    // ── 1. Extract Geometry on Game Thread (Thread-Safe Memory Copy) ─────────
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

    // Compute WebAR URL
    const FString LocalIP = GetLocalHostIPAddress();
    FString URL = WebARViewerPrefix;
    URL = URL.Replace(TEXT("{IP}"), *LocalIP);
    URL = URL.Replace(TEXT("{PORT}"), *FString::FromInt(LocalServerPort));
    URL += FileName;

    // ── 2. Run Heavy Serialization on Background Worker Thread ──────────────
    Async(EAsyncExecution::ThreadPool, [Primitives = MoveTemp(Primitives), FullFilePath, URL, OnFinished]()
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
        AsyncTask(ENamedThreads::GameThread, [bWriteSuccess, FullFilePath, URL, OnFinished]()
        {
            UTexture2D* QRTexture = nullptr;
            if (bWriteSuccess)
            {
                QRTexture = FQRCodeTextureHelper::GenerateQRCodeTexture(URL, 512);
            }

            OnFinished.ExecuteIfBound(bWriteSuccess, FullFilePath, URL, QRTexture);
        });
    });
}

void UARExportSubsystem::ExtractPrimitivesFromBooth(AShowroomBooth* Booth, TArray<FGLBPrimitive>& OutPrimitives)
{
    if (!Booth) return;

    auto GetCustomColor = [Booth](EFurnitureComponentType CompType, const FLinearColor& FallbackColor) -> FLinearColor
    {
        for (const FCustomColorOverride& Override : Booth->ActiveState.CustomColors)
        {
            if (Override.ComponentType == CompType)
            {
                return Override.CustomColor;
            }
        }
        return FallbackColor;
    };

    // Cabinet
    ExtractComponentGeometry(Booth->MainCabinet.Get(), TEXT("Cabinet"), GetCustomColor(EFurnitureComponentType::Cabinet, FLinearColor(0.85f, 0.85f, 0.85f)), 0.05f, 0.6f, OutPrimitives);

    // Closet
    ExtractComponentGeometry(Booth->ClosetMesh.Get(), TEXT("Closet"), GetCustomColor(EFurnitureComponentType::Closet, FLinearColor(0.85f, 0.85f, 0.85f)), 0.05f, 0.6f, OutPrimitives);

    // Doors
    ExtractComponentGeometry(Booth->DoorMeshSlot0.Get(), TEXT("Door_0"), GetCustomColor(EFurnitureComponentType::Doors, FLinearColor(0.9f, 0.9f, 0.9f)), 0.05f, 0.5f, OutPrimitives);
    ExtractComponentGeometry(Booth->DoorMeshSlot1.Get(), TEXT("Door_1"), GetCustomColor(EFurnitureComponentType::Doors, FLinearColor(0.9f, 0.9f, 0.9f)), 0.05f, 0.5f, OutPrimitives);
    ExtractComponentGeometry(Booth->ClosetDoorMeshSlot0.Get(), TEXT("ClosetDoor_0"), GetCustomColor(EFurnitureComponentType::Doors, FLinearColor(0.9f, 0.9f, 0.9f)), 0.05f, 0.5f, OutPrimitives);
    ExtractComponentGeometry(Booth->ClosetDoorMeshSlot1.Get(), TEXT("ClosetDoor_1"), GetCustomColor(EFurnitureComponentType::Doors, FLinearColor(0.9f, 0.9f, 0.9f)), 0.05f, 0.5f, OutPrimitives);

    // Countertop
    ExtractComponentGeometry(Booth->CountertopMesh.Get(), TEXT("Countertop"), GetCustomColor(EFurnitureComponentType::Countertop, FLinearColor(0.95f, 0.95f, 0.95f)), 0.1f, 0.3f, OutPrimitives);

    // Sink
    ExtractComponentGeometry(Booth->SinkMesh.Get(), TEXT("Sink"), GetCustomColor(EFurnitureComponentType::Sink, FLinearColor(1.0f, 1.0f, 1.0f)), 0.05f, 0.2f, OutPrimitives);

    // Faucet (Italian brass or polished chrome)
    ExtractComponentGeometry(Booth->FaucetMesh.Get(), TEXT("Faucet"), GetCustomColor(EFurnitureComponentType::Faucet, FLinearColor(0.85f, 0.7f, 0.3f)), 0.85f, 0.25f, OutPrimitives);

    // Mirror
    ExtractComponentGeometry(Booth->MirrorMesh.Get(), TEXT("Mirror"), GetCustomColor(EFurnitureComponentType::Mirror, FLinearColor(0.9f, 0.95f, 1.0f)), 0.95f, 0.05f, OutPrimitives);
}

void UARExportSubsystem::ExtractComponentGeometry(UStaticMeshComponent* Comp, const FString& MeshName, const FLinearColor& BaseColor, float Metallic, float Roughness, TArray<FGLBPrimitive>& OutPrimitives)
{
    if (!Comp || !Comp->IsVisible() || !Comp->GetStaticMesh())
    {
        return;
    }

    UStaticMesh* Mesh = Comp->GetStaticMesh();
    if (!Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.Num() == 0)
    {
        return;
    }

    const FTransform CompTransform = Comp->GetRelativeTransform();
    const FStaticMeshLODResources& LOD = Mesh->GetRenderData()->LODResources[0];
    const FPositionVertexBuffer& PosBuffer = LOD.VertexBuffers.PositionVertexBuffer;
    const FStaticMeshVertexBuffer& VertBuffer = LOD.VertexBuffers.StaticMeshVertexBuffer;
    const FRawStaticIndexBuffer& IndexBuffer = LOD.IndexBuffer;

    const int32 NumVerts = PosBuffer.GetNumVertices();
    if (NumVerts == 0)
    {
        return;
    }

    FGLBPrimitive Prim;
    Prim.MeshName = MeshName;
    Prim.BaseColor = BaseColor;
    Prim.Metallic = Metallic;
    Prim.Roughness = Roughness;
    Prim.Vertices.Reserve(NumVerts);

    for (int32 i = 0; i < NumVerts; ++i)
    {
        const FVector3f RawPos = PosBuffer.VertexPosition(i);
        const FVector WorldPos = CompTransform.TransformPosition(FVector(RawPos));

        // Coordinate transformation: Unreal Engine (X=Forward, Y=Right, Z=Up in cm)
        // -> glTF 2.0 (X=Right, Y=Up, Z=Back in meters)
        FGLBVertex V;
        V.Position.X = static_cast<float>(WorldPos.Y * 0.01);
        V.Position.Y = static_cast<float>(WorldPos.Z * 0.01);
        V.Position.Z = static_cast<float>(-WorldPos.X * 0.01);

        const FVector3f RawNormal = VertBuffer.VertexTangentZ(i);
        const FVector WorldNormal = CompTransform.TransformVector(FVector(RawNormal)).GetSafeNormal();
        V.Normal.X = static_cast<float>(WorldNormal.Y);
        V.Normal.Y = static_cast<float>(WorldNormal.Z);
        V.Normal.Z = static_cast<float>(-WorldNormal.X);

        if (VertBuffer.GetNumTexCoords() > 0)
        {
            V.UV = VertBuffer.GetVertexUV(i, 0);
        }
        else
        {
            V.UV = FVector2f::ZeroVector;
        }

        Prim.Vertices.Add(V);
    }

    // Extract Indices
    TArray<uint32> RawIndices;
    IndexBuffer.GetCopy(RawIndices);

    // glTF requires winding order adjustment for coordinate conversion (flip index 1 and 2 per triangle)
    Prim.Indices.Reserve(RawIndices.Num());
    for (int32 i = 0; i + 2 < RawIndices.Num(); i += 3)
    {
        Prim.Indices.Add(RawIndices[i]);
        Prim.Indices.Add(RawIndices[i + 2]);
        Prim.Indices.Add(RawIndices[i + 1]);
    }

    OutPrimitives.Add(MoveTemp(Prim));
}
