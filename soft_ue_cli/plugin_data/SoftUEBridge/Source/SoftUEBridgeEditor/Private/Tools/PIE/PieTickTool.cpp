// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/PIE/PieTickTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "LevelEditor.h"
#include "LevelEditorSubsystem.h"
#include "Modules/ModuleManager.h"
#include "Tools/PIE/BridgePIEWorlds.h"
#include "Utils/BridgeFixedStepTicker.h"

namespace
{
	const TCHAR* const PieTickToolName = TEXT("pie-tick");
	const TCHAR* const PieTickTimeoutErrorCode = TEXT("pie_tick_timeout");
	constexpr float DefaultPieTickDeltaSeconds = 1.0f / 60.0f;
	constexpr float DefaultPieTickTimeoutSeconds = 30.0f;
}

FString UPieTickTool::GetToolDescription() const
{
	return TEXT("Advance the PIE world by N frames at a pinned delta time. Starts PIE if not running by default.");
}

TMap<FString, FBridgeSchemaProperty> UPieTickTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty Frames;
	Frames.Type = TEXT("integer");
	Frames.Description = TEXT("Number of frames to advance (must be > 0)");
	Frames.bRequired = true;
	Schema.Add(TEXT("frames"), Frames);

	FBridgeSchemaProperty Delta;
	Delta.Type = TEXT("number");
	Delta.Description = TEXT("Pinned delta seconds per frame (default: 1/60)");
	Schema.Add(TEXT("delta"), Delta);

	FBridgeSchemaProperty AutoStart;
	AutoStart.Type = TEXT("boolean");
	AutoStart.Description = TEXT("Start PIE if it is not already running (default: true)");
	Schema.Add(TEXT("auto_start"), AutoStart);

	FBridgeSchemaProperty Map;
	Map.Type = TEXT("string");
	Map.Description = TEXT("Optional map path to load when auto-starting PIE");
	Schema.Add(TEXT("map"), Map);

	FBridgeSchemaProperty PIEInstanceProp;
	PIEInstanceProp.Type = TEXT("integer");
	PIEInstanceProp.Description = TEXT("PIE instance to tick (FWorldContext::PIEInstance); default: the first PIE world");
	Schema.Add(TEXT("pie_instance"), PIEInstanceProp);

	FBridgeSchemaProperty Timeout;
	Timeout.Type = TEXT("number");
	Timeout.Description = TEXT("Maximum seconds to wait for PIE start and tick completion (default: 30)");
	Schema.Add(TEXT("timeout"), Timeout);

	return Schema;
}

