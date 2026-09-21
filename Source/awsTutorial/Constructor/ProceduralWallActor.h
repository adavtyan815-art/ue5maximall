// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "RoomPlannerTypes.h"
#include "PlannerOpeningBuilder.h"
#include "PlannerFinishLayout.h"
#include "ProceduralWallActor.generated.h"

UCLASS()
class AWSTUTORIAL_API AProceduralWallActor : public AActor
{
	GENERATED_BODY()

public:
	AProceduralWallActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wall")
	TObjectPtr<USceneComponent> SceneRoot;

	/**
	 * The wall body with its door / window holes (collision): section 0 = left face, 1 = right face. Each carries its own finish
	 * (REQ-13); the top, sill tops, lintel soffits, reveals and end caps are split at the wall's mid-plane, each half belonging to the
	 * face on its side.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wall")
	TObjectPtr<UProceduralMeshComponent> WallProceduralMesh;

	static constexpr int32 LeftFaceSection = 0;
	static constexpr int32 RightFaceSection = 1;
	static constexpr int32 NumWallSections = 2;

	/**
	 * Door / window dressing fixed to the wall (thresholds, floor fills, frames); one section per material. In 3D only Visibility
	 * traces hit it, so trim can be picked for finishing (REQ-13); no collision in 2D.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wall")
	TObjectPtr<UProceduralMeshComponent> DressingMesh;

	/** 2D plan symbols (swing arcs). Shown only in the 2D view; never casts shadows and is invisible to ray tracing and GI. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wall")
	TObjectPtr<UProceduralMeshComponent> PlanSymbolMesh;

	/**
	 * One door leaf / window sash component per entry of WallData.Openings (empty for archways and for openings the wall
	 * skips). Each component's origin is the hinge axis, so opening a leaf is a pure rotation. Not built on a dedicated server.
	 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Wall")
	TArray<TObjectPtr<UProceduralMeshComponent>> LeafMeshes;

	/** Component tag carried by every leaf component (3D click / hover detection). */
	static const FName LeafComponentTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall")
	FWallSegment WallData;

	/** Rebuilds the procedural 3D wall geometry taking exact corner vertices and openings into account. */
	UFUNCTION(BlueprintCallable, Category = "Wall")
	void RebuildWallMesh(const FVector2D& StartPos, const FVector2D& EndPos,
	                     FVector2D InSL2D = FVector2D::ZeroVector,
	                     FVector2D InSR2D = FVector2D::ZeroVector,
	                     FVector2D InEL2D = FVector2D::ZeroVector,
	                     FVector2D InER2D = FVector2D::ZeroVector,
	                     bool bStartCap = true,
	                     bool bEndCap = true,
	                     bool bCreateCollision = true);

	UFUNCTION(BlueprintCallable, Category = "Wall")
	void SetWallMaterial(UMaterialInterface* NewMaterial);

	/**
	 * Sets the finishing material (paint / tile dynamic instance) of BOTH faces of this wall. Null clears it.
	 * A finish material replaces the default white wall material whenever the wall is not selected (REQ-13).
	 */
	UFUNCTION(BlueprintCallable, Category = "Wall")
	void SetFinishMaterial(UMaterialInterface* NewFinishMaterial);

	/** Finish of one face (REQ-13): the finish the material was built from and the material itself (null clears the face). */
	void SetFaceFinish(bool bLeftFace, const FSurfaceFinish& Finish, UMaterialInterface* Material);

	/** Tile-grid frame of both faces, so the pattern continues across openings and from wall to wall (REQ-13). Applied on the next RebuildWallMesh. */
	void SetFaceUVFrame(const FPlannerWallFaceUV& InFaceUV) { FaceUV = InFaceUV; }
	const FPlannerWallFaceUV& GetFaceUVFrame() const { return FaceUV; }

	UFUNCTION(BlueprintCallable, Category = "Wall")
	void SetSelectedHighlight(bool bSelected, int32 StencilValue = 2);

	/** Highlights one face (0 left, 1 right; INDEX_NONE = both), including its half of the top, reveals and caps. */
	void SetSelectedFaceHighlight(bool bSelected, int32 Face);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wall")
	TArray<TObjectPtr<UProceduralMeshComponent>> OpeningHighlightMeshes;

	/** The normal base material for the wall (defaults to standard white surface material). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall|Materials")
	TObjectPtr<UMaterialInterface> BaseWallMaterial;

	/** Finishing material currently applied to the wall's left face (dynamic instance created by the manager). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Wall|Materials")
	TObjectPtr<UMaterialInterface> FinishMaterial;

	/** The finish FinishMaterial was built from; lets the manager skip rebuilding the instance on every mesh rebuild. */
	UPROPERTY(Transient)
	FSurfaceFinish AppliedFinish;

	/** Finishing material currently applied to the wall's right face. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Wall|Materials")
	TObjectPtr<UMaterialInterface> FinishMaterialRight;

	/** The finish FinishMaterialRight was built from. */
	UPROPERTY(Transient)
	FSurfaceFinish AppliedFinishRight;

	/** Kept for compatibility; leaf materials are resolved by the owning ARoomPlannerManager (its LeafMaterial overrides every leaf). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall|Materials")
	TObjectPtr<UMaterialInterface> LeafMaterial;

	/** The material applied to the wall ONLY when selected (defaults to M_WallSelection). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall|Materials")
	TObjectPtr<UMaterialInterface> WallSelectionMaterial;

	/** The material applied to the opening highlight box ONLY when selected (defaults to M_OpeningSelection). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall|Materials")
	TObjectPtr<UMaterialInterface> OpeningSelectionMaterial;

	UFUNCTION(BlueprintCallable, Category = "Wall")
	void SetOpeningSelectedHighlight(int32 OpeningIndex, bool bSelected, int32 StencilValue = 2);

	UFUNCTION(BlueprintCallable, Category = "Wall")
	void ClearAllOpeningHighlights();

	/** Material of the left face when the wall is not selected: finish, else base, else engine white. */
	UMaterialInterface* ResolveNormalMaterial() const;

