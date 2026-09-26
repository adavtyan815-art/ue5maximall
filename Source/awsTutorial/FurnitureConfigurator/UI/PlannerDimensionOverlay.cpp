// Copyright 2026 MaxiMall. All Rights Reserved.

#include "FurnitureConfigurator/UI/PlannerDimensionOverlay.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

PlannerDimensionLayout::FResult PlannerDimensionLayout::Compute(const FPlannerScreenDimension& Dimension, const FVector2D& TextSize, const FStyle& Style)
{
	FResult Result;
	Result.BoxSize = TextSize + Style.TextPadding * 2.f;
	const FVector2D Mid = (Dimension.Start + Dimension.End) * 0.5f;
	Result.TextCenter = Mid;

	const FVector2D Span = Dimension.End - Dimension.Start;
	const double Length = Span.Size();
	auto ExtentAlong = [&Result](const FVector2D& Unit) { return FMath::Abs(Unit.X) * Result.BoxSize.X + FMath::Abs(Unit.Y) * Result.BoxSize.Y; };
	if (Dimension.bTextOnly || Length < 1.0)
	{
		// Beside its point, away from the measured points (a caption outside the wall, a height beside its jamb).
		const FVector2D Away = (Dimension.Start - Dimension.StartRef) + (Dimension.End - Dimension.EndRef);
		Result.bTextMovable = true;
		if (Away.SizeSquared() > 1.0)
		{
			Result.TextShiftDir = Away.GetSafeNormal();
			Result.TextCenter = Mid + Result.TextShiftDir * (0.5 * ExtentAlong(Result.TextShiftDir) + Style.Gap);
		}
		return Result;
	}
	Result.bDrawLine = true;
	const FVector2D Dir = Span / Length;

	// Text that does not fit between the arrows goes beside the line, on the side away from the measured points (where the
	// extension lines point); screen-up when there are none.
	FVector2D Across(-Dir.Y, Dir.X);
	const FVector2D Away = (Dimension.Start - Dimension.StartRef) + (Dimension.End - Dimension.EndRef);
	if (Away.SizeSquared() > 1.0)
	{
		if (FVector2D::DotProduct(Across, Away) < 0.0) Across = -Across;
	}
	else if (Across.Y > 0.0)
	{
		Across = -Across;
	}

	// The text box stays axis-aligned: its extent along and across the line.
	const double BoxAlong = ExtentAlong(Dir);
	const double BoxAcross = ExtentAlong(Across);
	const double ArrowRoom = 2.0 * Style.ArrowLength;
	Result.bTextOnLine = Length >= ArrowRoom + BoxAlong + 2.0 * Style.Gap;
	Result.bArrowsOutside = Length < ArrowRoom + 2.0;
	if (!Result.bTextOnLine)
	{
		Result.TextCenter = Mid + Across * (0.5 * BoxAcross + Style.Gap + Style.ArrowHalfWidth);
		Result.bTextMovable = true;
		Result.TextShiftDir = Across;
	}

	// Back: unit vector from the tip towards the arrow's base.
	auto MakeArrow = [&Style](const FVector2D& Tip, const FVector2D& Back, FVector2D Out[3])
	{
		const FVector2D Side(-Back.Y, Back.X);
		const FVector2D Base = Tip + Back * Style.ArrowLength;
		Out[0] = Tip;
		Out[1] = Base + Side * Style.ArrowHalfWidth;
		Out[2] = Base - Side * Style.ArrowHalfWidth;
	};
	if (Result.bArrowsOutside)
	{
		MakeArrow(Dimension.Start, -Dir, Result.StartArrow);
		MakeArrow(Dimension.End, Dir, Result.EndArrow);
		Result.LineFrom = Dimension.Start - Dir * (Style.ArrowLength + Style.ExtensionOvershoot);
		Result.LineTo = Dimension.End + Dir * (Style.ArrowLength + Style.ExtensionOvershoot);
	}
	else
	{
		MakeArrow(Dimension.Start, Dir, Result.StartArrow);
		MakeArrow(Dimension.End, -Dir, Result.EndArrow);
		Result.LineFrom = Dimension.Start;
		Result.LineTo = Dimension.End;
	}

	auto MakeExtension = [&Style](const FVector2D& Ref, const FVector2D& Tip, FVector2D Out[2])
	{
		const FVector2D ToTip = Tip - Ref;
		const double Distance = ToTip.Size();
		if (Distance < Style.ExtensionGap + 1.0) return false;
		const FVector2D Unit = ToTip / Distance;
		Out[0] = Ref + Unit * Style.ExtensionGap;
		Out[1] = Tip + Unit * Style.ExtensionOvershoot;
		return true;
	};
	Result.bHasStartExtension = MakeExtension(Dimension.StartRef, Dimension.Start, Result.StartExtension);
	Result.bHasEndExtension = MakeExtension(Dimension.EndRef, Dimension.End, Result.EndExtension);
	return Result;
}

