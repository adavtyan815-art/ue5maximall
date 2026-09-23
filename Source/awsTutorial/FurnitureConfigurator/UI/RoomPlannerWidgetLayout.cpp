// Copyright 2026 MaxiMall. All Rights Reserved.

// URoomPlannerWidget, part 2: the Planner 5D structure of the side panel, built in code from the WBP's own widgets (the repository
// carries no Content, so the layout reaches every machine with the source). Categories «Планировка» / «Каталог» / «Отделка», one open
// at a time; the tool segment; one page per category; the status strip over the plan; the input boundary of the planner UI.

#include "FurnitureConfigurator/UI/RoomPlannerWidget.h"
#include "awsTutorial_PlayerController.h"
#include "ColorCatalog/ColorCatalogWidget.h"
#include "Constructor/RoomPlannerManager.h"
#include "FurnitureConfigurator/UI/PlannerTileCatalogWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"

namespace PlannerPanelLayout
{
	/** A vertical-box slot's layout, carried over when a designed section moves into a page. */
	struct FVerticalSlotLayout
	{
		FMargin Padding;
		EHorizontalAlignment HAlign = HAlign_Fill;
		EVerticalAlignment VAlign = VAlign_Fill;
		FSlateChildSize Size;
		bool bValid = false;
	};

	static FVerticalSlotLayout ReadVerticalSlot(const UWidget* Widget)
	{
		FVerticalSlotLayout Layout;
		if (const UVerticalBoxSlot* VSlot = Widget ? Cast<UVerticalBoxSlot>(Widget->Slot) : nullptr)
		{
			Layout.Padding = VSlot->GetPadding();
			Layout.HAlign = VSlot->GetHorizontalAlignment();
			Layout.VAlign = VSlot->GetVerticalAlignment();
			Layout.Size = VSlot->GetSize();
			Layout.bValid = true;
		}
		return Layout;
	}

	static void ApplyVerticalSlot(UPanelSlot* PanelSlot, const FVerticalSlotLayout& Layout)
	{
		if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(PanelSlot))
		{
			VSlot->SetPadding(Layout.Padding);
			VSlot->SetHorizontalAlignment(Layout.HAlign);
			VSlot->SetVerticalAlignment(Layout.VAlign);
			VSlot->SetSize(Layout.Size);
		}
	}

	/** Moves a widget to the end of Target, keeping its vertical-box slot layout (padding, alignment). */
	static void MoveToVerticalBox(UWidget* Widget, UVerticalBox* Target)
	{
		if (!Widget || !Target) return;
		const FVerticalSlotLayout Layout = ReadVerticalSlot(Widget);
		Widget->RemoveFromParent();
		UPanelSlot* NewSlot = Target->AddChild(Widget);
		if (Layout.bValid) ApplyVerticalSlot(NewSlot, Layout);
	}

	static UVerticalBoxSlot* AddToVerticalBox(UVerticalBox* Box, UWidget* Widget, const FMargin& Padding, EHorizontalAlignment HAlign = HAlign_Fill)
	{
		if (!Box || !Widget) return nullptr;
		UVerticalBoxSlot* VSlot = Box->AddChildToVerticalBox(Widget);
		if (VSlot)
		{
			VSlot->SetPadding(Padding);
			VSlot->SetHorizontalAlignment(HAlign);
		}
		return VSlot;
	}

	static void InsertIntoVerticalBox(UVerticalBox* Box, int32 Index, UWidget* Widget, const FMargin& Padding, EHorizontalAlignment HAlign = HAlign_Fill)
	{
		if (!Box || !Widget) return;
		if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(Box->InsertChildAt(Index, Widget)))
		{
			VSlot->SetPadding(Padding);
			VSlot->SetHorizontalAlignment(HAlign);
		}
	}

	static void ShowIf(UWidget* Widget, bool bShow, ESlateVisibility VisibleAs)
	{
		if (!Widget) return;
		const ESlateVisibility Wanted = bShow ? VisibleAs : ESlateVisibility::Collapsed;
		if (Widget->GetVisibility() != Wanted) Widget->SetVisibility(Wanted);
	}

	static void EnableIf(UWidget* Widget, bool bEnabled)
	{
		if (Widget && Widget->GetIsEnabled() != bEnabled) Widget->SetIsEnabled(bEnabled);
	}

	/**
	 * Puts Widget directly before Anchor in their horizontal box. The slots keep their places: the one that was first stays first
	 * (padding, size, alignment), so only the order of the fields changes.
	 */
	static void MoveBeforeInRow(UWidget* Widget, UWidget* Anchor)
	{
		UHorizontalBox* Row = Widget ? Cast<UHorizontalBox>(Widget->GetParent()) : nullptr;
		if (!Row || !Anchor || Anchor->GetParent() != Row || Row->GetChildIndex(Widget) < Row->GetChildIndex(Anchor)) return;
		struct FHSlot { FMargin Padding; FSlateChildSize Size; EHorizontalAlignment HAlign; EVerticalAlignment VAlign; };
		auto Read = [](const UWidget* W)
		{
			const UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(W->Slot);
			return S ? FHSlot{ S->GetPadding(), S->GetSize(), S->GetHorizontalAlignment(), S->GetVerticalAlignment() } : FHSlot{ FMargin(0.f), FSlateChildSize(), HAlign_Fill, VAlign_Fill };
		};
		auto Write = [](UWidget* W, const FHSlot& L)
		{
			if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(W->Slot))
			{
				S->SetPadding(L.Padding);
				S->SetSize(L.Size);
				S->SetHorizontalAlignment(L.HAlign);
				S->SetVerticalAlignment(L.VAlign);
			}
		};
		const FHSlot AnchorLayout = Read(Anchor);
		const FHSlot WidgetLayout = Read(Widget);
		Widget->RemoveFromParent();
		Row->InsertChildAt(Row->GetChildIndex(Anchor), Widget);
		Write(Widget, AnchorLayout);
		Write(Anchor, WidgetLayout);
	}
}

// ═══════════════════════════════════════════════════════════════════════════════
// Building blocks
// ═══════════════════════════════════════════════════════════════════════════════

UButton* URoomPlannerWidget::MakeRuntimeButton(const UButton* StyleSource, const FString& Label, FName Name, const FString& Tooltip, const UButton* LabelSource)
{
	if (!WidgetTree || !StyleSource) return nullptr;
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(*(Name.ToString() + TEXT("Label"))));
	if (!Button || !Text) return nullptr;

	Button->SetStyle(StyleSource->GetStyle());
	Button->SetBackgroundColor(StyleSource->GetBackgroundColor());
	Button->SetColorAndOpacity(StyleSource->GetColorAndOpacity());

	Text->SetText(FText::FromString(Label));
	const UButton* FontSource = LabelSource ? LabelSource : StyleSource;
	const UTextBlock* SourceLabel = Cast<UTextBlock>(FontSource->GetContent());
	if (SourceLabel)
	{
		Text->SetFont(SourceLabel->GetFont());
		Text->SetColorAndOpacity(SourceLabel->GetColorAndOpacity());
	}
	else
	{
		Text->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 13.5f));
		Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	}
	if (UButtonSlot* LabelSlot = Cast<UButtonSlot>(Button->AddChild(Text)))
	{
		if (const UButtonSlot* SourceLabelSlot = SourceLabel ? Cast<UButtonSlot>(SourceLabel->Slot) : nullptr)
		{
			LabelSlot->SetPadding(SourceLabelSlot->GetPadding());
			LabelSlot->SetHorizontalAlignment(SourceLabelSlot->GetHorizontalAlignment());
			LabelSlot->SetVerticalAlignment(SourceLabelSlot->GetVerticalAlignment());
		}
	}
	if (!Tooltip.IsEmpty())
	{
		Button->SetToolTipText(FText::FromString(Tooltip));
	}
	return Button;
}

UTextBlock* URoomPlannerWidget::MakePanelText(const FString& Text, FName Name, bool bCaption)
{
	if (!WidgetTree) return nullptr;
	UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	if (!Block) return nullptr;
	Block->SetText(FText::FromString(Text));
	FSlateFontInfo Font = LblWallSize ? LblWallSize->GetFont() : FCoreStyle::GetDefaultFontStyle("Bold", 10.5f);
	if (bCaption)
	{
		// The captions above the size fields: Roboto Bold 10.5, #ADB3BC.
		Block->SetFont(Font);
		Block->SetColorAndOpacity(LblWallSize ? LblWallSize->GetColorAndOpacity() : FSlateColor(FLinearColor::FromSRGBColor(FColor(0xAD, 0xB3, 0xBC))));
	}
	else
	{
		// Notices and prompts: the panel's ink (#3E4C5E, as the totals), readable on the white panel, wrapping.
		Font.Size = 12.f;
		Block->SetFont(Font);
		Block->SetColorAndOpacity(TxtFloorArea ? TxtFloorArea->GetColorAndOpacity() : FSlateColor(FLinearColor::FromSRGBColor(FColor(0x3E, 0x4C, 0x5E))));
		Block->SetAutoWrapText(true);
	}
	Block->SetVisibility(ESlateVisibility::HitTestInvisible);
	return Block;
}

UWidget* URoomPlannerWidget::MakeSeparatorLine(FName Name)
{
	if (!WidgetTree) return nullptr;
	UImage* Line = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), Name);
	if (!Line) return nullptr;
	if (const UImage* Model = WidgetTree->FindWidget<UImage>(PlannerPanelNames::FirstSeparator))
	{
		Line->SetBrush(Model->GetBrush());
		Line->SetColorAndOpacity(Model->GetColorAndOpacity());
	}
	else
	{
		FSlateBrush Brush;
		Brush.TintColor = FSlateColor(FLinearColor::FromSRGBColor(FColor(0x42, 0x47, 0x4D)));
		Brush.ImageSize = FVector2D(32.f, 1.f);
		Line->SetBrush(Brush);
	}
	Line->SetVisibility(ESlateVisibility::HitTestInvisible);
	return Line;
}

FSlateBrush URoomPlannerWidget::MakeGroupFrameBrush()
{
	// The look of our buttons' frame: white fill, radius 4, 1 px #D9D9D9 outline.
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.TintColor = FSlateColor(FLinearColor::White);
	Brush.OutlineSettings = FSlateBrushOutlineSettings(4.f, FSlateColor(FLinearColor::FromSRGBColor(FColor(0xD9, 0xD9, 0xD9))), 1.f);
	return Brush;
}

