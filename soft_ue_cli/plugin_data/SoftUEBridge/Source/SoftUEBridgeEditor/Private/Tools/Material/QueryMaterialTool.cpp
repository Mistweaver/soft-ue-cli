// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Material/QueryMaterialTool.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionStaticBoolParameter.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "MaterialExpressionIO.h"
#include "SceneTypes.h"
#include "Tools/BridgeToolResult.h"
#include "SoftUEBridgeEditorModule.h"

FString UQueryMaterialTool::GetToolDescription() const
{
	return TEXT("Query Material, MaterialInstance, or MaterialFunction structure: expression graph and parameters. "
		"Use 'include' to select 'graph', 'parameters', or 'all' (default).");
}

TMap<FString, FBridgeSchemaProperty> UQueryMaterialTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Asset path to the Material, MaterialInstance, or MaterialFunction");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty Include;
	Include.Type = TEXT("string");
	Include.Description = TEXT("What to include: 'graph', 'parameters', or 'all' (default: 'all')");
	Include.bRequired = false;
	Schema.Add(TEXT("include"), Include);

	FBridgeSchemaProperty IncludePositions;
	IncludePositions.Type = TEXT("boolean");
	IncludePositions.Description = TEXT("Include expression X/Y positions (default: false)");
	IncludePositions.bRequired = false;
	Schema.Add(TEXT("include_positions"), IncludePositions);

	FBridgeSchemaProperty IncludeDefaults;
	IncludeDefaults.Type = TEXT("boolean");
	IncludeDefaults.Description = TEXT("Include default parameter values (default: true)");
	IncludeDefaults.bRequired = false;
	Schema.Add(TEXT("include_defaults"), IncludeDefaults);

	FBridgeSchemaProperty ParameterFilter;
	ParameterFilter.Type = TEXT("string");
	ParameterFilter.Description = TEXT("Filter parameters by name (wildcards supported)");
	ParameterFilter.bRequired = false;
	Schema.Add(TEXT("parameter_filter"), ParameterFilter);

	FBridgeSchemaProperty ParentChain;
	ParentChain.Type = TEXT("boolean");
	ParentChain.Description = TEXT("Include full parent material chain from leaf to root (default: false)");
	ParentChain.bRequired = false;
	Schema.Add(TEXT("parent_chain"), ParentChain);

	return Schema;
}

