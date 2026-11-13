from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLUGIN_SOURCE = ROOT / "soft_ue_cli" / "plugin_data" / "SoftUEBridge" / "Source"


def _read(relative_path: str) -> str:
    return (PLUGIN_SOURCE / relative_path).read_text(encoding="utf-8")


def test_cloth_query_uses_safe_section_binding_enumerator():
    query_source = _read("SoftUEBridgeEditor/Private/Tools/Cloth/ClothTools.cpp")
    utility_header = _read("SoftUEBridgeEditor/Private/Tools/Cloth/BridgeClothBindings.h")
    utility_source = _read("SoftUEBridgeEditor/Private/Tools/Cloth/BridgeClothBindings.cpp")

    query_block = query_source.split("TSharedPtr<FJsonObject> BuildQueryResult(", 1)[1].split(
        "UClothConfigBase* ResolveClothConfig", 1
    )[0]
    assert "BridgeClothBindings::Collect" in query_block
    assert "GetAllMeshClothingAssetBindings" not in query_block
    assert "GetAllLodMeshClothingAssetBindings" not in query_block
    assert 'SetArrayField(TEXT("binding_warnings")' in query_block
    assert 'SetNumberField(TEXT("binding_warning_count")' in query_block

    assert "struct FBindingRecord" in utility_header
    assert "struct FBindingWarning" in utility_header
    assert "ValidateResolvedAssetLod" in utility_header
    assert "FSkeletalMeshModel" in utility_source
    assert "LODModels" in utility_source
    assert "Sections" in utility_source
    assert "Section.HasClothingData()" in utility_source
    assert "unsupported_clothing_asset_type" in utility_source
    assert "GetAllMeshClothingAssetBindings" not in utility_source
    assert "GetAllLodMeshClothingAssetBindings" not in utility_source

    spec_source = _read("SoftUEBridgeEditor/Private/Tools/Cloth/BridgeClothBindingsSpec.cpp")
    assert "ClothMappingDataLODs" in spec_source
    assert "ignores metadata without cloth mapping data" in spec_source
    assert "warns when cloth mapping has a default asset GUID" in spec_source
    assert "rejects a resolved non-common clothing asset type" in spec_source
    assert "unsupported_clothing_asset_type" in spec_source

    assert "BuildBindingArray(USkeletalMesh* Mesh)" not in query_source
    assert query_source.count("TArray<TSharedPtr<FJsonValue>> BuildBindingArray(") == 1


def test_anim_montage_segment_helpers_are_unique_for_unity_builds():
    inspect_source = _read("SoftUEBridgeEditor/Private/Tools/Animation/AnimMontageInspectTool.cpp")
    slot_source = _read("SoftUEBridgeEditor/Private/Tools/Animation/AnimMontageSlotTool.cpp")

    assert "TSharedPtr<FJsonObject> SegmentToJson(" not in inspect_source
    assert "TSharedPtr<FJsonObject> SegmentToJson(" not in slot_source
    assert "MontageInspectSegmentToJson" in inspect_source
    assert "MontageSlotSegmentToJson" in slot_source


def test_editor_module_uses_ue58_post_engine_init_accessor():
    source = _read("SoftUEBridgeEditor/Private/SoftUEBridgeEditorModule.cpp")

    assert "ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8" in source
    assert "FCoreDelegates::GetOnPostEngineInit().AddRaw" in source
    assert "FCoreDelegates::GetOnPostEngineInit().Remove" in source


def test_editor_tools_avoid_ue58_deprecated_object_and_package_apis():
    object_sources = {
        "SoftUEBridgeEditor/Private/Tools/Asset/EditCustomizableObjectGraphTool.cpp":
            "EGetObjectsFlags::IncludeNestedObjects",
        "SoftUEBridgeEditor/Private/Tools/Asset/MutableIntrospectionUtils.cpp":
            "EGetObjectsFlags::IncludeNestedObjects",
        "SoftUEBridgeEditor/Private/Tools/Write/SetNodePositionTool.cpp":
            "EGetObjectsFlags::IncludeNestedObjects",
    }

    for relative_path, required_text in object_sources.items():
        source = _read(relative_path)
        assert "ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8" in source
        assert required_text in source

    wire_source = _read("SoftUEBridgeEditor/Private/Tools/Widget/WireWidgetNavigationTool.cpp")
    assert "UE::IsSavingPackage()" in wire_source
    assert "GIsSavingPackage" not in wire_source


