// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/ReloadBridgeModuleTool.h"
#include "SoftUEBridgeModule.h"
#include "Modules/ModuleManager.h"
#include "Tools/BridgeToolRegistry.h"

FString UReloadBridgeModuleTool::GetToolDescription() const
{
	return TEXT("Unload and reload a SoftUEBridge plugin module from the DLL currently on disk. Defaults to SoftUEBridgeEditor so the runtime HTTP server stays alive.");
}

TMap<FString, FBridgeSchemaProperty> UReloadBridgeModuleTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty Module;
	Module.Type = TEXT("string");
	Module.Description = TEXT("Module name to reload (default: SoftUEBridgeEditor). The SoftUEBridge runtime module cannot reload itself because it owns the HTTP server.");
	Module.bRequired = false;
	Schema.Add(TEXT("module"), Module);

	return Schema;
}

FBridgeToolResult UReloadBridgeModuleTool::Execute(
	const TSharedPtr<FJsonObject>& Args,
	const FBridgeToolContext& /*Ctx*/)
{
#if !WITH_EDITOR
	return FBridgeToolResult::Error(TEXT("reload-bridge-module is only available in editor builds"));
#else
	FString ModuleName = GetStringArgOrDefault(Args, TEXT("module"), TEXT("SoftUEBridgeEditor")).TrimStartAndEnd();
	if (ModuleName.IsEmpty())
	{
		ModuleName = TEXT("SoftUEBridgeEditor");
	}
	if (ModuleName.Equals(TEXT("SoftUEBridge"), ESearchCase::IgnoreCase))
	{
		return FBridgeToolResult::Error(TEXT("SoftUEBridge runtime module cannot reload itself because it owns the HTTP server and reload-bridge-module tool."));
	}

	FModuleManager& ModuleManager = FModuleManager::Get();
	const FName ModuleFName(*ModuleName);
	const bool bWasLoaded = ModuleManager.IsModuleLoaded(ModuleFName);
	FString ModulePathBefore;
	if (bWasLoaded)
	{
		ModulePathBefore = ModuleManager.GetModuleFilename(ModuleFName);
	}

	int32 RemovedToolCount = 0;
	bool bUnloaded = false;
	if (bWasLoaded)
	{
		RemovedToolCount = FBridgeToolRegistry::Get().RemoveToolsForModule(ModuleName);
		bUnloaded = ModuleManager.UnloadModule(ModuleFName, false, true);
		if (!bUnloaded)
		{
			return FBridgeToolResult::Error(FString::Printf(
				TEXT("Failed to unload module %s. Restart the editor if tools were partially removed."),
				*ModuleName));
		}
	}

	EModuleLoadResult LoadResult = EModuleLoadResult::Success;
	IModuleInterface* LoadedModule = ModuleManager.LoadModuleWithFailureReason(ModuleFName, LoadResult);
	if (!LoadedModule || LoadResult != EModuleLoadResult::Success)
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("Failed to load module %s: %s"),
			*ModuleName,
			LexToString(LoadResult)));
	}

	const FString ModulePathAfter = ModuleManager.GetModuleFilename(ModuleFName);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("module"), ModuleName);
	Result->SetBoolField(TEXT("was_loaded"), bWasLoaded);
	Result->SetBoolField(TEXT("unloaded"), bUnloaded);
	Result->SetBoolField(TEXT("loaded"), true);
	Result->SetNumberField(TEXT("removed_tool_count"), RemovedToolCount);
	Result->SetNumberField(TEXT("registered_tool_count"), FBridgeToolRegistry::Get().GetToolCount());
	if (!ModulePathBefore.IsEmpty())
	{
		Result->SetStringField(TEXT("module_path_before"), ModulePathBefore);
	}
	if (!ModulePathAfter.IsEmpty())
	{
		Result->SetStringField(TEXT("module_path_after"), ModulePathAfter);
	}
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Reloaded module %s from disk."), *ModuleName));
	return FBridgeToolResult::Json(Result);
#endif
}
