// Copyright MaxiMall Project. All Rights Reserved.
// FurnitureTypes.h
//
// Central type definitions for the Modular Furniture Configurator System.
// All enums and structs are Blueprint-exposed. No engine-version-specific APIs used.
// Compatible: UE 5.3 → UE 5.6+

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "FurnitureTypes.generated.h"

// ─────────────────────────────────────────────────────────────────────────────
// ENUMERATIONS
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Defines how the countertop and sink geometry interact structurally.
 *
 * BuiltIn       – The sink basin is part of the countertop mesh geometry.
 *                 The standalone Sink component must be hidden.
 *
 * SurfaceMounted – A separate sink mesh sits on top of the countertop.
 *                  The standalone Sink component is shown and repositioned
 *                  using FSinkPlacementOffset from the product data.
 */
UENUM(BlueprintType)
enum class ECountertopType : uint8
{
    BuiltIn          UMETA(DisplayName = "Built-In (Integrated Sink)"),
    SurfaceMounted   UMETA(DisplayName = "Surface-Mounted (Separate Sink)"),
};

/**
 * How many physical door/drawer components are present in a cabinet model.
 * Used to drive visibility and interaction eligibility of the two fixed
 * door slots without any runtime component allocation.
 */
UENUM(BlueprintType)
enum class EDoorCount : uint8
{
    NoDoors      UMETA(DisplayName = "No Doors / Open Cabinet"),
    OneDoor      UMETA(DisplayName = "Single Door"),
    TwoDoors     UMETA(DisplayName = "Two Doors / Double Cabinet"),
};

/**
 * Runtime interaction state of a single cabinet door slot.
 * Kept separate from EDoorCount so a door can be "present but closed"
 * vs "not present" — the distinction matters for collision toggling.
 */
UENUM(BlueprintType)
enum class EDoorSlotState : uint8
{
    NotPresent   UMETA(DisplayName = "Not Present (Hidden + No Collision)"),
    Closed       UMETA(DisplayName = "Closed"),
    Open         UMETA(DisplayName = "Open"),
};

// ─────────────────────────────────────────────────────────────────────────────
// SUPPORTING SUB-STRUCTS
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Describes a single named material override slot on any mesh component.
 * SlotIndex maps directly to UStaticMeshComponent::SetMaterial(Index, Material).
 */
USTRUCT(BlueprintType)
struct MAXIMALL_API FFurnitureMaterialSlot
{
    GENERATED_BODY()

    /** Zero-based material slot index on the target mesh. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material")
    int32 SlotIndex = 0;

    /** Material to apply. Soft reference — not loaded until needed. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material")
    TSoftObjectPtr<UMaterialInterface> Material;
};

/**
 * All material overrides for a single mesh component in a product variant.
 */
USTRUCT(BlueprintType)
struct MAXIMALL_API FFurnitureMeshMaterials
{
    GENERATED_BODY()

    /** Soft reference to the static mesh asset. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
    TSoftObjectPtr<UStaticMesh> Mesh;

    /** Per-slot material overrides. Empty = keep mesh defaults. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
    TArray<FFurnitureMaterialSlot> MaterialOverrides;
};

/**
 * Describes a door slot's resting offset from the cabinet pivot in local space
 * and the angular offset applied when the door transitions to the Open state.
 *
 * Both offsets are relative to the MainCabinet component's local origin so the
 * designer-established world placement is never overwritten — only the delta
 * between "product A door position" and "product B door position" is applied.
 */
USTRUCT(BlueprintType)
struct MAXIMALL_API FDoorSlotConfig
{
    GENERATED_BODY()

