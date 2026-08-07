// Copyright soft-ue-expert. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tools/References/BridgeFiBCompleteness.h"

#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(
	FBridgeFiBCompletenessSpec,
	"SoftUEBridge.References.FiBCompleteness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
END_DEFINE_SPEC(FBridgeFiBCompletenessSpec)

void FBridgeFiBCompletenessSpec::Define()
{
	It("accepts a ready cache with zero matches", [this]()
	{
		const BridgeFiBCompleteness::FState State{
			false, false, 0, 0, BridgeFiBCompleteness::ESearchMode::FullFallback, 0, 0, false};
		const BridgeFiBCompleteness::FResult Result = BridgeFiBCompleteness::Evaluate(State);

		TestTrue(TEXT("ready empty fallback path is complete"), Result.bComplete);
		TestTrue(TEXT("ready empty fallback path has no error"), Result.ErrorMessage.IsEmpty());
	});

	It("rejects an incomplete cache with zero candidates", [this]()
	{
		const BridgeFiBCompleteness::FState State{
			true, false, 4, 0, BridgeFiBCompleteness::ESearchMode::FullFallback, 0, 0, false};
		const BridgeFiBCompleteness::FResult Result = BridgeFiBCompleteness::Evaluate(State);

		TestFalse(TEXT("zero-candidate fallback is incomplete"), Result.bComplete);
		TestTrue(TEXT("error includes cache state"), Result.ErrorMessage.Contains(TEXT("cache_in_progress=true")));
		TestTrue(TEXT("error includes discovery state"), Result.ErrorMessage.Contains(TEXT("discovery_in_progress=false")));
		TestTrue(TEXT("error includes unindexed count"), Result.ErrorMessage.Contains(TEXT("unindexed_count=4")));
		TestTrue(TEXT("error includes candidate count"), Result.ErrorMessage.Contains(TEXT("candidate_count=0")));
		TestTrue(TEXT("error includes searched count"), Result.ErrorMessage.Contains(TEXT("blueprints_searched=0")));
		TestTrue(TEXT("error tells caller to retry"), Result.ErrorMessage.Contains(TEXT("retry")));
	});

	It("rejects an incomplete cache when every candidate fails to load", [this]()
	{
		const BridgeFiBCompleteness::FState State{
			false, true, 2, 0, BridgeFiBCompleteness::ESearchMode::FullFallback, 3, 0, false};
		const BridgeFiBCompleteness::FResult Result = BridgeFiBCompleteness::Evaluate(State);

		TestFalse(TEXT("failed loads do not count as traversal"), Result.bComplete);
		TestTrue(TEXT("error includes discovery state"), Result.ErrorMessage.Contains(TEXT("discovery_in_progress=true")));
		TestTrue(TEXT("error includes searched count"), Result.ErrorMessage.Contains(TEXT("blueprints_searched=0")));
	});

	It("rejects partial fallback traversal with no usages", [this]()
	{
		const BridgeFiBCompleteness::FState State{
			true, false, 9, 0, BridgeFiBCompleteness::ESearchMode::FullFallback, 2, 1, false};
		const BridgeFiBCompleteness::FResult Result = BridgeFiBCompleteness::Evaluate(State);

		TestFalse(TEXT("partial traversal is incomplete"), Result.bComplete);
	});

	It("accepts fallback traversal of every candidate", [this]()
	{
		const BridgeFiBCompleteness::FState State{
			true, false, 9, 0, BridgeFiBCompleteness::ESearchMode::FullFallback, 2, 2, false};
		const BridgeFiBCompleteness::FResult Result = BridgeFiBCompleteness::Evaluate(State);

		TestTrue(TEXT("complete fallback traversal is accepted"), Result.bComplete);
	});

	It("rejects currently known candidates while discovery is active", [this]()
	{
		const BridgeFiBCompleteness::FState State{
			false, true, 0, 0, BridgeFiBCompleteness::ESearchMode::FullFallback, 2, 2, false};
		const BridgeFiBCompleteness::FResult Result = BridgeFiBCompleteness::Evaluate(State);

		TestFalse(TEXT("undiscovered Blueprints may remain"), Result.bComplete);
	});

	It("accepts traversal stopped by the result limit", [this]()
	{
		const BridgeFiBCompleteness::FState State{
			true, false, 9, 0, BridgeFiBCompleteness::ESearchMode::FullFallback, 2, 1, true};
		const BridgeFiBCompleteness::FResult Result = BridgeFiBCompleteness::Evaluate(State);

		TestTrue(TEXT("intentional truncation is accepted"), Result.bComplete);
	});

	It("treats failed-to-cache assets as an incomplete cache", [this]()
	{
		const BridgeFiBCompleteness::FState State{
			false, false, 0, 1, BridgeFiBCompleteness::ESearchMode::FiBCache, 0, 0, false};
		const BridgeFiBCompleteness::FResult Result = BridgeFiBCompleteness::Evaluate(State);

		TestFalse(TEXT("failed cache entries require fallback"), Result.bComplete);
		TestTrue(TEXT("error includes failed cache count"), Result.ErrorMessage.Contains(TEXT("failed_to_cache_count=1")));
	});

	It("rejects FiB mode when the refreshed cache becomes incomplete", [this]()
	{
		const BridgeFiBCompleteness::FState State{
			true, false, 3, 0, BridgeFiBCompleteness::ESearchMode::FiBCache, 2, 2, false};
		const BridgeFiBCompleteness::FResult Result = BridgeFiBCompleteness::Evaluate(State);

		TestFalse(TEXT("candidate equality cannot rescue stale FiB selection"), Result.bComplete);
	});

	It("rejects partial fallback even when the refreshed cache becomes ready", [this]()
	{
		const BridgeFiBCompleteness::FState State{
			false, false, 0, 0, BridgeFiBCompleteness::ESearchMode::FullFallback, 2, 1, false};
		const BridgeFiBCompleteness::FResult Result = BridgeFiBCompleteness::Evaluate(State);

		TestFalse(TEXT("cache readiness cannot rescue partial fallback"), Result.bComplete);
	});

	It("rejects fallback enumerated while discovery was active", [this]()
	{
		const BridgeFiBCompleteness::FState State{
			false, false, 0, 0, BridgeFiBCompleteness::ESearchMode::FullFallback, 0, 0, false, true};
		const BridgeFiBCompleteness::FResult Result = BridgeFiBCompleteness::Evaluate(State);

		TestFalse(TEXT("an idle final snapshot cannot repair stale enumeration"), Result.bComplete);
	});
}

#endif
