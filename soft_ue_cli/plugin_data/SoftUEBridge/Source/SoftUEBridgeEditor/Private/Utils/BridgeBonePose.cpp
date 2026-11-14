// Copyright soft-ue-expert. All Rights Reserved.

#include "Utils/BridgeBonePose.h"

#include "ReferenceSkeleton.h"

namespace SoftUE::BonePose
{
TArray<TSharedPtr<FJsonValue>> VectorToJson(const FVector& Value)
{
	return {
		MakeShared<FJsonValueNumber>(Value.X),
		MakeShared<FJsonValueNumber>(Value.Y),
		MakeShared<FJsonValueNumber>(Value.Z),
	};
}

TArray<TSharedPtr<FJsonValue>> RotatorToJson(const FRotator& Value)
{
	return {
		MakeShared<FJsonValueNumber>(Value.Pitch),
		MakeShared<FJsonValueNumber>(Value.Yaw),
		MakeShared<FJsonValueNumber>(Value.Roll),
	};
}

TArray<TSharedPtr<FJsonValue>> QuatToJson(const FQuat& Value)
{
	return {
		MakeShared<FJsonValueNumber>(Value.X),
		MakeShared<FJsonValueNumber>(Value.Y),
		MakeShared<FJsonValueNumber>(Value.Z),
		MakeShared<FJsonValueNumber>(Value.W),
	};
}

void SetTransformFields(const TSharedPtr<FJsonObject>& Json, const FTransform& Transform)
{
	Json->SetArrayField(TEXT("location"), VectorToJson(Transform.GetLocation()));
	Json->SetArrayField(TEXT("rotation"), RotatorToJson(Transform.Rotator()));
	Json->SetArrayField(TEXT("quat"), QuatToJson(Transform.GetRotation()));
	Json->SetArrayField(TEXT("scale"), VectorToJson(Transform.GetScale3D()));
}

TArray<FTransform> ComponentSpaceFromLocal(const TArray<int32>& ParentIndices, const TArray<FTransform>& LocalPose)
{
	TArray<FTransform> ComponentPose;
	if (ParentIndices.Num() != LocalPose.Num())
	{
		return ComponentPose;
	}

	ComponentPose.SetNum(LocalPose.Num());
	for (int32 BoneIndex = 0; BoneIndex < LocalPose.Num(); ++BoneIndex)
	{
		const int32 ParentIndex = ParentIndices[BoneIndex];
		if (ParentIndex == INDEX_NONE)
		{
			ComponentPose[BoneIndex] = LocalPose[BoneIndex];
			continue;
		}
		if (ParentIndex < 0 || ParentIndex >= BoneIndex)
		{
			return {};
		}
		ComponentPose[BoneIndex] = LocalPose[BoneIndex] * ComponentPose[ParentIndex];
	}
	return ComponentPose;
}

TArray<FTransform> ComponentSpaceFromLocal(const FReferenceSkeleton& RefSkeleton, const TArray<FTransform>& LocalPose)
{
	TArray<int32> ParentIndices;
	ParentIndices.SetNum(RefSkeleton.GetNum());
	for (int32 BoneIndex = 0; BoneIndex < ParentIndices.Num(); ++BoneIndex)
	{
		ParentIndices[BoneIndex] = RefSkeleton.GetParentIndex(BoneIndex);
	}
	return ComponentSpaceFromLocal(ParentIndices, LocalPose);
}

TArray<FName> ParseBoneFilter(const TSharedPtr<FJsonObject>& Arguments, const FString& Field)
{
	TArray<FName> Bones;
	if (!Arguments.IsValid() || !Arguments->HasField(Field))
	{
		return Bones;
	}

	TArray<FString> Raw;
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	FString Joined;
	if (Arguments->TryGetArrayField(Field, Values) && Values)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			// TryGetString also converts numbers and booleans, so check the type: only strings name bones.
			if (Value.IsValid() && Value->Type == EJson::String)
			{
				Raw.Add(Value->AsString());
			}
		}
	}
	else if (Arguments->TryGetStringField(Field, Joined))
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
}
