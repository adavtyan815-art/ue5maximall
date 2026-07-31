//
// Created by Siqi Wu on 1/17/25.
//

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FurnitureConfigurator/Data/FurnitureTypes.h"
#include "MaxiMallPreviewController.generated.h"

class AShowroomBooth;
class AFurniturePreviewActor;
class UUserWidget;
class UPrimitiveComponent;

class UPixelStreamingInput;

UCLASS(Blueprintable,
       HideCategories = (Collision, Physics, Rendering, Lighting, HLOD, Navigation, Input, ActorTick, ComponentTick, LOD, Cooking, Replication, Tags, TextureStreaming, RayTracing, PathTracing, AssetUserData))
class MAXIMALL_API AMaxiMallPreviewController : public APlayerController {
	GENERATED_BODY()

public:
	AMaxiMallPreviewController();

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

    // в”Ђв”Ђ CONFIGURATOR PREVIEW MANAGEMENT в”Ђв”Ђ

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

    UPROPERTY(BlueprintReadOnly, Category = "MaxiMall | Preview")
    EFurnitureComponentType CurrentTargetComponent;

    // в”Ђв”Ђ BOOTH INTERACTION API в”Ђв”Ђ

    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Booth", meta = (DisplayName = "Request Booth Product Change"))
    void RequestBoothProductChange(AShowroomBooth* TargetBooth, FName NewProductID);

    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Booth", meta = (DisplayName = "Request Booth Door Toggle"))
    void RequestBoothDoorToggle(AShowroomBooth* TargetBooth, int32 SlotIndex);

    UFUNCTION(BlueprintCallable, Category = "MaxiMall | Booth", meta = (DisplayName = "Request Booth Component Selection"))
    void RequestBoothComponentSelection(AShowroomBooth* TargetBooth, EFurnitureComponentType ComponentType, int32 SizeIndex, int32 ColorIndex);

    // в”Ђв”Ђ BLUEPRINT EVENTS в”Ђв”Ђ

    UFUNCTION(BlueprintImplementableEvent, Category = "MaxiMall | Preview Events", meta = (DisplayName = "On Preview Opened"))
    void OnPreviewOpened();

    UFUNCTION(BlueprintImplementableEvent, Category = "MaxiMall | Preview Events", meta = (DisplayName = "On Preview Closed"))
    void OnPreviewClosed();

    // в”Ђв”Ђ CONFIG в”Ђв”Ђ

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

    UPROPERTY(BlueprintReadOnly, Category = "MaxiMall | UI")
    TObjectPtr<UUserWidget> MainWidgetInstance;

    UPROPERTY(BlueprintReadOnly, Category = "MaxiMall | UI")
    TObjectPtr<UUserWidget> ViewmodeOverlayInstance;

    UFUNCTION(BlueprintCallable, Category = "MaxiMall | UI")
    void ToggleConfiguratorUI(AShowroomBooth* Booth, EFurnitureComponentType Component, bool bOpen);

    UFUNCTION(BlueprintCallable, Category = "MaxiMall | PixelStreaming", meta = (DisplayName = "Send Open URL to Browser"))
    void SendOpenURLToBrowser(const FString& URL);

    UFUNCTION(Server, Reliable, WithValidation)
    void Server_LoadBoothState(AShowroomBooth* TargetBooth, FShowroomBoothConfigState State);

protected:
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_RequestBoothDoorToggle(AShowroomBooth* TargetBooth, int32 SlotIndex);

    UFUNCTION(Server, Reliable, WithValidation)
    void Server_RequestBoothProductChange(AShowroomBooth* TargetBooth, FName NewProductID);

    UFUNCTION(Server, Reliable, WithValidation)
    void Server_RequestBoothComponentSelection(AShowroomBooth* TargetBooth, EFurnitureComponentType ComponentType, int32 SizeIndex, int32 ColorIndex);

private:
    UPROPERTY()
    TObjectPtr<AFurniturePreviewActor> ActivePreviewActor;

    FRotator SavedControlRotation;

    UFUNCTION()
    void OnTargetBoothProductChanged(AShowroomBooth* Booth, FName NewProductID);

    UPROPERTY()
    TWeakObjectPtr<UPrimitiveComponent> HoveredComponent;

    bool bIsClosingUI = false;

    float LastClickTime = 0.f;

    bool bWasHoveringInteractable = false;

    void BroadcastCursorState(bool bHovering);

    void OnLeftMouseButtonPressed();

    /** True while the camera is being rotated with RMB held.
     *  Set by AddYawInput/AddPitchInput; reset on RMB release. */
    bool bRightMouseIsDragging = false;

    UPROPERTY()
    TObjectPtr<UPixelStreamingInput> PixelStreamingInput;

    UPROPERTY()
    TObjectPtr<UPixelStreamingInput> ActivePixelStreamingInput;

    FString LastKnownClipboardContent;
    float ClipboardCheckInterval = 0.2f;
    float ClipboardCheckTimer = 0.0f;

    UFUNCTION()
    void OnPixelStreamingInput(const FString& Descriptor);

public:
    /** Returns true if the player is currently rotating the camera with RMB held. */
    FORCEINLINE bool IsRightMouseDragging() const { return bRightMouseIsDragging; }
};

