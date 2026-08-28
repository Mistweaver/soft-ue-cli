// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Rig/RigGraphInspectTool.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "RigVMBlueprintLegacy.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMLink.h"
#include "RigVMModel/RigVMNode.h"
#include "RigVMModel/Nodes/RigVMTemplateNode.h"
#include "RigVMModel/RigVMPin.h"

namespace
{
	// `RigGraph`-prefixed to stay unity-blob safe alongside RigInspectTools.cpp, which defines its
	// own asset-loading and schema helpers in an anonymous namespace within this same module.

	FBridgeSchemaProperty RigGraphSchemaProperty(const FString& Type, const FString& Description, bool bRequired = false)
	{
		FBridgeSchemaProperty Property;
		Property.Type = Type;
		Property.Description = Description;
		Property.bRequired = bRequired;
		return Property;
	}

	/**
	 * Resolves the RigVM blueprint through URigVMBlueprint rather than IRigVMAssetInterface.
	 * 5.8 renamed that interface (IRigVMAssetInterface -> IRigVMEditorAssetInterface) and moved its
	 * header, but GetAllModels()/GetDefaultModel() are declared as class overrides in both versions.
	 */
	URigVMBlueprint* RigGraphLoadBlueprint(const FString& AssetPath, FString& OutError)
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

		URigVMBlueprint* Blueprint = Cast<URigVMBlueprint>(AssetObject);
		if (!Blueprint)
		{
			OutError = FString::Printf(
				TEXT("Asset '%s' is '%s', which is not a RigVM-backed Blueprint (Control Rig)"),
				*AssetPath,
				*AssetObject->GetClass()->GetName());
			return nullptr;
		}
		return Blueprint;
	}

	FString RigGraphPinDirectionToString(ERigVMPinDirection Direction)
	{
		if (const UEnum* Enum = StaticEnum<ERigVMPinDirection>())
		{
			const FString Name = Enum->GetNameStringByValue(static_cast<int64>(Direction));
			if (!Name.IsEmpty())
			{
				return Name;
			}
		}
		return TEXT("Unknown");
	}

	TSharedPtr<FJsonObject> RigGraphSerializePin(const URigVMPin* Pin)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("name"), Pin->GetName());
		Json->SetStringField(TEXT("pin_path"), Pin->GetPinPath());
		Json->SetStringField(TEXT("direction"), RigGraphPinDirectionToString(Pin->GetDirection()));
		Json->SetStringField(TEXT("cpp_type"), Pin->GetCPPType());
		Json->SetStringField(TEXT("default_value"), Pin->GetDefaultValue());

		const TArray<URigVMPin*>& SubPins = Pin->GetSubPins();
		if (SubPins.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> SubPinsJson;
			for (const URigVMPin* SubPin : SubPins)
			{
				if (SubPin)
				{
					SubPinsJson.Add(MakeShared<FJsonValueObject>(RigGraphSerializePin(SubPin)));
				}
			}
			Json->SetArrayField(TEXT("sub_pins"), SubPinsJson);
		}

		return Json;
	}

	TSharedPtr<FJsonObject> RigGraphSerializeNode(const URigVMNode* Node, bool bIncludePins)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("name"), Node->GetName());
		Json->SetStringField(TEXT("title"), Node->GetNodeTitle());
		Json->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());

		// GetClass() reports the RigVM wrapper (URigVMUnitNode / URigVMDispatchNode) for nearly every
		// node, which does not identify what the node does. GetScriptStruct() is declared on
		// URigVMTemplateNode -- the common base of both -- and names the actual rig unit.
		if (const URigVMTemplateNode* TemplateNode = Cast<URigVMTemplateNode>(Node))
		{
			if (const UScriptStruct* ScriptStruct = TemplateNode->GetScriptStruct())
			{
				Json->SetStringField(TEXT("script_struct"), ScriptStruct->GetName());
			}
		}

		const FVector2D Position = Node->GetPosition();
		TSharedPtr<FJsonObject> PositionJson = MakeShared<FJsonObject>();
		PositionJson->SetNumberField(TEXT("x"), Position.X);
		PositionJson->SetNumberField(TEXT("y"), Position.Y);
		Json->SetObjectField(TEXT("position"), PositionJson);

		const TArray<URigVMPin*>& Pins = Node->GetPins();
		Json->SetNumberField(TEXT("pin_count"), Pins.Num());
		if (bIncludePins)
		{
			TArray<TSharedPtr<FJsonValue>> PinsJson;
			for (const URigVMPin* Pin : Pins)
			{
				if (Pin)
				{
					PinsJson.Add(MakeShared<FJsonValueObject>(RigGraphSerializePin(Pin)));
				}
			}
			Json->SetArrayField(TEXT("pins"), PinsJson);
		}

		return Json;
	}

	TSharedPtr<FJsonObject> RigGraphSerializeGraph(
		const URigVMGraph* Graph,
		const FString& NodeFilter,
		bool bIncludePins,
		bool bIncludeLinks)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("name"), Graph->GetGraphName());

		TArray<TSharedPtr<FJsonValue>> NodesJson;
		for (const URigVMNode* Node : Graph->GetNodes())
		{
			if (!Node)
			{
				continue;
			}
			if (!NodeFilter.IsEmpty()
				&& !Node->GetName().Contains(NodeFilter)
				&& !Node->GetNodeTitle().Contains(NodeFilter))
			{
				continue;
			}
			NodesJson.Add(MakeShared<FJsonValueObject>(RigGraphSerializeNode(Node, bIncludePins)));
		}

		Json->SetNumberField(TEXT("node_count"), NodesJson.Num());
		Json->SetNumberField(TEXT("graph_node_count"), Graph->GetNodes().Num());
		Json->SetArrayField(TEXT("nodes"), NodesJson);

		const TArray<URigVMLink*>& Links = Graph->GetLinks();
		Json->SetNumberField(TEXT("link_count"), Links.Num());
		if (bIncludeLinks)
		{
			TArray<TSharedPtr<FJsonValue>> LinksJson;
			for (const URigVMLink* Link : Links)
			{
				if (!Link)
				{
					continue;
				}
				const URigVMPin* SourcePin = Link->GetSourcePin();
				const URigVMPin* TargetPin = Link->GetTargetPin();
				if (!SourcePin || !TargetPin)
				{
					continue;
				}

				TSharedPtr<FJsonObject> LinkJson = MakeShared<FJsonObject>();
				LinkJson->SetStringField(TEXT("source_pin"), SourcePin->GetPinPath());
				LinkJson->SetStringField(TEXT("target_pin"), TargetPin->GetPinPath());
				LinksJson.Add(MakeShared<FJsonValueObject>(LinkJson));
			}
			Json->SetArrayField(TEXT("links"), LinksJson);
		}

		return Json;
	}
}

