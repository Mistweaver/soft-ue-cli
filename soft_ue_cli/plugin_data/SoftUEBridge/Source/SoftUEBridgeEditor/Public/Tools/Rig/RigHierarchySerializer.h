// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rigs/RigHierarchyDefines.h"

class FJsonObject;
class FJsonValue;
class URigHierarchy;

/**
 * Pure serialization of a Control Rig URigHierarchy to JSON.
 *
 * Everything here reads through the const URigHierarchy API, whose signatures are identical in
 * UE 5.7 and 5.8. Nothing in this namespace names UControlRigBlueprint or the asset interfaces,
 * which 5.8 renamed (IControlRigAssetInterface -> IControlRigEditorAssetInterface).
 */
namespace RigHierarchySerializer
{
	enum class ERigTransformDetail : uint8
	{
		None,
		Local,
		Global,
		Both,
	};

	struct FRigInspectOptions
	{
		/** Bitmask of element types to serialize. Defaults to every type. */
		ERigElementType TypeMask = ERigElementType::All;

		/** When set, only elements whose name contains this substring are serialized. */
		FString NameFilter;

		ERigTransformDetail TransformDetail = ERigTransformDetail::Local;

		/** Include FRigControlSettings (control type, axis, limits, shape) on control elements. */
		bool bIncludeSettings = false;

		/**
		 * Include each control's offset transform. A rig that writes control offsets during its
		 * Forward Solve moves the gizmo independently of the control's own value, so the offset
		 * is the only way to tell where the control actually renders.
		 */
		bool bIncludeOffsets = false;
	};

	/** Parses "control,bone" into a type mask. An empty string means every type. */
	bool ParseElementTypeMask(const FString& CommaSeparatedTypes, ERigElementType& OutMask, FString& OutError);

	/** Parses "none" | "local" | "global" | "both". An empty string means Local. */
	bool ParseTransformDetail(const FString& Value, ERigTransformDetail& OutDetail, FString& OutError);

	FString ElementTypeToString(ERigElementType ElementType);

	FString ControlTypeToString(ERigControlType ControlType);

	/**
	 * Serializes one control value using the storage type that matches its ERigControlType.
	 * FRigControlValue is an untyped storage union, so reading it with the wrong type silently
	 * yields garbage rather than failing.
	 */
	TSharedPtr<FJsonValue> ControlValueToJson(const FRigControlValue& Value, ERigControlType ControlType);

	TSharedPtr<FJsonObject> SerializeHierarchy(const URigHierarchy* Hierarchy, const FRigInspectOptions& Options);

	/** Serializes control values. An empty ControlNames array means every control in the hierarchy. */
	TSharedPtr<FJsonObject> SerializeControlValues(
		const URigHierarchy* Hierarchy,
		const TArray<FString>& ControlNames,
		bool bInitialValues);
}
