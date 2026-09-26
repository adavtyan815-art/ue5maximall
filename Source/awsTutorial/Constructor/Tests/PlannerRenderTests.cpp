// Copyright 2026 MaxiMall. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Constructor/PlannerPlacedObjectActor.h"
#include "Constructor/PlannerRoomLightActor.h"
#include "Constructor/RoomPlannerManager.h"
#include "awsTutorial_PlayerController.h"
#include "FurnitureConfigurator/UI/RoomPlannerWidget.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "AssetCompilingManager.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/LevelStreaming.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Interfaces/Interface_PostProcessVolume.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Scalability.h"
#include "ShaderCompiler.h"
#include "Tests/AutomationCommon.h"
#include "UnrealClient.h"

// ─────────────────────────────────────────────────────────────────────────────
// Ceiling corners in the planner's 3D view, rendered and measured
// ─────────────────────────────────────────────────────────────────────────────
//
// Renders an upper room corner exactly as the planner shows it in 3D (the manager's own room, room light, fixed
// exposure and Lumen overrides; Epic scalability as the player controller forces it) and measures the brightness of
// fixed places on the picture: the ceiling centre, the ceiling a few cm from the wall seam, the ceiling corner, the
// top of the walls, the walls next to the upper corner, the middle of the walls, the vertical corner at mid height,
// plus profiles away from the seam on the ceiling and down the walls. Several variants (console variables, room light
// settings) are captured in one run, each written as a PNG, with the numbers in CeilingCorner_metrics.tsv.
//
// Needs a game viewport and a GPU: skipped unless -PlannerSnapshotDir=<folder> is given. Run it in -game, e.g.
//   UnrealEditor.exe awsTutorial.uproject "/Engine/Maps/Entry?game=/Script/Engine.GameModeBase" -game -RenderOffscreen
//     -ResX=1920 -ResY=1080 -ForceRes -unattended -nosound -UserDir=<scratch folder> -PlannerSnapshotDir=<folder>
//     -ExecCmds="Automation RunTests MaxiMall.Planner.Render.CeilingCorner; Quit" -testexit="Automation Test Queue Empty"
// Optional: -PlannerRenderVariants=<Label:key=value,key=value|Label2:...> (a key with a dot and no "light." / "world." prefix is
// a console variable, set at console priority and unset afterwards, ShowFlag.<Flag> included; "light.<Field>" is a
// FPlannerRoomLightSettings field; "eye=X;Y;Z" / "target=X;Y;Z" move the camera for that variant; "world.PostProcessVolumes=0"
// switches the level's post-process volumes off and "world.PawnCameraPP=1" puts the level's default pawn camera post-process on
// the capture camera, both for that variant and in memory only), -PlannerRenderSettle=<seconds> (Lumen settling time per
// variant, default 6), -PlannerRenderEye / -PlannerRenderTarget, and -PlannerRenderRoomAt=planner|X,Y: the room is then the
// planner's 4 × 4 m preset (BuildPreset4x4mRoom) centred where the planner puts the player in this map (planner: the widget's
// PlannerRelocationLocation through FindNonOverlappingPlannerSpot, as URoomPlannerWidget does) or at X,Y, instead of the room at
// the origin. The scene around the room (post-process volumes at the camera and what they override, the pawn camera's
// post-process, lights, fog, level geometry near the room, streaming levels) is logged as "[CeilingCorner] scene" lines.
// The owner's scene: "/Game/FirstPerson/Maps/WaitingRoomLobbyMap?game=/Script/Engine.GameModeBase" -PlannerRenderRoomAt=planner
// (the room lands at (-10000, 0), inside the level's bounded GlobalProcessVolume, on SM_Template_Map_Floor).
// With the default camera and an unchanged first variant the first capture must also clear the seam thresholds below.

namespace
{
	/** One captured variant: a label and what it changes. */
	struct FCeilingRenderVariant
	{
		FString Label;
		TArray<TPair<FString, FString>> CVars;       // console variable → value
		TArray<TPair<FString, FString>> LightFields; // FPlannerRoomLightSettings field → value
		TArray<TPair<FString, FString>> WorldFields; // scene switch (PostProcessVolumes, PawnCameraPP) → value
		TOptional<FVector> Eye;                      // camera for this variant (cm from the corner, Z from the floor)
		TOptional<FVector> Target;
		TOptional<float> Fov;                        // its field of view (default 80, the planner's perspective FOV)
	};

