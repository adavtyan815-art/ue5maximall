// Copyright MaxiMall Project. All Rights Reserved.

#include "FurnitureConfigurator/UI/BIMInspectorWidget.h"
#include "FurnitureConfigurator/UI/BIMAttributeRowWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Blueprint/WidgetTree.h"
#include "FurnitureConfigurator/Preview/MaxiMallPreviewController.h"

#include "Layout/Clipping.h"

void UBIMInspectorWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (Btn_Close)
    {
        Btn_Close->OnClicked.AddUniqueDynamic(this, &UBIMInspectorWidget::OnCloseClicked);
    }
}

void UBIMInspectorWidget::NativePreConstruct()
{
    Super::NativePreConstruct();
}

void UBIMInspectorWidget::OnCloseClicked()
{
    if (AMaxiMallPreviewController* PC = Cast<AMaxiMallPreviewController>(GetOwningPlayer()))
    {
        PC->SelectComponent(nullptr);
    }
    RemoveFromParent();
}

void UBIMInspectorWidget::RefreshBIMData(UPrimitiveComponent* Component)
{
    if (!Component)
    {
        RemoveFromParent();
        return;
    }

    FBIMElementData BIMData;
    if (AMaxiMallPreviewController::GetBIMElementData(Component, BIMData))
    {
        UpdateFromBIMData(BIMData);
    }
}

void UBIMInspectorWidget::UpdateFromBIMData(const FBIMElementData& BIMData)
{
    if (Txt_Category)
    {
        Txt_Category->SetText(FText::FromString(BIMData.Category));
    }
    if (Txt_Title)
    {
        Txt_Title->SetText(FText::FromString(BIMData.FamilyName));
    }
    if (Txt_Subtitle)
    {
        Txt_Subtitle->SetText(FText::FromString(BIMData.TypeName));
    }

    USizeBox* DedicatedSizeBox = nullptr;
    if (ScrollBox_Attributes)
    {
        DedicatedSizeBox = Cast<USizeBox>(ScrollBox_Attributes->GetParent());
    }

    if (!DedicatedSizeBox && WidgetTree)
    {
        WidgetTree->ForEachWidget([&DedicatedSizeBox](UWidget* W)
        {
            if (!DedicatedSizeBox)
            {
                DedicatedSizeBox = Cast<USizeBox>(W);
            }
        });
    }

    if (DedicatedSizeBox && WidgetTree)
    {
        if (InspectorScrollBoxHeight > 0.f)
        {
            DedicatedSizeBox->SetHeightOverride(InspectorScrollBoxHeight);
            DedicatedSizeBox->SetMaxDesiredHeight(InspectorScrollBoxHeight);
            DedicatedSizeBox->SetClipping(EWidgetClipping::ClipToBounds);

            if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(DedicatedSizeBox->Slot))
            {
                VSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
            }
        }

        DedicatedSizeBox->ClearChildren();

        UScrollBox* ScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
        if (ScrollBox)
        {
            ScrollBox->SetVisibility(ESlateVisibility::Visible);
            ScrollBox->SetScrollBarVisibility(ESlateVisibility::Visible);
            ScrollBox->SetAnimateWheelScrolling(true);

            for (const FBIMMetadataPair& Pair : BIMData.RawMetadata)
            {
                UHorizontalBox* RowBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
                if (RowBox)
                {
                    UTextBlock* KeyText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
                    UTextBlock* ValText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());

                    if (KeyText && ValText)
                    {
                        FString CleanKey = Pair.Key;
                        CleanKey.ReplaceInline(TEXT("Element="), TEXT(""));
                        CleanKey.ReplaceInline(TEXT("Element*"), TEXT(""));
                        CleanKey.ReplaceInline(TEXT("Element."), TEXT(""));
                        CleanKey.ReplaceInline(TEXT("Type="), TEXT(""));
                        CleanKey.ReplaceInline(TEXT("Type*"), TEXT(""));
                        CleanKey.ReplaceInline(TEXT("Type."), TEXT(""));

                        KeyText->SetText(FText::FromString(CleanKey));
                        KeyText->SetColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.85f, 0.85f, 1.0f)));
                        KeyText->SetAutoWrapText(true);
                        FSlateFontInfo KeyFont = KeyText->GetFont();
                        KeyFont.Size = 11.f;
                        KeyText->SetFont(KeyFont);

                        ValText->SetText(FText::FromString(Pair.Value));
                        ValText->SetColorAndOpacity(FSlateColor(FLinearColor(0.2f, 0.85f, 1.0f, 1.0f))); // Cyan
                        ValText->SetAutoWrapText(true);
                        FSlateFontInfo ValFont = ValText->GetFont();
                        ValFont.Size = 11.f;
                        ValText->SetFont(ValFont);

                        UHorizontalBoxSlot* KeySlot = Cast<UHorizontalBoxSlot>(RowBox->AddChild(KeyText));
                        if (KeySlot)
                        {
                            KeySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                            KeySlot->SetPadding(FMargin(0.f, 2.f, 10.f, 2.f));
                        }

                        UHorizontalBoxSlot* ValSlot = Cast<UHorizontalBoxSlot>(RowBox->AddChild(ValText));
                        if (ValSlot)
                        {
                            ValSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                            ValSlot->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));
                        }

                        UPanelSlot* PanelSlot = ScrollBox->AddChild(RowBox);
                        if (UScrollBoxSlot* ScrollSlot = Cast<UScrollBoxSlot>(PanelSlot))
                        {
                            ScrollSlot->SetPadding(FMargin(14.f, 3.f, 16.f, 3.f));
                            ScrollSlot->SetHorizontalAlignment(HAlign_Fill);
                        }
                    }
                }
            }

            USizeBox* ScrollLimitBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
            if (ScrollLimitBox)
            {
                if (InspectorScrollBoxHeight > 0.f)
                {
                    ScrollLimitBox->SetMaxDesiredHeight(InspectorScrollBoxHeight);
                }
                ScrollLimitBox->AddChild(ScrollBox);
                DedicatedSizeBox->AddChild(ScrollLimitBox);
            }
            else
            {
                DedicatedSizeBox->AddChild(ScrollBox);
            }
        }
    }
}
