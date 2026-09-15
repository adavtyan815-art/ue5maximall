// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "RoomPlannerTypes.h"

class UTexture2D;

/** Geometry of one procedural mesh section. Every vertex carries a normal, a UV (1 UV = 1 m) and a tangent. */
struct AWSTUTORIAL_API FPlannerMeshBuffers
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FProcMeshTangent> Tangents;

	bool IsEmpty() const { return Triangles.Num() == 0; }
	void Reset();
};

/** Material role of a piece of door / window dressing. */
enum class EPlannerOpeningMaterial : uint8
{
	Frame,          // lining, casing, window frame, sill board (paint)
	Leaf,           // door leaf / window sash body (paint, or the manager's LeafMaterial)
	Glass,          // clear thin-translucent glass
	FrostedGlass,   // satin / frosted glass
	Metal,          // handles, hinges
	Threshold,      // door threshold, archway floor fill
};

/** Construction of a door leaf. */
enum class EPlannerLeafDesign : uint8
{
	Flush,       // plain slab
	TwoPanel,    // stiles and rails with two recessed panels
	GlazedTop,   // lower recessed panel, glass in the upper field
	FullGlass,   // narrow frame around one tall glass field
};

/** Geometry grouped into one section per (material role, colour). */
struct AWSTUTORIAL_API FPlannerSectionedMesh
{
	struct FSection
	{
		EPlannerOpeningMaterial Kind = EPlannerOpeningMaterial::Frame;
		FLinearColor Color = FLinearColor::White;
		FPlannerMeshBuffers Buffers;
	};

	/** Indirect storage: references returned by Get stay valid when later calls add sections. */
	TIndirectArray<FSection> Sections;

	FPlannerMeshBuffers& Get(EPlannerOpeningMaterial Kind, const FLinearColor& Color);
	bool IsEmpty() const;
};

/** Face selection for AddBox (local axes of the box frame). */
namespace EPlannerBoxFace
{
	enum Type : uint8
	{
		NegX = 1 << 0,
		PosX = 1 << 1,
		NegY = 1 << 2,
		PosY = 1 << 3,
		NegZ = 1 << 4,
		PosZ = 1 << 5,
		All = 0x3F,
	};
}

/**
 * Procedural geometry primitives for the Room Planner.
 *
 * Facing rule (UE procedural meshes, KismetProceduralMeshLibrary box reference): a triangle (a, b, c) is front-facing on
 * the side OPPOSITE cross(b - a, c - a). Every primitive here takes the direction a face must point to and chooses the
 * winding from it, so the supplied normal and the rendered front face can never disagree (the defect that made opening
 * reveals speckled and door leaves black).
 */
namespace PlannerMeshBuilder
{
	/** Planar convex quad V0..V3 (in order around the quad) facing Normal. UVs follow V0→V1 (U) and V0→V3 (V) in cm / UVScale. */
	AWSTUTORIAL_API void AddQuad(FPlannerMeshBuffers& Out, const FVector& V0, const FVector& V1, const FVector& V2, const FVector& V3,
	                             const FVector& Normal, float UVScale = 100.f);

	/** Same as AddQuad with explicit UVs. */
	AWSTUTORIAL_API void AddQuadUV(FPlannerMeshBuffers& Out, const FVector& V0, const FVector& V1, const FVector& V2, const FVector& V3,
	                               const FVector& Normal, const FVector2D& UV0, const FVector2D& UV1, const FVector2D& UV2, const FVector2D& UV3);

	/** Triangle facing Normal with explicit UVs. */
	AWSTUTORIAL_API void AddTriangle(FPlannerMeshBuffers& Out, const FVector& A, const FVector& B, const FVector& C, const FVector& Normal,
	                                 const FVector2D& UVA, const FVector2D& UVB, const FVector2D& UVC);

	/**
	 * Box in a local frame: Origin + AxisX * x + AxisY * y + AxisZ * z for x, y, z between Min and Max (axes orthonormal).
	 * FaceMask (EPlannerBoxFace) skips faces that are always hidden.
	 */
	AWSTUTORIAL_API void AddBox(FPlannerMeshBuffers& Out, const FVector& Origin, const FVector& AxisX, const FVector& AxisY, const FVector& AxisZ,
	                            FVector Min, FVector Max, uint8 FaceMask = EPlannerBoxFace::All);

	/** Axis-aligned box in the mesh's own space. */
	AWSTUTORIAL_API void AddLocalBox(FPlannerMeshBuffers& Out, const FVector& Min, const FVector& Max, uint8 FaceMask = EPlannerBoxFace::All);