def test_skeletal_mesh_loaders_are_unique_for_unity_builds():
    cloth_source = _read("SoftUEBridgeEditor/Private/Tools/Cloth/ClothTools.cpp")
    socket_source = _read("SoftUEBridgeEditor/Private/Tools/Asset/SkeletalMeshSocketTool.cpp")

    # Both files defined LoadSkeletalMesh in an anonymous namespace. Anonymous
    # namespaces do not isolate translation units under a unity build - both
    # files land in one .cpp and the second definition is a redefinition error.
    assert "USkeletalMesh* LoadSkeletalMesh(" not in cloth_source
    assert "USkeletalMesh* LoadSkeletalMesh(" not in socket_source
    assert "ClothLoadSkeletalMesh" in cloth_source
    assert "SocketToolLoadSkeletalMesh" in socket_source


def test_cloth_tools_guard_ue58_only_dataflow_and_unbind_apis():
    source = _read("SoftUEBridgeEditor/Private/Tools/Cloth/ClothTools.cpp")

    # UChaosClothAssetBase::HasDataflow() is UE 5.8+. It is defined there as
    # GetDataflow() != nullptr, and GetDataflow() exists in 5.7 too, so the
    # helper needs no version guard - but it must not call HasDataflow directly.
    assert "bool ClothAssetHasDataflow(" in source
    assert "Asset->GetDataflow() != nullptr" in source
    assert "->HasDataflow()" not in source

    # The per-section UnbindFromSkeletalMesh overload is UE 5.8+; 5.7 only has
    # the LOD-wide one, so this call genuinely needs a version guard.
    assert "void UnbindClothFromSkeletalMesh(" in source
    assert "ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8" in source
    unbind_block = source.split("void UnbindClothFromSkeletalMesh(", 1)[1].split("\n}", 1)[0]
    assert "Asset->UnbindFromSkeletalMesh(Mesh, LodIndex, SectionIndex)" in unbind_block
    assert "Asset->UnbindFromSkeletalMesh(Mesh, LodIndex)" in unbind_block

    # Every call site must route through the helper, so the only two raw calls
    # to the engine API are the two guarded ones inside it.
    assert source.count("->UnbindFromSkeletalMesh(") == 2


def test_rewind_helper_avoids_removed_trace_file_loaded_check():
    source = _read("SoftUEBridgeEditor/Private/Tools/Rewind/RewindHelper.cpp")

    assert "IsTraceFileLoaded()" not in source
    assert "Debugger->IsRecording()" in source


def test_set_node_property_supports_nested_inner_anim_node_paths_and_struct_arrays():
    set_node_source = _read("SoftUEBridgeEditor/Private/Tools/Write/SetNodePropertyTool.cpp")
    anim_props_source = _read("SoftUEBridgeEditor/Private/Utils/BridgeAnimNodeProperties.cpp")
    modifier_source = _read("SoftUEBridgeEditor/Private/Utils/BridgeAssetModifier.cpp")
    serializer_source = _read("SoftUEBridgeEditor/Private/Utils/BridgePropertySerializer.cpp")

    assert "FBridgeAnimNodeProperties::ResolveInnerAnimNodePropertyPath" in set_node_source
    assert "ResolvePropertyPathAgainstStruct" in anim_props_source
    assert 'InnerPath.StartsWith(TEXT("Node."), ESearchCase::IgnoreCase)' in anim_props_source
    assert "FArrayProperty* ArrayProp = CastField<FArrayProperty>" in modifier_source
    assert "DeserializeArrayProperty" in serializer_source
    assert "FBridgePropertySerializer::DeserializePropertyValue(ArrayProp->Inner, ElementPtr, ElementValue" in serializer_source


