// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FClothLODDataCommon;
struct FPointWeightMap;

inline constexpr const TCHAR* BridgeSourceSectionPrefix = TEXT("SoftUESourceSection_");

struct SOFTUEBRIDGEEDITOR_API FBridgeLegacyWeightMapTarget
{
	uint8 Id = uint8{};
	FString CliName;
	FName MapName = NAME_None;

	bool IsValid() const
	{
		return Id != uint8{} && !MapName.IsNone();
	}
};

SOFTUEBRIDGEEDITOR_API const TArray<FString>& GetBridgeLegacyWeightMapTargetNames();

SOFTUEBRIDGEEDITOR_API bool ResolveBridgeLegacyWeightMapTarget(
	const FString& CliName,
	FBridgeLegacyWeightMapTarget& OutTarget,
	FString& OutError);

SOFTUEBRIDGEEDITOR_API bool IsBridgeSourceSectionMap(const FName& MapName);

SOFTUEBRIDGEEDITOR_API void ConfigureBridgeLegacyWeightMapMetadata(
	FPointWeightMap& WeightMap,
	const FBridgeLegacyWeightMapTarget& Target);

SOFTUEBRIDGEEDITOR_API void ReadBridgeLegacyWeightMapValues(
	const FClothLODDataCommon& LodData,
	const FBridgeLegacyWeightMapTarget& Target,
	TArray<float>& OutValues);

SOFTUEBRIDGEEDITOR_API void ApplyBridgeLegacyWeightMapToLodData(
	FClothLODDataCommon& LodData,
	const TArray<float>& Values,
	const FBridgeLegacyWeightMapTarget& Target);

SOFTUEBRIDGEEDITOR_API void RecordBridgeSourceSectionMembership(
	TMap<int32, TArray<bool>>& Memberships,
	int32 Section,
	int32 VertexIndex,
	int32 VertexCount);

SOFTUEBRIDGEEDITOR_API void WriteBridgeSourceSectionMaps(
	FClothLODDataCommon& LodData,
	const TMap<int32, TArray<bool>>& Memberships);

SOFTUEBRIDGEEDITOR_API bool ReadBridgeSourceSectionSelection(
	const FClothLODDataCommon& LodData,
	const TArray<int32>& Sections,
	TArray<bool>& OutSelection,
	FString& OutError);

SOFTUEBRIDGEEDITOR_API bool ApplyBridgeLegacySectionSelection(
	const TArray<float>& ExistingValues,
	const TArray<float>& CandidateValues,
	const TArray<bool>& SectionSelection,
	const TArray<bool>& SpatialSelection,
	TArray<float>& OutValues,
	TArray<bool>& OutFinalSelection,
	FString& OutError);

SOFTUEBRIDGEEDITOR_API int32 CountBridgeSelectedMultiSectionVertices(
	const FClothLODDataCommon& LodData,
	const TArray<bool>& FinalSelection);
