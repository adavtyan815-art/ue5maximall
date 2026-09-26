//
// Created by Siqi Wu on 1/17/25.
//

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FurnitureConfigurator/Data/FurnitureTypes.h"
#include "Constructor/RoomPlannerTypes.h"
#include "awsTutorial_PlayerController.generated.h"

class AShowroomBooth;
class AFurniturePreviewActor;
class AStudioStageActor;
class UUserWidget;
class UPrimitiveComponent;
class ACameraActor;

class UPixelStreamingInput;
class ARoomPlannerManager;
enum class EPlannerWheelTarget : uint8;

UCLASS(Blueprintable,
       HideCategories = (Collision, Physics, Rendering, Lighting, HLOD, Navigation, Input, ActorTick, ComponentTick, LOD, Cooking, Replication, Tags, TextureStreaming, RayTracing, PathTracing, AssetUserData))
class AWSTUTORIAL_API AAwsTutorial_PlayerController : public APlayerController {
	GENERATED_BODY()

public:
	AAwsTutorial_PlayerController();

	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void SetupInputComponent() override;

	/**
	 * Room Planner mouse wheel, 2D only: a notch over the plan turns the interior object being placed (catalog drag, click-to-place)
	 * or the selected / dragged object or cabinet set (PlannerPanelRules::ResolveWheelTarget) and is consumed. Everything else —
	 * 3D, the planner's UI, nothing to turn — takes the normal input path unchanged (camera zoom, Blueprint wheel bindings).
	 * Not bound in SetupInputComponent on purpose: a binding would consume the wheel always.
	 */
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;

	/** Intercept camera yaw to detect RMB camera rotation drags. */
	virtual void AddYawInput(float Val) override;

