// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/PIE/PieRecordTool.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Tools/PIE/BridgePIEWorlds.h"
#include "Tools/PIE/PieRecordPlan.h"
#include "Tools/Scripting/BridgePython.h"
#include "Utils/BridgeFixedStepTicker.h"

namespace
{
	const TCHAR* const PieRecordToolName = TEXT("pie-record");
	const TCHAR* const PieRecordTimeoutErrorCode = TEXT("pie_record_timeout");
	constexpr double DefaultPieRecordDeltaSeconds = 1.0 / 60.0;
	constexpr double DefaultPieRecordTimeoutSeconds = 120.0;
	const TCHAR* const SamplerRuntimeModule = TEXT("soft_ue_bridge_pie_record");

	/** A Python string literal for Value. A JSON string is one, because the JSON writer does not escape '/'. */
	FString ToPythonStringLiteral(const FString& Value)
	{
		FString Json;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
		Writer->WriteArrayStart();
		Writer->WriteValue(Value);
		Writer->WriteArrayEnd();
		Writer->Close();
		return Json.Mid(1, Json.Len() - 2);
	}

	FString ToJsonString(const TSharedRef<FJsonObject>& Object)
	{
		FString Json;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
		FJsonSerializer::Serialize(Object, Writer);
		return Json;
	}

	FString DefaultOutputPath()
	{
		const FString FileName = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S-%s")) + TEXT(".jsonl");
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SoftUEBridge"), TEXT("pie-record"), FileName);
	}

