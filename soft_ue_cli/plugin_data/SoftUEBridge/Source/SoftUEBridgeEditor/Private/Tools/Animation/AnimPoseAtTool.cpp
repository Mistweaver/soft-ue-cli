// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Animation/AnimPoseAtTool.h"

#include "Utils/BridgeAnimSampleTime.h"
#include "Utils/BridgeAssetModifier.h"
#include "Utils/BridgeBonePose.h"

#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimEnums.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AttributesRuntime.h"
#include "Animation/Skeleton.h"
#include "AnimationRuntime.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/MemStack.h"
#include "ReferenceSkeleton.h"

namespace
{
const TCHAR* const PoseAtToolName = TEXT("anim-pose-at");
const TCHAR* const PoseAtSupportedTypes = TEXT("AnimSequence");

FBridgeSchemaProperty PoseAtSchemaProperty(const FString& Type, const FString& Description, bool bRequired = false)
{
	FBridgeSchemaProperty Property;
	Property.Type = Type;
	Property.Description = Description;
	Property.bRequired = bRequired;
	return Property;
}

FBridgeToolResult PoseAtError(const FString& Message)
{
	return FBridgeToolResult::Error(FString::Printf(TEXT("%s: %s"), PoseAtToolName, *Message));
}

TOptional<int32> PoseAtOptionalInt(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* Field)
{
	double Value = 0.0;
	if (Arguments.IsValid() && Arguments->TryGetNumberField(Field, Value))
	{
		return FMath::RoundToInt32(Value);
	}
	return {};
}

TOptional<double> PoseAtOptionalNumber(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* Field)
{
	double Value = 0.0;
	if (Arguments.IsValid() && Arguments->TryGetNumberField(Field, Value))
	{
		return Value;
	}
	return {};
}

/** The bones a pose is sampled onto: a mesh when one is available, otherwise the skeleton itself. */
struct FPoseAtBoneSource
{
	UObject* Asset = nullptr;
	const FReferenceSkeleton* RefSkeleton = nullptr;
};

/** The animation's preview mesh, then its skeleton's preview mesh, then the bare skeleton. */
FPoseAtBoneSource PoseAtDefaultBoneSource(const UAnimSequenceBase& Animation)
{
	USkeleton* Skeleton = Animation.GetSkeleton();
	USkeletalMesh* Mesh = Animation.GetPreviewMesh();
	if (!Mesh && Skeleton)
	{
		Mesh = Skeleton->GetPreviewMesh();
	}
	if (Mesh)
	{
		return { Mesh, &Mesh->GetRefSkeleton() };
	}
	return { Skeleton, Skeleton ? &Skeleton->GetReferenceSkeleton() : nullptr };
}

/**
 * The mesh named by mesh_path when given, otherwise the default bone source. The mesh must use the
 * animation's skeleton or one marked compatible with it, the same rule as the editor's preview mesh picker.
 */
bool PoseAtResolveBoneSource(
	const FString& MeshPath,
	const UAnimSequenceBase& Animation,
	FPoseAtBoneSource& OutSource,
	FString& OutError)
{
	USkeleton* Skeleton = Animation.GetSkeleton();
	if (!Skeleton)
	{
		OutError = FString::Printf(TEXT("%s has no skeleton."), *Animation.GetPathName());
		return false;
	}

	if (MeshPath.IsEmpty())
	{
		OutSource = PoseAtDefaultBoneSource(Animation);
		return true;
	}

	USkeletalMesh* Mesh = FBridgeAssetModifier::LoadAssetByPath<USkeletalMesh>(MeshPath, OutError);
	if (!Mesh)
	{
		return false;
	}
	if (!Skeleton->IsCompatibleForEditor(Mesh->GetSkeleton()))
	{
		OutError = FString::Printf(
			TEXT("%s uses skeleton %s, which is not compatible with %s's skeleton %s."),
			*Mesh->GetPathName(),
			Mesh->GetSkeleton() ? *Mesh->GetSkeleton()->GetPathName() : TEXT("(none)"),
			*Animation.GetPathName(),
			*Skeleton->GetPathName());
		return false;
	}
	OutSource = { Mesh, &Mesh->GetRefSkeleton() };
	return true;
}

/** Validates space: "component" (default) or "local", ignoring case. */
bool PoseAtReadSpace(const FString& RawSpace, FString& OutSpace, FString& OutError)
{
	OutSpace = RawSpace.ToLower();
	if (OutSpace != TEXT("component") && OutSpace != TEXT("local"))
	{
		OutError = FString::Printf(TEXT("space must be 'component' or 'local', got '%s'."), *OutSpace);
		return false;
	}
	return true;
}

/**
 * Samples Animation at Time onto every bone of BoneSource and returns local (parent-relative)
 * transforms indexed like BoneSource's reference skeleton. Keys are read raw. Root motion stays in
 * the root bone. Mirrors UAnimPoseExtensions::GetAnimPoseAtTimeIntervals, which is the same in 5.7 and 5.8.
 */
TArray<FTransform> PoseAtSampleLocalPose(const UAnimSequenceBase& Animation, double Time, const FPoseAtBoneSource& BoneSource)
{
	FMemMark Mark(FMemStack::Get());

	const int32 NumBones = BoneSource.RefSkeleton->GetNum();
	TArray<FBoneIndexType> RequiredBoneIndices;
	RequiredBoneIndices.SetNumUninitialized(NumBones);
	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		RequiredBoneIndices[BoneIndex] = static_cast<FBoneIndexType>(BoneIndex);
	}

