// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Editor/CaptureScreenshotTool.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Modules/ModuleManager.h"
#include "Utils/BridgeImageOutput.h"
#include "Utils/BridgeJsonObjectUtils.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tools/BridgeToolRegistry.h"
#include "Widgets/SWindow.h"
#include "Widgets/SViewport.h"
#include "Widgets/Docking/SDockTab.h"
#include "SoftUEBridgeEditorModule.h"
#include "Interfaces/IMainFrameModule.h"

namespace
{
	void CollectDockTabs(const TSharedRef<SWidget>& Widget, TArray<TSharedPtr<SDockTab>>& OutTabs)
	{
		if (Widget->GetType() == FName(TEXT("SDockTab")))
		{
			OutTabs.Add(StaticCastSharedRef<SDockTab>(Widget));
		}

		FChildren* Children = Widget->GetChildren();
		if (!Children)
		{
			return;
		}

		for (int32 ChildIndex = 0; ChildIndex < Children->Num(); ++ChildIndex)
		{
			CollectDockTabs(Children->GetChildAt(ChildIndex), OutTabs);
		}
	}

	bool MatchesTabName(const FString& Candidate, const FString& RequestedName)
	{
		return Candidate.Equals(RequestedName, ESearchCase::IgnoreCase)
			|| Candidate.Contains(RequestedName, ESearchCase::IgnoreCase);
	}

	TSharedPtr<SDockTab> FindLiveTabByLabel(const FString& RequestedName)
	{
		TArray<TSharedRef<SWindow>> Windows = FSlateApplication::Get().GetTopLevelWindows();
		for (const TSharedRef<SWindow>& Window : Windows)
		{
			TArray<TSharedPtr<SDockTab>> Tabs;
			CollectDockTabs(Window->GetContent(), Tabs);

			for (const TSharedPtr<SDockTab>& Tab : Tabs)
			{
				if (Tab.IsValid() && MatchesTabName(Tab->GetTabLabel().ToString(), RequestedName))
				{
					return Tab;
				}
			}
		}

		return nullptr;
	}

	TSharedPtr<SWindow> FindTopLevelWindowByTitle(const FString& RequestedName)
	{
		TArray<TSharedRef<SWindow>> Windows = FSlateApplication::Get().GetTopLevelWindows();
		for (const TSharedRef<SWindow>& Window : Windows)
		{
			if (MatchesTabName(Window->GetTitle().ToString(), RequestedName))
			{
				return Window;
			}
		}

		return nullptr;
	}

	void CopyJsonFields(const TSharedPtr<FJsonObject>& Source, const TSharedPtr<FJsonObject>& Target)
	{
		if (!Source.IsValid() || !Target.IsValid())
		{
			return;
		}

		for (const auto& Pair : Source->Values)
		{
			Target->SetField(SoftUE::JsonObjectUtils::KeyToString(Pair.Key), Pair.Value);
		}
	}

	bool TryGetToolResultText(const FBridgeToolResult& ToolResult, FString& OutText)
	{
		if (ToolResult.Content.Num() != 1 || !ToolResult.Content[0].IsValid())
		{
			return false;
		}
		return ToolResult.Content[0]->TryGetStringField(TEXT("text"), OutText);
	}

	TSharedPtr<FJsonObject> ParseJsonToolResult(const FBridgeToolResult& ToolResult)
	{
		FString JsonText;
		if (!TryGetToolResultText(ToolResult, JsonText) || JsonText.IsEmpty())
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> ParsedObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, ParsedObject) || !ParsedObject.IsValid())
		{
			return nullptr;
		}
		return ParsedObject;
	}

	FBridgeToolResult AddJsonFieldsToToolResult(
		const FBridgeToolResult& ToolResult,
		const TSharedPtr<FJsonObject>& ExtraFields)
	{
		if (ToolResult.bIsError)
		{
			return ToolResult;
		}

		TSharedPtr<FJsonObject> ParsedObject = ParseJsonToolResult(ToolResult);
		if (!ParsedObject.IsValid())
		{
			return ToolResult;
		}

		CopyJsonFields(ExtraFields, ParsedObject);
		return FBridgeToolResult::Json(ParsedObject);
	}

	bool IsUnsafeWindowScreenshotDuringPIE()
	{
		if (!GEngine)
		{
			return false;
		}

		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			if (WorldContext.WorldType == EWorldType::PIE
				&& WorldContext.GameViewport
				&& WorldContext.GameViewport->bDisableWorldRendering)
			{
				return true;
			}
		}

		return false;
	}
}

