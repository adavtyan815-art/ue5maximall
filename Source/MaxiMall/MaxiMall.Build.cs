// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MaxiMall : ModuleRules
{
	public MaxiMall(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Expose the module's own source root so that subdirectory includes
		// like #include "FurnitureConfigurator/ShowroomBooth.h" resolve
		// from any .cpp file regardless of its own subdirectory location.
		PublicIncludePaths.AddRange(new string[]
		{
			ModuleDirectory
		});

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"NetCore",       // Replication helpers
			"UMG",           // UUserWidget for the isolated preview viewport
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			// Required for IPixelStreamingModule / IPixelStreamingStreamer used in
			// MaxiMallPreviewController::BroadcastCursorState (cursor data channel).
			// FModuleManager::GetModulePtr guards against the module being absent
			// at runtime (Editor PIE without PS plugin, standalone builds, etc.).
			"PixelStreaming",
			"PixelStreamingInput",
		});
	}
}
