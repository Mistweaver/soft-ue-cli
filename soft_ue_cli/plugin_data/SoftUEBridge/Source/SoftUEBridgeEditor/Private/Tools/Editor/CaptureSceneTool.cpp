// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Editor/CaptureSceneTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "TextureResource.h"
#include "Tools/PIE/BridgePIEWorlds.h"
#include "UObject/Package.h"
#include "Utils/BridgeImageOutput.h"
#include "Utils/BridgeSceneFraming.h"

namespace
{
	// Unity builds share anonymous namespaces, so names here carry a file-specific prefix.
	const TCHAR* const CaptureSceneFilePrefix = TEXT("scene_");
	constexpr int32 CaptureSceneDefaultSize = 1024;
	constexpr int32 CaptureSceneMinSize = 16;
	constexpr int32 CaptureSceneMaxSize = 4096;

	// Scene captures have no auto exposure, temporal AA, or Lumen history on their first frame, and
	// each angle starts with a camera cut that clears that history. Rendering a few frames per angle
	// lets it settle before the image that is kept.
	constexpr int32 CaptureSceneWarmupFrames = 2;

	// How many matching actor paths an ambiguous-name error lists.
	constexpr int32 CaptureSceneMaxListedMatches = 5;

	TArray<FString> CaptureSceneReadAngleNames(const TSharedPtr<FJsonObject>& Arguments)
	{
		TArray<FString> Names;
		const TArray<TSharedPtr<FJsonValue>>* AngleValues = nullptr;
		if (Arguments->TryGetArrayField(TEXT("angles"), AngleValues))
		{
			for (const TSharedPtr<FJsonValue>& Value : *AngleValues)
			{
				Names.Add(Value.IsValid() ? Value->AsString() : FString());
			}
			return Names;
		}

		FString Csv;
		if (Arguments->TryGetStringField(TEXT("angles"), Csv))
		{
			Csv.ParseIntoArray(Names, TEXT(","), true);
		}
		return Names;
	}

	/** Reads a 3-number array. Returns false with OutError when the field is present but malformed. */
	bool CaptureSceneReadTriple(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* Key, bool& bOutPresent, FVector& OutValue, FString& OutError)
	{
		bOutPresent = Arguments->HasField(Key);
		if (!bOutPresent)
		{
			return true;
		}

		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Arguments->TryGetArrayField(Key, Values) || Values->Num() != 3)
		{
			OutError = FString::Printf(TEXT("%s must be an array of 3 numbers"), Key);
			return false;
		}
		double Parts[3];
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (!(*Values)[Index].IsValid() || !(*Values)[Index]->TryGetNumber(Parts[Index]))
			{
				OutError = FString::Printf(TEXT("%s must be an array of 3 numbers"), Key);
				return false;
			}
		}
		OutValue = FVector(Parts[0], Parts[1], Parts[2]);
		return true;
	}

	TArray<TSharedPtr<FJsonValue>> CaptureSceneVectorJson(double X, double Y, double Z)
	{
		return {MakeShared<FJsonValueNumber>(X), MakeShared<FJsonValueNumber>(Y), MakeShared<FJsonValueNumber>(Z)};
	}

	FString CaptureSceneAmbiguousError(const FString& Name, const TArray<AActor*>& Matches)
	{
		TArray<FString> Paths;
		for (int32 Index = 0; Index < Matches.Num() && Index < CaptureSceneMaxListedMatches; ++Index)
		{
			Paths.Add(Matches[Index]->GetPathName());
		}
		const FString More = Matches.Num() > CaptureSceneMaxListedMatches
			? FString::Printf(TEXT(", and %d more"), Matches.Num() - CaptureSceneMaxListedMatches)
			: FString();
		return FString::Printf(TEXT("'%s' matches %d actors; pass an actor path to choose one: %s%s"),
			*Name, Matches.Num(), *FString::Join(Paths, TEXT(", ")), *More);
	}

	/**
	 * The skeletal mesh whose facing stands for the target's: the target component when it is one,
	 * otherwise the actor's top-level skeletal mesh. Modular characters attach clothing meshes to
	 * the body mesh, so the first one not attached to another skeletal mesh is the body.
	 */
	USkeletalMeshComponent* CaptureSceneFindFacingMesh(AActor* Actor, USceneComponent* Component)
	{
		if (USkeletalMeshComponent* TargetMesh = Cast<USkeletalMeshComponent>(Component))
		{
			return TargetMesh;
		}

		TArray<USkeletalMeshComponent*> Meshes;
		Actor->GetComponents(Meshes);
		for (USkeletalMeshComponent* Mesh : Meshes)
		{
			if (Mesh && !Cast<USkeletalMeshComponent>(Mesh->GetAttachParent()))
			{
				return Mesh;
			}
		}
		return Meshes.Num() > 0 ? Meshes[0] : nullptr;
	}
}

