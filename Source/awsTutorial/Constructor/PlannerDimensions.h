// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Clear distances between planner elements for the dimension lines (pure geometry, no world access). */
namespace PlannerDimensions
{
	/** Sides of a footprint in its own axes. */
	enum class EFootprintSide : uint8
	{
		PlusX,
		MinusX,
		PlusY,
		MinusY,
	};

	struct AWSTUTORIAL_API FFootprintGap
	{
		EFootprintSide Side = EFootprintSide::PlusX;
		/** Middle of the footprint side. */
		FVector2D From = FVector2D::ZeroVector;
		/** Where the gap meets the nearest wall face in front of that side. */
		FVector2D To = FVector2D::ZeroVector;
		float Distance = 0.f;
	};

	/**
	 * The clear gap from each side of a footprint (an oriented rectangle: centre, unit X axis, half size) to the nearest wall in front
	 * of that side: the smallest distance, square to the side, to any wall in the strip the side faces (From lies on the side where
	 * that wall is nearest; the middle of the side when a wall faces all of it). Walls are given by their outlines (any simple
	 * polygon). A side flush with a wall (closer than MinGap), lying inside one, or seeing none within MaxGap has no gap. A wall
	 * touching only the footprint's corner does not count; a part of a side inside a wall (an object pushed into it) is left out.
	 */
	AWSTUTORIAL_API TArray<FFootprintGap> FootprintGapsToWalls(const FVector2D& Center, const FVector2D& AxisX, const FVector2D& HalfSize,
		const TArray<TArray<FVector2D>>& WallOutlines, float MinGap, float MaxGap);
}
