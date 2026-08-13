// Copyright MaxiMall. All Rights Reserved.

#include "ColorCatalog/ColorCatalogSwatchWidget.h"
#include "ColorCatalog/ColorCatalogItemObject.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/Button.h"

void UColorCatalogSwatchWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (Button_SwatchClick)
	{
		Button_SwatchClick->OnClicked.AddUniqueDynamic(this, &UColorCatalogSwatchWidget::HandleSwatchClicked);
	}
}

void UColorCatalogSwatchWidget::NativeOnListItemObjectSet(UObject* ListItemObject)
{
	CachedItemObject = Cast<UColorCatalogItemObject>(ListItemObject);
	RefreshDisplay();
}

void UColorCatalogSwatchWidget::RefreshDisplay()
{
	if (!CachedItemObject)
	{
		return;
	}

	const FColorCatalogItem& Item = CachedItemObject->ColorItem;

	if (Image_ColorBox)
	{
		Image_ColorBox->SetColorAndOpacity(Item.Color);
	}

	if (Text_ColorCode)
	{
		Text_ColorCode->SetText(FText::FromString(Item.Code));
	}

	if (Border_Selection)
	{
		Border_Selection->SetVisibility(CachedItemObject->bIsSelected ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
	}
}

void UColorCatalogSwatchWidget::HandleSwatchClicked()
{
	if (CachedItemObject)
	{
		OnColorSwatchClicked.Broadcast(CachedItemObject->ColorItem);
	}
}


