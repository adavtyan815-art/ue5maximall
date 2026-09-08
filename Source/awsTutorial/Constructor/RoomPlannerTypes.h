// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "RoomPlannerTypes.generated.h"

UENUM(BlueprintType)
enum class EPlannerToolMode : uint8
{
	Select           UMETA(DisplayName = "Select & Move"),
	DrawWall         UMETA(DisplayName = "Draw Wall"),
	PlaceDoor        UMETA(DisplayName = "Place Door"),
	PlaceWindow      UMETA(DisplayName = "Place Window"),
	PlaceFurniture   UMETA(DisplayName = "Place Furniture"),
	ApplyMaterial    UMETA(DisplayName = "Apply Material"),
	Erase            UMETA(DisplayName = "Erase")
};

UENUM(BlueprintType)
enum class EPlannerViewMode : uint8
{
	View2D_TopDown      UMETA(DisplayName = "2D Top-Down View"),
	View3D_Perspective  UMETA(DisplayName = "3D Perspective View")
};

UENUM(BlueprintType)
enum class EOpeningType : uint8
{
	Door     UMETA(DisplayName = "Door"),
	Window   UMETA(DisplayName = "Window"),
	Archway  UMETA(DisplayName = "Archway")
};

/** Hinge side of a door / window leaf, seen from INSIDE the room looking at the wall. */
UENUM(BlueprintType)
enum class EOpeningSwingSide : uint8
{
	Left     UMETA(DisplayName = "Left (hinge on the left)"),
	Right    UMETA(DisplayName = "Right (hinge on the right)")
};

/** Direction a door / window leaf opens: into the room or out of it. */
UENUM(BlueprintType)
enum class EOpeningSwingDirection : uint8
{
	Inward   UMETA(DisplayName = "Inward (opens into the room)"),
	Outward  UMETA(DisplayName = "Outward (opens out of the room)")
};

/** Kind of finishing applied to a wall or floor surface. */
UENUM(BlueprintType)
enum class ESurfaceFinishType : uint8
{
	None     UMETA(DisplayName = "None"),
	Paint    UMETA(DisplayName = "Paint (RAL / NCS)"),
	Tile     UMETA(DisplayName = "Tile")
};

/** Which kind of planner element is currently selected. */
UENUM(BlueprintType)
enum class EPlannerSelectionKind : uint8
{
	None        UMETA(DisplayName = "Nothing"),
	Wall        UMETA(DisplayName = "Wall"),
	Opening     UMETA(DisplayName = "Door / Window"),
	Floor       UMETA(DisplayName = "Floor"),
	Object      UMETA(DisplayName = "Interior object"),
	CabinetSet  UMETA(DisplayName = "Cabinet set")
};

/** What a pending click-to-place operation will create. */
UENUM(BlueprintType)
enum class EPlannerPlacementKind : uint8
{
	None        UMETA(DisplayName = "None"),
	Object      UMETA(DisplayName = "Interior object"),
	CabinetSet  UMETA(DisplayName = "Cabinet set")
};

/**
 * A surface finish (paint colour from the RAL/NCS catalog, or a tile from the tile catalog).
 * Stored on walls, floors and placed objects; serialized into the planner JSON so it
 * replicates to every client and is saved / loaded with the project.
 */
USTRUCT(BlueprintType)
struct FSurfaceFinish
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Finish")
	ESurfaceFinishType Type = ESurfaceFinishType::None;

	/** Catalog code, e.g. "RAL 3020" or "S 1080-Y90R". Empty for tiles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Finish")
	FString ColorCode;

	/** Linear colour applied to the material's BaseColor parameter (paint) or tint (tile). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Finish")
	FLinearColor Color = FLinearColor::White;

	/** Row name in the tile catalog DataTable (DT_PlannerTiles) or a material asset path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Finish")
	FString TileAssetID;

	/** Physical tile edge length in cm (drives the material's TilesPerMeter parameter). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Finish")
	float TileSizeCm = 30.f;

	bool IsSet() const { return Type != ESurfaceFinishType::None; }

	/** Key used to group areas per finish (REQ-14). */
	FString GetKey() const
	{
		switch (Type)
		{
		case ESurfaceFinishType::Paint: return ColorCode.IsEmpty() ? Color.ToFColor(true).ToHex() : ColorCode;
		case ESurfaceFinishType::Tile:  return TileAssetID;
		default: return FString();
		}
	}

	bool operator==(const FSurfaceFinish& Other) const
	{
		return Type == Other.Type && ColorCode == Other.ColorCode && TileAssetID == Other.TileAssetID
			&& Color.Equals(Other.Color, 0.001f) && FMath::IsNearlyEqual(TileSizeCm, Other.TileSizeCm, 0.01f);
	}
	bool operator!=(const FSurfaceFinish& Other) const { return !(*this == Other); }
};

USTRUCT(BlueprintType)
struct FWallNode
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	int32 NodeID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FVector2D Position = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	TArray<int32> ConnectedSegmentIDs;
};

