// Copyright 2026 MaxiMall. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Constructor/PlannerOpeningBuilder.h"
#include "Constructor/PlannerOpeningStyles.h"

namespace
{
	int32 CountMisorientedSections(const FPlannerSectionedMesh& Mesh)
	{
		int32 Bad = 0;
		for (const FPlannerSectionedMesh::FSection& Section : Mesh.Sections)
		{
			Bad += PlannerMeshBuilder::CountMisorientedTriangles(Section.Buffers);
		}
		return Bad;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerOpeningBuilderFacingTest, "MaxiMall.Planner.Openings.BuilderFacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerOpeningBuilderFacingTest::RunTest(const FString& Parameters)
{
	// Quads: the rendered front face follows the requested normal whatever the vertex order.
	{
		FPlannerMeshBuffers B;
		const FVector V0(0., 0., 0.), V1(100., 0., 0.), V2(100., 0., 100.), V3(0., 0., 100.);
		PlannerMeshBuilder::AddQuad(B, V0, V1, V2, V3, FVector(0., 1., 0.));
		PlannerMeshBuilder::AddQuad(B, V0, V1, V2, V3, FVector(0., -1., 0.));
		PlannerMeshBuilder::AddQuad(B, V3, V2, V1, V0, FVector(0., 1., 0.));
		TestEqual(TEXT("Quads face their normals"), PlannerMeshBuilder::CountMisorientedTriangles(B), 0);
		TestEqual(TEXT("Every vertex has a tangent"), B.Tangents.Num(), B.Vertices.Num());
		TestEqual(TEXT("Every vertex has a UV"), B.UVs.Num(), B.Vertices.Num());
	}

	// Boxes in rotated and mirrored frames face outward.
	{
		FPlannerMeshBuffers B;
		const FVector Dir = FVector(1., 1., 0.).GetSafeNormal();
		const FVector Left(-Dir.Y, Dir.X, 0.);
		PlannerMeshBuilder::AddBox(B, FVector(10., 20., 0.), Dir, Left, FVector::UpVector, FVector(0., -10., 0.), FVector(90., 10., 210.));
		PlannerMeshBuilder::AddBox(B, FVector(10., 20., 0.), Dir, -Left, FVector::UpVector, FVector(90., 10., 210.), FVector(0., -10., 0.));
		TestEqual(TEXT("Boxes face their normals"), PlannerMeshBuilder::CountMisorientedTriangles(B), 0);

		FPlannerMeshBuffers Unit;
		PlannerMeshBuilder::AddLocalBox(Unit, FVector(-1.), FVector(1.));
		bool bOutward = Unit.Vertices.Num() == 24;
		for (int32 i = 0; i + 3 < Unit.Vertices.Num(); i += 4)
		{
			const FVector Center = (Unit.Vertices[i] + Unit.Vertices[i + 1] + Unit.Vertices[i + 2] + Unit.Vertices[i + 3]) * 0.25;
			bOutward &= FVector::DotProduct(Center, Unit.Normals[i]) > 0.;
		}
		TestTrue(TEXT("Box normals point away from the box centre"), bOutward);
	}

	// Plan arcs (both swing sides) and the exterior ground face up; the sky cylinder faces its axis.
	{
		FPlannerMeshBuffers B;
		PlannerMeshBuilder::AddArcStrip(B, FVector2D(0., 0.), FVector2D(1., 0.), FVector2D(0., 1.), UE_HALF_PI, 78.f, 80.f, 1.3f, 24);
		PlannerMeshBuilder::AddArcStrip(B, FVector2D(0., 0.), FVector2D(1., 0.), FVector2D(0., -1.), UE_HALF_PI, 78.f, 80.f, 1.3f, 24);
		PlannerMeshBuilder::AddDisc(B, FVector2D(5., 5.), 1000.f, 0.4f, 32);
		TestEqual(TEXT("Arcs and disc face their normals"), PlannerMeshBuilder::CountMisorientedTriangles(B), 0);
		bool bUp = true;
		for (const FVector& N : B.Normals) bUp &= N.Z > 0.99;
		TestTrue(TEXT("Arcs and disc point up"), bUp);

		FPlannerMeshBuffers Sky;
		PlannerMeshBuilder::AddInwardCylinder(Sky, FVector2D(0., 0.), 1000.f, -50.f, 1100.f, 16, true);
		TestEqual(TEXT("Sky cylinder faces its normals"), PlannerMeshBuilder::CountMisorientedTriangles(Sky), 0);
		bool bInward = true;
		for (int32 i = 0; i < Sky.Vertices.Num(); ++i)
		{
			if (FMath::Abs(Sky.Normals[i].Z) < 0.5)
			{
				bInward &= FVector::DotProduct(FVector(Sky.Vertices[i].X, Sky.Vertices[i].Y, 0.), Sky.Normals[i]) < 0.;
			}
		}
		TestTrue(TEXT("Sky walls face the axis"), bInward);
	}

	// Casings on both faces of a wall.
	{
		FPlannerMeshBuffers B;
		const FVector Along(0., 1., 0.);
		const FVector Out(-1., 0., 0.);
		PlannerOpeningGeometry::AddMitredCasing(B, FVector(-10., 0., 0.), Along, Out, 101.5f, 188.5f, 1.f, 208.5f, 7.f, 1.6f);
		PlannerOpeningGeometry::AddMitredCasing(B, FVector(10., 0., 0.), Along, -Out, 101.5f, 188.5f, 1.f, 208.5f, 7.f, 1.6f);
		TestTrue(TEXT("Casing has geometry"), B.Triangles.Num() > 0);
		TestEqual(TEXT("Casings face their normals"), PlannerMeshBuilder::CountMisorientedTriangles(B), 0);
	}

	// Every door / window style, both hinge orientations, normal and tiny sizes.
	for (const FPlannerOpeningStyle& Style : PlannerOpeningStyles::All())
	{
		if (Style.Type == EOpeningType::Archway) continue;
		for (float BodySign : { 1.f, -1.f })
		{
			for (const FVector2D& Size : { FVector2D(78., 204.), FVector2D(118., 118.), FVector2D(30., 60.), FVector2D(6., 6.) })
			{
				FPlannerLeafParams P;
				P.Type = Style.Type;
				P.Design = Style.LeafDesign;
				P.GlassKind = Style.GlassKind;
				P.Width = Size.X;
				P.Height = Size.Y;
				P.Thickness = (Style.Type == EOpeningType::Door) ? 4.f : 6.f;
				P.BodySign = BodySign;
				P.HandleZ = 97.f;
				FPlannerSectionedMesh Mesh;
				PlannerOpeningGeometry::BuildLeaf(P, Mesh);
				const FString What = FString::Printf(TEXT("%s leaf %.0fx%.0f (body %+.0f)"), *Style.ID.ToString(), Size.X, Size.Y, BodySign);
				TestFalse(*(What + TEXT(" has geometry")), Mesh.IsEmpty());
				TestEqual(*(What + TEXT(" faces its normals")), CountMisorientedSections(Mesh), 0);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerOpeningStyleResolveTest, "MaxiMall.Planner.Openings.StyleResolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerOpeningStyleResolveTest::RunTest(const FString& Parameters)
{
	for (EOpeningType Type : { EOpeningType::Door, EOpeningType::Window, EOpeningType::Archway })
	{
		const FPlannerOpeningStyle& Default = PlannerOpeningStyles::GetDefault(Type);
		TestTrue(TEXT("Default style belongs to its type"), Default.Type == Type);
		TestTrue(TEXT("No style resolves to the default"), PlannerOpeningStyles::Resolve(Type, NAME_None).ID == Default.ID);
		TestTrue(TEXT("Unknown style resolves to the default"), PlannerOpeningStyles::Resolve(Type, FName(TEXT("NoSuchStyle"))).ID == Default.ID);
	}
	TestFalse(TEXT("A window style is not valid for a door"), PlannerOpeningStyles::IsValidFor(EOpeningType::Door, FName(TEXT("Window_White"))));
	TestTrue(TEXT("A door style never applies to a window"),
		PlannerOpeningStyles::Resolve(EOpeningType::Window, FName(TEXT("Door_OakPanel"))).Type == EOpeningType::Window);

	TSet<FName> Seen;
	for (const FPlannerOpeningStyle& Style : PlannerOpeningStyles::All())
	{
		TestFalse(*FString::Printf(TEXT("Style ID %s is unique"), *Style.ID.ToString()), Seen.Contains(Style.ID));
		Seen.Add(Style.ID);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