	/** "X;Y;Z" → vector (unset when malformed). */
	TOptional<FVector> ParseCeilingRenderPoint(const FString& Text)
	{
		TArray<FString> Parts;
		if (Text.ParseIntoArray(Parts, TEXT(";"), true) != 3) return {};
		return FVector(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2]));
	}

	/**
	 * The default run: the current planner, the planner as it was before the ceiling seam fix (engine short-range AO
	 * albedo cap 0.5, light 5 cm under the ceiling), each half of the fix alone, and references: short-range AO off,
	 * no shadows at all, no contact shadows (neither shadowing term can reach the seam), and a stronger bounce.
	 */
	const TCHAR* DefaultCeilingRenderVariants =
		TEXT("current")
		TEXT("|legacy:r.Lumen.ScreenProbeGather.ShortRangeAO.MaxMultibounceAlbedo=0.5,light.CeilingOffsetCm=4")
		TEXT("|aocap_only:light.CeilingOffsetCm=4")
		TEXT("|offset_only:r.Lumen.ScreenProbeGather.ShortRangeAO.MaxMultibounceAlbedo=0.5")
		TEXT("|srao_off:r.Lumen.ScreenProbeGather.ShortRangeAO=0")
		TEXT("|no_shadows:light.bCastShadows=false")
		TEXT("|no_contact_shadows:r.ContactShadows=0")
		TEXT("|indirect13:light.IndirectIntensity=1.3");

	/** Lower bounds for the current planner (default camera): the pre-fix planner measured 0.915 / 0.630 / 0.556. */
	constexpr double MinCeilCornerRatio = 0.96;
	constexpr double MinWallTopRatio = 0.68;
	constexpr double MinWallCornerRatio = 0.60;

	TArray<FCeilingRenderVariant> ParseCeilingRenderVariants(const FString& Spec)
	{
		TArray<FCeilingRenderVariant> Variants;
		TArray<FString> Items;
		Spec.ParseIntoArray(Items, TEXT("|"), true);
		for (const FString& Item : Items)
		{
			FCeilingRenderVariant Variant;
			FString Assignments;
			if (!Item.Split(TEXT(":"), &Variant.Label, &Assignments))
			{
				Variant.Label = Item;
			}
			Variant.Label.TrimStartAndEndInline();
			TArray<FString> Pairs;
			Assignments.ParseIntoArray(Pairs, TEXT(","), true);
			for (const FString& Pair : Pairs)
			{
				FString Key, Value;
				if (!Pair.Split(TEXT("="), &Key, &Value)) continue;
				Key.TrimStartAndEndInline();
				Value.TrimStartAndEndInline();
				if (Key.StartsWith(TEXT("light.")))
				{
					Variant.LightFields.Emplace(Key.RightChop(6), Value);
				}
				else if (Key.StartsWith(TEXT("world.")))
				{
					Variant.WorldFields.Emplace(Key.RightChop(6), Value);
				}
				else if (Key.Equals(TEXT("eye"), ESearchCase::IgnoreCase))
				{
					Variant.Eye = ParseCeilingRenderPoint(Value);
				}
				else if (Key.Equals(TEXT("target"), ESearchCase::IgnoreCase))
				{
					Variant.Target = ParseCeilingRenderPoint(Value);
				}
				else if (Key.Equals(TEXT("fov"), ESearchCase::IgnoreCase))
				{
					Variant.Fov = FMath::Clamp(FCString::Atof(*Value), 10.f, 170.f);
				}
				else
				{
					Variant.CVars.Emplace(Key, Value);
				}
			}
			if (!Variant.Label.IsEmpty()) Variants.Add(MoveTemp(Variant));
		}
		return Variants;
	}

	/** A console variable changed at console priority for one variant, and given back as it was. */
	struct FCeilingRenderCVarPatch
	{
		IConsoleVariable* CVar = nullptr;
		FString SavedValue;
		bool bSavedWasConsole = false;

		void Restore() const
		{
			if (!CVar) return;
			if (bSavedWasConsole)
			{
				CVar->Set(*SavedValue, ECVF_SetByConsole);
			}
			else
			{
				CVar->Unset(ECVF_SetByConsole); // falls back to whatever lower priority (code, scalability, ini) holds
				if (CVar->GetString() != SavedValue)
				{
					CVar->Set(*SavedValue, ECVF_SetByConsole); // a variable that keeps no history (e.g. a show flag): write it back
				}
			}
		}
	};

	/** A group of world points whose pixels are averaged; reported relative to the group named Reference (none = absolute). */
	struct FCeilingSampleSet
	{
		FString Name;
		FString Reference;
		FColor Marker = FColor::Transparent; // ring drawn around each sample in the annotated capture (transparent = none)
		TArray<FVector> Points;
	};

	/** Brightness of one sample set in one capture. */
	struct FCeilingSampleResult
	{
		double MeanLuminance = 0.; // display-referred linear luminance (sRGB decoded), 0..1
		int32 Samples = 0;
	};

	/** State shared by the latent steps of one run. */
	struct FCeilingRenderRun
	{
		FString OutDir;
		float SettleSeconds = 6.f;
		TArray<FCeilingRenderVariant> Variants;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<ARoomPlannerManager> Manager;
		TWeakObjectPtr<ACameraActor> Camera;
		TWeakObjectPtr<APlayerController> Controller;
		bool bCreatedManager = false;
		bool bDefaultView = true;        // the current variant is seen from the default camera
		bool bBaseViewDefault = true;    // no -PlannerRenderEye / -PlannerRenderTarget
		FVector2D CornerMin = FVector2D::ZeroVector; // the room corner the camera looks into (smallest X / Y of the clear outline)
		FVector BaseEye = FVector::ZeroVector;       // camera of the run, relative to CornerMin (Z from the floor)
		FVector BaseTarget = FVector::ZeroVector;
		FString ViewLabel;
		TOptional<FVector> LastLoggedEye;             // post-process volumes at the camera are logged once per camera position
		TArray<TWeakObjectPtr<APostProcessVolume>> DisabledVolumes; // world.PostProcessVolumes=0
		bool bPawnCameraPP = false;                   // world.PawnCameraPP=1
		Scalability::FQualityLevels SavedQuality;
		FPlannerRoomLightSettings SavedLightSettings;
		TArray<FCeilingRenderCVarPatch> ActivePatches;
		TArray<FCeilingSampleSet> SampleSets;
		FDelegateHandle CaptureHandle;
		int32 CurrentVariant = INDEX_NONE;
		bool bCaptured = false;
		TArray<FString> TableRows;
		TArray<FString> Errors;
		TArray<FString> Infos;
		TMap<FString, TMap<FString, double>> Metrics; // variant → sample set → value (relative to its reference)
	};

	float ReadCVarFloat(const TCHAR* Name)
	{
		const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
		return CVar ? CVar->GetFloat() : -1.f;
	}

	double PixelLuminance(const FColor& Pixel)
	{
		const FLinearColor Linear(Pixel); // sRGB decode
		return 0.2126 * Linear.R + 0.7152 * Linear.G + 0.0722 * Linear.B;
	}

	/** Mean luminance of the 3×3 pixel boxes around the screen projections of Points. */
	FCeilingSampleResult MeasurePoints(const APlayerController* PC, const TArray<FVector>& Points, int32 Width, int32 Height,
		const FVector2D& PixelScale, const TArray<FColor>& Bitmap, TArray<FIntPoint>* OutPixels = nullptr)
	{
		FCeilingSampleResult Result;
		double Sum = 0.;
		for (const FVector& Point : Points)
		{
			FVector2D Screen;
			if (!PC || !PC->ProjectWorldLocationToScreen(Point, Screen, false)) continue;
			const int32 X = FMath::RoundToInt(Screen.X * PixelScale.X);
			const int32 Y = FMath::RoundToInt(Screen.Y * PixelScale.Y);
			if (X < 1 || Y < 1 || X >= Width - 1 || Y >= Height - 1) continue;
			double Box = 0.;
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					Box += PixelLuminance(Bitmap[(Y + DY) * Width + (X + DX)]);
				}
			}
			Sum += Box / 9.;
			++Result.Samples;
			if (OutPixels) OutPixels->Add(FIntPoint(X, Y));
		}
		Result.MeanLuminance = Result.Samples > 0 ? Sum / Result.Samples : 0.;
		return Result;
	}

	/**
	 * World sample points around the corner at the smallest X / Y of the room's clear outline (wall A runs along +X,
	 * wall B along +Y). Ceiling places are reported relative to the ceiling centre, wall places relative to the middle
	 * of the walls, so a value below 1 is a darkening towards the seam / corner.
	 */
	void BuildCeilingSamples(FCeilingRenderRun& Run, const FVector2D& Min, const FVector2D& Max, double CeilZ)
	{
		const FVector2D Centre = (Min + Max) * 0.5;
		auto OnWallA = [&](double Along, double Off, double Z) { return FVector(Min.X + Along, Min.Y + Off, Z); };
		auto OnWallB = [&](double Along, double Off, double Z) { return FVector(Min.X + Off, Min.Y + Along, Z); };
		const double Alongs[] = { 60., 80., 100., 120., 140., 160. };
		auto AlongBothWalls = [&](FCeilingSampleSet& Set, TArrayView<const double> Offsets, TArrayView<const double> Belows)
		{
			for (double Along : Alongs)
			{
				for (double Off : Offsets)
				{
					for (double Below : Belows)
					{
						Set.Points.Add(OnWallA(Along, Off, CeilZ - Below));
						Set.Points.Add(OnWallB(Along, Off, CeilZ - Below));
					}
				}
			}
		};
		auto NearVerticalCorner = [&](FCeilingSampleSet& Set, TArrayView<const double> Belows)
		{
			for (double Along : { 4., 7., 10. })
			{
				for (double Below : Belows)
				{
					Set.Points.Add(OnWallA(Along, 0., CeilZ - Below));
					Set.Points.Add(OnWallB(Along, 0., CeilZ - Below));
				}
			}
		};
		const double Zero[] = { 0. };

		FCeilingSampleSet CeilCentre{ TEXT("CeilCentre"), FString(), FColor::Red, {} };
		for (int32 I = -2; I <= 2; ++I)
		{
			for (int32 J = -2; J <= 2; ++J)
			{
				CeilCentre.Points.Add(FVector(Centre.X + 10. * I, Centre.Y + 10. * J, CeilZ));
			}
		}
		// Middle of the walls: the reference for everything on the walls.
		FCeilingSampleSet WallMid{ TEXT("WallMid"), TEXT("CeilCentre"), FColor::Magenta, {} };
		const double MidBelows[] = { 70., 80., 90. };
		AlongBothWalls(WallMid, Zero, MidBelows);
		// Ceiling 4–6 cm from the walls: inside the ~32 px short-range AO band of the seam.
		FCeilingSampleSet CeilSeam{ TEXT("CeilSeam"), TEXT("CeilCentre"), FColor::Green, {} };
		const double SeamOffsets[] = { 4., 6. };
		AlongBothWalls(CeilSeam, SeamOffsets, Zero);
		// Ceiling 4–10 cm from both walls: the ceiling side of the upper corner.
		FCeilingSampleSet CeilCorner{ TEXT("CeilCorner"), TEXT("CeilCentre"), FColor::Blue, {} };
		for (double A : { 4., 7., 10. })
		{
			for (double B : { 4., 7., 10. })
			{
				CeilCorner.Points.Add(FVector(Min.X + A, Min.Y + B, CeilZ));
			}
		}
		// Walls 4–8 cm below the ceiling: the wall side of the seam.
		FCeilingSampleSet WallTop{ TEXT("WallTop"), TEXT("WallMid"), FColor::Yellow, {} };
		const double TopBelows[] = { 4., 6., 8. };
		AlongBothWalls(WallTop, Zero, TopBelows);
		// Walls 4–10 cm below the ceiling and 4–10 cm from the vertical corner: the wall side of the upper corner.
		FCeilingSampleSet WallCorner{ TEXT("WallCorner"), TEXT("WallMid"), FColor::Cyan, {} };
		const double CornerBelows[] = { 4., 7., 10. };
		NearVerticalCorner(WallCorner, CornerBelows);
		// The vertical wall-to-wall corner at mid height: a concave edge away from the ceiling, for comparison.
		FCeilingSampleSet VertCorner{ TEXT("VertCorner"), TEXT("WallMid"), FColor::Orange, {} };
		NearVerticalCorner(VertCorner, MidBelows);
		// Close cameras do not see the ceiling centre: the ceiling 40–60 cm from both walls is the corner's local reference.
		FCeilingSampleSet CeilNear{ TEXT("CeilNear"), TEXT("CeilCentre"), FColor::Transparent, {} };
		for (double A : { 40., 50., 60. })
		{
			for (double B : { 40., 50., 60. })
			{
				CeilNear.Points.Add(FVector(Min.X + A, Min.Y + B, CeilZ));
			}
		}
		FCeilingSampleSet CeilCornerNear{ TEXT("CeilCornerLocal"), TEXT("CeilNear"), FColor::Transparent, CeilCorner.Points }; // the ceiling corner again
		Run.SampleSets = { CeilCentre, WallMid, CeilSeam, CeilCorner, WallTop, WallCorner, VertCorner, CeilNear, CeilCornerNear };

		// Profiles: ceiling by distance from the walls, walls by distance below the ceiling.
		for (double D : { 4., 8., 16., 32., 64., 128. })
		{
			FCeilingSampleSet& Set = Run.SampleSets.Add_GetRef({ FString::Printf(TEXT("CeilD%.0f"), D), TEXT("CeilCentre"), FColor::Transparent, {} });
			const double Offsets[] = { D };
			AlongBothWalls(Set, Offsets, Zero);
		}
		for (double D : { 2., 4., 8., 16., 32., 64. })
		{
			FCeilingSampleSet& Set = Run.SampleSets.Add_GetRef({ FString::Printf(TEXT("WallD%.0f"), D), TEXT("WallMid"), FColor::Transparent, {} });
			const double Belows[] = { D };
			AlongBothWalls(Set, Zero, Belows);
		}
	}

	// ── The scene around the room (what a level adds to the planner's own lighting), logged as "[CeilingCorner] scene" ──

	/** The fields a post-process block overrides, "Name=Value, ...", and its post-process materials. */
	FString DescribePostProcessOverrides(const FPostProcessSettings& Settings)
	{
		TArray<FString> Parts;
		const UScriptStruct* Struct = FPostProcessSettings::StaticStruct();
		for (TFieldIterator<FBoolProperty> It(Struct); It; ++It)
		{
			const FString FlagName = It->GetName();
			if (!FlagName.StartsWith(TEXT("bOverride_")) || !It->GetPropertyValue_InContainer(&Settings)) continue;
			const FString Name = FlagName.RightChop(10);
			FString Text = TEXT("?");
			if (const FProperty* Value = Struct->FindPropertyByName(FName(*Name)))
			{
				Text.Reset();
				Value->ExportTextItem_InContainer(Text, &Settings, nullptr, nullptr, PPF_None);
				if (Text.Len() > 70) Text = Text.Left(67) + TEXT("...");
			}
			Parts.Add(FString::Printf(TEXT("%s=%s"), *Name, *Text));
		}
		for (const FWeightedBlendable& Blendable : Settings.WeightedBlendables.Array)
		{
			Parts.Add(FString::Printf(TEXT("PostProcessMaterial(%.2f)=%s"), Blendable.Weight, Blendable.Object ? *Blendable.Object->GetPathName() : TEXT("None")));
		}
		return Parts.Num() > 0 ? FString::Join(Parts, TEXT(", ")) : TEXT("(none)");
	}

	/** The renderer's walk at a camera position: every enabled volume that holds the point (unbound = always), in blend order. */
	void LogCeilingRenderPostProcess(UWorld* World, const FVector& CamLoc)
	{
		UE_LOG(LogTemp, Display, TEXT("[CeilingCorner] scene: post-process volumes at the camera (%.0f, %.0f, %.0f), in blend order (the last to override a setting wins):"),
			CamLoc.X, CamLoc.Y, CamLoc.Z);
		for (IInterface_PostProcessVolume* Vol : World->PostProcessVolumes)
		{
			if (!Vol) continue;
			const FPostProcessVolumeProperties Props = Vol->GetProperties();
			float Dist = 0.f;
			const bool bEncompasses = Props.bIsUnbound || Vol->EncompassesPoint(CamLoc, 0.f, &Dist);
			const bool bApplies = Props.bIsEnabled && bEncompasses && Props.BlendWeight > 0.f;
			const UObject* Obj = Cast<UObject>(Vol);
			FString Name = Obj ? Obj->GetPathName() : TEXT("?");
			FString Where;
			if (const UActorComponent* Comp = Cast<UActorComponent>(Obj))
			{
				Name = FString::Printf(TEXT("%s (component on %s)"), *Comp->GetName(), Comp->GetOwner() ? *Comp->GetOwner()->GetName() : TEXT("?"));
			}
			else if (const AActor* Actor = Cast<AActor>(Obj))
			{
				Name = FString::Printf(TEXT("%s [%s, level %s]"), *Actor->GetName(), *Actor->GetActorNameOrLabel(), Actor->GetLevel() ? *Actor->GetLevel()->GetOuter()->GetName() : TEXT("?"));
				const FBox Box = Actor->GetComponentsBoundingBox(true);
				Where = FString::Printf(TEXT(" box X %.0f…%.0f Y %.0f…%.0f Z %.0f…%.0f"), Box.Min.X, Box.Max.X, Box.Min.Y, Box.Max.Y, Box.Min.Z, Box.Max.Z);
			}
			UE_LOG(LogTemp, Display, TEXT("[CeilingCorner] scene:   priority %5.1f %-7s enabled=%d unbound=%d encompasses=%d weight=%.2f %s%s | overrides: %s"),
				Props.Priority, bApplies ? TEXT("APPLIES") : TEXT("skipped"), Props.bIsEnabled ? 1 : 0, Props.bIsUnbound ? 1 : 0, bEncompasses ? 1 : 0,
				Props.BlendWeight, *Name, *Where, Props.Settings ? *DescribePostProcessOverrides(*Props.Settings) : TEXT("-"));
		}
	}

	/** The camera components of a pawn class as it spawns: C++ subobjects and Blueprint components (a child Blueprint's override first). */
	TArray<const UCameraComponent*> FindPawnClassCameras(UClass* PawnClass)
	{
		TArray<const UCameraComponent*> Cameras;
		if (!PawnClass) return Cameras;
		if (const AActor* CDO = PawnClass->GetDefaultObject<AActor>())
		{
			TInlineComponentArray<UCameraComponent*> Native;
			CDO->GetComponents(Native);
			for (const UCameraComponent* Camera : Native) Cameras.Add(Camera);
		}
		for (UClass* Class = PawnClass; Class; Class = Class->GetSuperClass())
		{
			const UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(Class);
			if (!BPClass || !BPClass->SimpleConstructionScript) continue;
			for (const USCS_Node* Node : BPClass->SimpleConstructionScript->GetAllNodes())
			{
				if (!Node || !Cast<UCameraComponent>(Node->ComponentTemplate)) continue;
				const UActorComponent* Template = Node->ComponentTemplate;
				for (UClass* Child = PawnClass; Child && Child != Class; Child = Child->GetSuperClass())
				{
					const UBlueprintGeneratedClass* ChildBP = Cast<UBlueprintGeneratedClass>(Child);
					const UActorComponent* Override = (ChildBP && ChildBP->InheritableComponentHandler)
						? ChildBP->InheritableComponentHandler->GetOverridenComponentTemplate(FComponentKey(Node)) : nullptr;
					if (Override) { Template = Override; break; }
				}
				Cameras.Add(Cast<UCameraComponent>(Template));
			}
		}
		return Cameras;
	}

	/** The pawn the level's own game mode spawns (the capture runs under GameModeBase; the planner's 3D view looks through this pawn). */
	UClass* FindLevelPawnClass(const UWorld* World)
	{
		const AWorldSettings* Settings = World ? World->GetWorldSettings() : nullptr;
		const UClass* GameMode = Settings ? Settings->DefaultGameMode.Get() : nullptr;
		const AGameModeBase* GameModeCDO = GameMode ? GameMode->GetDefaultObject<AGameModeBase>() : nullptr;
		return GameModeCDO ? GameModeCDO->DefaultPawnClass.Get() : nullptr;
	}

	/** Lights, sky, fog, post-process, the level pawn's camera and level geometry at the room, once per run. */
	void LogCeilingRenderScene(UWorld* World, const ARoomPlannerManager* Manager, const FBox2D& Clear, double CeilZ)
	{
		const FVector Centre(Clear.GetCenter().X, Clear.GetCenter().Y, CeilZ * 0.5);
		UE_LOG(LogTemp, Display, TEXT("[CeilingCorner] scene: map %s, room clear X %.0f…%.0f Y %.0f…%.0f, ceiling %.0f cm"),
			*World->GetMapName(), Clear.Min.X, Clear.Max.X, Clear.Min.Y, Clear.Max.Y, CeilZ);
		for (const ULevelStreaming* Streaming : World->GetStreamingLevels())
		{
			if (!Streaming) continue;
			UE_LOG(LogTemp, Display, TEXT("[CeilingCorner] scene: streaming level %s loaded=%d visible=%d"),
				*Streaming->GetWorldAssetPackageName(), Streaming->IsLevelLoaded() ? 1 : 0, Streaming->IsLevelVisible() ? 1 : 0);
		}

		// The level's game mode and its pawn's camera post-process (what the planner's 3D view really looks through).
		const AWorldSettings* Settings = World->GetWorldSettings();
		UClass* PawnClass = FindLevelPawnClass(World);
		UE_LOG(LogTemp, Display, TEXT("[CeilingCorner] scene: level game mode %s, pawn %s (running under %s)"),
			Settings && Settings->DefaultGameMode ? *Settings->DefaultGameMode->GetPathName() : TEXT("(project default)"),
			PawnClass ? *PawnClass->GetPathName() : TEXT("?"), World->GetAuthGameMode() ? *World->GetAuthGameMode()->GetClass()->GetName() : TEXT("?"));
		for (const UCameraComponent* Camera : FindPawnClassCameras(PawnClass))
		{
			if (!Camera) continue;
			UE_LOG(LogTemp, Display, TEXT("[CeilingCorner] scene:   pawn camera %s FOV %.1f at %s from its parent, auto-activate %d, post-process weight %.2f | overrides: %s"),
				*Camera->GetName(), Camera->FieldOfView, *Camera->GetRelativeLocation().ToCompactString(), Camera->bAutoActivate ? 1 : 0, Camera->PostProcessBlendWeight,
				*DescribePostProcessOverrides(Camera->PostProcessSettings));
		}

		// Lights, sky and fog. Local lights only when their range reaches the room.
		int32 FarLocalLights = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			const AActor* Actor = *It;
			TInlineComponentArray<UActorComponent*> Components;
			Actor->GetComponents(Components);
			for (const UActorComponent* Component : Components)
			{
				const USceneComponent* Scene = Cast<USceneComponent>(Component);
				if (!Scene || !Scene->IsRegistered()) continue;
				const FString Owner = FString::Printf(TEXT("%s [%s]"), *Actor->GetName(), *Actor->GetActorNameOrLabel());
				const int32 Visible = (Scene->IsVisible() && !Actor->IsHidden()) ? 1 : 0;
				if (const UDirectionalLightComponent* Sun = Cast<UDirectionalLightComponent>(Scene))
				{
					UE_LOG(LogTemp, Display, TEXT("[CeilingCorner] scene: directional light %s visible=%d affects world=%d mobility=%d intensity %.2f lux, rotation %s, casts shadows=%d, atmosphere sun=%d, indirect %.2f"),
						*Owner, Visible, Sun->bAffectsWorld ? 1 : 0, (int32)Sun->Mobility, Sun->Intensity, *Sun->GetComponentRotation().ToString(),
						Sun->CastShadows ? 1 : 0, Sun->IsUsedAsAtmosphereSunLight() ? 1 : 0, Sun->IndirectLightingIntensity);
				}
				else if (const USkyLightComponent* Sky = Cast<USkyLightComponent>(Scene))
				{
					UE_LOG(LogTemp, Display, TEXT("[CeilingCorner] scene: sky light %s visible=%d affects world=%d mobility=%d intensity %.2f, source %d, real-time capture=%d, lower hemisphere black=%d, casts shadows=%d, indirect %.2f"),
						*Owner, Visible, Sky->bAffectsWorld ? 1 : 0, (int32)Sky->Mobility, Sky->Intensity, (int32)Sky->SourceType, Sky->bRealTimeCapture ? 1 : 0,
						Sky->bLowerHemisphereIsBlack ? 1 : 0, Sky->CastShadows ? 1 : 0, Sky->IndirectLightingIntensity);
				}
				else if (const ULocalLightComponent* Local = Cast<ULocalLightComponent>(Scene))
				{
					const double Distance = FVector::Dist(Local->GetComponentLocation(), Centre);
					if (Local->GetOwner() && Local->GetOwner()->GetOwner() == Manager) continue; // the planner's own room light
					if (Distance > Local->AttenuationRadius + 400.) { ++FarLocalLights; continue; }
					UE_LOG(LogTemp, Display, TEXT("[CeilingCorner] scene: local light %s (%s) visible=%d affects world=%d intensity %.1f at %.0f cm from the room centre, radius %.0f"),
						*Owner, *Local->GetClass()->GetName(), Visible, Local->bAffectsWorld ? 1 : 0, Local->Intensity, Distance, Local->AttenuationRadius);
				}
				else if (const UExponentialHeightFogComponent* Fog = Cast<UExponentialHeightFogComponent>(Scene))
				{
					UE_LOG(LogTemp, Display, TEXT("[CeilingCorner] scene: height fog %s visible=%d density %.4f falloff %.3f at Z %.0f, start %.0f cm, inscattering %s, directional inscattering %s, volumetric fog=%d"),
						*Owner, Visible, Fog->FogDensity, Fog->FogHeightFalloff, Fog->GetComponentLocation().Z, Fog->StartDistance,
						*Fog->FogInscatteringLuminance.ToString(), *Fog->DirectionalInscatteringLuminance.ToString(), Fog->bEnableVolumetricFog ? 1 : 0);
				}
				else if (Component->GetClass()->GetName().Contains(TEXT("SkyAtmosphere")) || Component->GetClass()->GetName().Contains(TEXT("VolumetricCloud")))
				{
					UE_LOG(LogTemp, Display, TEXT("[CeilingCorner] scene: %s %s visible=%d"), *Component->GetClass()->GetName(), *Owner, Visible);
				}
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[CeilingCorner] scene: %d other local light(s) out of reach of the room"), FarLocalLights);

		// Level geometry at the room (the planner's own actors are left out): anything here shades or lights the room.
		const FBox Near(FVector(Clear.Min.X - 250., Clear.Min.Y - 250., -100.), FVector(Clear.Max.X + 250., Clear.Max.Y + 250., CeilZ + 300.));
		int32 Listed = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			const AActor* Actor = *It;
			if (Actor == Manager || Actor->GetOwner() == Manager || Actor->IsA<APawn>() || Actor->IsA<ACameraActor>() || Actor->IsA<APostProcessVolume>()) continue;
			bool bHasPrimitive = false;
			TInlineComponentArray<UPrimitiveComponent*> Primitives;
			Actor->GetComponents(Primitives);
			for (const UPrimitiveComponent* Primitive : Primitives)
			{
				if (Primitive && Primitive->IsRegistered() && Primitive->IsVisible() && !Actor->IsHidden()) { bHasPrimitive = true; break; }
			}
			if (!bHasPrimitive) continue;
			const FBox Box = Actor->GetComponentsBoundingBox(true);
			if (!Box.IsValid || !Box.Intersect(Near)) continue;
			if (++Listed > 60) continue;
			UE_LOG(LogTemp, Display, TEXT("[CeilingCorner] scene: level geometry %s [%s] (%s) box X %.0f…%.0f Y %.0f…%.0f Z %.0f…%.0f"),
				*Actor->GetName(), *Actor->GetActorNameOrLabel(), *Actor->GetClass()->GetName(), Box.Min.X, Box.Max.X, Box.Min.Y, Box.Max.Y, Box.Min.Z, Box.Max.Z);
		}
		UE_LOG(LogTemp, Display, TEXT("[CeilingCorner] scene: %d level actor(s) with visible geometry within 2.5 m of the room"), Listed);
	}

	/** Puts the capture camera where the variant (or the run) wants it and logs the post-process volumes there once per position. */
	void PlaceCeilingRenderCamera(FCeilingRenderRun& Run, const FCeilingRenderVariant& Variant)
	{
		ACameraActor* Camera = Run.Camera.Get();
		UWorld* World = Run.World.Get();
		if (!Camera || !World) return;
		const FVector EyeOffset = Variant.Eye.Get(Run.BaseEye);
		const FVector TargetOffset = Variant.Target.Get(Run.BaseTarget);
		Run.bDefaultView = Run.bBaseViewDefault && !Variant.Eye.IsSet() && !Variant.Target.IsSet() && !Variant.Fov.IsSet();
		const float Fov = Variant.Fov.Get(80.f); // AAwsTutorial_PlayerController::PlannerPerspectiveFOV
		Camera->GetCameraComponent()->SetFieldOfView(Fov);
		const FVector Eye(Run.CornerMin.X + EyeOffset.X, Run.CornerMin.Y + EyeOffset.Y, EyeOffset.Z);
		const FVector Target(Run.CornerMin.X + TargetOffset.X, Run.CornerMin.Y + TargetOffset.Y, TargetOffset.Z);
		Camera->SetActorLocationAndRotation(Eye, (Target - Eye).Rotation());
		Run.ViewLabel = FString::Printf(TEXT("%.0f;%.0f;%.0f>%.0f;%.0f;%.0f@fov%.0f"), EyeOffset.X, EyeOffset.Y, EyeOffset.Z, TargetOffset.X, TargetOffset.Y, TargetOffset.Z, Fov);
		if (!Run.LastLoggedEye.IsSet() || !Run.LastLoggedEye->Equals(Eye, 1.))
		{
			LogCeilingRenderPostProcess(World, Eye);
			Run.LastLoggedEye = Eye;
		}
	}

	void ApplyCeilingRenderVariant(FCeilingRenderRun& Run, const FCeilingRenderVariant& Variant)
	{
		PlaceCeilingRenderCamera(Run, Variant);
		for (const TPair<FString, FString>& Pair : Variant.WorldFields)
		{
			const bool bOn = Pair.Value.ToBool();
			if (Pair.Key.Equals(TEXT("PostProcessVolumes"), ESearchCase::IgnoreCase))
			{
				if (bOn) continue; // on is how the level is
				for (TActorIterator<APostProcessVolume> It(Run.World.Get()); It; ++It)
				{
					if (!It->bEnabled) continue;
					It->bEnabled = false; // in memory only, for this variant
					Run.DisabledVolumes.Add(*It);
				}
			}
			else if (Pair.Key.Equals(TEXT("PawnCameraPP"), ESearchCase::IgnoreCase))
			{
				UCameraComponent* Capture = Run.Camera.IsValid() ? Run.Camera->GetCameraComponent() : nullptr;
				const TArray<const UCameraComponent*> PawnCameras = FindPawnClassCameras(FindLevelPawnClass(Run.World.Get()));
				const UCameraComponent* PawnCamera = PawnCameras.Num() > 0 ? PawnCameras[0] : nullptr;
				if (!bOn || !Capture) continue;
				if (!PawnCamera)
				{
					Run.Errors.Add(FString::Printf(TEXT("%s: the level's pawn has no camera"), *Variant.Label));
					continue;
				}
				Capture->PostProcessSettings = PawnCamera->PostProcessSettings;
				Capture->PostProcessBlendWeight = PawnCamera->PostProcessBlendWeight;
				Run.bPawnCameraPP = true;
			}
			else
			{
				Run.Errors.Add(FString::Printf(TEXT("%s: unknown scene switch world.%s"), *Variant.Label, *Pair.Key));
			}
		}
		for (const TPair<FString, FString>& Pair : Variant.CVars)
		{
			IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Pair.Key);
			if (!CVar)
			{
				Run.Errors.Add(FString::Printf(TEXT("%s: unknown console variable %s"), *Variant.Label, *Pair.Key));
				continue;
			}
			FCeilingRenderCVarPatch& Patch = Run.ActivePatches.AddDefaulted_GetRef();
			Patch.CVar = CVar;
			Patch.SavedValue = CVar->GetString();
			Patch.bSavedWasConsole = (CVar->GetFlags() & ECVF_SetByMask) == ECVF_SetByConsole;
			CVar->Set(*Pair.Value, ECVF_SetByConsole);
		}
		ARoomPlannerManager* Manager = Run.Manager.Get();
		if (Manager && Variant.LightFields.Num() > 0)
		{
			FPlannerRoomLightSettings Settings = Run.SavedLightSettings;
			for (const TPair<FString, FString>& Pair : Variant.LightFields)
			{
				const FProperty* Prop = FPlannerRoomLightSettings::StaticStruct()->FindPropertyByName(FName(*Pair.Key));
				if (!Prop || !Prop->ImportText_InContainer(*Pair.Value, &Settings, nullptr, PPF_None))
				{
					Run.Errors.Add(FString::Printf(TEXT("%s: cannot set room light field %s = %s"), *Variant.Label, *Pair.Key, *Pair.Value));
				}
			}
			Manager->SetRoomLightSettings(Settings);
		}
	}

	void RevertCeilingRenderVariant(FCeilingRenderRun& Run, const FCeilingRenderVariant& Variant)
	{
		for (int32 I = Run.ActivePatches.Num() - 1; I >= 0; --I)
		{
			Run.ActivePatches[I].Restore();
		}
		Run.ActivePatches.Reset();
		for (const TWeakObjectPtr<APostProcessVolume>& Volume : Run.DisabledVolumes)
		{
			if (Volume.IsValid()) Volume->bEnabled = true;
		}
		Run.DisabledVolumes.Reset();
		if (Run.bPawnCameraPP)
		{
			if (UCameraComponent* Capture = Run.Camera.IsValid() ? Run.Camera->GetCameraComponent() : nullptr)
			{
				Capture->PostProcessSettings = FPostProcessSettings();
				Capture->PostProcessBlendWeight = GetDefault<UCameraComponent>()->PostProcessBlendWeight;
			}
			Run.bPawnCameraPP = false;
		}
		ARoomPlannerManager* Manager = Run.Manager.Get();
		if (Manager && Variant.LightFields.Num() > 0)
		{
			Manager->SetRoomLightSettings(Run.SavedLightSettings);
		}
	}

	FString CeilingRenderTableHeader(const FCeilingRenderRun& Run)
	{
		FString Header = TEXT("variant\tceil_centre_lum\tceil_centre_srgb");
		for (const FCeilingSampleSet& Set : Run.SampleSets)
		{
			if (!Set.Reference.IsEmpty()) Header += FString::Printf(TEXT("\t%s/%s"), *Set.Name, *Set.Reference);
		}
		return Header + TEXT("\tMBA\tSRAO\tLightingMode\tCeilingOffsetCm\tIndirectIntensity\tLightZ\tEmittedLm\tResolution\tView");
	}

	/** Measures and writes one capture (called from the viewport's screenshot delegate). */
	void HandleCeilingCapture(FCeilingRenderRun& Run, int32 Width, int32 Height, const TArray<FColor>& Bitmap)
	{
		if (!Run.Variants.IsValidIndex(Run.CurrentVariant) || Run.bCaptured) return;
		const FCeilingRenderVariant& Variant = Run.Variants[Run.CurrentVariant];
		APlayerController* PC = Run.Controller.Get();
		UWorld* World = Run.World.Get();

		// Projections are in viewport pixels; the capture is the viewport's render target.
		FVector2D PixelScale(1., 1.);
		if (World && World->GetGameViewport() && World->GetGameViewport()->Viewport)
		{
			const FIntPoint ViewSize = World->GetGameViewport()->Viewport->GetSizeXY();
			if (ViewSize.X > 0 && ViewSize.Y > 0)
			{
				PixelScale = FVector2D((double)Width / ViewSize.X, (double)Height / ViewSize.Y);
			}
		}

		TArray<FColor> Annotated;
		const bool bAnnotate = Run.CurrentVariant == 0;
		if (bAnnotate) Annotated = Bitmap;

		TMap<FString, double> Absolute; // sets with no sample on screen are left out
		for (const FCeilingSampleSet& Set : Run.SampleSets)
		{
			TArray<FIntPoint> Pixels;
			const bool bMark = bAnnotate && Set.Marker.A > 0;
			const FCeilingSampleResult Result = MeasurePoints(PC, Set.Points, Width, Height, PixelScale, Bitmap, bMark ? &Pixels : nullptr);
			if (Result.Samples > 0) Absolute.Add(Set.Name, Result.MeanLuminance);
			if (Result.Samples < Set.Points.Num() / 2)
			{
				// Expected for some places from a custom camera (e.g. the ceiling centre from close to the corner).
				(Run.bDefaultView ? Run.Errors : Run.Infos).Add(FString::Printf(TEXT("%s: only %d of %d %s samples are on screen"),
					*Variant.Label, Result.Samples, Set.Points.Num(), *Set.Name));
			}
			for (const FIntPoint& P : Pixels)
			{
				for (int32 DY = -1; DY <= 1; ++DY)
				{
					for (int32 DX = -1; DX <= 1; ++DX)
					{
						if (DX == 0 && DY == 0) continue; // the ring shows where the box is, the centre pixel stays readable
						Annotated[(P.Y + DY) * Width + (P.X + DX)] = Set.Marker;
					}
				}
			}
		}

		const double Centre = Absolute.FindRef(TEXT("CeilCentre"));
		const FColor CentreSRGB = FLinearColor((float)Centre, (float)Centre, (float)Centre).ToFColor(true);
		TMap<FString, double>& Values = Run.Metrics.FindOrAdd(Variant.Label);
		Values.Add(TEXT("CeilCentre"), Centre);
		Values.Add(TEXT("WallMidAbsolute"), Absolute.FindRef(TEXT("WallMid")));
		Values.Add(TEXT("DefaultView"), Run.bDefaultView ? 1. : 0.);
		FString Row = FString::Printf(TEXT("%s\t%.4f\t%d"), *Variant.Label, Centre, CentreSRGB.R);
		FString Summary;
		for (const FCeilingSampleSet& Set : Run.SampleSets)
		{
			if (Set.Reference.IsEmpty()) continue;
			// -1 = not measurable from this camera (the place or its reference is off screen).
			const double Value = Absolute.Contains(Set.Name) && Absolute.Contains(Set.Reference)
				? Absolute[Set.Name] / FMath::Max(Absolute[Set.Reference], 1e-6) : -1.;
			Values.Add(Set.Name, Value);
			Row += FString::Printf(TEXT("\t%.3f"), Value);
			Summary += FString::Printf(TEXT(" %s %.3f"), *Set.Name, Value);
		}

		// What the frame was rendered with.
		const ARoomPlannerManager* Manager = Run.Manager.Get();
		const FPlannerRoomLightSettings& Light = Manager ? Manager->RoomLightSettings : Run.SavedLightSettings;
		float LightZ = 0.f, EmittedLm = 0.f;
		if (Manager && Manager->GetRoomsForDebug().Num() > 0)
		{
			if (const APlannerRoomLightActor* RoomLight = Manager->GetRoomLight(Manager->GetRoomsForDebug().CreateConstIterator()->Key))
			{
				LightZ = RoomLight->Light ? (float)RoomLight->Light->GetComponentLocation().Z : 0.f;
				EmittedLm = RoomLight->EmittedLumens;
			}
		}
		const float MBA = ReadCVarFloat(TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO.MaxMultibounceAlbedo"));
		const float SRAO = ReadCVarFloat(TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO"));
		const float LightingMode = ReadCVarFloat(TEXT("r.Lumen.HardwareRayTracing.LightingMode"));
		Row += FString::Printf(TEXT("\t%.2f\t%.0f\t%.0f\t%.1f\t%.2f\t%.1f\t%.0f\t%dx%d\t%s"), MBA, SRAO, LightingMode, Light.CeilingOffsetCm, Light.IndirectIntensity, LightZ, EmittedLm,
			Width, Height, *Run.ViewLabel);
		Run.TableRows.Add(Row);
		Run.Infos.Add(FString::Printf(TEXT("[CeilingCorner] %s %dx%d view %s centre %.4f (sRGB %d) |%s | MBA %.2f SRAO %.0f LightingMode %.0f offset %.1f indirect %.2f light Z %.1f %.0f lm"),
			*Variant.Label, Width, Height, *Run.ViewLabel, Centre, CentreSRGB.R, *Summary, MBA, SRAO, LightingMode, Light.CeilingOffsetCm, Light.IndirectIntensity, LightZ, EmittedLm));

		auto WritePng = [&](const TArray<FColor>& Pixels, const FString& Name)
		{
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
			const FString File = FPaths::Combine(Run.OutDir, Name);
			if (!FFileHelper::SaveArrayToFile(Png, *File))
			{
				Run.Errors.Add(FString::Printf(TEXT("cannot write %s"), *File));
			}
		};
		WritePng(Bitmap, FString::Printf(TEXT("CeilingCorner_%02d_%s.png"), Run.CurrentVariant, *Variant.Label));
		if (bAnnotate)
		{
			WritePng(Annotated, TEXT("CeilingCorner_samples.png"));
		}
		Run.bCaptured = true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerCeilingCornerRenderTest, "MaxiMall.Planner.Render.CeilingCorner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FPlannerCeilingCornerRenderTest::RunTest(const FString& Parameters)
{
	// Without -PlannerSnapshotDir nobody asked for renders: skipped quietly. With it, every skip is reported as a warning, so a run
	// that renders nothing cannot pass for one that rendered.
	FString OutDir;
	if (!FParse::Value(FCommandLine::Get(), TEXT("PlannerSnapshotDir="), OutDir))
	{
		AddInfo(TEXT("Ceiling corner render skipped (needs a GPU and -PlannerSnapshotDir=<folder>)."));
		return true;
	}
	if (!FApp::CanEverRender())
	{
		AddWarning(TEXT("Ceiling corner render skipped although -PlannerSnapshotDir is given: this session cannot render (-nullrhi?)."));
		return true;
	}
	UWorld* World = AutomationCommon::GetAnyGameWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!World || !World->GetGameViewport() || !PC)
	{
		AddWarning(TEXT("Ceiling corner render skipped although -PlannerSnapshotDir is given: it needs a game viewport (run it under -game, see PlannerRenderTests.cpp)."));
		return true;
	}

	TSharedRef<FCeilingRenderRun> Run = MakeShared<FCeilingRenderRun>();
	Run->OutDir = OutDir;
	IFileManager::Get().MakeDirectory(*OutDir, true);
	FString Spec = DefaultCeilingRenderVariants;
	FParse::Value(FCommandLine::Get(), TEXT("PlannerRenderVariants="), Spec, false);
	Run->Variants = ParseCeilingRenderVariants(Spec);
	FParse::Value(FCommandLine::Get(), TEXT("PlannerRenderSettle="), Run->SettleSeconds);
	Run->SettleSeconds = FMath::Clamp(Run->SettleSeconds, 1.f, 60.f);
	if (!TestTrue(TEXT("At least one variant"), Run->Variants.Num() > 0)) return false;

	// A 4 × 4 m room drawn through the real manager path, entered in 3D exactly as the planner widget does.
	ARoomPlannerManager* Manager = nullptr;
	for (TActorIterator<ARoomPlannerManager> It(World); It; ++It) { Manager = *It; break; }
	Run->bCreatedManager = Manager == nullptr;
	Manager = ARoomPlannerManager::GetOrCreateInstance(World);
	if (!TestNotNull(TEXT("Planner manager"), Manager)) return false;
	if (Manager->GetWallSegmentsForDebug().Num() > 0)
	{
		AddWarning(TEXT("Ceiling corner render skipped although -PlannerSnapshotDir is given: the planner in this world already holds a layout."));
		return true;
	}
	// A setup failure before the capture steps leaves the world as it was found, as their last step does.
	auto Abandon = [Manager, bCreatedManager = Run->bCreatedManager]()
	{
		Manager->SetPlannerSessionActive(false);
		Manager->SetViewMode(true);
		Manager->ClearLayout();
		if (bCreatedManager) Manager->Destroy();
		return false;
	};
	// -PlannerRenderRoomAt=planner|X,Y: the planner's own 4 × 4 m preset where the planner puts the player in this map (or at X,Y).
	FString RoomAt;
	if (FParse::Value(FCommandLine::Get(), TEXT("PlannerRenderRoomAt="), RoomAt, false))
	{
		FVector2D RoomCentre = FVector2D::ZeroVector;
		if (RoomAt.Equals(TEXT("planner"), ESearchCase::IgnoreCase))
		{
			// As URoomPlannerWidget opens: its PlannerRelocationLocation, moved to a spot free of the level's geometry.
			FVector Relocation(-10000., 0., 0.);
			if (const UClass* WidgetClass = LoadClass<URoomPlannerWidget>(nullptr, TEXT("/Game/RoomPlanner/WBP_RoomPlannerWidget.WBP_RoomPlannerWidget_C")))
			{
				Relocation = WidgetClass->GetDefaultObject<URoomPlannerWidget>()->PlannerRelocationLocation;
			}
			const FVector Spot = AAwsTutorial_PlayerController::FindNonOverlappingPlannerSpot(World, PC->GetPawn(), Relocation);
			RoomCentre = FVector2D(Spot.X, Spot.Y);
			AddInfo(FString::Printf(TEXT("Room at the planner spot: relocation (%.0f, %.0f, %.0f) → free spot (%.0f, %.0f, %.0f)"),
				Relocation.X, Relocation.Y, Relocation.Z, Spot.X, Spot.Y, Spot.Z));
		}
		else
		{
			TArray<FString> Parts;
			if (!TestEqual(TEXT("-PlannerRenderRoomAt is planner or X,Y"), RoomAt.ParseIntoArray(Parts, TEXT(","), true), 2)) return Abandon();
			RoomCentre = FVector2D(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]));
		}
		Manager->BuildPreset4x4mRoom(RoomCentre); // what the planner's preset button builds (Server_BuildPreset4x4mRoom)
	}
	else
	{
		const double S = 400.;
		Manager->AddWallBetweenPoints(FVector2D(0., 0.), FVector2D(S, 0.));
		Manager->AddWallBetweenPoints(FVector2D(S, 0.), FVector2D(S, S));
		Manager->AddWallBetweenPoints(FVector2D(S, S), FVector2D(0., S));
		Manager->AddWallBetweenPoints(FVector2D(0., S), FVector2D(0., 0.));
		Manager->RebuildRooms();
	}
	if (!TestEqual(TEXT("One room"), Manager->GetRoomsForDebug().Num(), 1)) return Abandon();
	const FRoomData& Room = Manager->GetRoomsForDebug().CreateConstIterator()->Value;
	FBox2D Clear(ForceInit);
	for (const FVector2D& P : (Room.NetFloorPolygon.Num() >= 3 ? Room.NetFloorPolygon : Room.FloorPolygon)) Clear += P;
	const double CeilZ = Room.CeilingHeightCm;
	Run->SavedLightSettings = Manager->RoomLightSettings;
	Manager->SetPlannerSessionActive(true);
	Manager->SetViewMode(false);
	TestEqual(TEXT("The room has its one light"), Manager->GetCeilingLightCount(), 1);

	// Camera: planner FOV, inside the room, looking up into the corner at the smallest X / Y.
	// -PlannerRenderEye=X,Y,Z / -PlannerRenderTarget=X,Y,Z move it (cm from that corner of the clear outline, Z from the floor);
	// a variant's eye= / target= move it for that variant (PlaceCeilingRenderCamera).
	auto CornerRelative = [&](const TCHAR* Switch, const FVector& Default)
	{
		FVector Offset = Default;
		FString Text;
		TArray<FString> Parts;
		if (FParse::Value(FCommandLine::Get(), Switch, Text, false) && Text.ParseIntoArray(Parts, TEXT(","), true) == 3)
		{
			Offset = FVector(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2]));
			Run->bBaseViewDefault = false;
		}
		return Offset;
	};
	Run->CornerMin = Clear.Min;
	Run->BaseEye = CornerRelative(TEXT("PlannerRenderEye="), FVector(320., 320., 130.));
	Run->BaseTarget = CornerRelative(TEXT("PlannerRenderTarget="), FVector(50., 50., CeilZ - 18.));
	Run->bDefaultView = Run->bBaseViewDefault;
	const FVector Eye(Clear.Min.X + Run->BaseEye.X, Clear.Min.Y + Run->BaseEye.Y, Run->BaseEye.Z);
	const FVector Target(Clear.Min.X + Run->BaseTarget.X, Clear.Min.Y + Run->BaseTarget.Y, Run->BaseTarget.Z);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACameraActor* Camera = World->SpawnActor<ACameraActor>(Eye, (Target - Eye).Rotation(), Params);
	if (!TestNotNull(TEXT("Camera"), Camera)) return Abandon();
	Camera->GetCameraComponent()->SetFieldOfView(80.f); // AAwsTutorial_PlayerController::PlannerPerspectiveFOV
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	if (APawn* Pawn = PC->GetPawn())
	{
		Pawn->SetActorHiddenInGame(true);
		Pawn->SetActorEnableCollision(false);
	}
	PC->SetViewTarget(Camera);
	GEngine->Exec(World, TEXT("DisableAllScreenMessages"));

	// The player controller forces Epic scalability for the planner: do the same, and put it back afterwards.
	Run->SavedQuality = Scalability::GetQualityLevels();
	{
		Scalability::FQualityLevels Epic = Run->SavedQuality;
		Epic.SetFromSingleQualityLevel(3);
		Scalability::SetQualityLevels(Epic);
	}

	Run->World = World;
	Run->Manager = Manager;
	Run->Camera = Camera;
	Run->Controller = PC;
	BuildCeilingSamples(*Run, Clear.Min, Clear.Max, CeilZ);
	Run->CaptureHandle = UGameViewportClient::OnScreenshotCaptured().AddLambda([Run](int32 Width, int32 Height, const TArray<FColor>& Bitmap)
	{
		HandleCeilingCapture(*Run, Width, Height, Bitmap);
	});
	AddInfo(FString::Printf(TEXT("Ceiling corner render: %d variant(s), %.0f s settling each, room %.0f × %.0f cm clear (X %.0f…%.0f, Y %.0f…%.0f), ceiling %.0f cm, map %s, into %s"),
		Run->Variants.Num(), Run->SettleSeconds, Clear.GetSize().X, Clear.GetSize().Y, Clear.Min.X, Clear.Max.X, Clear.Min.Y, Clear.Max.Y, CeilZ,
		*World->GetMapName(), *OutDir));
	LogCeilingRenderScene(World, Manager, Clear, CeilZ);

	// Shaders and materials first (a fresh -game session compiles what the planner uses), then a first settle.
	ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]()
	{
		static double QuietSince = 0.;
		const bool bBusy = GShaderCompilingManager && GShaderCompilingManager->IsCompiling();
		const double Now = FPlatformTime::Seconds();
		if (bBusy || QuietSince == 0.) { QuietSince = bBusy ? 0. : Now; return false; }
		return Now - QuietSince > 2.;
	}, []() { UE_LOG(LogTemp, Warning, TEXT("[CeilingCorner] shaders still compiling after 900 s; capturing anyway.")); return true; }, 900.f));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.f));

	for (int32 Index = 0; Index < Run->Variants.Num(); ++Index)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Run, Index]()
		{
			Run->CurrentVariant = Index;
			Run->bCaptured = false;
			ApplyCeilingRenderVariant(*Run, Run->Variants[Index]);
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(Run->SettleSeconds));
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Run, Index]()
		{
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(Run->OutDir, TEXT("CeilingCorner_unused.png")), false, false);
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([Run]() { return Run->bCaptured; },
			[Run, Index]() { Run->Errors.Add(FString::Printf(TEXT("%s: no capture within 20 s"), *Run->Variants[Index].Label)); return true; }, 20.f));
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Run, Index]()
		{
			RevertCeilingRenderVariant(*Run, Run->Variants[Index]);
			return true;
		}));
	}

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run]()
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(Run->CaptureHandle);
		for (const FString& Info : Run->Infos) AddInfo(Info);
		for (const FString& Error : Run->Errors) AddError(Error);

		const FString Table = CeilingRenderTableHeader(*Run) + TEXT("\n") + FString::Join(Run->TableRows, TEXT("\n")) + TEXT("\n");
		FFileHelper::SaveStringToFile(Table, *FPaths::Combine(Run->OutDir, TEXT("CeilingCorner_metrics.tsv")));
		UE_LOG(LogTemp, Display, TEXT("[CeilingCorner] metrics\n%s"), *Table);

		// The capture route really renders the lit room: neither black nor clipped, walls lit as well.
		if (const TMap<FString, double>* First = Run->Metrics.Find(Run->Variants[0].Label))
		{
			const double Centre = First->FindRef(TEXT("CeilCentre"));
			const double Wall = First->FindRef(TEXT("WallMidAbsolute"));
			const bool bFirstDefaultView = First->FindRef(TEXT("DefaultView")) > 0.5;
			TestTrue(FString::Printf(TEXT("The room is lit (walls %.4f)"), Wall), Wall > 0.02 && Wall < 0.95);
			if (bFirstDefaultView)
			{
				TestTrue(FString::Printf(TEXT("The ceiling is lit (%.4f)"), Centre), Centre > 0.01 && Centre < 0.95);
			}

			// The current planner, seen from the default camera: no dark pinch at the ceiling seam and in the upper corner.
			const FCeilingRenderVariant& FirstVariant = Run->Variants[0];
			if (bFirstDefaultView && FirstVariant.CVars.Num() == 0 && FirstVariant.LightFields.Num() == 0 && FirstVariant.WorldFields.Num() == 0)
			{
				const double CeilCorner = First->FindRef(TEXT("CeilCorner"));
				const double WallTop = First->FindRef(TEXT("WallTop"));
				const double WallCorner = First->FindRef(TEXT("WallCorner"));
				TestTrue(FString::Printf(TEXT("Ceiling corner %.3f of the ceiling centre (at least %.2f)"), CeilCorner, MinCeilCornerRatio), CeilCorner >= MinCeilCornerRatio);
				TestTrue(FString::Printf(TEXT("Top of the walls %.3f of mid-wall (at least %.2f)"), WallTop, MinWallTopRatio), WallTop >= MinWallTopRatio);
				TestTrue(FString::Printf(TEXT("Walls at the upper corner %.3f of mid-wall (at least %.2f)"), WallCorner, MinWallCornerRatio), WallCorner >= MinWallCornerRatio);
			}
		}
		else
		{
			AddError(TEXT("The first variant was not captured."));
		}

		// Leave the world as it was found.
		if (ARoomPlannerManager* M = Run->Manager.Get())
		{
			M->SetRoomLightSettings(Run->SavedLightSettings);
			M->SetPlannerSessionActive(false);
			M->SetViewMode(true);
			M->ClearLayout();
			if (Run->bCreatedManager) M->Destroy();
		}
		if (APlayerController* Controller = Run->Controller.Get())
		{
			if (APawn* Pawn = Controller->GetPawn())
			{
				Pawn->SetActorHiddenInGame(false);
				Pawn->SetActorEnableCollision(true);
				Controller->SetViewTarget(Pawn);
			}
		}
		if (ACameraActor* Cam = Run->Camera.Get()) Cam->Destroy();
		Scalability::SetQualityLevels(Run->SavedQuality);
		return true;
	}));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// A furnished room in the planner's 3D view (the imported catalog models), rendered for a visual check
