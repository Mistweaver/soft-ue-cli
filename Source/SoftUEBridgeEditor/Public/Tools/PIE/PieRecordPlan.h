// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/ValueOrError.h"

/** How many fixed-delta ticks a pie-record recording runs. */
struct SOFTUEBRIDGEEDITOR_API FPieRecordPlan
{
	double DurationSeconds = 0.0;
	float DeltaSeconds = 0.0f;
	int32 Frames = 0;
};

/** Frame rules for pie-record. Plain values only, so the automation spec covers them. */
namespace SoftUE::PieRecord
{
	/** Every frame blocks the editor, so a recording stops at this many. */
	constexpr int32 MaxFrames = 10000;

	/**
	 * Frames to cover DurationSeconds of simulated time at DeltaSeconds per tick: rounded up, at least
	 * one, and at most MaxFrames. A count within a thousandth of a frame of a whole number is that whole
	 * number, so a delta typed with too few digits, such as 0.0166666, still gives 60 frames per second.
	 */
	SOFTUEBRIDGEEDITOR_API TValueOrError<FPieRecordPlan, FString> MakePlan(double DurationSeconds, double DeltaSeconds);
}
