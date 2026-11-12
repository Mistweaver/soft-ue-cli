// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Asset/PhysicsAssetQueryTool.h"

#include "Utils/BridgeAssetModifier.h"

#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "ReferenceSkeleton.h"

namespace
{
FBridgeSchemaProperty PhysQuerySchemaProperty(const FString& Type, const FString& Description, bool bRequired = false)
{
	FBridgeSchemaProperty Property;
	Property.Type = Type;
	Property.Description = Description;
	Property.bRequired = bRequired;
	return Property;
}

TArray<TSharedPtr<FJsonValue>> PhysQueryVector(const FVector& Value)
{
	return {
		MakeShared<FJsonValueNumber>(Value.X),
		MakeShared<FJsonValueNumber>(Value.Y),
		MakeShared<FJsonValueNumber>(Value.Z),
	};
}

TArray<TSharedPtr<FJsonValue>> PhysQueryRotator(const FRotator& Value)
{
	return {
		MakeShared<FJsonValueNumber>(Value.Pitch),
		MakeShared<FJsonValueNumber>(Value.Yaw),
		MakeShared<FJsonValueNumber>(Value.Roll),
	};
}

void PhysQuerySetTransform(const TSharedPtr<FJsonObject>& Json, const FTransform& Transform)
{
	Json->SetArrayField(TEXT("location"), PhysQueryVector(Transform.GetLocation()));
	Json->SetArrayField(TEXT("rotation"), PhysQueryRotator(Transform.Rotator()));
	Json->SetArrayField(TEXT("scale"), PhysQueryVector(Transform.GetScale3D()));
}

FString PhysQueryCollisionEnabled(ECollisionEnabled::Type Value)
{
	switch (Value)
	{
	case ECollisionEnabled::NoCollision: return TEXT("no_collision");
	case ECollisionEnabled::QueryOnly: return TEXT("query_only");
	case ECollisionEnabled::PhysicsOnly: return TEXT("physics_only");
	case ECollisionEnabled::QueryAndPhysics: return TEXT("query_and_physics");
	default: return FString::Printf(TEXT("value_%d"), static_cast<int32>(Value));
	}
}

FString PhysQueryPhysicsType(EPhysicsType Value)
{
	switch (Value)
	{
	case PhysType_Default: return TEXT("default");
	case PhysType_Kinematic: return TEXT("kinematic");
	case PhysType_Simulated: return TEXT("simulated");
	default: return FString::Printf(TEXT("value_%d"), static_cast<int32>(Value));
	}
}

FString PhysQueryAngularMotion(EAngularConstraintMotion Value)
{
	switch (Value)
	{
	case ACM_Free: return TEXT("free");
	case ACM_Limited: return TEXT("limited");
	case ACM_Locked: return TEXT("locked");
	default: return FString::Printf(TEXT("value_%d"), static_cast<int32>(Value));
	}
}

FString PhysQueryLinearMotion(ELinearConstraintMotion Value)
{
	switch (Value)
	{
	case LCM_Free: return TEXT("free");
	case LCM_Limited: return TEXT("limited");
	case LCM_Locked: return TEXT("locked");
	default: return FString::Printf(TEXT("value_%d"), static_cast<int32>(Value));
	}
}

/** Parses "bones" as a JSON array of names or a comma-separated string. */
TArray<FName> PhysQueryReadBones(const TSharedPtr<FJsonObject>& Arguments)
{
	TArray<FName> Bones;
	if (!Arguments.IsValid() || !Arguments->HasField(TEXT("bones")))
	{
		return Bones;
	}

	TArray<FString> Raw;
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	FString Joined;
	if (Arguments->TryGetArrayField(TEXT("bones"), Values) && Values)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FString Item;
			if (Value.IsValid() && Value->TryGetString(Item))
			{
				Raw.Add(Item);
			}
		}
	}
	else if (Arguments->TryGetStringField(TEXT("bones"), Joined))
	{
		Joined.ParseIntoArray(Raw, TEXT(","), true);
	}

	for (FString& Item : Raw)
	{
		Item.TrimStartAndEndInline();
		if (!Item.IsEmpty())
		{
			Bones.AddUnique(FName(*Item));
		}
	}
	return Bones;
}

