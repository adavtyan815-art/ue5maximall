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
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Framework/Application/SlateApplication.h"
#include "FurnitureConfigurator/Preview/MaxiMallPreviewController.h"

#include "Layout/Clipping.h"

bool UBIMInspectorWidget::IsPointerOverDragArea(const FVector2D& ScreenPos) const
{
    if (!Border_MainPanel)
    {
        return false;
    }

    if (Btn_Close && Btn_Close->GetCachedGeometry().IsUnderLocation(ScreenPos))
    {
        return false;
    }

    const UWidget* DragWidget = Header_Horizontal_Box ? Header_Horizontal_Box.Get() :
        (Header_HorizontalBox ? Header_HorizontalBox.Get() :
        (DragHeaderBar ? DragHeaderBar.Get() :
        (Border_Header ? Border_Header.Get() : nullptr)));

    if (DragWidget && DragWidget->GetCachedGeometry().IsUnderLocation(ScreenPos))
    {
        return true;
    }

    FGeometry MainGeo = Border_MainPanel->GetCachedGeometry();
    if (MainGeo.IsUnderLocation(ScreenPos))
    {
        FVector2D LocalPos = MainGeo.AbsoluteToLocal(ScreenPos);
        if (LocalPos.Y >= 0.f && LocalPos.Y <= 70.f)
        {
            return true;
        }
    }

    return false;
}

FReply UBIMInspectorWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        FVector2D ScreenPos = InMouseEvent.GetScreenSpacePosition();
        if (IsPointerOverDragArea(ScreenPos))
        {
            UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Border_MainPanel->Slot);
            if (CanvasSlot)
            {
                bIsDraggingWindow = true;
                DragStartCursorPos = InGeometry.AbsoluteToLocal(ScreenPos);
                DragStartPanelPos = CanvasSlot->GetPosition();
                SetCursor(EMouseCursor::GrabHandClosed);
                return FReply::Handled().CaptureMouse(TakeWidget());
            }
        }
    }

    return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UBIMInspectorWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    FVector2D CursorScreenPos = InMouseEvent.GetScreenSpacePosition();
    bool bIsOverHeader = IsPointerOverDragArea(CursorScreenPos);

    if (bIsDraggingWindow)
    {
        SetCursor(EMouseCursor::GrabHandClosed);
    }
    else if (bIsOverHeader)
    {
        SetCursor(EMouseCursor::GrabHand);
    }
    else
    {
        SetCursor(EMouseCursor::Default);
    }

    if (bIsDraggingWindow && Border_MainPanel)
    {
        UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Border_MainPanel->Slot);
        if (CanvasSlot)
        {
            FVector2D CurrentCursorLocalPos = InGeometry.AbsoluteToLocal(CursorScreenPos);
            FVector2D CursorDelta = CurrentCursorLocalPos - DragStartCursorPos;
            FVector2D TargetPos = DragStartPanelPos + CursorDelta;

            FVector2D ViewportLocalSize = InGeometry.GetLocalSize();
            if (ViewportLocalSize.X <= 0.f || ViewportLocalSize.Y <= 0.f)
            {
                ViewportLocalSize = FVector2D(1920.f, 1080.f);
            }

            FVector2D PanelLocalSize = Border_MainPanel->GetCachedGeometry().GetLocalSize();
            if (PanelLocalSize.X <= 0.f || PanelLocalSize.Y <= 0.f)
            {
                PanelLocalSize = FVector2D(500.f, 750.f);
            }

            FAnchors Anchors = CanvasSlot->GetAnchors();
            FVector2D Alignment = CanvasSlot->GetAlignment();
            FVector2D AnchorOffset(ViewportLocalSize.X * Anchors.Minimum.X, ViewportLocalSize.Y * Anchors.Minimum.Y);

            float Margin = 10.f;
            float MinPosX = Margin - AnchorOffset.X + PanelLocalSize.X * Alignment.X;
            float MaxPosX = (ViewportLocalSize.X - Margin) - AnchorOffset.X - PanelLocalSize.X * (1.0f - Alignment.X);
            float MinPosY = Margin - AnchorOffset.Y + PanelLocalSize.Y * Alignment.Y;
            float MaxPosY = (ViewportLocalSize.Y - Margin) - AnchorOffset.Y - PanelLocalSize.Y * (1.0f - Alignment.Y);

            FVector2D ClampedPos;
            ClampedPos.X = FMath::Clamp(TargetPos.X, FMath::Min(MinPosX, MaxPosX), FMath::Max(MinPosX, MaxPosX));
            ClampedPos.Y = FMath::Clamp(TargetPos.Y, FMath::Min(MinPosY, MaxPosY), FMath::Max(MinPosY, MaxPosY));

            CanvasSlot->SetPosition(ClampedPos);
            return FReply::Handled();
        }
    }

    return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UBIMInspectorWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsDraggingWindow)
    {
        bIsDraggingWindow = false;
        SetCursor(EMouseCursor::Default);
        return FReply::Handled().ReleaseMouseCapture();
    }

    return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

