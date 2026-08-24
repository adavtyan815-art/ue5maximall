// Copyright awsTutorial. All Rights Reserved.

#include "ColorCatalog/ColorCatalogWidget.h"
#include "ColorCatalog/ColorCatalogSubsystem.h"
#include "ColorCatalog/ColorCatalogItemObject.h"
#include "ColorCatalog/ColorCatalogSwatchWidget.h"
#include "awsTutorial_PlayerController.h"
#include "Components/TileView.h"
#include "Components/EditableTextBox.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"

UColorCatalogWidget* UColorCatalogWidget::OpenColorCatalogForWidget(
	UUserWidget* CallingWidget,
	TSubclassOf<UColorCatalogWidget> CatalogWidgetClass)
{
	if (!CallingWidget || !CatalogWidgetClass)
	{
		return nullptr;
	}

	APlayerController* PC = CallingWidget->GetOwningPlayer();
	if (!PC)
	{
		PC = UGameplayStatics::GetPlayerController(CallingWidget->GetWorld(), 0);
	}

	UColorCatalogWidget* CatalogWidget = CreateWidget<UColorCatalogWidget>(PC, CatalogWidgetClass);
	if (!CatalogWidget)
	{
		return nullptr;
	}

	CatalogWidget->SetParentCallingWidget(CallingWidget);

	// Collapse calling widget (WBP_PreviewWindow)
	CallingWidget->SetVisibility(ESlateVisibility::Collapsed);

	// Add Catalog to Viewport
	CatalogWidget->AddToViewport(99);
	return CatalogWidget;
}

void UColorCatalogWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (EditableText_Search)
	{
		EditableText_Search->SetHintText(FText::FromString(TEXT("Поиск...")));
		EditableText_Search->SetText(FText::GetEmpty());

		const FColor HexColor = FColor::FromHex(TEXT("0C121DFF"));
		const FSlateColor SlateHexColor = FSlateColor(FLinearColor::FromSRGBColor(HexColor));

		EditableText_Search->WidgetStyle.TextStyle.ColorAndOpacity = SlateHexColor;
		EditableText_Search->WidgetStyle.ForegroundColor = SlateHexColor;
		EditableText_Search->WidgetStyle.FocusedForegroundColor = SlateHexColor;

		EditableText_Search->OnTextChanged.AddUniqueDynamic(this, &UColorCatalogWidget::OnSearchTextChanged);
	}

	if (Button_RAL)
	{
		Button_RAL->OnClicked.AddUniqueDynamic(this, &UColorCatalogWidget::OnRALButtonClicked);
	}

	if (Button_NCS)
	{
		Button_NCS->OnClicked.AddUniqueDynamic(this, &UColorCatalogWidget::OnNCSButtonClicked);
	}

	if (Button_Back)
	{
		Button_Back->OnClicked.AddUniqueDynamic(this, &UColorCatalogWidget::OnBackButtonClicked);
	}

	if (TileView_Swatches)
	{
		TileView_Swatches->OnItemClicked().AddUObject(this, &UColorCatalogWidget::OnTileViewEntryClicked);
	}

	CurrentCatalogType = DefaultCatalogType;
	CurrentCategory = DefaultCategory;

	BindCategoryButtons();
	UpdateTabButtonStyles();
	UpdateCategoryButtonStyles();
	RefreshGrid();
}

void UColorCatalogWidget::NativeDestruct()
{
	Super::NativeDestruct();
}

void UColorCatalogWidget::SetParentCallingWidget(UUserWidget* InParentWidget)
{
	ParentCallingWidget = InParentWidget;
}

void UColorCatalogWidget::SetTabColors(FLinearColor InActiveColor, FLinearColor InInactiveColor)
{
	ActiveTabColor = InActiveColor;
	InactiveTabColor = InInactiveColor;
	UpdateTabButtonStyles();
}

void UColorCatalogWidget::SetCatalogType(EColorCatalogType NewCatalogType)
{
	CurrentCatalogType = NewCatalogType;
	UpdateTabButtonStyles();
	RefreshGrid();
}

void UColorCatalogWidget::SetActiveCategory(EColorShadeCategory NewCategory)
{
	CurrentCategory = NewCategory;
	UpdateCategoryButtonStyles();
	RefreshGrid();
}