// ─────────────────────────────────────────────────────────────────────────────
//
// Three phases in one 5 × 4 m room drawn through the real manager path, seen in the planner's 3D view as the widget enters it:
//  1. room:        the room furnished with floor drops (AddPlacedObject, as Server_PlaceCatalogItem makes them) at sensible spots;
//                  the pieces that belong against a wall are dropped there turned so their front faces the room (FurnishedFronts:
//                  which way each model's front points). Checks: every item on the floor, inside the room, none overlapping.
//                  Views Furnished_<n>_<view>.png; the brightness at each item's centre is logged (a black material reads near 0).
//  2. walldrop:    the desk, the drawer cabinet, the shelves, the chair and the armchair dropped ON walls (AddPlacedObjectOnWall, which
//                  turns each row's front, its FrontYawDeg, to the room); their yaw is checked and their depth off the wall logged.
//                  Views Furnished_<n>_walldrop_*.png.
//  3. orientation: each model alone at yaw 0 in the middle of the room, seen from +X, +Y, -X and -Y (Orientation_<Row>.png, a 2 × 2
//                  sheet in that order: top left from +X, top right from +Y, bottom left from -X, bottom right from -Y).
//
// Needs a game viewport and a GPU, like CeilingCorner above: skipped unless -PlannerSnapshotDir=<folder> is given. Run it in -game
// with the same switches and -ExecCmds="Automation RunTests MaxiMall.Planner.Render.FurnishedRoom; Quit".
// Optional: -PlannerRenderSettle=<seconds> (Lumen and streaming settling time per room view, default 5; orientation views take half).

