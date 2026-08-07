from __future__ import annotations

import argparse
import json
from pathlib import Path

import httpx
import pytest


ROOT = Path(__file__).parents[1]

from soft_ue_cli.__main__ import build_parser  # noqa: E402
from soft_ue_cli import __main__ as main_mod  # noqa: E402
from soft_ue_cli.command_catalog import get_command_metadata  # noqa: E402
from soft_ue_cli.mcp_schema import extract_tools  # noqa: E402
from soft_ue_cli.mcp_server import _make_tool_fn  # noqa: E402


PLUGIN_ROOT = ROOT / "plugin" / "SoftUEBridge"
if not PLUGIN_ROOT.exists():
    PLUGIN_ROOT = ROOT / "soft_ue_cli" / "plugin_data" / "SoftUEBridge"
REFERENCES_DIR = (
    PLUGIN_ROOT / "Source" / "SoftUEBridgeEditor" / "Private" / "Tools" / "References"
)


def test_find_references_node_uses_completeness_helper_and_hard_error():
    source = (REFERENCES_DIR / "FindReferencesTool.cpp").read_text(encoding="utf-8")
    helper = (REFERENCES_DIR / "BridgeFiBCompleteness.cpp").read_text(encoding="utf-8")
    property_section = source[
        source.index("FBridgeToolResult UFindReferencesTool::FindPropertyReferences") :
        source.index("TArray<FSoftObjectPath> UFindReferencesTool::FindMatchingBlueprintsViaFiB")
    ]
    node_section = source[
        source.index("FBridgeToolResult UFindReferencesTool::FindNodeReferences(") :
        source.index("FBridgeToolResult UFindReferencesTool::FindNodeReferencesLegacy")
    ]

    assert '#include "Tools/References/BridgeFiBCompleteness.h"' in source
    assert "BridgeFiBCompleteness::Evaluate" in source
    assert "FBridgeToolResult::Error(Completeness.ErrorMessage)" in source
    assert 'SetBoolField(TEXT("result_complete"), true)' in source
    assert "SearchManager.GetFailedToCacheCount()" in source
    assert 'SetNumberField(TEXT("failed_to_cache_count"), FailedToCacheCount)' in source
    assert "bResultTruncated" in source
    assert "FailedToCacheCount == 0" in source
    assert "State.BlueprintsSearched == State.CandidateCount" in helper
    assert "State.bResultTruncated" in helper
    assert "ESearchMode::FiBCache" in helper
    assert "ESearchMode::FullFallback" in source
    assert "SearchMode" in node_section
    assert "bUsedFiBCache = FiBMatches.Num() > 0" in node_section
    assert "State.CandidateCount > 0 || bCacheReady" in helper
    assert "!State.bDiscoveryInProgressAtSelection" in helper
    selection_capture = node_section.index("bDiscoveryInProgressAtSelection")
    fallback_enumeration = node_section.index("GetBlueprintsInPath(AssetPath)")
    assert selection_capture < fallback_enumeration
    assert "bDiscoveryInProgressAtSelection};" in node_section
    fallback_section = helper[
        helper.index("const bool bFallbackComplete") : helper.index("const bool bModeComplete")
    ]
    assert "!State.bDiscoveryInProgress" in fallback_section
    assert "Refresh FiB state after traversal" in node_section
    assert node_section.count("SearchManager.GetFailedToCacheCount()") >= 2
    assert "Refresh FiB state after traversal" not in property_section
    assert "SearchManager.GetFailedToCacheCount()" not in property_section
    for field in (
        "cache_in_progress",
        "discovery_in_progress",
        "unindexed_count",
        "failed_to_cache_count",
        "candidate_count",
        "blueprints_searched",
    ):
        assert field in helper
    assert "indexing and retry" in helper


def test_find_references_raw_bridge_error_exits_cli_without_bug_nudge(monkeypatch, capsys):
    message = (
        "find-references node: incomplete_fib_index: cache_in_progress=true, "
        "discovery_in_progress=false, unindexed_count=2, failed_to_cache_count=0, candidate_count=1, "
        "blueprints_searched=0. Finish Find in Blueprints indexing and retry."
    )
    payload = {
        "jsonrpc": "2.0",
        "id": "1",
        "result": {"isError": True, "content": [{"type": "text", "text": message}]},
    }
    request = httpx.Request("POST", "http://127.0.0.1:8080/bridge")
    monkeypatch.setattr(
        "soft_ue_cli.client.httpx.post",
        lambda *_args, **_kwargs: httpx.Response(200, json=payload, request=request),
    )
    monkeypatch.setattr("soft_ue_cli.client.get_server_url", lambda: "http://127.0.0.1:8080")
    args = argparse.Namespace(
        type="node",
        asset_path="/Game",
        variable_name=None,
        node_class="K2Node_CallFunction",
        function_name=None,
        limit=None,
        search=None,
    )

    with pytest.raises(SystemExit) as exc:
        main_mod.cmd_find_references(args)

    assert exc.value.code == 1
    error_output = capsys.readouterr().err
    assert message in error_output
    assert "Looks like a bug?" not in error_output


