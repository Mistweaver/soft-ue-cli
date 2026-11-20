// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "CaptureViewportTool.generated.h"

struct FBridgeImageOutputOptions;

/**
 * Capture the viewport screenshot (editor, PIE, or standalone).
 * Saves to a temp file and returns the path, or returns base64 data.
 */
UCLASS()
class UCaptureViewportTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("capture-viewport"); }
	virtual FString GetToolDescription() const override
	{
		return TEXT("Capture a viewport screenshot. Use source='editor' for the level editor viewport, "
				   "or source='game' (default) for PIE/standalone. "
				   "Returns a temp file path by default, or base64-encoded data.");
	}
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	FBridgeToolResult CaptureGameViewport(const FBridgeImageOutputOptions& Options);

#if WITH_EDITOR
	FBridgeToolResult CaptureEditorViewport(const FBridgeImageOutputOptions& Options);
#endif

	/** Encodes the pixels and adds the viewport-specific message. */
	FBridgeToolResult OutputCapture(
		TArray<FColor>& RawData,
		const FIntPoint& ViewportSize,
		const FBridgeImageOutputOptions& Options,
		const FString& SourceName);
};
