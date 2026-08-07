// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Performance/InsightsAnalyzeTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Modules/ModuleManager.h"

#include "TraceServices/AnalysisService.h"
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Counters.h"
#include "TraceServices/Model/Frames.h"
#include "TraceServices/Model/Threads.h"
#include "TraceServices/Model/TimingProfiler.h"

namespace
{
	/** Default number of rows returned by the aggregating analyses. */
	constexpr int32 InsightsDefaultTopN = 20;
	constexpr int32 InsightsMaxTopN = 500;

	/** A frame slower than this is reported as a hitch unless the caller overrides it. */
	constexpr double InsightsDefaultHitchThresholdMs = 33.4;

	/** Worst-frame samples included in the bottlenecks report. */
	constexpr int32 InsightsWorstFrameCount = 10;

	/**
	 * Runs a full analysis pass over a trace file and returns the completed session.
	 * IAnalysisService::Analyze() is synchronous - it blocks until the trace has been
	 * fully parsed - which is what we want for a one-shot CLI query.
	 */
	TSharedPtr<const TraceServices::IAnalysisSession> OpenTraceSession(const FString& TraceFile, FString& OutError)
	{
		ITraceServicesModule* TraceServicesModule =
			FModuleManager::Get().LoadModulePtr<ITraceServicesModule>(TEXT("TraceServices"));
		if (!TraceServicesModule)
		{
			OutError = TEXT("TraceServices module is not available in this editor build.");
			return nullptr;
		}

		TSharedPtr<TraceServices::IAnalysisService> AnalysisService = TraceServicesModule->GetAnalysisService();
		if (!AnalysisService.IsValid())
		{
			OutError = TEXT("TraceServices analysis service could not be created.");
			return nullptr;
		}

		TSharedPtr<const TraceServices::IAnalysisSession> Session = AnalysisService->Analyze(*TraceFile);
		if (!Session.IsValid())
		{
			OutError = FString::Printf(
				TEXT("Failed to analyze trace file '%s'. It may be truncated, still being written, or not a .utrace file."),
				*TraceFile);
			return nullptr;
		}

		return Session;
	}

	/** Linear-interpolated percentile over an already sorted array. */
	double PercentileOfSorted(const TArray<double>& SortedValues, double Fraction)
	{
		if (SortedValues.Num() == 0)
		{
			return 0.0;
		}
		if (SortedValues.Num() == 1)
		{
			return SortedValues[0];
		}

		const double Position = Fraction * static_cast<double>(SortedValues.Num() - 1);
		const int32 LowIndex = FMath::Clamp(FMath::FloorToInt32(Position), 0, SortedValues.Num() - 1);
		const int32 HighIndex = FMath::Clamp(LowIndex + 1, 0, SortedValues.Num() - 1);
		const double Alpha = Position - static_cast<double>(LowIndex);

		return FMath::Lerp(SortedValues[LowIndex], SortedValues[HighIndex], Alpha);
	}