void URoomPlannerWidget::ApplyButtonState(UButton* Button, EPanelButtonState State)
{
	if (!Button) return;
	const FLinearColor Color = State == EPanelButtonState::Selected ? ActiveTabColor
		: (State == EPanelButtonState::On ? ActiveToolColor : IdleControlColor);
	if (!Button->GetBackgroundColor().Equals(Color))
	{
		Button->SetBackgroundColor(Color);
	}

	// The selected view / category may also carry an ActiveTabColor outline; every other state keeps the designed style.
	const TObjectKey<UButton> Key(Button);
	if (!DesignedButtonStyles.Contains(Key))
	{
		DesignedButtonStyles.Add(Key, Button->GetStyle());
	}
	FButtonStyle Wanted = DesignedButtonStyles[Key];
	if (bOutlineSelectedNavigation && State == EPanelButtonState::Selected)
	{
		FSlateBrush* Brushes[] = { &Wanted.Normal, &Wanted.Hovered, &Wanted.Pressed };
		for (FSlateBrush* Brush : Brushes)
		{
			if (Brush->DrawAs == ESlateBrushDrawType::RoundedBox)
			{
				Brush->OutlineSettings.Color = FSlateColor(ActiveTabColor);
				Brush->OutlineSettings.Width = SelectedOutlineWidth;
			}
		}
	}
	const FSlateBrushOutlineSettings& Current = Button->GetStyle().Normal.OutlineSettings;
	if (Current.Width != Wanted.Normal.OutlineSettings.Width || !(Current.Color == Wanted.Normal.OutlineSettings.Color))
	{
		Button->SetStyle(Wanted);
	}
}

float URoomPlannerWidget::GetPanelWidth() const
{
	if (const UCanvasPanelSlot* PanelSlot = PanelBackground ? Cast<UCanvasPanelSlot>(PanelBackground->Slot) : nullptr)
	{
		const float Width = (float)PanelSlot->GetSize().X;
		if (Width > 1.f) return Width;
	}
	return 426.f;
}

bool URoomPlannerWidget::IsPlanEmpty() const
{
	if (!PlannerManager) return true;
	return PlannerManager->GetWallCount() == 0 && PlannerManager->GetPlacedObjects().Num() == 0 && PlannerManager->GetCabinetSets().Num() == 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// The category layout
// ═══════════════════════════════════════════════════════════════════════════════

bool URoomPlannerWidget::BuildCategoryLayout()
{
	using namespace PlannerPanelLayout;
	if (bCategoryLayoutBuilt) return true;
	if (!WidgetTree || !BtnCatalogToggle || !BtnDrawWallTool || !BtnSelectTool || !BtnPresetRoom) return false;

	UVerticalBox* Sections = WidgetTree->FindWidget<UVerticalBox>(PlannerPanelNames::PanelSections);
	UBorder* Tools = WidgetTree->FindWidget<UBorder>(PlannerPanelNames::ToolsRow);
	UHorizontalBox* ToolBox = Cast<UHorizontalBox>(BtnDrawWallTool->GetParent());
	UWidget* Selection = WidgetTree->FindWidget(PlannerPanelNames::SelectionBlock);
	UWidget* FinishRowWidget = WidgetTree->FindWidget(PlannerPanelNames::FinishRow);
	if (!Sections || !Tools || Tools->GetParent() != Sections || !ToolBox || BtnSelectTool->GetParent() != ToolBox
		|| BtnPresetRoom->GetParent() != ToolBox || !Selection || Selection->GetParent() != Sections
		|| !FinishRowWidget || FinishRowWidget->GetParent() != Sections)
	{
		return false; // not the panel this layout was made for: keep the one-row toolbar
	}

	// ── 1. Category tabs under the view row: «Планировка» | «Каталог» | «Отделка» (styled like the 2D / 3D switch) ──
	const UButton* TabStyle = Btn_3DView ? Btn_3DView.Get() : BtnDrawWallTool.Get();
	BtnCategoryLayout = MakeRuntimeButton(TabStyle, TEXT("Планировка"), TEXT("BtnCategoryLayout"),
		TEXT("Планировка: рисование стен, выбор элементов, шаблон комнаты"), BtnDrawWallTool);
	BtnCategoryFinish = MakeRuntimeButton(TabStyle, TEXT("Отделка"), TEXT("BtnCategoryFinish"),
		TEXT("Отделка: краска и плитка для стен, пола, плинтуса, потолка, дверей и окон"), BtnDrawWallTool);
	UHorizontalBox* Tabs = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CategoryTabs"));
	UBorder* Strip = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CategoryStrip"));
	if (!BtnCategoryLayout || !BtnCategoryFinish || !Tabs || !Strip) return false;

	BtnCatalogToggle->SetStyle(TabStyle->GetStyle());
	UButton* TabButtons[] = { BtnCategoryLayout.Get(), BtnCatalogToggle.Get(), BtnCategoryFinish.Get() };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(TabButtons); ++Index)
	{
		if (UHorizontalBoxSlot* TabSlot = Tabs->AddChildToHorizontalBox(TabButtons[Index]))
		{
			// Sized to their text: «Планировка» is never clipped.
			TabSlot->SetPadding(FMargin(0.f, 0.f, Index + 1 < UE_ARRAY_COUNT(TabButtons) ? 4.f : 0.f, 0.f));
			TabSlot->SetVerticalAlignment(VAlign_Center);
		}
	}
	Tabs->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Strip->SetBrush(MakeGroupFrameBrush());
	Strip->SetPadding(FMargin(2.f));
	Strip->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Strip->SetContent(Tabs);
	BtnCategoryLayout->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnCategoryLayoutClicked);
	BtnCategoryFinish->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnCategoryFinishClicked);
	CategoryStrip = Strip;
	{
		const int32 ToolsIndex = Sections->GetChildIndex(Tools);
		InsertIntoVerticalBox(Sections, ToolsIndex, Strip, FMargin(14.f, 6.f, 14.f, 6.f), HAlign_Left);
		InsertIntoVerticalBox(Sections, ToolsIndex + 1, MakeSeparatorLine(TEXT("Image_line_rt1")), FMargin(16.f, 4.f, 16.f, 4.f));
	}

	// ── 2. «Каталог» / «Отделка» on a plan without walls: what to do first ──
	{
		UVerticalBox* Notice = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("EmptyPlanNotice"));
		UTextBlock* NoticeText = MakePanelText(TEXT("Сначала постройте комнату: нарисуйте стену на вкладке «Планировка» или нажмите «4×4 м»"),
			TEXT("EmptyPlanNoticeText"), false);
		BtnNoticePreset = MakeRuntimeButton(BtnPresetRoom, TEXT("4×4 м"), TEXT("BtnNoticePreset"), TEXT("Построить готовую комнату 4×4 м"));
		if (Notice && NoticeText && BtnNoticePreset)
		{
			AddToVerticalBox(Notice, NoticeText, FMargin(0.f, 0.f, 0.f, 6.f));
			AddToVerticalBox(Notice, BtnNoticePreset, FMargin(0.f), HAlign_Left);
			Notice->SetVisibility(ESlateVisibility::Collapsed);
			BtnNoticePreset->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnNoticePresetClicked);
			InsertIntoVerticalBox(Sections, Sections->GetChildIndex(Tools), Notice, FMargin(16.f, 6.f, 16.f, 6.f));
			EmptyPlanNotice = Notice;
		}
	}

	// ── 3. The tool segment: «Создать стену» | «Выбрать», two equal halves in a frame (it never reflows) ──
	BtnPresetRoom->RemoveFromParent();
	UButton* SegmentButtons[] = { BtnDrawWallTool.Get(), BtnSelectTool.Get() };
	for (UButton* Button : SegmentButtons)
	{
		if (UHorizontalBoxSlot* ToolSlot = Cast<UHorizontalBoxSlot>(Button->Slot))
		{
			ToolSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			ToolSlot->SetHorizontalAlignment(HAlign_Fill);
			ToolSlot->SetPadding(FMargin(0.f, 0.f, Button == BtnDrawWallTool ? 4.f : 0.f, 0.f));
		}
	}
	Tools->SetBrush(MakeGroupFrameBrush());
	ToolsRowWidget = Tools;

	// ── 4. «Каталог» page: sub-tabs, prompt, cards, drag hint, cancel placement (the designed widgets, moved) ──
	UVerticalBox* CatalogPage = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PageCatalogBody"));
	if (!CatalogPage) return false;
	CatalogPage->SetVisibility(ESlateVisibility::Collapsed);
	{
		UWidget* FirstCatalogWidget = (CatalogTabBar && CatalogTabBar->GetParent() == Sections) ? CatalogTabBar.Get()
			: ((Catalog_Container && Catalog_Container->GetParent() == Sections) ? Catalog_Container.Get() : nullptr);
		const int32 CatalogIndex = FirstCatalogWidget ? Sections->GetChildIndex(FirstCatalogWidget) : Sections->GetChildIndex(Selection) + 2;
		InsertIntoVerticalBox(Sections, FMath::Min(CatalogIndex, Sections->GetChildrenCount()), CatalogPage, FMargin(0.f));
		if (CatalogTabBar && CatalogTabBar->GetParent() == Sections) MoveToVerticalBox(CatalogTabBar, CatalogPage);
		CatalogPrompt = MakePanelText(TEXT("Выберите раздел: «Интерьер» или «Тумбы»"), TEXT("CatalogPrompt"), false);
		AddToVerticalBox(CatalogPage, CatalogPrompt, FMargin(16.f, 4.f, 16.f, 4.f));
		if (Catalog_Container && Catalog_Container->GetParent() == Sections) MoveToVerticalBox(Catalog_Container, CatalogPage);
		CatalogDragHint = MakePanelText(TEXT("Перетащите карточку на пол или на стену"), TEXT("CatalogDragHint"), true);
		AddToVerticalBox(CatalogPage, CatalogDragHint, FMargin(16.f, 2.f, 16.f, 6.f));
		if (BtnCancelPlacement && BtnCancelPlacement->GetParent() == Sections) MoveToVerticalBox(BtnCancelPlacement, CatalogPage);
		PageCatalogBody = CatalogPage;
	}

	// ── 5. «Планировка» page: «Шаблон комнаты» [4×4 м] ··· [Очистить план] (the confirmation bar goes under it) ──
	{
		UVerticalBox* LayoutPage = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PageLayoutBody"));
		UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PlanActionsRow"));
		USpacer* Gap = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("PlanActionsSpacer"));
		BtnClearPlan = MakeRuntimeButton(BtnPresetRoom, TEXT("Очистить план"), TEXT("BtnClearPlan"),
			TEXT("Удалить весь план: стены, проёмы, отделку и мебель"));
		if (!LayoutPage || !Actions || !Gap || !BtnClearPlan) return false;
		LayoutPage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Actions->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		AddToVerticalBox(LayoutPage, MakePanelText(TEXT("Шаблон комнаты"), TEXT("PresetCaption"), true), FMargin(0.f, 0.f, 0.f, 4.f));
		if (UHorizontalBoxSlot* PresetSlot = Actions->AddChildToHorizontalBox(BtnPresetRoom))
		{
			PresetSlot->SetVerticalAlignment(VAlign_Center);
		}
		if (UHorizontalBoxSlot* GapSlot = Actions->AddChildToHorizontalBox(Gap))
		{
			GapSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		if (UHorizontalBoxSlot* ClearSlot = Actions->AddChildToHorizontalBox(BtnClearPlan))
		{
			ClearSlot->SetVerticalAlignment(VAlign_Center);
		}
		AddToVerticalBox(LayoutPage, Actions, FMargin(0.f));
		CreateConfirmBar(LayoutPage);
		BtnClearPlan->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnClearPlanClicked);
		InsertIntoVerticalBox(Sections, Sections->GetChildIndex(CatalogPage), LayoutPage, FMargin(16.f, 6.f, 16.f, 6.f));
		PageLayoutBody = LayoutPage;
	}

	// ── 6. «Отделка» page: prompt, «Краска» «Плитка» «Сбросить отделку» + description, net finish areas ──
	{
		UVerticalBox* FinishPage = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PageFinishBody"));
		if (!FinishPage) return false;
		FinishPage->SetVisibility(ESlateVisibility::Collapsed);
		InsertIntoVerticalBox(Sections, Sections->GetChildIndex(FinishRowWidget), FinishPage, FMargin(0.f));
		FinishPrompt = MakePanelText(TEXT("Выберите на плане поверхность, затем «Краска» или «Плитка»"), TEXT("FinishPrompt"), false);
		AddToVerticalBox(FinishPage, FinishPrompt, FMargin(16.f, 6.f, 16.f, 0.f));
		BuildRoomSurfaceChooser(FinishPage);
		MoveToVerticalBox(FinishRowWidget, FinishPage);
		if (TxtFinishAreas)
		{
			TxtFinishAreas->RemoveFromParent();
			TxtFinishAreas->SetAutoWrapText(true);
			// Designed white on the white panel (unreadable): the panel's ink, as the description above it.
			if (TxtFinishAreas->GetColorAndOpacity().GetSpecifiedColor().GetLuminance() > 0.6f)
			{
				TxtFinishAreas->SetColorAndOpacity(TxtFloorArea ? TxtFloorArea->GetColorAndOpacity() : FSlateColor(FLinearColor::FromSRGBColor(FColor(0x3E, 0x4C, 0x5E))));
			}
			AddToVerticalBox(FinishPage, TxtFinishAreas, FMargin(16.f, 0.f, 16.f, 6.f));
		}
		PageFinishBody = FinishPage;
	}
	// The paint button's page is «Отделка» itself: the button says what it opens.
	if (UTextBlock* PaintLabel = BtnFinishPaint ? Cast<UTextBlock>(BtnFinishPaint->GetContent()) : nullptr)
	{
		PaintLabel->SetText(FText::FromString(TEXT("Краска")));
	}

	// ── 7. View row: [2D|3D] as one framed switch; «Сохранить» at the right end (the delete-all icon beside it is gone) ──
	if (UHorizontalBox* ViewButtons = WidgetTree->FindWidget<UHorizontalBox>(PlannerPanelNames::ViewButtons))
	{
		if (Btn_2DView && Btn_3DView && Btn_2DView->GetParent() == ViewButtons && Btn_3DView->GetParent() == ViewButtons)
		{
			UBorder* Segment = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ViewSegment"));
			UHorizontalBox* SegmentRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ViewSegmentButtons"));
			if (Segment && SegmentRow)
			{
				const int32 Index = ViewButtons->GetChildIndex(Btn_2DView);
				UButton* ViewSegmentButtons[] = { Btn_2DView.Get(), Btn_3DView.Get() };
				for (UButton* Button : ViewSegmentButtons)
				{
					Button->RemoveFromParent();
					if (UHorizontalBoxSlot* ButtonSlot = SegmentRow->AddChildToHorizontalBox(Button))
					{
						ButtonSlot->SetPadding(FMargin(0.f, 0.f, Button == Btn_2DView ? 4.f : 0.f, 0.f));
						ButtonSlot->SetVerticalAlignment(VAlign_Center);
					}
				}
				SegmentRow->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				Segment->SetBrush(MakeGroupFrameBrush());
				Segment->SetPadding(FMargin(2.f));
				Segment->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				Segment->SetContent(SegmentRow);
				if (UHorizontalBoxSlot* SegmentSlot = Cast<UHorizontalBoxSlot>(ViewButtons->InsertChildAt(Index, Segment)))
				{
					SegmentSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
					SegmentSlot->SetVerticalAlignment(VAlign_Center);
				}
			}
		}
		UWidget* Save = WidgetTree->FindWidget(PlannerPanelNames::SaveButton);
		UWidget* Spacer = WidgetTree->FindWidget(PlannerPanelNames::ViewSpacer);
		if (Save && Spacer && Save->GetParent() == ViewButtons && Spacer->GetParent() == ViewButtons
			&& ViewButtons->GetChildIndex(Save) < ViewButtons->GetChildIndex(Spacer))
		{
			const UHorizontalBoxSlot* OldSlot = Cast<UHorizontalBoxSlot>(Save->Slot);
			const FMargin SavePadding = OldSlot ? OldSlot->GetPadding() : FMargin(0.f);
			const EVerticalAlignment SaveVAlign = OldSlot ? OldSlot->GetVerticalAlignment() : VAlign_Center;
			Save->RemoveFromParent(); // its Blueprint OnClicked binding stays with the button
			if (UHorizontalBoxSlot* SaveSlot = Cast<UHorizontalBoxSlot>(ViewButtons->InsertChildAt(ViewButtons->GetChildIndex(Spacer) + 1, Save)))
			{
				SaveSlot->SetPadding(SavePadding);
				SaveSlot->SetVerticalAlignment(SaveVAlign);
			}
		}
	}
	if (BtnClearLayout)
	{
		BtnClearLayout->SetVisibility(ESlateVisibility::Collapsed); // «Очистить план» on the «Планировка» page replaces it
	}

	bCategoryLayoutBuilt = true;
	return true;
}

