// Copyright 2026 MaxiMall. All Rights Reserved.

#include "PlannerOpeningBuilder.h"
#include "Engine/Texture2D.h"

void FPlannerMeshBuffers::Reset()
{
	Vertices.Reset();
	Triangles.Reset();
	Normals.Reset();
	UVs.Reset();
	Tangents.Reset();
}

FPlannerMeshBuffers& FPlannerSectionedMesh::Get(EPlannerOpeningMaterial Kind, const FLinearColor& Color)
{
	for (FSection& Section : Sections)
	{
		if (Section.Kind == Kind && Section.Color.Equals(Color, 0.002f))
		{
			return Section.Buffers;
		}
	}
	FSection* Added = new FSection();
	Added->Kind = Kind;
	Added->Color = Color;
	Sections.Add(Added);
	return Added->Buffers;
}

bool FPlannerSectionedMesh::IsEmpty() const
{
	for (const FSection& Section : Sections)
	{
		if (!Section.Buffers.IsEmpty()) return false;
	}
	return true;
}

namespace
{
	/** Engine front face of triangle (A, B, C) points along -cross(B - A, C - A). */
	bool ShouldReverseWinding(const FVector& Winding, const FVector& Normal)
	{
		return FVector::DotProduct(Winding, Normal) > 0.f;
	}

	FProcMeshTangent MakeSurfaceTangent(const FVector& Normal, const FVector& UDirection, const FVector& VDirection)
	{
		FVector TangentX = UDirection - Normal * FVector::DotProduct(UDirection, Normal);
		if (!TangentX.Normalize())
		{
			TangentX = FVector::CrossProduct(Normal, FMath::Abs(Normal.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector);
			TangentX.Normalize();
		}
		// The vertex factory rebuilds TangentY as cross(TangentZ, TangentX) * sign; pick the sign that follows +V.
		const bool bFlipY = FVector::DotProduct(FVector::CrossProduct(Normal, TangentX), VDirection) < 0.f;
		return FProcMeshTangent(TangentX, bFlipY);
	}
}

void PlannerMeshBuilder::AddQuadUV(FPlannerMeshBuffers& Out, const FVector& V0, const FVector& V1, const FVector& V2, const FVector& V3,
                                   const FVector& Normal, const FVector2D& UV0, const FVector2D& UV1, const FVector2D& UV2, const FVector2D& UV3)
{
	const FVector N = Normal.GetSafeNormal();
	const int32 Base = Out.Vertices.Num();

	Out.Vertices.Add(V0); Out.Vertices.Add(V1); Out.Vertices.Add(V2); Out.Vertices.Add(V3);
	Out.Normals.Add(N); Out.Normals.Add(N); Out.Normals.Add(N); Out.Normals.Add(N);
	Out.UVs.Add(UV0); Out.UVs.Add(UV1); Out.UVs.Add(UV2); Out.UVs.Add(UV3);

	const FProcMeshTangent Tangent = MakeSurfaceTangent(N, V1 - V0, V3 - V0);
	Out.Tangents.Add(Tangent); Out.Tangents.Add(Tangent); Out.Tangents.Add(Tangent); Out.Tangents.Add(Tangent);

	// Area vector of the whole quad (robust when one corner triangle is degenerate).
	const FVector Winding = FVector::CrossProduct(V1 - V0, V2 - V0) + FVector::CrossProduct(V2 - V0, V3 - V0);
	if (ShouldReverseWinding(Winding, N))
	{
		Out.Triangles.Add(Base); Out.Triangles.Add(Base + 2); Out.Triangles.Add(Base + 1);
		Out.Triangles.Add(Base); Out.Triangles.Add(Base + 3); Out.Triangles.Add(Base + 2);
	}
	else
	{
		Out.Triangles.Add(Base); Out.Triangles.Add(Base + 1); Out.Triangles.Add(Base + 2);
		Out.Triangles.Add(Base); Out.Triangles.Add(Base + 2); Out.Triangles.Add(Base + 3);
	}
}

void PlannerMeshBuilder::AddQuad(FPlannerMeshBuffers& Out, const FVector& V0, const FVector& V1, const FVector& V2, const FVector& V3,
                                 const FVector& Normal, float UVScale)
{
	const float Scale = FMath::Max(UVScale, KINDA_SMALL_NUMBER);
	const float W = FVector::Distance(V0, V1) / Scale;
	const float H = FVector::Distance(V0, V3) / Scale;
	AddQuadUV(Out, V0, V1, V2, V3, Normal, FVector2D(0.f, 0.f), FVector2D(W, 0.f), FVector2D(W, H), FVector2D(0.f, H));
}

void PlannerMeshBuilder::AddTriangle(FPlannerMeshBuffers& Out, const FVector& A, const FVector& B, const FVector& C, const FVector& Normal,
                                     const FVector2D& UVA, const FVector2D& UVB, const FVector2D& UVC)
{
	const FVector N = Normal.GetSafeNormal();
	const int32 Base = Out.Vertices.Num();
	Out.Vertices.Add(A); Out.Vertices.Add(B); Out.Vertices.Add(C);
	Out.Normals.Add(N); Out.Normals.Add(N); Out.Normals.Add(N);
	Out.UVs.Add(UVA); Out.UVs.Add(UVB); Out.UVs.Add(UVC);
	const FProcMeshTangent Tangent = MakeSurfaceTangent(N, B - A, C - A);
	Out.Tangents.Add(Tangent); Out.Tangents.Add(Tangent); Out.Tangents.Add(Tangent);

	if (ShouldReverseWinding(FVector::CrossProduct(B - A, C - A), N))
	{
		Out.Triangles.Add(Base); Out.Triangles.Add(Base + 2); Out.Triangles.Add(Base + 1);
	}
	else
	{
		Out.Triangles.Add(Base); Out.Triangles.Add(Base + 1); Out.Triangles.Add(Base + 2);
	}
}

void PlannerMeshBuilder::AddBox(FPlannerMeshBuffers& Out, const FVector& Origin, const FVector& AxisX, const FVector& AxisY, const FVector& AxisZ,
                                FVector Min, FVector Max, uint8 FaceMask)
{
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (Min[Axis] > Max[Axis]) Swap(Min[Axis], Max[Axis]);
	}
	if (FMath::IsNearlyEqual(Min.X, Max.X) || FMath::IsNearlyEqual(Min.Y, Max.Y) || FMath::IsNearlyEqual(Min.Z, Max.Z))
	{
		return; // flat box: nothing visible
	}