	/**
	 * Builds the distribution block shared by the frame analyses.
	 * Sorts Values in place.
	 */
	TSharedPtr<FJsonObject> BuildDurationStatsJson(TArray<double>& Values, double HitchThresholdMs)
	{
		TSharedPtr<FJsonObject> Stats = MakeShareable(new FJsonObject);
		Stats->SetNumberField(TEXT("count"), Values.Num());

		if (Values.Num() == 0)
		{
			return Stats;
		}

		Values.Sort();

		double Total = 0.0;
		int32 HitchCount = 0;
		const double HitchThresholdSeconds = HitchThresholdMs / 1000.0;
		for (double Value : Values)
		{
			Total += Value;
			if (Value > HitchThresholdSeconds)
			{
				++HitchCount;
			}
		}

		const double Average = Total / static_cast<double>(Values.Num());

		Stats->SetNumberField(TEXT("total_time_seconds"), Total);
		Stats->SetNumberField(TEXT("min_ms"), Values[0] * 1000.0);
		Stats->SetNumberField(TEXT("max_ms"), Values.Last() * 1000.0);
		Stats->SetNumberField(TEXT("average_ms"), Average * 1000.0);
		Stats->SetNumberField(TEXT("median_ms"), PercentileOfSorted(Values, 0.50) * 1000.0);
		Stats->SetNumberField(TEXT("p90_ms"), PercentileOfSorted(Values, 0.90) * 1000.0);
		Stats->SetNumberField(TEXT("p95_ms"), PercentileOfSorted(Values, 0.95) * 1000.0);
		Stats->SetNumberField(TEXT("p99_ms"), PercentileOfSorted(Values, 0.99) * 1000.0);
		Stats->SetNumberField(TEXT("average_fps"), Average > 0.0 ? 1.0 / Average : 0.0);
		Stats->SetNumberField(TEXT("hitch_threshold_ms"), HitchThresholdMs);
		Stats->SetNumberField(TEXT("hitch_count"), HitchCount);
		Stats->SetNumberField(
			TEXT("hitch_percent"),
			100.0 * static_cast<double>(HitchCount) / static_cast<double>(Values.Num()));

		return Stats;
	}

	const TCHAR* FrameTypeToString(ETraceFrameType FrameType)
	{
		switch (FrameType)
		{
		case TraceFrameType_Game:
			return TEXT("game");
		case TraceFrameType_Rendering:
			return TEXT("rendering");
		default:
			return TEXT("unknown");
		}
	}

	const TCHAR* TimerTypeToString(TraceServices::ETimingProfilerTimerType TimerType)
	{
		switch (TimerType)
		{
		case TraceServices::ETimingProfilerTimerType::CpuScope:
			return TEXT("cpu_scope");
		case TraceServices::ETimingProfilerTimerType::CpuSampling:
			return TEXT("cpu_sampling");
		case TraceServices::ETimingProfilerTimerType::GpuScope:
			return TEXT("gpu_scope");
		case TraceServices::ETimingProfilerTimerType::VerseSampling:
			return TEXT("verse_sampling");
		default:
			return TEXT("unknown");
		}
	}

	/** Collects frame durations (in seconds) for a frame type within the window. */
	void CollectFrameDurations(
		const TraceServices::IFrameProvider& FrameProvider,
		ETraceFrameType FrameType,
		const FInsightsAnalysisWindow& Window,
		TArray<double>& OutDurations,
		TArray<TraceServices::FFrame>& OutFrames)
	{
		FrameProvider.EnumerateFrames(
			FrameType,
			Window.StartTime,
			Window.EndTime,
			[&OutDurations, &OutFrames](const TraceServices::FFrame& Frame)
			{
				const double Duration = Frame.EndTime - Frame.StartTime;
				if (Duration >= 0.0)
				{
					OutDurations.Add(Duration);
					OutFrames.Add(Frame);
				}
			});
	}

	/**
	 * Runs a timer aggregation over the window. Returns nullptr when the trace has no
	 * timing data (for example a trace captured without the 'cpu' channel).
	 * Must be called inside an FAnalysisSessionReadScope.
	 */
	TUniquePtr<TraceServices::ITable<TraceServices::FTimingProfilerAggregatedStats>> CreateTimerAggregation(
		const TraceServices::ITimingProfilerProvider& TimingProvider,
		const FInsightsAnalysisWindow& Window,
		int32 TopN)
	{
		TraceServices::FCreateAggregationParams Params;
		Params.IntervalStart = Window.StartTime;
		Params.IntervalEnd = Window.EndTime;

		// Aggregate every CPU thread and GPU queue present in the trace.
		Params.CpuThreadFilter = [](uint32 /*ThreadId*/) { return true; };
		Params.GpuQueueFilter = [](uint32 /*QueueId*/) { return true; };

		Params.SortBy = TraceServices::FCreateAggregationParams::ESortBy::TotalInclusiveTime;
		Params.SortOrder = TraceServices::FCreateAggregationParams::ESortOrder::Descending;
		Params.TableEntryLimit = TopN;

		return TUniquePtr<TraceServices::ITable<TraceServices::FTimingProfilerAggregatedStats>>(
			TimingProvider.CreateAggregation(Params));
	}

