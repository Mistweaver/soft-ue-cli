// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/PIE/BridgePIEWorlds.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgePIEWorldsSpec,
	"SoftUEBridge.PIE.WorldDiscovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBridgePIEWorldsSpec::RunTest(const FString& Parameters)
{
	// filters invalid PIE contexts
	TestFalse(TEXT("editor contexts are excluded"), FBridgePIEWorlds::IsValidContext(EWorldType::Editor, nullptr));
	TestFalse(TEXT("null PIE worlds are excluded"), FBridgePIEWorlds::IsValidContext(EWorldType::PIE, nullptr));
	UWorld* WorldA = NewObject<UWorld>();
	UWorld* WorldB = NewObject<UWorld>();
	TestTrue(TEXT("valid PIE worlds are included"), FBridgePIEWorlds::IsValidContext(EWorldType::PIE, WorldA));

	// resolves instances and reports available IDs; duplicate IDs resolve in stable input order
	TArray<FBridgePIEWorld> Worlds = {{2, WorldA}, {1, WorldB}, {2, WorldB}};
	const FBridgePIEWorld* Resolved = FBridgePIEWorlds::Resolve(Worlds, 2);
	TestTrue(TEXT("instance found"), Resolved != nullptr);
	TestEqual(TEXT("first duplicate wins"), Resolved ? Resolved->World : nullptr, WorldA);
	TestTrue(TEXT("unknown instance is absent"), FBridgePIEWorlds::Resolve(Worlds, 9) == nullptr);
	TestEqual(TEXT("available IDs are unique and sorted"), FBridgePIEWorlds::AvailableInstanceIds(Worlds), FString(TEXT("1, 2")));

	// returns stable net mode names
	TestEqual(TEXT("standalone"), FBridgePIEWorlds::NetModeName(NM_Standalone), FString(TEXT("standalone")));
	TestEqual(TEXT("dedicated server"), FBridgePIEWorlds::NetModeName(NM_DedicatedServer), FString(TEXT("dedicated-server")));
	TestEqual(TEXT("listen server"), FBridgePIEWorlds::NetModeName(NM_ListenServer), FString(TEXT("listen-server")));
	TestEqual(TEXT("client"), FBridgePIEWorlds::NetModeName(NM_Client), FString(TEXT("client")));

	// resolves only valid local player controllers
	TestTrue(TEXT("null world has no local controllers"), FBridgePIEWorlds::ResolveLocalPlayerController(nullptr, 0) == nullptr);
	TestTrue(TEXT("negative local index is rejected"), FBridgePIEWorlds::ResolveLocalPlayerController(WorldA, -1) == nullptr);
	TestTrue(TEXT("world without a game instance is safe"), FBridgePIEWorlds::ResolveLocalPlayerController(WorldA, 0) == nullptr);
	return true;
}

#endif
