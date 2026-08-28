// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Rig/RigHierarchySerializer.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Rigs/RigHierarchy.h"
#include "Rigs/RigHierarchyElements.h"

namespace
{
	// Free functions here are prefixed `Rig` on purpose: anonymous-namespace helpers that share a
	// name with another translation unit in this module collide once UBT puts them in the same
	// unity blob (see the SegmentToJson and LoadSkeletalMesh regressions).

	TSharedPtr<FJsonObject> RigMakeVectorJson(double X, double Y, double Z)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("x"), X);
		Json->SetNumberField(TEXT("y"), Y);
		Json->SetNumberField(TEXT("z"), Z);
		return Json;
	}

	TSharedPtr<FJsonObject> RigMakeRotatorJson(double Pitch, double Yaw, double Roll)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("pitch"), Pitch);
		Json->SetNumberField(TEXT("yaw"), Yaw);
		Json->SetNumberField(TEXT("roll"), Roll);
		return Json;
	}

	TSharedPtr<FJsonObject> RigMakeTransformJson(const FTransform& Transform)
	{
		const FVector Location = Transform.GetLocation();
		const FRotator Rotation = Transform.GetRotation().Rotator();
		const FVector Scale = Transform.GetScale3D();

		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetObjectField(TEXT("location"), RigMakeVectorJson(Location.X, Location.Y, Location.Z));
		Json->SetObjectField(TEXT("rotation"), RigMakeRotatorJson(Rotation.Pitch, Rotation.Yaw, Rotation.Roll));
		Json->SetObjectField(TEXT("scale"), RigMakeVectorJson(Scale.X, Scale.Y, Scale.Z));
		return Json;
	}

	TSharedPtr<FJsonObject> RigMakeEulerTransformJson(
		double TX, double TY, double TZ,
		double Pitch, double Yaw, double Roll,
		double SX, double SY, double SZ)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetObjectField(TEXT("location"), RigMakeVectorJson(TX, TY, TZ));
		Json->SetObjectField(TEXT("rotation"), RigMakeRotatorJson(Pitch, Yaw, Roll));
		Json->SetObjectField(TEXT("scale"), RigMakeVectorJson(SX, SY, SZ));
		return Json;
	}

	FString RigEnumValueToString(const UEnum* Enum, int64 Value, const FString& Fallback)
	{
		if (!Enum)
		{
			return Fallback;
		}
		const FString Name = Enum->GetNameStringByValue(Value);
		return Name.IsEmpty() ? Fallback : Name;
	}

	TSharedPtr<FJsonObject> RigSerializeControlSettings(const FRigControlSettings& Settings)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("control_type"), RigHierarchySerializer::ControlTypeToString(Settings.ControlType));
		Json->SetStringField(
			TEXT("animation_type"),
			RigEnumValueToString(
				StaticEnum<ERigControlAnimationType>(),
				static_cast<int64>(Settings.AnimationType),
				TEXT("Unknown")));
		Json->SetStringField(
			TEXT("primary_axis"),
			RigEnumValueToString(StaticEnum<ERigControlAxis>(), static_cast<int64>(Settings.PrimaryAxis), TEXT("Unknown")));
		Json->SetStringField(TEXT("display_name"), Settings.DisplayName.ToString());
		Json->SetBoolField(TEXT("is_curve"), Settings.bIsCurve);
		Json->SetBoolField(TEXT("animatable"), Settings.IsAnimatable());
		Json->SetBoolField(TEXT("shape_visible"), Settings.bShapeVisible);
		Json->SetStringField(TEXT("shape_name"), Settings.ShapeName.ToString());
		Json->SetBoolField(TEXT("draw_limits"), Settings.bDrawLimits);

		TArray<TSharedPtr<FJsonValue>> Limits;
		for (const FRigControlLimitEnabled& Limit : Settings.LimitEnabled)
		{
			TSharedPtr<FJsonObject> LimitJson = MakeShared<FJsonObject>();
			LimitJson->SetBoolField(TEXT("minimum"), Limit.bMinimum);
			LimitJson->SetBoolField(TEXT("maximum"), Limit.bMaximum);
			Limits.Add(MakeShared<FJsonValueObject>(LimitJson));
		}
		Json->SetArrayField(TEXT("limit_enabled"), Limits);

		return Json;
	}
}

