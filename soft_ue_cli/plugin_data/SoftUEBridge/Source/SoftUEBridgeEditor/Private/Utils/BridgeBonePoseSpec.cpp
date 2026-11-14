// Copyright soft-ue-expert. All Rights Reserved.

#include "Utils/BridgeBonePose.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeBonePoseSpec,
	"SoftUEBridge.Animation.BonePose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBridgeBonePoseSpec::RunTest(const FString& Parameters)
{
	using namespace SoftUE::BonePose;

	const float Tolerance = 0.001f;
	const double NumberTolerance = 0.001;

	// Parent indices are passed as a named array: a one-element brace list also converts to FReferenceSkeleton.
	using FParents = TArray<int32>;

	auto Numbers = [](const TArray<TSharedPtr<FJsonValue>>& Values)
	{
		TArray<double> Out;
		for (const TSharedPtr<FJsonValue>& Value : Values)
		{
			Out.Add(Value->AsNumber());
		}
		return Out;
	};

	// ComponentSpaceFromLocal: a three-bone chain composes child-to-parent
	{
		// root at (0,0,10) turned 90 degrees in yaw; child 5 along local X; grandchild 3 along local X.
		const FTransform Root(FRotator(0.0, 90.0, 0.0), FVector(0.0, 0.0, 10.0));
		const FTransform Child(FRotator::ZeroRotator, FVector(5.0, 0.0, 0.0));
		const FTransform Grandchild(FRotator::ZeroRotator, FVector(3.0, 0.0, 0.0));

		const TArray<FTransform> Component = ComponentSpaceFromLocal(FParents{INDEX_NONE, 0, 1}, {Root, Child, Grandchild});
		TestEqual(TEXT("chain bone count"), Component.Num(), 3);
		if (Component.Num() == 3)
		{
			TestEqual(TEXT("root unchanged"), Component[0].GetLocation(), FVector(0.0, 0.0, 10.0), Tolerance);
			// Root yaw 90 turns local +X into component +Y.
			TestEqual(TEXT("child follows root rotation"), Component[1].GetLocation(), FVector(0.0, 5.0, 10.0), Tolerance);
			TestEqual(TEXT("grandchild accumulates"), Component[2].GetLocation(), FVector(0.0, 8.0, 10.0), Tolerance);
			TestTrue(TEXT("grandchild inherits yaw"), Component[2].GetRotation().Equals(Root.GetRotation(), Tolerance));
		}
	}

	// ComponentSpaceFromLocal: boundaries
	{
		const FTransform Only(FRotator(10.0, 20.0, 30.0), FVector(1.0, 2.0, 3.0));
		const TArray<FTransform> Single = ComponentSpaceFromLocal(FParents{INDEX_NONE}, {Only});
		TestTrue(TEXT("root-only skeleton keeps its local transform"), Single.Num() == 1 && Single[0].Equals(Only, Tolerance));

		TestEqual(TEXT("empty pose gives empty result"), ComponentSpaceFromLocal(TArray<int32>(), TArray<FTransform>()).Num(), 0);
		TestEqual(TEXT("count mismatch gives empty result"), ComponentSpaceFromLocal(FParents{INDEX_NONE, 0}, {Only}).Num(), 0);
		TestEqual(TEXT("parent after child gives empty result"), ComponentSpaceFromLocal(FParents{1, INDEX_NONE}, {Only, Only}).Num(), 0);
		TestEqual(TEXT("self parent gives empty result"), ComponentSpaceFromLocal(FParents{INDEX_NONE, 1}, {Only, Only}).Num(), 0);

		// Two roots are allowed; each keeps its own transform.
		const TArray<FTransform> TwoRoots = ComponentSpaceFromLocal(FParents{INDEX_NONE, INDEX_NONE}, {Only, FTransform::Identity});
		TestTrue(TEXT("second root unaffected by first"), TwoRoots.Num() == 2 && TwoRoots[1].Equals(FTransform::Identity, Tolerance));
	}

	// SetTransformFields: location, rotation, quat, scale with quat in x, y, z, w order
	{
		const FTransform Transform(FRotator(0.0, 90.0, 0.0), FVector(1.0, 2.0, 3.0), FVector(1.0, 1.0, 2.0));
		const TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		SetTransformFields(Json, Transform);

		TestTrue(TEXT("location"), Numbers(Json->GetArrayField(TEXT("location"))) == TArray<double>({1.0, 2.0, 3.0}));
		TestTrue(TEXT("scale"), Numbers(Json->GetArrayField(TEXT("scale"))) == TArray<double>({1.0, 1.0, 2.0}));

		const TArray<double> Rotation = Numbers(Json->GetArrayField(TEXT("rotation")));
		TestTrue(TEXT("rotation has pitch, yaw, roll"), Rotation.Num() == 3);
		if (Rotation.Num() == 3)
		{
			TestEqual(TEXT("rotation pitch"), Rotation[0], 0.0, NumberTolerance);
			TestEqual(TEXT("rotation yaw"), Rotation[1], 90.0, NumberTolerance);
			TestEqual(TEXT("rotation roll"), Rotation[2], 0.0, NumberTolerance);
		}

		// Yaw 90 about +Z is (0, 0, sin 45, cos 45).
		const TArray<double> Quat = Numbers(Json->GetArrayField(TEXT("quat")));
		TestTrue(TEXT("quat has four components"), Quat.Num() == 4);
		if (Quat.Num() == 4)
		{
			const double Half = FMath::Sqrt(0.5);
			TestEqual(TEXT("quat x"), Quat[0], 0.0, NumberTolerance);
			TestEqual(TEXT("quat y"), Quat[1], 0.0, NumberTolerance);
			TestEqual(TEXT("quat z"), Quat[2], Half, NumberTolerance);
			TestEqual(TEXT("quat w last"), Quat[3], Half, NumberTolerance);
		}

		TestTrue(TEXT("VectorToJson is x, y, z"), Numbers(VectorToJson(FVector(4.0, 5.0, 6.0))) == TArray<double>({4.0, 5.0, 6.0}));
	}

	// ParseBoneFilter: array, comma string, trimming, empties, duplicates
	{
		const TSharedPtr<FJsonObject> Missing = MakeShared<FJsonObject>();
		TestEqual(TEXT("missing field means every bone"), ParseBoneFilter(Missing).Num(), 0);
		TestEqual(TEXT("null arguments means every bone"), ParseBoneFilter(nullptr).Num(), 0);

		const TSharedPtr<FJsonObject> FromString = MakeShared<FJsonObject>();
		FromString->SetStringField(TEXT("bones"), TEXT(" thigh_l, hand_l,,thigh_l "));
		TestTrue(TEXT("comma string trimmed, deduplicated, ordered"),
			ParseBoneFilter(FromString) == TArray<FName>({FName(TEXT("thigh_l")), FName(TEXT("hand_l"))}));

		const TSharedPtr<FJsonObject> FromArray = MakeShared<FJsonObject>();
		FromArray->SetArrayField(TEXT("bones"), {
			MakeShared<FJsonValueString>(TEXT("pelvis")),
			MakeShared<FJsonValueString>(TEXT(" ")),
			MakeShared<FJsonValueNumber>(3.0),
			MakeShared<FJsonValueString>(TEXT(" spine_01 "))});
		TestTrue(TEXT("array skips blanks and non-strings"),
			ParseBoneFilter(FromArray) == TArray<FName>({FName(TEXT("pelvis")), FName(TEXT("spine_01"))}));

		const TSharedPtr<FJsonObject> OtherField = MakeShared<FJsonObject>();
		OtherField->SetStringField(TEXT("slots"), TEXT("a"));
		TestTrue(TEXT("custom field name"), ParseBoneFilter(OtherField, TEXT("slots")) == TArray<FName>({FName(TEXT("a"))}));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
