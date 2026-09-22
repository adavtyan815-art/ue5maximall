// Copyright 2026 MaxiMall. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Constructor/PlannerDimensions.h"
#include "Constructor/PlannerFinishLayout.h"
#include "Constructor/PlannerPlacedObjectActor.h"
#include "FurnitureConfigurator/ShowroomBooth.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Constructor/RoomPlannerManager.h"
#include "FurnitureConfigurator/UI/RoomPlannerWidget.h"
#include "FurnitureConfigurator/UI/PlannerDimensionOverlay.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/WrapBox.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Input/HittestGrid.h"
#include "Layout/WidgetPath.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SVirtualWindow.h"

namespace
{
	const TCHAR* PlannerWidgetClassPath = TEXT("/Game/RoomPlanner/WBP_RoomPlannerWidget.WBP_RoomPlannerWidget_C");

	struct FScopedPlannerTestWorld
	{
		UWorld* World = nullptr;
		FScopedPlannerTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (World && GEngine)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			}
		}
		~FScopedPlannerTestWorld()
		{
			if (World)
			{
				if (GEngine) GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World->RemoveFromRoot();
			}
		}
	};

	/**
	 * The planner widget as the game builds it (WBP layout, NativeConstruct run by TakeWidget). OutSlate keeps its Slate tree
	 * alive: releasing it runs NativeDestruct.
	 */
	URoomPlannerWidget* CreatePlannerWidget(FAutomationTestBase& Test, UWorld* World, TSharedPtr<SWidget>& OutSlate)
	{
		UClass* WidgetClass = LoadClass<URoomPlannerWidget>(nullptr, PlannerWidgetClassPath);
		if (!Test.TestNotNull(TEXT("WBP_RoomPlannerWidget"), WidgetClass)) return nullptr;
		URoomPlannerWidget* Widget = CreateWidget<URoomPlannerWidget>(World, WidgetClass);
		if (!Test.TestNotNull(TEXT("Planner widget"), Widget)) return nullptr;
		// Without a player NativeOnInitialized does not run; the layout built in code is (idempotent).
		Widget->BuildRuntimeLayout();
		OutSlate = Widget->TakeWidget();
		return Widget;
	}

	/** Lays out and paints a widget like a frame on screen (no render target): wrap boxes and wrapping text settle their size. */
	int32 PaintOffscreen(const TSharedRef<SWidget>& Content, const FVector2D& Size, int32 Passes = 3)
	{
		TSharedRef<SVirtualWindow> Window = SNew(SVirtualWindow).Size(Size);
		Window->SetContent(Content);
		Window->Resize(Size);
		int32 MaxLayer = 0;
		for (int32 Pass = 0; Pass < Passes; ++Pass)
		{
			Window->SlatePrepass(1.f);
			const FGeometry WindowGeometry = FGeometry::MakeRoot(Size, FSlateLayoutTransform(1.f));
			FSlateWindowElementList ElementList(Window);
			FPaintArgs PaintArgs(nullptr, Window->GetHittestGrid(), FVector2D::ZeroVector, FApp::GetCurrentTime(), 0.016f);
			MaxLayer = Window->Paint(PaintArgs, WindowGeometry, FSlateRect(FVector2D::ZeroVector, Size), ElementList, 0, FWidgetStyle(), true);
		}
		Window->SetContent(SNullWidget::NullWidget);
		return MaxLayer;
	}

	/** Draw elements of one offscreen frame, by type. */
	struct FElementCounts
	{
		int32 Lines = 0;
		int32 CustomVerts = 0;
		int32 RoundedBoxes = 0;
		int32 Texts = 0;
		/** Value boxes as drawn, and their fill. */
		TArray<FSlateRect> Boxes;
		TArray<FLinearColor> BoxTints;
	};

	FElementCounts PaintAndCount(const TSharedRef<SWidget>& Content, const FVector2D& Size)
	{
		TSharedRef<SVirtualWindow> Window = SNew(SVirtualWindow).Size(Size);
		Window->SetContent(Content);
		Window->Resize(Size);
		Window->SlatePrepass(1.f);
		const FGeometry WindowGeometry = FGeometry::MakeRoot(Size, FSlateLayoutTransform(1.f));
		FSlateWindowElementList ElementList(Window);
		FPaintArgs PaintArgs(nullptr, Window->GetHittestGrid(), FVector2D::ZeroVector, FApp::GetCurrentTime(), 0.016f);
		Window->Paint(PaintArgs, WindowGeometry, FSlateRect(FVector2D::ZeroVector, Size), ElementList, 0, FWidgetStyle(), true);
		const FSlateDrawElementMap& Elements = ElementList.GetUncachedDrawElements();
		FElementCounts Counts;
		Counts.Lines = Elements.Get<(uint8)EElementType::ET_Line>().Num();
		Counts.CustomVerts = Elements.Get<(uint8)EElementType::ET_CustomVerts>().Num();
		Counts.RoundedBoxes = Elements.Get<(uint8)EElementType::ET_RoundedBox>().Num();
		Counts.Texts = Elements.Get<(uint8)EElementType::ET_Text>().Num();
		for (const auto& Box : Elements.Get<(uint8)EElementType::ET_RoundedBox>())
		{
			const FVector2D Position = Box.GetPosition();
			const FVector2D BoxSize = Box.GetLocalSize();
			Counts.Boxes.Add(FSlateRect(Position, Position + BoxSize * Box.GetScale()));
			Counts.BoxTints.Add(Box.GetTint());
		}
		Window->SetContent(SNullWidget::NullWidget);
		return Counts;
	}

	/** Renders to a PNG (needs a GPU); returns false when it could not. */
	bool SnapshotToPng(const TSharedRef<SWidget>& Content, const FVector2D& Size, const FString& File)
	{
		UTextureRenderTarget2D* Target = FWidgetRenderer::CreateTargetFor(Size, TF_Bilinear, false);
		if (!Target) return false;
		{
			FWidgetRenderer Renderer(false, true);
			for (int32 Pass = 0; Pass < 3; ++Pass)
			{
				Renderer.DrawWidget(Target, Content, Size, 0.016f);
			}
			FlushRenderingCommands();
		}
		TArray<FColor> Pixels;
		FTextureRenderTargetResource* TargetResource = Target->GameThread_GetRenderTargetResource();
		if (!TargetResource || !TargetResource->ReadPixels(Pixels)) return false;
		for (FColor& Pixel : Pixels)
		{
			Pixel = FLinearColor(Pixel.R / 255.f, Pixel.G / 255.f, Pixel.B / 255.f, 1.f).ToFColor(true);
		}
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray((int32)Size.X, (int32)Size.Y, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		return FFileHelper::SaveArrayToFile(Png, *File);
	}

	float RightEdge(const UWidget* W) { const FGeometry& G = W->GetCachedGeometry(); return (float)(G.GetAbsolutePosition().X + G.GetAbsoluteSize().X); }
	float TopEdge(const UWidget* W) { return (float)W->GetCachedGeometry().GetAbsolutePosition().Y; }
	float BottomEdge(const UWidget* W) { const FGeometry& G = W->GetCachedGeometry(); return (float)(G.GetAbsolutePosition().Y + G.GetAbsoluteSize().Y); }
}

