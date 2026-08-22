// Copyright awsTutorial. All Rights Reserved.

#include "ColorCatalog/ColorCatalogSwatchWidget.h"
#include "ColorCatalog/ColorCatalogItemObject.h"
#include "ColorCatalog/ColorCatalogWidget.h"
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

	if (UColorCatalogWidget* ParentCatalog = GetTypedOuter<UColorCatalogWidget>())
	{
		ActiveScale = ParentCatalog->ActiveSwatchScale;
		InactiveScale = ParentCatalog->InactiveSwatchScale;
	}

	RefreshDisplay();
}

void UColorCatalogSwatchWidget::NativeOnItemSelectionChanged(bool bIsSelected)
{
	IUserObjectListEntry::NativeOnItemSelectionChanged(bIsSelected);
	if (CachedItemObject)
	{
		CachedItemObject->bIsSelected = bIsSelected;
	}
	RefreshDisplay();
}

void UColorCatalogSwatchWidget::RefreshDisplay()
{
	if (!CachedItemObject)
	{
		return;
	}

	const FColorCatalogItem& Item = CachedItemObject->ColorItem;
	bool bSelected = CachedItemObject->bIsSelected;

	if (Image_ColorBox)
	{
		Image_ColorBox->SetColorAndOpacity(Item.Color);
		Image_ColorBox->SetRenderScale(FVector2D(1.0f, 1.0f));
	}

	if (Text_ColorCode)
	{
		Text_ColorCode->SetText(FText::FromString(Item.Code));
	}

	// Apply scale to the ENTIRE WBP_ColorSwatchItem (Color Box + Text)
	SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	float TargetScale = bSelected ? ActiveScale : InactiveScale;
	SetRenderScale(FVector2D(TargetScale, TargetScale));
}

void UColorCatalogSwatchWidget::HandleSwatchClicked()
{
	if (CachedItemObject)
	{
		CachedItemObject->bIsSelected = true;
		RefreshDisplay();
		OnColorSwatchClicked.Broadcast(CachedItemObject->ColorItem);
	}
}


