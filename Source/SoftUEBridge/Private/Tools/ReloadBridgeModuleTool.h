// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "Tools/BridgeToolBase.h"
#include "ReloadBridgeModuleTool.generated.h"

UCLASS()
class UReloadBridgeModuleTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("reload-bridge-module"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual FBridgeToolResult Execute(const TSharedPtr<FJsonObject>& Args, const FBridgeToolContext& Ctx) override;
};
