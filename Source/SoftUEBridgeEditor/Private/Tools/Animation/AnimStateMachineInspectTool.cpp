// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Animation/AnimStateMachineInspectTool.h"

#include "Utils/BridgeAssetModifier.h"

#include "AlphaBlend.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimStateMachineTypes.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationTransitionGraph.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimStateAliasNode.h"
#include "AnimStateConduitNode.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateNodeBase.h"
#include "AnimStateTransitionNode.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Runtime/Launch/Resources/Version.h"

namespace
{
// Rule graphs are small. The cap keeps a malformed or cyclic graph from producing huge output.
constexpr int32 SmInspectMaxRuleNodes = 64;

FBridgeSchemaProperty SmInspectSchemaProperty(const FString& Type, const FString& Description, bool bRequired = false)
{
	FBridgeSchemaProperty Property;
	Property.Type = Type;
	Property.Description = Description;
	Property.bRequired = bRequired;
	return Property;
}

FString SmInspectGuid(const UEdGraphNode* Node)
{
	return Node ? Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens) : TEXT("");
}

FString SmInspectNodeTitle(const UEdGraphNode* Node)
{
	if (!Node)
	{
		return TEXT("");
	}
	FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
	Title.ReplaceInline(TEXT("\r"), TEXT(""));
	Title.ReplaceInline(TEXT("\n"), TEXT(" "));
	return Title.TrimStartAndEnd();
}

TArray<TSharedPtr<FJsonValue>> SmInspectPosition(const UEdGraphNode* Node)
{
	TArray<TSharedPtr<FJsonValue>> Position;
	Position.Add(MakeShared<FJsonValueNumber>(Node ? Node->NodePosX : 0));
	Position.Add(MakeShared<FJsonValueNumber>(Node ? Node->NodePosY : 0));
	return Position;
}

FString SmInspectStateKind(const UAnimStateNodeBase* Node)
{
	if (Cast<UAnimStateConduitNode>(Node))
	{
		return TEXT("conduit");
	}
	if (Cast<UAnimStateAliasNode>(Node))
	{
		return TEXT("alias");
	}
	return TEXT("state");
}

FString SmInspectBlendMode(EAlphaBlendOption BlendMode)
{
	const UEnum* Enum = StaticEnum<EAlphaBlendOption>();
	return Enum ? Enum->GetNameStringByValue(static_cast<int64>(BlendMode)) : TEXT("");
}

FString SmInspectLogicType(ETransitionLogicType::Type LogicType)
{
	switch (LogicType)
	{
	case ETransitionLogicType::TLT_StandardBlend:
		return TEXT("standard_blend");
	case ETransitionLogicType::TLT_Inertialization:
		return TEXT("inertialization");
	case ETransitionLogicType::TLT_Custom:
		return TEXT("custom");
	default:
		return TEXT("unknown");
	}
}

TSharedPtr<FJsonObject> SmInspectRuleNodeToJson(const UEdGraphNode* Node)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("node_guid"), SmInspectGuid(Node));
	Json->SetStringField(TEXT("class"), Node->GetClass()->GetName());
	Json->SetStringField(TEXT("title"), SmInspectNodeTitle(Node));

	// Unconnected input defaults make comparisons readable, e.g. "Speed > 10".
	TSharedPtr<FJsonObject> LiteralInputs = MakeShared<FJsonObject>();
	for (const UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->Direction != EGPD_Input || Pin->bHidden || Pin->LinkedTo.Num() > 0 || Pin->PinType.PinCategory == TEXT("exec"))
		{
			continue;
		}
		const FString DefaultValue = Pin->GetDefaultAsString();
		if (!DefaultValue.IsEmpty())
		{
			LiteralInputs->SetStringField(Pin->PinName.ToString(), DefaultValue);
		}
	}
	Json->SetObjectField(TEXT("literal_inputs"), LiteralInputs);
	return Json;
}