	auto P = [&](double X, double Y, double Z) { return Origin + AxisX * X + AxisY * Y + AxisZ * Z; };
	const FVector& A = Min;
	const FVector& B = Max;

	if (FaceMask & EPlannerBoxFace::NegX) AddQuad(Out, P(A.X, A.Y, A.Z), P(A.X, B.Y, A.Z), P(A.X, B.Y, B.Z), P(A.X, A.Y, B.Z), -AxisX);
	if (FaceMask & EPlannerBoxFace::PosX) AddQuad(Out, P(B.X, B.Y, A.Z), P(B.X, A.Y, A.Z), P(B.X, A.Y, B.Z), P(B.X, B.Y, B.Z), AxisX);
	if (FaceMask & EPlannerBoxFace::NegY) AddQuad(Out, P(B.X, A.Y, A.Z), P(A.X, A.Y, A.Z), P(A.X, A.Y, B.Z), P(B.X, A.Y, B.Z), -AxisY);
	if (FaceMask & EPlannerBoxFace::PosY) AddQuad(Out, P(A.X, B.Y, A.Z), P(B.X, B.Y, A.Z), P(B.X, B.Y, B.Z), P(A.X, B.Y, B.Z), AxisY);
	if (FaceMask & EPlannerBoxFace::NegZ) AddQuad(Out, P(A.X, A.Y, A.Z), P(B.X, A.Y, A.Z), P(B.X, B.Y, A.Z), P(A.X, B.Y, A.Z), -AxisZ);
	if (FaceMask & EPlannerBoxFace::PosZ) AddQuad(Out, P(A.X, A.Y, B.Z), P(A.X, B.Y, B.Z), P(B.X, B.Y, B.Z), P(B.X, A.Y, B.Z), AxisZ);
}