void URoomPlannerWidget::CreateStatusStrip()
{
	using namespace PlannerPanelLayout;
	if (StatusStrip || !WidgetTree) return;
	UCanvasPanel* Root = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (!Root || (!HorizontalBox_3 && !TxtOperationMessage)) return;

	USizeBox* Limit = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("StatusStrip"));
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("StatusStripColumn"));
	if (!Limit || !Column) return;
	Limit->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Column->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Limit->SetMaxDesiredWidth(1200.f);
	Limit->SetContent(Column);

	// Where the hint bar was in the root canvas: the selection labels and the live wall length still paint over the strip.
	const int32 StripIndex = (HorizontalBox_3 && HorizontalBox_3->GetParent() == Root) ? Root->GetChildIndex(HorizontalBox_3)
		: (SelectionLabelPanel && SelectionLabelPanel->GetParent() == Root ? Root->GetChildIndex(SelectionLabelPanel) : Root->GetChildrenCount());

	// The hint bar (hint + «X») keeps its own visibility rule (BtnHelp / BtnHideHelp). It fills the strip's width and the hint's pill
	// takes the rest of its row, so a long hint wraps at the strip's limit instead of running off the screen with its «X».
	if (HorizontalBox_3)
	{
		HorizontalBox_3->RemoveFromParent();
		AddToVerticalBox(Column, HorizontalBox_3, FMargin(0.f), HAlign_Fill);
		if (TxtGuidanceHint)
		{
			// An explicit wrap width (UpdateStatusStripPlacement keeps it at the free width): auto-wrap inside this shrink-to-content
			// strip would wrap every later hint at the narrowest width painted so far.
			TxtGuidanceHint->SetAutoWrapText(false);
			TxtGuidanceHint->SetWrapTextAt(1100.f);
			if (UHorizontalBoxSlot* PillSlot = TxtGuidanceHint->GetParent() ? Cast<UHorizontalBoxSlot>(TxtGuidanceHint->GetParent()->Slot) : nullptr)
			{
				PillSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}
		}
	}
	// Rejections: a white chip under the hint, visible in every state (it was inside the totals, hidden on an empty plan and in 3D).
	if (TxtOperationMessage)
	{
		UBorder* Chip = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MessageChip"));
		if (Chip)
		{
			if (const UBorder* LengthPanel = Cast<UBorder>(LiveLengthPanel))
			{
				Chip->SetBrush(LengthPanel->Background);
			}
			else
			{
				FSlateBrush White;
				White.TintColor = FSlateColor(FLinearColor::White);
				Chip->SetBrush(White);
			}
			Chip->SetPadding(FMargin(10.f, 6.f));
			TxtOperationMessage->SetAutoWrapText(false); // explicit width, as the hint (see UpdateStatusStripPlacement)
			TxtOperationMessage->SetWrapTextAt(1180.f);
			TxtOperationMessage->RemoveFromParent();
			Chip->SetContent(TxtOperationMessage);
			Chip->SetVisibility(ESlateVisibility::Collapsed);
			AddToVerticalBox(Column, Chip, FMargin(0.f, 6.f, 0.f, 0.f), HAlign_Center);
			MessageChip = Chip;
		}
	}

	// Centred over the free plan area beside the panel: viewport centre + half the panel width.
	if (UCanvasPanelSlot* StripSlot = Cast<UCanvasPanelSlot>(Root->InsertChildAt(FMath::Clamp(StripIndex, 0, Root->GetChildrenCount()), Limit)))
	{
		StripSlot->SetAnchors(FAnchors(0.5f, 0.f));
		StripSlot->SetAlignment(FVector2D(0.5f, 0.f));
		StripSlot->SetAutoSize(true);
		StripSlot->SetPosition(FVector2D(GetPanelWidth() * 0.5f, 20.f));
	}
	StatusStrip = Limit;
}

