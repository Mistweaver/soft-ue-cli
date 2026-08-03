// Copyright soft-ue-expert. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Utils/BridgeAnimNodeProperties.h"
#include "Utils/BridgePropertySerializer.h"

#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_ModifyBone.h"
#include "AnimGraphNode_TwoBoneIK.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

BEGIN_DEFINE_SPEC(
	FBridgeAnimNodePropertiesSpec,
	"SoftUEBridge.AnimGraph.NodeProperties",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	/** Set a value through the shared resolver. */
	bool SetThroughResolver(UObject* Node, const FString& Path, const TSharedPtr<FJsonValue>& Value, FString& OutError);

	/** Read an FName back through the shared resolver. */
	FName ReadNameThroughResolver(UObject* Node, const FString& Path);

	/** Read a bool back through the shared resolver. */
	bool ReadBoolThroughResolver(UObject* Node, const FString& Path);

END_DEFINE_SPEC(FBridgeAnimNodePropertiesSpec)

bool FBridgeAnimNodePropertiesSpec::SetThroughResolver(
	UObject* Node,
	const FString& Path,
	const TSharedPtr<FJsonValue>& Value,
	FString& OutError)
{
	FProperty* Property = nullptr;
	void* Container = nullptr;
	if (!FBridgeAnimNodeProperties::ResolveInnerAnimNodePropertyPath(Node, Path, Property, Container, OutError))
	{
		return false;
	}

	return FBridgePropertySerializer::DeserializePropertyValue(Property, Container, Value, OutError);
}

FName FBridgeAnimNodePropertiesSpec::ReadNameThroughResolver(UObject* Node, const FString& Path)
{
	FProperty* Property = nullptr;
	void* Container = nullptr;
	FString Error;
	if (!FBridgeAnimNodeProperties::ResolveInnerAnimNodePropertyPath(Node, Path, Property, Container, Error))
	{
		return NAME_None;
	}

	const FNameProperty* NameProperty = CastField<FNameProperty>(Property);
	return NameProperty ? NameProperty->GetPropertyValue_InContainer(Container) : NAME_None;
}

bool FBridgeAnimNodePropertiesSpec::ReadBoolThroughResolver(UObject* Node, const FString& Path)
{
	FProperty* Property = nullptr;
	void* Container = nullptr;
	FString Error;
	if (!FBridgeAnimNodeProperties::ResolveInnerAnimNodePropertyPath(Node, Path, Property, Container, Error))
	{
		return false;
	}

	const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property);
	return BoolProperty != nullptr && BoolProperty->GetPropertyValue_InContainer(Container);
}