def test_find_references_cli_preserves_ready_empty_fallback_result(monkeypatch, capsys):
    result = {
        "type": "node",
        "used_fib_cache": False,
        "fib_cache_status": {"cache_ready": True},
        "count": 0,
        "blueprints_searched": 0,
        "result_complete": True,
        "truncated": False,
    }
    monkeypatch.setattr(main_mod, "_run_tool", lambda *_args, **_kwargs: result)
    args = argparse.Namespace(
        type="node",
        asset_path="/Game/Empty",
        variable_name=None,
        node_class="K2Node_CallFunction",
        function_name=None,
        limit=None,
        search=None,
    )

    main_mod.cmd_find_references(args)

    assert json.loads(capsys.readouterr().out) == result


def test_find_references_help_explains_incomplete_fib_rejection(capsys):
    parser = build_parser()

    try:
        parser.parse_args(["find-references", "--help"])
    except SystemExit as exc:
        assert exc.code == 0

    help_text = capsys.readouterr().out
    assert "incomplete" in help_text.lower()
    assert "indexing" in help_text.lower()
    assert "retry" in help_text.lower()


@pytest.mark.parametrize("limit", ["0", "-1"])
def test_find_references_cli_rejects_non_positive_limit(limit, capsys):
    parser = build_parser()

    with pytest.raises(SystemExit) as exc:
        parser.parse_args([
            "find-references",
            "node",
            "/Game",
            "--node-class",
            "K2Node_CallFunction",
            "--limit",
            limit,
        ])

    assert exc.value.code == 2
    error_output = capsys.readouterr().err.lower()
    assert "positive integer" in error_output


def test_find_references_cli_accepts_positive_limit():
    args = build_parser().parse_args([
        "find-references",
        "node",
        "/Game",
        "--node-class",
        "K2Node_CallFunction",
        "--limit",
        "1",
    ])

    assert args.limit == 1


def test_find_references_mcp_schema_requires_positive_limit():
    tool = next(tool for tool in extract_tools() if tool["name"] == "find-references")

    assert tool["parameters"]["properties"]["limit"] == {
        "type": "integer",
        "description": "Max results (default: 100; must be positive)",
        "minimum": 1,
    }


def test_find_references_mcp_rejects_non_positive_limit_before_bridge(monkeypatch):
    from soft_ue_cli import mcp_server

    call_tool = pytest.fail
    monkeypatch.setattr(mcp_server._client, "call_tool_ex", call_tool)
    tool = next(tool for tool in extract_tools() if tool["name"] == "find-references")
    tool_fn = _make_tool_fn("find-references", tool["parameters"])

    result = json.loads(tool_fn(
        type="node",
        asset_path="/Game",
        node_class="K2Node_CallFunction",
        limit=0,
    ))

    assert result == {"error": "Parameter 'limit' must be at least 1."}


def test_find_references_mcp_preserves_positive_limit(monkeypatch):
    from unittest.mock import Mock

    from soft_ue_cli import mcp_server

    call_tool = Mock(return_value=({"count": 1}, Mock(notices=[])))
    monkeypatch.setattr(mcp_server._client, "call_tool_ex", call_tool)
    monkeypatch.setattr(mcp_server._streak, "record_success", lambda _tool: None)
    monkeypatch.setattr(mcp_server._streak, "should_nudge_testimonial", lambda: False)
    tool = next(tool for tool in extract_tools() if tool["name"] == "find-references")
    tool_fn = _make_tool_fn("find-references", tool["parameters"])

    result = json.loads(tool_fn(
        type="node",
        asset_path="/Game",
        node_class="K2Node_CallFunction",
        limit=1,
    ))

    assert result == {"count": 1}
    call_tool.assert_called_once_with(
        "find-references",
        {
            "type": "node",
            "asset_path": "/Game",
            "node_class": "K2Node_CallFunction",
            "limit": 1,
        },
        timeout=None,
    )


def test_find_references_catalog_warns_that_incomplete_node_results_are_rejected():
    metadata = get_command_metadata("find-references")
    summary = metadata["summary"].lower()

    assert "reject" in summary
    assert "incomplete" in summary
    assert any("--limit 25" in example for example in metadata["examples"])


def test_find_references_bridge_rejects_non_positive_limit_before_dispatch():
    source = (REFERENCES_DIR / "FindReferencesTool.cpp").read_text(encoding="utf-8")
    execute_section = source[
        source.index("FBridgeToolResult UFindReferencesTool::Execute(") :
        source.index("FBridgeToolResult UFindReferencesTool::FindAssetReferences")
    ]

    validation = execute_section.index("Limit <= 0")
    first_dispatch = execute_section.index('Type == TEXT("asset")')
    assert validation < first_dispatch
    assert "Parameter 'limit' must be a positive integer" in execute_section


def test_find_references_smoke_checks_node_result_completeness():
    skill = (ROOT / "soft_ue_cli" / "skills" / "test-tools.md").read_text(
        encoding="utf-8"
    )

    assert '_record("find-references node completeness"' in skill
    assert "incomplete_fib_index" in skill
    assert 'get("blueprints_searched", 0) > 0' in skill
    assert 'get("count", 0) > 0' in skill
