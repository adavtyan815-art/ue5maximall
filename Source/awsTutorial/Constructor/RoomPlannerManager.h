// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoomPlannerTypes.h"
#include "ProceduralWallActor.h"
#include "ProceduralMeshComponent.h"
#include "PlannerFinishLayout.h"
#include "RoomPlannerManager.generated.h"

class UDataTable;
class UMaterialInstanceDynamic;
class AShowroomBooth;
class APlannerPlacedObjectActor;
class FJsonObject;
class FJsonValue;
class ULocalLightComponent;
class UPostProcessComponent;
class UTexture2D;
struct FHitResult;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRoomPlannerUpdated, const FString&, JSONState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnInteractiveWallDragProgress, float, LengthMeters, FVector, MidpointWorld, float, AngleDeg, bool, bIsSnapped);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWallSelected, int32, SegmentID, float, LengthMeters);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFloorSelected, int32, RoomID, float, AreaM2);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPlannerObjectSelected, const FString&, InstanceID, EPlannerSelectionKind, Kind);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlannerSelectionChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlannerOperationRejected, const FString&, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNodeDragProgress, int32, NodeID, FVector, WorldPos);

class APlannerRoomLightActor;

UCLASS()
class AWSTUTORIAL_API ARoomPlannerManager : public AActor
{
	GENERATED_BODY()

public:
	ARoomPlannerManager();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
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

	/** Which surface of SelectedRoomID is selected: Floor, Ceiling or Baseboard (REQ-13). */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	EPlannerSelectionKind SelectedRoomSurface = EPlannerSelectionKind::Floor;

	/** Face of SelectedSegmentID that is selected for finishing: true = left face (Normal = (-Dir.Y, Dir.X)), false = right face (REQ-13). */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	bool bSelectedWallFaceLeft = true;

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

	/** Thickness of the closed ceiling slab (cm). The slab's top face is what blocks the sun / sky light. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner", meta = (ClampMin = "1"))
	float CeilingThicknessCm = 5.f;

	// ── Room lights ────────────────────────────────────────────────────────────
	// Every detected room owns one APlannerRoomLightActor: exactly ONE rect light whose emission is masked to
	// the room's panel polygon (room polygon inset from the walls), at that room's ceiling height, plus an optional
	// visible emissive panel of the same polygon (FPlannerRoomLightSettings::bShowSurface, off by default). Rebuilt with the rooms on every topology change and
	// destroyed with them, so a split room becomes two independent lights and a merge becomes one.

	/** Tunable settings (also overridable from the player controller, see AAwsTutorial_PlayerController::PlannerRoomLightSettings). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Room Light")
	FPlannerRoomLightSettings RoomLightSettings;

	/** Replaces the settings and rebuilds every room light. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Room Light")
	void SetRoomLightSettings(const FPlannerRoomLightSettings& NewSettings);

	/** Rebuilds all room lights from the current settings without changing them. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Room Light")
	void RebuildRoomLightsPublic() { RebuildRoomLights(); }

	/** Master switch shortcut (console planner.CeilingLights). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Room Light")
	void SetAutoCeilingLightsEnabled(bool bEnabled);

	/** Total real light sources across all rooms (one per room light). */
	UFUNCTION(BlueprintPure, Category = "RoomPlanner|Room Light")
	int32 GetCeilingLightCount() const;

	/** Real light sources of one room: 1, or 0 if none / unknown room. */
	UFUNCTION(BlueprintPure, Category = "RoomPlanner|Room Light")
	int32 GetRoomCeilingLightCount(int32 RoomID) const;

	/** The room light actor of a room, or null. */
	UFUNCTION(BlueprintPure, Category = "RoomPlanner|Room Light")
	APlannerRoomLightActor* GetRoomLight(int32 RoomID) const;

	/** Read-only view of the detected rooms (debug / console reporting). */
	const TMap<int32, FRoomData>& GetRoomsForDebug() const { return Rooms; }

	// ── Planner exposure ───────────────────────────────────────────────────────
	// Lighting is tuned against a deliberate exposure: while the planner UI is open in 3D, an unbound
	// post-process (priority 10) owns the exposure in physical EV100. The planner sets the light level itself
	// (CeilingLightLumensPerM2), so the exposure is FIXED (Min EV100 == Max EV100 disables adaptation): the
	// picture no longer changes with camera framing, and wall / floor brightness differences are real, not
	// metering artefacts. 6.8 EV100 puts the default white floor at ~75 % linear under the 280 lm/m² budget
	// (≈200 lux on the floor), two thirds of a stop below clipping. Widen the range only for adaptive behaviour.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Lighting|Exposure")
	bool bManagePlannerExposure = true;

