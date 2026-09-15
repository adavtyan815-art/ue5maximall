// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoomPlannerTypes.h"
#include "PlannerRoomLightActor.generated.h"

class UProceduralMeshComponent;
class URectLightComponent;
class UMaterialInstanceDynamic;
class UTexture2D;

/** Oriented rectangle enclosing a polygon in world XY (cm). AxisY = AxisX rotated +90°; Extent = full side lengths along AxisX / AxisY. */
struct FPlannerOrientedRect
{
	FVector2D Center = FVector2D::ZeroVector;
	FVector2D AxisX = FVector2D(1.f, 0.f);
	FVector2D AxisY = FVector2D(0.f, 1.f);
	FVector2D Extent = FVector2D::ZeroVector;
	float YawDeg = 0.f; // angle of AxisX in the world XY plane
};

/**
 * The Room Planner's ceiling light for ONE detected room: exactly one real light source plus the visible
 * luminous panel, both with the same polygon shape.
 *
 *   • Panel polygon: the room polygon inset from the walls by PolygonInsetCm. A room with a feature narrower than
 *     twice the inset uses a smaller inset (the inset must neither fold nor self-intersect), so rectangle →
 *     rectangle, trapezoid → trapezoid, triangle → triangle, any polygon → the same polygon.
 *   • Light: ONE URectLightComponent facing down just under the panel. Its rectangle is the panel polygon's
 *     minimum-area oriented bounding rectangle (aligned with the room; for rectangular rooms it IS the panel) and
 *     its SourceTexture is a runtime mask of the panel polygon rasterized in the light's own UV frame, so texels
 *     outside the polygon emit nothing. Intensity is divided by the mask coverage because UE multiplies the
 *     emission by the texture without normalizing it. Shadows are ray traced; their rays sample the whole
 *     rectangle and ignore the mask, so the intensity is also divided by the estimated share of the rectangle that
 *     lies inside the room (the walls block the rest).
 *   • Visible panel: the panel polygon, opaque unlit emissive, luminance derived from the light's emitted flux
 *     (flux / (π × panel area)) × EmissiveIntensity, delivered as an HDR float texture.
 *
 * Engine behaviour (UE 5.6 source): the path tracer clips emission to the mask texel by texel; the deferred
 * renderer and Lumen multiply the rectangle's lighting by one filtered mask lookup per shading point, which keeps
 * the flux and the overall distribution but not a hard polygon boundary of the emitter.
 *
 * Spawned, reused, rebuilt and destroyed by ARoomPlannerManager (one actor per room, all machines, never replicated).
 */
