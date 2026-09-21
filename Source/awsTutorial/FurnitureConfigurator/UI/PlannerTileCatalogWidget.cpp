// Copyright 2026 MaxiMall. All Rights Reserved.

#include "FurnitureConfigurator/UI/PlannerTileCatalogWidget.h"
#include "FurnitureConfigurator/UI/PlannerStyleButton.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"

namespace PlannerTileCatalogLayout
{
	// Same frame as WBP_ColorCatalog: a 426-wide panel on the left, content column from x = 20.
	constexpr float PanelWidth = 426.f;
	constexpr float ContentLeft = 20.f;
	constexpr float ContentWidth = 390.f;
	constexpr float CardGap = 10.f;
	constexpr float ScrollBarRoom = 14.f; // the scroll box draws its bar inside its width
	constexpr float CardsWidth = ContentWidth - ScrollBarRoom;
	constexpr float CardWidth = (CardsWidth - CardGap) * 0.5f;
	constexpr float CardPadding = 8.f;

	const TCHAR* IconPath = TEXT("/Game/RoomPlanner/Textures/T_TileCatalogIcon.T_TileCatalogIcon");
	/** The colour catalog's back button icon. */
	const TCHAR* BackIconPath = TEXT("/Game/FirstPerson/UI/CrowdTown/Online_Offline_Selection/Back.Back");

	FSlateBrush MakeRoundedBrush(const FLinearColor& Fill, const FLinearColor& Outline, float OutlineWidth, float Radius)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Fill);
		Brush.OutlineSettings.CornerRadii = FVector4(Radius, Radius, Radius, Radius);
		Brush.OutlineSettings.Color = FSlateColor(Outline);
		Brush.OutlineSettings.Width = OutlineWidth;
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		return Brush;
	}

	FButtonStyle MakeButtonStyle(const FSlateBrush& Normal, const FSlateBrush& Hovered, const FSlateBrush& Pressed)
	{
		FButtonStyle Style;
		Style.SetNormal(Normal);
		Style.SetHovered(Hovered);
		Style.SetPressed(Pressed);
		Style.SetDisabled(Normal);
		Style.SetNormalPadding(FMargin(0.f));
		Style.SetPressedPadding(FMargin(0.f));
		return Style;
	}
}

UPlannerTileCatalogWidget* UPlannerTileCatalogWidget::OpenForWidget(UUserWidget* CallingWidget, TSubclassOf<UPlannerTileCatalogWidget> CatalogClass)
{
	if (!CallingWidget)
	{
		return nullptr;
	}
	if (!CatalogClass)
	{
		CatalogClass = UPlannerTileCatalogWidget::StaticClass();
	}
	APlayerController* PC = CallingWidget->GetOwningPlayer();
	if (!PC)
	{
		PC = UGameplayStatics::GetPlayerController(CallingWidget->GetWorld(), 0);
	}
	if (!PC)
	{
		return nullptr;
	}
	UPlannerTileCatalogWidget* Catalog = CreateWidget<UPlannerTileCatalogWidget>(PC, CatalogClass);
	if (!Catalog)
	{
		return nullptr;
	}
	Catalog->ParentCallingWidget = CallingWidget;
	CallingWidget->SetVisibility(ESlateVisibility::Collapsed);
	Catalog->AddToViewport(99);
	return Catalog;
}

bool UPlannerTileCatalogWidget::Initialize()
{
	const bool bSuperInitialized = Super::Initialize();
	if (bSuperInitialized && WidgetTree && !WidgetTree->RootWidget)
	{
		SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		BuildLayout();
	}
	return bSuperInitialized;
}

// These are reached only by input over the panel (the rest of the screen is not hit-testable); buttons and the scroll box handle
// their own input first.
FReply UPlannerTileCatalogWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return FReply::Handled();
}

FReply UPlannerTileCatalogWidget::NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return FReply::Handled();
}

FReply UPlannerTileCatalogWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return FReply::Handled();
}

