// Copyright 2026 MaxiMall. All Rights Reserved.

#include "PlannerFinishLayout.h"

namespace
{
	double Cross2(const FVector2D& A, const FVector2D& B)
	{
		return (double)A.X * B.Y - (double)A.Y * B.X;
	}

	/** Direction of polygon edge Index; a zero-length edge takes the direction of the previous non-degenerate edge. */
	FVector2D EdgeDirection(const TArray<FVector2D>& Polygon, int32 Index)
	{
		const int32 N = Polygon.Num();
		for (int32 Step = 0; Step < N; ++Step)
		{
			const int32 I = (Index + N - Step) % N;
			const FVector2D D = Polygon[(I + 1) % N] - Polygon[I];
			if (D.SizeSquared() > 1.e-4f)
			{
				return D.GetSafeNormal();
			}
		}
		return FVector2D(1.f, 0.f);
	}

	/** Face end id: (wall * 2 + face) * 2 + end; face 0 = left, 1 = right; end 0 = start node, 1 = end node. */
	int32 FaceEndpointId(int32 Wall, int32 Face, int32 End)
	{
		return (Wall * 2 + Face) * 2 + End;
	}

	FVector2D WallDirection(const TArray<FPlannerWallFaceInput>& Walls, int32 Wall)
	{
		return (Walls[Wall].End - Walls[Wall].Start).GetSafeNormal();
	}

	FVector2D FaceEndCorner(const TArray<FPlannerWallFaceInput>& Walls, int32 Endpoint)
	{
		const FPlannerWallFaceInput& W = Walls[Endpoint / 4];
		const int32 Face = (Endpoint / 2) % 2;
		return (Endpoint % 2 == 0) ? W.StartCorner[Face] : W.EndCorner[Face];
	}

	/** True when P does not lie past the end corner of that face end, i.e. not beyond its wall. */
	bool IsWithinFaceEnd(const TArray<FPlannerWallFaceInput>& Walls, int32 Endpoint, const FVector2D& P)
	{
		const FPlannerWallFaceInput& W = Walls[Endpoint / 4];
		const FVector2D Dir = (W.End - W.Start).GetSafeNormal();
		const double CornerAt = FVector2D::DotProduct(FaceEndCorner(Walls, Endpoint) - W.Start, Dir);
		const double PointAt = FVector2D::DotProduct(P - W.Start, Dir);
		return (Endpoint % 2 == 0) ? PointAt >= CornerAt - 0.01 : PointAt <= CornerAt + 0.01;
	}

	/**
	 * Pairs the face ends that continue each other at every node: around a node, the wedge between a wall and the next wall
	 * counter-clockwise is bounded by the counter-clockwise face of the first and the clockwise face of the second. Returns, per face
	 * end id, the id of the face end continuing it; INDEX_NONE at a free end.
	 */
	TArray<int32> LinkWallFaces(const TArray<FPlannerWallFaceInput>& Walls)
	{
		const int32 NumWalls = Walls.Num();
		TArray<int32> Link;
		Link.Init(INDEX_NONE, NumWalls * 4);

		struct FNodeEntry
		{
			int32 Wall = 0;
			int32 End = 0;
			double Angle = 0.0;
		};
		TMap<int32, TArray<FNodeEntry>> ByNode;
		for (int32 WallIdx = 0; WallIdx < NumWalls; ++WallIdx)
		{
			const FPlannerWallFaceInput& W = Walls[WallIdx];
			const FVector2D Dir = W.End - W.Start;
			if (Dir.SizeSquared() < 1.e-4f) continue;
			ByNode.FindOrAdd(W.StartNodeID).Add({ WallIdx, 0, FMath::Atan2((double)Dir.Y, (double)Dir.X) });
			ByNode.FindOrAdd(W.EndNodeID).Add({ WallIdx, 1, FMath::Atan2(-(double)Dir.Y, -(double)Dir.X) });
		}
		for (TPair<int32, TArray<FNodeEntry>>& Pair : ByNode)
		{
			TArray<FNodeEntry>& Entries = Pair.Value;
			const int32 Count = Entries.Num();
			if (Count < 2) continue;
			Entries.Sort([](const FNodeEntry& A, const FNodeEntry& B) { return A.Angle < B.Angle || (A.Angle == B.Angle && A.Wall < B.Wall); });
			for (int32 k = 0; k < Count; ++k)
			{
				const FNodeEntry& I = Entries[k];
				const FNodeEntry& J = Entries[(k + 1) % Count];
				if (I.Wall == J.Wall) continue;
				// Leaving its start node, the counter-clockwise face of a wall is its left face; leaving its end node, its right face.
				const int32 A = FaceEndpointId(I.Wall, I.End == 0 ? 0 : 1, I.End);
				const int32 B = FaceEndpointId(J.Wall, J.End == 0 ? 1 : 0, J.End);
				if (Link[A] == INDEX_NONE && Link[B] == INDEX_NONE)
				{
					Link[A] = B;
					Link[B] = A;
				}
			}
		}
		return Link;
	}

