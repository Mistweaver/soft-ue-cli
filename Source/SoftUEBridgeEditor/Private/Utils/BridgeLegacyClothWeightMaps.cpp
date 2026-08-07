// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Utils/BridgeLegacyClothWeightMaps.h"

#include "ChaosCloth/ChaosClothingSimulationFactory.h"
#include "ClothLODData.h"
#include "PointWeightMap.h"
#include "UObject/UnrealType.h"

namespace
{
FString EnumNameToCliName(const FString& Name)
{
	FString Result;
	for (int32 Index = 0; Index < Name.Len(); ++Index)
	{
		const TCHAR Character = Name[Index];
		if (Character == TEXT('_') || Character == TEXT(' '))
		{
			if (!Result.IsEmpty() && !Result.EndsWith(TEXT("-")))
			{
				Result.AppendChar(TEXT('-'));
			}
			continue;
		}
		if (FChar::IsUpper(Character) && Index > 0 && !Result.EndsWith(TEXT("-")))
		{
			Result.AppendChar(TEXT('-'));
		}
		Result.AppendChar(FChar::ToLower(Character));
	}
	return Result;
}

FName SourceSectionMapName(int32 SectionIndex)
{
	return FName(*FString::Printf(TEXT("%s%d"), BridgeSourceSectionPrefix, SectionIndex));
}

bool TryParseSourceSection(const FName& Name, int32& OutSection)
{
	const FString Text = Name.ToString();
	if (!Text.StartsWith(BridgeSourceSectionPrefix, ESearchCase::CaseSensitive))
	{
		return false;
	}
	const FString Suffix = Text.RightChop(FCString::Strlen(BridgeSourceSectionPrefix));
	if (Suffix.IsEmpty())
	{
		return false;
	}
	for (TCHAR Character : Suffix)
	{
		if (!FChar::IsDigit(Character))
		{
			return false;
		}
	}
	OutSection = FCString::Atoi(*Suffix);
	return true;
}
}

const TArray<FString>& GetBridgeLegacyWeightMapTargetNames()
{
	static const TArray<FString> Names = {
		TEXT("max-distance"), TEXT("anim-drive-stiffness"), TEXT("anim-drive-damping"),
		TEXT("backstop-distance"), TEXT("backstop-radius"), TEXT("tether-ends-mask"),
		TEXT("tether-stiffness"), TEXT("tether-scale"), TEXT("drag"), TEXT("lift"),
		TEXT("edge-stiffness"), TEXT("bending-stiffness"), TEXT("area-stiffness"),
		TEXT("buckling-stiffness"), TEXT("pressure"), TEXT("flatness-ratio"),
		TEXT("outer-drag"), TEXT("outer-lift")
	};
	return Names;
}

bool ResolveBridgeLegacyWeightMapTarget(
	const FString& CliName,
	FBridgeLegacyWeightMapTarget& OutTarget,
	FString& OutError)
{
	OutTarget = FBridgeLegacyWeightMapTarget();
	OutError.Reset();

	const FString Requested = CliName.ToLower();
	if (!GetBridgeLegacyWeightMapTargetNames().Contains(Requested))
	{
		OutError = FString::Printf(TEXT("cloth: unknown weight-map target '%s'"), *CliName);
		return false;
	}

	const UChaosClothingSimulationFactory* Factory = GetDefault<UChaosClothingSimulationFactory>();
	const UEnum* Enum = Factory ? Factory->GetWeightMapTargetEnum() : nullptr;
	if (!Enum)
	{
		OutError = TEXT("cloth: the running Chaos cloth implementation does not expose weight-map targets");
		return false;
	}

	for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
	{
		if (Enum->HasMetaData(TEXT("Hidden"), Index))
		{
			continue;
		}
		const FString EnumName = Enum->GetNameStringByIndex(Index);
		if (EnumNameToCliName(EnumName).Equals(Requested, ESearchCase::CaseSensitive))
		{
			const int64 Value = Enum->GetValueByIndex(Index);
			if (Value < 0 || Value > MAX_uint8)
			{
				break;
			}
			OutTarget.Id = static_cast<uint8>(Value);
			OutTarget.CliName = Requested;
			OutTarget.MapName = FName(*EnumName);
			return OutTarget.IsValid();
		}
	}

	OutError = FString::Printf(
		TEXT("cloth: weight-map target '%s' is unavailable in this Unreal Engine/Chaos version"),
		*Requested);
	return false;
}

bool IsBridgeSourceSectionMap(const FName& MapName)
{
	int32 IgnoredSection = INDEX_NONE;
	return TryParseSourceSection(MapName, IgnoredSection);
}

void ConfigureBridgeLegacyWeightMapMetadata(
	FPointWeightMap& WeightMap,
	const FBridgeLegacyWeightMapTarget& Target)
{
#if WITH_EDITORONLY_DATA
	WeightMap.Name = Target.MapName;
	WeightMap.CurrentTarget = Target.Id;
	WeightMap.bEnabled = true;
#endif
}

