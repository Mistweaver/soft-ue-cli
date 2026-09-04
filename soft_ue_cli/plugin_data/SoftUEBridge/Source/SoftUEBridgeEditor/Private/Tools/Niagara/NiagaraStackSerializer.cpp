// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Niagara/NiagaraStackSerializer.h"

#include "Algo/Reverse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraphNode.h"
#include "NiagaraDataInterface.h"
#include "NiagaraEffectType.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterBase.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraParameterStore.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSimulationStageBase.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "NiagaraUserRedirectionParameterStore.h"

namespace NiagaraStackSerializer
{
	// Nested inside the named namespace on purpose: an anonymous namespace at file scope is one and
	// the same namespace across every .cpp folded into a unity blob, so a helper named SerializeStack
	// or EnumToString would collide with an identically named helper in a neighbouring tool.
	namespace Internal
	{
		/** Longest module chain to follow before assuming the graph is malformed. */
		constexpr int32 MaxStackChainLength = 4096;

		template <typename TEnum>
		FString EnumToString(TEnum Value)
		{
			if (const UEnum* Enum = StaticEnum<TEnum>())
			{
				const FString Name = Enum->GetNameStringByValue(static_cast<int64>(Value));
				if (!Name.IsEmpty())
				{
					return Name;
				}
			}
			return TEXT("Unknown");
		}

		TArray<UNiagaraNodeOutput*> CollectOutputNodes(UNiagaraGraph* Graph)
		{
			TArray<UNiagaraNodeOutput*> OutputNodes;
			if (!Graph)
			{
				return OutputNodes;
			}
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (UNiagaraNodeOutput* OutputNode = Cast<UNiagaraNodeOutput>(Node))
				{
					OutputNodes.Add(OutputNode);
				}
			}
			return OutputNodes;
		}

		TArray<TSharedPtr<FJsonValue>> SerializeStack(UNiagaraNodeOutput* OutputNode, bool bIncludeDisabled)
		{
			TArray<UNiagaraNodeFunctionCall*> Modules;
			CollectStackModules(OutputNode, Modules);

			TArray<TSharedPtr<FJsonValue>> ModulesJson;
			for (const UNiagaraNodeFunctionCall* Module : Modules)
			{
				if (!Module || (!bIncludeDisabled && !Module->IsNodeEnabled()))
				{
					continue;
				}
				ModulesJson.Add(MakeShared<FJsonValueObject>(SerializeModule(Module)));
			}
			return ModulesJson;
		}

		TSharedPtr<FJsonObject> SerializeBox(const FBox& Box)
		{
			TSharedPtr<FJsonObject> MinJson = MakeShared<FJsonObject>();
			MinJson->SetNumberField(TEXT("x"), Box.Min.X);
			MinJson->SetNumberField(TEXT("y"), Box.Min.Y);
			MinJson->SetNumberField(TEXT("z"), Box.Min.Z);

			TSharedPtr<FJsonObject> MaxJson = MakeShared<FJsonObject>();
			MaxJson->SetNumberField(TEXT("x"), Box.Max.X);
			MaxJson->SetNumberField(TEXT("y"), Box.Max.Y);
			MaxJson->SetNumberField(TEXT("z"), Box.Max.Z);

			TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetObjectField(TEXT("min"), MinJson);
			Json->SetObjectField(TEXT("max"), MaxJson);
			return Json;
		}

		TArray<TSharedPtr<FJsonValue>> SerializeUserParameters(UNiagaraSystem* System)
		{
			TArray<TSharedPtr<FJsonValue>> ParametersJson;

			const FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
			for (const FNiagaraVariableWithOffset& Variable : Store.ReadParameterVariables())
			{
				TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
				Json->SetStringField(TEXT("name"), Variable.GetName().ToString());
				Json->SetStringField(TEXT("type"), Variable.GetType().GetName());

				// Offset indexes a different array per kind: the raw byte blob for plain values, the
				// data-interface array for DIs, the object array for UObject parameters. Reading a DI
				// offset out of the byte blob yields garbage rather than failing, so branch first.
				if (Variable.IsDataInterface())
				{
					Json->SetStringField(TEXT("value_kind"), TEXT("data_interface"));
					const TArray<UNiagaraDataInterface*>& DataInterfaces = Store.GetDataInterfaces();
					if (DataInterfaces.IsValidIndex(Variable.Offset) && DataInterfaces[Variable.Offset])
					{
						Json->SetStringField(TEXT("value"), DataInterfaces[Variable.Offset]->GetPathName());
					}
				}
				else if (Variable.IsUObject())
				{
					Json->SetStringField(TEXT("value_kind"), TEXT("object"));
					const TArray<TObjectPtr<UObject>>& Objects = Store.GetUObjects();
					if (Objects.IsValidIndex(Variable.Offset) && Objects[Variable.Offset])
					{
						Json->SetStringField(TEXT("value"), Objects[Variable.Offset]->GetPathName());
					}
				}
				else
				{
					Json->SetStringField(TEXT("value_kind"), TEXT("value"));
					// FNiagaraTypeDefinition::ToString opens with checkf(IsValid()), so a parameter
					// left behind by a deleted type would take the editor down rather than report.
					if (Variable.GetType().IsValid())
					{
						if (const uint8* Data = Store.GetParameterData(Variable))
						{
							Json->SetStringField(TEXT("value"), Variable.GetType().ToString(Data));
						}
					}
				}

				ParametersJson.Add(MakeShared<FJsonValueObject>(Json));
			}

			return ParametersJson;
		}

