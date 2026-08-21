// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class awsTutorial : ModuleRules
{
	public awsTutorial(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "NetCore", "UMG", "HTTP", "Json", "JsonUtilities", "ApplicationCore", "RenderCore", "ProceduralMeshComponent" });

        PrivateDependencyModuleNames.AddRange(new string[]
       {
            "Slate",
            "SlateCore",
            "PixelStreaming",
            "PixelStreamingInput",
            "ImageWrapper"
       });
        PublicIncludePaths.AddRange(new string[]
{
    ModuleDirectory
});

    }
}