    /**
     * Closed-state position offset from the cabinet local origin.
     * Set this in the DataTable to match the physical door pivot on the mesh.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
    FVector ClosedPositionOffset = FVector::ZeroVector;

    /**
     * Yaw rotation offset (degrees) added to the component when transitioning
     * from Closed → Open. Negative = swing left, Positive = swing right.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
    float OpenYawDelta = 90.f;
};

/**
 * Relative transform offset (in the countertop's local space) for the
 * standalone Sink component when ECountertopType == SurfaceMounted.
 * Ignored entirely for BuiltIn countertops.
 */
USTRUCT(BlueprintType)
struct MAXIMALL_API FSinkPlacementOffset
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sink Placement")
    FVector  RelativeLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sink Placement")
    FRotator RelativeRotation = FRotator::ZeroRotator;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sink Placement")
    FVector  RelativeScale    = FVector::OneVector;
};

/**
 * Relative transform offset (in the countertop's local space) for the Faucet
 * component. Always applied regardless of ECountertopType.
 */
USTRUCT(BlueprintType)
struct MAXIMALL_API FFaucetPlacementOffset
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Faucet Placement")
    FVector  RelativeLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Faucet Placement")
    FRotator RelativeRotation = FRotator::ZeroRotator;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Faucet Placement")
    FVector  RelativeScale    = FVector::OneVector;
};

// ─────────────────────────────────────────────────────────────────────────────
// PRIMARY DATA TABLE ROW
// ─────────────────────────────────────────────────────────────────────────────

/**
 * FFurnitureProductRow — One row per distinct product SKU in the DataTable.
 *
 * The DataTable RowName serves as the ProductID (FName key).
 * Designers populate every field inside the Editor's DataTable editor.
 *
 * Memory note: All mesh and material references are TSoftObjectPtr.
 * Assets are loaded on-demand via the configurator and released when the
 * booth switches to a different product. No persistent hard references exist.
 */
USTRUCT(BlueprintType)
struct MAXIMALL_API FFurnitureProductRow : public FTableRowBase
{
    GENERATED_BODY()

    // ── Identity ──────────────────────────────────────────────────────────

    /** Human-readable product name shown in UI. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Identity")
    FText DisplayName;

    /** Optional short description for UI tooltips. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Identity")
    FText Description;

    // ── Cabinet Body ──────────────────────────────────────────────────────

    /** Main cabinet body mesh + material overrides. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Cabinet")
    FFurnitureMeshMaterials CabinetBody;

    // ── Doors ─────────────────────────────────────────────────────────────

    /** How many doors this model physically has. Drives slot visibility. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Doors")
    EDoorCount DoorCount = EDoorCount::TwoDoors;

    /** Mesh + materials shared by both door slots (same geometry per pair). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Doors")
    FFurnitureMeshMaterials DoorMesh;

    /**
     * Per-slot positional config. Index 0 = left/single door. Index 1 = right door.
     * Only Index 0 is used when DoorCount == OneDoor. Both ignored for NoDoors.
     *
     * Note: Populate exactly 2 entries in the DataTable editor.
     * Accessed as DoorSlots[0] and DoorSlots[1] at runtime.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Doors")
    TArray<FDoorSlotConfig> DoorSlots;

    // ── Countertop ────────────────────────────────────────────────────────

    /** Countertop mesh + material overrides. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Countertop")
    FFurnitureMeshMaterials CountertopMesh;

    /**
     * Structural type — determines Sink component visibility and placement.
     * BuiltIn  : Sink component hidden, no offset applied.
     * Surface  : Sink component shown, SinkOffset applied relative to Countertop.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Countertop")
    ECountertopType CountertopType = ECountertopType::SurfaceMounted;

    // ── Sink ──────────────────────────────────────────────────────────────

    /**
     * Standalone sink mesh + materials. Used only when CountertopType == SurfaceMounted.
     * Ignored for BuiltIn — the sink geometry lives inside CountertopMesh.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Sink")
    FFurnitureMeshMaterials SinkMesh;

    /**
     * Positional offset of the Sink component relative to the Countertop's local origin.
     * Evaluated at runtime after the Countertop mesh is applied.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Sink")
    FSinkPlacementOffset SinkOffset;

    // ── Faucet ────────────────────────────────────────────────────────────

    /** Faucet mesh + material overrides. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Faucet")
    FFurnitureMeshMaterials FaucetMesh;

    /**
     * Positional offset of the Faucet component relative to the Countertop's local origin.
     * Always applied regardless of CountertopType.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Faucet")
    FFaucetPlacementOffset FaucetOffset;

    // ── Mirror ────────────────────────────────────────────────────────────

    /** Mirror mesh + material overrides. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Mirror")
    FFurnitureMeshMaterials MirrorMesh;
};