		TArray<TSharedPtr<FJsonValue>> SerializeRenderers(const FVersionedNiagaraEmitterData* EmitterData)
		{
			TArray<TSharedPtr<FJsonValue>> RenderersJson;
			for (const UNiagaraRendererProperties* Renderer : EmitterData->GetRenderers())
			{
				if (!Renderer)
				{
					continue;
				}
				TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
				Json->SetStringField(TEXT("class"), Renderer->GetClass()->GetName());
				Json->SetBoolField(TEXT("enabled"), Renderer->GetIsEnabled());
				RenderersJson.Add(MakeShared<FJsonValueObject>(Json));
			}
			return RenderersJson;
		}

		/**
		 * Fills the emitter's per-stage module lists.
		 *
		 * Every script on an emitter -- emitter spawn/update, particle spawn/update, each simulation
		 * stage, each event handler -- shares one UNiagaraGraph and is told apart by its output node's
		 * usage and usage id. So the graph is walked once and its output nodes bucketed, rather than
		 * resolving each script's source separately.
		 */
		void SerializeEmitterStages(
			const FVersionedNiagaraEmitterData* EmitterData,
			const FNiagaraInspectOptions& Options,
			const TSharedRef<FJsonObject>& OutEmitterJson)
		{
			UNiagaraGraph* Graph = GetScriptGraph(EmitterData->SpawnScriptProps.Script);
			if (!Graph)
			{
				return;
			}

			// Simulation-stage output nodes are told apart by usage id, so index the stages by theirs.
			TMap<FGuid, const UNiagaraSimulationStageBase*> StagesByUsageId;
			for (const UNiagaraSimulationStageBase* Stage : EmitterData->GetSimulationStages())
			{
				if (Stage && Stage->Script)
				{
					StagesByUsageId.Add(Stage->Script->GetUsageId(), Stage);
				}
			}

			TSharedRef<FJsonObject> StagesJson = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> SimulationStagesJson;
			TArray<TSharedPtr<FJsonValue>> EventHandlersJson;

			for (UNiagaraNodeOutput* OutputNode : CollectOutputNodes(Graph))
			{
				const ENiagaraScriptUsage Usage = OutputNode->GetUsage();
				// The GPU compute script mirrors the particle scripts it was built from and has no
				// stack of its own, so walking it would only duplicate ParticleSpawn/ParticleUpdate.
				if (Usage == ENiagaraScriptUsage::ParticleGPUComputeScript)
				{
					continue;
				}

				TArray<TSharedPtr<FJsonValue>> ModulesJson =
					SerializeStack(OutputNode, Options.bIncludeDisabledModules);

				switch (Usage)
				{
				case ENiagaraScriptUsage::ParticleSimulationStageScript:
				{
					const UNiagaraSimulationStageBase* const* Stage = StagesByUsageId.Find(OutputNode->GetUsageId());
					TSharedPtr<FJsonObject> StageJson = MakeShared<FJsonObject>();
					StageJson->SetStringField(
						TEXT("name"),
						(Stage && *Stage) ? (*Stage)->SimulationStageName.ToString() : FString());
					StageJson->SetBoolField(TEXT("enabled"), (Stage && *Stage) ? (*Stage)->bEnabled != 0 : false);
					StageJson->SetStringField(TEXT("usage_id"), OutputNode->GetUsageId().ToString());
					StageJson->SetArrayField(TEXT("modules"), ModulesJson);
					SimulationStagesJson.Add(MakeShared<FJsonValueObject>(StageJson));
					break;
				}
				case ENiagaraScriptUsage::ParticleEventScript:
				{
					TSharedPtr<FJsonObject> EventJson = MakeShared<FJsonObject>();
					EventJson->SetStringField(TEXT("usage_id"), OutputNode->GetUsageId().ToString());
					EventJson->SetArrayField(TEXT("modules"), ModulesJson);
					EventHandlersJson.Add(MakeShared<FJsonValueObject>(EventJson));
					break;
				}
				default:
					StagesJson->SetArrayField(ScriptUsageToStageName(Usage), ModulesJson);
					break;
				}
			}

			OutEmitterJson->SetObjectField(TEXT("stages"), StagesJson);
			if (SimulationStagesJson.Num() > 0)
			{
				OutEmitterJson->SetArrayField(TEXT("simulation_stages"), SimulationStagesJson);
			}
			if (EventHandlersJson.Num() > 0)
			{
				OutEmitterJson->SetArrayField(TEXT("event_handlers"), EventHandlersJson);
			}
		}