TMap<FString, FBridgeSchemaProperty> UCaptureScreenshotTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	Schema.Add(TEXT("mode"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("Capture mode: 'window' (entire editor), 'tab' (specific panel), 'region' (coordinates), 'viewport' (render target), or 'pie-window' (composited PIE viewport with UMG)"),
		false,
		{TEXT("window"), TEXT("tab"), TEXT("region"), TEXT("viewport"), TEXT("pie-window")}
	});

	Schema.Add(TEXT("window_name"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("For 'tab' mode: Name of the editor tab/panel to capture (e.g., Blueprint, Material, OutputLog)"),
		false
	});

	Schema.Add(TEXT("region"), FBridgeSchemaProperty{
		TEXT("array"),
		TEXT("For 'region' mode: Screen coordinates [x, y, width, height] to capture"),
		false
	});

	Schema.Add(TEXT("format"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("Image format (default: png)"),
		false,
		{TEXT("png"), TEXT("jpeg")}
	});

	Schema.Add(TEXT("output"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("Output mode: 'file' writes to temp directory (default), 'base64' returns encoded data"),
		false,
		{TEXT("file"), TEXT("base64")}
	});

	Schema.Add(TEXT("scale"), FBridgeSchemaProperty{
		TEXT("number"),
		TEXT("Output scale percentage. 100 keeps current size; 50 halves width and height. Requests above 100 are clamped."),
		false
	});

	Schema.Add(TEXT("width"), FBridgeSchemaProperty{
		TEXT("integer"),
		TEXT("Requested output width in pixels. Height is derived from aspect ratio when omitted. Never upscales."),
		false
	});

	Schema.Add(TEXT("height"), FBridgeSchemaProperty{
		TEXT("integer"),
		TEXT("Requested output height in pixels. Width is derived from aspect ratio when omitted. Never upscales."),
		false
	});

	Schema.Add(TEXT("color_mode"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("Output color mode: color (default), grayscale, or monochrome"),
		false,
		{TEXT("color"), TEXT("grayscale"), TEXT("monochrome")}
	});

	Schema.Add(TEXT("cleanup_previous"), FBridgeSchemaProperty{
		TEXT("boolean"),
		TEXT("Delete previous screenshot files before writing this file output (default: false)"),
		false
	});

	Schema.Add(TEXT("safe_mode"), FBridgeSchemaProperty{
		TEXT("boolean"),
		TEXT("When true, window capture during PIE falls back to the composited PIE viewport to avoid unstable full-window Slate capture (default: true)"),
		false
	});

	Schema.Add(TEXT("unsafe_slate_window_capture"), FBridgeSchemaProperty{
		TEXT("boolean"),
		TEXT("Alias for safe_mode=false; allows direct full-window Slate capture during PIE"),
		false
	});

	return Schema;
}

TArray<FString> UCaptureScreenshotTool::GetRequiredParams() const
{
	return {};
}