def test_inner_anim_node_lookup_is_not_hardcoded_to_a_member_named_node():
    anim_props_source = _read("SoftUEBridgeEditor/Private/Utils/BridgeAnimNodeProperties.cpp")

    # The inner FAnimNode_* member is conventionally named "Node", but that is not
    # guaranteed - discovery must fall back to a type scan.
    assert "FAnimNode_Base::StaticStruct()" in anim_props_source
    assert "TFieldIterator<FStructProperty>" in anim_props_source

    for relative_path in (
        "SoftUEBridgeEditor/Private/Tools/Write/SetNodePropertyTool.cpp",
        "SoftUEBridgeEditor/Private/Tools/Write/AddGraphNodeTool.cpp",
    ):
        source = _read(relative_path)
        assert 'FindPropertyByName(TEXT("Node"))' not in source


def test_node_property_tools_report_unresolved_properties_with_node_context():
    anim_props_header = _read("SoftUEBridgeEditor/Public/Utils/BridgeAnimNodeProperties.h")
    anim_props_source = _read("SoftUEBridgeEditor/Private/Utils/BridgeAnimNodeProperties.cpp")
    assert "FBridgePropertyPathFailure" in anim_props_header
    assert "SetPathFailure" in anim_props_source
    assert "UStruct* SegmentContext = CurrentStruct" in anim_props_source
    assert "WalkPathTypes" not in anim_props_source
    assert 'ParseIntoArray(Segments, TEXT("."), false)' in anim_props_source
    assert "Preflight every segment's syntax before traversing" in anim_props_source
    assert "BracketEnd != Segment.Len() - 1" in anim_props_source
    assert 'Segment.Contains(TEXT("]"))' in anim_props_source
    assert "ParseStrictNonNegativeInt32" in anim_props_source
    assert "FCString::IsNumeric" not in anim_props_source
    assert "FCString::Atoi" not in anim_props_source
    assert "MAX_int32 - Digit" in anim_props_source
    assert 'TEXT("(empty segment)")' in anim_props_source
    assert "OutError.Reset()" in anim_props_source
    assert 'TEXT(" - stopped at \'%s\'")' in anim_props_source
    assert 'TEXT(" - settable pins: %s")' in anim_props_source
    assert "if (PinNames.Num() > 0)" not in anim_props_source

    for relative_path in (
        "SoftUEBridgeEditor/Private/Tools/Write/SetNodePropertyTool.cpp",
        "SoftUEBridgeEditor/Private/Tools/Write/AddGraphNodeTool.cpp",
    ):
        source = _read(relative_path)

        assert "FBridgeAnimNodeProperties::DescribeUnresolvedProperty" in source
        assert source.count("FBridgeAnimNodeProperties::IsSettableGraphPin(Pin)") >= 2
        # The pin-default fallback must not recover property names by parsing warning text.
        assert "RightChop(20)" not in source
        assert 'StartsWith(TEXT("Property not found: "))' not in source

    assert "Pin->LinkedTo.Num() == 0" in anim_props_source
    assert "!Pin->bDefaultValueIsIgnored" in anim_props_source
    assert "!Pin->bOrphanedPin" in anim_props_source


