// Copyright awsTutorial Project. All Rights Reserved.
// FurnitureTypes.h
//
// Central type definitions for the Modular Furniture Configurator System.
// All enums and structs are Blueprint-exposed. No engine-version-specific APIs used.
// Compatible: UE 5.3 в†’ UE 5.6+

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "FurnitureTypes.generated.h"

// в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
// ENUMERATIONS
// в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ

UENUM(BlueprintType)
enum class EOptionType : uint8
{
    Size,
    Color
};

/**
 * Defines how the countertop and sink geometry interact structurally.
 *
 * BuiltIn       вЂ“ The sink basin is part of the countertop mesh geometry.
 *                 The standalone Sink component must be hidden.
 *
 * SurfaceMounted вЂ“ A separate sink mesh sits on top of the countertop.
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
 * vs "not present" вЂ” the distinction matters for collision toggling.
 */
UENUM(BlueprintType)
enum class EDoorSlotState : uint8
{
    NotPresent   UMETA(DisplayName = "Not Present (Hidden + No Collision)"),
    Closed       UMETA(DisplayName = "Closed"),
    Open         UMETA(DisplayName = "Open"),
};

/**
 * Identifies a specific dynamic visual component of the modular furniture set.
 */
UENUM(BlueprintType)
enum class EFurnitureComponentType : uint8
{
    None             UMETA(DisplayName = "None"),
    Cabinet          UMETA(DisplayName = "Cabinet"),
    Closet           UMETA(DisplayName = "Closet"),
    Doors            UMETA(DisplayName = "Doors"),
    Countertop       UMETA(DisplayName = "Countertop"),
    Sink             UMETA(DisplayName = "Sink"),
    Faucet           UMETA(DisplayName = "Faucet"),
    Mirror           UMETA(DisplayName = "Mirror"),
};

UENUM(BlueprintType)
enum class EFaucetType : uint8
{
    Standard      UMETA(DisplayName = "Standard (Deck-Mounted)"),
    Integrated    UMETA(DisplayName = "Integrated (Wall/Counter-Integrated)"),
};



// в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
// SUPPORTING SUB-STRUCTS
// в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ

/**
 * Describes a single named material override slot on any mesh component.
 * SlotIndex maps directly to UStaticMeshComponent::SetMaterial(Index, Material).
 */
USTRUCT(BlueprintType)
struct FFurnitureMaterialSlot
{
    GENERATED_BODY()

    /** Zero-based material slot index on the target mesh. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material")
    int32 SlotIndex = 0;

    /** Material to apply. Soft reference вЂ” not loaded until needed. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material")
    TSoftObjectPtr<UMaterialInterface> Material;
};

/**
 * All material overrides for a single mesh component in a product variant.
 */
USTRUCT(BlueprintType)
struct FFurnitureMeshMaterials
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
 * A color/material option for a component, containing material overrides.
 */
USTRUCT(BlueprintType)
struct FFurnitureColorOption
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color Option")
    TSoftObjectPtr<UTexture2D> Thumbnail;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color Option")
    TArray<FFurnitureMaterialSlot> MaterialOverrides;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color Option | Metadata")
    FText ProductName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color Option | Metadata")
    FString SKU;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color Option | Metadata")
    FString URL;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color Option | Metadata")
    TArray<int32> SizeIndices;
};

/**
 * Wraps size and color/material options for a component.
 */
/**
 * Product metadata for a specific combination of component selections.
 */
USTRUCT(BlueprintType)
struct FFurnitureMetadata
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metadata")
    FText ProductName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metadata")
    FString SKU;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metadata")
    FString URL;
};

/**
 * A mapped combination key-value entry for size and color configurations.
 */
USTRUCT(BlueprintType)
struct FFurnitureMetadataEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combination")
    int32 SizeIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combination")
    int32 ColorIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combination")
    FFurnitureMetadata Metadata;
};

USTRUCT(BlueprintType)
struct FFurnitureCabinetOptions
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cabinet Options", meta = (DisplayName = "Sizes"))
    TArray<TSoftObjectPtr<UStaticMesh>> Sizes;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cabinet Options")
    TArray<FText> SizeNames;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cabinet Options")
    TArray<FFurnitureColorOption> Colors;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cabinet Options")
    TArray<FFurnitureMetadataEntry> CombinationsMetadata;

    /** If true, enables opening the RAL/NCS Color Catalog for this cabinet */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cabinet Options")
    bool bAllowColorCatalog = false;
};

