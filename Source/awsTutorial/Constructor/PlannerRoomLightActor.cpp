// Copyright 2026 MaxiMall. All Rights Reserved.

#include "Constructor/PlannerRoomLightActor.h"
#include "ProceduralMeshComponent.h"
#include "Components/RectLightComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "Math/Float16Color.h"

APlannerRoomLightActor::APlannerRoomLightActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	SurfaceMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SurfaceMesh"));
	SurfaceMesh->SetupAttachment(SceneRoot);
	SurfaceMesh->bUseAsyncCooking = false;
	SurfaceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SurfaceMesh->SetAbsolute(true, true, true);
	// The panel is only the visible face of the light: it must not shadow its own light (which sits below it),
	// and it must not be counted a second time as a light source by Lumen GI or ray-traced hit lighting.
	SurfaceMesh->SetCastShadow(false);
	SurfaceMesh->bAffectDynamicIndirectLighting = false;
	SurfaceMesh->bVisibleInRayTracing = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Geometry helpers
// ─────────────────────────────────────────────────────────────────────────────

float APlannerRoomLightActor::SignedArea(const TArray<FVector2D>& Poly)
{
	double A = 0.0;
	const int32 N = Poly.Num();
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D& P = Poly[i];
		const FVector2D& Q = Poly[(i + 1) % N];
		A += (double)P.X * Q.Y - (double)Q.X * P.Y;
	}
	return (float)(0.5 * A);
}

namespace PlannerRoomLightGeometry
{
	static double Cross(const FVector2D& O, const FVector2D& A, const FVector2D& B)
	{
		return ((double)A.X - O.X) * ((double)B.Y - O.Y) - ((double)A.Y - O.Y) * ((double)B.X - O.X);
	}

	/** Proper crossing of segments AB and CD (touching or collinear overlap does not count). */
	static bool SegmentsCross(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D)
	{
		const double D1 = Cross(C, D, A), D2 = Cross(C, D, B), D3 = Cross(A, B, C), D4 = Cross(A, B, D);
		const double Eps = 1e-6;
		if (FMath::Abs(D1) < Eps || FMath::Abs(D2) < Eps || FMath::Abs(D3) < Eps || FMath::Abs(D4) < Eps) return false;
		return ((D1 > 0.0) != (D2 > 0.0)) && ((D3 > 0.0) != (D4 > 0.0));
	}

	static float DistanceToSegment(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const float LenSq = AB.SizeSquared();
		const float T = LenSq > KINDA_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / LenSq, 0.f, 1.f) : 0.f;
		return FVector2D::Distance(P, A + AB * T);
	}
}

