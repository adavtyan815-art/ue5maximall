// Copyright 2026 MaxiMall. All Rights Reserved.

#include "PlannerSelectionOverlay.h"
#include "ProceduralMeshComponent.h"

void PlannerSelectionOverlay::AppendLiftedSection(const FProcMeshSection& Section, float Lift, const FVector& FacingFilter, FPlannerMeshBuffers& Out)
{
	const TArray<FProcMeshVertex>& Verts = Section.ProcVertexBuffer;
	const TArray<uint32>& Indices = Section.ProcIndexBuffer;
	const bool bFilter = !FacingFilter.IsNearlyZero();
	const FVector Facing = FacingFilter.GetSafeNormal();

	// Source vertex -> output vertex, filled on first use (only the kept triangles' vertices are copied).
	TArray<int32> Remap;
	Remap.Init(INDEX_NONE, Verts.Num());
	for (int32 Tri = 0; Tri + 2 < Indices.Num(); Tri += 3)
	{
		const uint32 I0 = Indices[Tri], I1 = Indices[Tri + 1], I2 = Indices[Tri + 2];
		if (!Verts.IsValidIndex(I0) || !Verts.IsValidIndex(I1) || !Verts.IsValidIndex(I2)) continue;
		if (bFilter && FVector::DotProduct(Verts[I0].Normal, Facing) <= 0.5f) continue;
		for (const uint32 Index : { I0, I1, I2 })
		{
			if (Remap[Index] == INDEX_NONE)
			{
				const FProcMeshVertex& V = Verts[Index];
				Remap[Index] = Out.Vertices.Num();
				Out.Vertices.Add(V.Position + V.Normal * Lift);
				Out.Normals.Add(V.Normal);
				Out.UVs.Add(V.UV0);
				Out.Tangents.Add(V.Tangent);
			}
			Out.Triangles.Add(Remap[Index]);
		}
	}
}

void PlannerSelectionOverlay::AddWallFaceFrame(FPlannerMeshBuffers& Out, const FVector2D& FaceOrigin, const FVector2D& AxisAlong, const FVector2D& OutNormal,
                                               float ExtentLo, float ExtentHi, float Height, const TArray<FFaceHole>& Holes, float Band, float Lift,
                                               bool bFaceOutline, float HoleMargin)
{
	if (ExtentHi - ExtentLo < 1.f || Height < 1.f || Band <= 0.f) return;
	const FVector Normal(OutNormal.X, OutNormal.Y, 0.f);
	auto At = [&](float Along, float Z)
	{
		const FVector2D P = FaceOrigin + AxisAlong * Along + OutNormal * Lift;
		return FVector(P.X, P.Y, Z);
	};
	// One band rectangle in face coordinates, clipped to the face.
	auto Rect = [&](float A0, float A1, float Z0, float Z1)
	{
		A0 = FMath::Max(A0, ExtentLo);
		A1 = FMath::Min(A1, ExtentHi);
		Z0 = FMath::Max(Z0, 0.f);
		Z1 = FMath::Min(Z1, Height);
		if (A1 - A0 < 0.1f || Z1 - Z0 < 0.1f) return;
		PlannerMeshBuilder::AddQuad(Out, At(A0, Z0), At(A1, Z0), At(A1, Z1), At(A0, Z1), Normal);
	};

	// The face's own outline: sides and top whole; the bottom stops at every opening that stands on the floor.
	if (bFaceOutline)
	{
		Rect(ExtentLo, ExtentLo + Band, 0.f, Height);
		Rect(ExtentHi - Band, ExtentHi, 0.f, Height);
		Rect(ExtentLo, ExtentHi, Height - Band, Height);
		TArray<FVector2D> FloorGaps;
		for (const FFaceHole& Hole : Holes)
		{
			if (Hole.bOnFloor) FloorGaps.Add(FVector2D(Hole.Start - HoleMargin, Hole.End + HoleMargin));
		}
		FloorGaps.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X; });
		float From = ExtentLo;
		for (const FVector2D& Gap : FloorGaps)
		{
			Rect(From, Gap.X - Band, 0.f, Band);
			From = FMath::Max(From, Gap.Y + Band);
		}
		Rect(From, ExtentHi, 0.f, Band);
	}

	// Around each hole, on the face beside it, HoleMargin out from its edges.
	for (const FFaceHole& Hole : Holes)
	{
		if (Hole.End - Hole.Start < 1.f || Hole.Top - Hole.Bottom < 1.f) continue;
		const float S = Hole.Start - HoleMargin, E = Hole.End + HoleMargin;
		const float T = Hole.Top + HoleMargin, B = Hole.Bottom - HoleMargin;
		const float Z0 = Hole.bOnFloor ? 0.f : B - Band;
		Rect(S - Band, S, Z0, T + Band);
		Rect(E, E + Band, Z0, T + Band);
		Rect(S, E, T, T + Band);
		if (!Hole.bOnFloor)
		{
			Rect(S, E, B - Band, B);
		}
	}
}

void PlannerSelectionOverlay::AddPolygonFrame(FPlannerMeshBuffers& Out, const TArray<FVector2D>& Polygon, float Z, float Band, const FVector& Normal)
{
	const int32 N = Polygon.Num();
	if (N < 3 || Band <= 0.f) return;
	double TwiceArea = 0.0;
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D& A = Polygon[i];
		const FVector2D& B = Polygon[(i + 1) % N];
		TwiceArea += (double)A.X * B.Y - (double)B.X * A.Y;
	}
	// Counter-clockwise: the interior is on the left of every edge.
	const float InwardSign = TwiceArea >= 0.0 ? 1.f : -1.f;
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D A = Polygon[i];
		const FVector2D B = Polygon[(i + 1) % N];
		const FVector2D Dir = (B - A).GetSafeNormal();
		if (Dir.IsNearlyZero()) continue;
		const FVector2D In = FVector2D(-Dir.Y, Dir.X) * InwardSign * Band;
		PlannerMeshBuilder::AddQuad(Out, FVector(A.X, A.Y, Z), FVector(B.X, B.Y, Z), FVector(B.X + In.X, B.Y + In.Y, Z), FVector(A.X + In.X, A.Y + In.Y, Z), Normal);
	}
}
