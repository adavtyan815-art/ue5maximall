// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RoomPlannerTypes.h"
#include "PlannerOpeningBuilder.h"

/**
 * Look of one door / window / archway: leaf design, glass and the colours of frame, leaf and hardware.
 * Geometry is procedural and follows the opening's size, so every style fits every opening.
 */
struct AWSTUTORIAL_API FPlannerOpeningStyle
{
	FName ID;
	/** Label shown in the planner UI. */
	FString DisplayName;
	EOpeningType Type = EOpeningType::Door;
	EPlannerLeafDesign LeafDesign = EPlannerLeafDesign::Flush;
	EPlannerOpeningMaterial GlassKind = EPlannerOpeningMaterial::Glass;
	FLinearColor FrameColor = FLinearColor(0.86f, 0.85f, 0.82f);
	FLinearColor LeafColor = FLinearColor(0.82f, 0.81f, 0.78f);
	FLinearColor MetalColor = FLinearColor(0.035f, 0.035f, 0.037f);
	/** Doors / archways: boards lining the reveals. */
	bool bLining = true;
	/** Doors / archways: moulded casing (architrave) on both wall faces. */
	bool bCasing = true;
	/** Doors: raised threshold (else a flush floor fill). */
	bool bThreshold = true;
};

/**
 * Built-in style catalog. An opening stores the style ID in FWallOpening::Style (serialized as "style"); an empty or
 * unknown ID, or a style of another opening type, resolves to the type's default style, so old layouts keep loading.
 * Style IDs are part of the saved layout format: never rename one.
 */
namespace PlannerOpeningStyles
{
	AWSTUTORIAL_API const TArray<FPlannerOpeningStyle>& All();
	AWSTUTORIAL_API const FPlannerOpeningStyle* Find(FName StyleID);
	AWSTUTORIAL_API const FPlannerOpeningStyle& GetDefault(EOpeningType Type);
	AWSTUTORIAL_API const FPlannerOpeningStyle& Resolve(EOpeningType Type, FName StyleID);
	AWSTUTORIAL_API bool IsValidFor(EOpeningType Type, FName StyleID);
}
