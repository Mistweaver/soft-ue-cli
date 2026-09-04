// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Niagara/NiagaraStackSerializer.h"

#include "Algo/Reverse.h"
#include "Curves/RichCurve.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraphNode.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraDataInterface.h"
#include "NiagaraEffectType.h"
#include "NiagaraNodeInput.h"
#include "UObject/UnrealType.h"
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

		TArray<TSharedPtr<FJsonValue>> SerializeModuleInputs(
			const UNiagaraNodeFunctionCall* ModuleNode,
			const FNiagaraInspectOptions& Options,
			int32 Depth);

		TArray<TSharedPtr<FJsonValue>> SerializeStack(
			UNiagaraNodeOutput* OutputNode,
			const FNiagaraInspectOptions& Options)
		{
			TArray<UNiagaraNodeFunctionCall*> Modules;
			CollectStackModules(OutputNode, Modules);

			TArray<TSharedPtr<FJsonValue>> ModulesJson;
			for (const UNiagaraNodeFunctionCall* Module : Modules)
			{
				if (!Module || (!Options.bIncludeDisabledModules && !Module->IsNodeEnabled()))
				{
					continue;
				}
				TSharedPtr<FJsonObject> ModuleJson = SerializeModule(Module);
				if (Options.bIncludeModuleInputs || Options.bIncludeCurves)
				{
					ModuleJson->SetArrayField(TEXT("inputs"), SerializeModuleInputs(Module, Options, 0));
				}
				ModulesJson.Add(MakeShared<FJsonValueObject>(ModuleJson));
			}
			return ModulesJson;
		}

		// The override and linked-value nodes live in NiagaraEditor/Private and cannot be included
		// from another plugin, so they are recognised by class name and read through UEdGraphPin.
		// The pins are the whole interface needed here, and these class names are stable.
		const TCHAR* ParameterMapSetClassName = TEXT("NiagaraNodeParameterMapSet");
		const TCHAR* ParameterMapGetClassName = TEXT("NiagaraNodeParameterMapGet");

		bool IsNodeOfClass(const UEdGraphNode* Node, const TCHAR* ClassName)
		{
			return Node != nullptr && Node->GetClass()->GetName() == ClassName;
		}

		/** Reads a property by name regardless of C++ access; several Niagara members are private. */
		UObject* GetObjectPropertyByName(const UObject* Owner, const TCHAR* PropertyName)
		{
			if (!Owner)
			{
				return nullptr;
			}
			const FObjectPropertyBase* Property =
				FindFProperty<FObjectPropertyBase>(Owner->GetClass(), PropertyName);
			return Property ? Property->GetObjectPropertyValue_InContainer(Owner) : nullptr;
		}

		/** Every FRichCurve on an object, by property name, as (time, value, interp) key arrays. */
		TSharedPtr<FJsonObject> SerializeCurvesOn(const UObject* Object)
		{
			TSharedPtr<FJsonObject> CurvesJson = MakeShared<FJsonObject>();
			if (!Object)
			{
				return CurvesJson;
			}

			for (TFieldIterator<FStructProperty> It(Object->GetClass()); It; ++It)
			{
				if (It->Struct != FRichCurve::StaticStruct())
				{
					continue;
				}
				// The cooked editor cache duplicates the authored curve; reporting both would double
				// every payload and invite a reader to edit the copy that is regenerated.
				const FString PropertyName = It->GetName();
				if (PropertyName.Contains(TEXT("CookedEditorCache")))
				{
					continue;
				}

				const FRichCurve* Curve = It->ContainerPtrToValuePtr<FRichCurve>(Object);
				if (!Curve)
				{
					continue;
				}

				TArray<TSharedPtr<FJsonValue>> KeysJson;
				for (const FRichCurveKey& Key : Curve->GetConstRefOfKeys())
				{
					TSharedPtr<FJsonObject> KeyJson = MakeShared<FJsonObject>();
					KeyJson->SetNumberField(TEXT("time"), Key.Time);
					KeyJson->SetNumberField(TEXT("value"), Key.Value);
					// InterpMode is a TEnumAsByte; StaticEnum<> has no specialisation for the wrapper,
					// so unwrap it to the underlying enum before reflecting on it.
					KeyJson->SetStringField(TEXT("interp"), EnumToString(Key.InterpMode.GetValue()));
					KeysJson.Add(MakeShared<FJsonValueObject>(KeyJson));
				}
				CurvesJson->SetArrayField(PropertyName, KeysJson);
			}

			return CurvesJson;
		}

		/**
		 * The override node for a module: the parameter-map set node immediately upstream of it on
		 * the stack chain. Its input pins, named "<ModuleName>.<Input>", carry every value the
		 * author actually set. An input with no pin here is still at the module's own default.
		 */
		const UEdGraphNode* FindOverrideNode(const UNiagaraNodeFunctionCall* ModuleNode)
		{
			if (!ModuleNode)
			{
				return nullptr;
			}
			for (const UEdGraphPin* Pin : ModuleNode->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Input || !IsParameterMapPin(Pin) || Pin->LinkedTo.Num() == 0)
				{
					continue;
				}
				const UEdGraphPin* UpstreamPin = Pin->LinkedTo[0];
				UEdGraphNode* Upstream = UpstreamPin ? UpstreamPin->GetOwningNodeUnchecked() : nullptr;
				return IsNodeOfClass(Upstream, ParameterMapSetClassName) ? Upstream : nullptr;
			}
			return nullptr;
		}

		TArray<TSharedPtr<FJsonValue>> SerializeModuleInputs(
			const UNiagaraNodeFunctionCall* ModuleNode,
			const FNiagaraInspectOptions& Options,
			int32 Depth);

		/**
		 * A pin's default as something a reader can act on. Niagara's static-switch enums are
		 * user-defined, so their entries are stored under autogenerated names -- a raw default of
		 * "NewEnumerator1" says nothing about whether Life Cycle Mode is Self or System.
		 */
		FString DescribePinDefault(const UEdGraphPin* Pin)
		{
			const FString RawValue = Pin->DefaultValue;
			const UEnum* Enum = Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get());
			if (!Enum || RawValue.IsEmpty())
			{
				return RawValue;
			}

			const int32 EnumIndex = Enum->GetIndexByNameString(RawValue);
			if (EnumIndex == INDEX_NONE)
			{
				return RawValue;
			}
			const FString DisplayName = Enum->GetDisplayNameTextByIndex(EnumIndex).ToString();
			return DisplayName.IsEmpty() ? RawValue : DisplayName;
		}

		/** Describes what an override pin's value resolves to, following one link. */
		void DescribeOverrideSource(
			const UEdGraphPin* OverridePin,
			const FNiagaraInspectOptions& Options,
			int32 Depth,
			const TSharedRef<FJsonObject>& OutJson)
		{
			if (OverridePin->LinkedTo.Num() == 0)
			{
				// No link: the pin holds the value inline. This is the common "typed a number" case.
				OutJson->SetStringField(TEXT("source"), InputSourceKindToString(EInputSourceKind::Literal));
				OutJson->SetStringField(TEXT("value"), DescribePinDefault(OverridePin));
				return;
			}

			const UEdGraphPin* SourcePin = OverridePin->LinkedTo[0];
			UEdGraphNode* SourceNode = SourcePin ? SourcePin->GetOwningNodeUnchecked() : nullptr;
			if (!SourceNode)
			{
				OutJson->SetStringField(TEXT("source"), InputSourceKindToString(EInputSourceKind::Unknown));
				return;
			}

			if (IsNodeOfClass(SourceNode, ParameterMapGetClassName))
			{
				// The get node's output pin is named for the parameter it reads, which is exactly the
				// "is this wired to User.PhaseDuration or typed in?" question.
				OutJson->SetStringField(TEXT("source"), InputSourceKindToString(EInputSourceKind::Linked));
				OutJson->SetStringField(TEXT("linked_parameter"), SourcePin->PinName.ToString());
				return;
			}

			if (const UNiagaraNodeFunctionCall* DynamicInput = Cast<UNiagaraNodeFunctionCall>(SourceNode))
			{
				OutJson->SetStringField(TEXT("source"), InputSourceKindToString(EInputSourceKind::DynamicInput));
				OutJson->SetStringField(TEXT("dynamic_input"), DynamicInput->GetFunctionName());
				if (const UNiagaraScript* Script = DynamicInput->FunctionScript)
				{
					OutJson->SetStringField(TEXT("dynamic_input_script"), Script->GetPathName());
				}
				if (Depth < Options.MaxDynamicInputDepth)
				{
					OutJson->SetArrayField(
						TEXT("inputs"), SerializeModuleInputs(DynamicInput, Options, Depth + 1));
				}
				else
				{
					OutJson->SetBoolField(TEXT("depth_limited"), true);
				}
				return;
			}

			if (const UNiagaraNodeInput* InputNode = Cast<UNiagaraNodeInput>(SourceNode))
			{
				OutJson->SetStringField(TEXT("source"), InputSourceKindToString(EInputSourceKind::DataInterface));
				OutJson->SetStringField(TEXT("input_name"), InputNode->Input.GetName().ToString());

				// UNiagaraNodeInput::GetDataInterface() is declared without NIAGARAEDITOR_API, so the
				// object is read reflectively rather than through the accessor.
				if (const UObject* DataInterface = GetObjectPropertyByName(InputNode, TEXT("DataInterface")))
				{
					OutJson->SetStringField(TEXT("data_interface_class"), DataInterface->GetClass()->GetName());
					OutJson->SetStringField(TEXT("data_interface"), DataInterface->GetPathName());
					if (Options.bIncludeCurves)
					{
						const TSharedPtr<FJsonObject> Curves = SerializeCurvesOn(DataInterface);
						if (Curves->Values.Num() > 0)
						{
							OutJson->SetObjectField(TEXT("curves"), Curves);
						}
					}
				}
				return;
			}

			// UNiagaraNodeCustomHlsl and anything else the graph allows here.
			OutJson->SetStringField(
				TEXT("source"),
				SourceNode->GetClass()->GetName().Contains(TEXT("CustomHlsl"))
					? InputSourceKindToString(EInputSourceKind::Expression)
					: InputSourceKindToString(EInputSourceKind::Unknown));
			OutJson->SetStringField(TEXT("source_node_class"), SourceNode->GetClass()->GetName());
		}

		TArray<TSharedPtr<FJsonValue>> SerializeModuleInputs(
			const UNiagaraNodeFunctionCall* ModuleNode,
			const FNiagaraInspectOptions& Options,
			int32 Depth)
		{
			TArray<TSharedPtr<FJsonValue>> InputsJson;
			if (!ModuleNode)
			{
				return InputsJson;
			}

			// Overrides are keyed by the aliased handle "<ModuleName>.<Input>".
			const FString OverridePrefix = ModuleNode->GetFunctionName() + TEXT(".");
			TMap<FString, const UEdGraphPin*> OverridesByInput;
			if (const UEdGraphNode* OverrideNode = FindOverrideNode(ModuleNode))
			{
				for (const UEdGraphPin* Pin : OverrideNode->Pins)
				{
					if (!Pin || Pin->Direction != EGPD_Input)
					{
						continue;
					}
					const FString PinName = Pin->PinName.ToString();
					if (PinName.StartsWith(OverridePrefix))
					{
						OverridesByInput.Add(PinName.RightChop(OverridePrefix.Len()), Pin);
					}
				}
			}

			// Iterate the module's own input pins so inputs left at their default are reported too --
			// "not set" is an answer, and an input missing from the payload is not.
			for (const UEdGraphPin* Pin : ModuleNode->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Input || IsParameterMapPin(Pin))
				{
					continue;
				}

				const FString InputName = Pin->PinName.ToString();
				TSharedRef<FJsonObject> InputJson = MakeShared<FJsonObject>();
				InputJson->SetStringField(TEXT("name"), InputName);
				if (const UObject* SubCategory = Pin->PinType.PinSubCategoryObject.Get())
				{
					InputJson->SetStringField(TEXT("type"), SubCategory->GetName());
				}

				if (const UEdGraphPin* const* OverridePin = OverridesByInput.Find(InputName))
				{
					DescribeOverrideSource(*OverridePin, Options, Depth, InputJson);
					OverridesByInput.Remove(InputName);
				}
				else
				{
					InputJson->SetStringField(TEXT("source"), InputSourceKindToString(EInputSourceKind::Default));
					InputJson->SetStringField(TEXT("value"), DescribePinDefault(Pin));
				}

				InputsJson.Add(MakeShared<FJsonValueObject>(InputJson));
			}

			// Anything still in the map is overridden but has no pin on the function call node. A
			// module only pins the inputs its current static switches make relevant, so the values an
			// author actually set often live here and nowhere else -- dropping them reported a
			// configured module as entirely default, which is the opposite of the truth.
			for (const TPair<FString, const UEdGraphPin*>& Remaining : OverridesByInput)
			{
				TSharedRef<FJsonObject> InputJson = MakeShared<FJsonObject>();
				InputJson->SetStringField(TEXT("name"), Remaining.Key);
				if (const UObject* SubCategory = Remaining.Value->PinType.PinSubCategoryObject.Get())
				{
					InputJson->SetStringField(TEXT("type"), SubCategory->GetName());
				}
				InputJson->SetBoolField(TEXT("override_only"), true);
				DescribeOverrideSource(Remaining.Value, Options, Depth, InputJson);
				InputsJson.Add(MakeShared<FJsonValueObject>(InputJson));
			}

			return InputsJson;
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

				// Walked reflectively rather than per renderer class: UNiagaraRendererProperties
				// declares no common accessor for either, the material property is named differently
				// on each subclass, and the binding set differs by renderer type. GetUsedMaterials()
				// is the runtime path and needs a live emitter instance, which an asset read has not
				// got. The material path in particular is the field that says which shader actually
				// consumes the particle attributes this emitter writes.
				TArray<TSharedPtr<FJsonValue>> MaterialsJson;
				TArray<TSharedPtr<FJsonValue>> BindingsJson;
				for (TFieldIterator<FProperty> It(Renderer->GetClass()); It; ++It)
				{
					if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(*It))
					{
						if (ObjectProperty->PropertyClass
							&& ObjectProperty->PropertyClass->IsChildOf(UMaterialInterface::StaticClass()))
						{
							if (const UObject* Material = ObjectProperty->GetObjectPropertyValue_InContainer(Renderer))
							{
								TSharedPtr<FJsonObject> MaterialJson = MakeShared<FJsonObject>();
								MaterialJson->SetStringField(TEXT("property"), It->GetName());
								MaterialJson->SetStringField(TEXT("material"), Material->GetPathName());
								MaterialsJson.Add(MakeShared<FJsonValueObject>(MaterialJson));
							}
						}
						continue;
					}

					const FStructProperty* StructProperty = CastField<FStructProperty>(*It);
					if (!StructProperty || StructProperty->Struct != FNiagaraVariableAttributeBinding::StaticStruct())
					{
						continue;
					}
					const FNiagaraVariableAttributeBinding* Binding =
						StructProperty->ContainerPtrToValuePtr<FNiagaraVariableAttributeBinding>(Renderer);
					if (!Binding)
					{
						continue;
					}
					TSharedPtr<FJsonObject> BindingJson = MakeShared<FJsonObject>();
					BindingJson->SetStringField(TEXT("name"), StructProperty->GetName());
					BindingJson->SetStringField(TEXT("bound_to"), Binding->GetName().ToString());
					BindingsJson.Add(MakeShared<FJsonValueObject>(BindingJson));
				}
				Json->SetArrayField(TEXT("materials"), MaterialsJson);
				Json->SetArrayField(TEXT("bindings"), BindingsJson);

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
					SerializeStack(OutputNode, Options);

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