FSlateRect PlannerDimensionLayout::TextBox(const FResult& Layout)
{
	const FVector2D Half = Layout.BoxSize * 0.5f;
	return FSlateRect(Layout.TextCenter - Half, Layout.TextCenter + Half);
}

void PlannerDimensionLayout::SeparateTexts(TArray<FResult>& Layouts, const FStyle& Style)
{
	auto Overlap = [](const FSlateRect& A, const FSlateRect& B)
	{
		// Boxes that only touch are fine.
		return A.Left < B.Right - 0.5f && B.Left < A.Right - 0.5f && A.Top < B.Bottom - 0.5f && B.Top < A.Bottom - 0.5f;
	};
	TArray<FSlateRect> Taken;
	for (const FResult& Layout : Layouts)
	{
		if (!Layout.bTextMovable && Layout.BoxSize.X > 0.f) Taken.Add(TextBox(Layout));
	}
	for (FResult& Layout : Layouts)
	{
		if (!Layout.bTextMovable || Layout.BoxSize.X <= 0.f) continue;
		const FVector2D Dir = Layout.TextShiftDir.GetSafeNormal();
		const double Step = FMath::Abs(Dir.X) * Layout.BoxSize.X + FMath::Abs(Dir.Y) * Layout.BoxSize.Y + Style.Gap;
		for (int32 Attempt = 0; Attempt < 8 && Taken.ContainsByPredicate([&](const FSlateRect& R) { return Overlap(R, TextBox(Layout)); }); ++Attempt)
		{
			Layout.TextCenter += Dir * Step;
		}
		Taken.Add(TextBox(Layout));
	}
}

bool PlannerDimensionLayout::ShowInView(bool bVertical, bool bPlanOnly, bool bPlanView, double ScreenLength, double MinLineLength, bool& bOutTextOnly)
{
	bOutTextOnly = false;
	if (bPlanView ? bVertical : bPlanOnly) return false;
	bOutTextOnly = bPlanOnly || (bVertical && ScreenLength < MinLineLength);
	return true;
}

