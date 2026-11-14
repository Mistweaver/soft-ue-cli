// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Animation/AnimPoseAtTool.h"

#include "Utils/BridgeAnimSampleTime.h"
#include "Utils/BridgeAssetModifier.h"
#include "Utils/BridgeBonePose.h"
#include "Utils/BridgeJsonObjectUtils.h"

#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimEnums.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AttributesRuntime.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
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
const TCHAR* const PoseAtSupportedTypes = TEXT("AnimSequence, AnimMontage, BlendSpace");

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

/** Reads frame as a whole number. Unset when the field is missing; a fractional value is an error. */
bool PoseAtOptionalFrame(const TSharedPtr<FJsonObject>& Arguments, TOptional<int32>& OutFrame, FString& OutError)
{
	double Value = 0.0;
	if (!Arguments.IsValid() || !Arguments->TryGetNumberField(TEXT("frame"), Value))
	{
		OutFrame.Reset();
		return true;
	}
	if (Value != FMath::FloorToDouble(Value) || FMath::Abs(Value) > static_cast<double>(MAX_int32))
	{
		OutError = FString::Printf(TEXT("frame must be a whole number, got %g."), Value);
		return false;
	}
	OutFrame = static_cast<int32>(Value);
	return true;
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

/** Reads blend as a JSON array of numbers. Unset when the field is missing; any other item is an error. */
bool PoseAtOptionalBlend(const TSharedPtr<FJsonObject>& Arguments, TOptional<TArray<double>>& OutBlend, FString& OutError)
{
	OutBlend.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Arguments.IsValid() || !Arguments->TryGetArrayField(TEXT("blend"), Values) || !Values)
	{
		return true;
	}
	TArray<double> Numbers;
	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		double Number = 0.0;
		// TryGetNumber also parses strings, so check the type: only numbers are blend input.
		if (!Value.IsValid() || Value->Type != EJson::Number || !Value->TryGetNumber(Number))
		{
			OutError = TEXT("blend values must be numbers, such as [150] or [90, 150].");
			return false;
		}
		Numbers.Add(Number);
	}
	OutBlend = MoveTemp(Numbers);
	return true;
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

/** What the caller asked for, read once from the arguments. */
struct FPoseAtRequest
{
	TOptional<int32> Frame;
	TOptional<double> Time;
	FString Slot;
	TOptional<TArray<double>> Blend;
};

/** The bones a pose is sampled onto: a mesh when one is available, otherwise the skeleton itself. */
struct FPoseAtBoneSource
{
	UObject* Asset = nullptr;
	const FReferenceSkeleton* RefSkeleton = nullptr;
};

/**
 * What to evaluate, resolved from the asset type. Sequences and montages evaluate one animation at one
 * time; a BlendSpace evaluates its weighted samples, each at its own time.
 */
struct FPoseAtResolved
{
	FBridgeAnimTimeline Timeline;
	FBridgeAnimSampleTime Sample;

	const UAnimSequenceBase* Animation = nullptr;
	double AnimationTime = 0.0;

	const UBlendSpace* BlendSpace = nullptr;
	TArray<FBlendSampleData> BlendSamples;

	/** Type-specific result fields, such as the montage slot or the BlendSpace samples. */
	TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
	TArray<FString> Warnings;
};

// ---- Bone source ----

