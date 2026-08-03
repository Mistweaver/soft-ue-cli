// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Utils/BridgeAnimNodeProperties.h"

#include "Animation/AnimNodeBase.h"
#include "EdGraph/EdGraphPin.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Class.h"

namespace
{
	static bool ParseStrictNonNegativeInt32(const FString& Text, int32& OutValue)
	{
		if (Text.IsEmpty())
		{
			return false;
		}

		int32 Value = 0;
		for (const TCHAR Character : Text)
		{
			if (Character < TEXT('0') || Character > TEXT('9'))
			{
				return false;
			}

			const int32 Digit = Character - TEXT('0');
			if (Value > (MAX_int32 - Digit) / 10)
			{
				return false;
			}
			Value = Value * 10 + Digit;
		}

		OutValue = Value;
		return true;
	}

	static bool ParsePathSegment(const FString& Segment, FString& OutName, int32& OutIndex)
	{
		OutIndex = INDEX_NONE;

		int32 BracketStart = INDEX_NONE;
		if (!Segment.FindChar(TEXT('['), BracketStart))
		{
			if (Segment.Contains(TEXT("]")))
			{
				return false;
			}
			OutName = Segment;
			return !OutName.IsEmpty();
		}

		int32 BracketEnd = INDEX_NONE;
		if (!Segment.FindChar(TEXT(']'), BracketEnd) ||
			BracketEnd <= BracketStart + 1 ||
			BracketEnd != Segment.Len() - 1)
		{
			return false;
		}

		OutName = Segment.Left(BracketStart);
		const FString IndexString = Segment.Mid(BracketStart + 1, BracketEnd - BracketStart - 1);
		if (OutName.IsEmpty() || !ParseStrictNonNegativeInt32(IndexString, OutIndex))
		{
			return false;
		}

		return true;
	}

	/** Names of the editable (non-transient) properties declared on a struct or class. */
	static TArray<FString> CollectEditableFieldNames(UStruct* Struct)
	{
		TArray<FString> Names;
		if (!Struct)
		{
			return Names;
		}

		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property || Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
			{
				continue;
			}
			Names.Add(Property->GetName());
		}

		return Names;
	}

	/** Join names for a diagnostic message, capping the list length. Never emits ';'. */
	static FString JoinNamesForMessage(const TArray<FString>& Names)
	{
		if (Names.Num() == 0)
		{
			return TEXT("(none)");
		}

		const int32 ListedCount = FMath::Min(Names.Num(), FBridgeAnimNodeProperties::MaxListedNames);
		TArray<FString> Listed;
		Listed.Reserve(ListedCount);
		for (int32 Index = 0; Index < ListedCount; ++Index)
		{
			Listed.Add(Names[Index]);
		}

		FString Joined = FString::Join(Listed, TEXT(", "));
		if (Names.Num() > ListedCount)
		{
			Joined += FString::Printf(TEXT(" (+%d more)"), Names.Num() - ListedCount);
		}
		return Joined;
	}

	static void SetPathFailure(
		FBridgePropertyPathFailure* OutFailure,
		UStruct* Context,
		const FString& StoppedSegment,
		int32 ResolvedSegmentCount,
		const FString& Reason)
	{
		if (OutFailure)
		{
			OutFailure->Context = Context;
			OutFailure->StoppedSegment = StoppedSegment;
			OutFailure->ResolvedSegmentCount = ResolvedSegmentCount;
			OutFailure->Reason = Reason;
		}
	}
}

