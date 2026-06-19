// Copyright MaxiMall Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FurnitureConfigurator/Data/FurnitureTypes.h"
#include "ConfiguratorMainWidget.generated.h"

class UTextBlock;
class UButton;
class UPanelWidget;
class AMaxiMallPreviewController;
class AShowroomBooth;

UENUM(BlueprintType)
enum class EOptionType : uint8
{
    Size,
    Color
};

UCLASS()
class MAXIMALL_API UFurnitureOptionListener : public UObject
{
    GENERATED_BODY()

public:
    void Init(class UConfiguratorMainWidget* InOwnerWidget, EFurnitureComponentType InComponent, EOptionType InType, int32 InOptionIndex)
    {
        OwnerWidget = InOwnerWidget;
        Component = InComponent;
        Type = InType;
        OptionIndex = InOptionIndex;
    }

    UFUNCTION()
    void OnButtonClicked();

    UFUNCTION()
    void OnButtonHovered();

    UFUNCTION()
    void OnButtonUnhovered();

    UPROPERTY()
    TObjectPtr<class UConfiguratorMainWidget> OwnerWidget;

    EFurnitureComponentType Component;
    EOptionType Type;
    int32 OptionIndex;
};

/**
 * UConfiguratorMainWidget
 * Direct C++ parent class for the main configurator UMG widget.
 * Uses strict BindWidget for core controls.
 */
UCLASS()
class MAXIMALL_API UConfiguratorMainWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;

    // ── STRICT WIDGET BINDINGS ───────────────────────────────────────────────
    
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> Btn_Viewmode;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> Btn_CloseUI;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> Txt_BtnURL;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UPanelWidget> Size_Container;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UPanelWidget> Color_Container;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> Txt_SKU;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> Txt_ProductName_1;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Warning;

    // ── DELEGATE CALLBACKS ───────────────────────────────────────────────────

    UFUNCTION()
    void OnViewmodeClicked();

    UFUNCTION()
    void OnCloseUIClicked();

    UFUNCTION()
    void OnURLButtonClicked();

public:
    /** Initialize and dynamically populate the widget. */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | UI")
    void SetupWidget(AMaxiMallPreviewController* InPC, AShowroomBooth* InBooth, EFurnitureComponentType InComponent);

    /** Refreshes current selections from the showroom booth state. */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | UI")
    void RefreshSelections();

    void HandleOptionSelected(EFurnitureComponentType Component, EOptionType Type, int32 OptionIndex);
    void HandleOptionHovered(EFurnitureComponentType Component, EOptionType Type, int32 OptionIndex);
    void HandleOptionUnhovered(EFurnitureComponentType Component, EOptionType Type, int32 OptionIndex);

private:
    UPROPERTY()
    TArray<TObjectPtr<UFurnitureOptionListener>> OptionListeners;

    UPROPERTY()
    TObjectPtr<AMaxiMallPreviewController> OwningPC;

    UPROPERTY()
    TObjectPtr<AShowroomBooth> TargetBooth;

    UPROPERTY()
    EFurnitureComponentType ActiveComponent;

    // Helper functions
    bool IsComponentMeshValid(AShowroomBooth* Booth, EFurnitureComponentType Component) const;
};