void FBridgeAnimNodePropertiesSpec::Define()
{
	Describe("FindInnerAnimNode", [this]()
	{
		It("resolves the embedded FAnimNode_* struct for different anim graph node classes", [this]()
		{
			UAnimGraphNode_ModifyBone* ModifyBone =
				NewObject<UAnimGraphNode_ModifyBone>(GetTransientPackage());
			UAnimGraphNode_TwoBoneIK* TwoBoneIK =
				NewObject<UAnimGraphNode_TwoBoneIK>(GetTransientPackage());

			const FBridgeInnerAnimNode ModifyBoneInner = FBridgeAnimNodeProperties::FindInnerAnimNode(ModifyBone);
			const FBridgeInnerAnimNode TwoBoneIKInner = FBridgeAnimNodeProperties::FindInnerAnimNode(TwoBoneIK);

			TestTrue(TEXT("ModifyBone inner node resolved"), ModifyBoneInner.IsValid());
			TestEqual(TEXT("ModifyBone inner struct"),
				ModifyBoneInner.Struct->GetName(), FString(TEXT("AnimNode_ModifyBone")));
			TestTrue(TEXT("TwoBoneIK inner node resolved"), TwoBoneIKInner.IsValid());
			TestEqual(TEXT("TwoBoneIK inner struct"),
				TwoBoneIKInner.Struct->GetName(), FString(TEXT("AnimNode_TwoBoneIK")));
		});

		It("returns an invalid result for objects without an FAnimNode_* member", [this]()
		{
			const FBridgeInnerAnimNode Inner = FBridgeAnimNodeProperties::FindInnerAnimNode(GetTransientPackage());
			TestFalse(TEXT("no inner node"), Inner.IsValid());
		});
	});

	Describe("ResolveInnerAnimNodePropertyPath", [this]()
	{
		It("resolves an existing array element", [this]()
		{
			UAnimGraphNode_LayeredBoneBlend* Node =
				NewObject<UAnimGraphNode_LayeredBoneBlend>(GetTransientPackage());

			FProperty* ArrayProperty = nullptr;
			void* ArrayContainer = nullptr;
			FString Error;
			TestTrue(TEXT("array property resolves"),
				FBridgeAnimNodeProperties::ResolveInnerAnimNodePropertyPath(
					Node, TEXT("BlendWeights"), ArrayProperty, ArrayContainer, Error));

			FArrayProperty* Array = CastField<FArrayProperty>(ArrayProperty);
			TestNotNull(TEXT("BlendWeights is an array"), Array);
			if (!Array)
			{
				return;
			}

			FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(ArrayContainer));
			const int32 ElementIndex = Helper.AddValue();
			FFloatProperty* FloatProperty = CastField<FFloatProperty>(Array->Inner);
			TestNotNull(TEXT("BlendWeights elements are floats"), FloatProperty);
			if (!FloatProperty)
			{
				return;
			}
			FloatProperty->SetPropertyValue(Helper.GetRawPtr(ElementIndex), 0.25f);

			FProperty* ElementProperty = nullptr;
			void* ElementContainer = nullptr;
			Error = TEXT("stale error");
			TestTrue(TEXT("array element resolves"),
				FBridgeAnimNodeProperties::ResolveInnerAnimNodePropertyPath(
					Node, TEXT("BlendWeights[0]"), ElementProperty, ElementContainer, Error));
			TestEqual(TEXT("successful resolution clears the error"), Error, FString());
			TestTrue(TEXT("resolved the array inner property"), ElementProperty == Array->Inner);
			TestEqual(TEXT("resolved the expected value"),
				FloatProperty->GetPropertyValue(ElementContainer), 0.25f);
		});

		It("rejects malformed path segments without resolving a writable property", [this]()
		{
			UAnimGraphNode_LayeredBoneBlend* Node =
				NewObject<UAnimGraphNode_LayeredBoneBlend>(GetTransientPackage());
			FProperty* ArrayProperty = nullptr;
			void* ArrayContainer = nullptr;
			FString SetupError;
			TestTrue(TEXT("array property resolves for setup"),
				FBridgeAnimNodeProperties::ResolveInnerAnimNodePropertyPath(
					Node, TEXT("BlendWeights"), ArrayProperty, ArrayContainer, SetupError));
			FArrayProperty* Array = CastField<FArrayProperty>(ArrayProperty);
			TestNotNull(TEXT("BlendWeights is an array for setup"), Array);
			if (!Array)
			{
				return;
			}
			FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(ArrayContainer));
			const int32 ElementIndex = Helper.AddValue();
			FFloatProperty* FloatProperty = CastField<FFloatProperty>(Array->Inner);
			TestNotNull(TEXT("BlendWeights elements are floats for setup"), FloatProperty);
			if (!FloatProperty)
			{
				return;
			}
			FloatProperty->SetPropertyValue(Helper.GetRawPtr(ElementIndex), 0.25f);

			const TArray<FString> InvalidPaths = {
				TEXT("BlendWeights[0]garbage"),
				TEXT("BlendWeights[0][1]"),
				TEXT("BlendWeights[0.5]"),
				TEXT("BlendWeights[.]"),
				TEXT("BlendWeights[+]"),
				TEXT("BlendWeights[-0]"),
				TEXT("BlendWeights[ 0]"),
				TEXT("BlendWeights[]"),
				TEXT("BlendWeights[2147483648]"),
				TEXT(".BlendWeights"),
				TEXT("BlendWeights."),
				TEXT("BlendWeights..Num")
			};

			for (const FString& InvalidPath : InvalidPaths)
			{
				FProperty* Property = reinterpret_cast<FProperty*>(1);
				void* Container = reinterpret_cast<void*>(1);
				FString Error;
				TestFalse(*FString::Printf(TEXT("rejects %s"), *InvalidPath),
					FBridgeAnimNodeProperties::ResolveInnerAnimNodePropertyPath(
						Node, InvalidPath, Property, Container, Error));
				TestNull(*FString::Printf(TEXT("clears property for %s"), *InvalidPath), Property);
				TestNull(*FString::Printf(TEXT("clears container for %s"), *InvalidPath), Container);
				TestFalse(*FString::Printf(TEXT("explains %s"), *InvalidPath), Error.IsEmpty());
			}
			TestEqual(TEXT("malformed paths do not mutate the array element"),
				FloatProperty->GetPropertyValue(Helper.GetRawPtr(ElementIndex)), 0.25f);
		});

		It("clears stale outputs when no inner anim node exists", [this]()
		{
			FProperty* Property = reinterpret_cast<FProperty*>(1);
			void* Container = reinterpret_cast<void*>(1);
			FString Error = TEXT("stale error");

			TestFalse(TEXT("package has no inner anim node"),
				FBridgeAnimNodeProperties::ResolveInnerAnimNodePropertyPath(
					GetTransientPackage(), TEXT("Anything"), Property, Container, Error));
			TestNull(TEXT("property output cleared"), Property);
			TestNull(TEXT("container output cleared"), Container);
			TestFalse(TEXT("failure error supplied"), Error.IsEmpty());
		});

		It("sets an FBoneReference from an object value, with and without the Node prefix", [this]()
		{
			UAnimGraphNode_ModifyBone* Node = NewObject<UAnimGraphNode_ModifyBone>(GetTransientPackage());

			TSharedPtr<FJsonObject> BoneObject = MakeShared<FJsonObject>();
			BoneObject->SetStringField(TEXT("BoneName"), TEXT("pelvis"));
			const TSharedPtr<FJsonValue> BoneValue = MakeShared<FJsonValueObject>(BoneObject);

			FString Error;
			TestTrue(TEXT("prefixed path set"),
				SetThroughResolver(Node, TEXT("Node.BoneToModify"), BoneValue, Error));
			TestEqual(TEXT("no error for prefixed path"), Error, FString());
			TestEqual(TEXT("prefixed path value"),
				ReadNameThroughResolver(Node, TEXT("Node.BoneToModify.BoneName")), FName(TEXT("pelvis")));

			UAnimGraphNode_ModifyBone* BareNode = NewObject<UAnimGraphNode_ModifyBone>(GetTransientPackage());
			TestTrue(TEXT("bare path set"),
				SetThroughResolver(BareNode, TEXT("BoneToModify"), BoneValue, Error));
			TestEqual(TEXT("bare path value"),
				ReadNameThroughResolver(BareNode, TEXT("BoneToModify.BoneName")), FName(TEXT("pelvis")));
		});

		It("sets a nested struct member through a dotted path", [this]()
		{
			UAnimGraphNode_ModifyBone* Node = NewObject<UAnimGraphNode_ModifyBone>(GetTransientPackage());
			const TSharedPtr<FJsonValue> NameValue = MakeShared<FJsonValueString>(TEXT("spine_03"));

			FString Error;
			TestTrue(TEXT("dotted path set"),
				SetThroughResolver(Node, TEXT("Node.BoneToModify.BoneName"), NameValue, Error));
			TestEqual(TEXT("no error for dotted path"), Error, FString());
			TestEqual(TEXT("dotted path value"),
				ReadNameThroughResolver(Node, TEXT("Node.BoneToModify.BoneName")), FName(TEXT("spine_03")));
		});

		It("sets a bool member on the inner anim node struct", [this]()
		{
			UAnimGraphNode_TwoBoneIK* Node = NewObject<UAnimGraphNode_TwoBoneIK>(GetTransientPackage());

			FString Error;
			TestTrue(TEXT("bool set"),
				SetThroughResolver(Node, TEXT("Node.bAllowStretching"),
					MakeShared<FJsonValueBoolean>(true), Error));
			TestTrue(TEXT("bAllowStretching set"),
				ReadBoolThroughResolver(Node, TEXT("Node.bAllowStretching")));
		});

		It("fails with an error for a member the inner struct does not declare", [this]()
		{
			UAnimGraphNode_ModifyBone* Node = NewObject<UAnimGraphNode_ModifyBone>(GetTransientPackage());

			FProperty* Property = nullptr;
			void* Container = nullptr;
			FString Error;
			const bool bResolved = FBridgeAnimNodeProperties::ResolveInnerAnimNodePropertyPath(
				Node, TEXT("Node.NotARealField"), Property, Container, Error);

			TestFalse(TEXT("unknown member does not resolve"), bResolved);
			TestTrue(TEXT("error names the member"), Error.Contains(TEXT("NotARealField")));
		});
	});

	Describe("DescribeUnresolvedProperty", [this]()
	{
		It("reports the node class, the inner struct actually searched, and its fields", [this]()
		{
			UAnimGraphNode_ModifyBone* Node = NewObject<UAnimGraphNode_ModifyBone>(GetTransientPackage());

			const FString Message = FBridgeAnimNodeProperties::DescribeUnresolvedProperty(
				Node, TEXT("Node.BoundBone"), TArray<FString>());

			TestTrue(TEXT("names the property"), Message.Contains(TEXT("Property not found: Node.BoundBone")));
			TestTrue(TEXT("names the node class"), Message.Contains(TEXT("AnimGraphNode_ModifyBone")));
			TestTrue(TEXT("names the inner struct"), Message.Contains(TEXT("AnimNode_ModifyBone")));
			TestTrue(TEXT("lists an available field"), Message.Contains(TEXT("BoneToModify")));
			TestFalse(TEXT("does not use the warning separator"), Message.Contains(TEXT(";")));
		});

		It("reports available pins when the node exposes them", [this]()
		{
			UAnimGraphNode_ModifyBone* Node = NewObject<UAnimGraphNode_ModifyBone>(GetTransientPackage());

			TArray<FString> PinNames;
			PinNames.Add(TEXT("Alpha"));

			const FString Message = FBridgeAnimNodeProperties::DescribeUnresolvedProperty(
				Node, TEXT("NotARealField"), PinNames);

			TestTrue(TEXT("names the segment where resolution stopped"),
				Message.Contains(TEXT("stopped at 'NotARealField'")));
			TestTrue(TEXT("lists settable pins"), Message.Contains(TEXT("settable pins: Alpha")));
		});

		It("reports the property where scalar traversal actually stopped", [this]()
		{
			UAnimGraphNode_TwoBoneIK* Node = NewObject<UAnimGraphNode_TwoBoneIK>(GetTransientPackage());

			const FString Message = FBridgeAnimNodeProperties::DescribeUnresolvedProperty(
				Node, TEXT("Node.bAllowStretching.NotARealField"), TArray<FString>());

			TestTrue(TEXT("stops at the scalar property"),
				Message.Contains(TEXT("stopped at 'bAllowStretching'")));
			TestTrue(TEXT("reports fields from the owning struct"),
				Message.Contains(TEXT("available on AnimNode_TwoBoneIK")));
			TestTrue(TEXT("reports no settable pins"),
				Message.Contains(TEXT("settable pins: (none)")));
		});

		It("reports the indexed array property where bounds checking stopped", [this]()
		{
			UAnimGraphNode_LayeredBoneBlend* Node =
				NewObject<UAnimGraphNode_LayeredBoneBlend>(GetTransientPackage());

			const FString Message = FBridgeAnimNodeProperties::DescribeUnresolvedProperty(
				Node, TEXT("Node.BlendWeights[999]"), TArray<FString>());

			TestTrue(TEXT("stops at the indexed array property"),
				Message.Contains(TEXT("stopped at 'BlendWeights[999]'")));
			TestTrue(TEXT("reports fields from the array-owning struct"),
				Message.Contains(TEXT("available on AnimNode_LayeredBoneBlend")));
		});

		It("reports the array property when traversal omits an index", [this]()
		{
			UAnimGraphNode_LayeredBoneBlend* Node =
				NewObject<UAnimGraphNode_LayeredBoneBlend>(GetTransientPackage());

			const FString Message = FBridgeAnimNodeProperties::DescribeUnresolvedProperty(
				Node, TEXT("Node.BlendWeights.NotARealField"), TArray<FString>());

			TestTrue(TEXT("stops at the unindexed array property"),
				Message.Contains(TEXT("stopped at 'BlendWeights'")));
			TestTrue(TEXT("reports fields from the array-owning struct"),
				Message.Contains(TEXT("available on AnimNode_LayeredBoneBlend")));
		});

		It("reports malformed and empty path segments exactly where parsing stopped", [this]()
		{
			UAnimGraphNode_LayeredBoneBlend* Node =
				NewObject<UAnimGraphNode_LayeredBoneBlend>(GetTransientPackage());

			const FString TrailingMessage = FBridgeAnimNodeProperties::DescribeUnresolvedProperty(
				Node, TEXT("Node.BlendWeights[0]garbage"), TArray<FString>());
			TestTrue(TEXT("reports the malformed bracket segment"),
				TrailingMessage.Contains(TEXT("stopped at 'BlendWeights[0]garbage'")));

			const FString EmptyMessage = FBridgeAnimNodeProperties::DescribeUnresolvedProperty(
				Node, TEXT("Node..BlendWeights"), TArray<FString>());
			TestTrue(TEXT("reports the empty segment"),
				EmptyMessage.Contains(TEXT("stopped at '(empty segment)'")));

			const FString LaterEmptyMessage = FBridgeAnimNodeProperties::DescribeUnresolvedProperty(
				Node, TEXT("Node.BlendWeights..Num"), TArray<FString>());
			TestTrue(TEXT("preflights an empty segment after an array property"),
				LaterEmptyMessage.Contains(TEXT("stopped at '(empty segment)'")));
		});

		It("caps long field lists", [this]()
		{
			UAnimGraphNode_TwoBoneIK* Node = NewObject<UAnimGraphNode_TwoBoneIK>(GetTransientPackage());

			const FString Message = FBridgeAnimNodeProperties::DescribeUnresolvedProperty(
				Node, TEXT("NotARealField"), TArray<FString>());

			int32 CommaCount = 0;
			for (const TCHAR Character : Message)
			{
				if (Character == TEXT(','))
				{
					++CommaCount;
				}
			}
			TestTrue(TEXT("field list is capped"), CommaCount <= FBridgeAnimNodeProperties::MaxListedNames);
		});

		It("still reports the class for objects without an inner anim node struct", [this]()
		{
			const FString Message = FBridgeAnimNodeProperties::DescribeUnresolvedProperty(
				GetTransientPackage(), TEXT("NotARealField"), TArray<FString>());

			TestTrue(TEXT("mentions missing anim node struct"),
				Message.Contains(TEXT("no embedded FAnimNode_* struct")));
			TestTrue(TEXT("reports no settable pins"),
				Message.Contains(TEXT("settable pins: (none)")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
