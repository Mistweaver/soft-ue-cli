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

	// selects the requested instance, or the first world when none is requested
	FString SelectError;
	const FBridgePIEWorld* Requested = FBridgePIEWorlds::Select(Worlds, 1, SelectError);
	TestEqual(TEXT("requested instance is selected"), Requested ? Requested->World : nullptr, WorldB);
	const FBridgePIEWorld* Default = FBridgePIEWorlds::Select(Worlds, {}, SelectError);
	TestEqual(TEXT("no request selects the first world"), Default ? Default->World : nullptr, WorldA);
	TestTrue(TEXT("unknown requested instance fails"), FBridgePIEWorlds::Select(Worlds, 9, SelectError) == nullptr);
	TestEqual(TEXT("unknown instance error lists the valid IDs"), SelectError,
		FString(TEXT("PIE instance 9 not found; available PIE instances: 1, 2")));
	TestTrue(TEXT("no worlds fails"), FBridgePIEWorlds::Select({}, {}, SelectError) == nullptr);
	TestEqual(TEXT("no worlds error says to start PIE"), SelectError, FString(TEXT("no PIE world available; start PIE first")));

	// reads pie_instance as an optional non-negative whole number
	auto ReadInstance = [](TFunctionRef<void(FJsonObject&)> Fill)
	{
		FJsonObject Arguments;
		Fill(Arguments);
		return FBridgePIEWorlds::ReadInstanceArgument(Arguments);
	};
	const auto Absent = ReadInstance([](FJsonObject&) {});
	TestTrue(TEXT("absent pie_instance is unset"), Absent.HasValue() && !Absent.GetValue().IsSet());
	const auto Two = ReadInstance([](FJsonObject& A) { A.SetNumberField(TEXT("pie_instance"), 2.0); });
	TestTrue(TEXT("whole pie_instance is read"), Two.HasValue() && Two.GetValue() == TOptional<int32>(2));
	const auto Zero = ReadInstance([](FJsonObject& A) { A.SetNumberField(TEXT("pie_instance"), 0.0); });
	TestTrue(TEXT("zero is a valid instance"), Zero.HasValue() && Zero.GetValue() == TOptional<int32>(0));
	const auto Fraction = ReadInstance([](FJsonObject& A) { A.SetNumberField(TEXT("pie_instance"), 1.5); });
	TestTrue(TEXT("fractional pie_instance is refused, not truncated"),
		Fraction.HasError() && Fraction.GetError().Contains(TEXT("non-negative integer (got 1.5)")));
	const auto Negative = ReadInstance([](FJsonObject& A) { A.SetNumberField(TEXT("pie_instance"), -1.0); });
	TestTrue(TEXT("negative pie_instance is refused"), Negative.HasError());
	const auto Huge = ReadInstance([](FJsonObject& A) { A.SetNumberField(TEXT("pie_instance"), 1e12); });
	TestTrue(TEXT("pie_instance past int32 is refused"), Huge.HasError());

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
