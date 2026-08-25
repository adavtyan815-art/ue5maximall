// Copyright MaxiMall Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ARExportModalWidget.generated.h"

class UImage;
class UProgressBar;
class UTextBlock;
class UButton;
class AShowroomBooth;

/**
 * UARExportModalWidget
 * Modal dialog for Instant AR Export displaying async progress, status messages,
 * the dynamically generated QR Code texture, and URL copy buttons.
 */
UCLASS()
class AWSTUTORIAL_API UARExportModalWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UImage> Img_QRCode;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UProgressBar> ProgressBar_Export;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Status;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_DirectURL;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> Btn_CloseModal;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> Btn_CopyURL;

    UFUNCTION()
    void OnCloseModalClicked();

    UFUNCTION()
    void OnCopyURLClicked();

public:
    /** Begins background export and starts modal loading animation. */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | AR UI")
    void StartExport(AShowroomBooth* TargetBooth);

    /** Called when the subsystem finishes generating the 3D model & QR code. */
    UFUNCTION()
    void HandleExportFinished(bool bSuccess, const FString& ExportedFilePath, const FString& WebARURL, UTexture2D* QRCodeTexture);

private:
    FString CachedWebARURL;
};