	/** Intercept camera pitch to detect RMB camera rotation drags. */
	virtual void AddPitchInput(float Val) override;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category="MaxiMall")
	FString GetRequestURL() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category="MaxiMall")
	TArray<FString> GetRequestOptions() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category="MaxiMall")
	bool HasRequestOption(const FString& key) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category="MaxiMall")
	FString GetRequestOption(const FString& key) const;

	UFUNCTION(BlueprintCallable, Server, Reliable, Category="MaxiMall")
	void Kick();

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_EnterRoomPlanner(FVector RelocationLocation);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_ExitRoomPlanner(FVector TargetLocation, FRotator TargetRotation);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Camera")
	void SetRoomPlannerCamera2D(bool bIn2D, FVector CenterLocation = FVector(-10000.f, 0.f, 0.f));

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Camera")
	void UpdateRoomPlannerCameraToolMode(EPlannerToolMode ToolMode);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Camera")
	void RestorePlayerCamera();

	/**
	 * Re-applies the planner input mode exactly as SetRoomPlannerCamera2D does (2D: GameAndUI, cursor kept
	 * visible during capture; 3D: GameAndUI, cursor hidden during capture; never locks the mouse), without
	 * touching the camera or control rotation. Used after overlay UI (e.g. the RAL/NCS catalog) closes.
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Camera")
	void ApplyRoomPlannerInputMode(bool bIn2D);

	// ── Room light tuning (edit in BP_MaxiMallPlayerController) ────────────────

	/** When true, PlannerRoomLightSettings replaces the manager's settings every time the planner UI binds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Room Light")
	bool bOverridePlannerRoomLightSettings = false;

	/** Custom per-room ceiling light settings pushed to the planner manager (see FPlannerRoomLightSettings). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Room Light", meta = (EditCondition = "bOverridePlannerRoomLightSettings"))
	FPlannerRoomLightSettings PlannerRoomLightSettings;

	/** Pushes PlannerRoomLightSettings to the manager (if the override is on) and rebuilds the room lights. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Room Light")
	void ApplyPlannerRoomLightSettings();

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	static FVector FindNonOverlappingPlannerSpot(UWorld* World, AActor* IgnoreActor, const FVector& BaseLocation);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_CommitWall(FVector2D StartPos, FVector2D EndPos, float Thickness = 20.f, float Height = 280.f);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_ClearLayout();

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_BuildPreset4x4mRoom();

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_SetWallLength(int32 SegmentID, float NewLengthMeters);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_DeleteWall(int32 SegmentID);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_DeleteOpening(int32 SegmentID, const FString& OpeningID);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_AddDoor(int32 SegmentID, float WidthMeters = 0.9f, float HeightMeters = 2.1f, float DistFromStartCm = -1.f);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_AddWindow(int32 SegmentID, float WidthMeters = 1.2f, float HeightMeters = 1.2f, float SillHeightMeters = 0.9f, float DistFromStartCm = -1.f);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_UpdateOpeningDimensions(int32 SegmentID, const FString& OpeningID, float WidthMeters, float HeightMeters, float SillHeightMeters);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_UpdateOpeningPosition(int32 SegmentID, const FString& OpeningID, float NewDistFromStartCm);

	// ── Room Planner: control points, swing, finishing, objects, cabinet sets, project (REQ-02..18) ──

	/** Sets the selected wall's height and thickness in cm (REQ-01); validated and replicated like every other wall edit. */
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_SetWallDimensions(int32 SegmentID, float HeightCm, float ThicknessCm);

	/** Moves a wall corner (control point). Openings stay attached; refused if one no longer fits (REQ-02 / REQ-09). */
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_MoveNode(int32 NodeID, FVector2D NewPosition);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_SetOpeningSwing(int32 SegmentID, const FString& OpeningID, EOpeningSwingSide Side, EOpeningSwingDirection Direction);

	/** Sets the look of a door / window / archway (built-in style ID); the manager rejects unknown or mismatched styles. */
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_SetOpeningStyle(int32 SegmentID, const FString& OpeningID, FName StyleID);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_SetWallFinish(int32 SegmentID, FSurfaceFinish Finish);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_SetFloorFinish(int32 RoomID, FSurfaceFinish Finish);

	/** REQ-13: finish of one wall face; the other face keeps its own. */
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_SetWallFaceFinish(int32 SegmentID, bool bLeftFace, FSurfaceFinish Finish);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_SetCeilingFinish(int32 RoomID, FSurfaceFinish Finish);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_SetBaseboardFinish(int32 RoomID, FSurfaceFinish Finish);

	/** REQ-13: finish of a door / window / archway trim. */
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_SetOpeningTrimFinish(int32 SegmentID, const FString& OpeningID, FSurfaceFinish Finish);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_AddPlacedObject(const FString& AssetID, FVector Location, FRotator Rotation, FVector Scale);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_MovePlacedObject(const FString& InstanceID, FVector Location, FRotator Rotation, FVector Scale);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_RemovePlacedObject(const FString& InstanceID);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_SetPlacedObjectFinish(const FString& InstanceID, FSurfaceFinish Finish);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_AddCabinetSet(FName ProductID, FVector Location, FRotator Rotation);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_MoveCabinetSet(const FString& InstanceID, FVector Location, FRotator Rotation);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_RemoveCabinetSet(const FString& InstanceID);

	/** Restores a whole saved project record (planner layout + booth states) on the server (REQ-16). */
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_LoadPlannerProject(const FString& SaveRecordJSON);

	/**
	 * Places a catalog item according to the resolved drop target. Objects: wall (WallSegmentID != -1) or floor, turned by
	 * FloorYawDeg (the yaw the mouse wheel gave it before the drop; a wall placement follows the wall).
	 * Cabinet sets: wall only — a floor drop is ignored.
	 */
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_PlaceCatalogItem(EPlannerPlacementKind Kind, const FString& ItemID, int32 WallSegmentID, float DistanceAlongWallCm, bool bLeftSide, float HeightCm, FVector FloorLocation, float FloorYawDeg = 0.f);

	/**
	 * Performs the armed click-to-place (BeginPlaceObject / BeginPlaceCabinetSet) at a world position; returns true if a request was sent.
	 * A floor placement takes the armed yaw (ARoomPlannerManager::PendingPlacementYawDeg, dialled with the mouse wheel); a wall one follows the wall.
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool PlannerPlacePendingAt(const FVector& WorldPos);

	/** Click-to-place using the cursor RAY (2D): wall tops are tested in their own plane, then the ground. Yaw as PlannerPlacePendingAt. */
	bool PlannerPlacePendingAtCursorRay(const FVector& RayOrigin, const FVector& RayDirection);

	/**
	 * Sends a placement for an already resolved drop; refuses (with a message) when the target is invalid for the item kind.
	 * FloorYawDeg: yaw of a floor placement (a wall placement follows the wall).
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool PlannerPlaceResolved(EPlannerPlacementKind Kind, const FString& ItemID, const FPlannerDropInfo& Drop, float FloorYawDeg = 0.f);

	/** Drag-and-drop entry point: resolves what is under the cursor (2D plan or 3D hit) and places the item there. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool PlannerDropCatalogItemUnderCursor(EPlannerPlacementKind Kind, const FString& ItemID);

	/**
	 * Same as PlannerDropCatalogItemUnderCursor but for an explicit Slate screen-space position (drag/drop
	 * event position). Required for UMG drops: while a drag is active the game viewport's cached mouse
	 * position is invalid, so DeprojectMousePositionToWorld fails.
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	bool PlannerDropCatalogItemAtScreenPosition(EPlannerPlacementKind Kind, const FString& ItemID, FVector2D ScreenSpacePosition, float YawDeg = 0.f);

	/** Converts an absolute Slate position into a world ray through this player's viewport. */
	bool PlannerDeprojectScreenSpace(const FVector2D& ScreenSpacePosition, FVector& OutOrigin, FVector& OutDirection, FVector2D& OutViewportPixels) const;

	/** Line-traces under the cursor and selects the planner wall / opening / floor / object / cabinet set hit (3D mode). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	EPlannerSelectionKind PlannerPickUnderCursor();

	/** Commits the current planner selection's transform (after a local drag) to the server, with a wheel turn made during the drag. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void PlannerCommitSelectedObjectTransform();

	// ── Room Planner: mouse-wheel rotation (2D) ─────────────────────────────────

	/** Degrees per wheel notch (the ↺ / ↻ buttons turn 15°). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Wheel Rotation", meta = (DisplayName = "Wheel Rotate Step (deg)", ClampMin = "0.1", ClampMax = "180.0"))
	float PlannerWheelRotateStepDeg = 15.f;

	/** Off: scroll up turns counter-clockwise on the plan, like «↺ 15°». On: the other way round. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Wheel Rotation", meta = (DisplayName = "Invert Wheel Rotation"))
	bool bInvertPlannerWheelRotation = false;

	/**
	 * The notches of one wheel burst on the selection turn it locally only; this sends them as ONE Server_MovePlacedObject /
	 * Server_MoveCabinetSet (by the item turned, whatever is selected now). Runs by itself 0.25 s after the last notch, and at
	 * once on an LMB press, Delete, a rotate button, leaving 2D and when the camera goes back. No-op when nothing is pending.
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void FlushPlannerWheelCommit();

	/** True, with the yaw, while a wheel turn of InstanceID is applied locally but not sent yet. */
	bool GetPendingPlannerWheelYaw(const FString& InstanceID, float& OutYawDeg) const;

	/** Rotation a drag frame of InstanceID uses: StoredRotation with the yaw of a wheel turn not sent yet (a re-import may have reset it). */
	FRotator GetPlannerDragRotation(const FString& InstanceID, const FRotator& StoredRotation) const;

    // РІвЂќР‚РІвЂќР‚ CONFIGURATOR PREVIEW MANAGEMENT РІвЂќР‚РІвЂќР‚

    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Preview", meta = (DisplayName = "Open Furniture Preview"))
    void OpenFurniturePreview(AShowroomBooth* TargetBooth, EFurnitureComponentType FocusComponent = EFurnitureComponentType::None);

    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Preview", meta = (DisplayName = "Close Furniture Preview"))
    void CloseFurniturePreview();

    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Preview", meta = (DisplayName = "Handle Preview Orbit Input"))
    void HandlePreviewOrbitInput(float DeltaYaw, float DeltaPitch);

    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Preview", meta = (DisplayName = "Reset Preview Rotation"))
    void ResetPreviewRotation();

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MaxiMall | Preview", meta = (DisplayName = "Is Preview Active"))
    bool IsPreviewActive() const;

    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Preview", meta = (DisplayName = "Handle Preview Zoom Input"))
    void HandlePreviewZoomInput(float DeltaZoom);

    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Interaction", meta = (DisplayName = "Trace Furniture Component"))
    bool TraceFurnitureComponent(AShowroomBooth*& OutBooth, EFurnitureComponentType& OutComponentType, UPrimitiveComponent*& OutHitComponent);


    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Interaction", meta = (DisplayName = "Handle Double-Click Interaction"))
    void HandleDoubleClickInteraction();

    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Preview", meta = (DisplayName = "Focus Preview On Component"))
    void FocusPreviewOnComponent(EFurnitureComponentType ComponentType);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MaxiMall | Preview", meta = (DisplayName = "Get Active Component Metadata"))
    bool GetActiveComponentMetadata(EFurnitureComponentType ComponentType, FText& OutProductName, FString& OutSKU, FString& OutURL) const;

    UPROPERTY(BlueprintReadOnly, Category = "MaxiMall | Preview")
    TObjectPtr<AShowroomBooth> CurrentTargetBooth;

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MaxiMall | Preview", meta = (DisplayName = "Get Current Target Booth"))
    AShowroomBooth* GetCurrentTargetBooth() const { return CurrentTargetBooth.Get(); }

    /** The ViewMode preview actor currently displayed (null when not in ViewMode). */
    AFurniturePreviewActor* GetActivePreviewActor() const { return ActivePreviewActor.Get(); }

    UPROPERTY(BlueprintReadOnly, Category = "MaxiMall | Preview")
    EFurnitureComponentType CurrentTargetComponent;

    // РІвЂќР‚РІвЂќР‚ BOOTH INTERACTION API РІвЂќР‚РІвЂќР‚

    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Booth", meta = (DisplayName = "Request Booth Product Change"))
    void RequestBoothProductChange(AShowroomBooth* TargetBooth, FName NewProductID);

    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Booth", meta = (DisplayName = "Request Booth Door Toggle"))
    void RequestBoothDoorToggle(AShowroomBooth* TargetBooth, int32 SlotIndex);

    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Booth", meta = (DisplayName = "Request Booth Component Selection"))
        void RequestBoothComponentSelection(AShowroomBooth* TargetBooth, EFurnitureComponentType ComponentType, int32 SizeIndex, int32 ColorIndex);

    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Booth", meta = (DisplayName = "Request Booth Custom Color Change"))
    void RequestBoothCustomColorChange(AShowroomBooth* TargetBooth, EFurnitureComponentType ComponentType, FLinearColor Color, UMaterialInterface* OverrideMaterial);

    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Interaction", meta = (DisplayName = "Select Component"))
    void SelectComponent(UPrimitiveComponent* ComponentToSelect);


    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MaxiMall | Interaction", meta = (DisplayName = "Get Selected Component"))
    UPrimitiveComponent* GetSelectedComponent() const;

    UPROPERTY(BlueprintReadOnly, Category = "MaxiMall | Interaction")
    TWeakObjectPtr<UPrimitiveComponent> SelectedComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnComponentSelectedDelegate, UPrimitiveComponent*, SelectedComp);

    // в”Ђв”Ђ BLUEPRINT EVENTS в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ

    UPROPERTY(BlueprintAssignable, Category = "MaxiMall | Interaction Events", meta = (DisplayName = "On Component Selected Delegate"))
    FOnComponentSelectedDelegate OnComponentSelectedDelegate;

    UFUNCTION(BlueprintImplementableEvent, Category = "MaxiMall | Interaction Events", meta = (DisplayName = "On Component Selected"))
    void OnComponentSelected(UPrimitiveComponent* SelectedComp);

    UFUNCTION(BlueprintImplementableEvent, Category = "MaxiMall | Preview Events", meta = (DisplayName = "On Preview Opened"))
    void OnPreviewOpened();

    UFUNCTION(BlueprintImplementableEvent, Category = "MaxiMall | Preview Events", meta = (DisplayName = "On Preview Closed"))
    void OnPreviewClosed();

    // РІвЂќР‚РІвЂќР‚ CONFIG РІвЂќР‚РІвЂќР‚

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | Preview Config", meta = (DisplayName = "Preview Actor Class"))
    TSubclassOf<AFurniturePreviewActor> PreviewActorClass;

    /** Optional BP subclass of AStudioStageActor with retuned stage calibration
        (lighting, backdrop, exposure — all studio visual tuning). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | Preview Config", meta = (DisplayName = "Studio Stage Class"))
    TSubclassOf<AStudioStageActor> StudioStageClass;

    /** Isolated spot where the studio preview lives — far above the map so no
        level/planner geometry (e.g. the -10000 planner area) can leak into the
        enclosed studio or its environment capture. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | Preview Config", meta = (DisplayName = "Studio Preview Location"))
    FVector StudioPreviewLocation = FVector(0.f, 0.f, 50000.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | Preview Config", meta = (DisplayName = "Orbit Sensitivity", ClampMin = "0.1", ClampMax = "10.0"))
    float OrbitSensitivity = 1.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MaxiMall | Preview Config", meta = (DisplayName = "Double-Click Threshold (s)", ClampMin = "0.1", ClampMax = "2.0"))
    float DoubleClickThreshold = 0.5f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Config")
    TSubclassOf<UUserWidget> MainWidgetClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Config")
    TSubclassOf<UUserWidget> ViewmodeOverlayClass;

    UPROPERTY(BlueprintReadOnly, Category = "MaxiMall | UI")
    TObjectPtr<UUserWidget> MainWidgetInstance;

    UPROPERTY(BlueprintReadOnly, Category = "MaxiMall | UI")
    TObjectPtr<UUserWidget> ViewmodeOverlayInstance;

    /** Base rotation for the studio preview spawn: the Yaw defines the
        booth-relative entry-view axis the per-component EntryYawOffset values
        are calibrated against. (Kept under its historical property name so
        saved Blueprint values carry over.) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | Preview Config", meta = (DisplayName = "Preview Entry Base Rotation"))
    FRotator ViewModeRelocationRotation = FRotator(0.f, 90.f, 0.f);

    // ── ROOM PLANNER CAMERA CONFIG ──────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | Planner Camera Config", meta = (DisplayName = "2D Camera Height (Z)"))
    float PlannerCameraZ = 1600.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | Planner Camera Config", meta = (DisplayName = "2D Ortho Width"))
    float PlannerOrthoWidth = 2500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | Planner Camera Config", meta = (DisplayName = "Constrain Aspect Ratio"))
    bool bPlannerConstrainAspectRatio = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | Planner Camera Config", meta = (DisplayName = "Aspect Ratio", EditCondition = "bPlannerConstrainAspectRatio"))
    float PlannerAspectRatio = 1.777778f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | Planner Camera Config", meta = (DisplayName = "Perspective Field of View"))
    float PlannerPerspectiveFOV = 80.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | Planner Camera Config", meta = (DisplayName = "Camera Blend Time"))
    float PlannerCameraBlendTime = 0.3f;

    UFUNCTION(BlueprintCallable, Category = "MaxiMall | UI")
    void ToggleConfiguratorUI(AShowroomBooth* Booth, EFurnitureComponentType Component, bool bOpen);

    UFUNCTION(BlueprintCallable, Category = "MaxiMall | PixelStreaming")
    void SendPixelStreamingResponse(const FString& Payload);

    UFUNCTION(BlueprintCallable, Category = "MaxiMall | PixelStreaming", meta = (DisplayName = "Send Open URL to Browser"))
    void SendOpenURLToBrowser(const FString& URL);

    UFUNCTION(Server, Reliable, WithValidation)
    void Server_LoadBoothState(AShowroomBooth* TargetBooth, FShowroomBoothConfigState State, const TArray<FCustomColorOverride>& InCustomColors, const TArray<EDoorSlotState>& InDoorStates);

protected:
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_RequestBoothDoorToggle(AShowroomBooth* TargetBooth, int32 SlotIndex);

    UFUNCTION(Server, Reliable, WithValidation)
    void Server_RequestBoothProductChange(AShowroomBooth* TargetBooth, FName NewProductID);

    UFUNCTION(Server, Reliable, WithValidation)
        void Server_RequestBoothComponentSelection(AShowroomBooth* TargetBooth, EFurnitureComponentType ComponentType, int32 SizeIndex, int32 ColorIndex);

    UFUNCTION(Server, Reliable, WithValidation)
    void Server_RequestBoothCustomColorChange(AShowroomBooth* TargetBooth, EFurnitureComponentType ComponentType, FLinearColor Color, UMaterialInterface* OverrideMaterial);
    bool Server_RequestBoothCustomColorChange_Validate(AShowroomBooth* TargetBooth, EFurnitureComponentType ComponentType, FLinearColor Color, UMaterialInterface* OverrideMaterial);
    void Server_RequestBoothCustomColorChange_Implementation(AShowroomBooth* TargetBooth, EFurnitureComponentType ComponentType, FLinearColor Color, UMaterialInterface* OverrideMaterial);


private:
    UPROPERTY()
    TObjectPtr<AFurniturePreviewActor> ActivePreviewActor;

    /** The Studio Stage environment around the active preview (studio mode only). */
    UPROPERTY()
    TObjectPtr<AStudioStageActor> ActiveStudioStage;

    /** (Re)builds ActiveStudioStage sized to the loaded product and applies the
        stage camera recipe. Called on open and after live product reloads. */
    void RebuildStudioStageForActivePreview();

    // Cursor state saved while the studio grab-hand cursor is active.
    bool bStudioCursorOverridden = false;
    int32 SavedStudioMouseCursor = 0;        // EMouseCursor::Type, raw
    int32 SavedStudioDefaultMouseCursor = 0; // EMouseCursor::Type, raw

    /**
     * While the studio is open, client->server camera position updates are
     * suppressed so the server keeps computing net relevancy from the PAWN
     * (which never leaves the room). Otherwise the reported studio camera at
     * Z=50000 makes distant pawns net-irrelevant, the server tears them down
     * on this client, and they visibly pop back in ~0.1-1 s after closing.
     * Purely client-local and session-scoped: no actor's replication settings
     * change and other players/the server see zero difference.
     */
    bool bStudioCameraUpdatesSuppressed = false;
    bool bSavedUseClientSideCameraUpdates = true;

    UPROPERTY()
    TObjectPtr<ACameraActor> RoomPlannerTopDownCamera;

    FRotator SavedControlRotation;

    UFUNCTION()
    void OnTargetBoothProductChanged(AShowroomBooth* Booth, FName NewProductID);

    UPROPERTY()
    TWeakObjectPtr<UPrimitiveComponent> HoveredComponent;

    bool bIsClosingUI = false;

    float LastClickTime = 0.f;

    bool bWasHoveringInteractable = false;

    void BroadcastCursorState(bool bHovering);

    void OnLeftMouseButtonDown();
    void OnLeftMouseButtonReleased();
    void OnLeftMouseButtonClicked();

    float LMBPressTime = 0.f;
    FVector2D LMBPressMousePos = FVector2D::ZeroVector;

    /** True while the camera is being rotated with RMB held.
     *  Set by AddYawInput/AddPitchInput; reset on RMB release. */
    bool bRightMouseIsDragging = false;

    /** True while dragging in 2D top-down mode to draw a wall. */
    bool bIs2DDrawingWall = false;

    /** The current LMB press started over the planner's own UI (side panel, status strip, a catalog beside it): the plan ignores it. */
    bool bLMBPressOverPlannerUI = false;

    /** True while dragging a wall control point in 2D Select mode (REQ-02). */
    bool bIs2DDraggingNode = false;

    /** InstanceID of the placed object / cabinet set being dragged in 2D Select mode (empty = none). */
    FString Dragged2DObjectID;
    bool bDragged2DIsCabinetSet = false;
    FVector Dragged2DOffset = FVector::ZeroVector;

    /** Mouse-wheel rotation (2D): fractions of a notch not yet turned, and what they were counted for. */
    float PlannerWheelAccum = 0.f;
    FString PlannerWheelTargetKey;
    /** Real time of the last wheel input that turned (or tried to turn) something. */
    double LastPlannerWheelTime = -100.0;

    /** The wheel burst applied locally but not committed yet (FlushPlannerWheelCommit); empty = none. */
    FString WheelCommitID;
    bool bWheelCommitIsCabinetSet = false;
    float WheelCommitYaw = 0.f;

    /** No wheel input for this long ends a burst: it is committed. Same as the planner's opening-drag idle time. */
    static constexpr double PlannerWheelCommitIdleSeconds = 0.25;
    /** No wheel input for this long drops a fraction of a notch left over. */
    static constexpr double PlannerWheelAccumResetSeconds = 0.3;

    /** What a wheel notch turns now (None: the wheel keeps its normal behaviour). */
    EPlannerWheelTarget ResolvePlannerWheelTarget(ARoomPlannerManager* Manager) const;

    /** Turns Target by the whole notches in WheelDelta (fractions add up). */
    void ApplyPlannerWheel(ARoomPlannerManager* Manager, EPlannerWheelTarget Target, float WheelDelta);

    /** Selection target: turns the selected item locally and records the burst for its single commit. */
    void RotatePlannerSelectionByWheel(ARoomPlannerManager* Manager, float DeltaYawDeg);

    /** Puts the pending burst's yaw back on its item when a replicated re-import reset it; drops a burst whose item is gone. */
    void ReassertPendingPlannerWheelYaw(ARoomPlannerManager* Manager);

    /** Every tick: commits the burst once the wheel is idle and LMB is up, else keeps its yaw on the item. */
    void TickPlannerWheelCommit();

    /** LMB held on either input path: the controller's keys, or Slate's pressed buttons (the planner widget holds the mouse in its drags). */
    bool IsPlannerLMBHeld() const;

    /**
     * Cached reference to the UPixelStreamingInput component owned by the PS plugin.
     * Populated ONCE in BeginPlay() via GetComponentByClass — NOT CreateDefaultSubobject.
     * FIX 2: ActivePixelStreamingInput removed (was causing duplicate delegate stacking).
     */
    UPROPERTY()
    TObjectPtr<UPixelStreamingInput> PixelStreamingInput;

    FString LastKnownClipboardContent;
    float ClipboardCheckInterval = 0.2f;
    float ClipboardCheckTimer = 0.0f;

    UFUNCTION()
    void OnPixelStreamingInput(const FString& Descriptor);

public:
    /** Returns true if the player is currently rotating the camera with RMB held. */
    FORCEINLINE bool IsRightMouseDragging() const { return bRightMouseIsDragging; }
};