	/** Fixed exposure (EV100) when equal to Max; otherwise the darkest the view may adapt to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Lighting|Exposure", meta = (ClampMin = "-10", ClampMax = "20"))
	float PlannerExposureMinEV100 = 6.8f;

	/** Equal to Min = fixed exposure (default). Raise above Min only to allow adaptation between the two values. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Lighting|Exposure", meta = (ClampMin = "-10", ClampMax = "20"))
	float PlannerExposureMaxEV100 = 6.8f;

	/** Exposure compensation in stops on top of the fixed EV100: +1 doubles the displayed brightness, −1 halves it. The one taste control. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Lighting|Exposure", meta = (ClampMin = "-5", ClampMax = "5"))
	float PlannerExposureBias = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RoomPlanner|Lighting|Exposure")
	TObjectPtr<UPostProcessComponent> PlannerExposure;

	/** Called by the planner widget on open / close; the exposure override exists only while a session is open in 3D. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Lighting|Exposure")
	void SetPlannerSessionActive(bool bActive);

	/** Re-applies the exposure settings and enables / disables the override from the current state. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Lighting|Exposure")
	void UpdatePlannerExposure();

	/**
	 * While the planner is open in 3D, run Lumen hardware ray tracing in hit-lighting mode for GI as well
	 * (r.Lumen.HardwareRayTracing.LightingMode 1). The planner's floor / walls / ceiling are procedural meshes,
	 * which have ray-tracing geometry but no Lumen surface-cache cards; in the project's mode 2 (hit lighting
	 * for reflections only) a GI ray that hits them returns black, so the white floor bounces nothing and the
	 * ceiling stays dark except where screen-space traces happen to see the floor. Mode 1 lights the hit point
	 * directly. Costs GPU; the project value is restored on close / 2D. Off = keep the project setting.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Lighting")
	bool bPlannerLumenHitLightingGI = true;

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

	// ── Doors & windows: materials ─────────────────────────────────────────────

	/** Parent of painted door / window parts (frames, leaves, thresholds); vector parameter "BaseColor" / "Color". Empty = planner paint material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Openings")
	TObjectPtr<UMaterialInterface> OpeningPaintMaterial;

	/** Clear glass of windows and glazed doors. Empty = /Game/NewDesign/scena/Materials/glass_2 (thin translucent). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Openings")
	TObjectPtr<UMaterialInterface> OpeningGlassMaterial;

	/** Frosted glass. Empty = /Game/NewDesign/scena/Materials/sid_glass_whitte (thin translucent), else clear glass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Openings")
	TObjectPtr<UMaterialInterface> OpeningFrostedGlassMaterial;

	/** Handles and hinges; vector parameter "Color", scalar "Roughness". Empty = /Engine/BasicShapes/BasicShapeMaterial. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Openings")
	TObjectPtr<UMaterialInterface> OpeningMetalMaterial;

	/** Material of one piece of door / window dressing, cached per role and colour. Null when unavailable (e.g. no glass material). */
	UMaterialInterface* GetOpeningMaterial(EPlannerOpeningMaterial Kind, const FLinearColor& Color);

	/** Near-black, fully rough material of the 2D plan symbols (swing arcs). */
	UMaterialInterface* GetPlanSymbolMaterial();

	// ── Exterior view (3D) ─────────────────────────────────────────────────────

	/**
	 * While the planner is open in 3D, an unlit sky + ground enclosure around the layout is shown behind the doors and windows
	 * (otherwise they show the level lit several stops below the planner exposure, i.e. black). It is not a light: it casts no
	 * shadow, contributes no GI, is invisible to ray tracing and reflection captures, and its brightness is absolute cd/m²
	 * matched to the planner's fixed exposure.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Exterior")
	bool bShowExteriorBackdrop = true;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Exterior")
	void SetExteriorBackdropEnabled(bool bEnabled);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RoomPlanner|Exterior")
	TObjectPtr<UProceduralMeshComponent> ExteriorSkyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RoomPlanner|Exterior")
	TObjectPtr<UProceduralMeshComponent> ExteriorGroundMesh;

	/** Tile catalog (rows: FPlannerTileRow). Falls back to /Game/DT/DT_PlannerTiles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Catalogs")
	TObjectPtr<UDataTable> TileCatalog;

	/** Interior object catalog (rows: FPlannerObjectRow). Falls back to /Game/DT/DT_PlannerObjects. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Catalogs")
	TObjectPtr<UDataTable> ObjectCatalog;

	/** Cabinet set catalog (rows: FFurnitureProductRow). Falls back to /Game/DT/DT_FurnitureCatalog. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Catalogs")
	TObjectPtr<UDataTable> CabinetSetCatalog;

	/** Cabinet set layouts (rows: FCabinetSetLayoutRow, RowName = ProductID). Falls back to /Game/DT/DT_CabinetSetLayouts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Catalogs")
	TObjectPtr<UDataTable> CabinetSetLayoutCatalog;

	/** Layout row for a product, from the manager's table if one exists, else from /Game/DT/DT_CabinetSetLayouts. Null when absent. */
	static const FCabinetSetLayoutRow* FindCabinetSetLayoutRow(UWorld* World, FName ProductID);