void ReadBridgeLegacyWeightMapValues(
	const FClothLODDataCommon& LodData,
	const FBridgeLegacyWeightMapTarget& Target,
	TArray<float>& OutValues)
{
	const FClothPhysicalMeshData& PhysicalMesh = LodData.PhysicalMeshData;
	const int32 VertexCount = PhysicalMesh.Vertices.Num();
	OutValues.Init(0.0f, VertexCount);
#if WITH_EDITORONLY_DATA
	bool bFoundPointWeightMap = false;
	for (const FPointWeightMap& PointWeightMap : LodData.PointWeightMaps)
	{
		if (PointWeightMap.bEnabled && PointWeightMap.CurrentTarget == Target.Id)
		{
			bFoundPointWeightMap = true;
			for (int32 Index = 0; Index < VertexCount; ++Index)
			{
				if (PointWeightMap.Values.IsValidIndex(Index))
				{
					OutValues[Index] = PointWeightMap.Values[Index];
				}
			}
		}
	}
	if (bFoundPointWeightMap)
	{
		return;
	}
#endif
	const FPointWeightMap* ExistingWeightMap = PhysicalMesh.FindWeightMap(Target.Id);
	if (!ExistingWeightMap)
	{
		return;
	}
	for (int32 Index = 0; Index < VertexCount; ++Index)
	{
		if (ExistingWeightMap->Values.IsValidIndex(Index))
		{
			OutValues[Index] = ExistingWeightMap->Values[Index];
		}
	}
}

void ApplyBridgeLegacyWeightMapToLodData(
	FClothLODDataCommon& LodData,
	const TArray<float>& Values,
	const FBridgeLegacyWeightMapTarget& Target)
{
	FPointWeightMap& PhysicalWeightMap = LodData.PhysicalMeshData.FindOrAddWeightMap(Target.Id);
	PhysicalWeightMap.Values = Values;
	ConfigureBridgeLegacyWeightMapMetadata(PhysicalWeightMap, Target);

#if WITH_EDITORONLY_DATA
	for (int32 Index = LodData.PointWeightMaps.Num() - 1; Index >= 0; --Index)
	{
		if (LodData.PointWeightMaps[Index].CurrentTarget == Target.Id
			&& !IsBridgeSourceSectionMap(LodData.PointWeightMaps[Index].Name))
		{
			LodData.PointWeightMaps.RemoveAt(Index);
		}
	}
	FPointWeightMap& PointWeightMap = LodData.PointWeightMaps.AddDefaulted_GetRef();
	PointWeightMap.Values = Values;
	ConfigureBridgeLegacyWeightMapMetadata(PointWeightMap, Target);
#endif
	LodData.PushWeightsToMesh();
	if (FPointWeightMap* AppliedPhysicalMap = LodData.PhysicalMeshData.FindWeightMap(Target.Id))
	{
		ConfigureBridgeLegacyWeightMapMetadata(*AppliedPhysicalMap, Target);
	}
}

void RecordBridgeSourceSectionMembership(
	TMap<int32, TArray<bool>>& Memberships,
	int32 Section,
	int32 VertexIndex,
	int32 VertexCount)
{
	for (TPair<int32, TArray<bool>>& Membership : Memberships)
	{
		Membership.Value.SetNum(VertexCount);
	}
	TArray<bool>& SectionMembership = Memberships.FindOrAdd(Section);
	SectionMembership.SetNum(VertexCount);
	if (SectionMembership.IsValidIndex(VertexIndex))
	{
		SectionMembership[VertexIndex] = true;
	}
}

void WriteBridgeSourceSectionMaps(
	FClothLODDataCommon& LodData,
	const TMap<int32, TArray<bool>>& Memberships)
{
#if WITH_EDITORONLY_DATA
	LodData.PointWeightMaps.RemoveAll([](const FPointWeightMap& Map)
	{
		return IsBridgeSourceSectionMap(Map.Name);
	});

	TArray<int32> Sections;
	Memberships.GetKeys(Sections);
	Sections.Sort();
	for (int32 Section : Sections)
	{
		const TArray<bool>& Membership = Memberships.FindChecked(Section);
		FPointWeightMap& Map = LodData.PointWeightMaps.AddDefaulted_GetRef();
		Map.Values.SetNum(Membership.Num());
		for (int32 Index = 0; Index < Membership.Num(); ++Index)
		{
			Map.Values[Index] = Membership[Index] ? 1.0f : 0.0f;
		}
		Map.Name = SourceSectionMapName(Section);
		Map.CurrentTarget = static_cast<uint8>(EWeightMapTargetCommon::None);
		Map.bEnabled = false;
	}
#endif
}

