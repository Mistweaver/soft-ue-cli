// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/CaptureViewportTool.h"
#include "SoftUEBridgeModule.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Utils/BridgeImageOutput.h"

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "RenderingThread.h"
#endif

TMap<FString, FBridgeSchemaProperty> UCaptureViewportTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	Schema.Add(TEXT("source"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("Which viewport to capture: 'auto' (default, tries game then editor), 'game' for PIE/standalone, 'editor' for the level editor viewport"),
		false,
		{TEXT("auto"), TEXT("game"), TEXT("editor")}
	});

	Schema.Add(TEXT("format"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("Image format (default: png)"),
		false,
		{TEXT("png"), TEXT("jpeg")}
	});

	Schema.Add(TEXT("output"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("Output mode: 'file' saves to temp dir and returns path (default), 'base64' returns encoded data"),
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
		TEXT("Delete previous viewport capture files before writing this file output (default: false)"),
		false
	});

	return Schema;
}

FBridgeToolResult UCaptureViewportTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString Source = GetStringArgOrDefault(Arguments, TEXT("source"), TEXT("auto"));
	const FBridgeImageOutputOptions Options = FBridgeImageOutputOptions::FromArguments(Arguments);

	if (Source == TEXT("editor"))
	{
#if WITH_EDITOR
		return CaptureEditorViewport(Options);
#else
		return FBridgeToolResult::Error(TEXT("Editor viewport capture is only available in editor builds"));
#endif
	}

	if (Source == TEXT("game"))
	{
		return CaptureGameViewport(Options);
	}

	if (Source == TEXT("auto"))
	{
		// Auto-detect: try game viewport first, fall back to editor viewport
		FBridgeToolResult GameResult = CaptureGameViewport(Options);
		if (!GameResult.bIsError)
		{
			return GameResult;
		}

#if WITH_EDITOR
		return CaptureEditorViewport(Options);
#else
		return GameResult; // Return the game viewport error in non-editor builds
#endif
	}

	return FBridgeToolResult::Error(FString::Printf(
		TEXT("Unknown source '%s'. Valid values: 'auto', 'game', 'editor'"), *Source));
}

FBridgeToolResult UCaptureViewportTool::CaptureGameViewport(const FBridgeImageOutputOptions& Options)
{
	// Find a game viewport — works for both PIE and standalone
	FViewport* GameViewport = nullptr;
	FString WorldTypeName;

	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		const bool bHasViewport = WorldContext.GameViewport && WorldContext.GameViewport->Viewport;
		const bool bIsPlayable = WorldContext.WorldType == EWorldType::PIE
			|| WorldContext.WorldType == EWorldType::Game;

		if (!bHasViewport || !bIsPlayable)
		{
			continue;
		}

		GameViewport = WorldContext.GameViewport->Viewport;
		WorldTypeName = (WorldContext.WorldType == EWorldType::PIE) ? TEXT("PIE") : TEXT("Standalone");
		break;
	}

	if (!GameViewport)
	{
		return FBridgeToolResult::Error(
			TEXT("No game viewport found. Start a PIE session or run as standalone first."));
	}

	// ReadPixels internally enqueues a render command and flushes,
	// so it handles render thread synchronization.
	TArray<FColor> RawData;
	if (!GameViewport->ReadPixels(RawData))
	{
		return FBridgeToolResult::Error(TEXT("Failed to read pixels from game viewport"));
	}

	const FIntPoint ViewportSize = GameViewport->GetSizeXY();
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0 || RawData.Num() == 0)
	{
		return FBridgeToolResult::Error(TEXT("Game viewport has no valid image data"));
	}

	return OutputCapture(RawData, ViewportSize, Options, WorldTypeName);
}

#if WITH_EDITOR
FBridgeToolResult UCaptureViewportTool::CaptureEditorViewport(const FBridgeImageOutputOptions& Options)
{
	if (!GEditor)
	{
		return FBridgeToolResult::Error(
			TEXT("GEditor is not available. The editor may not be fully initialized."));
	}

	FViewport* EditorViewport = GEditor->GetActiveViewport();
	if (!EditorViewport)
	{
		return FBridgeToolResult::Error(
			TEXT("No active editor viewport found. Click on the level editor viewport first."));
	}

	const FIntPoint ViewportSize = EditorViewport->GetSizeXY();
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
	{
		return FBridgeToolResult::Error(TEXT("Editor viewport has invalid size"));
	}

	// Editor viewports render to a render target (not a swapchain backbuffer),
	// so we can force a draw and then read pixels directly.
	EditorViewport->Draw(false);
	FlushRenderingCommands();

	TArray<FColor> RawData;
	if (!EditorViewport->ReadPixels(RawData))
	{
		return FBridgeToolResult::Error(TEXT("Failed to read pixels from editor viewport"));
	}

	if (RawData.Num() == 0)
	{
		return FBridgeToolResult::Error(TEXT("Editor viewport returned no pixel data"));
	}

	return OutputCapture(RawData, ViewportSize, Options, TEXT("editor"));
}
#endif

FBridgeToolResult UCaptureViewportTool::OutputCapture(
	TArray<FColor>& RawData,
	const FIntPoint& ViewportSize,
	const FBridgeImageOutputOptions& Options,
	const FString& SourceName)
{
	FString Error;
	TSharedPtr<FJsonObject> Result = SoftUE::ImageOutput::EncodeCapture(
		RawData, ViewportSize.X, ViewportSize.Y, Options, TEXT("viewport_"), Error);
	if (!Result.IsValid())
	{
		return FBridgeToolResult::Error(Error);
	}

	FString FilePath;
	if (Result->TryGetStringField(TEXT("file_path"), FilePath))
	{
		Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Viewport screenshot saved to %s"), *FilePath));
	}

	UE_LOG(LogSoftUEBridge, Log, TEXT("capture-viewport: Captured %s viewport %dx%d -> %dx%d as %s/%s (%d bytes)"),
		*SourceName,
		ViewportSize.X,
		ViewportSize.Y,
		static_cast<int32>(Result->GetNumberField(TEXT("width"))),
		static_cast<int32>(Result->GetNumberField(TEXT("height"))),
		*Options.Format,
		*Result->GetStringField(TEXT("color_mode")),
		static_cast<int32>(Result->GetNumberField(TEXT("size_bytes"))));

	return FBridgeToolResult::Json(Result);
}