namespace RigHierarchySerializer
{

FString ElementTypeToString(ERigElementType ElementType)
{
	switch (ElementType)
	{
	case ERigElementType::Bone:      return TEXT("bone");
	case ERigElementType::Null:      return TEXT("null");
	case ERigElementType::Control:   return TEXT("control");
	case ERigElementType::Curve:     return TEXT("curve");
	case ERigElementType::Physics:   return TEXT("physics");
	case ERigElementType::Reference: return TEXT("reference");
	case ERigElementType::Connector: return TEXT("connector");
	case ERigElementType::Socket:    return TEXT("socket");
	default:                         return TEXT("none");
	}
}

FString ControlTypeToString(ERigControlType ControlType)
{
	switch (ControlType)
	{
	case ERigControlType::Bool:             return TEXT("bool");
	case ERigControlType::Float:            return TEXT("float");
	case ERigControlType::Integer:          return TEXT("integer");
	case ERigControlType::Vector2D:         return TEXT("vector2d");
	case ERigControlType::Position:         return TEXT("position");
	case ERigControlType::Scale:            return TEXT("scale");
	case ERigControlType::Rotator:          return TEXT("rotator");
	case ERigControlType::Transform:        return TEXT("transform");
	case ERigControlType::TransformNoScale: return TEXT("transform_no_scale");
	case ERigControlType::EulerTransform:   return TEXT("euler_transform");
	case ERigControlType::ScaleFloat:       return TEXT("scale_float");
	default:                                return TEXT("unknown");
	}
}

bool ParseElementTypeMask(const FString& CommaSeparatedTypes, ERigElementType& OutMask, FString& OutError)
{
	if (CommaSeparatedTypes.TrimStartAndEnd().IsEmpty())
	{
		OutMask = ERigElementType::All;
		return true;
	}

	TArray<FString> Tokens;
	CommaSeparatedTypes.ParseIntoArray(Tokens, TEXT(","), true);

	uint8 Mask = 0;
	for (const FString& RawToken : Tokens)
	{
		const FString Token = RawToken.TrimStartAndEnd().ToLower();
		if (Token.IsEmpty())
		{
			continue;
		}

		if (Token == TEXT("all"))
		{
			Mask |= static_cast<uint8>(ERigElementType::All);
		}
		else if (Token == TEXT("bone"))
		{
			Mask |= static_cast<uint8>(ERigElementType::Bone);
		}
		else if (Token == TEXT("null") || Token == TEXT("space"))
		{
			Mask |= static_cast<uint8>(ERigElementType::Null);
		}
		else if (Token == TEXT("control"))
		{
			Mask |= static_cast<uint8>(ERigElementType::Control);
		}
		else if (Token == TEXT("curve"))
		{
			Mask |= static_cast<uint8>(ERigElementType::Curve);
		}
		else if (Token == TEXT("reference"))
		{
			Mask |= static_cast<uint8>(ERigElementType::Reference);
		}
		else if (Token == TEXT("connector"))
		{
			Mask |= static_cast<uint8>(ERigElementType::Connector);
		}
		else if (Token == TEXT("socket"))
		{
			Mask |= static_cast<uint8>(ERigElementType::Socket);
		}
		else
		{
			OutError = FString::Printf(
				TEXT("Unknown element type '%s'. Expected one or more of: bone, null, control, curve, reference, connector, socket, all"),
				*RawToken.TrimStartAndEnd());
			return false;
		}
	}

	if (Mask == 0)
	{
		OutMask = ERigElementType::All;
		return true;
	}

	OutMask = static_cast<ERigElementType>(Mask);
	return true;
}

bool ParseTransformDetail(const FString& Value, ERigTransformDetail& OutDetail, FString& OutError)
{
	const FString Normalized = Value.TrimStartAndEnd().ToLower();
	if (Normalized.IsEmpty() || Normalized == TEXT("local"))
	{
		OutDetail = ERigTransformDetail::Local;
		return true;
	}
	if (Normalized == TEXT("none"))
	{
		OutDetail = ERigTransformDetail::None;
		return true;
	}
	if (Normalized == TEXT("global"))
	{
		OutDetail = ERigTransformDetail::Global;
		return true;
	}
	if (Normalized == TEXT("both"))
	{
		OutDetail = ERigTransformDetail::Both;
		return true;
	}

	OutError = FString::Printf(
		TEXT("Unknown transforms value '%s'. Expected none, local, global, or both"),
		*Value.TrimStartAndEnd());
	return false;
}

TSharedPtr<FJsonValue> ControlValueToJson(const FRigControlValue& Value, ERigControlType ControlType)
{
	switch (ControlType)
	{
	case ERigControlType::Bool:
		return MakeShared<FJsonValueBoolean>(Value.Get<bool>());

	case ERigControlType::Float:
	case ERigControlType::ScaleFloat:
		return MakeShared<FJsonValueNumber>(Value.Get<float>());

	case ERigControlType::Integer:
		return MakeShared<FJsonValueNumber>(Value.Get<int32>());

	case ERigControlType::Vector2D:
	{
		// Stored in an FVector3f; only X and Y carry the value.
		const FVector3f Stored = Value.Get<FVector3f>();
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("x"), Stored.X);
		Json->SetNumberField(TEXT("y"), Stored.Y);
		return MakeShared<FJsonValueObject>(Json);
	}

	case ERigControlType::Position:
	case ERigControlType::Scale:
	{
		const FVector3f Stored = Value.Get<FVector3f>();
		return MakeShared<FJsonValueObject>(RigMakeVectorJson(Stored.X, Stored.Y, Stored.Z));
	}

	case ERigControlType::Rotator:
	{
		// Stored in an FVector3f as (X=Pitch, Y=Yaw, Z=Roll) -- not an XYZ Euler triple.
		const FVector3f Stored = Value.Get<FVector3f>();
		return MakeShared<FJsonValueObject>(RigMakeRotatorJson(Stored.X, Stored.Y, Stored.Z));
	}

	case ERigControlType::Transform:
	{
		const FRigControlValue::FTransform_Float Stored = Value.Get<FRigControlValue::FTransform_Float>();
		const FRotator Rotation = Stored.GetRotation().Rotator();
		return MakeShared<FJsonValueObject>(RigMakeEulerTransformJson(
			Stored.TranslationX, Stored.TranslationY, Stored.TranslationZ,
			Rotation.Pitch, Rotation.Yaw, Rotation.Roll,
			Stored.ScaleX, Stored.ScaleY, Stored.ScaleZ));
	}

	case ERigControlType::EulerTransform:
	{
		const FRigControlValue::FEulerTransform_Float Stored = Value.Get<FRigControlValue::FEulerTransform_Float>();
		return MakeShared<FJsonValueObject>(RigMakeEulerTransformJson(
			Stored.TranslationX, Stored.TranslationY, Stored.TranslationZ,
			Stored.RotationPitch, Stored.RotationYaw, Stored.RotationRoll,
			Stored.ScaleX, Stored.ScaleY, Stored.ScaleZ));
	}

	case ERigControlType::TransformNoScale:
	{
		const FRigControlValue::FTransformNoScale_Float Stored = Value.Get<FRigControlValue::FTransformNoScale_Float>();
		const FTransform AsTransform = Stored.ToTransform().ToFTransform();
		const FVector Location = AsTransform.GetLocation();
		const FRotator Rotation = AsTransform.GetRotation().Rotator();
		return MakeShared<FJsonValueObject>(RigMakeEulerTransformJson(
			Location.X, Location.Y, Location.Z,
			Rotation.Pitch, Rotation.Yaw, Rotation.Roll,
			1.0, 1.0, 1.0));
	}

	default:
		return MakeShared<FJsonValueNull>();
	}
}

