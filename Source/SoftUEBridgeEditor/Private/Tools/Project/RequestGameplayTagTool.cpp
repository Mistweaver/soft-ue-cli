// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Project/RequestGameplayTagTool.h"
#include "GameplayTagContainer.h"

FString URequestGameplayTagTool::GetToolDescription() const
{
	return TEXT("Resolve a registered GameplayTag by name and return its validity.");
}

TMap<FString, FBridgeSchemaProperty> URequestGameplayTagTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	FBridgeSchemaProperty TagName;
	TagName.Type = TEXT("string");
	TagName.Description = TEXT("GameplayTag name to resolve");
	TagName.bRequired = true;
	Schema.Add(TEXT("tag_name"), TagName);
	return Schema;
}

FBridgeToolResult URequestGameplayTagTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString TagName = GetStringArgOrDefault(Arguments, TEXT("tag_name"));
	if (TagName.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("request-gameplay-tag: tag_name is required"));
	}

	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TagName), false);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), Tag.IsValid());
	Result->SetBoolField(TEXT("valid"), Tag.IsValid());
	Result->SetStringField(TEXT("requested_name"), TagName);
	Result->SetStringField(TEXT("tag_name"), Tag.IsValid() ? Tag.ToString() : FString());
	Result->SetStringField(TEXT("export_text"), Tag.IsValid() ? FString::Printf(TEXT("(TagName=\"%s\")"), *Tag.ToString()) : FString());
	return FBridgeToolResult::Json(Result);
}