	/**
	 * Where each face end is visibly bounded. A mitred corner is shared already. A joint that is not mitred (T-junctions: the
	 * through wall cannot mitre, so no face there is) leaves each face ending at its own node offset, hidden inside the other wall;
	 * the visible corner is where the two face lines cross. Free and collinear ends keep their corner point.
	 */
	TArray<FVector2D> FaceMeetPoints(const TArray<FPlannerWallFaceInput>& Walls, const TArray<int32>& Link)
	{
		const int32 NumEndpoints = Walls.Num() * 4;
		TArray<FVector2D> MeetPoint;
		MeetPoint.SetNum(NumEndpoints);
		for (int32 Endpoint = 0; Endpoint < NumEndpoints; ++Endpoint)
		{
			MeetPoint[Endpoint] = FaceEndCorner(Walls, Endpoint);
		}
		for (int32 Endpoint = 0; Endpoint < NumEndpoints; ++Endpoint)
		{
			const int32 Other = Link[Endpoint];
			if (Other == INDEX_NONE || Other < Endpoint) continue;
			const FVector2D PA = FaceEndCorner(Walls, Endpoint);
			const FVector2D PB = FaceEndCorner(Walls, Other);
			if (PA.Equals(PB, 0.01f)) continue;
			const FVector2D DA = WallDirection(Walls, Endpoint / 4);
			const FVector2D DB = WallDirection(Walls, Other / 4);
			const double Denom = Cross2(DA, DB);
			if (FMath::Abs(Denom) < 0.02) continue; // (nearly) collinear faces simply continue
			const FVector2D Crossing = PA + DA * (float)(Cross2(PB - PA, DB) / Denom);
			const float MaxShift = 0.5f * (float)FMath::Min(FVector2D::Distance(Walls[Endpoint / 4].Start, Walls[Endpoint / 4].End),
				FVector2D::Distance(Walls[Other / 4].Start, Walls[Other / 4].End));
			if (FVector2D::Distance(Crossing, PA) > MaxShift || FVector2D::Distance(Crossing, PB) > MaxShift) continue;
			MeetPoint[Endpoint] = Crossing;
			MeetPoint[Other] = Crossing;
		}
		return MeetPoint;
	}
}

FVector2D PlannerFinishLayout::VisibleFaceExtent(const TArray<FPlannerWallFaceInput>& Walls, int32 WallIndex, int32 Face)
{
	if (!Walls.IsValidIndex(WallIndex) || Face < 0 || Face > 1) return FVector2D::ZeroVector;
	return VisibleFaceExtents(Walls)[WallIndex * 2 + Face];
}