// ─────────────────────────────────────────────────────────────────────────────
// Catalog tabs: none active when the planner opens, the card area folded until a tab is clicked
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerCatalogTabsFoldedTest, "MaxiMall.Planner.UI.CatalogTabsFolded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerCatalogTabsFoldedTest::RunTest(const FString& Parameters)
{
	FScopedPlannerTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	TSharedPtr<SWidget> Slate;
	URoomPlannerWidget* Widget = CreatePlannerWidget(*this, TestWorld.World, Slate);
	if (!Widget) return false;
	if (!TestNotNull(TEXT("Catalog_Container bound"), Widget->Catalog_Container.Get())
		|| !TestNotNull(TEXT("BtnCatalogInterior bound"), Widget->BtnCatalogInterior.Get())
		|| !TestNotNull(TEXT("BtnCatalogCabinets bound"), Widget->BtnCatalogCabinets.Get())) return false;
	TestTrue(TEXT("Planner opens in 2D"), Widget->CurrentViewMode == ERoomPlannerViewMode::View2D);

	// Opened: no tab, no cards, the card area folded.
	TestFalse(TEXT("No catalog tab is active when the planner opens"), Widget->HasActiveCatalogTab());
	TestEqual(TEXT("No cards before a tab is clicked"), Widget->Catalog_Container->GetChildrenCount(), 0);
	TestTrue(TEXT("Card area folded before a tab is clicked"), Widget->Catalog_Container->GetVisibility() == ESlateVisibility::Collapsed);
	TestTrue(TEXT("«Интерьер» not highlighted"), Widget->BtnCatalogInterior->GetBackgroundColor().Equals(Widget->InactiveTabColor));
	TestTrue(TEXT("«Тумбы» not highlighted"), Widget->BtnCatalogCabinets->GetBackgroundColor().Equals(Widget->InactiveTabColor));
	// The whole catalog (tabs and cards) is folded until the «Каталог» button opens it.
	TestFalse(TEXT("Catalog folded when the planner opens"), Widget->IsCatalogOpen());
	TestTrue(TEXT("Tabs hidden while folded"), Widget->CatalogTabBar == nullptr || Widget->CatalogTabBar->GetVisibility() == ESlateVisibility::Collapsed);
	UButton* Toggle = Widget->GetCatalogToggleButton();
	if (!TestNotNull(TEXT("«Каталог» button"), Toggle)) return false;
	TestTrue(TEXT("«Каталог» button shown in 2D"), Toggle->GetVisibility() == ESlateVisibility::Visible);

	// «Каталог»: the tabs appear, still no tab chosen, so no cards yet.
	Widget->SetCatalogOpen(true);
	TestTrue(TEXT("Opened: tabs shown"), Widget->CatalogTabBar == nullptr || Widget->CatalogTabBar->GetVisibility() != ESlateVisibility::Collapsed);
	TestTrue(TEXT("Opened: no cards before a tab is clicked"), Widget->Catalog_Container->GetVisibility() == ESlateVisibility::Collapsed);
	TestTrue(TEXT("Opened: the button shows it is on"), Toggle->GetBackgroundColor().Equals(Widget->ActiveTabColor));

	// The same after a 3D round trip.
	Widget->SetViewMode(ERoomPlannerViewMode::View3D);
	TestTrue(TEXT("3D: the «Каталог» button hides"), Toggle->GetVisibility() == ESlateVisibility::Collapsed);
	TestTrue(TEXT("3D: tabs hidden"), Widget->CatalogTabBar == nullptr || Widget->CatalogTabBar->GetVisibility() == ESlateVisibility::Collapsed);
	Widget->SetViewMode(ERoomPlannerViewMode::View2D);
	TestTrue(TEXT("Still no cards after 3D and back"), Widget->Catalog_Container->GetVisibility() == ESlateVisibility::Collapsed);

	// A click on «Тумбы» opens its cards.
	Widget->SetActiveCatalogTab(EPlannerPlacementKind::CabinetSet);
	TestTrue(TEXT("Tab active after the click"), Widget->HasActiveCatalogTab());
	TestTrue(TEXT("Card area shown after the click"), Widget->Catalog_Container->GetVisibility() != ESlateVisibility::Collapsed);
	TestTrue(TEXT("Card area filled after the click"), Widget->Catalog_Container->GetChildrenCount() > 0);
	TestTrue(TEXT("«Тумбы» highlighted"), Widget->BtnCatalogCabinets->GetBackgroundColor().Equals(Widget->ActiveTabColor));
	TestTrue(TEXT("«Интерьер» not highlighted"), Widget->BtnCatalogInterior->GetBackgroundColor().Equals(Widget->InactiveTabColor));

	// Then «Интерьер».
	Widget->SetActiveCatalogTab(EPlannerPlacementKind::Object);
	TestTrue(TEXT("«Интерьер» highlighted"), Widget->BtnCatalogInterior->GetBackgroundColor().Equals(Widget->ActiveTabColor));
	TestTrue(TEXT("«Тумбы» no longer highlighted"), Widget->BtnCatalogCabinets->GetBackgroundColor().Equals(Widget->InactiveTabColor));
	TestTrue(TEXT("Card area still shown"), Widget->Catalog_Container->GetVisibility() != ESlateVisibility::Collapsed);

	// In 3D the catalog is hidden as before.
	Widget->SetViewMode(ERoomPlannerViewMode::View3D);
	TestTrue(TEXT("Card area hidden in 3D"), Widget->Catalog_Container->GetVisibility() == ESlateVisibility::Collapsed);
	Widget->SetViewMode(ERoomPlannerViewMode::View2D);
	TestTrue(TEXT("Card area back in 2D with the chosen tab"), Widget->Catalog_Container->GetVisibility() != ESlateVisibility::Collapsed);

	// «Каталог» again folds everything; once more brings back the tab chosen before, with its cards.
	Widget->SetCatalogOpen(false);
	TestTrue(TEXT("Folded again: tabs and cards hidden"), Widget->Catalog_Container->GetVisibility() == ESlateVisibility::Collapsed
		&& (Widget->CatalogTabBar == nullptr || Widget->CatalogTabBar->GetVisibility() == ESlateVisibility::Collapsed));
	TestTrue(TEXT("Folded again: the button is off"), !Toggle->GetBackgroundColor().Equals(Widget->ActiveTabColor));
	Widget->SetCatalogOpen(true);
	TestTrue(TEXT("Reopened: the chosen tab's cards are back"), Widget->Catalog_Container->GetVisibility() != ESlateVisibility::Collapsed
		&& Widget->BtnCatalogInterior->GetBackgroundColor().Equals(Widget->ActiveTabColor));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tool row: «Каталог» next to the tools, like them, inside the side panel
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerToolbarFitsTest, "MaxiMall.Planner.UI.ToolbarFitsPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerToolbarFitsTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddInfo(TEXT("Skipped: no Slate application."));
		return true;
	}
	FScopedPlannerTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	TSharedPtr<SWidget> Slate;
	URoomPlannerWidget* Widget = CreatePlannerWidget(*this, TestWorld.World, Slate);
	if (!Widget) return false;
	UButton* Toggle = Widget->GetCatalogToggleButton();
	UWidget* Panel = Widget->WidgetTree ? Widget->WidgetTree->FindWidget(TEXT("LeftPanel")) : nullptr;
	if (!TestNotNull(TEXT("«Каталог» button"), Toggle) || !TestNotNull(TEXT("BtnPresetRoom"), Widget->BtnPresetRoom.Get())
		|| !TestNotNull(TEXT("BtnDrawWallTool"), Widget->BtnDrawWallTool.Get()) || !TestNotNull(TEXT("LeftPanel"), Panel)) return false;

	// In the tool row, last, after "4×4 м"; styled like it.
	UPanelWidget* Row = Toggle->GetParent();
	TestTrue(TEXT("In the tool row with the other tools"), Cast<UWrapBox>(Row) != nullptr && Widget->BtnPresetRoom->GetParent() == Row && Widget->BtnDrawWallTool->GetParent() == Row);
	TestTrue(TEXT("After «4×4 м»"), Row && Row->GetChildIndex(Toggle) == Row->GetChildrenCount() - 1);
	const UTextBlock* Label = Cast<UTextBlock>(Toggle->GetContent());
	const UTextBlock* PresetLabel = Cast<UTextBlock>(Widget->BtnPresetRoom->GetContent());
	TestTrue(TEXT("Labelled «Каталог»"), Label && Label->GetText().ToString() == TEXT("Каталог"));
	TestTrue(TEXT("Same font as the tools"), Label && PresetLabel && Label->GetFont().Size == PresetLabel->GetFont().Size
		&& Label->GetFont().FontObject == PresetLabel->GetFont().FontObject && Label->GetFont().TypefaceFontName == PresetLabel->GetFont().TypefaceFontName);
	TestTrue(TEXT("Same button style"), Toggle->GetStyle().Normal.GetResourceObject() == Widget->BtnPresetRoom->GetStyle().Normal.GetResourceObject());

	// All four tools at once (a wall exists, so "Выбрать" shows): nothing runs past the side panel.
	if (ARoomPlannerManager* Manager = ARoomPlannerManager::GetOrCreateInstance(TestWorld.World))
	{
		Manager->AddWallBetweenPoints(FVector2D(0., 0.), FVector2D(300., 0.));
	}
	const FVector2D ScreenSize(1920.f, 1080.f);
	PaintOffscreen(Slate.ToSharedRef(), ScreenSize);
	const float PanelRight = RightEdge(Panel);
	const UWidget* Tools[] = { Widget->BtnDrawWallTool.Get(), Widget->BtnSelectTool.Get(), Widget->BtnPresetRoom.Get(), Toggle };
	for (const UWidget* Tool : Tools)
	{
		if (!Tool || Tool->GetVisibility() == ESlateVisibility::Collapsed) continue;
		TestTrue(FString::Printf(TEXT("%s ends inside the side panel (%.1f <= %.1f)"), *Tool->GetName(), RightEdge(Tool), PanelRight), RightEdge(Tool) <= PanelRight + 0.5f);
	}
	TestTrue(TEXT("«Выбрать» shown with a wall"), Widget->BtnSelectTool && Widget->BtnSelectTool->GetVisibility() != ESlateVisibility::Collapsed);
	const float ToggleHeight = (float)Toggle->GetCachedGeometry().GetLocalSize().Y;
	const float PresetHeight = (float)Widget->BtnPresetRoom->GetCachedGeometry().GetLocalSize().Y;
	TestTrue(FString::Printf(TEXT("As tall as the tools (%.1f / %.1f)"), ToggleHeight, PresetHeight), ToggleHeight > 1.f && FMath::IsNearlyEqual(ToggleHeight, PresetHeight, 0.5f));

	FString OutDir;
	if (FParse::Value(FCommandLine::Get(), TEXT("PlannerSnapshotDir="), OutDir) && FApp::CanEverRender())
	{
		Widget->SetCatalogOpen(true);
		const FString File = FPaths::Combine(OutDir, TEXT("PlannerToolbar.png"));
		if (SnapshotToPng(Slate.ToSharedRef(), ScreenSize, File)) AddInfo(FString::Printf(TEXT("Toolbar snapshot: %s"), *File));
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Floor area: the selected room's own
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerSelectedRoomAreaTest, "MaxiMall.Planner.UI.SelectedRoomArea",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerSelectedRoomAreaTest::RunTest(const FString& Parameters)
{
	FScopedPlannerTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	TSharedPtr<SWidget> Slate;
	URoomPlannerWidget* Widget = CreatePlannerWidget(*this, TestWorld.World, Slate);
	if (!Widget) return false;
	ARoomPlannerManager* Manager = ARoomPlannerManager::GetOrCreateInstance(TestWorld.World);
	UTextBlock* Caption = Widget->WidgetTree ? Cast<UTextBlock>(Widget->WidgetTree->FindWidget(TEXT("TOTALFLOORAREA"))) : nullptr;
	if (!TestNotNull(TEXT("Manager"), Manager) || !TestNotNull(TEXT("TxtFloorArea"), Widget->TxtFloorArea.Get())) return false;

	// 6 x 4 m divided in two: 2.80 x 3.80 m clear each.
	Manager->AddWallBetweenPoints(FVector2D(0., 0.), FVector2D(600., 0.));
	Manager->AddWallBetweenPoints(FVector2D(600., 0.), FVector2D(600., 400.));
	Manager->AddWallBetweenPoints(FVector2D(600., 400.), FVector2D(0., 400.));
	Manager->AddWallBetweenPoints(FVector2D(0., 400.), FVector2D(0., 0.));
	Manager->AddWallBetweenPoints(FVector2D(300., 0.), FVector2D(300., 400.));

	Manager->SelectFloorAtWorldPos(FVector(150., 200., 0.));
	TestEqual(TEXT("Room picked: its own net area"), Widget->TxtFloorArea->GetText().ToString(), FString(TEXT("10.64 м²")));
	if (Caption) TestEqual(TEXT("Room picked: the caption says so"), Caption->GetText().ToString(), FString(TEXT("Площадь пола (комната)")));

	Manager->ClearAllSelection();
	TestEqual(TEXT("Nothing picked: all rooms together"), Widget->TxtFloorArea->GetText().ToString(), FString(TEXT("21.28 м²")));
	if (Caption) TestEqual(TEXT("Nothing picked: the designed caption"), Caption->GetText().ToString(), FString(TEXT("Площадь пола")));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Finish controls: the buttons wrap and the description breaks inside the side panel
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerFinishControlsFitTest, "MaxiMall.Planner.UI.FinishControlsFitPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerFinishControlsFitTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddInfo(TEXT("Skipped: no Slate application."));
		return true;
	}
	FScopedPlannerTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	TSharedPtr<SWidget> Slate;
	URoomPlannerWidget* Widget = CreatePlannerWidget(*this, TestWorld.World, Slate);
	if (!Widget) return false;
	UButton* TileButton = Widget->GetFinishTileButton();
	UWidget* Panel = Widget->WidgetTree ? Widget->WidgetTree->FindWidget(TEXT("LeftPanel")) : nullptr;
	if (!TestNotNull(TEXT("BtnFinishPaint"), Widget->BtnFinishPaint.Get()) || !TestNotNull(TEXT("BtnClearFinish"), Widget->BtnClearFinish.Get())
		|| !TestNotNull(TEXT("TxtFinishInfo"), Widget->TxtFinishInfo.Get()) || !TestNotNull(TEXT("«Плитка» button"), TileButton)
		|| !TestNotNull(TEXT("LeftPanel"), Panel)) return false;

	// Structure: the buttons in a wrap box, the description on its own line below them, wrapping.
	TestTrue(TEXT("Finish buttons are in a wrap box"), Cast<UWrapBox>(Widget->BtnFinishPaint->GetParent()) != nullptr);
	TestTrue(TEXT("«Плитка» next to «Отделка»"), TileButton->GetParent() == Widget->BtnFinishPaint->GetParent()
		&& TileButton->GetParent()->GetChildIndex(TileButton) == TileButton->GetParent()->GetChildIndex(Widget->BtnFinishPaint) + 1);
	TestTrue(TEXT("«Сбросить отделку» with them"), Widget->BtnClearFinish->GetParent() == Widget->BtnFinishPaint->GetParent());
	TestTrue(TEXT("Description below the buttons"), Cast<UVerticalBox>(Widget->TxtFinishInfo->GetParent()) != nullptr);
	TestTrue(TEXT("Description wraps"), Widget->TxtFinishInfo->GetAutoWrapText());
	TestTrue(TEXT("Description is dark on the white side panel"), Widget->TxtFinishInfo->GetColorAndOpacity().GetSpecifiedColor().GetLuminance() < 0.3f);

	// Everything shown at once, with the longest kind of description.
	Widget->BtnFinishPaint->SetVisibility(ESlateVisibility::Visible);
	TileButton->SetVisibility(ESlateVisibility::Visible);
	Widget->BtnClearFinish->SetVisibility(ESlateVisibility::Visible);
	Widget->TxtFinishInfo->SetText(FText::FromString(TEXT("Сторона в комнату: Плитка «Керамогранит графит матовый» 60×60 см, затирка светло-серая")));
	Widget->TxtFinishInfo->SetVisibility(ESlateVisibility::HitTestInvisible);

	const FVector2D ScreenSize(1920.f, 1080.f);
	PaintOffscreen(Slate.ToSharedRef(), ScreenSize);

	const float PanelRight = RightEdge(Panel);
	TestTrue(TEXT("Side panel laid out"), PanelRight > 100.f);
	const UWidget* Controls[] = { Widget->BtnFinishPaint.Get(), TileButton, Widget->BtnClearFinish.Get(), Widget->TxtFinishInfo.Get() };
	float ButtonsBottom = 0.f;
	for (const UWidget* Control : Controls)
	{
		TestTrue(FString::Printf(TEXT("%s ends inside the side panel (%.1f <= %.1f)"), *Control->GetName(), RightEdge(Control), PanelRight),
			RightEdge(Control) <= PanelRight + 0.5f);
		if (Control != Widget->TxtFinishInfo) ButtonsBottom = FMath::Max(ButtonsBottom, BottomEdge(Control));
	}
	TestTrue(TEXT("Description starts below the buttons"), TopEdge(Widget->TxtFinishInfo) >= ButtonsBottom - 0.5f);
	const float LineHeight = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->GetMaxCharacterHeight(Widget->TxtFinishInfo->GetFont());
	TestTrue(TEXT("Long description breaks into lines"), Widget->TxtFinishInfo->GetCachedGeometry().GetLocalSize().Y > 1.5f * LineHeight);

	FString OutDir;
	if (FParse::Value(FCommandLine::Get(), TEXT("PlannerSnapshotDir="), OutDir) && FApp::CanEverRender())
	{
		const FString File = FPaths::Combine(OutDir, TEXT("PlannerFinishControls.png"));
		if (SnapshotToPng(Slate.ToSharedRef(), ScreenSize, File)) AddInfo(FString::Printf(TEXT("Finish controls snapshot: %s"), *File));
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Opening dimension lines: geometry from the manager
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerOpeningDimensionLinesTest, "MaxiMall.Planner.Openings.DimensionLines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerOpeningDimensionLinesTest::RunTest(const FString& Parameters)
{
	FScopedPlannerTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;

	// 5 x 4 m room; the south wall runs from (0, 0) to (500, 0), 20 cm thick, the room on its left (+Y).
	const int32 N1 = Manager->AddNode(FVector2D(0., 0.));
	const int32 N2 = Manager->AddNode(FVector2D(500., 0.));
	const int32 N3 = Manager->AddNode(FVector2D(500., 400.));
	const int32 N4 = Manager->AddNode(FVector2D(0., 400.));
	const int32 South = Manager->AddWall(N1, N2);
	const int32 East = Manager->AddWall(N2, N3);
	Manager->AddWall(N3, N4);
	const int32 West = Manager->AddWall(N4, N1);
	TestTrue(TEXT("Door added"), Manager->AddOpeningToWall(South, EOpeningType::Door, 200.f, 90.f, 210.f, 0.f));
	Manager->RebuildRooms();

	auto Find = [](const TArray<FPlannerDimensionLine>& Lines, const TCHAR* Key) -> const FPlannerDimensionLine*
	{
		return Lines.FindByPredicate([Key](const FPlannerDimensionLine& L) { return L.Key == Key; });
	};
	auto Near = [](const FVector& A, const FVector& B) { return A.Equals(B, 0.01); };

	TestEqual(TEXT("Nothing selected: no dimension lines"), Manager->GetSelectionDimensionLines().Num(), 0);

	// A wall: its clear length on the selected face, between the inner faces of the west and east walls (x = 10 .. 490),
	// one row into the room, with extension lines from the inner corners.
	Manager->SelectedSegmentID = South;
	Manager->SelectedOpeningIndex = -1;
	Manager->bSelectedWallFaceLeft = true;
	{
		const TArray<FPlannerDimensionLine> Lines = Manager->GetSelectionDimensionLines();
		TestEqual(TEXT("A wall selected: one length line"), Lines.Num(), 1);
		const FPlannerDimensionLine* Length = Find(Lines, TEXT("length"));
		if (!TestNotNull(TEXT("Wall length"), Length)) return false;
		TestTrue(TEXT("Wall length: from inner corner to inner corner, in the room"), Near(Length->Start, FVector(10., 50., 2.)) && Near(Length->End, FVector(490., 50., 2.)));
		TestTrue(TEXT("Wall length: extension lines from the inner corners"), Near(Length->StartRef, FVector(10., 10., 2.)) && Near(Length->EndRef, FVector(490., 10., 2.)));
		TestTrue(TEXT("Wall length: the clear length 4.80 m (centre line 5.00 m)"), Length->Text == TEXT("4.80 м") && FMath::IsNearlyEqual(Length->Value, 480.f, 0.01f));
		const TArray<FPlannerDimensionLabel> Labels = Manager->GetSelectionDimensionLabels();
		const FPlannerDimensionLabel* LengthLabel = Labels.FindByPredicate([](const FPlannerDimensionLabel& Label) { return Label.Key == TEXT("length"); });
		TestTrue(TEXT("The length label carries the same clear length"), LengthLabel && FMath::IsNearlyEqual(LengthLabel->Value, 480.f, 0.01f));
	}
	// The outer face picked: its own length, from outer corner to outer corner, on that side.
	Manager->bSelectedWallFaceLeft = false;
	{
		const TArray<FPlannerDimensionLine> Lines = Manager->GetSelectionDimensionLines();
		const FPlannerDimensionLine* Length = Find(Lines, TEXT("length"));
		if (!TestNotNull(TEXT("Outer face length"), Length)) return false;
		TestTrue(TEXT("Outer face: 5.20 m outside the wall"), Length->Text == TEXT("5.20 м") && Near(Length->Start, FVector(-10., -50., 2.)) && Near(Length->End, FVector(510., -50., 2.)));
	}
	Manager->bSelectedWallFaceLeft = true;

	// The door: 90 cm wide, 200 cm from the start node → edges at 155 and 245.
	Manager->SelectedOpeningIndex = 0;
	{
		const TArray<FPlannerDimensionLine> Lines = Manager->GetSelectionDimensionLines();
		TestEqual(TEXT("Door: corner → door, door width, door → corner, door height, plan caption"), Lines.Num(), 5);
		const FPlannerDimensionLine* ToStart = Find(Lines, TEXT("distRight"));
		const FPlannerDimensionLine* Width = Find(Lines, TEXT("width"));
		const FPlannerDimensionLine* ToEnd = Find(Lines, TEXT("distLeft"));
		const FPlannerDimensionLine* Height = Find(Lines, TEXT("height"));
		if (!TestNotNull(TEXT("Start corner → door"), ToStart) || !TestNotNull(TEXT("Door width"), Width)
			|| !TestNotNull(TEXT("Door → end corner"), ToEnd) || !TestNotNull(TEXT("Door height"), Height)) return false;

		// One chain 40 cm into the room from the wall face (y = 10), just above the floor; arrows at the room's inner corners
		// (x = 10 and 490) and the door edges.
		TestTrue(TEXT("Corner line from the inner start corner"), Near(ToStart->Start, FVector(10., 50., 2.)) && Near(ToStart->End, FVector(155., 50., 2.)));
		TestTrue(TEXT("Width line across the door"), Near(Width->Start, FVector(155., 50., 2.)) && Near(Width->End, FVector(245., 50., 2.)));
		TestTrue(TEXT("Corner line to the inner end corner"), Near(ToEnd->Start, FVector(245., 50., 2.)) && Near(ToEnd->End, FVector(490., 50., 2.)));
		TestTrue(TEXT("Extension lines start on the wall face"), Near(Width->StartRef, FVector(155., 10., 2.)) && Near(Width->EndRef, FVector(245., 10., 2.))
			&& Near(ToStart->StartRef, FVector(10., 10., 2.)) && Near(ToEnd->EndRef, FVector(490., 10., 2.)));
		TestTrue(TEXT("Clear values: 1.45 / 0.90 / 2.45 m"), ToStart->Text == TEXT("1.45 м") && Width->Text == TEXT("0.90 м") && ToEnd->Text == TEXT("2.45 м"));
		TestTrue(TEXT("The chain adds up to the clear face length"), FMath::IsNearlyEqual(ToStart->Value + Width->Value + ToEnd->Value, 480.f, 0.01f));
		for (const FPlannerDimensionLine& Line : Lines)
		{
			TestTrue(FString::Printf(TEXT("%s: the line is as long as its value"), *Line.Key), FMath::IsNearlyEqual((float)FVector::Distance(Line.Start, Line.End), Line.Value, 0.01f));
		}

		// Same values as the distance labels (REQ-06).
		float L = 0.f, R = 0.f, Fl = 0.f, Nb = 0.f;
		bool bNb = false;
		TestTrue(TEXT("Distances"), Manager->GetOpeningDistances(South, 0, L, R, Fl, Nb, bNb));
		TestTrue(TEXT("Same distances as the REQ-06 distances"), FMath::IsNearlyEqual(ToStart->Value, R, 0.01f) && FMath::IsNearlyEqual(ToEnd->Value, L, 0.01f));
		const TArray<FPlannerDimensionLabel> Labels = Manager->GetSelectionDimensionLabels();
		const FPlannerDimensionLabel* LabelRight = Labels.FindByPredicate([](const FPlannerDimensionLabel& Label) { return Label.Key == TEXT("distRight"); });
		const FPlannerDimensionLabel* LabelLeft = Labels.FindByPredicate([](const FPlannerDimensionLabel& Label) { return Label.Key == TEXT("distLeft"); });
		TestTrue(TEXT("The distance labels carry the same clear values as the lines"), LabelRight && LabelLeft
			&& FMath::IsNearlyEqual(LabelRight->Value, ToStart->Value, 0.01f) && FMath::IsNearlyEqual(LabelLeft->Value, ToEnd->Value, 0.01f));

		// Picked from outside: the chain runs between the outer corners (x = -10 and 510); seen from there the start is on the left.
		Manager->bSelectedWallFaceLeft = false;
		const TArray<FPlannerDimensionLine> Outer = Manager->GetSelectionDimensionLines();
		const TArray<FPlannerDimensionLabel> OuterLabels = Manager->GetSelectionDimensionLabels();
		auto ValueOf = [](const TArray<FPlannerDimensionLine>& Ls, const TCHAR* Key) { const FPlannerDimensionLine* L = Ls.FindByPredicate([Key](const FPlannerDimensionLine& X) { return X.Key == Key; }); return L ? L->Value : -1.f; };
		auto LabelOf = [](const TArray<FPlannerDimensionLabel>& Ls, const TCHAR* Key) { const FPlannerDimensionLabel* L = Ls.FindByPredicate([Key](const FPlannerDimensionLabel& X) { return X.Key == Key; }); return L ? L->Value : -1.f; };
		TestTrue(TEXT("Outer face: 1.65 m and 2.65 m to the outer corners"), FMath::IsNearlyEqual(ValueOf(Outer, TEXT("distLeft")), 165.f, 0.01f) && FMath::IsNearlyEqual(ValueOf(Outer, TEXT("distRight")), 265.f, 0.01f));
		TestTrue(TEXT("Outer face: the labels agree"), FMath::IsNearlyEqual(LabelOf(OuterLabels, TEXT("distLeft")), 165.f, 0.01f) && FMath::IsNearlyEqual(LabelOf(OuterLabels, TEXT("distRight")), 265.f, 0.01f));
		Manager->bSelectedWallFaceLeft = true;

		// Height: beside the far jamb on the wall face, floor to the door top; vertical (a point in the plan).
		TestTrue(TEXT("Height line beside the jamb"), Near(Height->Start, FVector(260., 11., 0.)) && Near(Height->End, FVector(260., 11., 210.)));
		TestTrue(TEXT("Height extension lines from the jamb"), Near(Height->StartRef, FVector(245., 11., 0.)) && Near(Height->EndRef, FVector(245., 11., 210.)));
		TestTrue(TEXT("Height is vertical, the chain is not"), Height->bVertical && !Width->bVertical && !ToStart->bVertical && !ToEnd->bVertical);
		TestTrue(TEXT("Height names itself"), Height->Text == TEXT("выс. 2.10 м"));
		TestNull(TEXT("A door has no sill dimension"), Find(Lines, TEXT("sill")));

		// In the top-down plan the height is a caption outside the wall (opposite the chain), level with the wall top.
		const FPlannerDimensionLine* Caption = Find(Lines, TEXT("heightsPlan"));
		if (!TestNotNull(TEXT("Plan caption"), Caption)) return false;
		TestTrue(TEXT("Plan caption: plan only, the height"), Caption->bPlanOnly && !Caption->bVertical && Caption->Text == TEXT("выс. 2.10 м"));
		TestTrue(TEXT("Plan caption outside the wall at the middle of the door"), Near(Caption->Start, FVector(200., -15., 280.)) && Near(Caption->StartRef, FVector(200., 0., 280.)));
		TestTrue(TEXT("Only the plan caption is plan-only"), Lines.FilterByPredicate([](const FPlannerDimensionLine& L) { return L.bPlanOnly; }).Num() == 1);
	}

	// A window next to the door: 60 cm wide at 400 (edges 370 and 430), 120 cm high, sill at 90 cm.
	TestTrue(TEXT("Window added"), Manager->AddOpeningToWall(South, EOpeningType::Window, 400.f, 60.f, 120.f, 90.f));
	{
		Manager->SelectedOpeningIndex = 0;
		const TArray<FPlannerDimensionLine> Lines = Manager->GetSelectionDimensionLines();
		const FPlannerDimensionLine* Gap = Find(Lines, TEXT("distNeighbor"));
		if (!TestNotNull(TEXT("Door: gap to the window"), Gap)) return false;
		TestTrue(TEXT("Gap on the second row, door edge → window edge"), Near(Gap->Start, FVector(245., 90., 2.)) && Near(Gap->End, FVector(370., 90., 2.)));
		TestTrue(TEXT("Gap value"), Gap->Text == TEXT("1.25 м"));
	}
	{
		Manager->SelectedOpeningIndex = 1;
		const TArray<FPlannerDimensionLine> Lines = Manager->GetSelectionDimensionLines();
		TestEqual(TEXT("Window: chain of three, gap, height, sill, plan caption"), Lines.Num(), 7);
		const FPlannerDimensionLine* Width = Find(Lines, TEXT("width"));
		const FPlannerDimensionLine* Height = Find(Lines, TEXT("height"));
		const FPlannerDimensionLine* Sill = Find(Lines, TEXT("sill"));
		const FPlannerDimensionLine* Gap = Find(Lines, TEXT("distNeighbor"));
		if (!TestNotNull(TEXT("Window width"), Width) || !TestNotNull(TEXT("Window height"), Height)
			|| !TestNotNull(TEXT("Window sill"), Sill) || !TestNotNull(TEXT("Window gap"), Gap)) return false;
		TestTrue(TEXT("Window width line"), Near(Width->Start, FVector(370., 50., 2.)) && Near(Width->End, FVector(430., 50., 2.)));
		TestTrue(TEXT("Window gap back to the door"), Near(Gap->Start, FVector(245., 90., 2.)) && Near(Gap->End, FVector(370., 90., 2.)));
		TestTrue(TEXT("Window height from the sill to the top"), Near(Height->Start, FVector(445., 11., 90.)) && Near(Height->End, FVector(445., 11., 210.)));
		TestTrue(TEXT("Sill from the floor, beside the other jamb"), Near(Sill->Start, FVector(355., 11., 0.)) && Near(Sill->End, FVector(355., 11., 90.)));
		TestTrue(TEXT("Sill names itself"), Sill->Text == TEXT("от пола 0.90 м") && Sill->bVertical);
		const FPlannerDimensionLine* Caption = Find(Lines, TEXT("heightsPlan"));
		TestTrue(TEXT("Window plan caption: height and sill in one"), Caption && Caption->Text == TEXT("выс. 1.20 м · от пола 0.90 м")
			&& Near(Caption->Start, FVector(400., -15., 280.)));
	}

	// Vertical dimensions stay on the visible room-side face: on the east wall (from (500, 0) to (500, 400), room on its left = -X)
	// that face runs from y = 10 to y = 390, between the inner faces of the south and north walls.
	{
		// End jamb at 368: the height fits beside it (383 is still 7 cm short of the corner at 390).
		TestTrue(TEXT("Window near the east wall end"), Manager->AddOpeningToWall(East, EOpeningType::Window, 323.f, 90.f, 120.f, 90.f));
		Manager->SelectedSegmentID = East;
		Manager->SelectedOpeningIndex = 0;
		const TArray<FPlannerDimensionLine> Lines = Manager->GetSelectionDimensionLines();
		const FPlannerDimensionLine* Height = Find(Lines, TEXT("height"));
		const FPlannerDimensionLine* Sill = Find(Lines, TEXT("sill"));
		if (!TestNotNull(TEXT("Height"), Height) || !TestNotNull(TEXT("Sill"), Sill)) return false;
		TestTrue(TEXT("Height beside the end jamb, short of the corner"), Near(Height->Start, FVector(489., 383., 90.)) && Near(Height->End, FVector(489., 383., 210.)));
		TestTrue(TEXT("Sill beside the start jamb"), Near(Sill->Start, FVector(489., 263., 0.)) && Near(Sill->End, FVector(489., 263., 90.)));
		for (const FPlannerDimensionLine& Line : Lines)
		{
			if (!Line.bVertical) continue;
			TestTrue(FString::Printf(TEXT("%s stays on the room-side face (y 10..390)"), *Line.Key), Line.Start.Y >= 10. && Line.Start.Y <= 390.);
		}
	}
	{
		// West wall (from (0, 400) to (0, 0), room on its left = +X): end jamb at 380, beside it (395) would be inside the south wall,
		// so the height stands inside the opening.
		TestTrue(TEXT("Window near the west wall end"), Manager->AddOpeningToWall(West, EOpeningType::Window, 335.f, 90.f, 120.f, 90.f));
		Manager->SelectedSegmentID = West;
		Manager->SelectedOpeningIndex = 0;
		const TArray<FPlannerDimensionLine> Lines = Manager->GetSelectionDimensionLines();
		const FPlannerDimensionLine* Height = Find(Lines, TEXT("height"));
		const FPlannerDimensionLine* Sill = Find(Lines, TEXT("sill"));
		if (!TestNotNull(TEXT("Height"), Height) || !TestNotNull(TEXT("Sill"), Sill)) return false;
		TestTrue(TEXT("Height inside the opening"), Near(Height->Start, FVector(11., 35., 90.)) && Near(Height->End, FVector(11., 35., 210.)));
		TestTrue(TEXT("Sill beside its jamb"), Near(Sill->Start, FVector(11., 125., 0.)) && Near(Sill->End, FVector(11., 125., 90.)));
	}
	Manager->SelectedSegmentID = South;

	// While a control point is dragged: the live clear length of each wall at it, in place of the selection's dimensions. The
	// south-east corner moves from (500, 0) to (600, 0): the south face now runs from x = 10 to where it meets the slanted east
	// wall's inner face, and that face runs between the south and north inner faces.
	TestTrue(TEXT("Drag starts"), Manager->StartNodeDrag(N2));
	Manager->UpdateNodeDrag(FVector(600., 0., 0.));
	{
		// The south wall is selected with its outer face picked: its live length stays on that face (outer corners, y = -50).
		Manager->SelectedSegmentID = South;
		Manager->SelectedOpeningIndex = -1;
		Manager->bSelectedWallFaceLeft = false;
		const TArray<FPlannerDimensionLine> Lines = Manager->GetSelectionDimensionLines();
		const FPlannerDimensionLine* SouthLine = Lines.FindByPredicate([](const FPlannerDimensionLine& L) { return FMath::IsNearlyEqual(L.Start.Y, -50., 0.01); });
		TestTrue(TEXT("Dragging: the selected wall keeps its picked face"), SouthLine && FMath::IsNearlyEqual(SouthLine->Value, 622.81f, 0.05f));
		const TArray<FPlannerDimensionLabel> Labels = Manager->GetSelectionDimensionLabels();
		TestTrue(TEXT("Dragging: the length labels follow the same faces"), Labels.ContainsByPredicate([](const FPlannerDimensionLabel& L) { return L.Key == TEXT("length") && FMath::IsNearlyEqual(L.Value, 622.81f, 0.05f); })
			&& Labels.ContainsByPredicate([](const FPlannerDimensionLabel& L) { return L.Key == TEXT("length") && FMath::IsNearlyEqual(L.Value, 391.70f, 0.05f); }));
		Manager->bSelectedWallFaceLeft = true;
		Manager->ClearAllSelection();
	}
	{
		const TArray<FPlannerDimensionLine> Lines = Manager->GetSelectionDimensionLines();
		TestEqual(TEXT("Dragging: one line per wall at the corner"), Lines.Num(), 2);
		const FPlannerDimensionLine* SouthLine = Lines.FindByPredicate([](const FPlannerDimensionLine& L) { return FMath::IsNearlyEqual(L.Start.Y, 50., 0.01); });
		const FPlannerDimensionLine* EastLine = Lines.FindByPredicate([](const FPlannerDimensionLine& L) { return !FMath::IsNearlyEqual(L.Start.Y, 50., 0.01); });
		if (!TestNotNull(TEXT("South wall live length"), SouthLine) || !TestNotNull(TEXT("East wall live length"), EastLine)) return false;
		TestTrue(TEXT("South: live clear length 5.77 m"), SouthLine->Key == TEXT("length") && SouthLine->Text == TEXT("5.77 м")
			&& FMath::IsNearlyEqual(SouthLine->Value, 577.19f, 0.05f) && SouthLine->Start.Equals(FVector(10., 50., 2.), 0.05) && SouthLine->End.Equals(FVector(587.19, 50., 2.), 0.05));
		TestTrue(TEXT("East: live clear length 3.92 m along the slanted wall"), EastLine->Key == TEXT("length") && EastLine->Text == TEXT("3.92 м")
			&& FMath::IsNearlyEqual(EastLine->Value, 391.70f, 0.05f) && EastLine->Start.Equals(FVector(548.39, 0.30, 2.), 0.05) && EastLine->End.Equals(FVector(453.39, 380.30, 2.), 0.05));
	}
	int32 DraggedNode = -1;
	FVector2D DraggedTo;
	Manager->EndNodeDrag(DraggedNode, DraggedTo);

	Manager->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Interior objects and cabinet sets: clear gaps to the surrounding walls
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerObjectWallGapsTest, "MaxiMall.Planner.Objects.WallGaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerObjectWallGapsTest::RunTest(const FString& Parameters)
{
	using PlannerDimensions::EFootprintSide;
	auto Near2 = [](const FVector2D& A, const FVector2D& B) { return A.Equals(B, 0.01); };
	auto GapOn = [](const TArray<PlannerDimensions::FFootprintGap>& Gaps, EFootprintSide Side) -> const PlannerDimensions::FFootprintGap*
	{
		return Gaps.FindByPredicate([Side](const PlannerDimensions::FFootprintGap& G) { return G.Side == Side; });
	};
	auto Box = [](double X0, double Y0, double X1, double Y1) { return TArray<FVector2D>{ FVector2D(X0, Y0), FVector2D(X1, Y0), FVector2D(X1, Y1), FVector2D(X0, Y1) }; };
	// The 5 x 4 m room with 20 cm walls: inner faces at x = 10 / 490, y = 10 / 390.
	const TArray<TArray<FVector2D>> Room = { Box(-10., -10., 510., 10.), Box(490., -10., 510., 410.), Box(-10., 390., 510., 410.), Box(-10., -10., 10., 410.) };

	// A 100 x 60 footprint at (200, 150), square to the room: one gap per side, from the middle of the side to the inner face.
	{
		const TArray<PlannerDimensions::FFootprintGap> Gaps = PlannerDimensions::FootprintGapsToWalls(FVector2D(200., 150.), FVector2D(1., 0.), FVector2D(50., 30.), Room, 1.f, 2000.f);
		TestEqual(TEXT("Four gaps"), Gaps.Num(), 4);
		const PlannerDimensions::FFootprintGap* Front = GapOn(Gaps, EFootprintSide::PlusX);
		const PlannerDimensions::FFootprintGap* Back = GapOn(Gaps, EFootprintSide::MinusX);
		const PlannerDimensions::FFootprintGap* Right = GapOn(Gaps, EFootprintSide::PlusY);
		const PlannerDimensions::FFootprintGap* Left = GapOn(Gaps, EFootprintSide::MinusY);
		if (!Front || !Back || !Right || !Left) { AddError(TEXT("A side has no gap")); return false; }
		TestTrue(TEXT("+X: 2.40 m to the east wall"), Near2(Front->From, FVector2D(250., 150.)) && Near2(Front->To, FVector2D(490., 150.)) && FMath::IsNearlyEqual(Front->Distance, 240.f, 0.01f));
		TestTrue(TEXT("-X: 1.40 m to the west wall"), Near2(Back->From, FVector2D(150., 150.)) && Near2(Back->To, FVector2D(10., 150.)) && FMath::IsNearlyEqual(Back->Distance, 140.f, 0.01f));
		TestTrue(TEXT("+Y: 2.10 m to the north wall"), Near2(Right->From, FVector2D(200., 180.)) && Near2(Right->To, FVector2D(200., 390.)) && FMath::IsNearlyEqual(Right->Distance, 210.f, 0.01f));
		TestTrue(TEXT("-Y: 1.10 m to the south wall"), Near2(Left->From, FVector2D(200., 120.)) && Near2(Left->To, FVector2D(200., 10.)) && FMath::IsNearlyEqual(Left->Distance, 110.f, 0.01f));
	}
	// Turned by 90 degrees: the sides follow the footprint's own axes.
	{
		const TArray<PlannerDimensions::FFootprintGap> Gaps = PlannerDimensions::FootprintGapsToWalls(FVector2D(200., 150.), FVector2D(0., 1.), FVector2D(50., 30.), Room, 1.f, 2000.f);
		const PlannerDimensions::FFootprintGap* Front = GapOn(Gaps, EFootprintSide::PlusX);
		const PlannerDimensions::FFootprintGap* Right = GapOn(Gaps, EFootprintSide::PlusY);
		TestTrue(TEXT("Turned: +X faces the north wall (1.90 m)"), Front && FMath::IsNearlyEqual(Front->Distance, 190.f, 0.01f) && Near2(Front->To, FVector2D(200., 390.)));
		TestTrue(TEXT("Turned: +Y faces the west wall (1.60 m)"), Right && FMath::IsNearlyEqual(Right->Distance, 160.f, 0.01f) && Near2(Right->To, FVector2D(10., 150.)));
	}
	// At 45 degrees: measured square to the footprint's side, from the part of it nearest a wall (its end 1 cm in from the corner
	// nearest the north wall).
	{
		const float S = FMath::Sqrt(0.5f);
		const TArray<PlannerDimensions::FFootprintGap> Gaps = PlannerDimensions::FootprintGapsToWalls(FVector2D(250., 200.), FVector2D(S, S), FVector2D(50., 30.), Room, 1.f, 2000.f);
		const PlannerDimensions::FFootprintGap* Front = GapOn(Gaps, EFootprintSide::PlusX);
		TestTrue(TEXT("45 degrees: +X side, 1.90 m to the north wall from its nearest part"), Front
			&& FMath::IsNearlyEqual(Front->Distance, (390.f - (200.f + 79.f * S)) / S, 0.02f) && FMath::IsNearlyEqual(Front->To.Y, 390., 0.01));
	}
	// Against a wall: no gap on that side; cutting into a wall: none either (not the far side of the wall).
	{
		const TArray<PlannerDimensions::FFootprintGap> Touching = PlannerDimensions::FootprintGapsToWalls(FVector2D(60., 150.), FVector2D(1., 0.), FVector2D(50., 30.), Room, 1.f, 2000.f);
		TestTrue(TEXT("Against the west wall: no gap there"), Touching.Num() == 3 && !GapOn(Touching, EFootprintSide::MinusX));
		const TArray<PlannerDimensions::FFootprintGap> Cutting = PlannerDimensions::FootprintGapsToWalls(FVector2D(55., 150.), FVector2D(1., 0.), FVector2D(50., 30.), Room, 1.f, 2000.f);
		TestTrue(TEXT("Into the west wall: no gap there"), Cutting.Num() == 3 && !GapOn(Cutting, EFootprintSide::MinusX));
	}
	// A short wall in front of only part of a side (not in front of its middle): the gap is to that wall, where it is.
	{
		TArray<TArray<FVector2D>> WithStub = Room;
		WithStub.Add(Box(230., 250., 260., 270.));
		const TArray<PlannerDimensions::FFootprintGap> Gaps = PlannerDimensions::FootprintGapsToWalls(FVector2D(200., 150.), FVector2D(1., 0.), FVector2D(50., 30.), WithStub, 1.f, 2000.f);
		const PlannerDimensions::FFootprintGap* Right = GapOn(Gaps, EFootprintSide::PlusY);
		TestTrue(TEXT("Partly covered side: 0.70 m to the short wall"), Right && FMath::IsNearlyEqual(Right->Distance, 70.f, 0.01f)
			&& FMath::IsNearlyEqual(Right->To.Y, 250., 0.01) && Right->From.X >= 230. && Right->From.X <= 250.);
	}
	// Pushed into the south-west corner: flush with the south wall (no gap there), 20 cm from the west wall. The south wall touches
	// the west side's end, which does not stop that side's gap.
	{
		const TArray<PlannerDimensions::FFootprintGap> Gaps = PlannerDimensions::FootprintGapsToWalls(FVector2D(80., 40.), FVector2D(1., 0.), FVector2D(50., 30.), Room, 1.f, 2000.f);
		const PlannerDimensions::FFootprintGap* Back = GapOn(Gaps, EFootprintSide::MinusX);
		TestTrue(TEXT("In the corner: no gap to the wall it stands against"), !GapOn(Gaps, EFootprintSide::MinusY));
		TestTrue(TEXT("In the corner: 0.20 m to the west wall"), Back && FMath::IsNearlyEqual(Back->Distance, 20.f, 0.01f));
	}
	// Turned 5 degrees with one corner 2 cm into the east wall (a room beyond it, its far wall at x = 890): the side's gap is the
	// 6.7 cm to that wall, not a line through it into the next room.
	{
		TArray<TArray<FVector2D>> TwoRooms = Room;
		TwoRooms.Add(Box(890., -10., 910., 410.));
		const float Turn = FMath::DegreesToRadians(5.f);
		const FVector2D Axis(FMath::Cos(Turn), FMath::Sin(Turn));
		const FVector2D Mid(483.3, 200.);
		const TArray<PlannerDimensions::FFootprintGap> Gaps = PlannerDimensions::FootprintGapsToWalls(Mid - Axis * 30., Axis, FVector2D(30., 100.), TwoRooms, 1.f, 2000.f);
		const PlannerDimensions::FFootprintGap* Front = GapOn(Gaps, EFootprintSide::PlusX);
		TestTrue(TEXT("Corner in the wall: the gap is to that wall"), Front && Front->Distance < 10.f && FMath::IsNearlyEqual(Front->To.X, 490., 0.05));
	}
	// A short wall flush with part of a side (between the points the side is measured from): the side touches it, no gap.
	{
		TArray<TArray<FVector2D>> WithFlushStub = Room;
		WithFlushStub.Add(Box(215., 180., 235., 390.));
		const TArray<PlannerDimensions::FFootprintGap> Gaps = PlannerDimensions::FootprintGapsToWalls(FVector2D(200., 150.), FVector2D(1., 0.), FVector2D(50., 30.), WithFlushStub, 1.f, 2000.f);
		TestTrue(TEXT("Flush with part of a side: no gap there"), !GapOn(Gaps, EFootprintSide::PlusY));
	}
	// An open side (no north wall): no gap there.
	{
		const TArray<TArray<FVector2D>> ThreeWalls = { Room[0], Room[1], Room[3] };
		const TArray<PlannerDimensions::FFootprintGap> Gaps = PlannerDimensions::FootprintGapsToWalls(FVector2D(200., 150.), FVector2D(1., 0.), FVector2D(50., 30.), ThreeWalls, 1.f, 2000.f);
		TestTrue(TEXT("Open side: no gap"), Gaps.Num() == 3 && !GapOn(Gaps, EFootprintSide::PlusY));
	}

	// Real catalog items in a planner room: every gap line runs from the item to an inner wall face.
	FScopedPlannerTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	const int32 N1 = Manager->AddNode(FVector2D(0., 0.));
	const int32 N2 = Manager->AddNode(FVector2D(500., 0.));
	const int32 N3 = Manager->AddNode(FVector2D(500., 400.));
	const int32 N4 = Manager->AddNode(FVector2D(0., 400.));
	const int32 South = Manager->AddWall(N1, N2);
	Manager->AddWall(N2, N3);
	Manager->AddWall(N3, N4);
	Manager->AddWall(N4, N1);
	Manager->RebuildRooms();
	auto OnInnerFace = [](const FPlannerDimensionLine& L)
	{
		return FMath::IsNearlyEqual(L.End.X, 10., 0.05) || FMath::IsNearlyEqual(L.End.X, 490., 0.05)
			|| FMath::IsNearlyEqual(L.End.Y, 10., 0.05) || FMath::IsNearlyEqual(L.End.Y, 390., 0.05);
	};

	const TArray<FPlannerCatalogEntry> Objects = Manager->GetAvailableObjects();
	if (Objects.Num() == 0)
	{
		AddError(TEXT("DT_PlannerObjects has no rows: object gaps in a room cannot be checked."));
	}
	else
	{
		const FString ObjectID = Manager->AddPlacedObject(Objects[0].ID, FVector(250., 200., 0.), FRotator::ZeroRotator, FVector::OneVector);
		if (TestFalse(TEXT("Object placed"), ObjectID.IsEmpty()))
		{
			Manager->ClearAllSelection();
			Manager->SelectedObjectID = ObjectID;
			const TArray<FPlannerDimensionLine> Lines = Manager->GetSelectionDimensionLines();
			TestEqual(FString::Printf(TEXT("Object %s in the middle of the room: four gaps"), *Objects[0].ID), Lines.Num(), 4);
			const FPlannerDimensionLine* Front = Lines.FindByPredicate([](const FPlannerDimensionLine& L) { return L.Key == TEXT("gapFront"); });
			const FPlannerDimensionLine* Back = Lines.FindByPredicate([](const FPlannerDimensionLine& L) { return L.Key == TEXT("gapBack"); });
			TestTrue(TEXT("Object: front gap to the east wall, back gap to the west wall"), Front && Back
				&& FMath::IsNearlyEqual(Front->End.X, 490., 0.05) && FMath::IsNearlyEqual(Back->End.X, 10., 0.05) && Front->Start.X > Back->Start.X);
			for (const FPlannerDimensionLine& Line : Lines)
			{
				TestTrue(FString::Printf(TEXT("Object %s ends on an inner wall face"), *Line.Key), OnInnerFace(Line));
				TestTrue(FString::Printf(TEXT("Object %s: the line is as long as its value"), *Line.Key), FMath::IsNearlyEqual((float)FVector::Distance(Line.Start, Line.End), Line.Value, 0.05f));
			}
			// Turned while being moved: the gaps follow.
			Manager->MovePlacedObjectLocal(ObjectID, FVector(250., 200., 0.), FRotator(0., 90., 0.));
			const TArray<FPlannerDimensionLine> Turned = Manager->GetSelectionDimensionLines();
			const FPlannerDimensionLine* TurnedFront = Turned.FindByPredicate([](const FPlannerDimensionLine& L) { return L.Key == TEXT("gapFront"); });
			TestTrue(TEXT("Turned object: its front gap now reaches the north wall"), TurnedFront && FMath::IsNearlyEqual(TurnedFront->End.Y, 390., 0.05));
		}
	}

	const TArray<FPlannerCatalogEntry> Sets = Manager->GetAvailableCabinetSets();
	if (Sets.Num() == 0)
	{
		AddError(TEXT("DT_FurnitureCatalog offers no cabinet sets: cabinet set gaps cannot be checked."));
	}
	else
	{
		const FString SetID = Manager->AddCabinetSetOnWall(FName(*Sets[0].ID), South, 250.f, true);
		AShowroomBooth* Booth = Manager->FindCabinetSetActor(SetID);
		if (!TestFalse(TEXT("Cabinet set placed"), SetID.IsEmpty()) || !TestNotNull(TEXT("Cabinet set booth"), Booth)) return false;
		// A cabinet set dresses itself (its meshes) in BeginPlay, which the game runs on spawn, before the placement measures how far
		// it reaches back; this bare test world never begins play. Dress it, then stand it flush against the wall as the game does.
		if (!Booth->HasActorBegunPlay()) Booth->DispatchBeginPlay();
		{
			double RearY = TNumericLimits<double>::Max();
			TArray<UStaticMeshComponent*> MeshComps;
			Booth->GetComponents(MeshComps);
			for (const UStaticMeshComponent* Comp : MeshComps)
			{
				if (!Comp || !Comp->GetStaticMesh() || !Comp->IsVisible()) continue;
				const FBox MeshBox = Comp->GetStaticMesh()->GetBoundingBox();
				for (int32 Corner = 0; Corner < 8; ++Corner)
				{
					RearY = FMath::Min(RearY, Comp->GetComponentTransform().TransformPosition(FVector((Corner & 1) ? MeshBox.Max.X : MeshBox.Min.X,
						(Corner & 2) ? MeshBox.Max.Y : MeshBox.Min.Y, (Corner & 4) ? MeshBox.Max.Z : MeshBox.Min.Z)).Y);
				}
			}
			if (!TestTrue(TEXT("Cabinet set has visible parts"), RearY < TNumericLimits<double>::Max())) return false;
			Booth->AddActorWorldOffset(FVector(0., 10. - RearY, 0.));
		}
		{
			Manager->ClearAllSelection();
			Manager->SelectedCabinetSetID = SetID;
			const TArray<FPlannerDimensionLine> Lines = Manager->GetSelectionDimensionLines();
			TestEqual(FString::Printf(TEXT("Cabinet set %s flush against the south wall: front and side gaps only"), *Sets[0].ID), Lines.Num(), 3);
			for (const FPlannerDimensionLine& Line : Lines)
			{
				TestTrue(FString::Printf(TEXT("Cabinet set %s ends on an inner wall face"), *Line.Key), OnInnerFace(Line) && Line.Value >= 1.f);
				TestFalse(FString::Printf(TEXT("Cabinet set %s: no gap to the wall it stands against"), *Line.Key), FMath::IsNearlyEqual(Line.End.Y, 10., 0.05));
			}
		}
	}
	Manager->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Live wall lengths while a corner is dragged
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerDragDimensionsTest, "MaxiMall.Planner.Walls.DragDimensions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerDragDimensionsTest::RunTest(const FString& Parameters)
{
	auto LengthOf = [](const TArray<FPlannerDimensionLine>& Lines, TFunctionRef<bool(const FPlannerDimensionLine&)> Pick) -> float
	{
		const FPlannerDimensionLine* Line = Lines.FindByPredicate(Pick);
		return Line ? Line->Value : -1.f;
	};

	// A room whose east wall was drawn the other way round (its room side is its right face).
	{
		FScopedPlannerTestWorld TestWorld;
		if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
		ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
		if (!TestNotNull(TEXT("Manager"), Manager)) return false;
		const int32 N1 = Manager->AddNode(FVector2D(0., 0.));
		const int32 N2 = Manager->AddNode(FVector2D(500., 0.));
		const int32 N3 = Manager->AddNode(FVector2D(500., 400.));
		const int32 N4 = Manager->AddNode(FVector2D(0., 400.));
		Manager->AddWall(N1, N2);
		Manager->AddWall(N3, N2);
		Manager->AddWall(N3, N4);
		Manager->AddWall(N4, N1);
		Manager->RebuildRooms();
		TestTrue(TEXT("Reversed: drag starts"), Manager->StartNodeDrag(N2));
		Manager->UpdateNodeDrag(FVector(600., 0., 0.));
		const TArray<FPlannerDimensionLine> Lines = Manager->GetSelectionDimensionLines();
		const FPlannerDimensionLine* East = Lines.FindByPredicate([](const FPlannerDimensionLine& L) { return !FMath::IsNearlyEqual(L.Start.Y, 50., 0.01); });
		TestTrue(TEXT("Reversed wall: its room side, 3.92 m"), East && FMath::IsNearlyEqual(East->Value, 391.70f, 0.05f)
			&& East->Start.Equals(FVector(453.39, 380.30, 2.), 0.05) && East->End.Equals(FVector(548.39, 0.30, 2.), 0.05));
		int32 Dragged = -1;
		FVector2D DraggedTo;
		Manager->EndNodeDrag(Dragged, DraggedTo);
		Manager->Destroy();
	}

	// An open chain closed by dragging its free end onto the first corner: while snapped there, the dragged wall already ends at
	// the south wall's inner face (3.80 m), as it does once released and joined; the chain bounds no room yet, so its concave
	// (inner) side is shown.
	{
		FScopedPlannerTestWorld TestWorld;
		if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
		ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
		if (!TestNotNull(TEXT("Manager"), Manager)) return false;
		const int32 N1 = Manager->AddNode(FVector2D(0., 0.));
		const int32 N2 = Manager->AddNode(FVector2D(500., 0.));
		const int32 N3 = Manager->AddNode(FVector2D(500., 400.));
		const int32 N4 = Manager->AddNode(FVector2D(0., 400.));
		const int32 N5 = Manager->AddNode(FVector2D(0., 100.));
		Manager->AddWall(N1, N2);
		Manager->AddWall(N2, N3);
		Manager->AddWall(N3, N4);
		const int32 West = Manager->AddWall(N4, N5);
		Manager->RebuildRooms();
		TestTrue(TEXT("Closing: drag starts"), Manager->StartNodeDrag(N5));
		Manager->UpdateNodeDrag(FVector(0., 0., 0.));
		const float Live = LengthOf(Manager->GetSelectionDimensionLines(), [](const FPlannerDimensionLine& L) { return FMath::IsNearlyEqual(L.Start.X, 50., 0.01) || FMath::IsNearlyEqual(L.End.X, 50., 0.01); });
		TestTrue(FString::Printf(TEXT("Closing: live length 3.80 m on the inner side (got %.2f)"), Live), FMath::IsNearlyEqual(Live, 380.f, 0.05f));
		int32 Dragged = -1;
		FVector2D DraggedTo;
		Manager->EndNodeDrag(Dragged, DraggedTo);
		Manager->MoveNode(Dragged, DraggedTo); // the server joins the corners on release
		Manager->SelectedSegmentID = West;
		Manager->SelectedOpeningIndex = -1;
		Manager->bSelectedWallFaceLeft = true;
		const float Joined = LengthOf(Manager->GetSelectionDimensionLines(), [](const FPlannerDimensionLine& L) { return L.Key == TEXT("length"); });
		TestTrue(FString::Printf(TEXT("Closing: the same length once joined (got %.2f)"), Joined), FMath::IsNearlyEqual(Joined, 380.f, 0.05f));
		Manager->Destroy();
	}

	// The same, drawn the other way round (the inner side is now each wall's right face): still the inner side, 4.80 m.
	{
		FScopedPlannerTestWorld TestWorld;
		if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
		ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
		if (!TestNotNull(TEXT("Manager"), Manager)) return false;
		const int32 N1 = Manager->AddNode(FVector2D(0., 0.));
		const int32 N4 = Manager->AddNode(FVector2D(0., 400.));
		const int32 N3 = Manager->AddNode(FVector2D(500., 400.));
		const int32 N2 = Manager->AddNode(FVector2D(500., 0.));
		const int32 N5 = Manager->AddNode(FVector2D(100., 0.));
		Manager->AddWall(N1, N4);
		Manager->AddWall(N4, N3);
		Manager->AddWall(N3, N2);
		Manager->AddWall(N2, N5);
		Manager->RebuildRooms();
		TestTrue(TEXT("Clockwise: drag starts"), Manager->StartNodeDrag(N5));
		Manager->UpdateNodeDrag(FVector(0., 0., 0.));
		const TArray<FPlannerDimensionLine> Lines = Manager->GetSelectionDimensionLines();
		const float Live = Lines.Num() == 1 ? Lines[0].Value : -1.f;
		TestTrue(FString::Printf(TEXT("Clockwise: live length 4.80 m on the inner side (got %.2f)"), Live), FMath::IsNearlyEqual(Live, 480.f, 0.05f)
			&& Lines.Num() == 1 && FMath::IsNearlyEqual(Lines[0].Start.Y, 50., 0.01));
		int32 Dragged = -1;
		FVector2D DraggedTo;
		Manager->EndNodeDrag(Dragged, DraggedTo);
		Manager->Destroy();
	}

	// A partition dragged onto the middle of the south wall: while snapped there it already ends at that wall's inner face.
	{
		FScopedPlannerTestWorld TestWorld;
		if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
		ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
		if (!TestNotNull(TEXT("Manager"), Manager)) return false;
		const int32 N1 = Manager->AddNode(FVector2D(0., 0.));
		const int32 N2 = Manager->AddNode(FVector2D(500., 0.));
		const int32 N3 = Manager->AddNode(FVector2D(500., 400.));
		const int32 N4 = Manager->AddNode(FVector2D(0., 400.));
		const int32 N6 = Manager->AddNode(FVector2D(250., 200.));
		const int32 N7 = Manager->AddNode(FVector2D(250., 100.));
		Manager->AddWall(N1, N2);
		Manager->AddWall(N2, N3);
		Manager->AddWall(N3, N4);
		Manager->AddWall(N4, N1);
		Manager->AddWall(N6, N7);
		Manager->RebuildRooms();
		TestTrue(TEXT("T: drag starts"), Manager->StartNodeDrag(N7));
		Manager->UpdateNodeDrag(FVector(250., 0., 0.));
		const TArray<FPlannerDimensionLine> Lines = Manager->GetSelectionDimensionLines();
		TestEqual(TEXT("T: one wall at the dragged corner"), Lines.Num(), 1);
		const float Live = Lines.Num() > 0 ? Lines[0].Value : -1.f;
		TestTrue(FString::Printf(TEXT("T: live length 1.90 m to the south wall's inner face (got %.2f)"), Live), FMath::IsNearlyEqual(Live, 190.f, 0.05f));
		int32 Dragged = -1;
		FVector2D DraggedTo;
		Manager->EndNodeDrag(Dragged, DraggedTo);
		Manager->Destroy();
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Visible extent of a wall face: where the room-side corners really are
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerVisibleFaceExtentTest, "MaxiMall.Planner.Openings.VisibleFaceExtent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerVisibleFaceExtentTest::RunTest(const FString& Parameters)
{
	// Two walls from one node (0, 0), not mitred (plain corner points at the node): a 3 m wall along +X and a neighbour.
	auto MakeWall = [](int32 Id, const FVector2D& To, float Half)
	{
		FPlannerWallFaceInput Wall;
		Wall.SegmentID = Id;
		Wall.StartNodeID = 1;
		Wall.EndNodeID = Id + 1;
		Wall.Start = FVector2D(0., 0.);
		Wall.End = To;
		const FVector2D Left = FVector2D(-To.Y, To.X).GetSafeNormal();
		Wall.StartCorner[0] = Left * Half;
		Wall.StartCorner[1] = -Left * Half;
		Wall.EndCorner[0] = To + Left * Half;
		Wall.EndCorner[1] = To - Left * Half;
		return Wall;
	};
	// A 60 cm neighbour at a right angle: the room-side face of a 10 cm wall starts at its face, 30 cm from the node.
	{
		const TArray<FPlannerWallFaceInput> Walls = { MakeWall(1, FVector2D(300., 0.), 5.f), MakeWall(2, FVector2D(0., 300.), 30.f) };
		const FVector2D Extent = PlannerFinishLayout::VisibleFaceExtent(Walls, 0, 0);
		TestTrue(TEXT("Thick neighbour: face starts at its face (30 cm)"), FMath::IsNearlyEqual(Extent.X, 30., 0.01));
		TestTrue(TEXT("Free end: face runs to the corner point"), FMath::IsNearlyEqual(Extent.Y, 300., 0.01));
	}
	// A slight (3 degree) bend from a 10 cm wall into a 20 cm wall, not mitred (a step): the face lines cross about 1 m back along
	// the first wall, beyond the second one, so each face runs on to its own corner.
	{
		const float Bend = FMath::DegreesToRadians(3.f);
		FPlannerWallFaceInput A;
		A.SegmentID = 1; A.StartNodeID = 1; A.EndNodeID = 2;
		A.Start = FVector2D(0., 0.); A.End = FVector2D(400., 0.);
		A.StartCorner[0] = FVector2D(0., 5.); A.StartCorner[1] = FVector2D(0., -5.);
		A.EndCorner[0] = FVector2D(400., 5.); A.EndCorner[1] = FVector2D(400., -5.);
		FPlannerWallFaceInput B;
		B.SegmentID = 2; B.StartNodeID = 2; B.EndNodeID = 3;
		B.Start = FVector2D(400., 0.); B.End = B.Start + FVector2D(FMath::Cos(Bend), FMath::Sin(Bend)) * 400.f;
		const FVector2D LeftB = FVector2D(-FMath::Sin(Bend), FMath::Cos(Bend)) * 10.f;
		B.StartCorner[0] = B.Start + LeftB; B.StartCorner[1] = B.Start - LeftB;
		B.EndCorner[0] = B.End + LeftB; B.EndCorner[1] = B.End - LeftB;
		const TArray<FPlannerWallFaceInput> Walls = { A, B };
		TestTrue(TEXT("Slight bend: the thin wall's left face runs to its corner"), FMath::IsNearlyEqual(PlannerFinishLayout::VisibleFaceExtent(Walls, 0, 0).Y, 400., 0.05));
		TestTrue(TEXT("Slight bend: the thick wall's right face starts at its corner"), FMath::IsNearlyEqual(PlannerFinishLayout::VisibleFaceExtent(Walls, 1, 1).X, 0., 0.05));
	}
	// A 20 cm neighbour at 30 degrees: the inner corner lies h * cot(15 deg) = 37.32 cm from the node.
	{
		const float Angle = FMath::DegreesToRadians(30.f);
		const TArray<FPlannerWallFaceInput> Walls = { MakeWall(1, FVector2D(300., 0.), 10.f), MakeWall(2, FVector2D(300. * FMath::Cos(Angle), 300. * FMath::Sin(Angle)), 10.f) };
		const FVector2D Extent = PlannerFinishLayout::VisibleFaceExtent(Walls, 0, 0);
		TestTrue(TEXT("Acute neighbour: face starts at the inner corner"), FMath::IsNearlyEqual(Extent.X, 37.32, 0.05));
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dimension drawing: arrows, extension lines and the value's place
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerDimensionLayoutTest, "MaxiMall.Planner.UI.DimensionLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerDimensionLayoutTest::RunTest(const FString& Parameters)
{
	const PlannerDimensionLayout::FStyle Style;
	const FVector2D TextSize(40.f, 14.f);   // box 50 x 16 with the default padding
	auto Near = [](const FVector2D& A, const FVector2D& B) { return A.Equals(B, 0.01); };

	// A wall face below the line (refs at y = 140): a long dimension has its value centred on the line, arrows inside.
	{
		FPlannerScreenDimension D;
		D.Start = FVector2D(100., 100.); D.End = FVector2D(300., 100.);
		D.StartRef = FVector2D(100., 140.); D.EndRef = FVector2D(300., 140.);
		const PlannerDimensionLayout::FResult R = PlannerDimensionLayout::Compute(D, TextSize, Style);
		TestTrue(TEXT("Long: line drawn"), R.bDrawLine && Near(R.LineFrom, D.Start) && Near(R.LineTo, D.End));
		TestTrue(TEXT("Long: value centred on the line"), R.bTextOnLine && Near(R.TextCenter, FVector2D(200., 100.)));
		TestTrue(TEXT("Long: start arrow points at the start"), Near(R.StartArrow[0], D.Start) && Near((R.StartArrow[1] + R.StartArrow[2]) * 0.5, FVector2D(109., 100.)));
		TestTrue(TEXT("Long: end arrow points at the end"), Near(R.EndArrow[0], D.End) && Near((R.EndArrow[1] + R.EndArrow[2]) * 0.5, FVector2D(291., 100.)));
		TestTrue(TEXT("Long: arrowheads are 7 px wide"), FMath::IsNearlyEqual(FVector2D::Distance(R.StartArrow[1], R.StartArrow[2]), 7.0, 0.01));
		TestTrue(TEXT("Long: extension lines from the wall past the line"), R.bHasStartExtension && R.bHasEndExtension
			&& Near(R.StartExtension[0], FVector2D(100., 138.)) && Near(R.StartExtension[1], FVector2D(100., 94.))
			&& Near(R.EndExtension[0], FVector2D(300., 138.)) && Near(R.EndExtension[1], FVector2D(300., 94.)));
		TestTrue(TEXT("Long: box = text + padding"), Near(R.BoxSize, FVector2D(50., 16.)));
	}

	// Too short for the value between the arrows: arrows stay inside, the value moves beside the line, away from the wall.
	{
		FPlannerScreenDimension D;
		D.Start = FVector2D(100., 100.); D.End = FVector2D(150., 100.);
		D.StartRef = FVector2D(100., 140.); D.EndRef = FVector2D(150., 140.);
		const PlannerDimensionLayout::FResult R = PlannerDimensionLayout::Compute(D, TextSize, Style);
		TestTrue(TEXT("Short: arrows inside"), R.bDrawLine && !R.bArrowsOutside && !R.bTextOnLine);
		TestTrue(TEXT("Short: value centred beside the line, on the far side from the wall"), Near(R.TextCenter, FVector2D(125., 100. - (8. + 3. + 3.5))));
	}

	// Very short: the arrows point in from outside and the line runs on past them.
	{
		FPlannerScreenDimension D;
		D.Start = FVector2D(100., 100.); D.End = FVector2D(110., 100.);
		D.StartRef = D.Start; D.EndRef = D.End; // no measured points apart from the tips
		const PlannerDimensionLayout::FResult R = PlannerDimensionLayout::Compute(D, TextSize, Style);
		TestTrue(TEXT("Very short: arrows outside"), R.bArrowsOutside);
		TestTrue(TEXT("Very short: arrow tips on the measured points, bases outside"), Near(R.StartArrow[0], D.Start) && Near((R.StartArrow[1] + R.StartArrow[2]) * 0.5, FVector2D(91., 100.))
			&& Near(R.EndArrow[0], D.End) && Near((R.EndArrow[1] + R.EndArrow[2]) * 0.5, FVector2D(119., 100.)));
		TestTrue(TEXT("Very short: line runs past the arrows"), Near(R.LineFrom, FVector2D(85., 100.)) && Near(R.LineTo, FVector2D(125., 100.)));
		TestTrue(TEXT("Very short: no extension lines without measured points"), !R.bHasStartExtension && !R.bHasEndExtension);
		TestTrue(TEXT("Very short: value above the line"), R.TextCenter.Y < 100.);
	}

	// A vertical line on screen (a height in 3D): the value fits between the arrows.
	{
		FPlannerScreenDimension D;
		D.Start = FVector2D(400., 300.); D.End = FVector2D(400., 200.);
		D.StartRef = FVector2D(380., 300.); D.EndRef = FVector2D(380., 200.);
		const PlannerDimensionLayout::FResult R = PlannerDimensionLayout::Compute(D, TextSize, Style);
		TestTrue(TEXT("Vertical: value centred on the line"), R.bTextOnLine && Near(R.TextCenter, FVector2D(400., 250.)));
	}

	// Text only (a height in the top-down plan): no line, the value at the point.
	{
		FPlannerScreenDimension D;
		D.Start = FVector2D(400., 300.); D.End = FVector2D(402., 296.);
		D.StartRef = D.Start; D.EndRef = D.End;
		D.bTextOnly = true;
		const PlannerDimensionLayout::FResult R = PlannerDimensionLayout::Compute(D, TextSize, Style);
		TestTrue(TEXT("Text only: no line"), !R.bDrawLine);
		TestTrue(TEXT("Text only: value at the point"), Near(R.TextCenter, FVector2D(401., 298.)));
	}

	// A caption outside a wall (the wall below it on screen): the box sits wholly on the far side of its point.
	{
		FPlannerScreenDimension D;
		D.Start = D.End = FVector2D(400., 300.);
		D.StartRef = D.EndRef = FVector2D(400., 320.);
		D.bTextOnly = true;
		const PlannerDimensionLayout::FResult R = PlannerDimensionLayout::Compute(D, TextSize, Style);
		TestTrue(TEXT("Caption: beside its point, away from the wall"), Near(R.TextCenter, FVector2D(400., 300. - (8. + 3.))));
		TestTrue(TEXT("Caption: may step further out"), R.bTextMovable && Near(R.TextShiftDir, FVector2D(0., -1.)));
	}

	// A door near a corner: two short dimensions next to each other both put their values beside the line; they must not cover
	// each other, and a value centred on its line never moves.
	{
		TArray<PlannerDimensionLayout::FResult> Layouts;
		auto AddChainPart = [&](double X0, double X1)
		{
			FPlannerScreenDimension D;
			D.Start = FVector2D(X0, 100.); D.End = FVector2D(X1, 100.);
			D.StartRef = FVector2D(X0, 130.); D.EndRef = FVector2D(X1, 130.);
			Layouts.Add(PlannerDimensionLayout::Compute(D, TextSize, Style));
		};
		AddChainPart(100., 115.);   // 0.20 m gap
		AddChainPart(115., 180.);   // 0.90 m door
		AddChainPart(180., 480.);   // the rest of the wall: value on its line
		const FVector2D OnLine = Layouts[2].TextCenter;
		TestTrue(TEXT("Both short values start out overlapping"), PlannerDimensionLayout::TextBox(Layouts[0]).IntersectionWith(PlannerDimensionLayout::TextBox(Layouts[1])).GetArea() > 1.f);
		PlannerDimensionLayout::SeparateTexts(Layouts, Style);
		const FSlateRect A = PlannerDimensionLayout::TextBox(Layouts[0]);
		const FSlateRect B = PlannerDimensionLayout::TextBox(Layouts[1]);
		const FSlateRect C = PlannerDimensionLayout::TextBox(Layouts[2]);
		TestTrue(TEXT("Separated: the two values no longer cover each other"), A.IntersectionWith(B).GetArea() < 1.f);
		TestTrue(TEXT("Separated: neither covers the value on the line"), A.IntersectionWith(C).GetArea() < 1.f && B.IntersectionWith(C).GetArea() < 1.f);
		TestTrue(TEXT("The value on its line stays centred"), Near(Layouts[2].TextCenter, OnLine));
		TestTrue(TEXT("Moved values stay on the room side of their line"), Layouts[0].TextCenter.Y < 100. && Layouts[1].TextCenter.Y < 100.);
	}

	// A value centred on its line keeps its place even when it comes after a moved value that would land on it.
	{
		TArray<PlannerDimensionLayout::FResult> Layouts;
		FPlannerScreenDimension Short;
		Short.Start = FVector2D(100., 100.); Short.End = FVector2D(150., 100.);
		Short.StartRef = FVector2D(100., 130.); Short.EndRef = FVector2D(150., 130.);
		Layouts.Add(PlannerDimensionLayout::Compute(Short, TextSize, Style));   // moves to (125, 85.5)
		FPlannerScreenDimension Long;
		Long.Start = FVector2D(0., 85.5); Long.End = FVector2D(250., 85.5);
		Long.StartRef = FVector2D(0., 60.); Long.EndRef = FVector2D(250., 60.);
		Layouts.Add(PlannerDimensionLayout::Compute(Long, TextSize, Style));    // on its line at (125, 85.5)
		PlannerDimensionLayout::SeparateTexts(Layouts, Style);
		TestTrue(TEXT("Fixed first: the value on its line stays"), Near(Layouts[1].TextCenter, FVector2D(125., 85.5)));
		TestTrue(TEXT("Fixed first: the moved value clears it"), PlannerDimensionLayout::TextBox(Layouts[0]).IntersectionWith(PlannerDimensionLayout::TextBox(Layouts[1])).GetArea() < 1.f);
	}

	// Which view shows what.
	{
		bool bTextOnly = true;
		TestTrue(TEXT("Plan: a chain part as a line"), PlannerDimensionLayout::ShowInView(false, false, true, 100., 40., bTextOnly) && !bTextOnly);
		TestFalse(TEXT("Plan: no vertical lines"), PlannerDimensionLayout::ShowInView(true, false, true, 100., 40., bTextOnly));
		TestTrue(TEXT("Plan: the caption, as text"), PlannerDimensionLayout::ShowInView(false, true, true, 0., 40., bTextOnly) && bTextOnly);
		TestFalse(TEXT("3D: no plan caption"), PlannerDimensionLayout::ShowInView(false, true, false, 0., 40., bTextOnly));
		TestTrue(TEXT("3D: a vertical line"), PlannerDimensionLayout::ShowInView(true, false, false, 100., 40., bTextOnly) && !bTextOnly);
		TestTrue(TEXT("3D: a vertical line too short on screen, as its value"), PlannerDimensionLayout::ShowInView(true, false, false, 20., 40., bTextOnly) && bTextOnly);
		TestTrue(TEXT("3D: a chain part as a line"), PlannerDimensionLayout::ShowInView(false, false, false, 100., 40., bTextOnly) && !bTextOnly);
	}

	// The overlay paints them (arrows, lines, boxes, text) without a render target.
	if (FSlateApplication::IsInitialized())
	{
		UPlannerDimensionOverlay* Overlay = NewObject<UPlannerDimensionOverlay>(GetTransientPackage());
		Overlay->TextBackgroundColor = FLinearColor(0.9f, 0.8f, 0.2f, 0.7f);
		const TSharedRef<SWidget> OverlayWidget = Overlay->TakeWidget();
		const FElementCounts Empty = PaintAndCount(OverlayWidget, FVector2D(800., 600.));
		TArray<FPlannerScreenDimension> Dimensions;
		FPlannerScreenDimension Chain;
		Chain.Start = FVector2D(100., 100.); Chain.End = FVector2D(300., 100.);
		Chain.StartRef = FVector2D(100., 140.); Chain.EndRef = FVector2D(300., 140.);
		Chain.Text = TEXT("0.90 м");
		Dimensions.Add(Chain);
		FPlannerScreenDimension Caption;
		Caption.Start = Caption.End = FVector2D(400., 300.);
		Caption.StartRef = Caption.EndRef = Caption.Start;
		Caption.bTextOnly = true;
		Caption.Text = TEXT("выс. 2.10 м");
		Dimensions.Add(Caption);
		Overlay->SetDimensions(Dimensions);
		const FElementCounts Drawn = PaintAndCount(OverlayWidget, FVector2D(800., 600.));
		TestEqual(TEXT("Lines: the dimension line and two extension lines, each over its halo"), Drawn.Lines - Empty.Lines, 6);
		TestEqual(TEXT("Arrowheads in one batch"), Drawn.CustomVerts - Empty.CustomVerts, 1);
		TestEqual(TEXT("A box behind each value"), Drawn.RoundedBoxes - Empty.RoundedBoxes, 2);
		TestEqual(TEXT("Both values drawn"), Drawn.Texts - Empty.Texts, 2);
		for (const FLinearColor& Tint : Drawn.BoxTints)
		{
			TestTrue(TEXT("Value boxes take the configured background colour"), Tint.Equals(Overlay->TextBackgroundColor, 0.001f));
		}

		// Two short neighbours (a door near a corner): their values are drawn apart.
		TArray<FPlannerScreenDimension> Neighbours;
		for (const FVector2D& Span : { FVector2D(100., 115.), FVector2D(115., 180.) })
		{
			FPlannerScreenDimension D;
			D.Start = FVector2D(Span.X, 100.); D.End = FVector2D(Span.Y, 100.);
			D.StartRef = FVector2D(Span.X, 130.); D.EndRef = FVector2D(Span.Y, 130.);
			D.Text = TEXT("0.90 м");
			Neighbours.Add(D);
		}
		Overlay->SetDimensions(Neighbours);
		const FElementCounts Apart = PaintAndCount(OverlayWidget, FVector2D(800., 600.));
		if (TestEqual(TEXT("Two value boxes"), Apart.Boxes.Num(), 2))
		{
			TestTrue(TEXT("Drawn apart"), Apart.Boxes[0].IntersectionWith(Apart.Boxes[1]).GetArea() < 1.f);
		}
		Overlay->SetDimensions(TArray<FPlannerScreenDimension>());
		const FElementCounts Cleared = PaintAndCount(OverlayWidget, FVector2D(800., 600.));
		TestEqual(TEXT("Cleared: nothing drawn"), Cleared.Lines + Cleared.CustomVerts + Cleared.RoundedBoxes + Cleared.Texts,
			Empty.Lines + Empty.CustomVerts + Empty.RoundedBoxes + Empty.Texts);
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Snapshot of the drawing (needs a GPU and -PlannerSnapshotDir=<folder>)
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerDimensionSnapshotTest, "MaxiMall.Planner.UI.DimensionSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerDimensionSnapshotTest::RunTest(const FString& Parameters)
{
	FString OutDir;
	if (!FParse::Value(FCommandLine::Get(), TEXT("PlannerSnapshotDir="), OutDir) || !FApp::CanEverRender() || !FSlateApplication::IsInitialized())
	{
		AddInfo(TEXT("Snapshot skipped (needs a GPU and -PlannerSnapshotDir=<folder>)."));
		return true;
	}

	// A wall face along y = 300 (room above it on screen), a 0.90 m door with 2.55 / 4.55 m to the corners, the gap to a window
	// on the second row, a height seen from above (value only) and one seen in 3D (a vertical line), and a tiny gap.
	TArray<FPlannerScreenDimension> Dimensions;
	auto Add = [&Dimensions](double X0, double X1, double Y, double RefY, const TCHAR* Text)
	{
		FPlannerScreenDimension D;
		D.Start = FVector2D(X0, Y); D.End = FVector2D(X1, Y);
		D.StartRef = FVector2D(X0, RefY); D.EndRef = FVector2D(X1, RefY);
		D.Text = Text;
		Dimensions.Add(D);
	};
	Add(60., 315., 250., 300., TEXT("2.55 м"));
	Add(315., 405., 250., 300., TEXT("0.90 м"));
	Add(405., 860., 250., 300., TEXT("4.55 м"));
	Add(405., 530., 210., 300., TEXT("1.25 м"));
	Add(600., 612., 380., 300., TEXT("0.12 м"));
	{
		// The plan caption: outside the wall (below it here), centred on the door.
		FPlannerScreenDimension Caption;
		Caption.Start = Caption.End = FVector2D(360., 322.);
		Caption.StartRef = Caption.EndRef = FVector2D(360., 310.);
		Caption.bTextOnly = true;
		Caption.Text = TEXT("выс. 2.10 м");
		Dimensions.Add(Caption);
		// A door near a corner: two short values beside their lines, stepped apart.
		Add(60., 75., 150., 110., TEXT("0.20 м"));
		Add(75., 140., 150., 110., TEXT("0.90 м"));
		Add(140., 300., 150., 110., TEXT("2.20 м"));
		FPlannerScreenDimension Vertical;
		Vertical.Start = FVector2D(930., 480.); Vertical.End = FVector2D(930., 330.);
		Vertical.StartRef = FVector2D(900., 480.); Vertical.EndRef = FVector2D(900., 330.);
		Vertical.Text = TEXT("выс. 2.10 м");
		Dimensions.Add(Vertical);
	}

	UPlannerDimensionOverlay* Overlay = NewObject<UPlannerDimensionOverlay>(GetTransientPackage());
	Overlay->SetDimensions(Dimensions);
	const TSharedRef<SWidget> Scene = SNew(SOverlay)
		+ SOverlay::Slot()[SNew(SColorBlock).Color(FLinearColor(0.78f, 0.78f, 0.76f))]
		+ SOverlay::Slot().VAlign(VAlign_Top).Padding(FMargin(0.f, 300.f, 0.f, 0.f))[SNew(SBox).HeightOverride(20.f)[SNew(SColorBlock).Color(FLinearColor(0.35f, 0.35f, 0.35f))]]
		+ SOverlay::Slot()[Overlay->TakeWidget()];
	const FString File = FPaths::Combine(OutDir, TEXT("DimensionLines.png"));
	TestTrue(TEXT("Snapshot written"), SnapshotToPng(Scene, FVector2D(1000.f, 520.f), File));
	AddInfo(FString::Printf(TEXT("Dimension lines snapshot: %s"), *File));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