	/**
	 * Editor helper: writes Saved/PlannerExports/DT_CabinetSetLayouts.csv with one row per DT_FurnitureCatalog
	 * product, filled with the ACTUAL relative transforms (and meshes, if any) of the ten parts in the booth
	 * class defaults (BP_Booth). Import it as a DataTable with row struct CabinetSetLayoutRow.
	 * Console: planner.ExportCabinetSetLayoutsCSV
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Catalogs")
	FString ExportCabinetSetLayoutsCSV();

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

	/**
	 * Set by the planner widget: tells whether the cursor is over the planner's own UI (side panel, status strip, a catalog beside
	 * the panel). Every input path that reads raw mouse state asks it, so a press on the UI never acts on the plan behind it.
	 */
	void SetPlannerUIHitTest(TFunction<bool()> InHitTest) { PlannerUIHitTest = MoveTemp(InHitTest); }
	void ClearPlannerUIHitTest() { PlannerUIHitTest = nullptr; }
	bool IsCursorOverPlannerUI() const { return PlannerUIHitTest && PlannerUIHitTest(); }

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

	/**
	 * A wall drawn from StartPos to EndPos (the plan's wall tool). Its ends join nearby corners and walls as AddNode does, and it is
	 * joined to the plan along its length as well: split where it crosses other walls (they are split there too) and at corners it
	 * passes over, and free wall ends stopping against it are drawn onto it. So a partition always divides the room it is drawn
	 * across, whichever was drawn first. Returns the ID of its first piece, -1 if nothing was added.
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	int32 AddWallBetweenPoints(const FVector2D& StartPos, const FVector2D& EndPos, float Thickness = 20.f, float Height = 280.f);

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
	bool bHasActiveHoverSnap = false;

	/** Frame of the last CheckHoverSnapHint call; a hover snap is only "fresh" while that call keeps coming every frame. */
	uint64 HoverSnapFrame = 0;

	/** True while the Draw Wall hover preview is snapped to a wall endpoint AND was updated this frame or last. */
	UFUNCTION(BlueprintPure, Category = "RoomPlanner")
	bool HasFreshHoverSnap() const { return bHasActiveHoverSnap && (GFrameCounter - HoverSnapFrame) <= 2; }

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

	/**
	 * Sets the wall's height and thickness in cm (REQ-01). Refused (with OnOperationRejected) when an opening
	 * would end above the new height, or when a value is out of range (height 10..1000 cm, thickness 1..200 cm).
	 * Corner joints are recomputed by RebuildAllWalls; the change is committed to ReplicatedRoomJSON.
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool SetWallDimensions(int32 SegmentID, float HeightCm, float ThicknessCm);

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

	/** Builds the 4 × 4 m preset room centred on CenterCm (world X/Y, cm). Geometry, size and Z logic are unchanged. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void BuildPreset4x4mRoom(FVector2D CenterCm);

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

	// ── Door / window / archway styles ──────────────────────────────────────

	/** Sets an opening's look (PlannerOpeningStyles ID). Refused when the style does not exist or belongs to another opening type. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Openings")
	bool SetOpeningStyle(int32 SegmentID, int32 OpeningIndex, FName StyleID);

	/** Effective style of an opening (its type's default when none is stored). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Openings")
	bool GetOpeningStyle(int32 SegmentID, int32 OpeningIndex, FName& OutStyleID) const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Openings")
	bool GetOpeningType(int32 SegmentID, int32 OpeningIndex, EOpeningType& OutType) const;

	/** Styles available for an opening type: ID = style ID, DisplayName = UI label, Color = leaf colour. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Openings")
	TArray<FPlannerCatalogEntry> GetAvailableOpeningStyles(EOpeningType Type) const;

	// ── Door / window leaves in 3D: local view state, never saved or replicated ──

	/** Opens or closes one door / window leaf with an eased swing (3D view only). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Openings")
	void ToggleOpeningLeaf(int32 SegmentID, int32 OpeningIndex);

	/** Opens or closes every door and window leaf; leaves created later start in the same state. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Openings")
	void SetAllOpeningLeavesOpen(bool bOpen);

	UFUNCTION(BlueprintPure, Category = "RoomPlanner|Openings")
	bool GetDefaultOpeningLeavesOpen() const { return bDefaultLeavesOpen; }

	/** In 3D, when Hit is a door / window leaf: toggles it and returns true (the click is consumed). */
	bool TryToggleOpeningLeafFromHit(const FHitResult& Hit);