		TSharedPtr<FJsonObject> SerializeEmitter(
			const FNiagaraEmitterHandle& Handle,
			const FNiagaraInspectOptions& Options,
			TArray<FString>& OutWarnings)
		{
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			const FString EmitterName = Handle.GetName().ToString();
			Json->SetStringField(TEXT("name"), EmitterName);
			Json->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());

			const ENiagaraEmitterMode EmitterMode = Handle.GetEmitterMode();
			Json->SetStringField(TEXT("emitter_mode"), EmitterModeToString(EmitterMode));

			// GetEmitterBase() resolves for both modes, unlike GetEmitterData(), so the emitter asset
			// is named even for a Lightweight emitter that has no FVersionedNiagaraEmitterData.
			if (const UNiagaraEmitterBase* EmitterBase = Handle.GetEmitterBase())
			{
				Json->SetStringField(TEXT("emitter_asset"), EmitterBase->GetPathName());
				Json->SetStringField(TEXT("emitter_class"), EmitterBase->GetClass()->GetName());
			}

			if (EmitterMode == ENiagaraEmitterMode::Stateless)
			{
				// FNiagaraEmitterHandle::GetEmitterData() returns null for a Stateless emitter by
				// design -- it is configured by a fixed module set rather than a script graph. There
				// is no stack to walk, and reporting that as a resolve failure would send a reader
				// looking for a missing emitter version that was never missing.
				Json->SetBoolField(TEXT("resolved"), true);
				Json->SetBoolField(TEXT("has_module_stack"), false);
				return Json;
			}

			const FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
			if (!EmitterData)
			{
				// A handle whose emitter version failed to resolve still appears in the window, so it
				// is reported rather than dropped -- an agent seeing a missing emitter is the point.
				Json->SetBoolField(TEXT("resolved"), false);
				Json->SetBoolField(TEXT("has_module_stack"), false);
				OutWarnings.Add(FString::Printf(
					TEXT("Emitter '%s' is a Standard emitter with no resolved emitter data; its "
						 "version may be missing."),
					*EmitterName));
				return Json;
			}

			Json->SetBoolField(TEXT("resolved"), true);
			Json->SetBoolField(TEXT("has_module_stack"), true);
			Json->SetStringField(TEXT("sim_target"), SimTargetToString(EmitterData->SimTarget));
			Json->SetBoolField(TEXT("local_space"), EmitterData->bLocalSpace);
			Json->SetBoolField(TEXT("determinism"), EmitterData->bDeterminism);
			Json->SetNumberField(TEXT("random_seed"), EmitterData->RandomSeed);
			Json->SetStringField(TEXT("bounds_mode"), EnumToString(EmitterData->CalculateBoundsMode));
			Json->SetStringField(TEXT("allocation_mode"), EnumToString(EmitterData->AllocationMode));
			Json->SetBoolField(TEXT("requires_persistent_ids"), EmitterData->bRequiresPersistentIDs != 0);

			if (EmitterData->CalculateBoundsMode == ENiagaraEmitterCalculateBoundMode::Fixed)
			{
				Json->SetObjectField(TEXT("fixed_bounds"), SerializeBox(EmitterData->FixedBounds));
			}

			if (Options.bIncludeRenderers)
			{
				Json->SetArrayField(TEXT("renderers"), SerializeRenderers(EmitterData));
			}
			if (Options.bIncludeModules)
			{
				SerializeEmitterStages(EmitterData, Options, Json);
			}

			return Json;
		}
	}
}

