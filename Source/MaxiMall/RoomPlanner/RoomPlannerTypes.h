// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
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
};

USTRUCT(BlueprintType)
struct FWallSegment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	int32 SegmentID = 0;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FString FloorMaterialID = TEXT("DefaultFloor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FString CeilingMaterialID = TEXT("DefaultCeiling");
};

USTRUCT(BlueprintType)
struct FPlacedFurnitureData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner")
	FString InstanceID;

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
};