def test_multi_pie_world_utility_and_tools_share_instance_discovery():
    header = _read("SoftUEBridgeEditor/Public/Tools/PIE/BridgePIEWorlds.h")
    source = _read("SoftUEBridgeEditor/Private/Tools/PIE/BridgePIEWorlds.cpp")
    spec = _read("SoftUEBridgeEditor/Private/Tools/PIE/BridgePIEWorldsSpec.cpp")
    console = _read("SoftUEBridgeEditor/Private/Tools/PIE/ExecConsoleCommandTool.cpp")
    session = _read("SoftUEBridgeEditor/Private/Tools/PIE/PieSessionTool.cpp")

    assert "Enumerate" in header and "Resolve" in header and "AvailableInstanceIds" in header
    assert "EWorldType::PIE" in source
    for mode in ("standalone", "dedicated-server", "listen-server", "client"):
        assert f'TEXT("{mode}")' in source
    assert "filters invalid PIE contexts" in spec
    assert "resolves instances and reports available IDs" in spec
    assert "returns stable net mode names" in spec
    assert "FBridgePIEWorlds::Resolve" in console
    assert "FBridgePIEWorlds::ResolveLocalPlayerController" in console
    assert "? FBridgePIEWorlds::ResolveLocalPlayerController(World, PlayerIndex)" in console
    assert "UGameplayStatics::GetPlayerController(World, PlayerIndex)" not in console
    assert "local player controller %d not found" in console
    assert "GetGameInstance" in source
    assert "GetLocalPlayers" in source
    assert "LocalPlayer->GetPlayerController(World)" in source
    assert "LocalPlayer->GetPlayerController()" not in source
    assert "resolves only valid local player controllers" in spec
    assert "pie_instance is valid only when world is 'pie'" in console
    assert 'HasField(TEXT("player_index"))' in console
    assert 'HasField(TEXT("pie_instance"))' in console
    assert 'SetNumberField(TEXT("pie_instance")' in console
    assert 'SetStringField(TEXT("world_name")' in console
    assert 'SetStringField(TEXT("net_mode")' in console
    assert "FBridgePIEWorlds::Enumerate" in session
    assert 'SetArrayField(TEXT("worlds")' in session
    assert 'SetNumberField(TEXT("world_count")' in session


def test_legacy_cloth_weight_maps_use_public_runtime_targets_and_section_provenance():
    header = _read("SoftUEBridgeEditor/Public/Utils/BridgeLegacyClothWeightMaps.h")
    source = _read("SoftUEBridgeEditor/Private/Utils/BridgeLegacyClothWeightMaps.cpp")
    spec = _read("SoftUEBridgeEditor/Private/Utils/BridgeLegacyClothWeightMapsSpec.cpp")
    cloth = _read("SoftUEBridgeEditor/Private/Tools/Cloth/ClothTools.cpp")
    build_rules = _read("SoftUEBridgeEditor/SoftUEBridgeEditor.Build.cs")

    assert "FBridgeLegacyWeightMapTarget" in header
    assert "ReadBridgeLegacyWeightMapValues" in header
    assert "ApplyBridgeLegacyWeightMapToLodData" in header
    assert "ApplyBridgeLegacySectionSelection" in header
    assert "RecordBridgeSourceSectionMembership" in header
    assert "CountBridgeSelectedMultiSectionVertices" in header
    assert "GetWeightMapTargetEnum" in source
    assert "UChaosClothingSimulationFactory" in source
    assert "ChaosWeightMapTarget.h" not in source
    assert '"ChaosCloth"' in build_rules
    assert "BridgeSourceSectionPrefix" in header
    assert "SoftUESourceSection_" in header
    assert "WriteBridgeSourceSectionMaps" in cloth
    assert "RecordBridgeSourceSectionMembership" in cloth
    assert "ReadBridgeSourceSectionSelection" in cloth
    assert "section_indices" in cloth
    assert "multi_section_vertex_count" in cloth
    assert "bSourceSectionMap ? FMath::Max" in cloth
    assert "resolves common and extended targets" in spec
    assert "preserves welded dual section membership" in spec
    assert "restricts constant, vertex-color, and bone-distance candidates" in spec
    assert "intersects spatial and section selection" in spec
    assert "rejects an empty combined selection" in spec
    assert "records dual membership through the merge helper" in spec
    assert "round-trips extended target values through read and apply helpers" in spec


