// Copyright MaxiMall Project. All Rights Reserved.

#include "ARExportSubsystem.h"
#include "FurnitureConfigurator/ShowroomBooth.h"
#include "FurnitureConfigurator/Preview/FurniturePreviewActor.h"
#include "QRCode/QRCodeTextureHelper.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInstance.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "Exporters/GLTFExporter.h"
#include "Options/GLTFExportOptions.h"
#include "Utilities/GLTFProxyMaterialUtilities.h"
#include "UserData/GLTFMaterialUserData.h"

// The export itself is Epic's official glTF exporter (GLTFExporter plugin):
// all geometry extraction, coordinate conversions, UV transforms, texture baking,
// and PBR material synthesis are handled by the official engine pipeline.

namespace
{
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

    UMaterialInstanceDynamic* CreateProxyBridgeForRuntimeMID(UMaterialInstanceDynamic* RuntimeMID, UObject* Outer)
    {
        if (!RuntimeMID || !RuntimeMID->Parent)
        {
            return nullptr;
        }

        // 1. Walk up to find the root source material (the proxy is attached to the asset, not to dynamic instances)
        UMaterialInterface* ParentMat = RuntimeMID->Parent;
        UMaterialInterface* Proxy = FGLTFProxyMaterialUtilities::GetProxyMaterial(ParentMat);
        if (!Proxy)
        {
            if (UMaterialInstance* ParentMI = Cast<UMaterialInstance>(ParentMat))
            {
                if (ParentMI->Parent)
                {
                    Proxy = FGLTFProxyMaterialUtilities::GetProxyMaterial(ParentMI->Parent);
                }
            }
        }

        if (!Proxy)
        {
            return nullptr;
        }

        // 2. Create the bridge MID from the proxy's base material
        UMaterialInstanceDynamic* BridgeMID = UMaterialInstanceDynamic::Create(Proxy->GetMaterial(), Outer);
        if (!BridgeMID)
        {
            return nullptr;
        }

        // 3. Copy resolved proxy parameters onto the bridge
        CopyProxyParamsToBridgeMID(Proxy, BridgeMID);

        // 4. Copy dynamic runtime parameter overrides from RuntimeMID to BridgeMID
        TArray<FMaterialParameterInfo> OutVectorInfos;
        TArray<FGuid> OutVectorGuids;
        RuntimeMID->GetAllVectorParameterInfo(OutVectorInfos, OutVectorGuids);
        for (const FMaterialParameterInfo& Info : OutVectorInfos)
        {
            FLinearColor Val;
            if (RuntimeMID->GetVectorParameterValue(Info, Val))
            {
                const FString ParamName = Info.Name.ToString().ToLower();
                if (ParamName.Contains(TEXT("basecolor")) || ParamName.Contains(TEXT("color")) || ParamName.Contains(TEXT("tint")))
                {
                    BridgeMID->SetVectorParameterValue(FName(TEXT("Base Color Factor")), Val);
                }
                else
                {
                    BridgeMID->SetVectorParameterValue(Info.Name, Val);
                }
            }
        }

        TArray<FMaterialParameterInfo> OutScalarInfos;
        TArray<FGuid> OutScalarGuids;
        RuntimeMID->GetAllScalarParameterInfo(OutScalarInfos, OutScalarGuids);
        for (const FMaterialParameterInfo& Info : OutScalarInfos)
        {
            float Val;
            if (RuntimeMID->GetScalarParameterValue(Info, Val))
            {
                const FString ParamName = Info.Name.ToString().ToLower();
                if (ParamName.Contains(TEXT("roughness")))
                {
                    BridgeMID->SetScalarParameterValue(FName(TEXT("Roughness Factor")), Val);
                }
                else if (ParamName.Contains(TEXT("metallic")))
                {
                    BridgeMID->SetScalarParameterValue(FName(TEXT("Metallic Factor")), Val);
                }
                else
                {
                    BridgeMID->SetScalarParameterValue(Info.Name, Val);
                }
            }
        }

        return BridgeMID;
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

FString UARExportSubsystem::GetBackendBaseURL() const
{
    if (!BackendBaseURL.IsEmpty())
    {
        return BackendBaseURL;
    }

    const TCHAR* CommandLine = FCommandLine::Get();

    // 1. Explicit command line override: -BackendURL="https://yourdomain.com"
    FString ExplicitBackendURL;
    if (FParse::Value(CommandLine, TEXT("-BackendURL="), ExplicitBackendURL))
    {
        return ExplicitBackendURL;
    }

    // 2. Retrieve host from standard Pixel Streaming launch argument: -PixelStreamingURL="ws://ip:port"
    FString PixelStreamingURL;
    if (FParse::Value(CommandLine, TEXT("-PixelStreamingURL="), PixelStreamingURL))
    {
        FString HostAndPort = PixelStreamingURL;
        if (HostAndPort.StartsWith(TEXT("ws://")))
        {
            HostAndPort.RightChopInline(5);
        }
        else if (HostAndPort.StartsWith(TEXT("wss://")))
        {
            HostAndPort.RightChopInline(6);
        }

        FString Host;
        FString Port;
        if (HostAndPort.Split(TEXT(":"), &Host, &Port))
        {
            return FString::Printf(TEXT("http://%s:3000"), *Host);
        }
        else
        {
            if (HostAndPort.EndsWith(TEXT("/")))
            {
                HostAndPort.LeftChopInline(1);
            }
            return FString::Printf(TEXT("http://%s:3000"), *HostAndPort);
        }
    }

    // 3. Default fallback to the known AWS web orchestrator
    return TEXT("https://18-185-5-251.nip.io");
}

FString UARExportSubsystem::GetLocalHostIPAddress() const
{
    bool bCanBindAll = false;
    TSharedRef<FInternetAddr> LocalAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLocalHostAddr(*GLog, bCanBindAll);

    if (LocalAddr->IsValid())
    {
        return LocalAddr->ToString(false);
    }

    return TEXT("127.0.0.1");
}

void UARExportSubsystem::ExportBoothToAR(AShowroomBooth* TargetBooth, FOnARExportFinished OnFinished)
{
    ExportActorToAR(TargetBooth, OnFinished);
}

void UARExportSubsystem::ExportActorToAR(AActor* TargetActor, FOnARExportFinished OnFinished)
{
    ExportActorToAR_Internal(TargetActor, nullptr, OnFinished);
}

void UARExportSubsystem::ExportActorComponentsToAR(AActor* TargetActor, const TArray<UStaticMeshComponent*>& OnlyComponents, FOnARExportFinished OnFinished)
{
    ExportActorToAR_Internal(TargetActor, &OnlyComponents, OnFinished);
}

void UARExportSubsystem::ExportActorToAR_Internal(AActor* TargetActor, const TArray<UStaticMeshComponent*>* OnlyComponents, const FOnARExportFinished& OnFinished)
{
    if (!TargetActor || !TargetActor->GetWorld())
    {
        OnFinished.ExecuteIfBound(false, TEXT(""), TEXT(""), nullptr);
        return;
    }

    // ── 1. Destination path & local fallback URL ─────────────────────────────
    const FString ExportsDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AR_Exports"));
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*ExportsDir))
    {
        PlatformFile.CreateDirectoryTree(*ExportsDir);
    }