	/** Converts one aggregated timer row to JSON. */
	TSharedPtr<FJsonObject> AggregatedTimerToJson(const TraceServices::FTimingProfilerAggregatedStats& Row)
	{
		TSharedPtr<FJsonObject> Json = MakeShareable(new FJsonObject);

		if (Row.Timer)
		{
			Json->SetStringField(TEXT("name"), Row.Timer->Name ? Row.Timer->Name : TEXT("<unnamed>"));
			Json->SetStringField(TEXT("type"), TimerTypeToString(Row.Timer->Type));
			if (Row.Timer->File)
			{
				Json->SetStringField(TEXT("file"), Row.Timer->File);
				Json->SetNumberField(TEXT("line"), Row.Timer->Line);
			}
		}
		else
		{
			Json->SetStringField(TEXT("name"), TEXT("<unknown>"));
		}

		Json->SetNumberField(TEXT("instance_count"), static_cast<double>(Row.InstanceCount));
		Json->SetNumberField(TEXT("total_inclusive_ms"), Row.TotalInclusiveTime * 1000.0);
		Json->SetNumberField(TEXT("average_inclusive_ms"), Row.AverageInclusiveTime * 1000.0);
		Json->SetNumberField(TEXT("median_inclusive_ms"), Row.MedianInclusiveTime * 1000.0);
		Json->SetNumberField(TEXT("max_inclusive_ms"), Row.MaxInclusiveTime * 1000.0);
		Json->SetNumberField(TEXT("total_exclusive_ms"), Row.TotalExclusiveTime * 1000.0);
		Json->SetNumberField(TEXT("average_exclusive_ms"), Row.AverageExclusiveTime * 1000.0);
		Json->SetNumberField(TEXT("max_exclusive_ms"), Row.MaxExclusiveTime * 1000.0);

		return Json;
	}
} // namespace

void FInsightsAnalysisWindow::ResolveAgainstSession(const TraceServices::IAnalysisSession& Session)
{
	const double Duration = Session.GetDurationSeconds();

	if (EndTime <= 0.0 || EndTime > Duration)
	{
		EndTime = Duration;
	}
	StartTime = FMath::Clamp(StartTime, 0.0, EndTime);
}

FString UInsightsAnalyzeTool::GetToolDescription() const
{
	return TEXT(
		"Analyze Unreal Insights trace files using the TraceServices providers. "
		"Analysis types: 'basic_info' (file metadata only), 'frame_stats' (frame timing "
		"distribution and hitches), 'top_functions' (aggregated CPU/GPU timers by cost), "
		"'counters' (trace counters/stats), 'threads' (threads in the trace), and "
		"'bottlenecks' (composite report: frame health, worst frames, heaviest timers). "
		"Supports an optional [start_time, end_time] window.");
}