TArray<FVector2D> PlannerFinishLayout::VisibleFaceExtents(const TArray<FPlannerWallFaceInput>& Walls)
{
	const TArray<int32> Link = LinkWallFaces(Walls);
	const TArray<FVector2D> Meet = FaceMeetPoints(Walls, Link);
	TArray<FVector2D> Extents;
	Extents.SetNum(Walls.Num() * 2);
	for (int32 WallIndex = 0; WallIndex < Walls.Num(); ++WallIndex)
	{
		const FPlannerWallFaceInput& Wall = Walls[WallIndex];
		const FVector2D Dir = WallDirection(Walls, WallIndex);
		auto Along = [&Wall, &Dir](const FVector2D& P) { return (float)FVector2D::DotProduct(P - Wall.Start, Dir); };
		// Where the face meets the next one, when that point lies on both walls; otherwise (face lines crossing beyond a wall, as at a
		// slight bend between walls of different thickness) the face's own corner.
		auto Bound = [&](int32 Endpoint)
		{
			const int32 Other = Link[Endpoint];
			return (Other != INDEX_NONE && IsWithinFaceEnd(Walls, Endpoint, Meet[Endpoint]) && IsWithinFaceEnd(Walls, Other, Meet[Endpoint]))
				? Meet[Endpoint] : FaceEndCorner(Walls, Endpoint);
		};
		for (int32 Face = 0; Face < 2; ++Face)
		{
			const int32 StartEndpoint = FaceEndpointId(WallIndex, Face, 0);
			const int32 EndEndpoint = FaceEndpointId(WallIndex, Face, 1);
			Extents[WallIndex * 2 + Face] = FVector2D(FMath::Max(Along(FaceEndCorner(Walls, StartEndpoint)), Along(Bound(StartEndpoint))),
				FMath::Min(Along(FaceEndCorner(Walls, EndEndpoint)), Along(Bound(EndEndpoint))));
		}
	}
	return Extents;
}

TMap<int32, FPlannerWallFaceUV> PlannerFinishLayout::ComputeWallFaceUVs(const TArray<FPlannerWallFaceInput>& Walls)
{
	TMap<int32, FPlannerWallFaceUV> Result;
	const int32 NumWalls = Walls.Num();
	for (const FPlannerWallFaceInput& W : Walls)
	{
		Result.Add(W.SegmentID, FPlannerWallFaceUV());
	}
	if (NumWalls == 0)
	{
		return Result;
	}

	const TArray<int32> Link = LinkWallFaces(Walls);
	const TArray<FVector2D> MeetPoint = FaceMeetPoints(Walls, Link);
	auto EndpointId = [](int32 Wall, int32 Face, int32 End) { return FaceEndpointId(Wall, Face, End); };
	auto CornerAlong = [&](int32 WallIdx, int32 Face, int32 End) -> float
	{
		return (float)FVector2D::DotProduct(MeetPoint[EndpointId(WallIdx, Face, End)] - Walls[WallIdx].Start, WallDirection(Walls, WallIdx));
	};

	TArray<bool> Visited;
	Visited.Init(false, NumWalls * 2);

	// Walks a chain entering face (WallIdx, Face) at its EntryEnd corner with U = 0 there; U grows along the walk on every face.
	auto Walk = [&](int32 WallIdx, int32 Face, int32 EntryEnd)
	{
		float UAccum = 0.f;
		while (!Visited[WallIdx * 2 + Face])
		{
			Visited[WallIdx * 2 + Face] = true;
			const int32 ExitEnd = 1 - EntryEnd;
			const float Sign = (EntryEnd == 0) ? 1.f : -1.f;
			FPlannerWallFaceUV& UV = Result.FindChecked(Walls[WallIdx].SegmentID);
			UV.Sign[Face] = Sign;
			UV.Offset[Face] = UAccum - Sign * CornerAlong(WallIdx, Face, EntryEnd);
			UAccum = UV.U(Face, CornerAlong(WallIdx, Face, ExitEnd));

			const int32 Next = Link[EndpointId(WallIdx, Face, ExitEnd)];
			if (Next == INDEX_NONE) break;
			EntryEnd = Next % 2;
			Face = (Next / 2) % 2;
			WallIdx = Next / 4;
		}
	};

	// Open chains, from a free end.
	for (int32 WallIdx = 0; WallIdx < NumWalls; ++WallIdx)
	{
		for (int32 Face = 0; Face < 2; ++Face)
		{
			if (Visited[WallIdx * 2 + Face]) continue;
			if (Link[EndpointId(WallIdx, Face, 0)] == INDEX_NONE) Walk(WallIdx, Face, 0);
			else if (Link[EndpointId(WallIdx, Face, 1)] == INDEX_NONE) Walk(WallIdx, Face, 1);
		}
	}
	// Closed chains.
	for (int32 WallIdx = 0; WallIdx < NumWalls; ++WallIdx)
	{
		for (int32 Face = 0; Face < 2; ++Face)
		{
			if (!Visited[WallIdx * 2 + Face]) Walk(WallIdx, Face, 0);
		}
	}
	return Result;
}

