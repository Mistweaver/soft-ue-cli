// Copyright soft-ue-expert. All Rights Reserved.

#include "Utils/BridgeAnimSampleTime.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeAnimSampleTimeSpec,
	"SoftUEBridge.Animation.SampleTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBridgeAnimSampleTimeSpec::RunTest(const FString& Parameters)
{
	using namespace SoftUE::AnimSampleTime;

	const double Tolerance = 0.0001;

	// 31 keys at 30 fps is one second: frame 30 is the last key and lands on the play length.
	FBridgeAnimTimeline Timeline;
	Timeline.NumKeys = 31;
	Timeline.PlayLength = 1.0;
	Timeline.FrameRate = FFrameRate(30, 1);

	auto Run = [&Timeline](TOptional<int32> Frame, TOptional<double> Time, FBridgeAnimSampleTime& Out, FString& Error)
	{
		Out = FBridgeAnimSampleTime();
		Error.Reset();
		return Resolve(Timeline, Frame, Time, Out, Error);
	};

	FBridgeAnimSampleTime Out;
	FString Error;

	// frame converts with the frame rate and is reported back
	TestTrue(TEXT("frame 0 resolves"), Run(0, {}, Out, Error));
	TestEqual(TEXT("frame 0 is time 0"), Out.Time, 0.0, Tolerance);
	TestTrue(TEXT("frame is reported"), Out.Frame.IsSet() && Out.Frame.GetValue() == 0);

	TestTrue(TEXT("frame 15 resolves"), Run(15, {}, Out, Error));
	TestEqual(TEXT("frame 15 is half a second"), Out.Time, 0.5, Tolerance);

	TestTrue(TEXT("last frame resolves"), Run(30, {}, Out, Error));
	TestEqual(TEXT("last frame is the play length"), Out.Time, 1.0, Tolerance);

	// frame boundaries
	TestFalse(TEXT("negative frame fails"), Run(-1, {}, Out, Error));
	TestTrue(TEXT("negative frame error names the range"), Error.Contains(TEXT("0 to 30")));
	TestFalse(TEXT("frame past the last key fails"), Run(31, {}, Out, Error));
	TestTrue(TEXT("past-end error names the range"), Error.Contains(TEXT("0 to 30")));

	// time passes through and has no frame
	TestTrue(TEXT("time between keys resolves"), Run({}, 0.517, Out, Error));
	TestEqual(TEXT("time is unchanged"), Out.Time, 0.517, Tolerance);
	TestFalse(TEXT("time sample has no frame"), Out.Frame.IsSet());

	TestTrue(TEXT("time 0 resolves"), Run({}, 0.0, Out, Error));
	TestTrue(TEXT("time at play length resolves"), Run({}, 1.0, Out, Error));

	// time boundaries
	TestFalse(TEXT("negative time fails"), Run({}, -0.01, Out, Error));
	TestFalse(TEXT("time past play length fails"), Run({}, 1.01, Out, Error));
	TestTrue(TEXT("past-length error names the play length"), Error.Contains(TEXT("1.000")));

	// exactly one of frame or time
	TestFalse(TEXT("neither fails"), Run({}, {}, Out, Error));
	TestTrue(TEXT("neither error names both"), Error.Contains(TEXT("frame")) && Error.Contains(TEXT("time")));
	TestFalse(TEXT("both fails"), Run(1, 0.1, Out, Error));

	// a timeline without frames (a BlendSpace) accepts only time
	FBridgeAnimTimeline NoFrames;
	NoFrames.PlayLength = 2.0;
	NoFrames.bHasFrames = false;
	Out = FBridgeAnimSampleTime();
	Error.Reset();
	TestFalse(TEXT("frame on a frameless timeline fails"), Resolve(NoFrames, 0, {}, Out, Error));
	TestTrue(TEXT("frameless error asks for time"), Error.Contains(TEXT("time")));
	TestTrue(TEXT("time on a frameless timeline resolves"), Resolve(NoFrames, {}, 1.5, Out, Error));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