bool PhysQueryWantsBone(const TArray<FName>& Filter, FName Bone)
{
	return Filter.Num() == 0 || Filter.Contains(Bone);
}

/** Reference-pose component-space transform for every bone. Parents precede children in a reference skeleton. */
TArray<FTransform> PhysQueryComponentSpacePose(const FReferenceSkeleton& RefSkeleton)
{
	const TArray<FTransform>& LocalPose = RefSkeleton.GetRefBonePose();
	TArray<FTransform> ComponentPose;
	ComponentPose.SetNum(LocalPose.Num());
	for (int32 BoneIndex = 0; BoneIndex < LocalPose.Num(); ++BoneIndex)
	{
		const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
		ComponentPose[BoneIndex] = ParentIndex == INDEX_NONE
			? LocalPose[BoneIndex]
			: LocalPose[BoneIndex] * ComponentPose[ParentIndex];
	}
	return ComponentPose;
}

/** Shape fields common to every element. BoneTransform is identity in body space. */
TSharedPtr<FJsonObject> PhysQueryShapeBase(const FKShapeElem& Shape, const TCHAR* Type, const FTransform& BoneTransform)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("type"), Type);
	Json->SetStringField(TEXT("name"), Shape.GetName().ToString());
	Json->SetStringField(TEXT("collision_enabled"), PhysQueryCollisionEnabled(Shape.GetCollisionEnabled()));
	PhysQuerySetTransform(Json, Shape.GetTransform() * BoneTransform);
	return Json;
}

TArray<TSharedPtr<FJsonValue>> PhysQueryShapes(const FKAggregateGeom& Geom, const FTransform& BoneTransform, int32& OutOtherCount)
{
	TArray<TSharedPtr<FJsonValue>> Shapes;

	for (const FKSphereElem& Sphere : Geom.SphereElems)
	{
		TSharedPtr<FJsonObject> Json = PhysQueryShapeBase(Sphere, TEXT("sphere"), BoneTransform);
		Json->SetNumberField(TEXT("radius"), Sphere.Radius);
		Shapes.Add(MakeShared<FJsonValueObject>(Json));
	}
	for (const FKBoxElem& Box : Geom.BoxElems)
	{
		TSharedPtr<FJsonObject> Json = PhysQueryShapeBase(Box, TEXT("box"), BoneTransform);
		// Box X, Y, and Z are full edge lengths.
		Json->SetArrayField(TEXT("size"), PhysQueryVector(FVector(Box.X, Box.Y, Box.Z)));
		Shapes.Add(MakeShared<FJsonValueObject>(Json));
	}
	for (const FKSphylElem& Capsule : Geom.SphylElems)
	{
		TSharedPtr<FJsonObject> Json = PhysQueryShapeBase(Capsule, TEXT("capsule"), BoneTransform);
		// Length is the cylinder between the hemisphere centers; total_length includes both caps.
		Json->SetNumberField(TEXT("radius"), Capsule.Radius);
		Json->SetNumberField(TEXT("length"), Capsule.Length);
		Json->SetNumberField(TEXT("total_length"), Capsule.Length + 2.0f * Capsule.Radius);
		Shapes.Add(MakeShared<FJsonValueObject>(Json));
	}
	for (const FKTaperedCapsuleElem& Capsule : Geom.TaperedCapsuleElems)
	{
		TSharedPtr<FJsonObject> Json = PhysQueryShapeBase(Capsule, TEXT("tapered_capsule"), BoneTransform);
		Json->SetNumberField(TEXT("radius0"), Capsule.Radius0);
		Json->SetNumberField(TEXT("radius1"), Capsule.Radius1);
		Json->SetNumberField(TEXT("length"), Capsule.Length);
		Shapes.Add(MakeShared<FJsonValueObject>(Json));
	}
	for (const FKConvexElem& Convex : Geom.ConvexElems)
	{
		TSharedPtr<FJsonObject> Json = PhysQueryShapeBase(Convex, TEXT("convex"), BoneTransform);
		// Vertices and bounds are in the element's own frame, before its transform.
		Json->SetNumberField(TEXT("vertex_count"), Convex.VertexData.Num());
		Json->SetArrayField(TEXT("bounds_min"), PhysQueryVector(Convex.ElemBox.Min));
		Json->SetArrayField(TEXT("bounds_max"), PhysQueryVector(Convex.ElemBox.Max));
		Shapes.Add(MakeShared<FJsonValueObject>(Json));
	}

	// Level sets and skinned elements are counted only. Their element lists differ between engine versions.
	OutOtherCount = FMath::Max(0, Geom.GetElementCount() - Shapes.Num());
	return Shapes;
}

