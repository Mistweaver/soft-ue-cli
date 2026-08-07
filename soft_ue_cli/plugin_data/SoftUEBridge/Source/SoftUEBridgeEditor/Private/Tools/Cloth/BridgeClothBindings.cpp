// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Cloth/BridgeClothBindings.h"

#include "ClothingAsset.h"
#include "ClothingAssetBase.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"

namespace BridgeClothBindings
{
namespace
{
void AddClothBindingWarning(
	FBindingQueryResult& Result,
	int32 LodIndex,
	int32 SectionIndex,
	const FString& Reason,
	const FGuid* AssetGuid = nullptr)
{
	FBindingWarning& Warning = Result.Warnings.AddDefaulted_GetRef();
	Warning.LodIndex = LodIndex;
	Warning.SectionIndex = SectionIndex;
	Warning.Reason = Reason;
	if (AssetGuid && AssetGuid->IsValid())
	{
		Warning.AssetGuid = *AssetGuid;
	}
}

UClothingAssetBase* ResolveClothBindingAsset(
	TConstArrayView<UClothingAssetBase*> ClothingAssets,
	const FGuid& AssetGuid)
{
	for (UClothingAssetBase* Asset : ClothingAssets)
	{
		if (Asset && Asset->GetAssetGuid() == AssetGuid)
		{
			return Asset;
		}
	}
	return nullptr;
}
}

FString ValidateResolvedAssetLod(int32 AssetLodIndex, TOptional<int32> CommonAssetLodCount)
{
	if (AssetLodIndex < 0)
	{
		return TEXT("negative_asset_lod_index");
	}
	if (!CommonAssetLodCount.IsSet())
	{
		return TEXT("unsupported_clothing_asset_type");
	}
	if (AssetLodIndex >= CommonAssetLodCount.GetValue())
	{
		return TEXT("asset_lod_out_of_range");
	}
	return FString();
}

FBindingQueryResult Collect(const USkeletalMesh* Mesh, int32 LodFilter)
{
	if (!Mesh)
	{
		FBindingQueryResult Result;
		AddClothBindingWarning(Result, INDEX_NONE, INDEX_NONE, TEXT("missing_skeletal_mesh"));
		return Result;
	}

	return CollectImportedModel(Mesh->GetImportedModel(), Mesh->GetMeshClothingAssets(), LodFilter);
}

FBindingQueryResult CollectImportedModel(
	const FSkeletalMeshModel* ImportedModel,
	TConstArrayView<UClothingAssetBase*> ClothingAssets,
	int32 LodFilter)
{
	FBindingQueryResult Result;
	if (!ImportedModel)
	{
		AddClothBindingWarning(Result, INDEX_NONE, INDEX_NONE, TEXT("missing_imported_model"));
		return Result;
	}

	if (LodFilter != INDEX_NONE && !ImportedModel->LODModels.IsValidIndex(LodFilter))
	{
		AddClothBindingWarning(Result, LodFilter, INDEX_NONE, TEXT("lod_out_of_range"));
		return Result;
	}

	const int32 FirstLod = LodFilter == INDEX_NONE ? 0 : LodFilter;
	const int32 LastLod = LodFilter == INDEX_NONE ? ImportedModel->LODModels.Num() : LodFilter + 1;
	for (int32 LodIndex = FirstLod; LodIndex < LastLod; ++LodIndex)
	{
		const FSkeletalMeshLODModel& LodModel = ImportedModel->LODModels[LodIndex];
		for (int32 SectionIndex = 0; SectionIndex < LodModel.Sections.Num(); ++SectionIndex)
		{
			const FSkelMeshSection& Section = LodModel.Sections[SectionIndex];
			if (!Section.HasClothingData())
			{
				continue;
			}
			const FClothingSectionData& ClothingData = Section.ClothingData;

			if (!ClothingData.AssetGuid.IsValid())
			{
				AddClothBindingWarning(Result, LodIndex, SectionIndex, TEXT("invalid_asset_guid"));
				continue;
			}
			if (ClothingData.AssetLodIndex < 0)
			{
				AddClothBindingWarning(Result, LodIndex, SectionIndex, TEXT("negative_asset_lod_index"), &ClothingData.AssetGuid);
				continue;
			}

			UClothingAssetBase* Asset = ResolveClothBindingAsset(ClothingAssets, ClothingData.AssetGuid);
			if (!Asset)
			{
				AddClothBindingWarning(Result, LodIndex, SectionIndex, TEXT("unresolved_asset_guid"), &ClothingData.AssetGuid);
				continue;
			}
			TOptional<int32> CommonAssetLodCount;
			if (const UClothingAssetCommon* Common = Cast<UClothingAssetCommon>(Asset))
			{
				CommonAssetLodCount = Common->LodData.Num();
			}
			const FString AssetLodWarning =
				ValidateResolvedAssetLod(ClothingData.AssetLodIndex, CommonAssetLodCount);
			if (!AssetLodWarning.IsEmpty())
			{
				AddClothBindingWarning(Result, LodIndex, SectionIndex, AssetLodWarning, &ClothingData.AssetGuid);
				continue;
			}

			FBindingRecord& Binding = Result.Bindings.AddDefaulted_GetRef();
			Binding.Asset = Asset;
			Binding.LodIndex = LodIndex;
			Binding.SectionIndex = SectionIndex;
			Binding.AssetLodIndex = ClothingData.AssetLodIndex;
		}
	}
	return Result;
}
}