void PlannerMeshBuilder::AddLocalBox(FPlannerMeshBuffers& Out, const FVector& Min, const FVector& Max, uint8 FaceMask)
{
	AddBox(Out, FVector::ZeroVector, FVector::ForwardVector, FVector::RightVector, FVector::UpVector, Min, Max, FaceMask);
}

void PlannerMeshBuilder::AddArcStrip(FPlannerMeshBuffers& Out, const FVector2D& Center, const FVector2D& From, const FVector2D& Toward,
                                     float AngleRad, float InnerRadius, float OuterRadius, float Z, int32 Segments)
{
	const int32 Count = FMath::Max(1, Segments);
	for (int32 i = 0; i < Count; ++i)
	{
		const float T0 = AngleRad * (float)i / (float)Count;
		const float T1 = AngleRad * (float)(i + 1) / (float)Count;
		const FVector2D R0 = From * FMath::Cos(T0) + Toward * FMath::Sin(T0);
		const FVector2D R1 = From * FMath::Cos(T1) + Toward * FMath::Sin(T1);
		const FVector2D P0 = Center + R0 * InnerRadius;
		const FVector2D P1 = Center + R0 * OuterRadius;
		const FVector2D P2 = Center + R1 * OuterRadius;
		const FVector2D P3 = Center + R1 * InnerRadius;
		AddQuad(Out, FVector(P0.X, P0.Y, Z), FVector(P1.X, P1.Y, Z), FVector(P2.X, P2.Y, Z), FVector(P3.X, P3.Y, Z), FVector::UpVector);
	}
}

void PlannerMeshBuilder::AddDisc(FPlannerMeshBuffers& Out, const FVector2D& Center, float Radius, float Z, int32 Segments)
{
	const int32 Count = FMath::Max(3, Segments);
	const FVector C(Center.X, Center.Y, Z);
	for (int32 i = 0; i < Count; ++i)
	{
		const float A0 = UE_TWO_PI * (float)i / (float)Count;
		const float A1 = UE_TWO_PI * (float)(i + 1) / (float)Count;
		const FVector P0(Center.X + Radius * FMath::Cos(A0), Center.Y + Radius * FMath::Sin(A0), Z);
		const FVector P1(Center.X + Radius * FMath::Cos(A1), Center.Y + Radius * FMath::Sin(A1), Z);
		AddTriangle(Out, C, P0, P1, FVector::UpVector, FVector2D(0.f, 0.5f), FVector2D(1.f, 0.5f), FVector2D(1.f, 0.5f));
	}
}

void PlannerMeshBuilder::AddInwardCylinder(FPlannerMeshBuffers& Out, const FVector2D& Center, float Radius, float Z0, float Z1, int32 Segments, bool bTopCap)
{
	const int32 Count = FMath::Max(3, Segments);
	for (int32 i = 0; i < Count; ++i)
	{
		const float A0 = UE_TWO_PI * (float)i / (float)Count;
		const float A1 = UE_TWO_PI * (float)(i + 1) / (float)Count;
		const FVector2D D0(FMath::Cos(A0), FMath::Sin(A0));
		const FVector2D D1(FMath::Cos(A1), FMath::Sin(A1));
		const FVector2D Mid = (D0 + D1).GetSafeNormal();
		const FVector Inward(-Mid.X, -Mid.Y, 0.f);
		const FVector B0(Center.X + D0.X * Radius, Center.Y + D0.Y * Radius, Z0);
		const FVector B1(Center.X + D1.X * Radius, Center.Y + D1.Y * Radius, Z0);
		const FVector T0(B0.X, B0.Y, Z1);
		const FVector T1(B1.X, B1.Y, Z1);
		const float U0 = (float)i / (float)Count;
		const float U1 = (float)(i + 1) / (float)Count;
		AddQuadUV(Out, B0, B1, T1, T0, Inward, FVector2D(U0, 1.f), FVector2D(U1, 1.f), FVector2D(U1, 0.f), FVector2D(U0, 0.f));
		if (bTopCap)
		{
			AddTriangle(Out, FVector(Center.X, Center.Y, Z1), T0, T1, -FVector::UpVector, FVector2D(0.5f, 0.f), FVector2D(U0, 0.f), FVector2D(U1, 0.f));
		}
	}
}

