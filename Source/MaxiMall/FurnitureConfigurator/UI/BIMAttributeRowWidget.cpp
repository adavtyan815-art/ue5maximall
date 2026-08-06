// Copyright MaxiMall Project. All Rights Reserved.

#include "FurnitureConfigurator/UI/BIMAttributeRowWidget.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"

void UBIMAttributeRowWidget::SetRowData(const FString& InLabel, const FString& InValue)
{
    UTextBlock* TargetLabel = Txt_Label.Get();
    UTextBlock* TargetValue = Txt_Value.Get();

    if ((!TargetLabel || !TargetValue) && WidgetTree)
    {
        TArray<UTextBlock*> TextBlocks;
        WidgetTree->ForEachWidget([&TextBlocks](UWidget* W)
        {
            if (UTextBlock* TB = Cast<UTextBlock>(W))
            {
                TextBlocks.Add(TB);
            }
        });

        if (TextBlocks.Num() >= 1 && !TargetLabel) TargetLabel = TextBlocks[0];
        if (TextBlocks.Num() >= 2 && !TargetValue) TargetValue = TextBlocks[1];
    }

    if (TargetLabel)
    {
        TargetLabel->SetText(FText::FromString(InLabel));
        TargetLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
        TargetLabel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        FSlateFontInfo Font = TargetLabel->GetFont();
        Font.Size = 13.f;
        TargetLabel->SetFont(Font);
    }
    if (TargetValue)
    {
        TargetValue->SetText(FText::FromString(InValue));
        TargetValue->SetColorAndOpacity(FSlateColor(FLinearColor(0.2f, 0.8f, 1.0f, 1.0f))); // Cyan/Light Blue
        TargetValue->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        FSlateFontInfo Font = TargetValue->GetFont();
        Font.Size = 13.f;
        TargetValue->SetFont(Font);
    }
}
