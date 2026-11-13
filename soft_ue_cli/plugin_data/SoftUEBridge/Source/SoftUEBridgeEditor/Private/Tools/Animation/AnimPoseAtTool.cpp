// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Animation/AnimPoseAtTool.h"

#include "Utils/BridgeAnimSampleTime.h"
#include "Utils/BridgeAssetModifier.h"
#include "Utils/BridgeBonePose.h"

#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AttributesRuntime.h"
#include "Animation/Skeleton.h"
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
	CompactPose.ResetToRefPose();
	Animation.GetAnimationPose(PoseData, ExtractContext);

	TArray<FTransform> LocalPose;
	LocalPose.SetNum(NumBones);
	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const FCompactPoseBoneIndex CompactIndex = RequiredBones.MakeCompactPoseIndex(FMeshPoseBoneIndex(BoneIndex));
		LocalPose[BoneIndex] = CompactIndex.IsValid() ? CompactPose[CompactIndex] : BoneSource.RefSkeleton->GetRefBonePose()[BoneIndex];
	}
	return LocalPose;
}

TArray<TSharedPtr<FJsonValue>> PoseAtBonesToJson(const FReferenceSkeleton& RefSkeleton, const TArray<FTransform>& Pose)
{
	TArray<TSharedPtr<FJsonValue>> BonesJson;
	for (int32 BoneIndex = 0; BoneIndex < Pose.Num(); ++BoneIndex)
	{
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
	return TEXT("Sample an AnimSequence at a frame or time and report each bone's location, rotation, quat, and scale in component space, without PIE.");
}

TMap<FString, FBridgeSchemaProperty> UAnimPoseAtTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), PoseAtSchemaProperty(TEXT("string"), TEXT("AnimSequence asset path"), true));
	Schema.Add(TEXT("frame"), PoseAtSchemaProperty(TEXT("integer"), TEXT("Frame to sample, 0 to frame_count - 1. Give frame or time, not both.")));
	Schema.Add(TEXT("time"), PoseAtSchemaProperty(TEXT("number"), TEXT("Time in seconds to sample, 0 to play_length. Give frame or time, not both.")));
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

	const FPoseAtBoneSource BoneSource = PoseAtDefaultBoneSource(*Sequence);
	if (!BoneSource.Asset || !BoneSource.RefSkeleton)
	{
		return PoseAtError(FString::Printf(TEXT("%s has no skeleton."), *Sequence->GetPathName()));
	}

	const TArray<FTransform> LocalPose = PoseAtSampleLocalPose(*Sequence, Sample.Time, BoneSource);
	const TArray<FTransform> ComponentPose = SoftUE::BonePose::ComponentSpaceFromLocal(*BoneSource.RefSkeleton, LocalPose);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Sequence->GetPathName());
	Result->SetStringField(TEXT("asset_type"), Sequence->GetClass()->GetName());
	Result->SetStringField(TEXT("space"), TEXT("component"));
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
	Result->SetNumberField(TEXT("bone_count"), ComponentPose.Num());
	Result->SetArrayField(TEXT("bones"), PoseAtBonesToJson(*BoneSource.RefSkeleton, ComponentPose));
	return FBridgeToolResult::Json(Result);
}
