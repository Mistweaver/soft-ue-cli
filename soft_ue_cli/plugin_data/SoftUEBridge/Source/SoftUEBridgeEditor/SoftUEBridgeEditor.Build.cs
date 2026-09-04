// Copyright soft-ue-expert. All Rights Reserved.

using UnrealBuildTool;

public class SoftUEBridgeEditor : ModuleRules
{
	public SoftUEBridgeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"RHI",
			"SoftUEBridge"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Editor Framework
			"UnrealEd",
			"EditorSubsystem",
			"ToolMenus",
			"Slate",
			"SlateCore",
			"StatusBar",

			// Blueprint/Kismet
			"Kismet",
			"KismetCompiler",
			"BlueprintGraph",

			// Animation Blueprint
			"AnimGraph",
			"IKRig",
			"IKRigEditor",

			// Control Rig / RigVM (read-only hierarchy, control value, and graph inspection).
			// ControlRig carries URigHierarchy and IRigHierarchyProvider; RigVMDeveloper carries
			// URigVMBlueprint and the graph model. ControlRigDeveloper is deliberately absent:
			// 5.8 renamed its asset interface, and nothing here needs to name that type.
			"ControlRig",
			"RigVM",
			"RigVMDeveloper",

			// Chaos Cloth
			"ClothingSystemRuntimeInterface",
			"ClothingSystemRuntimeCommon",
			"ClothingSystemEditorInterface",
			"ClothingSystemEditor",
			"Chaos",
			"ChaosCore",
			"ChaosCloth",
			"ChaosClothAsset",
			"ChaosClothAssetEngine",
			"ChaosClothAssetTools",
			"DataflowEngine",
			"ClothPainter",

			// Asset Management
			"AssetTools",
			"AssetRegistry",
			"ContentBrowser",

			// Level Editing
			"LevelEditor",
			"MainFrame",
			"EditorScriptingUtilities",

			// UI/Widgets
			"UMG",
			"UMGEditor",
			"PropertyEditor",
			"PropertyPath",

			// Animation/Sequencer (for UMG animations)
			"MovieScene",

			// HTTP (required by SoftUEBridge public headers via BridgeServer.h)
			"HTTP",
			"HTTPServer",

			// JSON
			"Json",
			"JsonUtilities",

			// Input
			"InputCore",
			"EnhancedInput",
			"AIModule",

			// Project Settings
			"GameplayTags",
			"EngineSettings",
			"DesktopPlatform",
			"Projects",

			// StateTree
			"StateTreeModule",
			"StateTreeEditorModule",

			// MetaSound (read-only graph inspection)
			"MetasoundFrontend",
			"MetasoundEngine",

			// Niagara (read-only system, emitter, and module-stack inspection). Niagara carries
			// UNiagaraSystem and the emitter data; NiagaraEditor carries UNiagaraScriptSource and the
			// graph node classes. Note that most of FNiagaraStackGraphUtilities is declared without
			// NIAGARAEDITOR_API, so NiagaraStackSerializer walks the stack through UEdGraph instead
			// of calling GetOrderedModuleNodes, which would compile but not link.
			"Niagara",
			"NiagaraCore",
			"NiagaraEditor",

			// Python Scripting
			"PythonScriptPlugin",

			// Live Coding
			"LiveCoding",

			// Source Control (for SCM diff tool)
			"SourceControl",

			// Image Processing (for asset preview tool)
			"ImageWrapper",

			// Landscape (for LandscapeGrassType inspection)
			"Landscape",

			// Rewind Debugger
			"GameplayInsights",
			"RewindDebuggerInterface",
			"TraceLog",
			"TraceAnalysis",
			"TraceServices"
		});
	}
}