	FBoneContainer RequiredBones;
	RequiredBones.InitializeTo(
		RequiredBoneIndices,
		UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::DisallowAll),
		*BoneSource.Asset);
	RequiredBones.SetUseRAWData(true);

	FCompactPose CompactPose;
	FBlendedCurve Curve;
	UE::Anim::FStackAttributeContainer Attributes;
	FAnimationPoseData PoseData(CompactPose, Curve, Attributes);
	CompactPose.SetBoneContainer(&RequiredBones);
	Curve.InitFrom(RequiredBones);

	const bool bExtractRootMotion = false;
	FAnimExtractContext ExtractContext(Time, bExtractRootMotion);

	// An additive clip holds offsets, so they are accumulated onto its base pose to give a full pose.
	const UAnimSequence* AdditiveSequence = Animation.IsValidAdditive() ? Cast<UAnimSequence>(&Animation) : nullptr;
	if (AdditiveSequence)
	{
		CompactPose.ResetToAdditiveIdentity();
		Animation.GetAnimationPose(PoseData, ExtractContext);

		FCompactPose BasePose;
		BasePose.SetBoneContainer(&RequiredBones);
		FBlendedCurve BaseCurve;
		BaseCurve.InitFrom(RequiredBones);
		UE::Anim::FStackAttributeContainer BaseAttributes;
		FAnimationPoseData BasePoseData(BasePose, BaseCurve, BaseAttributes);
		AdditiveSequence->GetAdditiveBasePose(BasePoseData, ExtractContext);

		FAnimationRuntime::AccumulateAdditivePose(BasePoseData, PoseData, 1.f, AdditiveSequence->GetAdditiveAnimType());
		BasePose.NormalizeRotations();
		CompactPose.CopyBonesFrom(BasePose);
	}
	else
	{
		CompactPose.ResetToRefPose();
		Animation.GetAnimationPose(PoseData, ExtractContext);
	}

	TArray<FTransform> LocalPose;
	LocalPose.SetNum(NumBones);
	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const FCompactPoseBoneIndex CompactIndex = RequiredBones.MakeCompactPoseIndex(FMeshPoseBoneIndex(BoneIndex));
		LocalPose[BoneIndex] = CompactIndex.IsValid() ? CompactPose[CompactIndex] : BoneSource.RefSkeleton->GetRefBonePose()[BoneIndex];
	}
	return LocalPose;
}

/** Every bone in skeleton order, or only the bones in Filter when it is not empty. */
TArray<TSharedPtr<FJsonValue>> PoseAtBonesToJson(const FReferenceSkeleton& RefSkeleton, const TArray<FTransform>& Pose, const TArray<FName>& Filter)
{
	TArray<TSharedPtr<FJsonValue>> BonesJson;
	for (int32 BoneIndex = 0; BoneIndex < Pose.Num(); ++BoneIndex)
	{
		if (Filter.Num() > 0 && !Filter.Contains(RefSkeleton.GetBoneName(BoneIndex)))
		{
			continue;
		}
		const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
		TSharedPtr<FJsonObject> BoneJson = MakeShared<FJsonObject>();
		BoneJson->SetStringField(TEXT("name"), RefSkeleton.GetBoneName(BoneIndex).ToString());
		BoneJson->SetNumberField(TEXT("index"), BoneIndex);
		BoneJson->SetStringField(TEXT("parent"), ParentIndex == INDEX_NONE ? TEXT("") : RefSkeleton.GetBoneName(ParentIndex).ToString());
		SoftUE::BonePose::SetTransformFields(BoneJson, Pose[BoneIndex]);
		BonesJson.Add(MakeShared<FJsonValueObject>(BoneJson));
	}
	return BonesJson;
}

