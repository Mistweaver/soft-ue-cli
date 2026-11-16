// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Scripting/BridgePython.h"
#include "IPythonScriptPlugin.h"
#include "Modules/ModuleManager.h"

namespace SoftUE::Python
{
	bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("PythonScriptPlugin") && IPythonScriptPlugin::Get() != nullptr;
	}

	FBridgePythonResult Execute(const FString& Command, EPythonCommandExecutionMode Mode)
	{
		FBridgePythonResult Result;
		IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
		if (!PythonPlugin)
		{
			Result.Error = TEXT("PythonScriptPlugin interface not available");
			return Result;
		}

		FPythonCommandEx PythonCommand;
		PythonCommand.Flags = EPythonCommandFlags::Unattended;
		PythonCommand.ExecutionMode = Mode;
		PythonCommand.FileExecutionScope = EPythonFileExecutionScope::Public;
		PythonCommand.Command = Command;
		Result.bSucceeded = PythonPlugin->ExecPythonCommandEx(PythonCommand);

		TArray<FString> Lines;
		for (const FPythonLogOutputEntry& Entry : PythonCommand.LogOutput)
		{
			if (!Entry.Output.IsEmpty())
			{
				Lines.Add(Entry.Output);
			}
		}
		Result.Output = FString::Join(Lines, TEXT("\n"));

		Result.Error = PythonCommand.CommandResult;
		if (!Result.bSucceeded && Result.Error.IsEmpty())
		{
			Result.Error = Result.Output;
		}
		return Result;
	}
}
