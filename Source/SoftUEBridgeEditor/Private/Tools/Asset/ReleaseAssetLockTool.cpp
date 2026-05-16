// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Asset/ReleaseAssetLockTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "Utils/BridgeAssetModifier.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"

FString UReleaseAssetLockTool::GetToolDescription() const
{
	return TEXT("Best-effort release of editor-side asset handles by closing editors and forcing GC.");
}

TMap<FString, FBridgeSchemaProperty> UReleaseAssetLockTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to unlock");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);
	return Schema;
}

FBridgeToolResult UReleaseAssetLockTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("release-asset-lock: asset_path is required"));
	}

	FString LoadError;
	UObject* Asset = FBridgeAssetModifier::LoadAssetByPath(AssetPath, LoadError);
	if (!Asset)
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("release-asset-lock: failed to load asset '%s': %s"), *AssetPath, *LoadError));
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (AssetEditorSubsystem)
	{
		AssetEditorSubsystem->CloseAllEditorsForAsset(Asset);
	}

	FlushAsyncLoading();
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("package_file"), FPackageName::LongPackageNameToFilename(AssetPath, FPackageName::GetAssetPackageExtension()));
	Result->SetStringField(TEXT("message"), TEXT("Closed asset editors and forced garbage collection."));
	return FBridgeToolResult::Json(Result);
}