/** Names in Filter that RefSkeleton does not have. */
TArray<TSharedPtr<FJsonValue>> PoseAtMissingBones(const FReferenceSkeleton& RefSkeleton, const TArray<FName>& Filter)
{
	TArray<TSharedPtr<FJsonValue>> Missing;
	for (const FName& Bone : Filter)
	{
		if (RefSkeleton.FindBoneIndex(Bone) == INDEX_NONE)
		{
			Missing.Add(MakeShared<FJsonValueString>(Bone.ToString()));
		}
	}
	return Missing;
}

FString PoseAtAdditiveTypeName(EAdditiveAnimationType Type)
{
	switch (Type)
	{
	case AAT_LocalSpaceBase: return TEXT("local_space");
	case AAT_RotationOffsetMeshSpace: return TEXT("mesh_space");
	default: return FString::Printf(TEXT("value_%d"), static_cast<int32>(Type));
	}
}

FString PoseAtAdditiveBaseName(EAdditiveBasePoseType Type)
{
	switch (Type)
	{
	case ABPT_RefPose: return TEXT("skeleton_ref_pose");
	case ABPT_AnimScaled: return TEXT("animation_scaled");
	case ABPT_AnimFrame: return TEXT("animation_frame");
	case ABPT_LocalAnimFrame: return TEXT("own_frame");
	default: return FString::Printf(TEXT("value_%d"), static_cast<int32>(Type));
	}
}

/** What an additive clip's offsets were applied onto. */
TSharedPtr<FJsonObject> PoseAtAdditiveToJson(const UAnimSequence& Sequence)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("type"), PoseAtAdditiveTypeName(Sequence.GetAdditiveAnimType()));
	Json->SetStringField(TEXT("base_type"), PoseAtAdditiveBaseName(Sequence.RefPoseType));
	const bool bUsesOtherAnimation = Sequence.RefPoseType == ABPT_AnimScaled || Sequence.RefPoseType == ABPT_AnimFrame;
	Json->SetStringField(TEXT("base_animation"), bUsesOtherAnimation && Sequence.RefPoseSeq ? Sequence.RefPoseSeq->GetPathName() : TEXT(""));
	const bool bUsesFrame = Sequence.RefPoseType == ABPT_AnimFrame || Sequence.RefPoseType == ABPT_LocalAnimFrame;
	if (bUsesFrame)
	{
		Json->SetNumberField(TEXT("base_frame"), Sequence.RefFrameIndex);
	}
	return Json;
}

TArray<TSharedPtr<FJsonValue>> PoseAtStringsToJson(const TArray<FString>& Values)
{
	TArray<TSharedPtr<FJsonValue>> Json;
	for (const FString& Value : Values)
	{
		Json.Add(MakeShared<FJsonValueString>(Value));
	}
	return Json;
}

FBridgeAnimTimeline PoseAtSequenceTimeline(const UAnimSequenceBase& Animation)
{
	FBridgeAnimTimeline Timeline;
	Timeline.NumKeys = Animation.GetNumberOfSampledKeys();
	Timeline.PlayLength = Animation.GetPlayLength();
	Timeline.FrameRate = Animation.GetSamplingFrameRate();
	return Timeline;
}
}

FString UAnimPoseAtTool::GetToolDescription() const
{
	return TEXT("Sample an AnimSequence at a frame or time and report each bone's location, rotation, quat, and scale in component or local space, without PIE.");
}

TMap<FString, FBridgeSchemaProperty> UAnimPoseAtTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), PoseAtSchemaProperty(TEXT("string"), TEXT("AnimSequence asset path"), true));
	Schema.Add(TEXT("frame"), PoseAtSchemaProperty(TEXT("integer"), TEXT("Frame to sample, 0 to frame_count - 1. Give frame or time, not both.")));
	Schema.Add(TEXT("time"), PoseAtSchemaProperty(TEXT("number"), TEXT("Time in seconds to sample, 0 to play_length. Give frame or time, not both.")));
	FBridgeSchemaProperty Bones = PoseAtSchemaProperty(TEXT("array"), TEXT("Optional bone names to report (default: every bone). Names the mesh lacks are listed in missing_bones."));
	Bones.ItemsType = TEXT("string");
	Schema.Add(TEXT("bones"), Bones);
	Schema.Add(TEXT("space"), PoseAtSchemaProperty(TEXT("string"), TEXT("'component' (default) is relative to the mesh component; 'local' is relative to each bone's parent.")));
	Schema.Add(TEXT("mesh_path"), PoseAtSchemaProperty(TEXT("string"), TEXT("Optional SkeletalMesh to sample onto. Defaults to the animation's preview mesh, then the skeleton's. Must use a compatible skeleton.")));
	return Schema;
}