int32 PlannerMeshBuilder::CountMisorientedTriangles(const FPlannerMeshBuffers& Buffers)
{
	int32 Bad = 0;
	for (int32 t = 0; t + 2 < Buffers.Triangles.Num(); t += 3)
	{
		const int32 I0 = Buffers.Triangles[t], I1 = Buffers.Triangles[t + 1], I2 = Buffers.Triangles[t + 2];
		if (!Buffers.Vertices.IsValidIndex(I0) || !Buffers.Vertices.IsValidIndex(I1) || !Buffers.Vertices.IsValidIndex(I2)
			|| !Buffers.Normals.IsValidIndex(I0) || !Buffers.Normals.IsValidIndex(I1) || !Buffers.Normals.IsValidIndex(I2))
		{
			++Bad;
			continue;
		}
		const FVector Cross = FVector::CrossProduct(Buffers.Vertices[I1] - Buffers.Vertices[I0], Buffers.Vertices[I2] - Buffers.Vertices[I0]);
		if (Cross.SizeSquared() < KINDA_SMALL_NUMBER)
		{
			continue; // zero-area triangle: invisible, not misoriented
		}
		const FVector Front = -Cross;
		const FVector Normal = Buffers.Normals[I0] + Buffers.Normals[I1] + Buffers.Normals[I2];
		if (FVector::DotProduct(Front, Normal) <= 0.f)
		{
			++Bad;
		}
	}
	return Bad;
}