TSharedPtr<FJsonObject> SerializeHierarchy(const URigHierarchy* Hierarchy, const FRigInspectOptions& Options)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	if (!Hierarchy)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Control Rig has no hierarchy"));
		return Result;
	}

	const bool bWantLocal =
		Options.TransformDetail == ERigTransformDetail::Local || Options.TransformDetail == ERigTransformDetail::Both;
	const bool bWantGlobal =
		Options.TransformDetail == ERigTransformDetail::Global || Options.TransformDetail == ERigTransformDetail::Both;

	TArray<TSharedPtr<FJsonValue>> Elements;
	TMap<FString, int32> CountsByType;

	const TArray<FRigElementKey> Keys = Hierarchy->GetAllKeys(true, Options.TypeMask);
	for (const FRigElementKey& Key : Keys)
	{
		const FString ElementName = Key.Name.ToString();
		if (!Options.NameFilter.IsEmpty() && !ElementName.Contains(Options.NameFilter))
		{
			continue;
		}

		const FString TypeName = ElementTypeToString(Key.Type);
		CountsByType.FindOrAdd(TypeName)++;

		TSharedPtr<FJsonObject> ElementJson = MakeShared<FJsonObject>();
		ElementJson->SetStringField(TEXT("name"), ElementName);
		ElementJson->SetStringField(TEXT("type"), TypeName);
		ElementJson->SetNumberField(TEXT("index"), Hierarchy->GetIndex(Key));

		const FRigElementKey ParentKey = Hierarchy->GetFirstParent(Key);
		if (ParentKey.IsValid())
		{
			ElementJson->SetStringField(TEXT("parent"), ParentKey.Name.ToString());
			ElementJson->SetStringField(TEXT("parent_type"), ElementTypeToString(ParentKey.Type));
		}

		if (bWantLocal)
		{
			ElementJson->SetObjectField(
				TEXT("local_transform"), RigMakeTransformJson(Hierarchy->GetLocalTransform(Key, false)));
			ElementJson->SetObjectField(
				TEXT("initial_local_transform"), RigMakeTransformJson(Hierarchy->GetLocalTransform(Key, true)));
		}
		if (bWantGlobal)
		{
			ElementJson->SetObjectField(
				TEXT("global_transform"), RigMakeTransformJson(Hierarchy->GetGlobalTransform(Key, false)));
			ElementJson->SetObjectField(
				TEXT("initial_global_transform"), RigMakeTransformJson(Hierarchy->GetGlobalTransform(Key, true)));
		}

		if (Key.Type == ERigElementType::Control)
		{
			if (const FRigControlElement* ControlElement = Hierarchy->Find<FRigControlElement>(Key))
			{
				ElementJson->SetStringField(
					TEXT("control_type"), ControlTypeToString(ControlElement->Settings.ControlType));
				if (Options.bIncludeSettings)
				{
					ElementJson->SetObjectField(TEXT("settings"), RigSerializeControlSettings(ControlElement->Settings));
				}

				if (Options.bIncludeOffsets)
				{
					ElementJson->SetObjectField(
						TEXT("offset_global_transform"),
						RigMakeTransformJson(Hierarchy->GetGlobalControlOffsetTransform(Key, false)));
					ElementJson->SetObjectField(
						TEXT("initial_offset_global_transform"),
						RigMakeTransformJson(Hierarchy->GetGlobalControlOffsetTransform(Key, true)));
				}
			}
		}

		Elements.Add(MakeShared<FJsonValueObject>(ElementJson));
	}

	TSharedPtr<FJsonObject> CountsJson = MakeShared<FJsonObject>();
	for (const TPair<FString, int32>& Pair : CountsByType)
	{
		CountsJson->SetNumberField(Pair.Key, Pair.Value);
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("element_count"), Elements.Num());
	Result->SetNumberField(TEXT("hierarchy_element_count"), Hierarchy->Num());
	Result->SetObjectField(TEXT("element_counts_by_type"), CountsJson);
	Result->SetArrayField(TEXT("elements"), Elements);
	return Result;
}