    const AShowroomBooth* AsBooth = Cast<AShowroomBooth>(TargetActor);
    const FString ExportLabel = AsBooth ? AsBooth->ActiveState.ProductID.ToString() : TargetActor->GetName();
    const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    const FString FileName = FString::Printf(TEXT("export_%s_%s.glb"), *ExportLabel, *Timestamp);
    const FString FullFilePath = FPaths::Combine(ExportsDir, FileName);

    const FString LocalIP = GetLocalHostIPAddress();
    const FString FallbackLocalURL = FString::Printf(TEXT("http://%s:%d/index.html?model=%s"), *LocalIP, LocalServerPort, *FileName);

    // ── 2a. Make the actor exportable: temporarily lift actor-level hiding ───
    const bool bWasActorHidden = TargetActor->IsHidden();
    if (bWasActorHidden)
    {
        TargetActor->SetActorHiddenInGame(false);
    }

    TArray<UStaticMeshComponent*> HiddenMirrorComps;

    // ── 2b. Bridge runtime MIDs to their glTF family proxies ─────────────────
    struct FSlotRestore
    {
        UStaticMeshComponent* Comp;
        int32 SlotIndex;
        UMaterialInterface* Original;
    };
    TArray<FSlotRestore> SlotRestores;

    TArray<UStaticMeshComponent*> MeshComponents;
    TargetActor->GetComponents<UStaticMeshComponent>(MeshComponents);
    for (UStaticMeshComponent* Comp : MeshComponents)
    {
        if (!Comp || !Comp->GetStaticMesh())
        {
            continue;
        }
        const bool bFilteredOut = OnlyComponents && OnlyComponents->Num() > 0 && !OnlyComponents->Contains(Comp);
        if (!Comp->IsVisible() || bFilteredOut)
        {
            if (!Comp->bHiddenInGame)
            {
                Comp->SetHiddenInGame(true);
                HiddenMirrorComps.Add(Comp);
            }
            continue;
        }
        for (int32 SlotIndex = 0; SlotIndex < Comp->GetNumMaterials(); ++SlotIndex)
        {
            if (UMaterialInstanceDynamic* RuntimeMID = Cast<UMaterialInstanceDynamic>(Comp->GetMaterial(SlotIndex)))
            {
                if (UMaterialInstanceDynamic* Bridge = CreateProxyBridgeForRuntimeMID(RuntimeMID, Comp))
                {
                    SlotRestores.Add({ Comp, SlotIndex, RuntimeMID });
                    Comp->SetMaterial(SlotIndex, Bridge);
                    UE_LOG(LogTemp, Log, TEXT("[ARExportSubsystem] %s slot %d: runtime MID bridged to proxy of %s"),
                        *Comp->GetName(), SlotIndex, *RuntimeMID->Parent->GetName());
                }
                else
                {
                    UE_LOG(LogTemp, Log, TEXT("[ARExportSubsystem] %s slot %d: runtime MID has no proxy association - exporter will handle it directly"),
                        *Comp->GetName(), SlotIndex);
                }
            }
        }
    }

