// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "InsightsAnalyzeTool.generated.h"

namespace TraceServices
{
	class IAnalysisSession;
}

/** Time window to restrict an analysis to. An unset window covers the whole trace. */
struct FInsightsAnalysisWindow
{
	double StartTime = 0.0;
	double EndTime = 0.0;

	/** Clamps the window to the session duration, defaulting to the full trace. */
	void ResolveAgainstSession(const TraceServices::IAnalysisSession& Session);
};

/**
 * Tool for analyzing Unreal Insights trace files.
 *
 * Backed by the TraceServices analysis providers, so it reads the same data the
 * Unreal Insights UI does: frame timings, aggregated CPU/GPU timers, counters
 * and threads.
 *
 * Analysis types: basic_info, frame_stats, top_functions, counters, threads,
 * bottlenecks.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UInsightsAnalyzeTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("insights-analyze"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Arguments, const FBridgeToolContext& Context) override;

private:
	/** Trace file metadata only. Does not parse the trace, so it stays cheap. */
	FBridgeToolResult AnalyzeBasicInfo(const FString& TraceFile);

	/** Frame counts and timing distribution (avg/median/percentiles) plus hitch counts. */
	FBridgeToolResult AnalyzeFrameStats(
		const TraceServices::IAnalysisSession& Session,
		const FInsightsAnalysisWindow& Window,
		double HitchThresholdMs);

	/** Aggregated CPU/GPU timers, sorted by total inclusive time. */
	FBridgeToolResult AnalyzeTopFunctions(
		const TraceServices::IAnalysisSession& Session,
		const FInsightsAnalysisWindow& Window,
		int32 TopN);

	/** Trace counters (stats) with min/max/average over the window. */
	FBridgeToolResult AnalyzeCounters(
		const TraceServices::IAnalysisSession& Session,
		const FInsightsAnalysisWindow& Window,
		int32 TopN);

	/** Threads present in the trace. */
	FBridgeToolResult AnalyzeThreads(const TraceServices::IAnalysisSession& Session);

	/** Composite report: frame health, worst frames, and the heaviest timers. */
	FBridgeToolResult AnalyzeBottlenecks(
		const TraceServices::IAnalysisSession& Session,
		const FInsightsAnalysisWindow& Window,
		int32 TopN,
		double HitchThresholdMs);
};