	/** The sampler runtime ships in the plugin's Content/Python folder. Empty when the plugin cannot be found. */
	FString SamplerRuntimePath()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SoftUEBridge"));
		return Plugin.IsValid()
			? FPaths::Combine(Plugin->GetContentDir(), TEXT("Python"), FString(SamplerRuntimeModule) + TEXT(".py"))
			: FString();
	}

	/**
	 * One recording's Python side: loads the runtime by path, starts it, samples each tick, and always
	 * finishes it, so the output file is closed on every exit path.
	 */
	class FSamplerSession
	{
	public:
		FSamplerSession() = default;
		FSamplerSession(const FSamplerSession&) = delete;
		FSamplerSession& operator=(const FSamplerSession&) = delete;

		~FSamplerSession()
		{
			if (bStarted)
			{
				SoftUE::Python::Execute(
					FString::Printf(TEXT("__import__('sys').modules[%s].finish()"), *ToPythonStringLiteral(SamplerRuntimeModule)),
					EPythonCommandExecutionMode::EvaluateStatement);
			}
		}

		/** Loads the runtime fresh, then calls its start(**StartArguments). */
		FBridgePythonResult Start(const FString& RuntimePath, const TSharedRef<FJsonObject>& StartArguments)
		{
			const FString ModuleName = ToPythonStringLiteral(SamplerRuntimeModule);
			const FString Command = FString::Join(TArray<FString>{
				TEXT("import importlib.util as _soft_ue_util, json as _soft_ue_json, sys as _soft_ue_sys"),
				FString::Printf(TEXT("_soft_ue_spec = _soft_ue_util.spec_from_file_location(%s, %s)"),
					*ModuleName, *ToPythonStringLiteral(RuntimePath)),
				TEXT("_soft_ue_module = _soft_ue_util.module_from_spec(_soft_ue_spec)"),
				FString::Printf(TEXT("_soft_ue_sys.modules[%s] = _soft_ue_module"), *ModuleName),
				TEXT("_soft_ue_spec.loader.exec_module(_soft_ue_module)"),
				FString::Printf(TEXT("_soft_ue_module.start(**_soft_ue_json.loads(%s))"),
					*ToPythonStringLiteral(ToJsonString(StartArguments))),
			}, TEXT("\n"));

			bStarted = true;
			return SoftUE::Python::Execute(Command, EPythonCommandExecutionMode::ExecuteFile);
		}

		FBridgePythonResult Sample(int32 Frame, double WorldTimeSeconds, double DeltaSeconds) const
		{
			return SoftUE::Python::Execute(
				FString::Printf(TEXT("__import__('sys').modules[%s].sample(%d, %.17g, %.17g)"),
					*ToPythonStringLiteral(SamplerRuntimeModule), Frame, WorldTimeSeconds, DeltaSeconds),
				EPythonCommandExecutionMode::EvaluateStatement);
		}

	private:
		bool bStarted = false;
	};

	/** A checked pie-record request: everything Execute needs before it touches Python or PIE. */
	struct FPieRecordRequest
	{
		FPieRecordPlan Plan;
		double TimeoutSeconds = DefaultPieRecordTimeoutSeconds;
		FString ScriptPath;
		FString OutputPath;
		TOptional<int32> PIEInstance;
	};

	double NumberOrDefault(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* Field, double Default)
	{
		double Value = Default;
		Arguments->TryGetNumberField(Field, Value);
		return Value;
	}

	TValueOrError<FPieRecordRequest, FString> ParseRequest(const TSharedPtr<FJsonObject>& Arguments)
	{
		double DurationSeconds = 0.0;
		if (!Arguments->TryGetNumberField(TEXT("duration"), DurationSeconds))
		{
			return MakeError(TEXT("'duration' is required"));
		}
		TValueOrError<FPieRecordPlan, FString> Plan = SoftUE::PieRecord::MakePlan(
			DurationSeconds, NumberOrDefault(Arguments, TEXT("delta"), DefaultPieRecordDeltaSeconds));
		if (Plan.HasError())
		{
			return MakeError(Plan.StealError());
		}

		FPieRecordRequest Request;
		Request.Plan = Plan.GetValue();
		Request.TimeoutSeconds = NumberOrDefault(Arguments, TEXT("timeout"), DefaultPieRecordTimeoutSeconds);
		if (Request.TimeoutSeconds <= 0.0)
		{
			return MakeError(TEXT("'timeout' must be > 0"));
		}

		// The editor would resolve a relative path against its own working directory (Binaries/<Platform>),
		// never the caller's, so only absolute paths are accepted. The CLI always sends absolute paths.
		Arguments->TryGetStringField(TEXT("script"), Request.ScriptPath);
		if (Request.ScriptPath.IsEmpty())
		{
			return MakeError(TEXT("'script' is required"));
		}
		if (FPaths::IsRelative(Request.ScriptPath))
		{
			return MakeError(FString::Printf(TEXT("'script' must be an absolute path (got '%s')"), *Request.ScriptPath));
		}
		if (!FPaths::FileExists(Request.ScriptPath))
		{
			return MakeError(FString::Printf(TEXT("script not found: %s"), *Request.ScriptPath));
		}

		FString RequestedOutput;
		Arguments->TryGetStringField(TEXT("output"), RequestedOutput);
		if (!RequestedOutput.IsEmpty() && FPaths::IsRelative(RequestedOutput))
		{
			return MakeError(FString::Printf(TEXT("'output' must be an absolute path (got '%s')"), *RequestedOutput));
		}
		Request.OutputPath = FPaths::ConvertRelativePathToFull(RequestedOutput.IsEmpty() ? DefaultOutputPath() : RequestedOutput);

		TValueOrError<TOptional<int32>, FString> PIEInstance = FBridgePIEWorlds::ReadInstanceArgument(*Arguments);
		if (PIEInstance.HasError())
		{
			return MakeError(PIEInstance.StealError());
		}
		Request.PIEInstance = PIEInstance.GetValue();
		return MakeValue(MoveTemp(Request));
	}

	/** What one recording did: the tick run, the frames sampled, the sampler's cost, and its failure if any. */
	struct FRecordingOutcome
	{
		FBridgeTickRun Run;
		int32 FramesRecorded = 0;
		FBridgeDurationStats SampleDurations;
		FBridgePythonResult FailedSample;
	};

	TSharedPtr<FJsonObject> BuildSampleFailureResult(
		const FPieRecordRequest& Request,
		const FBridgeTickOperation& Operation,
		const FRecordingOutcome& Outcome)
	{
		TSharedPtr<FJsonObject> Failure = SoftUE::FixedStepTicker::BuildFailureResult(
			Operation,
			TEXT("sample"),
			FString::Printf(TEXT("sample(ctx) failed at frame %d."), Outcome.FramesRecorded),
			Outcome.Run.FramesTicked);
		Failure->SetNumberField(TEXT("failed_frame"), Outcome.FramesRecorded);
		Failure->SetNumberField(TEXT("frames_recorded"), Outcome.FramesRecorded);
		Failure->SetStringField(TEXT("traceback"), Outcome.FailedSample.Error);
		Failure->SetStringField(TEXT("output_path"), Request.OutputPath);
		return Failure;
	}

	TSharedPtr<FJsonObject> BuildSuccessResult(
		const FPieRecordRequest& Request,
		const FBridgeTickOperation& Operation,
		const FRecordingOutcome& Outcome,
		const FBridgePIEWorld& Target)
	{
		const FBridgeTickRun& Run = Outcome.Run;
		TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
		Response->SetStringField(TEXT("status"), TEXT("ok"));
		Response->SetStringField(TEXT("output_path"), Request.OutputPath);
		Response->SetNumberField(TEXT("frames_recorded"), Outcome.FramesRecorded);
		Response->SetNumberField(TEXT("requested_frames"), Request.Plan.Frames);
		Response->SetNumberField(TEXT("delta_seconds"), Request.Plan.DeltaSeconds);
		Response->SetNumberField(TEXT("simulated_seconds"), Run.WorldTimeAfter - Run.WorldTimeBefore);
		Response->SetNumberField(TEXT("wall_seconds"), FPlatformTime::Seconds() - Operation.StartSeconds);
		Response->SetNumberField(TEXT("avg_tick_ms"), Run.StepDurations.AverageMs());
		Response->SetNumberField(TEXT("max_tick_ms"), Run.StepDurations.MaxMs());
		Response->SetNumberField(TEXT("avg_sample_ms"), Outcome.SampleDurations.AverageMs());
		Response->SetNumberField(TEXT("pie_instance"), Target.PIEInstance);
		Response->SetStringField(TEXT("world_name"), Target.World->GetName());
		return Response;
	}

	TSharedRef<FJsonObject> MakeStartArguments(const FPieRecordRequest& Request, const FBridgePIEWorld& Target)
	{
		TSharedRef<FJsonObject> Header = MakeShared<FJsonObject>();
		Header->SetNumberField(TEXT("duration_seconds"), Request.Plan.DurationSeconds);
		Header->SetNumberField(TEXT("delta_seconds"), Request.Plan.DeltaSeconds);
		Header->SetNumberField(TEXT("requested_frames"), Request.Plan.Frames);
		Header->SetStringField(TEXT("world_name"), Target.World->GetName());
		Header->SetNumberField(TEXT("pie_instance"), Target.PIEInstance);

		TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
		Arguments->SetStringField(TEXT("script_path"), Request.ScriptPath);
		Arguments->SetStringField(TEXT("output_path"), Request.OutputPath);
		Arguments->SetStringField(TEXT("world_path"), Target.World->GetPathName());
		Arguments->SetObjectField(TEXT("header"), Header);
		return Arguments;
	}
}

