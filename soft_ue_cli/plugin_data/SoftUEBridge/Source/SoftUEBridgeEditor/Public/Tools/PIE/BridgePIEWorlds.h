// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Engine/EngineBaseTypes.h"
#include "Templates/ValueOrError.h"

class UWorld;
class APlayerController;

struct SOFTUEBRIDGEEDITOR_API FBridgePIEWorld
{
	int32 PIEInstance = INDEX_NONE;
	UWorld* World = nullptr;
};

class SOFTUEBRIDGEEDITOR_API FBridgePIEWorlds
{
public:
	static bool IsValidContext(EWorldType::Type WorldType, const UWorld* World);
	static TArray<FBridgePIEWorld> Enumerate();
	static const FBridgePIEWorld* Resolve(const TArray<FBridgePIEWorld>& Worlds, int32 PIEInstance);
	static FString AvailableInstanceIds(const TArray<FBridgePIEWorld>& Worlds);
	/**
	 * The world for PIEInstance, or the first world when none is requested. Returns null and sets
	 * OutError when the instance is unknown (listing the valid IDs) or there is no PIE world.
	 */
	static const FBridgePIEWorld* Select(const TArray<FBridgePIEWorld>& Worlds, TOptional<int32> PIEInstance, FString& OutError);
	/**
	 * Reads the optional pie_instance argument: unset when absent, an error naming the value when it is
	 * not a non-negative whole number, rather than truncating 1.5 to 1.
	 */
	static TValueOrError<TOptional<int32>, FString> ReadInstanceArgument(const FJsonObject& Arguments);
	static FString NetModeName(ENetMode NetMode);
	static APlayerController* ResolveLocalPlayerController(UWorld* World, int32 PlayerIndex);
};
