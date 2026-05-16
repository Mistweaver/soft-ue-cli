// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Asset/InspectMutableDiagnosticsTool.h"

#include "Tools/Asset/MutableIntrospectionUtils.h"

FString UInspectMutableDiagnosticsTool::GetToolDescription() const
{
	return TEXT("Inspect Mutable/CustomizableObject availability and best-effort runtime capability signals such as projector usage, plugin state, and graph-derived diagnostics. "
		"If Mutable is not enabled in the project, the command returns an unavailable/limited status instead of failing the build.");
}

TMap<FString, FBridgeSchemaProperty> UInspectMutableDiagnosticsTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to the CustomizableObject asset (e.g., /Game/Characters/CO_Hero.CO_Hero)");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	return Schema;
}

TArray<FString> UInspectMutableDiagnosticsTool::GetRequiredParams() const
{
	return {TEXT("asset_path")};
}

FBridgeToolResult UInspectMutableDiagnosticsTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FBridgeToolResult::Error(TEXT("Missing required parameter: asset_path"));
	}

	return FBridgeToolResult::Json(MutableIntrospectionUtils::BuildMutableDiagnosticsResult(AssetPath));
}