TSharedPtr<FJsonObject> SerializeControlValues(
	const URigHierarchy* Hierarchy,
	const TArray<FString>& ControlNames,
	bool bInitialValues)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	if (!Hierarchy)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Control Rig has no hierarchy"));
		return Result;
	}

	TSet<FString> Requested;
	for (const FString& Name : ControlNames)
	{
		const FString Trimmed = Name.TrimStartAndEnd();
		if (!Trimmed.IsEmpty())
		{
			Requested.Add(Trimmed);
		}
	}

	const ERigControlValueType ValueType =
		bInitialValues ? ERigControlValueType::Initial : ERigControlValueType::Current;

	TArray<TSharedPtr<FJsonValue>> Controls;
	TSet<FString> Found;

	const TArray<FRigElementKey> Keys = Hierarchy->GetAllKeys(true, ERigElementType::Control);
	for (const FRigElementKey& Key : Keys)
	{
		const FString ControlName = Key.Name.ToString();
		if (Requested.Num() > 0 && !Requested.Contains(ControlName))
		{
			continue;
		}
		Found.Add(ControlName);

		const FRigControlElement* ControlElement = Hierarchy->Find<FRigControlElement>(Key);
		if (!ControlElement)
		{
			continue;
		}

		const ERigControlType ControlType = ControlElement->Settings.ControlType;

		TSharedPtr<FJsonObject> ControlJson = MakeShared<FJsonObject>();
		ControlJson->SetStringField(TEXT("name"), ControlName);
		ControlJson->SetStringField(TEXT("control_type"), ControlTypeToString(ControlType));
		ControlJson->SetField(TEXT("value"), ControlValueToJson(Hierarchy->GetControlValue(Key, ValueType), ControlType));
		Controls.Add(MakeShared<FJsonValueObject>(ControlJson));
	}

	TArray<TSharedPtr<FJsonValue>> NotFound;
	for (const FString& Name : Requested)
	{
		if (!Found.Contains(Name))
		{
			NotFound.Add(MakeShared<FJsonValueString>(Name));
		}
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("value_type"), bInitialValues ? TEXT("initial") : TEXT("current"));
	Result->SetNumberField(TEXT("control_count"), Controls.Num());
	Result->SetArrayField(TEXT("controls"), Controls);
	Result->SetArrayField(TEXT("controls_not_found"), NotFound);
	return Result;
}

}
