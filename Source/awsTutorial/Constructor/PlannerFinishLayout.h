// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PlannerOpeningBuilder.h"

/** One wall for the face tile-grid pass: centreline and the corner points of both faces at both ends ([0] left, [1] right face). */
struct AWSTUTORIAL_API FPlannerWallFaceInput
{
	int32 SegmentID = -1;
	int32 StartNodeID = -1;
	int32 EndNodeID = -1;
	FVector2D Start = FVector2D::ZeroVector;
	FVector2D End = FVector2D::ZeroVector;
	FVector2D StartCorner[2] = { FVector2D::ZeroVector, FVector2D::ZeroVector };
	FVector2D EndCorner[2] = { FVector2D::ZeroVector, FVector2D::ZeroVector };
};

/**
 * Horizontal tile-grid coordinate of both faces of one wall ([0] left, [1] right): U (cm) of a point on a face is
 * Offset + Sign * Along, where Along is the distance (cm) of the point from the wall's start node along the wall. V is the height.
 */
struct AWSTUTORIAL_API FPlannerWallFaceUV
{
	float Offset[2] = { 0.f, 0.f };
	float Sign[2] = { 1.f, 1.f };

	float U(int32 Face, float AlongCm) const { return Offset[Face] + Sign[Face] * AlongCm; }
};

/** One wall for the baseboard pass. */
struct AWSTUTORIAL_API FPlannerBaseboardWall
{
	/** Centreline, node IDs and the corner points of both faces, as the wall mesh uses them. */
	FPlannerWallFaceInput Wall;
	float HalfThickness = 10.f;
	/** Room each face looks into ([0] left, [1] right); INDEX_NONE = no baseboard on that face. */
	int32 FaceRoom[2] = { INDEX_NONE, INDEX_NONE };
	/** (from, to) cm along the wall from its start node where a walk-through opening interrupts the baseboard on both faces. */
	TArray<FVector2D> Cuts;
};

/** REQ-13: layout of finishes on planner surfaces (tile grids, baseboards). Pure geometry, no world access. */
namespace PlannerFinishLayout
{
	/**
	 * Continuous horizontal tile coordinates for every wall face. Faces are chained at every node the way the corner joints pair them
	 * (each wall's counter-clockwise face with the next wall's clockwise face), so the coordinate runs on around corners and
	 * T-junctions: two chained faces agree where they visibly meet (the mitre point, or where their face lines cross when the joint
	 * is not mitred, as at T-junctions). Within a wall it never restarts at openings. An open chain starts at 0 at its free end; a
	 * closed chain (a room perimeter) starts at 0 on its first face in input order, where its one seam lies. Walls must be given in a
	 * stable order.
	 */
	AWSTUTORIAL_API TMap<int32, FPlannerWallFaceUV> ComputeWallFaceUVs(const TArray<FPlannerWallFaceInput>& Walls);

	/** Signed area (cm²) of a polygon, positive for counter-clockwise vertex order. */
	AWSTUTORIAL_API double SignedArea(const TArray<FVector2D>& Polygon);

	/** Unit normal of polygon edge Index (Polygon[Index] -> Polygon[Index + 1]) pointing into the polygon. */
	AWSTUTORIAL_API FVector2D InwardEdgeNormal(const TArray<FVector2D>& Polygon, int32 Index);

	/** The polygon with every edge i (Polygon[i] -> Polygon[i + 1]) moved EdgeOffsets[i] toward the interior. */
	AWSTUTORIAL_API TArray<FVector2D> OffsetInward(const TArray<FVector2D>& Polygon, const TArray<float>& EdgeOffsets);

	/**
	 * Tile grid frame of a room's floor and ceiling: origin at the interior wall-face corner where the longest wall of the room
	 * starts, U along that wall, V into the room. EdgeHalfThickness[i] is the half thickness of the wall on polygon edge i.
	 */
	AWSTUTORIAL_API void ComputeRoomSurfaceFrame(const TArray<FVector2D>& Polygon, const TArray<float>& EdgeHalfThickness,
	                                             FVector2D& OutOrigin, FVector2D& OutAxisU, FVector2D& OutAxisV);

	/** Metric UV (1 UV = 1 m) of a world XY point in a room surface frame. */
	inline FVector2D RoomSurfaceUV(const FVector2D& P, const FVector2D& Origin, const FVector2D& AxisU, const FVector2D& AxisV)
	{
		const FVector2D D = P - Origin;
		return FVector2D(FVector2D::DotProduct(D, AxisU), FVector2D::DotProduct(D, AxisV)) / 100.f;
	}

	/**
	 * Baseboards on every wall face that looks into a room: outer walls and interior partitions alike, whether or not a wall
	 * closes a room. Faces meeting at a corner or T-junction are mitred where their baseboards meet; a free wall end is wrapped
	 * (both faces run past it and a piece crosses the end); walk-through openings interrupt it with closed ends. The geometry of
	 * each face goes to the buffer of the room that face looks into.
	 */
	AWSTUTORIAL_API void BuildWallBaseboards(const TArray<FPlannerBaseboardWall>& Walls, float BottomZ, float Height, float Depth,
	                                         TMap<int32, FPlannerMeshBuffers>& OutByRoom);
}
