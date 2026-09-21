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

	// Face endpoint id: (wall * 2 + face) * 2 + end, end 0 = start node, 1 = end node.
	auto EndpointId = [](int32 Wall, int32 Face, int32 End) { return (Wall * 2 + Face) * 2 + End; };
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

	// Around a node, the wedge between a wall and the next wall counter-clockwise is bounded by the counter-clockwise face of the
	// first and the clockwise face of the second: those two faces meet in that corner and continue each other.
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
			const int32 A = EndpointId(I.Wall, I.End == 0 ? 0 : 1, I.End);
			const int32 B = EndpointId(J.Wall, J.End == 0 ? 1 : 0, J.End);
			if (Link[A] == INDEX_NONE && Link[B] == INDEX_NONE)
			{
				Link[A] = B;
				Link[B] = A;
			}
		}
	}

	auto WallDir = [&Walls](int32 WallIdx) { return (Walls[WallIdx].End - Walls[WallIdx].Start).GetSafeNormal(); };
	auto WallLength = [&Walls](int32 WallIdx) { return (float)FVector2D::Distance(Walls[WallIdx].Start, Walls[WallIdx].End); };
	auto EndpointCorner = [&Walls](int32 Endpoint) -> FVector2D
	{
		const FPlannerWallFaceInput& W = Walls[Endpoint / 4];
		const int32 Face = (Endpoint / 2) % 2;
		return (Endpoint % 2 == 0) ? W.StartCorner[Face] : W.EndCorner[Face];
	};

	// Where two chained faces visibly meet. A mitred corner is shared already. A joint that is not mitred (T-junctions: the through
	// wall cannot mitre, so no face there is) leaves each face ending at its own node offset, hidden inside the other wall; the
	// visible corner is where the two face lines cross.
	TArray<FVector2D> MeetPoint;
	MeetPoint.SetNum(NumWalls * 4);
	for (int32 Endpoint = 0; Endpoint < NumWalls * 4; ++Endpoint)
	{
		MeetPoint[Endpoint] = EndpointCorner(Endpoint);
	}
	for (int32 Endpoint = 0; Endpoint < NumWalls * 4; ++Endpoint)
	{
		const int32 Other = Link[Endpoint];
		if (Other == INDEX_NONE || Other < Endpoint) continue;
		const FVector2D PA = EndpointCorner(Endpoint);
		const FVector2D PB = EndpointCorner(Other);
		if (PA.Equals(PB, 0.01f)) continue;
		const FVector2D DA = WallDir(Endpoint / 4);
		const FVector2D DB = WallDir(Other / 4);
		const double Denom = Cross2(DA, DB);
		if (FMath::Abs(Denom) < 0.02) continue; // (nearly) collinear faces simply continue
		const FVector2D Crossing = PA + DA * (float)(Cross2(PB - PA, DB) / Denom);
		const float MaxShift = 0.5f * FMath::Min(WallLength(Endpoint / 4), WallLength(Other / 4));
		if (FVector2D::Distance(Crossing, PA) > MaxShift || FVector2D::Distance(Crossing, PB) > MaxShift) continue;
		MeetPoint[Endpoint] = Crossing;
		MeetPoint[Other] = Crossing;
	}

	auto CornerAlong = [&](int32 WallIdx, int32 Face, int32 End) -> float
	{
		return (float)FVector2D::DotProduct(MeetPoint[EndpointId(WallIdx, Face, End)] - Walls[WallIdx].Start, WallDir(WallIdx));
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

void PlannerFinishLayout::BuildBaseboard(const TArray<FVector2D>& Polygon, const TArray<float>& EdgeHalfThickness,
                                         const TArray<TArray<FVector2D>>& EdgeCuts, float BottomZ, float Height, float Depth,
                                         FPlannerMeshBuffers& Out)
{
	const int32 N = Polygon.Num();
	if (N < 3 || Height <= 0.f || Depth <= 0.f) return;

	TArray<float> FrontOffsets;
	FrontOffsets.SetNum(N);
	for (int32 i = 0; i < N; ++i)
	{
		FrontOffsets[i] = (EdgeHalfThickness.IsValidIndex(i) ? EdgeHalfThickness[i] : 0.f) + Depth;
	}
	const TArray<FVector2D> Back = OffsetInward(Polygon, EdgeHalfThickness); // on the interior wall faces
	const TArray<FVector2D> Front = OffsetInward(Polygon, FrontOffsets);
	const float TopZ = BottomZ + Height;
	constexpr float Unbounded = 1.0e6f;
	constexpr float MinSpan = 0.5f;

	// Edges whose first / last span is empty because a cut reaches into the corner: the neighbour's mitred end there is then open.
	TArray<bool> bStartOpen;
	TArray<bool> bEndOpen;
	bStartOpen.Init(false, N);
	bEndOpen.Init(false, N);
	for (int32 i = 0; i < N; ++i)
	{
		if (!EdgeCuts.IsValidIndex(i) || EdgeCuts[i].Num() == 0) continue;
		const FVector2D D = EdgeDirection(Polygon, i);
		const float FrontLo = (float)FVector2D::DotProduct(Front[i] - Polygon[i], D);
		const float FrontHi = (float)FVector2D::DotProduct(Front[(i + 1) % N] - Polygon[i], D);
		float FirstFrom = Unbounded;
		float LastTo = -Unbounded;
		for (const FVector2D& Cut : EdgeCuts[i])
		{
			FirstFrom = FMath::Min(FirstFrom, (float)Cut.X);
			LastTo = FMath::Max(LastTo, (float)Cut.Y);
		}
		bStartOpen[i] = FMath::Min(FirstFrom, FrontHi) - FrontLo < MinSpan;
		bEndOpen[i] = FrontHi - FMath::Max(LastTo, FrontLo) < MinSpan;
	}

	for (int32 i = 0; i < N; ++i)
	{
		const int32 Next = (i + 1) % N;
		const FVector2D P = Polygon[i];
		const FVector2D D = EdgeDirection(Polygon, i);
		const FVector2D In = InwardEdgeNormal(Polygon, i);
		const float Half = EdgeHalfThickness.IsValidIndex(i) ? EdgeHalfThickness[i] : 0.f;
		auto Along = [&P, &D](const FVector2D& Q) { return (float)FVector2D::DotProduct(Q - P, D); };
		const float BackLo = Along(Back[i]);
		const float BackHi = Along(Back[Next]);
		const float FrontLo = Along(Front[i]);
		const float FrontHi = Along(Front[Next]);
		auto BackPoint = [&](float S, float Z) { const FVector2D Q = P + D * S + In * Half; return FVector(Q.X, Q.Y, Z); };
		auto FrontPoint = [&](float S, float Z) { const FVector2D Q = P + D * S + In * (Half + Depth); return FVector(Q.X, Q.Y, Z); };
		auto UV = [](float S, float T) { return FVector2D(S / 100.f, T / 100.f); };

		auto AddSpan = [&](float From, float To, bool bCapFrom, bool bCapTo)
		{
			const float F0 = FMath::Max(From, FrontLo);
			const float F1 = FMath::Min(To, FrontHi);
			const float B0 = FMath::Max(From, BackLo);
			const float B1 = FMath::Min(To, BackHi);
			if (F1 - F0 < MinSpan) return;

			// Front face (toward the room)
			PlannerMeshBuilder::AddQuadUV(Out, FrontPoint(F0, BottomZ), FrontPoint(F1, BottomZ), FrontPoint(F1, TopZ), FrontPoint(F0, TopZ),
				FVector(In.X, In.Y, 0.f), UV(F0, BottomZ), UV(F1, BottomZ), UV(F1, TopZ), UV(F0, TopZ));
			// Top (mitred at room corners: the wall-side edge and the front edge end at their own corner points)
			if (B1 > B0)
			{
				PlannerMeshBuilder::AddQuadUV(Out, BackPoint(B0, TopZ), BackPoint(B1, TopZ), FrontPoint(F1, TopZ), FrontPoint(F0, TopZ),
					FVector::UpVector, UV(B0, 0.f), UV(B1, 0.f), UV(F1, Depth), UV(F0, Depth));
			}
			// End caps where the baseboard is interrupted
			if (bCapFrom)
			{
				PlannerMeshBuilder::AddQuadUV(Out, BackPoint(F0, BottomZ), FrontPoint(F0, BottomZ), FrontPoint(F0, TopZ), BackPoint(F0, TopZ),
					FVector(-D.X, -D.Y, 0.f), UV(0.f, BottomZ), UV(Depth, BottomZ), UV(Depth, TopZ), UV(0.f, TopZ));
			}
			if (bCapTo)
			{
				PlannerMeshBuilder::AddQuadUV(Out, FrontPoint(F1, BottomZ), BackPoint(F1, BottomZ), BackPoint(F1, TopZ), FrontPoint(F1, TopZ),
					FVector(D.X, D.Y, 0.f), UV(0.f, BottomZ), UV(Depth, BottomZ), UV(Depth, TopZ), UV(0.f, TopZ));
			}
		};

		TArray<FVector2D> Cuts = EdgeCuts.IsValidIndex(i) ? EdgeCuts[i] : TArray<FVector2D>();
		Cuts.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X; });
		float Cursor = -Unbounded;
		bool bAfterCut = false;
		for (const FVector2D& Cut : Cuts)
		{
			AddSpan(Cursor, Cut.X, bAfterCut, true);
			Cursor = FMath::Max(Cursor, Cut.Y);
			bAfterCut = true;
		}
		AddSpan(Cursor, Unbounded, bAfterCut, false);
	}

	// Close baseboard ends left exposed at the corners.
	for (int32 v = 0; v < N; ++v)
	{
		const int32 Prev = (v + N - 1) % N;
		const bool bNextOpen = bStartOpen[v]; // edge v starts at vertex v
		const bool bPrevOpen = bEndOpen[Prev]; // edge Prev ends at vertex v

		const FVector2D DPrev = EdgeDirection(Polygon, Prev);
		const FVector2D DNext = EdgeDirection(Polygon, v);
		if (FMath::Abs(Cross2(DPrev, DNext)) <= 1.e-3 && FVector2D::DotProduct(DPrev, DNext) > 0.0)
		{
			// In line (a wall split at a node): each baseboard ends square at the node at its own wall's depth. Cap the part of an end
			// that the other baseboard does not meet (a thicker wall's end, or an end beside a doorway).
			const FVector2D In = InwardEdgeNormal(Polygon, v);
			const float HPrev = EdgeHalfThickness.IsValidIndex(Prev) ? EdgeHalfThickness[Prev] : 0.f;
			const float HNext = EdgeHalfThickness.IsValidIndex(v) ? EdgeHalfThickness[v] : 0.f;
			auto SquareCap = [&](float From, float To, const FVector2D& Facing)
			{
				if (To - From < 0.01f) return;
				const FVector2D A = Polygon[v] + In * From;
				const FVector2D B = Polygon[v] + In * To;
				const float Width = To - From;
				PlannerMeshBuilder::AddQuadUV(Out, FVector(A.X, A.Y, BottomZ), FVector(B.X, B.Y, BottomZ), FVector(B.X, B.Y, TopZ), FVector(A.X, A.Y, TopZ),
					FVector(Facing.X, Facing.Y, 0.f),
					FVector2D(0.f, BottomZ / 100.f), FVector2D(Width / 100.f, BottomZ / 100.f), FVector2D(Width / 100.f, TopZ / 100.f), FVector2D(0.f, TopZ / 100.f));
			};
			if (!bPrevOpen)
			{
				SquareCap(bNextOpen ? HPrev : FMath::Max(HPrev, HNext + Depth), HPrev + Depth, DPrev);
			}
			if (!bNextOpen)
			{
				SquareCap(bPrevOpen ? HNext : FMath::Max(HNext, HPrev + Depth), HNext + Depth, -DNext);
			}
			continue;
		}

		if (bNextOpen == bPrevOpen) continue;  // both present (their mitres close each other) or both absent

		const FVector2D Across = Front[v] - Back[v];
		if (Across.SizeSquared() < 1.e-4f) continue;
		FVector2D CapNormal = FVector2D(-Across.Y, Across.X).GetSafeNormal();
		const FVector2D OpenSide = bNextOpen ? EdgeDirection(Polygon, v) : -EdgeDirection(Polygon, Prev);
		if (FVector2D::DotProduct(CapNormal, OpenSide) < 0.0)
		{
			CapNormal = -CapNormal;
		}
		const float Width = (float)Across.Size();
		PlannerMeshBuilder::AddQuadUV(Out, FVector(Back[v].X, Back[v].Y, BottomZ), FVector(Front[v].X, Front[v].Y, BottomZ),
			FVector(Front[v].X, Front[v].Y, TopZ), FVector(Back[v].X, Back[v].Y, TopZ), FVector(CapNormal.X, CapNormal.Y, 0.f),
			FVector2D(0.f, BottomZ / 100.f), FVector2D(Width / 100.f, BottomZ / 100.f), FVector2D(Width / 100.f, TopZ / 100.f), FVector2D(0.f, TopZ / 100.f));
	}
}