TArray<FString> UAnimPoseAtTool::GetRequiredParams() const
{
	return { TEXT("asset_path") };
}

FBridgeToolResult UAnimPoseAtTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"), TEXT(""));
	if (AssetPath.IsEmpty())
	{
		return PoseAtError(TEXT("asset_path is required"));
	}

	FString Error;
	UObject* Asset = FBridgeAssetModifier::LoadAssetByPath(AssetPath, Error);
	if (!Asset)
	{
		return FBridgeToolResult::Error(Error);
	}
	const UAnimSequence* Sequence = Cast<UAnimSequence>(Asset);
	if (!Sequence)
	{
		return PoseAtError(FString::Printf(TEXT("%s has type %s, which is not supported. Supported types: %s."),
			*Asset->GetPathName(), *Asset->GetClass()->GetName(), PoseAtSupportedTypes));
	}

	const FBridgeAnimTimeline Timeline = PoseAtSequenceTimeline(*Sequence);
	FBridgeAnimSampleTime Sample;
	if (!SoftUE::AnimSampleTime::Resolve(
		Timeline, PoseAtOptionalInt(Arguments, TEXT("frame")), PoseAtOptionalNumber(Arguments, TEXT("time")), Sample, Error))
	{
		return PoseAtError(Error);
	}

	FString Space;
	if (!PoseAtReadSpace(GetStringArgOrDefault(Arguments, TEXT("space"), TEXT("component")), Space, Error))
	{
		return PoseAtError(Error);
	}

	FPoseAtBoneSource BoneSource;
	if (!PoseAtResolveBoneSource(GetStringArgOrDefault(Arguments, TEXT("mesh_path"), TEXT("")), *Sequence, BoneSource, Error))
	{
		return PoseAtError(Error);
	}

	const TArray<FTransform> LocalPose = PoseAtSampleLocalPose(*Sequence, Sample.Time, BoneSource);
	const TArray<FTransform> Pose = Space == TEXT("local")
		? LocalPose
		: SoftUE::BonePose::ComponentSpaceFromLocal(*BoneSource.RefSkeleton, LocalPose);
	const TArray<FName> BoneFilter = SoftUE::BonePose::ParseBoneFilter(Arguments);
	const TArray<TSharedPtr<FJsonValue>> BonesJson = PoseAtBonesToJson(*BoneSource.RefSkeleton, Pose, BoneFilter);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Sequence->GetPathName());
	Result->SetStringField(TEXT("asset_type"), Sequence->GetClass()->GetName());
	Result->SetStringField(TEXT("space"), Space);
	Result->SetNumberField(TEXT("time"), Sample.Time);
	if (Sample.Frame.IsSet())
	{
		Result->SetNumberField(TEXT("frame"), Sample.Frame.GetValue());
	}
	else
	{
		Result->SetField(TEXT("frame"), MakeShared<FJsonValueNull>());
	}
	Result->SetNumberField(TEXT("frame_count"), Timeline.NumKeys);
	Result->SetNumberField(TEXT("play_length"), Timeline.PlayLength);
	Result->SetNumberField(TEXT("frame_rate"), Timeline.FrameRate.AsDecimal());
	Result->SetStringField(TEXT("pose_source"), BoneSource.Asset->GetPathName());
	Result->SetStringField(TEXT("evaluation"), TEXT("raw"));
	Result->SetNumberField(TEXT("bone_count"), BonesJson.Num());
	Result->SetArrayField(TEXT("bones"), BonesJson);
	Result->SetArrayField(TEXT("missing_bones"), PoseAtMissingBones(*BoneSource.RefSkeleton, BoneFilter));

	TArray<FString> Warnings;
	if (Sequence->IsValidAdditive())
	{
		Result->SetObjectField(TEXT("additive"), PoseAtAdditiveToJson(*Sequence));
		Warnings.Add(TEXT("This is an additive animation. Its offsets were applied onto its base pose (see additive) to give a full pose."));
	}
	Result->SetArrayField(TEXT("warnings"), PoseAtStringsToJson(Warnings));
	return FBridgeToolResult::Json(Result);
}