TArray<FVector2D> APlannerRoomLightActor::InsetPolygon(const TArray<FVector2D>& Polygon, float InsetCm)
{
	using namespace PlannerRoomLightGeometry;
	TArray<FVector2D> Out;
	const int32 N = Polygon.Num();
	if (N < 3) return Out;
	const float Area0 = SignedArea(Polygon);
	if (FMath::IsNearlyZero(Area0)) return Out;
	if (InsetCm <= 0.f) return Polygon;
	const float Sign = Area0 > 0.f ? 1.f : -1.f; // CCW: interior is to the LEFT of every edge

	Out.Reserve(N);
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D& Prev = Polygon[(i + N - 1) % N];
		const FVector2D& Cur = Polygon[i];
		const FVector2D& Next = Polygon[(i + 1) % N];
		const FVector2D D0 = (Cur - Prev).GetSafeNormal();
		const FVector2D D1 = (Next - Cur).GetSafeNormal();
		const FVector2D N0 = FVector2D(-D0.Y, D0.X) * Sign; // inward normals
		const FVector2D N1 = FVector2D(-D1.Y, D1.X) * Sign;

		// Offset lines (Prev + N0·m) + t·D0 and (Cur + N1·m) + u·D1 intersect at the mitred corner.
		const FVector2D A = Prev + N0 * InsetCm;
		const FVector2D B = Cur + N1 * InsetCm;
		const float Denom = D0.X * D1.Y - D0.Y * D1.X;
		FVector2D V;
		if (FMath::Abs(Denom) < 1e-4f)
		{
			V = Cur + N0 * InsetCm; // collinear edges
		}
		else
		{
			const FVector2D AB = B - A;
			const float T = (AB.X * D1.Y - AB.Y * D1.X) / Denom;
			V = A + D0 * T;
			if (FVector2D::Distance(V, Cur) > 3.f * InsetCm) // acute corner: cap the mitre
			{
				V = Cur + (N0 + N1).GetSafeNormal() * InsetCm * 1.5f;
			}
		}
		Out.Add(V);
	}

	const float Area1 = SignedArea(Out);
	if (Area1 * Area0 <= 0.f || FMath::Abs(Area1) < 400.f)
	{
		Out.Reset(); // collapsed or inverted
		return Out;
	}

	// A feature narrower than twice the inset folds: its offset edges reverse direction or cross other edges, which
	// would give a self-intersecting panel, a failed triangulation and a mask that no longer matches the panel.
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D Src = Polygon[(i + 1) % N] - Polygon[i];
		const FVector2D Off = Out[(i + 1) % N] - Out[i];
		if (FVector2D::DotProduct(Src, Off) <= 0.f)
		{
			Out.Reset();
			return Out;
		}
	}
	for (int32 i = 0; i < N; ++i)
	{
		for (int32 j = i + 2; j < N; ++j)
		{
			if (i == 0 && j == N - 1) continue; // adjacent through the wrap-around
			if (SegmentsCross(Out[i], Out[(i + 1) % N], Out[j], Out[(j + 1) % N]))
			{
				Out.Reset();
				return Out;
			}
		}
	}
	// Every inset vertex must keep (almost) the full inset from the source edges it is NOT built from. The acute-corner
	// mitre clamp can otherwise hide a collapsed inset: in a very thin room the clamped corners keep a positive area and
	// no edge reverses, yet the far vertex lands a few centimetres from the opposite wall (a sliver panel).
	for (int32 i = 0; i < N; ++i)
	{
		for (int32 e = 0; e < N; ++e)
		{
			if (e == i || e == (i + N - 1) % N) continue; // source edges (i−1, i) and (i, i+1) built this vertex
			if (DistanceToSegment(Out[i], Polygon[e], Polygon[(e + 1) % N]) < 0.9f * InsetCm)
			{
				Out.Reset();
				return Out;
			}
		}
	}
	return Out;
}

void APlannerRoomLightActor::TriangulatePolygon(const TArray<FVector2D>& Poly, TArray<int32>& OutTris)
{
	OutTris.Reset();
	const int32 N = Poly.Num();
	if (N < 3) return;
	const bool bCCW = SignedArea(Poly) > 0.f;
	TArray<int32> Idx; Idx.Reserve(N);
	for (int32 i = 0; i < N; ++i) Idx.Add(i);
	auto Cross = [](const FVector2D& A, const FVector2D& B, const FVector2D& C) { return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X); };
	auto InTri = [&](const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C)
	{
		const float C1 = Cross(A, B, P), C2 = Cross(B, C, P), C3 = Cross(C, A, P);
		return bCCW ? (C1 >= 0.f && C2 >= 0.f && C3 >= 0.f) : (C1 <= 0.f && C2 <= 0.f && C3 <= 0.f);
	};
	int32 Guard = 0;
	while (Idx.Num() > 3 && Guard++ < 10000)
	{
		bool bClipped = false;
		for (int32 k = 0; k < Idx.Num(); ++k)
		{
			const int32 Ia = Idx[(k + Idx.Num() - 1) % Idx.Num()], Ib = Idx[k], Ic = Idx[(k + 1) % Idx.Num()];
			const FVector2D& A = Poly[Ia]; const FVector2D& B = Poly[Ib]; const FVector2D& C = Poly[Ic];
			const float Turn = Cross(A, B, C);
			if (bCCW ? (Turn <= 0.f) : (Turn >= 0.f)) continue; // reflex corner is never an ear
			bool bEar = true;
			for (int32 Other : Idx)
			{
				if (Other == Ia || Other == Ib || Other == Ic) continue;
				if (InTri(Poly[Other], A, B, C)) { bEar = false; break; }
			}
			if (!bEar) continue;
			OutTris.Add(Ia); OutTris.Add(Ib); OutTris.Add(Ic);
			Idx.RemoveAt(k);
			bClipped = true;
			break;
		}
		if (!bClipped) break; // degenerate input: caller checks the triangle count
	}
	if (Idx.Num() == 3) { OutTris.Add(Idx[0]); OutTris.Add(Idx[1]); OutTris.Add(Idx[2]); }
}