/** The asset's preview mesh, then its skeleton's preview mesh, then the bare skeleton. */
FPoseAtBoneSource PoseAtDefaultBoneSource(const UAnimationAsset& Asset)
{
	USkeleton* Skeleton = Asset.GetSkeleton();
	USkeletalMesh* Mesh = Asset.GetPreviewMesh();
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
 * The mesh named by MeshPath when given, otherwise the default bone source. The mesh must use the
 * asset's skeleton or one marked compatible with it, the same rule as the editor's preview mesh picker.
 */
bool PoseAtResolveBoneSource(const FString& MeshPath, const UAnimationAsset& Asset, FPoseAtBoneSource& OutSource, FString& OutError)
{
	USkeleton* Skeleton = Asset.GetSkeleton();
	if (!Skeleton)
	{
		OutError = FString::Printf(TEXT("%s has no skeleton."), *Asset.GetPathName());
		return false;
	}

	if (MeshPath.IsEmpty())
	{
		OutSource = PoseAtDefaultBoneSource(Asset);
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
			*Asset.GetPathName(),
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

// ---- Resolving each asset type ----

FBridgeAnimTimeline PoseAtSequenceTimeline(const UAnimSequenceBase& Animation)
{
	FBridgeAnimTimeline Timeline;
	Timeline.PlayLength = Animation.GetPlayLength();
	Timeline.FrameRate = Animation.GetSamplingFrameRate();
	// UAnimSequence counts its keys; the UAnimSequenceBase default (montages) counts intervals, one fewer.
	Timeline.NumKeys = Animation.IsA<UAnimSequence>()
		? Animation.GetNumberOfSampledKeys()
		: SoftUE::AnimSampleTime::NumKeysForLength(Timeline.FrameRate, Timeline.PlayLength);
	return Timeline;
}

bool PoseAtResolveSequence(const UAnimSequence& Sequence, const FPoseAtRequest& Request, FPoseAtResolved& Out, FString& OutError)
{
	Out.Timeline = PoseAtSequenceTimeline(Sequence);
	if (!SoftUE::AnimSampleTime::Resolve(Out.Timeline, Request.Frame, Request.Time, Out.Sample, OutError))
	{
		return false;
	}
	Out.Animation = &Sequence;
	Out.AnimationTime = Out.Sample.Time;
	return true;
}

/** The slot track named Slot, or the first track when Slot is empty. */
const FSlotAnimationTrack* PoseAtFindSlot(const UAnimMontage& Montage, const FString& Slot, FString& OutError)
{
	if (Montage.SlotAnimTracks.Num() == 0)
	{
		OutError = FString::Printf(TEXT("%s has no slot tracks."), *Montage.GetPathName());
		return nullptr;
	}
	if (Slot.IsEmpty())
	{
		return &Montage.SlotAnimTracks[0];
	}

	TArray<FString> SlotNames;
	for (const FSlotAnimationTrack& Track : Montage.SlotAnimTracks)
	{
		if (Track.SlotName.ToString().Equals(Slot, ESearchCase::IgnoreCase))
		{
			return &Track;
		}
		SlotNames.Add(Track.SlotName.ToString());
	}
	OutError = FString::Printf(TEXT("%s has no slot '%s'. Slots: %s."), *Montage.GetPathName(), *Slot, *FString::Join(SlotNames, TEXT(", ")));
	return nullptr;
}

/** Time is on the montage timeline; the segment under it gives the animation and its local time. */
bool PoseAtResolveMontage(const UAnimMontage& Montage, const FPoseAtRequest& Request, FPoseAtResolved& Out, FString& OutError)
{
	Out.Timeline = PoseAtSequenceTimeline(Montage);
	if (!SoftUE::AnimSampleTime::Resolve(Out.Timeline, Request.Frame, Request.Time, Out.Sample, OutError))
	{
		return false;
	}

	const FSlotAnimationTrack* Track = PoseAtFindSlot(Montage, Request.Slot, OutError);
	if (!Track)
	{
		return false;
	}

	const float MontageTime = static_cast<float>(Out.Sample.Time);
	const FAnimSegment* Segment = Track->AnimTrack.GetSegmentAtTime(MontageTime);
	float SegmentTime = 0.0f;
	const UAnimSequenceBase* SegmentAnimation = Segment ? Segment->GetAnimationData(MontageTime, SegmentTime) : nullptr;
	if (!SegmentAnimation)
	{
		OutError = FString::Printf(TEXT("slot '%s' has no animation at %.3f seconds."), *Track->SlotName.ToString(), Out.Sample.Time);
		return false;
	}

	Out.Animation = SegmentAnimation;
	Out.AnimationTime = SegmentTime;
	Out.Details->SetStringField(TEXT("slot"), Track->SlotName.ToString());
	Out.Details->SetStringField(TEXT("segment_anim_path"), SegmentAnimation->GetPathName());
	Out.Details->SetNumberField(TEXT("segment_time"), SegmentTime);
	return true;
}

/**
 * Time is on the blended length at the blend input, so each sample plays at the same normalized
 * position. That is sync by normalized time; sync markers are not used.
 */
bool PoseAtResolveBlendSpace(const UBlendSpace& BlendSpace, const FPoseAtRequest& Request, FPoseAtResolved& Out, FString& OutError)
{
	if (BlendSpace.IsValidAdditive())
	{
		OutError = FString::Printf(TEXT("%s is an additive BlendSpace (aim offset), which is not supported. Sample its animations instead."), *BlendSpace.GetPathName());
		return false;
	}

	const int32 Dimensions = BlendSpace.IsA<UBlendSpace1D>() ? 1 : 2;
	if (!Request.Blend.IsSet())
	{
		OutError = FString::Printf(TEXT("a BlendSpace needs blend with %d value%s."), Dimensions, Dimensions == 1 ? TEXT("") : TEXT("s"));
		return false;
	}
	const TArray<double>& Blend = Request.Blend.GetValue();
	if (Blend.Num() != Dimensions)
	{
		OutError = FString::Printf(TEXT("%s is %dD, so blend needs %d value%s, got %d."),
			*BlendSpace.GetPathName(), Dimensions, Dimensions, Dimensions == 1 ? TEXT("") : TEXT("s"), Blend.Num());
		return false;
	}

	const FVector BlendInput(Blend[0], Dimensions > 1 ? Blend[1] : 0.0, 0.0);
	int32 CachedTriangulationIndex = INDEX_NONE;
	const bool bCombineAnimations = true;
	if (!BlendSpace.GetSamplesFromBlendInput(BlendInput, Out.BlendSamples, CachedTriangulationIndex, bCombineAnimations) || Out.BlendSamples.Num() == 0)
	{
		OutError = FString::Printf(TEXT("%s has no samples at the blend input."), *BlendSpace.GetPathName());
		return false;
	}

	Out.Timeline.PlayLength = BlendSpace.GetAnimationLengthFromSampleData(Out.BlendSamples);
	Out.Timeline.bHasFrames = false;
	if (!SoftUE::AnimSampleTime::Resolve(Out.Timeline, Request.Frame, Request.Time, Out.Sample, OutError))
	{
		return false;
	}

	const double NormalizedTime = Out.Timeline.PlayLength > 0.0 ? Out.Sample.Time / Out.Timeline.PlayLength : 0.0;
	TArray<TSharedPtr<FJsonValue>> SamplesJson;
	for (FBlendSampleData& SampleData : Out.BlendSamples)
	{
		const FBlendSample& BlendSample = BlendSpace.GetBlendSample(SampleData.SampleDataIndex);
		const UAnimSequence* SampleAnimation = BlendSample.Animation;
		// One frame's length for a sample marked bUseSingleFrameForBlending, as UBlendSpace::ResetBlendSamples does.
		const double SampleLength = BlendSample.GetSamplePlayLength();
		SampleData.Time = static_cast<float>(NormalizedTime * SampleLength);

		TSharedPtr<FJsonObject> SampleJson = MakeShared<FJsonObject>();
		SampleJson->SetStringField(TEXT("anim_path"), SampleAnimation ? SampleAnimation->GetPathName() : TEXT(""));
		SampleJson->SetNumberField(TEXT("weight"), SampleData.GetClampedWeight());
		SampleJson->SetNumberField(TEXT("time"), SampleData.Time);
		SamplesJson.Add(MakeShared<FJsonValueObject>(SampleJson));
	}

	Out.BlendSpace = &BlendSpace;
	TArray<TSharedPtr<FJsonValue>> BlendJson;
	for (double Value : Blend)
	{
		BlendJson.Add(MakeShared<FJsonValueNumber>(Value));
	}
	Out.Details->SetArrayField(TEXT("blend_input"), BlendJson);
	Out.Details->SetArrayField(TEXT("samples"), SamplesJson);
	Out.Warnings.Add(TEXT("BlendSpace samples are aligned by normalized time; sync markers are not used."));
	return true;
}

/** Dispatches on the asset type once. Options that do not apply to the type are reported as warnings. */
bool PoseAtResolve(const UAnimationAsset& Asset, const FPoseAtRequest& Request, FPoseAtResolved& Out, FString& OutError)
{
	bool bResolved = false;
	if (const UAnimSequence* Sequence = Cast<UAnimSequence>(&Asset))
	{
		bResolved = PoseAtResolveSequence(*Sequence, Request, Out, OutError);
	}
	else if (const UAnimMontage* Montage = Cast<UAnimMontage>(&Asset))
	{
		bResolved = PoseAtResolveMontage(*Montage, Request, Out, OutError);
	}
	else if (const UBlendSpace* BlendSpace = Cast<UBlendSpace>(&Asset))
	{
		bResolved = PoseAtResolveBlendSpace(*BlendSpace, Request, Out, OutError);
	}
	else
	{
		OutError = FString::Printf(TEXT("%s has type %s, which is not supported. Supported types: %s."),
			*Asset.GetPathName(), *Asset.GetClass()->GetName(), PoseAtSupportedTypes);
		return false;
	}

	if (bResolved && !Request.Slot.IsEmpty() && !Asset.IsA<UAnimMontage>())
	{
		Out.Warnings.Add(TEXT("slot applies only to AnimMontages and was ignored."));
	}
	if (bResolved && Request.Blend.IsSet() && !Asset.IsA<UBlendSpace>())
	{
		Out.Warnings.Add(TEXT("blend applies only to BlendSpaces and was ignored."));
	}
	return bResolved;
}

// ---- Evaluation ----

/**
 * One animation at one time. An additive AnimSequence's offsets are accumulated onto its base pose;
 * any other additive asset (an AnimComposite) names no base, so its offsets go onto the reference pose.
 */
void PoseAtEvaluateAnimation(const UAnimSequenceBase& Animation, double Time, FBoneContainer& RequiredBones, FAnimationPoseData& PoseData)
{
	const bool bExtractRootMotion = false;
	FAnimExtractContext ExtractContext(Time, bExtractRootMotion);
	FCompactPose& CompactPose = PoseData.GetPose();

	if (!Animation.IsValidAdditive())
	{
		CompactPose.ResetToRefPose();
		Animation.GetAnimationPose(PoseData, ExtractContext);
		return;
	}

	CompactPose.ResetToAdditiveIdentity();
	Animation.GetAnimationPose(PoseData, ExtractContext);

	FCompactPose BasePose;
	BasePose.SetBoneContainer(&RequiredBones);
	FBlendedCurve BaseCurve;
	BaseCurve.InitFrom(RequiredBones);
	UE::Anim::FStackAttributeContainer BaseAttributes;
	FAnimationPoseData BasePoseData(BasePose, BaseCurve, BaseAttributes);
	if (const UAnimSequence* AdditiveSequence = Cast<UAnimSequence>(&Animation))
	{
		AdditiveSequence->GetAdditiveBasePose(BasePoseData, ExtractContext);
	}
	else
	{
		BasePose.ResetToRefPose();
	}

	FAnimationRuntime::AccumulateAdditivePose(BasePoseData, PoseData, 1.f, Animation.GetAdditiveAnimType());
	BasePose.NormalizeRotations();
	CompactPose.CopyBonesFrom(BasePose);
}

/**
 * Evaluates Resolved onto every bone of BoneSource and returns local (parent-relative) transforms
 * indexed like BoneSource's reference skeleton. Keys are raw unless bUseRawData is false. Root motion
 * stays in the root bone. Mirrors UAnimPoseExtensions::GetAnimPoseAtTimeIntervals, the same in 5.7 and 5.8.
 */
TArray<FTransform> PoseAtSampleLocalPose(const FPoseAtResolved& Resolved, const FPoseAtBoneSource& BoneSource, bool bUseRawData)
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
	RequiredBones.SetUseRAWData(bUseRawData);

	FCompactPose CompactPose;
	FBlendedCurve Curve;
	UE::Anim::FStackAttributeContainer Attributes;
	FAnimationPoseData PoseData(CompactPose, Curve, Attributes);
	CompactPose.SetBoneContainer(&RequiredBones);
	Curve.InitFrom(RequiredBones);

	if (Resolved.BlendSpace)
	{
		const bool bExtractRootMotion = false;
		FAnimExtractContext ExtractContext(Resolved.Sample.Time, bExtractRootMotion);
		TArray<FBlendSampleData> Samples = Resolved.BlendSamples;
		CompactPose.ResetToRefPose();
		Resolved.BlendSpace->GetAnimationPose(Samples, ExtractContext, PoseData);
	}
	else
	{
		PoseAtEvaluateAnimation(*Resolved.Animation, Resolved.AnimationTime, RequiredBones, PoseData);
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

// ---- Result ----

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

/** The sample time, and the timeline it lies on. A timeline without frames reports null frame fields. */
void PoseAtSetTimeFields(const TSharedPtr<FJsonObject>& Result, const FPoseAtResolved& Resolved)
{
	Result->SetNumberField(TEXT("time"), Resolved.Sample.Time);
	if (Resolved.Sample.Frame.IsSet())
	{
		Result->SetNumberField(TEXT("frame"), Resolved.Sample.Frame.GetValue());
	}
	else
	{
		Result->SetField(TEXT("frame"), MakeShared<FJsonValueNull>());
	}
	Result->SetNumberField(TEXT("play_length"), Resolved.Timeline.PlayLength);
	if (Resolved.Timeline.bHasFrames)
	{
		Result->SetNumberField(TEXT("frame_count"), Resolved.Timeline.NumKeys);
		Result->SetNumberField(TEXT("frame_rate"), Resolved.Timeline.FrameRate.AsDecimal());
	}
	else
	{
		Result->SetField(TEXT("frame_count"), MakeShared<FJsonValueNull>());
		Result->SetField(TEXT("frame_rate"), MakeShared<FJsonValueNull>());
	}
}
}

FString UAnimPoseAtTool::GetToolDescription() const
{
	return TEXT("Sample an AnimSequence, AnimMontage, or BlendSpace at a frame or time and report each bone's location, rotation, quat, and scale in component or local space, without PIE.");
}

TMap<FString, FBridgeSchemaProperty> UAnimPoseAtTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;
	Schema.Add(TEXT("asset_path"), PoseAtSchemaProperty(TEXT("string"), TEXT("AnimSequence, AnimMontage, or BlendSpace asset path"), true));
	Schema.Add(TEXT("frame"), PoseAtSchemaProperty(TEXT("integer"), TEXT("Frame to sample, 0 to frame_count - 1. Give frame or time, not both. BlendSpaces accept only time.")));
	Schema.Add(TEXT("time"), PoseAtSchemaProperty(TEXT("number"), TEXT("Time in seconds to sample, 0 to play_length. Give frame or time, not both.")));
	FBridgeSchemaProperty Bones = PoseAtSchemaProperty(TEXT("array"), TEXT("Optional bone names to report (default: every bone). Names the mesh lacks are listed in missing_bones."));
	Bones.ItemsType = TEXT("string");
	Schema.Add(TEXT("bones"), Bones);
	Schema.Add(TEXT("space"), PoseAtSchemaProperty(TEXT("string"), TEXT("'component' (default) is relative to the mesh component; 'local' is relative to each bone's parent.")));
	Schema.Add(TEXT("compressed"), PoseAtSchemaProperty(TEXT("boolean"), TEXT("Sample the compressed data the game plays instead of the raw keys (default: false).")));
	Schema.Add(TEXT("mesh_path"), PoseAtSchemaProperty(TEXT("string"), TEXT("Optional SkeletalMesh to sample onto. Defaults to the asset's preview mesh, then the skeleton's. Must use a compatible skeleton.")));
	Schema.Add(TEXT("slot"), PoseAtSchemaProperty(TEXT("string"), TEXT("AnimMontage slot track to sample (default: the first slot).")));
	FBridgeSchemaProperty Blend = PoseAtSchemaProperty(TEXT("array"), TEXT("BlendSpace input: [x] for a 1D BlendSpace, [x, y] for 2D. Required for BlendSpaces."));
	Blend.ItemsType = TEXT("number");
	Schema.Add(TEXT("blend"), Blend);
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
	UObject* Object = FBridgeAssetModifier::LoadAssetByPath(AssetPath, Error);
	if (!Object)
	{
		return PoseAtError(Error);
	}
	const UAnimationAsset* Asset = Cast<UAnimationAsset>(Object);
	if (!Asset)
	{
		return PoseAtError(FString::Printf(TEXT("%s has type %s, which is not supported. Supported types: %s."),
			*Object->GetPathName(), *Object->GetClass()->GetName(), PoseAtSupportedTypes));
	}

	FPoseAtRequest Request;
	if (!PoseAtOptionalFrame(Arguments, Request.Frame, Error) || !PoseAtOptionalBlend(Arguments, Request.Blend, Error))
	{
		return PoseAtError(Error);
	}
	Request.Time = PoseAtOptionalNumber(Arguments, TEXT("time"));
	Request.Slot = GetStringArgOrDefault(Arguments, TEXT("slot"), TEXT(""));

	FString Space;
	if (!PoseAtReadSpace(GetStringArgOrDefault(Arguments, TEXT("space"), TEXT("component")), Space, Error))
	{
		return PoseAtError(Error);
	}

	FPoseAtResolved Resolved;
	if (!PoseAtResolve(*Asset, Request, Resolved, Error))
	{
		return PoseAtError(Error);
	}

	FPoseAtBoneSource BoneSource;
	if (!PoseAtResolveBoneSource(GetStringArgOrDefault(Arguments, TEXT("mesh_path"), TEXT("")), *Asset, BoneSource, Error))
	{
		return PoseAtError(Error);
	}

	const bool bCompressed = GetBoolArgOrDefault(Arguments, TEXT("compressed"), false);
	const TArray<FTransform> LocalPose = PoseAtSampleLocalPose(Resolved, BoneSource, !bCompressed);
	const TArray<FTransform> Pose = Space == TEXT("local")
		? LocalPose
		: SoftUE::BonePose::ComponentSpaceFromLocal(*BoneSource.RefSkeleton, LocalPose);
	const TArray<FName> BoneFilter = SoftUE::BonePose::ParseBoneFilter(Arguments);
	const TArray<TSharedPtr<FJsonValue>> BonesJson = PoseAtBonesToJson(*BoneSource.RefSkeleton, Pose, BoneFilter);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Result->SetStringField(TEXT("asset_type"), Asset->GetClass()->GetName());
	Result->SetStringField(TEXT("space"), Space);
	PoseAtSetTimeFields(Result, Resolved);
	// FJsonObject's key type differs in 5.8, so fields are copied by name through the shared helper.
	TArray<FString> DetailNames;
	SoftUE::JsonObjectUtils::GetFieldNames(Resolved.Details, DetailNames);
	for (const FString& Name : DetailNames)
	{
		Result->SetField(Name, SoftUE::JsonObjectUtils::FindField(Resolved.Details, Name));
	}
	Result->SetStringField(TEXT("pose_source"), BoneSource.Asset->GetPathName());
	Result->SetStringField(TEXT("evaluation"), bCompressed ? TEXT("compressed") : TEXT("raw"));
	Result->SetNumberField(TEXT("bone_count"), BonesJson.Num());
	Result->SetArrayField(TEXT("bones"), BonesJson);
	Result->SetArrayField(TEXT("missing_bones"), PoseAtMissingBones(*BoneSource.RefSkeleton, BoneFilter));

	const UAnimSequence* SampledSequence = Cast<UAnimSequence>(Resolved.Animation);
	if (SampledSequence && SampledSequence->IsValidAdditive())
	{
		Result->SetObjectField(TEXT("additive"), PoseAtAdditiveToJson(*SampledSequence));
		Resolved.Warnings.Add(TEXT("This is an additive animation. Its offsets were applied onto its base pose (see additive) to give a full pose."));
	}
	else if (Resolved.Animation && Resolved.Animation->IsValidAdditive())
	{
		// Only an AnimSequence names a base pose, so other additive assets report raw offsets.
		Resolved.Warnings.Add(FString::Printf(
			TEXT("%s is additive but not an AnimSequence, so its offsets were applied onto the reference pose, not a base pose."),
			*Resolved.Animation->GetPathName()));
	}
	Result->SetArrayField(TEXT("warnings"), PoseAtStringsToJson(Resolved.Warnings));
	return FBridgeToolResult::Json(Result);
}
