// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Asset/InspectCustomizableObjectGraphTool.h"

#include "Tools/Asset/MutableIntrospectionUtils.h"

FString UInspectCustomizableObjectGraphTool::GetToolDescription() const
{
	return TEXT("Inspect a Mutable/CustomizableObject asset graph and return machine-readable nodes, edges, and graph structure. "
		"If Mutable is not enabled in the project, the command returns an unavailable status instead of failing the build.");
}

TMap<FString, FBridgeSchemaProperty> UInspectCustomizableObjectGraphTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to the CustomizableObject asset (e.g., /Game/Characters/CO_Hero.CO_Hero)");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty IncludeNodeProperties;
	IncludeNodeProperties.Type = TEXT("boolean");
	IncludeNodeProperties.Description = TEXT("Include a broad reflected property dump for each node (default: false).");
	Schema.Add(TEXT("include_node_properties"), IncludeNodeProperties);

	return Schema;
}

TArray<FString> UInspectCustomizableObjectGraphTool::GetRequiredParams() const
{
	return {TEXT("asset_path")};
}

FBridgeToolResult UInspectCustomizableObjectGraphTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FBridgeToolResult::Error(TEXT("Missing required parameter: asset_path"));
	}

	const bool bIncludeNodeProperties = GetBoolArgOrDefault(Arguments, TEXT("include_node_properties"), false);
	return FBridgeToolResult::Json(
		MutableIntrospectionUtils::BuildCustomizableObjectGraphResult(AssetPath, bIncludeNodeProperties));
}
