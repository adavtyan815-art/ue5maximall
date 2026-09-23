// Copyright Epic Games, Inc. All Rights Reserved.

#include "awsTutorial.h"
#include "CodeLoadedAssets.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "UObject/ICookInfo.h"
#endif

class FAwsTutorialGameModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
#if WITH_EDITOR
		// Assets that code loads by path are invisible to the cooker; add them to every cook (client and server).
		if (IsRunningCookCommandlet())
		{
			ModifyCookHandle = UE::Cook::FDelegates::ModifyCook.AddLambda(
				[](UE::Cook::ICookInfo&, TArray<UE::Cook::FPackageCookRule>& InOutRules)
				{
					CodeLoadedAssets::AppendCookRules(InOutRules);
				});
		}
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_EDITOR
		UE::Cook::FDelegates::ModifyCook.Remove(ModifyCookHandle);
#endif
	}

private:
#if WITH_EDITOR
	FDelegateHandle ModifyCookHandle;
#endif
};

IMPLEMENT_PRIMARY_GAME_MODULE( FAwsTutorialGameModule, awsTutorial, "awsTutorial" );
 