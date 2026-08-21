// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class awsTutorialClientTarget : TargetRules
{
	public awsTutorialClientTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Client;
		bValidateFormatStrings = true;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_4;
		ExtraModuleNames.Add("awsTutorial");
	}
}