void UPlannerTileCatalogWidget::BuildLayout()
{
	using namespace PlannerTileCatalogLayout;

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("TileCatalogRoot"));
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible); // only the panel takes clicks; the scene stays usable beside it
	WidgetTree->RootWidget = Root;

	auto AddToCanvas = [Root](UWidget* Widget, const FAnchors& Anchors, const FMargin& Offsets, bool bAutoSize)
	{
		UCanvasPanelSlot* CanvasSlot = Root->AddChildToCanvas(Widget);
		CanvasSlot->SetAnchors(Anchors);
		CanvasSlot->SetOffsets(Offsets);
		CanvasSlot->SetAutoSize(bAutoSize);
		return CanvasSlot;
	};
	auto MakeText = [this](const FString& Text, const TCHAR* Typeface, float Size, const FLinearColor& Color)
	{
		UTextBlock* TextWidget = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		TextWidget->SetText(FText::FromString(Text));
		TextWidget->SetFont(FCoreStyle::GetDefaultFontStyle(Typeface, Size));
		TextWidget->SetColorAndOpacity(FSlateColor(Color));
		return TextWidget;
	};
	auto MakeDivider = [this]()
	{
		UImage* Divider = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		Divider->SetColorAndOpacity(DividerColor);
		return Divider;
	};

	// Panel (full height, blocks clicks to the scene behind it)
	UImage* Background = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("PanelBackground"));
	Background->SetColorAndOpacity(PanelColor);
	AddToCanvas(Background, FAnchors(0.f, 0.f, 0.f, 1.f), FMargin(0.f, 0.f, PanelWidth, 0.f), false);

	// Header: icon, title, back button
	UImage* Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("TitleIcon"));
	if (UTexture2D* IconTexture = LoadObject<UTexture2D>(nullptr, IconPath))
	{
		Icon->SetBrushFromTexture(IconTexture, false);
	}
	else
	{
		Icon->SetColorAndOpacity(TextColor);
	}
	AddToCanvas(Icon, FAnchors(0.f), FMargin(40.f, 36.f, 46.f, 46.f), false);

	AddToCanvas(MakeText(TEXT("Каталог плитки"), TEXT("Bold"), 18.f, TextColor), FAnchors(0.f), FMargin(104.f, 48.f, 0.f, 0.f), true);

	UButton* BackButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Button_Back"));
	FSlateBrush BackBrush = MakeRoundedBrush(ButtonColor, FLinearColor(0.695f, 0.695f, 0.695f, 1.f), 1.f, 4.f);
	UTexture2D* BackIcon = LoadObject<UTexture2D>(nullptr, BackIconPath);
	if (BackIcon)
	{
		BackBrush.SetResourceObject(BackIcon);
	}
	BackButton->SetStyle(MakeButtonStyle(BackBrush, BackBrush, BackBrush));
	if (!BackIcon)
	{
		BackButton->AddChild(MakeText(TEXT("✕"), TEXT("Bold"), 12.f, FLinearColor::White));
	}
	BackButton->SetToolTipText(FText::FromString(TEXT("Закрыть каталог плитки")));
	BackButton->OnClicked.AddUniqueDynamic(this, &UPlannerTileCatalogWidget::OnBackClicked);
	AddToCanvas(BackButton, FAnchors(0.f), FMargin(372.f, 44.f, 27.f, 24.f), false);

	// Active tile
	AddToCanvas(MakeDivider(), FAnchors(0.f), FMargin(ContentLeft, 100.f, 375.f, 1.f), false);
	USizeBox* ActiveBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	ActiveBox->SetWidthOverride(ContentWidth);
	ActiveTileText = MakeText(FString(), TEXT("Bold"), 14.f, TextColor);
	ActiveTileText->SetAutoWrapText(false); // one line: the dividers and the card list sit at fixed heights
	ActiveTileText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis); // applies inside the clip rect:
	ActiveTileText->SetClipping(EWidgetClipping::ClipToBounds);
	ActiveBox->AddChild(ActiveTileText);
	AddToCanvas(ActiveBox, FAnchors(0.f), FMargin(ContentLeft, 112.f, 0.f, 0.f), true);
	AddToCanvas(MakeDivider(), FAnchors(0.f), FMargin(ContentLeft, 150.f, 375.f, 1.f), false);

	// Cards: two columns, scrolling down to the bottom of the screen
	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("TileScroll"));
	CardBox = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("TileCards"));
	CardBox->SetExplicitWrapSize(true);
	CardBox->SetWrapSize(CardsWidth);
	CardBox->SetInnerSlotPadding(FVector2D(CardGap, CardGap));
	Scroll->AddChild(CardBox);
	AddToCanvas(Scroll, FAnchors(0.f, 0.f, 0.f, 1.f), FMargin(ContentLeft, 162.f, ContentWidth, 20.f), false);

	RefreshActiveState();
}

