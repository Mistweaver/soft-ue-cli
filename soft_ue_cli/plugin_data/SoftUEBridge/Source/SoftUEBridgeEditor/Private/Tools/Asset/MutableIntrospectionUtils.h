// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace MutableIntrospectionUtils
{
	TSharedPtr<FJsonObject> BuildCustomizableObjectGraphResult(const FString& AssetPath, bool bIncludeNodeProperties);
	TSharedPtr<FJsonObject> BuildMutableParameterResult(const FString& AssetPath);
	TSharedPtr<FJsonObject> BuildMutableDiagnosticsResult(const FString& AssetPath);

	/** True when any Mutable or CustomizableObject plugin is enabled in the running editor. */
	bool IsMutablePluginEnabled();
}
