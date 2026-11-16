// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/PIE/PieRecordPlan.h"

namespace SoftUE::PieRecord
{
	namespace
	{
		/** Fraction of a frame treated as rounding error in Duration / Delta. */
		constexpr double FrameRoundingTolerance = 0.001;
	}

	TValueOrError<FPieRecordPlan, FString> MakePlan(double DurationSeconds, double DeltaSeconds)
	{
		if (DurationSeconds <= 0.0)
		{
			return MakeError(FString::Printf(TEXT("duration must be > 0 seconds (got %g)"), DurationSeconds));
		}
		if (DeltaSeconds <= 0.0)
		{
			return MakeError(FString::Printf(TEXT("delta must be > 0 seconds (got %g)"), DeltaSeconds));
		}

		const double ExactFrames = DurationSeconds / DeltaSeconds;
		const int64 Frames = FMath::Max<int64>(1, static_cast<int64>(FMath::CeilToDouble(ExactFrames - FrameRoundingTolerance)));
		if (Frames > MaxFrames)
		{
			return MakeError(FString::Printf(
				TEXT("duration %gs at delta %gs is %lld frames; the limit is %d"),
				DurationSeconds,
				DeltaSeconds,
				Frames,
				MaxFrames));
		}

		FPieRecordPlan Plan;
		Plan.DurationSeconds = DurationSeconds;
		Plan.DeltaSeconds = static_cast<float>(DeltaSeconds);
		Plan.Frames = static_cast<int32>(Frames);
		return MakeValue(Plan);
	}
}
