// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "AnimPoseAtTool.generated.h"

/**
 * Samples an animation asset at a frame or time and reports bone transforms, without PIE. The pose
 * matches the Animation Editor preview on the chosen mesh: no AnimBlueprint, post-process, or physics.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UAnimPoseAtTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("anim-pose-at"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;
};
