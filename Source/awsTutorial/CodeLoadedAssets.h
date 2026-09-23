// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR
namespace UE::Cook { struct FPackageCookRule; }
#endif

/**
 * Assets that C++ loads by path at runtime (LoadObject in a function, not in a constructor) and that no map or asset
 * references. The cooker cannot see such loads, so a packaged client or dedicated server simply does not contain them:
 * the tile catalog icon draws as a dark square, the «Интерьер» catalog is empty and cabinet sets lose their layout
 * (height, rotation, part offsets). The game module adds these packages to every cook (all target platforms).
 *
 * When code starts loading another asset by path, add its package here.
 */
namespace CodeLoadedAssets
{
	/** Long package names, e.g. "/Game/DT/DT_PlannerObjects". */
	TConstArrayView<const TCHAR*> GetPackageNames();

#if WITH_EDITOR
	/** One AddToCook rule per package, for UE::Cook::FDelegates::ModifyCook. */
	void AppendCookRules(TArray<UE::Cook::FPackageCookRule>& InOutRules);
#endif
}
