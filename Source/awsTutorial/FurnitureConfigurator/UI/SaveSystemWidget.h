// Copyright MaxiMall Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "SaveSystemWidget.generated.h"

class UTextBlock;
class UButton;
class UEditableTextBox;
class UScrollBox;
class UImage;
class USaveHistoryItemWidget;

/**
 * USaveSystemWidget
 * Backing class for the main Save System UI.
 */
UCLASS()
class AWSTUTORIAL_API USaveSystemWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;

    // ── WIDGET BINDINGS ──────────────────────────────────────────────────────

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> BackButton;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UEditableTextBox> SaveNameInput;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> SaveButton;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UScrollBox> SaveHistoryScrollBox;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UWidget> LastSaveContainer;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UWidget> SaveHistoryContainer;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UWidget> FirstTimeWelcomeMessage;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UImage> LastSaveThumbnail;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> LastSaveName;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> LastSaveDate;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> LastSaveLoadButton;

    // ── CONFIG ───────────────────────────────────────────────────────────────

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Save System Config")
    TSubclassOf<USaveHistoryItemWidget> SaveHistoryItemClass;

    // ── UI SIZING CONFIG ─────────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maxi Mall | UI Sizing", meta = (DisplayName = "Save History Columns"))
    int32 SaveHistoryColumns = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maxi Mall | UI Sizing", meta = (DisplayName = "Save History Item Width"))
    float SaveHistoryItemWidth = 150.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maxi Mall | UI Sizing", meta = (DisplayName = "Save History Item Height"))
    float SaveHistoryItemHeight = 200.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maxi Mall | UI Sizing", meta = (DisplayName = "Save History Slot Padding"))
    float SaveHistorySlotPadding = 5.f;

    // ── BUTTON DELEGATE CALLBACKS ────────────────────────────────────────────

    UFUNCTION()
    void OnBackClicked();

    UFUNCTION()
    void OnSaveClicked();

    UFUNCTION()
    void OnLastSaveLoadClicked();

    // ── SAVE ITEM ACTION EVENT HANDLERS ──────────────────────────────────────

    UFUNCTION()
    void HandleLoadSaveItem(FString SaveId);

    UFUNCTION()
    void HandleDeleteSaveItem(FString SaveId);

    // ── HTTP CALLBACK HANDLERS ───────────────────────────────────────────────

    void OnGetSavesComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
    void OnPostSaveComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
    void OnDeleteSaveComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

public:
    /** Clear and populate the history list from the backend server database. */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | UI")
    void RefreshSaveHistory();

    /** Set the visual info for the 'Last Save' panel. */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | UI")
    void SetLastSaveDetails(const FString& SaveName, const FString& Date, UTexture2D* Thumbnail = nullptr);

private:
    /** Retrieve the active Cognito username from the Game Instance. */
    FString GetActiveUserName() const;

    /** Dynamic solver to find the manager server's URL from command line launch arguments. */
    FString GetBackendBaseURL() const;

    /** Update widget visibilities based on whether there are saves or not. */
    void UpdateUIVisibility(int32 SaveCount);

    TArray<FString> MockNamesList;
    TArray<FString> MockDatesList;
    TArray<TSharedPtr<class FJsonObject>> LoadedSaves;

    FString PendingSaveId;
    FString PendingSaveName;
    FTimerHandle ScreenshotTimeoutTimerHandle;
    bool bWaitingForScreenshot = false;
    void OnScreenshotTimeout();
    void OnScreenshotCapturedHandler(int32 Width, int32 Height, const TArray<FColor>& Colors);
    void ExecuteSaveGame(const FString& InSaveId, const FString& InSaveName, const FString& Base64Thumbnail);
    UTexture2D* LoadTextureFromBase64(const FString& Base64String);
};
