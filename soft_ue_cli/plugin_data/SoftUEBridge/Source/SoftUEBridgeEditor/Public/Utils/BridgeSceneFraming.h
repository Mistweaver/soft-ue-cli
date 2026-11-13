// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Named camera angles for capture scene. Each one is relative to the target's facing. */
enum class EBridgeSceneAngle : uint8
{
	Front,
	Back,
	Left,
	Right,
	Top,
	ThreeQuarter,
};

/** A camera pose in world space. */
struct SOFTUEBRIDGEEDITOR_API FBridgeSceneCamera
{
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
};

/**
 * Camera placement for capture scene. Plain math with no world, actor, or renderer, so the
 * automation spec covers it without an editor scene.
 */
namespace SoftUE::SceneFraming
{
	/** Horizontal field of view used when the caller does not give one. */
	inline constexpr float DefaultFovDegrees = 60.0f;

	/** Pitch below the horizon for the three-quarter angle. */
	inline constexpr float ThreeQuarterPitchDegrees = 20.0f;

	/** Extra room around the bounds sphere when the distance is fitted. */
	inline constexpr float FitMargin = 1.1f;

	/**
	 * Parses angle names, ignoring case and surrounding spaces. "side" is the same as "right".
	 * An empty list gives front, right, top. A repeated angle is kept once, in first-seen order.
	 * Returns false with OutError listing the valid names when a name is unknown.
	 */
	SOFTUEBRIDGEEDITOR_API bool ParseAngles(const TArray<FString>& Names, TArray<EBridgeSceneAngle>& OutAngles, FString& OutError);

	/** Lowercase name of an angle as reported in results, such as "three-quarter". */
	SOFTUEBRIDGEEDITOR_API FString AngleName(EBridgeSceneAngle Angle);

	/**
	 * Distance from the bounds center that fits a sphere of BoundsRadius in the narrower of the
	 * horizontal and vertical field of view, times FitMargin. Fails when the radius is not positive,
	 * the field of view is outside (0, 180), or the aspect ratio is not positive.
	 */
	SOFTUEBRIDGEEDITOR_API bool ComputeFitDistance(float BoundsRadius, float HorizontalFovDegrees, float AspectRatio, float& OutDistance, FString& OutError);

	/**
	 * Yaw that named angles treat as the target's front. Skeletal meshes are imported facing their
	 * component's +Y, so when the target has one its facing is the mesh component's yaw plus 90. This
	 * is right for a Character, whose mesh is turned -90 inside the actor, and for a bare
	 * SkeletalMeshActor alike. Without a skeletal mesh the actor's own yaw is the facing.
	 * YawOffsetDegrees is added last, for meshes authored facing another way. The result is
	 * normalized to (-180, 180].
	 */
	SOFTUEBRIDGEEDITOR_API float ComputeFacingYaw(float ActorYawDegrees, bool bHasSkeletalMesh, float MeshComponentYawDegrees, float YawOffsetDegrees);

	/**
	 * Camera at Distance from Center for Angle, looking at Center. Only the target's yaw is used,
	 * so top stays above the target when it is pitched or rolled. Top keeps the target's yaw, so
	 * the target's front points to the top of the image.
	 */
	SOFTUEBRIDGEEDITOR_API FBridgeSceneCamera ComputeCamera(const FVector& Center, float TargetYawDegrees, EBridgeSceneAngle Angle, float Distance);
}