TArray<FVector2D> APlannerRoomLightActor::ConvexHull(const TArray<FVector2D>& Points)
{
	using namespace PlannerRoomLightGeometry;
	TArray<FVector2D> P = Points;
	P.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X || (A.X == B.X && A.Y < B.Y); });
	const int32 N = P.Num();
	if (N < 3) return P;
	TArray<FVector2D> H;
	H.SetNum(2 * N);
	int32 K = 0;
	for (int32 i = 0; i < N; ++i)
	{
		while (K >= 2 && Cross(H[K - 2], H[K - 1], P[i]) <= 0.0) --K;
		H[K++] = P[i];
	}
	for (int32 i = N - 2, Lower = K + 1; i >= 0; --i)
	{
		while (K >= Lower && Cross(H[K - 2], H[K - 1], P[i]) <= 0.0) --K;
		H[K++] = P[i];
	}
	H.SetNum(FMath::Max(K - 1, 0));
	return H;
}

bool APlannerRoomLightActor::ComputeOrientedRect(const TArray<FVector2D>& Poly, FPlannerOrientedRect& OutRect)
{
	if (Poly.Num() < 3) return false;

	// The minimum-area enclosing rectangle has one side collinear with an edge of the convex hull, so every hull
	// edge direction is tested (polygon edges alone miss the hull's bridges over concave notches).
	const TArray<FVector2D> Hull = ConvexHull(Poly);
	const int32 N = Hull.Num();
	if (N < 3) return false;

	double BestArea = TNumericLimits<double>::Max();
	bool bFound = false;
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D Edge = Hull[(i + 1) % N] - Hull[i];
		if (Edge.SizeSquared() < 1.f) continue;
		const FVector2D Ax = Edge.GetSafeNormal();
		const FVector2D Ay(-Ax.Y, Ax.X);
		double MinX = TNumericLimits<double>::Max(), MaxX = -TNumericLimits<double>::Max();
		double MinY = TNumericLimits<double>::Max(), MaxY = -TNumericLimits<double>::Max();
		for (const FVector2D& P : Hull)
		{
			const double PX = FVector2D::DotProduct(P, Ax);
			const double PY = FVector2D::DotProduct(P, Ay);
			MinX = FMath::Min(MinX, PX); MaxX = FMath::Max(MaxX, PX);
			MinY = FMath::Min(MinY, PY); MaxY = FMath::Max(MaxY, PY);
		}
		const double Area = (MaxX - MinX) * (MaxY - MinY);
		if (Area < BestArea - 1e-3)
		{
			BestArea = Area;
			OutRect.AxisX = Ax;
			OutRect.AxisY = Ay;
			OutRect.Extent = FVector2D((float)(MaxX - MinX), (float)(MaxY - MinY));
			OutRect.Center = Ax * (float)(0.5 * (MinX + MaxX)) + Ay * (float)(0.5 * (MinY + MaxY));
			bFound = true;
		}
	}
	if (!bFound) return false;

	// SourceWidth (spanned by AxisY) is the size shadow maps use for the penumbra, so the SHORTER side goes on
	// AxisY. Rotating +90° keeps AxisY = AxisX rotated +90°.
	if (OutRect.Extent.Y > OutRect.Extent.X)
	{
		const FVector2D OldX = OutRect.AxisX;
		OutRect.AxisX = OutRect.AxisY;
		OutRect.AxisY = -OldX;
		OutRect.Extent = FVector2D(OutRect.Extent.Y, OutRect.Extent.X);
	}
	OutRect.YawDeg = FMath::RadiansToDegrees(FMath::Atan2(OutRect.AxisX.Y, OutRect.AxisX.X));
	return OutRect.Extent.X > 1.f && OutRect.Extent.Y > 1.f;
}