bool UBIMInspectorWidget::IsMouseOverMainPanel() const
{
    if (!IsInViewport())
    {
        return false;
    }

    const UWidget* TargetWidget = Border_MainPanel ? Cast<const UWidget>(Border_MainPanel.Get()) : Cast<const UWidget>(this);
    if (!TargetWidget)
    {
        return false;
    }

    if (TargetWidget->IsHovered())
    {
        return true;
    }

    if (FSlateApplication::IsInitialized())
    {
        FGeometry WidgetGeo = TargetWidget->GetCachedGeometry();
        FVector2D CursorPos = FSlateApplication::Get().GetCursorPos();
        return WidgetGeo.IsUnderLocation(CursorPos);
    }
    return false;
}

void UBIMCategoryHeaderHandler::OnHeaderClicked()
{
    if (ContentBox)
    {
        const bool bIsCollapsed = (ContentBox->GetVisibility() == ESlateVisibility::Collapsed);
        ContentBox->SetVisibility(bIsCollapsed ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

        if (ArrowText)
        {
            ArrowText->SetText(FText::FromString(bIsCollapsed ? TEXT("▼ ") : TEXT("▶ ")));
        }
    }
}

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
        Txt_Category->SetColorAndOpacity(FSlateColor(FLinearColor::White));
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

        CategoryHeaderHandlers.Empty();

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
                        FButtonStyle HeaderStyle = HeaderBtn->GetStyle();
                        HeaderStyle.Normal.DrawAs = ESlateBrushDrawType::NoDrawType;
                        HeaderStyle.Hovered.DrawAs = ESlateBrushDrawType::Box;
                        HeaderStyle.Hovered.TintColor = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.05f));
                        HeaderStyle.Pressed.DrawAs = ESlateBrushDrawType::Box;
                        HeaderStyle.Pressed.TintColor = FSlateColor(FLinearColor(0.2f, 0.6f, 0.9f, 0.15f));
                        HeaderBtn->SetStyle(HeaderStyle);

                        ArrowText->SetText(FText::FromString(TEXT("▼ ")));
                        FSlateFontInfo ArrowFont = ArrowText->GetFont();
                        ArrowFont.Size = 9.f;
                        ArrowText->SetFont(ArrowFont);
                        ArrowText->SetColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f)));

                        TitleText->SetText(FText::FromString(CatGroup.CategoryName));
                        FSlateFontInfo TitleFont = TitleText->GetFont();
                        TitleFont.Size = 11.f;
                        TitleText->SetFont(TitleFont);
                        TitleText->SetColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.85f, 0.85f, 1.f)));

                        UHorizontalBoxSlot* ArrowSlot = Cast<UHorizontalBoxSlot>(HeaderBox->AddChild(ArrowText));
                        if (ArrowSlot)
                        {
                            ArrowSlot->SetVerticalAlignment(VAlign_Center);
                            ArrowSlot->SetHorizontalAlignment(HAlign_Left);
                            ArrowSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
                        }

                        UHorizontalBoxSlot* TitleSlot = Cast<UHorizontalBoxSlot>(HeaderBox->AddChild(TitleText));
                        if (TitleSlot)
                        {
                            TitleSlot->SetVerticalAlignment(VAlign_Center);
                            TitleSlot->SetHorizontalAlignment(HAlign_Left);
                        }

                        if (UButtonSlot* BtnSlot = Cast<UButtonSlot>(HeaderBtn->AddChild(HeaderBox)))
                        {
                            BtnSlot->SetHorizontalAlignment(HAlign_Left);
                            BtnSlot->SetVerticalAlignment(VAlign_Center);
                            BtnSlot->SetPadding(FMargin(4.f, 3.f, 4.f, 3.f));
                        }

                        UVerticalBox* ContentBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
                        if (ContentBox)
                        {
                            ContentBox->SetVisibility(ESlateVisibility::Visible);
                        }

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
                                    KeyText->SetColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f, 1.0f)));
                                    KeyText->SetAutoWrapText(true);
                                    FSlateFontInfo KeyFont = KeyText->GetFont();
                                    KeyFont.Size = 11.f;
                                    KeyText->SetFont(KeyFont);

                                    ValText->SetText(FText::FromString(Pair.Value));
                                    ValText->SetColorAndOpacity(FSlateColor(FLinearColor(0.2f, 0.85f, 1.0f, 1.0f))); // Cyan
                                    ValText->SetToolTipText(FText::FromString(Pair.Value));
                                    ValText->SetAutoWrapText(false);
                                    ValText->SetClipping(EWidgetClipping::ClipToBounds);

                                    FSlateFontInfo ValFont = ValText->GetFont();
                                    ValFont.Size = 11.f;
                                    ValText->SetFont(ValFont);

                                    UHorizontalBoxSlot* KeySlot = Cast<UHorizontalBoxSlot>(RowBox->AddChild(KeyText));
                                    if (KeySlot)
                                    {
                                        KeySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                                        KeySlot->SetPadding(FMargin(0.f, 2.f, 8.f, 2.f));
                                        KeySlot->SetVerticalAlignment(VAlign_Center);
                                    }

                                    USizeBox* ValSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
                                    if (ValSizeBox)
                                    {
                                        ValSizeBox->SetMaxDesiredWidth(140.f);
                                        ValSizeBox->SetClipping(EWidgetClipping::ClipToBounds);
                                        ValSizeBox->AddChild(ValText);

                                        UHorizontalBoxSlot* ValSlot = Cast<UHorizontalBoxSlot>(RowBox->AddChild(ValSizeBox));
                                        if (ValSlot)
                                        {
                                            ValSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
                                            ValSlot->SetHorizontalAlignment(HAlign_Right);
                                            ValSlot->SetVerticalAlignment(VAlign_Center);
                                            ValSlot->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));
                                        }
                                    }
                                    else
                                    {
                                        UHorizontalBoxSlot* ValSlot = Cast<UHorizontalBoxSlot>(RowBox->AddChild(ValText));
                                        if (ValSlot)
                                        {
                                            ValSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                                            ValSlot->SetHorizontalAlignment(HAlign_Right);
                                            ValSlot->SetVerticalAlignment(VAlign_Center);
                                            ValSlot->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f));
                                        }
                                    }

                                    UVerticalBoxSlot* VRowSlot = ContentBox->AddChildToVerticalBox(RowBox);
                                    if (VRowSlot)
                                    {
                                        VRowSlot->SetPadding(FMargin(36.f, 3.f, 12.f, 3.f));
                                    }
                                }
                            }
                        }

                        UBIMCategoryHeaderHandler* Handler = NewObject<UBIMCategoryHeaderHandler>(this);
                        if (Handler)
                        {
                            Handler->ContentBox = ContentBox;
                            Handler->ArrowText = ArrowText;
                            HeaderBtn->OnClicked.AddUniqueDynamic(Handler, &UBIMCategoryHeaderHandler::OnHeaderClicked);
                            CategoryHeaderHandlers.Add(Handler);
                        }

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
                            CleanKey.ReplaceInline(TEXT("Element*"), TEXT(""));
                            CleanKey.ReplaceInline(TEXT("Element="), TEXT(""));
                            CleanKey.ReplaceInline(TEXT("Element."), TEXT(""));
                            CleanKey.ReplaceInline(TEXT("Element_"), TEXT(""));
                            CleanKey.ReplaceInline(TEXT("Type*"), TEXT(""));
                            CleanKey.ReplaceInline(TEXT("Type="), TEXT(""));
                            CleanKey.ReplaceInline(TEXT("Type."), TEXT(""));
                            CleanKey.ReplaceInline(TEXT("Type_"), TEXT(""));

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
