// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/PIE/ExecConsoleCommandTool.h"
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
	Schema.Add(TEXT("player_index"), Prop(TEXT("integer"), TEXT("Optional player controller index for PIE/game")));
	return Schema;
}

FBridgeToolResult UExecConsoleCommandTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString Command = GetStringArgOrDefault(Arguments, TEXT("command"));
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("pie"));
	const int32 PlayerIndex = GetIntArgOrDefault(Arguments, TEXT("player_index"), 0);

	if (Command.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("exec-console-command: command is required"));
	}

	UWorld* World = FindWorldByType(WorldType);
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
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, PlayerIndex))
		{
			Output = PC->ConsoleCommand(Command, true);
			bSuccess = true;
			bUsedPlayerController = true;
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
	Result->SetBoolField(TEXT("used_player_controller"), bUsedPlayerController);
	Result->SetNumberField(TEXT("player_index"), PlayerIndex);
	Result->SetStringField(TEXT("output"), Output);

	return FBridgeToolResult::Json(Result);
}