float APlannerRoomLightActor::RasterizeLightMask(const TArray<FVector2D>& Polygon, const FPlannerOrientedRect& Rect, int32 Res, TArray<uint8>& OutBGRA)
{
	OutBGRA.Reset();
	const int32 N = Polygon.Num();
	if (N < 3 || Res < 2) return 0.f;

	// Rect light frame with rotation (Pitch −90°, Yaw θ): local +Y = (−sinθ, cosθ) = Rect.AxisY, local +Z =
	// (cosθ, sinθ) = Rect.AxisX. SourceWidth W spans local Y, SourceHeight H spans local Z.
	// UE 5.6 rect light texture lookup (RectLight.ush SampleSourceTexture, RectUV = PointInRect/FullExtent ×
	// (0.5, −0.5) + 0.5 with Axis[0] = −Y, Axis[1] = +Z): u = 0.5 − y/W, v = 0.5 − z/H, and texel (column, row) =
	// (u·Res, v·Res) with row 0 the first row of the mip data (the +Z edge).
	const float W = Rect.Extent.Y;
	const float H = Rect.Extent.X;
	TArray<FVector2D> Px; Px.Reserve(N);
	for (const FVector2D& P : Polygon)
	{
		const FVector2D D = P - Rect.Center;
		const float LocalY = FVector2D::DotProduct(D, Rect.AxisY);
		const float LocalZ = FVector2D::DotProduct(D, Rect.AxisX);
		Px.Add(FVector2D((0.5f - LocalY / W) * Res, (0.5f - LocalZ / H) * Res));
	}

	// Scanline fill with exact horizontal coverage and 8 sub-rows per texel row (even–odd rule, simple polygons).
	const int32 SubRows = 8;
	const float RowWeight = 1.f / SubRows;
	TArray<float> Coverage; Coverage.SetNumZeroed(Res * Res);
	TArray<double> Xs; Xs.Reserve(N);
	for (int32 Row = 0; Row < Res; ++Row)
	{
		for (int32 S = 0; S < SubRows; ++S)
		{
			const double Y = Row + (S + 0.5) / SubRows;
			Xs.Reset();
			for (int32 i = 0; i < N; ++i)
			{
				const FVector2D& A = Px[i];
				const FVector2D& B = Px[(i + 1) % N];
				if ((A.Y <= Y && B.Y > Y) || (B.Y <= Y && A.Y > Y))
				{
					const double T = (Y - A.Y) / (B.Y - A.Y);
					Xs.Add(A.X + T * (B.X - A.X));
				}
			}
			Xs.Sort();
			for (int32 k = 0; k + 1 < Xs.Num(); k += 2)
			{
				const double X0 = FMath::Clamp(Xs[k], 0.0, (double)Res);
				const double X1 = FMath::Clamp(Xs[k + 1], 0.0, (double)Res);
				if (X1 <= X0) continue;
				const int32 I0 = FMath::Min((int32)X0, Res - 1);
				const int32 I1 = (int32)X1;
				float* RowCov = &Coverage[Row * Res];
				if (I0 == I1 || (I1 >= Res && I0 == Res - 1))
				{
					RowCov[I0] += (float)(X1 - X0) * RowWeight;
					continue;
				}
				RowCov[I0] += (float)((I0 + 1) - X0) * RowWeight;
				for (int32 I = I0 + 1; I < I1 && I < Res; ++I) RowCov[I] += RowWeight;
				if (I1 < Res) RowCov[I1] += (float)(X1 - I1) * RowWeight;
			}
		}
	}

	double Sum = 0.0;
	OutBGRA.SetNumUninitialized(Res * Res * 4);
	for (int32 i = 0; i < Res * Res; ++i)
	{
		const float C = FMath::Clamp(Coverage[i], 0.f, 1.f);
		Sum += C;
		const uint8 V = (uint8)FMath::RoundToInt(C * 255.f);
		OutBGRA[i * 4 + 0] = V; // B
		OutBGRA[i * 4 + 1] = V; // G
		OutBGRA[i * 4 + 2] = V; // R  (the rect light atlas keeps RGB only)
		OutBGRA[i * 4 + 3] = 255;
	}
	return (float)(Sum / (Res * Res));
}

UTexture2D* APlannerRoomLightActor::CreateLinearTexture(int32 SizeX, int32 SizeY, const TArray<uint8>& BGRA)
{
	if (BGRA.Num() != SizeX * SizeY * 4) return nullptr;
	UTexture2D* Tex = UTexture2D::CreateTransient(SizeX, SizeY, PF_B8G8R8A8);
	if (!Tex || !Tex->GetPlatformData() || Tex->GetPlatformData()->Mips.Num() == 0) return nullptr;
	Tex->SRGB = false; // mask values are linear coverage (the engine would otherwise sRGB-decode the edges)
	Tex->Filter = TF_Bilinear;
	Tex->AddressX = TA_Clamp;
	Tex->AddressY = TA_Clamp;
	FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
	if (void* Dest = Mip.BulkData.Lock(LOCK_READ_WRITE))
	{
		FMemory::Memcpy(Dest, BGRA.GetData(), BGRA.Num());
	}
	Mip.BulkData.Unlock();
	Tex->UpdateResource();
	return Tex;
}

