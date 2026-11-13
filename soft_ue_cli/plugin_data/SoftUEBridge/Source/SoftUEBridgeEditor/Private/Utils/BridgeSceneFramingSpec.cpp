// Copyright soft-ue-expert. All Rights Reserved.

#include "Utils/BridgeSceneFraming.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeSceneFramingSpec,
	"SoftUEBridge.Capture.SceneFraming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBridgeSceneFramingSpec::RunTest(const FString& Parameters)
{
	using namespace SoftUE::SceneFraming;

	const FVector Center(10.0, 20.0, 30.0);
	const float Distance = 100.0f;
	const float Tolerance = 0.01f;

	auto Parse = [](const TArray<FString>& Names, TArray<EBridgeSceneAngle>& OutAngles, FString& OutError)
	{
		OutAngles.Reset();
		OutError.Reset();
		return ParseAngles(Names, OutAngles, OutError);
	};

	// The camera must look at the bounds center from wherever it is placed.
	auto TestLooksAtCenter = [this, &Center](const TCHAR* What, const FBridgeSceneCamera& Camera)
	{
		const FVector Expected = (Center - Camera.Location).GetSafeNormal();
		TestEqual(What, Camera.Rotation.Vector(), Expected, 0.001f);
	};

	// empty angle list gives front, side, top
	{
		TArray<EBridgeSceneAngle> Angles;
		FString Error;
		TestTrue(TEXT("empty list parses"), Parse({}, Angles, Error));
		TestEqual(TEXT("default angle count"), Angles.Num(), 3);
		if (Angles.Num() == 3)
		{
			TestTrue(TEXT("default first is front"), Angles[0] == EBridgeSceneAngle::Front);
			TestTrue(TEXT("default second is side (right)"), Angles[1] == EBridgeSceneAngle::Right);
			TestTrue(TEXT("default third is top"), Angles[2] == EBridgeSceneAngle::Top);
		}
	}

	// every angle name parses, in any case, with spaces trimmed
	{
		TArray<EBridgeSceneAngle> Angles;
		FString Error;
		TestTrue(TEXT("all names parse"), Parse({TEXT(" FRONT "), TEXT("back"), TEXT("Left"), TEXT("right"), TEXT("top"), TEXT("Three-Quarter")}, Angles, Error));
		const TArray<EBridgeSceneAngle> Expected = {
			EBridgeSceneAngle::Front, EBridgeSceneAngle::Back, EBridgeSceneAngle::Left,
			EBridgeSceneAngle::Right, EBridgeSceneAngle::Top, EBridgeSceneAngle::ThreeQuarter};
		TestTrue(TEXT("all names map in order"), Angles == Expected);

		TestTrue(TEXT("side parses"), Parse({TEXT("side")}, Angles, Error));
		TestTrue(TEXT("side is right"), Angles.Num() == 1 && Angles[0] == EBridgeSceneAngle::Right);

		TestEqual(TEXT("three-quarter name"), AngleName(EBridgeSceneAngle::ThreeQuarter), FString(TEXT("three-quarter")));
		TestEqual(TEXT("right name"), AngleName(EBridgeSceneAngle::Right), FString(TEXT("right")));
	}

	// an unknown name fails and the error lists the valid names
	{
		TArray<EBridgeSceneAngle> Angles;
		FString Error;
		TestFalse(TEXT("unknown name fails"), Parse({TEXT("front"), TEXT("diagonal")}, Angles, Error));
		TestTrue(TEXT("error names the bad angle"), Error.Contains(TEXT("diagonal")));
		TestTrue(TEXT("error lists valid names"), Error.Contains(TEXT("three-quarter")) && Error.Contains(TEXT("side")));
	}

	// a repeated angle is kept once, in first-seen order
	{
		TArray<EBridgeSceneAngle> Angles;
		FString Error;
		TestTrue(TEXT("repeats parse"), Parse({TEXT("front"), TEXT("side"), TEXT("right"), TEXT("front")}, Angles, Error));
		const TArray<EBridgeSceneAngle> Expected = {EBridgeSceneAngle::Front, EBridgeSceneAngle::Right};
		TestTrue(TEXT("repeats removed"), Angles == Expected);
	}

	// front at yaw 0 sits on +X and looks back at the center
	{
		const FBridgeSceneCamera Camera = ComputeCamera(Center, 0.0f, EBridgeSceneAngle::Front, Distance);
		TestEqual(TEXT("front yaw 0 location"), Camera.Location, FVector(110.0, 20.0, 30.0), Tolerance);
		TestLooksAtCenter(TEXT("front yaw 0 looks at center"), Camera);
	}

	// front follows the target's yaw
	{
		const FBridgeSceneCamera Camera = ComputeCamera(Center, 90.0f, EBridgeSceneAngle::Front, Distance);
		TestEqual(TEXT("front yaw 90 location"), Camera.Location, FVector(10.0, 120.0, 30.0), Tolerance);
		TestLooksAtCenter(TEXT("front yaw 90 looks at center"), Camera);
	}

	// right sits on the target's right side, which is +Y at yaw 0 and -X at yaw 90
	{
		const FBridgeSceneCamera AtZero = ComputeCamera(Center, 0.0f, EBridgeSceneAngle::Right, Distance);
		TestEqual(TEXT("right yaw 0 location"), AtZero.Location, FVector(10.0, 120.0, 30.0), Tolerance);
		TestLooksAtCenter(TEXT("right yaw 0 looks at center"), AtZero);

		const FBridgeSceneCamera AtNinety = ComputeCamera(Center, 90.0f, EBridgeSceneAngle::Right, Distance);
		TestEqual(TEXT("right yaw 90 location"), AtNinety.Location, FVector(-90.0, 20.0, 30.0), Tolerance);
		TestLooksAtCenter(TEXT("right yaw 90 looks at center"), AtNinety);
	}

	// top looks straight down and keeps the target's yaw
	{
		const FBridgeSceneCamera Camera = ComputeCamera(Center, 90.0f, EBridgeSceneAngle::Top, Distance);
		TestEqual(TEXT("top location"), Camera.Location, FVector(10.0, 20.0, 130.0), Tolerance);
		TestEqual(TEXT("top pitch"), static_cast<float>(Camera.Rotation.Pitch), -90.0f, Tolerance);
		TestEqual(TEXT("top yaw"), static_cast<float>(Camera.Rotation.Yaw), 90.0f, Tolerance);
	}

	// three-quarter sits halfway between front and right, above the center, pitched down
	{
		const FBridgeSceneCamera Camera = ComputeCamera(Center, 0.0f, EBridgeSceneAngle::ThreeQuarter, Distance);
		const double Pitch = FMath::DegreesToRadians(static_cast<double>(ThreeQuarterPitchDegrees));
		const FVector Direction(FMath::Cos(Pitch) * UE_INV_SQRT_2, FMath::Cos(Pitch) * UE_INV_SQRT_2, FMath::Sin(Pitch));
		TestEqual(TEXT("three-quarter location"), Camera.Location, Center + Direction * Distance, Tolerance);
		TestEqual(TEXT("three-quarter pitch"), static_cast<float>(Camera.Rotation.Pitch), -ThreeQuarterPitchDegrees, Tolerance);
		TestLooksAtCenter(TEXT("three-quarter looks at center"), Camera);
	}

	// back and left are opposite front and right
	{
		const FBridgeSceneCamera Back = ComputeCamera(Center, 0.0f, EBridgeSceneAngle::Back, Distance);
		TestEqual(TEXT("back location"), Back.Location, FVector(-90.0, 20.0, 30.0), Tolerance);
		TestLooksAtCenter(TEXT("back looks at center"), Back);

		const FBridgeSceneCamera Left = ComputeCamera(Center, 0.0f, EBridgeSceneAngle::Left, Distance);
		TestEqual(TEXT("left location"), Left.Location, FVector(10.0, -80.0, 30.0), Tolerance);
		TestLooksAtCenter(TEXT("left looks at center"), Left);
	}

	// fitted distance fits the sphere in the narrower field of view, with margin
	{
		float Fitted = 0.0f;
		FString Error;
		TestTrue(TEXT("square fit succeeds"), ComputeFitDistance(100.0f, 90.0f, 1.0f, Fitted, Error));
		TestEqual(TEXT("square uses 45 degree half angle"), Fitted, 100.0f / FMath::Sin(FMath::DegreesToRadians(45.0f)) * FitMargin, Tolerance);

		// Wide image: the vertical field of view is narrower and decides the distance.
		TestTrue(TEXT("wide fit succeeds"), ComputeFitDistance(100.0f, 90.0f, 2.0f, Fitted, Error));
		const float VerticalHalf = FMath::Atan(FMath::Tan(FMath::DegreesToRadians(45.0f)) / 2.0f);
		TestEqual(TEXT("wide uses vertical half angle"), Fitted, 100.0f / FMath::Sin(VerticalHalf) * FitMargin, Tolerance);

		// Tall image: the horizontal field of view is narrower.
		TestTrue(TEXT("tall fit succeeds"), ComputeFitDistance(100.0f, 90.0f, 0.5f, Fitted, Error));
		TestEqual(TEXT("tall uses horizontal half angle"), Fitted, 100.0f / FMath::Sin(FMath::DegreesToRadians(45.0f)) * FitMargin, Tolerance);
	}

	// fitting rejects a field of view outside (0, 180), a non-positive radius, and a bad aspect ratio
	{
		float Fitted = 0.0f;
		FString Error;
		TestFalse(TEXT("fov 0 fails"), ComputeFitDistance(100.0f, 0.0f, 1.0f, Fitted, Error));
		TestFalse(TEXT("fov 180 fails"), ComputeFitDistance(100.0f, 180.0f, 1.0f, Fitted, Error));
		TestFalse(TEXT("negative fov fails"), ComputeFitDistance(100.0f, -5.0f, 1.0f, Fitted, Error));
		TestFalse(TEXT("zero radius fails"), ComputeFitDistance(0.0f, 60.0f, 1.0f, Fitted, Error));
		TestFalse(TEXT("zero aspect fails"), ComputeFitDistance(100.0f, 60.0f, 0.0f, Fitted, Error));
		TestFalse(TEXT("failure explains itself"), Error.IsEmpty());
	}


	// facing follows the actor without a skeletal mesh, and the mesh's +Y with one
	{
		TestEqual(TEXT("no mesh uses actor yaw"), ComputeFacingYaw(30.0f, false, 0.0f, 0.0f), 30.0f, Tolerance);
		// A bare SkeletalMeshActor: the mesh component shares the actor's yaw, and the mesh faces +Y.
		TestEqual(TEXT("bare skeletal mesh faces component +Y"), ComputeFacingYaw(30.0f, true, 30.0f, 0.0f), 120.0f, Tolerance);
		// A Character: the mesh component is turned -90 inside the actor, so the facing is the actor's yaw.
		TestEqual(TEXT("character mesh faces actor forward"), ComputeFacingYaw(30.0f, true, -60.0f, 0.0f), 30.0f, Tolerance);
		TestEqual(TEXT("offset is added"), ComputeFacingYaw(30.0f, false, 0.0f, -45.0f), -15.0f, Tolerance);
		TestEqual(TEXT("result is normalized"), ComputeFacingYaw(170.0f, true, 170.0f, 0.0f), -100.0f, Tolerance);
	}

	return true;
}

#endif