double PlannerFinishLayout::SignedArea(const TArray<FVector2D>& Polygon)
{
	double Twice = 0.0;
	const int32 N = Polygon.Num();
	for (int32 i = 0; i < N; ++i)
	{
		Twice += Cross2(Polygon[i], Polygon[(i + 1) % N]);
	}
	return Twice * 0.5;
}

FVector2D PlannerFinishLayout::InwardEdgeNormal(const TArray<FVector2D>& Polygon, int32 Index)
{
	if (Polygon.Num() < 2) return FVector2D(0.f, 1.f);
	const FVector2D D = EdgeDirection(Polygon, Index);
	const FVector2D Left(-D.Y, D.X);
	return SignedArea(Polygon) >= 0.0 ? Left : -Left;
}

TArray<FVector2D> PlannerFinishLayout::OffsetInward(const TArray<FVector2D>& Polygon, const TArray<float>& EdgeOffsets)
{
	const int32 N = Polygon.Num();
	TArray<FVector2D> Out = Polygon;
	if (N < 3)
	{
		return Out;
	}
	auto OffsetOf = [&EdgeOffsets](int32 i) { return EdgeOffsets.IsValidIndex(i) ? EdgeOffsets[i] : 0.f; };

	for (int32 i = 0; i < N; ++i)
	{
		const int32 Prev = (i + N - 1) % N;
		const FVector2D DPrev = EdgeDirection(Polygon, Prev);
		const FVector2D DCur = EdgeDirection(Polygon, i);
		const FVector2D PPrev = Polygon[i] + InwardEdgeNormal(Polygon, Prev) * OffsetOf(Prev);
		const FVector2D PCur = Polygon[i] + InwardEdgeNormal(Polygon, i) * OffsetOf(i);

		FVector2D Point = (PPrev + PCur) * 0.5f; // collinear edges
		const double Denom = Cross2(DPrev, DCur);
		if (FMath::Abs(Denom) > 1.e-3)
		{
			// PPrev + DPrev * T = PCur + DCur * S
			const double T = Cross2(PCur - PPrev, DCur) / Denom;
			const FVector2D Candidate = PPrev + DPrev * (float)T;
			const float MaxShift = 4.f * FMath::Max(OffsetOf(Prev), OffsetOf(i)) + 1.f; // very sharp corners keep a bounded point
			if (FVector2D::Distance(Candidate, Polygon[i]) <= MaxShift)
			{
				Point = Candidate;
			}
		}
		Out[i] = Point;
	}
	return Out;
}