TArray<FString> UQueryMaterialTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FBridgeToolResult UQueryMaterialTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FBridgeToolResult::Error(TEXT("Missing required parameter: asset_path"));
	}

	FString Include = GetStringArgOrDefault(Arguments, TEXT("include"), TEXT("all")).ToLower();
	bool bIncludePositions = GetBoolArgOrDefault(Arguments, TEXT("include_positions"), false);
	bool bIncludeDefaults = GetBoolArgOrDefault(Arguments, TEXT("include_defaults"), true);
	FString ParameterFilter = GetStringArgOrDefault(Arguments, TEXT("parameter_filter"), TEXT(""));
	bool bParentChain = GetBoolArgOrDefault(Arguments, TEXT("parent_chain"), false);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("query-material: path='%s', include='%s'"), *AssetPath, *Include);

	bool bIncludeGraph = (Include == TEXT("all") || Include == TEXT("graph"));
	bool bIncludeParams = (Include == TEXT("all") || Include == TEXT("parameters"));

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("asset_path"), AssetPath);

	// Try MaterialInstance first
	UMaterialInstance* MatInstance = LoadObject<UMaterialInstance>(nullptr, *AssetPath);
	if (MatInstance)
	{
		Result->SetStringField(TEXT("asset_type"), TEXT("MaterialInstance"));
		// An instance answers "is this Unlit?" as well as a base material does, and that is
		// usually the asset a renderer actually points at.
		Result->SetObjectField(TEXT("settings"), ExtractMaterialSettings(MatInstance));
		if (MatInstance->Parent)
		{
			Result->SetStringField(TEXT("parent_material"), MatInstance->Parent->GetPathName());
		}

		if (bIncludeGraph)
		{
			Result->SetStringField(TEXT("graph_note"), TEXT("MaterialInstances don't have expression graphs. Query the parent Material."));
		}

		if (bIncludeParams)
		{
			Result->SetObjectField(TEXT("parameters"), ExtractParameters(MatInstance, bIncludeDefaults, ParameterFilter));
		}

		if (bParentChain)
		{
			Result->SetArrayField(TEXT("parent_chain"), ExtractParentChain(MatInstance));
		}

		return FBridgeToolResult::Json(Result);
	}

	// Try base Material
	UMaterial* Material = LoadObject<UMaterial>(nullptr, *AssetPath);
	if (!Material)
	{
		// Try MaterialFunction
		UMaterialFunction* MatFunc = LoadObject<UMaterialFunction>(nullptr, *AssetPath);
		if (MatFunc)
		{
			Result->SetStringField(TEXT("asset_type"), TEXT("MaterialFunction"));
			if (!MatFunc->Description.IsEmpty())
			{
				Result->SetStringField(TEXT("description"), MatFunc->Description);
			}

			if (bIncludeGraph)
			{
				TSharedPtr<FJsonObject> GraphJson = MakeShareable(new FJsonObject);
				TArray<TSharedPtr<FJsonValue>> ExpressionsArray;
				for (UMaterialExpression* Expression : MatFunc->GetExpressions())
				{
					if (!Expression) continue;
					TSharedPtr<FJsonObject> ExprJson = ExpressionToJson(Expression, bIncludePositions);
					if (ExprJson.IsValid())
					{
						ExpressionsArray.Add(MakeShareable(new FJsonValueObject(ExprJson)));
					}
				}
				GraphJson->SetArrayField(TEXT("expressions"), ExpressionsArray);
				GraphJson->SetNumberField(TEXT("expression_count"), ExpressionsArray.Num());
				Result->SetObjectField(TEXT("graph"), GraphJson);
			}

			if (bIncludeParams)
			{
				Result->SetStringField(TEXT("parameters_note"),
					TEXT("MaterialFunctions do not expose parameters directly. Parameter expressions are visible as nodes in the graph."));
			}

			return FBridgeToolResult::Json(Result);
		}

		return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to load Material or MaterialFunction: %s"), *AssetPath));
	}

	Result->SetStringField(TEXT("asset_type"), TEXT("Material"));
	// Always emitted, not gated behind include=graph: "is this Unlit?" is a question about the
	// material, not about its graph, and it was previously only answerable from a raw bitfield.
	Result->SetObjectField(TEXT("settings"), ExtractMaterialSettings(Material));

	if (bIncludeGraph)
	{
		Result->SetObjectField(TEXT("graph"), ExtractGraph(Material, bIncludePositions));
	}

	if (bIncludeParams)
	{
		Result->SetObjectField(TEXT("parameters"), ExtractParameters(Material, bIncludeDefaults, ParameterFilter));
	}

	if (bParentChain)
	{
		Result->SetArrayField(TEXT("parent_chain"), ExtractParentChain(Material));
	}

	return FBridgeToolResult::Json(Result);
}

// === Graph extraction ===

TSharedPtr<FJsonObject> UQueryMaterialTool::ExtractGraph(UMaterial* Material, bool bIncludePositions) const
{
	if (!Material)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> GraphJson = MakeShareable(new FJsonObject);

	// Get all expressions
	TArray<TSharedPtr<FJsonValue>> ExpressionsArray;
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (!Expression) continue;

		TSharedPtr<FJsonObject> ExprJson = ExpressionToJson(Expression, bIncludePositions);
		if (ExprJson.IsValid())
		{
			ExpressionsArray.Add(MakeShareable(new FJsonValueObject(ExprJson)));
		}
	}

	GraphJson->SetArrayField(TEXT("expressions"), ExpressionsArray);
	GraphJson->SetNumberField(TEXT("expression_count"), ExpressionsArray.Num());

	// The root is what makes the expression list readable: without it, "which node feeds
	// EmissiveColor" can only be inferred, and an inference that lands on the wrong pin reads
	// exactly like a fact.
	GraphJson->SetArrayField(TEXT("root_connections"), ExtractRootConnections(Material));

	return GraphJson;
}

