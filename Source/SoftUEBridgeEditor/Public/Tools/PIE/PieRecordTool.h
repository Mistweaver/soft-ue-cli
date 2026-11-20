// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "PieRecordTool.generated.h"

/**
 * Advance the running PIE world at a fixed delta for a duration of simulated time, calling a Python
 * sampler after every tick and writing one JSON line per tick.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UPieRecordTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("pie-record"); }
	virtual FString GetToolDescription() const override;
	virtual EBridgeToolExecutionContext GetExecutionContextRequirement() const override
	{
		return EBridgeToolExecutionContext::PIEWorldTickSafe;
	}
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override { return {TEXT("duration"), TEXT("script")}; }
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;
};
