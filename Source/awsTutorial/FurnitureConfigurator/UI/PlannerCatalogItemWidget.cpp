// Copyright 2026 MaxiMall. All Rights Reserved.

#include "FurnitureConfigurator/UI/PlannerCatalogItemWidget.h"
#include "FurnitureConfigurator/UI/PlannerPanelRules.h"
#include "FurnitureConfigurator/UI/RoomPlannerWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/DragDropOperation.h"
#include "Components/SizeBox.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

TSharedRef<SWidget> UPlannerCatalogItemWidget::RebuildWidget()
{
	// Pure C++ card: SizeBox → Border → VerticalBox(ScaleBox(Image), Text) when no Blueprint design exists.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CardSize"));
		Size->SetWidthOverride(CardWidth);
		Size->SetHeightOverride(CardHeight);

		UBorder* Border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CardBorder"));
		Border->SetPadding(CardPadding);
		Border->SetBrushColor(NormalColor);

		UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CardBox"));

		UScaleBox* Scale = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("CardScale"));
		Scale->SetStretch(ImageStretch);
		Scale->SetVisibility(ESlateVisibility::HitTestInvisible);

		UImage* Img = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("ImgThumbnail"));
		Img->SetVisibility(ESlateVisibility::HitTestInvisible);
		Scale->AddChild(Img);

		if (UVerticalBoxSlot* ImgSlot = Box->AddChildToVerticalBox(Scale))
		{
			ImgSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			ImgSlot->SetHorizontalAlignment(HAlign_Fill);
			ImgSlot->SetVerticalAlignment(VAlign_Fill);
		}

		UTextBlock* Txt = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TxtName"));
		Txt->SetVisibility(ESlateVisibility::HitTestInvisible);
		Txt->SetJustification(ETextJustify::Center);
		Txt->SetAutoWrapText(true);
		if (TextFont.HasValidFont())
		{
			Txt->SetFont(TextFont);
		}
		else
		{
			FSlateFontInfo Font = Txt->GetFont();
			Font.Size = 9;
			Txt->SetFont(Font);
		}
		Txt->SetColorAndOpacity(FSlateColor(TextColor));
		if (UVerticalBoxSlot* TxtSlot = Box->AddChildToVerticalBox(Txt))
		{
			TxtSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			TxtSlot->SetHorizontalAlignment(HAlign_Fill);
			TxtSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
		}

		Border->SetContent(Box);
		Size->SetContent(Border);
		WidgetTree->RootWidget = Size;

		ImgThumbnail = Img;
		TxtName = Txt;
		CardBorder = Border;
	}
	return Super::RebuildWidget();
}

void UPlannerCatalogItemWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::Visible); // the card itself must be hit-testable to start a drag
	ApplyVisuals();
}

void UPlannerCatalogItemWidget::SetupCatalogItem(URoomPlannerWidget* InOwner, EPlannerPlacementKind InKind, const FPlannerCatalogEntry& Entry)
{
	OwnerPlanner = InOwner;
	Kind = InKind;
	ItemID = Entry.ID;
	DisplayName = Entry.DisplayName.IsEmpty() ? FText::FromString(Entry.ID) : Entry.DisplayName;
	Thumbnail = Entry.Thumbnail;
	SetToolTipText(DisplayName);
	ApplyVisuals();
}

void UPlannerCatalogItemWidget::ApplyVisuals()
{
	if (TxtName)
	{
		TxtName->SetText(DisplayName);
		TxtName->SetColorAndOpacity(FSlateColor(TextColor));
		if (TextFont.HasValidFont())
		{
			TxtName->SetFont(TextFont);
		}
	}
	if (ImgThumbnail)
	{
		if (UTexture2D* Tex = Thumbnail.LoadSynchronous())
		{
			ImgThumbnail->SetBrushFromTexture(Tex, false);
			ImgThumbnail->SetColorAndOpacity(FLinearColor::White);
		}
		else
		{
			// No thumbnail: keep a neutral tinted block so the card still reads as a draggable item.
			ImgThumbnail->SetBrushFromTexture(nullptr, false);
			ImgThumbnail->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.25f));
		}
	}
	ApplyStateTint();
	ApplyDragYaw();
}

void UPlannerCatalogItemWidget::ShowDragYaw(float YawDeg)
{
	DragYawDeg = (float)FRotator::NormalizeAxis(YawDeg);
	ApplyDragYaw();
}