	/** True when Component is a planner door / window leaf (hover affordance). */
	static bool IsOpeningLeafComponent(const UPrimitiveComponent* Component);

	// ── REQ-06: exact distances of an opening ───────────────────────────────

	/**
	 * Distances in cm from the opening edges to the left / right inner corner of the room-side wall face (seen from inside the room,
	 * clear dimensions), from the opening bottom to the floor, and edge-to-edge to the nearest neighbouring opening on the same wall.
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Openings")
	bool GetOpeningDistances(int32 SegmentID, int32 OpeningIndex, float& OutLeftCornerCm, float& OutRightCornerCm, float& OutFloorCm, float& OutNeighborCm, bool& bOutHasNeighbor) const;

	// ── REQ-02 / REQ-04 / REQ-06: dimension labels ──────────────────────────

	/** Labels (with world positions) describing the current selection or the control point being dragged. Empty when nothing is selected. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Labels")
	TArray<FPlannerDimensionLabel> GetSelectionDimensionLabels() const;

	/**
	 * Dimension lines of the selection, clear dimensions measured on the visible wall faces from the room's inner corners:
	 * - a wall: its face length, beside the selected face;
	 * - a control point being dragged: the live face length of every wall at it, on each wall's room side;
	 * - an opening, beside the selected face: the chain inner corner → opening → inner corner (it adds up to the face length), the
	 *   gap to the nearest neighbouring opening one row further out, the opening height and sill height along the face (3D), and for
	 *   the top-down plan both heights in one caption on the other side of the wall;
	 * - an interior object or cabinet set: the gap from each side of its footprint to the nearest wall face in front of it.
	 * Empty when nothing is selected.
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Labels")
	TArray<FPlannerDimensionLine> GetSelectionDimensionLines() const;

	UFUNCTION(BlueprintPure, Category = "RoomPlanner|Selection")
	EPlannerSelectionKind GetSelectionKind() const;

	/** Clears every kind of selection (wall, opening, floor, object, cabinet set). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Selection")
	void ClearAllSelection();

	/**
	 * When true the selection stays logically active but its visual highlight (selection material / outline)
	 * is not drawn. Set after a catalog colour is applied so the new finish is visible; reset automatically
	 * by the next explicit pick or clear. Local only, never replicated.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner|Selection")
	bool bSelectionHighlightSuppressed = false;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Selection")
	void SetSelectionHighlightSuppressed(bool bSuppressed);

	/** 2D pick: object → cabinet set → wall/opening → floor. Returns what was selected. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Selection")
	EPlannerSelectionKind SelectAtWorldPos2D(const FVector& WorldPos);

	/** 3D pick from a cursor line trace (walls, openings, floor, objects, cabinet sets). Works in any tool mode. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Selection")
	EPlannerSelectionKind SelectSurfaceFromHit(const FHitResult& Hit);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Selection")
	int32 SelectFloorAtWorldPos(const FVector& WorldPos);

	/** Selects the floor, ceiling or baseboard (Surface) of the room at WorldPos. Returns the RoomID or -1. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Selection")
	int32 SelectRoomSurfaceAtWorldPos(const FVector& WorldPos, EPlannerSelectionKind Surface);

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

	/** Z of a node's red handle disc: 1 cm above the top of the tallest wall meeting at that node (280 + 1 when none). */
	float GetNodeHandleZ(int32 NodeID) const;

	/**
	 * Handle hit test against the cursor RAY: for every node the ray is intersected with that node's own handle
	 * plane (GetNodeHandleZ) and the XY distance is tested there. This is what makes the visible disc and the
	 * clickable area identical under the tilted 2D camera; a ground-plane test would be displaced by parallax.
	 */
	int32 FindNodeAtCursorRay(const FVector& RayOrigin, const FVector& RayDirection, float RadiusCm = 25.f) const;

	/** Cursor ray → point on NodeID's handle plane, returned with Z = 0 (planner 2D space). False if the ray is parallel. */
	bool ProjectCursorRayToNodeHandlePlane(int32 NodeID, const FVector& RayOrigin, const FVector& RayDirection, FVector& OutWorldPos) const;

