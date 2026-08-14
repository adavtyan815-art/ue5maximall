// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoomPlannerTypes.h"
#include "ProceduralWallActor.h"
#include "ProceduralMeshComponent.h"
#include "RoomPlannerManager.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRoomPlannerUpdated, const FString&, JSONState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnInteractiveWallDragProgress, float, LengthMeters, FVector, MidpointWorld, float, AngleDeg, bool, bIsSnapped);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWallSelected, int32, SegmentID, float, LengthMeters);

UCLASS()
class MAXIMALL_API ARoomPlannerManager : public AActor
{
	GENERATED_BODY()

public:
	ARoomPlannerManager();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	UFUNCTION()
	void OnPixelStreamingInputReceived(const FString& Descriptor);

	UPROPERTY(BlueprintAssignable, Category = "RoomPlanner")
	FOnRoomPlannerUpdated OnRoomPlannerUpdated;

	UPROPERTY(BlueprintAssignable, Category = "RoomPlanner")
	FOnInteractiveWallDragProgress OnInteractiveWallDragProgress;

	UPROPERTY(BlueprintAssignable, Category = "RoomPlanner")
	FOnWallSelected OnWallSelected;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	int32 SelectedSegmentID = -1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RoomPlanner")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RoomPlanner")
	TObjectPtr<UProceduralMeshComponent> FloorProceduralMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RoomPlanner")
	TObjectPtr<UProceduralMeshComponent> CeilingProceduralMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RoomPlanner")
	TObjectPtr<UProceduralMeshComponent> BaseboardProceduralMesh;

	UPROPERTY(Transient)
	TObjectPtr<AProceduralWallActor> FloorActor;

	UPROPERTY(Transient)
	TObjectPtr<AProceduralWallActor> CeilingActor;

	UPROPERTY(Transient)
	TObjectPtr<AProceduralWallActor> BaseboardActor;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	bool bCeilingVisible = false;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void ToggleCeilingVisibility();

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void SetCeilingVisibility(bool bVisible);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	EPlannerToolMode ActiveToolMode = EPlannerToolMode::DrawWall;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	EPlannerViewMode ActiveViewMode = EPlannerViewMode::View3D_Perspective;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	int32 AddNode(const FVector2D& Position);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	int32 AddWall(int32 StartNodeID, int32 EndNodeID, float Thickness = 20.f, float Height = 280.f);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool AddOpeningToWall(int32 SegmentID, EOpeningType Type, float DistFromStart, float Width = 90.f, float Height = 210.f, float SillHeight = 0.f);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void RemoveWall(int32 SegmentID);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void ClearLayout();

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void RebuildAllWalls();

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void RebuildRooms();

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	FString ExportLayoutToJSON() const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool ImportLayoutFromJSON(const FString& JSONString);

	/** Parses incoming JSON command payloads from the WebRTC DataChannel (maximall-pixel-config). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	FString ProcessCommandJSON(const FString& JSONString);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	float CalculateFloorAreaM2() const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	float CalculatePerimeterM() const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void SetViewMode(bool bIn2DMode);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RoomPlanner")
	bool Is2DModeActive() const { return b2DViewMode; }

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void StartInteractiveWallDraw(const FVector& WorldPos);

	void UpdateActiveWallLength();
	void EndWallDrawing();

	void UpdateSelectionVisuals();
	bool bWasDraggingOpening = false;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void UpdateInteractiveWallDraw(const FVector& WorldPos);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void CommitInteractiveWallDraw();

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void CancelInteractiveWallDraw();

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void CheckHoverSnapHint(const FVector& WorldPos);

	bool FindWallSnapPoint2D(const FVector2D& Point2D, float SnapRadiusCm, FVector2D& OutSnapPos, int32& OutSnappedSegmentID, bool& bOutIsEndpoint) const;
	bool FindWallEndpointSnap2D(const FVector2D& Point2D, float SnapRadiusCm, FVector2D& OutSnapPos) const;
	int32 GetOrCreateNodeAtPosition(const FVector2D& Position, float NodeSnapRadiusCm = 25.f, float WallSnapRadiusCm = 20.f);
	int32 SplitWallSegment(int32 SegmentID, const FVector2D& SplitPos);

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FVector CurrentHoverSnapWorldPos;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	bool bHasActiveHoverSnap;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RoomPlanner")
	bool IsWallDrawingActive() const { return bIsDrawingWall; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RoomPlanner")
	FVector GetDragStartPoint() const { return DragStartPoint; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RoomPlanner")
	FVector GetDragCurrentPoint() const { return DragCurrentPoint; }

	UPROPERTY()
	FRotator SavedControlRotation;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void SetToolMode(EPlannerToolMode NewToolMode);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void ClearWallSelection();

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	int32 SelectWallAtWorldPos(const FVector& WorldPos);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RoomPlanner")
	int32 GetWallCount() const { return WallSegments.Num(); }

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	float GetWallLength(int32 SegmentID) const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool SetWallLength(int32 SegmentID, float NewLengthMeters);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool DeleteWallAtWorldPos(const FVector& WorldPos);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool DeleteSelectedWall();

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool DeleteSelectedOpening();

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool DeleteOpening(int32 SegmentID, int32 OpeningIndex);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool AddDoorToSelectedWall(float WidthMeters = 0.9f, float HeightMeters = 2.1f);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool AddWindowToSelectedWall(float WidthMeters = 1.2f, float HeightMeters = 1.2f, float SillHeightMeters = 0.9f);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool AddDoorToWall(int32 SegmentID, float WidthMeters = 0.9f, float HeightMeters = 2.1f, float DistFromStartCm = -1.f);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool AddWindowToWall(int32 SegmentID, float WidthMeters = 1.2f, float HeightMeters = 1.2f, float SillHeightMeters = 0.9f, float DistFromStartCm = -1.f);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void BuildPreset4x4mRoom();

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	int32 SelectedOpeningIndex = -1;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool UpdateOpeningDimensions(int32 SegmentID, int32 OpeningIndex, float WidthMeters, float HeightMeters, float SillHeightMeters);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool GetOpeningDetails(int32 SegmentID, int32 OpeningIndex, float& OutWidthMeters, float& OutHeightMeters, float& OutSillHeightMeters) const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool GetOpeningDistance(int32 SegmentID, int32 OpeningIndex, float& OutDistFromStartCm) const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool UpdateOpeningPosition(int32 SegmentID, int32 OpeningIndex, float NewDistFromStartCm);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool DragSelectedOpeningToWorldPos(const FVector& WorldPos);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	static ARoomPlannerManager* GetOrCreateInstance(UWorld* World);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedRoomJSON)
	FString ReplicatedRoomJSON;

	UFUNCTION()
	void OnRep_ReplicatedRoomJSON();

private:
	int32 NextNodeID = 1;
	int32 NextSegmentID = 1;

	TMap<int32, FWallNode> Nodes;
	TMap<int32, FWallSegment> WallSegments;
	TMap<int32, TObjectPtr<AProceduralWallActor>> WallActors;
	TMap<int32, FRoomData> Rooms;
	TWeakObjectPtr<UObject> BoundPSInput;

	bool b2DViewMode = false;
	bool bIsDrawingWall = false;
	FVector DragStartPoint = FVector::ZeroVector;
	FVector DragCurrentPoint = FVector::ZeroVector;

	FVector Cached3DCameraLocation = FVector::ZeroVector;
	FRotator Cached3DCameraRotation = FRotator::ZeroRotator;

	UPROPERTY()
	TObjectPtr<AProceduralWallActor> PreviewWallActor;

	UPROPERTY()
	TObjectPtr<ACameraActor> TopDownCameraActor;

	void ComputeMiterOffsetsAtNode(int32 NodeID, TMap<int32, FVector2D>& OutStartLeftOffsets,
	                               TMap<int32, FVector2D>& OutStartRightOffsets,
	                               TMap<int32, FVector2D>& OutEndLeftOffsets,
	                               TMap<int32, FVector2D>& OutEndRightOffsets);
};
