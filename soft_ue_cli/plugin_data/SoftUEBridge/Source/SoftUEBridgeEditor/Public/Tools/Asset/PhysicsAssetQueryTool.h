// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "PhysicsAssetQueryTool.generated.h"

/**
 * Read-only report of a PhysicsAsset: each body's collision shapes with sizes and
 * transforms, and the constraints between bodies with their limits.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UPhysicsAssetQueryTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("asset-query-physics"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;
	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;
};
