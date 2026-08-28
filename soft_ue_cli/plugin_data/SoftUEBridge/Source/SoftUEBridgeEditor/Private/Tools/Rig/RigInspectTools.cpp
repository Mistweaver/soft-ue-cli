// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Rig/RigInspectTools.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "Rigs/RigHierarchy.h"
#include "Tools/Rig/RigHierarchySerializer.h"

namespace
{
	// `Rig`-prefixed to stay unity-blob safe alongside the other tool translation units.

	FBridgeSchemaProperty RigSchemaProperty(const FString& Type, const FString& Description, bool bRequired = false)
	{
		FBridgeSchemaProperty Property;
		Property.Type = Type;
		Property.Description = Description;
		Property.bRequired = bRequired;
		return Property;
	}

	UObject* RigLoadAsset(const FString& AssetPath, FString& OutError)
	{
		IAssetRegistry& AssetRegistry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		const FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(AssetPath));
		UObject* AssetObject = AssetData.IsValid() ? AssetData.GetAsset() : nullptr;
		if (!AssetObject)
		{
			AssetObject = LoadObject<UObject>(nullptr, *AssetPath);
		}
		if (!AssetObject)
		{
			OutError = FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath);
			return nullptr;
		}
		return AssetObject;
	}

	/**
	 * Reaches the hierarchy through IRigHierarchyProvider rather than UControlRigBlueprint.
	 * The interface lives in the ControlRig runtime module and is identical in 5.7 and 5.8,
	 * so this avoids ControlRigDeveloper and the 5.8 asset-interface rename entirely.
	 */
	URigHierarchy* RigResolveHierarchy(const FString& AssetPath, UObject*& OutAsset, FString& OutError)
	{
		OutAsset = RigLoadAsset(AssetPath, OutError);
		if (!OutAsset)
		{
			return nullptr;
		}

		const IRigHierarchyProvider* Provider = Cast<IRigHierarchyProvider>(OutAsset);
		if (!Provider)
		{
			OutError = FString::Printf(
				TEXT("Asset '%s' is '%s', which does not provide a Control Rig hierarchy"),
				*AssetPath,
				*OutAsset->GetClass()->GetName());
			return nullptr;
		}

		URigHierarchy* Hierarchy = Provider->GetHierarchy();
		if (!Hierarchy)
		{
			OutError = FString::Printf(TEXT("Control Rig '%s' has no hierarchy"), *AssetPath);
			return nullptr;
		}
		return Hierarchy;
	}
}

FString URigInspectTool::GetToolDescription() const
{
	return TEXT(
		"Inspect a Control Rig hierarchy. Returns hierarchy elements (bones, controls, nulls, curves, "
		"sockets, connectors) with their parents and transforms, plus per-type element counts. "
		"Filter with element_type and name_filter; control settings are optional.");
}

TMap<FString, FBridgeSchemaProperty> URigInspectTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(
		TEXT("asset_path"),
		RigSchemaProperty(TEXT("string"), TEXT("Control Rig asset path (e.g., /Game/Characters/CR_Hero)"), true));
	Schema.Add(
		TEXT("element_type"),
		RigSchemaProperty(
			TEXT("string"),
			TEXT("Comma-separated element types: bone, null, control, curve, reference, connector, socket, all")));
	Schema.Add(
		TEXT("name_filter"),
		RigSchemaProperty(TEXT("string"), TEXT("Only include elements whose name contains this substring")));
	Schema.Add(
		TEXT("transforms"),
		RigSchemaProperty(TEXT("string"), TEXT("Transforms to serialize: none, local, global, or both (default: local)")));
	Schema.Add(
		TEXT("include_settings"),
		RigSchemaProperty(
			TEXT("boolean"),
			TEXT("Include control settings (control type, primary axis, limits, shape) on control elements")));
	Schema.Add(
		TEXT("include_offsets"),
		RigSchemaProperty(
			TEXT("boolean"),
			TEXT("Include each control offset transform, which a rig that writes offsets during Forward "
				"Solve moves independently of the control own value")));
	return Schema;
}

