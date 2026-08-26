// Copyright MaxiMall Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "FurnitureConfigurator/Data/FurnitureTypes.h"
#include "ARExportSubsystem.generated.h"

class AShowroomBooth;
class UTexture2D;
class UStaticMeshComponent;

DECLARE_DYNAMIC_DELEGATE_FourParams(FOnARExportFinished, bool, bSuccess, const FString&, ExportedFilePath, const FString&, WebARURL, UTexture2D*, QRCodeTexture);

/**
 * UARExportSubsystem
 * Standalone GameInstance subsystem handling in-engine GLB 3D model export,
 * local network IP resolution, and dynamic QR Code generation for instant AR preview.
 */
UCLASS(BlueprintType)
class AWSTUTORIAL_API UARExportSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /**
     * Starts an export of the configured showroom booth to a local .glb file
     * using the official engine GLTFExporter (with full texture & material baking)
     * and generates a WebAR URL & QR Code texture.
     */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | AR Export")
    void ExportBoothToAR(AShowroomBooth* TargetBooth, FOnARExportFinished OnFinished);

    /**
     * Generalized export of any actor's currently DISPLAYED meshes (used by ViewMode
     * to export the preview actor). Temporarily lifts actor-level HiddenInGame and
     * mirrors per-component visibility so the GLB matches exactly what is on screen.
     */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | AR Export")
    void ExportActorToAR(AActor* TargetActor, FOnARExportFinished OnFinished);

    /**
     * Exports only the given mesh components of the actor (selected-object AR export).
     * All other mesh components of the actor are excluded from the GLB for the
     * duration of the export; everything is restored afterwards.
     */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | AR Export")
    void ExportActorComponentsToAR(AActor* TargetActor, const TArray<UStaticMeshComponent*>& OnlyComponents, FOnARExportFinished OnFinished);

private:
    void ExportActorToAR_Internal(AActor* TargetActor, const TArray<UStaticMeshComponent*>* OnlyComponents, const FOnARExportFinished& OnFinished);

public:

    /** Resolves the local LAN IPv4 address of the host machine (e.g. 192.168.1.105). */
    UFUNCTION(BlueprintPure, Category = "MaxiMall | AR Export")
    FString GetLocalHostIPAddress() const;

    /** Configurable local HTTP server port (default: 8080). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | AR Export Config")
    int32 LocalServerPort = 8080;

    /** Base cloud or local viewer URL prefix (e.g. "http://{IP}:{PORT}/index.html?model="). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | AR Export Config")
    FString WebARViewerPrefix = TEXT("http://{IP}:{PORT}/index.html?model=");
};