FBridgeToolResult UCaptureScreenshotTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString Mode = GetStringArgOrDefault(Arguments, TEXT("mode"), TEXT("viewport"));
	const FBridgeImageOutputOptions Options = FBridgeImageOutputOptions::FromArguments(Arguments);
	bool bSafeMode = GetBoolArgOrDefault(Arguments, TEXT("safe_mode"), true);
	if (GetBoolArgOrDefault(Arguments, TEXT("unsafe_slate_window_capture"), false))
	{
		bSafeMode = false;
	}

	// Viewport mode delegates to the runtime tool (no Slate needed)
	if (Mode == TEXT("viewport"))
	{
		return CaptureViewport(Options);
	}
	if (Mode == TEXT("pie-window"))
	{
		return CapturePIEWindow(
			Options,
			bSafeMode,
			Mode,
			TEXT(""));
	}

	// All other modes require Slate
	if (!FSlateApplication::IsInitialized())
	{
		return FBridgeToolResult::Error(TEXT("Slate application not initialized"));
	}

	// Route to appropriate capture mode
	if (Mode == TEXT("window"))
	{
		return CaptureWindow(Options, bSafeMode);
	}
	else if (Mode == TEXT("tab"))
	{
		const FString WindowName = GetStringArgOrDefault(Arguments, TEXT("window_name"), TEXT(""));
		if (WindowName.IsEmpty())
		{
			return FBridgeToolResult::Error(TEXT("'window_name' is required for 'tab' mode"));
		}
		return CaptureTab(WindowName, Options);
	}
	else if (Mode == TEXT("region"))
	{
		const TArray<TSharedPtr<FJsonValue>>* RegionArray = nullptr;
		if (!Arguments->TryGetArrayField(TEXT("region"), RegionArray) || !RegionArray || RegionArray->Num() != 4)
		{
			return FBridgeToolResult::Error(TEXT("'region' must be an array of 4 integers [x, y, width, height]"));
		}

		FIntRect Region(
			(*RegionArray)[0]->AsNumber(),
			(*RegionArray)[1]->AsNumber(),
			(*RegionArray)[0]->AsNumber() + (*RegionArray)[2]->AsNumber(),
			(*RegionArray)[1]->AsNumber() + (*RegionArray)[3]->AsNumber()
		);

		return CaptureRegion(Region, Options);
	}
	else
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Invalid mode '%s'. Must be 'window', 'tab', 'region', 'viewport', or 'pie-window'."), *Mode));
	}
}

FBridgeToolResult UCaptureScreenshotTool::CaptureViewport(
	const FBridgeImageOutputOptions& Options)
{
	// Delegate to the runtime capture-viewport tool (works in both PIE and standalone)
	TSharedPtr<FJsonObject> Args = MakeShareable(new FJsonObject);
	Options.WriteArguments(Args);
	return FBridgeToolRegistry::Get().ExecuteTool(TEXT("capture-viewport"), Args, FBridgeToolContext());
}

FBridgeToolResult UCaptureScreenshotTool::CaptureWindow(
	const FBridgeImageOutputOptions& Options,
	bool bSafeMode)
{
	if (bSafeMode && GEditor && GEditor->IsPlaySessionInProgress())
	{
		return CapturePIEWindow(
			Options,
			true,
			TEXT("window"),
			TEXT("safe_mode_pie_window_fallback"));
	}

	// Prefer the main editor window regardless of focus — agents call this tool
	// without the editor being the active/foreground window.
	TSharedPtr<SWindow> EditorWindow;
	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		IMainFrameModule& MainFrame = FModuleManager::GetModuleChecked<IMainFrameModule>("MainFrame");
		EditorWindow = MainFrame.GetParentWindow();
	}

	// Fall back to the focused window (works in non-editor contexts)
	if (!EditorWindow.IsValid())
	{
		EditorWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
	}

	if (!EditorWindow.IsValid())
	{
		return FBridgeToolResult::Error(TEXT("No editor window found"));
	}

	if (IsUnsafeWindowScreenshotDuringPIE())
	{
		UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("capture-screenshot window: PIE game viewport has bDisableWorldRendering=true; falling back to viewport capture to avoid an unsafe Slate full-window screenshot."));
		return CaptureViewport(Options);
	}

	TSharedPtr<FJsonObject> ExtraFields = MakeShareable(new FJsonObject);
	ExtraFields->SetStringField(TEXT("requested_mode"), TEXT("window"));
	ExtraFields->SetStringField(TEXT("capture_source"), TEXT("slate_window"));
	ExtraFields->SetBoolField(TEXT("safe_mode"), bSafeMode);
	ExtraFields->SetBoolField(TEXT("unsafe_window_capture"), !bSafeMode);
	ExtraFields->SetStringField(TEXT("window_title"), EditorWindow->GetTitle().ToString());
	if (EditorWindow->GetNativeWindow().IsValid())
	{
		const void* NativeHandle = EditorWindow->GetNativeWindow()->GetOSWindowHandle();
		ExtraFields->SetStringField(TEXT("native_window_handle"), FString::Printf(TEXT("%p"), NativeHandle));
	}

	return CaptureFromWindow(EditorWindow, Options, ExtraFields);
}