UTexture2D* APlannerRoomLightActor::CreateHdrColorTexture(const FLinearColor& Color)
{
	UTexture2D* Tex = UTexture2D::CreateTransient(1, 1, PF_FloatRGBA);
	if (!Tex || !Tex->GetPlatformData() || Tex->GetPlatformData()->Mips.Num() == 0) return nullptr;
	Tex->SRGB = false;
	Tex->Filter = TF_Nearest;
	FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
	if (void* Dest = Mip.BulkData.Lock(LOCK_READ_WRITE))
	{
		const FFloat16Color Value(Color); // PF_FloatRGBA = R, G, B, A half floats (values above 1 are kept: HDR cd/m²)
		FMemory::Memcpy(Dest, &Value, sizeof(FFloat16Color));
	}
	Mip.BulkData.Unlock();
	Tex->UpdateResource();
	return Tex;
}

uint32 APlannerRoomLightActor::ComputeBuildSignature(const FRoomData& Room, const FPlannerRoomLightSettings& S)
{
	uint32 Hash = GetTypeHash(Room.FloorPolygon.Num());
	for (const FVector2D& P : Room.FloorPolygon)
	{
		Hash = HashCombineFast(Hash, GetTypeHash(FMath::RoundToInt(P.X * 10.f)));
		Hash = HashCombineFast(Hash, GetTypeHash(FMath::RoundToInt(P.Y * 10.f)));
	}
	Hash = HashCombineFast(Hash, GetTypeHash(FMath::RoundToInt(Room.CeilingHeightCm * 10.f)));
	Hash = HashCombineFast(Hash, GetTypeHash(FMath::RoundToInt(Room.AreaM2 * 1000.f)));
	return HashCombineFast(Hash, ComputeSettingsHash(S));
}

uint32 APlannerRoomLightActor::ComputeSettingsHash(const FPlannerRoomLightSettings& S)
{
	uint32 Hash = 0x5A17u;
	auto Add = [&Hash](uint32 V) { Hash = HashCombineFast(Hash, V); };
	Add(GetTypeHash(S.bEnabled)); Add(GetTypeHash(S.bShowSurface));
	Add(GetTypeHash(S.LightColor)); Add(GetTypeHash(S.EmissiveIntensity));
	Add(GetTypeHash(S.PolygonInsetCm)); Add(GetTypeHash(S.CeilingOffsetCm)); Add(GetTypeHash(S.SurfaceThicknessCm));
	Add(GetTypeHash(S.SurfaceMaterial.Get()));
	Add(GetTypeHash(S.LumensPerM2)); Add(GetTypeHash(S.IntensityScale)); Add(GetTypeHash(S.MaskResolution));
	Add(GetTypeHash(S.TemperatureK)); Add(GetTypeHash(S.bCastShadows)); Add(GetTypeHash(S.bRayTracedShadows));
	Add(GetTypeHash(S.IndirectIntensity)); Add(GetTypeHash(S.SpecularScale));
	return Hash;
}

// ─────────────────────────────────────────────────────────────────────────────
// Build
// ─────────────────────────────────────────────────────────────────────────────

