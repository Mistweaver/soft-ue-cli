// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "QueryMaterialTool.generated.h"

/**
 * Consolidated tool for Material inspection.
 * Replaces: get-material-graph, get-material-parameters
 *
 * Usage:
 * - Default: Returns both graph and parameters
 * - include="graph": Only graph structure
 * - include="parameters": Only parameters
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UQueryMaterialTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("query-material"); }
	virtual FString GetToolDescription() const override;
	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;

	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	// === Graph extraction ===

	/** Extract material expression graph */
	TSharedPtr<FJsonObject> ExtractGraph(class UMaterial* Material, bool bIncludePositions) const;

	/**
	 * What each material output property (BaseColor, EmissiveColor, Opacity, ...) is wired to.
	 * Without this the graph is a bag of expressions with no root, and the wiring has to be guessed
	 * from output indices.
	 */
	TArray<TSharedPtr<FJsonValue>> ExtractRootConnections(class UMaterial* Material) const;

	/**
	 * Shading model and blend mode as resolved strings. The underlying shading model is a bitmask
	 * whose bit N means shading model N, so the raw field reads as an off-by-one version of the
	 * enum and is easy to decode backwards.
	 */
	TSharedPtr<FJsonObject> ExtractMaterialSettings(class UMaterialInterface* Material) const;

	/** Convert expression to JSON */
	TSharedPtr<FJsonObject> ExpressionToJson(class UMaterialExpression* Expression, bool bIncludePositions) const;

	// === Parameter extraction ===

	/** Extract material parameters */
	TSharedPtr<FJsonObject> ExtractParameters(class UMaterialInterface* Material,
		bool bIncludeDefaults, const FString& ParameterFilter) const;

	/** Extract scalar parameters */
	void ExtractScalarParameters(class UMaterialInterface* Material, TArray<TSharedPtr<FJsonValue>>& OutArray) const;

	/** Extract vector parameters */
	void ExtractVectorParameters(class UMaterialInterface* Material, TArray<TSharedPtr<FJsonValue>>& OutArray) const;

	/** Extract texture parameters */
	void ExtractTextureParameters(class UMaterialInterface* Material, TArray<TSharedPtr<FJsonValue>>& OutArray) const;

	/** Extract static switch parameters */
	void ExtractStaticSwitchParameters(class UMaterialInterface* Material, TArray<TSharedPtr<FJsonValue>>& OutArray) const;

	/** Walk parent chain from leaf to root Material */
	TArray<TSharedPtr<FJsonValue>> ExtractParentChain(class UMaterialInterface* Material) const;
};
