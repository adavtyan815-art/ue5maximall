// Copyright MaxiMall Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "FurnitureConfigurator/Data/FurnitureTypes.h"
#include "ARExport/GLB/SimpleGLBWriter.h"
#include "ARExportSubsystem.generated.h"

class AShowroomBooth;
class UTexture2D;

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
     * Starts an asynchronous background export of the configured showroom booth to a local .glb file
     * and generates a WebAR URL & QR Code texture.
     */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | AR Export")
    void ExportBoothToAR(AShowroomBooth* TargetBooth, FOnARExportFinished OnFinished);

    /** Resolves the local LAN IPv4 address of the host machine (e.g. 192.168.1.105). */
    UFUNCTION(BlueprintPure, Category = "MaxiMall | AR Export")
    FString GetLocalHostIPAddress() const;

    /** Configurable local HTTP server port (default: 8080). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | AR Export Config")
    int32 LocalServerPort = 8080;

    /** Base cloud or local viewer URL prefix (e.g. "http://{IP}:{PORT}/index.html?model="). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | AR Export Config")
    FString WebARViewerPrefix = TEXT("http://{IP}:{PORT}/index.html?model=");

private:
    void ExtractPrimitivesFromBooth(AShowroomBooth* Booth, TArray<FGLBPrimitive>& OutPrimitives);
    void ExtractComponentGeometry(UStaticMeshComponent* Comp, const FString& MeshName, const FLinearColor& BaseColor, float Metallic, float Roughness, TArray<FGLBPrimitive>& OutPrimitives);
};