// ─────────────────────────────────────────────────────────────────────────────
// Leaf / sash
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	/** Box spanning X, Z and a depth interval measured from the swing face (0) toward the body (BodySign). */
	void AddLeafBox(FPlannerMeshBuffers& Out, float BodySign, float X0, float X1, float Z0, float Z1, float DepthFrom, float DepthTo,
	                uint8 FaceMask = EPlannerBoxFace::All)
	{
		const float YA = BodySign * DepthFrom;
		const float YB = BodySign * DepthTo;
		PlannerMeshBuilder::AddLocalBox(Out, FVector(X0, FMath::Min(YA, YB), Z0), FVector(X1, FMath::Max(YA, YB), Z1), FaceMask);
	}

	/** Box projecting from a face at FaceY by the distances Near..Far along OutwardSign (the face side is hidden). */
	void AddProjectingBox(FPlannerMeshBuffers& Out, float FaceY, float OutwardSign, float Near, float Far, float X0, float X1, float Z0, float Z1)
	{
		const float YA = FaceY + OutwardSign * Near;
		const float YB = FaceY + OutwardSign * Far;
		const uint8 HiddenFace = OutwardSign > 0.f ? EPlannerBoxFace::NegY : EPlannerBoxFace::PosY;
		PlannerMeshBuilder::AddLocalBox(Out, FVector(X0, FMath::Min(YA, YB), Z0), FVector(X1, FMath::Max(YA, YB), Z1),
			Near <= KINDA_SMALL_NUMBER ? (EPlannerBoxFace::All & ~HiddenFace) : EPlannerBoxFace::All);
	}

	/** Lever handle on one face: rosette, neck and a lever pointing toward the hinge. */
	void AddLeverHandle(FPlannerMeshBuffers& Metal, float LatchX, float CenterZ, float FaceY, float OutwardSign)
	{
		AddProjectingBox(Metal, FaceY, OutwardSign, 0.f, 0.9f, LatchX - 2.6f, LatchX + 2.6f, CenterZ - 2.6f, CenterZ + 2.6f);
		AddProjectingBox(Metal, FaceY, OutwardSign, 0.9f, 5.0f, LatchX - 0.9f, LatchX + 0.9f, CenterZ - 0.9f, CenterZ + 0.9f);
		AddProjectingBox(Metal, FaceY, OutwardSign, 5.0f, 6.6f, LatchX - 12.5f, LatchX + 1.0f, CenterZ - 0.9f, CenterZ + 0.9f);
	}

	void BuildDoorBody(const FPlannerLeafParams& P, float W, float H, float T, float S, FPlannerMeshBuffers& Leaf, FPlannerMeshBuffers& Glass)
	{
		const float Stile = FMath::Min(12.f, W * 0.18f);
		const float TopRail = FMath::Min(15.f, H * 0.08f);
		const float BottomRail = FMath::Min(22.f, H * 0.11f);
		const float MidRail = FMath::Min(12.f, H * 0.06f);
		const bool bFullGlass = P.Design == EPlannerLeafDesign::FullGlass;
		const float StileUsed = bFullGlass ? FMath::Min(10.f, W * 0.15f) : Stile;

		const bool bPanelled = P.Design != EPlannerLeafDesign::Flush
			&& W >= 2.f * StileUsed + 14.f
			&& H >= BottomRail + TopRail + (bFullGlass ? 30.f : MidRail + 50.f);
		if (!bPanelled)
		{
			AddLeafBox(Leaf, S, 0.f, W, 0.f, H, 0.f, T);
			return;
		}

		const uint8 RailFaces = EPlannerBoxFace::All & ~(EPlannerBoxFace::NegX | EPlannerBoxFace::PosX);
		AddLeafBox(Leaf, S, 0.f, StileUsed, 0.f, H, 0.f, T);                                      // hinge stile
		AddLeafBox(Leaf, S, W - StileUsed, W, 0.f, H, 0.f, T);                                    // latch stile
		AddLeafBox(Leaf, S, StileUsed, W - StileUsed, 0.f, BottomRail, 0.f, T, RailFaces);        // bottom rail
		AddLeafBox(Leaf, S, StileUsed, W - StileUsed, H - TopRail, H, 0.f, T, RailFaces);         // top rail

		const float Recess = FMath::Min(1.2f, T * 0.3f);
		const float PanelFaces = EPlannerBoxFace::NegY | EPlannerBoxFace::PosY;
		auto AddPanel = [&](float Z0, float Z1)
		{
			if (Z1 - Z0 > 1.f) AddLeafBox(Leaf, S, StileUsed, W - StileUsed, Z0, Z1, Recess, T - Recess, (uint8)PanelFaces);
		};
		auto AddPane = [&](float Z0, float Z1)
		{
			if (Z1 - Z0 > 1.f) AddLeafBox(Glass, S, StileUsed, W - StileUsed, Z0, Z1, T * 0.5f - 0.4f, T * 0.5f + 0.4f, (uint8)PanelFaces);
		};

		if (bFullGlass)
		{
			AddPane(BottomRail, H - TopRail);
			return;
		}

		// Lock rail centred on the handle, kept clear of the top and bottom rails.
		const float MidZ = FMath::Clamp(P.HandleZ - MidRail * 0.5f, BottomRail + 25.f, H - TopRail - 25.f - MidRail);
		AddLeafBox(Leaf, S, StileUsed, W - StileUsed, MidZ, MidZ + MidRail, 0.f, T, RailFaces);
		AddPanel(BottomRail, MidZ);
		if (P.Design == EPlannerLeafDesign::GlazedTop)
		{
			AddPane(MidZ + MidRail, H - TopRail);
		}
		else
		{
			AddPanel(MidZ + MidRail, H - TopRail);
		}
	}
}

