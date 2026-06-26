// Copyright MaxiMall Project. All Rights Reserved.

#include "FurnitureConfigurator/UI/FurnitureSizePillWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "FurnitureConfigurator/UI/ConfiguratorMainWidget.h"

void UFurnitureSizePillWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (Btn_Select)
    {
        Btn_Select->OnClicked.RemoveAll(this);
        Btn_Select->OnClicked.AddDynamic(this, &UFurnitureSizePillWidget::OnButtonClicked);
    }
}

void UFurnitureSizePillWidget::Init(UConfiguratorMainWidget* InOwner, EFurnitureComponentType InComponent, int32 InOptionIndex, const FText& InLabel, bool bIsSelected)
{
    OwnerWidget = InOwner;
    Component = InComponent;
    OptionIndex = InOptionIndex;

    if (Txt_Label)
    {
        Txt_Label->SetText(InLabel);
    }

    // Fire Blueprint event to let designer style selected vs unselected states visually
    OnSelectionStateChanged(bIsSelected);
}

void UFurnitureSizePillWidget::OnButtonClicked()
{
    if (OwnerWidget)
    {
        OwnerWidget->HandleOptionSelected(Component, EOptionType::Size, OptionIndex);
    }
}