def test_insights_analyze_reads_session_only_inside_a_read_scope():
    source = _read("SoftUEBridgeEditor/Private/Tools/Performance/InsightsAnalyzeTool.cpp")

    # IAnalysisSession::GetName()/GetDurationSeconds() look like plain accessors but
    # are session reads: TraceServices asserts "Trying to read from session outside
    # of a ReadScope" at runtime. This compiles cleanly either way, so the ordering
    # has to be pinned by a test.
    for analysis in ("AnalyzeFrameStats", "AnalyzeBottlenecks"):
        body = source.split(f"UInsightsAnalyzeTool::{analysis}(", 1)[1]
        scope_at = body.index("FAnalysisSessionReadScope")
        assert body.index("Session.GetName()") > scope_at, f"{analysis} reads GetName() before its read scope"
        assert body.index("Session.GetDurationSeconds()") > scope_at, (
            f"{analysis} reads GetDurationSeconds() before its read scope"
        )

    # ResolveAgainstSession() also calls GetDurationSeconds(), so its call site in
    # Execute() needs its own scope.
    execute_body = source.split("UInsightsAnalyzeTool::Execute(", 1)[1]
    resolve_at = execute_body.index("Window.ResolveAgainstSession(")
    preceding = execute_body[:resolve_at]
    assert "FAnalysisSessionReadScope" in preceding, (
        "ResolveAgainstSession() reads the session and must be called inside a read scope"
    )


def test_insights_capture_starts_traces_without_console_string_parsing():
    source = _read("SoftUEBridgeEditor/Private/Tools/Performance/InsightsCaptureTool.cpp")

    # UE splits console command arguments on whitespace without honouring quotes, so
    # "Trace.File <path> <channels>" is rejected as "Invalid arguments" whenever the
    # project path contains a space - and GEngine->Exec still returns true because the
    # command was handled, which made the failure silent.
    assert "FTraceAuxiliary::Start(" in source
    assert "FTraceAuxiliary::EConnectionType::File" in source
    assert "FTraceAuxiliary::Stop()" in source

    # No console command may be executed at all. The comments explaining why still
    # name Trace.File and GEngine->Exec, so check code lines only.
    code = "\n".join(
        line for line in source.splitlines() if not line.strip().startswith("//")
    )
    assert "GEngine->Exec" not in code
    assert "Trace.Start" not in code
    assert "Trace.Stop" not in code

    # The reported path must be absolute: ProjectSavedDir() is relative to the engine
    # binaries dir, and the path is handed back to callers for insights-analyze.
    assert "ConvertRelativePathToFull" in source
    assert '.utrace' in source


def test_asset_query_physics_is_registered_and_avoids_version_specific_elements():
    module_source = _read("SoftUEBridgeEditor/Private/SoftUEBridgeEditorModule.cpp")
    tool_source = _read("SoftUEBridgeEditor/Private/Tools/Asset/PhysicsAssetQueryTool.cpp")

    assert '#include "Tools/Asset/PhysicsAssetQueryTool.h"' in module_source
    assert "RegisterToolClass<UPhysicsAssetQueryTool>()" in module_source

    # Level set and skinned element lists change between engine versions, so they are counted
    # through GetElementCount() instead of being read by name.
    assert "GetElementCount()" in tool_source
    for element_list in ("LevelSetElems", "SkinnedLevelSetElems", "MLLevelSetElems", "SkinnedTriangleMeshElems"):
        assert element_list not in tool_source

    # Unity builds share anonymous namespaces, so helpers carry a file-specific prefix.
    assert "FBridgeSchemaProperty MakeSchemaProperty(" not in tool_source
    assert "PhysQuerySchemaProperty" in tool_source


def test_asset_query_physics_uses_shared_bone_pose_helpers():
    tool_source = _read("SoftUEBridgeEditor/Private/Tools/Asset/PhysicsAssetQueryTool.cpp")

    # Bone filtering, component-space composition, and transform JSON are shared with anim pose-at.
    assert '#include "Utils/BridgeBonePose.h"' in tool_source
    for duplicate in (
        "PhysQueryComponentSpacePose",
        "PhysQuerySetTransform",
        "PhysQueryVector",
        "PhysQueryRotator",
        "PhysQueryReadBones",
    ):
        assert duplicate not in tool_source