void UPlannerTileCatalogWidget::SetTiles(const TArray<FPlannerCatalogEntry>& InTiles)
{
	Tiles = InTiles;
	RebuildCards();
	RefreshActiveState();
}

void UPlannerTileCatalogWidget::SetActiveTile(FName TileID)
{
	if (ActiveTileID == TileID) return;
	ActiveTileID = TileID;
	RefreshActiveState();
}

void UPlannerTileCatalogWidget::SetCardsEnabled(bool bEnabled)
{
	if (bCardsEnabled == bEnabled) return;
	bCardsEnabled = bEnabled;
	RefreshActiveState();
}

FText UPlannerTileCatalogWidget::GetActiveTileLabel() const
{
	return ActiveTileText ? ActiveTileText->GetText() : FText::GetEmpty();
}

void UPlannerTileCatalogWidget::RebuildCards()
{
	using namespace PlannerTileCatalogLayout;
	if (!CardBox || !WidgetTree) return;

	CardBox->ClearChildren();
	CardButtons.Reset();
	CardImages.Reset();

	if (Tiles.Num() == 0)
	{
		UTextBlock* Empty = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Empty->SetText(FText::FromString(TEXT("В каталоге нет плитки (DT_PlannerTiles)")));
		Empty->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 13.f));
		Empty->SetColorAndOpacity(FSlateColor(SecondaryTextColor));
		CardBox->AddChild(Empty);
		return;
	}

	for (const FPlannerCatalogEntry& Tile : Tiles)
	{
		UPlannerStyleButton* Card = WidgetTree->ConstructWidget<UPlannerStyleButton>(UPlannerStyleButton::StaticClass());
		Card->StyleID = FName(*Tile.ID);
		Card->SetToolTipText(Tile.DisplayName);
		Card->OnStyleClicked.BindUObject(this, &UPlannerTileCatalogWidget::HandleCardClicked);
		Card->BindClick();

		UVerticalBox* Body = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		if (UButtonSlot* BodySlot = Cast<UButtonSlot>(Card->AddChild(Body)))
		{
			BodySlot->SetPadding(FMargin(CardPadding));
			BodySlot->SetHorizontalAlignment(HAlign_Fill);
			BodySlot->SetVerticalAlignment(VAlign_Fill);
		}

		// Preview picture (FPlannerTileRow::Thumbnail); a swatch of the tile's average colour when the row has none.
		USizeBox* PictureBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		PictureBox->SetWidthOverride(PreviewSize);
		PictureBox->SetHeightOverride(PreviewSize);
		UImage* Picture = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		if (UTexture2D* Thumbnail = Tile.Thumbnail.LoadSynchronous())
		{
			Picture->SetBrushFromTexture(Thumbnail, false);
		}
		else
		{
			Picture->SetColorAndOpacity(Tile.Color);
		}
		PictureBox->AddChild(Picture);
		if (UVerticalBoxSlot* PictureSlot = Body->AddChildToVerticalBox(PictureBox))
		{
			PictureSlot->SetHorizontalAlignment(HAlign_Center);
			PictureSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
		}

		UTextBlock* Name = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Name->SetText(Tile.DisplayName.IsEmpty() ? FText::FromString(Tile.ID) : Tile.DisplayName);
		Name->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 12.f));
		Name->SetColorAndOpacity(FSlateColor(TextColor));
		Name->SetAutoWrapText(true);
		Body->AddChildToVerticalBox(Name);

		UTextBlock* SizeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		SizeText->SetText(FText::FromString(FString::Printf(TEXT("%.0f × %.0f см"), Tile.TileSizeCm, Tile.TileSizeCm)));
		SizeText->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 11.f));
		SizeText->SetColorAndOpacity(FSlateColor(SecondaryTextColor));
		Body->AddChildToVerticalBox(SizeText);

		USizeBox* CardFrame = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		CardFrame->SetWidthOverride(CardWidth);
		CardFrame->AddChild(Card);
		CardBox->AddChild(CardFrame);

		CardButtons.Add(Card);
		CardImages.Add(Picture);
	}
}