TMap<FString, FBridgeSchemaProperty> UCaptureSceneTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	Schema.Add(TEXT("target"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("Actor path, actor name or label (exact, ignoring case), or Actor.Component"),
		true
	});

	FBridgeSchemaProperty AnglesProperty;
	AnglesProperty.Type = TEXT("array");
	AnglesProperty.ItemsType = TEXT("string");
	AnglesProperty.Description = TEXT("Named angles relative to the target's facing: front, back, left, right, side, top, three-quarter (default: front, side, top)");
	Schema.Add(TEXT("angles"), AnglesProperty);

	FBridgeSchemaProperty LocationProperty;
	LocationProperty.Type = TEXT("array");
	LocationProperty.ItemsType = TEXT("number");
	LocationProperty.Description = TEXT("[X, Y, Z] explicit world-space camera location; requires rotation, excludes angles");
	Schema.Add(TEXT("location"), LocationProperty);

	FBridgeSchemaProperty RotationProperty;
	RotationProperty.Type = TEXT("array");
	RotationProperty.ItemsType = TEXT("number");
	RotationProperty.Description = TEXT("[Pitch, Yaw, Roll] explicit world-space camera rotation in degrees; requires location");
	Schema.Add(TEXT("rotation"), RotationProperty);

	Schema.Add(TEXT("distance"), FBridgeSchemaProperty{
		TEXT("number"),
		TEXT("Camera distance in cm from the target's bounds center (default: fit the bounds in view)"),
		false
	});

	Schema.Add(TEXT("fov"), FBridgeSchemaProperty{
		TEXT("number"),
		TEXT("Horizontal field of view in degrees (default: 60)"),
		false
	});

	Schema.Add(TEXT("yaw_offset"), FBridgeSchemaProperty{
		TEXT("number"),
		TEXT("Degrees added to the target's facing for named angles, for meshes authored facing another way (default: 0)"),
		false
	});

	Schema.Add(TEXT("width"), FBridgeSchemaProperty{
		TEXT("integer"),
		TEXT("Render width in pixels (default: 1024, or height when only height is given)"),
		false
	});

	Schema.Add(TEXT("height"), FBridgeSchemaProperty{
		TEXT("integer"),
		TEXT("Render height in pixels (default: 1024, or width when only width is given)"),
		false
	});

	Schema.Add(TEXT("isolate"), FBridgeSchemaProperty{
		TEXT("boolean"),
		TEXT("Render only the target actor and the actors attached to it (default: false)"),
		false
	});

	Schema.Add(TEXT("world"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("World to capture: 'editor' (default) or 'pie'"),
		false,
		{TEXT("editor"), TEXT("pie")}
	});

	Schema.Add(TEXT("pie_instance"), FBridgeSchemaProperty{
		TEXT("integer"),
		TEXT("FWorldContext::PIEInstance of the PIE world; valid only when world is 'pie'"),
		false
	});

	Schema.Add(TEXT("format"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("Image format (default: png)"),
		false,
		{TEXT("png"), TEXT("jpeg")}
	});

	Schema.Add(TEXT("output"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("Output mode: 'file' saves each angle to the temp dir and returns its path (default), 'base64' returns encoded data"),
		false,
		{TEXT("file"), TEXT("base64")}
	});

	Schema.Add(TEXT("scale"), FBridgeSchemaProperty{
		TEXT("number"),
		TEXT("Output scale percentage of the render size. Requests above 100 are clamped."),
		false
	});

	Schema.Add(TEXT("color_mode"), FBridgeSchemaProperty{
		TEXT("string"),
		TEXT("Output color mode: color (default), grayscale, or monochrome"),
		false,
		{TEXT("color"), TEXT("grayscale"), TEXT("monochrome")}
	});

	Schema.Add(TEXT("cleanup_previous"), FBridgeSchemaProperty{
		TEXT("boolean"),
		TEXT("Delete earlier capture-scene files once before writing this call's files (default: false)"),
		false
	});

	return Schema;
}