TArray<TSharedPtr<FJsonValue>> UQueryMaterialTool::ExtractRootConnections(UMaterial* Material) const
{
	TArray<TSharedPtr<FJsonValue>> ConnectionsArray;
	if (!Material)
	{
		return ConnectionsArray;
	}

	for (int32 PropertyIndex = 0; PropertyIndex < MP_MAX; ++PropertyIndex)
	{
		const EMaterialProperty Property = static_cast<EMaterialProperty>(PropertyIndex);

		// MP_MaterialAttributes and MP_CustomOutput are not simple pin inputs, and the deprecated
		// entries no longer resolve; GetExpressionInputForProperty returns null for all of them.
		FExpressionInput* Input = Material->GetExpressionInputForProperty(Property);
		if (!Input)
		{
			continue;
		}

		const FString PropertyName = FMaterialAttributeDefinitionMap::GetAttributeName(Property);
		if (PropertyName.IsEmpty())
		{
			continue;
		}

		// Follow reroute nodes: a knot between the expression and the root would otherwise be
		// reported as the source, which is not a node the reader can act on.
		const FExpressionInput TracedInput = Input->GetTracedInput();

		TSharedPtr<FJsonObject> ConnectionJson = MakeShareable(new FJsonObject);
		ConnectionJson->SetStringField(TEXT("property"), PropertyName);
		ConnectionJson->SetBoolField(TEXT("connected"), TracedInput.Expression != nullptr);

		if (TracedInput.Expression)
		{
			ConnectionJson->SetStringField(TEXT("expression"), TracedInput.Expression->GetName());
			ConnectionJson->SetStringField(TEXT("expression_class"), TracedInput.Expression->GetClass()->GetName());
			ConnectionJson->SetNumberField(TEXT("output_index"), TracedInput.OutputIndex);
			ConnectionJson->SetStringField(
				TEXT("expression_guid"),
				TracedInput.Expression->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens));
		}

		ConnectionsArray.Add(MakeShareable(new FJsonValueObject(ConnectionJson)));
	}

	return ConnectionsArray;
}

namespace
{
	// `QueryMaterial`-prefixed to stay unity-blob safe: at file scope every .cpp folded into one
	// translation unit shares a single anonymous namespace, and a helper called StripEnumPrefix
	// is exactly the name a neighbouring tool would also pick.

	/**
	 * Enum entry name without its UE prefix. The report this answers asked for "Unlit", not
	 * "MSM_Unlit" -- the point of resolving the bitfield is that a reader should not have to decode
	 * anything, and a prefix is one more thing to strip before comparing.
	 */
	template <typename TEnum>
	FString QueryMaterialStripEnumPrefix(TEnum Value, const TCHAR* Prefix)
	{
		const UEnum* Enum = StaticEnum<TEnum>();
		if (!Enum)
		{
			return TEXT("Unknown");
		}
		FString Name = Enum->GetNameStringByValue(static_cast<int64>(Value));
		Name.RemoveFromStart(Prefix);
		return Name.IsEmpty() ? TEXT("Unknown") : Name;
	}

	FString QueryMaterialShadingModelName(EMaterialShadingModel Model)
	{
		return QueryMaterialStripEnumPrefix<EMaterialShadingModel>(Model, TEXT("MSM_"));
	}
}

TSharedPtr<FJsonObject> UQueryMaterialTool::ExtractMaterialSettings(UMaterialInterface* Material) const
{
	TSharedPtr<FJsonObject> SettingsJson = MakeShareable(new FJsonObject);
	if (!Material)
	{
		return SettingsJson;
	}

	const FMaterialShadingModelField ShadingModels = Material->GetShadingModels();

	// GetFirstShadingModel() opens with check(IsValid()), so an empty or malformed field would take
	// the editor down rather than report.
	if (ShadingModels.IsValid())
	{
		const EMaterialShadingModel FirstModel = ShadingModels.GetFirstShadingModel();
		SettingsJson->SetStringField(TEXT("shading_model"), QueryMaterialShadingModelName(FirstModel));

		// A material can carry more than one shading model (a From Material Expression setup, or a
		// Substrate material), in which case naming only the first would be a half-truth.
		TArray<TSharedPtr<FJsonValue>> AllModelsJson;
		for (int32 ModelIndex = 0; ModelIndex < MSM_NUM; ++ModelIndex)
		{
			const EMaterialShadingModel Model = static_cast<EMaterialShadingModel>(ModelIndex);
			if (ShadingModels.HasShadingModel(Model))
			{
				AllModelsJson.Add(MakeShareable(new FJsonValueString(QueryMaterialShadingModelName(Model))));
			}
		}
		SettingsJson->SetArrayField(TEXT("shading_models"), AllModelsJson);
		SettingsJson->SetNumberField(TEXT("shading_model_count"), ShadingModels.CountShadingModels());
	}
	else
	{
		SettingsJson->SetStringField(TEXT("shading_model"), TEXT("Unknown"));
	}

	// Declared on UMaterial only; an instance reports what its base material does.
	if (const UMaterial* BaseMaterial = Material->GetMaterial())
	{
		SettingsJson->SetBoolField(
			TEXT("shading_model_from_expression"),
			BaseMaterial->IsShadingModelFromMaterialExpression());
	}

	SettingsJson->SetStringField(
		TEXT("blend_mode"), QueryMaterialStripEnumPrefix<EBlendMode>(Material->GetBlendMode(), TEXT("BLEND_")));
	// MaterialDomain is declared on UMaterial, not the interface; an instance answers for its base.
	if (const UMaterial* BaseMaterial = Material->GetMaterial())
	{
		SettingsJson->SetStringField(
			TEXT("material_domain"),
			QueryMaterialStripEnumPrefix<EMaterialDomain>(BaseMaterial->MaterialDomain, TEXT("MD_")));
	}
	SettingsJson->SetBoolField(TEXT("two_sided"), Material->IsTwoSided());

	return SettingsJson;
}