namespace
{
	/** Which way a catalog model's front points in its own frame (degrees about Z: 0 = +X, 90 = +Y, 180 = -X, -90 = -Y). */
	struct FFurnishedFront
	{
		const TCHAR* Row;
		float FrontYaw;
	};

	/**
	 * Read off the orientation sheets (phase 3). The chair, the armchair, the desk and the drawer cabinet face +Y in their own frame,
	 * like the catalog's Blazer sofa; the cube shelves are open front and back along X; the two tables have no front (their long sides
	 * along Y for the coffee table). DT_PlannerObjects carries the same as each row's FrontYawDeg, which the wall drop turns to the
	 * room; phase 1 checks that the two agree.
	 */
	const FFurnishedFront FurnishedFronts[] = {
		{ TEXT("Desk_Office01"), 90.f },
		{ TEXT("Chair_Dining02"), 90.f },
		{ TEXT("Shelf_Display01"), 0.f },
		{ TEXT("Cabinet_Drawer01"), 90.f },
		{ TEXT("Armchair_Modern01"), 90.f },
		{ TEXT("Table_Coffee01"), 0.f },
		{ TEXT("Table_Side01"), 0.f },
		{ TEXT("NewRow_1"), 90.f },
	};

	float FurnishedFrontYaw(const FString& Row)
	{
		for (const FFurnishedFront& Front : FurnishedFronts)
		{
			if (Row == Front.Row) return Front.FrontYaw;
		}
		return 0.f;
	}