TMap<FString, FBridgeSchemaProperty> UInsightsAnalyzeTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty TraceFile;
	TraceFile.Type = TEXT("string");
	TraceFile.Description = TEXT("Path to trace file (.utrace) to analyze");
	TraceFile.bRequired = true;
	Schema.Add(TEXT("trace_file"), TraceFile);

	FBridgeSchemaProperty AnalysisType;
	AnalysisType.Type = TEXT("string");
	AnalysisType.Description = TEXT("Type of analysis to run (default: 'basic_info')");
	AnalysisType.bRequired = false;
	AnalysisType.Enum = {
		TEXT("basic_info"),
		TEXT("frame_stats"),
		TEXT("top_functions"),
		TEXT("counters"),
		TEXT("threads"),
		TEXT("bottlenecks")};
	Schema.Add(TEXT("analysis_type"), AnalysisType);

	FBridgeSchemaProperty TopN;
	TopN.Type = TEXT("integer");
	TopN.Description = TEXT("Max rows for 'top_functions', 'counters' and 'bottlenecks' (default: 20, max: 500)");
	TopN.bRequired = false;
	Schema.Add(TEXT("top_n"), TopN);

	FBridgeSchemaProperty StartTime;
	StartTime.Type = TEXT("number");
	StartTime.Description = TEXT("Window start time in seconds from trace start (default: 0)");
	StartTime.bRequired = false;
	Schema.Add(TEXT("start_time"), StartTime);

	FBridgeSchemaProperty EndTime;
	EndTime.Type = TEXT("number");
	EndTime.Description = TEXT("Window end time in seconds (default: end of trace)");
	EndTime.bRequired = false;
	Schema.Add(TEXT("end_time"), EndTime);

	FBridgeSchemaProperty HitchThreshold;
	HitchThreshold.Type = TEXT("number");
	HitchThreshold.Description = TEXT("Frame time in ms above which a frame counts as a hitch (default: 33.4)");
	HitchThreshold.bRequired = false;
	Schema.Add(TEXT("hitch_threshold_ms"), HitchThreshold);

	return Schema;
}

FBridgeToolResult UInsightsAnalyzeTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString TraceFile = GetStringArgOrDefault(Arguments, TEXT("trace_file"));
	if (TraceFile.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("Missing required argument: trace_file"));
	}

	const FString AnalysisType = GetStringArgOrDefault(Arguments, TEXT("analysis_type"), TEXT("basic_info"));

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("insights-analyze: Analyzing %s with type %s"), *TraceFile, *AnalysisType);

	if (!FPaths::FileExists(TraceFile))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Trace file not found: %s"), *TraceFile));
	}

	// basic_info deliberately avoids parsing the trace so it stays instant on large captures.
	if (AnalysisType == TEXT("basic_info"))
	{
		return AnalyzeBasicInfo(TraceFile);
	}

	const int32 TopN = FMath::Clamp(
		GetIntArgOrDefault(Arguments, TEXT("top_n"), InsightsDefaultTopN),
		1,
		InsightsMaxTopN);
	// Read the time-valued arguments as doubles rather than through the float
	// helper - long captures lose meaningful precision at float resolution.
	double HitchThresholdMs = InsightsDefaultHitchThresholdMs;
	Arguments->TryGetNumberField(TEXT("hitch_threshold_ms"), HitchThresholdMs);
	HitchThresholdMs = FMath::Max(HitchThresholdMs, 0.0);

	FString OpenError;
	TSharedPtr<const TraceServices::IAnalysisSession> Session = OpenTraceSession(TraceFile, OpenError);
	if (!Session.IsValid())
	{
		return FBridgeToolResult::Error(OpenError);
	}

	FInsightsAnalysisWindow Window;
	Arguments->TryGetNumberField(TEXT("start_time"), Window.StartTime);
	Arguments->TryGetNumberField(TEXT("end_time"), Window.EndTime);
	Window.ResolveAgainstSession(*Session);

	if (AnalysisType == TEXT("frame_stats"))
	{
		return AnalyzeFrameStats(*Session, Window, HitchThresholdMs);
	}
	if (AnalysisType == TEXT("top_functions"))
	{
		return AnalyzeTopFunctions(*Session, Window, TopN);
	}
	if (AnalysisType == TEXT("counters"))
	{
		return AnalyzeCounters(*Session, Window, TopN);
	}
	if (AnalysisType == TEXT("threads"))
	{
		return AnalyzeThreads(*Session);
	}
	if (AnalysisType == TEXT("bottlenecks"))
	{
		return AnalyzeBottlenecks(*Session, Window, TopN, HitchThresholdMs);
	}

	return FBridgeToolResult::Error(FString::Printf(
		TEXT("Unknown analysis_type '%s'. Supported: basic_info, frame_stats, top_functions, "
		     "counters, threads, bottlenecks."),
		*AnalysisType));
}