FBridgeToolResult UCaptureScreenshotTool::CaptureTab(
	const FString& TabName,
	const FBridgeImageOutputOptions& Options)
{
	// Try to find the tab by name
	TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(*TabName));
	if (!Tab.IsValid())
	{
		Tab = FindLiveTabByLabel(TabName);
	}

	if (!Tab.IsValid())
	{
		if (TSharedPtr<SWindow> Window = FindTopLevelWindowByTitle(TabName))
		{
			return CaptureFromWindow(Window, Options);
		}

		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Tab '%s' not found by tab id, visible tab label, or top-level window title"), *TabName));
	}

	// Get the tab's content widget
	TSharedPtr<SWidget> Content = Tab->GetContent();
	if (!Content.IsValid())
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Tab '%s' has no content to capture"), *TabName));
	}

	// Capture the widget
	TArray<FColor> RawData;
	int32 Width, Height;
	if (!TakeWidgetScreenshot(Content.ToSharedRef(), RawData, Width, Height))
	{
		if (TSharedPtr<SWindow> Window = Tab->GetParentWindow())
		{
			return CaptureFromWindow(Window, Options);
		}

		return FBridgeToolResult::Error(TEXT("Failed to capture tab screenshot"));
	}

	return OutputImage(RawData, Width, Height, Options, FString::Printf(TEXT("tab '%s'"), *TabName));
}

FBridgeToolResult UCaptureScreenshotTool::CaptureRegion(
	const FIntRect& Region,
	const FBridgeImageOutputOptions& Options)
{
	// Get the virtual desktop geometry
	FSlateRect VirtualDesktopRect = FSlateApplication::Get().GetWorkArea(FSlateRect());
	FVector2D ScreenSize = VirtualDesktopRect.GetSize();

	// Validate region is within screen bounds
	if (Region.Min.X < 0 || Region.Min.Y < 0 ||
		Region.Max.X > ScreenSize.X || Region.Max.Y > ScreenSize.Y)
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Region [%d, %d, %d, %d] is outside screen bounds [0, 0, %d, %d]"),
			Region.Min.X, Region.Min.Y, Region.Max.X, Region.Max.Y,
			(int32)ScreenSize.X, (int32)ScreenSize.Y));
	}

	// Get active window for screenshot
	TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
	if (!ActiveWindow.IsValid())
	{
		return FBridgeToolResult::Error(TEXT("No active editor window found for region capture"));
	}

	// Take screenshot using Slate
	TArray<FColor> RawData;
	FIntVector OutSize;

	if (!FSlateApplication::Get().TakeScreenshot(
		ActiveWindow.ToSharedRef(),
		Region,
		RawData,
		OutSize))
	{
		return FBridgeToolResult::Error(TEXT("Failed to capture region screenshot"));
	}

	const FString CaptureMode = FString::Printf(
		TEXT("region [%d, %d, %dx%d]"), Region.Min.X, Region.Min.Y, Region.Width(), Region.Height());
	return OutputImage(RawData, OutSize.X, OutSize.Y, Options, CaptureMode);
}

FBridgeToolResult UCaptureScreenshotTool::CaptureFromWindow(
	TSharedPtr<SWindow> Window,
	const FBridgeImageOutputOptions& Options,
	TSharedPtr<FJsonObject> ExtraFields)
{
	if (!Window.IsValid())
	{
		return FBridgeToolResult::Error(TEXT("Invalid window"));
	}

	TArray<FColor> RawData;
	FIntVector OutSize;

	// Capture the window
	if (!FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(), RawData, OutSize))
	{
		return FBridgeToolResult::Error(TEXT("Failed to capture window screenshot"));
	}

	return OutputImage(RawData, OutSize.X, OutSize.Y, Options, TEXT("editor window"), ExtraFields);
}