/** Draws the dimensions; takes no space and no input. */
class SPlannerDimensionOverlay : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPlannerDimensionOverlay) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		SetCanTick(false);
	}

	void SetDimensions(const TArray<FPlannerScreenDimension>& InDimensions)
	{
		Dimensions = InDimensions;
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	void SetStyle(const UPlannerDimensionOverlay& Owner)
	{
		LineColor = Owner.LineColor;
		HaloColor = Owner.HaloColor;
		TextColor = Owner.TextColor;
		BoxFillColor = Owner.TextBackgroundColor;
		LineThickness = Owner.LineThickness;
		Font = Owner.Font;
		TextBoxBrush = FSlateRoundedBoxBrush(Owner.TextBackgroundColor, 3.f, Owner.LineColor, 1.f);
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D::ZeroVector;
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		if (Dimensions.Num() == 0 || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
		{
			return LayerId;
		}
		FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer();
		const TSharedRef<FSlateFontMeasure> FontMeasure = Renderer->GetFontMeasureService();
		const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry();
		const int32 HaloLayer = LayerId + 1;
		const int32 LineLayer = LayerId + 2;
		const int32 BoxLayer = LayerId + 3;
		const int32 TextLayer = LayerId + 4;

		auto AddLine = [&](const FVector2D& From, const FVector2D& To)
		{
			TArray<FVector2f> Points;
			Points.Add(FVector2f(From));
			Points.Add(FVector2f(To));
			FSlateDrawElement::MakeLines(OutDrawElements, HaloLayer, PaintGeometry, Points, ESlateDrawEffect::None, HaloColor, true, LineThickness + 2.f);
			FSlateDrawElement::MakeLines(OutDrawElements, LineLayer, PaintGeometry, MoveTemp(Points), ESlateDrawEffect::None, LineColor, true, LineThickness);
		};

		// Arrowheads are filled triangles, all in one batch.
		TArray<FSlateVertex> ArrowVertices;
		TArray<SlateIndex> ArrowIndices;
		const FSlateRenderTransform& RenderTransform = AllottedGeometry.GetAccumulatedRenderTransform();
		const FColor ArrowColor = LineColor.ToFColorSRGB();
		auto AddArrow = [&](const FVector2D (&Triangle)[3])
		{
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				ArrowIndices.Add((SlateIndex)ArrowVertices.Num());
				ArrowVertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RenderTransform, FVector2f(Triangle[Corner]), FVector2f(0.f, 0.f), ArrowColor));
			}
		};

		TArray<PlannerDimensionLayout::FResult> Layouts;
		TArray<FVector2D> TextSizes;
		for (const FPlannerScreenDimension& Dimension : Dimensions)
		{
			const FVector2D TextSize = Dimension.Text.IsEmpty() ? FVector2D::ZeroVector : FVector2D(FontMeasure->Measure(Dimension.Text, Font));
			PlannerDimensionLayout::FResult Layout = PlannerDimensionLayout::Compute(Dimension, TextSize, Style);
			if (Dimension.Text.IsEmpty()) Layout.BoxSize = FVector2D::ZeroVector;
			Layouts.Add(Layout);
			TextSizes.Add(TextSize);
		}
		PlannerDimensionLayout::SeparateTexts(Layouts, Style);

		for (int32 Index = 0; Index < Dimensions.Num(); ++Index)
		{
			const FPlannerScreenDimension& Dimension = Dimensions[Index];
			const PlannerDimensionLayout::FResult& Layout = Layouts[Index];
			const FVector2D& TextSize = TextSizes[Index];
			if (Layout.bDrawLine)
			{
				AddLine(Layout.LineFrom, Layout.LineTo);
				if (Layout.bHasStartExtension) AddLine(Layout.StartExtension[0], Layout.StartExtension[1]);
				if (Layout.bHasEndExtension) AddLine(Layout.EndExtension[0], Layout.EndExtension[1]);
				AddArrow(Layout.StartArrow);
				AddArrow(Layout.EndArrow);
			}
			if (!Dimension.Text.IsEmpty())
			{
				const FVector2D BoxTopLeft = Layout.TextCenter - Layout.BoxSize * 0.5f;
				FSlateDrawElement::MakeBox(OutDrawElements, BoxLayer,
					AllottedGeometry.ToPaintGeometry(FVector2f(Layout.BoxSize), FSlateLayoutTransform(FVector2f(BoxTopLeft))),
					&TextBoxBrush, ESlateDrawEffect::None, BoxFillColor * InWidgetStyle.GetColorAndOpacityTint());
				const FVector2D TextTopLeft = Layout.TextCenter - TextSize * 0.5f;
				FSlateDrawElement::MakeText(OutDrawElements, TextLayer,
					AllottedGeometry.ToPaintGeometry(FVector2f(TextSize), FSlateLayoutTransform(FVector2f(TextTopLeft))),
					Dimension.Text, Font, ESlateDrawEffect::None, TextColor);
			}
		}

		if (ArrowVertices.Num() > 0)
		{
			const FSlateResourceHandle WhiteBrush = Renderer->GetResourceHandle(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
			FSlateDrawElement::MakeCustomVerts(OutDrawElements, LineLayer, WhiteBrush, ArrowVertices, ArrowIndices, nullptr, 0, 0);
		}
		return TextLayer;
	}

private:
	TArray<FPlannerScreenDimension> Dimensions;
	PlannerDimensionLayout::FStyle Style;
	FLinearColor LineColor = FLinearColor::Black;
	FLinearColor HaloColor = FLinearColor::White;
	FLinearColor TextColor = FLinearColor::Black;
	FLinearColor BoxFillColor = FLinearColor::White;
	float LineThickness = 1.5f;
	FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 11);
	FSlateRoundedBoxBrush TextBoxBrush = FSlateRoundedBoxBrush(FLinearColor::White, 3.f, FLinearColor::Black, 1.f);
};

UPlannerDimensionOverlay::UPlannerDimensionOverlay()
{
	Font = FCoreStyle::GetDefaultFontStyle("Bold", 11);
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UPlannerDimensionOverlay::SetDimensions(const TArray<FPlannerScreenDimension>& InDimensions)
{
	if (InDimensions == Dimensions)
	{
		return; // the same values in the same places, frame after frame: nothing to repaint
	}
	Dimensions = InDimensions;
	if (MyOverlay.IsValid())
	{
		MyOverlay->SetDimensions(Dimensions);
	}
}

TSharedRef<SWidget> UPlannerDimensionOverlay::RebuildWidget()
{
	MyOverlay = SNew(SPlannerDimensionOverlay);
	return MyOverlay.ToSharedRef();
}

void UPlannerDimensionOverlay::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (MyOverlay.IsValid())
	{
		MyOverlay->SetStyle(*this);
		MyOverlay->SetDimensions(Dimensions);
	}
}

void UPlannerDimensionOverlay::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyOverlay.Reset();
}
