// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Fonts/SlateFontInfo.h"
#include "PlannerDimensionOverlay.generated.h"

class SPlannerDimensionOverlay;

/** One dimension in the overlay's own coordinates: a line with an arrow at each end and its value centred on it. */
struct AWSTUTORIAL_API FPlannerScreenDimension
{
	/** Arrow tips; the dimension line runs between them. */
	FVector2D Start = FVector2D::ZeroVector;
	FVector2D End = FVector2D::ZeroVector;

	/** The measured points. Extension lines lead from them to the arrow tips; equal to Start / End when there are none. */
	FVector2D StartRef = FVector2D::ZeroVector;
	FVector2D EndRef = FVector2D::ZeroVector;

	FString Text;

	/**
	 * Only the value: a height seen from above has no length to draw a line along. It goes beside the midpoint, on the side away
	 * from the measured points (at the midpoint when they coincide with it).
	 */
	bool bTextOnly = false;
};

/** Where the parts of one dimension go (pure geometry, so it can be tested without drawing). */
namespace PlannerDimensionLayout
{
	struct AWSTUTORIAL_API FStyle
	{
		float ArrowLength = 9.f;
		float ArrowHalfWidth = 3.5f;
		/** Space between the text and the edge of its box. */
		FVector2D TextPadding = FVector2D(5.f, 1.f);
		/** Space kept between the arrows and the text box, and between the line and a text box beside it. */
		float Gap = 3.f;
		/** How far extension lines run past the dimension line. */
		float ExtensionOvershoot = 6.f;
		/** Extension lines stop this far short of the measured point. */
		float ExtensionGap = 2.f;
	};

	struct AWSTUTORIAL_API FResult
	{
		/** False for text-only dimensions and degenerate lines: only the value is drawn. */
		bool bDrawLine = false;

		/** The value sits on the line, between the arrows; otherwise it is beside the line. */
		bool bTextOnLine = false;

		/** Too short for arrows pointing outwards: they point in from outside and the line runs on past them. */
		bool bArrowsOutside = false;

		/** Dimension line (longer than Start..End when the arrows are outside). */
		FVector2D LineFrom = FVector2D::ZeroVector;
		FVector2D LineTo = FVector2D::ZeroVector;

		/** Arrowheads: tip, then the two corners of the base. */
		FVector2D StartArrow[3];
		FVector2D EndArrow[3];

		/** Extension lines (drawn when bHasStartExtension / bHasEndExtension). */
		bool bHasStartExtension = false;
		bool bHasEndExtension = false;
		FVector2D StartExtension[2];
		FVector2D EndExtension[2];

		/** Text box: centre and size (text size plus padding). */
		FVector2D TextCenter = FVector2D::ZeroVector;
		FVector2D BoxSize = FVector2D::ZeroVector;

		/** The value is beside its line (or a caption): it may step further out, along TextShiftDir, to clear other values. */
		bool bTextMovable = false;
		FVector2D TextShiftDir = FVector2D(0., -1.);
	};

	AWSTUTORIAL_API FResult Compute(const FPlannerScreenDimension& Dimension, const FVector2D& TextSize, const FStyle& Style);

	/** Text box of a layout. */
	AWSTUTORIAL_API FSlateRect TextBox(const FResult& Layout);

	/**
	 * Values centred on their lines stay put; every movable value steps out along its TextShiftDir until its box covers no value
	 * placed before it (a door near a corner: the width and the corner gap both beside their short lines).
	 */
	AWSTUTORIAL_API void SeparateTexts(TArray<FResult>& Layouts, const FStyle& Style);

	/**
	 * Whether a view shows one of the selection's dimensions, and whether as its value only. The top-down plan shows no vertical
	 * lines (they have no length there) but the plan-only caption; 3D the reverse, with a vertical line shorter than MinLineLength
	 * on screen shown as its value only.
	 */
	AWSTUTORIAL_API bool ShowInView(bool bVertical, bool bPlanOnly, bool bPlanView, double ScreenLength, double MinLineLength, bool& bOutTextOnly);
}

/**
 * Architectural dimension lines drawn over the viewport: a line with an arrow at each end, extension lines to the measured
 * points and the value centred on the line in a box. Hit-test invisible; the owner feeds it positions every frame.
 */
UCLASS()
class AWSTUTORIAL_API UPlannerDimensionOverlay : public UWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dimensions")
	FLinearColor LineColor = FLinearColor(0.02f, 0.05f, 0.12f, 1.f);

	/** Light band under every line, so the lines read on dark and light surfaces alike. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dimensions")
	FLinearColor HaloColor = FLinearColor(1.f, 1.f, 1.f, 0.8f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dimensions")
	FLinearColor TextColor = FLinearColor(0.02f, 0.05f, 0.12f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dimensions")
	FLinearColor TextBackgroundColor = FLinearColor(1.f, 1.f, 1.f, 0.95f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dimensions")
	FSlateFontInfo Font;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dimensions")
	float LineThickness = 1.5f;

	UPlannerDimensionOverlay();

	/** Replaces the dimensions shown (overlay coordinates = the viewport's DPI-scaled coordinates). */
	void SetDimensions(const TArray<FPlannerScreenDimension>& InDimensions);

	const TArray<FPlannerScreenDimension>& GetDimensions() const { return Dimensions; }

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TArray<FPlannerScreenDimension> Dimensions;
	TSharedPtr<SPlannerDimensionOverlay> MyOverlay;
};