def test_bridge_bone_pose_helpers_are_engine_free_and_covered_by_a_spec():
    header = _read("SoftUEBridgeEditor/Public/Utils/BridgeBonePose.h")
    source = _read("SoftUEBridgeEditor/Private/Utils/BridgeBonePose.cpp")
    spec = _read("SoftUEBridgeEditor/Private/Utils/BridgeBonePoseSpec.cpp")

    assert "namespace SoftUE::BonePose" in header
    assert "SOFTUEBRIDGEEDITOR_API" in header
    for function in ("ComponentSpaceFromLocal", "SetTransformFields", "ParseBoneFilter", "VectorToJson"):
        assert function in header
        assert function in spec

    # Plain math and JSON only: no asset loading, so the spec runs without content.
    assert "LoadAssetByPath" not in source
    assert "SoftUEBridge.Animation.BonePose" in spec


def test_asset_query_physics_constraint_frames_follow_space_and_mesh_is_body_space_noop():
    tool_source = _read("SoftUEBridgeEditor/Private/Tools/Asset/PhysicsAssetQueryTool.cpp")

    # Constraint frames are moved by their own bone's reference pose in component space,
    # matching the shapes, instead of always staying relative to the bone.
    assert "Frame * ComponentPose[BoneIndex]" in tool_source
    assert "Instance.GetRefFrame(EConstraintFrame::Frame1), Instance.ConstraintBone1" in tool_source
    assert "Instance.GetRefFrame(EConstraintFrame::Frame2), Instance.ConstraintBone2" in tool_source

    # mesh_path only matters in component space, so body space warns instead of loading it.
    assert "is used only with space 'component' and was ignored" in tool_source


def test_anim_state_machine_inspect_is_registered_and_version_gated():
    module_source = _read("SoftUEBridgeEditor/Private/SoftUEBridgeEditorModule.cpp")
    tool_source = _read("SoftUEBridgeEditor/Private/Tools/Animation/AnimStateMachineInspectTool.cpp")

    assert '#include "Tools/Animation/AnimStateMachineInspectTool.h"' in module_source
    assert "RegisterToolClass<UAnimStateMachineInspectTool>()" in module_source

    # bOnlyEvaluateWhenActive exists only in UE 5.8 headers.
    gate = "#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)"
    assert gate in tool_source
    assert tool_source.index(gate) < tool_source.index("bOnlyEvaluateWhenActive")

    # Unity builds share anonymous namespaces, so helpers carry a file-specific prefix.
    assert "FBridgeSchemaProperty MakeSchemaProperty(" not in tool_source
    assert "SmInspectSchemaProperty" in tool_source

    # A child AnimBlueprint has no graphs, so an empty result points at its parent.
    assert "UAnimBlueprint::GetParentAnimBlueprint(AnimBlueprint)" in tool_source
    assert '"parent_anim_blueprint"' in tool_source