TArray<FString> UCaptureSceneTool::GetRequiredParams() const
{
	return {TEXT("target")};
}

bool UCaptureSceneTool::ResolveTarget(UWorld* World, const FString& Target, AActor*& OutActor, USceneComponent*& OutComponent, FString& OutError)
{
	OutActor = nullptr;
	OutComponent = nullptr;

	if (Target.StartsWith(TEXT("/")))
	{
		UObject* Object = StaticFindObject(UObject::StaticClass(), nullptr, *Target);
		if (AActor* Actor = Cast<AActor>(Object))
		{
			OutActor = Actor;
		}
		else if (UActorComponent* Component = Cast<UActorComponent>(Object))
		{
			OutComponent = Cast<USceneComponent>(Component);
			if (!OutComponent)
			{
				OutError = FString::Printf(TEXT("component '%s' has no transform or bounds to frame"), *Target);
				return false;
			}
			OutActor = Component->GetOwner();
		}

		if (!OutActor)
		{
			OutError = FString::Printf(TEXT("no actor or component at path '%s'"), *Target);
			return false;
		}
		if (OutActor->GetWorld() != World)
		{
			OutError = FString::Printf(TEXT("'%s' is not in the %s world"), *Target, *World->GetName());
			return false;
		}
		return true;
	}

	// Labels need not be unique, and framing the wrong actor would look like a valid capture.
	const TArray<AActor*> Matches = FindActorsByNameOrLabel(World, Target);
	if (Matches.Num() > 1)
	{
		OutError = CaptureSceneAmbiguousError(Target, Matches);
		return false;
	}
	if (Matches.Num() == 1)
	{
		OutActor = Matches[0];
		return true;
	}

	int32 DotIndex = INDEX_NONE;
	if (Target.FindLastChar(TEXT('.'), DotIndex) && DotIndex > 0)
	{
		const FString ActorName = Target.Left(DotIndex);
		const FString ComponentName = Target.Mid(DotIndex + 1);
		const TArray<AActor*> ActorMatches = FindActorsByNameOrLabel(World, ActorName);
		if (ActorMatches.Num() > 1)
		{
			OutError = CaptureSceneAmbiguousError(ActorName, ActorMatches);
			return false;
		}
		if (AActor* Actor = ActorMatches.Num() == 1 ? ActorMatches[0] : nullptr)
		{
			UActorComponent* Component = FindComponentByName(Actor, ComponentName);
			if (!Component)
			{
				OutError = FString::Printf(TEXT("component '%s' not found on '%s'"), *ComponentName, *ActorName);
				return false;
			}
			OutComponent = Cast<USceneComponent>(Component);
			if (!OutComponent)
			{
				OutError = FString::Printf(TEXT("component '%s' has no transform or bounds to frame"), *ComponentName);
				return false;
			}
			OutActor = Actor;
			return true;
		}
	}

	OutError = FString::Printf(TEXT("actor '%s' not found"), *Target);
	return false;
}