FBridgeToolResult UInsightsAnalyzeTool::AnalyzeBasicInfo(const FString& TraceFile)
{
	IFileManager& FileManager = IFileManager::Get();
	const FFileStatData StatData = FileManager.GetStatData(*TraceFile);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("trace_file"), TraceFile);
	Result->SetStringField(TEXT("file_name"), FPaths::GetCleanFilename(TraceFile));
	Result->SetNumberField(TEXT("size_bytes"), static_cast<double>(StatData.FileSize));
	Result->SetNumberField(TEXT("size_mb"), static_cast<double>(StatData.FileSize) / (1024.0 * 1024.0));
	Result->SetStringField(TEXT("created"), StatData.CreationTime.ToString(TEXT("%Y-%m-%d %H:%M:%S")));
	Result->SetStringField(TEXT("modified"), StatData.ModificationTime.ToString(TEXT("%Y-%m-%d %H:%M:%S")));
	Result->SetStringField(TEXT("analysis_type"), TEXT("basic_info"));
	Result->SetStringField(
		TEXT("note"),
		TEXT("File metadata only - the trace is not parsed. Use analysis_type 'frame_stats', "
		     "'top_functions', 'counters', 'threads' or 'bottlenecks' for trace contents."));

	return FBridgeToolResult::Json(Result);
}

FBridgeToolResult UInsightsAnalyzeTool::AnalyzeFrameStats(
	const TraceServices::IAnalysisSession& Session,
	const FInsightsAnalysisWindow& Window,
	double HitchThresholdMs)
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("analysis_type"), TEXT("frame_stats"));
	Result->SetStringField(TEXT("session_name"), Session.GetName() ? Session.GetName() : TEXT(""));
	Result->SetNumberField(TEXT("trace_duration_seconds"), Session.GetDurationSeconds());
	Result->SetNumberField(TEXT("window_start_seconds"), Window.StartTime);
	Result->SetNumberField(TEXT("window_end_seconds"), Window.EndTime);

	TraceServices::FAnalysisSessionReadScope ReadScope(Session);

	const TraceServices::IFrameProvider& FrameProvider = TraceServices::ReadFrameProvider(Session);

	TSharedPtr<FJsonObject> ByType = MakeShareable(new FJsonObject);
	bool bAnyFrames = false;

	for (int32 TypeIndex = 0; TypeIndex < TraceFrameType_Count; ++TypeIndex)
	{
		const ETraceFrameType FrameType = static_cast<ETraceFrameType>(TypeIndex);

		TArray<double> Durations;
		TArray<TraceServices::FFrame> Frames;
		CollectFrameDurations(FrameProvider, FrameType, Window, Durations, Frames);

		if (Durations.Num() > 0)
		{
			bAnyFrames = true;
		}

		ByType->SetObjectField(FrameTypeToString(FrameType), BuildDurationStatsJson(Durations, HitchThresholdMs));
	}

	Result->SetObjectField(TEXT("frames"), ByType);

	if (!bAnyFrames)
	{
		Result->SetStringField(
			TEXT("note"),
			TEXT("No frames found in this window. The trace may have been captured without the "
			     "'frame' channel, or the window may fall outside the traced range."));
	}

	return FBridgeToolResult::Json(Result);
}

