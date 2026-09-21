// Copyright 2026 MaxiMall. All Rights Reserved.

#include "Constructor/PlannerDimensions.h"

namespace PlannerDimensionsInternal
{
	double Cross2(const FVector2D& A, const FVector2D& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	/** Even-odd rule; points on an edge may count either way. */
	bool IsInsidePolygon(const FVector2D& P, const TArray<FVector2D>& Polygon)
	{
		bool bInside = false;
		for (int32 i = 0, j = Polygon.Num() - 1; i < Polygon.Num(); j = i++)
		{
			const FVector2D& A = Polygon[i];
			const FVector2D& B = Polygon[j];
			if ((A.Y > P.Y) != (B.Y > P.Y) && P.X < (B.X - A.X) * (P.Y - A.Y) / (B.Y - A.Y) + A.X)
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	/** Distance along the ray to the first outline edge it crosses; negative when it crosses none. */
	double RayToOutline(const FVector2D& Origin, const FVector2D& Dir, const TArray<FVector2D>& Outline)
	{
		double Best = -1.0;
		for (int32 i = 0; i < Outline.Num(); ++i)
		{
			const FVector2D A = Outline[i];
			const FVector2D Edge = Outline[(i + 1) % Outline.Num()] - A;
			const double Denom = Cross2(Dir, Edge);
			if (FMath::Abs(Denom) < 1.e-9) continue; // parallel
			const FVector2D ToA = A - Origin;
			const double T = Cross2(ToA, Edge) / Denom;  // along the ray
			const double S = Cross2(ToA, Dir) / Denom;   // along the edge
			if (T >= 0.0 && S >= 0.0 && S <= 1.0 && (Best < 0.0 || T < Best))
			{
				Best = T;
			}
		}
		return Best;
	}

	/** True when segment P0-P1 crosses or touches an outline edge. */
	bool SegmentTouchesOutline(const FVector2D& P0, const FVector2D& P1, const TArray<FVector2D>& Outline)
	{
		const FVector2D D = P1 - P0;
		for (int32 i = 0; i < Outline.Num(); ++i)
		{
			const FVector2D A = Outline[i];
			const FVector2D Edge = Outline[(i + 1) % Outline.Num()] - A;
			const double Denom = Cross2(D, Edge);
			const FVector2D ToA = A - P0;
			if (FMath::Abs(Denom) < 1.e-9)
			{
				// Parallel: touching only when on the same line and overlapping.
				if (FMath::Abs(Cross2(ToA, D)) > 1.e-6 * FMath::Max(1.0, D.Size())) continue;
				const double Len2 = D.SizeSquared();
				if (Len2 < 1.e-12) continue;
				const double T0 = FVector2D::DotProduct(ToA, D) / Len2;
				const double T1 = FVector2D::DotProduct(ToA + Edge, D) / Len2;
				if (FMath::Max(T0, T1) >= 0.0 && FMath::Min(T0, T1) <= 1.0) return true;
				continue;
			}
			const double T = Cross2(ToA, Edge) / Denom;
			const double S = Cross2(ToA, D) / Denom;
			if (T >= 0.0 && T <= 1.0 && S >= 0.0 && S <= 1.0) return true;
		}
		return false;
	}
}

TArray<PlannerDimensions::FFootprintGap> PlannerDimensions::FootprintGapsToWalls(const FVector2D& Center, const FVector2D& AxisX,
	const FVector2D& HalfSize, const TArray<TArray<FVector2D>>& WallOutlines, float MinGap, float MaxGap)
{
	TArray<FFootprintGap> Gaps;
	const FVector2D X = AxisX.GetSafeNormal();
	if (X.IsNearlyZero()) return Gaps;
	const FVector2D Y(-X.Y, X.X);

	auto InsideAnyWall = [&WallOutlines](const FVector2D& P)
	{
		return WallOutlines.ContainsByPredicate([&P](const TArray<FVector2D>& Outline) { return Outline.Num() >= 3 && PlannerDimensionsInternal::IsInsidePolygon(P, Outline); });
	};

	// Each side: its outward normal, its distance from the centre and its half length.
	const struct { EFootprintSide Side; FVector2D Normal; double Half; double HalfLength; } Sides[] = {
		{ EFootprintSide::PlusX, X, HalfSize.X, HalfSize.Y },
		{ EFootprintSide::MinusX, -X, HalfSize.X, HalfSize.Y },
		{ EFootprintSide::PlusY, Y, HalfSize.Y, HalfSize.X },
		{ EFootprintSide::MinusY, -Y, HalfSize.Y, HalfSize.X },
	};
	for (const auto& Side : Sides)
	{
		const FVector2D Mid = Center + Side.Normal * Side.Half;
		const FVector2D Along(-Side.Normal.Y, Side.Normal.X);
		// 1 cm in from each end: a wall merely touching the footprint's corner does not face the side.
		const double Reach = FMath::Max(0.0, Side.HalfLength - 1.0);

		// A side inside a wall has no gap to show.
		if (InsideAnyWall(Mid))
		{
			continue;
		}

		// The clearance is the smallest distance, square to the side, to a wall in the strip in front of it. Over a polygon that
		// minimum lies on a ray from an end of the strip or at an outline corner inside it. The middle goes first and wins ties, so
		// a wall in front of the whole side gets its line from the middle. A part of the side that is inside a wall (an object pushed
		// into it) is left out: its rays would only measure through that wall.
		double Nearest = TNumericLimits<double>::Max();
		FVector2D NearestFrom = Mid;
		auto Consider = [&](double Distance, const FVector2D& From, bool bPreferred)
		{
			if (Distance >= 0.0 && Distance < Nearest - (bPreferred ? 0.0 : 0.01))
			{
				Nearest = Distance;
				NearestFrom = From;
			}
		};
		const FVector2D EndA = Mid - Along * Reach;
		const FVector2D EndB = Mid + Along * Reach;
		const bool bEndAInWall = InsideAnyWall(EndA);
		const bool bEndBInWall = InsideAnyWall(EndB);
		for (const TArray<FVector2D>& Outline : WallOutlines)
		{
			if (Outline.Num() < 3) continue;
			Consider(PlannerDimensionsInternal::RayToOutline(Mid, Side.Normal, Outline), Mid, true);
		}
		for (const TArray<FVector2D>& Outline : WallOutlines)
		{
			if (Outline.Num() < 3) continue;
			if (!bEndAInWall) Consider(PlannerDimensionsInternal::RayToOutline(EndA, Side.Normal, Outline), EndA, false);
			if (!bEndBInWall) Consider(PlannerDimensionsInternal::RayToOutline(EndB, Side.Normal, Outline), EndB, false);
			for (const FVector2D& Corner : Outline)
			{
				// A corner on the side itself (a wall flush with part of it) counts as touching.
				const double Across = FVector2D::DotProduct(Corner - Mid, Along);
				const double Ahead = FVector2D::DotProduct(Corner - Mid, Side.Normal);
				if (FMath::Abs(Across) < Reach && Ahead > -0.5 && !InsideAnyWall(Mid + Along * Across))
				{
					Consider(FMath::Max(Ahead, 0.0), Mid + Along * Across, false);
				}
			}
		}
		if (Nearest < MinGap || Nearest > MaxGap) continue;

		FFootprintGap Gap;
		Gap.Side = Side.Side;
		Gap.From = NearestFrom;
		Gap.To = NearestFrom + Side.Normal * Nearest;
		Gap.Distance = (float)Nearest;
		Gaps.Add(Gap);
	}
	return Gaps;
}
