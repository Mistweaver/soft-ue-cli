// Copyright soft-ue-expert. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tools/Cloth/BridgeClothBindings.h"

#include "ClothLODData.h"
#include "ClothingAsset.h"
#include "ClothingAssetBase.h"
#include "Misc/AutomationTest.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

BEGIN_DEFINE_SPEC(
	FBridgeClothBindingsSpec,
	"SoftUEBridge.Cloth.SafeBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	UClothingAssetCommon* MakeAsset(const FGuid& Guid, int32 LodCount);
	FSkeletalMeshModel MakeModel(int32 LodCount);
	void SetBinding(
		FSkeletalMeshModel& Model,
		int32 LodIndex,
		int32 SectionIndex,
		const FGuid& Guid,
		int32 AssetLodIndex,
		bool bAddClothMapping = true);

END_DEFINE_SPEC(FBridgeClothBindingsSpec)

UClothingAssetCommon* FBridgeClothBindingsSpec::MakeAsset(const FGuid& Guid, int32 LodCount)
{
	UClothingAssetCommon* Asset = NewObject<UClothingAssetCommon>(GetTransientPackage());
	FStructProperty* GuidProperty = FindFProperty<FStructProperty>(UClothingAssetBase::StaticClass(), TEXT("AssetGuid"));
	if (!GuidProperty)
	{
		AddError(TEXT("UClothingAssetBase.AssetGuid reflection property is unavailable"));
		return Asset;
	}
	*GuidProperty->ContainerPtrToValuePtr<FGuid>(Asset) = Guid;
	Asset->LodData.SetNum(LodCount);
	return Asset;
}

FSkeletalMeshModel FBridgeClothBindingsSpec::MakeModel(int32 LodCount)
{
	FSkeletalMeshModel Model;
	for (int32 LodIndex = 0; LodIndex < LodCount; ++LodIndex)
	{
		FSkeletalMeshLODModel* LodModel = new FSkeletalMeshLODModel();
		LodModel->Sections.AddDefaulted();
		Model.LODModels.Add(LodModel);
	}
	return Model;
}

void FBridgeClothBindingsSpec::SetBinding(
	FSkeletalMeshModel& Model,
	int32 LodIndex,
	int32 SectionIndex,
	const FGuid& Guid,
	int32 AssetLodIndex,
	bool bAddClothMapping)
{
	FSkelMeshSection& Section = Model.LODModels[LodIndex].Sections[SectionIndex];
	FClothingSectionData& ClothingData = Section.ClothingData;
	ClothingData.AssetGuid = Guid;
	ClothingData.AssetLodIndex = AssetLodIndex;
	if (bAddClothMapping)
	{
		Section.ClothMappingDataLODs.AddDefaulted();
		Section.ClothMappingDataLODs[0].AddDefaulted();
	}
}

void FBridgeClothBindingsSpec::Define()
{
	It("rejects a resolved non-common clothing asset type", [this]()
	{
		const FString Warning = BridgeClothBindings::ValidateResolvedAssetLod(0, TOptional<int32>());

		TestEqual(TEXT("stable unsupported type warning"), Warning, FString(TEXT("unsupported_clothing_asset_type")));
	});

	It("returns a validated binding record", [this]()
	{
		const FGuid Guid = FGuid::NewGuid();
		UClothingAssetCommon* Asset = MakeAsset(Guid, 1);
		FSkeletalMeshModel Model = MakeModel(1);
		SetBinding(Model, 0, 0, Guid, 0);
		TArray<UClothingAssetBase*> Assets = { Asset };

		const BridgeClothBindings::FBindingQueryResult Result =
			BridgeClothBindings::CollectImportedModel(&Model, Assets);

		TestEqual(TEXT("one valid binding"), Result.Bindings.Num(), 1);
		TestEqual(TEXT("no warnings"), Result.Warnings.Num(), 0);
		if (Result.Bindings.Num() == 1)
		{
			TestTrue(TEXT("resolved asset"), Result.Bindings[0].Asset == Asset);
			TestEqual(TEXT("mesh LOD"), Result.Bindings[0].LodIndex, 0);
			TestEqual(TEXT("section"), Result.Bindings[0].SectionIndex, 0);
			TestEqual(TEXT("asset LOD"), Result.Bindings[0].AssetLodIndex, 0);
		}
	});

	It("ignores metadata without cloth mapping data", [this]()
	{
		const FGuid Guid = FGuid::NewGuid();
		UClothingAssetCommon* Asset = MakeAsset(Guid, 1);
		FSkeletalMeshModel Model = MakeModel(1);
		SetBinding(Model, 0, 0, Guid, 0, false);
		TArray<UClothingAssetBase*> Assets = { Asset };

		const BridgeClothBindings::FBindingQueryResult Result =
			BridgeClothBindings::CollectImportedModel(&Model, Assets);

		TestEqual(TEXT("metadata-only section has no binding"), Result.Bindings.Num(), 0);
		TestEqual(TEXT("metadata-only section has no warning"), Result.Warnings.Num(), 0);
	});

	It("warns when cloth mapping has a default asset GUID", [this]()
	{
		FSkeletalMeshModel Model = MakeModel(1);
		SetBinding(Model, 0, 0, FGuid(), 0);
		const TArray<UClothingAssetBase*> Assets;

		const BridgeClothBindings::FBindingQueryResult Result =
			BridgeClothBindings::CollectImportedModel(&Model, Assets);

		TestEqual(TEXT("invalid binding excluded"), Result.Bindings.Num(), 0);
		TestEqual(TEXT("one default GUID warning"), Result.Warnings.Num(), 1);
		if (Result.Warnings.Num() == 1)
		{
			TestEqual(TEXT("stable reason"), Result.Warnings[0].Reason, FString(TEXT("invalid_asset_guid")));
			TestFalse(TEXT("default GUID omitted"), Result.Warnings[0].AssetGuid.IsSet());
		}
	});

	It("warns without dereferencing an unresolved asset GUID", [this]()
	{
		FSkeletalMeshModel Model = MakeModel(1);
		const FGuid MissingGuid = FGuid::NewGuid();
		SetBinding(Model, 0, 0, MissingGuid, 0);
		const TArray<UClothingAssetBase*> Assets;

		const BridgeClothBindings::FBindingQueryResult Result =
			BridgeClothBindings::CollectImportedModel(&Model, Assets);

		TestEqual(TEXT("no invalid binding"), Result.Bindings.Num(), 0);
		TestEqual(TEXT("one warning"), Result.Warnings.Num(), 1);
		if (Result.Warnings.Num() == 1)
		{
			TestEqual(TEXT("stable reason"), Result.Warnings[0].Reason, FString(TEXT("unresolved_asset_guid")));
			TestTrue(TEXT("warning includes GUID"), Result.Warnings[0].AssetGuid.IsSet());
		}
	});

	It("warns for negative and out-of-range common asset LOD metadata", [this]()
	{
		const FGuid Guid = FGuid::NewGuid();
		UClothingAssetCommon* Asset = MakeAsset(Guid, 1);
		TArray<UClothingAssetBase*> Assets = { Asset };

		FSkeletalMeshModel NegativeModel = MakeModel(1);
		SetBinding(NegativeModel, 0, 0, Guid, -2);
		const BridgeClothBindings::FBindingQueryResult Negative =
			BridgeClothBindings::CollectImportedModel(&NegativeModel, Assets);
		TestEqual(TEXT("one negative LOD warning"), Negative.Warnings.Num(), 1);
		if (Negative.Warnings.Num() == 1)
		{
			TestEqual(TEXT("negative LOD warning"), Negative.Warnings[0].Reason, FString(TEXT("negative_asset_lod_index")));
		}

		FSkeletalMeshModel OutOfRangeModel = MakeModel(1);
		SetBinding(OutOfRangeModel, 0, 0, Guid, 1);
		const BridgeClothBindings::FBindingQueryResult OutOfRange =
			BridgeClothBindings::CollectImportedModel(&OutOfRangeModel, Assets);
		TestEqual(TEXT("one common LOD warning"), OutOfRange.Warnings.Num(), 1);
		if (OutOfRange.Warnings.Num() == 1)
		{
			TestEqual(TEXT("common LOD warning"), OutOfRange.Warnings[0].Reason, FString(TEXT("asset_lod_out_of_range")));
		}
		TestEqual(TEXT("invalid records excluded"), Negative.Bindings.Num() + OutOfRange.Bindings.Num(), 0);
	});

	It("returns a warning when the imported model is missing", [this]()
	{
		const TArray<UClothingAssetBase*> Assets;
		const BridgeClothBindings::FBindingQueryResult Result =
			BridgeClothBindings::CollectImportedModel(nullptr, Assets);

		TestEqual(TEXT("empty bindings"), Result.Bindings.Num(), 0);
		TestEqual(TEXT("one warning"), Result.Warnings.Num(), 1);
		if (Result.Warnings.Num() == 1)
		{
			TestEqual(TEXT("stable reason"), Result.Warnings[0].Reason, FString(TEXT("missing_imported_model")));
		}
	});

	It("only enumerates the requested mesh LOD", [this]()
	{
		const FGuid Guid = FGuid::NewGuid();
		UClothingAssetCommon* Asset = MakeAsset(Guid, 1);
		FSkeletalMeshModel Model = MakeModel(2);
		SetBinding(Model, 0, 0, Guid, 0);
		SetBinding(Model, 1, 0, Guid, 0);
		TArray<UClothingAssetBase*> Assets = { Asset };

		const BridgeClothBindings::FBindingQueryResult Result =
			BridgeClothBindings::CollectImportedModel(&Model, Assets, 1);

		TestEqual(TEXT("one filtered binding"), Result.Bindings.Num(), 1);
		if (Result.Bindings.Num() == 1)
		{
			TestEqual(TEXT("requested LOD"), Result.Bindings[0].LodIndex, 1);
		}
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
