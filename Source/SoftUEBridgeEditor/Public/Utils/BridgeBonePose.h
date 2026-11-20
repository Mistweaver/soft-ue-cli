// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

struct FReferenceSkeleton;

/**
 * Bone transforms and their JSON shape, shared by asset query-physics and anim pose-at. Plain math
 * and JSON with no asset loading, so the automation spec covers it without content.
 */
namespace SoftUE::BonePose
{
	/** [x, y, z] */
	SOFTUEBRIDGEEDITOR_API TArray<TSharedPtr<FJsonValue>> VectorToJson(const FVector& Value);

	/** [pitch, yaw, roll] in degrees. */
	SOFTUEBRIDGEEDITOR_API TArray<TSharedPtr<FJsonValue>> RotatorToJson(const FRotator& Value);

	/** [x, y, z, w] */
	SOFTUEBRIDGEEDITOR_API TArray<TSharedPtr<FJsonValue>> QuatToJson(const FQuat& Value);

	/**
	 * Writes location, rotation ([pitch, yaw, roll]), quat ([x, y, z, w]), and scale onto Json.
	 * quat is exact; rotation is for reading and loses precision near +/-90 degrees pitch.
	 */
	SOFTUEBRIDGEEDITOR_API void SetTransformFields(const TSharedPtr<FJsonObject>& Json, const FTransform& Transform);

	/**
	 * Composes local bone transforms into component space. LocalPose is indexed like RefSkeleton, whose
	 * parents always precede their children. Returns an empty array when the counts differ.
	 */
	SOFTUEBRIDGEEDITOR_API TArray<FTransform> ComponentSpaceFromLocal(const FReferenceSkeleton& RefSkeleton, const TArray<FTransform>& LocalPose);

	/**
	 * Composes local transforms given each bone's parent index (INDEX_NONE for a root). Parents must
	 * precede their children. Returns an empty array when the counts differ or a parent comes after
	 * its child.
	 */
	SOFTUEBRIDGEEDITOR_API TArray<FTransform> ComponentSpaceFromLocal(const TArray<int32>& ParentIndices, const TArray<FTransform>& LocalPose);

	/**
	 * Reads Field from Arguments as a JSON array of names or a comma-separated string. Names are
	 * trimmed, empty names are dropped, and a repeated name is kept once in first-seen order.
	 * A missing field gives an empty array, which means every bone.
	 */
	SOFTUEBRIDGEEDITOR_API TArray<FName> ParseBoneFilter(const TSharedPtr<FJsonObject>& Arguments, const FString& Field = TEXT("bones"));
}