void URoomPlannerWidget::BuildContextBlock()
{
	using namespace PlannerPanelLayout;
	if (bContextBlockBuilt || !WidgetTree) return;
	bContextBlockBuilt = true;
	UVerticalBox* Selection = WidgetTree->FindWidget<UVerticalBox>(PlannerPanelNames::SelectionBlock);

	// The selection's title in the panel's ink (#3E4C5E, as the totals): it names what every control below acts on.
	if (TxtSelectionTitle)
	{
		FSlateFontInfo TitleFont = TxtSelectionTitle->GetFont();
		TitleFont.Size = 13.5f;
		TxtSelectionTitle->SetFont(TitleFont);
		if (TxtFloorArea) TxtSelectionTitle->SetColorAndOpacity(TxtFloorArea->GetColorAndOpacity());
	}

	// One-line size summary under the title where the size fields are not shown («Отделка», 3D).
	if (Selection && SelectionHeaderRow && SelectionHeaderRow->GetParent() == Selection)
	{
		TxtContextSummary = MakePanelText(FString(), TEXT("TxtContextSummary"), false);
		if (TxtContextSummary)
		{
			TxtContextSummary->SetVisibility(ESlateVisibility::Collapsed);
			InsertIntoVerticalBox(Selection, Selection->GetChildIndex(SelectionHeaderRow) + 1, TxtContextSummary, FMargin(0.f, 2.f, 0.f, 4.f));
		}
	}

	// «Добавить на стену» names the door / window creation blocks (they are for the selected wall, not the selected opening).
	if (Selection && Border_AddDoor && Border_AddDoor->GetParent() == Selection)
	{
		AddToWallCaption = MakePanelText(TEXT("Добавить на стену"), TEXT("AddToWallCaption"), true);
		if (AddToWallCaption)
		{
			AddToWallCaption->SetVisibility(ESlateVisibility::Collapsed);
			InsertIntoVerticalBox(Selection, Selection->GetChildIndex(Border_AddDoor), AddToWallCaption, FMargin(0.f, 6.f, 0.f, 0.f));
		}
	}

	// The creation fields in the order their captions read: door «ширина · высота», window «ширина · высота · высота от пола».
	// (Only the order changes; each field keeps its own commit binding.)
	MoveBeforeInRow(EditableTxtOpeningWidth, EditableTxtOpeningHeight);
	if (EditableTxtOpeningWidth && EditableTxtOpeningHeight)
	{
		EditableTxtOpeningWidth->SetMinDesiredWidth(FMath::Max(EditableTxtOpeningWidth->GetMinimumDesiredWidth(), EditableTxtOpeningHeight->GetMinimumDesiredWidth()));
	}
	MoveBeforeInRow(EditableTxtOpeningHeight_1, EditableTxtOpeningSillHeight);

	// The door / window style picker: the WBP has no StyleRow, so the built-in styles never showed. A caption and a wrap box after
	// the swing row; the caption is a sibling (RefreshStyleRow clears the box's children) with its own visibility.
	if (!StyleRow && Selection && SwingRow && SwingRow->GetParent() == Selection)
	{
		StyleCaption = MakePanelText(TEXT("Стиль"), TEXT("StyleCaption"), true);
		UWrapBox* Styles = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("StyleRow"));
		if (StyleCaption && Styles)
		{
			const int32 Index = Selection->GetChildIndex(SwingRow) + 1;
			StyleCaption->SetVisibility(ESlateVisibility::Collapsed);
			Styles->SetVisibility(ESlateVisibility::Collapsed);
			Styles->SetInnerSlotPadding(FVector2D(4.f, 4.f));
			InsertIntoVerticalBox(Selection, Index, StyleCaption, FMargin(0.f, 0.f, 0.f, 4.f));
			InsertIntoVerticalBox(Selection, Index + 1, Styles, FMargin(0.f, 0.f, 0.f, 8.f));
			StyleRow = Styles;
		}
	}

	// Totals: the caption on the left, the value on the right (they ran together: «Площадь пола0.00 м²»).
	UTextBlock* TotalValues[] = { TxtFloorArea.Get(), TxtPerimeter.Get() };
	for (UTextBlock* Value : TotalValues)
	{
		UHorizontalBox* TotalRow = Value ? Cast<UHorizontalBox>(Value->GetParent()) : nullptr;
		if (!TotalRow || TotalRow->GetChildrenCount() < 2) continue;
		for (int32 Index = 0; Index < TotalRow->GetChildrenCount(); ++Index)
		{
			UWidget* Child = TotalRow->GetChildAt(Index);
			UHorizontalBoxSlot* ChildSlot = Child ? Cast<UHorizontalBoxSlot>(Child->Slot) : nullptr;
			if (!ChildSlot) continue;
			if (Child == Value)
			{
				ChildSlot->SetHorizontalAlignment(HAlign_Right);
				ChildSlot->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
			}
			else if (Index == 0)
			{
				ChildSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}
		}
	}

	// Enter in the size fields applies them, as «Применить» does (the wall length stays the centreline length).
	UEditableTextBox* SizeFields[] = { EditableTxtProp1.Get(), EditableTxtProp2.Get(), EditableTxtProp3.Get() };
	for (UEditableTextBox* Field : SizeFields)
	{
		if (Field) Field->OnTextCommitted.AddUniqueDynamic(this, &URoomPlannerWidget::OnInspectorFieldCommitted);
	}
}

void URoomPlannerWidget::OnInspectorFieldCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		OnApplyPropertiesClicked();
	}
}

bool URoomPlannerWidget::IsContextEditorVisible() const
{
	return CurrentViewMode == ERoomPlannerViewMode::View2D && (!bCategoryLayoutBuilt || Active2DCategory != EPlannerPanelCategory::Finish);
}

void URoomPlannerWidget::UpdateCreationBlocksVisibility()
{
	using namespace PlannerPanelLayout;
	// For a selected wall only: with a door or window selected the same blocks (and captions) repeated the opening's own fields.
	const bool bShow = PlannerManager && IsContextEditorVisible() && PlannerManager->GetSelectionKind() == EPlannerSelectionKind::Wall;
	UWidget* Blocks[] = { BtnAddDoor.Get(), BtnAddWindow.Get(), EditableTxtOpeningWidth.Get(), EditableTxtOpeningHeight.Get(),
		EditableTxtOpeningWidth_1.Get(), EditableTxtOpeningHeight_1.Get(), EditableTxtOpeningSillHeight.Get(), Border_AddDoor.Get(), Border_AddWindow.Get() };
	for (UWidget* Block : Blocks)
	{
		ShowIf(Block, bShow, ESlateVisibility::Visible);
	}
	ShowIf(AddToWallCaption, bShow, ESlateVisibility::HitTestInvisible);
}

FString URoomPlannerWidget::ComposeContextSummary(const TArray<FPlannerDimensionLabel>& Labels) const
{
	auto Find = [&Labels](const TCHAR* Key) -> const FPlannerDimensionLabel*
	{
		return Labels.FindByPredicate([Key](const FPlannerDimensionLabel& L) { return L.Key == Key; });
	};
	if (!PlannerManager) return FString();
	const bool bIs2D = (CurrentViewMode == ERoomPlannerViewMode::View2D);
	switch (PlannerManager->GetSelectionKind())
	{
	case EPlannerSelectionKind::Wall:
		// The selected face's clear length (the dimension lines' value), named as such: the «Планировка» field is the centreline length.
		if (const FPlannerDimensionLabel* Length = Find(TEXT("length")))
		{
			return FString::Printf(TEXT("%s: %s"), PlannerManager->IsSelectedWallFaceInterior() ? TEXT("Длина по стороне в комнату") : TEXT("Длина по наружной стороне"), *Length->Text);
		}
		return FString();
	case EPlannerSelectionKind::Opening:
	{
		const FPlannerDimensionLabel* Width = Find(TEXT("width"));
		const FPlannerDimensionLabel* Height = Find(TEXT("height"));
		if (Width && Height) return FString::Printf(TEXT("%s × %s"), *Width->Text, *Height->Text);
		return Width ? Width->Text : FString();
	}
	case EPlannerSelectionKind::Object:
	case EPlannerSelectionKind::CabinetSet:
		if (const FPlannerDimensionLabel* Size = Find(TEXT("size"))) return Size->Text;
		return FString();
	case EPlannerSelectionKind::Floor:
	case EPlannerSelectionKind::Ceiling:
	case EPlannerSelectionKind::Baseboard:
	{
		// In 2D the totals already show the selected room's floor area. In 3D: the room's own numbers (the manager has no labels
		// for a ceiling or a baseboard).
		FRoomData Room;
		if (bIs2D || !PlannerManager->GetRoomData(PlannerManager->SelectedRoomID, Room)) return FString();
		if (PlannerManager->GetSelectionKind() == EPlannerSelectionKind::Baseboard)
		{
			double PerimeterCm = 0.0;
			for (int32 Index = 0; Index < Room.NetFloorPolygon.Num(); ++Index)
			{
				PerimeterCm += FVector2D::Distance(Room.NetFloorPolygon[Index], Room.NetFloorPolygon[(Index + 1) % Room.NetFloorPolygon.Num()]);
			}
			return PerimeterCm > 0.0 ? FString::Printf(TEXT("Периметр комнаты: %.2f м"), PerimeterCm / 100.0) : FString();
		}
		return FString::Printf(TEXT("Площадь: %.2f м²"), Room.AreaM2);
	}
	default:
		return FString();
	}
}

void URoomPlannerWidget::UpdateContextSummary(const TArray<FPlannerDimensionLabel>* Labels)
{
	if (!TxtContextSummary) return;
	// While a corner is dragged the labels are the dragged corner's walls, not the selection's: keep the line until the drop.
	if (PlannerManager && PlannerManager->IsNodeDragActive()) return;
	const bool bIs2D = (CurrentViewMode == ERoomPlannerViewMode::View2D);
	const bool bWanted = PlannerManager && PlannerManager->GetSelectionKind() != EPlannerSelectionKind::None
		&& (!bIs2D || (bCategoryLayoutBuilt && Active2DCategory == EPlannerPanelCategory::Finish));
	FString Summary;
	if (bWanted)
	{
		Summary = Labels ? ComposeContextSummary(*Labels) : ComposeContextSummary(PlannerManager->GetSelectionDimensionLabels());
	}
	if (!TxtContextSummary->GetText().ToString().Equals(Summary, ESearchCase::CaseSensitive))
	{
		TxtContextSummary->SetText(FText::FromString(Summary));
	}
	PlannerPanelLayout::ShowIf(TxtContextSummary, !Summary.IsEmpty(), ESlateVisibility::HitTestInvisible);
}

void URoomPlannerWidget::CreateMissingViewOptions()
{
	using namespace PlannerPanelLayout;
	// Only when the designer has none of them: a designed row always wins.
	if (!WidgetTree || BtnToggleCeiling || Btn_ToggleCeiling || BtnCeiling || BtnDoorsOpen || BtnDoorsClose || ViewOptionsRow) return;
	UVerticalBox* Sections = WidgetTree->FindWidget<UVerticalBox>(PlannerPanelNames::PanelSections);
	UWidget* ViewRowWidget = WidgetTree->FindWidget(PlannerPanelNames::ViewRow);
	const UButton* CommandStyle = BtnAddDoor ? BtnAddDoor.Get() : (BtnPresetRoom ? BtnPresetRoom.Get() : BtnDrawWallTool.Get());
	if (!Sections || !ViewRowWidget || ViewRowWidget->GetParent() != Sections || !CommandStyle) return;

	UWrapBox* Row = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("ViewOptionsRow"));
	UButton* Ceiling = MakeRuntimeButton(CommandStyle, TEXT("Потолок"), TEXT("BtnToggleCeiling"), FString(), BtnPresetRoom);
	UButton* DoorsOpen = MakeRuntimeButton(CommandStyle, TEXT("Открыть"), TEXT("BtnDoorsOpen"), FString(), BtnPresetRoom);
	UButton* DoorsClose = MakeRuntimeButton(CommandStyle, TEXT("Закрыть"), TEXT("BtnDoorsClose"), FString(), BtnPresetRoom);
	UTextBlock* DoorsCaption = MakePanelText(TEXT("Двери"), TEXT("DoorsCaption"), true);
	if (!Row || !Ceiling || !DoorsOpen || !DoorsClose || !DoorsCaption) return;

	// Compact: the row fits the column (it wraps if a font makes it wider).
	UButton* Compact[] = { Ceiling, DoorsOpen, DoorsClose };
	for (UButton* Button : Compact)
	{
		FButtonStyle Style = Button->GetStyle();
		Style.SetNormalPadding(FMargin(6.f, 1.5f));
		Style.SetPressedPadding(FMargin(6.f, 1.5f));
		Button->SetStyle(Style);
	}
	Row->SetInnerSlotPadding(FVector2D(6.f, 4.f));
	// One line in the 348 px column: the row only exists in 3D, so it needs no "3D" caption of its own.
	UWidget* Items[] = { Ceiling, DoorsCaption, DoorsOpen, DoorsClose };
	for (UWidget* Item : Items)
	{
		if (UWrapBoxSlot* ItemSlot = Row->AddChildToWrapBox(Item))
		{
			ItemSlot->SetVerticalAlignment(VAlign_Center);
		}
	}
	Row->SetVisibility(ESlateVisibility::Collapsed);
	InsertIntoVerticalBox(Sections, Sections->GetChildIndex(ViewRowWidget) + 1, Row, FMargin(16.f, 0.f, 16.f, 6.f));

	// NativeConstruct wires them (OnToggleCeilingClicked, OnDoorsOpen/CloseClicked, their tooltips); the view rules show them in 3D.
	BtnToggleCeiling = Ceiling;
	BtnDoorsOpen = DoorsOpen;
	BtnDoorsClose = DoorsClose;
	ViewOptionsRow = Row;
}