	/** The row's FrontYawDeg in DT_PlannerObjects (what the wall drop turns to the room); false without the table or the row. */
	bool FurnishedRowFrontYaw(const FString& Row, float& OutFrontYaw)
	{
		const UDataTable* Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/DT/DT_PlannerObjects.DT_PlannerObjects"));
		const FPlannerObjectRow* Found = Table ? Table->FindRow<FPlannerObjectRow>(FName(*Row), TEXT("FurnishedRoom"), false) : nullptr;
		if (!Found) return false;
		OutFrontYaw = Found->FrontYawDeg;
		return true;
	}

	/**
	 * An item of the furnished room. Against a wall (Wall != INDEX_NONE): a floor drop at Along cm from the wall's start, its front
	 * turned to the room and its back 1 cm off the wall's face. Free: a floor drop at (X, Y) with its front towards Facing.
	 */
	struct FFurnishedItem
	{
		const TCHAR* Row = nullptr;
		int32 Wall = INDEX_NONE; // 0 south, 1 east, 2 north, 3 west; each wall's room side is its left
		float Along = 0.f;
		double X = 0.;
		double Y = 0.;
		float Facing = 0.f;      // world direction of the front, degrees (0 = +X, 90 = +Y)
	};

	/** 5 × 4 m room of 20 cm walls from the origin: clear floor x 10…490, y 10…390. A work corner at the north wall, a lounge in the south-west. */
	const FFurnishedItem FurnishedItems[] = {
		{ TEXT("Desk_Office01"), 2, 150.f },                             // north wall (runs from x 500 to 0): centred at x 350
		{ TEXT("Chair_Dining02"), INDEX_NONE, 0.f, 350., 250., 90.f },   // pulled out from the desk, facing it
		{ TEXT("Shelf_Display01"), 3, 220.f },                           // west wall (runs from y 400 to 0): centred at y 180
		{ TEXT("Cabinet_Drawer01"), 1, 110.f },                          // east wall (runs from y 0 to 400): centred at y 110
		{ TEXT("Armchair_Modern01"), INDEX_NONE, 0.f, 105., 95., 0.f },  // facing the coffee table
		{ TEXT("Table_Coffee01"), INDEX_NONE, 0.f, 230., 95., 0.f },
		{ TEXT("Table_Side01"), INDEX_NONE, 0.f, 105., 175., 0.f },      // beside the armchair
	};

	/** Wall drops of phase 2: row, wall, distance from the wall's start. */
	const FFurnishedItem FurnishedWallDrops[] = {
		{ TEXT("Desk_Office01"), 2, 150.f },
		{ TEXT("Cabinet_Drawer01"), 1, 110.f },
		{ TEXT("Shelf_Display01"), 3, 220.f },
		{ TEXT("Chair_Dining02"), 0, 120.f },
		{ TEXT("Armchair_Modern01"), 0, 280.f },
	};

	/** Rows of the orientation sheets: the imported models and, for reference, the catalog's sofa. */
	const TCHAR* FurnishedOrientationRows[] = {
		TEXT("Chair_Dining02"), TEXT("Armchair_Modern01"), TEXT("Table_Coffee01"), TEXT("Table_Side01"),
		TEXT("Desk_Office01"), TEXT("Shelf_Display01"), TEXT("Cabinet_Drawer01"), TEXT("NewRow_1"),
	};

	struct FFurnishedRenderRun;

	/** One capture: its setup (layout and camera) runs before the settle; a Tile of 0…3 is a quarter of sheet Name. */
	struct FFurnishedShot
	{
		FString Name;
		TFunction<void(FFurnishedRenderRun&)> Setup;
		float Settle = 5.f;
		int32 Tile = INDEX_NONE;
	};

	/** State shared by the latent steps of one run. */
	struct FFurnishedRenderRun
	{
		FString OutDir;
		float SettleSeconds = 5.f;
		TArray<FFurnishedShot> Shots;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<ARoomPlannerManager> Manager;
		TWeakObjectPtr<ACameraActor> Camera;
		TWeakObjectPtr<APlayerController> Controller;
		bool bCreatedManager = false;
		bool bCeilingVisible = true; // the planner's ceiling switch as found
		Scalability::FQualityLevels SavedQuality;
		FDelegateHandle CaptureHandle;
		int32 CurrentShot = INDEX_NONE;
		bool bCaptured = false;
		int32 Walls[4] = { -1, -1, -1, -1 };
		double RoomW = 500.;
		double RoomD = 400.;
		FBox2D Clear = FBox2D(ForceInit);
		double CeilZ = 280.;
		TArray<TPair<FString, FString>> Objects; // catalog row → instance ID of what is placed now
		TArray<FColor> Sheet;                    // the orientation sheet being filled
		int32 SheetW = 0, SheetH = 0;
		TArray<TPair<FString, double>> FrameLuminance; // per captured shot
		TArray<FString> Errors;
		TArray<FString> Infos;
	};