/** Summarizes the bCanEnterTransition result of a transition or conduit rule graph. */
TSharedPtr<FJsonObject> SmInspectRuleGraphToJson(UEdGraph* Graph)
{
	TSharedPtr<FJsonObject> Rule = MakeShared<FJsonObject>();
	Rule->SetStringField(TEXT("rule_graph"), Graph ? Graph->GetName() : TEXT(""));

	UAnimationTransitionGraph* TransitionGraph = Cast<UAnimationTransitionGraph>(Graph);
	UAnimGraphNode_TransitionResult* ResultNode = TransitionGraph ? TransitionGraph->GetResultNode() : nullptr;
	UEdGraphPin* ResultPin = ResultNode ? ResultNode->FindPin(TEXT("bCanEnterTransition")) : nullptr;
	if (!ResultPin)
	{
		Rule->SetStringField(TEXT("kind"), TEXT("unknown"));
		return Rule;
	}

	if (ResultPin->LinkedTo.Num() == 0)
	{
		Rule->SetStringField(TEXT("kind"), TEXT("literal"));
		Rule->SetBoolField(TEXT("value"), ResultPin->DefaultValue.Equals(TEXT("true"), ESearchCase::IgnoreCase));
		return Rule;
	}

	Rule->SetStringField(TEXT("kind"), TEXT("graph"));

	// Walk upstream from the result pin, nearest nodes first.
	TArray<const UEdGraphNode*> Queue;
	TSet<const UEdGraphNode*> Visited;
	for (const UEdGraphPin* Linked : ResultPin->LinkedTo)
	{
		if (Linked && Linked->GetOwningNode() && !Visited.Contains(Linked->GetOwningNode()))
		{
			Visited.Add(Linked->GetOwningNode());
			Queue.Add(Linked->GetOwningNode());
		}
	}

	TArray<TSharedPtr<FJsonValue>> NodesJson;
	bool bTruncated = false;
	for (int32 Index = 0; Index < Queue.Num(); ++Index)
	{
		if (NodesJson.Num() >= SmInspectMaxRuleNodes)
		{
			bTruncated = true;
			break;
		}
		const UEdGraphNode* Node = Queue[Index];
		NodesJson.Add(MakeShared<FJsonValueObject>(SmInspectRuleNodeToJson(Node)));

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input)
			{
				continue;
			}
			for (const UEdGraphPin* Linked : Pin->LinkedTo)
			{
				const UEdGraphNode* Upstream = Linked ? Linked->GetOwningNode() : nullptr;
				if (Upstream && !Visited.Contains(Upstream))
				{
					Visited.Add(Upstream);
					Queue.Add(Upstream);
				}
			}
		}
	}

	Rule->SetStringField(TEXT("result_input"), NodesJson.Num() > 0 ? SmInspectNodeTitle(Queue[0]) : TEXT(""));
	Rule->SetArrayField(TEXT("nodes"), NodesJson);
	Rule->SetBoolField(TEXT("truncated"), bTruncated);
	return Rule;
}

TSharedPtr<FJsonObject> SmInspectStateToJson(UAnimStateNodeBase* Node, const UAnimStateNodeBase* EntryState)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("name"), Node->GetStateName());
	Json->SetStringField(TEXT("kind"), SmInspectStateKind(Node));
	Json->SetStringField(TEXT("node_guid"), SmInspectGuid(Node));
	Json->SetBoolField(TEXT("is_entry"), Node == EntryState);
	Json->SetArrayField(TEXT("position"), SmInspectPosition(Node));

	if (UAnimStateNode* State = Cast<UAnimStateNode>(Node))
	{
		Json->SetStringField(TEXT("state_graph"), State->BoundGraph ? State->BoundGraph->GetName() : TEXT(""));
		Json->SetBoolField(TEXT("always_reset_on_entry"), State->bAlwaysResetOnEntry);
	}
	else if (UAnimStateConduitNode* Conduit = Cast<UAnimStateConduitNode>(Node))
	{
		Json->SetObjectField(TEXT("rule"), SmInspectRuleGraphToJson(Conduit->BoundGraph));
	}
	else if (UAnimStateAliasNode* Alias = Cast<UAnimStateAliasNode>(Node))
	{
		Json->SetBoolField(TEXT("global_alias"), Alias->bGlobalAlias);
		TArray<TSharedPtr<FJsonValue>> Aliased;
		for (const TWeakObjectPtr<UAnimStateNodeBase>& AliasedState : Alias->GetAliasedStates())
		{
			if (AliasedState.IsValid())
			{
				Aliased.Add(MakeShared<FJsonValueString>(AliasedState->GetStateName()));
			}
		}
		Json->SetArrayField(TEXT("aliased_states"), Aliased);
	}
	return Json;
}

