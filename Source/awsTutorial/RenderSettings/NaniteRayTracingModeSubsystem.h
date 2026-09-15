// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "NaniteRayTracingModeSubsystem.generated.h"

/**
 * Ray tracing against the real Nanite geometry instead of the Nanite fallback mesh: r.RayTracing.Nanite.Mode 1
 * ("streamed out mesh") for the whole game session.
 *
 * Why: ray-traced shadows (the Room Planner's room lights force them) and Lumen hardware ray tracing trace the
 * fallback mesh by default. On furniture with deep tufts and creases the fallback bridges the recesses (1.4–4.8 cm
 * above the rendered surface on SM_Sofa-Blazer-283), while a shadow ray ignores back faces only for its first 1 cm
 * (r.RayTracing.Shadows.AvoidSelfIntersectionTraceDistance). Every shadow ray from those pixels therefore hits the
 * fallback's underside and the recess turns black. In mode 1 the BLAS is built from the resident Nanite clusters
 * (r.RayTracing.Nanite.CutError 0 = the finest clusters in memory), which is at least as fine as the rasterized cut.
 *
 * Engine behaviour (UE 5.6 source):
 *   • Every Set of the cvar, even to the value it already has, recreates the render state of every component in the
 *     process and refreshes the ray-tracing mesh command cache; Nanite meshes then stay out of ray tracing until
 *     their stream-out BLAS is built (a few frames). The mode is therefore set ONCE when the game instance starts
 *     (before the first map of a packaged game), never per planner session or 2D/3D switch.
 *   • Cost with the default budgets (r.RayTracing.Nanite.StreamOut.MaxNumVertices / MaxNumIndices = 16M / 64M): one
 *     stream-out vertex + index buffer pair is 448 MiB. The engine reallocates the pair on every update batch, so two
 *     pairs (~0.9 GiB) plus staging stay live, and older pairs can stay alive after idle periods because BLASes keep
 *     referencing the buffers they were built from. BLASes are uncompacted; Nanite page streaming rebuilds them.
 *   • Components that evaluate World Position Offset in ray tracing stay on the fallback mesh (engine TODO).
 *
 * Not created on processes that never render (dedicated / GameLift servers). A value typed in the console
 * (SetByConsole) outranks code and is left alone. In the editor the override is removed again when the last PIE game
 * instance ends (Unset, which restores the previous value and its owner), so the editor viewport returns to the
 * project value.
 */
UCLASS()
class AWSTUTORIAL_API UNaniteRayTracingModeSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	/** This game instance holds one of the process-wide requests (several PIE game instances can share one process). */
	bool bHoldsRequest = false;
};
