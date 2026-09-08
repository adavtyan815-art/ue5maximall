// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoomPlannerTypes.h"
#include "ProceduralWallActor.h"
#include "ProceduralMeshComponent.h"
#include "RoomPlannerManager.generated.h"

class UDataTable;
class UMaterialInstanceDynamic;
class AShowroomBooth;
class APlannerPlacedObjectActor;
class FJsonObject;
struct FHitResult;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRoomPlannerUpdated, const FString&, JSONState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnInteractiveWallDragProgress, float, LengthMeters, FVector, MidpointWorld, float, AngleDeg, bool, bIsSnapped);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWallSelected, int32, SegmentID, float, LengthMeters);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFloorSelected, int32, RoomID, float, AreaM2);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPlannerObjectSelected, const FString&, InstanceID, EPlannerSelectionKind, Kind);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlannerSelectionChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlannerOperationRejected, const FString&, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNodeDragProgress, int32, NodeID, FVector, WorldPos);

UCLASS()
class AWSTUTORIAL_API ARoomPlannerManager : public AActor
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

	/** Fired when a floor (room) is selected (RoomID = -1 on deselect). */
	UPROPERTY(BlueprintAssignable, Category = "RoomPlanner")
	FOnFloorSelected OnFloorSelected;

	/** Fired when a placed object or cabinet set is selected (empty id on deselect). */
	UPROPERTY(BlueprintAssignable, Category = "RoomPlanner")
	FOnPlannerObjectSelected OnPlannerObjectSelected;

	/** Fired after ANY selection change (wall, opening, floor, object, cabinet set, or clear). */
	UPROPERTY(BlueprintAssignable, Category = "RoomPlanner")
	FOnPlannerSelectionChanged OnSelectionChanged;

	/** Fired when an operation was refused (e.g. shortening a wall below its openings, REQ-09). Bind to show the reason in the UI. */
	UPROPERTY(BlueprintAssignable, Category = "RoomPlanner")
	FOnPlannerOperationRejected OnOperationRejected;

	/** Fired every frame while a wall control point is being dragged (REQ-02). */
	UPROPERTY(BlueprintAssignable, Category = "RoomPlanner")
	FOnNodeDragProgress OnNodeDragProgress;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	int32 SelectedSegmentID = -1;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	int32 SelectedOpeningIndex = -1;

	/** Selected floor (RoomID) or -1. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	int32 SelectedRoomID = -1;

	/** Selected placed interior object InstanceID, or empty. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FString SelectedObjectID;

	/** Selected cabinet set InstanceID, or empty. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FString SelectedCabinetSetID;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RoomPlanner")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RoomPlanner")
	TObjectPtr<UProceduralMeshComponent> FloorProceduralMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RoomPlanner")
	TObjectPtr<UProceduralMeshComponent> CeilingProceduralMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RoomPlanner")
	TObjectPtr<UProceduralMeshComponent> BaseboardProceduralMesh;

	/** Small discs drawn on every wall corner in 2D Select mode — the draggable control points (REQ-02). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RoomPlanner")
	TObjectPtr<UProceduralMeshComponent> NodeHandleMesh;

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

	/** Default base material applied to walls when unselected (clean white / surface material). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Materials")
	TObjectPtr<UMaterialInterface> DefaultWallMaterial;

	/** Material applied to wall ONLY when selected (M_WallSelection). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Materials")
	TObjectPtr<UMaterialInterface> WallSelectionMaterial;

	/** Material applied to opening highlight box ONLY when selected (M_OpeningSelection). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Materials")
	TObjectPtr<UMaterialInterface> OpeningSelectionMaterial;

	/**
	 * Parameterised paint material (vector parameter "BaseColor"). Used for RAL/NCS wall & floor paint
	 * and for the default floor / ceiling / baseboard colours. Falls back to
	 * /Game/RoomPlanner/Materials/M_PlannerPaint, then /Game/ColorCatalog/Materials/M_Change_Color.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Materials")
	TObjectPtr<UMaterialInterface> PaintMaterial;

	/**
	 * Parameterised tile material (vector "BaseColor", scalar "TilesPerMeter"). Used when a tile
	 * catalog row has no material of its own. Falls back to /Game/RoomPlanner/Materials/M_PlannerTile, then PaintMaterial.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Materials")
	TObjectPtr<UMaterialInterface> TileMaterial;

	/** Material for door / window leaves. Falls back to DefaultWallMaterial. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Materials")
	TObjectPtr<UMaterialInterface> LeafMaterial;

	/** Tile catalog (rows: FPlannerTileRow). Falls back to /Game/DT/DT_PlannerTiles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Catalogs")
	TObjectPtr<UDataTable> TileCatalog;

	/** Interior object catalog (rows: FPlannerObjectRow). Falls back to /Game/DT/DT_PlannerObjects. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Catalogs")
	TObjectPtr<UDataTable> ObjectCatalog;

	/** Cabinet set catalog (rows: FFurnitureProductRow). Falls back to /Game/DT/DT_FurnitureCatalog. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Catalogs")
	TObjectPtr<UDataTable> CabinetSetCatalog;

	/** Booth class spawned for cabinet sets. Falls back to /Game/BP_Booth, then AShowroomBooth. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Catalogs")
	TSubclassOf<AShowroomBooth> CabinetSetActorClass;

	/** Actor class used for placed interior objects. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Catalogs")
	TSubclassOf<APlannerPlacedObjectActor> PlacedObjectActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	EPlannerToolMode ActiveToolMode = EPlannerToolMode::DrawWall;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	EPlannerViewMode ActiveViewMode = EPlannerViewMode::View3D_Perspective;

	/** True while a URoomPlannerWidget is open on this machine (enables 3D surface picking). */
	UPROPERTY(BlueprintReadWrite, Category = "RoomPlanner")
	bool bPlannerUIOpen = false;

	// ── Pending click-to-place (REQ-17 / REQ-18) ────────────────────────────

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner|Placement")
	EPlannerPlacementKind PendingPlacementKind = EPlannerPlacementKind::None;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner|Placement")
	FString PendingPlacementAssetID;

	/** Arms click-to-place for an interior object (catalog row name or mesh asset path) and switches to the PlaceFurniture tool. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Placement")
	void BeginPlaceObject(const FString& AssetID);

	/** Arms click-to-place for a cabinet set (DT_FurnitureCatalog row name) and switches to the PlaceFurniture tool. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Placement")
	void BeginPlaceCabinetSet(FName ProductID);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Placement")
	void CancelPendingPlacement();

	UFUNCTION(BlueprintPure, Category = "RoomPlanner|Placement")
	bool HasPendingPlacement() const { return PendingPlacementKind != EPlannerPlacementKind::None; }

	// ── Existing wall / opening API ─────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	int32 AddNode(const FVector2D& Position);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	int32 AddWall(int32 StartNodeID, int32 EndNodeID, float Thickness = 20.f, float Height = 280.f);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool AddOpeningToWall(int32 SegmentID, EOpeningType Type, float DistFromStart, float Width = 90.f, float Height = 210.f, float SillHeight = 0.f);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void RemoveWall(int32 SegmentID);

	/** Clears EVERYTHING: walls, openings, finishes, placed objects and (on the server) planner-spawned cabinet sets. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void ClearLayout();

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void RebuildAllWalls();

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void RebuildRooms();

	/** Full planner state (walls, openings, finishes, objects, cabinet sets) as JSON. Also the replication payload. */
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

	/** Moves the wall's END node along the wall direction (connected walls follow — intended shared-node model). Blocked if an opening would no longer fit (REQ-09). */
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

	/** Free numeric sizing (REQ-08). Values are validated against the wall (REQ-09): the opening must fit, must not overlap a neighbour, and sill+height must not exceed the wall height. */
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

	// ── REQ-07: opening swing ───────────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Openings")
	bool SetOpeningSwing(int32 SegmentID, int32 OpeningIndex, EOpeningSwingSide Side, EOpeningSwingDirection Direction);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Openings")
	bool GetOpeningSwing(int32 SegmentID, int32 OpeningIndex, EOpeningSwingSide& OutSide, EOpeningSwingDirection& OutDirection) const;

	// ── REQ-06: exact distances of an opening ───────────────────────────────

	/**
	 * Distances in cm from the opening edges to the left / right wall corner (seen from inside the room),
	 * from the opening bottom to the floor, and edge-to-edge to the nearest neighbouring opening on the same wall.
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Openings")
	bool GetOpeningDistances(int32 SegmentID, int32 OpeningIndex, float& OutLeftCornerCm, float& OutRightCornerCm, float& OutFloorCm, float& OutNeighborCm, bool& bOutHasNeighbor) const;

	// ── REQ-02 / REQ-04 / REQ-06: dimension labels ──────────────────────────

	/** Labels (with world positions) describing the current selection or the control point being dragged. Empty when nothing is selected. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Labels")
	TArray<FPlannerDimensionLabel> GetSelectionDimensionLabels() const;

	UFUNCTION(BlueprintPure, Category = "RoomPlanner|Selection")
	EPlannerSelectionKind GetSelectionKind() const;

	/** Clears every kind of selection (wall, opening, floor, object, cabinet set). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Selection")
	void ClearAllSelection();

	/** 2D pick: object → cabinet set → wall/opening → floor. Returns what was selected. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Selection")
	EPlannerSelectionKind SelectAtWorldPos2D(const FVector& WorldPos);

	/** 3D pick from a cursor line trace (walls, openings, floor, objects, cabinet sets). Works in any tool mode. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Selection")
	EPlannerSelectionKind SelectSurfaceFromHit(const FHitResult& Hit);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Selection")
	int32 SelectFloorAtWorldPos(const FVector& WorldPos);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Selection")
	bool SelectPlacedObject(const FString& InstanceID);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Selection")
	bool SelectCabinetSet(const FString& InstanceID);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Selection")
	int32 FindRoomAtWorldPos(const FVector& WorldPos) const;

	// ── REQ-02: wall control points ─────────────────────────────────────────

	/** Node under the cursor within RadiusCm, or -1. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Nodes")
	int32 FindNodeAtWorldPos(const FVector& WorldPos, float RadiusCm = 25.f) const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Nodes")
	bool GetNodePosition(int32 NodeID, FVector2D& OutPosition) const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Nodes")
	TArray<FVector> GetNodeHandleWorldPositions() const;

	/** Begins a local (predicted) control-point drag. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Nodes")
	bool StartNodeDrag(int32 NodeID);

	/** Updates the local preview; refused positions (REQ-09) keep the last valid one. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Nodes")
	void UpdateNodeDrag(const FVector& WorldPos);

	/** Ends the local drag and returns the final node position to commit through the server RPC. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Nodes")
	bool EndNodeDrag(int32& OutNodeID, FVector2D& OutFinalPosition);

	UFUNCTION(BlueprintPure, Category = "RoomPlanner|Nodes")
	bool IsNodeDragActive() const { return DraggingNodeID != -1; }

	UFUNCTION(BlueprintPure, Category = "RoomPlanner|Nodes")
	int32 GetDraggingNodeID() const { return DraggingNodeID; }

	/** Validates a node move against every connected wall's openings (REQ-09). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Nodes")
	bool CanMoveNode(int32 NodeID, const FVector2D& NewPosition, FString& OutReason) const;

	/** Moves a node; openings on connected walls stay attached (distance from the unmoved corner is preserved). Blocked with a reason if an opening no longer fits. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Nodes")
	bool MoveNode(int32 NodeID, const FVector2D& NewPosition);

	// ── REQ-13: finishing ───────────────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	bool SetWallFinish(int32 SegmentID, const FSurfaceFinish& Finish);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	bool SetFloorFinish(int32 RoomID, const FSurfaceFinish& Finish);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	bool GetWallFinish(int32 SegmentID, FSurfaceFinish& OutFinish) const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	bool GetFloorFinish(int32 RoomID, FSurfaceFinish& OutFinish) const;

	/** Finish of whatever is selected (wall, floor or object). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	bool GetSelectedSurfaceFinish(FSurfaceFinish& OutFinish) const;

	/** Builds a paint finish from a catalog colour. */
	UFUNCTION(BlueprintPure, Category = "RoomPlanner|Finish")
	static FSurfaceFinish MakePaintFinish(const FString& ColorCode, FLinearColor Color);

	/** Builds a tile finish from a tile catalog row (returns false if the row does not exist). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	bool MakeTileFinish(FName TileID, FSurfaceFinish& OutFinish) const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	TArray<FPlannerCatalogEntry> GetAvailableTiles() const;

	/** True when the current selection can receive a finish (wall without opening selected, floor, or object). */
	UFUNCTION(BlueprintPure, Category = "RoomPlanner|Finish")
	bool CanApplyFinishToSelection() const;

	// ── REQ-14: finishing areas ─────────────────────────────────────────────

	/** Net finishing area per finish (openings subtracted from walls). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	TArray<FFinishAreaEntry> CalculateFinishAreas() const;

	/** Total finished area of one type (paint or tile), m². */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	float GetTotalFinishAreaM2(ESurfaceFinishType Type) const;

	/** Net area of a wall face in m² (length × height − openings). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	float GetWallNetAreaM2(int32 SegmentID) const;

	/** Multi-line summary, e.g. "Краска: 12.40 м²\n  RAL 3020: 8.10 м²\n..." */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	FString GetFinishAreaSummaryText() const;

	// ── REQ-17: interior objects ────────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	TArray<FPlannerCatalogEntry> GetAvailableObjects() const;

	/** Server: adds an object. Returns the new InstanceID (empty on failure). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	FString AddPlacedObject(const FString& AssetID, const FVector& Location, const FRotator& Rotation, const FVector& Scale);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	bool MovePlacedObject(const FString& InstanceID, const FVector& Location, const FRotator& Rotation, const FVector& Scale);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	bool RemovePlacedObject(const FString& InstanceID);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	bool SetPlacedObjectFinish(const FString& InstanceID, const FSurfaceFinish& Finish);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	TArray<FPlacedFurnitureData> GetPlacedObjects() const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	bool GetPlacedObject(const FString& InstanceID, FPlacedFurnitureData& OutData) const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	APlannerPlacedObjectActor* FindPlacedObjectActor(const FString& InstanceID) const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	FString FindPlacedObjectAtWorldPos(const FVector& WorldPos) const;

	/** Local (predicted) move used while dragging; commit with the server RPC on release. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	void MovePlacedObjectLocal(const FString& InstanceID, const FVector& Location, const FRotator& Rotation);

	// ── REQ-18: cabinet sets ────────────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|CabinetSets")
	TArray<FPlannerCatalogEntry> GetAvailableCabinetSets() const;

	/** Server: spawns a booth for the product and registers it. Returns the InstanceID (empty on failure). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|CabinetSets")
	FString AddCabinetSet(FName ProductID, const FVector& Location, const FRotator& Rotation);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|CabinetSets")
	bool MoveCabinetSet(const FString& InstanceID, const FVector& Location, const FRotator& Rotation);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|CabinetSets")
	bool RemoveCabinetSet(const FString& InstanceID);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|CabinetSets")
	TArray<FPlacedCabinetSetData> GetCabinetSets() const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|CabinetSets")
	bool GetCabinetSet(const FString& InstanceID, FPlacedCabinetSetData& OutData) const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|CabinetSets")
	AShowroomBooth* FindCabinetSetActor(const FString& InstanceID) const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|CabinetSets")
	FString FindCabinetSetAtWorldPos(const FVector& WorldPos) const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|CabinetSets")
	void MoveCabinetSetLocal(const FString& InstanceID, const FVector& Location, const FRotator& Rotation);

	// ── REQ-16: project save / load ─────────────────────────────────────────

	/**
	 * Server: restores a whole saved project record ({"planner": {...}, "boothStates": [...]}):
	 * imports the planner layout (spawning cabinet sets), then applies saved booth states
	 * by plannerInstanceId (planner-spawned booths) or boothName (level booths).
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Project")
	bool ImportProjectFromSaveJSON(const FString& SaveRecordJSON);

	/** True when the wall's left face (Normal = (-Dir.Y, Dir.X)) faces a room interior. */
	UFUNCTION(BlueprintPure, Category = "RoomPlanner")
	bool IsWallLeftSideInterior(int32 SegmentID) const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool GetWallSegmentData(int32 SegmentID, FWallSegment& OutSegment) const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool GetRoomData(int32 RoomID, FRoomData& OutRoom) const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	static ARoomPlannerManager* GetOrCreateInstance(UWorld* World);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedRoomJSON)
	FString ReplicatedRoomJSON;

	UFUNCTION()
	void OnRep_ReplicatedRoomJSON();

	/** Rebuilds the control-point discs (visible only in 2D Select mode). */
	void RefreshNodeHandles();

