// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Niagara/NiagaraSystemInspectTool.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "NiagaraSystem.h"
#include "Tools/Niagara/NiagaraStackSerializer.h"

namespace
{
	// `NiagaraSystemInspect`-prefixed to stay unity-blob safe: neighbouring tool .cpp files define
	// their own SchemaProperty/LoadAsset helpers in their own anonymous namespaces, and at file scope
	// those are all the same namespace once UBT concatenates them into one translation unit.

	FBridgeSchemaProperty NiagaraSystemInspectSchemaProperty(
		const FString& Type,
		const FString& Description,
		bool bRequired = false)
	{
		FBridgeSchemaProperty Property;
		Property.Type = Type;
		Property.Description = Description;
		Property.bRequired = bRequired;
		return Property;
	}

	UNiagaraSystem* NiagaraSystemInspectLoadSystem(const FString& AssetPath, FString& OutError)
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
			OutError = FString::Printf(TEXT("niagara-system-inspect: failed to load asset: %s"), *AssetPath);
			return nullptr;
		}

		UNiagaraSystem* System = Cast<UNiagaraSystem>(AssetObject);
		if (!System)
		{
			OutError = FString::Printf(
				TEXT("niagara-system-inspect: asset '%s' is '%s', not a Niagara System. Emitter assets "
					 "(UNiagaraEmitter) are inspected through the system that references them."),
				*AssetPath,
				*AssetObject->GetClass()->GetName());
			return nullptr;
		}
		return System;
	}
}

FString UNiagaraSystemInspectTool::GetToolDescription() const
{
	return TEXT(
		"Read a Niagara System the way its editor window presents it. Returns the system's settings, "
		"every emitter with its sim target and bounds/allocation modes, the ordered module stack for "
		"each script stage (SystemSpawn/SystemUpdate, EmitterSpawn/EmitterUpdate, "
		"ParticleSpawn/ParticleUpdate, simulation stages and event handlers), each emitter's "
		"renderers, and the user-exposed (User.*) parameters with their default values. Reads the "
		"asset, so no editor window has to be open.");
}

TMap<FString, FBridgeSchemaProperty> UNiagaraSystemInspectTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(
		TEXT("asset_path"),
		NiagaraSystemInspectSchemaProperty(
			TEXT("string"), TEXT("Niagara System asset path (e.g., /Game/FX/NS_Fire)"), true));
	Schema.Add(
		TEXT("emitter"),
		NiagaraSystemInspectSchemaProperty(
			TEXT("string"), TEXT("Only inspect the emitter with this exact name")));
	Schema.Add(
		TEXT("include_modules"),
		NiagaraSystemInspectSchemaProperty(
			TEXT("boolean"), TEXT("Include the ordered module stack for each script stage (default true)")));
	Schema.Add(
		TEXT("include_renderers"),
		NiagaraSystemInspectSchemaProperty(
			TEXT("boolean"), TEXT("Include each emitter's renderer list (default true)")));
	Schema.Add(
		TEXT("include_parameters"),
		NiagaraSystemInspectSchemaProperty(
			TEXT("boolean"), TEXT("Include the user-exposed parameters and their values (default true)")));
	Schema.Add(
		TEXT("include_disabled_modules"),
		NiagaraSystemInspectSchemaProperty(
			TEXT("boolean"), TEXT("Include stack modules whose node is disabled (default true)")));
	return Schema;
}

TArray<FString> UNiagaraSystemInspectTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FBridgeToolResult UNiagaraSystemInspectTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	FString AssetPath;
	if (!GetStringArg(Arguments, TEXT("asset_path"), AssetPath))
	{
		return FBridgeToolResult::Error(TEXT("niagara-system-inspect: asset_path is required"));
	}

	NiagaraStackSerializer::FNiagaraInspectOptions Options;
	Options.EmitterFilter = GetStringArgOrDefault(Arguments, TEXT("emitter"), TEXT(""));
	// These default on: an agent asking to read a Niagara System wants what the window shows, and
	// having to pass four flags to get it is a worse default than paying for the graph walk.
	Options.bIncludeModules = GetBoolArgOrDefault(Arguments, TEXT("include_modules"), true);
	Options.bIncludeRenderers = GetBoolArgOrDefault(Arguments, TEXT("include_renderers"), true);
	Options.bIncludeParameters = GetBoolArgOrDefault(Arguments, TEXT("include_parameters"), true);
	Options.bIncludeDisabledModules = GetBoolArgOrDefault(Arguments, TEXT("include_disabled_modules"), true);

	FString Error;
	UNiagaraSystem* System = NiagaraSystemInspectLoadSystem(AssetPath, Error);
	if (!System)
	{
		return FBridgeToolResult::Error(Error);
	}

	TSharedPtr<FJsonObject> Result = NiagaraStackSerializer::SerializeSystem(System, Options);
	if (!Result.IsValid())
	{
		return FBridgeToolResult::Error(
			FString::Printf(TEXT("niagara-system-inspect: failed to serialize '%s'"), *AssetPath));
	}

	// Reported after serialization so an emitter name typo names the emitters that do exist.
	if (!Options.EmitterFilter.IsEmpty())
	{
		const TArray<TSharedPtr<FJsonValue>>* Emitters = nullptr;
		if (Result->TryGetArrayField(TEXT("emitters"), Emitters) && Emitters->Num() == 0)
		{
			return FBridgeToolResult::Error(FString::Printf(
				TEXT("niagara-system-inspect: no emitter named '%s' on '%s'. Run without --emitter to "
					 "list the available emitters."),
				*Options.EmitterFilter,
				*AssetPath));
		}
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), System->GetPathName());
	return FBridgeToolResult::Json(Result);
}
