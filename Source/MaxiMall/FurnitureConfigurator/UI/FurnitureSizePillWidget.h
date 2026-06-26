// Copyright MaxiMall Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FurnitureConfigurator/Data/FurnitureTypes.h"
#include "FurnitureSizePillWidget.generated.h"

class UButton;
class UTextBlock;
class UConfiguratorMainWidget;

/**
 * UFurnitureSizePillWidget
 * Represents a reusable capsule/pill style button for Cabinet size selection.
 */
UCLASS()
class MAXIMALL_API UFurnitureSizePillWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> Btn_Select;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> Txt_Label;

    UFUNCTION()
    void OnButtonClicked();

private:
    UPROPERTY()
    TObjectPtr<UConfiguratorMainWidget> OwnerWidget;

    EFurnitureComponentType Component;
    int32 OptionIndex;

public:
    void Init(UConfiguratorMainWidget* InOwner, EFurnitureComponentType InComponent, int32 InOptionIndex, const FText& InLabel, bool bIsSelected);

    UFUNCTION(BlueprintImplementableEvent, Category = "SizePill")
    void OnSelectionStateChanged(bool bSelected);
};