FBridgeToolResult UCaptureScreenshotTool::CapturePIEWindow(
	const FBridgeImageOutputOptions& Options,
	bool bSafeMode,
	const FString& RequestedMode,
	const FString& FallbackReason)
{
	if (bSafeMode)
	{
		UE_LOG(LogSoftUEBridgeEditor, Warning, TEXT("capture-screenshot pie-window: defaulting to runtime viewport capture to avoid unsafe PIE Slate window screenshot; pass unsafe_slate_window_capture=true to opt in."));

		TSharedPtr<FJsonObject> ExtraFields = MakeShareable(new FJsonObject);
		ExtraFields->SetStringField(TEXT("requested_mode"), RequestedMode);
		ExtraFields->SetStringField(TEXT("requested_capture_mode"), TEXT("pie-window"));
		ExtraFields->SetStringField(TEXT("capture_mode"), TEXT("viewport"));
		ExtraFields->SetStringField(TEXT("capture_source"), TEXT("runtime_viewport_readpixels"));
		ExtraFields->SetStringField(TEXT("capture_fallback_kind"), TEXT("pie_window_safe_viewport_fallback"));
		ExtraFields->SetStringField(TEXT("fallback_reason"), FallbackReason.IsEmpty()
			? TEXT("pie_window_safe_viewport_fallback")
			: FallbackReason);
		ExtraFields->SetStringField(TEXT("fallback_command"), TEXT("capture-screenshot viewport"));
		ExtraFields->SetBoolField(TEXT("safe_mode"), true);
		ExtraFields->SetBoolField(TEXT("pie_running"), GEditor && GEditor->IsPlaySessionInProgress());
		ExtraFields->SetBoolField(TEXT("unsafe_window_capture"), false);
		ExtraFields->SetBoolField(TEXT("structured_exception"), false);

		FBridgeToolResult ViewportResult = CaptureViewport(
			Options);

		if (ViewportResult.bIsError)
		{
			TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
			CopyJsonFields(ExtraFields, Result);
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), TEXT("PIE window safe viewport fallback failed"));

			FString FallbackError;
			if (TryGetToolResultText(ViewportResult, FallbackError) && !FallbackError.IsEmpty())
			{
				Result->SetStringField(TEXT("fallback_error"), FallbackError);
			}
			return FBridgeToolResult::Json(Result);
		}

		return AddJsonFieldsToToolResult(ViewportResult, ExtraFields);
	}

	if (!FSlateApplication::IsInitialized())
	{
		return FBridgeToolResult::Error(TEXT("Slate application not initialized"));
	}

	TSharedPtr<SWidget> PIEViewportWidget = FindPIEGameViewportWidget();
	if (!PIEViewportWidget.IsValid())
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("No composited PIE game viewport widget found"));
		Result->SetStringField(TEXT("capture_mode"), TEXT("pie-window"));
		Result->SetStringField(TEXT("requested_mode"), RequestedMode);
		Result->SetStringField(TEXT("capture_source"), TEXT("pie_composited_slate_widget"));
		Result->SetStringField(TEXT("capture_failure_kind"), TEXT("no_pie_viewport_widget"));
		Result->SetBoolField(TEXT("structured_exception"), false);
		Result->SetBoolField(TEXT("safe_mode"), bSafeMode);
		Result->SetBoolField(TEXT("pie_running"), GEditor && GEditor->IsPlaySessionInProgress());
		Result->SetBoolField(TEXT("unsafe_window_capture"), !bSafeMode);
		Result->SetStringField(TEXT("capture_risk"), TEXT("pie_window_slate_capture_opt_in"));
		Result->SetStringField(TEXT("fallback_command"), TEXT("capture-screenshot viewport"));
		if (!FallbackReason.IsEmpty())
		{
			Result->SetStringField(TEXT("fallback_reason"), FallbackReason);
		}
		return FBridgeToolResult::Json(Result);
	}

	const FVector2D WidgetSize = PIEViewportWidget->GetTickSpaceGeometry().GetLocalSize();
	if (WidgetSize.X <= 0.0f || WidgetSize.Y <= 0.0f)
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Composited PIE viewport widget has invalid geometry"));
		Result->SetStringField(TEXT("capture_mode"), TEXT("pie-window"));
		Result->SetStringField(TEXT("requested_mode"), RequestedMode);
		Result->SetStringField(TEXT("capture_source"), TEXT("pie_composited_slate_widget"));
		Result->SetStringField(TEXT("capture_failure_kind"), TEXT("invalid_widget_geometry"));
		Result->SetBoolField(TEXT("structured_exception"), false);
		Result->SetBoolField(TEXT("safe_mode"), bSafeMode);
		Result->SetBoolField(TEXT("pie_running"), GEditor && GEditor->IsPlaySessionInProgress());
		Result->SetBoolField(TEXT("unsafe_window_capture"), !bSafeMode);
		Result->SetStringField(TEXT("capture_risk"), TEXT("pie_window_slate_capture_opt_in"));
		Result->SetStringField(TEXT("fallback_command"), TEXT("capture-screenshot viewport"));
		if (!FallbackReason.IsEmpty())
		{
			Result->SetStringField(TEXT("fallback_reason"), FallbackReason);
		}
		return FBridgeToolResult::Json(Result);
	}

	TArray<FColor> RawData;
	int32 Width = 0;
	int32 Height = 0;
	if (!TakeWidgetScreenshot(PIEViewportWidget.ToSharedRef(), RawData, Width, Height))
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Failed to capture composited PIE viewport widget"));
		Result->SetStringField(TEXT("capture_mode"), TEXT("pie-window"));
		Result->SetStringField(TEXT("requested_mode"), RequestedMode);
		Result->SetStringField(TEXT("capture_source"), TEXT("pie_composited_slate_widget"));
		Result->SetStringField(TEXT("capture_failure_kind"), TEXT("slate_capture_failed"));
		Result->SetBoolField(TEXT("structured_exception"), false);
		Result->SetBoolField(TEXT("safe_mode"), bSafeMode);
		Result->SetBoolField(TEXT("pie_running"), GEditor && GEditor->IsPlaySessionInProgress());
		Result->SetBoolField(TEXT("unsafe_window_capture"), !bSafeMode);
		Result->SetStringField(TEXT("capture_risk"), TEXT("pie_window_slate_capture_opt_in"));
		Result->SetStringField(TEXT("fallback_command"), TEXT("capture-screenshot viewport"));
		if (!FallbackReason.IsEmpty())
		{
			Result->SetStringField(TEXT("fallback_reason"), FallbackReason);
		}
		return FBridgeToolResult::Json(Result);
	}

	TSharedPtr<FJsonObject> ExtraFields = MakeShareable(new FJsonObject);
	ExtraFields->SetStringField(TEXT("requested_mode"), RequestedMode);
	ExtraFields->SetStringField(TEXT("capture_mode"), TEXT("pie-window"));
	ExtraFields->SetStringField(TEXT("capture_source"), TEXT("pie_composited_slate_widget"));
	ExtraFields->SetBoolField(TEXT("safe_mode"), bSafeMode);
	ExtraFields->SetBoolField(TEXT("pie_running"), GEditor && GEditor->IsPlaySessionInProgress());
	ExtraFields->SetBoolField(TEXT("unsafe_window_capture"), !bSafeMode);
	ExtraFields->SetBoolField(TEXT("structured_exception"), false);
	ExtraFields->SetStringField(TEXT("capture_risk"), TEXT("pie_window_slate_capture_opt_in"));
	if (!FallbackReason.IsEmpty())
	{
		ExtraFields->SetStringField(TEXT("fallback_reason"), FallbackReason);
	}

	TSharedPtr<SWindow> WidgetWindow = FSlateApplication::Get().FindWidgetWindow(PIEViewportWidget.ToSharedRef());
	if (WidgetWindow.IsValid())
	{
		ExtraFields->SetStringField(TEXT("window_title"), WidgetWindow->GetTitle().ToString());
		if (WidgetWindow->GetNativeWindow().IsValid())
		{
			const void* NativeHandle = WidgetWindow->GetNativeWindow()->GetOSWindowHandle();
			ExtraFields->SetStringField(TEXT("native_window_handle"), FString::Printf(TEXT("%p"), NativeHandle));
		}
	}

	return OutputImage(RawData, Width, Height, Options, TEXT("PIE game viewport (composited)"), ExtraFields);
}