TSharedPtr<FJsonObject> SmInspectTransitionToJson(UAnimStateTransitionNode* Transition, int32 Index)
{
	const UAnimStateNodeBase* Source = Transition->GetPreviousState();
	const UAnimStateNodeBase* Target = Transition->GetNextState();

	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetNumberField(TEXT("index"), Index);
	Json->SetStringField(TEXT("source_state"), Source ? Source->GetStateName() : TEXT(""));
	Json->SetStringField(TEXT("target_state"), Target ? Target->GetStateName() : TEXT(""));
	Json->SetStringField(TEXT("node_guid"), SmInspectGuid(Transition));
	Json->SetStringField(TEXT("transition_graph"), Transition->BoundGraph ? Transition->BoundGraph->GetName() : TEXT(""));
	Json->SetNumberField(TEXT("crossfade_duration"), Transition->CrossfadeDuration);
	Json->SetStringField(TEXT("blend_mode"), SmInspectBlendMode(Transition->BlendMode));
	Json->SetStringField(TEXT("logic_type"), SmInspectLogicType(Transition->LogicType));
	Json->SetNumberField(TEXT("priority"), Transition->PriorityOrder);
	Json->SetBoolField(TEXT("bidirectional"), Transition->Bidirectional);
	Json->SetBoolField(TEXT("disabled"), Transition->bDisabled);
	Json->SetNumberField(TEXT("min_time_before_reentry"), Transition->MinTimeBeforeReentry);
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
	Json->SetBoolField(TEXT("only_evaluate_when_active"), Transition->bOnlyEvaluateWhenActive);
#endif
	if (Transition->LogicType == ETransitionLogicType::TLT_Custom && Transition->CustomTransitionGraph)
	{
		Json->SetStringField(TEXT("custom_blend_graph"), Transition->CustomTransitionGraph->GetName());
	}

	TSharedPtr<FJsonObject> Rule;
	if (Transition->bAutomaticRuleBasedOnSequencePlayerInState)
	{
		// The engine ignores the rule graph when the automatic rule is on.
		Rule = MakeShared<FJsonObject>();
		Rule->SetStringField(TEXT("kind"), TEXT("automatic"));
		Rule->SetNumberField(TEXT("trigger_time"), Transition->AutomaticRuleTriggerTime);
		Rule->SetStringField(TEXT("rule_graph"), Transition->BoundGraph ? Transition->BoundGraph->GetName() : TEXT(""));
	}
	else
	{
		Rule = SmInspectRuleGraphToJson(Transition->BoundGraph);
	}
	if (Transition->bSharedRules)
	{
		Rule->SetStringField(TEXT("shared_rules_name"), Transition->SharedRulesName);
	}
	Json->SetObjectField(TEXT("rule"), Rule);
	return Json;
}

TSharedPtr<FJsonObject> SmInspectMachineToJson(UAnimationStateMachineGraph* Graph)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("name"), Graph->GetName());

	UAnimGraphNode_StateMachineBase* OwnerNode = Graph->OwnerAnimGraphNode;
	UEdGraph* ParentGraph = OwnerNode ? OwnerNode->GetGraph() : nullptr;
	Json->SetStringField(TEXT("node_guid"), SmInspectGuid(OwnerNode));
	Json->SetStringField(TEXT("parent_graph"), ParentGraph ? ParentGraph->GetName() : TEXT(""));

	// A state machine inside a state's graph is nested. Report the state and machine that own it.
	const UAnimStateNodeBase* ParentState = ParentGraph ? Cast<UAnimStateNodeBase>(ParentGraph->GetOuter()) : nullptr;
	const UEdGraph* ParentMachine = ParentState ? ParentState->GetGraph() : nullptr;
	if (ParentState && ParentMachine)
	{
		Json->SetStringField(TEXT("parent_state"), ParentState->GetStateName());
		Json->SetStringField(TEXT("parent_state_machine"), ParentMachine->GetName());
	}

	const UAnimStateNodeBase* EntryState = Graph->EntryNode ? Cast<UAnimStateNodeBase>(Graph->EntryNode->GetOutputNode()) : nullptr;
	Json->SetStringField(TEXT("entry_state"), EntryState ? EntryState->GetStateName() : TEXT(""));

	TArray<TSharedPtr<FJsonValue>> StatesJson;
	TArray<TSharedPtr<FJsonValue>> TransitionsJson;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(Node))
		{
			TransitionsJson.Add(MakeShared<FJsonValueObject>(SmInspectTransitionToJson(Transition, TransitionsJson.Num())));
		}
		else if (UAnimStateNodeBase* State = Cast<UAnimStateNodeBase>(Node))
		{
			StatesJson.Add(MakeShared<FJsonValueObject>(SmInspectStateToJson(State, EntryState)));
		}
	}

	Json->SetNumberField(TEXT("state_count"), StatesJson.Num());
	Json->SetNumberField(TEXT("transition_count"), TransitionsJson.Num());
	Json->SetArrayField(TEXT("states"), StatesJson);
	Json->SetArrayField(TEXT("transitions"), TransitionsJson);
	return Json;
}
}