	/** How far outside a wall face (cm) a drop / click still counts as "on that wall" (drag-drop and click-to-place, 2D). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Placement", meta = (ClampMin = "0"))
	float WallDropSnapToleranceCm = 30.f;

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

	/** Sets the finish of BOTH faces of a wall. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	bool SetWallFinish(int32 SegmentID, const FSurfaceFinish& Finish);

	/** Sets the finish of one face of a wall; the other face keeps its own. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	bool SetWallFaceFinish(int32 SegmentID, bool bLeftFace, const FSurfaceFinish& Finish);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	bool SetFloorFinish(int32 RoomID, const FSurfaceFinish& Finish);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	bool SetCeilingFinish(int32 RoomID, const FSurfaceFinish& Finish);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	bool SetBaseboardFinish(int32 RoomID, const FSurfaceFinish& Finish);

	/** Sets the finish of a door / window / archway trim (lining, casing, window frame, sill board). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	bool SetOpeningTrimFinish(int32 SegmentID, int32 OpeningIndex, const FSurfaceFinish& Finish);

	/** Finish of the wall's left face. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	bool GetWallFinish(int32 SegmentID, FSurfaceFinish& OutFinish) const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	bool GetWallFaceFinish(int32 SegmentID, bool bLeftFace, FSurfaceFinish& OutFinish) const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	bool GetFloorFinish(int32 RoomID, FSurfaceFinish& OutFinish) const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	bool GetCeilingFinish(int32 RoomID, FSurfaceFinish& OutFinish) const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	bool GetBaseboardFinish(int32 RoomID, FSurfaceFinish& OutFinish) const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	bool GetOpeningTrimFinish(int32 SegmentID, int32 OpeningIndex, FSurfaceFinish& OutFinish) const;

	/** True when the selected wall face looks into a detected room. */
	UFUNCTION(BlueprintPure, Category = "RoomPlanner|Finish")
	bool IsSelectedWallFaceInterior() const;

	/** Material of a finish (paint / tile instance), shared by every surface using the same finish; null for an unset finish. */
	UMaterialInterface* GetFinishMaterial(const FSurfaceFinish& Finish);

	/** Reads a wall's face finishes from its layout JSON object ("finish" = left face, "finishRight"; older layouts: "finish" on both). */
	static void ReadWallFaceFinishes(const TSharedPtr<FJsonObject>& WallObj, FSurfaceFinish& OutLeft, FSurfaceFinish& OutRight);

	/** Finish of whatever is selected (wall face, door / window trim, floor, ceiling, baseboard or object). */
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

	/** True when the current selection can receive a finish (wall face, door / window trim, floor, ceiling, baseboard, or object). */
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

	// ── Drop placement: walls / floor (DT_FurnitureCatalog = wall-only, DT_PlannerObjects = wall or floor) ──

	/** 2D plan: wall when the point lies on a wall footprint (± tolerance), floor when inside a room, else invalid. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Placement")
	FPlannerDropInfo ResolveDropAtWorldPos2D(const FVector& WorldPos) const;

	/** 3D: wall when the cursor hit a wall actor, floor when it hit the floor mesh, else invalid. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Placement")
	FPlannerDropInfo ResolveDropFromHit(const FHitResult& Hit) const;

	/**
	 * 2D plan under the perspective top-down camera: the cursor ray is intersected with each wall's TOP plane
	 * (Z = wall height) so a drop on the visible wall top counts as that wall, then with the ground plane
	 * for the floor. Avoids the parallax miss of projecting straight to Z = 0.
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Placement")
	FPlannerDropInfo ResolveDropFromCursorRay2D(const FVector& RayOrigin, const FVector& RayDirection) const;

	/** Server: places an interior object against a wall face. Returns the InstanceID (empty on failure). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Placement")
	FString AddPlacedObjectOnWall(const FString& AssetID, int32 SegmentID, float DistanceAlongWallCm, bool bLeftSide, float HeightCm);

	/** Server: places a cabinet set against a wall face (the only valid placement for DT_FurnitureCatalog items). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Placement")
	FString AddCabinetSetOnWall(FName ProductID, int32 SegmentID, float DistanceAlongWallCm, bool bLeftSide);

	/** Server: recomputes the transform of every wall-attached item from the current wall geometry (called on every commit). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Placement")
	void RefreshWallAttachedPlacements();

	/**
	 * Server: measures again how far a wall-attached cabinet set stands off its wall face (its configuration may have moved its
	 * rearmost point), re-places it and republishes the layout when that changed. False when the set is not on a wall, has no
	 * booth, or shows no mesh to measure (the stored offset is then kept).
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Placement")
	bool RemeasureCabinetSetWallDepth(const FString& InstanceID);

	/** True when the selected object / cabinet set is attached to a wall (rotation is then fixed by the wall). */
	UFUNCTION(BlueprintPure, Category = "RoomPlanner|Placement")
	bool IsSelectionWallAttached() const;

	/** Lets UI / controller code surface a refusal through OnOperationRejected. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void NotifyOperationRejected(const FString& Reason);

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
	/** One custom light per detected room, keyed by the RoomID of the current detection pass. Local, never replicated. */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<APlannerRoomLightActor>> RoomLights;

	/** Destroys every room light and rebuilds one per room from the current Rooms. */
	void RebuildRoomLights();
	void ClearRoomLights();

