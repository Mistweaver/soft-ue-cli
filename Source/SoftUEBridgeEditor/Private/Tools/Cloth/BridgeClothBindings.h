// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FSkeletalMeshModel;
class UClothingAssetBase;
class USkeletalMesh;

namespace BridgeClothBindings
{
struct FBindingRecord
{
	UClothingAssetBase* Asset = nullptr;
	int32 LodIndex = INDEX_NONE;
	int32 SectionIndex = INDEX_NONE;
	int32 AssetLodIndex = INDEX_NONE;
};

struct FBindingWarning
{
	int32 LodIndex = INDEX_NONE;
	int32 SectionIndex = INDEX_NONE;
	TOptional<FGuid> AssetGuid;
	FString Reason;
};

struct FBindingQueryResult
{
	TArray<FBindingRecord> Bindings;
	TArray<FBindingWarning> Warnings;
};

/** Validate a non-negative referenced LOD after resolving its clothing asset. */
FString ValidateResolvedAssetLod(int32 AssetLodIndex, TOptional<int32> CommonAssetLodCount);

/** Enumerate validated section bindings directly from a skeletal mesh's imported LOD models. */
FBindingQueryResult Collect(const USkeletalMesh* Mesh, int32 LodFilter = INDEX_NONE);

/** Read-only core used when callers already have an imported model and clothing asset view. */
FBindingQueryResult CollectImportedModel(
	const FSkeletalMeshModel* ImportedModel,
	TConstArrayView<UClothingAssetBase*> ClothingAssets,
	int32 LodFilter = INDEX_NONE);
}