TSharedPtr<FJsonObject> PhysQueryAngularLimit(EAngularConstraintMotion Motion, float LimitDegrees)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("motion"), PhysQueryAngularMotion(Motion));
	Json->SetNumberField(TEXT("limit_degrees"), LimitDegrees);
	return Json;
}

TSharedPtr<FJsonObject> PhysQueryConstraintToJson(const FConstraintInstance& Instance)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("joint_name"), Instance.JointName.ToString());
	// Bone1 is the child body and Bone2 is the parent body.
	Json->SetStringField(TEXT("child_bone"), Instance.ConstraintBone1.ToString());
	Json->SetStringField(TEXT("parent_bone"), Instance.ConstraintBone2.ToString());
	Json->SetBoolField(TEXT("disable_collision"), Instance.IsCollisionDisabled());

	TSharedPtr<FJsonObject> Angular = MakeShared<FJsonObject>();
	Angular->SetObjectField(TEXT("swing1"), PhysQueryAngularLimit(Instance.GetAngularSwing1Motion(), Instance.GetAngularSwing1Limit()));
	Angular->SetObjectField(TEXT("swing2"), PhysQueryAngularLimit(Instance.GetAngularSwing2Motion(), Instance.GetAngularSwing2Limit()));
	Angular->SetObjectField(TEXT("twist"), PhysQueryAngularLimit(Instance.GetAngularTwistMotion(), Instance.GetAngularTwistLimit()));
	Json->SetObjectField(TEXT("angular"), Angular);

	TSharedPtr<FJsonObject> Linear = MakeShared<FJsonObject>();
	Linear->SetStringField(TEXT("x_motion"), PhysQueryLinearMotion(Instance.GetLinearXMotion()));
	Linear->SetStringField(TEXT("y_motion"), PhysQueryLinearMotion(Instance.GetLinearYMotion()));
	Linear->SetStringField(TEXT("z_motion"), PhysQueryLinearMotion(Instance.GetLinearZMotion()));
	Linear->SetNumberField(TEXT("limit"), Instance.GetLinearLimit());
	Json->SetObjectField(TEXT("linear"), Linear);

	// Constraint frames relative to each body's bone.
	TSharedPtr<FJsonObject> ChildFrame = MakeShared<FJsonObject>();
	PhysQuerySetTransform(ChildFrame, Instance.GetRefFrame(EConstraintFrame::Frame1));
	Json->SetObjectField(TEXT("child_frame"), ChildFrame);
	TSharedPtr<FJsonObject> ParentFrame = MakeShared<FJsonObject>();
	PhysQuerySetTransform(ParentFrame, Instance.GetRefFrame(EConstraintFrame::Frame2));
	Json->SetObjectField(TEXT("parent_frame"), ParentFrame);
	return Json;
}
}

FString UPhysicsAssetQueryTool::GetToolDescription() const
{
	return TEXT("Report PhysicsAsset bodies with each collision shape's type, size, and transform, and the constraints between bodies with their angular and linear limits.");
}