FString UPieRecordTool::GetToolDescription() const
{
	return TEXT("Advance the running PIE world at a fixed delta for a duration of simulated time, calling sample(ctx) "
		"from a Python script after every tick and writing one JSON line per tick. Requires PythonScriptPlugin.");
}

TMap<FString, FBridgeSchemaProperty> UPieRecordTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty Duration;
	Duration.Type = TEXT("number");
	Duration.Description = TEXT("Simulated seconds to record (must be > 0)");
	Duration.bRequired = true;
	Schema.Add(TEXT("duration"), Duration);

	FBridgeSchemaProperty Script;
	Script.Type = TEXT("string");
	Script.Description = TEXT("Absolute path of a Python file that defines sample(ctx)");
	Script.bRequired = true;
	Schema.Add(TEXT("script"), Script);

	FBridgeSchemaProperty Delta;
	Delta.Type = TEXT("number");
	Delta.Description = TEXT("Pinned delta seconds per tick (default: 1/60)");
	Schema.Add(TEXT("delta"), Delta);

	FBridgeSchemaProperty Output;
	Output.Type = TEXT("string");
	Output.Description = TEXT("Absolute path of the JSONL file to write (default: <Project>/Saved/SoftUEBridge/pie-record/<timestamp>.jsonl)");
	Schema.Add(TEXT("output"), Output);

	FBridgeSchemaProperty PIEInstance;
	PIEInstance.Type = TEXT("integer");
	PIEInstance.Description = TEXT("PIE instance to record (FWorldContext::PIEInstance, a non-negative integer); default: the first PIE world");
	Schema.Add(TEXT("pie_instance"), PIEInstance);

	FBridgeSchemaProperty Timeout;
	Timeout.Type = TEXT("number");
	Timeout.Description = TEXT("Wall-clock budget for the recording in seconds (default: 120)");
	Schema.Add(TEXT("timeout"), Timeout);

	return Schema;
}