void APlannerRoomLightActor::Build(const FRoomData& Room, const FPlannerRoomLightSettings& Settings)
{
	RoomID = Room.RoomID;
	CeilingHeightCm = Room.CeilingHeightCm > 0.f ? Room.CeilingHeightCm : 280.f;
	AreaM2 = FMath::Max(FMath::Abs(Room.AreaM2), 0.f);
	Polygon = Room.FloorPolygon;
	BuildSignature = ComputeBuildSignature(Room, Settings);

	DestroyLight();
	if (SurfaceMesh) SurfaceMesh->ClearAllMeshSections();
	SurfacePolygon.Reset();
	SurfaceTris.Reset();
	UsedInsetCm = 0.f;
	LightRectSizeCm = FVector2D::ZeroVector;
	MaskCoverage = EmittedLumens = PanelLuminanceNits = 0.f;
	ShadowVisibility = 1.f;
	if (Polygon.Num() < 3 || !Settings.bEnabled) return;

	// Panel polygon = emitting shape. A room too narrow for the configured inset uses a smaller one; a candidate
	// counts only if it triangulates completely (N − 2 triangles), so panel and mask always describe one shape.
	const float Insets[] = { Settings.PolygonInsetCm, Settings.PolygonInsetCm * 0.5f, Settings.PolygonInsetCm * 0.25f, 0.f };
	for (float Inset : Insets)
	{
		TArray<FVector2D> Candidate = InsetPolygon(Polygon, Inset);
		if (Candidate.Num() < 3) continue;
		TArray<int32> Tris;
		TriangulatePolygon(Candidate, Tris);
		if (Tris.Num() != 3 * (Candidate.Num() - 2)) continue;
		SurfacePolygon = MoveTemp(Candidate);
		SurfaceTris = MoveTemp(Tris);
		UsedInsetCm = Inset;
		break;
	}
	if (SurfacePolygon.Num() < 3)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoomLight] room %d: no valid panel polygon (self-intersecting room outline?); no light built."), RoomID);
		return;
	}

	FPlannerOrientedRect Rect;
	if (!ComputeOrientedRect(SurfacePolygon, Rect)) return;

	BuildLight(Settings, Rect);   // first: the panel's luminance is derived from the light's flux
	if (Settings.bShowSurface)
	{
		BuildSurface(Settings);
	}

	UE_LOG(LogTemp, Log, TEXT("[RoomLight] room %d: %d pts → panel %d pts (inset %.0f cm), light %.0f×%.0f cm yaw %.1f°, coverage %.3f, shadow visibility %.2f, %.0f lm emitted, panel %.0f cd/m², ceiling %.0f cm"),
		RoomID, Polygon.Num(), SurfacePolygon.Num(), UsedInsetCm, LightRectSizeCm.X, LightRectSizeCm.Y, Rect.YawDeg, MaskCoverage, ShadowVisibility, EmittedLumens, PanelLuminanceNits, CeilingHeightCm);
}