void UPlannerTileCatalogWidget::RefreshActiveState()
{
	using namespace PlannerTileCatalogLayout;

	FText ActiveName = FText::FromString(TEXT("—"));
	for (int32 i = 0; i < CardButtons.Num() && Tiles.IsValidIndex(i); ++i)
	{
		const bool bActive = !ActiveTileID.IsNone() && FName(*Tiles[i].ID) == ActiveTileID;
		if (bActive)
		{
			ActiveName = Tiles[i].DisplayName.IsEmpty() ? FText::FromString(Tiles[i].ID) : Tiles[i].DisplayName;
		}
		const FLinearColor Outline = bActive ? ActiveCardColor : CardOutlineColor;
		const float OutlineWidth = bActive ? 3.f : 1.f;
		CardButtons[i]->SetIsEnabled(bCardsEnabled);
		CardButtons[i]->SetStyle(MakeButtonStyle(
			MakeRoundedBrush(FLinearColor::White, Outline, OutlineWidth, 8.f),
			MakeRoundedBrush(FLinearColor(0.94f, 0.95f, 0.97f, 1.f), bActive ? ActiveCardColor : SecondaryTextColor, OutlineWidth, 8.f),
			MakeRoundedBrush(FLinearColor(0.88f, 0.90f, 0.93f, 1.f), bActive ? ActiveCardColor : SecondaryTextColor, OutlineWidth, 8.f)));
	}
	if (ActiveTileText)
	{
		const FText Label = bCardsEnabled
			? FText::Format(FText::FromString(TEXT("Активная плитка: {0}")), ActiveName)
			: FText::FromString(TEXT("Выберите стену, пол, потолок или плинтус"));
		ActiveTileText->SetText(Label);
		ActiveTileText->SetToolTipText(Label); // the full text when the line is cut short
	}
}

void UPlannerTileCatalogWidget::HandleCardClicked(FName TileID)
{
	if (!bCardsEnabled) return;
	// The owner applies the tile and then marks it (SetActiveTile), so a rejected tile is never shown as applied.
	OnTileChosen.Broadcast(TileID);
	OnTileChosenNative.Broadcast(TileID);
}

void UPlannerTileCatalogWidget::CloseCatalog()
{
	if (ParentCallingWidget)
	{
		ParentCallingWidget->SetVisibility(ESlateVisibility::Visible);
	}
	OnCatalogClosed.Broadcast();
	RemoveFromParent();
}

void UPlannerTileCatalogWidget::OnBackClicked()
{
	CloseCatalog();
}

UPlannerStyleButton* UPlannerTileCatalogWidget::GetCardButton(int32 Index) const
{
	return CardButtons.IsValidIndex(Index) ? CardButtons[Index].Get() : nullptr;
}

UImage* UPlannerTileCatalogWidget::GetCardImage(int32 Index) const
{
	return CardImages.IsValidIndex(Index) ? CardImages[Index].Get() : nullptr;
}
