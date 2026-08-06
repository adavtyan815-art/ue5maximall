// Copyright MaxiMall Project. All Rights Reserved.

#include "FurnitureConfigurator/UI/BIMInspectorWidget.h"
#include "FurnitureConfigurator/UI/BIMAttributeRowWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/ButtonSlot.h"
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

void UBIMInspectorWidget::OnCategoryToggleClicked()
{
    for (auto& KVP : CategoryToggleMap)
    {
        UButton* Btn = KVP.Key;
        UVerticalBox* ContentBox = KVP.Value;
        if (Btn && ContentBox && (Btn->IsHovered() || Btn->HasUserFocus(GetOwningPlayer())))
        {
            const bool bIsVisible = ContentBox->GetVisibility() == ESlateVisibility::Visible;
            ContentBox->SetVisibility(bIsVisible ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);

            if (TObjectPtr<UTextBlock>* ArrowPtr = CategoryArrowMap.Find(Btn))
            {
                if (*ArrowPtr)
                {
                    (*ArrowPtr)->SetText(FText::FromString(bIsVisible ? TEXT("▶ ") : TEXT("▼ ")));
                }
            }
            break;
        }
    }
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

        CategoryToggleMap.Empty();
        CategoryArrowMap.Empty();

        DedicatedSizeBox->ClearChildren();

        UScrollBox* ScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
        if (ScrollBox)
        {
            ScrollBox->SetVisibility(ESlateVisibility::Visible);
            ScrollBox->SetScrollBarVisibility(ESlateVisibility::Visible);
            ScrollBox->SetAnimateWheelScrolling(true);

            if (BIMData.CategorizedMetadata.Num() > 0)
            {
                for (const FBIMCategoryGroup& CatGroup : BIMData.CategorizedMetadata)
                {
                    if (CatGroup.Pairs.Num() == 0) continue;

                    UButton* HeaderBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
                    UHorizontalBox* HeaderBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
                    UTextBlock* ArrowText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
                    UTextBlock* TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());

                    if (HeaderBtn && HeaderBox && ArrowText && TitleText)
                    {
                        ArrowText->SetText(FText::FromString(TEXT("▼ ")));
                        FSlateFontInfo ArrowFont = ArrowText->GetFont();
                        ArrowFont.Size = 10.f;
                        ArrowText->SetFont(ArrowFont);
                        ArrowText->SetColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f, 1.f)));

                        TitleText->SetText(FText::FromString(CatGroup.CategoryName));
                        FSlateFontInfo TitleFont = TitleText->GetFont();
                        TitleFont.Size = 11.f;
                        TitleText->SetFont(TitleFont);
                        TitleText->SetColorAndOpacity(FSlateColor(FLinearColor::White));

                        HeaderBox->AddChild(ArrowText);
                        HeaderBox->AddChild(TitleText);

                        if (UButtonSlot* BtnSlot = Cast<UButtonSlot>(HeaderBtn->AddChild(HeaderBox)))
                        {
                            BtnSlot->SetPadding(FMargin(8.f, 4.f, 8.f, 4.f));
                        }

                        UVerticalBox* ContentBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

                        for (const FBIMMetadataPair& Pair : CatGroup.Pairs)
                        {
                            UHorizontalBox* RowBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
                            if (RowBox)
                            {
                                UTextBlock* KeyText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
                                UTextBlock* ValText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());

                                if (KeyText && ValText)
                                {
                                    KeyText->SetText(FText::FromString(Pair.Key));
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

                                    UVerticalBoxSlot* VRowSlot = ContentBox->AddChildToVerticalBox(RowBox);
                                    if (VRowSlot)
                                    {
                                        VRowSlot->SetPadding(FMargin(14.f, 2.f, 16.f, 2.f));
                                    }
                                }
                            }
                        }

                        CategoryToggleMap.Add(HeaderBtn, ContentBox);
                        CategoryArrowMap.Add(HeaderBtn, ArrowText);
                        HeaderBtn->OnClicked.AddUniqueDynamic(this, &UBIMInspectorWidget::OnCategoryToggleClicked);

                        ScrollBox->AddChild(HeaderBtn);
                        ScrollBox->AddChild(ContentBox);
                    }
                }
            }
            else
            {
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