USTRUCT(BlueprintType)
struct FFurniturePlacementOffset
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
    FVector RelativeLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
    FRotator RelativeRotation = FRotator::ZeroRotator;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
    FVector RelativeScale = FVector::OneVector;
};

USTRUCT(BlueprintType)
struct FFurnitureModelOption
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model Option")
    TSoftObjectPtr<UStaticMesh> Mesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model Option")
    TSoftObjectPtr<UTexture2D> Thumbnail;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model Option")
    TArray<FFurnitureColorOption> Colors;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model Option | Configuration")
    ECountertopType CountertopType = ECountertopType::SurfaceMounted;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model Option | Configuration")
    FFurniturePlacementOffset RelativeOffset;

    /** Optional material override for the mirror glass surface inside the isolated viewmode. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model Option | Viewmode Override")
    TSoftObjectPtr<UMaterialInterface> MirrorMaterialOverride;

    /** Optional slot index to override. If -1, dynamically matches by material name. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model Option | Viewmode Override")
    int32 MirrorMaterialSlotIndex = -1;

    /** If true, enables opening the RAL/NCS Color Catalog for this model */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model Option")
    bool bAllowColorCatalog = false;
};

USTRUCT(BlueprintType)
struct FFurnitureCountertopRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Countertop")
    TArray<TSoftObjectPtr<UStaticMesh>> Sizes;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Countertop")
    TSoftObjectPtr<UTexture2D> Thumbnail;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Countertop")
    TArray<FFurnitureColorOption> Colors;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Countertop | Configuration")
    ECountertopType CountertopType = ECountertopType::SurfaceMounted;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Countertop | Configuration")
    FFurniturePlacementOffset RelativeOffset;

    /** If true, enables opening the RAL/NCS Color Catalog for this countertop model */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Countertop")
    bool bAllowColorCatalog = false;
};

USTRUCT(BlueprintType)
struct FFurnitureSinkRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sink")
    TSoftObjectPtr<UStaticMesh> Mesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sink")
    TSoftObjectPtr<UTexture2D> Thumbnail;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sink")
    TArray<FFurnitureColorOption> Colors;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sink | Configuration")
    FFurniturePlacementOffset RelativeOffset;

    /** If true, enables opening the RAL/NCS Color Catalog for this sink model */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sink")
    bool bAllowColorCatalog = false;
};

USTRUCT(BlueprintType)
struct FFurnitureFaucetRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Faucet")
    TSoftObjectPtr<UStaticMesh> Mesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Faucet")
    TSoftObjectPtr<UTexture2D> Thumbnail;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Faucet")
    TArray<FFurnitureColorOption> Colors;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Faucet | Configuration")
    FFurniturePlacementOffset RelativeOffset;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Faucet | Configuration")
    EFaucetType FaucetType = EFaucetType::Standard;

    /** If true, enables opening the RAL/NCS Color Catalog for this faucet model */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Faucet")
    bool bAllowColorCatalog = false;
};

USTRUCT(BlueprintType)
struct FFurnitureMirrorRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mirror")
    TSoftObjectPtr<UStaticMesh> Mesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mirror")
    TSoftObjectPtr<UTexture2D> Thumbnail;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mirror")
    TArray<FFurnitureColorOption> Colors;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mirror | Configuration")
    FFurniturePlacementOffset RelativeOffset;

    /** Optional material override for the mirror glass surface inside the isolated viewmode. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mirror | Viewmode Override")
    TSoftObjectPtr<UMaterialInterface> MirrorMaterialOverride;

    /** Optional slot index to override. If -1, dynamically matches by material name. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mirror | Viewmode Override")
    int32 MirrorMaterialSlotIndex = -1;

    /** If true, enables opening the RAL/NCS Color Catalog for this mirror model */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mirror")
    bool bAllowColorCatalog = false;
};

USTRUCT(BlueprintType)
struct FFurnitureComponentOptions
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Component Options", meta = (DisplayName = "Models"))
    TArray<FFurnitureModelOption> Models;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Component Options")
    TArray<FFurnitureMetadataEntry> CombinationsMetadata;

    /** If true, enables opening the RAL/NCS Color Catalog for this component */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Component Options")
    bool bAllowColorCatalog = false;
};

/**
 * A door color/material option containing material overrides for a single door mesh, with no Thumbnail.
 */