TSharedPtr<FJsonObject> UQueryMaterialTool::ExpressionToJson(UMaterialExpression* Expression, bool bIncludePositions) const
{
	if (!Expression)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> ExprJson = MakeShareable(new FJsonObject);

	ExprJson->SetStringField(TEXT("name"), Expression->GetName());
	ExprJson->SetStringField(TEXT("guid"), Expression->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens));
	ExprJson->SetStringField(TEXT("class"), Expression->GetClass()->GetName());
	ExprJson->SetStringField(TEXT("description"), Expression->GetDescription());

	if (bIncludePositions)
	{
		ExprJson->SetNumberField(TEXT("x"), Expression->MaterialExpressionEditorX);
		ExprJson->SetNumberField(TEXT("y"), Expression->MaterialExpressionEditorY);
	}

	// Inputs -- FExpressionInputIterator is the engine's bounded iterator.
	// Some expression subclasses never return nullptr from GetInput() on
	// out-of-range indices, so bounded traversal avoids spinning forever.
	TArray<TSharedPtr<FJsonValue>> InputsArray;
	for (FExpressionInputIterator It{ Expression }; It; ++It)
	{
		FExpressionInput* Input = It.Input;

		TSharedPtr<FJsonObject> InputJson = MakeShareable(new FJsonObject);
		InputJson->SetStringField(TEXT("name"), Expression->GetInputName(It.Index).ToString());
		InputJson->SetBoolField(TEXT("connected"), Input->Expression != nullptr);

		if (Input->Expression)
		{
			InputJson->SetStringField(TEXT("connected_to"), Input->Expression->GetName());
			InputJson->SetNumberField(TEXT("output_index"), Input->OutputIndex);
		}

		InputsArray.Add(MakeShareable(new FJsonValueObject(InputJson)));
	}
	ExprJson->SetArrayField(TEXT("inputs"), InputsArray);

	return ExprJson;
}

// === Parameter extraction ===

TSharedPtr<FJsonObject> UQueryMaterialTool::ExtractParameters(UMaterialInterface* Material,
	bool bIncludeDefaults, const FString& ParameterFilter) const
{
	if (!Material)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> ParamsJson = MakeShareable(new FJsonObject);

	TArray<TSharedPtr<FJsonValue>> ScalarArray;
	TArray<TSharedPtr<FJsonValue>> VectorArray;
	TArray<TSharedPtr<FJsonValue>> TextureArray;
	TArray<TSharedPtr<FJsonValue>> SwitchArray;

	ExtractScalarParameters(Material, ScalarArray);
	ExtractVectorParameters(Material, VectorArray);
	ExtractTextureParameters(Material, TextureArray);
	ExtractStaticSwitchParameters(Material, SwitchArray);

	ParamsJson->SetArrayField(TEXT("scalar"), ScalarArray);
	ParamsJson->SetArrayField(TEXT("vector"), VectorArray);
	ParamsJson->SetArrayField(TEXT("texture"), TextureArray);
	ParamsJson->SetArrayField(TEXT("static_switch"), SwitchArray);

	ParamsJson->SetNumberField(TEXT("total_count"),
		ScalarArray.Num() + VectorArray.Num() + TextureArray.Num() + SwitchArray.Num());

	return ParamsJson;
}

