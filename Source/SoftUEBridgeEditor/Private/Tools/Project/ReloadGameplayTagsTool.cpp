// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Project/ReloadGameplayTagsTool.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "GameplayTagsSettings.h"

FString UReloadGameplayTagsTool::GetToolDescription() const
{
	return TEXT("Reload GameplayTags settings and refresh in-memory tag tables.");
}

TMap<FString, FBridgeSchemaProperty> UReloadGameplayTagsTool::GetInputSchema() const
{
	return {};
}

FBridgeToolResult UReloadGameplayTagsTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	if (UGameplayTagsSettings* Settings = GetMutableDefault<UGameplayTagsSettings>())
	{
		Settings->ReloadConfig();
	}

	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	Manager.LoadGameplayTagTables(false);

	FGameplayTagContainer AllTags;
	Manager.RequestAllGameplayTags(AllTags, false);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("tag_count"), AllTags.Num());
	Result->SetStringField(TEXT("message"), TEXT("GameplayTags settings reloaded and tag tables refreshed."));
	return FBridgeToolResult::Json(Result);
}