    // ── 2c. ViewMode only: neutralize preview MeshRoot interactive pose ──────
    USceneComponent* PreviewMeshRoot = nullptr;
    FTransform SavedMeshRootRelative;
    if (AFurniturePreviewActor* Preview = Cast<AFurniturePreviewActor>(TargetActor))
    {
        FVector ResetLoc;
        FQuat ResetQuat;
        if (IsValid(Preview->MeshRoot) && Preview->GetMeshRootResetState(ResetLoc, ResetQuat))
        {
            PreviewMeshRoot = Preview->MeshRoot;
            SavedMeshRootRelative = PreviewMeshRoot->GetRelativeTransform();
            const FTransform NeutralRelative = FTransform(ResetQuat, ResetLoc).GetRelativeTransform(TargetActor->GetActorTransform());
            PreviewMeshRoot->SetRelativeLocationAndRotation(NeutralRelative.GetLocation(), NeutralRelative.GetRotation());
            UE_LOG(LogTemp, Log, TEXT("[ARExportSubsystem] ViewMode export: preview MeshRoot pose neutralized for export."));
        }
    }

    // ── 3. Export at the origin for a clean AR floor anchor ──────────────────
    const FTransform OriginalTransform = TargetActor->GetActorTransform();
    TargetActor->SetActorLocationAndRotation(FVector::ZeroVector, FQuat::Identity, false, nullptr, ETeleportType::TeleportPhysics);

    // ── 4. Official Epic glTF export of the target actor ─────────────────────
    UGLTFExportOptions* Options = NewObject<UGLTFExportOptions>();
    Options->ResetToDefault();
    Options->BakeMaterialInputs = EGLTFMaterialBakeMode::UseMeshData;
    Options->TextureImageFormat = EGLTFTextureImageFormat::PNG;
    Options->bExportLights = false;
    Options->bExportCameras = false;

    FGLTFExportMessages Messages;
    TSet<AActor*> SelectedActors;
    SelectedActors.Add(TargetActor);

    const double StartTime = FPlatformTime::Seconds();
    const bool bSuccess = UGLTFExporter::ExportToGLTF(TargetActor->GetWorld(), FullFilePath, Options, SelectedActors, Messages);
    const double Elapsed = FPlatformTime::Seconds() - StartTime;

    for (const FString& Msg : Messages.Errors)      { UE_LOG(LogTemp, Error,   TEXT("[ARExportSubsystem] Export error: %s"), *Msg); }
    for (const FString& Msg : Messages.Warnings)    { UE_LOG(LogTemp, Warning, TEXT("[ARExportSubsystem] Export warning: %s"), *Msg); }
    for (const FString& Msg : Messages.Suggestions) { UE_LOG(LogTemp, Log,     TEXT("[ARExportSubsystem] Export suggestion: %s"), *Msg); }