FBridgeToolResult UPieTickTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	int32 Frames = 0;
	if (!GetIntArg(Arguments, TEXT("frames"), Frames) || Frames <= 0)
	{
		return FBridgeToolResult::Error(TEXT("pie-tick: 'frames' must be a positive integer"));
	}

	const float Delta = GetFloatArgOrDefault(Arguments, TEXT("delta"), DefaultPieTickDeltaSeconds);
	if (Delta <= 0.0f)
	{
		return FBridgeToolResult::Error(TEXT("pie-tick: 'delta' must be > 0"));
	}

	const float TimeoutSeconds = GetFloatArgOrDefault(Arguments, TEXT("timeout"), DefaultPieTickTimeoutSeconds);
	if (TimeoutSeconds <= 0.0f)
	{
		return FBridgeToolResult::Error(TEXT("pie-tick: 'timeout' must be > 0"));
	}

	const bool bAutoStart = GetBoolArgOrDefault(Arguments, TEXT("auto_start"), true);
	const FString MapPath = GetStringArgOrDefault(Arguments, TEXT("map"));
	const TValueOrError<TOptional<int32>, FString> PIEInstanceOrError = FBridgePIEWorlds::ReadInstanceArgument(*Arguments);
	if (PIEInstanceOrError.HasError())
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("pie-tick: %s"), *PIEInstanceOrError.GetError()));
	}
	const TOptional<int32> PIEInstance = PIEInstanceOrError.GetValue();

	FBridgeTickOperation Operation;
	Operation.ToolName = PieTickToolName;
	Operation.TimeoutErrorCode = PieTickTimeoutErrorCode;
	Operation.RequestedFrames = Frames;
	Operation.StartSeconds = FPlatformTime::Seconds();
	Operation.DeadlineSeconds = Operation.StartSeconds + static_cast<double>(TimeoutSeconds);

	bool bPieStartedByCall = false;
	if (!IsPIERunning())
	{
		if (!bAutoStart)
		{
			return FBridgeToolResult::Error(TEXT("pie-tick: PIE is not running and auto_start=false"));
		}

		TSharedPtr<FJsonObject> StartFailure;
		if (!StartPIEForTick(MapPath, Operation, StartFailure))
		{
			return FBridgeToolResult::Json(StartFailure);
		}
		bPieStartedByCall = true;
	}

	const TArray<FBridgePIEWorld> PIEWorlds = FBridgePIEWorlds::Enumerate();
	FString SelectError;
	const FBridgePIEWorld* Selected = FBridgePIEWorlds::Select(PIEWorlds, PIEInstance, SelectError);
	if (!Selected)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("pie-tick: %s"), *SelectError));
	}
	UWorld* PIEWorld = Selected->World;

	if (FPlatformTime::Seconds() >= Operation.DeadlineSeconds)
	{
		return FBridgeToolResult::Json(SoftUE::FixedStepTicker::BuildTimeoutResult(
			Operation,
			TEXT("tick_frames"),
			FString::Printf(TEXT("Timeout expired before ticking (%0.2fs)."), TimeoutSeconds),
			0));
	}

	const FBridgeTickRun Run = SoftUE::FixedStepTicker::TickWorld(
		*PIEWorld,
		Operation,
		Delta,
		[](int32) { return true; });
	if (Run.Outcome != EBridgeTickRunOutcome::Completed)
	{
		return FBridgeToolResult::Json(SoftUE::FixedStepTicker::BuildRunFailureResult(Operation, Run));
	}

	const double WorldTimeDelta = Run.WorldTimeAfter - Run.WorldTimeBefore;
	if (WorldTimeDelta <= 0.0)
	{
		return FBridgeToolResult::Error(TEXT("pie-tick: world time did not advance"));
	}

	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("status"), TEXT("ok"));
	Response->SetNumberField(TEXT("ticks"), Run.FramesTicked);
	Response->SetNumberField(TEXT("delta"), Delta);
	Response->SetNumberField(TEXT("total_sim_time"), WorldTimeDelta);
	Response->SetNumberField(TEXT("world_time_before"), Run.WorldTimeBefore);
	Response->SetNumberField(TEXT("world_time_after"), Run.WorldTimeAfter);
	Response->SetNumberField(TEXT("world_time_delta"), WorldTimeDelta);
	Response->SetStringField(TEXT("tick_mode"), TEXT("direct_world_tick"));
	Response->SetStringField(TEXT("execution_context"), BridgeToolExecutionContextToString(GetExecutionContextRequirement()));
	Response->SetStringField(TEXT("active_phase"), TEXT("complete"));
	Response->SetBoolField(TEXT("pie_started_by_call"), bPieStartedByCall);
	Response->SetNumberField(TEXT("pie_instance"), Selected->PIEInstance);
	Response->SetStringField(TEXT("world_name"), PIEWorld->GetName());
	return FBridgeToolResult::Json(Response);
}

bool UPieTickTool::IsPIERunning() const
{
	return GEditor && GEditor->PlayWorld;
}

bool UPieTickTool::StartPIEForTick(
	const FString& MapPath,
	const FBridgeTickOperation& Operation,
	TSharedPtr<FJsonObject>& OutFailure)
{
	if (!GEditor)
	{
		OutFailure = SoftUE::FixedStepTicker::BuildFailureResult(
			Operation,
			TEXT("request_play_session"),
			TEXT("GEditor is not available."),
			0);
		return false;
	}

	if (GEditor->PlayWorld)
	{
		return true;
	}

	if (!MapPath.IsEmpty())
	{
		ULevelEditorSubsystem* LevelEditorSubsystem = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
		if (!LevelEditorSubsystem || !LevelEditorSubsystem->LoadLevel(MapPath))
		{
			OutFailure = SoftUE::FixedStepTicker::BuildFailureResult(
				Operation,
				TEXT("map_load"),
				FString::Printf(TEXT("Failed to load map: %s"), *MapPath),
				0);
			return false;
		}
	}

	if (FPlatformTime::Seconds() >= Operation.DeadlineSeconds)
	{
		OutFailure = SoftUE::FixedStepTicker::BuildTimeoutResult(
			Operation,
			TEXT("map_load"),
			TEXT("Timeout expired while loading the map before RequestPlaySession."),
			0);
		return false;
	}

	FRequestPlaySessionParams Params;
	GEditor->RequestPlaySession(Params);

	while (FPlatformTime::Seconds() < Operation.DeadlineSeconds)
	{
		if (GEditor->PlayWorld)
		{
			return true;
		}

		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().Tick();
		}
		FPlatformProcess::Sleep(0.01f);
	}

	OutFailure = SoftUE::FixedStepTicker::BuildTimeoutResult(
		Operation,
		TEXT("wait_for_play_world"),
		TEXT("PIE did not start before the timeout budget expired."),
		0);
	return false;
}
