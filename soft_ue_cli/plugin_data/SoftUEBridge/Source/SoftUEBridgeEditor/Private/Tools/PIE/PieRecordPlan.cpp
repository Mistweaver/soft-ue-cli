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
		// Written as !(x > 0) so NaN is refused too.
		if (!(DurationSeconds > 0.0) || !FMath::IsFinite(DurationSeconds))
		{
			return MakeError(FString::Printf(TEXT("duration must be a finite number > 0 seconds (got %g)"), DurationSeconds));
		}
		if (!(DeltaSeconds > 0.0) || !FMath::IsFinite(DeltaSeconds))
		{
			return MakeError(FString::Printf(TEXT("delta must be a finite number > 0 seconds (got %g)"), DeltaSeconds));
		}

		// Check the cap while the count is still a double: converting a count past the int64 range is
		// undefined, and on x64 it wraps to a negative number that would slip under the cap.
		const double Frames = FMath::Max(1.0, FMath::CeilToDouble(DurationSeconds / DeltaSeconds - FrameRoundingTolerance));
		if (!FMath::IsFinite(Frames) || Frames > MaxFrames)
		{
			return MakeError(FString::Printf(
				TEXT("duration %gs at delta %gs is %.0f frames; the limit is %d"),
				DurationSeconds,
				DeltaSeconds,
				Frames,
				MaxFrames));
		}

		FPieRecordPlan Plan;
		Plan.DurationSeconds = DurationSeconds;
		Plan.DeltaSeconds = static_cast<float>(DeltaSeconds);
		Plan.Frames = static_cast<int32>(Frames);
		if (!(Plan.DeltaSeconds > 0.0f))
		{
			return MakeError(FString::Printf(TEXT("delta %g seconds is too small to tick with"), DeltaSeconds));
		}
		return MakeValue(Plan);
	}
}
