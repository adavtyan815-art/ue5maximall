// Copyright 2026 MaxiMall. All Rights Reserved.

#include "FurnitureConfigurator/UI/PlannerCatalogItemWidget.h"
#include "FurnitureConfigurator/UI/RoomPlannerWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/DragDropOperation.h"
#include "Components/SizeBox.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"

TSharedRef<SWidget> UPlannerCatalogItemWidget::RebuildWidget()
{
	// Pure C++ card: build SizeBox → Border → VerticalBox(Image, Text) when no Blueprint design exists.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CardSize"));
		Size->SetWidthOverride(CardWidth);
		Size->SetHeightOverride(CardHeight);

		UBorder* Border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CardBorder"));
		Border->SetPadding(FMargin(4.f));
		Border->SetBrushColor(ObjectCardColor);

		UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CardBox"));

		UImage* Img = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("ImgThumbnail"));
		Img->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UVerticalBoxSlot* ImgSlot = Box->AddChildToVerticalBox(Img))
		{
			ImgSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			ImgSlot->SetHorizontalAlignment(HAlign_Fill);
			ImgSlot->SetVerticalAlignment(VAlign_Fill);
		}

		UTextBlock* Txt = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TxtName"));
		Txt->SetVisibility(ESlateVisibility::HitTestInvisible);
		Txt->SetJustification(ETextJustify::Center);
		Txt->SetAutoWrapText(true);
		FSlateFontInfo Font = Txt->GetFont();
		Font.Size = 9;
		Txt->SetFont(Font);
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
	if (CardBorder)
	{
		CardBorder->SetBrushColor(Kind == EPlannerPlacementKind::CabinetSet ? CabinetSetCardColor : ObjectCardColor);
	}
}

FReply UPlannerCatalogItemWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && !ItemID.IsEmpty())
	{
		return UWidgetBlueprintLibrary::DetectDragIfPressed(InMouseEvent, this, EKeys::LeftMouseButton).NativeReply;
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UPlannerCatalogItemWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	UDragDropOperation* Op = UWidgetBlueprintLibrary::CreateDragDropOperation(UDragDropOperation::StaticClass());
	if (!Op)
	{
		Super::NativeOnDragDetected(InGeometry, InMouseEvent, OutOperation);
		return;
	}

	Op->Payload = this;
	Op->Tag = ItemID;
	Op->Pivot = EDragPivot::CenterCenter;

	// Drag visual: a second card with the same data.
	if (UPlannerCatalogItemWidget* Visual = CreateWidget<UPlannerCatalogItemWidget>(GetOwningPlayer(), GetClass()))
	{
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
	// over its own catalog panels.
	if (URoomPlannerWidget* Planner = OwnerPlanner.Get())
	{
		Planner->HandleCatalogDragReleased(Kind, ItemID, InDragDropEvent.GetScreenSpacePosition());
	}
}