USTRUCT(BlueprintType)
struct FFurnitureDoorColorOption
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color Option")
    TArray<FFurnitureMaterialSlot> MaterialOverrides;
};

/**
 * Size options for double doors, holding exactly two explicit mesh slots.
 */
USTRUCT(BlueprintType)
struct FFurnitureDoubleDoorsSizeOption
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sizes")
    TSoftObjectPtr<UStaticMesh> Slot0Mesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sizes")
    TSoftObjectPtr<UStaticMesh> Slot1Mesh;
};

/**
 * Color options for double doors, holding two separate material override blocks.
 */
USTRUCT(BlueprintType)
struct FFurnitureDoubleDoorsColorOption
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
    TArray<FFurnitureMaterialSlot> Slot0MaterialOverrides;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
    TArray<FFurnitureMaterialSlot> Slot1MaterialOverrides;
};

/**
 * Describes a door slot's resting offset from the cabinet pivot in local space
 * and the angular offset applied when the door transitions to the Open state.
 *
 * Both offsets are relative to the MainCabinet component's local origin so the
 * designer-established world placement is never overwritten вЂ” only the delta
 * between "product A door position" and "product B door position" is applied.
 */
USTRUCT(BlueprintType)
struct FDoorSlotConfig
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
     * from Closed в†’ Open. Negative = swing left, Positive = swing right.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
    float OpenYawDelta = 90.f;

    /**
     * If true, the door/drawer opens using rotation (hinge swing).
     * If false, it opens using translation (drawer slide).
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
    bool bIsRotation = true;

    /**
     * Translation offset applied when transitioning from Closed в†’ Open.
     * Only evaluated if bIsRotation is false.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
    FVector OpenTranslationOffset = FVector(35.f, 0.f, 0.f);
};

/**
 * Configuration schema strictly for a single door setup.
 */
USTRUCT(BlueprintType)
struct FFurnitureSingleDoorConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doors")
    FDoorSlotConfig SlotConfig;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doors")
    TArray<TSoftObjectPtr<UStaticMesh>> Sizes;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doors")
    TArray<FFurnitureDoorColorOption> Colors;
};

/**
 * Configuration schema strictly for a double doors setup.
 */
USTRUCT(BlueprintType)
struct FFurnitureDoubleDoorsConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doors")
    FDoorSlotConfig Slot0Config;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doors")
    FDoorSlotConfig Slot1Config;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doors")
    TArray<FFurnitureDoubleDoorsSizeOption> Sizes;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doors")
    TArray<FFurnitureDoubleDoorsColorOption> Colors;
};

/**
 * Combines configuration for single or double doors, visible conditionally in details panel.
 */
USTRUCT(BlueprintType)
struct FFurnitureDoorGroup
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doors")
    EDoorCount DoorCount = EDoorCount::TwoDoors;

    /** Active only if DoorCount == SingleDoor */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doors", meta = (EditCondition = "DoorCount == 1"))
    FFurnitureSingleDoorConfig SingleDoor;

    /** Active only if DoorCount == TwoDoors */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doors", meta = (EditCondition = "DoorCount == 2"))
    FFurnitureDoubleDoorsConfig DoubleDoors;

    /** If true, enables opening the RAL/NCS Color Catalog for these doors */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doors")
    bool bAllowColorCatalog = false;
};

/**
 * Unified doors configuration splits cabinet and closet doors.
 */
USTRUCT(BlueprintType)
struct FFurnitureDoorsConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cabinet Doors")
    FFurnitureDoorGroup CabinetDoors;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Closet Doors")
    FFurnitureDoorGroup ClosetDoors;
};

/**
 * Custom color override for a specific component.
 */
USTRUCT(BlueprintType)
struct FCustomColorOverride
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom Color")
    EFurnitureComponentType ComponentType;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom Color")
    FLinearColor CustomColor;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom Color")
    UMaterialInterface* OverrideMaterial;

    FCustomColorOverride()
        : ComponentType(EFurnitureComponentType::Cabinet)
        , CustomColor(FLinearColor::White)
        , OverrideMaterial(nullptr)
    {}

    FCustomColorOverride(EFurnitureComponentType InComponent, const FLinearColor& InColor, UMaterialInterface* InMaterial)
        : ComponentType(InComponent)
        , CustomColor(InColor)
        , OverrideMaterial(InMaterial)
    {}
};

/**
 * Replicated configuration state tracking choices for all subcomponents of a booth.
 */