def test_actor_lookup_is_shared_through_the_tool_base():
    base_header = _read("SoftUEBridge/Public/Tools/BridgeToolBase.h")
    base_source = _read("SoftUEBridge/Private/Tools/BridgeToolBase.cpp")

    helper_return_types = {
        "FindActorByNameOrLabel": "AActor",
        "FindActorByPattern": "AActor",
        "FindComponentByName": "UActorComponent",
    }
    for helper, return_type in helper_return_types.items():
        assert f"static {return_type}* {helper}(" in base_header
        assert f"UBridgeToolBase::{helper}(" in base_source

    def helper_body(helper):
        return base_source.split(f"UBridgeToolBase::{helper}(", 1)[1].split("\n}\n", 1)[0]

    # Exact lookups never go through wildcard matching, so a pattern such as "*" cannot reach
    # every actor through the delete and modify tools.
    assert "MatchesWildcard" not in helper_body("FindActorByNameOrLabel")

    # Both actor lookups reject an empty name. MatchesWildcard matches anything for an empty
    # pattern, so without the guard a missing actor_name would resolve to an arbitrary actor.
    assert "NameOrLabel.IsEmpty()" in helper_body("FindActorByNameOrLabel")
    assert "Pattern.IsEmpty()" in helper_body("FindActorByPattern")

    exact_callers = {
        "SoftUEBridgeEditor/Private/Tools/Write/AddComponentTool.cpp": "FindActorByNameOrLabel(World, ActorName)",
        "SoftUEBridgeEditor/Private/Tools/Write/BatchDeleteActorTool.cpp": "FindActorByNameOrLabel(World, ActorName)",
        "SoftUEBridgeEditor/Private/Tools/Write/BatchModifyActorTool.cpp": "FindActorByNameOrLabel(World, ActorName)",
        "SoftUEBridgeEditor/Private/Tools/PIE/PieSessionTool.cpp": "FindActorByNameOrLabel(PIEWorld, ActorName)",
    }
    pattern_callers = (
        "SoftUEBridge/Private/Tools/CallFunctionTool.cpp",
        "SoftUEBridge/Private/Tools/GetPropertyTool.cpp",
        "SoftUEBridge/Private/Tools/SetPropertyTool.cpp",
        "SoftUEBridge/Private/Tools/QueryLevelTool.cpp",
    )

    for path, call in exact_callers.items():
        source = _read(path)
        assert call in source, path
        assert "FindActorByPattern" not in source, path
        assert "::FindActorByName" not in source, path

    for path in pattern_callers:
        source = _read(path)
        assert "FindActorByPattern(World, ActorName)" in source, path
        assert "MatchesWildcard(GetActorLabelSafe(A), ActorName)" not in source, path
        assert "MatchesWildcard(GetActorLabelSafe(Actor), ActorName)" not in source, path

    for path in ("SoftUEBridge/Private/Tools/GetPropertyTool.cpp", "SoftUEBridge/Private/Tools/SetPropertyTool.cpp"):
        source = _read(path)
        assert "FindComponentByName(Actor, CompName)" in source, path
        assert "C->GetName().Equals(CompName" not in source, path

    for header in (
        "SoftUEBridgeEditor/Public/Tools/Write/BatchDeleteActorTool.h",
        "SoftUEBridgeEditor/Public/Tools/Write/BatchModifyActorTool.h",
        "SoftUEBridgeEditor/Public/Tools/PIE/PieSessionTool.h",
    ):
        assert "FindActorByName" not in _read(header), header


def test_capture_tools_share_one_image_output_pipeline():
    viewport = _read("SoftUEBridge/Private/Tools/CaptureViewportTool.cpp")
    screenshot = _read("SoftUEBridgeEditor/Private/Tools/Editor/CaptureScreenshotTool.cpp")
    shared = _read("SoftUEBridge/Private/Utils/BridgeImageOutput.cpp")

    for source in (viewport, screenshot):
        assert '#include "Utils/BridgeImageOutput.h"' in source
        assert "SoftUE::ImageOutput::EncodeCapture(" in source
        assert "FBridgeImageOutputOptions::FromArguments(Arguments)" in source
        # The encoder, resize, and color mode code live only in the shared utility.
        assert "IImageWrapper" not in source
        assert "ApplyColorMode" not in source
        assert "ResolveCaptureSize" not in source
        assert "FBase64::Encode" not in source

    # Each tool keeps its own file name prefix so cleanup_previous only removes its own files.
    assert 'TEXT("viewport_")' in viewport
    assert 'TEXT("screenshot_")' in screenshot

    # Screenshot viewport mode forwards the same options to capture-viewport.
    assert "Options.WriteArguments(Args)" in screenshot

    # Unity builds share anonymous namespaces, so shared helpers carry a file-specific prefix.
    assert "ImageOutputResize(" in shared
    assert "bool ResizeImage(" not in shared