UCLASS(NotPlaceable, Transient)
class AWSTUTORIAL_API APlannerRoomLightActor : public AActor
{
	GENERATED_BODY()

public:
	APlannerRoomLightActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RoomLight")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Visible luminous panel (panel polygon). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RoomLight")
	TObjectPtr<UProceduralMeshComponent> SurfaceMesh;

	/** Emissive material instance of the panel. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RoomLight")
	TObjectPtr<UMaterialInstanceDynamic> SurfaceMaterial;

	/** The room's single real light source. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RoomLight")
	TObjectPtr<URectLightComponent> Light;

	/** Polygon mask of the light's emitting rectangle (white inside the panel polygon, black outside, linear). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RoomLight")
	TObjectPtr<UTexture2D> MaskTexture;

	/** 1×1 float texture holding the panel's HDR emissive colour (cd/m²). */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> EmissiveTexture;

	UPROPERTY(BlueprintReadOnly, Category = "RoomLight")
	int32 RoomID = 0;

	UPROPERTY(BlueprintReadOnly, Category = "RoomLight")
	float CeilingHeightCm = 280.f;

	UPROPERTY(BlueprintReadOnly, Category = "RoomLight")
	float AreaM2 = 0.f;

	/** Room polygon this light was built from (world XY, cm). */
	UPROPERTY(BlueprintReadOnly, Category = "RoomLight")
	TArray<FVector2D> Polygon;

	/** Panel polygon = emitting shape of the light (world XY, cm). */
	UPROPERTY(BlueprintReadOnly, Category = "RoomLight")
	TArray<FVector2D> SurfacePolygon;

	/** Inset actually used for the panel (cm); smaller than the setting for rooms too narrow for it. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomLight")
	float UsedInsetCm = 0.f;

	/** Light rectangle size (cm): SourceWidth (shorter side) and SourceHeight. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomLight")
	FVector2D LightRectSizeCm = FVector2D::ZeroVector;

	/** Mean mask value over the light rectangle (1 = the polygon fills the rectangle). */
	UPROPERTY(BlueprintReadOnly, Category = "RoomLight")
	float MaskCoverage = 0.f;

	/** Estimated share of the light rectangle inside the room: ray-traced shadow compensation (1 for rectangular rooms). */
	UPROPERTY(BlueprintReadOnly, Category = "RoomLight")
	float ShadowVisibility = 1.f;

	/** Luminous flux actually leaving the light (lm). */
	UPROPERTY(BlueprintReadOnly, Category = "RoomLight")
	float EmittedLumens = 0.f;

	/** Luminance of the panel (cd/m²). */
	UPROPERTY(BlueprintReadOnly, Category = "RoomLight")
	float PanelLuminanceNits = 0.f;

	/** Content signature of the last Build (room polygon, ceiling height, settings); equal signature = identical light. */
	uint32 BuildSignature = 0;

	/** (Re)builds panel and light from the room. Safe to call again; the previous light and textures are released. */
	void Build(const FRoomData& Room, const FPlannerRoomLightSettings& Settings);

	/** 2D mode hides the whole system (panel + light) without rebuilding it. */
	void SetShown(bool bShown);

	UFUNCTION(BlueprintPure, Category = "RoomLight")
	int32 GetLightCount() const { return Light ? 1 : 0; }

	/** Signature Build would store for this room and these settings. */
	static uint32 ComputeBuildSignature(const FRoomData& Room, const FPlannerRoomLightSettings& Settings);

	/** Hash of the settings alone (the manager uses it to notice direct edits of its settings). */
	static uint32 ComputeSettingsHash(const FPlannerRoomLightSettings& Settings);

	// ── Geometry helpers (pure) ──

	/**
	 * Polygon offset towards the interior by InsetCm (either winding), mitred corners with a clamp. Empty when the
	 * result collapses, inverts, reverses any edge or self-intersects (features narrower than twice the inset).
	 */
	static TArray<FVector2D> InsetPolygon(const TArray<FVector2D>& Polygon, float InsetCm);

	/** Ear clipping for a simple polygon of either winding; OutTris = index triplets in the polygon's winding. */
	static void TriangulatePolygon(const TArray<FVector2D>& Polygon, TArray<int32>& OutTris);

	/** Convex hull (Andrew's monotone chain), counter-clockwise, without collinear points. */
	static TArray<FVector2D> ConvexHull(const TArray<FVector2D>& Points);

	/** Minimum-area oriented bounding rectangle (tested over the convex hull's edge directions), with Extent.Y <= Extent.X. */
	static bool ComputeOrientedRect(const TArray<FVector2D>& Polygon, FPlannerOrientedRect& OutRect);

	/**
	 * Anti-aliased coverage mask of Polygon in the UV frame of a rect light placed at Rect with rotation
	 * (Pitch −90°, Yaw Rect.YawDeg) and SourceWidth = Rect.Extent.Y, SourceHeight = Rect.Extent.X.
	 * Writes Res×Res BGRA8 (row 0 first) and returns the mean linear coverage.
	 */
	static float RasterizeLightMask(const TArray<FVector2D>& Polygon, const FPlannerOrientedRect& Rect, int32 Res, TArray<uint8>& OutBGRA);

	static float SignedArea(const TArray<FVector2D>& Polygon);

private:
	/** Ear-clip triangles of SurfacePolygon (validated: exactly N − 2 triangles). */
	TArray<int32> SurfaceTris;

	void BuildLight(const FPlannerRoomLightSettings& Settings, const FPlannerOrientedRect& Rect);
	void BuildSurface(const FPlannerRoomLightSettings& Settings);
	void DestroyLight();

	static UTexture2D* CreateLinearTexture(int32 SizeX, int32 SizeY, const TArray<uint8>& BGRA);
	static UTexture2D* CreateHdrColorTexture(const FLinearColor& Color);
};