void UQueryMaterialTool::ExtractScalarParameters(UMaterialInterface* Material, TArray<TSharedPtr<FJsonValue>>& OutArray) const
{
	if (!Material) return;

	TArray<FMaterialParameterInfo> ParameterInfos;
	TArray<FGuid> ParameterGuids;
	Material->GetAllScalarParameterInfo(ParameterInfos, ParameterGuids);

	for (const FMaterialParameterInfo& Info : ParameterInfos)
	{
		float Value = 0.0f;
		Material->GetScalarParameterValue(Info, Value);

		TSharedPtr<FJsonObject> ParamJson = MakeShareable(new FJsonObject);
		ParamJson->SetStringField(TEXT("name"), Info.Name.ToString());
		ParamJson->SetNumberField(TEXT("value"), Value);

		OutArray.Add(MakeShareable(new FJsonValueObject(ParamJson)));
	}
}

void UQueryMaterialTool::ExtractVectorParameters(UMaterialInterface* Material, TArray<TSharedPtr<FJsonValue>>& OutArray) const
{
	if (!Material) return;

	TArray<FMaterialParameterInfo> ParameterInfos;
	TArray<FGuid> ParameterGuids;
	Material->GetAllVectorParameterInfo(ParameterInfos, ParameterGuids);

	for (const FMaterialParameterInfo& Info : ParameterInfos)
	{
		FLinearColor Value;
		Material->GetVectorParameterValue(Info, Value);

		TSharedPtr<FJsonObject> ParamJson = MakeShareable(new FJsonObject);
		ParamJson->SetStringField(TEXT("name"), Info.Name.ToString());

		TSharedPtr<FJsonObject> ColorJson = MakeShareable(new FJsonObject);
		ColorJson->SetNumberField(TEXT("r"), Value.R);
		ColorJson->SetNumberField(TEXT("g"), Value.G);
		ColorJson->SetNumberField(TEXT("b"), Value.B);
		ColorJson->SetNumberField(TEXT("a"), Value.A);
		ParamJson->SetObjectField(TEXT("value"), ColorJson);

		OutArray.Add(MakeShareable(new FJsonValueObject(ParamJson)));
	}
}

void UQueryMaterialTool::ExtractTextureParameters(UMaterialInterface* Material, TArray<TSharedPtr<FJsonValue>>& OutArray) const
{
	if (!Material) return;

	TArray<FMaterialParameterInfo> ParameterInfos;
	TArray<FGuid> ParameterGuids;
	Material->GetAllTextureParameterInfo(ParameterInfos, ParameterGuids);

	for (const FMaterialParameterInfo& Info : ParameterInfos)
	{
		UTexture* Texture = nullptr;
		Material->GetTextureParameterValue(Info, Texture);

		TSharedPtr<FJsonObject> ParamJson = MakeShareable(new FJsonObject);
		ParamJson->SetStringField(TEXT("name"), Info.Name.ToString());
		ParamJson->SetStringField(TEXT("value"), Texture ? Texture->GetPathName() : TEXT("None"));

		OutArray.Add(MakeShareable(new FJsonValueObject(ParamJson)));
	}
}

void UQueryMaterialTool::ExtractStaticSwitchParameters(UMaterialInterface* Material, TArray<TSharedPtr<FJsonValue>>& OutArray) const
{
	if (!Material) return;

	TArray<FMaterialParameterInfo> ParameterInfos;
	TArray<FGuid> ParameterGuids;
	Material->GetAllStaticSwitchParameterInfo(ParameterInfos, ParameterGuids);

	for (const FMaterialParameterInfo& Info : ParameterInfos)
	{
		bool bValue = false;
		FGuid OutGuid;
		Material->GetStaticSwitchParameterValue(Info, bValue, OutGuid);

		TSharedPtr<FJsonObject> ParamJson = MakeShareable(new FJsonObject);
		ParamJson->SetStringField(TEXT("name"), Info.Name.ToString());
		ParamJson->SetBoolField(TEXT("value"), bValue);

		OutArray.Add(MakeShareable(new FJsonValueObject(ParamJson)));
	}
}

TArray<TSharedPtr<FJsonValue>> UQueryMaterialTool::ExtractParentChain(UMaterialInterface* Material) const
{
	TArray<TSharedPtr<FJsonValue>> Chain;
	UMaterialInterface* Current = Material;
	while (Current)
	{
		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
		Entry->SetStringField(TEXT("name"), Current->GetName());
		Entry->SetStringField(TEXT("path"), Current->GetPathName());
		Entry->SetStringField(TEXT("class"), Current->GetClass()->GetName());
		Chain.Add(MakeShareable(new FJsonValueObject(Entry)));
		if (UMaterialInstance* MI = Cast<UMaterialInstance>(Current))
		{
			Current = MI->Parent;
		}
		else
		{
			break;
		}
	}
	return Chain;
}