FBridgeInnerAnimNode FBridgeAnimNodeProperties::FindInnerAnimNode(UObject* Node)
{
	FBridgeInnerAnimNode Result;
	if (!Node)
	{
		return Result;
	}

	UClass* NodeClass = Node->GetClass();
	UScriptStruct* AnimNodeBaseStruct = FAnimNode_Base::StaticStruct();

	// Conventional member name first, then any struct member derived from FAnimNode_Base.
	FStructProperty* InnerProp = CastField<FStructProperty>(NodeClass->FindPropertyByName(TEXT("Node")));
	if (!InnerProp || !InnerProp->Struct || !InnerProp->Struct->IsChildOf(AnimNodeBaseStruct))
	{
		InnerProp = nullptr;
		for (TFieldIterator<FStructProperty> It(NodeClass); It; ++It)
		{
			FStructProperty* Candidate = *It;
			if (Candidate && Candidate->Struct && Candidate->Struct->IsChildOf(AnimNodeBaseStruct))
			{
				InnerProp = Candidate;
				break;
			}
		}
	}

	if (!InnerProp)
	{
		return Result;
	}

	Result.Property = InnerProp;
	Result.Struct = InnerProp->Struct;
	Result.Container = InnerProp->ContainerPtrToValuePtr<void>(Node);
	return Result;
}