	/**
	 * Set by RebuildRooms. Room lights exist only for 3D, so a dirty flag is resolved on the next Tick while in
	 * 3D, or immediately when switching to 3D; 2D editing (corner drags rebuild the rooms every frame) never
	 * rebuilds hidden lights, and several RebuildRooms calls in one frame coalesce into one light rebuild.
	 */
	bool bRoomLightsDirty = false;

	/** Settings hash of the last RebuildRoomLights; a different hash (Details panel, Blueprint writes) marks the lights dirty. */
	uint32 AppliedRoomLightSettingsHash = 0;

	bool bPlannerSessionActive = false;

	/** Lumen lighting-mode override bookkeeping (see bPlannerLumenHitLightingGI). */
	bool bLumenLightingModeOverridden = false;
	int32 SavedLumenLightingMode = 0;
	void UpdatePlannerLumenMode(bool bWantHitLightingGI);

	/** Door / window dressing materials keyed by role + colour (see GetOpeningMaterial). */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMaterialInterface>> OpeningMaterialCache;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> PlanSymbolMaterial;

	/** Runtime textures referenced by planner materials (kept alive here). */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> ExteriorSkyTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> ExteriorGroundTexture;

	/** Exterior enclosure bookkeeping: rebuilt lazily (only while it can be seen) after layout or look changes. */
	bool bExteriorBackdropDirty = true;
	bool ShouldShowExteriorBackdrop() const;
	void RebuildExteriorBackdrop();
	void UpdateExteriorBackdropVisibility();

	/** Horizontal centre, radius and top of the built enclosure (0 radius = not built). */
	FVector2D ExteriorCenter = FVector2D::ZeroVector;
	float ExteriorRadius = 0.f;
	float ExteriorTop = 0.f;

	/** The enclosure is two-sided and additive: seen from outside it would veil the building, so it shows only around the camera. */
	bool IsCameraInsideExteriorBackdrop() const;

	/**
	 * > 0 while ImportLayoutFromJSON re-creates walls and openings: the per-element RebuildAllWalls / RebuildRooms calls of
	 * AddWall / AddOpeningToWall are skipped and the import rebuilds once at its end (it previously rebuilt every wall and
	 * all rooms after each wall and opening, i.e. quadratic work on every replicated edit).
	 */
	int32 LayoutImportDepth = 0;

	/** Open fraction of one leaf (0 closed, 1 open) and its running animation, keyed by "WallGuid#OpeningIndex". */
	struct FLeafAnimation
	{
		float Current = 0.f;
		float From = 0.f;
		float Target = 0.f;
		float Elapsed = 0.f;
		float Duration = 0.f;
		bool bAnimating = false;
		/** Fingerprint of the opening the state was created for (type + distance, cm): the key uses the opening's index,
		 *  which a delete, drag or split can hand to a different opening. */
		EOpeningType Type = EOpeningType::Door;
		int32 DistanceKey = 0;
	};
	TMap<FString, FLeafAnimation> LeafAnimations;
	bool bDefaultLeavesOpen = false;
	bool bAnyLeafAnimating = false;

	/** Last toggled leaf: one physical press can reach the pick twice (planner widget and PlayerTick). */
	FString LastLeafToggleKey;
	double LastLeafToggleTime = -1.0;

	/** Set by ComputeWallInteriorSides when it rebuilt the walls (lets an import build its walls exactly once). */
	bool bWallsRebuiltByInteriorPass = false;

	/** Length of a wall face covered by other walls meeting at NodeID (AwayDir: this wall's direction away from the node). */
	float ComputeBranchCoverOnFace(int32 SegmentID, int32 NodeID, const FVector2D& AwayDir, const FVector2D& FaceNormal, float HalfThickness) const;

	static FString MakeLeafKey(const FString& WallGuid, int32 OpeningIndex);
	/** State of a leaf, or null; state that belongs to a different opening (moved to this index) is discarded. */
	FLeafAnimation* FindLeafAnimation(AProceduralWallActor* Wall, int32 OpeningIndex);
	/** Starts easing a leaf toward Target; a leaf without state starts from InitialFraction. */
	void StartLeafAnimation(AProceduralWallActor* Wall, int32 OpeningIndex, float Target, float InitialFraction);
	void ToggleLeafOnWall(AProceduralWallActor* Wall, int32 OpeningIndex);
	void TickLeafAnimations(float DeltaTime);
	/** Re-poses a wall's leaves from the stored state (walls are rebuilt / respawned on every layout change). */
	void ApplyLeafAnimationsToWall(AProceduralWallActor* Wall);

	int32 NextNodeID = 1;
	int32 NextSegmentID = 1;

	TMap<int32, FWallNode> Nodes;
	TMap<int32, FWallSegment> WallSegments;
	TMap<int32, TObjectPtr<AProceduralWallActor>> WallActors;
	TMap<int32, FRoomData> Rooms;
	TWeakObjectPtr<UObject> BoundPSInput;