FBridgeToolResult UCaptureSceneTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	auto Fail = [](const FString& Message)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("capture-scene: %s"), *Message));
	};

	const FString Target = GetStringArgOrDefault(Arguments, TEXT("target"));
	if (Target.IsEmpty())
	{
		return Fail(TEXT("target is required"));
	}

	FString Error;

	// Camera: named angles, or one explicit camera.
	bool bHasLocation = false;
	bool bHasRotation = false;
	FVector ExplicitLocation = FVector::ZeroVector;
	FVector ExplicitRotation = FVector::ZeroVector;
	if (!CaptureSceneReadTriple(Arguments, TEXT("location"), bHasLocation, ExplicitLocation, Error)
		|| !CaptureSceneReadTriple(Arguments, TEXT("rotation"), bHasRotation, ExplicitRotation, Error))
	{
		return Fail(Error);
	}
	if (bHasLocation != bHasRotation)
	{
		return Fail(TEXT("location and rotation must be given together"));
	}
	const bool bExplicitCamera = bHasLocation;
	if (bExplicitCamera && Arguments->HasField(TEXT("angles")))
	{
		return Fail(TEXT("angles cannot be combined with location and rotation"));
	}

	TArray<EBridgeSceneAngle> Angles;
	if (!bExplicitCamera && !SoftUE::SceneFraming::ParseAngles(CaptureSceneReadAngleNames(Arguments), Angles, Error))
	{
		return Fail(Error);
	}

	// Render size and lens.
	int32 Width = 0;
	int32 Height = 0;
	const bool bHasWidth = GetIntArg(Arguments, TEXT("width"), Width);
	const bool bHasHeight = GetIntArg(Arguments, TEXT("height"), Height);
	if (!bHasWidth)
	{
		Width = bHasHeight ? Height : CaptureSceneDefaultSize;
	}
	if (!bHasHeight)
	{
		Height = Width;
	}
	if (Width < CaptureSceneMinSize || Width > CaptureSceneMaxSize || Height < CaptureSceneMinSize || Height > CaptureSceneMaxSize)
	{
		return Fail(FString::Printf(TEXT("width and height must be between %d and %d, got %dx%d"), CaptureSceneMinSize, CaptureSceneMaxSize, Width, Height));
	}

	const float Fov = GetFloatArgOrDefault(Arguments, TEXT("fov"), SoftUE::SceneFraming::DefaultFovDegrees);
	if (!(Fov > 0.0f && Fov < 180.0f))
	{
		return Fail(FString::Printf(TEXT("fov must be between 0 and 180 degrees, got %g"), Fov));
	}

	float RequestedDistance = 0.0f;
	const bool bHasDistance = GetFloatArg(Arguments, TEXT("distance"), RequestedDistance);
	if (bHasDistance && !(RequestedDistance > 0.0f))
	{
		return Fail(FString::Printf(TEXT("distance must be positive, got %g"), RequestedDistance));
	}
	const float YawOffset = GetFloatArgOrDefault(Arguments, TEXT("yaw_offset"), 0.0f);
	const bool bIsolate = GetBoolArgOrDefault(Arguments, TEXT("isolate"), false);

	// World.
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("editor"));
	const bool bHasPIEInstance = Arguments->HasField(TEXT("pie_instance"));
	if (WorldType != TEXT("editor") && WorldType != TEXT("pie"))
	{
		return Fail(FString::Printf(TEXT("unknown world '%s'; valid worlds: editor, pie"), *WorldType));
	}
	if (bHasPIEInstance && WorldType != TEXT("pie"))
	{
		return Fail(TEXT("pie_instance is valid only when world is 'pie'"));
	}

	UWorld* World = nullptr;
	int32 SelectedPIEInstance = INDEX_NONE;
	if (WorldType == TEXT("pie"))
	{
		const TArray<FBridgePIEWorld> PIEWorlds = FBridgePIEWorlds::Enumerate();
		const int32 PIEInstance = GetIntArgOrDefault(Arguments, TEXT("pie_instance"), INDEX_NONE);
		const FBridgePIEWorld* Selected = bHasPIEInstance
			? FBridgePIEWorlds::Resolve(PIEWorlds, PIEInstance)
			: (PIEWorlds.Num() > 0 ? &PIEWorlds[0] : nullptr);
		if (!Selected)
		{
			return bHasPIEInstance
				? Fail(FString::Printf(TEXT("PIE instance %d not found; available PIE instances: %s"), PIEInstance, *FBridgePIEWorlds::AvailableInstanceIds(PIEWorlds)))
				: Fail(TEXT("no PIE world available; start PIE first"));
		}
		World = Selected->World;
		SelectedPIEInstance = Selected->PIEInstance;
	}
	else
	{
		World = FindWorldByType(TEXT("editor"));
	}
	if (!World)
	{
		return Fail(FString::Printf(TEXT("no %s world available"), *WorldType));
	}

	// Target, bounds, and facing.
	AActor* Actor = nullptr;
	USceneComponent* TargetComponent = nullptr;
	if (!ResolveTarget(World, Target, Actor, TargetComponent, Error))
	{
		return Fail(Error);
	}

	FVector BoundsCenter;
	float BoundsRadius = 0.0f;
	if (TargetComponent)
	{
		BoundsCenter = TargetComponent->Bounds.Origin;
		BoundsRadius = TargetComponent->Bounds.BoxExtent.Size();
	}
	else
	{
		FVector BoundsExtent;
		Actor->GetActorBounds(false, BoundsCenter, BoundsExtent, true);
		BoundsRadius = BoundsExtent.Size();
	}

	USkeletalMeshComponent* FacingMesh = CaptureSceneFindFacingMesh(Actor, TargetComponent);
	const float FacingYaw = SoftUE::SceneFraming::ComputeFacingYaw(
		Actor->GetActorRotation().Yaw,
		FacingMesh != nullptr,
		FacingMesh ? FacingMesh->GetComponentRotation().Yaw : 0.0f,
		YawOffset);

	float Distance = RequestedDistance;
	if (!bExplicitCamera && !bHasDistance
		&& !SoftUE::SceneFraming::ComputeFitDistance(BoundsRadius, Fov, static_cast<float>(Width) / Height, Distance, Error))
	{
		return Fail(Error);
	}

	// Cameras to render, in order.
	TArray<TPair<FString, FBridgeSceneCamera>> Cameras;
	if (bExplicitCamera)
	{
		FBridgeSceneCamera Camera;
		Camera.Location = ExplicitLocation;
		Camera.Rotation = FRotator(ExplicitRotation.X, ExplicitRotation.Y, ExplicitRotation.Z);
		Cameras.Emplace(TEXT("custom"), Camera);
	}
	else
	{
		for (const EBridgeSceneAngle Angle : Angles)
		{
			Cameras.Emplace(SoftUE::SceneFraming::AngleName(Angle), SoftUE::SceneFraming::ComputeCamera(BoundsCenter, FacingYaw, Angle, Distance));
		}
	}

	FBridgeImageOutputOptions Options = FBridgeImageOutputOptions::FromArguments(Arguments);
	// width and height set the render size here, so they must not resize the output again.
	// Only scale may shrink the output further.
	Options.TargetWidth = 0;
	Options.TargetHeight = 0;
	if (Options.bCleanupPrevious && Options.OutputMode == TEXT("file"))
	{
		// Clean once here. EncodeCapture cleans per image, which would delete this call's earlier angles.
		SoftUE::ImageOutput::DeletePreviousCaptures(CaptureSceneFilePrefix);
	}
	Options.bCleanupPrevious = false;

	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
	// InitCustomFormat calls UpdateResource, which queues the resource ahead of the capture's render commands.
	RenderTarget->InitCustomFormat(Width, Height, PF_B8G8R8A8, false);

	// No owning actor and a transient outer, so nothing is added to the level or its undo history.
	USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(GetTransientPackage(), NAME_None, RF_Transient);
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->FOVAngle = Fov;
	Capture->TextureTarget = RenderTarget;
	// Isolation keeps actors attached to the target, such as a rider seated on it or a held prop,
	// because they are part of the pose being checked.
	TArray<AActor*> IsolatedActors;
	if (bIsolate)
	{
		IsolatedActors.Add(Actor);
		Actor->GetAttachedActors(IsolatedActors, false, true);
		Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		for (AActor* Shown : IsolatedActors)
		{
			Capture->ShowOnlyActors.Add(Shown);
		}
	}
	Capture->RegisterComponentWithWorld(World);

	ON_SCOPE_EXIT
	{
		Capture->TextureTarget = nullptr;
		Capture->DestroyComponent();
		RenderTarget->ReleaseResource();
		RenderTarget->MarkAsGarbage();
	};

	TArray<TSharedPtr<FJsonValue>> Captures;
	for (int32 CameraIndex = 0; CameraIndex < Cameras.Num(); ++CameraIndex)
	{
		const FString& AngleName = Cameras[CameraIndex].Key;
		const FBridgeSceneCamera& Camera = Cameras[CameraIndex].Value;
		Capture->SetWorldLocationAndRotation(Camera.Location, Camera.Rotation);

		// Without a cut, the renderer reuses the previous angle's occlusion results and temporal
		// history, which can cull something visible from this angle or blend in the last view.
		// The engine clears the flag after each capture, so only the first frame here is a cut.
		Capture->bCameraCutThisFrame = true;
		for (int32 Frame = 0; Frame < CaptureSceneWarmupFrames + 1; ++Frame)
		{
			Capture->CaptureScene();
		}

		FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
		TArray<FColor> Pixels;
		if (!Resource || !Resource->ReadPixels(Pixels) || Pixels.Num() != Width * Height)
		{
			return Fail(FString::Printf(TEXT("failed to read pixels for angle '%s'"), *AngleName));
		}

		const FString FilePrefix = FString::Printf(TEXT("%s%s_"), CaptureSceneFilePrefix, *AngleName);
		TSharedPtr<FJsonObject> Image = SoftUE::ImageOutput::EncodeCapture(Pixels, Width, Height, Options, FilePrefix, Error);
		if (!Image.IsValid())
		{
			return Fail(FString::Printf(TEXT("angle '%s': %s"), *AngleName, *Error));
		}

		TSharedPtr<FJsonObject> CameraJson = MakeShared<FJsonObject>();
		CameraJson->SetArrayField(TEXT("location"), CaptureSceneVectorJson(Camera.Location.X, Camera.Location.Y, Camera.Location.Z));
		CameraJson->SetArrayField(TEXT("rotation"), CaptureSceneVectorJson(Camera.Rotation.Pitch, Camera.Rotation.Yaw, Camera.Rotation.Roll));
		CameraJson->SetNumberField(TEXT("fov"), Fov);
		Image->SetObjectField(TEXT("camera"), CameraJson);
		Image->SetStringField(TEXT("angle"), AngleName);
		Captures.Add(MakeShared<FJsonValueObject>(Image));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("target"), GetActorLabelSafe(Actor));
	Result->SetStringField(TEXT("target_path"), Actor->GetPathName());
	if (TargetComponent)
	{
		Result->SetStringField(TEXT("target_component"), TargetComponent->GetName());
	}
	Result->SetStringField(TEXT("world"), WorldType);
	if (SelectedPIEInstance != INDEX_NONE)
	{
		Result->SetNumberField(TEXT("pie_instance"), SelectedPIEInstance);
	}
	TSharedPtr<FJsonObject> BoundsJson = MakeShared<FJsonObject>();
	BoundsJson->SetArrayField(TEXT("center"), CaptureSceneVectorJson(BoundsCenter.X, BoundsCenter.Y, BoundsCenter.Z));
	BoundsJson->SetNumberField(TEXT("radius"), BoundsRadius);
	Result->SetObjectField(TEXT("bounds"), BoundsJson);
	TSharedPtr<FJsonObject> FacingJson = MakeShared<FJsonObject>();
	FacingJson->SetNumberField(TEXT("yaw"), FacingYaw);
	FacingJson->SetStringField(TEXT("source"), FacingMesh ? FString::Printf(TEXT("skeletal_mesh:%s"), *FacingMesh->GetName()) : FString(TEXT("actor")));
	FacingJson->SetNumberField(TEXT("yaw_offset"), YawOffset);
	Result->SetObjectField(TEXT("facing"), FacingJson);
	if (!bExplicitCamera)
	{
		Result->SetNumberField(TEXT("distance"), Distance);
	}
	Result->SetBoolField(TEXT("isolate"), bIsolate);
	if (bIsolate)
	{
		TArray<TSharedPtr<FJsonValue>> IsolatedJson;
		for (const AActor* Shown : IsolatedActors)
		{
			IsolatedJson.Add(MakeShared<FJsonValueString>(GetActorLabelSafe(Shown)));
		}
		Result->SetArrayField(TEXT("isolated_actors"), IsolatedJson);
	}
	Result->SetNumberField(TEXT("render_width"), Width);
	Result->SetNumberField(TEXT("render_height"), Height);
	Result->SetArrayField(TEXT("captures"), Captures);

	UE_LOG(LogSoftUEBridgeEditor, Log, TEXT("capture-scene: %d image(s) of %s in %s world"),
		Captures.Num(), *GetActorLabelSafe(Actor), *WorldType);

	return FBridgeToolResult::Json(Result);
}
