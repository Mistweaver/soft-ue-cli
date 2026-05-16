// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/PIE/InspectPawnPossessionTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "AIController.h"
#include "Components/SceneComponent.h"

namespace
{
	bool IsActorVisible(const AActor* Actor)
	{
		if (const USceneComponent* Root = Actor ? Actor->GetRootComponent() : nullptr)
		{
			return Root->IsVisible();
		}
		return Actor ? !Actor->IsHiddenEd() : false;
	}

	bool MatchesActorFilter(const AActor* Actor, const FString& ActorName, const FString& ClassFilter)
	{
		if (!Actor)
		{
			return false;
		}

		const bool bMatchesName = ActorName.IsEmpty()
			|| Actor->GetName().Equals(ActorName, ESearchCase::IgnoreCase)
			|| Actor->GetActorNameOrLabel().Equals(ActorName, ESearchCase::IgnoreCase);
		const bool bMatchesClass = ClassFilter.IsEmpty()
			|| Actor->GetClass()->GetName().Equals(ClassFilter, ESearchCase::IgnoreCase);
		return bMatchesName && bMatchesClass;
	}

	TArray<TSharedPtr<FJsonValue>> VectorToJson(const FVector& Value)
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Add(MakeShareable(new FJsonValueNumber(Value.X)));
		Array.Add(MakeShareable(new FJsonValueNumber(Value.Y)));
		Array.Add(MakeShareable(new FJsonValueNumber(Value.Z)));
		return Array;
	}
}

FString UInspectPawnPossessionTool::GetToolDescription() const
{
	return TEXT("Inspect controller/pawn possession links in a running world, including AI auto-possession settings and hidden state.");
}

TMap<FString, FBridgeSchemaProperty> UInspectPawnPossessionTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	auto Prop = [](const FString& Type, const FString& Desc) {
		FBridgeSchemaProperty P; P.Type = Type; P.Description = Desc; return P;
	};

	Schema.Add(TEXT("world"), Prop(TEXT("string"), TEXT("World context: pie, editor, or game")));
	Schema.Add(TEXT("class_filter"), Prop(TEXT("string"), TEXT("Optional pawn class filter")));
	Schema.Add(TEXT("actor_name"), Prop(TEXT("string"), TEXT("Optional actor/controller name filter")));
	Schema.Add(TEXT("include_hidden"), Prop(TEXT("boolean"), TEXT("Reserved for future filtering behavior")));
	return Schema;
}

FBridgeToolResult UInspectPawnPossessionTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString WorldType = GetStringArgOrDefault(Arguments, TEXT("world"), TEXT("pie"));
	const FString ClassFilter = GetStringArgOrDefault(Arguments, TEXT("class_filter"));
	const FString ActorName = GetStringArgOrDefault(Arguments, TEXT("actor_name"));

	UWorld* World = FindWorldByType(WorldType);
	if (!World)
	{
		return FBridgeToolResult::Error(FString::Printf(
			TEXT("inspect-pawn-possession: no %s world available"), *WorldType));
	}

	TArray<TSharedPtr<FJsonValue>> ControllersArray;
	for (TActorIterator<AController> It(World); It; ++It)
	{
		AController* Controller = *It;
		if (!MatchesActorFilter(Controller, ActorName, TEXT("")))
		{
			continue;
		}

		TSharedPtr<FJsonObject> ControllerJson = MakeShareable(new FJsonObject);
		ControllerJson->SetStringField(TEXT("name"), Controller->GetName());
		ControllerJson->SetStringField(TEXT("label"), Controller->GetActorNameOrLabel());
		ControllerJson->SetStringField(TEXT("class"), Controller->GetClass()->GetPathName());
		ControllerJson->SetBoolField(TEXT("is_player_controller"), Controller->IsA<APlayerController>());
		ControllerJson->SetBoolField(TEXT("visible"), IsActorVisible(Controller));
		ControllerJson->SetBoolField(TEXT("hidden"), !IsActorVisible(Controller));
		if (APawn* Pawn = Controller->GetPawn())
		{
			ControllerJson->SetStringField(TEXT("pawn_name"), Pawn->GetName());
			ControllerJson->SetStringField(TEXT("pawn_class"), Pawn->GetClass()->GetPathName());
		}
		ControllersArray.Add(MakeShareable(new FJsonValueObject(ControllerJson)));
	}

	TArray<TSharedPtr<FJsonValue>> PawnsArray;
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Pawn = *It;
		if (!MatchesActorFilter(Pawn, ActorName, ClassFilter))
		{
			continue;
		}

		TSharedPtr<FJsonObject> PawnJson = MakeShareable(new FJsonObject);
		PawnJson->SetStringField(TEXT("name"), Pawn->GetName());
		PawnJson->SetStringField(TEXT("label"), Pawn->GetActorNameOrLabel());
		PawnJson->SetStringField(TEXT("class"), Pawn->GetClass()->GetPathName());
		PawnJson->SetBoolField(TEXT("visible"), IsActorVisible(Pawn));
		PawnJson->SetBoolField(TEXT("hidden"), !IsActorVisible(Pawn));
		PawnJson->SetArrayField(TEXT("location"), VectorToJson(Pawn->GetActorLocation()));
		PawnJson->SetStringField(TEXT("auto_possess_ai"), StaticEnum<EAutoPossessAI>()->GetNameStringByValue(static_cast<int64>(Pawn->AutoPossessAI)));
		if (Pawn->AIControllerClass)
		{
			PawnJson->SetStringField(TEXT("ai_controller_class"), Pawn->AIControllerClass->GetPathName());
		}
		if (AController* Controller = Pawn->GetController())
		{
			PawnJson->SetStringField(TEXT("controller_name"), Controller->GetName());
			PawnJson->SetStringField(TEXT("controller_class"), Controller->GetClass()->GetPathName());
		}
		PawnsArray.Add(MakeShareable(new FJsonValueObject(PawnJson)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("world"), WorldType);
	Result->SetArrayField(TEXT("controllers"), ControllersArray);
	Result->SetArrayField(TEXT("pawns"), PawnsArray);
	Result->SetNumberField(TEXT("controller_count"), ControllersArray.Num());
	Result->SetNumberField(TEXT("pawn_count"), PawnsArray.Num());
	return FBridgeToolResult::Json(Result);
}