void URoomPlannerWidget::MakePanelScrollable()
{
	if (BodyScroll || !bCategoryLayoutBuilt || !WidgetTree || !CategoryStrip) return;
	UWidget* Panel = WidgetTree->FindWidget(PlannerPanelNames::LeftPanel);
	UCanvasPanelSlot* PanelSlot = Panel ? Cast<UCanvasPanelSlot>(Panel->Slot) : nullptr;
	UVerticalBox* Sections = WidgetTree->FindWidget<UVerticalBox>(PlannerPanelNames::PanelSections);
	if (!PanelSlot || !Sections || CategoryStrip->GetParent() != Sections) return;
	UScrollBox* Body = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("BodyScroll"));
	if (!Body) return;

	// Full height: stretched top to bottom, same x and width (the right edge the other tests and the input boundary rely on stays).
	const FVector2D Position = PanelSlot->GetPosition();
	const FVector2D Size = PanelSlot->GetSize();
	PanelSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 1.f));
	PanelSlot->SetOffsets(FMargin(Position.X, 0.f, Size.X, 0.f));

	// Everything after the tab row (and the line under it) moves into the scroll box, keeping its padding and alignment.
	int32 FirstBodyIndex = Sections->GetChildIndex(CategoryStrip) + 1;
	if (UWidget* Next = Sections->GetChildAt(FirstBodyIndex))
	{
		if (Next->GetName().StartsWith(PlannerPanelNames::SeparatorPrefix)) ++FirstBodyIndex;
	}
	TArray<UWidget*> Moving;
	for (int32 Index = FirstBodyIndex; Index < Sections->GetChildrenCount(); ++Index)
	{
		Moving.Add(Sections->GetChildAt(Index));
	}
	for (UWidget* Child : Moving)
	{
		if (!Child) continue;
		const PlannerPanelLayout::FVerticalSlotLayout Layout = PlannerPanelLayout::ReadVerticalSlot(Child);
		Child->RemoveFromParent();
		if (UScrollBoxSlot* BodySlot = Cast<UScrollBoxSlot>(Body->AddChild(Child)))
		{
			BodySlot->SetPadding(Layout.Padding);
			BodySlot->SetHorizontalAlignment(Layout.bValid ? Layout.HAlign : HAlign_Fill);
		}
	}
	Body->SetVisibility(ESlateVisibility::Visible); // wheel scrolling anywhere over the body
	Body->SetAnimateWheelScrolling(true);
	if (UVerticalBoxSlot* BodySlot = Sections->AddChildToVerticalBox(Body))
	{
		BodySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	BodyScroll = Body;
}

void URoomPlannerWidget::CreateFloatingContextBar()
{
	if (FloatingContextBar || !WidgetTree) return;
	UCanvasPanel* Root = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	const UButton* CommandStyle = BtnAddDoor ? BtnAddDoor.Get() : BtnPresetRoom.Get();
	if (!Root || !CommandStyle) return;
	UBorder* Bar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("FloatingContextBar"));
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("FloatingContextButtons"));
	BtnFloatingRotateLeft = MakeRuntimeButton(CommandStyle, TEXT("↺ 15°"), TEXT("BtnFloatingRotateLeft"), TEXT("Повернуть объект на 15° против часовой стрелки"), BtnPresetRoom);
	BtnFloatingRotateRight = MakeRuntimeButton(CommandStyle, TEXT("15° ↻"), TEXT("BtnFloatingRotateRight"), TEXT("Повернуть объект на 15° по часовой стрелке"), BtnPresetRoom);
	BtnFloatingFinish = MakeRuntimeButton(CommandStyle, TEXT("Отделка"), TEXT("BtnFloatingFinish"), TEXT("Открыть «Отделку» для выбранного"), BtnPresetRoom);
	if (!Bar || !Row || !BtnFloatingRotateLeft || !BtnFloatingRotateRight || !BtnFloatingFinish) return;
	// Delete: the trash icon of the Context header (its designed icon style, no label).
	if (BtnDeleteTool)
	{
		BtnFloatingDelete = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("BtnFloatingDelete"));
		if (BtnFloatingDelete)
		{
			BtnFloatingDelete->SetStyle(BtnDeleteTool->GetStyle());
			BtnFloatingDelete->SetBackgroundColor(FLinearColor::White); // the light #BBBBBB icon on the dark bar (idle near-black would hide it)
			BtnFloatingDelete->SetToolTipText(FText::FromString(TEXT("Удалить выбранный элемент (Delete / Backspace)")));
		}
	}
	UButton* Buttons[] = { BtnFloatingDelete.Get(), BtnFloatingRotateLeft.Get(), BtnFloatingRotateRight.Get(), BtnFloatingFinish.Get() };
	for (UButton* Button : Buttons)
	{
		if (!Button) continue;
		if (UHorizontalBoxSlot* ButtonSlot = Row->AddChildToHorizontalBox(Button))
		{
			ButtonSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
			ButtonSlot->SetVerticalAlignment(VAlign_Center);
		}
	}
	Row->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	// The hint pill's dark slate (#3E4753 at 70 %).
	if (const UBorder* Pill = TxtGuidanceHint ? Cast<UBorder>(TxtGuidanceHint->GetParent()) : nullptr)
	{
		Bar->SetBrush(Pill->Background);
	}
	else
	{
		FSlateBrush Slate;
		Slate.TintColor = FSlateColor(FLinearColor::FromSRGBColor(FColor(0x3E, 0x47, 0x53, 0xB3)));
		Bar->SetBrush(Slate);
	}
	Bar->SetPadding(FMargin(6.f, 4.f, 2.f, 4.f));
	Bar->SetContent(Row);
	Bar->SetVisibility(ESlateVisibility::Collapsed);
	// Under the selection labels (they stay readable over it).
	const int32 Index = (SelectionLabelPanel && SelectionLabelPanel->GetParent() == Root) ? Root->GetChildIndex(SelectionLabelPanel) : Root->GetChildrenCount();
	if (UCanvasPanelSlot* BarSlot = Cast<UCanvasPanelSlot>(Root->InsertChildAt(Index, Bar)))
	{
		BarSlot->SetAnchors(FAnchors(0.f, 0.f));
		BarSlot->SetAlignment(FVector2D(0.f, 0.f));
		BarSlot->SetAutoSize(true);
	}
	if (BtnFloatingDelete) BtnFloatingDelete->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnFloatingDeleteClicked);
	BtnFloatingRotateLeft->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnRotateLeftClicked);
	BtnFloatingRotateRight->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnRotateRightClicked);
	BtnFloatingFinish->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnFloatingFinishClicked);
	FloatingContextBar = Bar;
}

void URoomPlannerWidget::UpdateFloatingContextBar(const TArray<FPlannerDimensionLabel>* Labels)
{
	using namespace PlannerPanelLayout;
	if (!FloatingContextBar) return;
	const EPlannerSelectionKind Kind = PlannerManager ? PlannerManager->GetSelectionKind() : EPlannerSelectionKind::None;
	const bool bBusy = bIsWidgetDrawingWall || bIsWidgetDraggingNode || !WidgetDraggedObjectID.IsEmpty()
		|| (PlannerManager && (PlannerManager->IsWallDrawingActive() || PlannerManager->IsNodeDragActive()));
	const FPlannerDimensionLabel* Anchor = nullptr;
	if (bShowFloatingContextBar && Labels && Kind != EPlannerSelectionKind::None && !bBusy && CurrentViewMode == ERoomPlannerViewMode::View2D)
	{
		const TCHAR* AnchorKeys[] = { TEXT("width"), TEXT("length"), TEXT("area"), TEXT("size") };
		for (const TCHAR* Key : AnchorKeys)
		{
			Anchor = Labels->FindByPredicate([Key](const FPlannerDimensionLabel& L) { return L.Key == Key; });
			if (Anchor) break;
		}
	}
	const bool bDeletable = Kind == EPlannerSelectionKind::Wall || Kind == EPlannerSelectionKind::Opening
		|| Kind == EPlannerSelectionKind::Object || Kind == EPlannerSelectionKind::CabinetSet;
	const bool bRotatable = Kind == EPlannerSelectionKind::Object || Kind == EPlannerSelectionKind::CabinetSet;
	const bool bFinishable = PlannerManager && PlannerManager->CanApplyFinishToSelection() && GetActiveCategory() != EPlannerPanelCategory::Finish;
	FVector2D Viewport(1920.f, 1080.f);
	if (WidgetTree && WidgetTree->RootWidget && WidgetTree->RootWidget->GetCachedGeometry().GetLocalSize().X > 1.f)
	{
		Viewport = WidgetTree->RootWidget->GetCachedGeometry().GetLocalSize();
	}
	const float LeftLimit = GetPanelWidth() + ((bFinishCatalogBesidePanel && IsSideCatalogOpen()) ? GetPanelWidth() : 0.f);
	// "On screen" from the projection only means in front of the camera: a selection scrolled out of the free plan area gets no bar
	// (it would be pinned to an edge, far from the object).
	const bool bAnchorVisible = Anchor && Anchor->bOnScreen && Anchor->ScreenPosition.X >= LeftLimit && Anchor->ScreenPosition.X <= Viewport.X
		&& Anchor->ScreenPosition.Y >= 0.f && Anchor->ScreenPosition.Y <= Viewport.Y;
	if (!bAnchorVisible || !(bDeletable || bRotatable || bFinishable))
	{
		ShowIf(FloatingContextBar, false, ESlateVisibility::Visible);
		return;
	}
	ShowIf(BtnFloatingDelete, bDeletable, ESlateVisibility::Visible);
	ShowIf(BtnFloatingRotateLeft, bRotatable, ESlateVisibility::Visible);
	ShowIf(BtnFloatingRotateRight, bRotatable, ESlateVisibility::Visible);
	ShowIf(BtnFloatingFinish, bFinishable, ESlateVisibility::Visible);
	const bool bTurnable = bRotatable && !PlannerManager->IsSelectionWallAttached();
	EnableIf(BtnFloatingRotateLeft, bTurnable);
	EnableIf(BtnFloatingRotateRight, bTurnable);
	ShowIf(FloatingContextBar, true, ESlateVisibility::Visible);
	FloatingContextBar->ForceLayoutPrepass(); // this frame's button set, not last frame's size
	const FVector2D Position = PlannerPanelRules::PlaceContextBar(Anchor->ScreenPosition, FloatingContextBar->GetDesiredSize(), Viewport, LeftLimit);
	if (UCanvasPanelSlot* BarSlot = Cast<UCanvasPanelSlot>(FloatingContextBar->Slot))
	{
		if (!BarSlot->GetPosition().Equals(Position, 0.5f)) BarSlot->SetPosition(Position);
	}
}

