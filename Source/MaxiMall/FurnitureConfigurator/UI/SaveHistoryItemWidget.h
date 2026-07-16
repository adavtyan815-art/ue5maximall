// Copyright MaxiMall Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SaveHistoryItemWidget.generated.h"

class UTextBlock;
class UButton;
class UImage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSaveItemAction, FString, SaveId);

/**
 * USaveHistoryItemWidget
 * Represents an individual save project card in the history list.
 */
UCLASS()
class MAXIMALL_API USaveHistoryItemWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;

    // ── WIDGET BINDINGS ──────────────────────────────────────────────────────

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UImage> ThumbnailImage;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> SaveNameText;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> SaveDateText;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> LoadButton;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> DeleteButton;

    // ── BUTTON DELEGATE CALLBACKS ────────────────────────────────────────────

    UFUNCTION()
    void OnLoadClicked();

    UFUNCTION()
    void OnDeleteClicked();

public:
    /** Set the visual data for this save history card. */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | UI")
    void SetupItem(const FString& InSaveId, const FString& InSaveName, const FString& InDate, UTexture2D* InThumbnail = nullptr);

    // Callbacks to notify parent widget
    FOnSaveItemAction OnLoadPressed;
    FOnSaveItemAction OnDeletePressed;

private:
    FString SaveId;
};
