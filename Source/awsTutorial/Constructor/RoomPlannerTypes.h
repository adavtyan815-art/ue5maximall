// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "RoomPlannerTypes.generated.h"

class UMaterialInterface;

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
	CabinetSet  UMETA(DisplayName = "Cabinet set"),
	Ceiling     UMETA(DisplayName = "Ceiling"),
	Baseboard   UMETA(DisplayName = "Baseboard")
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

	/** Look of the door / window / archway (PlannerOpeningStyles ID, serialized as "style"). None = the type's default style. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FName Style;

	/** Finishing of the trim (lining, casing, window frame, sill board), serialized as "trim". Unset = the style's colours (REQ-13). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FSurfaceFinish TrimFinish;
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

	/**
	 * Applied finishing (paint / tile) of the wall's LEFT face (Normal = (-Dir.Y, Dir.X)), serialized as "finish" (REQ-13).
	 * Layouts saved before per-face finishing stored one finish for the whole wall under this key; it is loaded onto both faces.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FSurfaceFinish Finish;

	/** Applied finishing of the wall's RIGHT face, serialized as "finishRight" (REQ-13). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FSurfaceFinish FinishRight;

	const FSurfaceFinish& GetFaceFinish(bool bLeftFace) const { return bLeftFace ? Finish : FinishRight; }
	FSurfaceFinish& GetFaceFinish(bool bLeftFace) { return bLeftFace ? Finish : FinishRight; }

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

	/** A point inside the room (its centroid when that lies inside): where the room's finishes are anchored. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FVector2D InteriorPoint = FVector2D::ZeroVector;

	/** The clear floor outline, along the inner faces of the room's walls (FloorPolygon runs along their centre lines). AreaM2 is its area. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	TArray<FVector2D> NetFloorPolygon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FString FloorMaterialID = TEXT("DefaultFloor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FString CeilingMaterialID = TEXT("DefaultCeiling");

	/** Ceiling plane height (cm) = tallest wall of this room; drives the automatic ceiling lights. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	float CeilingHeightCm = 280.f;

	/** Applied floor finishing (REQ-13). */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FSurfaceFinish FloorFinish;

	/** Applied ceiling finishing (REQ-13). */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FSurfaceFinish CeilingFinish;

	/** Applied baseboard finishing (REQ-13). */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FSurfaceFinish BaseboardFinish;

	/**
	 * Tile grid frame of this room's floor and ceiling (world XY, cm; derived, not serialized): the grid starts at a corner of the
	 * room's interior wall faces and runs along its longest wall. UVs stay metric (1 UV = 1 m).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FVector2D SurfaceUVOrigin = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FVector2D SurfaceUVAxisU = FVector2D(1.f, 0.f);

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FVector2D SurfaceUVAxisV = FVector2D(0.f, 1.f);
};

/** Room surface (floor / ceiling / baseboard) finish record keyed by room centroid (rooms are re-detected on every rebuild). */
USTRUCT(BlueprintType)
struct FFloorFinishRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FVector2D Anchor = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FSurfaceFinish Finish;
};

/** Where a catalog item was dropped. */
UENUM(BlueprintType)
enum class EPlannerDropTarget : uint8
{
	None    UMETA(DisplayName = "Invalid"),
	Wall    UMETA(DisplayName = "Wall"),
	Floor   UMETA(DisplayName = "Floor")
};

/**
 * Attachment of a placed item to a wall. Empty WallGuid = free (floor) placement.
 * The item's world transform is derived from the wall: position DistanceAlongWallCm from the wall's
 * start node, on the chosen face, pushed out by DepthOffsetCm so its back touches the face, facing away from the wall.
 */
USTRUCT(BlueprintType)
struct FWallAttachment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FString WallGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	float DistanceAlongWallCm = 0.f;

	/** True = the wall's LEFT face (Normal = (-Dir.Y, Dir.X)), false = right face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	bool bLeftSide = true;

	/** Pivot height above the floor (0 = floor-standing). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	float HeightCm = 0.f;

	/** Distance from the wall face to the item pivot along the face normal (measured from the item bounds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	float DepthOffsetCm = 0.f;

	bool IsAttached() const { return !WallGuid.IsEmpty(); }
};

/** Result of resolving a drop position against the plan (2D) or a cursor hit (3D). */
USTRUCT(BlueprintType)
struct FPlannerDropInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	EPlannerDropTarget Target = EPlannerDropTarget::None;

	/** Wall hit (Target == Wall). */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	int32 SegmentID = -1;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	float DistanceAlongWallCm = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	bool bLeftSide = true;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	float HeightCm = 0.f;

	/** Floor hit (Target == Floor): room and world point. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	int32 RoomID = -1;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FVector WorldLocation = FVector::ZeroVector;
};

/** One placed interior object (REQ-17). Serialized into the planner JSON. */
USTRUCT(BlueprintType)
struct FPlacedFurnitureData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FString InstanceID;

	/** Wall attachment; empty WallGuid = floor placement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FWallAttachment WallAttachment;

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

	/** Cabinet sets are wall-only: always attached when placed through the drop flow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FWallAttachment WallAttachment;
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

/**
 * One architectural dimension of the selection (REQ-02/04/06): a line with arrows between two world points, parallel to what it
 * measures, with extension lines from the measured points and the value centred on it. Distances are clear dimensions: measured on
 * the visible wall faces, from the room's inner corners.
 */