    // ── 5. Restore transform, hidden states, and original slot materials ─────
    TargetActor->SetActorTransform(OriginalTransform, false, nullptr, ETeleportType::TeleportPhysics);
    if (PreviewMeshRoot)
    {
        PreviewMeshRoot->SetRelativeLocationAndRotation(SavedMeshRootRelative.GetLocation(), SavedMeshRootRelative.GetRotation());
    }
    if (bWasActorHidden)
    {
        TargetActor->SetActorHiddenInGame(true);
    }
    for (UStaticMeshComponent* Comp : HiddenMirrorComps)
    {
        Comp->SetHiddenInGame(false);
    }
    for (const FSlotRestore& Restore : SlotRestores)
    {
        Restore.Comp->SetMaterial(Restore.SlotIndex, Restore.Original);
    }

    UE_LOG(LogTemp, Log, TEXT("[ARExportSubsystem] Official glTF export %s (%.1fs) -> %s"),
        bSuccess ? TEXT("succeeded") : TEXT("FAILED"), Elapsed, *FullFilePath);

    if (!bSuccess)
    {
        OnFinished.ExecuteIfBound(false, FullFilePath, FallbackLocalURL, nullptr);
        return;
    }

    // ── 6. Upload GLB to web orchestrator backend and generate QR code ───────
    UploadGLBToBackend(FullFilePath, FileName, FallbackLocalURL, OnFinished);
}

void UARExportSubsystem::UploadGLBToBackend(const FString& FullFilePath, const FString& FileName, const FString& FallbackLocalURL, const FOnARExportFinished& OnFinished)
{
    TArray<uint8> FileBytes;
    if (!FFileHelper::LoadFileToArray(FileBytes, *FullFilePath) || FileBytes.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[ARExportSubsystem] Failed to read exported GLB file from disk: %s"), *FullFilePath);
        UTexture2D* QRTexture = FQRCodeTextureHelper::GenerateQRCodeTexture(FallbackLocalURL, 512, 8);
        OnFinished.ExecuteIfBound(false, FullFilePath, FallbackLocalURL, QRTexture);
        return;
    }

    const FString BaseURL = GetBackendBaseURL();
    const FString UploadURL = FString::Printf(TEXT("%s/api/ar/upload"), *BaseURL);
    UE_LOG(LogTemp, Log, TEXT("[ARExportSubsystem] Uploading GLB '%s' (%d bytes) to %s ..."), *FileName, FileBytes.Num(), *UploadURL);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(UploadURL);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("model/gltf-binary"));
    Request->SetHeader(TEXT("X-File-Name"), FileName);
    Request->SetContent(FileBytes);
    Request->SetTimeout(15.0f);

    Request->OnProcessRequestComplete().BindLambda(
        [this, FullFilePath, FallbackLocalURL, OnFinished](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bConnectedSuccessfully)
        {
            FString FinalURL = FallbackLocalURL;
            bool bUploadSucceeded = false;

            if (bConnectedSuccessfully && Res.IsValid() && EHttpResponseCodes::IsOk(Res->GetResponseCode()))
            {
                const FString ResponseStr = Res->GetContentAsString();
                TSharedPtr<FJsonObject> JsonObj;
                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseStr);
                if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
                {
                    if (JsonObj->HasTypedField<EJson::String>(TEXT("url")))
                    {
                        FinalURL = JsonObj->GetStringField(TEXT("url"));
                        bUploadSucceeded = true;
                        UE_LOG(LogTemp, Log, TEXT("[ARExportSubsystem] Upload succeeded! Public WebAR URL: %s"), *FinalURL);
                    }
                }
            }

            if (!bUploadSucceeded)
            {
                const int32 ResponseCode = Res.IsValid() ? Res->GetResponseCode() : 0;
                UE_LOG(LogTemp, Warning, TEXT("[ARExportSubsystem] Backend upload failed (code: %d, connected: %d). Using fallback local URL: %s"),
                    ResponseCode, bConnectedSuccessfully ? 1 : 0, *FallbackLocalURL);
            }

            UTexture2D* QRTexture = FQRCodeTextureHelper::GenerateQRCodeTexture(FinalURL, 512, 8);
            OnFinished.ExecuteIfBound(true, FullFilePath, FinalURL, QRTexture);
        });

    Request->ProcessRequest();
}