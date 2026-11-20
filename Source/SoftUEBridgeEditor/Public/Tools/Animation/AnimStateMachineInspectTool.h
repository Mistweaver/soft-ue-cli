// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "AnimStateMachineInspectTool.generated.h"

/**
 * Read-only report of AnimBlueprint state machines: states, conduits, aliases,
 * the entry state, and transitions with blend settings and a rule summary.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UAnimStateMachineInspectTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("anim-state-machine-inspect"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;
};
