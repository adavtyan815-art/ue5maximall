// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PlannerOpeningBuilder.h"

struct FProcMeshSection;

/**
 * Geometry of the 3D selection overlay (ARoomPlannerManager::RebuildSelectionOverlay). The project renders without custom depth
 * (r.CustomDepth 0), so the selection is drawn as extra geometry: a thin frame on the selected surface's borders and around its
 * openings (lit, so it reads the same at every exposure), and a faint pulsing tint over the whole surface (additive), which says
 * "selected" wherever the camera looks, including the middle of a wall whose borders are off-screen. The surface's own finish stays
 * visible under both.
 */
namespace PlannerSelectionOverlay
{
	/** A hole in a wall face, along the wall (cm from the start node) and in height. */
	struct FFaceHole
	{
		float Start = 0.f;
		float End = 0.f;
		float Bottom = 0.f;
		float Top = 0.f;
		/** A door / archway standing on the floor: no frame along its bottom and none along the face's bottom across it. */
		bool bOnFloor = false;
	};

	/**
	 * Appends the triangles of Section whose first vertex normal points along FacingFilter (dot > 0.5; zero vector = every
	 * triangle), each vertex pushed Lift cm along its own normal, so the copy floats just in front of the surface it covers.
	 */
	AWSTUTORIAL_API void AppendLiftedSection(const FProcMeshSection& Section, float Lift, const FVector& FacingFilter, FPlannerMeshBuffers& Out);

	/**
	 * Frame of one wall face: a band of width Band inside the face's outline (along [ExtentLo, ExtentHi], height [0, Height]) and
	 * one around each hole on the face. FaceOrigin is the face line at along = 0; AxisAlong runs along the wall; OutNormal points out
	 * of the face; the band floats Lift cm in front of it. bFaceOutline false: only the bands around the holes (a selected opening).
	 * HoleMargin: the bands around a hole stand this far out from its edges (clear of the casing / frame dressing the hole).
	 */
	AWSTUTORIAL_API void AddWallFaceFrame(FPlannerMeshBuffers& Out, const FVector2D& FaceOrigin, const FVector2D& AxisAlong, const FVector2D& OutNormal,
	                                      float ExtentLo, float ExtentHi, float Height, const TArray<FFaceHole>& Holes, float Band, float Lift,
	                                      bool bFaceOutline = true, float HoleMargin = 0.f);

	/** Band of width Band inside a closed polygon (either winding) at height Z, facing Normal (+Z floor, −Z ceiling). */
	AWSTUTORIAL_API void AddPolygonFrame(FPlannerMeshBuffers& Out, const TArray<FVector2D>& Polygon, float Z, float Band, const FVector& Normal);

	/**
	 * Luminance (cd/m²) that shows as Fraction of display white at a fixed exposure of EV100: the planner's EV100 6.8 has its white
	 * at 2^6.8 ≈ 111 cd/m² (see the exterior palette).
	 */
	inline float LuminanceForFraction(float EV100, float Fraction)
	{
		return FMath::Pow(2.f, EV100) * Fraction;
	}

	/** Pulse of the tint between MinFraction and MaxFraction with period PeriodSeconds (smooth, starts at the maximum). */
	inline float PulseFraction(float TimeSeconds, float PeriodSeconds, float MinFraction, float MaxFraction)
	{
		if (PeriodSeconds <= KINDA_SMALL_NUMBER) return MaxFraction;
		const float Phase = FMath::Fmod(TimeSeconds, PeriodSeconds) / PeriodSeconds;
		const float Wave = 0.5f + 0.5f * FMath::Cos(2.f * PI * Phase);
		return FMath::Lerp(MinFraction, MaxFraction, Wave);
	}
}
