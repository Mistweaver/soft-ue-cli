// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/FrameRate.h"

/** The length and frame layout of something that can be sampled. */
struct SOFTUEBRIDGEEDITOR_API FBridgeAnimTimeline
{
	/** Sampled keys, so the last frame is NumKeys - 1. */
	int32 NumKeys = 0;
	double PlayLength = 0.0;
	FFrameRate FrameRate;
	/** False for a BlendSpace, whose length depends on the blend position, so it has no fixed frames. */
	bool bHasFrames = true;
};

/** The time to sample, and the frame it came from when the caller gave a frame. */
struct SOFTUEBRIDGEEDITOR_API FBridgeAnimSampleTime
{
	double Time = 0.0;
	TOptional<int32> Frame;
};

/** Frame and time rules for anim pose-at. Plain values only, so the automation spec covers them. */
namespace SoftUE::AnimSampleTime
{
	/**
	 * Keys in PlayLength seconds at FrameRate, counting both ends: 1 second at 30 fps is 31 keys.
	 * UAnimSequence reports this itself; the UAnimSequenceBase default (used by montages) counts
	 * intervals instead, one fewer.
	 */
	SOFTUEBRIDGEEDITOR_API int32 NumKeysForLength(const FFrameRate& FrameRate, double PlayLength);

	/**
	 * Resolves exactly one of Frame or Time into a time on Timeline. A frame must be 0 to NumKeys - 1
	 * and is converted with the frame rate. A time must be 0 to PlayLength; a time between keys is
	 * kept, and the sampler interpolates. A timeline without frames accepts only a time.
	 */
	SOFTUEBRIDGEEDITOR_API bool Resolve(
		const FBridgeAnimTimeline& Timeline,
		TOptional<int32> Frame,
		TOptional<double> Time,
		FBridgeAnimSampleTime& OutSample,
		FString& OutError);
}
