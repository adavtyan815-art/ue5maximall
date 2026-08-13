// Copyright MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/IUserObjectListEntry.h"
#include "ColorCatalogTypes.h"
#include "ColorCatalogSwatchWidget.generated.h"

class UImage;
class UTextBlock;
class UBorder;
class UButton;
class UColorCatalogItemObject;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnColorSwatchClickedSignature, const FColorCatalogItem&, SelectedColorItem);

/**
 * Single Color Swatch Entry widget used inside UTileView (3 Columns).
 */
UCLASS(Abstract)
class MAXIMALL_API UColorCatalogSwatchWidget : public UUserWidget, public IUserObjectListEntry
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeOnListItemObjectSet(UObject* ListItemObject) override;

	UPROPERTY(BlueprintAssignable, Category = "Color Catalog Swatch")
	FOnColorSwatchClickedSignature OnColorSwatchClicked;

	UFUNCTION(BlueprintCallable, Category = "Color Catalog Swatch")
	void RefreshDisplay();

protected:
	UPROPERTY(meta = (BindWidgetOptional))
	UImage* Image_ColorBox;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* Text_ColorCode;

	UPROPERTY(meta = (BindWidgetOptional))
	UBorder* Border_Selection;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* Button_SwatchClick;

private:
	UPROPERTY()
	UColorCatalogItemObject* CachedItemObject;

	UFUNCTION()
	void HandleSwatchClicked();
};

