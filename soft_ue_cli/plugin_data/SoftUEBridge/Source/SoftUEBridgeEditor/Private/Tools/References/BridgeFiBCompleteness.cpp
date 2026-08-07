// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/References/BridgeFiBCompleteness.h"

namespace BridgeFiBCompleteness
{
	FResult Evaluate(const FState& State)
	{
		const bool bCacheReady =
			!State.bCacheInProgress && !State.bDiscoveryInProgress &&
			State.UnindexedCount == 0 && State.FailedToCacheCount == 0;
		const bool bFallbackComplete = !State.bDiscoveryInProgressAtSelection &&
			!State.bDiscoveryInProgress &&
			State.BlueprintsSearched == State.CandidateCount &&
			(State.CandidateCount > 0 || bCacheReady);
		const bool bModeComplete = State.SearchMode == ESearchMode::FiBCache
			? bCacheReady
			: bFallbackComplete;
		if (State.bResultTruncated || bModeComplete)
		{
			return {true, FString()};
		}

		return {
			false,
			FString::Printf(
				TEXT("find-references node: incomplete_fib_index: cache_in_progress=%s, discovery_in_progress=%s, "
					 "discovery_in_progress_at_selection=%s, "
					 "unindexed_count=%d, failed_to_cache_count=%d, candidate_count=%d, blueprints_searched=%d. "
					 "Finish Find in Blueprints indexing and retry."),
				State.bCacheInProgress ? TEXT("true") : TEXT("false"),
				State.bDiscoveryInProgress ? TEXT("true") : TEXT("false"),
				State.bDiscoveryInProgressAtSelection ? TEXT("true") : TEXT("false"),
				State.UnindexedCount,
				State.FailedToCacheCount,
				State.CandidateCount,
				State.BlueprintsSearched)};
	}
}
