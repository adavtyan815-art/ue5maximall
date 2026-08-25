// Copyright MaxiMall Project. All Rights Reserved.

#include "ARExport/ARExportSubsystem.h"
#include "FurnitureConfigurator/ShowroomBooth.h"
#include "ARExport/QRCode/QRCodeTextureHelper.h"
#include "Exporters/GLTFExporter.h"
#include "Options/GLTFExportOptions.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
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

    // Configure Official Epic Games GLTF Exporter options
    UGLTFExportOptions* ExportOptions = NewObject<UGLTFExportOptions>();
    ExportOptions->ExportUniformScale = 0.01f; // Convert cm to meters
    ExportOptions->BakeMaterialInputs = EGLTFMaterialBakeMode::UseMeshData; // Automatically bakes full PBR textures (BaseColor, Metallic, Roughness, Normal)
    ExportOptions->DefaultMaterialBakeSize = EGLTFMaterialBakeSizePOT::POT_1024;
    ExportOptions->bExportVertexColors = false;

    TSet<AActor*> SelectedActors = { TargetBooth };
    FGLTFExportMessages OutMessages;
    const bool bSuccess = UGLTFExporter::ExportToGLTF(TargetBooth->GetWorld(), FullFilePath, ExportOptions, SelectedActors, OutMessages);

    for (const FString& Warning : OutMessages.Warnings)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ARExportSubsystem] GLTF Warning: %s"), *Warning);
    }
    for (const FString& Error : OutMessages.Errors)
    {
        UE_LOG(LogTemp, Error, TEXT("[ARExportSubsystem] GLTF Error: %s"), *Error);
    }

    UTexture2D* QRTexture = nullptr;
    if (bSuccess)
    {
        QRTexture = FQRCodeTextureHelper::GenerateQRCodeTexture(ShortWebARURL, 512, 8);
        UE_LOG(LogTemp, Log, TEXT("[ARExportSubsystem] Successfully exported booth to %s via Epic GLTFExporter"), *FullFilePath);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[ARExportSubsystem] Failed to export booth via Epic GLTFExporter"));
    }

    OnFinished.ExecuteIfBound(bSuccess, FullFilePath, DirectModelURL, QRTexture);
}
