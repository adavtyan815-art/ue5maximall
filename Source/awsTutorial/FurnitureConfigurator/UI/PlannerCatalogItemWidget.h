// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Constructor/RoomPlannerTypes.h"
#include "PlannerCatalogItemWidget.generated.h"

class UImage;
class UTextBlock;
class UBorder;
class URoomPlannerWidget;
class UTexture2D;

/**
 * One draggable catalog card inside WBP_RoomPlannerWidget (DT_PlannerObjects or DT_FurnitureCatalog row).
 *
 * Works as a pure C++ widget: if no Blueprint subclass provides a design, the card builds its own
 * SizeBox → Border → VerticalBox(ImgThumbnail, TxtName) tree. A Blueprint subclass may instead provide
 * widgets named ImgThumbnail (Image) and TxtName (TextBlock); they are bound by name.
 *
 * Dragging: LMB press starts a UMG drag; the drag operation carries this card as Payload and the row
 * name as Tag. The drop is resolved by URoomPlannerWidget (NativeOnDrop) or, when released over the
 * plan outside any hit-testable widget, by this card's NativeOnDragCancelled → planner drop logic.
 */
UCLASS()
class AWSTUTORIAL_API UPlannerCatalogItemWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Object (DT_PlannerObjects) or CabinetSet (DT_FurnitureCatalog). */
	UPROPERTY(BlueprintReadOnly, Category = "PlannerCatalog")
	EPlannerPlacementKind Kind = EPlannerPlacementKind::Object;

	/** Row name: AssetID for objects, ProductID for cabinet sets. */
	UPROPERTY(BlueprintReadOnly, Category = "PlannerCatalog")
	FString ItemID;

	UPROPERTY(BlueprintReadOnly, Category = "PlannerCatalog")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "PlannerCatalog")
	TSoftObjectPtr<UTexture2D> Thumbnail;

	/** Card size used when the card builds its own visuals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlannerCatalog")
	float CardWidth = 96.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlannerCatalog")
	float CardHeight = 112.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlannerCatalog")
	FLinearColor ObjectCardColor = FLinearColor(0.12f, 0.16f, 0.22f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlannerCatalog")
	FLinearColor CabinetSetCardColor = FLinearColor(0.20f, 0.14f, 0.10f, 1.f);

	/** Fills the card. Called by URoomPlannerWidget when it populates the catalog panels. */
	UFUNCTION(BlueprintCallable, Category = "PlannerCatalog")
	void SetupCatalogItem(URoomPlannerWidget* InOwner, EPlannerPlacementKind InKind, const FPlannerCatalogEntry& Entry);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual void NativeOnDragCancelled(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> ImgThumbnail;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtName;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> CardBorder;

private:
	TWeakObjectPtr<URoomPlannerWidget> OwnerPlanner;

	void ApplyVisuals();
};