void PlannerOpeningGeometry::BuildLeaf(const FPlannerLeafParams& Params, FPlannerSectionedMesh& Out)
{
	const float W = FMath::Max(4.f, Params.Width);
	const float H = FMath::Max(4.f, Params.Height);
	const float T = FMath::Max(1.f, Params.Thickness);
	const float S = Params.BodySign >= 0.f ? 1.f : -1.f;
	const EPlannerOpeningMaterial GlassKind = (Params.GlassKind == EPlannerOpeningMaterial::FrostedGlass)
		? EPlannerOpeningMaterial::FrostedGlass : EPlannerOpeningMaterial::Glass;

	FPlannerMeshBuffers& Leaf = Out.Get(EPlannerOpeningMaterial::Leaf, Params.LeafColor);
	FPlannerMeshBuffers& Metal = Out.Get(EPlannerOpeningMaterial::Metal, Params.MetalColor);
	FPlannerMeshBuffers& Glass = Out.Get(GlassKind, FLinearColor::White);

	if (Params.Type == EOpeningType::Door)
	{
		BuildDoorBody(Params, W, H, T, S, Leaf, Glass);

		// Lever handles on both faces near the latch edge.
		const float HandleZ = FMath::Clamp(Params.HandleZ, 10.f, H - 10.f);
		const float LatchX = W - 6.f;
		AddLeverHandle(Metal, LatchX, HandleZ, 0.f, -S);   // swing face (Y = 0): outward away from the body
		AddLeverHandle(Metal, LatchX, HandleZ, S * T, S);  // opposite face

		// Hinge knuckles centred on the rotation axis.
		const float KnuckleH = FMath::Min(10.f, H * 0.1f);
		TArray<float, TInlineAllocator<3>> HingeZ;
		HingeZ.Add(FMath::Min(22.f, H * 0.12f));
		HingeZ.Add(H - FMath::Min(22.f, H * 0.12f) - KnuckleH);
		if (H > 220.f) HingeZ.Add(H * 0.5f - KnuckleH * 0.5f);
		for (float Z : HingeZ)
		{
			PlannerMeshBuilder::AddLocalBox(Metal, FVector(-0.75f, -0.75f, Z), FVector(0.75f, 0.75f, Z + KnuckleH));
		}
		return;
	}

	// Window sash: frame around a glass pane, handle on the swing (room) face.
	const float Stile = FMath::Min(6.f, FMath::Min(W, H) * 0.25f);
	const uint8 RailFaces = EPlannerBoxFace::All & ~(EPlannerBoxFace::NegX | EPlannerBoxFace::PosX);
	AddLeafBox(Leaf, S, 0.f, Stile, 0.f, H, 0.f, T);                                   // hinge stile
	AddLeafBox(Leaf, S, W - Stile, W, 0.f, H, 0.f, T);                                 // latch stile
	AddLeafBox(Leaf, S, Stile, W - Stile, 0.f, Stile, 0.f, T, RailFaces);              // bottom rail
	AddLeafBox(Leaf, S, Stile, W - Stile, H - Stile, H, 0.f, T, RailFaces);            // top rail
	if (W - 2.f * Stile > 1.f && H - 2.f * Stile > 1.f)
	{
		AddLeafBox(Glass, S, Stile, W - Stile, Stile, H - Stile, T * 0.5f - 0.4f, T * 0.5f + 0.4f,
			EPlannerBoxFace::NegY | EPlannerBoxFace::PosY);
	}

	// Handle on the room face: the swing face for inward sashes, the opposite face for outward ones.
	const float HandleX = W - Stile * 0.5f;
	const float HandleZ = H * 0.5f;
	const float HandleFaceY = Params.bSwingFaceIsInterior ? 0.f : S * T;
	const float HandleOutward = Params.bSwingFaceIsInterior ? -S : S;
	AddProjectingBox(Metal, HandleFaceY, HandleOutward, 0.f, 1.0f, HandleX - 1.6f, HandleX + 1.6f, HandleZ - 3.5f, HandleZ + 3.5f);
	AddProjectingBox(Metal, HandleFaceY, HandleOutward, 1.0f, 3.2f, HandleX - 0.8f, HandleX + 0.8f, HandleZ - 11.f, HandleZ + 1.f);
}