FString URigGraphInspectTool::GetToolDescription() const
{
	return TEXT(
		"Inspect the RigVM graph models on a Control Rig asset. Returns each model with its nodes "
		"(name, title, node class, position, pin count) and, optionally, full pin data and the link "
		"list. This reads the RigVM model itself, not the Blueprint EdGraph view that "
		"query-blueprint-graph walks, so the data reflects what the rig actually executes.");
}

TMap<FString, FBridgeSchemaProperty> URigGraphInspectTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(
		TEXT("asset_path"),
		RigGraphSchemaProperty(TEXT("string"), TEXT("Control Rig asset path (e.g., /Game/Characters/CR_Hero)"), true));
	Schema.Add(
		TEXT("graph_name"),
		RigGraphSchemaProperty(TEXT("string"), TEXT("Only inspect the RigVM model with this name")));
	Schema.Add(
		TEXT("node_filter"),
		RigGraphSchemaProperty(TEXT("string"), TEXT("Only include nodes whose name or title contains this substring")));
	Schema.Add(
		TEXT("include_pins"),
		RigGraphSchemaProperty(TEXT("boolean"), TEXT("Include pin data (path, direction, cpp type, default value) on every node")));
	Schema.Add(
		TEXT("include_links"),
		RigGraphSchemaProperty(TEXT("boolean"), TEXT("Include the source/target pin paths of every link in the graph")));
	return Schema;
}

TArray<FString> URigGraphInspectTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FBridgeToolResult URigGraphInspectTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FBridgeToolResult::Error(TEXT("rig-graph-inspect: asset_path is required"));
	}

	const FString GraphNameFilter = GetStringArgOrDefault(Arguments, TEXT("graph_name"), TEXT(""));
	const FString NodeFilter = GetStringArgOrDefault(Arguments, TEXT("node_filter"), TEXT(""));
	const bool bIncludePins = GetBoolArgOrDefault(Arguments, TEXT("include_pins"), false);
	const bool bIncludeLinks = GetBoolArgOrDefault(Arguments, TEXT("include_links"), false);

	FString Error;
	URigVMBlueprint* Blueprint = RigGraphLoadBlueprint(AssetPath, Error);
	if (!Blueprint)
	{
		return FBridgeToolResult::Error(Error);
	}

	const TArray<URigVMGraph*> Models = Blueprint->GetAllModels();

	TArray<TSharedPtr<FJsonValue>> GraphsJson;
	TArray<TSharedPtr<FJsonValue>> AvailableGraphNames;
	for (const URigVMGraph* Graph : Models)
	{
		if (!Graph)
		{
			continue;
		}
		const FString GraphName = Graph->GetGraphName();
		AvailableGraphNames.Add(MakeShared<FJsonValueString>(GraphName));

		if (!GraphNameFilter.IsEmpty() && GraphName != GraphNameFilter)
		{
			continue;
		}
		GraphsJson.Add(
			MakeShared<FJsonValueObject>(RigGraphSerializeGraph(Graph, NodeFilter, bIncludePins, bIncludeLinks)));
	}

	if (!GraphNameFilter.IsEmpty() && GraphsJson.Num() == 0)
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("rig-graph-inspect: no RigVM model named '%s' on '%s'. Run without --graph to list the available models."),
			*GraphNameFilter,
			*AssetPath));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
	Result->SetStringField(TEXT("asset_class"), Blueprint->GetClass()->GetName());
	Result->SetNumberField(TEXT("model_count"), Models.Num());
	Result->SetArrayField(TEXT("available_graphs"), AvailableGraphNames);
	Result->SetArrayField(TEXT("graphs"), GraphsJson);
	return FBridgeToolResult::Json(Result);
}