void APlannerRoomLightActor::BuildLight(const FPlannerRoomLightSettings& Settings, const FPlannerOrientedRect& Rect)
{
	int32 Res = FMath::Clamp((int32)FMath::RoundUpToPowerOfTwo((uint32)FMath::Max(Settings.MaskResolution, 2)), 64, 1024);
	TArray<uint8> Bytes;
	MaskCoverage = RasterizeLightMask(SurfacePolygon, Rect, Res, Bytes);
	// A concave room fills only part of its rectangle: keep the same texel detail on the polygon itself.
	if (MaskCoverage < 0.35f && Res < 1024)
	{
		Res *= 2;
		MaskCoverage = RasterizeLightMask(SurfacePolygon, Rect, Res, Bytes);
	}
	if (MaskCoverage <= 0.001f) return;
	MaskTexture = CreateLinearTexture(Res, Res, Bytes);
	if (!MaskTexture) return;

	const float W = Rect.Extent.Y; // SourceWidth: shorter side
	const float H = Rect.Extent.X; // SourceHeight
	LightRectSizeCm = FVector2D(W, H);

	// Flux: the same room budget the planner exposure was calibrated for (LumensPerM2 × room area). UE 5.6 rect
	// lights divide the colour by the full rectangle area and multiply the emission by the texture without
	// normalizing it, so the flux leaving the light is 2 × Intensity × MaskCoverage; dividing the intensity by
	// the coverage keeps triangular, trapezoidal and L-shaped rooms as bright as a rectangular room of equal area.
	const float Budget = Settings.LumensPerM2 * FMath::Max(AreaM2, 1.f) * FMath::Max(Settings.IntensityScale, 0.f);
	// Ray-traced shadow rays sample the WHOLE rectangle and never read the mask (RayTracingRectLight.ush). In a
	// non-rectangular room the walls block the rays aimed at the part of the rectangle hanging outside the room, so the
	// room receives only the visible share of the light. That share is estimated as the fraction of the rectangle
	// covered by the room outline and compensated, so every room shape gets the same illuminance (rectangles: 1).
	ShadowVisibility = 1.f;
	if (Settings.bCastShadows && Settings.bRayTracedShadows)
	{
		TArray<uint8> RoomMaskBytes;
		ShadowVisibility = FMath::Clamp(RasterizeLightMask(Polygon, Rect, 128, RoomMaskBytes), 0.2f, 1.f);
	}
	const float Intensity = Budget / (FMath::Max(MaskCoverage, 0.02f) * ShadowVisibility);
	EmittedLumens = 2.f * Intensity * MaskCoverage;

	// Influence bound: the farthest point of the room (corner at floor level) seen from the light centre, with a
	// margin so the attenuation window (1 − (d/R)^4)^2 stays near 0.9 at the farthest corner.
	const float LightZ = CeilingHeightCm - Settings.CeilingOffsetCm - 1.f; // 1 cm under the luminous face
	float MaxHorizontal = 0.f;
	for (const FVector2D& P : Polygon) MaxHorizontal = FMath::Max(MaxHorizontal, FVector2D::Distance(P, Rect.Center));
	const float MaxDistance = FMath::Sqrt(MaxHorizontal * MaxHorizontal + LightZ * LightZ);

	URectLightComponent* L = NewObject<URectLightComponent>(this, URectLightComponent::StaticClass(), NAME_None, RF_Transient);
	if (!L) return;
	L->SetMobility(EComponentMobility::Movable);
	L->SetupAttachment(SceneRoot);
	L->SetAbsolute(true, true, true);
	// Pitch −90° faces the light straight down; the yaw aligns its rectangle with the room (local Z = AxisX, local Y = AxisY).
	L->SetRelativeLocationAndRotation(FVector(Rect.Center.X, Rect.Center.Y, LightZ), FRotator(-90.f, Rect.YawDeg, 0.f));
	L->SetIntensityUnits(ELightUnits::Lumens);
	L->SetIntensity(Intensity);
	L->SetSourceWidth(W);
	L->SetSourceHeight(H);
	L->SourceTextureScale = FVector2f(1.f, 1.f);
	L->SourceTextureOffset = FVector2f(0.f, 0.f);
	L->SetSourceTexture(MaskTexture);
	L->SetBarnDoorAngle(88.f);   // engine maximum: no barn-door clipping, so the flux formula above holds
	L->SetBarnDoorLength(20.f);
	L->bUseTemperature = true;
	L->SetTemperature(Settings.TemperatureK);
	L->SetLightColor(FLinearColor::White);
	L->SetCastShadows(Settings.bCastShadows);
	// Ray-traced shadows sample the real light rectangle (not its mask: see ShadowVisibility above). Virtual shadow
	// maps would treat it as a disk of radius SourceWidth/2 around the rectangle centre, far too soft and invalid for
	// receivers closer than that radius. Without ray tracing the engine falls back to shadow maps.
	L->CastRaytracedShadow = Settings.bRayTracedShadows ? ECastRayTracedShadow::Enabled : ECastRayTracedShadow::UseProjectSetting;
	L->bCastVolumetricShadow = false;
	L->SetAffectTranslucentLighting(true);
	L->SetIndirectLightingIntensity(Settings.IndirectIntensity);
	L->SetSpecularScale(Settings.SpecularScale);
	L->SetAttenuationRadius(FMath::Clamp(MaxDistance * 2.f, 300.f, 5000.f));
	L->RegisterComponent();
	Light = L;
}

