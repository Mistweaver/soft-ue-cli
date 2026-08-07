// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Utils/BridgeLegacyClothWeightMaps.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ClothLODData.h"
#include "Misc/AutomationTest.h"
#include "PointWeightMap.h"

BEGIN_DEFINE_SPEC(
	FBridgeLegacyClothWeightMapsSpec,
	"SoftUEBridge.Cloth.LegacyWeightMaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
END_DEFINE_SPEC(FBridgeLegacyClothWeightMapsSpec)

void FBridgeLegacyClothWeightMapsSpec::Define()
{
	Describe("runtime target discovery", [this]()
	{
		It("resolves common and extended targets", [this]()
		{
			for (const FString& Name : GetBridgeLegacyWeightMapTargetNames())
			{
				FBridgeLegacyWeightMapTarget Target;
				FString Error;
				const bool bResolved = ResolveBridgeLegacyWeightMapTarget(Name, Target, Error);
				TestTrue(*FString::Printf(TEXT("%s resolves when available"), *Name), bResolved || Error.Contains(TEXT("unavailable")));
				if (bResolved)
				{
					TestTrue(TEXT("resolved target is valid"), Target.IsValid());
					TestEqual(TEXT("canonical CLI name round-trips"), Target.CliName, Name);
				}
			}
		});

		It("rejects unknown target names", [this]()
		{
			FBridgeLegacyWeightMapTarget Target;
			FString Error;
			TestFalse(TEXT("unknown fails"), ResolveBridgeLegacyWeightMapTarget(TEXT("not-a-map"), Target, Error));
			TestTrue(TEXT("unknown error is clear"), Error.Contains(TEXT("unknown")));
		});

		It("round-trips extended target values through read and apply helpers", [this]()
		{
			FBridgeLegacyWeightMapTarget Target;
			FString Error;
			if (!ResolveBridgeLegacyWeightMapTarget(TEXT("edge-stiffness"), Target, Error))
			{
				AddInfo(Error);
				return;
			}
			FClothLODDataCommon LodData;
			LodData.PhysicalMeshData.Vertices.SetNum(2);
			const TArray<float> AppliedValues = { 0.25f, 0.75f };
			ApplyBridgeLegacyWeightMapToLodData(LodData, AppliedValues, Target);
			TArray<float> ReadValues;
			ReadBridgeLegacyWeightMapValues(LodData, Target, ReadValues);

			const FPointWeightMap* PhysicalMap = LodData.PhysicalMeshData.FindWeightMap(Target.Id);
			TestNotNull(TEXT("extended physical map exists"), PhysicalMap);
			TestTrue(TEXT("read helper returns physical values"), ReadValues == AppliedValues);
			if (PhysicalMap)
			{
				TestTrue(TEXT("physical values round-trip"), PhysicalMap->Values == AppliedValues);
#if WITH_EDITORONLY_DATA
				TestEqual(TEXT("physical target ID round-trips"), PhysicalMap->CurrentTarget, Target.Id);
				TestEqual(TEXT("physical target name round-trips"), PhysicalMap->Name, Target.MapName);
				TestTrue(TEXT("physical target remains enabled"), PhysicalMap->bEnabled);
#endif
			}
#if WITH_EDITORONLY_DATA
			TestEqual(TEXT("one editor point map is written"), LodData.PointWeightMaps.Num(), 1);
			if (LodData.PointWeightMaps.Num() == 1)
			{
				const FPointWeightMap& PointMap = LodData.PointWeightMaps[0];
				TestEqual(TEXT("target metadata ID round-trips"), PointMap.CurrentTarget, Target.Id);
				TestEqual(TEXT("target metadata name round-trips"), PointMap.Name, Target.MapName);
				TestTrue(TEXT("target metadata remains enabled"), PointMap.bEnabled);
				TestTrue(TEXT("editor values round-trip"), PointMap.Values == AppliedValues);
			}
#endif
		});
	});

	Describe("source section provenance", [this]()
	{
		It("preserves welded dual section membership", [this]()
		{
			FClothLODDataCommon LodData;
			LodData.PhysicalMeshData.Vertices.SetNum(3);
			TMap<int32, TArray<bool>> Memberships;
			Memberships.Add(0, TArray<bool>{ true, true, false });
			Memberships.Add(1, TArray<bool>{ false, true, true });
			WriteBridgeSourceSectionMaps(LodData, Memberships);

			TArray<bool> Selection;
			FString Error;
			TestTrue(TEXT("union reads"), ReadBridgeSourceSectionSelection(LodData, { 0, 1 }, Selection, Error));
			TestTrue(TEXT("all vertices selected"), Selection[0] && Selection[1] && Selection[2]);
			TestEqual(TEXT("welded vertex belongs to both"), CountBridgeSelectedMultiSectionVertices(LodData, Selection), 1);

			TestTrue(TEXT("single-section selection reads"), ReadBridgeSourceSectionSelection(LodData, { 0 }, Selection, Error));
			TestEqual(TEXT("selected welded vertex still reports dual provenance"), CountBridgeSelectedMultiSectionVertices(LodData, Selection), 1);
			TestEqual(TEXT("final mask excluding welded vertex reports none"), CountBridgeSelectedMultiSectionVertices(LodData, { true, false, false }), 0);
			TestEqual(TEXT("final mask including welded vertex reports one"), CountBridgeSelectedMultiSectionVertices(LodData, { false, true, false }), 1);
#if WITH_EDITORONLY_DATA
			for (const FPointWeightMap& Map : LodData.PointWeightMaps)
			{
				TestFalse(TEXT("provenance is disabled"), Map.bEnabled);
			}
#endif
		});

		It("records dual membership through the merge helper", [this]()
		{
			TMap<int32, TArray<bool>> Memberships;
			RecordBridgeSourceSectionMembership(Memberships, 0, 0, 1);
			RecordBridgeSourceSectionMembership(Memberships, 1, 0, 1);
			FClothLODDataCommon LodData;
			LodData.PhysicalMeshData.Vertices.SetNum(1);
			WriteBridgeSourceSectionMaps(LodData, Memberships);
			TArray<bool> Selection;
			FString Error;
			TestTrue(TEXT("merged memberships read"), ReadBridgeSourceSectionSelection(LodData, { 0 }, Selection, Error));
			TestEqual(TEXT("same welded vertex retains both source sections"), CountBridgeSelectedMultiSectionVertices(LodData, Selection), 1);
		});

		It("rejects missing and unknown provenance without a selection", [this]()
		{
			FClothLODDataCommon LodData;
			LodData.PhysicalMeshData.Vertices.SetNum(2);
			TArray<bool> Selection;
			FString Error;
			TestFalse(TEXT("missing provenance fails"), ReadBridgeSourceSectionSelection(LodData, { 0 }, Selection, Error));
			TestTrue(TEXT("recreate guidance is present"), Error.Contains(TEXT("recreate")));

			TMap<int32, TArray<bool>> Memberships;
			Memberships.Add(0, TArray<bool>{ true, false });
			WriteBridgeSourceSectionMaps(LodData, Memberships);
			Error.Reset();
			TestFalse(TEXT("unknown section fails"), ReadBridgeSourceSectionSelection(LodData, { 9 }, Selection, Error));
			TestTrue(TEXT("unknown section is identified"), Error.Contains(TEXT("9")));
		});
	});

	Describe("section-restricted candidate application", [this]()
	{
		It("restricts constant, vertex-color, and bone-distance candidates", [this]()
		{
			const TArray<float> Existing = { 10.0f, 20.0f, 30.0f };
			const TArray<bool> Sections = { true, false, true };
			const TArray<TArray<float>> RuleCandidates = {
				TArray<float>{ 1.0f, 1.0f, 1.0f },
				TArray<float>{ 0.1f, 0.5f, 0.9f },
				TArray<float>{ 0.0f, 40.0f, 80.0f },
			};
			for (const TArray<float>& Candidate : RuleCandidates)
			{
				TArray<float> Values;
				TArray<bool> FinalSelection;
				FString Error;
				TestTrue(TEXT("rule selection succeeds"), ApplyBridgeLegacySectionSelection(Existing, Candidate, Sections, {}, Values, FinalSelection, Error));
				TestEqual(TEXT("selected first value uses candidate"), Values[0], Candidate[0]);
				TestEqual(TEXT("unselected value is preserved"), Values[1], Existing[1]);
				TestEqual(TEXT("selected last value uses candidate"), Values[2], Candidate[2]);
			}
		});

		It("intersects spatial and section selection", [this]()
		{
			TArray<float> Values;
			TArray<bool> FinalSelection;
			FString Error;
			TestTrue(TEXT("intersection succeeds"), ApplyBridgeLegacySectionSelection(
				{ 10.0f, 20.0f, 30.0f, 40.0f },
				{ 1.0f, 2.0f, 3.0f, 4.0f },
				{ true, true, false, true },
				{ false, true, true, false },
				Values,
				FinalSelection,
				Error));
			const TArray<float> ExpectedValues = { 10.0f, 2.0f, 30.0f, 40.0f };
			TestTrue(TEXT("only intersection changes"), Values == ExpectedValues);
			TestTrue(TEXT("intersection vertex selected"), FinalSelection[1]);
			TestFalse(TEXT("section-only vertex excluded"), FinalSelection[0]);
			TestFalse(TEXT("spatial-only vertex excluded"), FinalSelection[2]);
		});

		It("rejects an empty combined selection", [this]()
		{
			TArray<float> Values;
			TArray<bool> FinalSelection;
			FString Error;
			TestFalse(TEXT("empty intersection fails"), ApplyBridgeLegacySectionSelection(
				{ 10.0f, 20.0f }, { 1.0f, 2.0f }, { true, false }, { false, true }, Values, FinalSelection, Error));
			TestTrue(TEXT("empty intersection error is clear"), Error.Contains(TEXT("did not match")));
		});
	});
}

#endif