USTRUCT(BlueprintType)
struct FPlannerDimensionLine
{
	GENERATED_BODY()

	/**
	 * "length" (a wall, or each wall at a dragged corner), "distLeft" / "distRight" / "width" / "distNeighbor" / "height" / "sill"
	 * (an opening, as in FPlannerDimensionLabel), "heightsPlan" (the opening's plan caption), "gapFront" / "gapBack" / "gapRight" /
	 * "gapLeft" (an object's or cabinet set's gaps to the walls, in its own axes: +X front, +Y right).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FString Key;

	/** Ready-to-display value, e.g. "3.32 м". */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FString Text;

	/** Measured distance in centimetres (= the distance between Start and End). */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	float Value = 0.f;

	/** Arrow tips (world). */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FVector Start = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FVector End = FVector::ZeroVector;

	/** The measured points on the wall face (world); extension lines lead from them to the arrow tips. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FVector StartRef = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FVector EndRef = FVector::ZeroVector;

	/** Measured along Z (opening height, sill): drawn in 3D; it has no length in the top-down plan, which shows bPlanOnly instead. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	bool bVertical = false;

	/**
	 * For the top-down plan only: the vertical values in one caption (no line, Value 0) at Start, placed away from StartRef —
	 * outside the wall, opposite the chain.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	bool bPlanOnly = false;
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
/** One material slot replacement for a planner object mesh (DT_PlannerObjects → Material Overrides). */
USTRUCT(BlueprintType)
struct FPlannerMaterialOverride
{
	GENERATED_BODY()

	/** Material slot on the row's Mesh (0-based, as listed on the static mesh). Out-of-range indices are ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Override", meta = (ClampMin = "0"))
	int32 SlotIndex = 0;

	/** Material assigned to that slot when the object is spawned. Empty = slot keeps the mesh's own material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Override")
	TSoftObjectPtr<UMaterialInterface> Material;
};

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

	/**
	 * Per-slot material replacements applied to Mesh when the object is spawned (empty = mesh materials as
	 * authored). Each element: Slot Index + Material. Invalid slots or unloadable materials are skipped.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Object")
	TArray<FPlannerMaterialOverride> MaterialOverrides;
};

/** One static-mesh part of a cabinet set: mesh + relative transform to its parent component (see FCabinetSetLayoutRow). */
USTRUCT(BlueprintType)
struct FCabinetSetPartData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	TSoftObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	FVector RelativeLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	FRotator RelativeRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part")
	FVector RelativeScale3D = FVector::OneVector;
};

/**
 * Row of DT_CabinetSetLayouts. RowName = ProductID (same as DT_FurnitureCatalog).
 * Static-mesh parts of a cabinet set spawned from the planner catalog, with their relative transforms.
 * Parent hierarchy is fixed by AShowroomBooth:
 *   MainCabinet -> BoothRoot; DoorMeshSlot0/1, CountertopMesh, SinkMesh, FaucetMesh -> MainCabinet;
 *   MirrorMesh, ClosetMesh -> BoothRoot; ClosetDoorMeshSlot0/1 -> ClosetMesh.
 * A part's Mesh left empty keeps the product catalog mesh; transforms are always applied.
 */
USTRUCT(BlueprintType)
struct FCabinetSetLayoutRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cabinet Set Layout")
	FCabinetSetPartData MainCabinet;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cabinet Set Layout")
	FCabinetSetPartData DoorMeshSlot0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cabinet Set Layout")
	FCabinetSetPartData DoorMeshSlot1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cabinet Set Layout")
	FCabinetSetPartData CountertopMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cabinet Set Layout")
	FCabinetSetPartData SinkMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cabinet Set Layout")
	FCabinetSetPartData FaucetMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cabinet Set Layout")
	FCabinetSetPartData MirrorMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cabinet Set Layout")
	FCabinetSetPartData ClosetMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cabinet Set Layout")
	FCabinetSetPartData ClosetDoorMeshSlot0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cabinet Set Layout")
	FCabinetSetPartData ClosetDoorMeshSlot1;