USTRUCT(BlueprintType)
struct FShowroomBoothConfigState
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Booth State")
    FName ProductID;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Booth State")
    int32 ActiveSizeIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Booth State")
    int32 ActiveColorIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Booth State")
    int32 CountertopSizeIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Booth State")
    int32 ActiveCountertopColorIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Booth State")
    int32 ClosetSizeIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Booth State")
    int32 ClosetColorIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Booth State")
    int32 SinkSizeIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Booth State")
    int32 SinkColorIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Booth State")
    int32 FaucetSizeIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Booth State")
    int32 FaucetColorIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Booth State")
    int32 MirrorSizeIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Booth State")
    int32 MirrorColorIndex = 0;
};




// ─────────────────────────────────────────────────────────────────────────────
// PRIMARY DATA TABLE ROW
// ─────────────────────────────────────────────────────────────────────────────

/**
 * FFurnitureProductRow — One row per distinct product category/set in the DataTable.
 *
 * The DataTable RowName serves as the ProductID (FName key).
 * Designers populate every field inside the Editor's DataTable editor.
 *
 * Memory note: All mesh and material references are TSoftObjectPtr.
 * Assets are loaded on-demand via the configurator and released when the
 * booth switches to a different product. No persistent hard references exist.
 */
USTRUCT(BlueprintType)
struct FFurnitureProductRow : public FTableRowBase
{
    GENERATED_BODY()

    // ── Cabinet Body ──────────────────────────────────────────────────────

    /** Main cabinet body options. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Cabinet")
    FFurnitureCabinetOptions CabinetOptions;

    // ── Closet Body ───────────────────────────────────────────────────────

    /** Closet options. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Closet")
    FFurnitureComponentOptions ClosetOptions;

    // ── Doors ─────────────────────────────────────────────────────────────

    /** Unified doors configuration for cabinet and closet doors. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Doors")
    FFurnitureDoorsConfig DoorsConfig;

    // ── Countertop ────────────────────────────────────────────────────────

    /** Countertop allowed model IDs from shared catalog. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Countertop", meta = (GetOptions = "MaxiMall.FurnitureEditorHelper.GetCountertopOptions"))
    TArray<FName> AllowedCountertopIDs;

    // ── Sink ──────────────────────────────────────────────────────────────

    /** Standalone sink allowed model IDs from shared catalog. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Sink", meta = (GetOptions = "MaxiMall.FurnitureEditorHelper.GetSinkOptions"))
    TArray<FName> AllowedSinkIDs;

    // ── Faucet ────────────────────────────────────────────────────────────

    /** Faucet allowed model IDs from shared catalog. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Faucet", meta = (GetOptions = "MaxiMall.FurnitureEditorHelper.GetFaucetOptions"))
    TArray<FName> AllowedFaucetIDs;

    // ── Mirror ────────────────────────────────────────────────────────────

    /** Mirror allowed model IDs from shared catalog. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Mirror", meta = (GetOptions = "MaxiMall.FurnitureEditorHelper.GetMirrorOptions"))
    TArray<FName> AllowedMirrorIDs;

    // ── Color Catalog (RAL / NCS) Capabilities ────────────────────────────

    /** Enable RAL/NCS Color Catalog for Cabinet (applies to both Cabinet body and Doors) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Color Catalog")
    bool bAllowCabinetColorCatalog = false;

    /** Enable RAL/NCS Color Catalog for Closet */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Color Catalog")
    bool bAllowClosetColorCatalog = false;

    /** Enable RAL/NCS Color Catalog for Countertop */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Color Catalog")
    bool bAllowCountertopColorCatalog = false;

    /** Enable RAL/NCS Color Catalog for Sink */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Color Catalog")
    bool bAllowSinkColorCatalog = false;

    /** Enable RAL/NCS Color Catalog for Faucet */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Color Catalog")
    bool bAllowFaucetColorCatalog = false;

    /** Enable RAL/NCS Color Catalog for Mirror */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Product | Color Catalog")
    bool bAllowMirrorColorCatalog = false;
};

UCLASS()
class AWSTUTORIAL_API UFurnitureEditorHelper : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION()
    static TArray<FString> GetCountertopOptions();

    UFUNCTION()
    static TArray<FString> GetSinkOptions();

    UFUNCTION()
    static TArray<FString> GetFaucetOptions();

    UFUNCTION()
    static TArray<FString> GetMirrorOptions();
};