void PlannerFinishLayout::ComputeRoomSurfaceFrame(const TArray<FVector2D>& Polygon, const TArray<float>& EdgeHalfThickness,
                                                  FVector2D& OutOrigin, FVector2D& OutAxisU, FVector2D& OutAxisV)
{
	OutOrigin = Polygon.Num() > 0 ? Polygon[0] : FVector2D::ZeroVector;
	OutAxisU = FVector2D(1.f, 0.f);
	OutAxisV = FVector2D(0.f, 1.f);
	const int32 N = Polygon.Num();
	if (N < 3) return;

	int32 Longest = 0;
	float LongestLen = -1.f;
	for (int32 i = 0; i < N; ++i)
	{
		const float Len = FVector2D::Distance(Polygon[i], Polygon[(i + 1) % N]);
		if (Len > LongestLen + 0.5f) // near-equal lengths keep the first edge, so the frame does not flip between rebuilds
		{
			LongestLen = Len;
			Longest = i;
		}
	}
	const TArray<FVector2D> Interior = OffsetInward(Polygon, EdgeHalfThickness);
	OutOrigin = Interior[Longest];
	OutAxisU = EdgeDirection(Polygon, Longest);
	OutAxisV = InwardEdgeNormal(Polygon, Longest);
}

void PlannerFinishLayout::BuildWallBaseboards(const TArray<FPlannerBaseboardWall>& Walls, float BottomZ, float Height, float Depth,
                                              TMap<int32, FPlannerMeshBuffers>& OutByRoom)
{
	const int32 NumWalls = Walls.Num();
	if (NumWalls == 0 || Height <= 0.f || Depth <= 0.f) return;

	TArray<FPlannerWallFaceInput> Inputs;
	Inputs.Reserve(NumWalls);
	for (const FPlannerBaseboardWall& W : Walls)
	{
		Inputs.Add(W.Wall);
	}
	const TArray<int32> Link = LinkWallFaces(Inputs);
	const TArray<FVector2D> Meet = FaceMeetPoints(Inputs, Link);

	const float TopZ = BottomZ + Height;
	constexpr float MinSpan = 0.5f;
	constexpr float Unbounded = 1.0e6f;

	auto Dir = [&Inputs](int32 W) { return WallDirection(Inputs, W); };
	auto FaceNormal = [&Dir](int32 W, int32 F)
	{
		const FVector2D D = Dir(W);
		const FVector2D Left(-D.Y, D.X);
		return F == 0 ? Left : -Left;
	};
	auto Along = [&Inputs, &Dir](int32 W, const FVector2D& P) { return (float)FVector2D::DotProduct(P - Inputs[W].Start, Dir(W)); };
	auto RoomOfEndpoint = [&Walls](int32 Endpoint) { return Walls[Endpoint / 4].FaceRoom[(Endpoint / 2) % 2]; };
	auto HalfOf = [&Walls](int32 W) { return Walls[W].HalfThickness; };
	auto UV = [](float S, float T) { return FVector2D(S / 100.f, T / 100.f); };
	// True when P does not lie past the end corner of that face end (outside its wall). Where the corner joint was not mitred (a
	// very acute or a nearly straight bend) the face lines can cross far beyond the walls; the baseboards must not follow them there.
	auto WithinFaceEnd = [&](int32 Endpoint, const FVector2D& P)
	{
		const int32 WallIdx = Endpoint / 4;
		const float CornerAt = Along(WallIdx, FaceEndCorner(Inputs, Endpoint));
		const float PointAt = Along(WallIdx, P);
		return (Endpoint % 2 == 0) ? PointAt >= CornerAt - 0.01f : PointAt <= CornerAt + 0.01f;
	};
	auto AddCap = [&](FPlannerMeshBuffers& Out, const FVector2D& A, const FVector2D& B, const FVector2D& Facing)
	{
		const float Width = (float)FVector2D::Distance(A, B);
		if (Width < 0.01f) return;
		PlannerMeshBuilder::AddQuadUV(Out, FVector(A.X, A.Y, BottomZ), FVector(B.X, B.Y, BottomZ), FVector(B.X, B.Y, TopZ), FVector(A.X, A.Y, TopZ),
			FVector(Facing.X, Facing.Y, 0.f), UV(0.f, BottomZ), UV(Width, BottomZ), UV(Width, TopZ), UV(0.f, TopZ));
	};

	enum class EEndKind : uint8
	{
		Square,     // ends flat at the wall end / visible corner
		Mitre,      // meets the baseboard of the face continuing it at a corner or T-junction
		Collinear,  // meets the baseboard of the next wall in line (a wall split at a node)
		Wrap,       // free wall end: runs past the end, where a piece crosses the wall end
	};
	struct FFaceEnd
	{
		EEndKind Kind = EEndKind::Square;
		float BackAlong = 0.f;   // end of the edge on the wall face
		float FrontAlong = 0.f;  // end of the front edge (Depth off the wall face)
		FVector2D BackPoint = FVector2D::ZeroVector;
		FVector2D FrontPoint = FVector2D::ZeroVector;
		bool bPresent = false;   // the baseboard reaches this end (no walk-through opening in the way)
	};
	TArray<FFaceEnd> Ends;
	Ends.SetNum(NumWalls * 4);

	// 1. How each face's baseboard ends at each end of its wall.
	for (int32 W = 0; W < NumWalls; ++W)
	{
		for (int32 F = 0; F < 2; ++F)
		{
			const int32 Room = Walls[W].FaceRoom[F];
			if (Room == INDEX_NONE) continue;
			for (int32 E = 0; E < 2; ++E)
			{
				const int32 Ep = FaceEndpointId(W, F, E);
				FFaceEnd& End = Ends[Ep];
				const int32 Other = Link[Ep];
				if (Other != INDEX_NONE && RoomOfEndpoint(Other) == Room)
				{
					const int32 W2 = Other / 4;
					const int32 F2 = (Other / 2) % 2;
					const FVector2D DA = Dir(W);
					const FVector2D DB = Dir(W2);
					const double Denom = Cross2(DA, DB);
					if (FMath::Abs(Denom) < 0.02)
					{
						End.Kind = EEndKind::Collinear;
						End.BackAlong = End.FrontAlong = Along(W, E == 0 ? Inputs[W].Start : Inputs[W].End);
					}
					else if (!WithinFaceEnd(Ep, Meet[Ep]) || !WithinFaceEnd(Other, Meet[Ep]))
					{
						End.Kind = EEndKind::Square; // the faces do not visibly meet: end flat at the wall's own corner
						End.BackAlong = End.FrontAlong = Along(W, FaceEndCorner(Inputs, Ep));
					}
					else
					{
						const FVector2D FrontA = Inputs[W].Start + FaceNormal(W, F) * (HalfOf(W) + Depth);
						const FVector2D FrontB = Inputs[W2].Start + FaceNormal(W2, F2) * (HalfOf(W2) + Depth);
						const FVector2D FrontCross = FrontA + DA * (float)(Cross2(FrontB - FrontA, DB) / Denom);
						const float Bound = 4.f * (FMath::Max(HalfOf(W), HalfOf(W2)) + Depth) + 1.f;
						End.BackPoint = Meet[Ep];
						End.BackAlong = Along(W, Meet[Ep]);
						if (FVector2D::Distance(FrontCross, Meet[Ep]) <= Bound)
						{
							End.Kind = EEndKind::Mitre;
							End.FrontPoint = FrontCross;
							End.FrontAlong = Along(W, FrontCross);
						}
						else
						{
							End.Kind = EEndKind::Square; // very sharp corner: end flat at the visible corner
							End.FrontAlong = End.BackAlong;
						}
					}
				}
				else if (Other == INDEX_NONE && Link[FaceEndpointId(W, 1 - F, E)] == INDEX_NONE && Walls[W].FaceRoom[1 - F] == Room)
				{
					End.Kind = EEndKind::Wrap;
					End.BackAlong = Along(W, FaceEndCorner(Inputs, Ep));
					End.FrontAlong = End.BackAlong + (E == 1 ? Depth : -Depth);
				}
				else
				{
					End.Kind = EEndKind::Square;
					const bool bUseMeet = Other != INDEX_NONE && WithinFaceEnd(Ep, Meet[Ep]) && WithinFaceEnd(Other, Meet[Ep]);
					End.BackAlong = End.FrontAlong = Along(W, bUseMeet ? Meet[Ep] : FaceEndCorner(Inputs, Ep));
				}
			}
		}
	}

	// 2. The runs of each face between walk-through openings.
	for (int32 W = 0; W < NumWalls; ++W)
	{
		for (int32 F = 0; F < 2; ++F)
		{
			const int32 Room = Walls[W].FaceRoom[F];
			if (Room == INDEX_NONE) continue;
			FPlannerMeshBuffers& Out = OutByRoom.FindOrAdd(Room);
			FFaceEnd& StartEnd = Ends[FaceEndpointId(W, F, 0)];
			FFaceEnd& EndEnd = Ends[FaceEndpointId(W, F, 1)];
			const FVector2D S = Inputs[W].Start;
			const FVector2D D = Dir(W);
			const FVector2D N = FaceNormal(W, F);
			const float H = HalfOf(W);
			auto BackPoint = [&](float A, float Z) { const FVector2D Q = S + D * A + N * H; return FVector(Q.X, Q.Y, Z); };
			auto FrontPoint = [&](float A, float Z) { const FVector2D Q = S + D * A + N * (H + Depth); return FVector(Q.X, Q.Y, Z); };

			auto AddSpan = [&](float From, float To, bool bCapFrom, bool bCapTo) -> bool
			{
				const float F0 = FMath::Max(From, StartEnd.FrontAlong);
				const float F1 = FMath::Min(To, EndEnd.FrontAlong);
				const float B0 = FMath::Max(From, StartEnd.BackAlong);
				const float B1 = FMath::Min(To, EndEnd.BackAlong);
				if (F1 - F0 < MinSpan) return false;
				// An opening reaching the free wall end leaves nothing along the face there, only the wrap's overhang: no baseboard.
				const bool bAtWrap = (From <= -Unbounded && StartEnd.Kind == EEndKind::Wrap) || (To >= Unbounded && EndEnd.Kind == EEndKind::Wrap);
				if (bAtWrap && B1 - B0 < MinSpan) return false;

				// Front, facing the room
				PlannerMeshBuilder::AddQuadUV(Out, FrontPoint(F0, BottomZ), FrontPoint(F1, BottomZ), FrontPoint(F1, TopZ), FrontPoint(F0, TopZ),
					FVector(N.X, N.Y, 0.f), UV(F0, BottomZ), UV(F1, BottomZ), UV(F1, TopZ), UV(F0, TopZ));
				// Top: the wall-side edge and the front edge end at their own corner points, which mitres it
				if (B1 > B0)
				{
					PlannerMeshBuilder::AddQuadUV(Out, BackPoint(B0, TopZ), BackPoint(B1, TopZ), FrontPoint(F1, TopZ), FrontPoint(F0, TopZ),
						FVector::UpVector, UV(B0, 0.f), UV(B1, 0.f), UV(F1, Depth), UV(F0, Depth));
				}
				// Closed ends at openings
				if (bCapFrom)
				{
					AddCap(Out, FVector2D(BackPoint(F0, 0.f)), FVector2D(FrontPoint(F0, 0.f)), -D);
				}
				if (bCapTo)
				{
					AddCap(Out, FVector2D(FrontPoint(F1, 0.f)), FVector2D(BackPoint(F1, 0.f)), D);
				}
				return true;
			};

			TArray<FVector2D> Cuts = Walls[W].Cuts;
			Cuts.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X; });
			float Cursor = -Unbounded;
			bool bAfterCut = false;
			bool bFirstSpan = true;
			for (const FVector2D& Cut : Cuts)
			{
				const bool bMade = AddSpan(Cursor, (float)Cut.X, bAfterCut, true);
				if (bFirstSpan)
				{
					StartEnd.bPresent = bMade;
					bFirstSpan = false;
				}
				Cursor = FMath::Max(Cursor, (float)Cut.Y);
				bAfterCut = true;
			}
			const bool bLastMade = AddSpan(Cursor, Unbounded, bAfterCut, false);
			if (bFirstSpan)
			{
				StartEnd.bPresent = bLastMade;
			}
			EndEnd.bPresent = bLastMade;
		}
	}

	// 3. Close the ends that nothing else closes.
	for (int32 W = 0; W < NumWalls; ++W)
	{
		for (int32 F = 0; F < 2; ++F)
		{
			const int32 Room = Walls[W].FaceRoom[F];
			if (Room == INDEX_NONE) continue;
			FPlannerMeshBuffers& Out = OutByRoom.FindOrAdd(Room);
			const FVector2D S = Inputs[W].Start;
			const FVector2D D = Dir(W);
			const FVector2D N = FaceNormal(W, F);
			const float H = HalfOf(W);
			for (int32 E = 0; E < 2; ++E)
			{
				const int32 Ep = FaceEndpointId(W, F, E);
				const FFaceEnd& End = Ends[Ep];
				if (!End.bPresent) continue;
				const FVector2D Outward = (E == 1) ? D : -D;
				auto AtEnd = [&](float Lateral) { return S + D * End.FrontAlong + N * Lateral; };

				switch (End.Kind)
				{
				case EEndKind::Square:
					AddCap(Out, AtEnd(H), AtEnd(H + Depth), Outward);
					break;

				case EEndKind::Collinear:
				{
					// Cap what the next baseboard in line does not cover: all of it beside an opening, the step to a thinner wall.
					const int32 Other = Link[Ep];
					const float From = Ends[Other].bPresent ? FMath::Max(H, HalfOf(Other / 4) + Depth) : H;
					if (H + Depth - From > 0.01f)
					{
						AddCap(Out, AtEnd(From), AtEnd(H + Depth), Outward);
					}
					break;
				}

				case EEndKind::Mitre:
				{
					const int32 Other = Link[Ep];
					if (Ends[Other].bPresent) break; // the two baseboards close each other
					// The other one stops short (an opening reaches the corner): close this mitred end along the mitre.
					const FVector2D OpenSide = (Other % 2 == 0) ? Dir(Other / 4) : -Dir(Other / 4);
					const FVector2D Across = End.FrontPoint - End.BackPoint;
					FVector2D CapNormal = FVector2D(-Across.Y, Across.X).GetSafeNormal();
					if (FVector2D::DotProduct(CapNormal, OpenSide) < 0.0)
					{
						CapNormal = -CapNormal;
					}
					AddCap(Out, End.BackPoint, End.FrontPoint, CapNormal);
					break;
				}

				case EEndKind::Wrap:
				{
					if (!Ends[FaceEndpointId(W, 1 - F, E)].bPresent)
					{
						AddCap(Out, AtEnd(H), AtEnd(H + Depth), Outward);
						break;
					}
					if (F != 0) break; // one piece across the wall end, built with the left face
					const FVector2D FrontL = S + D * End.FrontAlong + N * (H + Depth);
					const FVector2D FrontR = S + D * End.FrontAlong - N * (H + Depth);
					const FVector2D BackL = S + D * End.BackAlong + N * H;
					const FVector2D BackR = S + D * End.BackAlong - N * H;
					AddCap(Out, FrontL, FrontR, Outward);
					PlannerMeshBuilder::AddQuadUV(Out, FVector(BackL.X, BackL.Y, TopZ), FVector(BackR.X, BackR.Y, TopZ), FVector(FrontR.X, FrontR.Y, TopZ),
						FVector(FrontL.X, FrontL.Y, TopZ), FVector::UpVector, UV(0.f, 0.f), UV(2.f * H, 0.f), UV(2.f * (H + Depth), Depth), UV(0.f, Depth));
					break;
				}
				}
			}
		}
	}
}
