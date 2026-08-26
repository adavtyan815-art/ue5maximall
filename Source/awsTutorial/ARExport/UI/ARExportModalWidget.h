// Copyright MaxiMall Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ARExportModalWidget.generated.h"

class UImage;
class UProgressBar;
class UTextBlock;
class UButton;
class UEditableTextBox;
class UStaticMeshComponent;
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

    /**
     * Single-line, read-only URL field. Must be an EditableTextBox in the WBP (same
     * name) so the user can click, select, and copy the URL manually. Unfocused it
     * shows an ellipsized URL that fits the fixed width; on focus it swaps to the
     * full URL for selection/copy.
     */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UEditableTextBox> Txt_DirectURL;

    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

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

    /** Generalized entry: export any actor's currently displayed meshes (ViewMode). */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | AR UI")
    void StartExportForActor(AActor* TargetActor);

    /** Export only the given mesh components of the actor (selected-object AR export). */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | AR UI")
    void StartExportForComponents(AActor* TargetActor, const TArray<UStaticMeshComponent*>& OnlyComponents);

    /** Called when the subsystem finishes generating the 3D model & QR code. */
    UFUNCTION()
    void HandleExportFinished(bool bSuccess, const FString& ExportedFilePath, const FString& WebARURL, UTexture2D* QRCodeTexture);

private:
    /** Resets the modal UI into the "exporting" state and schedules the export for
     *  the next tick, so the modal (and ProgressBar_Export) actually paint before
     *  the synchronous export blocks the game thread. */
    void BeginDeferredExport();

    /** Runs the deferred export for the pending target/components. */
    void RunPendingExport();

    /** Sets Txt_DirectURL's text: full URL when focused, ellipsized-to-width otherwise. */
    void RefreshDirectURLField(bool bFocused);

    FString CachedWebARURL;
    bool bURLFieldFocused = false;
    float LastURLFieldWidth = 0.0f;

    TWeakObjectPtr<AActor> PendingTarget;

    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> PendingComponents;
};