TArray<FString> URigInspectTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FBridgeToolResult URigInspectTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FBridgeToolResult::Error(TEXT("rig-inspect: asset_path is required"));
	}

	RigHierarchySerializer::FRigInspectOptions Options;
	FString Error;

	if (!RigHierarchySerializer::ParseElementTypeMask(
			GetStringArgOrDefault(Arguments, TEXT("element_type"), TEXT("")), Options.TypeMask, Error))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("rig-inspect: %s"), *Error));
	}
	if (!RigHierarchySerializer::ParseTransformDetail(
			GetStringArgOrDefault(Arguments, TEXT("transforms"), TEXT("")), Options.TransformDetail, Error))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("rig-inspect: %s"), *Error));
	}
	Options.NameFilter = GetStringArgOrDefault(Arguments, TEXT("name_filter"), TEXT(""));
	Options.bIncludeSettings = GetBoolArgOrDefault(Arguments, TEXT("include_settings"), false);
	Options.bIncludeOffsets = GetBoolArgOrDefault(Arguments, TEXT("include_offsets"), false);

	UObject* Asset = nullptr;
	const URigHierarchy* Hierarchy = RigResolveHierarchy(AssetPath, Asset, Error);
	if (!Hierarchy)
	{
		return FBridgeToolResult::Error(Error);
	}

	TSharedPtr<FJsonObject> Result = RigHierarchySerializer::SerializeHierarchy(Hierarchy, Options);
	Result->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Result->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetName());
	return FBridgeToolResult::Json(Result);
}

FString URigControlGetTool::GetToolDescription() const
{
	return TEXT(
		"Read Control Rig control values. Each value is serialized using the storage type that matches "
		"the control's ERigControlType (bool, float, integer, vector2d, position, scale, rotator, "
		"transform, transform_no_scale, euler_transform, scale_float). Reads current or initial values.");
}

TMap<FString, FBridgeSchemaProperty> URigControlGetTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(
		TEXT("asset_path"),
		RigSchemaProperty(TEXT("string"), TEXT("Control Rig asset path (e.g., /Game/Characters/CR_Hero)"), true));
	Schema.Add(
		TEXT("controls"),
		RigSchemaProperty(
			TEXT("string"),
			TEXT("Comma-separated control names to read (default: every control in the hierarchy)")));
	Schema.Add(
		TEXT("value_type"),
		RigSchemaProperty(TEXT("string"), TEXT("Read 'current' or 'initial' control values (default: current)")));
	return Schema;
}

TArray<FString> URigControlGetTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FBridgeToolResult URigControlGetTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FBridgeToolResult::Error(TEXT("rig-control-get: asset_path is required"));
	}

	const FString ValueTypeArg = GetStringArgOrDefault(Arguments, TEXT("value_type"), TEXT("current")).ToLower();
	if (ValueTypeArg != TEXT("current") && ValueTypeArg != TEXT("initial"))
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("rig-control-get: unknown value_type '%s'. Expected 'current' or 'initial'"), *ValueTypeArg));
	}

	TArray<FString> ControlNames;
	const FString ControlsArg = GetStringArgOrDefault(Arguments, TEXT("controls"), TEXT(""));
	if (!ControlsArg.TrimStartAndEnd().IsEmpty())
	{
		ControlsArg.ParseIntoArray(ControlNames, TEXT(","), true);
	}

	FString Error;
	UObject* Asset = nullptr;
	const URigHierarchy* Hierarchy = RigResolveHierarchy(AssetPath, Asset, Error);
	if (!Hierarchy)
	{
		return FBridgeToolResult::Error(Error);
	}

	TSharedPtr<FJsonObject> Result =
		RigHierarchySerializer::SerializeControlValues(Hierarchy, ControlNames, ValueTypeArg == TEXT("initial"));
	Result->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Result->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetName());
	return FBridgeToolResult::Json(Result);
}