	/** Floor finishes keyed by room centroid so they survive room re-detection. */
	TArray<FFloorFinishRecord> FloorFinishes;

	/** Ceiling and baseboard finishes, keyed like FloorFinishes (REQ-13). */
	TArray<FFloorFinishRecord> CeilingFinishes;
	TArray<FFloorFinishRecord> BaseboardFinishes;

	/** Tile-grid frames of every wall face, recomputed with the corner joints (REQ-13). */
	TMap<int32, FPlannerWallFaceUV> WallFaceUVFrames;

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

	/** Per-room ceiling / baseboard materials (section index = RoomID - 1). */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UMaterialInterface>> CeilingSectionMaterials;

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UMaterialInterface>> BaseboardSectionMaterials;

	/** Finish materials shared per finish (see GetFinishMaterial). */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMaterialInterface>> FinishMaterialCache;

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

	/** See SetPlannerUIHitTest. */
	TFunction<bool()> PlannerUIHitTest;

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

	/** Corner points of one wall end (in the wall's own left/right terms) and whether each face was mitred against a neighbour. */
	struct FWallCornerJoint
	{
		FVector2D Left = FVector2D::ZeroVector;
		FVector2D Right = FVector2D::ZeroVector;
		bool bLeftMitred = false;
		bool bRightMitred = false;
	};

	/** Joints computed by the pre-pass, keyed by (SegmentID, NodeID). Rebuilt on every RebuildAllWalls. */
	TMap<uint64, FWallCornerJoint> CornerJoints;

	static uint64 MakeJointKey(int32 SegID, int32 NodeID) { return ((uint64)(uint32)SegID << 32) | (uint64)(uint32)NodeID; }

	/**
	 * Joint pre-pass: at every node the connected walls are sorted by angle; each wall's counter-clockwise
	 * face is intersected with the next wall's clockwise face (each with its own thickness). The decision
	 * to mitre or fall back is taken once per face pair, so both walls always agree. Works for 2, 3 or more walls.
	 */
	void ComputeAllCornerJoints();

	/** Tile-grid frames of all wall faces from the current corner joints (see PlannerFinishLayout::ComputeWallFaceUVs). */
	void ComputeWallFaceUVFrames();

	/** Centreline and face corner points of a wall, as RebuildAllWalls gives them to the wall mesh (corner joints must be current). */
	bool MakeWallFaceInput(int32 SegID, FPlannerWallFaceInput& Out) const;

	/**
	 * Face inputs of every wall (by segment ID) and the visible extent of each face, [index * 2 + face] (0 = left, 1 = right). While a
	 * corner is dragged onto another corner or onto a wall, they are paired as the junction made on release will pair them.
	 */
	void GatherWallFaces(TArray<FPlannerWallFaceInput>& OutInputs, TMap<int32, int32>& OutIndexBySegment, TArray<FVector2D>& OutFaceExtents) const;

	/** Visible extent of one wall face in cm along the wall from its start node: between the room's inner corners. */
	bool GetVisibleWallFaceExtent(int32 SegmentID, bool bLeftFace, FVector2D& OutExtent) const;

	/**
	 * The face a wall's length is shown on when no face is picked: its room side; for a wall that bounds no room (an open chain, a
	 * free-standing wall) the concave side, whose face is the shorter one.
	 */
	bool GetRoomSideFaceLeft(int32 SegmentID, const TMap<int32, int32>& IndexBySegment, const TArray<FVector2D>& FaceExtents) const;

	/** Footprint of an actor's visible meshes in its own yaw frame: centre, unit X axis, half size (cm) and top height. */
	bool GetActorFootprint(const AActor* Actor, FVector2D& OutCenter, FVector2D& OutAxisX, FVector2D& OutHalfSize, float& OutTopZ) const;

	/**
	 * Baseboards along every wall face that looks into a room, outer walls and interior partitions alike (REQ-13), one mesh section
	 * per room (section index = RoomID - 1). DefaultMaterial is used where the room's baseboards have no finish.
	 */
	void RebuildBaseboards(UMaterialInterface* DefaultMaterial);

	/** Material RebuildRooms last gave rooms without a baseboard finish (baseboards are rebuilt with it when openings change). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseboardDefaultMaterial;

	/** True while RebuildRooms runs (it rebuilds the baseboards itself, at its end). */
	bool bRebuildingRooms = false;

	/** Outlines around each group of joined rooms, from the last RebuildRooms (what a layout before version 3 took as one room). */
	TArray<TArray<FVector2D>> RoomGroupOutlines;

	/**
	 * Layouts saved before version 3 took rooms divided by walls as one room around all of them, so a floor, ceiling or baseboard
	 * finish set then covered all of them: every room in that outline without a finish of its own gets a copy.
	 */
	void SpreadLegacyRoomFinishes();

