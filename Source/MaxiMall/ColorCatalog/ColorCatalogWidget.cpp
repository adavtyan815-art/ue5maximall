// Copyright MaxiMall. All Rights Reserved.

#include "ColorCatalog/ColorCatalogWidget.h"
#include "ColorCatalog/ColorCatalogSubsystem.h"
#include "ColorCatalog/ColorCatalogItemObject.h"
#include "FurnitureConfigurator/Preview/MaxiMallPreviewController.h"
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

		FEditableTextBoxStyle Style = EditableText_Search->GetWidgetStyle();
		Style.TextStyle.ColorAndOpacity = SlateHexColor;
		Style.ForegroundColor = SlateHexColor;
		Style.FocusedForegroundColor = SlateHexColor;
		EditableText_Search->SetWidgetStyle(Style);

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

	BindCategoryButtons();
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

void UColorCatalogWidget::SetCatalogType(EColorCatalogType NewCatalogType)
{
	CurrentCatalogType = NewCatalogType;
	RefreshGrid();
}

void UColorCatalogWidget::SetActiveCategory(EColorShadeCategory NewCategory)
{
	CurrentCategory = NewCategory;
	RefreshGrid();
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

void UColorCatalogWidget::OnBackButtonClicked()
{
	// Uncollapse parent Calling Widget (WBP_PreviewWindow)
	if (ParentCallingWidget)
	{
		ParentCallingWidget->SetVisibility(ESlateVisibility::Visible);
	}

	OnCatalogClosed.Broadcast();
	RemoveFromParent();
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

	for (const FColorCatalogItem& ItemData : FilteredItems)
	{
		UColorCatalogItemObject* ItemObj = NewObject<UColorCatalogItemObject>(this);
		bool bIsSelected = (bHasActiveSelection && ActiveSelectedColorItem.Code.Equals(ItemData.Code, ESearchCase::IgnoreCase));
		ItemObj->Init(ItemData, bIsSelected);
		TileView_Swatches->AddItem(ItemObj);
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
		FString ActiveLabelStr = FString::Printf(TEXT("Активный: %s"), *ActiveSelectedColorItem.Code);
		Text_ActiveColor->SetText(FText::FromString(ActiveLabelStr));
	}

	// Live Preview on target mesh
	BroadcastColorSelected(ActiveSelectedColorItem.Color, OverrideMaterial);
}

void UColorCatalogWidget::BroadcastColorSelected(const FLinearColor& LinearColor, UMaterialInterface* InOverrideMaterial)
{
    OnColorSelected.Broadcast(LinearColor, InOverrideMaterial);
}