TSharedPtr<SWidget> UCaptureScreenshotTool::FindPIEGameViewportWidget() const
{
	if (GEngine)
	{
		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			if (WorldContext.WorldType != EWorldType::PIE)
			{
				continue;
			}

			UWorld* World = WorldContext.World();
			UGameViewportClient* GameViewportClient = World ? World->GetGameViewport() : nullptr;
			if (!GameViewportClient)
			{
				continue;
			}

			TSharedPtr<SViewport> GameViewportWidget = GameViewportClient->GetGameViewportWidget();
			if (GameViewportWidget.IsValid())
			{
				return StaticCastSharedPtr<SWidget>(GameViewportWidget);
			}
		}
	}

	if (GEditor && GEditor->PlayWorld)
	{
		if (UGameViewportClient* GameViewportClient = GEditor->PlayWorld->GetGameViewport())
		{
			TSharedPtr<SViewport> GameViewportWidget = GameViewportClient->GetGameViewportWidget();
			if (GameViewportWidget.IsValid())
			{
				return StaticCastSharedPtr<SWidget>(GameViewportWidget);
			}
		}
	}

	return nullptr;
}

bool UCaptureScreenshotTool::TakeWidgetScreenshot(
	TSharedRef<SWidget> Widget,
	TArray<FColor>& OutImageData,
	int32& OutWidth,
	int32& OutHeight)
{
	// Get widget geometry
	FGeometry WidgetGeometry = Widget->GetCachedGeometry();
	FVector2D LocalSize = WidgetGeometry.GetLocalSize();

	OutWidth = FMath::TruncToInt(LocalSize.X);
	OutHeight = FMath::TruncToInt(LocalSize.Y);

	if (OutWidth <= 0 || OutHeight <= 0)
	{
		return false;
	}

	// Create render target
	FIntPoint Size(OutWidth, OutHeight);
	TArray<FColor> ColorData;

	// Use Slate's screenshot functionality
	FIntVector OutSize;
	FSlateRect WidgetRect = WidgetGeometry.GetRenderBoundingRect();

	// Get the window containing this widget
	TSharedPtr<SWindow> WidgetWindow = FSlateApplication::Get().FindWidgetWindow(Widget);
	if (!WidgetWindow.IsValid())
	{
		return false;
	}

	// Take screenshot of the widget area
	FIntRect CaptureRect(
		FMath::TruncToInt(WidgetRect.Left),
		FMath::TruncToInt(WidgetRect.Top),
		FMath::TruncToInt(WidgetRect.Right),
		FMath::TruncToInt(WidgetRect.Bottom)
	);

	if (!FSlateApplication::Get().TakeScreenshot(WidgetWindow.ToSharedRef(), CaptureRect, ColorData, OutSize))
	{
		return false;
	}

	OutImageData = MoveTemp(ColorData);
	OutWidth = OutSize.X;
	OutHeight = OutSize.Y;

	return true;
}

