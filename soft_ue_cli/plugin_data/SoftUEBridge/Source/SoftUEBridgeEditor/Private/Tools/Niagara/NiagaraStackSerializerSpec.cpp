// Copyright soft-ue-expert. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tools/Niagara/NiagaraStackSerializer.h"

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "NiagaraTypes.h"

BEGIN_DEFINE_SPEC(
	FNiagaraStackSerializerSpec,
	"SoftUEBridge.Niagara.StackSerializer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
END_DEFINE_SPEC(FNiagaraStackSerializerSpec)

void FNiagaraStackSerializerSpec::Define()
{
	Describe("ScriptUsageToStageName", [this]()
	{
		It("uses the stack's own section headings, not the raw enum names", [this]()
		{
			TestEqual(
				TEXT("particle spawn"),
				NiagaraStackSerializer::ScriptUsageToStageName(ENiagaraScriptUsage::ParticleSpawnScript),
				TEXT("ParticleSpawn"));
			TestEqual(
				TEXT("emitter update"),
				NiagaraStackSerializer::ScriptUsageToStageName(ENiagaraScriptUsage::EmitterUpdateScript),
				TEXT("EmitterUpdate"));
			TestEqual(
				TEXT("system spawn"),
				NiagaraStackSerializer::ScriptUsageToStageName(ENiagaraScriptUsage::SystemSpawnScript),
				TEXT("SystemSpawn"));
		});

		It("folds interpolated spawn into the ParticleSpawn section", [this]()
		{
			// The window shows one ParticleSpawn group either way; interpolated spawn differs only in
			// also pulling in the update script. Emitting a second key would invent a section.
			TestEqual(
				TEXT("interpolated spawn shares the ParticleSpawn key"),
				NiagaraStackSerializer::ScriptUsageToStageName(
					ENiagaraScriptUsage::ParticleSpawnScriptInterpolated),
				NiagaraStackSerializer::ScriptUsageToStageName(ENiagaraScriptUsage::ParticleSpawnScript));
		});

		It("names an unrecognised usage rather than returning an empty string", [this]()
		{
			TestEqual(
				TEXT("out-of-range usage"),
				NiagaraStackSerializer::ScriptUsageToStageName(static_cast<ENiagaraScriptUsage>(200)),
				TEXT("Unknown"));
		});
	});

	Describe("SimTargetToString", [this]()
	{
		It("names both simulation targets", [this]()
		{
			TestEqual(
				TEXT("cpu"),
				NiagaraStackSerializer::SimTargetToString(ENiagaraSimTarget::CPUSim),
				TEXT("CPUSim"));
			TestEqual(
				TEXT("gpu"),
				NiagaraStackSerializer::SimTargetToString(ENiagaraSimTarget::GPUComputeSim),
				TEXT("GPUComputeSim"));
		});
	});

	Describe("InputSourceKindToString", [this]()
	{
		It("names every source kind", [this]()
		{
			// A module input's value is not actionable without knowing where it came from: a literal
			// can be edited in place, a link has to be followed, a dynamic input is a subtree.
			using NiagaraStackSerializer::EInputSourceKind;
			TestEqual(TEXT("default"),
				NiagaraStackSerializer::InputSourceKindToString(EInputSourceKind::Default), TEXT("default"));
			TestEqual(TEXT("literal"),
				NiagaraStackSerializer::InputSourceKindToString(EInputSourceKind::Literal), TEXT("literal"));
			TestEqual(TEXT("linked"),
				NiagaraStackSerializer::InputSourceKindToString(EInputSourceKind::Linked), TEXT("linked"));
			TestEqual(TEXT("dynamic input"),
				NiagaraStackSerializer::InputSourceKindToString(EInputSourceKind::DynamicInput),
				TEXT("dynamic_input"));
			TestEqual(TEXT("data interface"),
				NiagaraStackSerializer::InputSourceKindToString(EInputSourceKind::DataInterface),
				TEXT("data_interface"));
			TestEqual(TEXT("expression"),
				NiagaraStackSerializer::InputSourceKindToString(EInputSourceKind::Expression),
				TEXT("expression"));
		});

		It("distinguishes default from literal", [this]()
		{
			// "left alone" and "explicitly set to this value" are different authoring facts, and
			// collapsing them would hide whether an override exists at all.
			using NiagaraStackSerializer::EInputSourceKind;
			TestNotEqual(
				TEXT("default is not literal"),
				NiagaraStackSerializer::InputSourceKindToString(EInputSourceKind::Default),
				NiagaraStackSerializer::InputSourceKindToString(EInputSourceKind::Literal));
		});
	});

	Describe("EmitterModeToString", [this]()
	{
		It("names both emitter modes", [this]()
		{
			// A Stateless (Lightweight) emitter returns null from GetEmitterData() by design, so the
			// mode is what tells a caller apart a graph-less emitter from a broken one.
			TestEqual(
				TEXT("standard"),
				NiagaraStackSerializer::EmitterModeToString(ENiagaraEmitterMode::Standard),
				TEXT("Standard"));
			TestEqual(
				TEXT("stateless"),
				NiagaraStackSerializer::EmitterModeToString(ENiagaraEmitterMode::Stateless),
				TEXT("Stateless"));
		});
	});

	Describe("IsParameterMapPinType", [this]()
	{
		It("accepts a pin carrying FNiagaraParameterMap", [this]()
		{
			FEdGraphPinType PinType;
			PinType.PinSubCategoryObject = FNiagaraParameterMap::StaticStruct();

			TestTrue(TEXT("parameter map pin"), NiagaraStackSerializer::IsParameterMapPinType(PinType));
		});

		It("rejects a value pin", [this]()
		{
			// Module value inputs and the dynamic-input function calls hanging off them sit on other
			// types. Matching one of those would walk the stack chain into a dynamic input subtree.
			FEdGraphPinType PinType;
			PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();

			TestFalse(TEXT("vector pin"), NiagaraStackSerializer::IsParameterMapPinType(PinType));
		});

		It("rejects a pin with no sub-category object", [this]()
		{
			const FEdGraphPinType PinType;

			TestFalse(TEXT("bare pin"), NiagaraStackSerializer::IsParameterMapPinType(PinType));
		});

		It("treats a null pin as not a parameter map", [this]()
		{
			TestFalse(TEXT("null pin"), NiagaraStackSerializer::IsParameterMapPin(nullptr));
		});
	});

	Describe("null tolerance", [this]()
	{
		It("returns an empty module list for a null output node", [this]()
		{
			TArray<UNiagaraNodeFunctionCall*> Modules;
			Modules.Add(nullptr); // must be cleared, not appended to

			NiagaraStackSerializer::CollectStackModules(nullptr, Modules);

			TestEqual(TEXT("no modules"), Modules.Num(), 0);
		});

		It("returns an empty object for a null module node", [this]()
		{
			const TSharedPtr<FJsonObject> Json = NiagaraStackSerializer::SerializeModule(nullptr);

			TestTrue(TEXT("object is valid"), Json.IsValid());
			TestEqual(TEXT("no fields"), Json->Values.Num(), 0);
		});

		It("returns an empty object for a null system", [this]()
		{
			const NiagaraStackSerializer::FNiagaraInspectOptions Options;
			const TSharedPtr<FJsonObject> Json = NiagaraStackSerializer::SerializeSystem(nullptr, Options);

			TestTrue(TEXT("object is valid"), Json.IsValid());
			TestEqual(TEXT("no fields"), Json->Values.Num(), 0);
		});

		It("returns a null graph for a null script", [this]()
		{
			TestNull(TEXT("no graph"), NiagaraStackSerializer::GetScriptGraph(nullptr));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
