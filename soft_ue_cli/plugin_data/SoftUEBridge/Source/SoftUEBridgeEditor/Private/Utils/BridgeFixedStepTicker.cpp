// Copyright soft-ue-expert. All Rights Reserved.

#include "Utils/BridgeFixedStepTicker.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"

namespace SoftUE::FixedStepTicker
{
	FBridgeTickRun Run(
		const FBridgeTickOperation& Operation,
		TFunctionRef<double()> Clock,
		TFunctionRef<void()> Step,
		TFunctionRef<bool(int32 FrameIndex)> AfterStep)
	{
		FBridgeTickRun Result;
		for (int32 FrameIndex = 0; FrameIndex < Operation.RequestedFrames; ++FrameIndex)
		{
			const double StepStartSeconds = Clock();
			if (StepStartSeconds >= Operation.DeadlineSeconds)
			{
				Result.Outcome = EBridgeTickRunOutcome::TimedOut;
				return Result;
			}

			Step();
			Result.StepDurations.Add(Clock() - StepStartSeconds);
			++Result.FramesTicked;

			if (!AfterStep(FrameIndex))
			{
				Result.Outcome = EBridgeTickRunOutcome::StoppedByCallback;
				return Result;
			}

			// Every requested frame ran, so the run is complete even if the last one finished late.
			const bool bLastFrame = FrameIndex + 1 == Operation.RequestedFrames;
			if (!bLastFrame && Clock() > Operation.DeadlineSeconds)
			{
				Result.Outcome = EBridgeTickRunOutcome::TimedOut;
				return Result;
			}
		}
		return Result;
	}

	FBridgeTickRun TickWorld(
		UWorld& World,
		const FBridgeTickOperation& Operation,
		float DeltaSeconds,
		TFunctionRef<bool(int32 FrameIndex)> AfterTick)
	{
		const double WorldTimeBefore = World.GetTimeSeconds();
		if (World.bInTick)
		{
			FBridgeTickRun Busy;
			Busy.Outcome = EBridgeTickRunOutcome::WorldAlreadyTicking;
			Busy.WorldTimeBefore = WorldTimeBefore;
			Busy.WorldTimeAfter = WorldTimeBefore;
			return Busy;
		}

		FBridgeTickRun Result = Run(
			Operation,
			[]() { return FPlatformTime::Seconds(); },
			[&World, DeltaSeconds]() { World.Tick(ELevelTick::LEVELTICK_All, DeltaSeconds); },
			AfterTick);
		Result.WorldTimeBefore = WorldTimeBefore;
		Result.WorldTimeAfter = World.GetTimeSeconds();
		return Result;
	}

	TSharedPtr<FJsonObject> BuildFailureResult(
		const FBridgeTickOperation& Operation,
		const FString& ActivePhase,
		const FString& Message,
		int32 FramesCompleted)
	{
		const double NowSeconds = FPlatformTime::Seconds();
		const double TimeoutSeconds = FMath::Max(0.0, Operation.DeadlineSeconds - Operation.StartSeconds);
		const double ElapsedSeconds = FMath::Max(0.0, NowSeconds - Operation.StartSeconds);
		const double RemainingBudgetSeconds = FMath::Max(0.0, Operation.DeadlineSeconds - NowSeconds);

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("status"), TEXT("failed"));
		Result->SetStringField(TEXT("tool"), Operation.ToolName);
		Result->SetStringField(TEXT("active_phase"), ActivePhase);
		Result->SetStringField(TEXT("message"), Message);
		Result->SetNumberField(TEXT("requested_frames"), Operation.RequestedFrames);
		Result->SetNumberField(TEXT("frames_completed"), FramesCompleted);
		Result->SetNumberField(TEXT("elapsed_seconds"), ElapsedSeconds);
		Result->SetNumberField(TEXT("timeout_seconds"), TimeoutSeconds);
		Result->SetNumberField(TEXT("remaining_budget_seconds"), RemainingBudgetSeconds);
		return Result;
	}

	TSharedPtr<FJsonObject> BuildTimeoutResult(
		const FBridgeTickOperation& Operation,
		const FString& ActivePhase,
		const FString& Message,
		int32 FramesCompleted)
	{
		TSharedPtr<FJsonObject> Result = BuildFailureResult(Operation, ActivePhase, Message, FramesCompleted);
		Result->SetStringField(TEXT("status"), TEXT("timeout"));
		Result->SetStringField(TEXT("error_code"), Operation.TimeoutErrorCode);
		return Result;
	}

	TSharedPtr<FJsonObject> BuildRunFailureResult(
		const FBridgeTickOperation& Operation,
		const FBridgeTickRun& Run)
	{
		static const FString TickFramesPhase = TEXT("tick_frames");
		if (Run.Outcome == EBridgeTickRunOutcome::WorldAlreadyTicking)
		{
			return BuildFailureResult(Operation, TickFramesPhase, TEXT("World is already ticking."), 0);
		}

		const double TimeoutSeconds = Operation.DeadlineSeconds - Operation.StartSeconds;
		return BuildTimeoutResult(
			Operation,
			TickFramesPhase,
			FString::Printf(TEXT("Tick timed out after %.2fs (%d/%d frames)."),
				TimeoutSeconds,
				Run.FramesTicked,
				Operation.RequestedFrames),
			Run.FramesTicked);
	}
}