	/** Re-points every wall of NodeID to TargetNodeID and deletes NodeID. Refused if a wall would collapse or duplicate another. */
	bool MergeNodeInto(int32 NodeID, int32 TargetNodeID);

	// Wall attachment helpers
	int32 FindSegmentIDByGuid(const FString& Guid) const;
	int32 FindSegmentIDForWallActor(const AProceduralWallActor* Actor) const;
	bool GetSegmentGeometry(int32 SegmentID, FVector2D& OutStart, FVector2D& OutDir, FVector2D& OutLeftNormal, float& OutLength, float& OutHalfThickness) const;
	bool ComputeWallAttachedTransform(const FWallAttachment& Attachment, FVector& OutLocation, FRotator& OutRotation) const;
	/** Wall-attached transform of a cabinet set plus the row-level RotationZ (added yaw) from DT_CabinetSetLayouts. */
	bool ComputeCabinetSetTransform(const FPlacedCabinetSetData& Data, FVector& OutLocation, FRotator& OutRotation) const;
	/** Sets DepthOffsetCm so the actor's rearmost visible point touches the face; false (offset untouched) without a visible mesh. */
	bool MeasureAttachmentDepth(AActor* Actor, FWallAttachment& Attachment) const;
	bool SlideAttachmentTo(FWallAttachment& Attachment, const FVector& RequestedLocation) const;
	void DetachItemsFromWall(const FString& WallGuid);
	void RehomeAttachmentsAfterSplit(const FString& OldGuid, const FString& NewGuid, float SplitDistanceCm);
	static TSharedPtr<FJsonObject> AttachmentToJson(const FWallAttachment& Attachment);
	static FWallAttachment AttachmentFromJson(const TSharedPtr<FJsonObject>& Obj);

	/** After a node move: joins the node to a coincident node, or splits a wall it landed on and joins the junction. */
	bool TryConnectMovedNode(int32 NodeID);
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
	/** Brings one wall actor's face finish materials in line with its segment data. */
	void ApplyWallFinishToActor(AProceduralWallActor* Actor, const FWallSegment& Seg);
	/** Index of the stored finish record that belongs to a room (PlannerFinishLayout::AssignRecordsToRooms), or INDEX_NONE. */
	int32 FindRoomRecordIndex(const TArray<FFloorFinishRecord>& Records, int32 RoomID) const;
	/** Stores (or, with an unset finish, removes) a room's finish record, anchored inside that room. */
	void SetRoomFinishRecord(TArray<FFloorFinishRecord>& Records, int32 RoomID, const FSurfaceFinish& Finish);
	/** (from, to) cm along a wall from its start node where walk-through openings (doors, archways) cut it through. */
	void GetWalkThroughSpans(int32 SegmentID, TArray<FVector2D>& OutSpans) const;
	static TArray<TSharedPtr<FJsonValue>> RoomFinishRecordsToJson(const TArray<FFloorFinishRecord>& Records);
	static void RoomFinishRecordsFromJson(const TSharedPtr<FJsonObject>& Root, const TCHAR* Field, TArray<FFloorFinishRecord>& OutRecords);
	/** Stores a ceiling / baseboard finish of a room and republishes; Surface is Ceiling or Baseboard. */
	bool SetRoomSurfaceFinish(int32 RoomID, EPlannerSelectionKind Surface, const FSurfaceFinish& Finish);
	UDataTable* ResolveTileCatalog() const;
	UDataTable* ResolveObjectCatalog() const;
	UDataTable* ResolveCabinetSetCatalog() const;
	UClass* ResolveCabinetSetActorClass() const;
	UStaticMesh* ResolveObjectMesh(const FString& AssetID) const;

	/** DT_PlannerObjects row → Material Overrides (empty when the row has none or is not found). */
	TArray<FPlannerMaterialOverride> ResolveObjectMaterialOverrides(const FString& AssetID) const;

	// Objects / cabinet sets
	void RebuildPlacedObjectActors();
	void ApplyPlacedObjectActor(const FPlacedFurnitureData& Data);
	void ReconcileCabinetSetActors();
	AShowroomBooth* SpawnCabinetSetActor(const FPlacedCabinetSetData& Data);
	void DestroyAllPlannerCabinetSets();

	/** Server: a planner booth was reconfigured in the configurator; its rear may have moved, so it is re-measured against its wall. */
	UFUNCTION()
	void HandleCabinetSetProductChanged(AShowroomBooth* Booth, FName NewProductID);

	// JSON helpers
	static TSharedPtr<FJsonObject> FinishToJson(const FSurfaceFinish& Finish);
	static FSurfaceFinish FinishFromJson(const TSharedPtr<FJsonObject>& Obj);
};