bool FBridgeAnimNodeProperties::ResolvePropertyPathAgainstStruct(
	UStruct* RootStruct,
	void* RootContainer,
	const FString& PropertyPath,
	FProperty*& OutProperty,
	void*& OutContainer,
	FString& OutError,
	FBridgePropertyPathFailure* OutFailure)
{
	OutProperty = nullptr;
	OutContainer = nullptr;
	OutError.Reset();
	if (OutFailure)
	{
		*OutFailure = FBridgePropertyPathFailure();
	}

	if (!RootStruct || !RootContainer)
	{
		OutError = TEXT("Struct root is null");
		SetPathFailure(OutFailure, RootStruct, TEXT("(root)"), 0, OutError);
		return false;
	}
	if (PropertyPath.IsEmpty())
	{
		OutError = TEXT("Property path is empty");
		SetPathFailure(OutFailure, RootStruct, TEXT("(empty path)"), 0, OutError);
		return false;
	}

	TArray<FString> Segments;
	PropertyPath.ParseIntoArray(Segments, TEXT("."), false);
	if (Segments.Num() == 0)
	{
		OutError = TEXT("Invalid property path");
		SetPathFailure(OutFailure, RootStruct, TEXT("(empty path)"), 0, OutError);
		return false;
	}

	// Preflight every segment's syntax before traversing so a malformed suffix can never
	// normalize to a writable prefix (for example, "Values[0]garbage" or "Values..Num").
	for (const FString& Segment : Segments)
	{
		FString ParsedName;
		int32 ParsedIndex = INDEX_NONE;
		if (!ParsePathSegment(Segment, ParsedName, ParsedIndex))
		{
			const FString StoppedSegment = Segment.IsEmpty() ? TEXT("(empty segment)") : Segment;
			OutError = FString::Printf(TEXT("Invalid property path segment: %s"), *StoppedSegment);
			SetPathFailure(OutFailure, RootStruct, StoppedSegment, 0, OutError);
			return false;
		}
	}

	UStruct* CurrentStruct = RootStruct;
	void* CurrentContainer = RootContainer;
	FProperty* CurrentProperty = nullptr;

	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		const FString& Segment = Segments[Index];
		UStruct* SegmentContext = CurrentStruct;
		FString PropertyName;
		int32 ArrayIndex = INDEX_NONE;
		if (!ParsePathSegment(Segment, PropertyName, ArrayIndex))
		{
			const FString StoppedSegment = Segment.IsEmpty() ? TEXT("(empty segment)") : Segment;
			OutError = FString::Printf(TEXT("Invalid property path segment: %s"), *StoppedSegment);
			SetPathFailure(OutFailure, SegmentContext, StoppedSegment, Index, OutError);
			return false;
		}

		CurrentProperty = CurrentStruct ? CurrentStruct->FindPropertyByName(*PropertyName) : nullptr;
		if (!CurrentProperty)
		{
			OutError = FString::Printf(TEXT("Property not found: %s"), *PropertyName);
			SetPathFailure(OutFailure, SegmentContext, Segment, Index, OutError);
			return false;
		}

		if (ArrayIndex >= 0)
		{
			FArrayProperty* ArrayProp = CastField<FArrayProperty>(CurrentProperty);
			if (!ArrayProp)
			{
				OutError = FString::Printf(TEXT("Property '%s' is not an array"), *PropertyName);
				SetPathFailure(OutFailure, SegmentContext, Segment, Index + 1, OutError);
				return false;
			}

			FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(CurrentContainer));
			if (ArrayIndex >= ArrayHelper.Num())
			{
				OutError = FString::Printf(TEXT("Array index %d out of bounds (size: %d)"), ArrayIndex, ArrayHelper.Num());
				SetPathFailure(OutFailure, SegmentContext, Segment, Index + 1, OutError);
				return false;
			}

			CurrentProperty = ArrayProp->Inner;
			CurrentContainer = ArrayHelper.GetRawPtr(ArrayIndex);
			if (Index == Segments.Num() - 1)
			{
				break;
			}

			FStructProperty* InnerStructProp = CastField<FStructProperty>(ArrayProp->Inner);
			if (!InnerStructProp)
			{
				OutError = FString::Printf(TEXT("Cannot traverse into non-struct array element at: %s"), *Segment);
				SetPathFailure(OutFailure, SegmentContext, Segment, Index + 1, OutError);
				return false;
			}

			CurrentStruct = InnerStructProp->Struct;
			if (CurrentStruct == FInstancedStruct::StaticStruct())
			{
				FInstancedStruct* InstancedStruct = static_cast<FInstancedStruct*>(CurrentContainer);
				if (!InstancedStruct || !InstancedStruct->IsValid())
				{
					OutError = FString::Printf(TEXT("InstancedStruct array element '%s' is empty"), *Segment);
					SetPathFailure(OutFailure, SegmentContext, Segment, Index + 1, OutError);
					return false;
				}

				CurrentContainer = InstancedStruct->GetMutableMemory();
				CurrentStruct = const_cast<UScriptStruct*>(InstancedStruct->GetScriptStruct());
			}
			continue;
		}

		if (Index == Segments.Num() - 1)
		{
			break;
		}

		if (FStructProperty* StructProp = CastField<FStructProperty>(CurrentProperty))
		{
			CurrentContainer = StructProp->ContainerPtrToValuePtr<void>(CurrentContainer);
			CurrentStruct = StructProp->Struct;
			if (CurrentStruct == FInstancedStruct::StaticStruct())
			{
				FInstancedStruct* InstancedStruct = static_cast<FInstancedStruct*>(CurrentContainer);
				if (!InstancedStruct || !InstancedStruct->IsValid())
				{
					OutError = FString::Printf(TEXT("InstancedStruct property '%s' is empty"), *PropertyName);
					SetPathFailure(OutFailure, SegmentContext, Segment, Index + 1, OutError);
					return false;
				}

				CurrentContainer = InstancedStruct->GetMutableMemory();
				CurrentStruct = const_cast<UScriptStruct*>(InstancedStruct->GetScriptStruct());
			}
		}
		else if (FObjectProperty* ObjectProp = CastField<FObjectProperty>(CurrentProperty))
		{
			UObject* ObjectValue = ObjectProp->GetObjectPropertyValue_InContainer(CurrentContainer);
			if (!ObjectValue)
			{
				OutError = FString::Printf(TEXT("Object property '%s' is null"), *PropertyName);
				SetPathFailure(OutFailure, SegmentContext, Segment, Index + 1, OutError);
				return false;
			}
			CurrentContainer = ObjectValue;
			CurrentStruct = ObjectValue->GetClass();
		}
		else
		{
			OutError = FString::Printf(TEXT("Cannot traverse property '%s' - not a struct or object"), *PropertyName);
			SetPathFailure(OutFailure, SegmentContext, Segment, Index + 1, OutError);
			return false;
		}
	}

	OutProperty = CurrentProperty;
	OutContainer = CurrentContainer;
	return OutProperty != nullptr;
}

