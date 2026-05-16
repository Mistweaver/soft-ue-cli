// Copyright soft-ue-expert. All Rights Reserved.

#include "Tools/Analysis/ValidateClassPathTool.h"
#include "SoftUEBridgeEditorModule.h"
#include "Utils/BridgePropertySerializer.h"
#include "Misc/PackageName.h"

namespace
{
	FString BuildAssetObjectPath(const FString& InPath)
	{
		if (!InPath.StartsWith(TEXT("/")))
		{
			return InPath;
		}

		FString Path = InPath;
		if (Path.EndsWith(TEXT("_C")))
		{
			Path.LeftChopInline(2);
		}

		if (!Path.Contains(TEXT(".")))
		{
			const FString AssetName = FPackageName::GetLongPackageAssetName(Path);
			Path += TEXT(".") + AssetName;
		}

		return Path;
	}
}

FString UValidateClassPathTool::GetToolDescription() const
{
	return TEXT("Validate that a soft class path resolves to a loadable UClass and return its parent hierarchy.");
}

TMap<FString, FBridgeSchemaProperty> UValidateClassPathTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	auto Prop = [](const FString& Type, const FString& Desc) {
		FBridgeSchemaProperty P; P.Type = Type; P.Description = Desc; return P;
	};

	Schema.Add(TEXT("class_path"), Prop(TEXT("string"), TEXT("Soft class path or asset path to validate")));
	Schema.Add(TEXT("parent_depth"), Prop(TEXT("integer"), TEXT("Optional maximum parent depth")));
	return Schema;
}

FBridgeToolResult UValidateClassPathTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString ClassPath = GetStringArgOrDefault(Arguments, TEXT("class_path"));
	const int32 ParentDepth = GetIntArgOrDefault(Arguments, TEXT("parent_depth"), 10);
	if (ClassPath.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("validate-class-path: class_path is required"));
	}

	const FString AssetObjectPath = BuildAssetObjectPath(ClassPath);
	UObject* AssetObject = nullptr;
	if (AssetObjectPath.StartsWith(TEXT("/")))
	{
		AssetObject = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetObjectPath);
	}

	FString ResolveError;
	UClass* ResolvedClass = FBridgePropertySerializer::ResolveClass(ClassPath, ResolveError);

	TArray<TSharedPtr<FJsonValue>> ParentClasses;
	for (UClass* Current = ResolvedClass ? ResolvedClass->GetSuperClass() : nullptr;
		Current && ParentClasses.Num() < ParentDepth;
		Current = Current->GetSuperClass())
	{
		ParentClasses.Add(MakeShareable(new FJsonValueString(Current->GetPathName())));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), ResolvedClass != nullptr);
	Result->SetStringField(TEXT("input_path"), ClassPath);
	Result->SetStringField(TEXT("asset_path"), AssetObjectPath);
	Result->SetBoolField(TEXT("asset_exists"), AssetObject != nullptr);
	Result->SetBoolField(TEXT("class_exists"), ResolvedClass != nullptr);
	Result->SetBoolField(TEXT("load_success"), ResolvedClass != nullptr);
	Result->SetArrayField(TEXT("parent_classes"), ParentClasses);
	if (ResolvedClass)
	{
		Result->SetStringField(TEXT("resolved_class_path"), ResolvedClass->GetPathName());
		Result->SetStringField(TEXT("class_name"), ResolvedClass->GetName());
	}
	else
	{
		Result->SetStringField(TEXT("error"), ResolveError);
	}

	return FBridgeToolResult::Json(Result);
}
