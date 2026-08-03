// Copyright soft-ue-expert. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tools/References/FindReferencesTool.h"

#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(
	FFindReferencesToolSpec,
	"SoftUEBridge.References.FindReferencesTool",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
END_DEFINE_SPEC(FFindReferencesToolSpec)

void FFindReferencesToolSpec::Define()
{
	Describe("limit validation", [this]()
	{
		for (const int32 Limit : {0, -1})
		{
			It(FString::Printf(TEXT("rejects limit %d before dispatch"), Limit), [this, Limit]()
			{
				TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
				Arguments->SetStringField(TEXT("type"), TEXT("node"));
				Arguments->SetStringField(TEXT("asset_path"), TEXT("/Game"));
				Arguments->SetStringField(TEXT("node_class"), TEXT("K2Node_CallFunction"));
				Arguments->SetNumberField(TEXT("limit"), Limit);

				const FBridgeToolResult Result = NewObject<UFindReferencesTool>()->Execute(
					Arguments, FBridgeToolContext{});

				TestTrue(TEXT("non-positive limit is rejected"), Result.bIsError);
				if (!TestEqual(TEXT("one error item is returned"), Result.Content.Num(), 1))
				{
					return;
				}
				FString Message;
				TestTrue(
					TEXT("error content contains text"),
					Result.Content[0]->TryGetStringField(TEXT("text"), Message));
				TestTrue(
					TEXT("error explains the positive limit requirement"),
					Message.Contains(TEXT("positive integer")));
			});
		}
	});
}

#endif