void UPlannerCatalogItemWidget::ApplyDragYaw()
{
	// Positive yaw is clockwise on the plan (the 2D camera looks down with +X up), and so is a positive render angle on screen.
	if (ImgThumbnail && !FMath::IsNearlyEqual(ImgThumbnail->GetRenderTransformAngle(), DragYawDeg))
	{
		ImgThumbnail->SetRenderTransformAngle(DragYawDeg);
	}
	if (TxtName && !FMath::IsNearlyZero(DragYawDeg))
	{
		TxtName->SetText(FText::FromString(FString::Printf(TEXT("%s · %s"), *DisplayName.ToString(), *PlannerPanelRules::FormatPlanAngle(DragYawDeg))));
	}
	else if (TxtName)
	{
		TxtName->SetText(DisplayName);
	}
}

void UPlannerCatalogDragOperation::SetYaw(float InYawDeg)
{
	YawDeg = (float)FRotator::NormalizeAxis(InYawDeg);
	if (UPlannerCatalogItemWidget* Visual = Cast<UPlannerCatalogItemWidget>(DefaultDragVisual))
	{
		Visual->ShowDragYaw(YawDeg);
	}
}

void UPlannerCatalogItemWidget::ApplyStateTint()
{
	if (CardBorder)
	{
		CardBorder->SetBrushColor(bPressed ? PressedColor : (bHovered ? HoveredColor : NormalColor));
	}
}

void UPlannerCatalogItemWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	bHovered = true;
	ApplyStateTint();
}

void UPlannerCatalogItemWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	bHovered = false;
	bPressed = false;
	ApplyStateTint();
}

FReply UPlannerCatalogItemWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && !ItemID.IsEmpty())
	{
		bPressed = true;
		ApplyStateTint();
		return UWidgetBlueprintLibrary::DetectDragIfPressed(InMouseEvent, this, EKeys::LeftMouseButton).NativeReply;
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UPlannerCatalogItemWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	bPressed = false;
	ApplyStateTint();
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

void UPlannerCatalogItemWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	bPressed = false;
	ApplyStateTint();

	UPlannerCatalogDragOperation* Op = Cast<UPlannerCatalogDragOperation>(UWidgetBlueprintLibrary::CreateDragDropOperation(UPlannerCatalogDragOperation::StaticClass()));
	if (!Op)
	{
		Super::NativeOnDragDetected(InGeometry, InMouseEvent, OutOperation);
		return;
	}

	Op->Payload = this;
	Op->Tag = ItemID;
	Op->Pivot = EDragPivot::CenterCenter;
	Op->Kind = Kind;
	Op->ItemID = ItemID; // the yaw starts at 0; the mouse wheel turns it over the 2D plan

	// Drag visual: a second card with the same data and style.
	if (UPlannerCatalogItemWidget* Visual = CreateWidget<UPlannerCatalogItemWidget>(GetOwningPlayer(), GetClass()))
	{
		Visual->CardWidth = CardWidth;
		Visual->CardHeight = CardHeight;
		Visual->CardPadding = CardPadding;
		Visual->ImageStretch = ImageStretch;
		Visual->NormalColor = NormalColor;
		Visual->HoveredColor = HoveredColor;
		Visual->PressedColor = PressedColor;
		Visual->TextColor = TextColor;
		Visual->TextFont = TextFont;
		FPlannerCatalogEntry Entry;
		Entry.ID = ItemID;
		Entry.DisplayName = DisplayName;
		Entry.Thumbnail = Thumbnail;
		Visual->SetupCatalogItem(OwnerPlanner.Get(), Kind, Entry);
		Visual->SetRenderOpacity(0.85f);
		Op->DefaultDragVisual = Visual;
	}

	OutOperation = Op;
	UE_LOG(LogTemp, Warning, TEXT("[PlannerDrop] Drag started: '%s' (kind %d), owner planner %s"), *ItemID, (int32)Kind, OwnerPlanner.IsValid() ? TEXT("ok") : TEXT("NULL"));
}

void UPlannerCatalogItemWidget::NativeOnDragCancelled(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragCancelled(InDragDropEvent, InOperation);

	// Released over something that did not accept the drop (typically the plan itself when the planner
	// root is not hit-testable). Let the planner resolve the drop at the cursor; it ignores releases
	// over its own catalog area. The yaw the wheel gave the item during the drag goes with it.
	if (URoomPlannerWidget* Planner = OwnerPlanner.Get())
	{
		const UPlannerCatalogDragOperation* CatalogDrag = Cast<UPlannerCatalogDragOperation>(InOperation);
		Planner->HandleCatalogDragReleased(Kind, ItemID, InDragDropEvent.GetScreenSpacePosition(), CatalogDrag ? CatalogDrag->YawDeg : 0.f);
	}
}