FBridgeToolResult UPieRecordTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	TValueOrError<FPieRecordRequest, FString> RequestOrError = ParseRequest(Arguments);
	if (RequestOrError.HasError())
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("pie-record: %s"), *RequestOrError.GetError()));
	}
	const FPieRecordRequest& Request = RequestOrError.GetValue();

	if (!SoftUE::Python::IsAvailable())
	{
		return PluginUnavailable(
			TEXT("PythonScriptPlugin"),
			GetToolName(),
			TEXT("Enable it in Edit > Plugins > Scripting > Python Editor Script Plugin, then restart the editor."));
	}

	const FString RuntimePath = SamplerRuntimePath();
	if (RuntimePath.IsEmpty() || !FPaths::FileExists(RuntimePath))
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("pie-record: sampler runtime not found at '%s'; reinstall the SoftUEBridge plugin"), *RuntimePath));
	}

	const TArray<FBridgePIEWorld> PIEWorlds = FBridgePIEWorlds::Enumerate();
	FString SelectError;
	const FBridgePIEWorld* Target = FBridgePIEWorlds::Select(PIEWorlds, Request.PIEInstance, SelectError);
	if (!Target)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("pie-record: %s"), *SelectError));
	}
	UWorld& World = *Target->World;

	FBridgeTickOperation Operation;
	Operation.ToolName = PieRecordToolName;
	Operation.TimeoutErrorCode = PieRecordTimeoutErrorCode;
	Operation.RequestedFrames = Request.Plan.Frames;
	Operation.StartSeconds = FPlatformTime::Seconds();
	Operation.DeadlineSeconds = Operation.StartSeconds + Request.TimeoutSeconds;

	FSamplerSession Sampler;
	const FBridgePythonResult Started = Sampler.Start(RuntimePath, MakeStartArguments(Request, *Target));
	if (!Started.bSucceeded)
	{
		TSharedPtr<FJsonObject> Failure = SoftUE::FixedStepTicker::BuildFailureResult(
			Operation, TEXT("start_sampler"), TEXT("The sampler script could not be started."), 0);
		Failure->SetStringField(TEXT("traceback"), Started.Error);
		return FBridgeToolResult::Json(Failure);
	}

	FRecordingOutcome Outcome;
	Outcome.Run = SoftUE::FixedStepTicker::TickWorld(
		World,
		Operation,
		Request.Plan.DeltaSeconds,
		[&](int32 FrameIndex)
		{
			const double SampleStartSeconds = FPlatformTime::Seconds();
			FBridgePythonResult Sampled = Sampler.Sample(FrameIndex, World.GetTimeSeconds(), World.GetDeltaSeconds());
			Outcome.SampleDurations.Add(FPlatformTime::Seconds() - SampleStartSeconds);
			if (!Sampled.bSucceeded)
			{
				Outcome.FailedSample = MoveTemp(Sampled);
				return false;
			}
			++Outcome.FramesRecorded;
			return true;
		});

	switch (Outcome.Run.Outcome)
	{
	case EBridgeTickRunOutcome::Completed:
		return FBridgeToolResult::Json(BuildSuccessResult(Request, Operation, Outcome, *Target));
	case EBridgeTickRunOutcome::StoppedByCallback:
		return FBridgeToolResult::Json(BuildSampleFailureResult(Request, Operation, Outcome));
	default:
	{
		TSharedPtr<FJsonObject> Failure = SoftUE::FixedStepTicker::BuildRunFailureResult(Operation, Outcome.Run);
		Failure->SetNumberField(TEXT("frames_recorded"), Outcome.FramesRecorded);
		Failure->SetStringField(TEXT("output_path"), Request.OutputPath);
		return FBridgeToolResult::Json(Failure);
	}
	}
}
