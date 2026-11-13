// Copyright soft-ue-expert. All Rights Reserved.

#include "Utils/BridgeAnimSampleTime.h"

namespace
{
	// Frame-to-seconds conversion can land a hair past the play length on the last key.
	constexpr double AnimSampleTimeEndTolerance = 1.0e-4;
}

namespace SoftUE::AnimSampleTime
{
bool Resolve(
	const FBridgeAnimTimeline& Timeline,
	TOptional<int32> Frame,
	TOptional<double> Time,
	FBridgeAnimSampleTime& OutSample,
	FString& OutError)
{
	if (Frame.IsSet() == Time.IsSet())
	{
		OutError = TEXT("Give exactly one of frame or time.");
		return false;
	}

	if (Frame.IsSet())
	{
		if (!Timeline.bHasFrames)
		{
			OutError = TEXT("This asset has no fixed frames; sample it with time instead of frame.");
			return false;
		}
		const int32 LastFrame = Timeline.NumKeys - 1;
		if (Frame.GetValue() < 0 || Frame.GetValue() > LastFrame)
		{
			OutError = FString::Printf(TEXT("frame %d is outside 0 to %d."), Frame.GetValue(), LastFrame);
			return false;
		}
		OutSample.Frame = Frame;
		OutSample.Time = FMath::Min(Timeline.FrameRate.AsSeconds(Frame.GetValue()), Timeline.PlayLength);
		return true;
	}

	const double Seconds = Time.GetValue();
	if (Seconds < 0.0 || Seconds > Timeline.PlayLength + AnimSampleTimeEndTolerance)
	{
		OutError = FString::Printf(TEXT("time %.3f is outside 0 to %.3f seconds."), Seconds, Timeline.PlayLength);
		return false;
	}
	OutSample.Frame.Reset();
	OutSample.Time = FMath::Min(Seconds, Timeline.PlayLength);
	return true;
}
}
