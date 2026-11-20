// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PythonScriptTypes.h"

/** What one Python command printed, and the error text when it failed. */
struct SOFTUEBRIDGEEDITOR_API FBridgePythonResult
{
	bool bSucceeded = false;
	/** Every non-empty log line the command produced, joined with newlines. */
	FString Output;
	/** The command result; falls back to Output when a failed command reports no result. */
	FString Error;
};

/** Runs Python in the editor for bridge tools. */
namespace SoftUE::Python
{
	/** True when PythonScriptPlugin is loaded and its interface is ready. Tools return plugin_unavailable otherwise. */
	SOFTUEBRIDGEEDITOR_API bool IsAvailable();

	/**
	 * Runs Command unattended in the public scope: a file path or source text for ExecuteFile, or a single
	 * expression for EvaluateStatement. Call only when IsAvailable() is true.
	 */
	SOFTUEBRIDGEEDITOR_API FBridgePythonResult Execute(const FString& Command, EPythonCommandExecutionMode Mode);
}