FString NiagaraStackSerializer::ScriptUsageToStageName(ENiagaraScriptUsage Usage)
{
	switch (Usage)
	{
	case ENiagaraScriptUsage::SystemSpawnScript:				return TEXT("SystemSpawn");
	case ENiagaraScriptUsage::SystemUpdateScript:				return TEXT("SystemUpdate");
	case ENiagaraScriptUsage::EmitterSpawnScript:				return TEXT("EmitterSpawn");
	case ENiagaraScriptUsage::EmitterUpdateScript:				return TEXT("EmitterUpdate");
	// Interpolated spawn is the same ParticleSpawn section in the stack; it differs only in also
	// pulling in the update script, which the window does not show as a separate group.
	case ENiagaraScriptUsage::ParticleSpawnScript:				return TEXT("ParticleSpawn");
	case ENiagaraScriptUsage::ParticleSpawnScriptInterpolated:	return TEXT("ParticleSpawn");
	case ENiagaraScriptUsage::ParticleUpdateScript:				return TEXT("ParticleUpdate");
	case ENiagaraScriptUsage::ParticleEventScript:				return TEXT("ParticleEvent");
	case ENiagaraScriptUsage::ParticleSimulationStageScript:		return TEXT("SimulationStage");
	case ENiagaraScriptUsage::ParticleGPUComputeScript:			return TEXT("ParticleGPUCompute");
	case ENiagaraScriptUsage::Module:							return TEXT("Module");
	case ENiagaraScriptUsage::DynamicInput:						return TEXT("DynamicInput");
	case ENiagaraScriptUsage::Function:							return TEXT("Function");
	default:													return TEXT("Unknown");
	}
}

FString NiagaraStackSerializer::SimTargetToString(ENiagaraSimTarget SimTarget)
{
	switch (SimTarget)
	{
	case ENiagaraSimTarget::CPUSim:			return TEXT("CPUSim");
	case ENiagaraSimTarget::GPUComputeSim:	return TEXT("GPUComputeSim");
	default:								return TEXT("Unknown");
	}
}

FString NiagaraStackSerializer::EmitterModeToString(ENiagaraEmitterMode Mode)
{
	switch (Mode)
	{
	case ENiagaraEmitterMode::Standard:		return TEXT("Standard");
	case ENiagaraEmitterMode::Stateless:	return TEXT("Stateless");
	default:								return TEXT("Unknown");
	}
}

bool NiagaraStackSerializer::IsParameterMapPinType(const FEdGraphPinType& PinType)
{
	return PinType.PinSubCategoryObject.Get() == FNiagaraParameterMap::StaticStruct();
}

bool NiagaraStackSerializer::IsParameterMapPin(const UEdGraphPin* Pin)
{
	return Pin != nullptr && IsParameterMapPinType(Pin->PinType);
}

UNiagaraGraph* NiagaraStackSerializer::GetScriptGraph(const UNiagaraScript* Script)
{
	if (!Script)
	{
		return nullptr;
	}
	const UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
	return Source ? Source->NodeGraph : nullptr;
}

void NiagaraStackSerializer::CollectStackModules(
	UNiagaraNodeOutput* OutputNode,
	TArray<UNiagaraNodeFunctionCall*>& OutModules)
{
	OutModules.Reset();
	if (!OutputNode)
	{
		return;
	}

	// The stack is a linear chain threaded onto the parameter map, running from the first module up
	// into the output node. Walking it backwards from the output collects the modules in reverse
	// execution order; the reverse at the end restores the order the window lists them in.
	TSet<const UEdGraphNode*> Visited;
	const UEdGraphNode* Current = OutputNode;

	for (int32 Step = 0; Step < Internal::MaxStackChainLength && Current != nullptr; ++Step)
	{
		bool bAlreadyVisited = false;
		Visited.Add(Current, &bAlreadyVisited);
		if (bAlreadyVisited)
		{
			break;
		}

		const UEdGraphPin* MapInputPin = nullptr;
		for (const UEdGraphPin* Pin : Current->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input && IsParameterMapPin(Pin))
			{
				MapInputPin = Pin;
				break;
			}
		}
		if (!MapInputPin || MapInputPin->LinkedTo.Num() == 0)
		{
			break;
		}

		const UEdGraphPin* UpstreamPin = MapInputPin->LinkedTo[0];
		UEdGraphNode* UpstreamNode = UpstreamPin ? UpstreamPin->GetOwningNodeUnchecked() : nullptr;
		if (!UpstreamNode)
		{
			break;
		}

		if (UNiagaraNodeFunctionCall* ModuleNode = Cast<UNiagaraNodeFunctionCall>(UpstreamNode))
		{
			OutModules.Add(ModuleNode);
		}
		Current = UpstreamNode;
	}

	Algo::Reverse(OutModules);
}

