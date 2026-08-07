// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace BridgeFiBCompleteness
{
	enum class ESearchMode : uint8
	{
		FiBCache,
		FullFallback
	};

	struct FState
	{
		bool bCacheInProgress = false;
		bool bDiscoveryInProgress = false;
		int32 UnindexedCount = 0;
		int32 FailedToCacheCount = 0;
		ESearchMode SearchMode = ESearchMode::FullFallback;
		int32 CandidateCount = 0;
		int32 BlueprintsSearched = 0;
		bool bResultTruncated = false;
		bool bDiscoveryInProgressAtSelection = false;
	};

	struct FResult
	{
		bool bComplete = false;
		FString ErrorMessage;
	};

	FResult Evaluate(const FState& State);
}
