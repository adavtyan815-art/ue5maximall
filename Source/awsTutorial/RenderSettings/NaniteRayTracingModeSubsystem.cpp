// Copyright 2026 MaxiMall. All Rights Reserved.

#include "RenderSettings/NaniteRayTracingModeSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"

namespace NaniteRayTracingMode
{
	static const TCHAR* CVarName = TEXT("r.RayTracing.Nanite.Mode");
	static constexpr int32 StreamOut = 1;

	/** The cvar is process-wide, game instances are not: count them and restore only after the last one. */
	static int32 GRequests = 0;

	/** True only when this code wrote the SetByCode value, so Unset never removes somebody else's value. */
	static bool GWrittenByUs = false;

	static EConsoleVariableFlags SetByOf(const IConsoleVariable* CVar)
	{
		return (EConsoleVariableFlags)(CVar->GetFlags() & ECVF_SetByMask);
	}
}

bool UNaniteRayTracingModeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Dedicated / GameLift servers never render: there is no ray tracing to switch.
	return FApp::CanEverRender() && Super::ShouldCreateSubsystem(Outer);
}

void UNaniteRayTracingModeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(NaniteRayTracingMode::CVarName);
	if (!CVar)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MaxiMallRendering] %s not found: ray tracing keeps the Nanite fallback meshes."), NaniteRayTracingMode::CVarName);
		return;
	}

	bHoldsRequest = true;
	if (NaniteRayTracingMode::GRequests++ > 0)
	{
		return; // another game instance in this process already applied it
	}

	const int32 OldValue = CVar->GetInt();
	const EConsoleVariableFlags OldSetBy = NaniteRayTracingMode::SetByOf(CVar);
	const TCHAR* Outcome = TEXT("applied");
	if (OldValue == NaniteRayTracingMode::StreamOut)
	{
		// No Set: it would recreate every component's render state even though the value does not change.
		Outcome = TEXT("already active, left unchanged");
	}
	else if ((uint32)OldSetBy > (uint32)ECVF_SetByCode)
	{
		// A console value outranks code; a Set would be refused with a warning.
		Outcome = TEXT("NOT applied: the console owns the value");
	}
	else
	{
		CVar->Set(NaniteRayTracingMode::StreamOut, ECVF_SetByCode);
		NaniteRayTracingMode::GWrittenByUs = true;
	}

	UE_LOG(LogTemp, Log, TEXT("[MaxiMallRendering] %s %d (SetBy%s) -> %d (SetBy%s): %s. Ray tracing uses the streamed-out Nanite geometry instead of the fallback meshes."),
		NaniteRayTracingMode::CVarName, OldValue, GetConsoleVariableSetByName(OldSetBy),
		CVar->GetInt(), GetConsoleVariableSetByName(NaniteRayTracingMode::SetByOf(CVar)), Outcome);
}

void UNaniteRayTracingModeSubsystem::Deinitialize()
{
	if (bHoldsRequest)
	{
		bHoldsRequest = false;
		if (--NaniteRayTracingMode::GRequests == 0 && NaniteRayTracingMode::GWrittenByUs)
		{
			NaniteRayTracingMode::GWrittenByUs = false;
			// Editor only: PIE has ended, give the editor viewport its project value back. A packaged game keeps the mode
			// until the process exits; switching back during shutdown would only recreate render state that is being torn down.
			if (GIsEditor)
			{
				if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(NaniteRayTracingMode::CVarName))
				{
					CVar->Unset(ECVF_SetByCode);
					UE_LOG(LogTemp, Log, TEXT("[MaxiMallRendering] %s override removed after PIE: now %d (SetBy%s)."),
						NaniteRayTracingMode::CVarName, CVar->GetInt(), GetConsoleVariableSetByName(NaniteRayTracingMode::SetByOf(CVar)));
				}
			}
		}
	}

	Super::Deinitialize();
}
