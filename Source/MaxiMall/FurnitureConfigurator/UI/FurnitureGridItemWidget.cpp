// Copyright MaxiMall Project. All Rights Reserved.

#include "FurnitureConfigurator/UI/FurnitureGridItemWidget.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "FurnitureConfigurator/UI/ConfiguratorMainWidget.h"

void UFurnitureGridItemWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (Btn_Select)
    {
        Btn_Select->OnClicked.RemoveAll(this);
        Btn_Select->OnClicked.AddDynamic(this, &UFurnitureGridItemWidget::OnButtonClicked);
    }
}

void UFurnitureGridItemWidget::Init(UConfiguratorMainWidget* InOwner, EFurnitureComponentType InComponent, EOptionType InType, int32 InOptionIndex, TSoftObjectPtr<UTexture2D> InThumbnail, bool bIsSelected)
{
    OwnerWidget = InOwner;
    Component = InComponent;
    Type = InType;
    OptionIndex = InOptionIndex;

    if (Img_Thumbnail)
    {
        if (!InThumbnail.IsNull())
        {
            UTexture2D* LoadedTex = InThumbnail.LoadSynchronous();
            if (LoadedTex)
            {
                Img_Thumbnail->SetBrushFromTexture(LoadedTex);
                Img_Thumbnail->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
            }
            else
            {
                Img_Thumbnail->SetVisibility(ESlateVisibility::Collapsed);
            }
        }
        else
        {
            Img_Thumbnail->SetVisibility(ESlateVisibility::Collapsed);
        }
    }

    OnSelectionStateChanged(bIsSelected);
}

void UFurnitureGridItemWidget::OnButtonClicked()
{
    if (OwnerWidget)
    {
        OwnerWidget->HandleOptionSelected(Component, Type, OptionIndex);
    }
}