void URoomPlannerWidget::OnFloatingDeleteClicked() { DeleteSelected(); }
void URoomPlannerWidget::OnFloatingFinishClicked() { SetActiveCategory(EPlannerPanelCategory::Finish); }

void URoomPlannerWidget::ClearStrayRootTooltip()
{
	// The designed root carries a «Назад» tooltip: it showed over every bare part of the panel.
	if (WidgetTree && WidgetTree->RootWidget)
	{
		WidgetTree->RootWidget->SetToolTipText(FText::GetEmpty());
	}
}

// ═══════════════════════════════════════════════════════════════════════════════
// Category state
// ═══════════════════════════════════════════════════════════════════════════════

void URoomPlannerWidget::SetActiveCategory(EPlannerPanelCategory Category)
{
	CancelConfirmation(); // a tab change answers "no"
	if (Category != EPlannerPanelCategory::Finish && CurrentViewMode == ERoomPlannerViewMode::View2D)
	{
		CloseFinishFlyouts(); // the paint / tile catalogs belong to «Отделка»
	}
	if (!bCategoryLayoutBuilt)
	{
		// The one-row toolbar has only the catalog section.
		bCatalogOpen = (Category == EPlannerPanelCategory::Catalog);
		ApplyCatalogSectionVisibility();
		return;
	}
	// In 3D «Отделка» is the shown, active tab: clicking it changes nothing (the 2D tab stays remembered).
	if (Category == EPlannerPanelCategory::Finish && CurrentViewMode != ERoomPlannerViewMode::View2D)
	{
		return;
	}
	if (Category == EPlannerPanelCategory::Catalog && Active2DCategory != EPlannerPanelCategory::Catalog)
	{
		CategoryBeforeCatalog = Active2DCategory;
	}
	Active2DCategory = Category;
	bCatalogOpen = (Category == EPlannerPanelCategory::Catalog);

	EnforceCategoryTool();
	ApplyCategoryVisibility();
	UpdateDynamicPropertiesPanel();
	UpdateGuidanceHintText();
}

void URoomPlannerWidget::EnforceCategoryTool()
{
	if (!bCategoryLayoutBuilt || !PlannerManager || CurrentViewMode != ERoomPlannerViewMode::View2D) return;
	if (PlannerPanelRules::SwitchesDrawToSelect(Active2DCategory, PlannerManager->GetWallCount() > 0,
		PlannerManager->ActiveToolMode == EPlannerToolMode::DrawWall))
	{
		SetToolMode(EPlannerToolMode::Select);
	}
}

EPlannerPanelCategory URoomPlannerWidget::GetActiveCategory() const
{
	if (CurrentViewMode != ERoomPlannerViewMode::View2D) return EPlannerPanelCategory::Finish;
	if (!bCategoryLayoutBuilt) return bCatalogOpen ? EPlannerPanelCategory::Catalog : EPlannerPanelCategory::Layout;
	return Active2DCategory;
}

UButton* URoomPlannerWidget::GetCategoryButton(EPlannerPanelCategory Category) const
{
	switch (Category)
	{
	case EPlannerPanelCategory::Layout:  return BtnCategoryLayout;
	case EPlannerPanelCategory::Catalog: return BtnCatalogToggle;
	case EPlannerPanelCategory::Finish:  return BtnCategoryFinish;
	default: return nullptr;
	}
}

void URoomPlannerWidget::RefreshPanelState()
{
	bPlanEmptyCached = IsPlanEmpty();
	if (PlannerManager)
	{
		LastTickWallCount = PlannerManager->GetWallCount();
		const bool bDrawing = bIsWidgetDrawingWall || PlannerManager->IsWallDrawingActive();
		if (bCategoryLayoutBuilt && PlannerPanelRules::ReturnsToLayout(Active2DCategory, CurrentViewMode == ERoomPlannerViewMode::View2D, bDrawing))
		{
			SetActiveCategory(EPlannerPanelCategory::Layout);
		}
	}
	EnforceCategoryTool();
	ApplyCategoryVisibility();
	UpdateToolModeButtonStyles();
	UpdateDynamicPropertiesPanel();
	UpdateSummaryStatsUI();
	UpdateStatusStripPlacement();
}

void URoomPlannerWidget::ApplyCategoryVisibility()
{
	using namespace PlannerPanelLayout;
	if (bCategoryLayoutBuilt)
	{
		FPlannerPanelInputs Inputs;
		Inputs.bIs2D = (CurrentViewMode == ERoomPlannerViewMode::View2D);
		Inputs.Category = Active2DCategory;
		Inputs.bHasWalls = PlannerManager && PlannerManager->GetWallCount() > 0;
		const FPlannerPanelVisibility Rules = PlannerPanelRules::Compute(Inputs);

		ShowIf(CategoryStrip, true, ESlateVisibility::SelfHitTestInvisible);
		ShowIf(ToolsRowWidget, Rules.bToolsRow, ESlateVisibility::Visible);
		ShowIf(PageLayoutBody, Rules.bLayoutPage, ESlateVisibility::SelfHitTestInvisible);
		ShowIf(PageCatalogBody, Rules.bCatalogPage, ESlateVisibility::SelfHitTestInvisible);
		ShowIf(PageFinishBody, Rules.bFinishPage, ESlateVisibility::SelfHitTestInvisible);
		ShowIf(EmptyPlanNotice, Rules.bEmptyPlanNotice, ESlateVisibility::SelfHitTestInvisible);
		ShowIf(BtnCatalogToggle, true, ESlateVisibility::Visible);

		// 3D is picking and finishing only: the editing tabs stay in place, disabled, and say why.
		const bool bWasLayoutEnabled = BtnCategoryLayout && BtnCategoryLayout->GetIsEnabled();
		EnableIf(BtnCategoryLayout, Rules.bLayoutTabEnabled);
		EnableIf(BtnCatalogToggle, Rules.bCatalogTabEnabled);
		EnableIf(BtnCategoryFinish, Rules.bFinishTabEnabled);
		if (BtnCategoryLayout && bWasLayoutEnabled != Rules.bLayoutTabEnabled)
		{
			BtnCategoryLayout->SetToolTipText(FText::FromString(Rules.bLayoutTabEnabled
				? TEXT("Планировка: рисование стен, выбор элементов, шаблон комнаты") : TEXT("Доступно в 2D")));
			if (BtnCatalogToggle)
			{
				BtnCatalogToggle->SetToolTipText(FText::FromString(Rules.bCatalogTabEnabled
					? TEXT("Каталог: интерьер и тумбы (перетащите карточку на план). Нажмите ещё раз, чтобы свернуть") : TEXT("Доступно в 2D")));
			}
		}
		EnableIf(BtnClearPlan, !bPlanEmptyCached);
		UpdateCategoryButtonStyles();
	}
	ApplyCatalogSectionVisibility();
}

void URoomPlannerWidget::UpdateCategoryButtonStyles()
{
	const EPlannerPanelCategory Shown = GetActiveCategory();
	ApplyButtonState(BtnCategoryLayout, Shown == EPlannerPanelCategory::Layout ? EPanelButtonState::Selected : EPanelButtonState::Idle);
	ApplyButtonState(BtnCatalogToggle, Shown == EPlannerPanelCategory::Catalog ? EPanelButtonState::Selected : EPanelButtonState::Idle);
	ApplyButtonState(BtnCategoryFinish, Shown == EPlannerPanelCategory::Finish ? EPanelButtonState::Selected : EPanelButtonState::Idle);
}

void URoomPlannerWidget::OnCategoryLayoutClicked() { SetActiveCategory(EPlannerPanelCategory::Layout); }
void URoomPlannerWidget::OnCategoryFinishClicked() { SetActiveCategory(EPlannerPanelCategory::Finish); }

void URoomPlannerWidget::OnClearPlanClicked()
{
	RequestClearPlan();
}

void URoomPlannerWidget::OnNoticePresetClicked()
{
	SetActiveCategory(EPlannerPanelCategory::Layout);
	RequestPresetRoom();
}

void URoomPlannerWidget::OnPresetRoomRequested()
{
	RequestPresetRoom();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Replace actions: ask first
// ═══════════════════════════════════════════════════════════════════════════════

void URoomPlannerWidget::CreateConfirmBar(UVerticalBox* LayoutPage)
{
	using namespace PlannerPanelLayout;
	if (ConfirmBar || !WidgetTree || !LayoutPage || !BtnPresetRoom) return;
	UVerticalBox* Bar = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ConfirmBar"));
	UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ConfirmButtons"));
	ConfirmQuestion = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ConfirmQuestion"));
	// Commands in our slate button look (the "Добавить дверь" style: its hover stays slate), labelled like «4×4 м».
	const UButton* CommandStyle = BtnAddDoor ? BtnAddDoor.Get() : BtnPresetRoom.Get();
	BtnConfirmAccept = MakeRuntimeButton(CommandStyle, TEXT("Да, заменить"), TEXT("BtnConfirmAccept"), FString(), BtnPresetRoom);
	BtnConfirmCancel = MakeRuntimeButton(CommandStyle, TEXT("Отмена"), TEXT("BtnConfirmCancel"), TEXT("Оставить план как есть"), BtnPresetRoom);
	if (!Bar || !Buttons || !ConfirmQuestion || !BtnConfirmAccept || !BtnConfirmCancel) return;

	// The question in the panel's warning red (the rejection messages' colour), wrapping.
	if (TxtOperationMessage)
	{
		ConfirmQuestion->SetFont(TxtOperationMessage->GetFont());
		ConfirmQuestion->SetColorAndOpacity(TxtOperationMessage->GetColorAndOpacity());
	}
	else
	{
		ConfirmQuestion->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 13.5f));
		ConfirmQuestion->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.f, 0.f)));
	}
	ConfirmQuestion->SetAutoWrapText(true);
	ConfirmQuestion->SetVisibility(ESlateVisibility::HitTestInvisible);
	Buttons->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (UHorizontalBoxSlot* AcceptSlot = Buttons->AddChildToHorizontalBox(BtnConfirmAccept))
	{
		AcceptSlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
	}
	Buttons->AddChildToHorizontalBox(BtnConfirmCancel);
	AddToVerticalBox(Bar, ConfirmQuestion, FMargin(0.f, 6.f, 0.f, 6.f));
	AddToVerticalBox(Bar, Buttons, FMargin(0.f), HAlign_Right);
	Bar->SetVisibility(ESlateVisibility::Collapsed);
	AddToVerticalBox(LayoutPage, Bar, FMargin(0.f, 4.f, 0.f, 0.f));
	BtnConfirmAccept->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnConfirmAcceptClicked);
	BtnConfirmCancel->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnConfirmCancelClicked);
	ConfirmBar = Bar;
}

