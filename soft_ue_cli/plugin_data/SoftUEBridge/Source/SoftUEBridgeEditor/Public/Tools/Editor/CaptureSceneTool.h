// Copyright soft-ue-expert. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BridgeToolBase.h"
#include "CaptureSceneTool.generated.h"

class USceneComponent;

/**
 * Renders the world from cameras placed around a target actor or component, one image per angle.
 *
 * A temporary USceneCaptureComponent2D in the transient package does the rendering, so the
 * editor viewport does not move and nothing is added to the level, the Outliner, or the undo
 * history. Camera placement comes from SoftUE::SceneFraming; image encoding and file output come
 * from SoftUE::ImageOutput.
 */
UCLASS()
class SOFTUEBRIDGEEDITOR_API UCaptureSceneTool : public UBridgeToolBase
{
	GENERATED_BODY()

public:
	virtual FString GetToolName() const override { return TEXT("capture-scene"); }

	virtual FString GetToolDescription() const override
	{
		return TEXT("Render the world from cameras placed around an actor or component and return one image per angle. "
				   "Named angles (front, back, left, right, side, top, three-quarter) are relative to the target actor's facing. "
				   "Uses a temporary scene capture, so the editor viewport does not move.");
	}

	virtual TMap<FString, FBridgeSchemaProperty> GetInputSchema() const override;
	virtual TArray<FString> GetRequiredParams() const override;

	virtual FBridgeToolResult Execute(
		const TSharedPtr<FJsonObject>& Arguments,
		const FBridgeToolContext& Context) override;

private:
	/**
	 * Resolves an actor path, an exact actor name or label, or Actor.Component. Paths are tried
	 * first because they are unambiguous; a path to a component resolves that component. A name or
	 * label that matches more than one actor is an error that lists the matching actor paths.
	 * OutComponent is null when the target is a whole actor.
	 */
	static bool ResolveTarget(UWorld* World, const FString& Target, AActor*& OutActor, USceneComponent*& OutComponent, FString& OutError);
};