	/** Material of one wall body section when not selected: that face's finish, else base. */
	UMaterialInterface* ResolveSectionMaterial(int32 Section) const;

	/**
	 * True when the leaf hinge of this opening sits at the wall's START node.
	 * Hinge side is defined from inside the room looking at the wall (REQ-07), so it
	 * depends on which wall face is interior (WallData.bLeftSideIsInterior).
	 */
	static bool IsHingeAtStart(const FWallOpening& Opening, bool bLeftSideIsInterior);

	/**
	 * 2D view: plan symbols visible, leaves in their plan pose (door 90°, window 25°), leaves without collision.
	 * 3D view: plan symbols hidden, leaves posed by their open fraction (0 = closed), leaves hit by Visibility traces.
	 */
	void SetPresentation(bool bIn3D);
	bool IsPresentation3D() const { return bPresentation3D; }

	/** 3D pose of one leaf: 0 closed, 1 fully open (values slightly above 1 allow an easing overshoot). */
	void SetLeafOpenFraction(int32 OpeningIndex, float Fraction);

	/**
	 * Length (cm, from each wall end along the wall) of each face covered by other walls meeting at that end. The mitred
	 * corner points only describe corners; a T-junction branch is not mitred, so trim uses these limits to stay clear of it.
	 * Applied on the next RebuildWallMesh.
	 */
	void SetDressingFaceLimits(float StartLeft, float StartRight, float EndLeft, float EndRight);

	/** Index into WallData.Openings of the leaf owning Component, or INDEX_NONE. */
	int32 FindLeafIndex(const UPrimitiveComponent* Component) const;

	/** True when the opening has a built leaf (doors and windows the wall could build). */
	bool HasLeaf(int32 OpeningIndex) const;

private:
	/** Hinge-axis pose data of one leaf, filled by RebuildWallMesh. */
	struct FLeafPose
	{
		bool bValid = false;
		FVector Pivot = FVector::ZeroVector;
		float ClosedYawDeg = 0.f;
		/** +1: opening rotates the leaf with increasing yaw, -1: decreasing yaw. */
		float SwingSign = 1.f;
		float PlanAngleDeg = 90.f;
		float OpenAngle3DDeg = 90.f;
	};

	/** Centreline frame of the wall being rebuilt (world XY, cm). */
	struct FWallBuildFrame
	{
		FVector2D Start = FVector2D::ZeroVector;
		FVector2D Dir = FVector2D(1.f, 0.f);
		FVector2D Normal = FVector2D(0.f, 1.f);
		float HalfThickness = 10.f;
		float Length = 0.f;
		float Height = 280.f;
		/** Along-wall extent of each face ([0] left, [1] right), shorter than [0, Length] where a corner is mitred or a branch wall meets it. */
		float FaceLo[2] = { 0.f, 0.f };
		float FaceHi[2] = { 0.f, 0.f };
		/** Along-wall range where both faces run straight; openings are cut and dressed only inside it. */
		float JointLo = 0.f;
		float JointHi = 0.f;
	};

	float DressingStartCover[2] = { 0.f, 0.f };
	float DressingEndCover[2] = { 0.f, 0.f };

	FPlannerWallFaceUV FaceUV;

	/** Selection highlight state of the wall body (see SetSelectedFaceHighlight). */
	bool bHighlightShown = false;
	int32 HighlightFace = INDEX_NONE;

	/** Base wall material: BaseWallMaterial, else engine white. */
	UMaterialInterface* ResolveBaseMaterial() const;
	UMaterialInterface* ResolveSelectionMaterial() const;
	/** Applies highlight / finish / base materials to the wall body sections. */
	void ApplyWallSectionMaterials();

	TArray<FLeafPose> LeafPoses;
	TArray<float> LeafOpenFractions;
	bool bPresentation3D = true;

	/**
	 * Dressing (threshold / floor fill, lining, casing, window frame, sill board), leaf geometry, leaf pose and plan symbol of one
	 * opening. PrevOpeningEnd / NextOpeningStart are the neighbouring openings on this wall (±large when none); trim never
	 * extends past half the gap to them or past the wall faces' extents.
	 */
	void BuildOpeningVisuals(int32 OpeningIndex, const FWallBuildFrame& Frame, float PrevOpeningEnd, float NextOpeningStart,
	                         FPlannerSectionedMesh& Dressing, FPlannerMeshBuffers& Plan);

	/** Creates / destroys pooled leaf components so there is exactly one per opening. */
	void SyncLeafComponents(int32 Count);
	UProceduralMeshComponent* CreateLeafComponent();

	void ApplyLeafPose(int32 OpeningIndex);
	void ApplyAllLeafPoses();

	/** Writes every non-empty section of Mesh into Component (clearing it first) with materials from the manager. */
	void CommitSections(UProceduralMeshComponent* Component, const FPlannerSectionedMesh& Mesh, bool bCreateCollision);

	UMaterialInterface* ResolveOpeningMaterial(EPlannerOpeningMaterial Kind, const FLinearColor& Color) const;
	UMaterialInterface* ResolvePlanSymbolMaterial() const;
};