	/** Flat, upward-facing ring segment around Center from direction From turning toward Toward by AngleRad (plan swing arc). */
	AWSTUTORIAL_API void AddArcStrip(FPlannerMeshBuffers& Out, const FVector2D& Center, const FVector2D& From, const FVector2D& Toward,
	                                 float AngleRad, float InnerRadius, float OuterRadius, float Z, int32 Segments);

	/** Disc at height Z facing up; U runs from the centre (0) to the rim (1). */
	AWSTUTORIAL_API void AddDisc(FPlannerMeshBuffers& Out, const FVector2D& Center, float Radius, float Z, int32 Segments);

	/** Cylinder wall seen from INSIDE (faces point to the axis) from Z0 to Z1; V runs from Z1 (0) to Z0 (1). Optional inward cap at Z1 (V = 0). */
	AWSTUTORIAL_API void AddInwardCylinder(FPlannerMeshBuffers& Out, const FVector2D& Center, float Radius, float Z0, float Z1, int32 Segments, bool bTopCap);

	/**
	 * Diagnostics / tests: number of triangles whose rendered front face points away from their vertex normals
	 * (plus triangles with invalid indices). 0 for correct geometry.
	 */
	AWSTUTORIAL_API int32 CountMisorientedTriangles(const FPlannerMeshBuffers& Buffers);
}

/** Parameters of a door leaf / window sash in its own local space (see BuildLeaf). */
struct AWSTUTORIAL_API FPlannerLeafParams
{
	EOpeningType Type = EOpeningType::Door;
	EPlannerLeafDesign Design = EPlannerLeafDesign::Flush;
	/** Glass role used by glazed designs and window sashes (Glass or FrostedGlass). */
	EPlannerOpeningMaterial GlassKind = EPlannerOpeningMaterial::Glass;
	float Width = 80.f;
	float Height = 200.f;
	float Thickness = 4.f;
	/** +1 or -1: the body spans local Y from 0 (hinge-side swing face) to BodySign * Thickness. */
	float BodySign = -1.f;
	/** Door handle height above the leaf bottom (cm). */
	float HandleZ = 97.f;
	/** Windows: true when the swing face (local Y = 0) faces the room (inward opening); the handle goes on the room face. */
	bool bSwingFaceIsInterior = true;
	FLinearColor LeafColor = FLinearColor(0.82f, 0.81f, 0.78f);
	FLinearColor MetalColor = FLinearColor(0.035f, 0.035f, 0.037f);
};

namespace PlannerOpeningGeometry
{
	/**
	 * Door leaf or window sash in local space: X from the hinge edge (0) to the latch edge (Width), Y through the thickness,
	 * Z up from the bottom (0). The hinge axis is the local Z axis, so the owning component rotates it around its origin.
	 * Doors: Params.Design (panels keep fixed margins and fall back to a flush slab when the leaf is too small), lever handles
	 * on both faces, hinge knuckles on the axis. Windows: sash frame, glass, handle on the swing face.
	 */
	AWSTUTORIAL_API void BuildLeaf(const FPlannerLeafParams& Params, FPlannerSectionedMesh& Out);

	/**
	 * Casing (architrave) around an opening on one wall face: two legs and a head joined with 45° mitres, profile Width × Depth
	 * with a small chamfer on the outer front edge. FaceOrigin is the face plane at along = 0, z = 0; AxisAlong runs along the
	 * wall, AxisOut points out of the face. InnerStart / InnerEnd / InnerTopZ are the casing's inner edges (along, along, height);
	 * the legs stand on BottomZ. Hidden faces (back, mitre joints, bottom ends) are not generated.
	 */
	AWSTUTORIAL_API void AddMitredCasing(FPlannerMeshBuffers& Out, const FVector& FaceOrigin, const FVector& AxisAlong, const FVector& AxisOut,
	                                     float InnerStart, float InnerEnd, float BottomZ, float InnerTopZ, float Width, float Depth);
}

namespace PlannerRuntimeTextures
{
	/** 1×1 half-float texture (HDR values allowed, e.g. emissive cd/m²). */
	AWSTUTORIAL_API UTexture2D* CreateHdrColor(const FLinearColor& Color);

	/** SizeX × SizeY half-float texture filled row by row (row 0 first); Pixels.Num() must be SizeX * SizeY. Bilinear, clamped. */
	AWSTUTORIAL_API UTexture2D* CreateHdrPixels(int32 SizeX, int32 SizeY, const TArray<FLinearColor>& Pixels);
}
