// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/PIE/PieRecordPlan.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPieRecordPlanSpec,
	"SoftUEBridge.PIE.RecordPlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPieRecordPlanSpec::RunTest(const FString& Parameters)
{
	using namespace SoftUE::PieRecord;

	auto FramesFor = [](double Duration, double Delta) -> int32
	{
		const TValueOrError<FPieRecordPlan, FString> Plan = MakePlan(Duration, Delta);
		return Plan.HasValue() ? Plan.GetValue().Frames : -1;
	};
	auto ErrorFor = [](double Duration, double Delta) -> FString
	{
		const TValueOrError<FPieRecordPlan, FString> Plan = MakePlan(Duration, Delta);
		return Plan.HasError() ? Plan.GetError() : FString();
	};

	// whole durations give exact frame counts, including a delta typed with too few digits
	TestEqual(TEXT("1s at 1/60 is 60 frames"), FramesFor(1.0, 1.0 / 60.0), 60);
	TestEqual(TEXT("1s at 0.0166666 is still 60 frames"), FramesFor(1.0, 0.0166666), 60);
	TestEqual(TEXT("2s at 1/90 is 180 frames"), FramesFor(2.0, 1.0 / 90.0), 180);

	// a duration that is not a whole number of ticks rounds up so the whole duration is covered
	TestEqual(TEXT("1.01s at 1/60 rounds up to 61"), FramesFor(1.01, 1.0 / 60.0), 61);

	// anything positive records at least one frame
	TestEqual(TEXT("delta longer than duration is 1 frame"), FramesFor(0.5, 1.0), 1);
	TestEqual(TEXT("a tiny duration is 1 frame"), FramesFor(0.0001, 1.0), 1);

	// the plan keeps the inputs it was made from
	const TValueOrError<FPieRecordPlan, FString> Plan = MakePlan(2.0, 0.5);
	TestTrue(TEXT("plan is made"), Plan.HasValue());
	if (Plan.HasValue())
	{
		TestEqual(TEXT("duration kept"), Plan.GetValue().DurationSeconds, 2.0);
		TestEqual(TEXT("delta kept"), Plan.GetValue().DeltaSeconds, 0.5f);
		TestEqual(TEXT("frames"), Plan.GetValue().Frames, 4);
	}

	// the frame cap is inclusive
	TestEqual(TEXT("exactly the cap is accepted"), FramesFor(MaxFrames, 1.0), MaxFrames);
	TestEqual(TEXT("one over the cap is refused"), FramesFor(MaxFrames + 1, 1.0), -1);
	TestTrue(TEXT("cap error names the frames and the limit"),
		ErrorFor(MaxFrames + 1, 1.0).Contains(TEXT("10001 frames")) && ErrorFor(MaxFrames + 1, 1.0).Contains(TEXT("10000")));

	// durations and deltas must be positive
	TestTrue(TEXT("zero duration is refused"), ErrorFor(0.0, 1.0 / 60.0).Contains(TEXT("duration")));
	TestTrue(TEXT("negative duration is refused"), ErrorFor(-1.0, 1.0 / 60.0).Contains(TEXT("duration")));
	TestTrue(TEXT("zero delta is refused"), ErrorFor(1.0, 0.0).Contains(TEXT("delta")));
	TestTrue(TEXT("negative delta is refused"), ErrorFor(1.0, -0.1).Contains(TEXT("delta")));

	return true;
}

#endif
