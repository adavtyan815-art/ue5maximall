// Copyright MaxiMall Project. All Rights Reserved.
// ShowroomBooth.h
//
// AShowroomBooth — the replicated Exhibition Booth Actor.
//
// Designer workflow:
//   1. Place this actor in the level.
//   2. In the Details panel, assign the six static mesh components (created
//      automatically as UPROPERTY sub-objects) and arrange them visually.
//   3. Set ProductCatalog (DataTable) and InitialProductID in the Details panel.
//   4. Hit Play — the system locks in the editor layout as the baseline
//      and applies dynamic offsets only when a configuration change occurs.
//
// Multiplayer:
//   - All visual state is driven by two replicated variables:
//       ActiveProductID  (FName)
//       DoorStates       (TArray<EDoorSlotState>, max 2 entries)
//   - RepNotify callbacks rebuild visuals deterministically from product data.
//   - Clients never run authority logic; the server validates all requests.
//
// Loose coupling:
//   - No hard reference to any Character class.
//   - External actors call RequestProductChange() / RequestDoorToggle() via
//     BlueprintCallable interfaces — the booth does not care who calls them.
//
// Compatible: UE 5.3 → UE 5.6+ (no deprecated APIs used)

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/DataTable.h"
#include "FurnitureConfigurator/Data/FurnitureTypes.h"
#include "ShowroomBooth.generated.h"

class UStaticMeshComponent;
class UDataTable;
class UBoothProximityComponent;

// ─────────────────────────────────────────────────────────────────────────────
// Delegates (Blueprint-Assignable)
// ─────────────────────────────────────────────────────────────────────────────

