// Copyright MaxiMall Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FurnitureConfigurator/Preview/MaxiMallPreviewController.h"
#include "BIMInspectorWidget.generated.h"

class UTextBlock;
class UButton;
class UScrollBox;
class UVerticalBox;
class UPrimitiveComponent;
class UBIMAttributeRowWidget;

UCLASS()
class MAXIMALL_API UBIMCategoryHeaderHandler : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY()
    TObjectPtr<UVerticalBox> ContentBox;

    UPROPERTY()
    TObjectPtr<UTextBlock> ArrowText;

    UFUNCTION()
    void OnHeaderClicked();
};

/**
 * UBIMInspectorWidget
 * Direct C++ parent class for WBP_BIMInspector.
 */
UCLASS()
class MAXIMALL_API UBIMInspectorWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "BIM UI")
    void RefreshBIMData(UPrimitiveComponent* Component);

    UFUNCTION(BlueprintCallable, Category = "BIM UI")
    void UpdateFromBIMData(const FBIMElementData& BIMData);

    UFUNCTION(BlueprintCallable, Category = "BIM UI")
    bool IsMouseOverMainPanel() const;

    bool IsPointerOverDragArea(const FVector2D& ScreenPos) const;

    UFUNCTION(BlueprintCallable, Category = "BIM UI")
    void UpdateShareButtonText(bool bIsSharing);

protected:
    virtual void NativeConstruct() override;
    virtual void NativePreConstruct() override;

    UFUNCTION()
    void OnCloseClicked();

    UFUNCTION()
    void OnToggleSharedSelectionClicked();

    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

    UPROPERTY()
    TArray<TObjectPtr<UBIMCategoryHeaderHandler>> CategoryHeaderHandlers;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "BIM UI")
    TObjectPtr<UWidget> Border_MainPanel;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "BIM UI")
    TObjectPtr<UWidget> Border_Header;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "BIM UI")
    TObjectPtr<UWidget> Header_Horizontal_Box;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "BIM UI")
    TObjectPtr<UWidget> Header_HorizontalBox;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "BIM UI")
    TObjectPtr<UWidget> DragHeaderBar;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "BIM UI")
    TObjectPtr<UTextBlock> Txt_Category;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "BIM UI")
    TObjectPtr<UTextBlock> Txt_Title;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "BIM UI")
    TObjectPtr<UTextBlock> Txt_Subtitle;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "BIM UI")
    TObjectPtr<UButton> Btn_Close;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "BIM UI")
    TObjectPtr<UButton> ToggleSharedSelection;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "BIM UI")
    TObjectPtr<UButton> Btn_ToggleSharedSelection;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "BIM UI")
    TObjectPtr<UButton> Toggle_Shared_Selection;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "BIM UI")
    TObjectPtr<UTextBlock> Txt_ToggleSharedSelection;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "BIM UI")
    TObjectPtr<UTextBlock> Txt_ShareText;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "BIM UI")
    TObjectPtr<UTextBlock> TextBlock_93;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "BIM UI")
    TObjectPtr<UScrollBox> ScrollBox_Attributes;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "BIM UI")
    TObjectPtr<UScrollBox> ScrollBox_BIMAttributes;

    UPROPERTY(EditDefaultsOnly, Category = "BIM UI")
    TSubclassOf<UBIMAttributeRowWidget> AttributeRowClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BIM UI Sizing")
    float InspectorScrollBoxHeight = 450.f;

private:
    bool bIsDraggingWindow = false;
    FVector2D DragStartCursorPos = FVector2D::ZeroVector;
    FVector2D DragStartPanelPos = FVector2D::ZeroVector;
    FVector2D DragStartScreenPos = FVector2D::ZeroVector;
};