void UColorCatalogWidget::UpdateTabButtonStyles()
{
	bool bIsRAL = (CurrentCatalogType == EColorCatalogType::RAL);

	if (Button_RAL)
	{
		Button_RAL->SetBackgroundColor(bIsRAL ? ActiveTabColor : InactiveTabColor);
	}

	if (Button_NCS)
	{
		Button_NCS->SetBackgroundColor(!bIsRAL ? ActiveTabColor : InactiveTabColor);
	}
}

void UColorCatalogWidget::UpdateCategoryButtonStyles()
{
	TArray<TPair<UButton*, EColorShadeCategory>> CatButtons = {
		{ Button_CategoryAll,     EColorShadeCategory::All },
		{ Button_CategoryRed,     EColorShadeCategory::Red },
		{ Button_CategoryOrange,  EColorShadeCategory::Orange },
		{ Button_CategoryYellow,  EColorShadeCategory::Yellow },
		{ Button_CategoryGreen,   EColorShadeCategory::Green },
		{ Button_CategoryBlue,    EColorShadeCategory::Blue },
		{ Button_CategoryViolet,  EColorShadeCategory::Violet },
		{ Button_CategoryGrey,    EColorShadeCategory::Grey },
		{ Button_CategoryWhite,   EColorShadeCategory::White },
		{ Button_CategoryBrown,   EColorShadeCategory::Brown },
		{ Button_CategoryBeige,   EColorShadeCategory::Beige },
		{ Button_CategoryBlack,   EColorShadeCategory::Black },
		{ Button_CategoryNeutral, EColorShadeCategory::Neutral }
	};

	for (const auto& Pair : CatButtons)
	{
		if (UButton* Btn = Pair.Key)
		{
			bool bIsSelected = (Pair.Value == CurrentCategory);
			Btn->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
			float Scale = bIsSelected ? ActiveCategoryScale : InactiveCategoryScale;
			Btn->SetRenderScale(FVector2D(Scale, Scale));

			if (Pair.Value == EColorShadeCategory::All)
			{
				Btn->SetBackgroundColor(bIsSelected ? ActiveCategoryAllColor : InactiveCategoryAllColor);
			}
		}
	}
}

void UColorCatalogWidget::OnSearchTextChanged(const FText& NewText)
{
	CurrentSearchQuery = NewText.ToString();
	RefreshGrid();
}

void UColorCatalogWidget::OnRALButtonClicked()
{
	SetCatalogType(EColorCatalogType::RAL);
}

void UColorCatalogWidget::OnNCSButtonClicked()
{
	SetCatalogType(EColorCatalogType::NCS);
}

void UColorCatalogWidget::CloseColorCatalog()
{
	// Uncollapse parent Calling Widget (WBP_PreviewWindow)
	if (ParentCallingWidget)
	{
		ParentCallingWidget->SetVisibility(ESlateVisibility::Visible);
	}

	OnCatalogClosed.Broadcast();
	RemoveFromParent();
}

void UColorCatalogWidget::OnBackButtonClicked()
{
	CloseColorCatalog();
}

void UColorCatalogWidget::BindCategoryButtons()
{
	if (Button_CategoryAll) Button_CategoryAll->OnClicked.AddUniqueDynamic(this, &UColorCatalogWidget::OnCatAllClicked);
	if (Button_CategoryRed) Button_CategoryRed->OnClicked.AddUniqueDynamic(this, &UColorCatalogWidget::OnCatRedClicked);
	if (Button_CategoryOrange) Button_CategoryOrange->OnClicked.AddUniqueDynamic(this, &UColorCatalogWidget::OnCatOrangeClicked);
	if (Button_CategoryYellow) Button_CategoryYellow->OnClicked.AddUniqueDynamic(this, &UColorCatalogWidget::OnCatYellowClicked);
	if (Button_CategoryGreen) Button_CategoryGreen->OnClicked.AddUniqueDynamic(this, &UColorCatalogWidget::OnCatGreenClicked);
	if (Button_CategoryBlue) Button_CategoryBlue->OnClicked.AddUniqueDynamic(this, &UColorCatalogWidget::OnCatBlueClicked);
	if (Button_CategoryViolet) Button_CategoryViolet->OnClicked.AddUniqueDynamic(this, &UColorCatalogWidget::OnCatVioletClicked);
	if (Button_CategoryGrey) Button_CategoryGrey->OnClicked.AddUniqueDynamic(this, &UColorCatalogWidget::OnCatGreyClicked);
	if (Button_CategoryWhite) Button_CategoryWhite->OnClicked.AddUniqueDynamic(this, &UColorCatalogWidget::OnCatWhiteClicked);
	if (Button_CategoryBrown) Button_CategoryBrown->OnClicked.AddUniqueDynamic(this, &UColorCatalogWidget::OnCatBrownClicked);
	if (Button_CategoryBeige) Button_CategoryBeige->OnClicked.AddUniqueDynamic(this, &UColorCatalogWidget::OnCatBeigeClicked);
	if (Button_CategoryBlack) Button_CategoryBlack->OnClicked.AddUniqueDynamic(this, &UColorCatalogWidget::OnCatBlackClicked);
	if (Button_CategoryNeutral) Button_CategoryNeutral->OnClicked.AddUniqueDynamic(this, &UColorCatalogWidget::OnCatNeutralClicked);
}

