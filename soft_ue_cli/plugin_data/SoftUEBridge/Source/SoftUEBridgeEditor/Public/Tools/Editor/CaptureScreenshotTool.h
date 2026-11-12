// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "Utils/BridgeImageOutput.h"
#include "CaptureScreenshotTool.generated.h"

/**
 * Tool to capture screenshots of editor windows, tabs, regions, or the game viewport.
 *
 * Supports five capture modes:
 * 1. Window Mode: Captures the entire editor window
 * 2. Tab Mode: Captures a specific editor tab/panel (Blueprint Editor, Material Editor, etc.)
 * 3. Region Mode: Captures a specific rectangular region by coordinates
 * 4. Viewport Mode: Captures the PIE game screen (delegates to runtime capture-viewport tool)
 * 5. PIE Window Mode: Captures the composited PIE game viewport, including UMG
 *
 * Output can be saved to a file or returned as base64-encoded image data.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UCaptureScreenshotTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("capture-screenshot"); }

	virtual FString GetToolDescription() const override
	{
		return TEXT("Capture screenshots of editor windows, tabs, regions, or the game viewport. "
				   "Use 'mode' to specify capture type: 'window' (entire editor), "
				   "'tab' (specific editor panel), 'region' (rectangular area), "
				   "'viewport' (PIE render target), or 'pie-window' (composited PIE viewport). "
				   "Output as file or base64-encoded image data.");
	}

	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;

	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	/**
	 * Capture the PIE game viewport.
	 * @param Options - Shared image format, size, color, and output settings
	 * @return Tool result with image data or file path
	 */
	FBridgeToolResult CaptureViewport(
		const FBridgeImageOutputOptions& Options);

	/**
	 * Capture the entire editor window.
	 * @param Options - Shared image format, size, color, and output settings
	 * @return Tool result with image data or file path
	 */
	FBridgeToolResult CaptureWindow(
		const FBridgeImageOutputOptions& Options,
		bool bSafeMode);

	/**
	 * Capture the composited PIE game viewport Slate widget, including UMG.
	 */
	FBridgeToolResult CapturePIEWindow(
		const FBridgeImageOutputOptions& Options,
		bool bSafeMode,
		const FString& RequestedMode,
		const FString& FallbackReason);

	/**
	 * Capture a specific editor tab/panel.
	 * @param TabName - Name of the tab to capture
	 * @param Options - Shared image format, size, color, and output settings
	 * @return Tool result with image data or file path
	 */
	FBridgeToolResult CaptureTab(
		const FString& TabName,
		const FBridgeImageOutputOptions& Options);

	/**
	 * Capture a specific rectangular region.
	 * @param Region - Rectangle to capture [x, y, width, height]
	 * @param Options - Shared image format, size, color, and output settings
	 * @return Tool result with image data or file path
	 */
	FBridgeToolResult CaptureRegion(
		const FIntRect& Region,
		const FBridgeImageOutputOptions& Options);

	/**
	 * Capture screenshot from a specific window.
	 * @param Window - Window to capture
	 * @param Options - Shared image format, size, color, and output settings
	 * @return Tool result with image data or file path
	 */
	FBridgeToolResult CaptureFromWindow(
		TSharedPtr<SWindow> Window,
		const FBridgeImageOutputOptions& Options,
		TSharedPtr<FJsonObject> ExtraFields = nullptr);

	/**
	 * Take a screenshot of a widget and return raw RGBA data.
	 * @param Widget - Widget to capture
	 * @param OutImageData - Output raw RGBA pixel data
	 * @param OutWidth - Output image width
	 * @param OutHeight - Output image height
	 * @return True if successful
	 */
	bool TakeWidgetScreenshot(
		TSharedRef<SWidget> Widget,
		TArray<FColor>& OutImageData,
		int32& OutWidth,
		int32& OutHeight);

	TSharedPtr<SWidget> FindPIEGameViewportWidget() const;

	/**
	 * Encode a capture through the shared image output and add the screenshot fields.
	 * @param CaptureMode - Description of what was captured (for the response message)
	 * @param ExtraFields - Mode-specific fields copied into the response
	 */
	FBridgeToolResult OutputImage(
		TArray<FColor>& RawData,
		int32 Width,
		int32 Height,
		const FBridgeImageOutputOptions& Options,
		const FString& CaptureMode,
		const TSharedPtr<FJsonObject>& ExtraFields = nullptr);
};
