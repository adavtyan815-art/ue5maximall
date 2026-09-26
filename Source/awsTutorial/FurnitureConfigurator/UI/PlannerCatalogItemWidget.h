// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/DragDropOperation.h"
#include "Constructor/RoomPlannerTypes.h"
#include "Widgets/Layout/SScaleBox.h"
#include "PlannerCatalogItemWidget.generated.h"

class UImage;
class UTextBlock;
class UBorder;
class UScaleBox;
class URoomPlannerWidget;
class UTexture2D;

/**
 * One draggable catalog card inside the planner's catalog content area (DT_PlannerObjects or DT_FurnitureCatalog row).
 *
 * Works as a pure C++ widget: if no Blueprint subclass provides a design, the card builds its own
 * SizeBox → Border → VerticalBox(ScaleBox(ImgThumbnail), TxtName) tree, styled from the values the planner
 * widget forwards from its root "UI Sizing - Catalog" section (normal / hovered / pressed tints, text colour,
 * font, image stretch). A Blueprint subclass may instead provide widgets named ImgThumbnail (Image),
 * TxtName (TextBlock) and CardBorder (Border); they are bound by name.
 *
 * Dragging: LMB press starts a UMG drag (UPlannerCatalogDragOperation); the drag operation carries this card
 * as Payload and the row name as Tag. The drop is resolved by URoomPlannerWidget (NativeOnDrop) or, when
 * released over the plan outside any hit-testable widget, by this card's NativeOnDragCancelled → planner drop logic.
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

	// ── Style (forwarded from URoomPlannerWidget "UI Sizing - Catalog") ──

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlannerCatalog|Style")
	float CardWidth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlannerCatalog|Style")
	float CardHeight = 110.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlannerCatalog|Style")
	FMargin CardPadding = FMargin(4.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlannerCatalog|Style")
	TEnumAsByte<EStretch::Type> ImageStretch = EStretch::ScaleToFit;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlannerCatalog|Style")
	FLinearColor NormalColor = FLinearColor(1.f, 1.f, 1.f, 0.05f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlannerCatalog|Style")
	FLinearColor HoveredColor = FLinearColor(1.f, 1.f, 1.f, 0.15f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlannerCatalog|Style")
	FLinearColor PressedColor = FLinearColor(1.f, 1.f, 1.f, 0.25f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlannerCatalog|Style")
	FLinearColor TextColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlannerCatalog|Style")
	FSlateFontInfo TextFont;

	/** Fills the card. Called by URoomPlannerWidget when it populates the catalog content area. */
	UFUNCTION(BlueprintCallable, Category = "PlannerCatalog")
	void SetupCatalogItem(URoomPlannerWidget* InOwner, EPlannerPlacementKind InKind, const FPlannerCatalogEntry& Entry);

	/** Drag visual only: the yaw the mouse wheel has given the dragged item. The thumbnail turns with it; the caption shows it. */
	void ShowDragYaw(float YawDeg);

	float GetDragYaw() const { return DragYawDeg; }
	UImage* GetThumbnailImage() const { return ImgThumbnail; }
	UTextBlock* GetNameText() const { return TxtName; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
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
	bool bHovered = false;
	bool bPressed = false;
	float DragYawDeg = 0.f;

	void ApplyVisuals();
	void ApplyStateTint();
	void ApplyDragYaw();
};

/**
 * The drag of a catalog card. Besides the card (Payload) it carries the item and the yaw the mouse wheel gives it while it is
 * dragged over the 2D plan (interior objects; AAwsTutorial_PlayerController::InputKey). The drag visual's thumbnail turns with
 * it and its caption shows the angle; a floor drop places the object with that yaw, a wall drop follows the wall.
 */
UCLASS()
class AWSTUTORIAL_API UPlannerCatalogDragOperation : public UDragDropOperation
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "PlannerCatalog")
	EPlannerPlacementKind Kind = EPlannerPlacementKind::Object;

	/** Row name: AssetID for objects, ProductID for cabinet sets. */
	UPROPERTY(BlueprintReadOnly, Category = "PlannerCatalog")
	FString ItemID;

	/** Degrees, clockwise on the plan; normalized to (-180, 180]. */
	UPROPERTY(BlueprintReadOnly, Category = "PlannerCatalog")
	float YawDeg = 0.f;

	/** Sets the yaw and shows it on the drag visual (a UPlannerCatalogItemWidget). */
	UFUNCTION(BlueprintCallable, Category = "PlannerCatalog")
	void SetYaw(float InYawDeg);
};
