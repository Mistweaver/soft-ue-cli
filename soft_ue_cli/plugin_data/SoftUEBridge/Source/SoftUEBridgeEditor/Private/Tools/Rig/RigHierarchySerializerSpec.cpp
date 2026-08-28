// Copyright soft-ue-expert. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tools/Rig/RigHierarchySerializer.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(
	FRigHierarchySerializerSpec,
	"SoftUEBridge.Rig.HierarchySerializer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
END_DEFINE_SPEC(FRigHierarchySerializerSpec)

void FRigHierarchySerializerSpec::Define()
{
	Describe("ParseElementTypeMask", [this]()
	{
		It("defaults an empty string to every element type", [this]()
		{
			ERigElementType Mask = ERigElementType::None;
			FString Error;

			TestTrue(TEXT("parsed"), RigHierarchySerializer::ParseElementTypeMask(TEXT(""), Mask, Error));
			TestEqual(TEXT("mask is All"), static_cast<uint8>(Mask), static_cast<uint8>(ERigElementType::All));
		});

		It("combines comma-separated types into one mask", [this]()
		{
			ERigElementType Mask = ERigElementType::None;
			FString Error;

			TestTrue(TEXT("parsed"), RigHierarchySerializer::ParseElementTypeMask(TEXT("control, bone"), Mask, Error));
			TestEqual(
				TEXT("mask is Control|Bone"),
				static_cast<uint8>(Mask),
				static_cast<uint8>(static_cast<uint8>(ERigElementType::Control) | static_cast<uint8>(ERigElementType::Bone)));
		});

		It("accepts the legacy 'space' alias for null", [this]()
		{
			ERigElementType Mask = ERigElementType::None;
			FString Error;

			TestTrue(TEXT("parsed"), RigHierarchySerializer::ParseElementTypeMask(TEXT("space"), Mask, Error));
			TestEqual(TEXT("mask is Null"), static_cast<uint8>(Mask), static_cast<uint8>(ERigElementType::Null));
		});

		It("rejects an unknown type and names it", [this]()
		{
			ERigElementType Mask = ERigElementType::None;
			FString Error;

			TestFalse(TEXT("rejected"), RigHierarchySerializer::ParseElementTypeMask(TEXT("bone,widget"), Mask, Error));
			TestTrue(TEXT("error names the token"), Error.Contains(TEXT("widget")));
		});
	});

	Describe("ParseTransformDetail", [this]()
	{
		It("defaults an empty string to local", [this]()
		{
			RigHierarchySerializer::ERigTransformDetail Detail = RigHierarchySerializer::ERigTransformDetail::None;
			FString Error;

			TestTrue(TEXT("parsed"), RigHierarchySerializer::ParseTransformDetail(TEXT(""), Detail, Error));
			TestTrue(TEXT("is local"), Detail == RigHierarchySerializer::ERigTransformDetail::Local);
		});

		It("rejects an unknown value", [this]()
		{
			RigHierarchySerializer::ERigTransformDetail Detail = RigHierarchySerializer::ERigTransformDetail::None;
			FString Error;

			TestFalse(TEXT("rejected"), RigHierarchySerializer::ParseTransformDetail(TEXT("sideways"), Detail, Error));
			TestTrue(TEXT("error names the value"), Error.Contains(TEXT("sideways")));
		});
	});

	Describe("ControlValueToJson", [this]()
	{
		It("serializes a bool control", [this]()
		{
			const FRigControlValue Value = FRigControlValue::Make<bool>(true);
			const TSharedPtr<FJsonValue> Json =
				RigHierarchySerializer::ControlValueToJson(Value, ERigControlType::Bool);

			TestTrue(TEXT("json produced"), Json.IsValid());
			TestTrue(TEXT("is true"), Json->AsBool());
		});

		It("serializes a float control", [this]()
		{
			const FRigControlValue Value = FRigControlValue::Make<float>(2.5f);
			const TSharedPtr<FJsonValue> Json =
				RigHierarchySerializer::ControlValueToJson(Value, ERigControlType::Float);

			TestEqual(TEXT("value"), Json->AsNumber(), 2.5, 1.e-4);
		});

		It("serializes a position control as x/y/z", [this]()
		{
			const FRigControlValue Value = FRigControlValue::Make<FVector3f>(FVector3f(1.f, 2.f, 3.f));
			const TSharedPtr<FJsonObject> Json =
				RigHierarchySerializer::ControlValueToJson(Value, ERigControlType::Position)->AsObject();

			TestEqual(TEXT("x"), Json->GetNumberField(TEXT("x")), 1.0, 1.e-4);
			TestEqual(TEXT("y"), Json->GetNumberField(TEXT("y")), 2.0, 1.e-4);
			TestEqual(TEXT("z"), Json->GetNumberField(TEXT("z")), 3.0, 1.e-4);
		});

		It("maps rotator storage to pitch/yaw/roll rather than x/y/z", [this]()
		{
			// A Rotator control stores (X=Pitch, Y=Yaw, Z=Roll) in an FVector3f. Reading it as an
			// XYZ Euler triple would silently mislabel two of the three channels.
			const FRigControlValue Value = FRigControlValue::Make<FVector3f>(FVector3f(10.f, 20.f, 30.f));
			const TSharedPtr<FJsonObject> Json =
				RigHierarchySerializer::ControlValueToJson(Value, ERigControlType::Rotator)->AsObject();

			TestEqual(TEXT("pitch"), Json->GetNumberField(TEXT("pitch")), 10.0, 1.e-4);
			TestEqual(TEXT("yaw"), Json->GetNumberField(TEXT("yaw")), 20.0, 1.e-4);
			TestEqual(TEXT("roll"), Json->GetNumberField(TEXT("roll")), 30.0, 1.e-4);
		});

		It("serializes a vector2d control without a z channel", [this]()
		{
			const FRigControlValue Value = FRigControlValue::Make<FVector3f>(FVector3f(4.f, 5.f, 0.f));
			const TSharedPtr<FJsonObject> Json =
				RigHierarchySerializer::ControlValueToJson(Value, ERigControlType::Vector2D)->AsObject();

			TestEqual(TEXT("x"), Json->GetNumberField(TEXT("x")), 4.0, 1.e-4);
			TestEqual(TEXT("y"), Json->GetNumberField(TEXT("y")), 5.0, 1.e-4);
			TestFalse(TEXT("no z channel"), Json->HasField(TEXT("z")));
		});

		It("serializes an euler transform control into location/rotation/scale", [this]()
		{
			FEulerTransform EulerTransform = FEulerTransform::Identity;
			EulerTransform.Location = FVector(1.0, 2.0, 3.0);
			EulerTransform.Rotation = FRotator(10.0, 20.0, 30.0);
			EulerTransform.Scale = FVector(2.0, 2.0, 2.0);

			const FRigControlValue Value = FRigControlValue::Make<FRigControlValue::FEulerTransform_Float>(EulerTransform);
			const TSharedPtr<FJsonObject> Json =
				RigHierarchySerializer::ControlValueToJson(Value, ERigControlType::EulerTransform)->AsObject();

			const TSharedPtr<FJsonObject> Location = Json->GetObjectField(TEXT("location"));
			const TSharedPtr<FJsonObject> Rotation = Json->GetObjectField(TEXT("rotation"));
			const TSharedPtr<FJsonObject> Scale = Json->GetObjectField(TEXT("scale"));

			TestEqual(TEXT("location.x"), Location->GetNumberField(TEXT("x")), 1.0, 1.e-4);
			TestEqual(TEXT("rotation.pitch"), Rotation->GetNumberField(TEXT("pitch")), 10.0, 1.e-4);
			TestEqual(TEXT("rotation.yaw"), Rotation->GetNumberField(TEXT("yaw")), 20.0, 1.e-4);
			TestEqual(TEXT("rotation.roll"), Rotation->GetNumberField(TEXT("roll")), 30.0, 1.e-4);
			TestEqual(TEXT("scale.z"), Scale->GetNumberField(TEXT("z")), 2.0, 1.e-4);
		});
	});

	Describe("SerializeHierarchy", [this]()
	{
		It("reports a null hierarchy as an unsuccessful result rather than crashing", [this]()
		{
			const RigHierarchySerializer::FRigInspectOptions Options;
			const TSharedPtr<FJsonObject> Json = RigHierarchySerializer::SerializeHierarchy(nullptr, Options);

			TestTrue(TEXT("json produced"), Json.IsValid());
			TestFalse(TEXT("not successful"), Json->GetBoolField(TEXT("success")));
		});
	});

	Describe("SerializeControlValues", [this]()
	{
		It("reports a null hierarchy as an unsuccessful result rather than crashing", [this]()
		{
			const TSharedPtr<FJsonObject> Json =
				RigHierarchySerializer::SerializeControlValues(nullptr, {}, false);

			TestTrue(TEXT("json produced"), Json.IsValid());
			TestFalse(TEXT("not successful"), Json->GetBoolField(TEXT("success")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