void APlannerRoomLightActor::BuildSurface(const FPlannerRoomLightSettings& Settings)
{
	if (!SurfaceMesh || SurfacePolygon.Num() < 3 || SurfaceTris.Num() < 3) return;

	const float Bottom = CeilingHeightCm - Settings.CeilingOffsetCm;                             // luminous face
	const float Top = FMath::Min(Bottom + Settings.SurfaceThicknessCm, CeilingHeightCm - 0.5f); // never inside the slab
	const int32 VCount = SurfacePolygon.Num();

	TArray<FVector> V; TArray<int32> T; TArray<FVector> Nrm; TArray<FVector2D> UV; TArray<FColor> Col;
	const FColor White(255, 255, 255, 255);

	// Luminous face, emitted in BOTH windings: the panel material is opaque and one-sided, and the room is only ever
	// seen from below, so whichever winding faces the camera is drawn and the other is back-face culled (no double
	// contribution, no dependence on the polygon's winding).
	for (int32 i = 0; i < VCount; ++i) { V.Add(FVector(SurfacePolygon[i].X, SurfacePolygon[i].Y, Bottom)); Nrm.Add(-FVector::UpVector); UV.Add(FVector2D(0.5f, 0.5f)); Col.Add(White); }
	for (int32 i = 0; i < SurfaceTris.Num(); i += 3)
	{
		T.Add(SurfaceTris[i]); T.Add(SurfaceTris[i + 1]); T.Add(SurfaceTris[i + 2]);
		T.Add(SurfaceTris[i]); T.Add(SurfaceTris[i + 2]); T.Add(SurfaceTris[i + 1]);
	}
	// Rim (thickness), both windings as well.
	for (int32 i = 0; i < VCount; ++i)
	{
		const FVector2D P1 = SurfacePolygon[i];
		const FVector2D P2 = SurfacePolygon[(i + 1) % VCount];
		const FVector2D Dir = (P2 - P1).GetSafeNormal();
		const FVector ON(Dir.Y, -Dir.X, 0.f);
		const int32 S = V.Num();
		V.Add(FVector(P1.X, P1.Y, Bottom)); V.Add(FVector(P2.X, P2.Y, Bottom)); V.Add(FVector(P2.X, P2.Y, Top)); V.Add(FVector(P1.X, P1.Y, Top));
		for (int k = 0; k < 4; ++k) { Nrm.Add(ON); UV.Add(FVector2D(0.5f, 0.5f)); Col.Add(White); }
		T.Add(S); T.Add(S + 1); T.Add(S + 2); T.Add(S); T.Add(S + 2); T.Add(S + 3);
		T.Add(S); T.Add(S + 2); T.Add(S + 1); T.Add(S); T.Add(S + 3); T.Add(S + 2);
	}
	SurfaceMesh->CreateMeshSection(0, V, T, Nrm, UV, Col, TArray<FProcMeshTangent>(), false);

	// Luminance of a Lambertian panel emitting the light's flux over the panel area: L = Φ / (π · A).
	// Emissive values are absolute cd/m² in UE 5.6 (white point at EV100 6.8 is 2^6.8 ≈ 111 cd/m²).
	const float PanelAreaM2 = FMath::Max(FMath::Abs(SignedArea(SurfacePolygon)) / 10000.f, 0.01f);
	// The flux the room actually receives (emission × ray-traced shadow visibility) spread over the panel.
	PanelLuminanceNits = EmittedLumens * ShadowVisibility / (PI * PanelAreaM2) * FMath::Max(Settings.EmissiveIntensity, 0.f);

	// Default: the engine's opaque unlit EmissiveTexturedMaterial (emissive = texture parameter "Texture"), fed with a
	// 1×1 half-float texture carrying the HDR colour. A project material may instead read the vector parameter
	// "Color" / "EmissiveColor" (same HDR value, alpha 1).
	UMaterialInterface* Base = Settings.SurfaceMaterial;
	if (!Base)
	{
		Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/EmissiveTexturedMaterial.EmissiveTexturedMaterial"));
	}
	if (!Base) return;

	FLinearColor Glow = Settings.LightColor * PanelLuminanceNits;
	Glow.A = 1.f;
	SurfaceMaterial = UMaterialInstanceDynamic::Create(Base, this);
	SurfaceMaterial->SetVectorParameterValue(TEXT("Color"), Glow);
	SurfaceMaterial->SetVectorParameterValue(TEXT("EmissiveColor"), Glow);
	EmissiveTexture = CreateHdrColorTexture(Glow);
	if (EmissiveTexture)
	{
		SurfaceMaterial->SetTextureParameterValue(TEXT("Texture"), EmissiveTexture);
	}
	SurfaceMesh->SetMaterial(0, SurfaceMaterial);
}

void APlannerRoomLightActor::DestroyLight()
{
	if (Light && Light->IsValidLowLevel())
	{
		Light->DestroyComponent(); // removes the scene proxy, which releases the mask's rect-light atlas slot
	}
	Light = nullptr;
	MaskTexture = nullptr;
	EmissiveTexture = nullptr;
}

void APlannerRoomLightActor::SetShown(bool bShown)
{
	if (SurfaceMesh && SurfaceMesh->IsVisible() != bShown) SurfaceMesh->SetVisibility(bShown);
	if (Light && Light->IsVisible() != bShown) Light->SetVisibility(bShown);
}