FBridgeToolResult UInsightsAnalyzeTool::AnalyzeTopFunctions(
	const TraceServices::IAnalysisSession& Session,
	const FInsightsAnalysisWindow& Window,
	int32 TopN)
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("analysis_type"), TEXT("top_functions"));
	Result->SetNumberField(TEXT("window_start_seconds"), Window.StartTime);
	Result->SetNumberField(TEXT("window_end_seconds"), Window.EndTime);
	Result->SetNumberField(TEXT("top_n"), TopN);

	TraceServices::FAnalysisSessionReadScope ReadScope(Session);

	const TraceServices::ITimingProfilerProvider* TimingProvider = TraceServices::ReadTimingProfilerProvider(Session);
	if (!TimingProvider)
	{
		return FBridgeToolResult::Error(TEXT(
			"This trace has no timing data. Capture with the 'cpu' (and optionally 'gpu') "
			"channel to get timer information."));
	}

	TUniquePtr<TraceServices::ITable<TraceServices::FTimingProfilerAggregatedStats>> Table =
		CreateTimerAggregation(*TimingProvider, Window, TopN);
	if (!Table.IsValid())
	{
		return FBridgeToolResult::Error(TEXT("Timer aggregation failed for this trace."));
	}

	TArray<TSharedPtr<FJsonValue>> Timers;
	TUniquePtr<TraceServices::ITableReader<TraceServices::FTimingProfilerAggregatedStats>> Reader(
		Table->CreateReader());
	for (; Reader.IsValid() && Reader->IsValid(); Reader->NextRow())
	{
		if (const TraceServices::FTimingProfilerAggregatedStats* Row = Reader->GetCurrentRow())
		{
			Timers.Add(MakeShareable(new FJsonValueObject(AggregatedTimerToJson(*Row))));
		}
	}

	Result->SetNumberField(TEXT("timer_count"), static_cast<double>(Table->GetRowCount()));
	Result->SetArrayField(TEXT("timers"), Timers);
	Result->SetStringField(TEXT("sorted_by"), TEXT("total_inclusive_ms"));

	return FBridgeToolResult::Json(Result);
}

FBridgeToolResult UInsightsAnalyzeTool::AnalyzeCounters(
	const TraceServices::IAnalysisSession& Session,
	const FInsightsAnalysisWindow& Window,
	int32 TopN)
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("analysis_type"), TEXT("counters"));
	Result->SetNumberField(TEXT("window_start_seconds"), Window.StartTime);
	Result->SetNumberField(TEXT("window_end_seconds"), Window.EndTime);

	TraceServices::FAnalysisSessionReadScope ReadScope(Session);

	const TraceServices::ICounterProvider& CounterProvider = TraceServices::ReadCounterProvider(Session);

	TArray<TSharedPtr<FJsonValue>> Counters;
	int32 TotalCounters = 0;

	CounterProvider.EnumerateCounters(
		[&Counters, &TotalCounters, &Window, TopN](uint32 /*CounterId*/, const TraceServices::ICounter& Counter)
		{
			++TotalCounters;
			if (Counters.Num() >= TopN)
			{
				return;
			}

			double Min = TNumericLimits<double>::Max();
			double Max = TNumericLimits<double>::Lowest();
			double Total = 0.0;
			int64 SampleCount = 0;
			double LastValue = 0.0;

			const auto Accumulate = [&Min, &Max, &Total, &SampleCount, &LastValue](double Value)
			{
				Min = FMath::Min(Min, Value);
				Max = FMath::Max(Max, Value);
				Total += Value;
				LastValue = Value;
				++SampleCount;
			};

			if (Counter.IsFloatingPoint())
			{
				Counter.EnumerateFloatValues(
					Window.StartTime,
					Window.EndTime,
					false,
					[&Accumulate](double /*Time*/, double Value) { Accumulate(Value); });
			}
			else
			{
				Counter.EnumerateValues(
					Window.StartTime,
					Window.EndTime,
					false,
					[&Accumulate](double /*Time*/, int64 Value) { Accumulate(static_cast<double>(Value)); });
			}

			if (SampleCount == 0)
			{
				return;
			}

			TSharedPtr<FJsonObject> CounterJson = MakeShareable(new FJsonObject);
			CounterJson->SetStringField(TEXT("name"), Counter.GetName() ? Counter.GetName() : TEXT("<unnamed>"));
			if (Counter.GetGroup())
			{
				CounterJson->SetStringField(TEXT("group"), Counter.GetGroup());
			}
			CounterJson->SetBoolField(TEXT("is_floating_point"), Counter.IsFloatingPoint());
			CounterJson->SetNumberField(TEXT("sample_count"), static_cast<double>(SampleCount));
			CounterJson->SetNumberField(TEXT("min"), Min);
			CounterJson->SetNumberField(TEXT("max"), Max);
			CounterJson->SetNumberField(TEXT("average"), Total / static_cast<double>(SampleCount));
			CounterJson->SetNumberField(TEXT("last"), LastValue);

			Counters.Add(MakeShareable(new FJsonValueObject(CounterJson)));
		});

	Result->SetNumberField(TEXT("counter_count"), TotalCounters);
	Result->SetNumberField(TEXT("returned_count"), Counters.Num());
	Result->SetArrayField(TEXT("counters"), Counters);

	if (TotalCounters > Counters.Num())
	{
		Result->SetStringField(
			TEXT("note"),
			FString::Printf(
				TEXT("Showing %d of %d counters (counters without samples in the window are omitted). "
				     "Raise top_n to see more."),
				Counters.Num(),
				TotalCounters));
	}

	return FBridgeToolResult::Json(Result);
}