TSharedPtr<FJsonObject> NiagaraStackSerializer::SerializeModule(const UNiagaraNodeFunctionCall* ModuleNode)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	if (!ModuleNode)
	{
		return Json;
	}

	Json->SetStringField(TEXT("name"), ModuleNode->GetFunctionName());
	Json->SetStringField(TEXT("title"), ModuleNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
	// UNiagaraNodeAssignment -- the stack's "Set Parameter" items -- also lands here; the class name
	// is what tells one apart from a plain module.
	Json->SetStringField(TEXT("node_class"), ModuleNode->GetClass()->GetName());
	Json->SetBoolField(TEXT("enabled"), ModuleNode->IsNodeEnabled());
	Json->SetStringField(TEXT("node_guid"), ModuleNode->NodeGuid.ToString());

	if (const UNiagaraScript* FunctionScript = ModuleNode->FunctionScript)
	{
		Json->SetStringField(TEXT("script"), FunctionScript->GetPathName());
	}

	return Json;
}

TSharedPtr<FJsonObject> NiagaraStackSerializer::SerializeSystem(
	UNiagaraSystem* System,
	const FNiagaraInspectOptions& Options)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	if (!System)
	{
		return Result;
	}

	TArray<FString> Warnings;

	TSharedRef<FJsonObject> SystemJson = MakeShared<FJsonObject>();
	SystemJson->SetStringField(TEXT("name"), System->GetName());
	SystemJson->SetNumberField(TEXT("warmup_time"), System->GetWarmupTime());
	SystemJson->SetNumberField(TEXT("warmup_tick_count"), System->GetWarmupTickCount());
	SystemJson->SetNumberField(TEXT("warmup_tick_delta"), System->GetWarmupTickDelta());
	if (const UNiagaraEffectType* EffectType = System->GetEffectType())
	{
		SystemJson->SetStringField(TEXT("effect_type"), EffectType->GetPathName());
	}

	const FBox FixedBounds = System->GetFixedBounds();
	if (FixedBounds.IsValid != 0)
	{
		SystemJson->SetObjectField(TEXT("fixed_bounds"), Internal::SerializeBox(FixedBounds));
	}

	const TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
	SystemJson->SetNumberField(TEXT("emitter_count"), EmitterHandles.Num());
	Result->SetObjectField(TEXT("system"), SystemJson);

	if (Options.bIncludeModules)
	{
		// System spawn and update share one graph, told apart by their output nodes' usage.
		TSharedRef<FJsonObject> SystemStackJson = MakeShared<FJsonObject>();
		UNiagaraGraph* SystemGraph = GetScriptGraph(System->GetSystemSpawnScript());
		for (UNiagaraNodeOutput* OutputNode : Internal::CollectOutputNodes(SystemGraph))
		{
			const ENiagaraScriptUsage Usage = OutputNode->GetUsage();
			if (Usage == ENiagaraScriptUsage::SystemSpawnScript || Usage == ENiagaraScriptUsage::SystemUpdateScript)
			{
				SystemStackJson->SetArrayField(
					ScriptUsageToStageName(Usage),
					Internal::SerializeStack(OutputNode, Options.bIncludeDisabledModules));
			}
		}
		Result->SetObjectField(TEXT("system_stack"), SystemStackJson);
	}

	TArray<TSharedPtr<FJsonValue>> EmittersJson;
	TArray<TSharedPtr<FJsonValue>> AvailableEmitterNames;
	for (const FNiagaraEmitterHandle& Handle : EmitterHandles)
	{
		const FString EmitterName = Handle.GetName().ToString();
		AvailableEmitterNames.Add(MakeShared<FJsonValueString>(EmitterName));

		if (!Options.EmitterFilter.IsEmpty() && EmitterName != Options.EmitterFilter)
		{
			continue;
		}
		EmittersJson.Add(MakeShared<FJsonValueObject>(Internal::SerializeEmitter(Handle, Options, Warnings)));
	}

	Result->SetArrayField(TEXT("available_emitters"), AvailableEmitterNames);
	Result->SetArrayField(TEXT("emitters"), EmittersJson);

	if (Options.bIncludeParameters)
	{
		Result->SetArrayField(TEXT("user_parameters"), Internal::SerializeUserParameters(System));
	}

	if (Warnings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> WarningsJson;
		for (const FString& Warning : Warnings)
		{
			WarningsJson.Add(MakeShared<FJsonValueString>(Warning));
		}
		Result->SetArrayField(TEXT("warnings"), WarningsJson);
	}

	return Result;
}
