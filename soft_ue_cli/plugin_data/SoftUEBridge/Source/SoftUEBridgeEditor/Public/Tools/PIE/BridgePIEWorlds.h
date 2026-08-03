// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"

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
	static FString NetModeName(ENetMode NetMode);
	static APlayerController* ResolveLocalPlayerController(UWorld* World, int32 PlayerIndex);
};