FBridgeToolResult UInsightsAnalyzeTool::AnalyzeThreads(const TraceServices::IAnalysisSession& Session)
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("analysis_type"), TEXT("threads"));

	TraceServices::FAnalysisSessionReadScope ReadScope(Session);

	const TraceServices::IThreadProvider& ThreadProvider = TraceServices::ReadThreadProvider(Session);

	TArray<TSharedPtr<FJsonValue>> Threads;
	ThreadProvider.EnumerateThreads(
		[&Threads](const TraceServices::FThreadInfo& ThreadInfo)
		{
			TSharedPtr<FJsonObject> ThreadJson = MakeShareable(new FJsonObject);
			ThreadJson->SetNumberField(TEXT("id"), ThreadInfo.Id);
			ThreadJson->SetStringField(TEXT("name"), ThreadInfo.Name ? ThreadInfo.Name : TEXT("<unnamed>"));
			if (ThreadInfo.GroupName)
			{
				ThreadJson->SetStringField(TEXT("group"), ThreadInfo.GroupName);
			}
			Threads.Add(MakeShareable(new FJsonValueObject(ThreadJson)));
		});

	Result->SetNumberField(TEXT("thread_count"), Threads.Num());
	Result->SetArrayField(TEXT("threads"), Threads);

	return FBridgeToolResult::Json(Result);
}

