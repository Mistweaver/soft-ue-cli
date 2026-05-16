// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Asset/InspectMutableParametersTool.h"

#include "Tools/Asset/MutableIntrospectionUtils.h"

FString UInspectMutableParametersTool::GetToolDescription() const
{
	return TEXT("Inspect a Mutable/CustomizableObject asset and derive structured parameter metadata such as groups, defaults, options, tags, and related nodes. "
		"If Mutable is not enabled in the project, the command returns an unavailable status instead of failing the build.");
}

TMap<FString, FBridgeSchemaProperty> UInspectMutableParametersTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to the CustomizableObject asset (e.g., /Game/Characters/CO_Hero.CO_Hero)");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	return Schema;
}

TArray<FString> UInspectMutableParametersTool::GetRequiredParams() const
{
	return {TEXT("asset_path")};
}

FBridgeToolResult UInspectMutableParametersTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FBridgeToolResult::Error(TEXT("Missing required parameter: asset_path"));
	}

	return FBridgeToolResult::Json(MutableIntrospectionUtils::BuildMutableParameterResult(AssetPath));
}