	/** Whole set: world-space Z of the booth actor, applied once when the set is spawned from the planner catalog. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cabinet Set Layout")
	float WorldLocationZ = 0.f;

	/** Whole set: additional yaw (degrees) ADDED to the booth actor's placement rotation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cabinet Set Layout")
	float RotationZ = 0.f;
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

/**
 * Settings of the Room Planner's per-room ceiling light (APlannerRoomLightActor): ONE rect light per room whose
 * emission is masked to the room's panel polygon, plus the visible emissive panel of the same polygon.
 * Editable on ARoomPlannerManager (RoomLightSettings) and overridable from the player controller
 * (BP_MaxiMallPlayerController → Planner Room Light Settings).
 */
USTRUCT(BlueprintType)
struct FPlannerRoomLightSettings
{
	GENERATED_BODY()

	/** Master switch: off = no room lights (existing ones are destroyed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Light")
	bool bEnabled = true;

	// ── Panel (visible surface, same polygon as the light) ──

	/** Show the emissive panel. The light works without it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Light|Panel")
	bool bShowSurface = true;

	/** Colour of the luminous panel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Light|Panel")
	FLinearColor LightColor = FLinearColor(1.f, 0.98f, 0.94f, 1.f);

	/**
	 * Multiplier on the panel's physical luminance. 1 = the luminance a diffuse panel emitting the light's flux
	 * really has (flux / (π × panel area)); a 4 × 4 m room gives ≈ 300 cd/m², clearly glowing at EV100 6.8.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Light|Panel", meta = (ClampMin = "0"))
	float EmissiveIntensity = 1.f;

	/** Inset of the panel polygon (and of the light's emitting shape) from the walls (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Light|Panel", meta = (ClampMin = "0"))
	float PolygonInsetCm = 45.f;

	/** Distance of the luminous face below the room's ceiling (cm); the light sits 1 cm under it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Light|Panel", meta = (ClampMin = "0.5"))
	float CeilingOffsetCm = 4.f;

	/** Panel slab thickness (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Light|Panel", meta = (ClampMin = "0.5"))
	float SurfaceThicknessCm = 3.f;

	/**
	 * Panel material. Empty = engine EmissiveTexturedMaterial (opaque unlit) fed with a 1×1 HDR float texture.
	 * A project material receives the HDR colour (cd/m², alpha 1) in the vector parameters "Color" and "EmissiveColor"
	 * and the same value as the texture parameter "Texture".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Light|Panel")
	TObjectPtr<UMaterialInterface> SurfaceMaterial;

	// ── Light (one rect light per room) ──

	/** Luminous flux budget per m² of room floor (lumens). 280 is the value the fixed planner exposure (EV100 6.8) was tuned with. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Light|Light", meta = (ClampMin = "0"))
	float LumensPerM2 = 280.f;

	/** Global multiplier on the flux (planner.CeilingLightScale). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Light|Light", meta = (ClampMin = "0"))
	float IntensityScale = 1.f;

	/** Resolution of the polygon mask texture of the light (square, power of two; doubled automatically for concave rooms that fill little of their rectangle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Light|Light", meta = (ClampMin = "64", ClampMax = "1024"))
	int32 MaskResolution = 256;

	/** Colour temperature of the light (K); 5200 = neutral white. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Light|Light", meta = (ClampMin = "1700", ClampMax = "12000"))
	float TemperatureK = 5200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Light|Light")
	bool bCastShadows = true;

	/**
	 * Trace the light's shadows with hardware ray tracing: they sample the real light rectangle. Virtual shadow maps
	 * treat a rect light as a disk of radius SourceWidth/2 around its centre, which is far too soft for a room-sized
	 * panel and invalid for receivers closer than that radius. Ray-traced shadow rays ignore the mask, so the intensity
	 * of non-rectangular rooms is compensated by the estimated share of the rectangle inside the room.
	 * Without ray tracing the engine falls back to shadow maps.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Light|Light")
	bool bRayTracedShadows = true;

	/** Contribution to Lumen GI (1 = physical). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Light|Light", meta = (ClampMin = "0", ClampMax = "2"))
	float IndirectIntensity = 1.f;

	/** Specular highlight scale of the light on glossy finishes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Light|Light", meta = (ClampMin = "0", ClampMax = "1"))
	float SpecularScale = 0.6f;
};