void UColorCatalogWidget::RefreshGrid()
{
	if (!TileView_Swatches)
	{
		return;
	}

	TileView_Swatches->ClearListItems();

	UGameInstance* GI = GetGameInstance();
	if (!GI) return;

	UColorCatalogSubsystem* ColorSubsystem = GI->GetSubsystem<UColorCatalogSubsystem>();
	if (!ColorSubsystem) return;

	TArray<FColorCatalogItem> FilteredItems = ColorSubsystem->FilterColors(CurrentCatalogType, CurrentCategory, CurrentSearchQuery);

	UColorCatalogItemObject* DefaultSelectedObj = nullptr;

	for (const FColorCatalogItem& ItemData : FilteredItems)
	{
		UColorCatalogItemObject* ItemObj = NewObject<UColorCatalogItemObject>(this);
		bool bIsSelected = (bHasActiveSelection && ActiveSelectedColorItem.Code.Equals(ItemData.Code, ESearchCase::IgnoreCase));
		ItemObj->Init(ItemData, bIsSelected);
		TileView_Swatches->AddItem(ItemObj);

		if (bIsSelected)
		{
			DefaultSelectedObj = ItemObj;
		}
	}

	if (DefaultSelectedObj)
	{
		TileView_Swatches->SetSelectedItem(DefaultSelectedObj);
	}
	else
	{
		TileView_Swatches->ClearSelection();
	}
}

void UColorCatalogWidget::OnTileViewEntryClicked(UObject* Item)
{
	UColorCatalogItemObject* ItemObj = Cast<UColorCatalogItemObject>(Item);
	if (!ItemObj) return;

	ActiveSelectedColorItem = ItemObj->ColorItem;
	bHasActiveSelection = true;

	if (Text_ActiveColor)
	{
		FString ActiveLabelStr = FString::Printf(TEXT("Активный цвет: %s"), *ActiveSelectedColorItem.Code);
		Text_ActiveColor->SetText(FText::FromString(ActiveLabelStr));
	}

	if (TileView_Swatches)
	{
		TileView_Swatches->SetSelectedItem(ItemObj);

		for (UObject* ListObj : TileView_Swatches->GetListItems())
		{
			if (UColorCatalogItemObject* SwatchObj = Cast<UColorCatalogItemObject>(ListObj))
			{
				SwatchObj->bIsSelected = (SwatchObj->ColorItem.Code.Equals(ActiveSelectedColorItem.Code, ESearchCase::IgnoreCase));
				if (UUserWidget* EntryWidget = TileView_Swatches->GetEntryWidgetFromItem(SwatchObj))
				{
					if (UColorCatalogSwatchWidget* SwatchWidget = Cast<UColorCatalogSwatchWidget>(EntryWidget))
					{
						SwatchWidget->ActiveScale = ActiveSwatchScale;
						SwatchWidget->InactiveScale = InactiveSwatchScale;
						SwatchWidget->RefreshDisplay();
					}
				}
			}
		}
	}

	// Live Preview on target mesh
	BroadcastColorSelected(ActiveSelectedColorItem.Color, OverrideMaterial);
}

void UColorCatalogWidget::BroadcastColorSelected(const FLinearColor& LinearColor, UMaterialInterface* InOverrideMaterial)
{
    OnColorSelected.Broadcast(LinearColor, InOverrideMaterial);
}
