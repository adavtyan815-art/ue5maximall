//
// Created by Siqi Wu on 1/17/25.
//

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FurnitureConfigurator/Data/FurnitureTypes.h"
#include "awsTutorial_PlayerController.generated.h"

class AShowroomBooth;
class AFurniturePreviewActor;
class UUserWidget;
class UPrimitiveComponent;
class ACameraActor;

class UPixelStreamingInput;

UCLASS(Blueprintable,
       HideCategories = (Collision, Physics, Rendering, Lighting, HLOD, Navigation, Input, ActorTick, ComponentTick, LOD, Cooking, Replication, Tags, TextureStreaming, RayTracing, PathTracing, AssetUserData))
class AWSTUTORIAL_API AAwsTutorial_PlayerController : public APlayerController {
	GENERATED_BODY()

public:
	AAwsTutorial_PlayerController();

	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void SetupInputComponent() override;

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
	void RestorePlayerCamera();

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
	void Server_DeleteOpening(int32 SegmentID, int32 OpeningIndex);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_AddDoor(int32 SegmentID, float WidthMeters = 0.9f, float HeightMeters = 2.1f, float DistFromStartCm = -1.f);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_AddWindow(int32 SegmentID, float WidthMeters = 1.2f, float HeightMeters = 1.2f, float SillHeightMeters = 0.9f, float DistFromStartCm = -1.f);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_UpdateOpeningDimensions(int32 SegmentID, int32 OpeningIndex, float WidthMeters, float HeightMeters, float SillHeightMeters);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RoomPlanner|Network")
	void Server_UpdateOpeningPosition(int32 SegmentID, int32 OpeningIndex, float NewDistFromStartCm);

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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | Preview Config", meta = (DisplayName = "Orbit Sensitivity", ClampMin = "0.1", ClampMax = "10.0"))
    float OrbitSensitivity = 1.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MaxiMall | Preview Config", meta = (DisplayName = "Double-Click Threshold (s)", ClampMin = "0.1", ClampMax = "2.0"))
    float DoubleClickThreshold = 0.5f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Config")
    TSubclassOf<UUserWidget> MainWidgetClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Config")
    TSubclassOf<UUserWidget> ViewmodeOverlayClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Config")
    TSubclassOf<UUserWidget> RoomPlannerClass;

    UPROPERTY(BlueprintReadOnly, Category = "MaxiMall | UI")
    TObjectPtr<UUserWidget> MainWidgetInstance;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI")
    TObjectPtr<UUserWidget> RoomPlannerInstance;

    UPROPERTY(BlueprintReadOnly, Category = "MaxiMall | UI")
    TObjectPtr<UUserWidget> ViewmodeOverlayInstance;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | RoomPlanner Config", meta = (DisplayName = "Planner Relocation Location"))
    FVector RoomPlannerRelocationLocation = FVector(-10000.f, 0.f, 0.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | ViewMode Config", meta = (DisplayName = "View Mode Relocation Location"))
    FVector ViewModeRelocationLocation = FVector(-10000.f, 0.f, 0.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | ViewMode Config", meta = (DisplayName = "View Mode Relocation Rotation"))
    FRotator ViewModeRelocationRotation = FRotator(0.f, 90.f, 0.f);

    UFUNCTION(BlueprintCallable, Category = "MaxiMall | UI")
    void ToggleConfiguratorUI(AShowroomBooth* Booth, EFurnitureComponentType Component, bool bOpen);

    UFUNCTION(BlueprintCallable, Category = "MaxiMall | UI")
    void ToggleRoomPlannerUI(bool bOpen);

    UFUNCTION(BlueprintCallable, Category = "MaxiMall | PixelStreaming")
    void SendPixelStreamingResponse(const FString& Payload);

    /** Starts a smooth 60 FPS cinematic studio auto-tour around the target booth. */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Cinematic Tour")
    void StartCinematicTour(AShowroomBooth* TargetBooth = nullptr);

    /** Stops the active cinematic tour and returns camera control seamlessly to the player. */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Cinematic Tour")
    void StopCinematicTour();

    /** Toggles the cinematic tour on/off. */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Cinematic Tour")
    void ToggleCinematicTour(AShowroomBooth* TargetBooth = nullptr);

    /** True if Cinematic Tour is currently active and animating. */
    UPROPERTY(BlueprintReadOnly, Category = "MaxiMall | Cinematic Tour")
    bool bIsCinematicTourActive = false;

    /** Orbit speed in degrees per second. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | Cinematic Tour Config")
    float CinematicTourOrbitSpeed = 20.0f;

    /** Base distance from booth focal center for fallback frontal arc. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | Cinematic Tour Config")
    float CinematicTourBaseRadius = 180.0f;

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

    UPROPERTY()
    TObjectPtr<ACameraActor> RoomPlannerTopDownCamera;

    FRotator SavedControlRotation;
    FTransform CachedOriginalBoothTransform;
    bool bHasCachedBoothTransform = false;

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

    void OnRightMouseButtonDown();
    void OnRightMouseButtonReleased();

    float LMBPressTime = 0.f;
    FVector2D LMBPressMousePos = FVector2D::ZeroVector;

    float RMBPressTime = 0.f;
    FVector2D RMBPressMousePos = FVector2D::ZeroVector;

    /** True while the camera is being rotated with RMB held.
     *  Set by AddYawInput/AddPitchInput; reset on RMB release. */
    bool bRightMouseIsDragging = false;

    /** True while dragging in 2D top-down mode to draw a wall. */
    bool bIs2DDrawingWall = false;

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

    // ── Cinematic Tour Private State ──────────────────────────────────────
    FTimerHandle CinematicTourTimerHandle;
    float CinematicTourElapsedTime = 0.0f;
    TWeakObjectPtr<AShowroomBooth> CinematicTourTargetBooth;

    void UpdateCinematicTourStep();

    UFUNCTION()
    void OnPixelStreamingInput(const FString& Descriptor);

public:
    /** Returns true if the player is currently rotating the camera with RMB held. */
    FORCEINLINE bool IsRightMouseDragging() const { return bRightMouseIsDragging; }
};

