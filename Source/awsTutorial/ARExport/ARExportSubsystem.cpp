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
#include "SocketSubsystem.h"
#include "IPAddress.h"

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
    // Full configured booth/scene export (WBP_PreviewWindow flow).
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

    // ── 1. Destination path & WebAR URL (unchanged delivery layer) ───────────
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
    const FString DirectModelURL = FString::Printf(TEXT("http://%s:%d/index.html?model=%s"), *LocalIP, LocalServerPort, *FileName);

    // ── 2a. Make the actor exportable: the official exporter skips meshes on
    //        HiddenInGame actors/components (GLTFNodeConverters.cpp), but it does
    //        NOT check per-component visibility. Temporarily lift actor-level
    //        hiding (e.g. the booth is hidden while ViewMode shows its preview
    //        copy) and mirror "not visible" components to HiddenInGame so the GLB
    //        contains exactly what is currently displayed. All restored below;
    //        the export is synchronous, so nothing renders in between.
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
        // Selected-object export: everything outside the requested component set is
        // treated like an invisible component (excluded, restored after export).
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

    // ── 2c. ViewMode only: neutralize the preview's interactive MeshRoot pose.
    //       RotatePreview bakes the user's yaw/pitch and an orbital offset into the
    //       MeshRoot CHILD component, which actor-level relocation cannot undo —
    //       without this, the GLB inherits whatever tilt the preview showed at
    //       export time. Computed in relative space (while the actor still sits at
    //       its original transform) so it composes correctly with the origin move.
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

    // ── 3. Export at the origin for a clean AR anchor (restored below; the ────
    //      export is synchronous, so no frame renders the moved actor).
    const FTransform OriginalTransform = TargetActor->GetActorTransform();
    TargetActor->SetActorLocationAndRotation(FVector::ZeroVector, FQuat::Identity, false, nullptr, ETeleportType::TeleportPhysics);

    // ── 4. Official Epic glTF export of the target actor ─────────────────────
    UGLTFExportOptions* Options = NewObject<UGLTFExportOptions>();
    Options->ResetToDefault();
    Options->BakeMaterialInputs = EGLTFMaterialBakeMode::UseMeshData;
    Options->TextureImageFormat = EGLTFTextureImageFormat::PNG;
    Options->bExportLights = false;  // preview studio lights must not ride into the GLB
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

    // ── 6. QR code & UI callback (unchanged delivery layer) ──────────────────
    UTexture2D* QRTexture = nullptr;
    if (bSuccess)
    {
        QRTexture = FQRCodeTextureHelper::GenerateQRCodeTexture(DirectModelURL, 512, 8);
    }

    OnFinished.ExecuteIfBound(bSuccess, FullFilePath, DirectModelURL, QRTexture);
}