USTRUCT(BlueprintType)
struct FWallOpening
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FString OpeningID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	EOpeningType Type = EOpeningType::Door;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	float DistanceFromStart = 100.f; // Distance in cm along wall segment center line

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	float Width = 90.f; // Opening width in cm

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	float Height = 210.f; // Opening height in cm

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	float SillHeight = 0.f; // Height off floor in cm (0 for doors, ~90 for windows)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FString AssetID;

	/** Hinge side, seen from inside the room (REQ-07). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	EOpeningSwingSide SwingSide = EOpeningSwingSide::Left;

	/** Opens into the room or out of it (REQ-07). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	EOpeningSwingDirection SwingDirection = EOpeningSwingDirection::Inward;
};

USTRUCT(BlueprintType)
struct FWallSegment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	int32 SegmentID = 0;

	/** Stable identity that survives JSON re-import (segment IDs are renumbered on import). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FString WallGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	int32 StartNodeID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	int32 EndNodeID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	float Thickness = 20.f; // Default 20cm thickness

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	float Height = 280.f; // Default 2.8m wall height

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FString MaterialID = TEXT("DefaultWall");

	/** Applied finishing (paint / tile) for the whole wall (REQ-13). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FSurfaceFinish Finish;

	/** True when the wall's LEFT face (Normal = (-Dir.Y, Dir.X)) faces a detected room interior. Derived, not serialized. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	bool bLeftSideIsInterior = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	TArray<FWallOpening> Openings;
};

USTRUCT(BlueprintType)
struct FRoomData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	int32 RoomID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	TArray<int32> WallSegmentIDs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	TArray<FVector2D> FloorPolygon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	float AreaM2 = 0.f;

	/** Polygon centroid, used to re-associate floor finishes after every rebuild. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FVector2D Centroid = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FString FloorMaterialID = TEXT("DefaultFloor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FString CeilingMaterialID = TEXT("DefaultCeiling");

	/** Applied floor finishing (REQ-13). */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FSurfaceFinish FloorFinish;
};

/** Floor finish record keyed by room centroid (rooms are re-detected on every rebuild). */
USTRUCT(BlueprintType)
struct FFloorFinishRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FVector2D Anchor = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FSurfaceFinish Finish;
};

/** One placed interior object (REQ-17). Serialized into the planner JSON. */
USTRUCT(BlueprintType)
struct FPlacedFurnitureData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FString InstanceID;

	/** Row name in the object catalog (DT_PlannerObjects) or a static mesh asset path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FString AssetID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FVector Scale = FVector::OneVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FString CustomMaterialID;

	/** Optional RAL/NCS colour applied to the object's materials. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FSurfaceFinish Finish;
};

/** One placed cabinet set (REQ-18) — an AShowroomBooth spawned by the planner. */
USTRUCT(BlueprintType)
struct FPlacedCabinetSetData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FString InstanceID;

	/** RowName in DT_FurnitureCatalog (FFurnitureProductRow). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FName ProductID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FRotator Rotation = FRotator::ZeroRotator;
};

/** A dimension / distance label to draw next to the selected element (REQ-02/04/06). */
USTRUCT(BlueprintType)
struct FPlannerDimensionLabel
{
	GENERATED_BODY()

	/** Stable key: "length", "width", "height", "sill", "distLeft", "distRight", "distFloor", "distNeighbor", "area", "size". */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FString Key;

	/** Ready-to-display text, e.g. "2.45 м". */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FString Text;

	/** Raw value in centimetres (or m² for area labels). */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	float Value = 0.f;

	/** World position the label belongs to. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FVector WorldLocation = FVector::ZeroVector;

	/** Screen position (viewport units, DPI-corrected). Filled by URoomPlannerWidget. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FVector2D ScreenPosition = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	bool bOnScreen = false;
};

/** Finishing area per finish type / catalog entry (REQ-14). */
USTRUCT(BlueprintType)
struct FFinishAreaEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	ESurfaceFinishType Type = ESurfaceFinishType::None;

	/** Colour code or tile id. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FString FinishKey;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	float AreaM2 = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	int32 SurfaceCount = 0;
};

/** Row of the tile catalog DataTable (DT_PlannerTiles). */
USTRUCT(BlueprintType)
struct FPlannerTileRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
	TSoftObjectPtr<UTexture2D> Thumbnail;

	/** Material with BaseColor (vector) and TilesPerMeter (scalar) parameters. Falls back to the manager's TileMaterial. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
	TSoftObjectPtr<UMaterialInterface> Material;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
	float TileSizeCm = 30.f;

	/** Tint / average colour (used when the material exposes BaseColor and for the 2D plan). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
	FLinearColor AverageColor = FLinearColor::White;
};

/** Row of the interior object catalog DataTable (DT_PlannerObjects). */
USTRUCT(BlueprintType)
struct FPlannerObjectRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Object")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Object")
	TSoftObjectPtr<UTexture2D> Thumbnail;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Object")
	TSoftObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Object")
	FString Category;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Object")
	FVector DefaultScale = FVector::OneVector;

	/** If true the RAL/NCS colour catalog may recolour this object. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Object")
	bool bAllowColorCatalog = true;
};

/** Catalog entry exposed to the UI for building object / tile / cabinet-set lists. */
USTRUCT(BlueprintType)
struct FPlannerCatalogEntry
{
	GENERATED_BODY()

	/** Object AssetID, tile id, or cabinet-set ProductID (as string). */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FString ID;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	TSoftObjectPtr<UTexture2D> Thumbnail;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FString Category;

	/** Tiles only: tile size in cm. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	float TileSizeCm = 0.f;

	/** Tiles only: representative colour. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FLinearColor Color = FLinearColor::White;
};