bool FBridgeAnimNodeProperties::ResolveInnerAnimNodePropertyPath(
	UObject* Node,
	const FString& PropertyPath,
	FProperty*& OutProperty,
	void*& OutContainer,
	FString& OutError)
{
	OutProperty = nullptr;
	OutContainer = nullptr;
	OutError.Reset();

	const FBridgeInnerAnimNode InnerNode = FindInnerAnimNode(Node);
	if (!InnerNode.IsValid())
	{
		OutError = TEXT("Node has no embedded FAnimNode_* struct member");
		return false;
	}

	FString InnerPath = PropertyPath;
	if (InnerPath.StartsWith(TEXT("Node."), ESearchCase::IgnoreCase))
	{
		InnerPath = InnerPath.RightChop(5);
	}

	return ResolvePropertyPathAgainstStruct(
		InnerNode.Struct, InnerNode.Container, InnerPath, OutProperty, OutContainer, OutError);
}

FString FBridgeAnimNodeProperties::DescribeUnresolvedProperty(
	UObject* Node,
	const FString& PropertyName,
	const TArray<FString>& PinNames)
{
	FString Message = FString::Printf(TEXT("Property not found: %s"), *PropertyName);
	if (!Node)
	{
		return Message;
	}

	UClass* NodeClass = Node->GetClass();
	const FBridgeInnerAnimNode InnerNode = FindInnerAnimNode(Node);

	FProperty* IgnoredProperty = nullptr;
	void* IgnoredContainer = nullptr;
	FString IgnoredError;
	FBridgePropertyPathFailure ClassFailure;
	ResolvePropertyPathAgainstStruct(
		NodeClass, Node, PropertyName, IgnoredProperty, IgnoredContainer, IgnoredError, &ClassFailure);

	FBridgePropertyPathFailure InnerFailure;
	if (InnerNode.IsValid())
	{
		FString InnerPath = PropertyName;
		if (InnerPath.StartsWith(TEXT("Node."), ESearchCase::IgnoreCase))
		{
			InnerPath.RightChopInline(5);
		}
		ResolvePropertyPathAgainstStruct(
			InnerNode.Struct, InnerNode.Container, InnerPath,
			IgnoredProperty, IgnoredContainer, IgnoredError, &InnerFailure);
	}

	const FBridgePropertyPathFailure* Failure = &ClassFailure;
	if (InnerFailure.IsValid() &&
		(!ClassFailure.IsValid() || InnerFailure.ResolvedSegmentCount >= ClassFailure.ResolvedSegmentCount))
	{
		Failure = &InnerFailure;
	}

	Message += FString::Printf(TEXT(" (node class %s"), *NodeClass->GetName());
	if (InnerNode.IsValid())
	{
		Message += FString::Printf(TEXT(", inner anim node struct %s"), *InnerNode.Struct->GetName());
	}
	else
	{
		Message += TEXT(", no embedded FAnimNode_* struct");
	}
	Message += TEXT(")");

	if (Failure->IsValid())
	{
		Message += FString::Printf(TEXT(" - stopped at '%s'"), *Failure->StoppedSegment);

		Message += FString::Printf(
			TEXT(" - available on %s: %s"),
			*Failure->Context->GetName(),
			*JoinNamesForMessage(CollectEditableFieldNames(Failure->Context)));
	}

	Message += FString::Printf(TEXT(" - settable pins: %s"), *JoinNamesForMessage(PinNames));

	return Message;
}

bool FBridgeAnimNodeProperties::IsSettableGraphPin(const UEdGraphPin* Pin)
{
	return Pin &&
		Pin->Direction == EGPD_Input &&
		Pin->LinkedTo.Num() == 0 &&
		!Pin->bDefaultValueIsReadOnly &&
		!Pin->bDefaultValueIsIgnored &&
		!Pin->bOrphanedPin;
}
