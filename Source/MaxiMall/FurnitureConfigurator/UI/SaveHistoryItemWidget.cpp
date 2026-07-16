// Copyright MaxiMall Project. All Rights Reserved.

#include "FurnitureConfigurator/UI/SaveHistoryItemWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Image.h"

void USaveHistoryItemWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (LoadButton)
    {
        LoadButton->OnClicked.AddUniqueDynamic(this, &USaveHistoryItemWidget::OnLoadClicked);
    }

    if (DeleteButton)
    {
        DeleteButton->OnClicked.AddUniqueDynamic(this, &USaveHistoryItemWidget::OnDeleteClicked);
    }
}

void USaveHistoryItemWidget::SetupItem(const FString& InSaveId, const FString& InSaveName, const FString& InDate, UTexture2D* InThumbnail)
{
    SaveId = InSaveId;

    if (SaveNameText)
    {
        SaveNameText->SetText(FText::FromString(InSaveName));
    }

    if (SaveDateText)
    {
        SaveDateText->SetText(FText::FromString(InDate));
    }

    if (ThumbnailImage && InThumbnail)
    {
        ThumbnailImage->SetBrushFromTexture(InThumbnail);
    }
}

void USaveHistoryItemWidget::OnLoadClicked()
{
    OnLoadPressed.Broadcast(SaveId);
}

void USaveHistoryItemWidget::OnDeleteClicked()
{
    OnDeletePressed.Broadcast(SaveId);
}