	/** The world box of an item's mesh (its local bounds' corners through the component transform). */
	bool FurnishedMeshBox(const ARoomPlannerManager* Manager, const FString& ID, FBox& OutBox)
	{
		const APlannerPlacedObjectActor* Actor = Manager ? Manager->FindPlacedObjectActor(ID) : nullptr;
		const UStaticMeshComponent* Comp = Actor ? Actor->MeshComponent.Get() : nullptr;
		if (!Comp || !Comp->GetStaticMesh()) return false;
		const FBox Local = Comp->GetStaticMesh()->GetBoundingBox();
		const FTransform& TM = Comp->GetComponentTransform();
		OutBox = FBox(ForceInit);
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			OutBox += TM.TransformPosition(FVector((Corner & 1) ? Local.Max.X : Local.Min.X, (Corner & 2) ? Local.Max.Y : Local.Min.Y, (Corner & 4) ? Local.Max.Z : Local.Min.Z));
		}
		return true;
	}

	void ClearFurnishedObjects(FFurnishedRenderRun& Run)
	{
		if (ARoomPlannerManager* Manager = Run.Manager.Get())
		{
			for (const TPair<FString, FString>& Object : Run.Objects) Manager->RemovePlacedObject(Object.Value);
		}
		Run.Objects.Reset();
	}

	/**
	 * A wall's room-side face at Along cm from the wall's start, and its inward normal, for the room of this test (four walls drawn
	 * anticlockwise from the origin, so each wall's room side is its left).
	 */
	bool FurnishedWallFace(const FFurnishedRenderRun& Run, int32 Wall, float Along, FVector2D& OutFace, FVector2D& OutNormal)
	{
		const FBox2D& C = Run.Clear;
		switch (Wall)
		{
		case 0: OutFace = FVector2D(Along, C.Min.Y); OutNormal = FVector2D(0., 1.); return true;               // south, from (0, 0) along +X
		case 1: OutFace = FVector2D(C.Max.X, Along); OutNormal = FVector2D(-1., 0.); return true;              // east, from (W, 0) along +Y
		case 2: OutFace = FVector2D(Run.RoomW - Along, C.Max.Y); OutNormal = FVector2D(0., -1.); return true;  // north, from (W, D) along -X
		case 3: OutFace = FVector2D(C.Min.X, Run.RoomD - Along); OutNormal = FVector2D(1., 0.); return true;   // west, from (0, D) along -Y
		default: return false;
		}
	}

	void SetFurnishedCamera(FFurnishedRenderRun& Run, const FVector& Eye, const FRotator& Rotation, float Fov)
	{
		if (ACameraActor* Cam = Run.Camera.Get())
		{
			Cam->SetActorLocationAndRotation(Eye, Rotation);
			Cam->GetCameraComponent()->SetFieldOfView(Fov);
		}
	}

	/**
	 * The planner's own ceiling switch (the 3D view option): off for the plan view from above (an orthographic camera under the
	 * ceiling renders black under Lumen here), back to how the planner had it for every other shot.
	 */
	void SetFurnishedCeilingHidden(FFurnishedRenderRun& Run, bool bHidden)
	{
		if (ARoomPlannerManager* Manager = Run.Manager.Get())
		{
			const bool bVisible = !bHidden && Run.bCeilingVisible;
			if (Manager->bCeilingVisible != bVisible) Manager->SetCeilingVisibility(bVisible);
		}
	}

	/** Writes one capture (or its quarter of an orientation sheet) and logs the brightness at each item's centre. */
	void HandleFurnishedCapture(FFurnishedRenderRun& Run, int32 Width, int32 Height, const TArray<FColor>& Bitmap)
	{
		if (!Run.Shots.IsValidIndex(Run.CurrentShot) || Run.bCaptured) return;
		const FFurnishedShot& Shot = Run.Shots[Run.CurrentShot];
		const APlayerController* PC = Run.Controller.Get();
		const UWorld* World = Run.World.Get();
		const ARoomPlannerManager* Manager = Run.Manager.Get();
		FVector2D PixelScale(1., 1.);
		if (World && World->GetGameViewport() && World->GetGameViewport()->Viewport)
		{
			const FIntPoint ViewSize = World->GetGameViewport()->Viewport->GetSizeXY();
			if (ViewSize.X > 0 && ViewSize.Y > 0) PixelScale = FVector2D((double)Width / ViewSize.X, (double)Height / ViewSize.Y);
		}

		double Sum = 0.;
		for (const FColor& Pixel : Bitmap) Sum += PixelLuminance(Pixel);
		const double Frame = Bitmap.Num() > 0 ? Sum / Bitmap.Num() : 0.;
		const FString Label = Shot.Tile == INDEX_NONE ? Shot.Name : FString::Printf(TEXT("%s/%d"), *Shot.Name, Shot.Tile);
		Run.FrameLuminance.Emplace(Label, Frame);

		// Each item: the 9 × 9 pixel box at its bounds' centre, when that is in view (it may still be behind another item).
		FString Items;
		for (const TPair<FString, FString>& Object : Run.Objects)
		{
			const APlannerPlacedObjectActor* Actor = Manager ? Manager->FindPlacedObjectActor(Object.Value) : nullptr;
			FVector2D Screen;
			if (!Actor || !Actor->MeshComponent || !PC || !PC->ProjectWorldLocationToScreen(Actor->MeshComponent->Bounds.Origin, Screen, false)) continue;
			const int32 X = FMath::RoundToInt(Screen.X * PixelScale.X);
			const int32 Y = FMath::RoundToInt(Screen.Y * PixelScale.Y);
			if (X < 4 || Y < 4 || X >= Width - 4 || Y >= Height - 4) continue;
			double Box = 0.;
			for (int32 DY = -4; DY <= 4; ++DY)
			{
				for (int32 DX = -4; DX <= 4; ++DX)
				{
					Box += PixelLuminance(Bitmap[(Y + DY) * Width + (X + DX)]);
				}
			}
			Items += FString::Printf(TEXT(" %s %.3f at %d,%d;"), *Object.Key, Box / 81., X, Y);
		}
		Run.Infos.Add(FString::Printf(TEXT("[Furnished] %s %dx%d frame %.3f |%s"), *Label, Width, Height, Frame, *Items));

		auto WritePng = [&Run](int32 W, int32 H, const TArray<FColor>& Pixels, const FString& Name)
		{
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(W, H, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
			const FString File = FPaths::Combine(Run.OutDir, Name);
			if (!FFileHelper::SaveArrayToFile(Png, *File)) Run.Errors.Add(FString::Printf(TEXT("cannot write %s"), *File));
		};
		if (Shot.Tile == INDEX_NONE)
		{
			WritePng(Width, Height, Bitmap, FString::Printf(TEXT("Furnished_%02d_%s.png"), Run.CurrentShot, *Shot.Name));
		}
		else
		{
			// A quarter of the sheet, halved by 2 × 2 box averaging.
			const int32 TW = Width / 2, TH = Height / 2;
			if (Shot.Tile == 0 || Run.SheetW != TW * 2 || Run.SheetH != TH * 2)
			{
				Run.SheetW = TW * 2;
				Run.SheetH = TH * 2;
				Run.Sheet.Init(FColor::Black, Run.SheetW * Run.SheetH);
			}
			const int32 OX = (Shot.Tile % 2) * TW, OY = (Shot.Tile / 2) * TH;
			for (int32 Y = 0; Y < TH; ++Y)
			{
				for (int32 X = 0; X < TW; ++X)
				{
					int32 R = 0, G = 0, B = 0;
					for (int32 S = 0; S < 4; ++S)
					{
						const FColor& P = Bitmap[(2 * Y + S / 2) * Width + (2 * X + S % 2)];
						R += P.R; G += P.G; B += P.B;
					}
					Run.Sheet[(OY + Y) * Run.SheetW + (OX + X)] = FColor((uint8)(R / 4), (uint8)(G / 4), (uint8)(B / 4), 255);
				}
			}
			if (Shot.Tile == 3) WritePng(Run.SheetW, Run.SheetH, Run.Sheet, FString::Printf(TEXT("Orientation_%s.png"), *Shot.Name));
		}
		Run.bCaptured = true;
	}

	/** Places the furnished room of phase 1 and checks it; returns the items' world boxes. */
	void PlaceFurnishedRoom(FAutomationTestBase& Test, FFurnishedRenderRun& Run)
	{
		ARoomPlannerManager* Manager = Run.Manager.Get();
		if (!Manager) return;
		TArray<FBox> Boxes;
		for (const FFurnishedItem& Item : FurnishedItems)
		{
			const float Front = FurnishedFrontYaw(Item.Row);
			float RowFront = 0.f;
			const bool bRowFront = FurnishedRowFrontYaw(Item.Row, RowFront);
			Test.TestTrue(FString::Printf(TEXT("%s: DT_PlannerObjects FrontYawDeg (%s) is the front read off the orientation sheet (%.0f)"), Item.Row,
				bRowFront ? *FString::SanitizeFloat(RowFront) : TEXT("no row"), Front), bRowFront && FMath::Abs(FRotator::NormalizeAxis(RowFront - Front)) < 0.01f);
			FString ID;
			if (Item.Wall == INDEX_NONE)
			{
				ID = Manager->AddPlacedObject(Item.Row, FVector(Item.X, Item.Y, 0.f), FRotator(0.f, FRotator::NormalizeAxis(Item.Facing - Front), 0.f), FVector::OneVector);
			}
			else
			{
				// Turned so its front faces the room, dropped on the face, then moved out by half its depth (+1 cm): its back 1 cm off the wall.
				FVector2D Face, Normal;
				if (!Test.TestTrue(FString::Printf(TEXT("%s: wall %d found"), Item.Row, Item.Wall), FurnishedWallFace(Run, Item.Wall, Item.Along, Face, Normal))) continue;
				const float Yaw = FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(Normal.Y, Normal.X)) - Front);
				ID = Manager->AddPlacedObject(Item.Row, FVector(Face.X, Face.Y, 0.f), FRotator(0.f, Yaw, 0.f), FVector::OneVector);
				FBox Box;
				FPlacedFurnitureData D;
				if (!ID.IsEmpty() && FurnishedMeshBox(Manager, ID, Box) && Manager->GetPlacedObject(ID, D))
				{
					const double Depth = FMath::Abs(Normal.X) > 0.5 ? Box.GetSize().X : Box.GetSize().Y;
					const FVector2D Location = Face + Normal * (Depth * 0.5 + 1.);
					Manager->MovePlacedObject(ID, FVector(Location.X, Location.Y, 0.f), D.Rotation, D.Scale);
				}
			}
			FBox Box;
			const APlannerPlacedObjectActor* Actor = ID.IsEmpty() ? nullptr : Manager->FindPlacedObjectActor(ID);
			if (!Test.TestTrue(FString::Printf(TEXT("%s placed"), Item.Row), Actor && FurnishedMeshBox(Manager, ID, Box))) continue;
			Test.AddInfo(FString::Printf(TEXT("[Furnished] %s (%s): x %.1f…%.1f, y %.1f…%.1f, z %.1f…%.1f, yaw %.0f (front %.0f in the model)"), Item.Row,
				*Actor->MeshComponent->GetStaticMesh()->GetName(), Box.Min.X, Box.Max.X, Box.Min.Y, Box.Max.Y, Box.Min.Z, Box.Max.Z, Actor->GetActorRotation().Yaw, Front));
			Test.TestTrue(FString::Printf(TEXT("%s stands on the floor (%.2f)"), Item.Row, Box.Min.Z), FMath::Abs(Box.Min.Z) < 1.);
			Test.TestTrue(FString::Printf(TEXT("%s is inside the room, under the ceiling"), Item.Row), Box.Min.X >= Run.Clear.Min.X - 0.1 && Box.Min.Y >= Run.Clear.Min.Y - 0.1
				&& Box.Max.X <= Run.Clear.Max.X + 0.1 && Box.Max.Y <= Run.Clear.Max.Y + 0.1 && Box.Max.Z < Run.CeilZ);
			for (int32 Other = 0; Other < Boxes.Num(); ++Other)
			{
				const bool bOverlap = Box.Min.X < Boxes[Other].Max.X && Boxes[Other].Min.X < Box.Max.X && Box.Min.Y < Boxes[Other].Max.Y && Boxes[Other].Min.Y < Box.Max.Y;
				Test.TestFalse(FString::Printf(TEXT("%s does not overlap %s"), Item.Row, *Run.Objects[Other].Key), bOverlap);
			}
			Run.Objects.Emplace(Item.Row, ID);
			Boxes.Add(Box);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerFurnishedRoomRenderTest, "MaxiMall.Planner.Render.FurnishedRoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FPlannerFurnishedRoomRenderTest::RunTest(const FString& Parameters)
{
	// As CeilingCorner: skipped quietly without -PlannerSnapshotDir, with a warning for every skip when renders were asked for.
	FString OutDir;
	if (!FParse::Value(FCommandLine::Get(), TEXT("PlannerSnapshotDir="), OutDir))
	{
		AddInfo(TEXT("Furnished room render skipped (needs a GPU and -PlannerSnapshotDir=<folder>)."));
		return true;
	}
	if (!FApp::CanEverRender())
	{
		AddWarning(TEXT("Furnished room render skipped although -PlannerSnapshotDir is given: this session cannot render (-nullrhi?)."));
		return true;
	}
	UWorld* World = AutomationCommon::GetAnyGameWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!World || !World->GetGameViewport() || !PC)
	{
		AddWarning(TEXT("Furnished room render skipped although -PlannerSnapshotDir is given: it needs a game viewport (run it under -game, see PlannerRenderTests.cpp)."));
		return true;
	}

	TSharedRef<FFurnishedRenderRun> Run = MakeShared<FFurnishedRenderRun>();
	Run->OutDir = OutDir;
	IFileManager::Get().MakeDirectory(*OutDir, true);
	FParse::Value(FCommandLine::Get(), TEXT("PlannerRenderSettle="), Run->SettleSeconds);
	Run->SettleSeconds = FMath::Clamp(Run->SettleSeconds, 1.f, 60.f);

	ARoomPlannerManager* Manager = nullptr;
	for (TActorIterator<ARoomPlannerManager> It(World); It; ++It) { Manager = *It; break; }
	Run->bCreatedManager = Manager == nullptr;
	Manager = ARoomPlannerManager::GetOrCreateInstance(World);
	if (!TestNotNull(TEXT("Planner manager"), Manager)) return false;
	if (Manager->GetWallSegmentsForDebug().Num() > 0)
	{
		AddWarning(TEXT("Furnished room render skipped although -PlannerSnapshotDir is given: the planner in this world already holds a layout."));
		return true;
	}
	Run->World = World;
	Run->Manager = Manager;
	Run->Controller = PC;

	// A setup failure before the capture steps leaves the world as it was found, as their last step does.
	auto Abandon = [Run]()
	{
		ClearFurnishedObjects(*Run);
		if (ARoomPlannerManager* M = Run->Manager.Get())
		{
			M->SetPlannerSessionActive(false);
			M->SetViewMode(true);
			M->ClearLayout();
			if (Run->bCreatedManager) M->Destroy();
		}
		return false;
	};

	// The room, drawn through the real manager path and furnished in 2D, as the user does it.
	const double RoomW = Run->RoomW, RoomD = Run->RoomD;
	Run->Walls[0] = Manager->AddWallBetweenPoints(FVector2D(0., 0.), FVector2D(RoomW, 0.));
	Run->Walls[1] = Manager->AddWallBetweenPoints(FVector2D(RoomW, 0.), FVector2D(RoomW, RoomD));
	Run->Walls[2] = Manager->AddWallBetweenPoints(FVector2D(RoomW, RoomD), FVector2D(0., RoomD));
	Run->Walls[3] = Manager->AddWallBetweenPoints(FVector2D(0., RoomD), FVector2D(0., 0.));
	Manager->RebuildRooms();
	if (!TestEqual(TEXT("One room"), Manager->GetRoomsForDebug().Num(), 1)) return Abandon();
	const FRoomData& Room = Manager->GetRoomsForDebug().CreateConstIterator()->Value;
	for (const FVector2D& P : (Room.NetFloorPolygon.Num() >= 3 ? Room.NetFloorPolygon : Room.FloorPolygon)) Run->Clear += P;
	Run->CeilZ = Room.CeilingHeightCm;
	const double CeilZ = Run->CeilZ;
	const FVector2D Centre = Run->Clear.GetCenter();
	Manager->SetPlannerSessionActive(true);
	Manager->SetViewMode(true);
	PlaceFurnishedRoom(*this, *Run);
	TestEqual(TEXT("Every item placed"), Run->Objects.Num(), (int32)UE_ARRAY_COUNT(FurnishedItems)); // rendered anyway: what did place is still worth a look

	// 3D, as the planner widget enters it.
	Manager->SetViewMode(false);
	Run->bCeilingVisible = Manager->bCeilingVisible;
	TestEqual(TEXT("The room has its one light"), Manager->GetCeilingLightCount(), 1);

	// Phase 1 views: over the room from the south-east and the north-west corners, the desk, the lounge, the drawer cabinet, a look
	// along the floor (floating or sunk items) and a plan straight down from 12 m with the planner's ceiling switched off (as the 2D
	// plan: screen up = +X, screen right = +Y).
	const float Settle = Run->SettleSeconds;
	auto Look = [](const FVector& Eye, const FVector& Target, float Fov)
	{
		return [Eye, Target, Fov](FFurnishedRenderRun& R) { SetFurnishedCamera(R, Eye, (Target - Eye).Rotation(), Fov); };
	};
	const FVector TopEye(Centre.X, Centre.Y, 1200.);
	auto Top = [TopEye](FFurnishedRenderRun& R)
	{
		SetFurnishedCeilingHidden(R, true);
		SetFurnishedCamera(R, TopEye, FRotator(-90., 0., 0.), 48.f);
	};
	Run->Shots.Add({ TEXT("overview_se"), Look(FVector(400., 18., CeilZ - 35.), FVector(180., 270., 40.), 80.f), Settle });
	Run->Shots.Add({ TEXT("overview_nw"), Look(FVector(130., 385., CeilZ - 35.), FVector(330., 130., 40.), 80.f), Settle });
	Run->Shots.Add({ TEXT("desk"), Look(FVector(190., 140., 160.), FVector(350., 330., 50.), 70.f), Settle });
	Run->Shots.Add({ TEXT("lounge"), Look(FVector(400., 250., 165.), FVector(110., 110., 45.), 70.f), Settle });
	Run->Shots.Add({ TEXT("cabinet"), Look(FVector(230., 130., 140.), FVector(470., 110., 95.), 80.f), Settle });
	Run->Shots.Add({ TEXT("floor_level"), Look(FVector(400., 18., 30.), FVector(150., 250., 20.), 80.f), Settle });
	Run->Shots.Add({ TEXT("top"), Top, Settle });

	// Phase 2: the same pieces dropped on walls, as the wall drop places them.
	Run->Shots.Add({ TEXT("walldrop_overview"), [Look, CeilZ](FFurnishedRenderRun& R)
	{
		ClearFurnishedObjects(R);
		if (ARoomPlannerManager* M = R.Manager.Get())
		{
			for (const FFurnishedItem& Item : FurnishedWallDrops)
			{
				const FString ID = M->AddPlacedObjectOnWall(Item.Row, R.Walls[Item.Wall], Item.Along, true, 0.f);
				FBox Box;
				FPlacedFurnitureData D;
				if (ID.IsEmpty() || !FurnishedMeshBox(M, ID, Box) || !M->GetPlacedObject(ID, D))
				{
					R.Errors.Add(FString::Printf(TEXT("walldrop: %s not placed on wall %d"), Item.Row, Item.Wall));
					continue;
				}
				const double Depth = (Item.Wall % 2 == 0) ? Box.GetSize().Y : Box.GetSize().X; // south / north walls: depth along Y
				const double Width = (Item.Wall % 2 == 0) ? Box.GetSize().X : Box.GetSize().Y;
				// Its front to the room: the wall's inward normal minus the row's FrontYawDeg.
				float RowFront = 0.f;
				FurnishedRowFrontYaw(Item.Row, RowFront);
				FVector2D Face, Normal;
				FurnishedWallFace(R, Item.Wall, Item.Along, Face, Normal);
				const double ExpectedYaw = FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(Normal.Y, Normal.X)) - RowFront);
				if (FMath::Abs(FRotator::NormalizeAxis(D.Rotation.Yaw - ExpectedYaw)) > 0.01)
				{
					R.Errors.Add(FString::Printf(TEXT("walldrop: %s on wall %d turned to yaw %.1f, its front to the room needs %.1f (front yaw %.0f)"),
						Item.Row, Item.Wall, D.Rotation.Yaw, ExpectedYaw, RowFront));
				}
				R.Infos.Add(FString::Printf(TEXT("[Furnished] walldrop %s on wall %d: %.1f cm deep off the wall, %.1f cm along it, yaw %.0f (front yaw %.0f: front to the room)"),
					Item.Row, Item.Wall, Depth, Width, D.Rotation.Yaw, RowFront));
				R.Objects.Emplace(Item.Row, ID);
			}
		}
		Look(FVector(130., 385., CeilZ - 35.), FVector(330., 130., 40.), 80.f)(R);
	}, Settle });
	Run->Shots.Add({ TEXT("walldrop_top"), Top, Settle });

	// Phase 3: each model alone, seen from +X, +Y, -X and -Y.
	for (const TCHAR* Row : FurnishedOrientationRows)
	{
		for (int32 Tile = 0; Tile < 4; ++Tile)
		{
			const FString RowName(Row);
			Run->Shots.Add({ RowName, [RowName, Tile, Centre](FFurnishedRenderRun& R)
			{
				ARoomPlannerManager* M = R.Manager.Get();
				if (!M) return;
				if (Tile == 0)
				{
					ClearFurnishedObjects(R);
					const FString ID = M->AddPlacedObject(RowName, FVector(Centre.X, Centre.Y, 0.f), FRotator::ZeroRotator, FVector::OneVector);
					if (ID.IsEmpty()) R.Errors.Add(FString::Printf(TEXT("orientation: %s not placed"), *RowName));
					else R.Objects.Emplace(RowName, ID);
				}
				FBox Box(FVector(Centre.X - 50., Centre.Y - 50., 0.), FVector(Centre.X + 50., Centre.Y + 50., 100.));
				if (R.Objects.Num() > 0) FurnishedMeshBox(M, R.Objects[0].Value, Box);
				const FVector Size = Box.GetSize();
				const double Largest = FMath::Max3(Size.X, Size.Y, Size.Z);
				static const FVector2D Dirs[4] = { FVector2D(1., 0.), FVector2D(0., 1.), FVector2D(-1., 0.), FVector2D(0., -1.) };
				const FVector2D Dir = Dirs[Tile];
				const double Room = FMath::Abs(Dir.X) > 0.5 ? R.Clear.GetExtent().X : R.Clear.GetExtent().Y;
				const double Distance = FMath::Clamp(1.3 * Largest, 110., Room - 12.);
				const FVector Target(Centre.X, Centre.Y, Box.GetCenter().Z);
				const FVector Eye(Centre.X + Dir.X * Distance, Centre.Y + Dir.Y * Distance, FMath::Min(Target.Z + 0.45 * Distance, R.CeilZ - 10.));
				SetFurnishedCamera(R, Eye, (Target - Eye).Rotation(), 90.f);
			}, FMath::Max(1.5f, Settle * 0.5f), Tile });
		}
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACameraActor* Camera = World->SpawnActor<ACameraActor>(FVector(Centre.X, Centre.Y, 150.), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("Camera"), Camera)) return Abandon();
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	Run->Camera = Camera;
	if (APawn* Pawn = PC->GetPawn())
	{
		Pawn->SetActorHiddenInGame(true);
		Pawn->SetActorEnableCollision(false);
	}
	PC->SetViewTarget(Camera);
	GEngine->Exec(World, TEXT("DisableAllScreenMessages"));

	// The player controller forces Epic scalability for the planner: do the same, and put it back afterwards.
	Run->SavedQuality = Scalability::GetQualityLevels();
	{
		Scalability::FQualityLevels Epic = Run->SavedQuality;
		Epic.SetFromSingleQualityLevel(3);
		Scalability::SetQualityLevels(Epic);
	}

	Run->CaptureHandle = UGameViewportClient::OnScreenshotCaptured().AddLambda([Run](int32 Width, int32 Height, const TArray<FColor>& Bitmap)
	{
		HandleFurnishedCapture(*Run, Width, Height, Bitmap);
	});
	AddInfo(FString::Printf(TEXT("Furnished room render: %d items, %d captures, %.0f s settling per room view, room %.0f × %.0f cm clear, ceiling %.0f cm, into %s"),
		Run->Objects.Num(), Run->Shots.Num(), Run->SettleSeconds, Run->Clear.GetSize().X, Run->Clear.GetSize().Y, CeilZ, *OutDir));

	// Waits until no shader, texture or mesh is compiling (a fresh -game session compiles what the new models use) and 2 s have passed.
	auto AddCompileWait = [](float Timeout)
	{
		TSharedRef<double> QuietSince = MakeShared<double>(0.);
		ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([QuietSince]()
		{
			const bool bBusy = (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) || FAssetCompilingManager::Get().GetNumRemainingAssets() > 0;
			const double Now = FPlatformTime::Seconds();
			if (bBusy || *QuietSince == 0.) { *QuietSince = bBusy ? 0. : Now; return false; }
			return Now - *QuietSince > 2.;
		}, []() { UE_LOG(LogTemp, Warning, TEXT("[Furnished] shaders or assets still compiling; capturing anyway.")); return true; }, Timeout));
	};
	AddCompileWait(900.f);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(4.f));

	for (int32 Index = 0; Index < Run->Shots.Num(); ++Index)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Run, Index]()
		{
			Run->CurrentShot = Index;
			Run->bCaptured = false;
			SetFurnishedCeilingHidden(*Run, false);
			if (Run->Shots[Index].Setup) Run->Shots[Index].Setup(*Run);
			return true;
		}));
		if (Run->Shots[Index].Tile <= 0) AddCompileWait(300.f); // a new layout may bring new materials
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(Run->Shots[Index].Settle));
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Run]()
		{
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(Run->OutDir, TEXT("Furnished_unused.png")), false, false);
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([Run]() { return Run->bCaptured; },
			[Run, Index]() { Run->Errors.Add(FString::Printf(TEXT("%s: no capture within 20 s"), *Run->Shots[Index].Name)); return true; }, 20.f));
	}

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run]()
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(Run->CaptureHandle);
		for (const FString& Info : Run->Infos) AddInfo(Info);
		for (const FString& Error : Run->Errors) AddError(Error);
		TestEqual(TEXT("Every shot captured"), Run->FrameLuminance.Num(), Run->Shots.Num());
		for (const TPair<FString, double>& Frame : Run->FrameLuminance)
		{
			TestTrue(FString::Printf(TEXT("%s: the room is lit (%.3f)"), *Frame.Key, Frame.Value), Frame.Value > 0.02 && Frame.Value < 0.95);
		}

		// Leave the world as it was found.
		SetFurnishedCeilingHidden(*Run, false);
		ClearFurnishedObjects(*Run);
		if (ARoomPlannerManager* M = Run->Manager.Get())
		{
			M->SetPlannerSessionActive(false);
			M->SetViewMode(true);
			M->ClearLayout();
			if (Run->bCreatedManager) M->Destroy();
		}
		if (APlayerController* Controller = Run->Controller.Get())
		{
			if (APawn* Pawn = Controller->GetPawn())
			{
				Pawn->SetActorHiddenInGame(false);
				Pawn->SetActorEnableCollision(true);
				Controller->SetViewTarget(Pawn);
			}
		}
		if (ACameraActor* Cam = Run->Camera.Get()) Cam->Destroy();
		Scalability::SetQualityLevels(Run->SavedQuality);
		return true;
	}));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// The planner's Lumen overrides live exactly as long as the planner is open in 3D
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerLumenOverridesTest, "MaxiMall.Planner.Render.LumenOverrides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerLumenOverridesTest::RunTest(const FString& Parameters)
{
	const TCHAR* AlbedoName = TEXT("r.Lumen.ScreenProbeGather.ShortRangeAO.MaxMultibounceAlbedo");
	const TCHAR* ModeName = TEXT("r.Lumen.HardwareRayTracing.LightingMode");
	IConsoleVariable* Albedo = IConsoleManager::Get().FindConsoleVariable(AlbedoName);
	IConsoleVariable* Mode = IConsoleManager::Get().FindConsoleVariable(ModeName);
	if (!TestNotNull(TEXT("Short-range AO albedo cap variable"), Albedo) || !TestNotNull(TEXT("Lumen lighting mode variable"), Mode)) return false;
	auto SetBy = [](const IConsoleVariable* CVar) { return (uint32)(CVar->GetFlags() & ECVF_SetByMask); };
	auto Claims = [AlbedoName, ModeName]() { return FPlannerCVarOverride::GetOwnerCount(AlbedoName) + FPlannerCVarOverride::GetOwnerCount(ModeName); };
	// Only claims that survive a collection count as residue: a manager an earlier test left to the BeginDestroy fallback (its world
	// torn down without EndPlay or Destroyed) gives its claims back here.
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	const FString AlbedoBefore = Albedo->GetString();
	const uint32 AlbedoSetByBefore = SetBy(Albedo);
	const FString ModeBefore = Mode->GetString();
	const uint32 ModeSetByBefore = SetBy(Mode);
	const FString StateBefore = FString::Printf(TEXT("albedo cap %s by %s, lighting mode %s by %s, %d planner claims"),
		*AlbedoBefore, GetConsoleVariableSetByName((EConsoleVariableFlags)AlbedoSetByBefore),
		*ModeBefore, GetConsoleVariableSetByName((EConsoleVariableFlags)ModeSetByBefore), Claims());
	// A code-priority value or a planner claim at the start is exactly the residue this test exists to catch (a planner left
	// open in 3D by an earlier test, or an override given back wrongly). A console value may be the user's own: skip then.
	if (Claims() > 0 || AlbedoSetByBefore == (uint32)ECVF_SetByCode || ModeSetByBefore == (uint32)ECVF_SetByCode)
	{
		AddError(FString::Printf(TEXT("The planner's Lumen overrides are already in place before the test (%s): an earlier test or planner left them behind."), *StateBefore));
		return false;
	}
	if (AlbedoSetByBefore > (uint32)ECVF_SetByCode || ModeSetByBefore > (uint32)ECVF_SetByCode)
	{
		AddInfo(FString::Printf(TEXT("Lumen overrides not checked: a console value outranks them in this session (%s)."), *StateBefore));
		return true;
	}
	auto IsUntouched = [&]()
	{
		return Albedo->GetString() == AlbedoBefore && SetBy(Albedo) == AlbedoSetByBefore
			&& Mode->GetString() == ModeBefore && SetBy(Mode) == ModeSetByBefore && Claims() == 0;
	};
	auto IsOverridden = [&](float Cap)
	{
		return FMath::IsNearlyEqual(Albedo->GetFloat(), Cap) && SetBy(Albedo) == (uint32)ECVF_SetByCode
			&& Mode->GetInt() == 1 && SetBy(Mode) == (uint32)ECVF_SetByCode;
	};

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ARoomPlannerManager* Manager = World->SpawnActor<ARoomPlannerManager>();
	ARoomPlannerManager* Other = nullptr;
	ARoomPlannerManager* Unstarted = nullptr;
	ON_SCOPE_EXIT
	{
		for (ARoomPlannerManager* M : { Manager, Other, Unstarted })
		{
			if (IsValid(M)) M->SetPlannerSessionActive(false);
		}
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		World->RemoveFromRoot();
	};
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	if (!Manager->HasActorBegunPlay()) Manager->DispatchBeginPlay();

	// 3D without an open planner: nothing is overridden.
	Manager->SetViewMode(false);
	TestTrue(TEXT("Closed planner: the project values stay"), IsUntouched());

	// Open in 3D: both overrides at code priority.
	Manager->SetPlannerSessionActive(true);
	TestEqual(TEXT("Open in 3D: AO albedo cap"), Albedo->GetFloat(), Manager->PlannerShortRangeAOMaxMultibounceAlbedo);
	TestEqual(TEXT("Open in 3D: AO albedo cap set by code"), SetBy(Albedo), (uint32)ECVF_SetByCode);
	TestEqual(TEXT("Open in 3D: hit lighting for GI"), Mode->GetInt(), 1);
	TestEqual(TEXT("Open in 3D: one claim on each variable"), Claims(), 2);

	// A new value while open is applied at once.
	Manager->PlannerShortRangeAOMaxMultibounceAlbedo = 0.95f;
	Manager->UpdatePlannerExposure();
	TestEqual(TEXT("Changed while open"), Albedo->GetFloat(), 0.95f);
	TestEqual(TEXT("Changed while open: still one claim each"), Claims(), 2);

	// 2D gives everything back, with nothing left at code priority.
	Manager->SetViewMode(true);
	TestTrue(TEXT("2D: the project values are back, not re-set by code"), IsUntouched());

	// Back to 3D, then the planner closes.
	Manager->SetViewMode(false);
	TestEqual(TEXT("3D again"), Albedo->GetFloat(), 0.95f);
	Manager->SetPlannerSessionActive(false);
	TestTrue(TEXT("Closed: the project values are back"), IsUntouched());

	// 0 keeps the project value of the cap (the lighting mode is still overridden).
	Manager->PlannerShortRangeAOMaxMultibounceAlbedo = 0.f;
	Manager->SetPlannerSessionActive(true);
	TestEqual(TEXT("Cap 0: the project value stays"), Albedo->GetString(), AlbedoBefore);
	TestEqual(TEXT("Cap 0: the lighting mode is still overridden"), Mode->GetInt(), 1);
	Manager->SetPlannerSessionActive(false);
	TestTrue(TEXT("Cap 0, closed: the project values are back"), IsUntouched());

	// A console value outranks the override and survives it: the override yields (sets nothing, so the engine has nothing to refuse).
	Manager->PlannerShortRangeAOMaxMultibounceAlbedo = 0.9f;
	Albedo->Set(TEXT("0.3"), ECVF_SetByConsole);
	Manager->SetPlannerSessionActive(true);
	TestEqual(TEXT("Console value wins while open"), Albedo->GetFloat(), 0.3f);
	TestEqual(TEXT("Console value on top: still one claim each (the cap's yielded)"), Claims(), 2);
	TestEqual(TEXT("Console value on the cap: the lighting mode is still overridden"), Mode->GetInt(), 1);
	Manager->SetPlannerSessionActive(false);
	TestEqual(TEXT("Console value kept after close"), Albedo->GetFloat(), 0.3f);
	TestEqual(TEXT("Console value still at console priority"), SetBy(Albedo), (uint32)ECVF_SetByConsole);
	Albedo->Unset(ECVF_SetByConsole);
	TestTrue(TEXT("Console value removed: the project values are back"), IsUntouched());

	// Two planners in one process (multi-client PIE, a listen server and its client: one manager per world, which the console
	// variables do not care about). Both claim the one override; it is given back only when the last of them leaves 3D,
	// whatever the order they open and close in.
	Other = World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Second planner"), Other)) return false;
	if (!Other->HasActorBegunPlay()) Other->DispatchBeginPlay();
	Other->SetViewMode(false);
	Manager->SetPlannerSessionActive(true);
	Other->SetPlannerSessionActive(true);
	TestTrue(TEXT("Both open in 3D: overridden"), IsOverridden(0.9f));
	TestEqual(TEXT("Both open in 3D: two claims on each variable"), Claims(), 4);
	Manager->SetPlannerSessionActive(false);
	TestTrue(TEXT("The first planner closed, the second still in 3D: still overridden"), IsOverridden(0.9f));
	TestEqual(TEXT("The first planner closed: one claim each left"), Claims(), 2);
	Other->SetPlannerSessionActive(false);
	TestTrue(TEXT("Both closed: the project values are back"), IsUntouched());

	// Opened the other way round; the second planner goes to 2D first, the first then closes, and the second one's EndPlay
	// (PIE stop) comes last, after it went back to 3D.
	Other->SetPlannerSessionActive(true);
	Manager->SetPlannerSessionActive(true);
	Other->SetViewMode(true);
	TestTrue(TEXT("One planner in 2D, the other in 3D: still overridden"), IsOverridden(0.9f));
	Other->SetViewMode(false);
	TestEqual(TEXT("Back to 3D: it claims again"), Claims(), 4);
	Manager->SetPlannerSessionActive(false);
	TestTrue(TEXT("The first planner closed, the second in 3D: still overridden"), IsOverridden(0.9f));
	Other->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
	TestTrue(TEXT("The last planner's EndPlay: the project values are back"), IsUntouched());
	Other->Destroy();

	// Another system's code-priority value from before the first planner opened is written back by the last one to close.
	Other = World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Third planner"), Other)) return false;
	if (!Other->HasActorBegunPlay()) Other->DispatchBeginPlay();
	Other->SetViewMode(false);
	Albedo->Set(TEXT("0.7"), ECVF_SetByCode);
	Manager->SetPlannerSessionActive(true);
	Other->SetPlannerSessionActive(true);
	TestTrue(TEXT("Over a code value: overridden"), IsOverridden(0.9f));
	Manager->SetPlannerSessionActive(false);
	TestTrue(TEXT("Over a code value, one planner closed: still overridden"), IsOverridden(0.9f));
	Other->SetPlannerSessionActive(false);
	TestEqual(TEXT("Over a code value, both closed: that value is back"), Albedo->GetFloat(), 0.7f);
	TestEqual(TEXT("Over a code value, both closed: at code priority, as found"), SetBy(Albedo), (uint32)ECVF_SetByCode);
	TestEqual(TEXT("Over a code value, both closed: no claims left"), Claims(), 0);
	Albedo->Unset(ECVF_SetByCode);
	TestTrue(TEXT("The other system's value removed: the project values are back"), IsUntouched());
	Other->Destroy();

	// Another system's code-priority value UNDER a console value: the override yields and touches nothing (a code-priority Set would
	// be refused, yet the engine would still record it in the single code slot, over the other system's value). Once the console
	// value goes, that code value is still there.
	Albedo->Set(TEXT("0.7"), ECVF_SetByCode);
	Albedo->Set(TEXT("0.3"), ECVF_SetByConsole);
	Manager->SetPlannerSessionActive(true);
	TestEqual(TEXT("Code value under a console value, open: the console value shows"), Albedo->GetFloat(), 0.3f);
	TestEqual(TEXT("Code value under a console value, open: one claim each"), Claims(), 2);
	Manager->SetPlannerSessionActive(false);
	TestEqual(TEXT("Code value under a console value, closed: still the console value"), Albedo->GetFloat(), 0.3f);
	TestEqual(TEXT("Code value under a console value, closed: no claims left"), Claims(), 0);
	Albedo->Unset(ECVF_SetByConsole);
	TestEqual(TEXT("Console value removed: the other system's code value is back"), Albedo->GetFloat(), 0.7f);
	TestEqual(TEXT("Console value removed: at code priority, as found"), SetBy(Albedo), (uint32)ECVF_SetByCode);

	// The same, but the console value goes while the planner is open: the next update takes over as a first claim would (it saves
	// the other system's value), and the close writes that value back.
	Albedo->Set(TEXT("0.3"), ECVF_SetByConsole);
	Manager->SetPlannerSessionActive(true);
	TestEqual(TEXT("Yielded while open"), Albedo->GetFloat(), 0.3f);
	Albedo->Unset(ECVF_SetByConsole);
	Manager->UpdatePlannerExposure();
	TestTrue(TEXT("Console value gone while open: the override takes over"), IsOverridden(0.9f));
	Manager->SetPlannerSessionActive(false);
	TestEqual(TEXT("Taken over, then closed: the other system's code value is back"), Albedo->GetFloat(), 0.7f);
	TestEqual(TEXT("Taken over, then closed: at code priority"), SetBy(Albedo), (uint32)ECVF_SetByCode);
	TestEqual(TEXT("Taken over, then closed: no claims left"), Claims(), 0);
	Albedo->Unset(ECVF_SetByCode);
	TestTrue(TEXT("The other system's value removed again: the project values are back"), IsUntouched());

	// The session ends with the actor (PIE stop, level change): EndPlay alone gives everything back. Destroy would give it back
	// in Destroyed before EndPlay runs, so it comes afterwards.
	Manager->SetPlannerSessionActive(true);
	TestEqual(TEXT("Open again"), Albedo->GetFloat(), 0.9f);
	Manager->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
	TestTrue(TEXT("EndPlay: the project values are back"), IsUntouched());
	Manager->Destroy();
	TestTrue(TEXT("Destroyed after EndPlay: still the project values"), IsUntouched());

	// A manager destroyed without ever beginning play (no EndPlay) gives everything back as well.
	Unstarted = World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Unstarted manager"), Unstarted)) return false;
	Unstarted->SetViewMode(false);
	Unstarted->SetPlannerSessionActive(true);
	TestEqual(TEXT("Unstarted manager open in 3D"), Albedo->GetFloat(), 0.9f);
	Unstarted->Destroy();
	TestTrue(TEXT("Destroyed without EndPlay: the project values are back"), IsUntouched());

	// A manager that gets neither EndPlay nor Destroyed: its world never began play and is torn down around it. Only the collection
	// (BeginDestroy) gives its claims back. The managers above are all destroyed and are freed by that collection: forget them first,
	// the scope exit must not touch them.
	Manager = nullptr;
	Other = nullptr;
	Unstarted = nullptr;
	UWorld* TornDown = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("A second world, never begun"), TornDown)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(TornDown);
	TWeakObjectPtr<ARoomPlannerManager> Orphan = TornDown->SpawnActor<ARoomPlannerManager>();
	if (TestTrue(TEXT("Manager in the second world"), Orphan.IsValid()))
	{
		Orphan->SetViewMode(false);
		Orphan->SetPlannerSessionActive(true);
		TestEqual(TEXT("Its world never began play, open in 3D: one claim each"), Claims(), 2);
	}
	GEngine->DestroyWorldContext(TornDown);
	TornDown->DestroyWorld(false);
	TornDown->RemoveFromRoot();
	TestEqual(TEXT("World torn down (no EndPlay, no Destroyed): the claims are still held"), Claims(), 2);
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestFalse(TEXT("The orphaned manager was collected"), Orphan.IsValid(true));
	TestTrue(TEXT("Collected (BeginDestroy): the project values are back"), IsUntouched());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