FString UAnimStateMachineInspectTool::GetToolDescription() const
{
	return TEXT("Inspect AnimBlueprint state machines: states, conduits, aliases, the entry state, and transitions with blend settings and a rule summary.");
}

TMap<FString, FBridgeSchemaProperty> UAnimStateMachineInspectTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), SmInspectSchemaProperty(TEXT("string"), TEXT("AnimBlueprint asset path"), true));
	Schema.Add(TEXT("state_machine_name"), SmInspectSchemaProperty(TEXT("string"), TEXT("Optional state machine graph name. Omit to report every state machine.")));
	return Schema;
}

TArray<FString> UAnimStateMachineInspectTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FBridgeToolResult UAnimStateMachineInspectTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"), TEXT(""));
	if (AssetPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("anim-state-machine-inspect: asset_path is required"));
	}
	const FString StateMachineName = GetStringArgOrDefault(Arguments, TEXT("state_machine_name"), TEXT(""));

	FString Error;
	UAnimBlueprint* AnimBlueprint = FBridgeAssetModifier::LoadAssetByPath<UAnimBlueprint>(AssetPath, Error);
	if (!AnimBlueprint)
	{
		return FBridgeToolResult::Error(Error);
	}

	TArray<UEdGraph*> AllGraphs;
	AnimBlueprint->GetAllGraphs(AllGraphs);

	TArray<TSharedPtr<FJsonValue>> MachinesJson;
	TArray<FString> AvailableNames;
	for (UEdGraph* Graph : AllGraphs)
	{
		UAnimationStateMachineGraph* StateMachineGraph = Cast<UAnimationStateMachineGraph>(Graph);
		if (!StateMachineGraph)
		{
			continue;
		}
		AvailableNames.Add(StateMachineGraph->GetName());
		// A named lookup takes the first case-insensitive match, as the authoring tools do.
		const bool bNamedMatch = !StateMachineName.IsEmpty()
			&& MachinesJson.Num() == 0
			&& StateMachineGraph->GetName().Equals(StateMachineName, ESearchCase::IgnoreCase);
		if (StateMachineName.IsEmpty() || bNamedMatch)
		{
			MachinesJson.Add(MakeShared<FJsonValueObject>(SmInspectMachineToJson(StateMachineGraph)));
		}
	}

	if (!StateMachineName.IsEmpty() && MachinesJson.Num() == 0)
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("AnimBlueprint state machine graph not found: %s. Available: %s"),
			*StateMachineName,
			AvailableNames.Num() > 0 ? *FString::Join(AvailableNames, TEXT(", ")) : TEXT("(none)")));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AnimBlueprint->GetPathName());
	Result->SetNumberField(TEXT("state_machine_count"), MachinesJson.Num());
	Result->SetArrayField(TEXT("state_machines"), MachinesJson);

	// A child AnimBlueprint has no graphs of its own, so an empty result does not mean the
	// animation has no state machines. Point the caller at the blueprint that owns them.
	if (MachinesJson.Num() == 0)
	{
		if (const UAnimBlueprint* ParentAnimBlueprint = UAnimBlueprint::GetParentAnimBlueprint(AnimBlueprint))
		{
			Result->SetStringField(TEXT("parent_anim_blueprint"), ParentAnimBlueprint->GetPathName());
			Result->SetStringField(TEXT("note"), FString::Printf(
				TEXT("Child AnimBlueprints inherit their parent's graphs. Inspect %s for its state machines."),
				*ParentAnimBlueprint->GetPathName()));
		}
	}
	return FBridgeToolResult::Json(Result);
}