void PlannerOpeningGeometry::AddMitredCasing(FPlannerMeshBuffers& Out, const FVector& FaceOrigin, const FVector& AxisAlong, const FVector& AxisOut,
                                             float InnerStart, float InnerEnd, float BottomZ, float InnerTopZ, float Width, float Depth)
{
	if (Width <= 0.f || Depth <= 0.f || InnerEnd <= InnerStart || InnerTopZ <= BottomZ) return;

	const float Chamfer = FMath::Min(0.6f, FMath::Min(Width, Depth) * 0.35f);
	// Profile in (u = away from the opening, v = out of the wall face), from the inner edge on the wall to the outer edge on the wall.
	const FVector2D Profile[5] = {
		FVector2D(0.f, 0.f), FVector2D(0.f, Depth), FVector2D(Width - Chamfer, Depth), FVector2D(Width, Depth - Chamfer), FVector2D(Width, 0.f) };
	const FVector Up = FVector::UpVector;
	auto At = [&](float Along, float Z, float Out) { return FaceOrigin + AxisAlong * Along + AxisOut * Out + Up * Z; };

	for (int32 k = 0; k < 4; ++k)
	{
		const FVector2D A = Profile[k];
		const FVector2D B = Profile[k + 1];
		const FVector2D D = B - A;
		const FVector2D N2(-D.Y, D.X); // outward normal of this profile edge in (u, v)

		// Start leg: u grows against AxisAlong; the top is mitred (z = InnerTopZ + u).
		PlannerMeshBuilder::AddQuad(Out,
			At(InnerStart - A.X, BottomZ, A.Y), At(InnerStart - A.X, InnerTopZ + A.X, A.Y),
			At(InnerStart - B.X, InnerTopZ + B.X, B.Y), At(InnerStart - B.X, BottomZ, B.Y),
			-AxisAlong * N2.X + AxisOut * N2.Y);

		// End leg: u grows along AxisAlong.
		PlannerMeshBuilder::AddQuad(Out,
			At(InnerEnd + A.X, BottomZ, A.Y), At(InnerEnd + A.X, InnerTopZ + A.X, A.Y),
			At(InnerEnd + B.X, InnerTopZ + B.X, B.Y), At(InnerEnd + B.X, BottomZ, B.Y),
			AxisAlong * N2.X + AxisOut * N2.Y);

		// Head: u grows upward; both ends mitred.
		PlannerMeshBuilder::AddQuad(Out,
			At(InnerStart - A.X, InnerTopZ + A.X, A.Y), At(InnerEnd + A.X, InnerTopZ + A.X, A.Y),
			At(InnerEnd + B.X, InnerTopZ + B.X, B.Y), At(InnerStart - B.X, InnerTopZ + B.X, B.Y),
			Up * N2.X + AxisOut * N2.Y);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Runtime textures
// ─────────────────────────────────────────────────────────────────────────────

UTexture2D* PlannerRuntimeTextures::CreateHdrPixels(int32 SizeX, int32 SizeY, const TArray<FLinearColor>& Pixels)
{
	if (SizeX <= 0 || SizeY <= 0 || Pixels.Num() != SizeX * SizeY) return nullptr;
	UTexture2D* Tex = UTexture2D::CreateTransient(SizeX, SizeY, PF_FloatRGBA);
	if (!Tex || !Tex->GetPlatformData() || Tex->GetPlatformData()->Mips.Num() == 0) return nullptr;
	Tex->SRGB = false;
	Tex->Filter = (SizeX == 1 && SizeY == 1) ? TF_Nearest : TF_Bilinear;
	Tex->AddressX = TA_Clamp;
	Tex->AddressY = TA_Clamp;
	FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
	if (FFloat16Color* Dest = static_cast<FFloat16Color*>(Mip.BulkData.Lock(LOCK_READ_WRITE)))
	{
		for (int32 i = 0; i < Pixels.Num(); ++i)
		{
			Dest[i] = FFloat16Color(Pixels[i]);
		}
	}
	Mip.BulkData.Unlock();
	Tex->UpdateResource();
	return Tex;
}

UTexture2D* PlannerRuntimeTextures::CreateHdrColor(const FLinearColor& Color)
{
	return CreateHdrPixels(1, 1, { Color });
}