void URoomPlannerWidget::ShowConfirmation(EPlannerConfirmAction Action)
{
	if (!ConfirmBar || !ConfirmQuestion || Action == EPlannerConfirmAction::None) return;
	PendingConfirmAction = Action;
	const bool bPreset = (Action == EPlannerConfirmAction::PresetRoom);
	ConfirmQuestion->SetText(FText::FromString(bPreset
		? TEXT("Заменить текущий план комнатой 4×4 м? Стены, проёмы, отделка и мебель будут удалены.")
		: TEXT("Очистить весь план? Стены, проёмы, отделка и мебель будут удалены.")));
	if (UTextBlock* AcceptLabel = BtnConfirmAccept ? Cast<UTextBlock>(BtnConfirmAccept->GetContent()) : nullptr)
	{
		AcceptLabel->SetText(FText::FromString(bPreset ? TEXT("Да, заменить") : TEXT("Да, очистить")));
	}
	if (BtnConfirmAccept)
	{
		BtnConfirmAccept->SetToolTipText(FText::FromString(bPreset ? TEXT("Удалить текущий план и построить комнату 4×4 м") : TEXT("Удалить весь план")));
	}
	ConfirmBar->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UpdateSeparatorLines();
}

void URoomPlannerWidget::CancelConfirmation()
{
	PendingConfirmAction = EPlannerConfirmAction::None;
	if (ConfirmBar && ConfirmBar->GetVisibility() != ESlateVisibility::Collapsed)
	{
		ConfirmBar->SetVisibility(ESlateVisibility::Collapsed);
		UpdateSeparatorLines();
	}
}

void URoomPlannerWidget::RequestPresetRoom()
{
	if (bConfirmDestructiveActions && ConfirmBar && !IsPlanEmpty())
	{
		if (bCategoryLayoutBuilt && Active2DCategory != EPlannerPanelCategory::Layout)
		{
			SetActiveCategory(EPlannerPanelCategory::Layout); // the question sits on the «Планировка» page
		}
		ShowConfirmation(EPlannerConfirmAction::PresetRoom);
		return;
	}
	RunPresetRoom();
}

void URoomPlannerWidget::RequestClearPlan()
{
	if (IsPlanEmpty())
	{
		CancelConfirmation();
		return;
	}
	if (bConfirmDestructiveActions && ConfirmBar)
	{
		if (bCategoryLayoutBuilt && Active2DCategory != EPlannerPanelCategory::Layout)
		{
			SetActiveCategory(EPlannerPanelCategory::Layout);
		}
		ShowConfirmation(EPlannerConfirmAction::ClearPlan);
		return;
	}
	OnClearLayoutClicked();
}

void URoomPlannerWidget::RunPresetRoom()
{
	CancelConfirmation();
	// The new room is ready to edit: once its walls arrive, the tool becomes «Выбрать» (HandleRoomPlannerUpdated).
	bSelectToolAfterPresetPending = bSelectToolAfterPreset;
	PresetRequestTime = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	OnPresetRoomClicked(); // unchanged: the server clears the layout and builds the room centred on the pawn
}

void URoomPlannerWidget::OnConfirmAcceptClicked()
{
	const EPlannerConfirmAction Action = PendingConfirmAction;
	CancelConfirmation();
	if (Action == EPlannerConfirmAction::PresetRoom)
	{
		RunPresetRoom();
	}
	else if (Action == EPlannerConfirmAction::ClearPlan)
	{
		OnClearLayoutClicked();
	}
}

void URoomPlannerWidget::OnConfirmCancelClicked()
{
	CancelConfirmation();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Tool, selection and panel agree
// ═══════════════════════════════════════════════════════════════════════════════

void URoomPlannerWidget::BeginSelectDroppedItem(EPlannerPlacementKind Kind, const FString& ItemID)
{
	PendingDrop = FPendingDropSelection();
	if (!PlannerManager || ItemID.IsEmpty() || Kind == EPlannerPlacementKind::None) return;
	PendingDrop.bActive = true;
	PendingDrop.Kind = Kind;
	PendingDrop.ItemID = ItemID;
	for (const FPlacedFurnitureData& Object : PlannerManager->GetPlacedObjects()) PendingDrop.KnownInstances.Add(Object.InstanceID);
	for (const FPlacedCabinetSetData& Set : PlannerManager->GetCabinetSets()) PendingDrop.KnownInstances.Add(Set.InstanceID);
	FVector Ground;
	if (DeprojectCursorToGround(Ground))
	{
		PendingDrop.bHasGround = true;
		PendingDrop.GroundXY = FVector2D(Ground.X, Ground.Y);
	}
	PendingDrop.StartTime = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
}

void URoomPlannerWidget::TrySelectDroppedItem()
{
	if (!PendingDrop.bActive || !PlannerManager) return;
	const double Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	if (Now - PendingDrop.StartTime > 3.0)
	{
		PendingDrop = FPendingDropSelection(); // refused, or someone else's update: forget it
		return;
	}
	// The new instance of the dropped item; with several (e.g. another user's), the one nearest the drop point.
	FString Best;
	double BestDistSq = TNumericLimits<double>::Max();
	auto Consider = [this, &Best, &BestDistSq](const FString& InstanceID, const FVector& Location)
	{
		if (PendingDrop.KnownInstances.Contains(InstanceID)) return;
		const double DistSq = PendingDrop.bHasGround ? FVector2D::DistSquared(PendingDrop.GroundXY, FVector2D(Location.X, Location.Y)) : 0.0;
		if (Best.IsEmpty() || DistSq < BestDistSq)
		{
			Best = InstanceID;
			BestDistSq = DistSq;
		}
	};
	if (PendingDrop.Kind == EPlannerPlacementKind::Object)
	{
		for (const FPlacedFurnitureData& Object : PlannerManager->GetPlacedObjects())
		{
			if (Object.AssetID == PendingDrop.ItemID) Consider(Object.InstanceID, Object.Location);
		}
	}
	else
	{
		for (const FPlacedCabinetSetData& Set : PlannerManager->GetCabinetSets())
		{
			if (Set.ProductID.ToString() == PendingDrop.ItemID) Consider(Set.InstanceID, Set.Location);
		}
	}
	if (Best.IsEmpty()) return; // not arrived yet
	// Another gesture has started meanwhile (a corner or an object being dragged, a wall being drawn): leave its selection alone.
	if (bIsWidgetDraggingNode || !WidgetDraggedObjectID.IsEmpty() || bIsWidgetDrawingWall
		|| PlannerManager->IsNodeDragActive() || PlannerManager->IsWallDrawingActive())
	{
		PendingDrop = FPendingDropSelection();
		return;
	}

	const EPlannerPlacementKind Kind = PendingDrop.Kind;
	PendingDrop = FPendingDropSelection();
	if (CurrentViewMode != ERoomPlannerViewMode::View2D) return;
	if (PlannerManager->ActiveToolMode != EPlannerToolMode::Select && PlannerManager->GetWallCount() > 0)
	{
		SetToolMode(EPlannerToolMode::Select);
	}
	if (Kind == EPlannerPlacementKind::Object)
	{
		PlannerManager->SelectPlacedObject(Best);
	}
	else
	{
		PlannerManager->SelectCabinetSet(Best);
	}
	UpdateDynamicPropertiesPanel();
}

void URoomPlannerWidget::ReconcileAfterViewSwitch(ERoomPlannerViewMode OldMode)
{
	CancelConfirmation();
	if (!PlannerManager || OldMode == CurrentViewMode || bOpeningPlanner) return;
	if (CurrentViewMode == ERoomPlannerViewMode::View3D)
	{
		// Placement is 2D-only: an armed click-to-place ends here (its cancel button is on the 2D «Каталог» page).
		if (PlannerManager->HasPendingPlacement())
		{
			CancelPlacement();
		}
		return;
	}

	// Back in 2D with the selection kept from 3D: the panel, the tool and the camera must agree with it.
	const EPlannerSelectionKind Kind = PlannerManager->GetSelectionKind();
	const bool bCatalogBeside = IsSideCatalogOpen();
	if (Kind == EPlannerSelectionKind::Ceiling)
	{
		PlannerManager->ClearAllSelection(); // the plan view has no ceiling to show it on
		ShowStatusMessage(TEXT("Потолок в 2D не показывается: выбор снят"));
	}
	// A pick kept from 3D, or a paint / tile catalog still open (e.g. after finishing a ceiling): carry on finishing on «Отделка»
	// with «Выбрать» (not the Draw tool).
	const bool bKept = PlannerManager->GetSelectionKind() != EPlannerSelectionKind::None;
	if ((bKept || bCatalogBeside) && bCategoryLayoutBuilt && PlannerManager->GetWallCount() > 0)
	{
		SetActiveCategory(EPlannerPanelCategory::Finish);
		if (PlannerManager->ActiveToolMode != EPlannerToolMode::Select)
		{
			SetToolMode(EPlannerToolMode::Select);
		}
	}
	else if (bCatalogBeside && GetActiveCategory() != EPlannerPanelCategory::Finish)
	{
		CloseFinishFlyouts(); // the catalogs belong to «Отделка»
	}
	// Whatever tab comes back, its tool rule holds (e.g. «Каталог» chosen in 3D with the Draw tool still on) ...
	EnforceCategoryTool();
	// ... and the 2D camera follows the final tool (a wall picked in 3D had switched it to the Select projection).
	if (AAwsTutorial_PlayerController* PC = GetPreviewController())
	{
		PC->UpdateRoomPlannerCameraToolMode(PlannerManager->ActiveToolMode);
	}
}

// ═══════════════════════════════════════════════════════════════════════════════
// «Отделка»: the room's surfaces, catalogs beside the panel
// ═══════════════════════════════════════════════════════════════════════════════

void URoomPlannerWidget::BuildRoomSurfaceChooser(UVerticalBox* FinishPage)
{
	using namespace PlannerPanelLayout;
	if (SurfaceChooser || !WidgetTree || !FinishPage) return;
	// Styled like the catalog's sub-tabs («Интерьер» / «Тумбы»): the same active / idle tab colours.
	const UButton* TabStyle = BtnCatalogInterior ? BtnCatalogInterior.Get() : (Btn_3DView ? Btn_3DView.Get() : BtnDrawWallTool.Get());
	if (!TabStyle) return;
	UVerticalBox* Chooser = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SurfaceChooser"));
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SurfaceButtons"));
	BtnSurfaceFloor = MakeRuntimeButton(TabStyle, TEXT("Пол"), TEXT("BtnSurfaceFloor"), TEXT("Отделка пола этой комнаты"));
	BtnSurfaceBaseboard = MakeRuntimeButton(TabStyle, TEXT("Плинтус"), TEXT("BtnSurfaceBaseboard"), TEXT("Отделка плинтуса этой комнаты"));
	BtnSurfaceCeiling = MakeRuntimeButton(TabStyle, TEXT("Потолок"), TEXT("BtnSurfaceCeiling"), TEXT("Отделка потолка этой комнаты (в 3D)"));
	if (!Chooser || !Row || !BtnSurfaceFloor || !BtnSurfaceBaseboard || !BtnSurfaceCeiling) return;
	UButton* Buttons[] = { BtnSurfaceFloor.Get(), BtnSurfaceBaseboard.Get(), BtnSurfaceCeiling.Get() };
	for (UButton* Button : Buttons)
	{
		if (UHorizontalBoxSlot* ButtonSlot = Row->AddChildToHorizontalBox(Button))
		{
			ButtonSlot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
			ButtonSlot->SetVerticalAlignment(VAlign_Center);
		}
	}
	Row->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	AddToVerticalBox(Chooser, MakePanelText(TEXT("Поверхность"), TEXT("SurfaceCaption"), true), FMargin(0.f, 0.f, 0.f, 4.f));
	AddToVerticalBox(Chooser, Row, FMargin(0.f));
	Chooser->SetVisibility(ESlateVisibility::Collapsed);
	AddToVerticalBox(FinishPage, Chooser, FMargin(16.f, 6.f, 16.f, 0.f));
	BtnSurfaceFloor->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnSurfaceFloorClicked);
	BtnSurfaceBaseboard->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnSurfaceBaseboardClicked);
	BtnSurfaceCeiling->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnSurfaceCeilingClicked);
	SurfaceChooser = Chooser;
}

