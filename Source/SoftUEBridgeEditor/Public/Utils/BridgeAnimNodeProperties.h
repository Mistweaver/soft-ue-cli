// Copyright softdaddy-o 2024. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/UnrealType.h"

class UEdGraphPin;

/**
 * Reference to the FAnimNode_* struct member embedded in an anim graph node.
 * Anim graph nodes (UAnimGraphNode_Base subclasses) keep their editable data in an
 * inner struct member - conventionally named "Node", but the name is not guaranteed.
 */
struct SOFTUEBRIDGEEDITOR_API FBridgeInnerAnimNode
{
	FStructProperty* Property = nullptr;
	UScriptStruct* Struct = nullptr;
	void* Container = nullptr;

	bool IsValid() const
	{
		return Property != nullptr && Struct != nullptr && Container != nullptr;
	}
};

/** Structured context captured where property-path resolution actually stopped. */
struct SOFTUEBRIDGEEDITOR_API FBridgePropertyPathFailure
{
	UStruct* Context = nullptr;
	FString StoppedSegment;
	FString Reason;
	int32 ResolvedSegmentCount = 0;

	bool IsValid() const
	{
		return Context != nullptr && !StoppedSegment.IsEmpty();
	}
};

/**
 * Shared property resolution and diagnostics for graph nodes that embed an FAnimNode_* struct.
 * Used by the node property write tools so resolution and the "property not found" diagnostics
 * always describe the same struct.
 */
class SOFTUEBRIDGEEDITOR_API FBridgeAnimNodeProperties
{
public:
	/**
	 * Find the embedded FAnimNode_* struct member of a node.
	 * Prefers a member literally named "Node", then falls back to the first struct member
	 * derived from FAnimNode_Base.
	 * @param Node - Object to inspect (any UObject; returns an invalid result for non-anim nodes)
	 */
	static FBridgeInnerAnimNode FindInnerAnimNode(UObject* Node);

	/**
	 * Resolve a dotted property path (supporting struct members, object members, array indices,
	 * and FInstancedStruct payloads) against an arbitrary struct instance.
	 */
	static bool ResolvePropertyPathAgainstStruct(
		UStruct* RootStruct,
		void* RootContainer,
		const FString& PropertyPath,
		FProperty*& OutProperty,
		void*& OutContainer,
		FString& OutError,
		FBridgePropertyPathFailure* OutFailure = nullptr);

	/**
	 * Resolve a property path against a node's embedded FAnimNode_* struct.
	 * An optional leading "Node." prefix is stripped before resolution.
	 */
	static bool ResolveInnerAnimNodePropertyPath(
		UObject* Node,
		const FString& PropertyPath,
		FProperty*& OutProperty,
		void*& OutContainer,
		FString& OutError);

	/**
	 * Build a diagnostic message for a property path that could not be resolved on a node.
	 * Reports the node class, the resolved inner anim node struct (when present), and the
	 * editable field names actually available on it, so class/property mismatches are obvious.
	 * @param PinNames - Pin names available on the node, reported as settable pin defaults
	 */
	static FString DescribeUnresolvedProperty(
		UObject* Node,
		const FString& PropertyName,
		const TArray<FString>& PinNames);

	/** Whether a graph pin can participate in the default-value fallback. */
	static bool IsSettableGraphPin(const UEdGraphPin* Pin);

	/** Maximum number of field or pin names listed in a diagnostic message. */
	static constexpr int32 MaxListedNames = 15;
};
