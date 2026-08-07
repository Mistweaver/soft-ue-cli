// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/PIE/BridgePIEWorlds.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

bool FBridgePIEWorlds::IsValidContext(EWorldType::Type WorldType, const UWorld* World)
{
	return WorldType == EWorldType::PIE && World != nullptr;
}

TArray<FBridgePIEWorld> FBridgePIEWorlds::Enumerate()
{
	TArray<FBridgePIEWorld> Result;
	if (!GEngine)
	{
		return Result;
	}

	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.World();
		if (IsValidContext(Context.WorldType, World))
		{
			Result.Add({Context.PIEInstance, World});
		}
	}
	return Result;
}

const FBridgePIEWorld* FBridgePIEWorlds::Resolve(const TArray<FBridgePIEWorld>& Worlds, int32 PIEInstance)
{
	return Worlds.FindByPredicate([PIEInstance](const FBridgePIEWorld& Entry)
	{
		return Entry.PIEInstance == PIEInstance && Entry.World != nullptr;
	});
}

FString FBridgePIEWorlds::AvailableInstanceIds(const TArray<FBridgePIEWorld>& Worlds)
{
	TArray<int32> InstanceIds;
	for (const FBridgePIEWorld& Entry : Worlds)
	{
		if (Entry.World && !InstanceIds.Contains(Entry.PIEInstance))
		{
			InstanceIds.Add(Entry.PIEInstance);
		}
	}
	InstanceIds.Sort();

	TArray<FString> Values;
	for (int32 InstanceId : InstanceIds)
	{
		Values.Add(FString::FromInt(InstanceId));
	}
	return Values.Num() > 0 ? FString::Join(Values, TEXT(", ")) : TEXT("none");
}

FString FBridgePIEWorlds::NetModeName(ENetMode NetMode)
{
	switch (NetMode)
	{
	case NM_Standalone: return TEXT("standalone");
	case NM_DedicatedServer: return TEXT("dedicated-server");
	case NM_ListenServer: return TEXT("listen-server");
	case NM_Client: return TEXT("client");
	default: return TEXT("unknown");
	}
}

APlayerController* FBridgePIEWorlds::ResolveLocalPlayerController(UWorld* World, int32 PlayerIndex)
{
	if (!World || PlayerIndex < 0)
	{
		return nullptr;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance || GameInstance->GetWorld() != World)
	{
		return nullptr;
	}

	int32 ValidControllerIndex = 0;
	for (ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
	{
		APlayerController* PlayerController = LocalPlayer ? LocalPlayer->GetPlayerController(World) : nullptr;
		if (!PlayerController)
		{
			continue;
		}
		if (ValidControllerIndex == PlayerIndex)
		{
			return PlayerController;
		}
		++ValidControllerIndex;
	}
	return nullptr;
}
