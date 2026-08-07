// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/PIE/ExecConsoleCommandTool.h"
#include "Tools/PIE/BridgePIEWorlds.h"
#include "SoftUEBridgeEditorModule.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

FString UExecConsoleCommandTool::GetToolDescription() const
{
	return TEXT("Execute an arbitrary UE console command in the editor, PIE, or game world.");
}

TMap<FString, FBridgeSchemaProperty> UExecConsoleCommandTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	auto Prop = [](const FString& Type, const FString& Desc) {
		FBridgeSchemaProperty P; P.Type = Type; P.Description = Desc; return P;
	};

	Schema.Add(TEXT("command"), Prop(TEXT("string"), TEXT("Console command to execute")));
	Schema.Add(TEXT("world"), Prop(TEXT("string"), TEXT("World context: pie, editor, or game")));
	Schema.Add(TEXT("pie_instance"), Prop(TEXT("integer"), TEXT("Optional FWorldContext::PIEInstance to target; valid only for PIE")));
	Schema.Add(TEXT("player_index"), Prop(TEXT("integer"), TEXT("Optional local player controller index inside the selected PIE/game world")));
	return Schema;
}

FBridgeToolResult UExecConsoleCommandTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString Command = GetStringArgOrDefault(Arguments, TEXT("command"));
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("pie"));
	const bool bHasPIEInstance = Arguments->HasField(TEXT("pie_instance"));
	const bool bHasPlayerIndex = Arguments->HasField(TEXT("player_index"));
	const int32 PIEInstance = GetIntArgOrDefault(Arguments, TEXT("pie_instance"), INDEX_NONE);
	const int32 PlayerIndex = GetIntArgOrDefault(Arguments, TEXT("player_index"), 0);

	if (Command.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("exec-console-command: command is required"));
	}

	if (bHasPIEInstance && WorldType != TEXT("pie"))
	{
		return FBridgeToolResult::Error(TEXT("exec-console-command: pie_instance is valid only when world is 'pie'"));
	}

	UWorld* World = nullptr;
	int32 SelectedPIEInstance = INDEX_NONE;
	if (WorldType == TEXT("pie"))
	{
		const TArray<FBridgePIEWorld> PIEWorlds = FBridgePIEWorlds::Enumerate();
		const FBridgePIEWorld* Selected = bHasPIEInstance
			? FBridgePIEWorlds::Resolve(PIEWorlds, PIEInstance)
			: (PIEWorlds.Num() > 0 ? &PIEWorlds[0] : nullptr);
		if (!Selected)
		{
			return bHasPIEInstance
				? FBridgeToolResult::Error(FString::Printf(TEXT("exec-console-command: PIE instance %d not found; available PIE instances: %s"), PIEInstance, *FBridgePIEWorlds::AvailableInstanceIds(PIEWorlds)))
				: FBridgeToolResult::Error(TEXT("exec-console-command: no pie world available"));
		}
		World = Selected->World;
		SelectedPIEInstance = Selected->PIEInstance;
	}
	else
	{
		World = FindWorldByType(WorldType);
	}
	if (!World)
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("exec-console-command: no %s world available"), *WorldType));
	}

	bool bSuccess = false;
	bool bUsedPlayerController = false;
	FString Output;

	if (WorldType != TEXT("editor"))
	{
		APlayerController* PC = bHasPlayerIndex
			? FBridgePIEWorlds::ResolveLocalPlayerController(World, PlayerIndex)
			: UGameplayStatics::GetPlayerController(World, 0);
		if (PC)
		{
			Output = PC->ConsoleCommand(Command, true);
			bSuccess = true;
			bUsedPlayerController = true;
		}
		else if (bHasPlayerIndex)
		{
			return FBridgeToolResult::Error(FString::Printf(
				TEXT("exec-console-command: local player controller %d not found in world '%s'"),
				PlayerIndex, *World->GetName()));
		}
	}

	if (!bSuccess && GEngine)
	{
		bSuccess = GEngine->Exec(World, *Command);
	}

	if (!bSuccess)
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("exec-console-command: failed to execute '%s'"), *Command));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("command"), Command);
	Result->SetStringField(TEXT("world"), WorldType);
	Result->SetNumberField(TEXT("pie_instance"), SelectedPIEInstance);
	Result->SetStringField(TEXT("world_name"), World->GetName());
	Result->SetStringField(TEXT("net_mode"), FBridgePIEWorlds::NetModeName(World->GetNetMode()));
	Result->SetBoolField(TEXT("used_player_controller"), bUsedPlayerController);
	Result->SetNumberField(TEXT("player_index"), PlayerIndex);
	Result->SetStringField(TEXT("output"), Output);

	return FBridgeToolResult::Json(Result);
}
