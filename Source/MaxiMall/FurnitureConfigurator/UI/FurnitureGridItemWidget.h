// Copyright MaxiMall Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FurnitureConfigurator/Data/FurnitureTypes.h"
#include "FurnitureGridItemWidget.generated.h"

class UButton;
class UImage;
class UTexture2D;
class UConfiguratorMainWidget;

/**
 * UFurnitureGridItemWidget
 * Represents a reusable grid item (thumbnail + button) for option configuration.
 */
UCLASS()
class MAXIMALL_API UFurnitureGridItemWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> Btn_Select;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UImage> Img_Thumbnail;

    UFUNCTION()
    void OnButtonClicked();

private:
    UPROPERTY()
    TObjectPtr<UConfiguratorMainWidget> OwnerWidget;

    EFurnitureComponentType Component;
    EOptionType Type;
    int32 OptionIndex;

public:
    void Init(UConfiguratorMainWidget* InOwner, EFurnitureComponentType InComponent, EOptionType InType, int32 InOptionIndex, TSoftObjectPtr<UTexture2D> InThumbnail, bool bIsSelected);

    UFUNCTION(BlueprintImplementableEvent, Category = "GridItem")
    void OnSelectionStateChanged(bool bSelected);
};