FBridgeToolResult UCaptureScreenshotTool::OutputImage(
	TArray<FColor>& RawData,
	int32 Width,
	int32 Height,
	const FBridgeImageOutputOptions& Options,
	const FString& CaptureMode,
	const TSharedPtr<FJsonObject>& ExtraFields)
{
	FString Error;
	TSharedPtr<FJsonObject> Result = SoftUE::ImageOutput::EncodeCapture(
		RawData, Width, Height, Options, TEXT("screenshot_"), Error);
	if (!Result.IsValid())
	{
		return FBridgeToolResult::Error(Error);
	}

	Result->SetStringField(TEXT("captured"), CaptureMode);
	CopyJsonFields(ExtraFields, Result);

	FString FilePath;
	if (Result->TryGetStringField(TEXT("file_path"), FilePath))
	{
		Result->SetStringField(TEXT("message"), FString::Printf(
			TEXT("Screenshot of %s saved to %s"), *CaptureMode, *FilePath));
	}

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("capture-screenshot: Captured %s as %s -> %s (%d bytes)"),
		*CaptureMode,
		*Options.Format,
		FilePath.IsEmpty() ? TEXT("base64") : *FilePath,
		static_cast<int32>(Result->GetNumberField(TEXT("size_bytes"))));

	return FBridgeToolResult::Json(Result);
}