/** Fired on all machines after a product configuration change is fully applied. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnBoothProductChanged,
    AShowroomBooth*, Booth,
    FName,           NewProductID);

/** Fired on all machines when a door slot changes its open/closed state. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnBoothDoorStateChanged,
    AShowroomBooth*, Booth,
    int32,           SlotIndex,
    EDoorSlotState,  NewState);

// ─────────────────────────────────────────────────────────────────────────────
// AShowroomBooth
// ─────────────────────────────────────────────────────────────────────────────

UCLASS(ClassGroup = (MaxiMall), meta = (BlueprintSpawnableComponent))
class MAXIMALL_API AShowroomBooth : public AActor
{
    GENERATED_BODY()

public:

    AShowroomBooth();

    // ── Lifecycle ─────────────────────────────────────────────────────────

    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void OnConstruction(const FTransform& Transform) override;

    /** Initializes the default visual state of the booth using the InitialProductID and ProductCatalog. */
    UFUNCTION(BlueprintCallable, Category = "Booth | Initialization")
    void InitializeDefaultBooth();

    // ─────────────────────────────────────────────────────────────────────
    // VISUAL COMPONENTS
    // Designers assign and arrange these in the Editor.
    // The C++ system reads their editor-established transforms as the
    // "baseline" and only applies deltas during runtime product changes.
    // ─────────────────────────────────────────────────────────────────────

    /** Root — invisible scene anchor. All other components attach to this. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Booth | Components")
    TObjectPtr<USceneComponent> BoothRoot;

    /** Main cabinet body mesh. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Booth | Components")
    TObjectPtr<UStaticMeshComponent> MainCabinet;

    /**
     * Door Slot 0 — Left door or single door.
     * Visibility/collision controlled at runtime based on EDoorCount.
     * Never destroyed or reallocated — pure state management.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Booth | Components")
    TObjectPtr<UStaticMeshComponent> DoorMeshSlot0;

    /**
     * Door Slot 1 — Right door.
     * Hidden and collision-disabled when EDoorCount < TwoDoors.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Booth | Components")
    TObjectPtr<UStaticMeshComponent> DoorMeshSlot1;

    /** Countertop / vanity top mesh. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Booth | Components")
    TObjectPtr<UStaticMeshComponent> CountertopMesh;

    /**
     * Standalone sink mesh.
     * Hidden when CountertopType == BuiltIn.
     * Repositioned relative to CountertopMesh when CountertopType == SurfaceMounted.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Booth | Components")
    TObjectPtr<UStaticMeshComponent> SinkMesh;

    /** Faucet mesh — always repositioned relative to CountertopMesh. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Booth | Components")
    TObjectPtr<UStaticMeshComponent> FaucetMesh;

    /** Mirror mesh — no runtime transform dependency, mesh/material only. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Booth | Components")
    TObjectPtr<UStaticMeshComponent> MirrorMesh;

    /** Closet mesh component. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Booth | Components")
    TObjectPtr<UStaticMeshComponent> ClosetMesh;

    // ─────────────────────────────────────────────────────────────────────
    // CONFIGURATION (Designer-set in Editor)
    // ─────────────────────────────────────────────────────────────────────

    /**
     * The DataTable asset containing FFurnitureProductRow rows.
     * Set this in the Details panel. One table can be shared by all booths.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Booth | Config",
              meta = (RequiredAssetDataTags = "RowStructure=FurnitureProductRow"))
    TObjectPtr<UDataTable> ProductCatalog;

    /**
     * Product loaded when the game starts (RowName from ProductCatalog).
     * Designers set this per-booth to establish the default display.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Booth | Config")
    FName InitialProductID;

    /**
     * Human-readable booth identifier shown in UI.
     * Not replicated — purely cosmetic/informational.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Booth | Config")
    FText BoothDisplayName;

    // ─────────────────────────────────────────────────────────────────────
    // REPLICATED STATE
    // ─────────────────────────────────────────────────────────────────────

    /**
     * Authoritative configuration state of the showroom booth (replicated).
     */
    UPROPERTY(ReplicatedUsing = OnRep_ActiveState, BlueprintReadOnly,
              Category = "Booth | State")
    FShowroomBoothConfigState ActiveState;

    /**
     * Per-slot door state. Max 2 entries (indices 0 and 1).
     * Replicated with RepNotify. Initialized from EDoorCount in the product data.
     *
     * Memory note: TArray with exactly 2 pre-allocated entries — no dynamic growth.
     */
    UPROPERTY(ReplicatedUsing = OnRep_DoorStates, BlueprintReadOnly,
              Category = "Booth | State")
    TArray<EDoorSlotState> DoorStates;

    // ─────────────────────────────────────────────────────────────────────
    // PUBLIC API (Blueprint Callable)
    // ─────────────────────────────────────────────────────────────────────

    /**
     * Primary entry point for changing the displayed product.
     *
     * Can be called from ANY Blueprint (Character, Widget, AI, etc.).
     * If running on a client, this sends a Server RPC automatically.
     * If running on the server (dedicated or listen), applies directly.
     *
     * @param NewProductID  RowName of the product in ProductCatalog.
     */
    UFUNCTION(BlueprintCallable, Category = "Booth | Interaction",
              meta = (DisplayName = "Request Product Change"))
    void RequestProductChange(FName NewProductID);

    /**
     * Request a size or color selection change on a specific subcomponent.
     */
    UFUNCTION(BlueprintCallable, Category = "Booth | Interaction",
              meta = (DisplayName = "Request Component Selection"))
    void RequestComponentSelection(EFurnitureComponentType ComponentType, FName SizeID, FName ColorID);

    /**
     * Toggles the open/closed state of a single door slot.
     *
     * @param SlotIndex  0 = left/single, 1 = right. Clamped to [0,1].
     */
    UFUNCTION(BlueprintCallable, Category = "Booth | Interaction",
              meta = (DisplayName = "Request Door Toggle"))
    void RequestDoorToggle(int32 SlotIndex);

    /**
     * Returns the resolved product row for the currently active product.
     * Returns false if the product ID is invalid or the table is not set.
     */
    UFUNCTION(BlueprintCallable, Category = "Booth | Query",
              meta = (DisplayName = "Get Active Product Data"))
    bool GetActiveProductData(FFurnitureProductRow& OutData) const;

    /**
     * Returns the current open/closed state of a door slot.
     * Returns EDoorSlotState::NotPresent if the slot index is out of range.
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Booth | Query")
    EDoorSlotState GetDoorSlotState(int32 SlotIndex) const;

    /**
     * Returns true if this booth has a valid, loaded product catalog
     * and the given ProductID exists as a row.
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Booth | Query")
    bool IsValidProductID(FName ProductID) const;

    /**
     * Initializes default selections for a configuration state.
     */
    void InitializeDefaultStateForProduct(FShowroomBoothConfigState& State, FName ProductID, const FFurnitureProductRow& Row);

    /** Getter for BaselineSinkTransform. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Booth | Transforms")
    FTransform GetBaselineSinkTransform() const { return BaselineSinkTransform; }

    /** Getter for BaselineFaucetTransform. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Booth | Transforms")
    FTransform GetBaselineFaucetTransform() const { return BaselineFaucetTransform; }

    // ─────────────────────────────────────────────────────────────────────
    // DELEGATES (Blueprint-Assignable)
    // ─────────────────────────────────────────────────────────────────────

    /** Broadcast after every successful product configuration change. */
    UPROPERTY(BlueprintAssignable, Category = "Booth | Events")
    FOnBoothProductChanged OnProductChanged;

    /** Broadcast after every door state change. */
    UPROPERTY(BlueprintAssignable, Category = "Booth | Events")
    FOnBoothDoorStateChanged OnDoorStateChanged;