private:
	int32 NextNodeID = 1;
	int32 NextSegmentID = 1;

	TMap<int32, FWallNode> Nodes;
	TMap<int32, FWallSegment> WallSegments;
	TMap<int32, TObjectPtr<AProceduralWallActor>> WallActors;
	TMap<int32, FRoomData> Rooms;
	TWeakObjectPtr<UObject> BoundPSInput;

	/** Floor finishes keyed by room centroid so they survive room re-detection. */
	TArray<FFloorFinishRecord> FloorFinishes;

	/** Placed interior objects (REQ-17). */
	TMap<FString, FPlacedFurnitureData> PlacedObjects;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<APlannerPlacedObjectActor>> PlacedObjectActors;

	/** Placed cabinet sets (REQ-18). */
	TMap<FString, FPlacedCabinetSetData> CabinetSets;

	/** Cached booth actors per InstanceID (looked up by PlannerInstanceID on clients). */
	mutable TMap<FString, TWeakObjectPtr<AShowroomBooth>> CabinetSetActorCache;

	/** Per-room floor finish material instances (section index = RoomID - 1). */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UMaterialInterface>> FloorSectionMaterials;

	bool b2DViewMode = false;
	bool bIsDrawingWall = false;
	FVector DragStartPoint = FVector::ZeroVector;
	FVector DragCurrentPoint = FVector::ZeroVector;

	FVector Cached3DCameraLocation = FVector::ZeroVector;
	FRotator Cached3DCameraRotation = FRotator::ZeroRotator;

	int32 DraggingNodeID = -1;
	FVector2D NodeDragOriginalPos = FVector2D::ZeroVector;
	float LastRejectBroadcastTime = -100.f;

	/** Previous-frame LMB state (Slate pressed-button set) for the manager-driven corner drag. */
	bool bPrevLMBDownForNodeDrag = false;

	/**
	 * Manager-driven control-point drag (REQ-02), independent of which click path is live:
	 * detects LMB press on a handle, updates the drag from the cursor every frame and commits
	 * through the local controller's Server_MoveNode on release. 2D + Select mode only.
	 */
	void TickLocalNodeDrag();

	UPROPERTY()
	TObjectPtr<AProceduralWallActor> PreviewWallActor;

	void ComputeMiterOffsetsAtNode(int32 NodeID, TMap<int32, FVector2D>& OutStartLeftOffsets,
	                               TMap<int32, FVector2D>& OutStartRightOffsets,
	                               TMap<int32, FVector2D>& OutEndLeftOffsets,
	                               TMap<int32, FVector2D>& OutEndRightOffsets);

	// Internal helpers
	void ClearWallsAndRooms();
	void BroadcastRejected(const FString& Reason);
	bool ApplyNodeMove(int32 NodeID, const FVector2D& NewPosition, bool bLocalPreviewOnly);
	bool ValidateOpeningFits(const FWallSegment& Seg, float WallLengthCm, const FWallOpening& Candidate, int32 IgnoreOpeningIndex, FString& OutReason) const;
	void ComputeWallInteriorSides(const TArray<TArray<FVector2D>>& RoomPolygons);
	FVector2D SnapNodeDragPosition(int32 NodeID, const FVector2D& RawPos) const;
	void NotifySelectionChanged();
	void CommitStateAfterMutation();

	// Finish materials
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> CachedPaintBaseMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> CachedTileBaseMaterial;

	UMaterialInterface* ResolvePaintBaseMaterial();
	UMaterialInterface* ResolveTileBaseMaterial(const FSurfaceFinish& Finish);
	UMaterialInstanceDynamic* CreateFinishMaterialInstance(const FSurfaceFinish& Finish, UObject* Outer);
	UMaterialInterface* ResolveFloorMaterialForRoom(const FRoomData& Room);
	void ApplyWallFinishMaterials();
	const FFloorFinishRecord* FindFloorFinishRecord(const FVector2D& Centroid) const;
	UDataTable* ResolveTileCatalog() const;
	UDataTable* ResolveObjectCatalog() const;
	UDataTable* ResolveCabinetSetCatalog() const;
	UClass* ResolveCabinetSetActorClass() const;
	UStaticMesh* ResolveObjectMesh(const FString& AssetID) const;

	// Objects / cabinet sets
	void RebuildPlacedObjectActors();
	void ApplyPlacedObjectActor(const FPlacedFurnitureData& Data);
	void ReconcileCabinetSetActors();
	AShowroomBooth* SpawnCabinetSetActor(const FPlacedCabinetSetData& Data);
	void DestroyAllPlannerCabinetSets();

	// JSON helpers
	static TSharedPtr<FJsonObject> FinishToJson(const FSurfaceFinish& Finish);
	static FSurfaceFinish FinishFromJson(const TSharedPtr<FJsonObject>& Obj);
};
