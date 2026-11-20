// Copyright soft-ue-expert. All Rights Reserved.

#include "Utils/BridgeSceneFraming.h"

namespace
{
	struct FSceneFramingAngleName
	{
		const TCHAR* Name;
		EBridgeSceneAngle Angle;
	};

	// Unity builds share anonymous namespaces, so names here carry a file-specific prefix.
	const FSceneFramingAngleName SceneFramingAngleNames[] = {
		{TEXT("front"), EBridgeSceneAngle::Front},
		{TEXT("back"), EBridgeSceneAngle::Back},
		{TEXT("left"), EBridgeSceneAngle::Left},
		{TEXT("right"), EBridgeSceneAngle::Right},
		{TEXT("side"), EBridgeSceneAngle::Right},
		{TEXT("top"), EBridgeSceneAngle::Top},
		{TEXT("three-quarter"), EBridgeSceneAngle::ThreeQuarter},
	};

	FString SceneFramingValidNames()
	{
		TArray<FString> Names;
		for (const FSceneFramingAngleName& Entry : SceneFramingAngleNames)
		{
			Names.Add(Entry.Name);
		}
		return FString::Join(Names, TEXT(", "));
	}
}

bool SoftUE::SceneFraming::ParseAngles(const TArray<FString>& Names, TArray<EBridgeSceneAngle>& OutAngles, FString& OutError)
{
	OutAngles.Reset();

	if (Names.Num() == 0)
	{
		OutAngles = {EBridgeSceneAngle::Front, EBridgeSceneAngle::Right, EBridgeSceneAngle::Top};
		return true;
	}

	for (const FString& RawName : Names)
	{
		const FString Name = RawName.TrimStartAndEnd();
		const FSceneFramingAngleName* Match = nullptr;
		for (const FSceneFramingAngleName& Entry : SceneFramingAngleNames)
		{
			if (Name.Equals(Entry.Name, ESearchCase::IgnoreCase))
			{
				Match = &Entry;
				break;
			}
		}

		if (!Match)
		{
			OutAngles.Reset();
			OutError = FString::Printf(TEXT("Unknown angle '%s'. Valid angles: %s"), *Name, *SceneFramingValidNames());
			return false;
		}
		OutAngles.AddUnique(Match->Angle);
	}
	return true;
}

FString SoftUE::SceneFraming::AngleName(EBridgeSceneAngle Angle)
{
	switch (Angle)
	{
	case EBridgeSceneAngle::Front:        return TEXT("front");
	case EBridgeSceneAngle::Back:         return TEXT("back");
	case EBridgeSceneAngle::Left:         return TEXT("left");
	case EBridgeSceneAngle::Right:        return TEXT("right");
	case EBridgeSceneAngle::Top:          return TEXT("top");
	case EBridgeSceneAngle::ThreeQuarter: return TEXT("three-quarter");
	}
	return TEXT("unknown");
}

bool SoftUE::SceneFraming::ComputeFitDistance(float BoundsRadius, float HorizontalFovDegrees, float AspectRatio, float& OutDistance, FString& OutError)
{
	if (!(BoundsRadius > UE_KINDA_SMALL_NUMBER))
	{
		OutError = TEXT("The target has no measurable bounds; pass a distance");
		return false;
	}
	if (!(HorizontalFovDegrees > 0.0f && HorizontalFovDegrees < 180.0f))
	{
		OutError = FString::Printf(TEXT("Field of view must be between 0 and 180 degrees, got %g"), HorizontalFovDegrees);
		return false;
	}
	if (!(AspectRatio > 0.0f))
	{
		OutError = FString::Printf(TEXT("Aspect ratio must be positive, got %g"), AspectRatio);
		return false;
	}

	const float HorizontalHalf = FMath::DegreesToRadians(HorizontalFovDegrees) * 0.5f;
	const float VerticalHalf = FMath::Atan(FMath::Tan(HorizontalHalf) / AspectRatio);
	const float NarrowHalf = FMath::Min(HorizontalHalf, VerticalHalf);

	OutDistance = BoundsRadius / FMath::Sin(NarrowHalf) * FitMargin;
	return true;
}

FBridgeSceneCamera SoftUE::SceneFraming::ComputeCamera(const FVector& Center, float TargetYawDegrees, EBridgeSceneAngle Angle, float Distance)
{
	const FRotator Facing(0.0f, TargetYawDegrees, 0.0f);
	const FVector Forward = Facing.Vector();
	const FVector Right = FRotationMatrix(Facing).GetUnitAxis(EAxis::Y);

	FVector Direction = Forward;
	switch (Angle)
	{
	case EBridgeSceneAngle::Front: Direction = Forward; break;
	case EBridgeSceneAngle::Back:  Direction = -Forward; break;
	case EBridgeSceneAngle::Left:  Direction = -Right; break;
	case EBridgeSceneAngle::Right: Direction = Right; break;
	case EBridgeSceneAngle::Top:   Direction = FVector::UpVector; break;
	case EBridgeSceneAngle::ThreeQuarter:
	{
		const float Pitch = FMath::DegreesToRadians(ThreeQuarterPitchDegrees);
		Direction = (Forward + Right).GetSafeNormal() * FMath::Cos(Pitch) + FVector::UpVector * FMath::Sin(Pitch);
		break;
	}
	}

	FBridgeSceneCamera Camera;
	Camera.Location = Center + Direction * Distance;
	if (Angle == EBridgeSceneAngle::Top)
	{
		// Looking straight down leaves yaw free; keep the target's so its front is at the top of the image.
		Camera.Rotation = FRotator(-90.0f, TargetYawDegrees, 0.0f);
	}
	else
	{
		Camera.Rotation = (-Direction).Rotation();
	}
	return Camera;
}

float SoftUE::SceneFraming::ComputeFacingYaw(float ActorYawDegrees, bool bHasSkeletalMesh, float MeshComponentYawDegrees, float YawOffsetDegrees)
{
	const float Base = bHasSkeletalMesh ? MeshComponentYawDegrees + 90.0f : ActorYawDegrees;
	return FRotator::NormalizeAxis(Base + YawOffsetDegrees);
}
