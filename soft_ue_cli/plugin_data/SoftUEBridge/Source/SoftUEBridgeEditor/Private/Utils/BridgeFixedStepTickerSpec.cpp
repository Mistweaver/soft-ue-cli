// Copyright soft-ue-expert. All Rights Reserved.

#include "Utils/BridgeFixedStepTicker.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeFixedStepTickerSpec,
	"SoftUEBridge.PIE.FixedStepTicker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBridgeFixedStepTickerSpec::RunTest(const FString& Parameters)
{
	using namespace SoftUE::FixedStepTicker;

	// A fake clock that advances one second per step, so deadlines land on exact frames.
	double Now = 0.0;
	int32 Steps = 0;
	TArray<int32> SeenFrames;
	auto Clock = [&Now]() { return Now; };
	auto Step = [&Now, &Steps]() { Now += 1.0; ++Steps; };
	auto Reset = [&Now, &Steps, &SeenFrames]()
	{
		Now = 0.0;
		Steps = 0;
		SeenFrames.Reset();
	};
	auto Operation = [](int32 Frames, double Deadline)
	{
		FBridgeTickOperation Op;
		Op.ToolName = TEXT("pie-tick");
		Op.TimeoutErrorCode = TEXT("pie_tick_timeout");
		Op.RequestedFrames = Frames;
		Op.DeadlineSeconds = Deadline;
		return Op;
	};
	auto KeepGoing = [&SeenFrames](int32 FrameIndex) { SeenFrames.Add(FrameIndex); return true; };

	// every requested frame runs, and the callback sees zero-based frame indexes in order
	Reset();
	FBridgeTickRun Done = Run(Operation(3, 100.0), Clock, Step, KeepGoing);
	TestTrue(TEXT("run completes"), Done.Outcome == EBridgeTickRunOutcome::Completed);
	TestEqual(TEXT("three frames ticked"), Done.FramesTicked, 3);
	TestEqual(TEXT("step ran three times"), Steps, 3);
	TestTrue(TEXT("callback saw frames 0, 1, 2"), SeenFrames == TArray<int32>({0, 1, 2}));

	// the callback stops the run after the frame it was called for
	Reset();
	FBridgeTickRun Stopped = Run(Operation(5, 100.0), Clock, Step,
		[](int32 FrameIndex) { return FrameIndex < 1; });
	TestTrue(TEXT("callback stop is reported"), Stopped.Outcome == EBridgeTickRunOutcome::StoppedByCallback);
	TestEqual(TEXT("the stopping frame counts as ticked"), Stopped.FramesTicked, 2);

	// a deadline already passed runs nothing
	Reset();
	Now = 10.0;
	FBridgeTickRun Late = Run(Operation(3, 10.0), Clock, Step, KeepGoing);
	TestTrue(TEXT("a passed deadline times out"), Late.Outcome == EBridgeTickRunOutcome::TimedOut);
	TestEqual(TEXT("no frame ticked past the deadline"), Late.FramesTicked, 0);

	// a frame that finishes exactly on the deadline is fine; the next one is refused before it starts
	Reset();
	FBridgeTickRun OnDeadline = Run(Operation(3, 2.0), Clock, Step, KeepGoing);
	TestTrue(TEXT("refused before frame 3"), OnDeadline.Outcome == EBridgeTickRunOutcome::TimedOut);
	TestEqual(TEXT("two frames fit the budget"), OnDeadline.FramesTicked, 2);

	// a frame that finishes past the deadline is a timeout, even when it was the last one
	Reset();
	FBridgeTickRun Overrun = Run(Operation(1, 0.5), Clock, Step, KeepGoing);
	TestTrue(TEXT("overrunning the last frame times out"), Overrun.Outcome == EBridgeTickRunOutcome::TimedOut);
	TestEqual(TEXT("the overrunning frame still counts"), Overrun.FramesTicked, 1);

	// the run times each step on the clock, without the after-step callback
	Reset();
	FBridgeTickRun Timed = Run(Operation(3, 100.0), Clock, Step,
		[&Now](int32) { Now += 5.0; return true; });
	TestEqual(TEXT("three steps timed"), Timed.StepDurations.Count(), 3);
	TestEqual(TEXT("average step is one second"), Timed.StepDurations.AverageMs(), 1000.0);
	TestEqual(TEXT("max step is one second"), Timed.StepDurations.MaxMs(), 1000.0);

	// duration stats: empty, single, and mixed
	FBridgeDurationStats Stats;
	TestEqual(TEXT("empty count"), Stats.Count(), 0);
	TestEqual(TEXT("empty average is zero"), Stats.AverageMs(), 0.0);
	TestEqual(TEXT("empty max is zero"), Stats.MaxMs(), 0.0);
	Stats.Add(0.002);
	TestEqual(TEXT("single average"), Stats.AverageMs(), 2.0, 1e-9);
	TestEqual(TEXT("single max"), Stats.MaxMs(), 2.0, 1e-9);
	Stats.Add(0.004);
	Stats.Add(0.000);
	TestEqual(TEXT("mixed average"), Stats.AverageMs(), 2.0, 1e-9);
	TestEqual(TEXT("mixed max"), Stats.MaxMs(), 4.0, 1e-9);
	TestEqual(TEXT("mixed count"), Stats.Count(), 3);

	// zero frames is an empty, completed run
	Reset();
	FBridgeTickRun Empty = Run(Operation(0, 100.0), Clock, Step, KeepGoing);
	TestTrue(TEXT("zero frames completes"), Empty.Outcome == EBridgeTickRunOutcome::Completed);
	TestEqual(TEXT("zero frames ticks nothing"), Empty.FramesTicked, 0);

	// failure results carry the operation's tool name and timeout code
	FBridgeTickOperation Op = Operation(4, 100.0);
	FBridgeTickRun TimedOut;
	TimedOut.Outcome = EBridgeTickRunOutcome::TimedOut;
	TimedOut.FramesTicked = 3;
	TSharedPtr<FJsonObject> Timeout = BuildRunFailureResult(Op, TimedOut);
	TestEqual(TEXT("timeout status"), Timeout->GetStringField(TEXT("status")), FString(TEXT("timeout")));
	TestEqual(TEXT("timeout code"), Timeout->GetStringField(TEXT("error_code")), FString(TEXT("pie_tick_timeout")));
	TestEqual(TEXT("tool name"), Timeout->GetStringField(TEXT("tool")), FString(TEXT("pie-tick")));
	TestEqual(TEXT("frames completed"), static_cast<int32>(Timeout->GetNumberField(TEXT("frames_completed"))), 3);
	TestEqual(TEXT("requested frames"), static_cast<int32>(Timeout->GetNumberField(TEXT("requested_frames"))), 4);

	FBridgeTickRun Busy;
	Busy.Outcome = EBridgeTickRunOutcome::WorldAlreadyTicking;
	TSharedPtr<FJsonObject> BusyResult = BuildRunFailureResult(Op, Busy);
	TestEqual(TEXT("busy is a failure, not a timeout"), BusyResult->GetStringField(TEXT("status")), FString(TEXT("failed")));
	TestFalse(TEXT("busy has no timeout code"), BusyResult->HasField(TEXT("error_code")));

	return true;
}

#endif
