// Copyright 2026 MaxiMall. All Rights Reserved.

#include "Constructor/RoomPlannerManager.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "UObject/UObjectIterator.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "MaterialDomain.h"
#include "Engine/EngineTypes.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "JsonObjectConverter.h"
#include "PixelStreamingInputComponent.h"
#include "Constructor/PlannerPlacedObjectActor.h"
#include "Constructor/PlannerRoomLightActor.h"
#include "FurnitureConfigurator/ShowroomBooth.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/HitResult.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/World.h"
#include "Interfaces/Interface_PostProcessVolume.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Constructor/PlannerOpeningBuilder.h"
#include "Constructor/PlannerOpeningStyles.h"
#include "Constructor/PlannerDimensions.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/IConsoleManager.h"
#include "awsTutorial_PlayerController.h"

namespace PlannerJsonKeys
{
	static const TCHAR* FinishTypeToString(ESurfaceFinishType Type)
	{
		switch (Type)
		{
		case ESurfaceFinishType::Paint: return TEXT("paint");
		case ESurfaceFinishType::Tile:  return TEXT("tile");
		default: return TEXT("none");
		}
	}
	static ESurfaceFinishType FinishTypeFromString(const FString& S)
	{
		if (S.Equals(TEXT("paint"), ESearchCase::IgnoreCase)) return ESurfaceFinishType::Paint;
		if (S.Equals(TEXT("tile"), ESearchCase::IgnoreCase)) return ESurfaceFinishType::Tile;
		return ESurfaceFinishType::None;
	}
	static FString NewInstanceID()
	{
		return FGuid::NewGuid().ToString(EGuidFormats::Short);
	}
	static FString FormatMeters(float Cm)
	{
		return FString::Printf(TEXT("%.2f м"), Cm / 100.f);
	}

	/**
	 * Condensed, and doubles written only as long as they mean anything. The stock policy spells a double out to 17
	 * significant digits, so one dragged corner reaches every client as "133.33000000000001" in a string that is
	 * re-serialized on every mutation.
	 */
	template <class CharType>
	struct TShortDoublePrintPolicy : public TCondensedJsonPrintPolicy<CharType>
	{
		static inline void WriteDouble(FArchive* Stream, double Value)
		{
			// A non-finite coordinate would be written as "nan" and make the whole layout unparseable for every client. Writing
			// 0 keeps the rest of the layout readable, but it silently moves something to the origin, so it is worth saying.
			if (!FMath::IsFinite(Value))
			{
				UE_LOG(LogTemp, Error, TEXT("[Planner] Non-finite value in the layout JSON, written as 0."));
				TJsonPrintPolicy<CharType>::WriteString(Stream, FString(TEXT("0")));
				return;
			}
			TJsonPrintPolicy<CharType>::WriteString(Stream, FString::SanitizeFloat(Value, 0));
		}

		/** The float path is inherited from TJsonPrintPolicy and would print %g, i.e. 6 significant digits. */
		static inline void WriteFloat(FArchive* Stream, float Value)
		{
			WriteDouble(Stream, (double)Value);
		}
	};
	using FLayoutJsonWriter = TJsonWriter<TCHAR, TShortDoublePrintPolicy<TCHAR>>;
	using FLayoutJsonWriterFactory = TJsonWriterFactory<TCHAR, TShortDoublePrintPolicy<TCHAR>>;

	/** A measured value carries more digits than it means; the ones past this only lengthen the string. */
	static void SetRounded(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, double Value, int32 Decimals)
	{
		const double Scale = FMath::Pow(10.0, (double)Decimals);
		Obj->SetNumberField(Field, FMath::RoundToDouble(Value * Scale) / Scale);
	}
	/** Centimetres: 0.01 cm is a tenth of a millimetre, finer than the planner draws or snaps to. */
	static void SetCm(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, double Cm) { SetRounded(Obj, Field, Cm, 2); }
	/** Degrees. */
	static void SetDegrees(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, double Degrees) { SetRounded(Obj, Field, Degrees, 3); }
	/** Colour components and scale factors: FSurfaceFinish compares colours with a 0.001 tolerance, so 0.0001 is safe. */
	static void SetUnit(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, double Value) { SetRounded(Obj, Field, Value, 4); }
}

ARoomPlannerManager::ARoomPlannerManager()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	AActor::SetReplicateMovement(false);
	NetCullDistanceSquared = 0.0f;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	FloorProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FloorProceduralMesh"));
	FloorProceduralMesh->SetupAttachment(SceneRoot);
	FloorProceduralMesh->bUseAsyncCooking = true;
	FloorProceduralMesh->bUseComplexAsSimpleCollision = true;
	FloorProceduralMesh->SetCastShadow(false);
	FloorProceduralMesh->SetAbsolute(true, true, true);

	CeilingProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("CeilingProceduralMesh"));
	CeilingProceduralMesh->SetupAttachment(SceneRoot);
	CeilingProceduralMesh->bUseAsyncCooking = true;
	CeilingProceduralMesh->bUseComplexAsSimpleCollision = true;
	CeilingProceduralMesh->SetCastShadow(true);
	CeilingProceduralMesh->SetAbsolute(true, true, true);

	BaseboardProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BaseboardProceduralMesh"));
	BaseboardProceduralMesh->SetupAttachment(SceneRoot);
	BaseboardProceduralMesh->bUseAsyncCooking = true;
	BaseboardProceduralMesh->bUseComplexAsSimpleCollision = true;
	BaseboardProceduralMesh->SetCastShadow(false);
	BaseboardProceduralMesh->SetAbsolute(true, true, true);
	// Only Visibility traces (3D click) see baseboards, so they can be picked for finishing (REQ-13); nothing collides with them.
	BaseboardProceduralMesh->SetCollisionObjectType(ECC_WorldDynamic);
	BaseboardProceduralMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	BaseboardProceduralMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	BaseboardProceduralMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

	NodeHandleMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("NodeHandleMesh"));
	NodeHandleMesh->SetupAttachment(SceneRoot);
	NodeHandleMesh->bUseAsyncCooking = false;
	NodeHandleMesh->SetCastShadow(false);
	NodeHandleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NodeHandleMesh->SetAbsolute(true, true, true);
	NodeHandleMesh->SetVisibility(false);

	// Exterior view behind doors / windows (3D only): unlit, not a light, invisible to shadows, GI, ray tracing and captures.
	auto CreateExteriorMesh = [this](const TCHAR* Name)
	{
		UProceduralMeshComponent* Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(Name);
		Mesh->SetupAttachment(SceneRoot);
		Mesh->SetAbsolute(true, true, true);
		Mesh->bUseAsyncCooking = true;
		Mesh->SetCastShadow(false);
		Mesh->bVisibleInRayTracing = false;
		Mesh->bAffectDynamicIndirectLighting = false;
		Mesh->bAffectDistanceFieldLighting = false;
		Mesh->bVisibleInReflectionCaptures = false;
		Mesh->bVisibleInRealTimeSkyCaptures = false;
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetVisibility(false);
		return Mesh;
	};
	ExteriorSkyMesh = CreateExteriorMesh(TEXT("ExteriorSkyMesh"));
	ExteriorGroundMesh = CreateExteriorMesh(TEXT("ExteriorGroundMesh"));

	// Planner exposure override (see header): disabled until a planner session is open in 3D.
	PlannerExposure = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PlannerExposure"));
	PlannerExposure->SetupAttachment(SceneRoot);
	PlannerExposure->bUnbound = true;
	PlannerExposure->Priority = 10.f;
	PlannerExposure->BlendWeight = 1.f;
	PlannerExposure->bEnabled = false;
	{
		FPostProcessSettings& PP = PlannerExposure->Settings;
		PP.bOverride_AutoExposureMethod = true;                     PP.AutoExposureMethod = AEM_Histogram;
		PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true; PP.AutoExposureApplyPhysicalCameraExposure = false;
		PP.bOverride_AutoExposureMinBrightness = true;              PP.AutoExposureMinBrightness = PlannerExposureMinEV100;
		PP.bOverride_AutoExposureMaxBrightness = true;              PP.AutoExposureMaxBrightness = PlannerExposureMaxEV100;
		PP.bOverride_AutoExposureBias = true;                       PP.AutoExposureBias = PlannerExposureBias;
		// Min == Max EV100 → fixed exposure; the speeds only matter if the range is widened later.
		PP.bOverride_AutoExposureSpeedUp = true;                    PP.AutoExposureSpeedUp = 4.f;
		PP.bOverride_AutoExposureSpeedDown = true;                  PP.AutoExposureSpeedDown = 2.f;
	}

	PlacedObjectActorClass = APlannerPlacedObjectActor::StaticClass();

	WallSelectionMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RoomPlanner/Materials/M_WallSelection.M_WallSelection"));
	if (!WallSelectionMaterial)
	{
		WallSelectionMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Constructor/Materials/M_WallSelection.M_WallSelection"));
	}

	OpeningSelectionMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RoomPlanner/Materials/M_OpeningSelection.M_OpeningSelection"));
	if (!OpeningSelectionMaterial)
	{
		OpeningSelectionMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Constructor/Materials/M_OpeningSelection.M_OpeningSelection"));
	}

	// REQ-13 finishing assets. The resolvers keep their path fallbacks for instances that clear these slots.
	PaintMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RoomPlanner/Materials/M_PlannerPaint.M_PlannerPaint"));
	TileMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RoomPlanner/Materials/M_PlannerTile.M_PlannerTile"));
	TileCatalog = LoadObject<UDataTable>(nullptr, TEXT("/Game/DT/DT_PlannerTiles.DT_PlannerTiles"));

	bCeilingVisible = true;
}

void ARoomPlannerManager::BeginPlay()
{
	Super::BeginPlay();

	SetActorLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	SetActorScale3D(FVector(1.f, 1.f, 1.f));

	UE_LOG(LogTemp, Warning, TEXT("[MaxiMallConstructor] ARoomPlannerManager BeginPlay"));
}

void ARoomPlannerManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (ActiveToolMode == EPlannerToolMode::Select)
	{
		// We no longer need to track drag state here since openings stay selected on click!
		bWasDraggingOpening = false;
	}

	TickLocalNodeDrag();
	TickOpeningDragEnd(); // an opening drag that stopped without a release is published and its cooking resumed here
	EnsureLayoutCollision(); // a drag that skipped collision cooking is made good here, however it ended

	// Room lights follow room changes lazily and only when they can be seen (see bRoomLightsDirty). Direct edits of
	// RoomLightSettings (Details panel, Blueprint) are noticed through the settings hash.
	if (APlannerRoomLightActor::ComputeSettingsHash(RoomLightSettings) != AppliedRoomLightSettingsHash)
	{
		bRoomLightsDirty = true;
	}
	if (bRoomLightsDirty && !b2DViewMode)
	{
		RebuildRoomLights();
	}
	// Exterior view: rebuild when dirty and needed, hide it once the layout no longer qualifies (e.g. cleared while in 3D),
	// and follow the camera in and out of the enclosure.
	{
		const bool bShouldShow = ShouldShowExteriorBackdrop();
		const bool bCurrentlyVisible = (ExteriorSkyMesh && ExteriorSkyMesh->GetVisibleFlag()) || (ExteriorGroundMesh && ExteriorGroundMesh->GetVisibleFlag());
		const bool bWantVisible = bShouldShow && IsCameraInsideExteriorBackdrop();
		if ((bExteriorBackdropDirty && (bShouldShow || bCurrentlyVisible)) || (!bExteriorBackdropDirty && bCurrentlyVisible != bWantVisible))
		{
			UpdateExteriorBackdropVisibility();
		}
	}
	TickLeafAnimations(DeltaTime);

	if (!BoundPSInput.IsValid())
	{
		for (TObjectIterator<UPixelStreamingInput> It; It; ++It)
		{
			if (UPixelStreamingInput* PSInput = *It)
			{
				PSInput->OnInputEvent.AddUniqueDynamic(this, &ARoomPlannerManager::OnPixelStreamingInputReceived);
				BoundPSInput = PSInput;
				UE_LOG(LogTemp, Warning, TEXT("[MaxiMallConstructor] ARoomPlannerManager bound directly to UPixelStreamingInput %s via TObjectIterator!"), *PSInput->GetName());
				break;
			}
		}
	}
}

void ARoomPlannerManager::OnPixelStreamingInputReceived(const FString& Descriptor)
{
	UE_LOG(LogTemp, Warning, TEXT("[MaxiMallConstructor] OnPixelStreamingInputReceived raw Descriptor: %s"), *Descriptor);

	FString CleanDescriptor = Descriptor;
	CleanDescriptor.ReplaceInline(TEXT("\0"), TEXT(""));
	if (CleanDescriptor.StartsWith(TEXT("UIInteraction:")))
	{
		CleanDescriptor = CleanDescriptor.Mid(14).TrimStart();
	}
	else if (CleanDescriptor.StartsWith(TEXT("UIInteraction")))
	{
		CleanDescriptor = CleanDescriptor.Mid(13).TrimStart();
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CleanDescriptor);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return;
	}

	FString EffectiveJSON = Descriptor;
	if (JsonObject->HasField(TEXT("descriptor")))
	{
		EffectiveJSON = JsonObject->GetStringField(TEXT("descriptor"));
		TSharedPtr<FJsonObject> InnerObject;
		TSharedRef<TJsonReader<>> InnerReader = TJsonReaderFactory<>::Create(EffectiveJSON);
		if (FJsonSerializer::Deserialize(InnerReader, InnerObject) && InnerObject.IsValid())
		{
			JsonObject = InnerObject;
		}
	}
	else if (JsonObject->HasField(TEXT("Descriptor")))
	{
		EffectiveJSON = JsonObject->GetStringField(TEXT("Descriptor"));
		TSharedPtr<FJsonObject> InnerObject;
		TSharedRef<TJsonReader<>> InnerReader = TJsonReaderFactory<>::Create(EffectiveJSON);
		if (FJsonSerializer::Deserialize(InnerReader, InnerObject) && InnerObject.IsValid())
		{
			JsonObject = InnerObject;
		}
	}

	if (JsonObject->HasField(TEXT("cmd")) || JsonObject->HasField(TEXT("Cmd")))
	{
		FString CmdVal = JsonObject->HasField(TEXT("cmd")) ? JsonObject->GetStringField(TEXT("cmd")) : JsonObject->GetStringField(TEXT("Cmd"));
		if (CmdVal.StartsWith(TEXT("add_wall")) || CmdVal.StartsWith(TEXT("add_opening")) || CmdVal.StartsWith(TEXT("clear")) || CmdVal.StartsWith(TEXT("get_state")))
		{
			UE_LOG(LogTemp, Warning, TEXT("[MaxiMallConstructor] OnPixelStreamingInputReceived matched cmd: %s"), *CmdVal);
			FString Response = ProcessCommandJSON(EffectiveJSON);
			if (BoundPSInput.IsValid())
			{
				if (UPixelStreamingInput* PSInput = Cast<UPixelStreamingInput>(BoundPSInput.Get()))
				{
					PSInput->SendPixelStreamingResponse(FString::Printf(TEXT("MaxiMallConstructor:%s"), *Response));
				}
			}
		}
	}
}

ARoomPlannerManager* ARoomPlannerManager::GetOrCreateInstance(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<ARoomPlannerManager> It(World); It; ++It)
	{
		return *It;
	}

	if (World->GetNetMode() != NM_Client)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ARoomPlannerManager>(ARoomPlannerManager::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	}

	return nullptr;
}

int32 ARoomPlannerManager::AddNode(const FVector2D& Position)
{
	return GetOrCreateNodeAtPosition(Position, 25.f, 20.f);
}

int32 ARoomPlannerManager::GetOrCreateNodeAtPosition(const FVector2D& Position, float NodeSnapRadiusCm, float WallSnapRadiusCm)
{
	// 1. Check for snapping to existing node
	float BestNodeDistSq = NodeSnapRadiusCm * NodeSnapRadiusCm;
	int32 ClosestNodeID = INDEX_NONE;
	for (const TPair<int32, FWallNode>& Pair : Nodes)
	{
		float DistSq = FVector2D::DistSquared(Pair.Value.Position, Position);
		if (DistSq <= BestNodeDistSq)
		{
			BestNodeDistSq = DistSq;
			ClosestNodeID = Pair.Key;
		}
	}
	if (ClosestNodeID != INDEX_NONE)
	{
		return ClosestNodeID;
	}

	// 2. Check for mid-wall T-junction intersection along existing wall segments
	float BestWallDist = WallSnapRadiusCm;
	int32 TargetSegID = INDEX_NONE;
	FVector2D TargetSplitPoint = Position;

	for (const TPair<int32, FWallSegment>& Pair : WallSegments)
	{
		const FWallSegment& Seg = Pair.Value;
		if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
		{
			FVector2D P1 = Nodes[Seg.StartNodeID].Position;
			FVector2D P2 = Nodes[Seg.EndNodeID].Position;
			FVector2D SegVec = P2 - P1;
			float SegLen = SegVec.Size();
			if (SegLen < 1.0f) continue;

			FVector2D SegDir = SegVec / SegLen;
			float t = FVector2D::DotProduct(Position - P1, SegDir);

			// Keep at least 20cm away from endpoints to avoid microscopic slivers
			if (t >= 20.f && t <= (SegLen - 20.f))
			{
				FVector2D ProjPoint = P1 + SegDir * t;
				float DistToCenterline = FVector2D::Distance(Position, ProjPoint);
				float MaxSnapDist = (Seg.Thickness * 0.5f) + WallSnapRadiusCm;

				if (DistToCenterline <= MaxSnapDist && DistToCenterline < BestWallDist)
				{
					BestWallDist = DistToCenterline;
					TargetSegID = Pair.Key;
					TargetSplitPoint = ProjPoint;
				}
			}
		}
	}

	if (TargetSegID != INDEX_NONE)
	{
		return SplitWallSegment(TargetSegID, TargetSplitPoint);
	}

	// 3. Create brand new standalone node
	int32 NewID = NextNodeID++;
	FWallNode Node;
	Node.NodeID = NewID;
	Node.Position = Position;
	Nodes.Add(NewID, Node);
	return NewID;
}

int32 ARoomPlannerManager::SplitWallSegment(int32 SegmentID, const FVector2D& SplitPos)
{
	if (!WallSegments.Contains(SegmentID))
	{
		return INDEX_NONE;
	}

	FWallSegment OldSeg = WallSegments[SegmentID];
	if (!Nodes.Contains(OldSeg.StartNodeID) || !Nodes.Contains(OldSeg.EndNodeID))
	{
		return INDEX_NONE;
	}

	int32 NodeA = OldSeg.StartNodeID;
	int32 NodeB = OldSeg.EndNodeID;
	FVector2D PosA = Nodes[NodeA].Position;
	float SplitDist = FVector2D::Distance(PosA, SplitPos);

	// 1. Create the Junction Node J
	int32 JunctionNodeID = NextNodeID++;
	FWallNode JNode;
	JNode.NodeID = JunctionNodeID;
	JNode.Position = SplitPos;
	Nodes.Add(JunctionNodeID, JNode);

	// 2. Separate existing openings between Segment 1 (A -> J) and Segment 2 (J -> B)
	TArray<FWallOpening> Openings1;
	TArray<FWallOpening> Openings2;

	for (const FWallOpening& Op : OldSeg.Openings)
	{
		if (Op.DistanceFromStart < SplitDist)
		{
			Openings1.Add(Op);
		}
		else
		{
			FWallOpening ShiftedOp = Op;
			ShiftedOp.DistanceFromStart = FMath::Max(10.f, Op.DistanceFromStart - SplitDist);
			Openings2.Add(ShiftedOp);
		}
	}

	// 3. Modify existing segment to become (NodeA -> JunctionNodeID)
	FWallSegment& Seg1 = WallSegments[SegmentID];
	Seg1.EndNodeID = JunctionNodeID;
	Seg1.Openings = Openings1;

	// Update node connectivity
	Nodes[NodeB].ConnectedSegmentIDs.Remove(SegmentID);
	Nodes[JunctionNodeID].ConnectedSegmentIDs.AddUnique(SegmentID);

	// Update actor for Seg1
	if (TObjectPtr<AProceduralWallActor>* Actor1Ptr = WallActors.Find(SegmentID))
	{
		if (*Actor1Ptr)
		{
			(*Actor1Ptr)->WallData = Seg1;
		}
	}

	// 4. Create new segment (JunctionNodeID -> NodeB)
	int32 Seg2ID = NextSegmentID++;
	FWallSegment Seg2;
	Seg2.SegmentID = Seg2ID;
	Seg2.StartNodeID = JunctionNodeID;
	Seg2.EndNodeID = NodeB;
	Seg2.Thickness = OldSeg.Thickness;
	Seg2.Height = OldSeg.Height;
	Seg2.Openings = Openings2;
	Seg2.Finish = OldSeg.Finish;              // a split wall keeps its finish on both halves (REQ-13)
	Seg2.FinishRight = OldSeg.FinishRight;
	Seg2.bLeftSideIsInterior = OldSeg.bLeftSideIsInterior;
	Seg2.WallGuid = PlannerJsonKeys::NewInstanceID();

	// Wall-attached items beyond the split point now belong to the new half.
	RehomeAttachmentsAfterSplit(OldSeg.WallGuid, Seg2.WallGuid, SplitDist);

	WallSegments.Add(Seg2ID, Seg2);
	Nodes[JunctionNodeID].ConnectedSegmentIDs.AddUnique(Seg2ID);
	Nodes[NodeB].ConnectedSegmentIDs.AddUnique(Seg2ID);

	// Spawn actor for Seg2
	if (GetWorld())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		AProceduralWallActor* WallActor2 = GetWorld()->SpawnActor<AProceduralWallActor>(AProceduralWallActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		if (WallActor2)
		{
			WallActor2->WallData = Seg2;
			WallActor2->BaseWallMaterial = DefaultWallMaterial;
			WallActor2->WallSelectionMaterial = WallSelectionMaterial;
			WallActor2->OpeningSelectionMaterial = OpeningSelectionMaterial;
			WallActor2->LeafMaterial = LeafMaterial;
			WallActors.Add(Seg2ID, WallActor2);
		}
	}

	RebuildWallsAndRooms();

	return JunctionNodeID;
}

int32 ARoomPlannerManager::AddWallBetweenPoints(const FVector2D& StartPos, const FVector2D& EndPos, float Thickness, float Height)
{
	const int32 N1 = AddNode(StartPos);
	const int32 N2 = AddNode(EndPos);
	if (N1 == INDEX_NONE || N2 == INDEX_NONE || N1 == N2) return -1;
	const FVector2D A = Nodes[N1].Position;
	const FVector2D B = Nodes[N2].Position;
	const float Len = FVector2D::Distance(A, B);
	if (Len < 1.f) return -1;
	const FVector2D Dir = (B - A) / Len;
	auto Cross = [](const FVector2D& U, const FVector2D& V) { return (double)U.X * V.Y - (double)U.Y * V.X; };

	// Where the new wall must join the plan, by distance along it: a node at each such point.
	TArray<TPair<float, int32>> Stops;
	auto HasStop = [&Stops](int32 NodeID) { return Stops.ContainsByPredicate([NodeID](const TPair<float, int32>& S) { return S.Value == NodeID; }); };

	// 1. Walls it crosses: split there (both walls meet at a new corner).
	struct FCrossing { int32 SegID; FVector2D Point; float T; };
	TArray<FCrossing> Crossings;
	for (const TPair<int32, FWallSegment>& Pair : WallSegments)
	{
		const FWallSegment& Seg = Pair.Value;
		if (Seg.StartNodeID == N1 || Seg.EndNodeID == N1 || Seg.StartNodeID == N2 || Seg.EndNodeID == N2) continue;
		const FWallNode* S0 = Nodes.Find(Seg.StartNodeID);
		const FWallNode* S1 = Nodes.Find(Seg.EndNodeID);
		if (!S0 || !S1) continue;
		const FVector2D Edge = S1->Position - S0->Position;
		const double SegLen = Edge.Size();
		const double Denom = Cross(Dir, Edge);
		if (SegLen < 1.0 || FMath::Abs(Denom) < 1.e-6 * SegLen) continue; // parallel
		const FVector2D ToS0 = S0->Position - A;
		const double T = Cross(ToS0, Edge) / Denom;          // along the new wall
		const double U = Cross(ToS0, Dir) / Denom * SegLen;  // along the crossed wall
		if (T > 1.0 && T < Len - 1.0 && U > 1.0 && U < SegLen - 1.0)
		{
			Crossings.Add({ Pair.Key, A + Dir * (float)T, (float)T });
		}
	}
	for (const FCrossing& Crossing : Crossings)
	{
		const int32 Junction = SplitWallSegment(Crossing.SegID, Crossing.Point);
		if (Junction != INDEX_NONE) Stops.Add(TPair<float, int32>(Crossing.T, Junction));
	}

	// 2. Corners it passes over, and free wall ends stopping against it (drawn onto it, as a corner dragged there would be).
	TArray<int32> NodeIDs;
	Nodes.GetKeys(NodeIDs);
	bool bPulledAny = false;
	for (int32 NodeID : NodeIDs)
	{
		if (NodeID == N1 || NodeID == N2 || HasStop(NodeID)) continue;
		const FWallNode& Node = Nodes[NodeID];
		const float T = FVector2D::DotProduct(Node.Position - A, Dir);
		const float Dist = FVector2D::Distance(Node.Position, A + Dir * T);
		if (T > 1.f && T < Len - 1.f && Dist <= 1.f)
		{
			Stops.Add(TPair<float, int32>(T, NodeID));
			continue;
		}
		if (Node.ConnectedSegmentIDs.Num() != 1 || T < 20.f || T > Len - 20.f || Dist > Thickness * 0.5f + 20.f) continue;
		// Only a wall that runs into the new one (not along it), and not the stub left beyond a crossing just split above.
		const FWallSegment* Own = WallSegments.Find(Node.ConnectedSegmentIDs[0]);
		if (!Own) continue;
		const int32 OtherEnd = (Own->StartNodeID == NodeID) ? Own->EndNodeID : Own->StartNodeID;
		const FWallNode* Other = Nodes.Find(OtherEnd);
		if (!Other || HasStop(OtherEnd)) continue;
		const FVector2D OwnDir = (Node.Position - Other->Position).GetSafeNormal();
		if (FMath::Abs(Cross(Dir, OwnDir)) < 0.5) continue;
		FString Refusal;
		if (!CanMoveNode(NodeID, A + Dir * T, Refusal)) continue; // an optional join: no message when it cannot be made
		if (ApplyNodeMove(NodeID, A + Dir * T, true))
		{
			Stops.Add(TPair<float, int32>(T, NodeID));
			bPulledAny = true;
		}
	}

	// 3. The wall itself, one piece between consecutive stops.
	Stops.Sort([](const TPair<float, int32>& L, const TPair<float, int32>& R) { return L.Key < R.Key; });
	int32 FirstPiece = -1;
	int32 From = N1;
	Stops.Add(TPair<float, int32>(Len, N2));
	for (const TPair<float, int32>& Stop : Stops)
	{
		if (Stop.Value == From || !Nodes.Contains(From) || !Nodes.Contains(Stop.Value)) continue;
		if (FVector2D::Distance(Nodes[From].Position, Nodes[Stop.Value].Position) < 1.f)
		{
			MergeNodeInto(Stop.Value, From); // two stops at one point: one corner, no zero-length wall
			continue;
		}
		const int32 Piece = AddWall(From, Stop.Value, Thickness, Height);
		if (FirstPiece == -1) FirstPiece = Piece;
		From = Stop.Value;
	}
	if (bPulledAny && HasAuthority())
	{
		RefreshWallAttachedPlacements(); // items hung on a pulled wall follow it
	}
	return FirstPiece;
}

int32 ARoomPlannerManager::AddWall(int32 StartNodeID, int32 EndNodeID, float Thickness, float Height, int32 DesiredSegmentID)
{
	if (StartNodeID == EndNodeID || !Nodes.Contains(StartNodeID) || !Nodes.Contains(EndNodeID))
	{
		return -1;
	}

	// Check if wall segment between these nodes already exists
	for (const TPair<int32, FWallSegment>& Pair : WallSegments)
	{
		if ((Pair.Value.StartNodeID == StartNodeID && Pair.Value.EndNodeID == EndNodeID) ||
		    (Pair.Value.StartNodeID == EndNodeID && Pair.Value.EndNodeID == StartNodeID))
		{
			return Pair.Key;
		}
	}

	// DesiredSegmentID: the import keeps the id the layout was saved with, so a wall means the same wall on every machine.
	const bool bKeepID = DesiredSegmentID > 0 && !WallSegments.Contains(DesiredSegmentID);
	int32 SegID = bKeepID ? DesiredSegmentID : NextSegmentID;
	if (SegID >= NextSegmentID) NextSegmentID = SegID + 1;

	FWallSegment Segment;
	Segment.SegmentID = SegID;
	Segment.WallGuid = PlannerJsonKeys::NewInstanceID();
	Segment.StartNodeID = StartNodeID;
	Segment.EndNodeID = EndNodeID;
	Segment.Thickness = Thickness;
	Segment.Height = Height;

	WallSegments.Add(SegID, Segment);

	Nodes[StartNodeID].ConnectedSegmentIDs.AddUnique(SegID);
	Nodes[EndNodeID].ConnectedSegmentIDs.AddUnique(SegID);

	// Spawn 3D Wall Actor
	if (GetWorld())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		AProceduralWallActor* WallActor = GetWorld()->SpawnActor<AProceduralWallActor>(AProceduralWallActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		if (WallActor)
		{
			WallActor->WallData = Segment;
			WallActor->BaseWallMaterial = DefaultWallMaterial;
			WallActor->WallSelectionMaterial = WallSelectionMaterial;
			WallActor->OpeningSelectionMaterial = OpeningSelectionMaterial;
			WallActor->LeafMaterial = LeafMaterial;
			WallActors.Add(SegID, WallActor);
		}
	}

	RebuildWallsAndRooms();

	return SegID;
}

bool ARoomPlannerManager::AddOpeningToWall(int32 SegmentID, EOpeningType Type, float DistFromStart, float Width, float Height, float SillHeight)
{
	FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg)
	{
		return false;
	}

	FWallOpening Opening;
	// Serial numbers repeat: delete the first of two openings and the next one added is "Op_S_2" a second time. Both the leaf
	// state and the saved layout identify an opening by this string, so it has to stay unique for good, like WallGuid.
	Opening.OpeningID = PlannerJsonKeys::NewInstanceID();
	Opening.Type = Type;
	Opening.DistanceFromStart = DistFromStart;
	Opening.Width = Width;
	Opening.Height = Height;
	Opening.SillHeight = SillHeight;

	Seg->Openings.Add(Opening);

	if (TObjectPtr<AProceduralWallActor>* ActorPtr = WallActors.Find(SegmentID))
	{
		if (*ActorPtr)
		{
			(*ActorPtr)->WallData = *Seg;
		}
	}

	RebuildWallsAndRooms(); // floor thresholds and baseboard gaps follow the openings
	return true;
}

void ARoomPlannerManager::RemoveWall(int32 SegmentID)
{
	FWallSegment Seg;
	if (!WallSegments.RemoveAndCopyValue(SegmentID, Seg))
	{
		return;
	}

	// Items attached to this wall stay where they are but are no longer bound to it.
	DetachItemsFromWall(Seg.WallGuid);

	if (SelectedSegmentID == SegmentID)
	{
		ClearWallSelection();
	}

	if (FWallNode* StartNode = Nodes.Find(Seg.StartNodeID))
	{
		StartNode->ConnectedSegmentIDs.Remove(SegmentID);
		if (StartNode->ConnectedSegmentIDs.Num() == 0)
		{
			Nodes.Remove(Seg.StartNodeID);
		}
	}
	if (FWallNode* EndNode = Nodes.Find(Seg.EndNodeID))
	{
		EndNode->ConnectedSegmentIDs.Remove(SegmentID);
		if (EndNode->ConnectedSegmentIDs.Num() == 0)
		{
			Nodes.Remove(Seg.EndNodeID);
		}
	}

	if (TObjectPtr<AProceduralWallActor>* ActorPtr = WallActors.Find(SegmentID))
	{
		if (*ActorPtr && (*ActorPtr)->IsValidLowLevel())
		{
			if ((*ActorPtr)->WallProceduralMesh)
			{
				(*ActorPtr)->WallProceduralMesh->ClearAllMeshSections();
				(*ActorPtr)->WallProceduralMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
			(*ActorPtr)->SetActorEnableCollision(false);
			(*ActorPtr)->Destroy();
		}
		WallActors.Remove(SegmentID);
	}

	RebuildWallsAndRooms();
	ReplicatedRoomJSON = ExportLayoutToJSON();
	OnRoomPlannerUpdated.Broadcast(ReplicatedRoomJSON);
	OnWallSelected.Broadcast(-1, 0.f);
}

void ARoomPlannerManager::ClearWallsAndRooms()
{
	for (auto& Pair : WallActors)
	{
		if (Pair.Value && Pair.Value->IsValidLowLevel())
		{
			Pair.Value->Destroy();
		}
	}
	WallActors.Empty();
	WallSegments.Empty();
	Nodes.Empty();
	Rooms.Empty();
	FloorSectionMaterials.Empty();
	CeilingSectionMaterials.Empty();
	BaseboardSectionMaterials.Empty();
	bExteriorBackdropDirty = true; // the enclosure follows the layout (hidden by Tick when nothing qualifies)
	NextNodeID = 1;
	NextSegmentID = 1;
	DraggingNodeID = -1;

	if (FloorProceduralMesh) FloorProceduralMesh->ClearAllMeshSections();
	if (CeilingProceduralMesh) CeilingProceduralMesh->ClearAllMeshSections();
	if (BaseboardProceduralMesh) BaseboardProceduralMesh->ClearAllMeshSections();
	if (NodeHandleMesh) NodeHandleMesh->ClearAllMeshSections();
	// Room lights are matched by content on the next rebuild (RebuildRoomLights). Destroying them here would throw away
	// every unchanged room's light on each replicated edit (every mutation re-imports the layout through this path).
	bRoomLightsDirty = true;
}

void ARoomPlannerManager::ClearLayout()
{
	// Walls / nodes / rooms
	ClearWallsAndRooms();

	// Finishes
	FloorFinishes.Empty();
	CeilingFinishes.Empty();
	BaseboardFinishes.Empty();

	// Placed interior objects (local actors on every machine)
	for (auto& Pair : PlacedObjectActors)
	{
		if (Pair.Value && Pair.Value->IsValidLowLevel())
		{
			Pair.Value->Destroy();
		}
	}
	PlacedObjectActors.Empty();
	PlacedObjects.Empty();

	// Cabinet sets (replicated booths — only the server destroys them)
	CabinetSets.Empty();
	if (HasAuthority())
	{
		DestroyAllPlannerCabinetSets();
	}
	CabinetSetActorCache.Empty();

	SelectedSegmentID = -1;
	SelectedOpeningIndex = -1;
	SelectedRoomID = -1;
	SelectedObjectID.Empty();
	SelectedCabinetSetID.Empty();
	PendingPlacementKind = EPlannerPlacementKind::None;
	PendingPlacementAssetID.Empty();
	PendingPlacementYawDeg = 0.f;
}

void ARoomPlannerManager::ComputeMiterOffsetsAtNode(int32 NodeID, TMap<int32, FVector2D>& OutStartLeftOffsets,
                                                     TMap<int32, FVector2D>& OutStartRightOffsets,
                                                     TMap<int32, FVector2D>& OutEndLeftOffsets,
                                                     TMap<int32, FVector2D>& OutEndRightOffsets)
{
	// Centerline extension logic is handled in RebuildAllWalls / ComputeMitreCorner to ensure clean corner closing.
}

void ARoomPlannerManager::ComputeAllCornerJoints()
{
	// Node positions and each wall's ends and thickness are the only input, and none of them changes inside one logical rebuild:
	// the rooms pass and the interior-side pass that follows it read the joints the walls pass computed.
	if (RebuildScopeDepth > 0 && bCornerJointsComputedInScope)
	{
		return;
	}
	bCornerJointsComputedInScope = (RebuildScopeDepth > 0);

	CornerJoints.Reset();

	struct FEntry
	{
		int32 SegID = -1;
		FVector2D A = FVector2D::ZeroVector;     // outgoing direction from the node into the wall
		FVector2D NLeft = FVector2D::ZeroVector; // the wall's own LEFT face normal (start->end terms, matches the mesh builder)
		bool bCCWIsLeft = true;                  // true when the counter-clockwise face (w.r.t. A) is the wall's LEFT face
		float Half = 10.f;
		float Thickness = 20.f;
		float Len = 0.f;
		float Angle = 0.f;
	};

	auto Cross2 = [](const FVector2D& U, const FVector2D& V) { return U.X * V.Y - U.Y * V.X; };

	for (const auto& NodePair : Nodes)
	{
		const int32 NodeID = NodePair.Key;
		const FVector2D N = NodePair.Value.Position;

		// Gather the walls leaving this node.
		TArray<FEntry> Entries;
		for (int32 SegID : NodePair.Value.ConnectedSegmentIDs)
		{
			const FWallSegment* Seg = WallSegments.Find(SegID);
			if (!Seg || (Seg->StartNodeID != NodeID && Seg->EndNodeID != NodeID)) continue;
			const FWallNode* S = Nodes.Find(Seg->StartNodeID);
			const FWallNode* E = Nodes.Find(Seg->EndNodeID);
			if (!S || !E) continue;
			const float Len = FVector2D::Distance(S->Position, E->Position);
			if (Len < 1.f) continue;

			FEntry En;
			En.SegID = SegID;
			const FVector2D Dir = (E->Position - S->Position) / Len;
			En.NLeft = FVector2D(-Dir.Y, Dir.X);
			const bool bAtStart = (Seg->StartNodeID == NodeID);
			En.A = bAtStart ? Dir : -Dir;
			En.bCCWIsLeft = bAtStart; // CCW normal of A is (-A.Y, A.X): equals NLeft when A == Dir, -NLeft when A == -Dir
			En.Half = Seg->Thickness * 0.5f;
			En.Thickness = Seg->Thickness;
			En.Len = Len;
			En.Angle = FMath::Atan2(En.A.Y, En.A.X);
			Entries.Add(En);
		}
		if (Entries.Num() == 0) continue;

		// Per-wall face state at this node (indexed like Entries): CCW face / CW face w.r.t. the wall's outgoing direction.
		const int32 NumWalls = Entries.Num();
		TArray<bool> bCCWMitred, bCWMitred;
		TArray<FVector2D> CCWPoint, CWPoint;
		TArray<int32> CCWPartner, CWPartner;
		bCCWMitred.Init(false, NumWalls);
		bCWMitred.Init(false, NumWalls);
		CCWPoint.Init(FVector2D::ZeroVector, NumWalls);
		CWPoint.Init(FVector2D::ZeroVector, NumWalls);
		CCWPartner.Init(-1, NumWalls);
		CWPartner.Init(-1, NumWalls);

		if (NumWalls >= 2)
		{
			// Counter-clockwise order around the node.
			TArray<int32> Order;
			for (int32 i = 0; i < NumWalls; ++i) Order.Add(i);
			Order.Sort([&Entries](int32 L, int32 R) { return Entries[L].Angle < Entries[R].Angle; });

			const int32 Count = Order.Num();
			for (int32 k = 0; k < Count; ++k)
			{
				const int32 i = Order[k];
				const int32 j = Order[(k + 1) % Count]; // next wall counter-clockwise
				if (i == j) continue;

				const FEntry& Wi = Entries[i];
				const FEntry& Wj = Entries[j];

				const float Cross = Cross2(Wi.A, Wj.A);
				if (FMath::Abs(Cross) < 0.02f) continue; // parallel / collinear: flush caps

				// Wedge between wall i and wall j is bounded by i's CCW face and j's CW face.
				const FVector2D NCCW_i(-Wi.A.Y, Wi.A.X);
				const FVector2D NCW_j(Wj.A.Y, -Wj.A.X);
				const FVector2D P1 = N + NCCW_i * Wi.Half;
				const FVector2D P2 = N + NCW_j * Wj.Half;
				const FVector2D D = P2 - P1;

				// P1 + t*Ai = P2 + s*Aj
				const float T = Cross2(D, Wj.A) / Cross;
				const float Sv = Cross2(D, Wi.A) / Cross;

				// One shared decision for the pair.
				const float Limit = FMath::Min(3.f * FMath::Max(Wi.Thickness, Wj.Thickness), 0.5f * FMath::Min(Wi.Len, Wj.Len));
				if (FMath::Abs(T) > Limit || FMath::Abs(Sv) > Limit) continue;

				const FVector2D Point = P1 + Wi.A * T;

				bCCWMitred[i] = true; CCWPoint[i] = Point; CCWPartner[i] = j;
				bCWMitred[j] = true;  CWPoint[j] = Point;  CWPartner[j] = i;
			}

			// A wall end may stay mitred only when BOTH of its faces are mitred. A half-mitred end would be
			// drawn with a diagonal cap across the wall body (holes at T-junctions, spikes at acute branches).
			// Reverting a wall also un-mitres the partner faces that were paired with it, until stable.
			bool bChanged = true;
			while (bChanged)
			{
				bChanged = false;
				for (int32 i = 0; i < NumWalls; ++i)
				{
					if (bCCWMitred[i] == bCWMitred[i]) continue; // both or none: consistent

					if (bCCWMitred[i])
					{
						bCCWMitred[i] = false;
						const int32 j = CCWPartner[i];
						if (j >= 0 && bCWMitred[j] && CWPartner[j] == i)
						{
							bCWMitred[j] = false;
						}
					}
					if (bCWMitred[i])
					{
						bCWMitred[i] = false;
						const int32 k = CWPartner[i];
						if (k >= 0 && bCCWMitred[k] && CCWPartner[k] == i)
						{
							bCCWMitred[k] = false;
						}
					}
					bChanged = true;
				}
			}
		}

		// Build the joints: plain node offsets by default, mitre points where the pair survived.
		for (int32 i = 0; i < NumWalls; ++i)
		{
			const FEntry& W = Entries[i];
			FWallCornerJoint J;
			J.Left = N + W.NLeft * W.Half;
			J.Right = N - W.NLeft * W.Half;

			if (bCCWMitred[i])
			{
				if (W.bCCWIsLeft) { J.Left = CCWPoint[i]; J.bLeftMitred = true; }
				else              { J.Right = CCWPoint[i]; J.bRightMitred = true; }
			}
			if (bCWMitred[i])
			{
				if (W.bCCWIsLeft) { J.Right = CWPoint[i]; J.bRightMitred = true; }
				else              { J.Left = CWPoint[i]; J.bLeftMitred = true; }
			}
			CornerJoints.Add(MakeJointKey(W.SegID, NodeID), J);
		}
	}
}

void ARoomPlannerManager::RebuildWallsAndRooms()
{
	++RebuildScopeDepth;
	ON_SCOPE_EXIT
	{
		if (--RebuildScopeDepth == 0)
		{
			bCornerJointsComputedInScope = false;
		}
	};

	RebuildAllWalls();
	RebuildRooms();

	// Walls and slabs have just been built together. If that cooked, there is nothing left for EnsureLayoutCollision to
	// repair — the commit that ends a drag lands here, so the frame after a release does not rebuild the layout a second time.
	if (ShouldCookCollision() && LayoutImportDepth == 0)
	{
		bLayoutCollisionStale = false;
	}
}

void ARoomPlannerManager::RebuildAllWalls()
{
	if (LayoutImportDepth > 0)
	{
		return; // ImportLayoutFromJSON rebuilds once when every wall and opening exists
	}
	// Baseboards stop at doors and archways: rebuild them with the walls, so a moved, resized, added or removed opening moves its
	// gap with it (RebuildRooms does this itself, at its end).
	ON_SCOPE_EXIT
	{
		// In a scope the rooms pass follows and ends with the baseboards itself, from the rooms this rebuild is about to detect;
		// doing them here would build them from the previous rooms and throw them away a moment later.
		if (!bRebuildingRooms && RebuildScopeDepth == 0 && BaseboardDefaultMaterial && Rooms.Num() > 0)
		{
			RebuildBaseboards(BaseboardDefaultMaterial);
		}
	};

	ComputeAllCornerJoints();
	ComputeWallFaceUVFrames();

	const bool bCookCollision = ShouldCookCollision();
	if (!bCookCollision)
	{
		bLayoutCollisionStale = true; // only a walls+rooms rebuild that cooks clears it again (RebuildWallsAndRooms)
	}

	for (auto& Pair : WallActors)
	{
		int32 SegID = Pair.Key;
		AProceduralWallActor* WallActor = Pair.Value;
		const FWallSegment* Seg = WallSegments.Find(SegID);

		if (WallActor && Seg && Nodes.Contains(Seg->StartNodeID) && Nodes.Contains(Seg->EndNodeID))
		{
			FVector2D StartPos = Nodes[Seg->StartNodeID].Position;
			FVector2D EndPos = Nodes[Seg->EndNodeID].Position;

			FVector2D Dir = (EndPos - StartPos).GetSafeNormal();
			FVector2D Normal(-Dir.Y, Dir.X);
			float HalfThick = Seg->Thickness * 0.5f;

			FVector2D SL2D = StartPos + Normal * HalfThick;
			FVector2D SR2D = StartPos - Normal * HalfThick;
			FVector2D EL2D = EndPos + Normal * HalfThick;
			FVector2D ER2D = EndPos - Normal * HalfThick;

			bool bStartCap = true;
			bool bEndCap = true;

			// Corner joints from the per-node pre-pass (shared decision per face pair, 2..N walls per node).
			// A cap is drawn whenever at least one face of this end is not mitred.
			if (const FWallCornerJoint* J = CornerJoints.Find(MakeJointKey(SegID, Seg->StartNodeID)))
			{
				SL2D = J->Left;
				SR2D = J->Right;
				bStartCap = !(J->bLeftMitred && J->bRightMitred);
			}
			if (const FWallCornerJoint* J = CornerJoints.Find(MakeJointKey(SegID, Seg->EndNodeID)))
			{
				EL2D = J->Left;
				ER2D = J->Right;
				bEndCap = !(J->bLeftMitred && J->bRightMitred);
			}

			WallActor->WallData = *Seg;

			// Keep the wall's face finish materials in sync with its data (REQ-13); instances are rebuilt only when a finish changed.
			ApplyWallFinishToActor(WallActor, *Seg);
			// Tile grid that continues across openings and from wall to wall (REQ-13).
			if (const FPlannerWallFaceUV* FaceUV = WallFaceUVFrames.Find(SegID))
			{
				WallActor->SetFaceUVFrame(*FaceUV);
			}

			// Trim must stay clear of other walls meeting this wall's ends (T-junction branches are not mitred).
			WallActor->SetDressingFaceLimits(
				ComputeBranchCoverOnFace(SegID, Seg->StartNodeID, Dir, Normal, HalfThick),
				ComputeBranchCoverOnFace(SegID, Seg->StartNodeID, Dir, -Normal, HalfThick),
				ComputeBranchCoverOnFace(SegID, Seg->EndNodeID, -Dir, Normal, HalfThick),
				ComputeBranchCoverOnFace(SegID, Seg->EndNodeID, -Dir, -Normal, HalfThick));
			WallActor->SetPresentation(!b2DViewMode);
			WallActor->RebuildWallMesh(StartPos, EndPos, SL2D, SR2D, EL2D, ER2D, bStartCap, bEndCap, bCookCollision);
			ApplyLeafAnimationsToWall(WallActor);
		}
	}

	RefreshNodeHandles();
}

void ARoomPlannerManager::TickOpeningDragEnd()
{
	if (!bOpeningDragActive) return;

	// The drag has no release of its own here: the player controller commits on the mouse-up it sees, and this is the backstop
	// for every other way it can stop (selection cleared, tool or view change, the UI closing, the press consumed elsewhere).
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (Now - LastOpeningDragSeconds < OpeningDragIdleSeconds) return;

	bOpeningDragActive = false;
	if (bOpeningDragPublishPending)
	{
		bOpeningDragPublishPending = false;
		if (HasAuthority())
		{
			CommitStateAfterMutation(); // the authority moved the opening during the preview; the clients still hold the old one
		}
		else if (!ReplicatedRoomJSON.IsEmpty())
		{
			// A client's preview is local until the release commits it. Nothing committed this one, so drop it rather than
			// leaving this machine showing a door the server never heard about (and ordering its openings differently).
			ImportLayoutFromJSON(ReplicatedRoomJSON);
		}
	}
}

void ARoomPlannerManager::EnsureLayoutCollision()
{
	if (!bLayoutCollisionStale || !ShouldCookCollision() || LayoutImportDepth > 0)
	{
		return; // an import rebuilds everything at its end anyway, and both rebuilds below would no-op inside one
	}
	RebuildWallsAndRooms(); // one scope: the corner joints and the baseboards are computed once, as in any other rebuild
	UpdateSelectionVisuals(); // the rebuild resets every highlight, and the user's selection outlives the drag
	bLayoutCollisionStale = false;
}

void ARoomPlannerManager::ToggleCeilingVisibility()
{
	SetCeilingVisibility(!bCeilingVisible);
}

void ARoomPlannerManager::SetCeilingVisibility(bool bVisible)
{
	bCeilingVisible = bVisible;
	if (CeilingProceduralMesh)
	{
		CeilingProceduralMesh->SetVisibility(bCeilingVisible && !b2DViewMode);
	}
}

void ARoomPlannerManager::RebuildRooms()
{
	if (LayoutImportDepth > 0)
	{
		return; // ImportLayoutFromJSON rebuilds once when every wall and opening exists
	}

	// Its own scope when it is called alone (import, legacy finishes, tests): the interior-side pass at its end rebuilds the
	// walls, which would compute the corner joints this pass already computed.
	++RebuildScopeDepth;
	ON_SCOPE_EXIT
	{
		if (--RebuildScopeDepth == 0)
		{
			bCornerJointsComputedInScope = false;
		}
	};

	TGuardValue<bool> RebuildingRooms(bRebuildingRooms, true);
	Rooms.Empty();
	bRoomLightsDirty = true; // every exit below (including "no closed room") must refresh the room lights
	bExteriorBackdropDirty = true; // the enclosure follows the layout bounds
	// The slabs are rebuilt on every frame of a corner drag too. Without collision the synchronous cook path is used, so no body
	// setup is allocated and no previous cook is aborted per update — the trade-off the wall dressing and the leaves already make.
	const bool bCookCollision = ShouldCookCollision();
	if (!bCookCollision)
	{
		bLayoutCollisionStale = true;
	}
	if (FloorProceduralMesh) FloorProceduralMesh->bUseAsyncCooking = bCookCollision;
	if (CeilingProceduralMesh) CeilingProceduralMesh->bUseAsyncCooking = bCookCollision;
	if (BaseboardProceduralMesh) BaseboardProceduralMesh->bUseAsyncCooking = bCookCollision;
	if (FloorProceduralMesh) FloorProceduralMesh->ClearAllMeshSections();
	if (CeilingProceduralMesh) CeilingProceduralMesh->ClearAllMeshSections();
	if (BaseboardProceduralMesh) BaseboardProceduralMesh->ClearAllMeshSections();

	if (Nodes.Num() < 3 || WallSegments.Num() < 3)
	{
		ComputeWallInteriorSides(TArray<TArray<FVector2D>>{});
		ClearRoomLights();
		return;
	}

	// 1. Build adjacency map for 2-core degree pruning
	TMap<int32, TSet<int32>> NodeNeighbors;
	for (const auto& Pair : WallSegments)
	{
		const FWallSegment& Seg = Pair.Value;
		if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
		{
			int32 u = Seg.StartNodeID;
			int32 v = Seg.EndNodeID;
			if (u != v)
			{
				NodeNeighbors.FindOrAdd(u).Add(v);
				NodeNeighbors.FindOrAdd(v).Add(u);
			}
		}
	}

	// 2. Iteratively prune leaves and dead-end spurs (degree < 2)
	// Open walls, standalone segments, and unclosed U-shapes are eliminated!
	bool bPruned = true;
	while (bPruned)
	{
		bPruned = false;
		TArray<int32> ToPrune;
		for (const auto& Pair : NodeNeighbors)
		{
			if (Pair.Value.Num() < 2)
			{
				ToPrune.Add(Pair.Key);
			}
		}
		for (int32 NodeID : ToPrune)
		{
			for (int32 Neighbor : NodeNeighbors[NodeID])
			{
				NodeNeighbors[Neighbor].Remove(NodeID);
			}
			NodeNeighbors.Remove(NodeID);
			bPruned = true;
		}
	}

	// If no closed 2-core cycles exist in the graph, abort immediately (zero floors/ceilings for open walls)
	if (NodeNeighbors.Num() < 3)
	{
		ComputeWallInteriorSides(TArray<TArray<FVector2D>>{});
		return;
	}

	// 3. Build Directed Half-Edges exclusively on the pruned closed 2-core graph
	struct FDirectedHalfEdge
	{
		int32 FromNode;
		int32 ToNode;
		float PolarAngle;
	};

	TMap<int32, TArray<FDirectedHalfEdge>> OutgoingEdges;

	for (const auto& Pair : WallSegments)
	{
		const FWallSegment& Seg = Pair.Value;
		int32 u = Seg.StartNodeID;
		int32 v = Seg.EndNodeID;
		if (NodeNeighbors.Contains(u) && NodeNeighbors.Contains(v) && NodeNeighbors[u].Contains(v))
		{
			FVector2D PosU = Nodes[u].Position;
			FVector2D PosV = Nodes[v].Position;

			FVector2D VecUV = PosV - PosU;
			FVector2D VecVU = PosU - PosV;

			if (VecUV.SizeSquared() < 1.0f) continue;

			float AngleUV = FMath::Atan2(VecUV.Y, VecUV.X);
			float AngleVU = FMath::Atan2(VecVU.Y, VecVU.X);

			OutgoingEdges.FindOrAdd(u).Add({ u, v, AngleUV });
			OutgoingEdges.FindOrAdd(v).Add({ v, u, AngleVU });
		}
	}

	// 4. Sort outgoing edges around each node counter-clockwise by polar angle
	for (auto& NodeEdgesPair : OutgoingEdges)
	{
		NodeEdgesPair.Value.Sort([](const FDirectedHalfEdge& A, const FDirectedHalfEdge& B) {
			return A.PolarAngle < B.PolarAngle;
		});
	}

	// 5. Directed Edge Visited Tracking
	TSet<uint64> VisitedDirectedEdges;
	auto MakeEdgeKey = [](int32 From, int32 To) -> uint64 {
		return ((uint64)(uint32)From << 32) | (uint64)(uint32)To;
	};

	TArray<TArray<FVector2D>> DetectedRoomPolygons;
	RoomGroupOutlines.Reset();

	// One traced loop of nodes: a room when it runs clockwise, the outline around a group of joined rooms when counter-clockwise.
	auto ConsiderLoop = [&](const TArray<int32>& Loop)
	{
		TArray<FVector2D> Poly;
		for (int32 NodeID : Loop)
		{
			if (Nodes.Contains(NodeID))
			{
				Poly.Add(Nodes[NodeID].Position);
			}
		}

		// Clean consecutive duplicate vertices
		TArray<FVector2D> CleanPoly;
		for (int32 i = 0; i < Poly.Num(); ++i)
		{
			const FVector2D& P = Poly[i];
			if (CleanPoly.Num() == 0 || FVector2D::DistSquared(P, CleanPoly.Last()) > 1.0f)
			{
				CleanPoly.Add(P);
			}
		}
		if (CleanPoly.Num() >= 3 && FVector2D::DistSquared(CleanPoly[0], CleanPoly.Last()) < 1.0f)
		{
			CleanPoly.Pop();
		}
		if (CleanPoly.Num() < 3) return;

		// Shoelace Formula for signed area
		float TwiceArea = 0.f;
		const int32 N = CleanPoly.Num();
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D& P1 = CleanPoly[i];
			const FVector2D& P2 = CleanPoly[(i + 1) % N];
			TwiceArea += (P1.X * P2.Y - P2.X * P1.Y);
		}

		// Bounding box size validation
		FVector2D MinP = CleanPoly[0];
		FVector2D MaxP = CleanPoly[0];
		for (const FVector2D& Pt : CleanPoly)
		{
			MinP.X = FMath::Min(MinP.X, Pt.X);
			MinP.Y = FMath::Min(MinP.Y, Pt.Y);
			MaxP.X = FMath::Max(MaxP.X, Pt.X);
			MaxP.Y = FMath::Max(MaxP.Y, Pt.Y);
		}
		if (MaxP.X - MinP.X < 25.f || MaxP.Y - MinP.Y < 25.f) return;

		// Walking on to the next edge counter-clockwise from the way back traces every bounded face (a room) clockwise and the
		// outline around a whole group of rooms counter-clockwise. Keep the rooms, turned counter-clockwise (interior on the left,
		// as everything below expects) and starting at the same corner as before. A single room gives both with the same outline,
		// which hid that only the outer outline had been kept: a layout divided into rooms came out as one room over all of them.
		if (TwiceArea < -500.0f)
		{
			Algo::Reverse(CleanPoly);
			CleanPoly.Insert(CleanPoly.Pop(), 0);
			DetectedRoomPolygons.Add(CleanPoly);
		}
		else if (TwiceArea > 500.0f)
		{
			RoomGroupOutlines.Add(CleanPoly);
		}
	};

	// 6. Trace all closed faces using the Left-Turn rule
	for (const auto& NodePair : OutgoingEdges)
	{
		for (const FDirectedHalfEdge& StartEdge : NodePair.Value)
		{
			uint64 StartKey = MakeEdgeKey(StartEdge.FromNode, StartEdge.ToNode);
			if (VisitedDirectedEdges.Contains(StartKey))
			{
				continue;
			}

			TArray<int32> FaceCycle;
			int32 CurrU = StartEdge.FromNode;
			int32 CurrV = StartEdge.ToNode;
			bool bValidFace = false;

			int32 MaxSteps = WallSegments.Num() * 4 + 16;
			int32 StepCount = 0;

			while (StepCount++ < MaxSteps)
			{
				uint64 CurrKey = MakeEdgeKey(CurrU, CurrV);
				if (VisitedDirectedEdges.Contains(CurrKey))
				{
					break;
				}
				VisitedDirectedEdges.Add(CurrKey);
				FaceCycle.Add(CurrU);

				if (!OutgoingEdges.Contains(CurrV))
				{
					break;
				}

				const TArray<FDirectedHalfEdge>& OutFromV = OutgoingEdges[CurrV];
				int32 NumOut = OutFromV.Num();
				if (NumOut == 0) break;

				int32 IncomingIdx = INDEX_NONE;
				for (int32 i = 0; i < NumOut; ++i)
				{
					if (OutFromV[i].ToNode == CurrU)
					{
						IncomingIdx = i;
						break;
					}
				}

				if (IncomingIdx == INDEX_NONE)
				{
					break;
				}

				int32 NextIdx = (IncomingIdx + 1) % NumOut;
				const FDirectedHalfEdge& NextEdge = OutFromV[NextIdx];

				CurrU = NextEdge.FromNode;
				CurrV = NextEdge.ToNode;

				if (CurrU == StartEdge.FromNode && CurrV == StartEdge.ToNode)
				{
					bValidFace = true;
					break;
				}
			}

			if (bValidFace && FaceCycle.Num() >= 3)
			{
				// A face that passes a node twice (a room with a closed loop inside it joined to it by one wall, or a closet touching it
				// at a single corner) is split there into simple loops: the room itself, the loop around what it encloses, and the way
				// along the joining wall and back (two nodes, dropped).
				TArray<TArray<int32>> Loops;
				TArray<int32> Walk;
				for (int32 NodeID : FaceCycle)
				{
					const int32 Earlier = Walk.Find(NodeID);
					if (Earlier != INDEX_NONE)
					{
						Loops.Add(TArray<int32>(Walk.GetData() + Earlier, Walk.Num() - Earlier));
						Walk.SetNum(Earlier + 1);
					}
					else
					{
						Walk.Add(NodeID);
					}
				}
				Loops.Add(Walk);
				for (const TArray<int32>& Loop : Loops)
				{
					if (Loop.Num() >= 3) ConsiderLoop(Loop);
				}
			}
		}
	}

	if (DetectedRoomPolygons.Num() == 0)
	{
		ComputeWallInteriorSides(TArray<TArray<FVector2D>>{});
		return;
	}

	// 5. Materials Setup
	// Ceiling / baseboard use the parameterised paint material (BaseColor); floors get a per-room
	// material from ResolveFloorMaterialForRoom (finish if any, else white paint) — REQ-13.
	UMaterialInterface* BaseMat = ResolvePaintBaseMaterial();
	if (!BaseMat)
	{
		BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
	}
	if (!BaseMat)
	{
		BaseMat = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	// Through the finish cache: one instance per colour for the whole session instead of two new ones per rebuild, and the same
	// instances SetRoomSurfaceFinish already falls back to. Nothing changes a finish material's parameters after it is created.
	UMaterialInterface* CeilMatInst = GetFinishMaterial(MakeDefaultPaintFinish(FLinearColor(0.95f, 0.95f, 0.95f, 1.0f)));
	UMaterialInterface* BbMatInst = GetFinishMaterial(MakeDefaultPaintFinish(FLinearColor(0.4f, 0.3f, 0.2f, 1.0f)));

	auto IsPointInTriangle = [](const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C) {
		auto Sign = [](const FVector2D& P1, const FVector2D& P2, const FVector2D& P3) {
			return (P1.X - P3.X) * (P2.Y - P3.Y) - (P2.X - P3.X) * (P1.Y - P3.Y);
		};
		bool b1 = Sign(P, A, B) < 0.0f;
		bool b2 = Sign(P, B, C) < 0.0f;
		bool b3 = Sign(P, C, A) < 0.0f;
		return ((b1 == b2) && (b2 == b3));
	};

	// Corner joints decide where walls cut their door holes (baseboards and floor thresholds follow the same span).
	ComputeAllCornerJoints();

	// Ear clipping of a simple polygon (either orientation handled by the caller); indices reversed for Unreal's front faces.
	auto EarClip = [&IsPointInTriangle](const TArray<FVector2D>& Poly)
	{
		TArray<int32> Indices;
		for (int32 i = 0; i < Poly.Num(); ++i) Indices.Add(i);
		TArray<int32> Out;
		int32 IterationCount = 0;
		while (Indices.Num() > 3 && IterationCount < 1000)
		{
			IterationCount++;
			bool bEarFound = false;
			for (int32 i = 0; i < Indices.Num(); ++i)
			{
				const int32 PrevIdx = (i == 0) ? Indices.Num() - 1 : i - 1;
				const int32 NextIdx = (i == Indices.Num() - 1) ? 0 : i + 1;
				const int32 V0 = Indices[PrevIdx];
				const int32 V1 = Indices[i];
				const int32 V2 = Indices[NextIdx];
				const FVector2D& P0 = Poly[V0];
				const FVector2D& P1 = Poly[V1];
				const FVector2D& P2 = Poly[V2];
				const float Cross = (P1.X - P0.X) * (P2.Y - P1.Y) - (P1.Y - P0.Y) * (P2.X - P1.X);
				if (Cross >= -0.01f)
				{
					bool bValid = true;
					for (int32 j = 0; j < Indices.Num(); ++j)
					{
						if (j == PrevIdx || j == i || j == NextIdx) continue;
						if (IsPointInTriangle(Poly[Indices[j]], P0, P1, P2))
						{
							bValid = false;
							break;
						}
					}
					if (bValid)
					{
						Out.Add(V0);
						Out.Add(V1);
						Out.Add(V2);
						Indices.RemoveAt(i);
						bEarFound = true;
						break;
					}
				}
			}
			if (!bEarFound)
			{
				Out.Add(Indices[0]);
				Out.Add(Indices[1]);
				Out.Add(Indices[2]);
				Indices.RemoveAt(1);
			}
		}
		if (Indices.Num() == 3)
		{
			Out.Add(Indices[0]);
			Out.Add(Indices[1]);
			Out.Add(Indices[2]);
		}
		Algo::Reverse(Out); // clockwise front faces in Unreal
		return Out;
	};

	// Per room: its centroid, a point surely inside it, and which stored floor / ceiling / baseboard finish belongs to it. A record
	// belongs to the room containing its anchor, and to that room only, so a finish set on one room never shows on its neighbour.
	TArray<FVector2D> RoomCentroids;
	TArray<FVector2D> RoomInteriorPoints;
	for (const TArray<FVector2D>& Poly : DetectedRoomPolygons)
	{
		double TwiceArea = 0.0, Cx = 0.0, Cy = 0.0;
		for (int32 i = 0; i < Poly.Num(); ++i)
		{
			const FVector2D& P1 = Poly[i];
			const FVector2D& P2 = Poly[(i + 1) % Poly.Num()];
			const double Cross = (double)P1.X * P2.Y - (double)P2.X * P1.Y;
			TwiceArea += Cross;
			Cx += (P1.X + P2.X) * Cross;
			Cy += (P1.Y + P2.Y) * Cross;
		}
		FVector2D Centroid = FVector2D::ZeroVector;
		if (FMath::Abs(TwiceArea) > KINDA_SMALL_NUMBER)
		{
			Centroid = FVector2D((float)(Cx / (3.0 * TwiceArea)), (float)(Cy / (3.0 * TwiceArea)));
		}
		else
		{
			for (const FVector2D& P : Poly) Centroid += P;
			Centroid /= (float)FMath::Max(1, Poly.Num());
		}
		RoomCentroids.Add(Centroid);
		RoomInteriorPoints.Add(PlannerFinishLayout::PolygonInteriorPoint(Poly));
	}
	// A room around a smaller room (standing inside it) may have its centroid inside the smaller one: its anchor must be a point that
	// is found as this room (the smallest room around it), or its finishes would land on the inner room.
	auto SmallestRoomAround = [&DetectedRoomPolygons](const FVector2D& P)
	{
		int32 Best = INDEX_NONE;
		double BestArea = TNumericLimits<double>::Max();
		for (int32 i = 0; i < DetectedRoomPolygons.Num(); ++i)
		{
			if (!PlannerFinishLayout::IsPointInPolygon(P, DetectedRoomPolygons[i])) continue;
			const double Area = FMath::Abs(PlannerFinishLayout::SignedArea(DetectedRoomPolygons[i]));
			if (Area < BestArea)
			{
				BestArea = Area;
				Best = i;
			}
		}
		return Best;
	};
	for (int32 i = 0; i < DetectedRoomPolygons.Num(); ++i)
	{
		if (SmallestRoomAround(RoomInteriorPoints[i]) == i) continue;
		const TArray<FVector2D>& Poly = DetectedRoomPolygons[i];
		bool bFound = false;
		for (const float Inset : { 30.f, 60.f, 100.f })
		{
			for (int32 Edge = 0; Edge < Poly.Num() && !bFound; ++Edge)
			{
				const FVector2D Candidate = (Poly[Edge] + Poly[(Edge + 1) % Poly.Num()]) * 0.5f + PlannerFinishLayout::InwardEdgeNormal(Poly, Edge) * Inset;
				if (SmallestRoomAround(Candidate) == i)
				{
					RoomInteriorPoints[i] = Candidate;
					bFound = true;
				}
			}
			if (bFound) break;
		}
	}

	auto AnchorsOf = [](const TArray<FFloorFinishRecord>& Records)
	{
		TArray<FVector2D> Anchors;
		for (const FFloorFinishRecord& Rec : Records) Anchors.Add(Rec.Anchor);
		return Anchors;
	};
	const TArray<int32> FloorRecordOfRoom = PlannerFinishLayout::AssignRecordsToRooms(AnchorsOf(FloorFinishes), DetectedRoomPolygons, RoomCentroids, 100.f);
	const TArray<int32> CeilingRecordOfRoom = PlannerFinishLayout::AssignRecordsToRooms(AnchorsOf(CeilingFinishes), DetectedRoomPolygons, RoomCentroids, 100.f);
	const TArray<int32> BaseboardRecordOfRoom = PlannerFinishLayout::AssignRecordsToRooms(AnchorsOf(BaseboardFinishes), DetectedRoomPolygons, RoomCentroids, 100.f);
	// Each record follows its room (moved walls reshape rooms step by step; the anchor stays inside the room it belongs to).
	for (int32 i = 0; i < DetectedRoomPolygons.Num(); ++i)
	{
		if (FloorFinishes.IsValidIndex(FloorRecordOfRoom[i])) FloorFinishes[FloorRecordOfRoom[i]].Anchor = RoomInteriorPoints[i];
		if (CeilingFinishes.IsValidIndex(CeilingRecordOfRoom[i])) CeilingFinishes[CeilingRecordOfRoom[i]].Anchor = RoomInteriorPoints[i];
		if (BaseboardFinishes.IsValidIndex(BaseboardRecordOfRoom[i])) BaseboardFinishes[BaseboardRecordOfRoom[i]].Anchor = RoomInteriorPoints[i];
	}

	// 6. Generate Procedural Meshes for every detected room
	for (int32 RoomIdx = 0; RoomIdx < DetectedRoomPolygons.Num(); ++RoomIdx)
	{
		const TArray<FVector2D>& FloorPolygon = DetectedRoomPolygons[RoomIdx];
		int32 VertCount = FloorPolygon.Num();
		if (VertCount < 3) continue;

		// Calculate room area
		float TwiceArea = 0.f;
		for (int32 i = 0; i < VertCount; ++i)
		{
			const FVector2D& P1 = FloorPolygon[i];
			const FVector2D& P2 = FloorPolygon[(i + 1) % VertCount];
			TwiceArea += (P1.X * P2.Y - P2.X * P1.Y);
		}

		FRoomData Room;
		Room.RoomID = RoomIdx + 1;
		Room.FloorPolygon = FloorPolygon;
		Room.AreaM2 = TwiceArea * 0.5f / 10000.f;

		// Centroid (the key finishes were stored by before), interior point, and this room's own finish records.
		Room.Centroid = RoomCentroids[RoomIdx];
		Room.InteriorPoint = RoomInteriorPoints[RoomIdx];
		if (FloorFinishes.IsValidIndex(FloorRecordOfRoom[RoomIdx])) Room.FloorFinish = FloorFinishes[FloorRecordOfRoom[RoomIdx]].Finish;
		if (CeilingFinishes.IsValidIndex(CeilingRecordOfRoom[RoomIdx])) Room.CeilingFinish = CeilingFinishes[CeilingRecordOfRoom[RoomIdx]].Finish;
		if (BaseboardFinishes.IsValidIndex(BaseboardRecordOfRoom[RoomIdx])) Room.BaseboardFinish = BaseboardFinishes[BaseboardRecordOfRoom[RoomIdx]].Finish;

		// Half thickness of the wall on each outline edge (the floor / ceiling tile grid starts at the interior wall-face corner), and
		// which wall that is (thresholds of its doors belong to the floor).
		TArray<float> EdgeHalfThickness;
		EdgeHalfThickness.Init(10.f, VertCount);
		TArray<int32> EdgeSegment;
		EdgeSegment.Init(INDEX_NONE, VertCount);
		TArray<bool> EdgeForward;
		EdgeForward.Init(true, VertCount);
		for (int32 i = 0; i < VertCount; ++i)
		{
			const FVector2D P1 = FloorPolygon[i];
			const FVector2D P2 = FloorPolygon[(i + 1) % VertCount];
			for (const TPair<int32, FWallSegment>& SegPair : WallSegments)
			{
				const FWallNode* SegStart = Nodes.Find(SegPair.Value.StartNodeID);
				const FWallNode* SegEnd = Nodes.Find(SegPair.Value.EndNodeID);
				if (!SegStart || !SegEnd) continue;
				const bool bForward = SegStart->Position.Equals(P1, 0.5f) && SegEnd->Position.Equals(P2, 0.5f);
				const bool bBackward = SegStart->Position.Equals(P2, 0.5f) && SegEnd->Position.Equals(P1, 0.5f);
				if (!bForward && !bBackward) continue;
				EdgeHalfThickness[i] = SegPair.Value.Thickness * 0.5f;
				EdgeSegment[i] = SegPair.Key;
				EdgeForward[i] = bForward;
				break;
			}
		}

		// Floor / ceiling tile grid of this room: from a corner of its interior wall faces along its longest wall; metric UVs (REQ-13).
		PlannerFinishLayout::ComputeRoomSurfaceFrame(FloorPolygon, EdgeHalfThickness, Room.SurfaceUVOrigin, Room.SurfaceUVAxisU, Room.SurfaceUVAxisV);

		// The room's own floor: its clear area, out to the inner faces of its walls (not under them, so neighbouring rooms' floors never
		// overlap), and its net area.
		Room.NetFloorPolygon = PlannerFinishLayout::InteriorFacePolygon(FloorPolygon, EdgeHalfThickness);
		Room.AreaM2 = (float)(FMath::Abs(PlannerFinishLayout::SignedArea(Room.NetFloorPolygon)) / 10000.0);
		auto SurfaceUV = [&Room](const FVector2D& P)
		{
			return PlannerFinishLayout::RoomSurfaceUV(P, Room.SurfaceUVOrigin, Room.SurfaceUVAxisU, Room.SurfaceUVAxisV);
		};
		Rooms.Add(Room.RoomID, Room);

		// Triangles of the centre-line outline (the ceiling) and of the clear floor outline.
		const TArray<int32> TriangulatedIndices = EarClip(FloorPolygon);
		const TArray<FVector2D>& NetPolygon = Room.NetFloorPolygon;
		const TArray<int32> NetIndices = EarClip(NetPolygon);

		// 6.1. Generate Floor Mesh Section
		if (FloorProceduralMesh)
		{
			TArray<FVector> Vertices;
			TArray<int32> Triangles;
			TArray<FVector> Normals;
			TArray<FVector2D> UVs;
			TArray<FColor> FloorColors;

			const int32 NetCount = NetPolygon.Num();

			// Top face (Z=1.f)
			for (int32 i = 0; i < NetCount; ++i)
			{
				Vertices.Add(FVector(NetPolygon[i].X, NetPolygon[i].Y, 1.f));
				Normals.Add(FVector::UpVector);
				UVs.Add(SurfaceUV(NetPolygon[i]));
				FloorColors.Add(FColor(255, 255, 255, 255));
			}
			Triangles = NetIndices;

			// Bottom face (Z=0.f)
			int32 StartIdx = Vertices.Num();
			for (int32 i = 0; i < NetCount; ++i)
			{
				Vertices.Add(FVector(NetPolygon[i].X, NetPolygon[i].Y, 0.f));
				Normals.Add(-FVector::UpVector);
				UVs.Add(SurfaceUV(NetPolygon[i]));
				FloorColors.Add(FColor(255, 255, 255, 255));
			}
			for (int32 i = 0; i < NetIndices.Num(); i += 3)
			{
				Triangles.Add(StartIdx + NetIndices[i]);
				Triangles.Add(StartIdx + NetIndices[i + 2]);
				Triangles.Add(StartIdx + NetIndices[i + 1]);
			}

			// Side faces for 1cm thickness
			for (int32 i = 0; i < NetCount; ++i)
			{
				FVector2D P1 = NetPolygon[i];
				FVector2D P2 = NetPolygon[(i + 1) % NetCount];
				FVector2D EdgeDir = (P2 - P1).GetSafeNormal();
				FVector2D EdgeNorm(EdgeDir.Y, -EdgeDir.X);
				FVector OutNormal(EdgeNorm.X, EdgeNorm.Y, 0.f);

				int32 SIdx = Vertices.Num();
				Vertices.Add(FVector(P2.X, P2.Y, 0.f));
				Vertices.Add(FVector(P1.X, P1.Y, 0.f));
				Vertices.Add(FVector(P1.X, P1.Y, 1.f));
				Vertices.Add(FVector(P2.X, P2.Y, 1.f));

				for(int k=0; k<4; k++) { Normals.Add(OutNormal); UVs.Add(FVector2D::ZeroVector); FloorColors.Add(FColor(255, 255, 255, 255)); }

				Triangles.Add(SIdx + 0); Triangles.Add(SIdx + 1); Triangles.Add(SIdx + 2);
				Triangles.Add(SIdx + 0); Triangles.Add(SIdx + 2); Triangles.Add(SIdx + 3);
			}

			// Thresholds: through a door or archway the floor runs on from the inner face to the middle of its wall, where the next
			// room's floor (or the outside) begins.
			for (int32 i = 0; i < VertCount; ++i)
			{
				if (EdgeSegment[i] == INDEX_NONE) continue;
				TArray<FVector2D> Spans;
				GetWalkThroughSpans(EdgeSegment[i], Spans);
				if (Spans.Num() == 0) continue;
				const FVector2D E1 = FloorPolygon[i];
				const FVector2D E2 = FloorPolygon[(i + 1) % VertCount];
				const float EdgeLen = FVector2D::Distance(E1, E2);
				if (EdgeLen < 1.f) continue;
				const FVector2D EdgeDir = (E2 - E1) / EdgeLen;
				const FVector2D Inward = PlannerFinishLayout::InwardEdgeNormal(FloorPolygon, i) * EdgeHalfThickness[i];
				for (const FVector2D& Span : Spans)
				{
					const float A0 = FMath::Clamp(EdgeForward[i] ? (float)Span.X : EdgeLen - (float)Span.Y, 0.f, EdgeLen);
					const float A1 = FMath::Clamp(EdgeForward[i] ? (float)Span.Y : EdgeLen - (float)Span.X, A0, EdgeLen);
					if (A1 - A0 < 1.f) continue;
					const FVector2D Q[4] = { E1 + EdgeDir * A0, E1 + EdgeDir * A1, E1 + EdgeDir * A1 + Inward, E1 + EdgeDir * A0 + Inward };
					const int32 TopIdx = Vertices.Num();
					for (int32 k = 0; k < 4; ++k)
					{
						Vertices.Add(FVector(Q[k].X, Q[k].Y, 1.f));
						Normals.Add(FVector::UpVector);
						UVs.Add(SurfaceUV(Q[k]));
						FloorColors.Add(FColor(255, 255, 255, 255));
					}
					Triangles.Append({ TopIdx + 2, TopIdx + 1, TopIdx + 0, TopIdx + 3, TopIdx + 2, TopIdx + 0 });
					const int32 BottomIdx = Vertices.Num();
					for (int32 k = 0; k < 4; ++k)
					{
						Vertices.Add(FVector(Q[k].X, Q[k].Y, 0.f));
						Normals.Add(-FVector::UpVector);
						UVs.Add(SurfaceUV(Q[k]));
						FloorColors.Add(FColor(255, 255, 255, 255));
					}
					Triangles.Append({ BottomIdx + 0, BottomIdx + 1, BottomIdx + 2, BottomIdx + 0, BottomIdx + 2, BottomIdx + 3 });
				}
			}

			FloorProceduralMesh->CreateMeshSection(RoomIdx, Vertices, Triangles, Normals, UVs, FloorColors, TArray<FProcMeshTangent>(), bCookCollision);
			UMaterialInterface* FloorMat = ResolveFloorMaterialForRoom(Rooms[RoomIdx + 1]);
			FloorProceduralMesh->SetMaterial(RoomIdx, FloorMat ? FloorMat : BaseMat);
		}

		// 6.2. Generate Ceiling Mesh Section
		if (CeilingProceduralMesh)
		{
			TArray<FVector> CeilVerts;
			TArray<int32> CeilTris;
			TArray<FVector> CeilNorms;
			TArray<FVector2D> CeilUVs;
			TArray<FColor> CeilColors;

			// Ceiling sits on the tallest wall of this room (walls whose both corners are polygon vertices).
			float CeilZ = 0.f;
			for (const auto& SegPair : WallSegments)
			{
				const FWallNode* SNode = Nodes.Find(SegPair.Value.StartNodeID);
				const FWallNode* ENode = Nodes.Find(SegPair.Value.EndNodeID);
				if (!SNode || !ENode) continue;
				bool bStartOnPoly = false, bEndOnPoly = false;
				for (const FVector2D& PV : FloorPolygon)
				{
					if (FVector2D::DistSquared(PV, SNode->Position) < 4.f) bStartOnPoly = true;
					if (FVector2D::DistSquared(PV, ENode->Position) < 4.f) bEndOnPoly = true;
				}
				if (bStartOnPoly && bEndOnPoly)
				{
					CeilZ = FMath::Max(CeilZ, SegPair.Value.Height);
				}
			}
			if (CeilZ <= 0.f) CeilZ = 280.f;
			if (FRoomData* RoomRec = Rooms.Find(RoomIdx + 1)) RoomRec->CeilingHeightCm = CeilZ;

			// The ceiling is a closed slab (bottom face at CeilZ, top face above it, side faces around the
			// polygon), built like the floor slab. A single downward-facing sheet is invisible to the shadow
			// pass from above (back faces are culled for directional / VSM shadows), so sun and sky light would
			// pass straight through it; the slab has a front face towards every light and closes the room for
			// Lumen and ray-traced reflections as well.
			const float CeilTop = CeilZ + CeilingThicknessCm;
			const FColor CeilColor(240, 240, 240, 255);

			// Bottom face (Z = CeilZ), normals down, seen from inside the room.
			for (int32 i = 0; i < VertCount; ++i)
			{
				CeilVerts.Add(FVector(FloorPolygon[i].X, FloorPolygon[i].Y, CeilZ));
				CeilNorms.Add(-FVector::UpVector);
				CeilUVs.Add(SurfaceUV(FloorPolygon[i]));
				CeilColors.Add(CeilColor);
			}
			for (int32 i = 0; i < TriangulatedIndices.Num(); i += 3)
			{
				CeilTris.Add(TriangulatedIndices[i]);
				CeilTris.Add(TriangulatedIndices[i + 2]);
				CeilTris.Add(TriangulatedIndices[i + 1]);
			}

			// Top face (Z = CeilTop), normals up: this is the face the sun and the sky see.
			{
				const int32 TopStart = CeilVerts.Num();
				for (int32 i = 0; i < VertCount; ++i)
				{
					CeilVerts.Add(FVector(FloorPolygon[i].X, FloorPolygon[i].Y, CeilTop));
					CeilNorms.Add(FVector::UpVector);
					CeilUVs.Add(SurfaceUV(FloorPolygon[i]));
					CeilColors.Add(CeilColor);
				}
				for (int32 i = 0; i < TriangulatedIndices.Num(); i += 3)
				{
					CeilTris.Add(TopStart + TriangulatedIndices[i]);
					CeilTris.Add(TopStart + TriangulatedIndices[i + 1]);
					CeilTris.Add(TopStart + TriangulatedIndices[i + 2]);
				}
			}

			// Side faces (same construction and winding as the floor slab's sides).
			for (int32 i = 0; i < VertCount; ++i)
			{
				const FVector2D P1 = FloorPolygon[i];
				const FVector2D P2 = FloorPolygon[(i + 1) % VertCount];
				const FVector2D EdgeDir = (P2 - P1).GetSafeNormal();
				const FVector2D EdgeNorm(EdgeDir.Y, -EdgeDir.X);
				const FVector OutNormal(EdgeNorm.X, EdgeNorm.Y, 0.f);

				const int32 SIdx = CeilVerts.Num();
				CeilVerts.Add(FVector(P2.X, P2.Y, CeilZ));
				CeilVerts.Add(FVector(P1.X, P1.Y, CeilZ));
				CeilVerts.Add(FVector(P1.X, P1.Y, CeilTop));
				CeilVerts.Add(FVector(P2.X, P2.Y, CeilTop));
				for (int k = 0; k < 4; ++k) { CeilNorms.Add(OutNormal); CeilUVs.Add(FVector2D::ZeroVector); CeilColors.Add(CeilColor); }
				CeilTris.Add(SIdx + 0); CeilTris.Add(SIdx + 1); CeilTris.Add(SIdx + 2);
				CeilTris.Add(SIdx + 0); CeilTris.Add(SIdx + 2); CeilTris.Add(SIdx + 3);
			}

			CeilingProceduralMesh->CreateMeshSection(RoomIdx, CeilVerts, CeilTris, CeilNorms, CeilUVs, CeilColors, TArray<FProcMeshTangent>(), bCookCollision);
			UMaterialInterface* CeilMat = Room.CeilingFinish.IsSet() ? GetFinishMaterial(Room.CeilingFinish) : nullptr;
			if (!CeilMat) CeilMat = CeilMatInst ? CeilMatInst : BaseMat;
			CeilingSectionMaterials.Add(Room.RoomID, CeilMat);
			CeilingProceduralMesh->SetMaterial(RoomIdx, CeilMat);
		}

	}

	// 6.3. Baseboards: along every wall face that looks into a room. Walls that close no room (partitions ending inside a room,
	// free-standing walls) are pruned from the room outlines above, so baseboards follow the walls themselves (REQ-13).
	BaseboardDefaultMaterial = BbMatInst ? BbMatInst : BaseMat;
	RebuildBaseboards(BaseboardDefaultMaterial);

	if (FloorProceduralMesh) FloorProceduralMesh->SetVisibility(true);
	if (CeilingProceduralMesh) CeilingProceduralMesh->SetVisibility(bCeilingVisible && !b2DViewMode);
	if (BaseboardProceduralMesh) BaseboardProceduralMesh->SetVisibility(true);

	// Which face of every wall looks into a room (drives door/window swing direction, REQ-07).
	ComputeWallInteriorSides(DetectedRoomPolygons);

	// Room lights follow the rooms (shape / size / height); resolved on the next Tick in 3D or on entering 3D.
	bRoomLightsDirty = true;
}


TSharedPtr<FJsonObject> ARoomPlannerManager::FinishToJson(const FSurfaceFinish& Finish)
{
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
	Obj->SetStringField(TEXT("type"), PlannerJsonKeys::FinishTypeToString(Finish.Type));
	Obj->SetStringField(TEXT("code"), Finish.ColorCode);
	PlannerJsonKeys::SetUnit(Obj, TEXT("r"), Finish.Color.R);
	PlannerJsonKeys::SetUnit(Obj, TEXT("g"), Finish.Color.G);
	PlannerJsonKeys::SetUnit(Obj, TEXT("b"), Finish.Color.B);
	PlannerJsonKeys::SetUnit(Obj, TEXT("a"), Finish.Color.A);
	Obj->SetStringField(TEXT("tile"), Finish.TileAssetID);
	PlannerJsonKeys::SetCm(Obj, TEXT("tileSize"), Finish.TileSizeCm);
	return Obj;
}

FSurfaceFinish ARoomPlannerManager::FinishFromJson(const TSharedPtr<FJsonObject>& Obj)
{
	FSurfaceFinish Finish;
	if (!Obj.IsValid())
	{
		return Finish;
	}
	FString TypeStr;
	if (Obj->TryGetStringField(TEXT("type"), TypeStr))
	{
		Finish.Type = PlannerJsonKeys::FinishTypeFromString(TypeStr);
	}
	Obj->TryGetStringField(TEXT("code"), Finish.ColorCode);
	double R = 1.0, G = 1.0, B = 1.0, A = 1.0;
	Obj->TryGetNumberField(TEXT("r"), R);
	Obj->TryGetNumberField(TEXT("g"), G);
	Obj->TryGetNumberField(TEXT("b"), B);
	Obj->TryGetNumberField(TEXT("a"), A);
	Finish.Color = FLinearColor((float)R, (float)G, (float)B, (float)A);
	Obj->TryGetStringField(TEXT("tile"), Finish.TileAssetID);
	double TileSize = 30.0;
	if (Obj->TryGetNumberField(TEXT("tileSize"), TileSize))
	{
		Finish.TileSizeCm = (float)TileSize;
	}
	return Finish;
}

FString ARoomPlannerManager::ExportLayoutToJSON() const
{
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject());
	RootObject->SetNumberField(TEXT("version"), 3); // 3: rooms divided by walls are separate rooms (finishes per room)

	// Serializing Nodes
	TArray<TSharedPtr<FJsonValue>> NodesArray;
	for (const TPair<int32, FWallNode>& Pair : Nodes)
	{
		TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject());
		NodeObj->SetNumberField(TEXT("id"), Pair.Key);
		PlannerJsonKeys::SetCm(NodeObj, TEXT("x"), Pair.Value.Position.X);
		PlannerJsonKeys::SetCm(NodeObj, TEXT("y"), Pair.Value.Position.Y);
		NodesArray.Add(MakeShareable(new FJsonValueObject(NodeObj)));
	}
	RootObject->SetArrayField(TEXT("nodes"), NodesArray);

	// Serializing Wall Segments (+ guid, finish, opening swing)
	TArray<TSharedPtr<FJsonValue>> WallsArray;
	for (const TPair<int32, FWallSegment>& Pair : WallSegments)
	{
		TSharedPtr<FJsonObject> WallObj = MakeShareable(new FJsonObject());
		WallObj->SetNumberField(TEXT("id"), Pair.Key);
		WallObj->SetStringField(TEXT("guid"), Pair.Value.WallGuid);
		WallObj->SetNumberField(TEXT("start"), Pair.Value.StartNodeID);
		WallObj->SetNumberField(TEXT("end"), Pair.Value.EndNodeID);
		PlannerJsonKeys::SetCm(WallObj, TEXT("thickness"), Pair.Value.Thickness);
		PlannerJsonKeys::SetCm(WallObj, TEXT("height"), Pair.Value.Height);
		WallObj->SetObjectField(TEXT("finish"), FinishToJson(Pair.Value.Finish));
		WallObj->SetObjectField(TEXT("finishRight"), FinishToJson(Pair.Value.FinishRight));

		TArray<TSharedPtr<FJsonValue>> OpeningsArray;
		for (const FWallOpening& Op : Pair.Value.Openings)
		{
			TSharedPtr<FJsonObject> OpObj = MakeShareable(new FJsonObject());
			OpObj->SetStringField(TEXT("id"), Op.OpeningID);
			const TCHAR* TypeStr = TEXT("door");
			if (Op.Type == EOpeningType::Window) TypeStr = TEXT("window");
			else if (Op.Type == EOpeningType::Archway) TypeStr = TEXT("archway");
			OpObj->SetStringField(TEXT("type"), TypeStr);
			PlannerJsonKeys::SetCm(OpObj, TEXT("dist"), Op.DistanceFromStart);
			PlannerJsonKeys::SetCm(OpObj, TEXT("width"), Op.Width);
			PlannerJsonKeys::SetCm(OpObj, TEXT("height"), Op.Height);
			PlannerJsonKeys::SetCm(OpObj, TEXT("sill"), Op.SillHeight);
			OpObj->SetStringField(TEXT("swingSide"), Op.SwingSide == EOpeningSwingSide::Right ? TEXT("right") : TEXT("left"));
			OpObj->SetStringField(TEXT("swingDir"), Op.SwingDirection == EOpeningSwingDirection::Outward ? TEXT("out") : TEXT("in"));
			if (!Op.Style.IsNone())
			{
				OpObj->SetStringField(TEXT("style"), Op.Style.ToString());
			}
			if (Op.TrimFinish.IsSet())
			{
				OpObj->SetObjectField(TEXT("trim"), FinishToJson(Op.TrimFinish));
			}
			OpeningsArray.Add(MakeShareable(new FJsonValueObject(OpObj)));
		}
		WallObj->SetArrayField(TEXT("openings"), OpeningsArray);
		WallsArray.Add(MakeShareable(new FJsonValueObject(WallObj)));
	}
	RootObject->SetArrayField(TEXT("walls"), WallsArray);

	// Serializing Rooms Stats (derived; informational for the browser / save record)
	TArray<TSharedPtr<FJsonValue>> RoomsArray;
	for (const TPair<int32, FRoomData>& Pair : Rooms)
	{
		TSharedPtr<FJsonObject> RoomObj = MakeShareable(new FJsonObject());
		RoomObj->SetNumberField(TEXT("id"), Pair.Key);
		PlannerJsonKeys::SetRounded(RoomObj, TEXT("area_m2"), Pair.Value.AreaM2, 3);
		PlannerJsonKeys::SetCm(RoomObj, TEXT("cx"), Pair.Value.Centroid.X);
		PlannerJsonKeys::SetCm(RoomObj, TEXT("cy"), Pair.Value.Centroid.Y);
		RoomObj->SetObjectField(TEXT("finish"), FinishToJson(Pair.Value.FloorFinish));
		RoomsArray.Add(MakeShareable(new FJsonValueObject(RoomObj)));
	}
	RootObject->SetArrayField(TEXT("rooms"), RoomsArray);

	// Floor finishes (authoritative, keyed by room centroid)
	TArray<TSharedPtr<FJsonValue>> FloorFinishArray;
	for (const FFloorFinishRecord& Rec : FloorFinishes)
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
		PlannerJsonKeys::SetCm(Obj, TEXT("x"), Rec.Anchor.X);
		PlannerJsonKeys::SetCm(Obj, TEXT("y"), Rec.Anchor.Y);
		Obj->SetObjectField(TEXT("finish"), FinishToJson(Rec.Finish));
		FloorFinishArray.Add(MakeShareable(new FJsonValueObject(Obj)));
	}
	RootObject->SetArrayField(TEXT("floorFinishes"), FloorFinishArray);

	// Ceiling / baseboard finishes (same format as floorFinishes)
	RootObject->SetArrayField(TEXT("ceilingFinishes"), RoomFinishRecordsToJson(CeilingFinishes));
	RootObject->SetArrayField(TEXT("baseboardFinishes"), RoomFinishRecordsToJson(BaseboardFinishes));

	// Placed interior objects (REQ-17)
	TArray<TSharedPtr<FJsonValue>> ObjectsArray;
	for (const TPair<FString, FPlacedFurnitureData>& Pair : PlacedObjects)
	{
		const FPlacedFurnitureData& D = Pair.Value;
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
		Obj->SetStringField(TEXT("id"), D.InstanceID);
		Obj->SetStringField(TEXT("asset"), D.AssetID);
		PlannerJsonKeys::SetCm(Obj, TEXT("x"), D.Location.X);
		PlannerJsonKeys::SetCm(Obj, TEXT("y"), D.Location.Y);
		PlannerJsonKeys::SetCm(Obj, TEXT("z"), D.Location.Z);
		PlannerJsonKeys::SetDegrees(Obj, TEXT("pitch"), D.Rotation.Pitch);
		PlannerJsonKeys::SetDegrees(Obj, TEXT("yaw"), D.Rotation.Yaw);
		PlannerJsonKeys::SetDegrees(Obj, TEXT("roll"), D.Rotation.Roll);
		PlannerJsonKeys::SetUnit(Obj, TEXT("sx"), D.Scale.X);
		PlannerJsonKeys::SetUnit(Obj, TEXT("sy"), D.Scale.Y);
		PlannerJsonKeys::SetUnit(Obj, TEXT("sz"), D.Scale.Z);
		Obj->SetStringField(TEXT("material"), D.CustomMaterialID);
		Obj->SetObjectField(TEXT("finish"), FinishToJson(D.Finish));
		if (D.WallAttachment.IsAttached())
		{
			Obj->SetObjectField(TEXT("wall"), AttachmentToJson(D.WallAttachment));
		}
		ObjectsArray.Add(MakeShareable(new FJsonValueObject(Obj)));
	}
	RootObject->SetArrayField(TEXT("objects"), ObjectsArray);

	// Placed cabinet sets (REQ-18)
	TArray<TSharedPtr<FJsonValue>> SetsArray;
	for (const TPair<FString, FPlacedCabinetSetData>& Pair : CabinetSets)
	{
		const FPlacedCabinetSetData& D = Pair.Value;
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
		Obj->SetStringField(TEXT("id"), D.InstanceID);
		Obj->SetStringField(TEXT("product"), D.ProductID.ToString());
		PlannerJsonKeys::SetCm(Obj, TEXT("x"), D.Location.X);
		PlannerJsonKeys::SetCm(Obj, TEXT("y"), D.Location.Y);
		PlannerJsonKeys::SetCm(Obj, TEXT("z"), D.Location.Z);
		PlannerJsonKeys::SetDegrees(Obj, TEXT("pitch"), D.Rotation.Pitch);
		PlannerJsonKeys::SetDegrees(Obj, TEXT("yaw"), D.Rotation.Yaw);
		PlannerJsonKeys::SetDegrees(Obj, TEXT("roll"), D.Rotation.Roll);
		if (D.WallAttachment.IsAttached())
		{
			Obj->SetObjectField(TEXT("wall"), AttachmentToJson(D.WallAttachment));
		}
		SetsArray.Add(MakeShareable(new FJsonValueObject(Obj)));
	}
	RootObject->SetArrayField(TEXT("cabinetSets"), SetsArray);

	FString OutputString;
	TSharedRef<PlannerJsonKeys::FLayoutJsonWriter> Writer = PlannerJsonKeys::FLayoutJsonWriterFactory::Create(&OutputString);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	return OutputString;
}

bool ARoomPlannerManager::ImportLayoutFromJSON(const FString& JSONString)
{
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JSONString);

	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		return false;
	}

	// Walls / nodes / rooms are rebuilt from scratch (established full-reimport model);
	// placed objects and cabinet sets are RECONCILED by InstanceID so they do not flicker.
	// The selection is held as an index, and the import renumbers the openings of a wall. Remember which opening it was.
	const FString SelectedOpeningIDBeforeImport = GetOpeningID(SelectedSegmentID, SelectedOpeningIndex);

	ClearWallsAndRooms();
	++LayoutImportDepth; // walls / openings below rebuild once at the end of the import

	// Read Nodes
	const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
	if (RootObject->TryGetArrayField(TEXT("nodes"), NodesArray) && NodesArray)
	{
		for (const TSharedPtr<FJsonValue>& Val : *NodesArray)
		{
			TSharedPtr<FJsonObject> NodeObj = Val->AsObject();
			if (NodeObj.IsValid())
			{
				int32 ID = NodeObj->GetIntegerField(TEXT("id"));
				float X = NodeObj->GetNumberField(TEXT("x"));
				float Y = NodeObj->GetNumberField(TEXT("y"));

				FWallNode Node;
				Node.NodeID = ID;
				Node.Position = FVector2D(X, Y);
				Nodes.Add(ID, Node);

				if (ID >= NextNodeID) NextNodeID = ID + 1;
			}
		}
	}

	// Read Floor finishes BEFORE walls so RebuildRooms (triggered by AddWall) can associate them.
	FloorFinishes.Empty();
	const TArray<TSharedPtr<FJsonValue>>* FloorFinishArray = nullptr;
	if (RootObject->TryGetArrayField(TEXT("floorFinishes"), FloorFinishArray) && FloorFinishArray)
	{
		for (const TSharedPtr<FJsonValue>& Val : *FloorFinishArray)
		{
			TSharedPtr<FJsonObject> Obj = Val->AsObject();
			if (Obj.IsValid())
			{
				FFloorFinishRecord Rec;
				Rec.Anchor = FVector2D(Obj->GetNumberField(TEXT("x")), Obj->GetNumberField(TEXT("y")));
				const TSharedPtr<FJsonObject>* FinishObj = nullptr;
				if (Obj->TryGetObjectField(TEXT("finish"), FinishObj) && FinishObj)
				{
					Rec.Finish = FinishFromJson(*FinishObj);
				}
				if (Rec.Finish.IsSet())
				{
					FloorFinishes.Add(Rec);
				}
			}
		}
	}

	RoomFinishRecordsFromJson(RootObject, TEXT("ceilingFinishes"), CeilingFinishes);
	RoomFinishRecordsFromJson(RootObject, TEXT("baseboardFinishes"), BaseboardFinishes);

	// Read Walls
	const TArray<TSharedPtr<FJsonValue>>* WallsArray = nullptr;
	if (RootObject->TryGetArrayField(TEXT("walls"), WallsArray) && WallsArray)
	{
		for (const TSharedPtr<FJsonValue>& Val : *WallsArray)
		{
			TSharedPtr<FJsonObject> WallObj = Val->AsObject();
			if (WallObj.IsValid())
			{
				int32 StartID = WallObj->GetIntegerField(TEXT("start"));
				int32 EndID = WallObj->GetIntegerField(TEXT("end"));
				float Thickness = WallObj->GetNumberField(TEXT("thickness"));
				float Height = WallObj->GetNumberField(TEXT("height"));

				// Keep the wall's own id, as the nodes above keep theirs: every command a client sends names a wall by this
				// number, and renumbering the walls here (a layout with a deleted wall has gaps) would make the client talk
				// about one wall while the server hears another.
				double SavedSegID = 0.0;
				WallObj->TryGetNumberField(TEXT("id"), SavedSegID);
				int32 SegID = AddWall(StartID, EndID, Thickness > 0.f ? Thickness : 20.f, Height > 0.f ? Height : 280.f,
					SavedSegID > 0.0 ? (int32)SavedSegID : -1);

				if (SegID != -1)
				{
					if (FWallSegment* Seg = WallSegments.Find(SegID))
					{
						FString Guid;
						if (WallObj->TryGetStringField(TEXT("guid"), Guid) && !Guid.IsEmpty())
						{
							Seg->WallGuid = Guid;
						}
						ReadWallFaceFinishes(WallObj, Seg->Finish, Seg->FinishRight);
					}
				}

				const TArray<TSharedPtr<FJsonValue>>* OpeningsArray = nullptr;
				if (SegID != -1 && WallObj->TryGetArrayField(TEXT("openings"), OpeningsArray) && OpeningsArray)
				{
					for (const TSharedPtr<FJsonValue>& OpVal : *OpeningsArray)
					{
						TSharedPtr<FJsonObject> OpObj = OpVal->AsObject();
						if (OpObj.IsValid())
						{
							FString TypeStr = OpObj->GetStringField(TEXT("type"));
							EOpeningType Type = EOpeningType::Door;
							if (TypeStr.Equals(TEXT("window"), ESearchCase::IgnoreCase)) Type = EOpeningType::Window;
							else if (TypeStr.Equals(TEXT("archway"), ESearchCase::IgnoreCase)) Type = EOpeningType::Archway;
							float Dist = OpObj->GetNumberField(TEXT("dist"));
							float Width = OpObj->GetNumberField(TEXT("width"));
							float OpHeight = OpObj->GetNumberField(TEXT("height"));
							float Sill = OpObj->GetNumberField(TEXT("sill"));

							if (AddOpeningToWall(SegID, Type, Dist, Width, OpHeight, Sill))
							{
								if (FWallSegment* Seg = WallSegments.Find(SegID))
								{
									if (Seg->Openings.Num() > 0)
									{
										FWallOpening& NewOp = Seg->Openings.Last();
										FString SideStr, DirStr, StyleStr, OpIDStr;
										// Every replicated edit re-imports the whole layout, so an opening that does not carry its own id across
										// is a different opening each time and loses its leaf. A layout written by the old, repeatable scheme can
										// hold the same id twice on one wall: such a duplicate keeps the freshly generated one.
										if (OpObj->TryGetStringField(TEXT("id"), OpIDStr) && !OpIDStr.IsEmpty()
											&& !Seg->Openings.ContainsByPredicate([&OpIDStr](const FWallOpening& O) { return O.OpeningID == OpIDStr; }))
										{
											NewOp.OpeningID = OpIDStr;
										}
										if (OpObj->TryGetStringField(TEXT("style"), StyleStr) && !StyleStr.IsEmpty())
										{
											NewOp.Style = FName(*StyleStr); // unknown IDs fall back to the type's default style when drawn
										}
										const TSharedPtr<FJsonObject>* TrimObj = nullptr;
										if (OpObj->TryGetObjectField(TEXT("trim"), TrimObj) && TrimObj)
										{
											NewOp.TrimFinish = FinishFromJson(*TrimObj);
										}
										if (OpObj->TryGetStringField(TEXT("swingSide"), SideStr))
										{
											NewOp.SwingSide = SideStr.Equals(TEXT("right"), ESearchCase::IgnoreCase) ? EOpeningSwingSide::Right : EOpeningSwingSide::Left;
										}
										if (OpObj->TryGetStringField(TEXT("swingDir"), DirStr))
										{
											NewOp.SwingDirection = DirStr.Equals(TEXT("out"), ESearchCase::IgnoreCase) ? EOpeningSwingDirection::Outward : EOpeningSwingDirection::Inward;
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	// Read placed objects (REQ-17)
	PlacedObjects.Empty();
	const TArray<TSharedPtr<FJsonValue>>* ObjectsArray = nullptr;
	if (RootObject->TryGetArrayField(TEXT("objects"), ObjectsArray) && ObjectsArray)
	{
		for (const TSharedPtr<FJsonValue>& Val : *ObjectsArray)
		{
			TSharedPtr<FJsonObject> Obj = Val->AsObject();
			if (!Obj.IsValid()) continue;

			FPlacedFurnitureData D;
			D.InstanceID = Obj->GetStringField(TEXT("id"));
			D.AssetID = Obj->GetStringField(TEXT("asset"));
			if (D.InstanceID.IsEmpty()) continue;
			D.Location = FVector(Obj->GetNumberField(TEXT("x")), Obj->GetNumberField(TEXT("y")), Obj->GetNumberField(TEXT("z")));
			D.Rotation = FRotator(Obj->GetNumberField(TEXT("pitch")), Obj->GetNumberField(TEXT("yaw")), Obj->GetNumberField(TEXT("roll")));
			double Sx = 1.0, Sy = 1.0, Sz = 1.0;
			Obj->TryGetNumberField(TEXT("sx"), Sx); Obj->TryGetNumberField(TEXT("sy"), Sy); Obj->TryGetNumberField(TEXT("sz"), Sz);
			D.Scale = FVector((float)Sx, (float)Sy, (float)Sz);
			Obj->TryGetStringField(TEXT("material"), D.CustomMaterialID);
			const TSharedPtr<FJsonObject>* FinishObj = nullptr;
			if (Obj->TryGetObjectField(TEXT("finish"), FinishObj) && FinishObj)
			{
				D.Finish = FinishFromJson(*FinishObj);
			}
			const TSharedPtr<FJsonObject>* WallObjPtr = nullptr;
			if (Obj->TryGetObjectField(TEXT("wall"), WallObjPtr) && WallObjPtr)
			{
				D.WallAttachment = AttachmentFromJson(*WallObjPtr);
			}
			PlacedObjects.Add(D.InstanceID, D);
		}
	}

	// Read cabinet sets (REQ-18)
	CabinetSets.Empty();
	const TArray<TSharedPtr<FJsonValue>>* SetsArray = nullptr;
	if (RootObject->TryGetArrayField(TEXT("cabinetSets"), SetsArray) && SetsArray)
	{
		for (const TSharedPtr<FJsonValue>& Val : *SetsArray)
		{
			TSharedPtr<FJsonObject> Obj = Val->AsObject();
			if (!Obj.IsValid()) continue;

			FPlacedCabinetSetData D;
			D.InstanceID = Obj->GetStringField(TEXT("id"));
			D.ProductID = FName(*Obj->GetStringField(TEXT("product")));
			if (D.InstanceID.IsEmpty()) continue;
			D.Location = FVector(Obj->GetNumberField(TEXT("x")), Obj->GetNumberField(TEXT("y")), Obj->GetNumberField(TEXT("z")));
			D.Rotation = FRotator(Obj->GetNumberField(TEXT("pitch")), Obj->GetNumberField(TEXT("yaw")), Obj->GetNumberField(TEXT("roll")));
			const TSharedPtr<FJsonObject>* WallObjPtr = nullptr;
			if (Obj->TryGetObjectField(TEXT("wall"), WallObjPtr) && WallObjPtr)
			{
				D.WallAttachment = AttachmentFromJson(*WallObjPtr);
			}
			CabinetSets.Add(D.InstanceID, D);
		}
	}

	--LayoutImportDepth;
	// Rooms first: they decide each wall's interior side (hinge side, sill boards), which the JSON does not carry, so the
	// walls are then built exactly once with the right side.
	bWallsRebuiltByInteriorPass = false;
	RebuildRooms();
	double LayoutVersion = 2.0;
	RootObject->TryGetNumberField(TEXT("version"), LayoutVersion);
	if (LayoutVersion < 3.0)
	{
		SpreadLegacyRoomFinishes();
	}
	if (!bWallsRebuiltByInteriorPass)
	{
		RebuildAllWalls();
	}
	const bool bWallStandOffCorrected = RebuildPlacedObjectActors();
	ReconcileCabinetSetActors();

	if (!SelectedOpeningIDBeforeImport.IsEmpty())
	{
		// Point at the same opening again; -1 when it is gone, which DropSelectionOfVanishedItems would have done anyway.
		SelectedOpeningIndex = FindOpeningIndexByID(SelectedSegmentID, SelectedOpeningIDBeforeImport);
	}
	DropSelectionOfVanishedItems();
	PruneLeafAnimations();

	UpdateSelectionVisuals();
	RefreshNodeHandles();
	if (bWallStandOffCorrected && HasAuthority())
	{
		// A wall object's mesh or scale no longer matched the stand-off this layout carries (e.g. its catalog row's mesh was swapped
		// since it was saved): it was put back on its wall face above, and the clients need that stand-off too. Published once, here.
		// A client corrects its own copy the same way and never publishes; the authority never re-imports what it publishes.
		CommitStateAfterMutation();
	}
	else
	{
		OnRoomPlannerUpdated.Broadcast(JSONString);
	}
	if (SelectedSegmentID != -1)
	{
		OnWallSelected.Broadcast(SelectedSegmentID, GetWallLength(SelectedSegmentID));
	}
	NotifySelectionChanged();

	return true;
}

FString ARoomPlannerManager::ProcessCommandJSON(const FString& JSONString)
{
	UE_LOG(LogTemp, Warning, TEXT("[MaxiMallConstructor] ProcessCommandJSON received: %s"), *JSONString);

	TSharedPtr<FJsonObject> CommandObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JSONString);

	if (!FJsonSerializer::Deserialize(Reader, CommandObject) || !CommandObject.IsValid())
	{
		return TEXT("{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
	}

	FString Cmd = CommandObject->GetStringField(TEXT("cmd"));

	if (Cmd.Equals(TEXT("add_wall"), ESearchCase::IgnoreCase))
	{
		float X1 = CommandObject->GetNumberField(TEXT("x1"));
		float Y1 = CommandObject->GetNumberField(TEXT("y1"));
		float X2 = CommandObject->GetNumberField(TEXT("x2"));
		float Y2 = CommandObject->GetNumberField(TEXT("y2"));

		int32 N1 = AddNode(FVector2D(X1, Y1));
		int32 N2 = AddNode(FVector2D(X2, Y2));
		int32 SegID = AddWall(N1, N2);
		ReplicatedRoomJSON = ExportLayoutToJSON();

		FString ResponseJSON = FString::Printf(TEXT("{\"status\":\"ok\",\"cmd\":\"add_wall\",\"segment_id\":%d,\"state\":%s}"), SegID, *ReplicatedRoomJSON);
		OnRoomPlannerUpdated.Broadcast(ResponseJSON);
		return ResponseJSON;
	}
	else if (Cmd.Equals(TEXT("add_opening"), ESearchCase::IgnoreCase))
	{
		int32 SegID = CommandObject->GetIntegerField(TEXT("segment_id"));
		FString TypeStr = CommandObject->GetStringField(TEXT("type"));
		EOpeningType Type = TypeStr.Equals(TEXT("window"), ESearchCase::IgnoreCase) ? EOpeningType::Window : EOpeningType::Door;
		float Dist = CommandObject->GetNumberField(TEXT("dist"));
		float Width = CommandObject->HasField(TEXT("width")) ? CommandObject->GetNumberField(TEXT("width")) : 90.f;
		float Height = CommandObject->HasField(TEXT("height")) ? CommandObject->GetNumberField(TEXT("height")) : 210.f;
		float Sill = CommandObject->HasField(TEXT("sill")) ? CommandObject->GetNumberField(TEXT("sill")) : (Type == EOpeningType::Window ? 90.f : 0.f);

		bool bSuccess = AddOpeningToWall(SegID, Type, Dist, Width, Height, Sill);
		ReplicatedRoomJSON = ExportLayoutToJSON();
		FString ResponseJSON = FString::Printf(TEXT("{\"status\":\"%s\",\"cmd\":\"add_opening\",\"state\":%s}"), bSuccess ? TEXT("ok") : TEXT("error"), *ReplicatedRoomJSON);
		OnRoomPlannerUpdated.Broadcast(ResponseJSON);
		return ResponseJSON;
	}
	else if (Cmd.Equals(TEXT("clear"), ESearchCase::IgnoreCase))
	{
		ClearLayout();
		ReplicatedRoomJSON = ExportLayoutToJSON();
		FString ResponseJSON = TEXT("{\"status\":\"ok\",\"cmd\":\"clear\",\"state\":{\"nodes\":[],\"walls\":[],\"rooms\":[]}}");
		OnRoomPlannerUpdated.Broadcast(ResponseJSON);
		return ResponseJSON;
	}
	else if (Cmd.Equals(TEXT("get_state"), ESearchCase::IgnoreCase))
	{
		return FString::Printf(TEXT("{\"status\":\"ok\",\"cmd\":\"get_state\",\"state\":%s}"), *ExportLayoutToJSON());
	}

	return TEXT("{\"status\":\"error\",\"message\":\"unknown_command\"}");
}

float ARoomPlannerManager::CalculateFloorAreaM2() const
{
	float TotalArea = 0.f;
	for (const auto& Pair : Rooms)
	{
		TotalArea += Pair.Value.AreaM2;
	}
	return TotalArea;
}

float ARoomPlannerManager::CalculatePerimeterM() const
{
	float TotalPerimeterCm = 0.f;
	for (const auto& Pair : WallSegments)
	{
		const FWallSegment& Seg = Pair.Value;
		if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
		{
			FVector2D P1 = Nodes[Seg.StartNodeID].Position;
			FVector2D P2 = Nodes[Seg.EndNodeID].Position;
			TotalPerimeterCm += FVector2D::Distance(P1, P2);
		}
	}
	return TotalPerimeterCm / 100.f;
}

void ARoomPlannerManager::SetViewMode(bool bIn2DMode)
{
	const bool bWas2D = b2DViewMode;
	b2DViewMode = bIn2DMode;
	ActiveViewMode = bIn2DMode ? EPlannerViewMode::View2D_TopDown : EPlannerViewMode::View3D_Perspective;

	if (!bIn2DMode)
	{
		bCeilingVisible = true;
		DraggingNodeID = -1; // corner dragging is a 2D-only interaction
		ClearAllSelection();
	}

	if (CeilingProceduralMesh)
	{
		CeilingProceduralMesh->SetVisibility(bCeilingVisible && !bIn2DMode);
	}

	// Room lights are a 3D-only feature: hidden in 2D, shown in 3D. Rooms edited while in 2D are rebuilt now, once.
	if (!bIn2DMode && bRoomLightsDirty)
	{
		RebuildRoomLights();
	}
	for (const auto& Pair : RoomLights)
	{
		if (Pair.Value) Pair.Value->SetShown(!bIn2DMode);
	}
	// Doors / windows: plan symbols and plan-pose leaves in 2D, closed / opened leaves in 3D. Switching from 2D to 3D rebuilds
	// the walls once: 2D rebuilds (every frame while dragging) skip leaf collision, which 3D clicks and hover need.
	if (!bIn2DMode && bWas2D)
	{
		RebuildAllWalls();
	}
	else
	{
		for (const auto& Pair : WallActors)
		{
			if (Pair.Value) Pair.Value->SetPresentation(!bIn2DMode);
		}
	}
	UpdatePlannerExposure();
	UpdateExteriorBackdropVisibility();

	if (bIn2DMode && !bWas2D)
	{
		UpdateSelectionVisuals(); // a selection kept from 3D switches to the whole-wall highlight
	}

	RefreshNodeHandles();
}

bool ARoomPlannerManager::FindWallSnapPoint2D(const FVector2D& Point2D, float SnapRadiusCm, FVector2D& OutSnapPos, int32& OutSnappedSegmentID, bool& bOutIsEndpoint) const
{
	bOutIsEndpoint = false;
	OutSnappedSegmentID = INDEX_NONE;
	float BestDistSq = SnapRadiusCm * SnapRadiusCm;
	bool bFoundSnap = false;

	// 1. First, check if Point2D is near ANY corner endpoint node (within SnapRadiusCm)
	for (const auto& Pair : Nodes)
	{
		float DistSq = FVector2D::DistSquared(Point2D, Pair.Value.Position);
		if (DistSq <= BestDistSq)
		{
			BestDistSq = DistSq;
			OutSnapPos = Pair.Value.Position;
			bOutIsEndpoint = true;
			bFoundSnap = true;
		}
	}

	if (bFoundSnap)
	{
		return true;
	}

	// 2. Check if Point2D is hovering along any wall centerline
	float BestWallDist = SnapRadiusCm + 15.f;
	for (const auto& Pair : WallSegments)
	{
		const FWallSegment& Seg = Pair.Value;
		if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
		{
			FVector2D P1 = Nodes[Seg.StartNodeID].Position;
			FVector2D P2 = Nodes[Seg.EndNodeID].Position;
			FVector2D SegVec = P2 - P1;
			float SegLen = SegVec.Size();
			if (SegLen < 1.0f) continue;

			FVector2D SegDir = SegVec / SegLen;
			float t = FVector2D::DotProduct(Point2D - P1, SegDir);

			float ClampedT = FMath::Clamp(t, 0.f, SegLen);
			FVector2D ProjPoint = P1 + SegDir * ClampedT;
			float DistToCenterline = FVector2D::Distance(Point2D, ProjPoint);

			float HoverTolerance = (Seg.Thickness * 0.5f) + SnapRadiusCm;
			if (DistToCenterline <= HoverTolerance && DistToCenterline < BestWallDist)
			{
				BestWallDist = DistToCenterline;
				OutSnappedSegmentID = Pair.Key;

				// Magnetic snapping threshold to corner endpoints (25cm)
				if (ClampedT <= 25.f)
				{
					OutSnapPos = P1;
					bOutIsEndpoint = true;
				}
				else if (ClampedT >= (SegLen - 25.f))
				{
					OutSnapPos = P2;
					bOutIsEndpoint = true;
				}
				else
				{
					OutSnapPos = ProjPoint; // Always on Centerline!
					bOutIsEndpoint = false;
				}
				bFoundSnap = true;
			}
		}
	}

	return bFoundSnap;
}

bool ARoomPlannerManager::FindWallEndpointSnap2D(const FVector2D& Point2D, float SnapRadiusCm, FVector2D& OutSnapPos) const
{
	int32 DummySegID = INDEX_NONE;
	bool bDummyIsEndpoint = false;
	return FindWallSnapPoint2D(Point2D, SnapRadiusCm, OutSnapPos, DummySegID, bDummyIsEndpoint);
}

void ARoomPlannerManager::CheckHoverSnapHint(const FVector& WorldPos)
{
	FVector2D Pos2D(WorldPos.X, WorldPos.Y);
	FVector2D SnapPos2D;
	bool bSnapped = FindWallEndpointSnap2D(Pos2D, 30.f, SnapPos2D);

	FVector SnapPointWorld = bSnapped ? FVector(SnapPos2D.X, SnapPos2D.Y, 0.f) : WorldPos;

	bHasActiveHoverSnap = bSnapped;
	CurrentHoverSnapWorldPos = SnapPointWorld;
	HoverSnapFrame = GFrameCounter;
	OnInteractiveWallDragProgress.Broadcast(0.f, SnapPointWorld, 0.f, bSnapped);
}

void ARoomPlannerManager::StartInteractiveWallDraw(const FVector& WorldPos)
{
	bIsDrawingWall = true;

	if (bHasActiveHoverSnap)
	{
		DragStartPoint = CurrentHoverSnapWorldPos;
	}
	else
	{
		DragStartPoint = WorldPos;
		FVector2D Start2D(WorldPos.X, WorldPos.Y);
		FVector2D SnapStart2D;
		if (FindWallEndpointSnap2D(Start2D, 30.f, SnapStart2D))
		{
			DragStartPoint.X = SnapStart2D.X;
			DragStartPoint.Y = SnapStart2D.Y;
		}
	}

	DragCurrentPoint = DragStartPoint;

	if (!PreviewWallActor && GetWorld())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		PreviewWallActor = GetWorld()->SpawnActor<AProceduralWallActor>(AProceduralWallActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		if (PreviewWallActor)
		{
			PreviewWallActor->SetActorEnableCollision(false);
			PreviewWallActor->BaseWallMaterial = DefaultWallMaterial;
			PreviewWallActor->WallSelectionMaterial = WallSelectionMaterial;
			PreviewWallActor->OpeningSelectionMaterial = OpeningSelectionMaterial;
			if (PreviewWallActor->WallProceduralMesh)
			{
				PreviewWallActor->WallProceduralMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}
	}

	if (PreviewWallActor)
	{
		PreviewWallActor->SetActorHiddenInGame(false);
	}
}

void ARoomPlannerManager::UpdateInteractiveWallDraw(const FVector& WorldPos)
{
	if (!bIsDrawingWall) return;

	DragCurrentPoint = WorldPos;

	FVector2D P1(DragStartPoint.X, DragStartPoint.Y);
	FVector2D P2(DragCurrentPoint.X, DragCurrentPoint.Y);

	float CurrentLengthCm = FVector2D::Distance(P1, P2);

	FVector2D SnapP2;
	bool bSnappedToOtherWall = false;

	// Only attempt to snap P2 to other walls if P2 has moved far enough from P1 (> 30cm)
	if (CurrentLengthCm > 30.f)
	{
		bSnappedToOtherWall = FindWallEndpointSnap2D(P2, 30.f, SnapP2);
		// Don't snap P2 back to P1's position
		if (bSnappedToOtherWall && FVector2D::Distance(SnapP2, P1) > 20.f)
		{
			P2 = SnapP2;
			DragCurrentPoint.X = P2.X;
			DragCurrentPoint.Y = P2.Y;
		}
		else
		{
			bSnappedToOtherWall = false;
		}
	}

	float LengthCm = FVector2D::Distance(P1, P2);

	if (LengthCm > 1.f)
	{
		float AngleRad = FMath::Atan2(P2.Y - P1.Y, P2.X - P1.X);
		float AngleDeg = FMath::RadiansToDegrees(AngleRad);
		float NormAngle = FMath::UnwindDegrees(AngleDeg);

		static const float SnapAngles[] = { 0.f, 45.f, 90.f, 135.f, 180.f, -45.f, -90.f, -135.f, -180.f };
		float ThresholdDeg = 8.0f;
		bool bSnappedToAngle = false;
		float TargetSnapDeg = NormAngle;

		if (!bSnappedToOtherWall)
		{
			for (float SnapAngle : SnapAngles)
			{
				if (FMath::Abs(FMath::UnwindDegrees(NormAngle - SnapAngle)) <= ThresholdDeg)
				{
					TargetSnapDeg = SnapAngle;
					bSnappedToAngle = true;
					break;
				}
			}

			if (bSnappedToAngle)
			{
				float SnapRad = FMath::DegreesToRadians(TargetSnapDeg);
				P2.X = P1.X + LengthCm * FMath::Cos(SnapRad);
				P2.Y = P1.Y + LengthCm * FMath::Sin(SnapRad);
				DragCurrentPoint.X = P2.X;
				DragCurrentPoint.Y = P2.Y;
			}
		}

		if (PreviewWallActor)
		{
			PreviewWallActor->RebuildWallMesh(P1, P2, FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector, true, true, false);
		}

		float LengthMeters = LengthCm / 100.f;
		FVector MidpointWorld = (DragStartPoint + DragCurrentPoint) * 0.5f;
		OnInteractiveWallDragProgress.Broadcast(LengthMeters, MidpointWorld, TargetSnapDeg, (bSnappedToOtherWall || bSnappedToAngle));
	}
}

void ARoomPlannerManager::CommitInteractiveWallDraw()
{
	if (!bIsDrawingWall) return;

	bIsDrawingWall = false;
	if (PreviewWallActor)
	{
		PreviewWallActor->SetActorHiddenInGame(true);
	}

	FVector2D P1(DragStartPoint.X, DragStartPoint.Y);
	FVector2D P2(DragCurrentPoint.X, DragCurrentPoint.Y);

	if (FVector2D::Distance(P1, P2) >= 10.f)
	{
		if (HasAuthority())
		{
			// Joined along its length like Server_CommitWall's wall (which follows and then finds every piece already there).
			AddWallBetweenPoints(P1, P2, 20.f, 280.f);
			ReplicatedRoomJSON = ExportLayoutToJSON();
			OnRoomPlannerUpdated.Broadcast(ReplicatedRoomJSON);
		}
	}
}

void ARoomPlannerManager::CancelInteractiveWallDraw()
{
	bIsDrawingWall = false;
	if (PreviewWallActor)
	{
		PreviewWallActor->SetActorHiddenInGame(true);
	}
}

void ARoomPlannerManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ARoomPlannerManager, ReplicatedRoomJSON);
}

void ARoomPlannerManager::OnRep_ReplicatedRoomJSON()
{
	bOpeningDragPublishPending = false; // the authority has spoken; a local drag preview is settled either way

	// Clients only. The server edited this layout in place and published it; re-importing here would clear the walls and
	// rooms it has just built and respawn every wall actor from its own JSON.
	if (HasAuthority()) return;

	ImportLayoutFromJSON(ReplicatedRoomJSON);
}


void ARoomPlannerManager::SetToolMode(EPlannerToolMode NewToolMode)
{
	const bool bChanged = (ActiveToolMode != NewToolMode);
	ActiveToolMode = NewToolMode;
	if (ActiveToolMode != EPlannerToolMode::Select)
	{
		ClearAllSelection();
	}
	if (ActiveToolMode != EPlannerToolMode::PlaceFurniture)
	{
		PendingPlacementKind = EPlannerPlacementKind::None;
		PendingPlacementAssetID.Empty();
		PendingPlacementYawDeg = 0.f;
	}
	// Only an actual tool change cancels a corner drag; re-asserting Select on every click must not.
	if (bChanged && DraggingNodeID != -1)
	{
		DraggingNodeID = -1;
	}
	if (bChanged)
	{
		bHasActiveHoverSnap = false; // the Draw Wall hover preview belongs to that tool only
	}
	RefreshNodeHandles();
}

void ARoomPlannerManager::TickLocalNodeDrag()
{
	if (!b2DViewMode || ActiveToolMode != EPlannerToolMode::Select || !bPlannerUIOpen || !GetWorld())
	{
		// The drag is over whatever the reason. Leaving the id set would keep ShouldCookCollision false for good, and with it
		// a layout that no trace can hit.
		DraggingNodeID = -1;
		bPrevLMBDownForNodeDrag = false;
		return;
	}

	// Local player controller only (never the dedicated server / remote controllers).
	AAwsTutorial_PlayerController* LocalPC = nullptr;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AAwsTutorial_PlayerController* PC = Cast<AAwsTutorial_PlayerController>(It->Get()))
		{
			if (PC->IsLocalController()) { LocalPC = PC; break; }
		}
	}
	if (!LocalPC)
	{
		DraggingNodeID = -1; // nobody is driving the drag any more (see above)
		bPrevLMBDownForNodeDrag = false;
		return;
	}

	// Slate's own pressed-button set: valid even when a widget consumed the press before PlayerInput saw it.
	const bool bLMBDown = FSlateApplication::IsInitialized() && FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
	const bool bJustPressed = bLMBDown && !bPrevLMBDownForNodeDrag;
	const bool bJustReleased = !bLMBDown && bPrevLMBDownForNodeDrag;
	bPrevLMBDownForNodeDrag = bLMBDown;

	// Cursor ray; the handle test and the drag position both use the dragged node's handle plane (wall top),
	// so the disc stays under the cursor and the clickable area equals the visible disc.
	FVector RayOrigin = FVector::ZeroVector, RayDir = FVector::ZeroVector;
	const bool bHasRay = LocalPC->DeprojectMousePositionToWorld(RayOrigin, RayDir) && !FMath::IsNearlyZero(RayDir.Z);

	if (DraggingNodeID == -1)
	{
		// Start: LMB pressed this frame over a corner handle (whatever click path is live), but not a press on the planner's UI
		// (a button above a handle, the side panel, a catalog beside it): Slate's pressed set holds those presses too.
		if (bJustPressed && bHasRay && !bIsDrawingWall && !IsCursorOverPlannerUI())
		{
			const int32 NodeID = FindNodeAtCursorRay(RayOrigin, RayDir, 25.f);
			if (NodeID != -1)
			{
				StartNodeDrag(NodeID);
			}
		}
		return;
	}

	if (bLMBDown)
	{
		FVector DragPos;
		if (bHasRay && ProjectCursorRayToNodeHandlePlane(DraggingNodeID, RayOrigin, RayDir, DragPos))
		{
			UpdateNodeDrag(DragPos);
		}
	}
	else if (bJustReleased || !bLMBDown)
	{
		int32 NodeID = -1;
		FVector2D FinalPos;
		if (EndNodeDrag(NodeID, FinalPos))
		{
			LocalPC->Server_MoveNode(NodeID, FinalPos);
		}
	}
}

void ARoomPlannerManager::ClearWallSelection()
{
	SelectedSegmentID = -1;
	SelectedOpeningIndex = -1;
	bSelectionHighlightSuppressed = false;
	for (auto& Pair : WallActors)
	{
		if (Pair.Value)
		{
			Pair.Value->SetSelectedHighlight(false);
			Pair.Value->ClearAllOpeningHighlights();
		}
	}
	OnWallSelected.Broadcast(-1, 0.f);
	NotifySelectionChanged();
}

int32 ARoomPlannerManager::SelectWallAtWorldPos(const FVector& WorldPos)
{
	if (ActiveToolMode != EPlannerToolMode::Select)
	{
		ClearWallSelection();
		return -1;
	}

	// REQ-02: a press on a corner handle (2D only) is a drag, not a wall pick — whichever
	// click path (native, tick, or Blueprint) called us. The current selection is left untouched.
	FVector CursorOrigin = FVector::ZeroVector;
	FVector CursorDir = FVector::ZeroVector;
	bool bRayTested = false;
	if (b2DViewMode)
	{
		if (DraggingNodeID != -1)
		{
			return SelectedSegmentID;
		}
		// The handle lives on the wall top: test the cursor ray against the handle planes. The ground-XY test
		// is only a fallback for callers without a local cursor (never the case in-game).
		int32 HandleNodeID = -1;
		if (UWorld* World = GetWorld())
		{
			if (APlayerController* LocalPC = World->GetFirstPlayerController())
			{
				FVector O, D;
				if (LocalPC->DeprojectMousePositionToWorld(O, D) && !FMath::IsNearlyZero(D.Z))
				{
					HandleNodeID = FindNodeAtCursorRay(O, D, 25.f);
					bRayTested = true;
					CursorOrigin = O;
					CursorDir = D;
				}
			}
		}
		if (!bRayTested) HandleNodeID = FindNodeAtWorldPos(WorldPos, 25.f);
		if (HandleNodeID != -1)
		{
			StartNodeDrag(HandleNodeID);
			return SelectedSegmentID;
		}
	}

	FVector2D Click2D(WorldPos.X, WorldPos.Y);
	int32 ClosestSegID = -1;
	float MinDistSq = 22500.f;

	for (const auto& Pair : WallSegments)
	{
		const FWallSegment& Seg = Pair.Value;
		if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
		{
			FVector2D P1 = Nodes[Seg.StartNodeID].Position;
			FVector2D P2 = Nodes[Seg.EndNodeID].Position;

			FVector2D Dir = P2 - P1;
			float LengthSq = Dir.SizeSquared();
			if (LengthSq > 0.001f)
			{
				float t = FMath::Clamp(FVector2D::DotProduct(Click2D - P1, Dir) / LengthSq, 0.f, 1.f);
				FVector2D ClosestPoint = P1 + t * Dir;
				float DistSq = FVector2D::DistSquared(Click2D, ClosestPoint);
				if (DistSq < MinDistSq)
				{
					MinDistSq = DistSq;
					ClosestSegID = Pair.Key;
				}
			}
		}
	}

	// A wall / opening pick replaces any floor / object / cabinet-set selection.
	if (ClosestSegID != -1)
	{
		SelectedRoomID = -1;
		SelectedObjectID.Empty();
		SelectedCabinetSetID.Empty();
	}

	bSelectionHighlightSuppressed = false; // an explicit pick always shows the normal highlight again
	SelectedSegmentID = ClosestSegID;
	SelectedOpeningIndex = -1;
	float LengthMeters = 0.f;

	if (SelectedSegmentID != -1 && WallSegments.Contains(SelectedSegmentID))
	{
		const FWallSegment& Seg = WallSegments[SelectedSegmentID];
		FVector2D P1 = Nodes[Seg.StartNodeID].Position;
		FVector2D P2 = Nodes[Seg.EndNodeID].Position;
		float SegLen = FVector2D::Distance(P1, P2);
		LengthMeters = SegLen / 100.f;

		if (SegLen > 0.1f)
		{
			FVector2D SegDir = (P2 - P1) / SegLen;
			float ClickDistAlongWall = FVector2D::DotProduct(Click2D - P1, SegDir);
			// The face a finish goes to (REQ-13). The plan shows wall tops, and in its perspective top-down view the ground point
			// under a clicked wall top lies beside the wall, so the side of that point says little: the face looking into a room at the
			// click wins; on a wall with a room on both sides (or none) it is the side of the wall top that was clicked.
			const FVector2D LeftNormal(-SegDir.Y, SegDir.X);
			const FVector2D AtClick = P1 + SegDir * FMath::Clamp(ClickDistAlongWall, 0.f, SegLen);
			const float ProbeDistance = Seg.Thickness * 0.5f + 5.f;
			const FVector2D ProbeLeft = AtClick + LeftNormal * ProbeDistance;
			const FVector2D ProbeRight = AtClick - LeftNormal * ProbeDistance;
			const bool bRoomLeft = FindRoomAtWorldPos(FVector(ProbeLeft.X, ProbeLeft.Y, 0.f)) != -1;
			const bool bRoomRight = FindRoomAtWorldPos(FVector(ProbeRight.X, ProbeRight.Y, 0.f)) != -1;
			if (bRoomLeft != bRoomRight)
			{
				bSelectedWallFaceLeft = bRoomLeft;
			}
			else
			{
				FVector2D SidePoint = Click2D;
				if (bRayTested)
				{
					const double T = (Seg.Height - CursorOrigin.Z) / CursorDir.Z;
					if (T > 0.0)
					{
						const FVector OnTop = CursorOrigin + CursorDir * T;
						SidePoint = FVector2D(OnTop.X, OnTop.Y);
					}
				}
				bSelectedWallFaceLeft = FVector2D::DotProduct(SidePoint - P1, LeftNormal) >= 0.f;
			}

			for (int32 OpIdx = 0; OpIdx < Seg.Openings.Num(); ++OpIdx)
			{
				const FWallOpening& Op = Seg.Openings[OpIdx];
				if (FMath::Abs(ClickDistAlongWall - Op.DistanceFromStart) <= (Op.Width * 0.5f + 25.f))
				{
					SelectedOpeningIndex = OpIdx;
					break;
				}
			}
		}
	}

	UpdateSelectionVisuals();

	OnWallSelected.Broadcast(SelectedSegmentID, LengthMeters);
	NotifySelectionChanged();
	return SelectedSegmentID;
}

void ARoomPlannerManager::SetSelectionHighlightSuppressed(bool bSuppressed)
{
	if (bSelectionHighlightSuppressed != bSuppressed)
	{
		bSelectionHighlightSuppressed = bSuppressed;
		UpdateSelectionVisuals();
	}
}

void ARoomPlannerManager::UpdateSelectionVisuals()
{
	// Visual-only switch: the logical selection is unchanged, only the highlight is hidden while suppressed.
	const bool bShowHighlight = !bSelectionHighlightSuppressed;

	for (auto& Pair : WallActors)
	{
		if (Pair.Value)
		{
			bool bIsWallSelected = (Pair.Key == SelectedSegmentID) && bShowHighlight;
			bool bHighlightWall = bIsWallSelected;
			bool bHighlightOpening = false;

			if (bIsWallSelected && SelectedOpeningIndex != -1)
			{
				bHighlightWall = false;
				bHighlightOpening = true;
			}

			// 3D: the selected face shows the highlight (REQ-13 per-face selection). 2D: the whole wall, as the plan shows wall tops.
			Pair.Value->SetSelectedFaceHighlight(bHighlightWall, b2DViewMode ? INDEX_NONE
				: (bSelectedWallFaceLeft ? AProceduralWallActor::LeftFaceSection : AProceduralWallActor::RightFaceSection));
			Pair.Value->ClearAllOpeningHighlights();

			if (bHighlightOpening)
			{
				Pair.Value->SetOpeningSelectedHighlight(SelectedOpeningIndex, true, 2);
			}
		}
	}

	// Floors: selected room shows the selection material, every other room its finish / default material.
	if (FloorProceduralMesh)
	{
		const int32 NumSections = FloorProceduralMesh->GetNumSections();
		for (const auto& Pair : Rooms)
		{
			const int32 SectionIdx = Pair.Key - 1;
			if (SectionIdx < 0 || SectionIdx >= NumSections) continue;

			UMaterialInterface* Mat = nullptr;
			if (Pair.Key == SelectedRoomID && SelectedRoomSurface == EPlannerSelectionKind::Floor && bShowHighlight && WallSelectionMaterial)
			{
				Mat = WallSelectionMaterial;
			}
			else if (const TObjectPtr<UMaterialInterface>* Found = FloorSectionMaterials.Find(Pair.Key))
			{
				Mat = Found->Get();
			}
			if (Mat)
			{
				FloorProceduralMesh->SetMaterial(SectionIdx, Mat);
			}
		}
	}

	// Ceilings and baseboards: like floors (REQ-13).
	auto ApplyRoomSurfaceSelection = [&](UProceduralMeshComponent* Mesh, const TMap<int32, TObjectPtr<UMaterialInterface>>& Materials, EPlannerSelectionKind Surface)
	{
		if (!Mesh) return;
		const int32 NumSections = Mesh->GetNumSections();
		for (const auto& Pair : Rooms)
		{
			const int32 SectionIdx = Pair.Key - 1;
			if (SectionIdx < 0 || SectionIdx >= NumSections) continue;

			UMaterialInterface* Mat = nullptr;
			if (Pair.Key == SelectedRoomID && SelectedRoomSurface == Surface && bShowHighlight && WallSelectionMaterial)
			{
				Mat = WallSelectionMaterial;
			}
			else if (const TObjectPtr<UMaterialInterface>* Found = Materials.Find(Pair.Key))
			{
				Mat = Found->Get();
			}
			if (Mat)
			{
				Mesh->SetMaterial(SectionIdx, Mat);
			}
		}
	};
	ApplyRoomSurfaceSelection(CeilingProceduralMesh, CeilingSectionMaterials, EPlannerSelectionKind::Ceiling);
	ApplyRoomSurfaceSelection(BaseboardProceduralMesh, BaseboardSectionMaterials, EPlannerSelectionKind::Baseboard);

	// Placed objects
	for (auto& Pair : PlacedObjectActors)
	{
		if (Pair.Value)
		{
			Pair.Value->SetSelectedHighlight(Pair.Key == SelectedObjectID && bShowHighlight);
		}
	}

	// Cabinet sets (custom depth outline on every mesh of the booth)
	for (const auto& Pair : CabinetSets)
	{
		if (AShowroomBooth* Booth = FindCabinetSetActor(Pair.Key))
		{
			const bool bSel = (Pair.Key == SelectedCabinetSetID) && bShowHighlight;
			TArray<UPrimitiveComponent*> Prims;
			Booth->GetComponents<UPrimitiveComponent>(Prims);
			for (UPrimitiveComponent* Prim : Prims)
			{
				if (Prim)
				{
					Prim->SetRenderCustomDepth(bSel);
					if (bSel) Prim->SetCustomDepthStencilValue(2);
				}
			}
		}
	}
}

float ARoomPlannerManager::GetWallLength(int32 SegmentID) const
{
	if (!WallSegments.Contains(SegmentID)) return 0.f;

	const FWallSegment& Seg = WallSegments[SegmentID];
	if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
	{
		FVector2D P1 = Nodes[Seg.StartNodeID].Position;
		FVector2D P2 = Nodes[Seg.EndNodeID].Position;
		return FVector2D::Distance(P1, P2) / 100.f; // Return in meters
	}
	return 0.f;
}

bool ARoomPlannerManager::SetWallLength(int32 SegmentID, float NewLengthMeters)
{
	if (NewLengthMeters <= 0.1f || !WallSegments.Contains(SegmentID)) return false;

	const FWallSegment& Seg = WallSegments[SegmentID];
	if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
	{
		FVector2D P1 = Nodes[Seg.StartNodeID].Position;
		FVector2D P2 = Nodes[Seg.EndNodeID].Position;
		FVector2D Dir = (P2 - P1).GetSafeNormal();
		float NewLengthCm = NewLengthMeters * 100.f;

		// Shared-node model (intended): the END node moves and every connected wall follows.
		// ApplyNodeMove keeps openings attached and refuses the edit if one no longer fits (REQ-09).
		return ApplyNodeMove(Seg.EndNodeID, P1 + Dir * NewLengthCm, false);
	}
	return false;
}

bool ARoomPlannerManager::SetWallDimensions(int32 SegmentID, float HeightCm, float ThicknessCm)
{
	FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg) return false;

	if (HeightCm < 10.f || HeightCm > 1000.f)
	{
		BroadcastRejected(TEXT("Высота стены должна быть от 10 до 1000 см"));
		return false;
	}
	if (ThicknessCm < 1.f || ThicknessCm > 200.f)
	{
		BroadcastRejected(TEXT("Толщина стены должна быть от 1 до 200 см"));
		return false;
	}

	// Existing openings must still fit under the new height (same rule as opening sizing).
	for (const FWallOpening& Op : Seg->Openings)
	{
		if (Op.SillHeight + Op.Height > HeightCm + 0.5f)
		{
			BroadcastRejected(FString::Printf(TEXT("Проём (%.0f см от пола) выше новой высоты стены %.0f см"), Op.SillHeight + Op.Height, HeightCm));
			return false;
		}
	}

	if (FMath::IsNearlyEqual(Seg->Height, HeightCm, 0.01f) && FMath::IsNearlyEqual(Seg->Thickness, ThicknessCm, 0.01f))
	{
		return true; // nothing to do
	}

	Seg->Height = HeightCm;
	Seg->Thickness = ThicknessCm;

	RebuildWallsAndRooms(); // the corner joints follow the new thickness, the ceiling the new height
	CommitStateAfterMutation();
	UpdateSelectionVisuals();
	return true;
}

bool ARoomPlannerManager::DeleteWallAtWorldPos(const FVector& WorldPos)
{
	int32 TargetSeg = SelectWallAtWorldPos(WorldPos);
	if (TargetSeg != -1)
	{
		RemoveWall(TargetSeg);
		return true;
	}
	return false;
}

static float FindNonOverlappingOpeningDist(const FWallSegment& Seg, float NewWidthCm, float WallLengthCm)
{
	float HalfW = NewWidthCm * 0.5f;
	float DefaultCenter = WallLengthCm * 0.5f;

	if (Seg.Openings.Num() == 0)
	{
		return FMath::Clamp(DefaultCenter, HalfW + 5.f, FMath::Max(HalfW + 5.f, WallLengthCm - HalfW - 5.f));
	}

	TArray<TPair<float, float>> Intervals;
	for (const FWallOpening& Op : Seg.Openings)
	{
		Intervals.Add(TPair<float, float>(Op.DistanceFromStart - Op.Width * 0.5f, Op.DistanceFromStart + Op.Width * 0.5f));
	}
	Intervals.Sort([](const TPair<float, float>& A, const TPair<float, float>& B) {
		return A.Key < B.Key;
	});

	float RightEdge = Intervals.Last().Value;
	float CandidateRight = RightEdge + 15.f + HalfW;
	if (CandidateRight + HalfW <= WallLengthCm - 5.f)
	{
		return CandidateRight;
	}

	float LeftEdge = Intervals[0].Key;
	float CandidateLeft = LeftEdge - 15.f - HalfW;
	if (CandidateLeft - HalfW >= 5.f)
	{
		return CandidateLeft;
	}

	for (int32 i = 0; i < Intervals.Num() - 1; ++i)
	{
		float GapStart = Intervals[i].Value;
		float GapEnd = Intervals[i + 1].Key;
		if (GapEnd - GapStart >= NewWidthCm + 10.f)
		{
			return GapStart + 5.f + HalfW;
		}
	}

	// Cannot find any space
	return -1.0f;
}

bool ARoomPlannerManager::AddDoorToWall(int32 SegmentID, float WidthMeters, float HeightMeters, float DistFromStartCm)
{
	if (!WallSegments.Contains(SegmentID)) return false;

	const FWallSegment& Seg = WallSegments[SegmentID];
	if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
	{
		FVector2D P1 = Nodes[Seg.StartNodeID].Position;
		FVector2D P2 = Nodes[Seg.EndNodeID].Position;
		float WallLengthCm = FVector2D::Distance(P1, P2);

		float WidthCm = WidthMeters * 100.f;
		float HeightCm = HeightMeters * 100.f;

		if (DistFromStartCm < 0.f)
		{
			DistFromStartCm = FindNonOverlappingOpeningDist(Seg, WidthCm, WallLengthCm);
			if (DistFromStartCm < 0.f)
			{
				return false;
			}
		}

		AddOpeningToWall(SegmentID, EOpeningType::Door, DistFromStartCm, WidthCm, HeightCm, 0.f);
		if (SelectedSegmentID == SegmentID)
		{
			SelectedOpeningIndex = WallSegments[SegmentID].Openings.Num() - 1;
			UpdateSelectionVisuals();
		}
		ReplicatedRoomJSON = ExportLayoutToJSON();
		OnRoomPlannerUpdated.Broadcast(ReplicatedRoomJSON);
		return true;
	}
	return false;
}

bool ARoomPlannerManager::AddDoorToSelectedWall(float WidthMeters, float HeightMeters)
{
	if (SelectedSegmentID == -1) return false;
	return AddDoorToWall(SelectedSegmentID, WidthMeters, HeightMeters, -1.f);
}

bool ARoomPlannerManager::AddWindowToWall(int32 SegmentID, float WidthMeters, float HeightMeters, float SillHeightMeters, float DistFromStartCm)
{
	if (!WallSegments.Contains(SegmentID)) return false;

	const FWallSegment& Seg = WallSegments[SegmentID];
	if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
	{
		FVector2D P1 = Nodes[Seg.StartNodeID].Position;
		FVector2D P2 = Nodes[Seg.EndNodeID].Position;
		float WallLengthCm = FVector2D::Distance(P1, P2);

		float WidthCm = WidthMeters * 100.f;
		float HeightCm = HeightMeters * 100.f;
		float SillCm = SillHeightMeters * 100.f;

		if (DistFromStartCm < 0.f)
		{
			DistFromStartCm = FindNonOverlappingOpeningDist(Seg, WidthCm, WallLengthCm);
			if (DistFromStartCm < 0.f)
			{
				return false;
			}
		}

		AddOpeningToWall(SegmentID, EOpeningType::Window, DistFromStartCm, WidthCm, HeightCm, SillCm);
		if (SelectedSegmentID == SegmentID)
		{
			SelectedOpeningIndex = WallSegments[SegmentID].Openings.Num() - 1;
			UpdateSelectionVisuals();
		}
		ReplicatedRoomJSON = ExportLayoutToJSON();
		OnRoomPlannerUpdated.Broadcast(ReplicatedRoomJSON);
		return true;
	}
	return false;
}

bool ARoomPlannerManager::AddWindowToSelectedWall(float WidthMeters, float HeightMeters, float SillHeightMeters)
{
	if (SelectedSegmentID == -1) return false;
	return AddWindowToWall(SelectedSegmentID, WidthMeters, HeightMeters, SillHeightMeters, -1.f);
}

void ARoomPlannerManager::BuildPreset4x4mRoom(FVector2D CenterCm)
{
	ClearLayout();

	// Same 4 × 4 m square as before, offset by the requested centre (previously fixed at the world origin).
	int32 N1 = AddNode(CenterCm + FVector2D(-200.f, -200.f));
	int32 N2 = AddNode(CenterCm + FVector2D(200.f, -200.f));
	int32 N3 = AddNode(CenterCm + FVector2D(200.f, 200.f));
	int32 N4 = AddNode(CenterCm + FVector2D(-200.f, 200.f));

	AddWall(N1, N2);
	AddWall(N2, N3);
	AddWall(N3, N4);
	AddWall(N4, N1);

	RebuildRooms();
	ReplicatedRoomJSON = ExportLayoutToJSON();
	OnRoomPlannerUpdated.Broadcast(ReplicatedRoomJSON);
}

bool ARoomPlannerManager::GetOpeningDetails(int32 SegmentID, int32 OpeningIndex, float& OutWidthMeters, float& OutHeightMeters, float& OutSillHeightMeters) const
{
	if (WallSegments.Contains(SegmentID))
	{
		const FWallSegment& Seg = WallSegments[SegmentID];
		// No fallback to opening 0: an index of -1 is a selection that is not there, or a FindOpeningIndexByID that failed.
		if (Seg.Openings.IsValidIndex(OpeningIndex))
		{
			const FWallOpening& Op = Seg.Openings[OpeningIndex];
			OutWidthMeters = Op.Width / 100.f;
			OutHeightMeters = Op.Height / 100.f;
			OutSillHeightMeters = Op.SillHeight / 100.f;
			return true;
		}
	}
	return false;
}

bool ARoomPlannerManager::GetOpeningDistance(int32 SegmentID, int32 OpeningIndex, float& OutDistFromStartCm) const
{
	if (WallSegments.Contains(SegmentID))
	{
		const FWallSegment& Seg = WallSegments[SegmentID];
		if (Seg.Openings.IsValidIndex(OpeningIndex))
		{
			OutDistFromStartCm = Seg.Openings[OpeningIndex].DistanceFromStart;
			return true;
		}
	}
	return false;
}

FString ARoomPlannerManager::GetOpeningID(int32 SegmentID, int32 OpeningIndex) const
{
	const FWallSegment* Seg = WallSegments.Find(SegmentID);
	return (Seg && Seg->Openings.IsValidIndex(OpeningIndex)) ? Seg->Openings[OpeningIndex].OpeningID : FString();
}

int32 ARoomPlannerManager::FindOpeningIndexByID(int32 SegmentID, const FString& OpeningID) const
{
	const FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg || OpeningID.IsEmpty()) return INDEX_NONE;
	return Seg->Openings.IndexOfByPredicate([&OpeningID](const FWallOpening& Opening) { return Opening.OpeningID == OpeningID; });
}

bool ARoomPlannerManager::UpdateOpeningPosition(int32 SegmentID, int32 OpeningIndex, float NewDistFromStartCm, bool bLocalPreviewOnly)
{
	if (!WallSegments.Contains(SegmentID)) return false;

	FWallSegment& Seg = WallSegments[SegmentID];
	if (Seg.Openings.IsValidIndex(OpeningIndex))
	{
		if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
		{
			FVector2D P1 = Nodes[Seg.StartNodeID].Position;
			FVector2D P2 = Nodes[Seg.EndNodeID].Position;
			float WallLen = FVector2D::Distance(P1, P2);
			float HalfW = Seg.Openings[OpeningIndex].Width * 0.5f;

			float WallMinDist = HalfW + 5.f;
			float WallMaxDist = WallLen - HalfW - 5.f;
			if (WallMinDist > WallMaxDist)
			{
				WallMinDist = WallLen * 0.5f;
				WallMaxDist = WallMinDist;
			}

			float CandidateDist = FMath::Clamp(NewDistFromStartCm, WallMinDist, WallMaxDist);
			FString DraggedOpID = Seg.Openings[OpeningIndex].OpeningID;

			// Push clear of the openings it would overlap, so a drag can pass one. One pass only resolves one neighbour: the
			// push can land on top of the next one, and which one wins would depend on the array order — and that order
			// differs between the machine that drags and the machine that commits.
			for (int32 Pass = 0; Pass < Seg.Openings.Num(); ++Pass)
			{
				bool bMovedThisPass = false;
				for (int32 i = 0; i < Seg.Openings.Num(); ++i)
				{
					if (i == OpeningIndex) continue;
					const FWallOpening& Other = Seg.Openings[i];
					float OtherHalfW = Other.Width * 0.5f;
					float MinClearance = HalfW + OtherHalfW + 5.f;

					if (FMath::Abs(CandidateDist - Other.DistanceFromStart) < MinClearance)
					{
						const float Pushed = CandidateDist >= Other.DistanceFromStart
							? FMath::Min(WallMaxDist, Other.DistanceFromStart + MinClearance)  // clear of its far side
							: FMath::Max(WallMinDist, Other.DistanceFromStart - MinClearance); // clear of its near side
						bMovedThisPass |= !FMath::IsNearlyEqual(Pushed, CandidateDist, 0.01f);
						CandidateDist = Pushed;
					}
				}
				if (!bMovedThisPass) break;
			}

			// Nowhere on this wall clears every neighbour (a crowded wall): keep the opening where it is rather than writing
			// an overlap, which the wall builder answers by dropping one of the two holes.
			{
				FWallOpening Candidate = Seg.Openings[OpeningIndex];
				Candidate.DistanceFromStart = CandidateDist;
				FString Reason;
				if (!ValidateOpeningFits(Seg, WallLen, Candidate, OpeningIndex, Reason))
				{
					if (!bLocalPreviewOnly) BroadcastRejected(Reason);
					return false;
				}
			}

			// A drag frame that clamps and snaps back to the distance the opening already has changes nothing: rebuilding every
			// wall and every room for it is pure cost. A commit still has to publish — on a listen server it repeats the distance
			// its own preview already applied, and returning silently there would strand the layout in the last exported state.
			if (FMath::IsNearlyEqual(Seg.Openings[OpeningIndex].DistanceFromStart, CandidateDist, 0.01f))
			{
				if (!bLocalPreviewOnly)
				{
					ReplicatedRoomJSON = ExportLayoutToJSON();
					OnRoomPlannerUpdated.Broadcast(ReplicatedRoomJSON);
				}
				return true;
			}

			Seg.Openings[OpeningIndex].DistanceFromStart = CandidateDist;

			// Sort openings by distance from start to maintain topological order
			Seg.Openings.Sort([](const FWallOpening& A, const FWallOpening& B) {
				return A.DistanceFromStart < B.DistanceFromStart;
			});

			// Re-acquire index of the dragged opening after sorting
			int32 NewDraggedIndex = Seg.Openings.IndexOfByPredicate([&](const FWallOpening& Op) {
				return Op.OpeningID == DraggedOpID;
			});

			if (NewDraggedIndex != INDEX_NONE && SelectedSegmentID == SegmentID)
			{
				SelectedOpeningIndex = NewDraggedIndex;
			}

			if (TObjectPtr<AProceduralWallActor>* ActorPtr = WallActors.Find(SegmentID))
			{
				if (*ActorPtr)
				{
					(*ActorPtr)->WallData = Seg;
				}
			}

			RebuildWallsAndRooms(); // floor thresholds and baseboard gaps follow the opening (also in a client's local drag preview)
			UpdateSelectionVisuals();
			if (!bLocalPreviewOnly)
			{
				// Serialising the whole layout and waking every listener is the commit's job; a drag frame only has to look right.
				bOpeningDragPublishPending = false; // committed: the Tick backstop has nothing left to rescue
				ReplicatedRoomJSON = ExportLayoutToJSON();
				OnRoomPlannerUpdated.Broadcast(ReplicatedRoomJSON);
			}
			return true;
		}
	}
	return false;
}

bool ARoomPlannerManager::DragSelectedOpeningToWorldPos(const FVector& WorldPos)
{
	if (SelectedSegmentID == -1 || SelectedOpeningIndex == -1 || !WallSegments.Contains(SelectedSegmentID))
	{
		return false;
	}

	const FWallSegment& Seg = WallSegments[SelectedSegmentID];
	if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
	{
		FVector2D P1 = Nodes[Seg.StartNodeID].Position;
		FVector2D P2 = Nodes[Seg.EndNodeID].Position;
		FVector2D Dir = (P2 - P1).GetSafeNormal();
		float NewDistCm = FVector2D::DotProduct(FVector2D(WorldPos.X, WorldPos.Y) - P1, Dir);
		// Drag frame: the release commits it (AAwsTutorial_PlayerController::Server_UpdateOpeningPosition). Tick watches the
		// two flags below, so a drag that never reaches that release still stops skipping collision and still gets published.
		bOpeningDragActive = true;
		LastOpeningDragSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
		const bool bMoved = UpdateOpeningPosition(SelectedSegmentID, SelectedOpeningIndex, NewDistCm, true);
		bOpeningDragPublishPending |= bMoved;
		return bMoved;
	}
	return false;
}

bool ARoomPlannerManager::UpdateOpeningDimensions(int32 SegmentID, int32 OpeningIndex, float WidthMeters, float HeightMeters, float SillHeightMeters)
{
	if (!WallSegments.Contains(SegmentID)) return false;

	FWallSegment& Seg = WallSegments[SegmentID];
	if (Seg.Openings.IsValidIndex(OpeningIndex) && Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
	{
		const float WallLen = FVector2D::Distance(Nodes[Seg.StartNodeID].Position, Nodes[Seg.EndNodeID].Position);

		// Free numeric sizing (REQ-08), validated against the wall (REQ-09).
		FWallOpening Candidate = Seg.Openings[OpeningIndex];
		Candidate.Width = WidthMeters * 100.f;
		Candidate.Height = HeightMeters * 100.f;
		Candidate.SillHeight = SillHeightMeters * 100.f;

		// If the new width no longer fits at the current position, slide the opening inside the wall first.
		const float HalfW = Candidate.Width * 0.5f;
		if (Candidate.Width <= WallLen)
		{
			Candidate.DistanceFromStart = FMath::Clamp(Candidate.DistanceFromStart, HalfW, WallLen - HalfW);
		}

		FString Reason;
		if (!ValidateOpeningFits(Seg, WallLen, Candidate, OpeningIndex, Reason))
		{
			BroadcastRejected(Reason);
			return false;
		}

		Seg.Openings[OpeningIndex] = Candidate;

		RebuildWallsAndRooms(); // floor thresholds and baseboard gaps follow the opening
		CommitStateAfterMutation();
		UpdateSelectionVisuals();
		return true;
	}
	return false;
}

bool ARoomPlannerManager::DeleteSelectedWall()
{
	if (SelectedSegmentID == -1) return false;

	int32 TargetSeg = SelectedSegmentID;
	SelectedSegmentID = -1;
	SelectedOpeningIndex = -1;
	RemoveWall(TargetSeg);
	return true;
}

bool ARoomPlannerManager::DeleteOpening(int32 SegmentID, int32 OpeningIndex)
{
	if (!WallSegments.Contains(SegmentID)) return false;
	if (!WallSegments[SegmentID].Openings.IsValidIndex(OpeningIndex)) return false;

	WallSegments[SegmentID].Openings.RemoveAt(OpeningIndex);
	RebuildWallsAndRooms(); // the floor threshold and baseboard gap go with the opening
	if (SelectedSegmentID == SegmentID && SelectedOpeningIndex == OpeningIndex)
	{
		SelectedOpeningIndex = -1;
	}
	else if (SelectedSegmentID == SegmentID && SelectedOpeningIndex > OpeningIndex)
	{
		SelectedOpeningIndex--;
	}
	UpdateSelectionVisuals();
	ReplicatedRoomJSON = ExportLayoutToJSON();
	OnRoomPlannerUpdated.Broadcast(ReplicatedRoomJSON);
	OnWallSelected.Broadcast(SelectedSegmentID, GetWallLength(SelectedSegmentID));
	return true;
}

bool ARoomPlannerManager::DeleteSelectedOpening()
{
	if (SelectedSegmentID == -1 || SelectedOpeningIndex == -1) return false;
	return DeleteOpening(SelectedSegmentID, SelectedOpeningIndex);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Internal helpers
// ═══════════════════════════════════════════════════════════════════════════════

void ARoomPlannerManager::CommitStateAfterMutation()
{
	// Wall-attached items follow their wall (length / corner / thickness edits) before the state is published.
	if (HasAuthority())
	{
		RefreshWallAttachedPlacements();
	}
	// A mutation can dissolve the room or delete the wall the user had selected. On a client that is noticed when the layout
	// re-imports; the authority never re-imports its own layout, so it has to notice here.
	DropSelectionOfVanishedItems();
	PruneLeafAnimations();
	ReplicatedRoomJSON = ExportLayoutToJSON();
	OnRoomPlannerUpdated.Broadcast(ReplicatedRoomJSON);
}

void ARoomPlannerManager::DropSelectionOfVanishedItems()
{
	const int32 PrevSegment = SelectedSegmentID;
	const int32 PrevOpening = SelectedOpeningIndex;
	const int32 PrevRoom = SelectedRoomID;
	const FString PrevObject = SelectedObjectID;
	const FString PrevCabinetSet = SelectedCabinetSetID;

	if (SelectedSegmentID != -1 && !WallSegments.Contains(SelectedSegmentID))
	{
		SelectedSegmentID = -1;
		SelectedOpeningIndex = -1;
	}
	else if (SelectedSegmentID != -1 && SelectedOpeningIndex != -1)
	{
		if (!WallSegments[SelectedSegmentID].Openings.IsValidIndex(SelectedOpeningIndex))
		{
			SelectedOpeningIndex = -1;
		}
	}
	if (SelectedRoomID != -1 && !Rooms.Contains(SelectedRoomID))
	{
		SelectedRoomID = -1;
	}
	if (!SelectedObjectID.IsEmpty() && !PlacedObjects.Contains(SelectedObjectID))
	{
		SelectedObjectID.Empty();
	}
	if (!SelectedCabinetSetID.IsEmpty() && !CabinetSets.Contains(SelectedCabinetSetID))
	{
		SelectedCabinetSetID.Empty();
	}

	if (PrevSegment != SelectedSegmentID || PrevOpening != SelectedOpeningIndex || PrevRoom != SelectedRoomID
		|| PrevObject != SelectedObjectID || PrevCabinetSet != SelectedCabinetSetID)
	{
		NotifySelectionChanged(); // the panel is showing properties of something that is no longer there
	}
}

void ARoomPlannerManager::PruneLeafAnimations()
{
	if (LeafAnimations.IsEmpty()) return;

	// The key names an opening (wall guid + opening id) and nothing else drops entries now that the old index fingerprint is
	// gone, so a deleted door would keep its open state for the session — and hand it back to a door restored from a save.
	TSet<FString> Live;
	for (const auto& Pair : WallActors)
	{
		const AProceduralWallActor* Wall = Pair.Value;
		if (!Wall) continue;
		for (int32 OpeningIndex = 0; OpeningIndex < Wall->WallData.Openings.Num(); ++OpeningIndex)
		{
			Live.Add(MakeLeafKey(Wall, OpeningIndex));
		}
	}
	for (auto It = LeafAnimations.CreateIterator(); It; ++It)
	{
		if (!Live.Contains(It.Key())) It.RemoveCurrent();
	}
}

void ARoomPlannerManager::NotifyOperationRejected(const FString& Reason)
{
	BroadcastRejected(Reason);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Wall / floor drop placement
// ═══════════════════════════════════════════════════════════════════════════════

int32 ARoomPlannerManager::FindSegmentIDByGuid(const FString& Guid) const
{
	if (Guid.IsEmpty()) return -1;
	for (const auto& Pair : WallSegments)
	{
		if (Pair.Value.WallGuid == Guid) return Pair.Key;
	}
	return -1;
}

int32 ARoomPlannerManager::FindSegmentIDForWallActor(const AProceduralWallActor* Actor) const
{
	if (!Actor) return -1;
	for (const auto& Pair : WallActors)
	{
		if (Pair.Value == Actor) return Pair.Key;
	}
	return -1;
}

bool ARoomPlannerManager::GetSegmentGeometry(int32 SegmentID, FVector2D& OutStart, FVector2D& OutDir, FVector2D& OutLeftNormal, float& OutLength, float& OutHalfThickness) const
{
	const FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg || !Nodes.Contains(Seg->StartNodeID) || !Nodes.Contains(Seg->EndNodeID)) return false;
	const FVector2D P1 = Nodes[Seg->StartNodeID].Position;
	const FVector2D P2 = Nodes[Seg->EndNodeID].Position;
	OutLength = FVector2D::Distance(P1, P2);
	if (OutLength < 1.f) return false;
	OutStart = P1;
	OutDir = (P2 - P1) / OutLength;
	OutLeftNormal = FVector2D(-OutDir.Y, OutDir.X);
	OutHalfThickness = Seg->Thickness * 0.5f;
	return true;
}

FPlannerDropInfo ARoomPlannerManager::ResolveDropAtWorldPos2D(const FVector& WorldPos) const
{
	FPlannerDropInfo Info;
	Info.WorldLocation = FVector(WorldPos.X, WorldPos.Y, 0.f);
	const FVector2D P(WorldPos.X, WorldPos.Y);

	// 1. Wall footprint (± WallDropSnapToleranceCm outside the faces)
	int32 BestSeg = -1;
	float BestPerp = TNumericLimits<float>::Max();
	float BestAlong = 0.f;
	bool bBestLeft = true;
	for (const auto& Pair : WallSegments)
	{
		FVector2D P1, Dir, NLeft; float Len, Half;
		if (!GetSegmentGeometry(Pair.Key, P1, Dir, NLeft, Len, Half)) continue;
		const float Along = FVector2D::DotProduct(P - P1, Dir);
		if (Along < 0.f || Along > Len) continue;
		const float Perp = FVector2D::DotProduct(P - P1, NLeft);
		const float AbsPerp = FMath::Abs(Perp);
		if (AbsPerp <= Half + WallDropSnapToleranceCm && AbsPerp < BestPerp)
		{
			BestPerp = AbsPerp;
			BestSeg = Pair.Key;
			BestAlong = Along;
			// Dropped on the wall body → interior face (when known); dropped just outside a face → that face.
			bBestLeft = (AbsPerp <= Half) ? Pair.Value.bLeftSideIsInterior : (Perp > 0.f);
		}
	}
	if (BestSeg != -1)
	{
		Info.Target = EPlannerDropTarget::Wall;
		Info.SegmentID = BestSeg;
		Info.DistanceAlongWallCm = BestAlong;
		Info.bLeftSide = bBestLeft;
		Info.HeightCm = 0.f;
		return Info;
	}

	// 2. Floor (inside a detected room)
	const int32 RoomID = FindRoomAtWorldPos(WorldPos);
	if (RoomID != -1)
	{
		Info.Target = EPlannerDropTarget::Floor;
		Info.RoomID = RoomID;
		return Info;
	}

	return Info; // invalid
}

namespace PlannerWallRay
{
	/** Where a ray first meets a wall's solid (footprint × height), in the wall's frame. */
	struct FHit
	{
		double T = 0.0;
		double Along = 0.0;
		/** Perpendicular offset from the centre line where the ray enters (+ = the left side). */
		double PerpAtEntry = 0.0;
		/** Height where the ray enters. */
		double ZAtEntry = 0.0;
		/** 0: an end of the wall, 1: one of its faces, 2: its top (or the ray starts inside). */
		int32 EntryAxis = 2;
	};

	/**
	 * Slab test of the ray against the box Along ∈ [0, Len], Perp ∈ [-HalfWidth, HalfWidth], Z ∈ [0, Height] of a wall. HalfWidth may
	 * be widened by a snap tolerance; the ray only counts from its origin on.
	 */
	static bool Intersect(const FVector& Origin, const FVector& Direction, const FVector2D& Start, const FVector2D& Dir, const FVector2D& LeftNormal,
		double Len, double HalfWidth, double Height, FHit& OutHit)
	{
		const FVector2D O2(Origin.X, Origin.Y);
		const FVector2D D2(Direction.X, Direction.Y);
		const double Origins[3] = { FVector2D::DotProduct(O2 - Start, Dir), FVector2D::DotProduct(O2 - Start, LeftNormal), Origin.Z };
		const double Steps[3] = { FVector2D::DotProduct(D2, Dir), FVector2D::DotProduct(D2, LeftNormal), Direction.Z };
		const double Lows[3] = { 0.0, -HalfWidth, 0.0 };
		const double Highs[3] = { Len, HalfWidth, Height };
		double TEnter = 0.0;
		double TExit = TNumericLimits<double>::Max();
		int32 EnterAxis = 2;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (FMath::Abs(Steps[Axis]) < 1.e-9)
			{
				if (Origins[Axis] < Lows[Axis] || Origins[Axis] > Highs[Axis]) return false;
				continue;
			}
			double T1 = (Lows[Axis] - Origins[Axis]) / Steps[Axis];
			double T2 = (Highs[Axis] - Origins[Axis]) / Steps[Axis];
			if (T1 > T2) Swap(T1, T2);
			if (T1 > TEnter)
			{
				TEnter = T1;
				EnterAxis = Axis;
			}
			TExit = FMath::Min(TExit, T2);
			if (TEnter > TExit) return false;
		}
		OutHit.T = TEnter;
		OutHit.Along = FMath::Clamp(Origins[0] + Steps[0] * TEnter, 0.0, Len);
		OutHit.PerpAtEntry = Origins[1] + Steps[1] * TEnter;
		OutHit.ZAtEntry = Origins[2] + Steps[2] * TEnter;
		OutHit.EntryAxis = EnterAxis;
		return true;
	}
}

FPlannerDropInfo ARoomPlannerManager::ResolveDropFromCursorRay2D(const FVector& RayOrigin, const FVector& RayDirection) const
{
	FPlannerDropInfo Info;
	if (FMath::IsNearlyZero(RayDirection.Z)) return Info;

	// 1. Walls as the camera sees them. The 2D view of «Выбрать» / «Каталог» is a perspective one: a wall away from the middle of the
	//    screen shows its whole inner face, and the ray to a point on that face meets the wall-top plane far inside the room and the
	//    floor outside the wall (testing only those two planes put the item on the outer face, or on no wall at all).
	//    a) The first wall solid the cursor ray really meets (through a door / window opening it goes on): the face it enters.
	//    b) Otherwise a near miss within WallDropSnapToleranceCm: where the cursor lands on the floor beside a wall, or just past its
	//       top edge; that side, at that point along the wall.
	//    c) Cabinets and wall objects go into rooms: a face with no room in front of it (the camera follows the pawn and can see a
	//       wall's outer face) gives way to the other face when that one has a room in front of it.
	const double TGround = -RayOrigin.Z / RayDirection.Z;
	const bool bHasGround = TGround >= 0.0;
	const FVector Ground = bHasGround ? FVector(RayOrigin + RayDirection * TGround) : FVector::ZeroVector;

	int32 BestSeg = -1;
	float BestAlong = 0.f;
	bool bBestLeft = true;

	// a) Exact hits: the nearest along the ray.
	double BestT = TNumericLimits<double>::Max();
	for (const auto& Pair : WallSegments)
	{
		FVector2D P1, Dir, NLeft; float Len, Half;
		if (!GetSegmentGeometry(Pair.Key, P1, Dir, NLeft, Len, Half)) continue;
		PlannerWallRay::FHit Hit;
		if (!PlannerWallRay::Intersect(RayOrigin, RayDirection, P1, Dir, NLeft, Len, Half, FMath::Max(1.f, Pair.Value.Height), Hit) || Hit.T >= BestT) continue;
		if (Hit.EntryAxis == 1)
		{
			// Entering a face inside a door / window / archway: the ray goes through the opening (to the next room's floor).
			const bool bThroughOpening = Pair.Value.Openings.ContainsByPredicate([&Hit](const FWallOpening& Opening)
			{
				return FMath::Abs(Hit.Along - Opening.DistanceFromStart) <= 0.5 * Opening.Width
					&& Hit.ZAtEntry >= Opening.SillHeight && Hit.ZAtEntry <= Opening.SillHeight + Opening.Height;
			});
			if (bThroughOpening) continue;
		}
		BestSeg = Pair.Key;
		BestT = Hit.T;
		BestAlong = (float)Hit.Along;
		const double CameraPerp = FVector2D::DotProduct(FVector2D(RayOrigin.X, RayOrigin.Y) - P1, NLeft);
		if (Hit.EntryAxis == 1)
		{
			bBestLeft = Hit.PerpAtEntry > 0.0; // the face the ray enters
		}
		else if (FMath::Abs(CameraPerp) > Half)
		{
			bBestLeft = CameraPerp > 0.0; // over the top or an end: the face on the camera's side, the one the user sees
		}
		else
		{
			// Straight down onto the wall (the orthographic camera stands over it): the half of the top under the cursor.
			bBestLeft = FMath::Abs(Hit.PerpAtEntry) > 0.5 ? Hit.PerpAtEntry > 0.0 : Pair.Value.bLeftSideIsInterior;
		}
	}

	// b) Near misses: the floor point beside a wall, else the point just past its top edge (the smallest distance from a face wins).
	if (BestSeg == -1)
	{
		float BestExcess = TNumericLimits<float>::Max();
		for (const auto& Pair : WallSegments)
		{
			FVector2D P1, Dir, NLeft; float Len, Half;
			if (!GetSegmentGeometry(Pair.Key, P1, Dir, NLeft, Len, Half)) continue;
			auto Consider = [&](const FVector& Point)
			{
				const FVector2D P(Point.X, Point.Y);
				const float Along = FVector2D::DotProduct(P - P1, Dir);
				const float Perp = FVector2D::DotProduct(P - P1, NLeft);
				const float Excess = FMath::Abs(Perp) - Half;
				// On the wall body the ray went through an opening (it would have met the solid otherwise): not a near miss.
				if (Along < 0.f || Along > Len || Excess < 0.f || Excess > WallDropSnapToleranceCm) return false;
				if (Excess < BestExcess)
				{
					BestExcess = Excess;
					BestSeg = Pair.Key;
					BestAlong = Along;
					bBestLeft = Perp > 0.f;
				}
				return true;
			};
			if (bHasGround && Consider(Ground)) continue;
			const double TTop = (FMath::Max(1.f, Pair.Value.Height) - RayOrigin.Z) / RayDirection.Z;
			if (TTop >= 0.0) Consider(RayOrigin + RayDirection * TTop);
		}
	}

	if (BestSeg != -1)
	{
		FVector2D P1, Dir, NLeft; float Len, Half;
		if (GetSegmentGeometry(BestSeg, P1, Dir, NLeft, Len, Half))
		{
			// c) A face with no room in front of it gives way to the other face when that one has a room in front of it.
			auto RoomInFront = [&](bool bLeft)
			{
				const float InsetAlong = FMath::Min(0.5f * Len, Half + 5.f);
				const FVector2D Probe = P1 + Dir * FMath::Clamp(BestAlong, InsetAlong, Len - InsetAlong) + (bLeft ? NLeft : -NLeft) * (Half + 15.f);
				return FindRoomAtWorldPos(FVector(Probe.X, Probe.Y, 0.f)) != -1;
			};
			if (!RoomInFront(bBestLeft) && RoomInFront(!bBestLeft))
			{
				bBestLeft = !bBestLeft;
			}
			const FVector2D Pt = P1 + Dir * BestAlong;
			Info.WorldLocation = FVector(Pt.X, Pt.Y, 0.f);
		}
		Info.Target = EPlannerDropTarget::Wall;
		Info.SegmentID = BestSeg;
		Info.DistanceAlongWallCm = BestAlong;
		Info.bLeftSide = bBestLeft;
		Info.HeightCm = 0.f;
		return Info;
	}

	// 2. The floor under the cursor, inside a room.
	if (bHasGround)
	{
		Info.WorldLocation = FVector(Ground.X, Ground.Y, 0.f);
		const int32 RoomID = FindRoomAtWorldPos(Info.WorldLocation);
		if (RoomID != -1)
		{
			Info.Target = EPlannerDropTarget::Floor;
			Info.RoomID = RoomID;
		}
	}
	return Info;
}

FPlannerDropInfo ARoomPlannerManager::ResolveDropFromHit(const FHitResult& Hit) const
{
	FPlannerDropInfo Info;
	Info.WorldLocation = Hit.ImpactPoint;

	if (const AProceduralWallActor* Wall = Cast<AProceduralWallActor>(Hit.GetActor()))
	{
		const int32 SegID = FindSegmentIDForWallActor(Wall);
		FVector2D P1, Dir, NLeft; float Len, Half;
		if (SegID != -1 && GetSegmentGeometry(SegID, P1, Dir, NLeft, Len, Half))
		{
			const FVector2D P(Hit.ImpactPoint.X, Hit.ImpactPoint.Y);
			Info.Target = EPlannerDropTarget::Wall;
			Info.SegmentID = SegID;
			Info.DistanceAlongWallCm = FMath::Clamp(FVector2D::DotProduct(P - P1, Dir), 0.f, Len);
			const FVector2D N2(Hit.ImpactNormal.X, Hit.ImpactNormal.Y);
			Info.bLeftSide = N2.IsNearlyZero() ? (FVector2D::DotProduct(P - P1, NLeft) > 0.f) : (FVector2D::DotProduct(N2, NLeft) > 0.f);
			Info.HeightCm = FMath::Max(0.f, Hit.ImpactPoint.Z);
			return Info;
		}
	}

	if (Hit.GetComponent() && FloorProceduralMesh && Hit.GetComponent() == static_cast<UPrimitiveComponent*>(FloorProceduralMesh.Get()))
	{
		const int32 RoomID = FindRoomAtWorldPos(Hit.ImpactPoint);
		if (RoomID != -1)
		{
			Info.Target = EPlannerDropTarget::Floor;
			Info.RoomID = RoomID;
			Info.WorldLocation = FVector(Hit.ImpactPoint.X, Hit.ImpactPoint.Y, 0.f);
			return Info;
		}
	}

	return Info; // invalid
}

bool ARoomPlannerManager::ComputeWallAttachedTransform(const FWallAttachment& Attachment, FVector& OutLocation, FRotator& OutRotation) const
{
	const int32 SegID = FindSegmentIDByGuid(Attachment.WallGuid);
	FVector2D P1, Dir, NLeft; float Len, Half;
	if (SegID == -1 || !GetSegmentGeometry(SegID, P1, Dir, NLeft, Len, Half)) return false;

	const FVector2D N = Attachment.bLeftSide ? NLeft : -NLeft;
	const float Dist = FMath::Clamp(Attachment.DistanceAlongWallCm, 0.f, Len);
	const FVector2D Face = P1 + Dir * Dist + N * Half;
	const FVector2D Pivot = Face + N * Attachment.DepthOffsetCm;

	OutLocation = FVector(Pivot.X, Pivot.Y, Attachment.HeightCm);
	OutRotation = FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(N.Y, N.X)), 0.f); // faces away from the wall
	return true;
}

bool ARoomPlannerManager::ComputeCabinetSetTransform(const FPlacedCabinetSetData& Data, FVector& OutLocation, FRotator& OutRotation) const
{
	if (!ComputeWallAttachedTransform(Data.WallAttachment, OutLocation, OutRotation)) return false;
	if (const FCabinetSetLayoutRow* Row = FindCabinetSetLayoutRow(GetWorld(), Data.ProductID))
	{
		OutRotation.Yaw = FRotator::NormalizeAxis(OutRotation.Yaw + Row->RotationZ); // whole-set RotationZ is additive
	}
	return true;
}

bool ARoomPlannerManager::ComputePlacedObjectWallTransform(const FString& AssetID, const FWallAttachment& Attachment, FVector& OutLocation, FRotator& OutRotation) const
{
	if (!ComputeWallAttachedTransform(Attachment, OutLocation, OutRotation)) return false;
	// OutRotation turns actor +X away from the wall; the model's front (FrontYawDeg in its own frame) goes there instead.
	OutRotation.Yaw = FRotator::NormalizeAxis(OutRotation.Yaw - ResolveObjectFrontYawDeg(AssetID));
	return true;
}

bool ARoomPlannerManager::MeasureAttachmentDepth(AActor* Actor, FWallAttachment& Attachment, bool bLogResult) const
{
	// Actor must already stand at the face point (DepthOffsetCm == 0) with the attached rotation.
	if (!Actor) return false;
	const int32 SegID = FindSegmentIDByGuid(Attachment.WallGuid);
	FVector2D P1, Dir, NLeft; float Len, Half;
	if (SegID == -1 || !GetSegmentGeometry(SegID, P1, Dir, NLeft, Len, Half)) return false;

	const FVector2D N = Attachment.bLeftSide ? NLeft : -NLeft;
	const FVector2D Face = P1 + Dir * FMath::Clamp(Attachment.DistanceAlongWallCm, 0.f, Len) + N * Half;

	// Back face of the object measured ALONG THE WALL NORMAL from the oriented mesh bounds: every visible
	// static mesh's local bounding box is taken through its world transform (the actor already carries the
	// wall-aligned rotation) and each corner is projected onto N; the smallest projection is the object's
	// rearmost point in the wall's own frame. This is exact for any wall angle.
	//
	// The previous code used Comp->Bounds.GetBox(), the world-space AXIS-ALIGNED box of the rotated mesh, and
	// projected that box's X/Y extents onto N. For axis-aligned walls the AABB equals the real footprint, so it
	// was exact; for a wall at angle θ the AABB of a rotated w×d box grows to (w·cosθ + d·sinθ) × (w·sinθ + d·cosθ)
	// and its support along N over-estimates the true half depth, pushing the object away from the wall by the
	// difference (largest at 45°). Only visible meshes are used: AActor::GetActorBounds would also include
	// trigger / proximity shapes (BP_Booth carries one), which pushed cabinet sets metres away from the wall.
	float BackAlongN = TNumericLimits<float>::Max();
	bool bAnyMesh = false;
	TArray<UStaticMeshComponent*> MeshComps;
	Actor->GetComponents<UStaticMeshComponent>(MeshComps);
	for (UStaticMeshComponent* Comp : MeshComps)
	{
		if (!Comp || !Comp->GetStaticMesh() || !Comp->IsVisible()) continue;
		const FBox Local = Comp->GetStaticMesh()->GetBoundingBox();
		if (!Local.IsValid) continue;
		const FTransform& TM = Comp->GetComponentTransform();
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector LocalCorner(
				(Corner & 1) ? Local.Max.X : Local.Min.X,
				(Corner & 2) ? Local.Max.Y : Local.Min.Y,
				(Corner & 4) ? Local.Max.Z : Local.Min.Z);
			const FVector World = TM.TransformPosition(LocalCorner);
			BackAlongN = FMath::Min(BackAlongN, FVector2D::DotProduct(FVector2D(World.X, World.Y), N));
			bAnyMesh = true;
		}
	}
	// bLogResult off (a placed object re-measured on every re-apply, see RemeasurePlacedObjectWallDepth): Verbose only, the caller
	// logs a real change itself.
	if (!bAnyMesh)
	{
		if (bLogResult)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PlannerDrop] %s has no visible mesh bounds yet; depth offset left at %.1f."), *Actor->GetName(), Attachment.DepthOffsetCm);
		}
		else
		{
			UE_LOG(LogTemp, Verbose, TEXT("[PlannerDrop] %s has no visible mesh bounds yet; depth offset left at %.1f."), *Actor->GetName(), Attachment.DepthOffsetCm);
		}
		return false;
	}

	const float FaceAlongN = FVector2D::DotProduct(Face, N);
	Attachment.DepthOffsetCm = FaceAlongN - BackAlongN; // push out along N so the rearmost point touches the face
	if (bLogResult)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlannerDrop] %s rear point along wall normal %.1f, face %.1f → depth offset %.1f cm"), *Actor->GetName(), BackAlongN, FaceAlongN, Attachment.DepthOffsetCm);
	}
	else
	{
		UE_LOG(LogTemp, Verbose, TEXT("[PlannerDrop] %s rear point along wall normal %.1f, face %.1f → depth offset %.1f cm"), *Actor->GetName(), BackAlongN, FaceAlongN, Attachment.DepthOffsetCm);
	}
	return true;
}

bool ARoomPlannerManager::SlideAttachmentTo(FWallAttachment& Attachment, const FVector& RequestedLocation) const
{
	const int32 SegID = FindSegmentIDByGuid(Attachment.WallGuid);
	FVector2D P1, Dir, NLeft; float Len, Half;
	if (SegID == -1 || !GetSegmentGeometry(SegID, P1, Dir, NLeft, Len, Half)) return false;
	const FVector2D P(RequestedLocation.X, RequestedLocation.Y);
	Attachment.DistanceAlongWallCm = FMath::Clamp(FVector2D::DotProduct(P - P1, Dir), 0.f, Len);
	return true;
}

void ARoomPlannerManager::DetachItemsFromWall(const FString& WallGuid)
{
	if (WallGuid.IsEmpty()) return;
	for (auto& Pair : PlacedObjects)
	{
		if (Pair.Value.WallAttachment.WallGuid == WallGuid) Pair.Value.WallAttachment = FWallAttachment();
	}
	for (auto& Pair : CabinetSets)
	{
		if (Pair.Value.WallAttachment.WallGuid == WallGuid) Pair.Value.WallAttachment = FWallAttachment();
	}
}

void ARoomPlannerManager::RehomeAttachmentsAfterSplit(const FString& OldGuid, const FString& NewGuid, float SplitDistanceCm)
{
	auto Rehome = [&](FWallAttachment& Att)
	{
		if (Att.WallGuid == OldGuid && Att.DistanceAlongWallCm >= SplitDistanceCm)
		{
			Att.WallGuid = NewGuid;
			Att.DistanceAlongWallCm -= SplitDistanceCm;
		}
	};
	for (auto& Pair : PlacedObjects) Rehome(Pair.Value.WallAttachment);
	for (auto& Pair : CabinetSets) Rehome(Pair.Value.WallAttachment);
}

TSharedPtr<FJsonObject> ARoomPlannerManager::AttachmentToJson(const FWallAttachment& Attachment)
{
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
	Obj->SetStringField(TEXT("guid"), Attachment.WallGuid);
	PlannerJsonKeys::SetCm(Obj, TEXT("dist"), Attachment.DistanceAlongWallCm);
	Obj->SetBoolField(TEXT("left"), Attachment.bLeftSide);
	PlannerJsonKeys::SetCm(Obj, TEXT("z"), Attachment.HeightCm);
	PlannerJsonKeys::SetCm(Obj, TEXT("depth"), Attachment.DepthOffsetCm);
	return Obj;
}

FWallAttachment ARoomPlannerManager::AttachmentFromJson(const TSharedPtr<FJsonObject>& Obj)
{
	FWallAttachment Att;
	if (!Obj.IsValid()) return Att;
	Obj->TryGetStringField(TEXT("guid"), Att.WallGuid);
	double V = 0.0;
	if (Obj->TryGetNumberField(TEXT("dist"), V)) Att.DistanceAlongWallCm = (float)V;
	Obj->TryGetBoolField(TEXT("left"), Att.bLeftSide);
	if (Obj->TryGetNumberField(TEXT("z"), V)) Att.HeightCm = (float)V;
	if (Obj->TryGetNumberField(TEXT("depth"), V)) Att.DepthOffsetCm = (float)V;
	return Att;
}

FString ARoomPlannerManager::AddPlacedObjectOnWall(const FString& AssetID, int32 SegmentID, float DistanceAlongWallCm, bool bLeftSide, float HeightCm)
{
	const FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg || AssetID.IsEmpty())
	{
		BroadcastRejected(TEXT("Стена для размещения не найдена"));
		return FString();
	}

	FPlacedFurnitureData D;
	D.InstanceID = PlannerJsonKeys::NewInstanceID();
	D.AssetID = AssetID;
	D.Scale = FVector::OneVector;
	if (UDataTable* Catalog = ResolveObjectCatalog())
	{
		if (const FPlannerObjectRow* Row = Catalog->FindRow<FPlannerObjectRow>(FName(*AssetID), TEXT("AddPlacedObjectOnWall"), false))
		{
			D.Scale = Row->DefaultScale.IsNearlyZero() ? FVector::OneVector : Row->DefaultScale;
		}
	}
	D.WallAttachment.WallGuid = Seg->WallGuid;
	D.WallAttachment.DistanceAlongWallCm = DistanceAlongWallCm;
	D.WallAttachment.bLeftSide = bLeftSide;
	D.WallAttachment.HeightCm = FMath::Max(0.f, HeightCm);
	D.WallAttachment.DepthOffsetCm = 0.f;

	// Stand it on the face, its front to the room; applying its actor measures its bounds and pushes it out so the back touches the
	// wall (ApplyPlacedObjectActor → RemeasurePlacedObjectWallDepth, which stores the stand-off and the Location).
	if (!ComputePlacedObjectWallTransform(D.AssetID, D.WallAttachment, D.Location, D.Rotation)) return FString();
	PlacedObjects.Add(D.InstanceID, D);
	ApplyPlacedObjectActor(D);
	CommitStateAfterMutation();
	return D.InstanceID;
}

FString ARoomPlannerManager::AddCabinetSetOnWall(FName ProductID, int32 SegmentID, float DistanceAlongWallCm, bool bLeftSide)
{
	if (!HasAuthority() || ProductID.IsNone()) return FString();
	const FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg)
	{
		BroadcastRejected(TEXT("Гарнитур можно разместить только у стены"));
		return FString();
	}

	FPlacedCabinetSetData D;
	D.InstanceID = PlannerJsonKeys::NewInstanceID();
	D.ProductID = ProductID;
	D.WallAttachment.WallGuid = Seg->WallGuid;
	D.WallAttachment.DistanceAlongWallCm = DistanceAlongWallCm;
	D.WallAttachment.bLeftSide = bLeftSide;
	D.WallAttachment.HeightCm = 0.f; // floor-standing against the wall
	D.WallAttachment.DepthOffsetCm = 0.f;
	// Whole-set WorldLocationZ from DT_CabinetSetLayouts: applied once, at spawn, as the actor's Z.
	if (const FCabinetSetLayoutRow* Layout = FindCabinetSetLayoutRow(GetWorld(), ProductID))
	{
		D.WallAttachment.HeightCm = Layout->WorldLocationZ;
	}
	if (!ComputeCabinetSetTransform(D, D.Location, D.Rotation)) return FString();

	AShowroomBooth* Booth = SpawnCabinetSetActor(D);
	if (!Booth)
	{
		UE_LOG(LogTemp, Error, TEXT("[PlannerDrop] Cabinet set '%s' could not be spawned (booth class / product missing)."), *ProductID.ToString());
		return FString();
	}

	MeasureAttachmentDepth(Booth, D.WallAttachment);
	ComputeCabinetSetTransform(D, D.Location, D.Rotation);
	Booth->SetActorLocationAndRotation(D.Location, D.Rotation);
	UE_LOG(LogTemp, Warning, TEXT("[PlannerDrop] Cabinet set %s (%s) → booth %s at (%.0f, %.0f, %.0f) yaw %.0f on wall seg %d"),
		*D.InstanceID, *ProductID.ToString(), *Booth->GetName(), D.Location.X, D.Location.Y, D.Location.Z, D.Rotation.Yaw, SegmentID);

	CabinetSets.Add(D.InstanceID, D);
	CommitStateAfterMutation();
	return D.InstanceID;
}

void ARoomPlannerManager::RefreshWallAttachedPlacements()
{
	for (auto& Pair : PlacedObjects)
	{
		FPlacedFurnitureData& D = Pair.Value;
		if (!D.WallAttachment.IsAttached()) continue;
		FVector Loc; FRotator Rot;
		if (ComputePlacedObjectWallTransform(D.AssetID, D.WallAttachment, Loc, Rot))
		{
			if (!D.Location.Equals(Loc, 0.01f) || !D.Rotation.Equals(Rot, 0.01f))
			{
				D.Location = Loc;
				D.Rotation = Rot;
				ApplyPlacedObjectActor(D);
			}
		}
		else
		{
			D.WallAttachment = FWallAttachment(); // wall gone: keep the item where it is, detached
		}
	}
	for (auto& Pair : CabinetSets)
	{
		FPlacedCabinetSetData& D = Pair.Value;
		if (!D.WallAttachment.IsAttached()) continue;
		FVector Loc; FRotator Rot;
		if (ComputeCabinetSetTransform(D, Loc, Rot))
		{
			if (!D.Location.Equals(Loc, 0.01f) || !D.Rotation.Equals(Rot, 0.01f))
			{
				D.Location = Loc;
				D.Rotation = Rot;
				if (AShowroomBooth* Booth = FindCabinetSetActor(Pair.Key))
				{
					Booth->SetActorLocationAndRotation(Loc, Rot);
				}
			}
		}
		else
		{
			D.WallAttachment = FWallAttachment();
		}
	}
}

bool ARoomPlannerManager::RemeasureCabinetSetWallDepth(const FString& InstanceID)
{
	if (!HasAuthority()) return false;
	FPlacedCabinetSetData* D = CabinetSets.Find(InstanceID);
	AShowroomBooth* Booth = FindCabinetSetActor(InstanceID);
	if (!D || !Booth || !D->WallAttachment.IsAttached()) return false;

	// As AddCabinetSetOnWall: stand the set on the face point, measure its rear along the wall normal, then push it out.
	FPlacedCabinetSetData Measured = *D;
	Measured.WallAttachment.DepthOffsetCm = 0.f;
	FVector FaceLoc; FRotator FaceRot;
	if (!ComputeCabinetSetTransform(Measured, FaceLoc, FaceRot)) return false;
	Booth->SetActorLocationAndRotation(FaceLoc, FaceRot);
	const bool bMeasured = MeasureAttachmentDepth(Booth, Measured.WallAttachment);
	if (!bMeasured)
	{
		Measured.WallAttachment.DepthOffsetCm = D->WallAttachment.DepthOffsetCm; // nothing visible to measure: keep the stand-off
	}
	ComputeCabinetSetTransform(Measured, Measured.Location, Measured.Rotation);
	Booth->SetActorLocationAndRotation(Measured.Location, Measured.Rotation);

	const bool bChanged = !FMath::IsNearlyEqual(Measured.WallAttachment.DepthOffsetCm, D->WallAttachment.DepthOffsetCm, 0.01f)
		|| !Measured.Location.Equals(D->Location, 0.01f) || !Measured.Rotation.Equals(D->Rotation, 0.01f);
	if (bChanged)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlannerDrop] Cabinet set %s reconfigured → depth offset %.1f → %.1f cm"),
			*InstanceID, D->WallAttachment.DepthOffsetCm, Measured.WallAttachment.DepthOffsetCm);
		*D = Measured;
		CommitStateAfterMutation();
	}
	return bMeasured;
}

bool ARoomPlannerManager::RemeasurePlacedObjectWallDepth(const FString& InstanceID)
{
	FPlacedFurnitureData* D = PlacedObjects.Find(InstanceID);
	APlannerPlacedObjectActor* Actor = FindPlacedObjectActor(InstanceID);
	if (!D || !Actor || !D->WallAttachment.IsAttached()) return false;

	// Not authority-only, unlike cabinet sets (replicated booths): every machine builds its own object actors, so every machine keeps
	// its copy flush. Stand the object on the face point, measure its rear along the wall normal, then push it out. The actor centres
	// its mesh's footprint on its origin, so this comes out as its half depth, whatever mesh or scale the row gives it now.
	FWallAttachment Measured = D->WallAttachment;
	Measured.DepthOffsetCm = 0.f;
	FVector Location; FRotator Rotation;
	if (!ComputePlacedObjectWallTransform(D->AssetID, Measured, Location, Rotation)) return false; // its wall is gone: RefreshWallAttachedPlacements detaches it
	Actor->SetActorLocationAndRotation(Location, Rotation);
	if (!MeasureAttachmentDepth(Actor, Measured, /*bLogResult*/ false)) // runs on every re-apply: a real change is logged below
	{
		Measured.DepthOffsetCm = D->WallAttachment.DepthOffsetCm; // no mesh to measure (asset not resolved): keep the stand-off
	}
	ComputePlacedObjectWallTransform(D->AssetID, Measured, Location, Rotation);

	// The layout JSON rounds centimetres to 0.01, so a copy rebuilt from it differs by that much: only a real move counts as a change.
	constexpr float Tolerance = 0.05f;
	const bool bChanged = !FMath::IsNearlyEqual(Measured.DepthOffsetCm, D->WallAttachment.DepthOffsetCm, Tolerance)
		|| !Location.Equals(D->Location, Tolerance) || !Rotation.Equals(D->Rotation, 0.01f);
	if (bChanged)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlannerDrop] Object %s (%s) re-measured against its wall → depth offset %.2f → %.2f cm, yaw %.1f → %.1f"),
			*InstanceID, *D->AssetID, D->WallAttachment.DepthOffsetCm, Measured.DepthOffsetCm, D->Rotation.Yaw, Rotation.Yaw);
		D->WallAttachment = Measured;
		D->Location = Location;
		D->Rotation = Rotation;
	}
	// Unchanged: back where the data has it (what was published), not off by the JSON's rounding.
	Actor->SetActorLocationAndRotation(D->Location, D->Rotation);
	Actor->Data.WallAttachment = D->WallAttachment;
	Actor->Data.Location = D->Location;
	Actor->Data.Rotation = D->Rotation;
	return bChanged;
}

void ARoomPlannerManager::HandleCabinetSetProductChanged(AShowroomBooth* Booth, FName NewProductID)
{
	if (!HasAuthority() || !Booth || Booth->PlannerInstanceID.IsEmpty()) return;
	if (FindCabinetSetActor(Booth->PlannerInstanceID) != Booth) return; // a stale booth of a removed set
	RemeasureCabinetSetWallDepth(Booth->PlannerInstanceID);
}

bool ARoomPlannerManager::IsSelectionWallAttached() const
{
	if (!SelectedObjectID.IsEmpty())
	{
		if (const FPlacedFurnitureData* D = PlacedObjects.Find(SelectedObjectID)) return D->WallAttachment.IsAttached();
	}
	if (!SelectedCabinetSetID.IsEmpty())
	{
		if (const FPlacedCabinetSetData* D = CabinetSets.Find(SelectedCabinetSetID)) return D->WallAttachment.IsAttached();
	}
	return false;
}

bool ARoomPlannerManager::RotateSelectionLocal(float DeltaYawDeg, FString& OutInstanceID, bool& bOutCabinetSet, float& OutYawDeg)
{
	OutInstanceID.Empty();
	bOutCabinetSet = false;
	OutYawDeg = 0.f;
	// Moving / rotating is a 2D workflow; a 3D pick (for finishing) selects objects too, and must never turn them.
	if (!b2DViewMode || (SelectedObjectID.IsEmpty() && SelectedCabinetSetID.IsEmpty())) return false;
	if (IsSelectionWallAttached())
	{
		BroadcastRejected(WallAttachedRotationMessage);
		return false;
	}

	if (!SelectedObjectID.IsEmpty())
	{
		const FPlacedFurnitureData* D = PlacedObjects.Find(SelectedObjectID);
		if (!D) return false;
		const FString ID = SelectedObjectID;
		const FVector Location = D->Location;
		const FRotator Rotation(D->Rotation.Pitch, FRotator::NormalizeAxis(D->Rotation.Yaw + DeltaYawDeg), D->Rotation.Roll);
		MovePlacedObjectLocal(ID, Location, Rotation);
		OutInstanceID = ID;
		OutYawDeg = (float)Rotation.Yaw;
		return true;
	}

	const FPlacedCabinetSetData* D = CabinetSets.Find(SelectedCabinetSetID);
	if (!D) return false;
	const FString ID = SelectedCabinetSetID;
	const FVector Location = D->Location;
	const FRotator Rotation(D->Rotation.Pitch, FRotator::NormalizeAxis(D->Rotation.Yaw + DeltaYawDeg), D->Rotation.Roll);
	MoveCabinetSetLocal(ID, Location, Rotation);
	OutInstanceID = ID;
	bOutCabinetSet = true;
	OutYawDeg = (float)Rotation.Yaw;
	return true;
}

void ARoomPlannerManager::NotifySelectionChanged()
{
	OnSelectionChanged.Broadcast();
}

void ARoomPlannerManager::BroadcastRejected(const FString& Reason)
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (Now - LastRejectBroadcastTime >= 0.25f)
	{
		LastRejectBroadcastTime = Now;
		UE_LOG(LogTemp, Warning, TEXT("[MaxiMallConstructor] Operation rejected: %s"), *Reason);
		OnOperationRejected.Broadcast(Reason);
	}
}

bool ARoomPlannerManager::ValidateOpeningFits(const FWallSegment& Seg, float WallLengthCm, const FWallOpening& Candidate, int32 IgnoreOpeningIndex, FString& OutReason) const
{
	if (Candidate.Width < 10.f || Candidate.Height < 10.f)
	{
		OutReason = TEXT("Минимальный размер проёма — 10 см");
		return false;
	}
	if (Candidate.SillHeight < 0.f)
	{
		OutReason = TEXT("Высота от пола не может быть отрицательной");
		return false;
	}
	if (Candidate.SillHeight + Candidate.Height > Seg.Height + 0.5f)
	{
		OutReason = FString::Printf(TEXT("Проём выше стены (макс. %.0f см)"), Seg.Height);
		return false;
	}

	const float Start = Candidate.DistanceFromStart - Candidate.Width * 0.5f;
	const float End = Candidate.DistanceFromStart + Candidate.Width * 0.5f;
	if (Start < -0.5f || End > WallLengthCm + 0.5f)
	{
		OutReason = FString::Printf(TEXT("Проём (%.0f см) не помещается на стене (%.0f см)"), Candidate.Width, WallLengthCm);
		return false;
	}

	for (int32 i = 0; i < Seg.Openings.Num(); ++i)
	{
		if (i == IgnoreOpeningIndex) continue;
		const FWallOpening& Other = Seg.Openings[i];
		const float OStart = Other.DistanceFromStart - Other.Width * 0.5f;
		const float OEnd = Other.DistanceFromStart + Other.Width * 0.5f;
		if (Start < OEnd - 0.5f && End > OStart + 0.5f)
		{
			OutReason = TEXT("Проём пересекается с соседним проёмом");
			return false;
		}
	}
	return true;
}

bool ARoomPlannerManager::CanMoveNode(int32 NodeID, const FVector2D& NewPosition, FString& OutReason) const
{
	const FWallNode* Node = Nodes.Find(NodeID);
	if (!Node)
	{
		OutReason = TEXT("Узел не найден");
		return false;
	}

	for (int32 SegID : Node->ConnectedSegmentIDs)
	{
		const FWallSegment* Seg = WallSegments.Find(SegID);
		if (!Seg) continue;
		const int32 OtherNodeID = (Seg->StartNodeID == NodeID) ? Seg->EndNodeID : Seg->StartNodeID;
		const FWallNode* Other = Nodes.Find(OtherNodeID);
		if (!Other) continue;

		const float OldLen = FVector2D::Distance(Node->Position, Other->Position);
		const float NewLen = FVector2D::Distance(NewPosition, Other->Position);
		if (NewLen < 10.f)
		{
			OutReason = TEXT("Стена не может быть короче 10 см");
			return false;
		}

		// Openings keep their distance from the UNMOVED corner. When the start node moves,
		// DistanceFromStart therefore shifts by the length delta.
		const float Shift = (Seg->StartNodeID == NodeID) ? (NewLen - OldLen) : 0.f;

		FWallSegment Shifted = *Seg;
		for (FWallOpening& Op : Shifted.Openings)
		{
			Op.DistanceFromStart += Shift;
		}
		for (int32 i = 0; i < Shifted.Openings.Num(); ++i)
		{
			if (!ValidateOpeningFits(Shifted, NewLen, Shifted.Openings[i], i, OutReason))
			{
				OutReason = FString::Printf(TEXT("Стена не может быть изменена: %s"), *OutReason);
				return false;
			}
		}
	}
	return true;
}

bool ARoomPlannerManager::ApplyNodeMove(int32 NodeID, const FVector2D& NewPosition, bool bLocalPreviewOnly)
{
	FString Reason;
	if (!CanMoveNode(NodeID, NewPosition, Reason))
	{
		BroadcastRejected(Reason);
		return false;
	}

	FWallNode& Node = Nodes[NodeID];
	for (int32 SegID : Node.ConnectedSegmentIDs)
	{
		FWallSegment* Seg = WallSegments.Find(SegID);
		if (!Seg || Seg->StartNodeID != NodeID) continue;
		const FWallNode* Other = Nodes.Find(Seg->EndNodeID);
		if (!Other) continue;
		const float OldLen = FVector2D::Distance(Node.Position, Other->Position);
		const float NewLen = FVector2D::Distance(NewPosition, Other->Position);
		const float Shift = NewLen - OldLen;
		for (FWallOpening& Op : Seg->Openings)
		{
			Op.DistanceFromStart += Shift;
		}
	}
	Node.Position = NewPosition;

	RebuildWallsAndRooms();
	if (!bLocalPreviewOnly)
	{
		CommitStateAfterMutation();
	}
	UpdateSelectionVisuals();
	return true;
}

bool ARoomPlannerManager::MoveNode(int32 NodeID, const FVector2D& NewPosition)
{
	// Server-side: snap exactly like the client preview, validate/apply, then join the node to
	// whatever it landed on (another corner, or the middle of a wall) so a real junction exists.
	const FVector2D Snapped = SnapNodeDragPosition(NodeID, NewPosition);
	if (!ApplyNodeMove(NodeID, Snapped, false))
	{
		return false;
	}
	TryConnectMovedNode(NodeID);
	return true;
}

bool ARoomPlannerManager::MergeNodeInto(int32 NodeID, int32 TargetNodeID)
{
	if (NodeID == TargetNodeID || !Nodes.Contains(NodeID) || !Nodes.Contains(TargetNodeID)) return false;

	const TArray<int32> SrcSegs = Nodes[NodeID].ConnectedSegmentIDs;
	const TArray<int32> TgtSegs = Nodes[TargetNodeID].ConnectedSegmentIDs;

	// Validation: no wall may collapse (both ends on the target) and no duplicate wall may appear.
	for (int32 SegID : SrcSegs)
	{
		const FWallSegment* Seg = WallSegments.Find(SegID);
		if (!Seg) continue;
		const int32 OtherEnd = (Seg->StartNodeID == NodeID) ? Seg->EndNodeID : Seg->StartNodeID;
		if (OtherEnd == TargetNodeID)
		{
			BroadcastRejected(TEXT("Стена не может быть соединена сама с собой"));
			return false;
		}
		for (int32 TSegID : TgtSegs)
		{
			const FWallSegment* TSeg = WallSegments.Find(TSegID);
			if (!TSeg) continue;
			const int32 TOther = (TSeg->StartNodeID == TargetNodeID) ? TSeg->EndNodeID : TSeg->StartNodeID;
			if (TOther == OtherEnd)
			{
				BroadcastRejected(TEXT("Между этими углами уже есть стена"));
				return false;
			}
		}
	}

	// Re-point every wall of the moved node to the target node.
	for (int32 SegID : SrcSegs)
	{
		if (FWallSegment* Seg = WallSegments.Find(SegID))
		{
			if (Seg->StartNodeID == NodeID) Seg->StartNodeID = TargetNodeID;
			if (Seg->EndNodeID == NodeID) Seg->EndNodeID = TargetNodeID;
			Nodes[TargetNodeID].ConnectedSegmentIDs.AddUnique(SegID);
		}
	}
	Nodes.Remove(NodeID);
	if (DraggingNodeID == NodeID) DraggingNodeID = -1;
	return true;
}

bool ARoomPlannerManager::TryConnectMovedNode(int32 NodeID)
{
	const FWallNode* Node = Nodes.Find(NodeID);
	if (!Node) return false;
	const FVector2D Pos = Node->Position;
	const TArray<int32> MySegs = Node->ConnectedSegmentIDs;

	auto IsNeighbourNode = [&](int32 OtherNodeID) -> bool
	{
		for (int32 SegID : MySegs)
		{
			const FWallSegment* Seg = WallSegments.Find(SegID);
			if (Seg && (Seg->StartNodeID == OtherNodeID || Seg->EndNodeID == OtherNodeID)) return true;
		}
		return false;
	};

	// 1. Coincident corner → merge.
	int32 TargetNodeID = -1;
	float BestDistSq = 25.f * 25.f;
	for (const auto& Pair : Nodes)
	{
		if (Pair.Key == NodeID || IsNeighbourNode(Pair.Key)) continue;
		const float D = FVector2D::DistSquared(Pair.Value.Position, Pos);
		if (D < BestDistSq) { BestDistSq = D; TargetNodeID = Pair.Key; }
	}
	if (TargetNodeID != -1)
	{
		if (!MergeNodeInto(NodeID, TargetNodeID)) return false;
		RebuildWallsAndRooms();
		CommitStateAfterMutation();
		UpdateSelectionVisuals();
		return true;
	}

	// 2. Landed on the middle of a wall that is not one of its own → split it (T-junction) and merge with the junction.
	int32 TargetSegID = -1;
	FVector2D SplitPoint = Pos;
	float BestWallDist = TNumericLimits<float>::Max();
	for (const auto& Pair : WallSegments)
	{
		if (MySegs.Contains(Pair.Key)) continue;
		const FWallSegment& Seg = Pair.Value;
		if (!Nodes.Contains(Seg.StartNodeID) || !Nodes.Contains(Seg.EndNodeID)) continue;
		const FVector2D P1 = Nodes[Seg.StartNodeID].Position;
		const FVector2D P2 = Nodes[Seg.EndNodeID].Position;
		const float SegLen = FVector2D::Distance(P1, P2);
		if (SegLen < 40.f) continue;
		const FVector2D SegDir = (P2 - P1) / SegLen;
		const float T = FVector2D::DotProduct(Pos - P1, SegDir);
		if (T < 20.f || T > SegLen - 20.f) continue;
		const FVector2D Proj = P1 + SegDir * T;
		const float Dist = FVector2D::Distance(Pos, Proj);
		if (Dist <= Seg.Thickness * 0.5f + 20.f && Dist < BestWallDist)
		{
			BestWallDist = Dist;
			TargetSegID = Pair.Key;
			SplitPoint = Proj;
		}
	}
	if (TargetSegID != -1)
	{
		const int32 JunctionID = SplitWallSegment(TargetSegID, SplitPoint);
		if (JunctionID == INDEX_NONE || !Nodes.Contains(NodeID)) return false;
		if (!MergeNodeInto(NodeID, JunctionID)) return false;
		RebuildWallsAndRooms();
		CommitStateAfterMutation();
		UpdateSelectionVisuals();
		return true;
	}

	return false;
}

FVector2D ARoomPlannerManager::SnapNodeDragPosition(int32 NodeID, const FVector2D& RawPos) const
{
	const FWallNode* Node = Nodes.Find(NodeID);
	const TArray<int32> MySegs = Node ? Node->ConnectedSegmentIDs : TArray<int32>();

	auto IsNeighbourNode = [&](int32 OtherNodeID) -> bool
	{
		for (int32 SegID : MySegs)
		{
			const FWallSegment* Seg = WallSegments.Find(SegID);
			if (Seg && (Seg->StartNodeID == OtherNodeID || Seg->EndNodeID == OtherNodeID)) return true;
		}
		return false;
	};

	// 1. Magnet to another corner (not a direct neighbour, which would collapse a wall) — 25 cm, same as wall drawing.
	int32 BestNode = -1;
	float BestDistSq = 25.f * 25.f;
	for (const auto& Pair : Nodes)
	{
		if (Pair.Key == NodeID || IsNeighbourNode(Pair.Key)) continue;
		const float D = FVector2D::DistSquared(Pair.Value.Position, RawPos);
		if (D < BestDistSq) { BestDistSq = D; BestNode = Pair.Key; }
	}
	if (BestNode != -1)
	{
		return Nodes[BestNode].Position;
	}

	// 2. Magnet to the centreline of a wall that is not one of its own (T-junction target).
	float BestWallDist = TNumericLimits<float>::Max();
	FVector2D WallSnap = RawPos;
	bool bWallSnap = false;
	for (const auto& Pair : WallSegments)
	{
		if (MySegs.Contains(Pair.Key)) continue;
		const FWallSegment& Seg = Pair.Value;
		if (!Nodes.Contains(Seg.StartNodeID) || !Nodes.Contains(Seg.EndNodeID)) continue;
		const FVector2D P1 = Nodes[Seg.StartNodeID].Position;
		const FVector2D P2 = Nodes[Seg.EndNodeID].Position;
		const float SegLen = FVector2D::Distance(P1, P2);
		if (SegLen < 40.f) continue;
		const FVector2D SegDir = (P2 - P1) / SegLen;
		const float T = FVector2D::DotProduct(RawPos - P1, SegDir);
		if (T < 20.f || T > SegLen - 20.f) continue;
		const FVector2D Proj = P1 + SegDir * T;
		const float Dist = FVector2D::Distance(RawPos, Proj);
		if (Dist <= Seg.Thickness * 0.5f + 20.f && Dist < BestWallDist)
		{
			BestWallDist = Dist;
			WallSnap = Proj;
			bWallSnap = true;
		}
	}
	if (bWallSnap)
	{
		return WallSnap;
	}

	// 3. Axis alignment with any other node (keeps walls orthogonal while dragging a corner).
	const float SnapCm = 15.f;
	FVector2D Snapped = RawPos;
	float BestDX = SnapCm, BestDY = SnapCm;
	for (const auto& Pair : Nodes)
	{
		if (Pair.Key == NodeID) continue;
		const float DX = FMath::Abs(Pair.Value.Position.X - RawPos.X);
		const float DY = FMath::Abs(Pair.Value.Position.Y - RawPos.Y);
		if (DX < BestDX) { BestDX = DX; Snapped.X = Pair.Value.Position.X; }
		if (DY < BestDY) { BestDY = DY; Snapped.Y = Pair.Value.Position.Y; }
	}
	return Snapped;
}

int32 ARoomPlannerManager::FindNodeAtWorldPos(const FVector& WorldPos, float RadiusCm) const
{
	const FVector2D P(WorldPos.X, WorldPos.Y);
	int32 Best = -1;
	float BestDistSq = RadiusCm * RadiusCm;
	for (const auto& Pair : Nodes)
	{
		const float D = FVector2D::DistSquared(P, Pair.Value.Position);
		if (D <= BestDistSq)
		{
			BestDistSq = D;
			Best = Pair.Key;
		}
	}
	return Best;
}

float ARoomPlannerManager::GetNodeHandleZ(int32 NodeID) const
{
	float MaxHeight = 0.f;
	if (const FWallNode* Node = Nodes.Find(NodeID))
	{
		for (int32 SegID : Node->ConnectedSegmentIDs)
		{
			if (const FWallSegment* Seg = WallSegments.Find(SegID)) MaxHeight = FMath::Max(MaxHeight, Seg->Height);
		}
	}
	if (MaxHeight <= 0.f) MaxHeight = 280.f;
	return MaxHeight + 1.f; // just above the wall top so the disc is never z-fighting with it
}

int32 ARoomPlannerManager::FindNodeAtCursorRay(const FVector& RayOrigin, const FVector& RayDirection, float RadiusCm) const
{
	if (FMath::IsNearlyZero(RayDirection.Z)) return -1;
	int32 Best = -1;
	float BestDistSq = RadiusCm * RadiusCm;
	for (const auto& Pair : Nodes)
	{
		const float T = (GetNodeHandleZ(Pair.Key) - RayOrigin.Z) / RayDirection.Z;
		if (T < 0.f) continue;
		const FVector OnPlane = RayOrigin + RayDirection * T;
		const float D = FVector2D::DistSquared(FVector2D(OnPlane.X, OnPlane.Y), Pair.Value.Position);
		if (D <= BestDistSq)
		{
			BestDistSq = D;
			Best = Pair.Key;
		}
	}
	return Best;
}

bool ARoomPlannerManager::ProjectCursorRayToNodeHandlePlane(int32 NodeID, const FVector& RayOrigin, const FVector& RayDirection, FVector& OutWorldPos) const
{
	if (FMath::IsNearlyZero(RayDirection.Z)) return false;
	const float T = (GetNodeHandleZ(NodeID) - RayOrigin.Z) / RayDirection.Z;
	if (T < 0.f) return false;
	const FVector OnPlane = RayOrigin + RayDirection * T;
	OutWorldPos = FVector(OnPlane.X, OnPlane.Y, 0.f);
	return true;
}

bool ARoomPlannerManager::GetNodePosition(int32 NodeID, FVector2D& OutPosition) const
{
	if (const FWallNode* Node = Nodes.Find(NodeID))
	{
		OutPosition = Node->Position;
		return true;
	}
	return false;
}

TArray<FVector> ARoomPlannerManager::GetNodeHandleWorldPositions() const
{
	TArray<FVector> Out;
	for (const auto& Pair : Nodes)
	{
		Out.Add(FVector(Pair.Value.Position.X, Pair.Value.Position.Y, 0.f));
	}
	return Out;
}

bool ARoomPlannerManager::StartNodeDrag(int32 NodeID)
{
	const FWallNode* Node = Nodes.Find(NodeID);
	if (!Node) return false;
	DraggingNodeID = NodeID;
	NodeDragOriginalPos = Node->Position;
	RefreshNodeHandles();
	return true;
}

void ARoomPlannerManager::UpdateNodeDrag(const FVector& WorldPos)
{
	if (DraggingNodeID == -1 || !Nodes.Contains(DraggingNodeID)) return;

	const FVector2D Raw(WorldPos.X, WorldPos.Y);
	const FVector2D Target = SnapNodeDragPosition(DraggingNodeID, Raw);
	const bool bSnapped = !Target.Equals(Raw, 0.01f);

	if (!Target.Equals(Nodes[DraggingNodeID].Position, 0.01f))
	{
		ApplyNodeMove(DraggingNodeID, Target, true); // refused positions keep the last valid one (REQ-09)
	}

	const FVector2D Cur = Nodes[DraggingNodeID].Position;
	OnNodeDragProgress.Broadcast(DraggingNodeID, FVector(Cur.X, Cur.Y, 0.f));

	// Live wall dimension for the 3D scene label (REQ-02): prefer the selected wall if it touches this node.
	const FWallNode& Node = Nodes[DraggingNodeID];
	int32 PrimarySeg = -1;
	if (SelectedSegmentID != -1 && Node.ConnectedSegmentIDs.Contains(SelectedSegmentID))
	{
		PrimarySeg = SelectedSegmentID;
	}
	else if (Node.ConnectedSegmentIDs.Num() > 0)
	{
		PrimarySeg = Node.ConnectedSegmentIDs[0];
	}
	if (const FWallSegment* Seg = (PrimarySeg != -1) ? WallSegments.Find(PrimarySeg) : nullptr)
	{
		if (Nodes.Contains(Seg->StartNodeID) && Nodes.Contains(Seg->EndNodeID))
		{
			const FVector2D P1 = Nodes[Seg->StartNodeID].Position;
			const FVector2D P2 = Nodes[Seg->EndNodeID].Position;
			const float LenM = FVector2D::Distance(P1, P2) / 100.f;
			const FVector2D Mid = (P1 + P2) * 0.5f;
			const float AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(P2.Y - P1.Y, P2.X - P1.X));
			OnInteractiveWallDragProgress.Broadcast(LenM, FVector(Mid.X, Mid.Y, Seg->Height), AngleDeg, bSnapped);
		}
	}
}

bool ARoomPlannerManager::EndNodeDrag(int32& OutNodeID, FVector2D& OutFinalPosition)
{
	if (DraggingNodeID == -1 || !Nodes.Contains(DraggingNodeID))
	{
		DraggingNodeID = -1;
		return false;
	}
	OutNodeID = DraggingNodeID;
	OutFinalPosition = Nodes[DraggingNodeID].Position;
	DraggingNodeID = -1;
	RefreshNodeHandles();
	return true;
}

void ARoomPlannerManager::RefreshNodeHandles()
{
	if (!NodeHandleMesh) return;

	NodeHandleMesh->ClearAllMeshSections();
	const bool bShow = b2DViewMode && ActiveToolMode == EPlannerToolMode::Select && Nodes.Num() > 0;
	NodeHandleMesh->SetVisibility(bShow);
	if (!bShow) return;

	int32 Section = 0;
	for (const auto& Pair : Nodes)
	{
		const bool bActive = (Pair.Key == DraggingNodeID);
		// Handle on the wall TOP (see GetNodeHandleZ) and wider than the thickest wall at the node, so the disc
		// sits clearly above the wall and a ring stays visible around its footprint. Hit testing uses the same
		// plane (FindNodeAtCursorRay), so what is seen is exactly what is dragged.
		float MaxHalfThickness = 10.f;
		for (int32 SegID : Pair.Value.ConnectedSegmentIDs)
		{
			if (const FWallSegment* Seg = WallSegments.Find(SegID)) MaxHalfThickness = FMath::Max(MaxHalfThickness, Seg->Thickness * 0.5f);
		}
		const float Radius = FMath::Max(bActive ? 16.f : 12.f, MaxHalfThickness + (bActive ? 10.f : 6.f));
		const float Z = GetNodeHandleZ(Pair.Key);

		TArray<FVector> V; TArray<int32> T; TArray<FVector> N; TArray<FVector2D> UV;
		const FVector2D C = Pair.Value.Position;
		V.Add(FVector(C.X, C.Y, Z)); N.Add(FVector::UpVector); UV.Add(FVector2D(0.5f, 0.5f));
		const int32 Steps = 12;
		for (int32 i = 0; i < Steps; ++i)
		{
			const float A = 2.f * PI * (float)i / (float)Steps;
			V.Add(FVector(C.X + FMath::Cos(A) * Radius, C.Y + FMath::Sin(A) * Radius, Z));
			N.Add(FVector::UpVector);
			UV.Add(FVector2D(0.5f + 0.5f * FMath::Cos(A), 0.5f + 0.5f * FMath::Sin(A)));
		}
		for (int32 i = 0; i < Steps; ++i)
		{
			const int32 A = 1 + i;
			const int32 B = 1 + ((i + 1) % Steps);
			// Both windings so the disc is visible from above and below in every camera mode.
			T.Add(0); T.Add(B); T.Add(A);
			T.Add(0); T.Add(A); T.Add(B);
		}
		NodeHandleMesh->CreateMeshSection(Section, V, T, N, UV, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
		UMaterialInterface* Mat = bActive ? WallSelectionMaterial.Get() : OpeningSelectionMaterial.Get();
		if (!Mat) Mat = WallSelectionMaterial.Get();
		if (Mat) NodeHandleMesh->SetMaterial(Section, Mat);
		++Section;
	}
}

void ARoomPlannerManager::ComputeWallInteriorSides(const TArray<TArray<FVector2D>>& RoomPolygons)
{
	bool bAnyChanged = false;

	TMap<int32, bool> NewFlags;
	for (const auto& Pair : WallSegments)
	{
		NewFlags.Add(Pair.Key, true);
	}
	for (auto& RoomPair : Rooms)
	{
		RoomPair.Value.WallSegmentIDs.Reset();
	}

	auto NearlySame = [](const FVector2D& A, const FVector2D& B) { return FVector2D::DistSquared(A, B) < 4.f; };
	TMap<int32, int32> Claims; // rooms a wall bounds

	for (int32 PolyIdx = 0; PolyIdx < RoomPolygons.Num(); ++PolyIdx)
	{
		const TArray<FVector2D>& Poly = RoomPolygons[PolyIdx];
		const int32 N = Poly.Num();
		FRoomData* Room = Rooms.Find(PolyIdx + 1);
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D& Pi = Poly[i];
			const FVector2D& Pj = Poly[(i + 1) % N];
			for (const auto& Pair : WallSegments)
			{
				const FWallSegment& Seg = Pair.Value;
				const FWallNode* S = Nodes.Find(Seg.StartNodeID);
				const FWallNode* E = Nodes.Find(Seg.EndNodeID);
				if (!S || !E) continue;
				if (NearlySame(S->Position, Pi) && NearlySame(E->Position, Pj))
				{
					NewFlags[Pair.Key] = true;   // polygon is CCW: interior on the left of Start->End
					Claims.FindOrAdd(Pair.Key)++;
					if (Room) Room->WallSegmentIDs.AddUnique(Pair.Key);
				}
				else if (NearlySame(S->Position, Pj) && NearlySame(E->Position, Pi))
				{
					NewFlags[Pair.Key] = false;  // traversed End->Start: interior on the right
					Claims.FindOrAdd(Pair.Key)++;
					if (Room) Room->WallSegmentIDs.AddUnique(Pair.Key);
				}
			}
		}
	}

	// A wall between two rooms has a room on both sides: its side stays the left one whichever room is traced last (door swing and
	// hinge sides saved with the layout were authored against that).
	for (const TPair<int32, int32>& Claim : Claims)
	{
		if (Claim.Value >= 2) NewFlags[Claim.Key] = true;
	}

	for (auto& Pair : WallSegments)
	{
		const bool NewVal = NewFlags.FindRef(Pair.Key);
		if (Pair.Value.bLeftSideIsInterior != NewVal)
		{
			Pair.Value.bLeftSideIsInterior = NewVal;
			bAnyChanged = true;
		}
	}

	if (bAnyChanged)
	{
		bWallsRebuiltByInteriorPass = true;
		RebuildAllWalls(); // leaves depend on the interior side
	}
}

bool ARoomPlannerManager::IsWallLeftSideInterior(int32 SegmentID) const
{
	if (const FWallSegment* Seg = WallSegments.Find(SegmentID))
	{
		return Seg->bLeftSideIsInterior;
	}
	return true;
}

bool ARoomPlannerManager::GetWallSegmentData(int32 SegmentID, FWallSegment& OutSegment) const
{
	if (const FWallSegment* Seg = WallSegments.Find(SegmentID))
	{
		OutSegment = *Seg;
		return true;
	}
	return false;
}

bool ARoomPlannerManager::GetRoomData(int32 RoomID, FRoomData& OutRoom) const
{
	if (const FRoomData* Room = Rooms.Find(RoomID))
	{
		OutRoom = *Room;
		return true;
	}
	return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
// REQ-07: opening swing
// ═══════════════════════════════════════════════════════════════════════════════

bool ARoomPlannerManager::SetOpeningSwing(int32 SegmentID, int32 OpeningIndex, EOpeningSwingSide Side, EOpeningSwingDirection Direction)
{
	FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg || !Seg->Openings.IsValidIndex(OpeningIndex)) return false;

	Seg->Openings[OpeningIndex].SwingSide = Side;
	Seg->Openings[OpeningIndex].SwingDirection = Direction;

	RebuildAllWalls();
	CommitStateAfterMutation();
	UpdateSelectionVisuals();
	return true;
}

bool ARoomPlannerManager::SetOpeningStyle(int32 SegmentID, int32 OpeningIndex, FName StyleID)
{
	FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg || !Seg->Openings.IsValidIndex(OpeningIndex)) return false;

	FWallOpening& Op = Seg->Openings[OpeningIndex];
	if (!PlannerOpeningStyles::IsValidFor(Op.Type, StyleID))
	{
		UE_LOG(LogTemp, Warning, TEXT("[MaxiMallConstructor] SetOpeningStyle: '%s' is not a style for this opening type."), *StyleID.ToString());
		return false;
	}
	if (Op.Style == StyleID) return true;

	Op.Style = StyleID;
	RebuildAllWalls();
	CommitStateAfterMutation();
	UpdateSelectionVisuals();
	return true;
}

bool ARoomPlannerManager::GetOpeningStyle(int32 SegmentID, int32 OpeningIndex, FName& OutStyleID) const
{
	const FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg || !Seg->Openings.IsValidIndex(OpeningIndex)) return false;
	const FWallOpening& Op = Seg->Openings[OpeningIndex];
	OutStyleID = PlannerOpeningStyles::Resolve(Op.Type, Op.Style).ID;
	return true;
}

bool ARoomPlannerManager::GetOpeningType(int32 SegmentID, int32 OpeningIndex, EOpeningType& OutType) const
{
	const FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg || !Seg->Openings.IsValidIndex(OpeningIndex)) return false;
	OutType = Seg->Openings[OpeningIndex].Type;
	return true;
}

TArray<FPlannerCatalogEntry> ARoomPlannerManager::GetAvailableOpeningStyles(EOpeningType Type) const
{
	TArray<FPlannerCatalogEntry> Entries;
	for (const FPlannerOpeningStyle& Style : PlannerOpeningStyles::All())
	{
		if (Style.Type != Type) continue;
		FPlannerCatalogEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.ID = Style.ID.ToString();
		Entry.DisplayName = FText::FromString(Style.DisplayName);
		Entry.Category = TEXT("OpeningStyle");
		Entry.Color = Style.LeafColor;
	}
	return Entries;
}

// ── Door / window leaves in 3D (local view state) ──

namespace
{
	constexpr float LeafOpenSeconds = 0.65f;
	constexpr float LeafCloseSeconds = 0.5f;

	/** Opening: ease-out that settles with a barely visible (~1°) overshoot. Closing: ease-in-out. */
	float EaseLeafMotion(float T, bool bOpening)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		if (bOpening)
		{
			const float C1 = 0.6f;
			const float C3 = C1 + 1.f;
			const float U = T - 1.f;
			return 1.f + C3 * U * U * U + C1 * U * U;
		}
		return T < 0.5f ? 4.f * T * T * T : 1.f - FMath::Pow(-2.f * T + 2.f, 3.f) * 0.5f;
	}
}

FString ARoomPlannerManager::MakeLeafKey(const AProceduralWallActor* Wall, int32 OpeningIndex)
{
	// Keyed by the opening itself, not by its place in the array: UpdateOpeningPosition re-sorts the openings by distance,
	// so an index names a different opening from one drag frame to the next.
	if (!Wall || !Wall->WallData.Openings.IsValidIndex(OpeningIndex)) return FString();
	// An opening built by hand (tests, Blueprint) can carry no ID; two of them on one wall would otherwise share one key.
	const FString& OpeningID = Wall->WallData.Openings[OpeningIndex].OpeningID;
	return OpeningID.IsEmpty()
		? FString::Printf(TEXT("%s#i%d"), *Wall->WallData.WallGuid, OpeningIndex)
		: FString::Printf(TEXT("%s#%s"), *Wall->WallData.WallGuid, *OpeningID);
}

ARoomPlannerManager::FLeafAnimation* ARoomPlannerManager::FindLeafAnimation(AProceduralWallActor* Wall, int32 OpeningIndex)
{
	if (!Wall || !Wall->WallData.Openings.IsValidIndex(OpeningIndex)) return nullptr;
	// The key names the opening, so whatever is stored under it belongs to it: the type + distance fingerprint that stood in
	// for an identity is gone, and with it the reset it forced on every drag, resize and delete.
	return LeafAnimations.Find(MakeLeafKey(Wall, OpeningIndex));
}

float ARoomPlannerManager::ComputeBranchCoverOnFace(int32 SegmentID, int32 NodeID, const FVector2D& AwayDir, const FVector2D& FaceNormal, float HalfThickness) const
{
	const FWallNode* Node = Nodes.Find(NodeID);
	if (!Node) return 0.f;
	float Cover = 0.f;
	for (int32 OtherID : Node->ConnectedSegmentIDs)
	{
		if (OtherID == SegmentID) continue;
		const FWallSegment* Other = WallSegments.Find(OtherID);
		if (!Other) continue;
		const FWallNode* Far = Nodes.Find(Other->StartNodeID == NodeID ? Other->EndNodeID : Other->StartNodeID);
		if (!Far) continue;
		const FVector2D OtherDir = (Far->Position - Node->Position).GetSafeNormal();
		if (FVector2D::DotProduct(OtherDir, FaceNormal) <= 0.f) continue; // on the other face's side
		const float Sin = FMath::Abs(AwayDir.X * OtherDir.Y - AwayDir.Y * OtherDir.X);
		if (Sin < 0.05f) continue; // collinear continuation
		// Distance along this face from the node to the far edge of the other wall's footprint. A branch leaning away (obtuse)
		// starts at its end cap, which limits how far along this face it can reach.
		const float Cos = FVector2D::DotProduct(AwayDir, OtherDir);
		float WallCover = (HalfThickness * Cos + Other->Thickness * 0.5f) / Sin;
		if (Cos < 0.f)
		{
			WallCover = FMath::Min(WallCover, HalfThickness * Sin / -Cos);
		}
		Cover = FMath::Max(Cover, WallCover);
	}
	return Cover;
}

bool ARoomPlannerManager::IsOpeningLeafComponent(const UPrimitiveComponent* Component)
{
	return Component && Component->ComponentHasTag(AProceduralWallActor::LeafComponentTag)
		&& Cast<AProceduralWallActor>(Component->GetOwner()) != nullptr;
}

void ARoomPlannerManager::StartLeafAnimation(AProceduralWallActor* Wall, int32 OpeningIndex, float Target, float InitialFraction)
{
	if (!Wall || !Wall->HasLeaf(OpeningIndex)) return;

	const FString Key = MakeLeafKey(Wall, OpeningIndex);
	const bool bNew = LeafAnimations.Find(Key) == nullptr;
	FLeafAnimation& Anim = LeafAnimations.FindOrAdd(Key);
	if (bNew)
	{
		Anim.Current = Anim.Target = InitialFraction;
	}
	Anim.From = Anim.Current;
	Anim.Target = Target;
	Anim.Elapsed = 0.f;
	// Reversing a half-finished swing takes proportionally less time.
	Anim.Duration = (Target > Anim.Current ? LeafOpenSeconds : LeafCloseSeconds) * FMath::Clamp(FMath::Abs(Target - Anim.Current), 0.25f, 1.f);
	Anim.bAnimating = !FMath::IsNearlyEqual(Anim.Current, Target);
	bAnyLeafAnimating |= Anim.bAnimating;
}

void ARoomPlannerManager::ToggleLeafOnWall(AProceduralWallActor* Wall, int32 OpeningIndex)
{
	if (!Wall || !Wall->HasLeaf(OpeningIndex)) return;
	const float Default = bDefaultLeavesOpen ? 1.f : 0.f;
	const FLeafAnimation* Existing = FindLeafAnimation(Wall, OpeningIndex);
	const float CurrentTarget = Existing ? Existing->Target : Default;
	StartLeafAnimation(Wall, OpeningIndex, CurrentTarget > 0.5f ? 0.f : 1.f, Default);
}

void ARoomPlannerManager::ToggleOpeningLeaf(int32 SegmentID, int32 OpeningIndex)
{
	if (b2DViewMode) return; // the 2D plan always shows the plan pose
	const TObjectPtr<AProceduralWallActor>* WallPtr = WallActors.Find(SegmentID);
	ToggleLeafOnWall(WallPtr ? WallPtr->Get() : nullptr, OpeningIndex);
}

bool ARoomPlannerManager::TryToggleOpeningLeafFromHit(const FHitResult& Hit)
{
	if (b2DViewMode) return false;
	const UPrimitiveComponent* Component = Hit.GetComponent();
	if (!IsOpeningLeafComponent(Component)) return false;
	AProceduralWallActor* Wall = Cast<AProceduralWallActor>(Component->GetOwner());
	const int32 Index = Wall ? Wall->FindLeafIndex(Component) : INDEX_NONE;
	if (Index == INDEX_NONE) return false;

	// One press can arrive here twice (the planner widget's 3D pick and PlayerTick's pick): toggle it once.
	const FString Key = MakeLeafKey(Wall, Index);
	const double Now = FPlatformTime::Seconds();
	if (Key == LastLeafToggleKey && Now - LastLeafToggleTime < 0.2)
	{
		return true;
	}
	LastLeafToggleKey = Key;
	LastLeafToggleTime = Now;

	ToggleLeafOnWall(Wall, Index);
	return true;
}

void ARoomPlannerManager::SetAllOpeningLeavesOpen(bool bOpen)
{
	const float PreviousDefault = bDefaultLeavesOpen ? 1.f : 0.f;
	bDefaultLeavesOpen = bOpen;
	const float Target = bOpen ? 1.f : 0.f;
	for (const auto& Pair : WallActors)
	{
		AProceduralWallActor* Wall = Pair.Value;
		if (!Wall) continue;
		for (int32 i = 0; i < Wall->WallData.Openings.Num(); ++i)
		{
			if (!Wall->HasLeaf(i)) continue;
			if (b2DViewMode)
			{
				// Not visible in 2D: jump straight to the new state for the next 3D view.
				FLeafAnimation& Anim = LeafAnimations.FindOrAdd(MakeLeafKey(Wall, i));
				Anim.Current = Anim.From = Anim.Target = Target;
				Anim.bAnimating = false;
				Wall->SetLeafOpenFraction(i, Target);
			}
			else
			{
				StartLeafAnimation(Wall, i, Target, PreviousDefault);
			}
		}
	}
}

void ARoomPlannerManager::TickLeafAnimations(float DeltaTime)
{
	if (!bAnyLeafAnimating) return;
	bAnyLeafAnimating = false;
	for (const auto& Pair : WallActors)
	{
		AProceduralWallActor* Wall = Pair.Value;
		if (!Wall) continue;
		for (int32 i = 0; i < Wall->WallData.Openings.Num(); ++i)
		{
			FLeafAnimation* Anim = FindLeafAnimation(Wall, i);
			if (!Anim || !Anim->bAnimating) continue;

			Anim->Elapsed += DeltaTime;
			const float T = Anim->Duration > KINDA_SMALL_NUMBER ? Anim->Elapsed / Anim->Duration : 1.f;
			if (T >= 1.f)
			{
				Anim->Current = Anim->Target;
				Anim->bAnimating = false;
			}
			else
			{
				Anim->Current = FMath::Lerp(Anim->From, Anim->Target, EaseLeafMotion(T, Anim->Target > Anim->From));
				bAnyLeafAnimating = true;
			}
			Wall->SetLeafOpenFraction(i, Anim->Current);
		}
	}
}

void ARoomPlannerManager::ApplyLeafAnimationsToWall(AProceduralWallActor* Wall)
{
	if (!Wall) return;
	const float Default = bDefaultLeavesOpen ? 1.f : 0.f;
	for (int32 i = 0; i < Wall->WallData.Openings.Num(); ++i)
	{
		const FLeafAnimation* Anim = FindLeafAnimation(Wall, i);
		Wall->SetLeafOpenFraction(i, Anim ? Anim->Current : Default);
		if (Anim && Anim->bAnimating)
		{
			bAnyLeafAnimating = true;
		}
	}
}

float ARoomPlannerManager::GetLeafOpenTargetForDebug(int32 SegmentID, int32 OpeningIndex) const
{
	// Target, not Current: the ease runs on Tick, which a test world never reaches.
	const TObjectPtr<AProceduralWallActor>* WallPtr = WallActors.Find(SegmentID);
	const AProceduralWallActor* Wall = WallPtr ? WallPtr->Get() : nullptr;
	const FLeafAnimation* Anim = Wall ? LeafAnimations.Find(MakeLeafKey(Wall, OpeningIndex)) : nullptr;
	return Anim ? Anim->Target : (bDefaultLeavesOpen ? 1.f : 0.f);
}

bool ARoomPlannerManager::GetOpeningSwing(int32 SegmentID, int32 OpeningIndex, EOpeningSwingSide& OutSide, EOpeningSwingDirection& OutDirection) const
{
	const FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg || !Seg->Openings.IsValidIndex(OpeningIndex)) return false;
	OutSide = Seg->Openings[OpeningIndex].SwingSide;
	OutDirection = Seg->Openings[OpeningIndex].SwingDirection;
	return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// REQ-06: exact distances
// ═══════════════════════════════════════════════════════════════════════════════

bool ARoomPlannerManager::GetOpeningDistances(int32 SegmentID, int32 OpeningIndex, float& OutLeftCornerCm, float& OutRightCornerCm, float& OutFloorCm, float& OutNeighborCm, bool& bOutHasNeighbor) const
{
	OutLeftCornerCm = OutRightCornerCm = OutFloorCm = OutNeighborCm = 0.f;
	bOutHasNeighbor = false;

	const FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg || !Seg->Openings.IsValidIndex(OpeningIndex)) return false;
	if (!Nodes.Contains(Seg->StartNodeID) || !Nodes.Contains(Seg->EndNodeID)) return false;

	const float WallLen = FVector2D::Distance(Nodes[Seg->StartNodeID].Position, Nodes[Seg->EndNodeID].Position);
	const FWallOpening& Op = Seg->Openings[OpeningIndex];
	const float Start = Op.DistanceFromStart - Op.Width * 0.5f;
	const float End = Op.DistanceFromStart + Op.Width * 0.5f;

	// Clear dimensions: from the inner corners of the room-side face (where it meets the adjoining walls).
	FVector2D FaceExtent(0.f, WallLen);
	GetVisibleWallFaceExtent(SegmentID, Seg->bLeftSideIsInterior, FaceExtent);
	const float ToStartCorner = FMath::Max(0.f, Start - (float)FaceExtent.X);
	const float ToEndCorner = FMath::Max(0.f, (float)FaceExtent.Y - End);

	// Seen from inside the room: with the interior on the wall's LEFT face, the observer's
	// right hand points to the START node (see AProceduralWallActor::IsHingeAtStart).
	if (Seg->bLeftSideIsInterior)
	{
		OutRightCornerCm = ToStartCorner;
		OutLeftCornerCm = ToEndCorner;
	}
	else
	{
		OutLeftCornerCm = ToStartCorner;
		OutRightCornerCm = ToEndCorner;
	}
	OutFloorCm = FMath::Max(0.f, Op.SillHeight);

	float Best = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Seg->Openings.Num(); ++i)
	{
		if (i == OpeningIndex) continue;
		const FWallOpening& Other = Seg->Openings[i];
		const float OStart = Other.DistanceFromStart - Other.Width * 0.5f;
		const float OEnd = Other.DistanceFromStart + Other.Width * 0.5f;
		float Gap;
		if (OStart >= End) Gap = OStart - End;
		else if (OEnd <= Start) Gap = Start - OEnd;
		else Gap = 0.f; // overlapping
		if (Gap < Best) Best = Gap;
	}
	if (Best < TNumericLimits<float>::Max())
	{
		bOutHasNeighbor = true;
		OutNeighborCm = Best;
	}
	return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// REQ-02 / 04 / 06: dimension labels
// ═══════════════════════════════════════════════════════════════════════════════

TArray<FPlannerDimensionLabel> ARoomPlannerManager::GetSelectionDimensionLabels() const
{
	TArray<FPlannerDimensionLabel> Labels;

	auto AddLabel = [&Labels](const FString& Key, const FString& Text, float Value, const FVector& World)
	{
		FPlannerDimensionLabel L;
		L.Key = Key; L.Text = Text; L.Value = Value; L.WorldLocation = World;
		Labels.Add(L);
	};

	// Clear length of a wall face, between the room's inner corners.
	auto WallLengthLabel = [&](int32 SegID, bool bLeftFace)
	{
		const FWallSegment* Seg = WallSegments.Find(SegID);
		if (!Seg || !Nodes.Contains(Seg->StartNodeID) || !Nodes.Contains(Seg->EndNodeID)) return;
		const FVector2D P1 = Nodes[Seg->StartNodeID].Position;
		const FVector2D P2 = Nodes[Seg->EndNodeID].Position;
		FVector2D Extent(0.f, FVector2D::Distance(P1, P2));
		GetVisibleWallFaceExtent(SegID, bLeftFace, Extent);
		const float LenCm = FMath::Max(0.f, (float)(Extent.Y - Extent.X));
		const FVector2D Mid = (P1 + P2) * 0.5f;
		AddLabel(TEXT("length"), PlannerJsonKeys::FormatMeters(LenCm), LenCm, FVector(Mid.X, Mid.Y, Seg->Height + 15.f));
	};

	// While a control point is dragged: live length of every wall attached to it (REQ-02), on the same faces as the dimension lines.
	if (DraggingNodeID != -1)
	{
		if (const FWallNode* Node = Nodes.Find(DraggingNodeID))
		{
			TArray<FPlannerWallFaceInput> Inputs;
			TMap<int32, int32> IndexBySegment;
			TArray<FVector2D> FaceExtents;
			GatherWallFaces(Inputs, IndexBySegment, FaceExtents);
			for (int32 SegID : Node->ConnectedSegmentIDs)
			{
				WallLengthLabel(SegID, (SegID == SelectedSegmentID) ? bSelectedWallFaceLeft : GetRoomSideFaceLeft(SegID, IndexBySegment, FaceExtents));
			}
		}
		return Labels;
	}

	switch (GetSelectionKind())
	{
	case EPlannerSelectionKind::Wall:
	{
		WallLengthLabel(SelectedSegmentID, bSelectedWallFaceLeft);
		if (const FWallSegment* Seg = WallSegments.Find(SelectedSegmentID))
		{
			if (Nodes.Contains(Seg->EndNodeID))
			{
				const FVector2D P = Nodes[Seg->EndNodeID].Position;
				AddLabel(TEXT("height"), PlannerJsonKeys::FormatMeters(Seg->Height), Seg->Height, FVector(P.X, P.Y, Seg->Height * 0.5f));
			}
		}
		break;
	}
	case EPlannerSelectionKind::Opening:
	{
		const FWallSegment* Seg = WallSegments.Find(SelectedSegmentID);
		if (!Seg || !Seg->Openings.IsValidIndex(SelectedOpeningIndex)) break;
		if (!Nodes.Contains(Seg->StartNodeID) || !Nodes.Contains(Seg->EndNodeID)) break;

		const FWallOpening& Op = Seg->Openings[SelectedOpeningIndex];
		const FVector2D P1 = Nodes[Seg->StartNodeID].Position;
		const FVector2D P2 = Nodes[Seg->EndNodeID].Position;
		const float WallLen = FVector2D::Distance(P1, P2);
		const FVector2D Dir = (P2 - P1).GetSafeNormal();
		const FVector2D Normal(-Dir.Y, Dir.X);
		const FVector2D Interior = bSelectedWallFaceLeft ? Normal : -Normal; // the picked face, as the dimension lines
		const FVector2D Off = Interior * (Seg->Thickness * 0.5f + 5.f);

		auto AlongWall = [&](float DistCm, float Z) { const FVector2D P = P1 + Dir * DistCm + Off; return FVector(P.X, P.Y, Z); };

		const float Start = Op.DistanceFromStart - Op.Width * 0.5f;
		const float End = Op.DistanceFromStart + Op.Width * 0.5f;
		const float TopZ = Op.SillHeight + Op.Height;

		AddLabel(TEXT("width"), PlannerJsonKeys::FormatMeters(Op.Width), Op.Width, AlongWall(Op.DistanceFromStart, TopZ + 12.f));
		AddLabel(TEXT("height"), PlannerJsonKeys::FormatMeters(Op.Height), Op.Height, AlongWall(End + 8.f, Op.SillHeight + Op.Height * 0.5f));
		if (Op.SillHeight > 0.5f)
		{
			AddLabel(TEXT("sill"), PlannerJsonKeys::FormatMeters(Op.SillHeight), Op.SillHeight, AlongWall(Start - 8.f, Op.SillHeight * 0.5f));
		}

		float L, R, Fl, Nb; bool bHasNb;
		if (GetOpeningDistances(SelectedSegmentID, SelectedOpeningIndex, L, R, Fl, Nb, bHasNb))
		{
			// Clear distances from the inner corners of the picked face (the same values as its dimension lines), each placed in the
			// middle of its gap.
			FVector2D FaceExtent(0.f, WallLen);
			GetVisibleWallFaceExtent(SelectedSegmentID, bSelectedWallFaceLeft, FaceExtent);
			const float ToStart = FMath::Max(0.f, Start - (float)FaceExtent.X);
			const float ToEnd = FMath::Max(0.f, (float)FaceExtent.Y - End);
			const FVector StartGapPos = AlongWall(((float)FaceExtent.X + Start) * 0.5f, 25.f);
			const FVector EndGapPos = AlongWall((End + (float)FaceExtent.Y) * 0.5f, 25.f);
			if (bSelectedWallFaceLeft)
			{
				AddLabel(TEXT("distRight"), PlannerJsonKeys::FormatMeters(ToStart), ToStart, StartGapPos);
				AddLabel(TEXT("distLeft"), PlannerJsonKeys::FormatMeters(ToEnd), ToEnd, EndGapPos);
			}
			else
			{
				AddLabel(TEXT("distLeft"), PlannerJsonKeys::FormatMeters(ToStart), ToStart, StartGapPos);
				AddLabel(TEXT("distRight"), PlannerJsonKeys::FormatMeters(ToEnd), ToEnd, EndGapPos);
			}
			AddLabel(TEXT("distFloor"), PlannerJsonKeys::FormatMeters(Fl), Fl, AlongWall(Op.DistanceFromStart, FMath::Max(8.f, Fl * 0.5f)));
			if (bHasNb)
			{
				// Place at the middle of the gap to the nearest neighbour.
				float GapCenter = Op.DistanceFromStart;
				float BestGap = TNumericLimits<float>::Max();
				for (int32 i = 0; i < Seg->Openings.Num(); ++i)
				{
					if (i == SelectedOpeningIndex) continue;
					const FWallOpening& Other = Seg->Openings[i];
					const float OStart = Other.DistanceFromStart - Other.Width * 0.5f;
					const float OEnd = Other.DistanceFromStart + Other.Width * 0.5f;
					if (OStart >= End && OStart - End < BestGap) { BestGap = OStart - End; GapCenter = (End + OStart) * 0.5f; }
					else if (OEnd <= Start && Start - OEnd < BestGap) { BestGap = Start - OEnd; GapCenter = (OEnd + Start) * 0.5f; }
				}
				AddLabel(TEXT("distNeighbor"), PlannerJsonKeys::FormatMeters(Nb), Nb, AlongWall(GapCenter, TopZ * 0.5f));
			}
		}
		break;
	}
	case EPlannerSelectionKind::Floor:
	{
		if (const FRoomData* Room = Rooms.Find(SelectedRoomID))
		{
			AddLabel(TEXT("area"), FString::Printf(TEXT("%.2f м²"), Room->AreaM2), Room->AreaM2, FVector(Room->InteriorPoint.X, Room->InteriorPoint.Y, 5.f));
		}
		break;
	}
	case EPlannerSelectionKind::Object:
	{
		if (const FPlacedFurnitureData* D = PlacedObjects.Find(SelectedObjectID))
		{
			// The object actor stands its mesh's bounds bottom-centre on its origin (any authored pivot), so the top is 2·Half.Z above it.
			FVector Half(50.f, 50.f, 50.f);
			if (APlannerPlacedObjectActor* A = FindPlacedObjectActor(SelectedObjectID)) Half = A->GetLocalHalfExtents();
			const FString SizeText = FString::Printf(TEXT("%.2f × %.2f × %.2f м"), Half.X * 2.f / 100.f, Half.Y * 2.f / 100.f, Half.Z * 2.f / 100.f);
			AddLabel(TEXT("size"), SizeText, Half.X * 2.f, D->Location + FVector(0.f, 0.f, Half.Z * 2.f + 10.f));
		}
		break;
	}
	case EPlannerSelectionKind::CabinetSet:
	{
		if (const FPlacedCabinetSetData* D = CabinetSets.Find(SelectedCabinetSetID))
		{
			// The footprint of its visible parts in its own axes, as its gap lines (the world box of a turned set is larger, and
			// GetActorBounds also counts its trigger shapes).
			FVector2D Center(D->Location.X, D->Location.Y);
			FVector2D AxisX(1., 0.);
			FVector2D HalfSize(50., 50.);
			float TopZ = D->Location.Z + 100.f;
			GetActorFootprint(FindCabinetSetActor(SelectedCabinetSetID), Center, AxisX, HalfSize, TopZ);
			const FString SizeText = FString::Printf(TEXT("%s  %.2f × %.2f м"), *D->ProductID.ToString(), HalfSize.X * 2.f / 100.f, HalfSize.Y * 2.f / 100.f);
			AddLabel(TEXT("size"), SizeText, HalfSize.X * 2.f, FVector(Center.X, Center.Y, TopZ + 10.f));
		}
		break;
	}
	default:
		break;
	}

	return Labels;
}

namespace PlannerDimensionPlacement
{
	constexpr float RowSpacing = 40.f;      // dimension rows stand this far apart, the first one this far off the face
	constexpr float ChainZ = 2.f;           // just above the floor slab (its top is at 1 cm)
	constexpr float JambOffset = 15.f;      // an opening's vertical dimensions stand this far beside it
	constexpr float MinDimension = 0.5f;    // shorter distances (an opening flush with a corner) get no line
	constexpr float CornerClearance = 5.f;  // vertical dimensions keep this far from the face's corners
	constexpr float MinObjectGap = 1.f;     // an object side closer to a wall touches it
	constexpr float MaxObjectGap = 2000.f;  // walls further away than this are not dimensioned

	/** A wall and one of its faces: where dimension lines beside that face go. */
	struct FFaceFrame
	{
		FVector2D P1 = FVector2D::ZeroVector;
		FVector2D Dir = FVector2D(1., 0.);
		FVector2D Normal = FVector2D(0., 1.);   // out of the face
		float Length = 0.f;
		float Face = 0.f;                       // distance from the centre line to the face
		FVector2D Extent = FVector2D::ZeroVector; // visible part of the face, along the wall from its start node

		FVector At(float Along, float Offset, float Z) const
		{
			const FVector2D P = P1 + Dir * Along + Normal * Offset;
			return FVector(P.X, P.Y, Z);
		}

		/** A line along the face, Row rows out, with extension lines from the face. */
		void AddAlong(TArray<FPlannerDimensionLine>& Lines, const TCHAR* Key, float From, float To, float Row) const
		{
			const float Value = To - From;
			if (Value < MinDimension) return;
			FPlannerDimensionLine Line;
			Line.Key = Key;
			Line.Value = Value;
			Line.Text = PlannerJsonKeys::FormatMeters(Value);
			Line.Start = At(From, Face + Row * RowSpacing, ChainZ);
			Line.End = At(To, Face + Row * RowSpacing, ChainZ);
			Line.StartRef = At(From, Face, ChainZ);
			Line.EndRef = At(To, Face, ChainZ);
			Lines.Add(Line);
		}

		/** A vertical line on the face beside a jamb, with extension lines from the jamb. */
		void AddVertical(TArray<FPlannerDimensionLine>& Lines, const TCHAR* Key, const TCHAR* Prefix, float Jamb, float Beside, float FromZ, float ToZ) const
		{
			const float Value = ToZ - FromZ;
			if (Value < MinDimension) return;
			FPlannerDimensionLine Line;
			Line.Key = Key;
			Line.Value = Value;
			Line.Text = FString(Prefix) + PlannerJsonKeys::FormatMeters(Value);
			Line.Start = At(Beside, Face + 1.f, FromZ);
			Line.End = At(Beside, Face + 1.f, ToZ);
			Line.StartRef = At(Jamb, Face + 1.f, FromZ);
			Line.EndRef = At(Jamb, Face + 1.f, ToZ);
			Line.bVertical = true;
			Lines.Add(Line);
		}
	};
}

void ARoomPlannerManager::GatherWallFaces(TArray<FPlannerWallFaceInput>& OutInputs, TMap<int32, int32>& OutIndexBySegment, TArray<FVector2D>& OutFaceExtents) const
{
	OutInputs.Reset();
	OutIndexBySegment.Reset();
	TArray<int32> SegIDs;
	WallSegments.GetKeys(SegIDs);
	SegIDs.Sort();
	for (int32 SegID : SegIDs)
	{
		FPlannerWallFaceInput In;
		if (!MakeWallFaceInput(SegID, In)) continue;
		OutIndexBySegment.Add(SegID, OutInputs.Num());
		OutInputs.Add(In);
	}

	// A dragged corner snapped onto another corner or onto another wall is only joined to it on release (TryConnectMovedNode).
	// Pair its faces as that junction will, so the live lengths already end at the visible corners.
	if (const FWallNode* Dragged = (DraggingNodeID != -1) ? Nodes.Find(DraggingNodeID) : nullptr)
	{
		const FVector2D P = Dragged->Position;
		auto IsNeighbour = [&](int32 NodeID)
		{
			for (int32 SegID : Dragged->ConnectedSegmentIDs)
			{
				const FWallSegment* Seg = WallSegments.Find(SegID);
				if (Seg && (Seg->StartNodeID == NodeID || Seg->EndNodeID == NodeID)) return true;
			}
			return false;
		};
		// MergeNodeInto refuses a merge that would duplicate a wall (both corners already walled to the same third corner).
		auto WouldDuplicateWall = [&](const FWallNode& Target, int32 TargetID)
		{
			for (int32 SegID : Dragged->ConnectedSegmentIDs)
			{
				const FWallSegment* Seg = WallSegments.Find(SegID);
				if (!Seg) continue;
				const int32 OtherEnd = (Seg->StartNodeID == DraggingNodeID) ? Seg->EndNodeID : Seg->StartNodeID;
				for (int32 TargetSegID : Target.ConnectedSegmentIDs)
				{
					const FWallSegment* TargetSeg = WallSegments.Find(TargetSegID);
					if (TargetSeg && ((TargetSeg->StartNodeID == TargetID) ? TargetSeg->EndNodeID : TargetSeg->StartNodeID) == OtherEnd) return true;
				}
			}
			return false;
		};
		int32 JoinID = INDEX_NONE;
		for (const TPair<int32, FWallNode>& Pair : Nodes)
		{
			if (Pair.Key != DraggingNodeID && !IsNeighbour(Pair.Key) && Pair.Value.Position.Equals(P, 0.5f) && !WouldDuplicateWall(Pair.Value, Pair.Key))
			{
				JoinID = Pair.Key;
				break;
			}
		}
		if (JoinID == INDEX_NONE)
		{
			// Onto the middle of a wall: that wall is split there (the through wall of a T-junction, plain corners at the split), within
			// the same limits as TryConnectMovedNode.
			constexpr int32 SplitNodeID = -2;
			for (int32 i = 0; i < OutInputs.Num(); ++i)
			{
				FPlannerWallFaceInput& Wall = OutInputs[i];
				if (Wall.StartNodeID == DraggingNodeID || Wall.EndNodeID == DraggingNodeID) continue;
				const float Len = FVector2D::Distance(Wall.Start, Wall.End);
				if (Len < 40.f) continue;
				const FVector2D Dir = (Wall.End - Wall.Start) / Len;
				const float T = FVector2D::DotProduct(P - Wall.Start, Dir);
				if (T < 20.f || T > Len - 20.f || FVector2D::Distance(Wall.Start + Dir * T, P) > 0.5f) continue;
				const FWallSegment* Seg = WallSegments.Find(Wall.SegmentID);
				const float Half = Seg ? Seg->Thickness * 0.5f : 10.f;
				const FVector2D Left(-Dir.Y, Dir.X);
				FPlannerWallFaceInput Second = Wall;
				Second.SegmentID = -Wall.SegmentID - 1; // not a real segment; IndexBySegment keeps the first half
				Second.Start = P;
				Second.StartNodeID = SplitNodeID;
				Second.StartCorner[0] = P + Left * Half;
				Second.StartCorner[1] = P - Left * Half;
				Wall.End = P;
				Wall.EndNodeID = SplitNodeID;
				Wall.EndCorner[0] = Second.StartCorner[0];
				Wall.EndCorner[1] = Second.StartCorner[1];
				OutInputs.Add(Second);
				JoinID = SplitNodeID;
				break;
			}
		}
		if (JoinID != INDEX_NONE)
		{
			for (FPlannerWallFaceInput& Wall : OutInputs)
			{
				if (Wall.StartNodeID == DraggingNodeID) Wall.StartNodeID = JoinID;
				if (Wall.EndNodeID == DraggingNodeID) Wall.EndNodeID = JoinID;
			}
		}
	}
	OutFaceExtents = PlannerFinishLayout::VisibleFaceExtents(OutInputs);
}

bool ARoomPlannerManager::GetRoomSideFaceLeft(int32 SegmentID, const TMap<int32, int32>& IndexBySegment, const TArray<FVector2D>& FaceExtents) const
{
	const FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg) return true;
	for (const TPair<int32, FRoomData>& Pair : Rooms)
	{
		if (Pair.Value.WallSegmentIDs.Contains(SegmentID)) return Seg->bLeftSideIsInterior;
	}
	const int32* Index = IndexBySegment.Find(SegmentID);
	if (!Index) return Seg->bLeftSideIsInterior;
	const FVector2D Left = FaceExtents[*Index * 2];
	const FVector2D Right = FaceExtents[*Index * 2 + 1];
	return (Left.Y - Left.X) <= (Right.Y - Right.X) + 0.5f;
}

bool ARoomPlannerManager::GetVisibleWallFaceExtent(int32 SegmentID, bool bLeftFace, FVector2D& OutExtent) const
{
	TArray<FPlannerWallFaceInput> Inputs;
	TMap<int32, int32> IndexBySegment;
	TArray<FVector2D> FaceExtents;
	GatherWallFaces(Inputs, IndexBySegment, FaceExtents);
	const int32* Index = IndexBySegment.Find(SegmentID);
	if (!Index) return false;
	OutExtent = FaceExtents[*Index * 2 + (bLeftFace ? 0 : 1)];
	return true;
}

bool ARoomPlannerManager::GetActorFootprint(const AActor* Actor, FVector2D& OutCenter, FVector2D& OutAxisX, FVector2D& OutHalfSize, float& OutTopZ) const
{
	if (!Actor) return false;
	const FVector Forward = Actor->GetActorForwardVector();
	FVector2D AxisX(Forward.X, Forward.Y);
	if (!AxisX.Normalize()) AxisX = FVector2D(1., 0.);
	const FVector2D AxisY(-AxisX.Y, AxisX.X);
	const FVector2D Origin(Actor->GetActorLocation().X, Actor->GetActorLocation().Y);

	// Visible meshes only, each local box through its own transform (as MeasureAttachmentDepth: booths carry trigger shapes).
	FBox2D Local(ForceInit);
	float TopZ = -TNumericLimits<float>::Max();
	TArray<UStaticMeshComponent*> MeshComps;
	Actor->GetComponents<UStaticMeshComponent>(MeshComps);
	for (const UStaticMeshComponent* Comp : MeshComps)
	{
		if (!Comp || !Comp->GetStaticMesh() || !Comp->IsVisible()) continue;
		const FBox Box = Comp->GetStaticMesh()->GetBoundingBox();
		if (!Box.IsValid) continue;
		const FTransform& TM = Comp->GetComponentTransform();
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector World = TM.TransformPosition(FVector((Corner & 1) ? Box.Max.X : Box.Min.X, (Corner & 2) ? Box.Max.Y : Box.Min.Y,
				(Corner & 4) ? Box.Max.Z : Box.Min.Z));
			const FVector2D Rel = FVector2D(World.X, World.Y) - Origin;
			Local += FVector2D(FVector2D::DotProduct(Rel, AxisX), FVector2D::DotProduct(Rel, AxisY));
			TopZ = FMath::Max(TopZ, (float)World.Z);
		}
	}
	if (!Local.bIsValid) return false;
	const FVector2D Mid = (Local.Min + Local.Max) * 0.5f;
	OutCenter = Origin + AxisX * Mid.X + AxisY * Mid.Y;
	OutAxisX = AxisX;
	OutHalfSize = (Local.Max - Local.Min) * 0.5f;
	OutTopZ = TopZ;
	return true;
}

TArray<FPlannerDimensionLine> ARoomPlannerManager::GetSelectionDimensionLines() const
{
	using namespace PlannerDimensionPlacement;
	TArray<FPlannerDimensionLine> Lines;

	// Only a dragged corner and these four kinds are measured below; for anything else (a floor, a ceiling, a baseboard,
	// nothing at all) the wall walk that follows would run every frame just to fall through the switch.
	const EPlannerSelectionKind Kind = GetSelectionKind();
	if (DraggingNodeID == -1 && Kind != EPlannerSelectionKind::Wall && Kind != EPlannerSelectionKind::Opening
		&& Kind != EPlannerSelectionKind::Object && Kind != EPlannerSelectionKind::CabinetSet)
	{
		return Lines;
	}

	// Faces, corners and visible face extents of every wall, as the wall meshes use them (current while a corner is dragged too:
	// every drag step rebuilds the walls and their corner joints).
	TArray<FPlannerWallFaceInput> Inputs;
	TMap<int32, int32> IndexBySegment;
	TArray<FVector2D> FaceExtents;
	GatherWallFaces(Inputs, IndexBySegment, FaceExtents);

	auto MakeFrame = [&](int32 SegID, bool bLeftFace, FFaceFrame& Out)
	{
		const FWallSegment* Seg = WallSegments.Find(SegID);
		const int32* Index = IndexBySegment.Find(SegID);
		if (!Seg || !Index) return false;
		const FPlannerWallFaceInput& In = Inputs[*Index];
		Out.Length = FVector2D::Distance(In.Start, In.End);
		if (Out.Length < 1.f) return false;
		Out.P1 = In.Start;
		Out.Dir = (In.End - In.Start) / Out.Length;
		const FVector2D Left(-Out.Dir.Y, Out.Dir.X);
		Out.Normal = bLeftFace ? Left : -Left;
		Out.Face = Seg->Thickness * 0.5f;
		Out.Extent = FaceExtents[*Index * 2 + (bLeftFace ? 0 : 1)];
		return true;
	};

	// While a control point is dragged: the live length of every wall at it, on the picked face of a selected wall and on the room
	// side of the others.
	if (DraggingNodeID != -1)
	{
		if (const FWallNode* Node = Nodes.Find(DraggingNodeID))
		{
			for (int32 SegID : Node->ConnectedSegmentIDs)
			{
				const bool bLeftFace = (SegID == SelectedSegmentID) ? bSelectedWallFaceLeft : GetRoomSideFaceLeft(SegID, IndexBySegment, FaceExtents);
				FFaceFrame Frame;
				if (MakeFrame(SegID, bLeftFace, Frame))
				{
					Frame.AddAlong(Lines, TEXT("length"), Frame.Extent.X, Frame.Extent.Y, 1.f);
				}
			}
		}
		return Lines;
	}

	switch (GetSelectionKind())
	{
	case EPlannerSelectionKind::Wall:
	{
		FFaceFrame Frame;
		if (MakeFrame(SelectedSegmentID, bSelectedWallFaceLeft, Frame))
		{
			Frame.AddAlong(Lines, TEXT("length"), Frame.Extent.X, Frame.Extent.Y, 1.f);
		}
		break;
	}

	case EPlannerSelectionKind::Opening:
	{
		const FWallSegment* Seg = WallSegments.Find(SelectedSegmentID);
		FFaceFrame Frame;
		if (!Seg || !Seg->Openings.IsValidIndex(SelectedOpeningIndex) || !MakeFrame(SelectedSegmentID, bSelectedWallFaceLeft, Frame)) break;
		const FWallOpening& Op = Seg->Openings[SelectedOpeningIndex];
		const float Start = Op.DistanceFromStart - Op.Width * 0.5f;
		const float End = Op.DistanceFromStart + Op.Width * 0.5f;

		// Row 1: inner corner -> opening -> inner corner. Seen from in front of a left face, the start node is on the right.
		const TCHAR* StartSideKey = bSelectedWallFaceLeft ? TEXT("distRight") : TEXT("distLeft");
		const TCHAR* EndSideKey = bSelectedWallFaceLeft ? TEXT("distLeft") : TEXT("distRight");
		Frame.AddAlong(Lines, StartSideKey, Frame.Extent.X, Start, 1.f);
		Frame.AddAlong(Lines, TEXT("width"), Start, End, 1.f);
		Frame.AddAlong(Lines, EndSideKey, End, Frame.Extent.Y, 1.f);

		// Row 2: the gap to the nearest neighbouring opening on the same wall.
		float BestGap = TNumericLimits<float>::Max();
		float GapFrom = 0.f;
		float GapTo = 0.f;
		for (int32 i = 0; i < Seg->Openings.Num(); ++i)
		{
			if (i == SelectedOpeningIndex) continue;
			const FWallOpening& Other = Seg->Openings[i];
			const float OStart = Other.DistanceFromStart - Other.Width * 0.5f;
			const float OEnd = Other.DistanceFromStart + Other.Width * 0.5f;
			if (OStart >= End && OStart - End < BestGap) { BestGap = OStart - End; GapFrom = End; GapTo = OStart; }
			else if (OEnd <= Start && Start - OEnd < BestGap) { BestGap = Start - OEnd; GapFrom = OEnd; GapTo = Start; }
		}
		if (BestGap < TNumericLimits<float>::Max())
		{
			Frame.AddAlong(Lines, TEXT("distNeighbor"), GapFrom, GapTo, 2.f);
		}

		// Along the face: the opening height beside its end jamb, the sill height beside its start jamb. Each stays on the visible
		// face, clear of its inner corners; without room it stands inside the opening.
		auto BesideJamb = [&Frame](float Jamb, float OutSign)
		{
			const float Out = Jamb + OutSign * JambOffset;
			return (Out >= Frame.Extent.X + CornerClearance && Out <= Frame.Extent.Y - CornerClearance) ? Out : Jamb - OutSign * JambOffset;
		};
		const int32 FirstVertical = Lines.Num();
		Frame.AddVertical(Lines, TEXT("height"), TEXT("выс. "), End, BesideJamb(End, 1.f), Op.SillHeight, Op.SillHeight + Op.Height);
		Frame.AddVertical(Lines, TEXT("sill"), TEXT("от пола "), Start, BesideJamb(Start, -1.f), 0.f, Op.SillHeight);

		// The top-down plan has no length for heights: both values in one caption, centred on the opening on the other side of the
		// wall (the chain is on this side), level with the wall top so it clears the wall's image in the straight-down view.
		FString PlanCaption;
		for (int32 i = FirstVertical; i < Lines.Num(); ++i)
		{
			PlanCaption += (PlanCaption.IsEmpty() ? TEXT("") : TEXT(" · ")) + Lines[i].Text;
		}
		if (!PlanCaption.IsEmpty())
		{
			FPlannerDimensionLine Caption;
			Caption.Key = TEXT("heightsPlan");
			Caption.Text = PlanCaption;
			Caption.Value = 0.f; // a caption, not a length
			Caption.Start = Caption.End = Frame.At(Op.DistanceFromStart, -(Frame.Face + 5.f), Seg->Height);
			Caption.StartRef = Caption.EndRef = Frame.At(Op.DistanceFromStart, 0.f, Seg->Height);
			Caption.bPlanOnly = true;
			Lines.Add(Caption);
		}
		break;
	}

	case EPlannerSelectionKind::Object:
	case EPlannerSelectionKind::CabinetSet:
	{
		// From each side of the footprint to the nearest wall face in front of it, level with the object's top (in the straight-down
		// plan that runs from its visible outline).
		const AActor* Actor = (GetSelectionKind() == EPlannerSelectionKind::Object)
			? static_cast<const AActor*>(FindPlacedObjectActor(SelectedObjectID)) : static_cast<const AActor*>(FindCabinetSetActor(SelectedCabinetSetID));
		FVector2D Center, AxisX, HalfSize;
		float TopZ = 0.f;
		if (!GetActorFootprint(Actor, Center, AxisX, HalfSize, TopZ)) break;
		TArray<TArray<FVector2D>> WallOutlines;
		for (const FPlannerWallFaceInput& In : Inputs)
		{
			WallOutlines.Add({ In.StartCorner[0], In.EndCorner[0], In.EndCorner[1], In.StartCorner[1] });
		}
		const float Z = FMath::Clamp(TopZ, ChainZ, 270.f);
		for (const PlannerDimensions::FFootprintGap& Gap : PlannerDimensions::FootprintGapsToWalls(Center, AxisX, HalfSize, WallOutlines, MinObjectGap, MaxObjectGap))
		{
			static const TCHAR* const SideKeys[] = { TEXT("gapFront"), TEXT("gapBack"), TEXT("gapRight"), TEXT("gapLeft") };
			FPlannerDimensionLine Line;
			Line.Key = SideKeys[(int32)Gap.Side];
			Line.Value = Gap.Distance;
			Line.Text = PlannerJsonKeys::FormatMeters(Gap.Distance);
			Line.Start = Line.StartRef = FVector(Gap.From.X, Gap.From.Y, Z);
			Line.End = Line.EndRef = FVector(Gap.To.X, Gap.To.Y, Z);
			Lines.Add(Line);
		}
		break;
	}

	default:
		break;
	}
	return Lines;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Selection
// ═══════════════════════════════════════════════════════════════════════════════

EPlannerSelectionKind ARoomPlannerManager::GetSelectionKind() const
{
	if (SelectedSegmentID != -1)
	{
		return (SelectedOpeningIndex != -1) ? EPlannerSelectionKind::Opening : EPlannerSelectionKind::Wall;
	}
	if (SelectedRoomID != -1)
	{
		return (SelectedRoomSurface == EPlannerSelectionKind::Ceiling || SelectedRoomSurface == EPlannerSelectionKind::Baseboard)
			? SelectedRoomSurface : EPlannerSelectionKind::Floor;
	}
	if (!SelectedObjectID.IsEmpty()) return EPlannerSelectionKind::Object;
	if (!SelectedCabinetSetID.IsEmpty()) return EPlannerSelectionKind::CabinetSet;
	return EPlannerSelectionKind::None;
}

void ARoomPlannerManager::ClearAllSelection()
{
	const bool bHadFloor = (SelectedRoomID != -1);
	const bool bHadObject = !SelectedObjectID.IsEmpty() || !SelectedCabinetSetID.IsEmpty();

	SelectedRoomID = -1;
	SelectedObjectID.Empty();
	SelectedCabinetSetID.Empty();

	ClearWallSelection(); // broadcasts OnWallSelected(-1) + OnSelectionChanged
	UpdateSelectionVisuals();

	if (bHadFloor) OnFloorSelected.Broadcast(-1, 0.f);
	if (bHadObject) OnPlannerObjectSelected.Broadcast(FString(), EPlannerSelectionKind::None);
}

int32 ARoomPlannerManager::FindRoomAtWorldPos(const FVector& WorldPos) const
{
	// The smallest room around the point: a room standing inside a larger one (walls not touching it) is found as itself.
	const FVector2D P(WorldPos.X, WorldPos.Y);
	int32 Best = -1;
	double BestArea = TNumericLimits<double>::Max();
	for (const auto& Pair : Rooms)
	{
		const TArray<FVector2D>& Poly = Pair.Value.FloorPolygon;
		if (Poly.Num() < 3 || !PlannerFinishLayout::IsPointInPolygon(P, Poly)) continue;
		const double Area = FMath::Abs(PlannerFinishLayout::SignedArea(Poly));
		if (Area < BestArea)
		{
			BestArea = Area;
			Best = Pair.Key;
		}
	}
	return Best;
}

int32 ARoomPlannerManager::SelectFloorAtWorldPos(const FVector& WorldPos)
{
	const int32 RoomID = FindRoomAtWorldPos(WorldPos);

	bSelectionHighlightSuppressed = false;
	SelectedSegmentID = -1;
	SelectedOpeningIndex = -1;
	SelectedObjectID.Empty();
	SelectedCabinetSetID.Empty();
	SelectedRoomID = RoomID;
	SelectedRoomSurface = EPlannerSelectionKind::Floor;

	UpdateSelectionVisuals();
	OnWallSelected.Broadcast(-1, 0.f);
	OnFloorSelected.Broadcast(RoomID, (RoomID != -1 && Rooms.Contains(RoomID)) ? Rooms[RoomID].AreaM2 : 0.f);
	NotifySelectionChanged();
	return RoomID;
}

int32 ARoomPlannerManager::SelectRoomSurfaceAtWorldPos(const FVector& WorldPos, EPlannerSelectionKind Surface)
{
	if (Surface != EPlannerSelectionKind::Ceiling && Surface != EPlannerSelectionKind::Baseboard)
	{
		return SelectFloorAtWorldPos(WorldPos);
	}
	const int32 RoomID = FindRoomAtWorldPos(WorldPos);
	if (RoomID == -1)
	{
		return -1;
	}

	const bool bHadFloor = (SelectedRoomID != -1 && SelectedRoomSurface == EPlannerSelectionKind::Floor);
	bSelectionHighlightSuppressed = false;
	SelectedSegmentID = -1;
	SelectedOpeningIndex = -1;
	SelectedObjectID.Empty();
	SelectedCabinetSetID.Empty();
	SelectedRoomID = RoomID;
	SelectedRoomSurface = Surface;

	UpdateSelectionVisuals();
	OnWallSelected.Broadcast(-1, 0.f);
	if (bHadFloor) OnFloorSelected.Broadcast(-1, 0.f);
	NotifySelectionChanged();
	return RoomID;
}

bool ARoomPlannerManager::SelectPlacedObject(const FString& InstanceID)
{
	if (!PlacedObjects.Contains(InstanceID)) return false;
	bSelectionHighlightSuppressed = false;
	SelectedSegmentID = -1;
	SelectedOpeningIndex = -1;
	SelectedRoomID = -1;
	SelectedCabinetSetID.Empty();
	SelectedObjectID = InstanceID;
	UpdateSelectionVisuals();
	OnWallSelected.Broadcast(-1, 0.f);
	OnPlannerObjectSelected.Broadcast(InstanceID, EPlannerSelectionKind::Object);
	NotifySelectionChanged();
	return true;
}

bool ARoomPlannerManager::SelectCabinetSet(const FString& InstanceID)
{
	if (!CabinetSets.Contains(InstanceID)) return false;
	bSelectionHighlightSuppressed = false;
	SelectedSegmentID = -1;
	SelectedOpeningIndex = -1;
	SelectedRoomID = -1;
	SelectedObjectID.Empty();
	SelectedCabinetSetID = InstanceID;
	UpdateSelectionVisuals();
	OnWallSelected.Broadcast(-1, 0.f);
	OnPlannerObjectSelected.Broadcast(InstanceID, EPlannerSelectionKind::CabinetSet);
	NotifySelectionChanged();
	return true;
}

EPlannerSelectionKind ARoomPlannerManager::SelectAtWorldPos2D(const FVector& WorldPos)
{
	if (ActiveToolMode != EPlannerToolMode::Select)
	{
		ClearAllSelection();
		return EPlannerSelectionKind::None;
	}

	const FString ObjID = FindPlacedObjectAtWorldPos(WorldPos);
	if (!ObjID.IsEmpty() && SelectPlacedObject(ObjID))
	{
		return EPlannerSelectionKind::Object;
	}

	const FString SetID = FindCabinetSetAtWorldPos(WorldPos);
	if (!SetID.IsEmpty() && SelectCabinetSet(SetID))
	{
		return EPlannerSelectionKind::CabinetSet;
	}

	if (SelectWallAtWorldPos(WorldPos) != -1)
	{
		return (SelectedOpeningIndex != -1) ? EPlannerSelectionKind::Opening : EPlannerSelectionKind::Wall;
	}

	if (SelectFloorAtWorldPos(WorldPos) != -1)
	{
		return EPlannerSelectionKind::Floor;
	}

	ClearAllSelection();
	return EPlannerSelectionKind::None;
}

EPlannerSelectionKind ARoomPlannerManager::SelectSurfaceFromHit(const FHitResult& Hit)
{
	AActor* HitActor = Hit.GetActor();
	UPrimitiveComponent* HitComp = Hit.GetComponent();

	if (AProceduralWallActor* Wall = Cast<AProceduralWallActor>(HitActor))
	{
		int32 SegID = -1;
		for (const auto& Pair : WallActors)
		{
			if (Pair.Value == Wall) { SegID = Pair.Key; break; }
		}
		const FWallSegment* Seg = (SegID != -1) ? WallSegments.Find(SegID) : nullptr;
		if (Seg && Nodes.Contains(Seg->StartNodeID) && Nodes.Contains(Seg->EndNodeID))
		{
			const FVector2D P1 = Nodes[Seg->StartNodeID].Position;
			const FVector2D P2 = Nodes[Seg->EndNodeID].Position;
			const FVector2D Dir = (P2 - P1).GetSafeNormal();
			const float Along = FVector2D::DotProduct(FVector2D(Hit.ImpactPoint.X, Hit.ImpactPoint.Y) - P1, Dir);
			const float Z = Hit.ImpactPoint.Z;

			// Face to finish (REQ-13): a hit on a face lies on that face; on the top, a reveal or trim, the side the view comes from.
			const FVector2D LeftNormal(-Dir.Y, Dir.X);
			const float ImpactSide = FVector2D::DotProduct(FVector2D(Hit.ImpactPoint.X, Hit.ImpactPoint.Y) - P1, LeftNormal);
			const FVector2D TraceFrom(Hit.TraceStart.X, Hit.TraceStart.Y);
			if (FMath::Abs(ImpactSide) >= Seg->Thickness * 0.5f - 0.5f || Hit.TraceStart.Equals(Hit.TraceEnd))
			{
				bSelectedWallFaceLeft = ImpactSide >= 0.f;
			}
			else
			{
				bSelectedWallFaceLeft = FVector2D::DotProduct(TraceFrom - P1, LeftNormal) >= 0.f;
			}

			int32 OpIdx = -1;
			for (int32 i = 0; i < Seg->Openings.Num(); ++i)
			{
				const FWallOpening& Op = Seg->Openings[i];
				if (FMath::Abs(Along - Op.DistanceFromStart) <= Op.Width * 0.5f + 6.f && Z >= Op.SillHeight - 6.f && Z <= Op.SillHeight + Op.Height + 6.f)
				{
					OpIdx = i;
					break;
				}
			}

			bSelectionHighlightSuppressed = false;
			SelectedRoomID = -1;
			SelectedObjectID.Empty();
			SelectedCabinetSetID.Empty();
			SelectedSegmentID = SegID;
			SelectedOpeningIndex = OpIdx;
			UpdateSelectionVisuals();
			OnWallSelected.Broadcast(SegID, GetWallLength(SegID));
			NotifySelectionChanged();
			return (OpIdx != -1) ? EPlannerSelectionKind::Opening : EPlannerSelectionKind::Wall;
		}
	}

	if (HitComp && FloorProceduralMesh && HitComp == static_cast<UPrimitiveComponent*>(FloorProceduralMesh.Get()))
	{
		if (SelectFloorAtWorldPos(Hit.ImpactPoint) != -1)
		{
			return EPlannerSelectionKind::Floor;
		}
	}

	// Ceilings (only while shown: a hidden ceiling keeps its collision) and baseboards (REQ-13).
	if (HitComp && CeilingProceduralMesh && HitComp == static_cast<UPrimitiveComponent*>(CeilingProceduralMesh.Get()) && CeilingProceduralMesh->IsVisible())
	{
		if (SelectRoomSurfaceAtWorldPos(Hit.ImpactPoint, EPlannerSelectionKind::Ceiling) != -1)
		{
			return EPlannerSelectionKind::Ceiling;
		}
	}
	if (HitComp && BaseboardProceduralMesh && HitComp == static_cast<UPrimitiveComponent*>(BaseboardProceduralMesh.Get()))
	{
		if (SelectRoomSurfaceAtWorldPos(Hit.ImpactPoint, EPlannerSelectionKind::Baseboard) != -1)
		{
			return EPlannerSelectionKind::Baseboard;
		}
	}

	if (APlannerPlacedObjectActor* Obj = Cast<APlannerPlacedObjectActor>(HitActor))
	{
		if (SelectPlacedObject(Obj->Data.InstanceID))
		{
			return EPlannerSelectionKind::Object;
		}
	}

	if (AShowroomBooth* Booth = Cast<AShowroomBooth>(HitActor))
	{
		if (!Booth->PlannerInstanceID.IsEmpty() && SelectCabinetSet(Booth->PlannerInstanceID))
		{
			return EPlannerSelectionKind::CabinetSet;
		}
	}

	ClearAllSelection();
	return EPlannerSelectionKind::None;
}

// ═══════════════════════════════════════════════════════════════════════════════
// REQ-13: finishing
// ═══════════════════════════════════════════════════════════════════════════════

UMaterialInterface* ARoomPlannerManager::ResolvePaintBaseMaterial()
{
	if (PaintMaterial) return PaintMaterial;
	if (CachedPaintBaseMaterial) return CachedPaintBaseMaterial;

	CachedPaintBaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RoomPlanner/Materials/M_PlannerPaint.M_PlannerPaint"));
	if (!CachedPaintBaseMaterial)
	{
		CachedPaintBaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ColorCatalog/Materials/M_Change_Color.M_Change_Color"));
	}
	if (!CachedPaintBaseMaterial)
	{
		CachedPaintBaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	if (!CachedPaintBaseMaterial)
	{
		CachedPaintBaseMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	return CachedPaintBaseMaterial;
}

UMaterialInterface* ARoomPlannerManager::ResolveTileBaseMaterial(const FSurfaceFinish& Finish)
{
	// 1. Tile catalog row material
	if (UDataTable* Catalog = ResolveTileCatalog())
	{
		if (!Finish.TileAssetID.IsEmpty())
		{
			if (const FPlannerTileRow* Row = Catalog->FindRow<FPlannerTileRow>(FName(*Finish.TileAssetID), TEXT("ResolveTileBaseMaterial"), false))
			{
				if (UMaterialInterface* RowMat = Row->Material.LoadSynchronous())
				{
					return RowMat;
				}
			}
		}
	}
	// 2. Asset path stored directly in TileAssetID
	if (Finish.TileAssetID.StartsWith(TEXT("/")))
	{
		if (UMaterialInterface* PathMat = LoadObject<UMaterialInterface>(nullptr, *Finish.TileAssetID))
		{
			return PathMat;
		}
	}
	// 3. Generic tile material
	if (TileMaterial) return TileMaterial;
	if (!CachedTileBaseMaterial)
	{
		CachedTileBaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RoomPlanner/Materials/M_PlannerTile.M_PlannerTile"));
	}
	if (CachedTileBaseMaterial) return CachedTileBaseMaterial;
	// 4. Paint fallback (tinted with the tile's average colour)
	return ResolvePaintBaseMaterial();
}

UMaterialInstanceDynamic* ARoomPlannerManager::CreateFinishMaterialInstance(const FSurfaceFinish& Finish, UObject* Outer)
{
	UMaterialInterface* Base = (Finish.Type == ESurfaceFinishType::Tile) ? ResolveTileBaseMaterial(Finish) : ResolvePaintBaseMaterial();
	if (!Base) return nullptr;

	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Outer ? Outer : this);
	if (!MID) return nullptr;

	MID->SetVectorParameterValue(FName("BaseColor"), Finish.Color);
	MID->SetVectorParameterValue(FName("Color"), Finish.Color);
	if (Finish.Type == ESurfaceFinishType::Tile)
	{
		const float TileSize = FMath::Max(1.f, Finish.TileSizeCm);
		MID->SetScalarParameterValue(FName("TilesPerMeter"), 100.f / TileSize); // UVs are metric (1 UV = 1 m)
		MID->SetScalarParameterValue(FName("TileSize"), TileSize / 100.f);
	}
	return MID;
}

FSurfaceFinish ARoomPlannerManager::MakeDefaultPaintFinish(const FLinearColor& Color)
{
	FSurfaceFinish Finish;
	Finish.Type = ESurfaceFinishType::Paint;
	Finish.Color = Color;
	return Finish;
}

UMaterialInterface* ARoomPlannerManager::GetFinishMaterial(const FSurfaceFinish& Finish)
{
	if (!Finish.IsSet()) return nullptr;
	const FString Key = FString::Printf(TEXT("%d|%s|%s|%.4f|%.4f|%.4f|%.4f|%.2f"), (int32)Finish.Type, *Finish.ColorCode, *Finish.TileAssetID,
		Finish.Color.R, Finish.Color.G, Finish.Color.B, Finish.Color.A, Finish.TileSizeCm);
	if (const TObjectPtr<UMaterialInterface>* Found = FinishMaterialCache.Find(Key))
	{
		if (*Found) return Found->Get();
	}
	UMaterialInterface* Material = CreateFinishMaterialInstance(Finish, this);
	FinishMaterialCache.Add(Key, Material);
	return Material;
}

void ARoomPlannerManager::ComputeWallFaceUVFrames()
{
	TArray<int32> SegIDs;
	WallSegments.GetKeys(SegIDs);
	SegIDs.Sort();

	TArray<FPlannerWallFaceInput> Inputs;
	Inputs.Reserve(SegIDs.Num());
	for (int32 SegID : SegIDs)
	{
		FPlannerWallFaceInput In;
		if (MakeWallFaceInput(SegID, In))
		{
			Inputs.Add(In);
		}
	}
	WallFaceUVFrames = PlannerFinishLayout::ComputeWallFaceUVs(Inputs);
}

bool ARoomPlannerManager::MakeWallFaceInput(int32 SegID, FPlannerWallFaceInput& Out) const
{
	const FWallSegment* Seg = WallSegments.Find(SegID);
	const FWallNode* StartNode = Seg ? Nodes.Find(Seg->StartNodeID) : nullptr;
	const FWallNode* EndNode = Seg ? Nodes.Find(Seg->EndNodeID) : nullptr;
	if (!StartNode || !EndNode) return false;

	Out = FPlannerWallFaceInput();
	Out.SegmentID = SegID;
	Out.StartNodeID = Seg->StartNodeID;
	Out.EndNodeID = Seg->EndNodeID;
	Out.Start = StartNode->Position;
	Out.End = EndNode->Position;
	// Same corner points RebuildAllWalls gives the wall mesh.
	const FVector2D Dir = (Out.End - Out.Start).GetSafeNormal();
	const FVector2D Normal(-Dir.Y, Dir.X);
	const float Half = Seg->Thickness * 0.5f;
	Out.StartCorner[0] = Out.Start + Normal * Half;
	Out.StartCorner[1] = Out.Start - Normal * Half;
	Out.EndCorner[0] = Out.End + Normal * Half;
	Out.EndCorner[1] = Out.End - Normal * Half;
	if (const FWallCornerJoint* J = CornerJoints.Find(MakeJointKey(SegID, Seg->StartNodeID)))
	{
		Out.StartCorner[0] = J->Left;
		Out.StartCorner[1] = J->Right;
	}
	if (const FWallCornerJoint* J = CornerJoints.Find(MakeJointKey(SegID, Seg->EndNodeID)))
	{
		Out.EndCorner[0] = J->Left;
		Out.EndCorner[1] = J->Right;
	}
	return true;
}

void ARoomPlannerManager::GetWalkThroughSpans(int32 SegmentID, TArray<FVector2D>& OutSpans) const
{
	OutSpans.Reset();
	const FWallSegment* Seg = WallSegments.Find(SegmentID);
	FPlannerWallFaceInput Wall;
	if (!Seg || !MakeWallFaceInput(SegmentID, Wall)) return;
	// Over the span RebuildWallMesh cuts holes in (where both faces run straight).
	const float Len = FVector2D::Distance(Wall.Start, Wall.End);
	if (Len < 1.f) return;
	const FVector2D Dir = (Wall.End - Wall.Start) / Len;
	auto Along = [&Wall, &Dir](const FVector2D& P) { return (float)FVector2D::DotProduct(P - Wall.Start, Dir); };
	const float LeftLo = FMath::Min(Along(Wall.StartCorner[0]), Along(Wall.EndCorner[0]));
	const float LeftHi = FMath::Max(Along(Wall.StartCorner[0]), Along(Wall.EndCorner[0]));
	const float RightLo = FMath::Min(Along(Wall.StartCorner[1]), Along(Wall.EndCorner[1]));
	const float RightHi = FMath::Max(Along(Wall.StartCorner[1]), Along(Wall.EndCorner[1]));
	const float HoleLo = FMath::Clamp(FMath::Max(LeftLo, RightLo), 0.f, Len);
	const float HoleHi = FMath::Clamp(FMath::Min(LeftHi, RightHi), HoleLo, Len);
	for (const FWallOpening& Op : Seg->Openings)
	{
		if (Op.SillHeight >= 11.f) continue; // a window: not walked through
		OutSpans.Add(FVector2D(FMath::Clamp(Op.DistanceFromStart - Op.Width * 0.5f, HoleLo, HoleHi),
			FMath::Clamp(Op.DistanceFromStart + Op.Width * 0.5f, HoleLo, HoleHi)));
	}
}

void ARoomPlannerManager::RebuildBaseboards(UMaterialInterface* DefaultMaterial)
{
	if (!BaseboardProceduralMesh) return;

	// Also reached from RebuildAllWalls's ON_SCOPE_EXIT (a moved opening moves its gap), which a corner drag runs every frame.
	const bool bCookCollision = ShouldCookCollision();
	if (!bCookCollision)
	{
		bLayoutCollisionStale = true; // its own caller may not be one of the two rebuilds that already say so
	}
	BaseboardProceduralMesh->bUseAsyncCooking = bCookCollision;

	TArray<int32> SegIDs;
	WallSegments.GetKeys(SegIDs);
	SegIDs.Sort();

	// Every wall takes part (a face without a room still pairs the faces around a node); only faces that look into a room get one.
	TArray<FPlannerBaseboardWall> BaseboardWalls;
	BaseboardWalls.Reserve(SegIDs.Num());
	for (int32 SegID : SegIDs)
	{
		FPlannerBaseboardWall Wall;
		if (!MakeWallFaceInput(SegID, Wall.Wall)) continue;
		const FWallSegment& Seg = WallSegments[SegID];
		Wall.HalfThickness = Seg.Thickness * 0.5f;

		const FVector2D Dir = (Wall.Wall.End - Wall.Wall.Start).GetSafeNormal();
		const FVector2D Left(-Dir.Y, Dir.X);
		const FVector2D Mid = (Wall.Wall.Start + Wall.Wall.End) * 0.5f;
		for (int32 Face = 0; Face < 2; ++Face)
		{
			// The room a face looks into: the room containing a point just in front of it.
			const FVector2D Probe = Mid + (Face == 0 ? Left : -Left) * (Wall.HalfThickness + 5.f);
			Wall.FaceRoom[Face] = FindRoomAtWorldPos(FVector(Probe.X, Probe.Y, 0.f));
		}

		// Walk-through openings interrupt it (windows keep the baseboard under them).
		GetWalkThroughSpans(SegID, Wall.Cuts);
		BaseboardWalls.Add(Wall);
	}

	TMap<int32, FPlannerMeshBuffers> ByRoom;
	PlannerFinishLayout::BuildWallBaseboards(BaseboardWalls, 1.f, 10.f, 1.5f, ByRoom);

	// One section per room, so finishes and selection address a room's baseboards like its floor and ceiling.
	const FPlannerMeshBuffers NoBaseboard;
	for (const TPair<int32, FRoomData>& Pair : Rooms)
	{
		const FPlannerMeshBuffers* Buffers = ByRoom.Find(Pair.Key);
		if (!Buffers)
		{
			Buffers = &NoBaseboard;
		}
		const int32 Section = Pair.Key - 1;
		BaseboardProceduralMesh->CreateMeshSection(Section, Buffers->Vertices, Buffers->Triangles, Buffers->Normals, Buffers->UVs,
			TArray<FColor>(), Buffers->Tangents, bCookCollision);
		UMaterialInterface* Material = Pair.Value.BaseboardFinish.IsSet() ? GetFinishMaterial(Pair.Value.BaseboardFinish) : nullptr;
		if (!Material)
		{
			Material = DefaultMaterial;
		}
		BaseboardSectionMaterials.Add(Pair.Key, Material);
		BaseboardProceduralMesh->SetMaterial(Section, Material);
	}
}

void ARoomPlannerManager::ReadWallFaceFinishes(const TSharedPtr<FJsonObject>& WallObj, FSurfaceFinish& OutLeft, FSurfaceFinish& OutRight)
{
	OutLeft = FSurfaceFinish();
	OutRight = FSurfaceFinish();
	if (!WallObj.IsValid()) return;

	const TSharedPtr<FJsonObject>* LeftObj = nullptr;
	if (WallObj->TryGetObjectField(TEXT("finish"), LeftObj) && LeftObj)
	{
		OutLeft = FinishFromJson(*LeftObj);
	}
	const TSharedPtr<FJsonObject>* RightObj = nullptr;
	if (WallObj->TryGetObjectField(TEXT("finishRight"), RightObj) && RightObj)
	{
		OutRight = FinishFromJson(*RightObj);
	}
	else
	{
		OutRight = OutLeft; // saved before per-face finishing: one finish covered the whole wall
	}
}

int32 ARoomPlannerManager::FindRoomRecordIndex(const TArray<FFloorFinishRecord>& Records, int32 RoomID) const
{
	// The same assignment RebuildRooms makes (rooms in RoomID order, as they were built).
	TArray<int32> RoomIDs;
	Rooms.GetKeys(RoomIDs);
	RoomIDs.Sort();
	TArray<TArray<FVector2D>> Polygons;
	TArray<FVector2D> Centroids;
	int32 Which = INDEX_NONE;
	for (int32 ID : RoomIDs)
	{
		if (ID == RoomID) Which = Polygons.Num();
		Polygons.Add(Rooms[ID].FloorPolygon);
		Centroids.Add(Rooms[ID].Centroid);
	}
	if (Which == INDEX_NONE) return INDEX_NONE;
	TArray<FVector2D> Anchors;
	for (const FFloorFinishRecord& Rec : Records) Anchors.Add(Rec.Anchor);
	return PlannerFinishLayout::AssignRecordsToRooms(Anchors, Polygons, Centroids, 100.f)[Which];
}

void ARoomPlannerManager::SetRoomFinishRecord(TArray<FFloorFinishRecord>& Records, int32 RoomID, const FSurfaceFinish& Finish)
{
	const FRoomData* Room = Rooms.Find(RoomID);
	if (!Room) return;
	// Every record in this room goes (the one it uses and any left over, e.g. from a neighbour merged into it by removing a wall),
	// so a cleared finish stays cleared and no old one comes back later.
	const int32 Claimed = FindRoomRecordIndex(Records, RoomID);
	for (int32 i = Records.Num() - 1; i >= 0; --i)
	{
		if (i == Claimed || FindRoomAtWorldPos(FVector(Records[i].Anchor.X, Records[i].Anchor.Y, 0.f)) == RoomID)
		{
			Records.RemoveAt(i);
		}
	}
	if (Finish.IsSet())
	{
		FFloorFinishRecord Rec;
		Rec.Anchor = Room->InteriorPoint;
		Rec.Finish = Finish;
		Records.Add(Rec);
	}
}

void ARoomPlannerManager::SpreadLegacyRoomFinishes()
{
	TArray<int32> RoomIDs;
	Rooms.GetKeys(RoomIDs);
	RoomIDs.Sort();
	bool bAdded = false;
	auto Spread = [&](TArray<FFloorFinishRecord>& Records)
	{
		for (const TArray<FVector2D>& Outline : RoomGroupOutlines)
		{
			const FFloorFinishRecord* Source = Records.FindByPredicate([&Outline](const FFloorFinishRecord& Rec) { return PlannerFinishLayout::IsPointInPolygon(Rec.Anchor, Outline); });
			if (!Source) continue;
			const FFloorFinishRecord Copy = *Source;
			for (int32 ID : RoomIDs)
			{
				const FRoomData& Room = Rooms[ID];
				if (!PlannerFinishLayout::IsPointInPolygon(Room.InteriorPoint, Outline) || FindRoomRecordIndex(Records, ID) != INDEX_NONE) continue;
				FFloorFinishRecord Rec = Copy;
				Rec.Anchor = Room.InteriorPoint;
				Records.Add(Rec);
				bAdded = true;
			}
		}
	};
	Spread(FloorFinishes);
	Spread(CeilingFinishes);
	Spread(BaseboardFinishes);
	if (bAdded)
	{
		RebuildRooms();
	}
}

TArray<TSharedPtr<FJsonValue>> ARoomPlannerManager::RoomFinishRecordsToJson(const TArray<FFloorFinishRecord>& Records)
{
	TArray<TSharedPtr<FJsonValue>> Array;
	for (const FFloorFinishRecord& Rec : Records)
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
		PlannerJsonKeys::SetCm(Obj, TEXT("x"), Rec.Anchor.X);
		PlannerJsonKeys::SetCm(Obj, TEXT("y"), Rec.Anchor.Y);
		Obj->SetObjectField(TEXT("finish"), FinishToJson(Rec.Finish));
		Array.Add(MakeShareable(new FJsonValueObject(Obj)));
	}
	return Array;
}

void ARoomPlannerManager::RoomFinishRecordsFromJson(const TSharedPtr<FJsonObject>& Root, const TCHAR* Field, TArray<FFloorFinishRecord>& OutRecords)
{
	OutRecords.Empty();
	const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
	if (!Root.IsValid() || !Root->TryGetArrayField(Field, Array) || !Array) return;
	for (const TSharedPtr<FJsonValue>& Val : *Array)
	{
		const TSharedPtr<FJsonObject> Obj = Val.IsValid() ? Val->AsObject() : nullptr;
		if (!Obj.IsValid()) continue;
		FFloorFinishRecord Rec;
		double X = 0.0, Y = 0.0;
		Obj->TryGetNumberField(TEXT("x"), X);
		Obj->TryGetNumberField(TEXT("y"), Y);
		Rec.Anchor = FVector2D((float)X, (float)Y);
		const TSharedPtr<FJsonObject>* FinishObj = nullptr;
		if (Obj->TryGetObjectField(TEXT("finish"), FinishObj) && FinishObj)
		{
			Rec.Finish = FinishFromJson(*FinishObj);
		}
		if (Rec.Finish.IsSet())
		{
			OutRecords.Add(Rec);
		}
	}
}


UMaterialInterface* ARoomPlannerManager::ResolveFloorMaterialForRoom(const FRoomData& Room)
{
	// Rooms sharing a finish (and every room without one) share one instance: a floor material is never changed per room.
	UMaterialInterface* Mat = GetFinishMaterial(Room.FloorFinish);
	if (!Mat)
	{
		Mat = GetFinishMaterial(MakeDefaultPaintFinish(FLinearColor(0.92f, 0.92f, 0.92f, 1.f)));
	}
	FloorSectionMaterials.Add(Room.RoomID, Mat);
	return Mat;
}

void ARoomPlannerManager::ApplyWallFinishMaterials()
{
	for (auto& Pair : WallActors)
	{
		const FWallSegment* Seg = WallSegments.Find(Pair.Key);
		if (Pair.Value && Seg)
		{
			ApplyWallFinishToActor(Pair.Value, *Seg);
		}
	}
}

void ARoomPlannerManager::ApplyWallFinishToActor(AProceduralWallActor* Actor, const FWallSegment& Seg)
{
	if (!Actor) return;
	for (int32 Face = 0; Face < 2; ++Face)
	{
		const bool bLeft = (Face == 0);
		const FSurfaceFinish& Finish = Seg.GetFaceFinish(bLeft);
		const FSurfaceFinish& Applied = bLeft ? Actor->AppliedFinish : Actor->AppliedFinishRight;
		const bool bHasMaterial = bLeft ? Actor->FinishMaterial != nullptr : Actor->FinishMaterialRight != nullptr;
		if (Finish == Applied && (!Finish.IsSet() || bHasMaterial))
		{
			continue;
		}
		UMaterialInterface* Material = nullptr;
		if (Finish.IsSet())
		{
			// Both faces with one finish share one instance.
			Material = (!bLeft && Finish == Actor->AppliedFinish && Actor->FinishMaterial) ? Actor->FinishMaterial.Get()
				: CreateFinishMaterialInstance(Finish, Actor);
		}
		Actor->SetFaceFinish(bLeft, Finish, Material);
	}
}

bool ARoomPlannerManager::SetWallFinish(int32 SegmentID, const FSurfaceFinish& Finish)
{
	FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg) return false;
	Seg->Finish = Finish;
	Seg->FinishRight = Finish;
	ApplyWallFinishMaterials();
	CommitStateAfterMutation();
	UpdateSelectionVisuals();
	return true;
}

bool ARoomPlannerManager::SetWallFaceFinish(int32 SegmentID, bool bLeftFace, const FSurfaceFinish& Finish)
{
	FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg) return false;
	Seg->GetFaceFinish(bLeftFace) = Finish;
	ApplyWallFinishMaterials();
	CommitStateAfterMutation();
	UpdateSelectionVisuals();
	return true;
}

bool ARoomPlannerManager::SetRoomSurfaceFinish(int32 RoomID, EPlannerSelectionKind Surface, const FSurfaceFinish& Finish)
{
	FRoomData* Room = Rooms.Find(RoomID);
	if (!Room) return false;
	const bool bCeiling = (Surface == EPlannerSelectionKind::Ceiling);
	if (!bCeiling && Surface != EPlannerSelectionKind::Baseboard) return false;

	SetRoomFinishRecord(bCeiling ? CeilingFinishes : BaseboardFinishes, RoomID, Finish);
	(bCeiling ? Room->CeilingFinish : Room->BaseboardFinish) = Finish;

	// Swap the section material in place (the geometry does not change).
	UProceduralMeshComponent* Mesh = bCeiling ? CeilingProceduralMesh.Get() : BaseboardProceduralMesh.Get();
	TMap<int32, TObjectPtr<UMaterialInterface>>& SectionMaterials = bCeiling ? CeilingSectionMaterials : BaseboardSectionMaterials;
	UMaterialInterface* Material = GetFinishMaterial(Finish);
	if (!Material)
	{
		const FLinearColor Default = bCeiling ? FLinearColor(0.95f, 0.95f, 0.95f, 1.f) : FLinearColor(0.4f, 0.3f, 0.2f, 1.f);
		FSurfaceFinish DefaultPaint;
		DefaultPaint.Type = ESurfaceFinishType::Paint;
		DefaultPaint.Color = Default;
		Material = GetFinishMaterial(DefaultPaint);
	}
	SectionMaterials.Add(RoomID, Material);
	if (Mesh && Material && RoomID - 1 < Mesh->GetNumSections())
	{
		Mesh->SetMaterial(RoomID - 1, Material);
	}

	CommitStateAfterMutation();
	UpdateSelectionVisuals();
	return true;
}

bool ARoomPlannerManager::SetCeilingFinish(int32 RoomID, const FSurfaceFinish& Finish)
{
	return SetRoomSurfaceFinish(RoomID, EPlannerSelectionKind::Ceiling, Finish);
}

bool ARoomPlannerManager::SetBaseboardFinish(int32 RoomID, const FSurfaceFinish& Finish)
{
	return SetRoomSurfaceFinish(RoomID, EPlannerSelectionKind::Baseboard, Finish);
}

bool ARoomPlannerManager::SetOpeningTrimFinish(int32 SegmentID, int32 OpeningIndex, const FSurfaceFinish& Finish)
{
	FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg || !Seg->Openings.IsValidIndex(OpeningIndex)) return false;
	FWallOpening& Op = Seg->Openings[OpeningIndex];
	if (Op.TrimFinish == Finish) return true;

	Op.TrimFinish = Finish;
	RebuildAllWalls();
	CommitStateAfterMutation();
	UpdateSelectionVisuals();
	return true;
}

bool ARoomPlannerManager::SetFloorFinish(int32 RoomID, const FSurfaceFinish& Finish)
{
	FRoomData* Room = Rooms.Find(RoomID);
	if (!Room) return false;

	// This room's own record only: a neighbouring room's finish is never taken over or changed.
	SetRoomFinishRecord(FloorFinishes, RoomID, Finish);

	Room->FloorFinish = Finish;
	ResolveFloorMaterialForRoom(*Room);
	CommitStateAfterMutation();
	UpdateSelectionVisuals();
	return true;
}

bool ARoomPlannerManager::GetWallFinish(int32 SegmentID, FSurfaceFinish& OutFinish) const
{
	if (const FWallSegment* Seg = WallSegments.Find(SegmentID))
	{
		OutFinish = Seg->Finish;
		return true;
	}
	return false;
}

bool ARoomPlannerManager::GetFloorFinish(int32 RoomID, FSurfaceFinish& OutFinish) const
{
	if (const FRoomData* Room = Rooms.Find(RoomID))
	{
		OutFinish = Room->FloorFinish;
		return true;
	}
	return false;
}

bool ARoomPlannerManager::GetWallFaceFinish(int32 SegmentID, bool bLeftFace, FSurfaceFinish& OutFinish) const
{
	if (const FWallSegment* Seg = WallSegments.Find(SegmentID))
	{
		OutFinish = Seg->GetFaceFinish(bLeftFace);
		return true;
	}
	return false;
}

bool ARoomPlannerManager::GetCeilingFinish(int32 RoomID, FSurfaceFinish& OutFinish) const
{
	if (const FRoomData* Room = Rooms.Find(RoomID))
	{
		OutFinish = Room->CeilingFinish;
		return true;
	}
	return false;
}

bool ARoomPlannerManager::GetBaseboardFinish(int32 RoomID, FSurfaceFinish& OutFinish) const
{
	if (const FRoomData* Room = Rooms.Find(RoomID))
	{
		OutFinish = Room->BaseboardFinish;
		return true;
	}
	return false;
}

bool ARoomPlannerManager::GetOpeningTrimFinish(int32 SegmentID, int32 OpeningIndex, FSurfaceFinish& OutFinish) const
{
	const FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg || !Seg->Openings.IsValidIndex(OpeningIndex)) return false;
	OutFinish = Seg->Openings[OpeningIndex].TrimFinish;
	return true;
}

bool ARoomPlannerManager::IsSelectedWallFaceInterior() const
{
	const FWallSegment* Seg = WallSegments.Find(SelectedSegmentID);
	if (!Seg || !Nodes.Contains(Seg->StartNodeID) || !Nodes.Contains(Seg->EndNodeID)) return false;
	// A wall between two rooms has a room on both faces: probe just in front of the selected face.
	const FVector2D P1 = Nodes[Seg->StartNodeID].Position;
	const FVector2D P2 = Nodes[Seg->EndNodeID].Position;
	const FVector2D Dir = (P2 - P1).GetSafeNormal();
	const FVector2D FaceNormal = FVector2D(-Dir.Y, Dir.X) * (bSelectedWallFaceLeft ? 1.f : -1.f);
	const FVector2D Probe = (P1 + P2) * 0.5f + FaceNormal * (Seg->Thickness * 0.5f + 5.f);
	return FindRoomAtWorldPos(FVector(Probe.X, Probe.Y, 0.f)) != -1;
}

bool ARoomPlannerManager::GetSelectedSurfaceFinish(FSurfaceFinish& OutFinish) const
{
	switch (GetSelectionKind())
	{
	case EPlannerSelectionKind::Wall:      return GetWallFaceFinish(SelectedSegmentID, bSelectedWallFaceLeft, OutFinish);
	case EPlannerSelectionKind::Opening:   return GetOpeningTrimFinish(SelectedSegmentID, SelectedOpeningIndex, OutFinish);
	case EPlannerSelectionKind::Floor:     return GetFloorFinish(SelectedRoomID, OutFinish);
	case EPlannerSelectionKind::Ceiling:   return GetCeilingFinish(SelectedRoomID, OutFinish);
	case EPlannerSelectionKind::Baseboard: return GetBaseboardFinish(SelectedRoomID, OutFinish);
	case EPlannerSelectionKind::Object:
		if (const FPlacedFurnitureData* D = PlacedObjects.Find(SelectedObjectID)) { OutFinish = D->Finish; return true; }
		return false;
	default: return false;
	}
}

bool ARoomPlannerManager::CanApplyFinishToSelection() const
{
	const EPlannerSelectionKind Kind = GetSelectionKind();
	return Kind == EPlannerSelectionKind::Wall || Kind == EPlannerSelectionKind::Floor || Kind == EPlannerSelectionKind::Object
		|| Kind == EPlannerSelectionKind::Opening || Kind == EPlannerSelectionKind::Ceiling || Kind == EPlannerSelectionKind::Baseboard;
}

FSurfaceFinish ARoomPlannerManager::MakePaintFinish(const FString& ColorCode, FLinearColor Color)
{
	FSurfaceFinish F;
	F.Type = ESurfaceFinishType::Paint;
	F.ColorCode = ColorCode;
	F.Color = Color;
	return F;
}

UDataTable* ARoomPlannerManager::ResolveTileCatalog() const
{
	if (TileCatalog) return TileCatalog;
	return LoadObject<UDataTable>(nullptr, TEXT("/Game/DT/DT_PlannerTiles.DT_PlannerTiles"));
}

UDataTable* ARoomPlannerManager::ResolveObjectCatalog() const
{
	if (ObjectCatalog) return ObjectCatalog;
	return LoadObject<UDataTable>(nullptr, TEXT("/Game/DT/DT_PlannerObjects.DT_PlannerObjects"));
}

UDataTable* ARoomPlannerManager::ResolveCabinetSetCatalog() const
{
	if (CabinetSetCatalog) return CabinetSetCatalog;
	return LoadObject<UDataTable>(nullptr, TEXT("/Game/DT/DT_FurnitureCatalog.DT_FurnitureCatalog"));
}

const FCabinetSetLayoutRow* ARoomPlannerManager::FindCabinetSetLayoutRow(UWorld* World, FName ProductID)
{
	if (ProductID.IsNone()) return nullptr;

	UDataTable* Table = nullptr;
	if (World)
	{
		for (TActorIterator<ARoomPlannerManager> It(World); It; ++It)
		{
			if (It->CabinetSetLayoutCatalog) { Table = It->CabinetSetLayoutCatalog; }
			break;
		}
	}
	if (!Table)
	{
		Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/DT/DT_CabinetSetLayouts.DT_CabinetSetLayouts"));
	}
	if (!Table) return nullptr;
	return Table->FindRow<FCabinetSetLayoutRow>(ProductID, TEXT("FindCabinetSetLayoutRow"), false);
}

FString ARoomPlannerManager::ExportCabinetSetLayoutsCSV()
{
#if WITH_EDITOR
	UClass* BoothClass = ResolveCabinetSetActorClass();
	const AShowroomBooth* CDO = BoothClass ? Cast<AShowroomBooth>(BoothClass->GetDefaultObject()) : nullptr;
	if (!CDO)
	{
		UE_LOG(LogTemp, Error, TEXT("[MaxiMallConstructor] ExportCabinetSetLayoutsCSV: booth class defaults not available."));
		return FString();
	}

	// Read the actual relative transforms (and meshes, if assigned) from the booth class defaults.
	FCabinetSetLayoutRow Template;
	auto Capture = [&](FCabinetSetPartData& Part, const UStaticMeshComponent* Comp)
	{
		if (!Comp) return;
		Part.RelativeLocation = Comp->GetRelativeLocation();
		Part.RelativeRotation = Comp->GetRelativeRotation();
		Part.RelativeScale3D = Comp->GetRelativeScale3D();
		if (Comp->GetStaticMesh()) Part.Mesh = Comp->GetStaticMesh();
	};
	Capture(Template.MainCabinet, CDO->MainCabinet);
	Capture(Template.DoorMeshSlot0, CDO->DoorMeshSlot0);
	Capture(Template.DoorMeshSlot1, CDO->DoorMeshSlot1);
	Capture(Template.CountertopMesh, CDO->CountertopMesh);
	Capture(Template.SinkMesh, CDO->SinkMesh);
	Capture(Template.FaucetMesh, CDO->FaucetMesh);
	Capture(Template.MirrorMesh, CDO->MirrorMesh);
	Capture(Template.ClosetMesh, CDO->ClosetMesh);
	Capture(Template.ClosetDoorMeshSlot0, CDO->ClosetDoorMeshSlot0);
	Capture(Template.ClosetDoorMeshSlot1, CDO->ClosetDoorMeshSlot1);

	UDataTable* Temp = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
	Temp->RowStruct = FCabinetSetLayoutRow::StaticStruct();

	TArray<FName> Products;
	if (UDataTable* Catalog = ResolveCabinetSetCatalog())
	{
		Products = Catalog->GetRowNames();
	}
	if (Products.Num() == 0)
	{
		Products.Add(FName(TEXT("Default")));
	}
	for (const FName& Product : Products)
	{
		Temp->AddRow(Product, Template);
	}

	const FString Csv = Temp->GetTableAsCSV();
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PlannerExports"), TEXT("DT_CabinetSetLayouts.csv"));
	if (FFileHelper::SaveStringToFile(Csv, *Path))
	{
		UE_LOG(LogTemp, Warning, TEXT("[MaxiMallConstructor] Cabinet set layout CSV written: %s (%d rows)"), *Path, Products.Num());
		return Path;
	}
	UE_LOG(LogTemp, Error, TEXT("[MaxiMallConstructor] Could not write %s"), *Path);
	return FString();
#else
	UE_LOG(LogTemp, Warning, TEXT("[MaxiMallConstructor] ExportCabinetSetLayoutsCSV is editor-only."));
	return FString();
#endif
}

#if WITH_EDITOR
static FAutoConsoleCommandWithWorld GPlannerExportCabinetSetLayoutsCmd(
	TEXT("planner.ExportCabinetSetLayoutsCSV"),
	TEXT("Writes Saved/PlannerExports/DT_CabinetSetLayouts.csv from the BP_Booth part transforms (row struct CabinetSetLayoutRow)."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (ARoomPlannerManager* Manager = ARoomPlannerManager::GetOrCreateInstance(World))
		{
			Manager->ExportCabinetSetLayoutsCSV();
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[MaxiMallConstructor] planner.ExportCabinetSetLayoutsCSV: no planner manager in this world (run while playing)."));
		}
	}));
#endif

UClass* ARoomPlannerManager::ResolveCabinetSetActorClass() const
{
	if (CabinetSetActorClass) return CabinetSetActorClass;
	if (UClass* BP = LoadClass<AShowroomBooth>(nullptr, TEXT("/Game/BP_Booth.BP_Booth_C")))
	{
		return BP;
	}
	return AShowroomBooth::StaticClass();
}

bool ARoomPlannerManager::MakeTileFinish(FName TileID, FSurfaceFinish& OutFinish) const
{
	UDataTable* Catalog = ResolveTileCatalog();
	const FPlannerTileRow* Row = Catalog ? Catalog->FindRow<FPlannerTileRow>(TileID, TEXT("MakeTileFinish"), false) : nullptr;
	if (!Row)
	{
		// Allow a raw material path as tile id (no catalog needed).
		const FString IdStr = TileID.ToString();
		if (!IdStr.StartsWith(TEXT("/"))) return false;
		OutFinish = FSurfaceFinish();
		OutFinish.Type = ESurfaceFinishType::Tile;
		OutFinish.TileAssetID = IdStr;
		OutFinish.TileSizeCm = 30.f;
		OutFinish.Color = FLinearColor::White;
		return true;
	}
	OutFinish = FSurfaceFinish();
	OutFinish.Type = ESurfaceFinishType::Tile;
	OutFinish.TileAssetID = TileID.ToString();
	OutFinish.TileSizeCm = Row->TileSizeCm > 0.f ? Row->TileSizeCm : 30.f;
	OutFinish.Color = Row->AverageColor;
	return true;
}

TArray<FPlannerCatalogEntry> ARoomPlannerManager::GetAvailableTiles() const
{
	TArray<FPlannerCatalogEntry> Out;
	UDataTable* Catalog = ResolveTileCatalog();
	if (!Catalog) return Out;
	for (const auto& Pair : Catalog->GetRowMap())
	{
		const FPlannerTileRow* Row = reinterpret_cast<const FPlannerTileRow*>(Pair.Value);
		if (!Row) continue;
		FPlannerCatalogEntry E;
		E.ID = Pair.Key.ToString();
		E.DisplayName = Row->DisplayName.IsEmpty() ? FText::FromName(Pair.Key) : Row->DisplayName;
		E.Thumbnail = Row->Thumbnail;
		E.TileSizeCm = Row->TileSizeCm;
		E.Color = Row->AverageColor;
		Out.Add(E);
	}
	return Out;
}

// ═══════════════════════════════════════════════════════════════════════════════
// REQ-14: finishing areas
// ═══════════════════════════════════════════════════════════════════════════════

float ARoomPlannerManager::GetWallNetAreaM2(int32 SegmentID) const
{
	const FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg || !Nodes.Contains(Seg->StartNodeID) || !Nodes.Contains(Seg->EndNodeID)) return 0.f;
	const float Len = FVector2D::Distance(Nodes[Seg->StartNodeID].Position, Nodes[Seg->EndNodeID].Position);
	float AreaCm2 = Len * Seg->Height;
	for (const FWallOpening& Op : Seg->Openings)
	{
		AreaCm2 -= Op.Width * Op.Height;
	}
	return FMath::Max(0.f, AreaCm2) / 10000.f;
}

TArray<FFinishAreaEntry> ARoomPlannerManager::CalculateFinishAreas() const
{
	TArray<FFinishAreaEntry> Out;
	auto Accumulate = [&Out](const FSurfaceFinish& F, float AreaM2)
	{
		if (!F.IsSet() || AreaM2 <= 0.f) return;
		const FString Key = F.GetKey();
		for (FFinishAreaEntry& E : Out)
		{
			if (E.Type == F.Type && E.FinishKey == Key)
			{
				E.AreaM2 += AreaM2;
				E.SurfaceCount++;
				return;
			}
		}
		FFinishAreaEntry NewE;
		NewE.Type = F.Type;
		NewE.FinishKey = Key;
		NewE.AreaM2 = AreaM2;
		NewE.SurfaceCount = 1;
		Out.Add(NewE);
	};

	for (const auto& Pair : WallSegments)
	{
		// Each face carries its own finish (REQ-13).
		const float FaceAreaM2 = GetWallNetAreaM2(Pair.Key);
		Accumulate(Pair.Value.Finish, FaceAreaM2);
		Accumulate(Pair.Value.FinishRight, FaceAreaM2);
	}
	for (const auto& Pair : Rooms)
	{
		Accumulate(Pair.Value.FloorFinish, Pair.Value.AreaM2);
	}
	Out.Sort([](const FFinishAreaEntry& A, const FFinishAreaEntry& B)
	{
		if (A.Type != B.Type) return (uint8)A.Type < (uint8)B.Type;
		return A.AreaM2 > B.AreaM2;
	});
	return Out;
}

float ARoomPlannerManager::GetTotalFinishAreaM2(ESurfaceFinishType Type) const
{
	float Total = 0.f;
	for (const FFinishAreaEntry& E : CalculateFinishAreas())
	{
		if (E.Type == Type) Total += E.AreaM2;
	}
	return Total;
}

FString ARoomPlannerManager::GetFinishAreaSummaryText() const
{
	const TArray<FFinishAreaEntry> Entries = CalculateFinishAreas();
	if (Entries.Num() == 0)
	{
		return TEXT("Отделка не назначена");
	}
	FString Text;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const ESurfaceFinishType Type = (Pass == 0) ? ESurfaceFinishType::Paint : ESurfaceFinishType::Tile;
		float Total = 0.f;
		FString Lines;
		for (const FFinishAreaEntry& E : Entries)
		{
			if (E.Type != Type) continue;
			Total += E.AreaM2;
			Lines += FString::Printf(TEXT("  %s: %.2f м² (%d)\n"), *E.FinishKey, E.AreaM2, E.SurfaceCount);
		}
		if (Total > 0.f)
		{
			Text += FString::Printf(TEXT("%s: %.2f м²\n%s"), (Type == ESurfaceFinishType::Paint) ? TEXT("Краска") : TEXT("Плитка"), Total, *Lines);
		}
	}
	return Text.TrimEnd();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Placement (REQ-17 / REQ-18)
// ═══════════════════════════════════════════════════════════════════════════════

void ARoomPlannerManager::BeginPlaceObject(const FString& AssetID)
{
	PendingPlacementKind = EPlannerPlacementKind::Object;
	PendingPlacementAssetID = AssetID;
	PendingPlacementYawDeg = 0.f;
	ActiveToolMode = EPlannerToolMode::PlaceFurniture;
	ClearAllSelection();
	RefreshNodeHandles();
}

void ARoomPlannerManager::BeginPlaceCabinetSet(FName ProductID)
{
	PendingPlacementKind = EPlannerPlacementKind::CabinetSet;
	PendingPlacementAssetID = ProductID.ToString();
	PendingPlacementYawDeg = 0.f;
	ActiveToolMode = EPlannerToolMode::PlaceFurniture;
	ClearAllSelection();
	RefreshNodeHandles();
}

void ARoomPlannerManager::CancelPendingPlacement()
{
	PendingPlacementKind = EPlannerPlacementKind::None;
	PendingPlacementAssetID.Empty();
	PendingPlacementYawDeg = 0.f;
}

UStaticMesh* ARoomPlannerManager::ResolveObjectMesh(const FString& AssetID) const
{
	if (AssetID.IsEmpty()) return nullptr;
	if (UDataTable* Catalog = ResolveObjectCatalog())
	{
		if (const FPlannerObjectRow* Row = Catalog->FindRow<FPlannerObjectRow>(FName(*AssetID), TEXT("ResolveObjectMesh"), false))
		{
			if (UStaticMesh* Mesh = Row->Mesh.LoadSynchronous())
			{
				return Mesh;
			}
		}
	}
	if (AssetID.StartsWith(TEXT("/")))
	{
		if (UStaticMesh* PathMesh = LoadObject<UStaticMesh>(nullptr, *AssetID))
		{
			return PathMesh;
		}
	}

	// Nothing usable: report precisely what was tried and fall back to a visible placeholder so the
	// placement is never silently invisible.
	FString RowMeshPath = TEXT("<row not found>");
	if (UDataTable* Catalog = ResolveObjectCatalog())
	{
		if (const FPlannerObjectRow* Row = Catalog->FindRow<FPlannerObjectRow>(FName(*AssetID), TEXT("ResolveObjectMesh"), false))
		{
			RowMeshPath = Row->Mesh.IsNull() ? TEXT("<Mesh field is None>") : Row->Mesh.ToString();
		}
	}
	UE_LOG(LogTemp, Error, TEXT("[PlannerDrop] No static mesh for object '%s' (row Mesh = %s). Using placeholder cube — fill the Mesh column in DT_PlannerObjects."), *AssetID, *RowMeshPath);
	return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
}

TArray<FPlannerMaterialOverride> ARoomPlannerManager::ResolveObjectMaterialOverrides(const FString& AssetID) const
{
	if (AssetID.IsEmpty()) return {};
	if (UDataTable* Catalog = ResolveObjectCatalog())
	{
		if (const FPlannerObjectRow* Row = Catalog->FindRow<FPlannerObjectRow>(FName(*AssetID), TEXT("ResolveObjectMaterialOverrides"), false))
		{
			return Row->MaterialOverrides;
		}
	}
	return {};
}

float ARoomPlannerManager::ResolveObjectFrontYawDeg(const FString& AssetID) const
{
	if (AssetID.IsEmpty()) return 0.f;
	if (UDataTable* Catalog = ResolveObjectCatalog())
	{
		if (const FPlannerObjectRow* Row = Catalog->FindRow<FPlannerObjectRow>(FName(*AssetID), TEXT("ResolveObjectFrontYawDeg"), false))
		{
			return Row->FrontYawDeg;
		}
	}
	return 0.f;
}

namespace
{
	FPlannerCatalogEntry MakeObjectCatalogEntry(const FName& RowName, const FPlannerObjectRow& Row)
	{
		FPlannerCatalogEntry E;
		E.ID = RowName.ToString();
		E.DisplayName = Row.DisplayName.IsEmpty() ? FText::FromName(RowName) : Row.DisplayName;
		E.Thumbnail = Row.Thumbnail;
		E.Category = Row.Category;
		return E;
	}
}

TArray<FPlannerCatalogEntry> ARoomPlannerManager::GetAvailableObjects() const
{
	TArray<FPlannerCatalogEntry> Out;
	UDataTable* Catalog = ResolveObjectCatalog();
	if (!Catalog) return Out;
	for (const auto& Pair : Catalog->GetRowMap())
	{
		const FPlannerObjectRow* Row = reinterpret_cast<const FPlannerObjectRow*>(Pair.Value);
		if (!Row || Row->bHideInCatalog) continue; // hidden: not offered, but still resolved for what is placed (FindObjectCatalogEntry)
		Out.Add(MakeObjectCatalogEntry(Pair.Key, *Row));
	}
	return Out;
}

bool ARoomPlannerManager::FindObjectCatalogEntry(const FString& AssetID, FPlannerCatalogEntry& OutEntry) const
{
	UDataTable* Catalog = AssetID.IsEmpty() ? nullptr : ResolveObjectCatalog();
	const FPlannerObjectRow* Row = Catalog ? Catalog->FindRow<FPlannerObjectRow>(FName(*AssetID), TEXT("FindObjectCatalogEntry"), false) : nullptr;
	if (!Row) return false;
	OutEntry = MakeObjectCatalogEntry(FName(*AssetID), *Row);
	return true;
}

FString ARoomPlannerManager::AddPlacedObject(const FString& AssetID, const FVector& Location, const FRotator& Rotation, const FVector& Scale)
{
	if (AssetID.IsEmpty()) return FString();

	FPlacedFurnitureData D;
	D.InstanceID = PlannerJsonKeys::NewInstanceID();
	D.AssetID = AssetID;
	D.Location = Location;
	D.Rotation = Rotation;
	D.Scale = Scale.IsNearlyZero() ? FVector::OneVector : Scale;
	if (UDataTable* Catalog = ResolveObjectCatalog())
	{
		if (const FPlannerObjectRow* Row = Catalog->FindRow<FPlannerObjectRow>(FName(*AssetID), TEXT("AddPlacedObject"), false))
		{
			if (Scale.IsNearlyZero() || Scale.Equals(FVector::OneVector))
			{
				D.Scale = Row->DefaultScale.IsNearlyZero() ? FVector::OneVector : Row->DefaultScale;
			}
		}
	}

	PlacedObjects.Add(D.InstanceID, D);
	ApplyPlacedObjectActor(D);
	CommitStateAfterMutation();
	return D.InstanceID;
}

bool ARoomPlannerManager::MovePlacedObject(const FString& InstanceID, const FVector& Location, const FRotator& Rotation, const FVector& Scale)
{
	FPlacedFurnitureData* D = PlacedObjects.Find(InstanceID);
	if (!D) return false;
	if (D->WallAttachment.IsAttached() && SlideAttachmentTo(D->WallAttachment, Location))
	{
		// Attached items slide along their wall; rotation stays fixed by the wall.
		ComputePlacedObjectWallTransform(D->AssetID, D->WallAttachment, D->Location, D->Rotation);
	}
	else
	{
		D->Location = Location;
		D->Rotation = Rotation;
	}
	if (!Scale.IsNearlyZero()) D->Scale = Scale;
	ApplyPlacedObjectActor(*D);
	CommitStateAfterMutation();
	return true;
}

bool ARoomPlannerManager::RemovePlacedObject(const FString& InstanceID)
{
	if (!PlacedObjects.Remove(InstanceID)) return false;
	if (TObjectPtr<APlannerPlacedObjectActor>* ActorPtr = PlacedObjectActors.Find(InstanceID))
	{
		if (*ActorPtr) (*ActorPtr)->Destroy();
		PlacedObjectActors.Remove(InstanceID);
	}
	if (SelectedObjectID == InstanceID)
	{
		SelectedObjectID.Empty();
		OnPlannerObjectSelected.Broadcast(FString(), EPlannerSelectionKind::None);
		NotifySelectionChanged();
	}
	CommitStateAfterMutation();
	return true;
}

bool ARoomPlannerManager::SetPlacedObjectFinish(const FString& InstanceID, const FSurfaceFinish& Finish)
{
	FPlacedFurnitureData* D = PlacedObjects.Find(InstanceID);
	if (!D) return false;
	D->Finish = Finish;
	ApplyPlacedObjectActor(*D);
	CommitStateAfterMutation();
	return true;
}

TArray<FPlacedFurnitureData> ARoomPlannerManager::GetPlacedObjects() const
{
	TArray<FPlacedFurnitureData> Out;
	PlacedObjects.GenerateValueArray(Out);
	return Out;
}

bool ARoomPlannerManager::GetPlacedObject(const FString& InstanceID, FPlacedFurnitureData& OutData) const
{
	if (const FPlacedFurnitureData* D = PlacedObjects.Find(InstanceID))
	{
		OutData = *D;
		return true;
	}
	return false;
}

APlannerPlacedObjectActor* ARoomPlannerManager::FindPlacedObjectActor(const FString& InstanceID) const
{
	if (const TObjectPtr<APlannerPlacedObjectActor>* Found = PlacedObjectActors.Find(InstanceID))
	{
		return Found->Get();
	}
	return nullptr;
}

FString ARoomPlannerManager::FindPlacedObjectAtWorldPos(const FVector& WorldPos) const
{
	// Around each object's Location: its actor origin, which is its footprint centre (the actor re-centres its mesh on it).
	const FVector2D P(WorldPos.X, WorldPos.Y);
	FString Best;
	float BestDist = TNumericLimits<float>::Max();
	for (const auto& Pair : PlacedObjects)
	{
		float Radius = 50.f;
		if (APlannerPlacedObjectActor* A = FindPlacedObjectActor(Pair.Key)) Radius = A->GetFootprintRadiusCm();
		const float D = FVector2D::Distance(P, FVector2D(Pair.Value.Location.X, Pair.Value.Location.Y));
		if (D <= Radius && D < BestDist)
		{
			BestDist = D;
			Best = Pair.Key;
		}
	}
	return Best;
}

void ARoomPlannerManager::MovePlacedObjectLocal(const FString& InstanceID, const FVector& Location, const FRotator& Rotation)
{
	FPlacedFurnitureData* D = PlacedObjects.Find(InstanceID);
	if (!D) return;
	if (D->WallAttachment.IsAttached() && SlideAttachmentTo(D->WallAttachment, Location))
	{
		ComputePlacedObjectWallTransform(D->AssetID, D->WallAttachment, D->Location, D->Rotation);
	}
	else
	{
		D->Location = Location;
		D->Rotation = Rotation;
	}
	if (APlannerPlacedObjectActor* A = FindPlacedObjectActor(InstanceID))
	{
		A->SetActorLocationAndRotation(D->Location, D->Rotation);
		A->Data.Location = D->Location;
		A->Data.Rotation = D->Rotation;
	}
}

bool ARoomPlannerManager::ApplyPlacedObjectActor(const FPlacedFurnitureData& Data)
{
	bool bWallStandOffCorrected = false;
	APlannerPlacedObjectActor* Actor = FindPlacedObjectActor(Data.InstanceID);
	if (!Actor && GetWorld())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		UClass* Cls = PlacedObjectActorClass ? PlacedObjectActorClass.Get() : APlannerPlacedObjectActor::StaticClass();
		Actor = GetWorld()->SpawnActor<APlannerPlacedObjectActor>(Cls, Data.Location, Data.Rotation, SpawnParams);
		if (Actor)
		{
			PlacedObjectActors.Add(Data.InstanceID, Actor);
		}
	}
	if (Actor)
	{
		UStaticMesh* Mesh = ResolveObjectMesh(Data.AssetID);
		UMaterialInterface* ColorMat = Data.Finish.IsSet() ? ResolvePaintBaseMaterial() : nullptr;
		Actor->ApplyData(Data, Mesh, ColorMat, ResolveObjectMaterialOverrides(Data.AssetID));
		Actor->SetSelectedHighlight(Data.InstanceID == SelectedObjectID);
		UE_LOG(LogTemp, Warning, TEXT("[PlannerDrop] Object %s (%s) → actor %s, mesh %s, at (%.0f, %.0f, %.0f)%s"),
			*Data.InstanceID, *Data.AssetID, *Actor->GetName(), Mesh ? *Mesh->GetName() : TEXT("NONE"),
			Data.Location.X, Data.Location.Y, Data.Location.Z, Data.WallAttachment.IsAttached() ? TEXT(" [wall]") : TEXT(" [floor]"));

		// On a wall, the stand-off was measured with the mesh and scale of that moment. The row's mesh may have been swapped since the
		// layout was saved, or the scale changed: keep the back on the face on every apply. (Data may be the stored entry itself, which
		// this rewrites; nothing reads Data after it.)
		if (Data.WallAttachment.IsAttached())
		{
			bWallStandOffCorrected = RemeasurePlacedObjectWallDepth(Data.InstanceID);
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[PlannerDrop] Failed to spawn actor for object %s (%s)."), *Data.InstanceID, *Data.AssetID);
	}
	return bWallStandOffCorrected;
}

bool ARoomPlannerManager::RebuildPlacedObjectActors()
{
	// Remove actors whose data is gone
	TArray<FString> ToRemove;
	for (const auto& Pair : PlacedObjectActors)
	{
		if (!PlacedObjects.Contains(Pair.Key))
		{
			if (Pair.Value) Pair.Value->Destroy();
			ToRemove.Add(Pair.Key);
		}
	}
	for (const FString& Key : ToRemove) PlacedObjectActors.Remove(Key);

	// Spawn / update the rest
	bool bWallStandOffCorrected = false;
	for (const auto& Pair : PlacedObjects)
	{
		bWallStandOffCorrected |= ApplyPlacedObjectActor(Pair.Value);
	}
	return bWallStandOffCorrected;
}

TArray<FPlannerCatalogEntry> ARoomPlannerManager::GetAvailableCabinetSets() const
{
	TArray<FPlannerCatalogEntry> Out;
	UDataTable* Catalog = ResolveCabinetSetCatalog();
	if (!Catalog) return Out;
	for (const auto& Pair : Catalog->GetRowMap())
	{
		const FFurnitureProductRow* Row = reinterpret_cast<const FFurnitureProductRow*>(Pair.Value);
		if (!Row) continue;
		if (!Row->ShowInConstructor) continue; // DT_FurnitureCatalog → "Show In Constructor" unticked: hidden from the catalog
		FPlannerCatalogEntry E;
		E.ID = Pair.Key.ToString();
		E.DisplayName = FText::FromName(Pair.Key);
		if (Row->CabinetOptions.Colors.Num() > 0)
		{
			const FFurnitureColorOption& First = Row->CabinetOptions.Colors[0];
			if (!First.ProductName.IsEmpty()) E.DisplayName = First.ProductName;
			E.Thumbnail = First.Thumbnail;
		}
		// Row-level preview image (DT_FurnitureCatalog → "Constructor Preview Image") wins; otherwise the
		// first colour option's Thumbnail as before; otherwise the card draws its neutral placeholder.
		if (!Row->ConstructorPreviewImage.IsNull())
		{
			E.Thumbnail = Row->ConstructorPreviewImage;
		}
		E.Category = TEXT("CabinetSet");
		Out.Add(E);
	}
	return Out;
}

AShowroomBooth* ARoomPlannerManager::SpawnCabinetSetActor(const FPlacedCabinetSetData& Data)
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority()) return nullptr;

	UClass* Cls = ResolveCabinetSetActorClass();
	const FTransform SpawnTM(Data.Rotation, Data.Location);
	AShowroomBooth* Booth = World->SpawnActorDeferred<AShowroomBooth>(Cls, SpawnTM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Booth) return nullptr;

	Booth->InitialProductID = Data.ProductID;
	Booth->PlannerInstanceID = Data.InstanceID;
	Booth->SetReplicates(true);
	Booth->SetReplicateMovement(true);

	// DT_CabinetSetLayouts: part transforms become the baseline BeginPlay captures (server side; clients apply on OnRep).
	if (const FCabinetSetLayoutRow* Layout = FindCabinetSetLayoutRow(GetWorld(), Data.ProductID))
	{
		Booth->ApplyPlannerLayout(*Layout, false);
	}

	Booth->FinishSpawning(SpawnTM);

	// Reconfiguring the set (cabinet / countertop / mirror size, model) can move its rearmost point off the wall face.
	Booth->OnProductChanged.AddUniqueDynamic(this, &ARoomPlannerManager::HandleCabinetSetProductChanged);

	CabinetSetActorCache.Add(Data.InstanceID, Booth);
	return Booth;
}

AShowroomBooth* ARoomPlannerManager::FindCabinetSetActor(const FString& InstanceID) const
{
	if (InstanceID.IsEmpty()) return nullptr;
	if (const TWeakObjectPtr<AShowroomBooth>* Cached = CabinetSetActorCache.Find(InstanceID))
	{
		if (Cached->IsValid() && (*Cached)->PlannerInstanceID == InstanceID)
		{
			return Cached->Get();
		}
	}
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AShowroomBooth> It(World); It; ++It)
		{
			if (It->PlannerInstanceID == InstanceID)
			{
				CabinetSetActorCache.Add(InstanceID, *It);
				return *It;
			}
		}
	}
	return nullptr;
}

FString ARoomPlannerManager::AddCabinetSet(FName ProductID, const FVector& Location, const FRotator& Rotation)
{
	if (!HasAuthority() || ProductID.IsNone()) return FString();

	FPlacedCabinetSetData D;
	D.InstanceID = PlannerJsonKeys::NewInstanceID();
	D.ProductID = ProductID;
	D.Location = Location;
	D.Rotation = Rotation;

	if (!SpawnCabinetSetActor(D))
	{
		return FString();
	}
	CabinetSets.Add(D.InstanceID, D);
	CommitStateAfterMutation();
	return D.InstanceID;
}

bool ARoomPlannerManager::MoveCabinetSet(const FString& InstanceID, const FVector& Location, const FRotator& Rotation)
{
	FPlacedCabinetSetData* D = CabinetSets.Find(InstanceID);
	if (!D) return false;
	if (D->WallAttachment.IsAttached() && SlideAttachmentTo(D->WallAttachment, Location))
	{
		ComputeCabinetSetTransform(*D, D->Location, D->Rotation);
	}
	else
	{
		D->Location = Location;
		D->Rotation = Rotation;
	}
	if (AShowroomBooth* Booth = FindCabinetSetActor(InstanceID))
	{
		Booth->SetActorLocationAndRotation(D->Location, D->Rotation);
	}
	CommitStateAfterMutation();
	return true;
}

bool ARoomPlannerManager::RemoveCabinetSet(const FString& InstanceID)
{
	if (!CabinetSets.Remove(InstanceID)) return false;
	if (HasAuthority())
	{
		if (AShowroomBooth* Booth = FindCabinetSetActor(InstanceID))
		{
			Booth->Destroy();
		}
	}
	CabinetSetActorCache.Remove(InstanceID);
	if (SelectedCabinetSetID == InstanceID)
	{
		SelectedCabinetSetID.Empty();
		OnPlannerObjectSelected.Broadcast(FString(), EPlannerSelectionKind::None);
		NotifySelectionChanged();
	}
	CommitStateAfterMutation();
	return true;
}

TArray<FPlacedCabinetSetData> ARoomPlannerManager::GetCabinetSets() const
{
	TArray<FPlacedCabinetSetData> Out;
	CabinetSets.GenerateValueArray(Out);
	return Out;
}

bool ARoomPlannerManager::GetCabinetSet(const FString& InstanceID, FPlacedCabinetSetData& OutData) const
{
	if (const FPlacedCabinetSetData* D = CabinetSets.Find(InstanceID))
	{
		OutData = *D;
		return true;
	}
	return false;
}

FString ARoomPlannerManager::FindCabinetSetAtWorldPos(const FVector& WorldPos) const
{
	FString Best;
	float BestDist = TNumericLimits<float>::Max();
	for (const auto& Pair : CabinetSets)
	{
		FVector Origin = Pair.Value.Location;
		FVector Extent(60.f, 60.f, 60.f);
		if (AShowroomBooth* Booth = FindCabinetSetActor(Pair.Key))
		{
			Booth->GetActorBounds(false, Origin, Extent);
		}
		const float DX = FMath::Abs(WorldPos.X - Origin.X);
		const float DY = FMath::Abs(WorldPos.Y - Origin.Y);
		if (DX <= Extent.X + 10.f && DY <= Extent.Y + 10.f)
		{
			const float D = DX + DY;
			if (D < BestDist)
			{
				BestDist = D;
				Best = Pair.Key;
			}
		}
	}
	return Best;
}

void ARoomPlannerManager::MoveCabinetSetLocal(const FString& InstanceID, const FVector& Location, const FRotator& Rotation)
{
	FPlacedCabinetSetData* D = CabinetSets.Find(InstanceID);
	if (!D) return;
	if (D->WallAttachment.IsAttached() && SlideAttachmentTo(D->WallAttachment, Location))
	{
		ComputeCabinetSetTransform(*D, D->Location, D->Rotation);
	}
	else
	{
		D->Location = Location;
		D->Rotation = Rotation;
	}
	if (AShowroomBooth* Booth = FindCabinetSetActor(InstanceID))
	{
		Booth->SetActorLocationAndRotation(D->Location, D->Rotation);
	}
}

void ARoomPlannerManager::ReconcileCabinetSetActors()
{
	if (HasAuthority())
	{
		// Destroy planner booths that are no longer in the layout
		if (UWorld* World = GetWorld())
		{
			TArray<AShowroomBooth*> Stale;
			for (TActorIterator<AShowroomBooth> It(World); It; ++It)
			{
				if (!It->PlannerInstanceID.IsEmpty() && !CabinetSets.Contains(It->PlannerInstanceID))
				{
					Stale.Add(*It);
				}
			}
			for (AShowroomBooth* Booth : Stale)
			{
				CabinetSetActorCache.Remove(Booth->PlannerInstanceID);
				Booth->Destroy();
			}
		}
		// Spawn missing / move existing
		for (const auto& Pair : CabinetSets)
		{
			AShowroomBooth* Booth = FindCabinetSetActor(Pair.Key);
			if (!Booth)
			{
				Booth = SpawnCabinetSetActor(Pair.Value);
			}
			else
			{
				Booth->OnProductChanged.AddUniqueDynamic(this, &ARoomPlannerManager::HandleCabinetSetProductChanged); // adopted booth
				if (!Booth->GetActorLocation().Equals(Pair.Value.Location, 0.5f) || !Booth->GetActorRotation().Equals(Pair.Value.Rotation, 0.1f))
				{
					Booth->SetActorLocationAndRotation(Pair.Value.Location, Pair.Value.Rotation);
				}
			}
		}
	}
	else
	{
		// Clients: booths replicate; just keep the transforms aligned with the layout for immediate feedback.
		for (const auto& Pair : CabinetSets)
		{
			if (AShowroomBooth* Booth = FindCabinetSetActor(Pair.Key))
			{
				if (!Booth->GetActorLocation().Equals(Pair.Value.Location, 0.5f) || !Booth->GetActorRotation().Equals(Pair.Value.Rotation, 0.1f))
				{
					Booth->SetActorLocationAndRotation(Pair.Value.Location, Pair.Value.Rotation);
				}
			}
		}
	}
}

void ARoomPlannerManager::DestroyAllPlannerCabinetSets()
{
	if (!HasAuthority()) return;
	if (UWorld* World = GetWorld())
	{
		TArray<AShowroomBooth*> ToDestroy;
		for (TActorIterator<AShowroomBooth> It(World); It; ++It)
		{
			if (!It->PlannerInstanceID.IsEmpty())
			{
				ToDestroy.Add(*It);
			}
		}
		for (AShowroomBooth* Booth : ToDestroy)
		{
			Booth->Destroy();
		}
	}
	CabinetSetActorCache.Empty();
}

// ═══════════════════════════════════════════════════════════════════════════════
// REQ-16: project restore
// ═══════════════════════════════════════════════════════════════════════════════

bool ARoomPlannerManager::ImportProjectFromSaveJSON(const FString& SaveRecordJSON)
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("[MaxiMallConstructor] ImportProjectFromSaveJSON must run on the server."));
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SaveRecordJSON);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[MaxiMallConstructor] ImportProjectFromSaveJSON: invalid JSON."));
		return false;
	}

	// 1. Planner layout (walls, openings, finishes, objects, cabinet sets)
	const TSharedPtr<FJsonObject>* PlannerObj = nullptr;
	if (Root->TryGetObjectField(TEXT("planner"), PlannerObj) && PlannerObj && PlannerObj->IsValid())
	{
		FString PlannerString;
		TSharedRef<PlannerJsonKeys::FLayoutJsonWriter> Writer = PlannerJsonKeys::FLayoutJsonWriterFactory::Create(&PlannerString);
		FJsonSerializer::Serialize(PlannerObj->ToSharedRef(), Writer);
		ClearLayout();
		ImportLayoutFromJSON(PlannerString);
	}
	else if (Root->HasField(TEXT("nodes")) && Root->HasField(TEXT("walls")))
	{
		// A bare planner document was passed.
		ClearLayout();
		ImportLayoutFromJSON(SaveRecordJSON);
	}
	ReplicatedRoomJSON = ExportLayoutToJSON();

	// 2. Booth configuration states (planner-spawned sets by plannerInstanceId, level booths by name)
	const TArray<TSharedPtr<FJsonValue>>* BoothStates = nullptr;
	if (Root->TryGetArrayField(TEXT("boothStates"), BoothStates) && BoothStates)
	{
		TArray<AActor*> LevelBooths;
		if (GetWorld())
		{
			UGameplayStatics::GetAllActorsOfClass(GetWorld(), AShowroomBooth::StaticClass(), LevelBooths);
		}

		for (const TSharedPtr<FJsonValue>& Val : *BoothStates)
		{
			TSharedPtr<FJsonObject> BoothObj = Val->AsObject();
			if (!BoothObj.IsValid()) continue;

			AShowroomBooth* Target = nullptr;
			FString PlannerID;
			if (BoothObj->TryGetStringField(TEXT("plannerInstanceId"), PlannerID) && !PlannerID.IsEmpty())
			{
				Target = FindCabinetSetActor(PlannerID);
			}
			if (!Target)
			{
				const FString BoothName = BoothObj->GetStringField(TEXT("boothName"));
				for (AActor* A : LevelBooths)
				{
					if (A && A->GetName() == BoothName)
					{
						Target = Cast<AShowroomBooth>(A);
						break;
					}
				}
			}
			if (!Target) continue;

			const TSharedPtr<FJsonObject>* StateObjPtr = nullptr;
			if (!BoothObj->TryGetObjectField(TEXT("state"), StateObjPtr) || !StateObjPtr || !StateObjPtr->IsValid()) continue;
			const TSharedPtr<FJsonObject>& StateObj = *StateObjPtr;

			FShowroomBoothConfigState State;
			State.ProductID = FName(*StateObj->GetStringField(TEXT("productID")));
			State.ActiveSizeIndex = StateObj->GetIntegerField(TEXT("activeSizeIndex"));
			State.ActiveColorIndex = StateObj->GetIntegerField(TEXT("activeColorIndex"));
			State.CountertopSizeIndex = StateObj->GetIntegerField(TEXT("countertopSizeIndex"));
			State.ActiveCountertopColorIndex = StateObj->GetIntegerField(TEXT("activeCountertopColorIndex"));
			State.ClosetSizeIndex = StateObj->GetIntegerField(TEXT("closetSizeIndex"));
			State.ClosetColorIndex = StateObj->GetIntegerField(TEXT("closetColorIndex"));
			State.SinkSizeIndex = StateObj->GetIntegerField(TEXT("sinkSizeIndex"));
			State.SinkColorIndex = StateObj->GetIntegerField(TEXT("sinkColorIndex"));
			State.FaucetSizeIndex = StateObj->GetIntegerField(TEXT("faucetSizeIndex"));
			State.FaucetColorIndex = StateObj->GetIntegerField(TEXT("faucetColorIndex"));
			State.MirrorSizeIndex = StateObj->GetIntegerField(TEXT("mirrorSizeIndex"));
			State.MirrorColorIndex = StateObj->GetIntegerField(TEXT("mirrorColorIndex"));

			TArray<FCustomColorOverride> CustomColors;
			const TArray<TSharedPtr<FJsonValue>>* ColorsArray = nullptr;
			if (BoothObj->TryGetArrayField(TEXT("customColors"), ColorsArray) && ColorsArray)
			{
				for (const TSharedPtr<FJsonValue>& CVal : *ColorsArray)
				{
					TSharedPtr<FJsonObject> CObj = CVal->AsObject();
					if (!CObj.IsValid()) continue;
					const EFurnitureComponentType CompType = static_cast<EFurnitureComponentType>(CObj->GetIntegerField(TEXT("componentType")));
					FLinearColor Color = FLinearColor::White;
					const TSharedPtr<FJsonObject>* RGBA = nullptr;
					if (CObj->TryGetObjectField(TEXT("color"), RGBA) && RGBA && RGBA->IsValid())
					{
						Color.R = (*RGBA)->GetNumberField(TEXT("r"));
						Color.G = (*RGBA)->GetNumberField(TEXT("g"));
						Color.B = (*RGBA)->GetNumberField(TEXT("b"));
						Color.A = (*RGBA)->GetNumberField(TEXT("a"));
					}
					UMaterialInterface* OverrideMat = nullptr;
					FString MatPath;
					if (CObj->TryGetStringField(TEXT("overrideMaterial"), MatPath) && !MatPath.IsEmpty())
					{
						OverrideMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MatPath));
					}
					CustomColors.Add(FCustomColorOverride(CompType, Color, OverrideMat));
				}
			}

			TArray<EDoorSlotState> DoorStates;
			const TArray<TSharedPtr<FJsonValue>>* DoorsArray = nullptr;
			if (BoothObj->TryGetArrayField(TEXT("doorStates"), DoorsArray) && DoorsArray)
			{
				for (const TSharedPtr<FJsonValue>& DVal : *DoorsArray)
				{
					DoorStates.Add(static_cast<EDoorSlotState>(static_cast<int32>(DVal->AsNumber())));
				}
			}

			Target->LoadBoothFullState(State, CustomColors, DoorStates);
		}
	}

	return true;
}


// ═══════════════════════════════════════════════════════════════════════════════
// Room lights (one APlannerRoomLightActor per detected room)
// ═══════════════════════════════════════════════════════════════════════════════

void ARoomPlannerManager::ClearRoomLights()
{
	for (auto& Pair : RoomLights)
	{
		if (Pair.Value && Pair.Value->IsValidLowLevel())
		{
			Pair.Value->Destroy();
		}
	}
	RoomLights.Empty();
}

void ARoomPlannerManager::RebuildRoomLights()
{
	bRoomLightsDirty = false;
	AppliedRoomLightSettingsHash = APlannerRoomLightActor::ComputeSettingsHash(RoomLightSettings);
	UWorld* World = GetWorld();
	if (!RoomLightSettings.bEnabled || !World || World->GetNetMode() == NM_DedicatedServer)
	{
		ClearRoomLights(); // lights are local rendering only
		return;
	}

	// Room IDs are re-assigned by every detection pass, so lights are matched by CONTENT: a light whose signature
	// (room polygon, ceiling height, settings) equals a current room is kept as it is (its shadows, GI and mask
	// atlas slot survive edits elsewhere); every other light is destroyed and a new one is built. A split room
	// therefore gets two new lights, a merge one, and nothing stale or duplicated can survive.
	TMap<uint32, TArray<TObjectPtr<APlannerRoomLightActor>>> Reusable;
	for (const auto& Pair : RoomLights)
	{
		if (Pair.Value && IsValid(Pair.Value))
		{
			Reusable.FindOrAdd(Pair.Value->BuildSignature).Add(Pair.Value);
		}
	}
	RoomLights.Empty();

	int32 Kept = 0, Built = 0;
	for (const auto& Pair : Rooms)
	{
		const FRoomData& Room = Pair.Value;
		if (Room.FloorPolygon.Num() < 3) continue;

		APlannerRoomLightActor* Light = nullptr;
		const uint32 Signature = APlannerRoomLightActor::ComputeBuildSignature(Room, RoomLightSettings);
		if (TArray<TObjectPtr<APlannerRoomLightActor>>* Same = Reusable.Find(Signature))
		{
			if (Same->Num() > 0)
			{
				Light = Same->Pop();
				++Kept;
			}
		}
		if (!Light)
		{
			FActorSpawnParameters Params;
			Params.Owner = this;
			Params.ObjectFlags |= RF_Transient;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Light = World->SpawnActor<APlannerRoomLightActor>(APlannerRoomLightActor::StaticClass(), FTransform::Identity, Params);
			if (!Light) continue;
			Light->Build(Room, RoomLightSettings);
			++Built;
		}
		Light->RoomID = Room.RoomID;
		Light->SetShown(!b2DViewMode);
		RoomLights.Add(Room.RoomID, Light);
	}

	for (auto& Pair : Reusable)
	{
		for (TObjectPtr<APlannerRoomLightActor>& Unused : Pair.Value)
		{
			if (Unused && IsValid(Unused)) Unused->Destroy();
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[RoomLight] rebuild: %d room(s), %d light(s) kept, %d built."), RoomLights.Num(), Kept, Built);
}

void ARoomPlannerManager::SetRoomLightSettings(const FPlannerRoomLightSettings& NewSettings)
{
	RoomLightSettings = NewSettings;
	RebuildRoomLights();
}

int32 ARoomPlannerManager::GetCeilingLightCount() const
{
	int32 Total = 0;
	for (const auto& Pair : RoomLights)
	{
		if (Pair.Value) Total += Pair.Value->GetLightCount();
	}
	return Total;
}

int32 ARoomPlannerManager::GetRoomCeilingLightCount(int32 RoomID) const
{
	const TObjectPtr<APlannerRoomLightActor>* Found = RoomLights.Find(RoomID);
	return (Found && *Found) ? (*Found)->GetLightCount() : 0;
}

APlannerRoomLightActor* ARoomPlannerManager::GetRoomLight(int32 RoomID) const
{
	const TObjectPtr<APlannerRoomLightActor>* Found = RoomLights.Find(RoomID);
	return Found ? Found->Get() : nullptr;
}

void ARoomPlannerManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Console variables outlive a PIE session: never leave the Lumen override behind.
	UpdatePlannerLumenMode(false);
	Super::EndPlay(EndPlayReason);
}

void ARoomPlannerManager::Destroyed()
{
	UpdatePlannerLumenMode(false); // a manager that never began play (test worlds) gets no EndPlay
	Super::Destroyed();
}

void ARoomPlannerManager::BeginDestroy()
{
	UpdatePlannerLumenMode(false); // no claim held (the usual case): nothing happens
	Super::BeginDestroy();
}

void ARoomPlannerManager::SetPlannerSessionActive(bool bActive)
{
	bPlannerSessionActive = bActive;
	UpdatePlannerExposure();
	UpdateExteriorBackdropVisibility();
}

void ARoomPlannerManager::UpdatePlannerExposure()
{
	if (!PlannerExposure) return;
	FPostProcessSettings& PP = PlannerExposure->Settings;
	PP.AutoExposureMinBrightness = FMath::Min(PlannerExposureMinEV100, PlannerExposureMaxEV100);
	PP.AutoExposureMaxBrightness = FMath::Max(PlannerExposureMinEV100, PlannerExposureMaxEV100);
	PP.AutoExposureBias = PlannerExposureBias;
	// Session = planner UI open (either flag; bPlannerUIOpen is the widget's existing open/close flag).
	const bool bSessionOpen = bPlannerSessionActive || bPlannerUIOpen;
	const bool bWantEnabled = bManagePlannerExposure && bSessionOpen && !b2DViewMode
		&& GetWorld() && GetWorld()->GetNetMode() != NM_DedicatedServer;
	if (PlannerExposure->bEnabled != bWantEnabled)
	{
		PlannerExposure->bEnabled = bWantEnabled;
	}

	// The Lumen overrides follow the same lifecycle as the exposure override (planner open, 3D, never on a dedicated server).
	UpdatePlannerLumenMode(bSessionOpen && !b2DViewMode && GetWorld() && GetWorld()->GetNetMode() != NM_DedicatedServer);
}

namespace PlannerCVarOverrides
{
	/** The planner's process-wide override of one console variable (see FPlannerCVarOverride). */
	struct FShared
	{
		/** Managers holding a claim. */
		int32 Owners = 0;
		/** The variable held a code-priority value before the first claim: write SavedValue back rather than unsetting. */
		bool bRestoreSavedValue = false;
		FString SavedValue;
		/** The value the planner set last. */
		FString AppliedValue;
		/**
		 * A higher-priority value (console, command line…) was on top when the claim began: the planner sets nothing, not even into
		 * the code-priority slot beneath it (the engine records a rejected Set there, overwriting another system's code value), and
		 * gives nothing back. Checked again on every Apply: once that value is gone, the override takes over as a first claim would.
		 */
		bool bYielded = false;
	};

	/** The variable's current priority is above code: a code-priority Set would be refused, and would still overwrite the code slot. */
	bool IsOutrankedAboveCode(const IConsoleVariable* CVar)
	{
		return (uint32)(CVar->GetFlags() & ECVF_SetByMask) > (uint32)ECVF_SetByCode;
	}

	/** Records how to give the variable back (its value now, and whether that is a code-priority value) and leaves the yield. */
	void TakeOver(FShared& Shared, const IConsoleVariable* CVar)
	{
		Shared.SavedValue = CVar->GetString();
		Shared.bRestoreSavedValue = (CVar->GetFlags() & ECVF_SetByMask) == ECVF_SetByCode;
		Shared.AppliedValue.Reset();
		Shared.bYielded = false;
	}

	/** Keyed by console variable name. Console variables and the managers' lifecycle belong to the game thread. */
	TMap<FString, FShared>& Get()
	{
		static TMap<FString, FShared> Overrides;
		return Overrides;
	}
}

void FPlannerCVarOverride::Apply(const FString& Value)
{
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
	if (!CVar) return;
	PlannerCVarOverrides::FShared& Shared = PlannerCVarOverrides::Get().FindOrAdd(Name);
	bool bTakingOver = false;
	if (!bActive)
	{
		bActive = true;
		if (++Shared.Owners == 1)
		{
			// The first claim in the process remembers how to give the variable back, unless a higher-priority value is on top: then
			// it yields and touches nothing.
			if (PlannerCVarOverrides::IsOutrankedAboveCode(CVar))
			{
				Shared.bYielded = true;
				Shared.AppliedValue.Reset();
				UE_LOG(LogTemp, Log, TEXT("[MaxiMallConstructor] %s stays %s: a %s value outranks the planner's override (not applied)."),
					Name, *CVar->GetString(), GetConsoleVariableSetByName((EConsoleVariableFlags)(CVar->GetFlags() & ECVF_SetByMask)));
			}
			else
			{
				PlannerCVarOverrides::TakeOver(Shared, CVar);
				bTakingOver = true;
			}
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[MaxiMallConstructor] %s is already claimed by another planner in this process (%d claims)."), Name, Shared.Owners);
		}
	}
	if (Shared.bYielded && !PlannerCVarOverrides::IsOutrankedAboveCode(CVar))
	{
		// The value that outranked the override is gone: take over now, as the first claim would have.
		PlannerCVarOverrides::TakeOver(Shared, CVar);
		bTakingOver = true;
	}
	if (Shared.bYielded) return;
	if (Shared.AppliedValue != Value)
	{
		CVar->Set(*Value, ECVF_SetByCode); // ignored (with the engine's warning) if a console value came on top since the claim
		Shared.AppliedValue = Value;
	}
	if (bTakingOver)
	{
		UE_LOG(LogTemp, Log, TEXT("[MaxiMallConstructor] %s %s → %s while the planner is open in 3D."), Name, *Shared.SavedValue, *CVar->GetString());
	}
}

void FPlannerCVarOverride::Restore()
{
	if (!bActive) return;
	bActive = false;
	TMap<FString, PlannerCVarOverrides::FShared>& Overrides = PlannerCVarOverrides::Get();
	PlannerCVarOverrides::FShared* Shared = Overrides.Find(Name);
	if (!Shared) return;
	if (--Shared->Owners > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[MaxiMallConstructor] %s stays %s: %d other planner(s) in this process still open in 3D."), Name,
			Shared->bYielded ? TEXT("claimed (yielded)") : TEXT("overridden"), Shared->Owners);
		return;
	}
	// The last claim gives the variable back; a yielded override never set anything, so there is nothing to give back.
	if (Shared->bYielded)
	{
		UE_LOG(LogTemp, Log, TEXT("[MaxiMallConstructor] %s released: the override had yielded, the variable was not touched."), Name);
	}
	else if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		if (Shared->bRestoreSavedValue)
		{
			CVar->Set(*Shared->SavedValue, ECVF_SetByCode);
		}
		else
		{
			CVar->Unset(ECVF_SetByCode); // back to the project / scalability / command-line value, nothing left at code priority
		}
		UE_LOG(LogTemp, Log, TEXT("[MaxiMallConstructor] %s given back: %s."), Name, *CVar->GetString());
	}
	Overrides.Remove(Name);
}

int32 FPlannerCVarOverride::GetOwnerCount(const TCHAR* InName)
{
	const PlannerCVarOverrides::FShared* Shared = PlannerCVarOverrides::Get().Find(InName);
	return Shared ? Shared->Owners : 0;
}

void ARoomPlannerManager::UpdatePlannerLumenMode(bool bIn3DSession)
{
	auto Update = [](FPlannerCVarOverride& Override, bool bWant, const FString& Value)
	{
		if (bWant)
		{
			Override.Apply(Value);
		}
		else
		{
			Override.Restore();
		}
	};
	// Hit lighting for GI (see bPlannerLumenHitLightingGI).
	Update(LumenLightingModeOverride, bIn3DSession && bPlannerLumenHitLightingGI, TEXT("1"));
	// White ceilings and walls keep their bounce light in concave edges (see PlannerShortRangeAOMaxMultibounceAlbedo).
	Update(ShortRangeAOAlbedoOverride, bIn3DSession && PlannerShortRangeAOMaxMultibounceAlbedo > 0.f,
		FString::SanitizeFloat(FMath::Clamp(PlannerShortRangeAOMaxMultibounceAlbedo, 0.f, 1.f)));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Doors & windows: materials and plan symbols; exterior view
// ═══════════════════════════════════════════════════════════════════════════════

UMaterialInterface* ARoomPlannerManager::GetOpeningMaterial(EPlannerOpeningMaterial Kind, const FLinearColor& Color)
{
	const FString Key = FString::Printf(TEXT("%d_%s"), (int32)Kind, *Color.ToFColor(false).ToHex());
	if (const TObjectPtr<UMaterialInterface>* Found = OpeningMaterialCache.Find(Key))
	{
		return Found->Get();
	}

	auto MakeTinted = [this, &Color](UMaterialInterface* Parent, float Roughness) -> UMaterialInterface*
	{
		if (!Parent) return nullptr;
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, this);
		if (!MID) return Parent;
		MID->SetVectorParameterValue(FName("BaseColor"), Color);
		MID->SetVectorParameterValue(FName("Color"), Color);
		if (Roughness >= 0.f)
		{
			MID->SetScalarParameterValue(FName("Roughness"), Roughness);
		}
		return MID;
	};

	UMaterialInterface* Result = nullptr;
	switch (Kind)
	{
	case EPlannerOpeningMaterial::Glass:
		Result = OpeningGlassMaterial ? OpeningGlassMaterial.Get()
			: LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/NewDesign/scena/Materials/glass_2.glass_2"));
		break;
	case EPlannerOpeningMaterial::FrostedGlass:
		Result = OpeningFrostedGlassMaterial ? OpeningFrostedGlassMaterial.Get()
			: LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/NewDesign/scena/Materials/sid_glass_whitte.sid_glass_whitte"));
		if (!Result)
		{
			Result = GetOpeningMaterial(EPlannerOpeningMaterial::Glass, Color);
		}
		break;
	case EPlannerOpeningMaterial::Metal:
		Result = MakeTinted(OpeningMetalMaterial ? OpeningMetalMaterial.Get()
			: LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")), 0.4f);
		break;
	case EPlannerOpeningMaterial::Leaf:
		if (LeafMaterial)
		{
			Result = LeafMaterial.Get();
			break;
		}
		[[fallthrough]];
	default:
		Result = MakeTinted(OpeningPaintMaterial ? OpeningPaintMaterial.Get() : ResolvePaintBaseMaterial(), -1.f);
		break;
	}

	OpeningMaterialCache.Add(Key, Result);
	return Result;
}

UMaterialInterface* ARoomPlannerManager::GetPlanSymbolMaterial()
{
	if (PlanSymbolMaterial)
	{
		return PlanSymbolMaterial;
	}
	// Near-black, fully rough surface: a dark line whatever lighting the 2D view has. BasicShapeMaterial is cooked (the lobby
	// map references it); EmissiveTexturedMaterial is referenced by nothing the cook follows, so packaged builds lack it.
	if (UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, this))
		{
			MID->SetVectorParameterValue(FName("Color"), FLinearColor(0.012f, 0.012f, 0.014f, 1.f));
			MID->SetScalarParameterValue(FName("Roughness"), 1.f);
			PlanSymbolMaterial = MID;
		}
	}
	if (!PlanSymbolMaterial)
	{
		PlanSymbolMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	return PlanSymbolMaterial;
}

namespace
{
	/** Exterior colours as absolute luminance (cd/m²) per channel, matched to the planner's fixed EV100 6.8 exposure (scene white ≈ 111 cd/m²; a white wall under the room light ≈ 45–50 cd/m²). */
	struct FPlannerExteriorPalette
	{
		FLinearColor Zenith;
		FLinearColor Horizon;
		FLinearColor GroundNear;
		FLinearColor GroundFar;
	};

	/** Daylight, the planner's only exterior look. */
	FPlannerExteriorPalette GetDaylightPalette()
	{
		return { FLinearColor(0.28f, 0.48f, 0.92f) * 62.f, FLinearColor(0.84f, 0.90f, 0.98f) * 98.f,
		         FLinearColor(0.50f, 0.51f, 0.47f) * 50.f, FLinearColor(0.80f, 0.85f, 0.90f) * 70.f };
	}
}

void ARoomPlannerManager::SetExteriorBackdropEnabled(bool bEnabled)
{
	bShowExteriorBackdrop = bEnabled;
	UpdateExteriorBackdropVisibility();
}

bool ARoomPlannerManager::ShouldShowExteriorBackdrop() const
{
	const UWorld* World = GetWorld();
	// Only around closed rooms: without a room there is no planner floor, and the ground would replace the level floor.
	return bShowExteriorBackdrop && (bPlannerSessionActive || bPlannerUIOpen) && !b2DViewMode && Rooms.Num() > 0
		&& World && World->GetNetMode() != NM_DedicatedServer;
}

void ARoomPlannerManager::UpdateExteriorBackdropVisibility()
{
	const bool bShow = ShouldShowExteriorBackdrop();
	if (bShow && bExteriorBackdropDirty)
	{
		RebuildExteriorBackdrop();
	}
	const bool bVisible = bShow && IsCameraInsideExteriorBackdrop();
	if (ExteriorSkyMesh) ExteriorSkyMesh->SetVisibility(bVisible && ExteriorSkyMesh->GetNumSections() > 0);
	if (ExteriorGroundMesh) ExteriorGroundMesh->SetVisibility(bVisible && ExteriorGroundMesh->GetNumSections() > 0);
}

bool ARoomPlannerManager::IsCameraInsideExteriorBackdrop() const
{
	if (ExteriorRadius <= 0.f) return true;
	const APlayerCameraManager* CameraManager = GetWorld() ? UGameplayStatics::GetPlayerCameraManager(GetWorld(), 0) : nullptr;
	if (!CameraManager) return true;
	const FVector Camera = CameraManager->GetCameraLocation();
	return FVector2D::Distance(FVector2D(Camera.X, Camera.Y), ExteriorCenter) < ExteriorRadius - 20.f && Camera.Z < ExteriorTop - 20.f;
}

void ARoomPlannerManager::RebuildExteriorBackdrop()
{
	bExteriorBackdropDirty = false;
	if (!ExteriorSkyMesh || !ExteriorGroundMesh) return;
	ExteriorSkyMesh->ClearAllMeshSections();
	ExteriorGroundMesh->ClearAllMeshSections();
	if (Nodes.Num() == 0) return;

	// EmissiveMeshMaterial (unlit, additive, emissive = "Color" × texture "LinearColor") is an engine startup package, so it
	// is present in cooked builds. Additive over the level (several stops below the planner exposure) shows the gradient as is.
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));
	if (!Parent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MaxiMallConstructor] Exterior view: /Engine/EngineMaterials/EmissiveMeshMaterial is not available; doors and windows show the level behind them."));
		return;
	}

	// Enclosure around the layout: a sky cylinder with a cap and a ground disc just below the planner floor (floor top Z = 1).
	FBox2D Bounds(ForceInit);
	for (const TPair<int32, FWallNode>& Pair : Nodes)
	{
		Bounds += Pair.Value.Position;
	}
	const FVector2D Center = Bounds.GetCenter();
	const float Radius = FMath::Max(Bounds.GetExtent().Size() + 900.f, 2200.f);
	const float SkyBottom = -50.f;
	const float SkyTop = Radius * 1.1f;
	const float EyeZ = 160.f;
	ExteriorCenter = Center;
	ExteriorRadius = Radius;
	ExteriorTop = SkyTop;

	FPlannerMeshBuffers Sky;
	PlannerMeshBuilder::AddInwardCylinder(Sky, Center, Radius, SkyBottom, SkyTop, 96, true);
	FPlannerMeshBuffers Ground;
	PlannerMeshBuilder::AddDisc(Ground, Center, Radius - 2.f, 0.4f, 96);

	const FPlannerExteriorPalette Palette = GetDaylightPalette();

	// Sky gradient by elevation angle seen from eye height (texture row 0 = top of the cylinder, V = 0).
	const int32 Rows = 64;
	TArray<FLinearColor> SkyPixels;
	SkyPixels.SetNum(Rows);
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		const float V = (Row + 0.5f) / Rows;
		const float Z = FMath::Lerp(SkyTop, SkyBottom, V);
		const float Elevation = FMath::Atan2(Z - EyeZ, Radius);
		const float T = FMath::Clamp(Elevation / FMath::DegreesToRadians(40.f), 0.f, 1.f);
		SkyPixels[Row] = FMath::Lerp(Palette.Horizon, Palette.Zenith, FMath::SmoothStep(0.f, 1.f, FMath::Pow(T, 0.65f)));
		SkyPixels[Row].A = 1.f;
	}
	// Ground: near colour around the building fading into the horizon haze at the rim (U = 0 centre, 1 rim).
	const int32 Cols = 32;
	TArray<FLinearColor> GroundPixels;
	GroundPixels.SetNum(Cols);
	for (int32 Col = 0; Col < Cols; ++Col)
	{
		const float U = (Col + 0.5f) / Cols;
		GroundPixels[Col] = FMath::Lerp(Palette.GroundNear, Palette.GroundFar, FMath::SmoothStep(0.35f, 1.f, U));
		GroundPixels[Col].A = 1.f;
	}

	ExteriorSkyTexture = PlannerRuntimeTextures::CreateHdrPixels(1, Rows, SkyPixels);
	ExteriorGroundTexture = PlannerRuntimeTextures::CreateHdrPixels(Cols, 1, GroundPixels);
	if (!ExteriorSkyTexture || !ExteriorGroundTexture) return;

	UMaterialInstanceDynamic* SkyMID = UMaterialInstanceDynamic::Create(Parent, this);
	UMaterialInstanceDynamic* GroundMID = UMaterialInstanceDynamic::Create(Parent, this);
	if (!SkyMID || !GroundMID) return;
	for (UMaterialInstanceDynamic* MID : { SkyMID, GroundMID })
	{
		MID->SetVectorParameterValue(FName("Color"), FLinearColor::White);
	}
	SkyMID->SetTextureParameterValue(FName("LinearColor"), ExteriorSkyTexture);
	GroundMID->SetTextureParameterValue(FName("LinearColor"), ExteriorGroundTexture);

	ExteriorSkyMesh->CreateMeshSection(0, Sky.Vertices, Sky.Triangles, Sky.Normals, Sky.UVs, TArray<FColor>(), Sky.Tangents, false);
	ExteriorSkyMesh->SetMaterial(0, SkyMID);
	ExteriorGroundMesh->CreateMeshSection(0, Ground.Vertices, Ground.Triangles, Ground.Normals, Ground.UVs, TArray<FColor>(), Ground.Tangents, false);
	ExteriorGroundMesh->SetMaterial(0, GroundMID);
}

void ARoomPlannerManager::SetAutoCeilingLightsEnabled(bool bEnabled)
{
	RoomLightSettings.bEnabled = bEnabled;
	RebuildRoomLights();
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorldAndArgs GPlannerCeilingLightsCmd(
	TEXT("planner.CeilingLights"),
	TEXT("planner.CeilingLights [0|1] — disable / enable the automatic ceiling lights; no argument = rebuild and print the count."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		ARoomPlannerManager* Manager = ARoomPlannerManager::GetOrCreateInstance(World);
		if (!Manager)
		{
			UE_LOG(LogTemp, Error, TEXT("[MaxiMallConstructor] planner.CeilingLights: no planner manager in this world (run while playing)."));
			return;
		}
		if (Args.Num() > 0)
		{
			Manager->SetAutoCeilingLightsEnabled(FCString::Atoi(*Args[0]) != 0);
		}
		else
		{
			Manager->SetAutoCeilingLightsEnabled(Manager->RoomLightSettings.bEnabled);
		}
		UE_LOG(LogTemp, Log, TEXT("[MaxiMallConstructor] planner.CeilingLights: enabled=%d, room lights=%d, light components=%d"), Manager->RoomLightSettings.bEnabled ? 1 : 0, Manager->GetRoomsForDebug().Num(), Manager->GetCeilingLightCount());
		for (const auto& RoomPair : Manager->GetRoomsForDebug())
		{
			const APlannerRoomLightActor* RL = Manager->GetRoomLight(RoomPair.Key);
			UE_LOG(LogTemp, Log, TEXT("    room %d: %.1f m², ceiling %.0f cm, polygon %d pts → panel %d pts, %d light, rect %.0f×%.0f cm, coverage %.2f, %.0f lm, panel %.0f cd/m²"),
				RoomPair.Key, RoomPair.Value.AreaM2, RoomPair.Value.CeilingHeightCm, RoomPair.Value.FloorPolygon.Num(),
				RL ? RL->SurfacePolygon.Num() : 0, Manager->GetRoomCeilingLightCount(RoomPair.Key),
				RL ? RL->LightRectSizeCm.X : 0.f, RL ? RL->LightRectSizeCm.Y : 0.f, RL ? RL->MaskCoverage : 0.f,
				RL ? RL->EmittedLumens : 0.f, RL ? RL->PanelLuminanceNits : 0.f);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GPlannerExposureCmd(
	TEXT("planner.Exposure"),
	TEXT("planner.Exposure [minEV100 maxEV100 [bias]] — bounded auto-exposure used while the planner is open in 3D; 'off' disables the override. No args = print."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		ARoomPlannerManager* Manager = ARoomPlannerManager::GetOrCreateInstance(World);
		if (!Manager)
		{
			UE_LOG(LogTemp, Error, TEXT("[MaxiMallConstructor] planner.Exposure: no planner manager in this world (run while playing)."));
			return;
		}
		if (Args.Num() == 1 && Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
		{
			Manager->bManagePlannerExposure = false;
		}
		else if (Args.Num() >= 2)
		{
			Manager->bManagePlannerExposure = true;
			Manager->PlannerExposureMinEV100 = FCString::Atof(*Args[0]);
			Manager->PlannerExposureMaxEV100 = FCString::Atof(*Args[1]);
			if (Args.Num() >= 3) Manager->PlannerExposureBias = FCString::Atof(*Args[2]);
		}
		Manager->UpdatePlannerExposure();
		UE_LOG(LogTemp, Log, TEXT("[MaxiMallConstructor] planner.Exposure: managed=%d active=%d EV100 %.1f…%.1f bias %+.1f"),
			Manager->bManagePlannerExposure ? 1 : 0, (Manager->PlannerExposure && Manager->PlannerExposure->bEnabled) ? 1 : 0,
			Manager->PlannerExposureMinEV100, Manager->PlannerExposureMaxEV100, Manager->PlannerExposureBias);

		// Effective post-process chain at the local camera: the renderer walks World->PostProcessVolumes in ascending
		// priority and blends every enabled volume that encompasses the camera (unbound = always); the last one to
		// override a setting wins. Printing the same walk shows which volume really owns the exposure right now.
		FVector CamLoc = FVector::ZeroVector;
		if (APlayerCameraManager* PCM = UGameplayStatics::GetPlayerCameraManager(World, 0)) CamLoc = PCM->GetCameraLocation();
		UE_LOG(LogTemp, Log, TEXT("[MaxiMallConstructor] planner.Exposure: camera at (%.0f, %.0f, %.0f); post-process volumes in blend order:"), CamLoc.X, CamLoc.Y, CamLoc.Z);
		const FPostProcessSettings* ExposureOwner = nullptr;
		FString ExposureOwnerName = TEXT("(none: engine defaults)");
		for (IInterface_PostProcessVolume* Vol : World->PostProcessVolumes)
		{
			if (!Vol) continue;
			const FPostProcessVolumeProperties Props = Vol->GetProperties();
			float Dist = 0.f;
			const bool bEncompasses = Props.bIsUnbound || Vol->EncompassesPoint(CamLoc, 0.f, &Dist);
			const bool bApplies = Props.bIsEnabled && bEncompasses && Props.BlendWeight > 0.f;
			const UObject* Obj = Cast<UObject>(Vol);
			FString Name = Obj ? Obj->GetPathName() : TEXT("?");
			if (const UActorComponent* Comp = Cast<UActorComponent>(Obj)) Name = FString::Printf(TEXT("%s (component on %s)"), *Comp->GetName(), Comp->GetOwner() ? *Comp->GetOwner()->GetName() : TEXT("?"));
			else if (const AActor* Act = Cast<AActor>(Obj)) Name = Act->GetName();
			FString Exposure;
			if (Props.Settings)
			{
				Exposure = FString::Printf(TEXT("method %s, min %s, max %s, bias %s"),
					Props.Settings->bOverride_AutoExposureMethod ? *FString::FromInt((int32)Props.Settings->AutoExposureMethod) : TEXT("-"),
					Props.Settings->bOverride_AutoExposureMinBrightness ? *FString::SanitizeFloat(Props.Settings->AutoExposureMinBrightness) : TEXT("-"),
					Props.Settings->bOverride_AutoExposureMaxBrightness ? *FString::SanitizeFloat(Props.Settings->AutoExposureMaxBrightness) : TEXT("-"),
					Props.Settings->bOverride_AutoExposureBias ? *FString::SanitizeFloat(Props.Settings->AutoExposureBias) : TEXT("-"));
				if (bApplies && (Props.Settings->bOverride_AutoExposureMinBrightness || Props.Settings->bOverride_AutoExposureMaxBrightness))
				{
					ExposureOwner = Props.Settings;
					ExposureOwnerName = Name;
				}
			}
			UE_LOG(LogTemp, Log, TEXT("    priority %6.1f  %-7s  enabled=%d unbound=%d encompasses=%d weight=%.2f  %s  exposure overrides: %s"),
				Props.Priority, bApplies ? TEXT("APPLIES") : TEXT("skipped"), Props.bIsEnabled ? 1 : 0, Props.bIsUnbound ? 1 : 0, bEncompasses ? 1 : 0, Props.BlendWeight, *Name, *Exposure);
		}
		if (ExposureOwner)
		{
			UE_LOG(LogTemp, Log, TEXT("[MaxiMallConstructor] planner.Exposure: EFFECTIVE exposure range comes from %s → EV100 %.2f…%.2f"),
				*ExposureOwnerName, ExposureOwner->AutoExposureMinBrightness, ExposureOwner->AutoExposureMaxBrightness);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[MaxiMallConstructor] planner.Exposure: EFFECTIVE exposure range comes from %s"), *ExposureOwnerName);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GPlannerExteriorCmd(
	TEXT("planner.Exterior"),
	TEXT("planner.Exterior [on|off] — unlit daylight view behind doors and windows in 3D. No argument = print."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		ARoomPlannerManager* Manager = ARoomPlannerManager::GetOrCreateInstance(World);
		if (!Manager)
		{
			UE_LOG(LogTemp, Error, TEXT("[MaxiMallConstructor] planner.Exterior: no planner manager in this world (run while playing)."));
			return;
		}
		if (Args.Num() > 0)
		{
			const FString& Arg = Args[0];
			if (Arg.Equals(TEXT("off"), ESearchCase::IgnoreCase)) Manager->SetExteriorBackdropEnabled(false);
			else if (Arg.Equals(TEXT("on"), ESearchCase::IgnoreCase)) Manager->SetExteriorBackdropEnabled(true);
		}
		UE_LOG(LogTemp, Log, TEXT("[MaxiMallConstructor] planner.Exterior: enabled=%d visible=%d"),
			Manager->bShowExteriorBackdrop ? 1 : 0,
			(Manager->ExteriorSkyMesh && Manager->ExteriorSkyMesh->IsVisible()) ? 1 : 0);
	}));

static FAutoConsoleCommandWithWorldAndArgs GPlannerDoorsCmd(
	TEXT("planner.Doors"),
	TEXT("planner.Doors [open|closed] — opens or closes every door / window leaf in the 3D view. No argument = print."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		ARoomPlannerManager* Manager = ARoomPlannerManager::GetOrCreateInstance(World);
		if (!Manager)
		{
			UE_LOG(LogTemp, Error, TEXT("[MaxiMallConstructor] planner.Doors: no planner manager in this world (run while playing)."));
			return;
		}
		if (Args.Num() > 0)
		{
			Manager->SetAllOpeningLeavesOpen(Args[0].Equals(TEXT("open"), ESearchCase::IgnoreCase));
		}
		UE_LOG(LogTemp, Log, TEXT("[MaxiMallConstructor] planner.Doors: leaves %s by default"), Manager->GetDefaultOpeningLeavesOpen() ? TEXT("open") : TEXT("closed"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GPlannerCeilingLightScaleCmd(
	TEXT("planner.CeilingLightScale"),
	TEXT("planner.CeilingLightScale <multiplier> — scales the flux of every automatic ceiling light (1 = defaults) and rebuilds them."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		ARoomPlannerManager* Manager = ARoomPlannerManager::GetOrCreateInstance(World);
		if (!Manager)
		{
			UE_LOG(LogTemp, Error, TEXT("[MaxiMallConstructor] planner.CeilingLightScale: no planner manager in this world (run while playing)."));
			return;
		}
		if (Args.Num() > 0)
		{
			Manager->RoomLightSettings.IntensityScale = FMath::Max(0.f, FCString::Atof(*Args[0]));
			Manager->RebuildRoomLightsPublic();
		}
		UE_LOG(LogTemp, Log, TEXT("[MaxiMallConstructor] planner.CeilingLightScale = %.2f (lumens/m² %.0f, lights=%d)"),
			Manager->RoomLightSettings.IntensityScale, Manager->RoomLightSettings.LumensPerM2, Manager->GetCeilingLightCount());
	}));
#endif