void URoomPlannerWidget::UpdateSurfaceChooser()
{
	using namespace PlannerPanelLayout;
	if (!SurfaceChooser || !PlannerManager) return;
	const EPlannerSelectionKind Kind = PlannerManager->GetSelectionKind();
	const bool bRoomSurface = PlannerManager->SelectedRoomID != -1
		&& (Kind == EPlannerSelectionKind::Floor || Kind == EPlannerSelectionKind::Baseboard || Kind == EPlannerSelectionKind::Ceiling);
	ShowIf(SurfaceChooser, bRoomSurface, ESlateVisibility::SelfHitTestInvisible);
	if (!bRoomSurface) return;
	// The ceiling is hidden in the plan view: it can be chosen in 3D only.
	ShowIf(BtnSurfaceCeiling, CurrentViewMode != ERoomPlannerViewMode::View2D, ESlateVisibility::Visible);
	auto Style = [this, Kind](UButton* Button, EPlannerSelectionKind Surface)
	{
		if (!Button) return;
		const FLinearColor Color = (Kind == Surface) ? ActiveTabColor : InactiveTabColor;
		if (!Button->GetBackgroundColor().Equals(Color)) Button->SetBackgroundColor(Color);
	};
	Style(BtnSurfaceFloor, EPlannerSelectionKind::Floor);
	Style(BtnSurfaceBaseboard, EPlannerSelectionKind::Baseboard);
	Style(BtnSurfaceCeiling, EPlannerSelectionKind::Ceiling);
}

UButton* URoomPlannerWidget::GetSurfaceButton(EPlannerSelectionKind Surface) const
{
	switch (Surface)
	{
	case EPlannerSelectionKind::Floor:     return BtnSurfaceFloor;
	case EPlannerSelectionKind::Baseboard: return BtnSurfaceBaseboard;
	case EPlannerSelectionKind::Ceiling:   return BtnSurfaceCeiling;
	default: return nullptr;
	}
}

void URoomPlannerWidget::SelectRoomSurfaceForFinish(EPlannerSelectionKind Surface)
{
	if (!PlannerManager || PlannerManager->SelectedRoomID == -1) return;
	if (Surface == EPlannerSelectionKind::Ceiling && CurrentViewMode == ERoomPlannerViewMode::View2D) return;
	FRoomData Room;
	if (!PlannerManager->GetRoomData(PlannerManager->SelectedRoomID, Room)) return;
	// The room's interior point picks the same room (best-effort for odd shapes, e.g. a room around a nested one): check first, so a
	// point that lands in another room never touches the current selection.
	const FVector Point(Room.InteriorPoint.X, Room.InteriorPoint.Y, 0.f);
	if (PlannerManager->FindRoomAtWorldPos(Point) != PlannerManager->SelectedRoomID)
	{
		ShowStatusMessage(TEXT("Не удалось выбрать поверхность этой комнаты: кликните по ней на плане"));
		return;
	}
	PlannerManager->SelectRoomSurfaceAtWorldPos(Point, Surface);
	UpdateDynamicPropertiesPanel();
}

void URoomPlannerWidget::OnSurfaceFloorClicked() { SelectRoomSurfaceForFinish(EPlannerSelectionKind::Floor); }
void URoomPlannerWidget::OnSurfaceBaseboardClicked() { SelectRoomSurfaceForFinish(EPlannerSelectionKind::Baseboard); }
void URoomPlannerWidget::OnSurfaceCeilingClicked() { SelectRoomSurfaceForFinish(EPlannerSelectionKind::Ceiling); }

bool URoomPlannerWidget::IsSideCatalogOpen() const
{
	return (::IsValid(ActivePlannerColorCatalog) && ActivePlannerColorCatalog->IsInViewport())
		|| (::IsValid(ActivePlannerTileCatalog) && ActivePlannerTileCatalog->IsInViewport());
}

void URoomPlannerWidget::OpenFinishFlyout(UUserWidget* Catalog)
{
	if (!Catalog) return;
	// Same Z as before (over the plan); shifted right by the panel's width, so it stands beside the planner instead of over it.
	Catalog->AddToViewport(99);
	Catalog->SetRenderTranslation(FVector2D(GetPanelWidth(), 0.f));
	UpdateStatusStripPlacement();
}

void URoomPlannerWidget::CloseFinishFlyouts()
{
	if (::IsValid(ActivePlannerColorCatalog) && ActivePlannerColorCatalog->IsInViewport())
	{
		ActivePlannerColorCatalog->CloseColorCatalog();
	}
	if (::IsValid(ActivePlannerTileCatalog) && ActivePlannerTileCatalog->IsInViewport())
	{
		ActivePlannerTileCatalog->CloseCatalog();
	}
	UpdateStatusStripPlacement();
}

void URoomPlannerWidget::UpdateStatusStripPlacement()
{
	if (!StatusStrip) return;
	const float SideWidth = (bFinishCatalogBesidePanel && IsSideCatalogOpen()) ? GetPanelWidth() : 0.f;
	if (UCanvasPanelSlot* StripSlot = Cast<UCanvasPanelSlot>(StatusStrip->Slot))
	{
		const FVector2D Wanted(PlannerPanelRules::StatusStripOffset(GetPanelWidth(), SideWidth), 20.f);
		if (!StripSlot->GetPosition().Equals(Wanted, 0.5f)) StripSlot->SetPosition(Wanted);
	}
	// It wraps at the free width beside the UI.
	if (WidgetTree && WidgetTree->RootWidget)
	{
		const float RootWidth = (float)WidgetTree->RootWidget->GetCachedGeometry().GetLocalSize().X;
		if (RootWidth > 1.f)
		{
			const float MaxWidth = FMath::Max(320.f, RootWidth - GetPanelWidth() - SideWidth - 80.f);
			if (!FMath::IsNearlyEqual(StatusStrip->GetMaxDesiredWidth(), MaxWidth, 1.f)) StatusStrip->SetMaxDesiredWidth(MaxWidth);
			// The texts wrap at the width the strip can give them: the pill's padding (2 x 10) and «X» (with its 10 px gap) come off.
			const float CloseWidth = FMath::Max(40.f, BtnHideHelp ? (float)BtnHideHelp->GetDesiredSize().X : 0.f);
			const float HintWrap = FMath::Max(200.f, MaxWidth - 20.f - 10.f - CloseWidth);
			const float MessageWrap = FMath::Max(200.f, MaxWidth - 20.f);
			if (TxtGuidanceHint && !FMath::IsNearlyEqual(TxtGuidanceHint->GetWrapTextAt(), HintWrap, 1.f)) TxtGuidanceHint->SetWrapTextAt(HintWrap);
			if (TxtOperationMessage && !FMath::IsNearlyEqual(TxtOperationMessage->GetWrapTextAt(), MessageWrap, 1.f)) TxtOperationMessage->SetWrapTextAt(MessageWrap);
		}
	}
}

// ═══════════════════════════════════════════════════════════════════════════════
// Input boundary
// ═══════════════════════════════════════════════════════════════════════════════

bool URoomPlannerWidget::IsScreenPositionOverPlannerUI(const FVector2D& ScreenSpacePosition) const
{
	auto IsUnder = [&ScreenSpacePosition](const UWidget* Widget)
	{
		if (!Widget || !Widget->IsVisible()) return false;
		const FGeometry& Geometry = Widget->GetCachedGeometry();
		return Geometry.GetLocalSize().X > 0.f && Geometry.IsUnderLocation(ScreenSpacePosition);
	};

	// The white side panel. (LeftPanel is not used: its designed slot is 380 x 30, the sections overflow it.)
	if (PanelBackground)
	{
		if (IsUnder(PanelBackground)) return true;
	}
	else if (const UWidget* Root = WidgetTree ? WidgetTree->RootWidget.Get() : nullptr)
	{
		const FGeometry& RootGeometry = Root->GetCachedGeometry();
		if (RootGeometry.GetLocalSize().X > 0.f && RootGeometry.IsUnderLocation(ScreenSpacePosition)
			&& RootGeometry.AbsoluteToLocal(ScreenSpacePosition).X <= GetPanelWidth())
		{
			return true;
		}
	}
	// The hint's pill, its «X» and the message chip over the plan (not the transparent parts of the hint bar's box: the plan shows
	// through there).
	if (HorizontalBox_3 && HorizontalBox_3->IsVisible())
	{
		const UWidget* Pill = TxtGuidanceHint ? TxtGuidanceHint->GetParent() : nullptr;
		if (IsUnder(Pill ? Pill : HorizontalBox_3.Get()) || IsUnder(BtnHideHelp)) return true;
	}
	if (IsUnder(MessageChip) || IsUnder(FloatingContextBar)) return true;
	// A paint / tile catalog beside the panel: its column (it takes its own clicks; the ones between its controls stop here too).
	if (bFinishCatalogBesidePanel && IsSideCatalogOpen())
	{
		if (const UWidget* Root = WidgetTree ? WidgetTree->RootWidget.Get() : nullptr)
		{
			const FGeometry& RootGeometry = Root->GetCachedGeometry();
			if (RootGeometry.GetLocalSize().X > 0.f && RootGeometry.IsUnderLocation(ScreenSpacePosition)
				&& PlannerPanelRules::IsOverSideCatalog((float)RootGeometry.AbsoluteToLocal(ScreenSpacePosition).X, GetPanelWidth(), GetPanelWidth()))
			{
				return true;
			}
		}
	}
	return false;
}

bool URoomPlannerWidget::IsCursorOverPlannerUI() const
{
	if (!FSlateApplication::IsInitialized() || !IsVisible()) return false;
	return IsScreenPositionOverPlannerUI(FSlateApplication::Get().GetCursorPos());
}