protected:

    // ─────────────────────────────────────────────────────────────────────
    // REPLICATION CALLBACKS
    // ─────────────────────────────────────────────────────────────────────

    /** Called on every client (and listen-server client) when ActiveState changes. */
    UFUNCTION()
    void OnRep_ActiveState();

    /** Called on every client (and listen-server client) when DoorStates changes. */
    UFUNCTION()
    void OnRep_DoorStates();

    // ─────────────────────────────────────────────────────────────────────
    // SERVER RPCs
    // ─────────────────────────────────────────────────────────────────────

    /**
     * Server-authoritative product change.
     */
    UFUNCTION(Server, Reliable, WithValidation,
              meta = (DisplayName = "[Server] Apply Product Change"))
    void Server_ApplyProductChange(FName NewProductID);
    bool Server_ApplyProductChange_Validate(FName NewProductID);
    void Server_ApplyProductChange_Implementation(FName NewProductID);

    /**
     * Server-authoritative component selection change.
     */
    UFUNCTION(Server, Reliable, WithValidation,
              meta = (DisplayName = "[Server] Apply Component Selection"))
    void Server_ApplyComponentSelection(EFurnitureComponentType ComponentType, FName SizeID, FName ColorID);
    bool Server_ApplyComponentSelection_Validate(EFurnitureComponentType ComponentType, FName SizeID, FName ColorID);
    void Server_ApplyComponentSelection_Implementation(EFurnitureComponentType ComponentType, FName SizeID, FName ColorID);

    /**
     * Server-authoritative door toggle.
     */
    UFUNCTION(Server, Reliable, WithValidation,
              meta = (DisplayName = "[Server] Toggle Door"))
    void Server_ToggleDoor(int32 SlotIndex);
    bool Server_ToggleDoor_Validate(int32 SlotIndex);
    void Server_ToggleDoor_Implementation(int32 SlotIndex);

    // ─────────────────────────────────────────────────────────────────────
    // INTERNAL VISUAL APPLICATION (runs on ALL machines via RepNotify)
    // ─────────────────────────────────────────────────────────────────────

    /**
     * Master rebuild — applies every visual aspect of a product row:
     *  1. Cabinet mesh + materials
     *  2. Door meshes + visibility + collision
     *  3. Countertop mesh + materials
     *  4. Sink visibility + relative transform
     *  5. Faucet relative transform
     *  6. Mirror mesh + materials
     *
     * This function is DETERMINISTIC: given the same ProductID and the same
     * DataTable, every machine produces identical visual output.
     * Called by OnRep_ActiveProductID() and by the server immediately on change.
     */
    void ApplyProductData(const FFurnitureProductRow& Data);

    /**
     * Applies dynamic size and color selections to a target mesh component.
     */
    void ApplyComponentMeshAndMaterials(UStaticMeshComponent* Target,
                                        const FFurnitureComponentOptions& Options,
                                        const FFurnitureComponentState& State);

    /**
     * Drives door slot visibility and collision from active product data.
     * No components are created or destroyed — only state is modified.
     *
     * DoorCount == NoDoors  : Both slots → NotPresent (hidden, no collision)
     * DoorCount == OneDoor  : Slot0 → Closed, Slot1 → NotPresent
     * DoorCount == TwoDoors : Both slots → Closed
     */
    void ApplyDoorConfiguration(const FFurnitureProductRow& Data);

    /**
     * Applies the visual representation of a single door slot.
     *
     * @param Slot      The door mesh component to update.
     * @param State     The target state (NotPresent / Closed / Open).
     * @param SlotCfg   Door slot config carrying positional and angular offsets.
     */
    void ApplyDoorSlotVisual(UStaticMeshComponent* Slot,
                             EDoorSlotState State,
                             const FDoorSlotConfig& SlotCfg);

    /**
     * Recalculates and applies the relative transforms of Sink and Faucet
     * relative to the CountertopMesh component's current local transform.
     *
     * This is the core of the "structural dependency" pipeline:
     *  - Only the delta from the product data is applied.
     *  - The designer's original world placement is NOT overwritten for
     *    components that have no product-defined offset (e.g., Mirror).
     *
     * @param Data  The full product row for the new active product.
     */
    void RecalculateDependentTransforms(const FFurnitureProductRow& Data);

    /**
     * Initializes DoorStates array to exactly 2 entries on BeginPlay.
     * Entries default to EDoorSlotState::NotPresent until a product is applied.
     */
    void InitializeDoorStateArray();

    /**
     * Convenience: looks up a row from ProductCatalog by RowName.
     * Returns nullptr if ProductCatalog is null or RowName does not exist.
     */
    const FFurnitureProductRow* FindProductRow(FName RowName) const;

private:

    /**
     * Stores the baseline (editor-established) relative transforms of all
     * six components, captured in BeginPlay or OnConstruction before any runtime change occurs.
     */
    UPROPERTY()
    FTransform BaselineSinkTransform;

    UPROPERTY()
    FTransform BaselineFaucetTransform;

    UPROPERTY()
    bool bBaselineTransformsCaptured;
};
