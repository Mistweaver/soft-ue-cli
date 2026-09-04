// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"
#include "NiagaraCommon.h"
#include "NiagaraEmitterHandle.h"

class FJsonObject;
class FJsonValue;
class UNiagaraGraph;
class UNiagaraNodeFunctionCall;
class UNiagaraNodeOutput;
class UNiagaraScript;
class UNiagaraSystem;

/**
 * Pure serialization of a UNiagaraSystem to the JSON shape the Niagara System editor window
 * presents: emitters, their per-stage module stacks, renderers, and the user-exposed parameters.
 *
 * Nothing here calls into FNiagaraStackGraphUtilities. That header declares ~60 functions and
 * exports only 16 of them with NIAGARAEDITOR_API; GetOrderedModuleNodes and the parameter-map pin
 * accessors are among the unexported ones, so naming them links but does not resolve (LNK2019).
 * The stack walk below therefore threads the parameter-map wire itself using UEdGraph APIs, which
 * are ENGINE_API, plus the inline accessors on the Niagara node classes.
 */
namespace NiagaraStackSerializer
{
	struct FNiagaraInspectOptions
	{
		/** When set, only the emitter with this exact name is serialized. */
		FString EmitterFilter;

		/** Walk each script graph for its module stack. The only part of this that touches graphs. */
		bool bIncludeModules = true;

		/** Include each emitter's renderer list. */
		bool bIncludeRenderers = true;

		/** Include the system's user-exposed (User.*) parameters and their default values. */
		bool bIncludeParameters = true;

		/** Include stack modules whose node is disabled. They are still part of what the window shows. */
		bool bIncludeDisabledModules = true;

		/**
		 * Include each module's input values and, for every one, where the value comes from. Off by
		 * default: it walks each module's override node and every dynamic input hanging off it, so
		 * it is the most expensive part of an inspection by a wide margin.
		 */
		bool bIncludeModuleInputs = false;

		/** Include the keys of any curve reached through a module input. Implies bIncludeModuleInputs. */
		bool bIncludeCurves = false;

		/** How far to recurse into nested dynamic inputs before reporting the node and stopping. */
		int32 MaxDynamicInputDepth = 3;
	};

	/**
	 * Where a module input's value comes from. Knowing the value alone is not enough to act on:
	 * a literal can be edited in place, a link has to be followed to its parameter, and a dynamic
	 * input is a whole subtree. An input left at its default has no override pin at all.
	 */
	enum class EInputSourceKind : uint8
	{
		Default,
		Literal,
		Linked,
		DynamicInput,
		DataInterface,
		Expression,
		Unknown,
	};

	FString InputSourceKindToString(EInputSourceKind Kind);

	/**
	 * Stage label matching the Niagara stack's own section headings ("ParticleSpawn",
	 * "EmitterUpdate", ...) rather than the raw ENiagaraScriptUsage name.
	 */
	FString ScriptUsageToStageName(ENiagaraScriptUsage Usage);

	FString SimTargetToString(ENiagaraSimTarget SimTarget);

	/**
	 * "Standard" or "Stateless". A Stateless (Lightweight) emitter is configured by a fixed module
	 * set instead of a script graph, so FNiagaraEmitterHandle::GetEmitterData() returns null for one
	 * by design -- that is not a failure to resolve, and it has no module stack to walk.
	 */
	FString EmitterModeToString(ENiagaraEmitterMode Mode);

	/**
	 * True when the pin carries FNiagaraParameterMap -- the wire every stack module is threaded
	 * onto. Module input pins carrying values (and the dynamic-input function calls hanging off
	 * them) are on other types, which is what keeps the walk from wandering out of the stack.
	 */
	bool IsParameterMapPinType(const FEdGraphPinType& PinType);
	bool IsParameterMapPin(const UEdGraphPin* Pin);

	/** The editor graph backing a Niagara script, or nullptr for a script with no editor source. */
	UNiagaraGraph* GetScriptGraph(const UNiagaraScript* Script);

	/**
	 * The module function-call nodes feeding OutputNode, in execution order.
	 *
	 * Walks backwards from the output node along the parameter-map input pin, collecting every
	 * UNiagaraNodeFunctionCall on the chain, then reverses. Guards against cycles and caps the
	 * chain length, so a malformed graph yields a short list rather than hanging the game thread.
	 */
	void CollectStackModules(UNiagaraNodeOutput* OutputNode, TArray<UNiagaraNodeFunctionCall*>& OutModules);

	/** name, display title, node class, enabled state, and the module script this node calls. */
	TSharedPtr<FJsonObject> SerializeModule(const UNiagaraNodeFunctionCall* ModuleNode);

	TSharedPtr<FJsonObject> SerializeSystem(UNiagaraSystem* System, const FNiagaraInspectOptions& Options);
}