FBridgeToolResult UInsightsAnalyzeTool::AnalyzeBottlenecks(
	const TraceServices::IAnalysisSession& Session,
	const FInsightsAnalysisWindow& Window,
	int32 TopN,
	double HitchThresholdMs)
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("analysis_type"), TEXT("bottlenecks"));
	Result->SetStringField(TEXT("session_name"), Session.GetName() ? Session.GetName() : TEXT(""));
	Result->SetNumberField(TEXT("trace_duration_seconds"), Session.GetDurationSeconds());
	Result->SetNumberField(TEXT("window_start_seconds"), Window.StartTime);
	Result->SetNumberField(TEXT("window_end_seconds"), Window.EndTime);

	TraceServices::FAnalysisSessionReadScope ReadScope(Session);

	// --- Frame health, and the worst frames worth jumping to in the Insights UI ---
	const TraceServices::IFrameProvider& FrameProvider = TraceServices::ReadFrameProvider(Session);

	TArray<double> GameDurations;
	TArray<TraceServices::FFrame> GameFrames;
	CollectFrameDurations(FrameProvider, TraceFrameType_Game, Window, GameDurations, GameFrames);

	// BuildDurationStatsJson sorts its input, so capture worst frames from the
	// unsorted frame list first.
	TArray<int32> FrameOrder;
	FrameOrder.Reserve(GameFrames.Num());
	for (int32 Index = 0; Index < GameFrames.Num(); ++Index)
	{
		FrameOrder.Add(Index);
	}
	FrameOrder.Sort(
		[&GameFrames](int32 A, int32 B)
		{
			const double DurationA = GameFrames[A].EndTime - GameFrames[A].StartTime;
			const double DurationB = GameFrames[B].EndTime - GameFrames[B].StartTime;
			return DurationA > DurationB;
		});

	TArray<TSharedPtr<FJsonValue>> WorstFrames;
	const int32 WorstCount = FMath::Min(InsightsWorstFrameCount, FrameOrder.Num());
	for (int32 Index = 0; Index < WorstCount; ++Index)
	{
		const TraceServices::FFrame& Frame = GameFrames[FrameOrder[Index]];

		TSharedPtr<FJsonObject> FrameJson = MakeShareable(new FJsonObject);
		FrameJson->SetNumberField(TEXT("index"), static_cast<double>(Frame.Index));
		FrameJson->SetNumberField(TEXT("start_time_seconds"), Frame.StartTime);
		FrameJson->SetNumberField(TEXT("duration_ms"), (Frame.EndTime - Frame.StartTime) * 1000.0);
		WorstFrames.Add(MakeShareable(new FJsonValueObject(FrameJson)));
	}

	Result->SetObjectField(TEXT("game_frames"), BuildDurationStatsJson(GameDurations, HitchThresholdMs));
	Result->SetArrayField(TEXT("worst_frames"), WorstFrames);

	// --- Heaviest timers: inclusive cost, plus self-cost for narrowing the culprit ---
	const TraceServices::ITimingProfilerProvider* TimingProvider = TraceServices::ReadTimingProfilerProvider(Session);
	if (!TimingProvider)
	{
		Result->SetStringField(
			TEXT("note"),
			TEXT("This trace has no timing data, so only frame statistics are reported. "
			     "Capture with the 'cpu' channel to identify expensive timers."));
		return FBridgeToolResult::Json(Result);
	}

	TUniquePtr<TraceServices::ITable<TraceServices::FTimingProfilerAggregatedStats>> Table =
		CreateTimerAggregation(*TimingProvider, Window, TopN);
	if (!Table.IsValid())
	{
		Result->SetStringField(TEXT("note"), TEXT("Timer aggregation failed for this trace."));
		return FBridgeToolResult::Json(Result);
	}

	// Collect once, then present two orderings: total time spent under a timer
	// (inclusive) and time spent in the timer itself (exclusive/self).
	TArray<TraceServices::FTimingProfilerAggregatedStats> Rows;
	TUniquePtr<TraceServices::ITableReader<TraceServices::FTimingProfilerAggregatedStats>> Reader(
		Table->CreateReader());
	for (; Reader.IsValid() && Reader->IsValid(); Reader->NextRow())
	{
		if (const TraceServices::FTimingProfilerAggregatedStats* Row = Reader->GetCurrentRow())
		{
			Rows.Add(*Row);
		}
	}

	TArray<TSharedPtr<FJsonValue>> ByInclusive;
	for (const TraceServices::FTimingProfilerAggregatedStats& Row : Rows)
	{
		ByInclusive.Add(MakeShareable(new FJsonValueObject(AggregatedTimerToJson(Row))));
	}

	Rows.Sort(
		[](const TraceServices::FTimingProfilerAggregatedStats& A,
		   const TraceServices::FTimingProfilerAggregatedStats& B)
		{
			return A.TotalExclusiveTime > B.TotalExclusiveTime;
		});

	TArray<TSharedPtr<FJsonValue>> ByExclusive;
	for (const TraceServices::FTimingProfilerAggregatedStats& Row : Rows)
	{
		ByExclusive.Add(MakeShareable(new FJsonValueObject(AggregatedTimerToJson(Row))));
	}

	Result->SetArrayField(TEXT("top_timers_by_inclusive_time"), ByInclusive);
	Result->SetArrayField(TEXT("top_timers_by_self_time"), ByExclusive);
	Result->SetStringField(
		TEXT("hint"),
		TEXT("'top_timers_by_self_time' isolates where time is actually spent; "
		     "'top_timers_by_inclusive_time' shows which call trees are most expensive overall."));

	return FBridgeToolResult::Json(Result);
}