TMap<FString, FBridgeSchemaProperty> UPhysicsAssetQueryTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), PhysQuerySchemaProperty(TEXT("string"), TEXT("PhysicsAsset asset path"), true));
	FBridgeSchemaProperty Bones = PhysQuerySchemaProperty(TEXT("array"), TEXT("Optional bone names. Reports only these bodies and the constraints that touch them."));
	Bones.ItemsType = TEXT("string");
	Schema.Add(TEXT("bones"), Bones);
	Schema.Add(TEXT("space"), PhysQuerySchemaProperty(TEXT("string"), TEXT("'body' (default) reports shape transforms relative to each body's bone. 'component' adds the reference-pose bone transform.")));
	Schema.Add(TEXT("mesh_path"), PhysQuerySchemaProperty(TEXT("string"), TEXT("Optional SkeletalMesh for component space. Defaults to the PhysicsAsset's preview mesh.")));
	Schema.Add(TEXT("include_constraints"), PhysQuerySchemaProperty(TEXT("boolean"), TEXT("Report constraints (default: true)")));
	return Schema;
}

TArray<FString> UPhysicsAssetQueryTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FBridgeToolResult UPhysicsAssetQueryTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"), TEXT(""));
	if (AssetPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset-query-physics: asset_path is required"));
	}

	const FString Space = GetStringArgOrDefault(Arguments, TEXT("space"), TEXT("body")).ToLower();
	if (Space != TEXT("body") && Space != TEXT("component"))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("asset-query-physics: space must be 'body' or 'component', got '%s'"), *Space));
	}
	const bool bComponentSpace = Space == TEXT("component");
	const bool bIncludeConstraints = GetBoolArgOrDefault(Arguments, TEXT("include_constraints"), true);
	const TArray<FName> BoneFilter = PhysQueryReadBones(Arguments);

	FString Error;
	UPhysicsAsset* PhysicsAsset = FBridgeAssetModifier::LoadAssetByPath<UPhysicsAsset>(AssetPath, Error);
	if (!PhysicsAsset)
	{
		return FBridgeToolResult::Error(Error.IsEmpty()
			? FString::Printf(TEXT("Asset is not a PhysicsAsset: %s"), *AssetPath)
			: Error);
	}

	USkeletalMesh* PreviewMesh = PhysicsAsset->GetPreviewMesh();
	USkeletalMesh* PoseMesh = PreviewMesh;
	const FString MeshPath = GetStringArgOrDefault(Arguments, TEXT("mesh_path"), TEXT(""));
	if (!MeshPath.IsEmpty())
	{
		PoseMesh = FBridgeAssetModifier::LoadAssetByPath<USkeletalMesh>(MeshPath, Error);
		if (!PoseMesh)
		{
			return FBridgeToolResult::Error(Error.IsEmpty()
				? FString::Printf(TEXT("Asset is not a SkeletalMesh: %s"), *MeshPath)
				: Error);
		}
	}

	TArray<FTransform> ComponentPose;
	if (bComponentSpace)
	{
		if (!PoseMesh)
		{
			return FBridgeToolResult::Error(FString::Printf(
				TEXT("asset-query-physics: component space needs a SkeletalMesh. %s has no preview mesh; pass mesh_path."),
				*PhysicsAsset->GetPathName()));
		}
		ComponentPose = PhysQueryComponentSpacePose(PoseMesh->GetRefSkeleton());
	}

	TArray<FString> Warnings;
	TArray<FName> FoundBones;
	TArray<TSharedPtr<FJsonValue>> BodiesJson;
	for (const USkeletalBodySetup* BodySetup : PhysicsAsset->SkeletalBodySetups)
	{
		if (!BodySetup || !PhysQueryWantsBone(BoneFilter, BodySetup->BoneName))
		{
			continue;
		}
		FoundBones.AddUnique(BodySetup->BoneName);

		TSharedPtr<FJsonObject> BodyJson = MakeShared<FJsonObject>();
		BodyJson->SetStringField(TEXT("bone_name"), BodySetup->BoneName.ToString());
		BodyJson->SetStringField(TEXT("physics_type"), PhysQueryPhysicsType(BodySetup->PhysicsType));
		BodyJson->SetBoolField(TEXT("collision_enabled"), BodySetup->CollisionReponse == EBodyCollisionResponse::BodyCollision_Enabled);
		BodyJson->SetBoolField(TEXT("consider_for_bounds"), BodySetup->bConsiderForBounds != 0);

		FTransform BoneTransform = FTransform::Identity;
		if (bComponentSpace)
		{
			const int32 BoneIndex = PoseMesh->GetRefSkeleton().FindBoneIndex(BodySetup->BoneName);
			if (ComponentPose.IsValidIndex(BoneIndex))
			{
				BoneTransform = ComponentPose[BoneIndex];
				TSharedPtr<FJsonObject> BoneJson = MakeShared<FJsonObject>();
				PhysQuerySetTransform(BoneJson, BoneTransform);
				BodyJson->SetObjectField(TEXT("bone_transform"), BoneJson);
			}
			else
			{
				Warnings.Add(FString::Printf(
					TEXT("Bone %s is not in %s; its shapes are reported in body space."),
					*BodySetup->BoneName.ToString(), *PoseMesh->GetPathName()));
				BodyJson->SetStringField(TEXT("space"), TEXT("body"));
			}
		}

		int32 OtherCount = 0;
		TArray<TSharedPtr<FJsonValue>> Shapes = PhysQueryShapes(BodySetup->AggGeom, BoneTransform, OtherCount);
		BodyJson->SetNumberField(TEXT("shape_count"), Shapes.Num());
		BodyJson->SetNumberField(TEXT("other_shape_count"), OtherCount);
		BodyJson->SetArrayField(TEXT("shapes"), Shapes);
		BodiesJson.Add(MakeShared<FJsonValueObject>(BodyJson));
	}

	TArray<TSharedPtr<FJsonValue>> MissingJson;
	for (const FName& Bone : BoneFilter)
	{
		if (!FoundBones.Contains(Bone))
		{
			MissingJson.Add(MakeShared<FJsonValueString>(Bone.ToString()));
		}
	}

	TArray<TSharedPtr<FJsonValue>> ConstraintsJson;
	if (bIncludeConstraints)
	{
		for (const UPhysicsConstraintTemplate* Template : PhysicsAsset->ConstraintSetup)
		{
			if (!Template)
			{
				continue;
			}
			const FConstraintInstance& Instance = Template->DefaultInstance;
			if (BoneFilter.Num() > 0
				&& !BoneFilter.Contains(Instance.ConstraintBone1)
				&& !BoneFilter.Contains(Instance.ConstraintBone2))
			{
				continue;
			}
			ConstraintsJson.Add(MakeShared<FJsonValueObject>(PhysQueryConstraintToJson(Instance)));
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), PhysicsAsset->GetPathName());
	Result->SetStringField(TEXT("preview_mesh"), PreviewMesh ? PreviewMesh->GetPathName() : TEXT(""));
	Result->SetStringField(TEXT("space"), Space);
	if (bComponentSpace)
	{
		Result->SetStringField(TEXT("pose_mesh"), PoseMesh->GetPathName());
	}
	Result->SetNumberField(TEXT("body_count"), BodiesJson.Num());
	Result->SetArrayField(TEXT("bodies"), BodiesJson);
	Result->SetArrayField(TEXT("missing_bones"), MissingJson);
	if (bIncludeConstraints)
	{
		Result->SetNumberField(TEXT("constraint_count"), ConstraintsJson.Num());
		Result->SetArrayField(TEXT("constraints"), ConstraintsJson);
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
	return FBridgeToolResult::Json(Result);
}
