// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UWorld;

/** One tool call that advances a world frame by frame: who is asking, how many frames, and its wall-clock budget. */
struct SOFTUEBRIDGEEDITOR_API FBridgeTickOperation
{
	FString ToolName;
	/** error_code reported when the budget runs out, such as pie_tick_timeout. */
	FString TimeoutErrorCode;
	int32 RequestedFrames = 0;
	double StartSeconds = 0.0;
	double DeadlineSeconds = 0.0;
};

enum class EBridgeTickRunOutcome : uint8
{
	Completed,
	TimedOut,
	/** The after-frame callback returned false. */
	StoppedByCallback,
	/** The world was already inside its own tick, so no frame was run. */
	WorldAlreadyTicking,
};

/** Count, average, and maximum of a series of wall-clock durations. */
class SOFTUEBRIDGEEDITOR_API FBridgeDurationStats
{
public:
	void Add(double Seconds)
	{
		++NumSamples;
		TotalSeconds += Seconds;
		MaxSeconds = FMath::Max(MaxSeconds, Seconds);
	}

	int32 Count() const { return NumSamples; }
	double AverageMs() const { return NumSamples > 0 ? TotalSeconds * 1000.0 / NumSamples : 0.0; }
	double MaxMs() const { return MaxSeconds * 1000.0; }

private:
	int32 NumSamples = 0;
	double TotalSeconds = 0.0;
	double MaxSeconds = 0.0;
};

/** What a fixed-step run did. */
struct SOFTUEBRIDGEEDITOR_API FBridgeTickRun
{
	EBridgeTickRunOutcome Outcome = EBridgeTickRunOutcome::Completed;
	int32 FramesTicked = 0;
	double WorldTimeBefore = 0.0;
	double WorldTimeAfter = 0.0;
	/** Wall-clock time of each step alone, without the after-step callback. */
	FBridgeDurationStats StepDurations;
};

/** Advances a world a fixed number of frames at a pinned delta, inside one bridge request. */
namespace SoftUE::FixedStepTicker
{
	/**
	 * Runs Step once per requested frame, then AfterStep with the zero-based frame index; AfterStep
	 * returning false stops the run. The budget is checked before each frame and again after every frame
	 * but the last, so a frame that finishes past the deadline is reported as a timeout unless it completed
	 * the run; a run that ticked every requested frame is complete. Plain callables only, so the
	 * automation spec drives it without a world.
	 */
	SOFTUEBRIDGEEDITOR_API FBridgeTickRun Run(
		const FBridgeTickOperation& Operation,
		TFunctionRef<double()> Clock,
		TFunctionRef<void()> Step,
		TFunctionRef<bool(int32 FrameIndex)> AfterStep);

	/** Run with World->Tick(LEVELTICK_All, DeltaSeconds) as the step, recording world time before and after. */
	SOFTUEBRIDGEEDITOR_API FBridgeTickRun TickWorld(
		UWorld& World,
		const FBridgeTickOperation& Operation,
		float DeltaSeconds,
		TFunctionRef<bool(int32 FrameIndex)> AfterTick);

	/** The structured failure every tick tool returns: phase, message, frame progress, and budget. */
	SOFTUEBRIDGEEDITOR_API TSharedPtr<FJsonObject> BuildFailureResult(
		const FBridgeTickOperation& Operation,
		const FString& ActivePhase,
		const FString& Message,
		int32 FramesCompleted);

	/** BuildFailureResult with status timeout and the operation's TimeoutErrorCode. */
	SOFTUEBRIDGEEDITOR_API TSharedPtr<FJsonObject> BuildTimeoutResult(
		const FBridgeTickOperation& Operation,
		const FString& ActivePhase,
		const FString& Message,
		int32 FramesCompleted);

	/** The failure for a run that timed out or found the world already ticking, in the tick_frames phase. */
	SOFTUEBRIDGEEDITOR_API TSharedPtr<FJsonObject> BuildRunFailureResult(
		const FBridgeTickOperation& Operation,
		const FBridgeTickRun& Run);
}