bool ReadBridgeSourceSectionSelection(
	const FClothLODDataCommon& LodData,
	const TArray<int32>& Sections,
	TArray<bool>& OutSelection,
	FString& OutError)
{
	OutError.Reset();
	const int32 VertexCount = LodData.PhysicalMeshData.Vertices.Num();
	OutSelection.Init(false, VertexCount);
	if (Sections.IsEmpty())
	{
		OutError = TEXT("cloth: section_indices must contain at least one section");
		return false;
	}

#if WITH_EDITORONLY_DATA
	TMap<int32, const FPointWeightMap*> AvailableMaps;
	for (const FPointWeightMap& Map : LodData.PointWeightMaps)
	{
		int32 Section = INDEX_NONE;
		if (TryParseSourceSection(Map.Name, Section))
		{
			if (Map.Values.Num() != VertexCount)
			{
				OutError = FString::Printf(TEXT("cloth: source-section provenance map %s has %d values for %d vertices"), *Map.Name.ToString(), Map.Values.Num(), VertexCount);
				return false;
			}
			AvailableMaps.Add(Section, &Map);
		}
	}
	if (AvailableMaps.IsEmpty())
	{
		OutError = TEXT("cloth: source-section provenance is missing; recreate the merged cloth with the current SoftUEBridge plugin");
		return false;
	}

	TArray<const FPointWeightMap*> RequestedMaps;
	for (int32 Section : Sections)
	{
		const FPointWeightMap* const* Map = AvailableMaps.Find(Section);
		if (!Map)
		{
			OutError = FString::Printf(TEXT("cloth: source section %d is not present in this merged cloth"), Section);
			return false;
		}
		RequestedMaps.Add(*Map);
	}

	int32 SelectedCount = 0;
	for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		bool bRequestedMembership = false;
		for (const FPointWeightMap* Map : RequestedMaps)
		{
			if (Map->Values[VertexIndex] > 0.5f)
			{
				bRequestedMembership = true;
				break;
			}
		}
		OutSelection[VertexIndex] = bRequestedMembership;
		SelectedCount += OutSelection[VertexIndex] ? 1 : 0;
	}
	if (SelectedCount == 0)
	{
		OutError = TEXT("cloth: selected source sections do not contain any physical mesh vertices");
		return false;
	}
	return true;
#else
	OutError = TEXT("cloth: source-section provenance requires editor-only cloth data");
	return false;
#endif
}

bool ApplyBridgeLegacySectionSelection(
	const TArray<float>& ExistingValues,
	const TArray<float>& CandidateValues,
	const TArray<bool>& SectionSelection,
	const TArray<bool>& SpatialSelection,
	TArray<float>& OutValues,
	TArray<bool>& OutFinalSelection,
	FString& OutError)
{
	OutError.Reset();
	const int32 VertexCount = ExistingValues.Num();
	if (CandidateValues.Num() != VertexCount || SectionSelection.Num() != VertexCount
		|| (!SpatialSelection.IsEmpty() && SpatialSelection.Num() != VertexCount))
	{
		OutError = TEXT("cloth: section selection arrays do not match the physical mesh vertex count");
		return false;
	}

	const TArray<float> CandidateCopy = CandidateValues;
	OutValues = ExistingValues;
	OutFinalSelection.Init(false, VertexCount);
	int32 SelectedCount = 0;
	for (int32 Index = 0; Index < VertexCount; ++Index)
	{
		const bool bSelected = SectionSelection[Index]
			&& (SpatialSelection.IsEmpty() || SpatialSelection[Index]);
		OutFinalSelection[Index] = bSelected;
		if (bSelected)
		{
			OutValues[Index] = CandidateCopy[Index];
			++SelectedCount;
		}
	}
	if (SelectedCount == 0)
	{
		OutError = TEXT("cloth: section and spatial selection did not match any physical mesh vertices");
		return false;
	}
	return true;
}

int32 CountBridgeSelectedMultiSectionVertices(
	const FClothLODDataCommon& LodData,
	const TArray<bool>& FinalSelection)
{
	int32 MultiSectionCount = 0;
#if WITH_EDITORONLY_DATA
	for (int32 VertexIndex = 0; VertexIndex < FinalSelection.Num(); ++VertexIndex)
	{
		if (!FinalSelection[VertexIndex])
		{
			continue;
		}
		int32 MembershipCount = 0;
		for (const FPointWeightMap& Map : LodData.PointWeightMaps)
		{
			if (IsBridgeSourceSectionMap(Map.Name)
				&& Map.Values.IsValidIndex(VertexIndex)
				&& Map.Values[VertexIndex] > 0.5f)
			{
				++MembershipCount;
			}
		}
		MultiSectionCount += MembershipCount > 1 ? 1 : 0;
	}
#endif
	return MultiSectionCount;
}