def test_image_output_names_files_from_the_whole_image_and_exposes_cleanup():
    header = _read("SoftUEBridge/Public/Utils/BridgeImageOutput.h")
    shared = _read("SoftUEBridge/Private/Utils/BridgeImageOutput.cpp")

    # A hash of only the first bytes lets two same-size images share a file name, and a later
    # capture silently overwrites an earlier one from the same multi-angle call.
    assert "FMath::Min(1024" not in shared
    assert "FMD5::HashBytes(ImageData.GetData(), ImageData.Num())" in shared

    # Multi-image tools clean once before their first image instead of once per image.
    assert "SOFTUEBRIDGE_API void DeletePreviousCaptures(const FString& FilePrefix);" in header
    assert "void SoftUE::ImageOutput::DeletePreviousCaptures(const FString& FilePrefix)" in shared
    assert "DeletePreviousCaptures(FilePrefix);" in shared


def test_capture_scene_tool_reuses_shared_lookup_and_image_output():
    header = _read("SoftUEBridgeEditor/Public/Tools/Editor/CaptureSceneTool.h")
    tool = _read("SoftUEBridgeEditor/Private/Tools/Editor/CaptureSceneTool.cpp")
    module = _read("SoftUEBridgeEditor/Private/SoftUEBridgeEditorModule.cpp")

    assert 'GetToolName() const override { return TEXT("capture-scene"); }' in header
    assert '#include "Tools/Editor/CaptureSceneTool.h"' in module
    assert "Registry.RegisterToolClass<UCaptureSceneTool>();" in module

    # Exact lookup only: a wildcard could silently frame the wrong actor.
    assert "FindActorsByNameOrLabel(" in tool
    assert "FindActorByPattern" not in tool

    # Shared image output, cleaned once per call, with a per-angle file prefix.
    assert "SoftUE::ImageOutput::EncodeCapture(" in tool
    assert "SoftUE::ImageOutput::DeletePreviousCaptures(CaptureSceneFilePrefix)" in tool
    assert "Options.bCleanupPrevious = false;" in tool
    assert 'CaptureSceneFilePrefix = TEXT("scene_")' in tool

    # The temporary camera has no owning actor, lives in the transient package, and is always torn down.
    assert "NewObject<USceneCaptureComponent2D>(GetTransientPackage()" in tool
    assert "RegisterComponentWithWorld(World)" in tool
    assert "ON_SCOPE_EXIT" in tool
    assert "Capture->DestroyComponent();" in tool
    assert "SpawnActor" not in tool
    assert "FScopedTransaction" not in tool



def test_capture_scene_cuts_between_angles_rejects_ambiguous_labels_and_isolates_attachments():
    tool = _read("SoftUEBridgeEditor/Private/Tools/Editor/CaptureSceneTool.cpp")
    base_header = _read("SoftUEBridge/Public/Tools/BridgeToolBase.h")
    base_source = _read("SoftUEBridge/Private/Tools/BridgeToolBase.cpp")

    # Every angle is a camera cut with its own warm-up frames, so it never renders on the
    # previous angle's occlusion results or temporal history.
    loop = tool.split("for (int32 CameraIndex = 0;", 1)[1].split("ReadPixels", 1)[0]
    assert "Capture->bCameraCutThisFrame = true;" in loop
    assert loop.index("bCameraCutThisFrame") < loop.index("CaptureScene()")
    assert "CaptureSceneWarmupFrames + 1" in loop
    assert "CameraIndex == 0" not in loop

    # A label shared by several actors is an error, for the whole target and for Actor.Component.
    assert "static TArray<AActor*> FindActorsByNameOrLabel(" in base_header
    assert "UBridgeToolBase::FindActorsByNameOrLabel(" in base_source
    assert tool.count("FindActorsByNameOrLabel(World,") == 2
    assert tool.count("CaptureSceneAmbiguousError(") == 3
    assert "FindActorByNameOrLabel(" not in tool

    # Isolation shows the target and everything attached to it, recursively.
    assert "Actor->GetAttachedActors(IsolatedActors, false, true);" in tool
    assert "Capture->ShowOnlyActors.Add(Shown);" in tool
    assert 'SetArrayField(TEXT("isolated_actors")' in tool