FString NiagaraStackSerializer::InputSourceKindToString(EInputSourceKind Kind)
{
	switch (Kind)
	{
	case EInputSourceKind::Default:			return TEXT("default");
	case EInputSourceKind::Literal:			return TEXT("literal");
	case EInputSourceKind::Linked:			return TEXT("linked");
	case EInputSourceKind::DynamicInput:	return TEXT("dynamic_input");
	case EInputSourceKind::DataInterface:	return TEXT("data_interface");
	case EInputSourceKind::Expression:		return TEXT("expression");
	default:								return TEXT("unknown");
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
	// Emitted even when unset: an absent key cannot be told apart from a field the tool does
	// not support, and "no effect type" is a real scalability answer.
	const UNiagaraEffectType* EffectType = System->GetEffectType();
	if (EffectType)
	{
		SystemJson->SetStringField(TEXT("effect_type"), EffectType->GetPathName());
	}
	else
	{
		SystemJson->SetField(TEXT("effect_type"), MakeShared<FJsonValueNull>());
	}

	// UNiagaraSystem::GetFixedBounds() returns FixedBounds unconditionally -- it does not consult
	// bFixedBounds. Emitting the box gated only on IsValid therefore reported a stale, disabled box
	// as though it were in force, which is a wrong answer rather than a missing field. The flag is
	// always reported so the box can be interpreted.
	SystemJson->SetBoolField(TEXT("fixed_bounds_enabled"), System->bFixedBounds != 0);
	const FBox FixedBounds = System->GetFixedBounds();
	if (FixedBounds.IsValid != 0)
	{
		SystemJson->SetObjectField(TEXT("fixed_bounds"), Internal::SerializeBox(FixedBounds));
	}

	// Distance culling is the setting most often blamed for "the effect vanishes"; without it an
	// agent cannot tell a culled system from a broken one.
	const FNiagaraSystemScalabilitySettings& Scalability = System->GetScalabilitySettings();
	TSharedRef<FJsonObject> ScalabilityJson = MakeShared<FJsonObject>();
	ScalabilityJson->SetBoolField(TEXT("cull_by_distance"), Scalability.bCullByDistance != 0);
	ScalabilityJson->SetNumberField(TEXT("max_distance"), Scalability.MaxDistance);
	ScalabilityJson->SetBoolField(TEXT("cull_by_max_instance_count"), Scalability.bCullMaxInstanceCount != 0);
	ScalabilityJson->SetNumberField(TEXT("max_instances"), Scalability.MaxInstances);
	ScalabilityJson->SetBoolField(
		TEXT("cull_by_max_system_instance_count"), Scalability.bCullPerSystemMaxInstanceCount != 0);
	ScalabilityJson->SetNumberField(TEXT("max_system_instances"), Scalability.MaxSystemInstances);
	SystemJson->SetObjectField(TEXT("scalability"), ScalabilityJson);

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
					Internal::SerializeStack(OutputNode, Options));
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
