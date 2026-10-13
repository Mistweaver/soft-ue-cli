"""Tests for the plugin_unavailable error contract across client, CLI, and MCP."""

from __future__ import annotations

import json

import httpx
import pytest

from soft_ue_cli import __main__ as main_mod
from soft_ue_cli import client as client_mod
from soft_ue_cli.errors import BridgeError, ErrorKind
from soft_ue_cli.mcp_server import create_server


_PLUGIN_PAYLOAD = {
    "success": False,
    "error_code": "plugin_unavailable",
    "plugin": "PythonScriptPlugin",
    "command": "run-python-script",
    "message": "PythonScriptPlugin plugin is not enabled.",
    "recovery": "Enable it in Edit > Plugins > Scripting > Python Editor Script Plugin, then restart the editor.",
}


@pytest.fixture
def plugin_unavailable_bridge(monkeypatch):
    """Serve a bridge response shaped like UBridgeToolBase::PluginUnavailable."""
    raw = {
        "jsonrpc": "2.0",
        "id": "1",
        "result": {
            "isError": True,
            "content": [{"type": "text", "text": json.dumps(_PLUGIN_PAYLOAD)}],
        },
    }
    request = httpx.Request("POST", "http://127.0.0.1:8080/bridge")
    monkeypatch.setattr(
        "soft_ue_cli.client.httpx.post",
        lambda *_args, **_kwargs: httpx.Response(200, json=raw, request=request),
    )
    monkeypatch.setattr("soft_ue_cli.client.get_server_url", lambda: "http://127.0.0.1:8080")
    client_mod.clear_session_label()
    yield
    client_mod.clear_session_label()


def test_client_raises_expected_error_with_structured_details(plugin_unavailable_bridge):
    with pytest.raises(BridgeError) as exc:
        client_mod.call_tool("run-python-script", {"script": "print(1)"})

    assert exc.value.kind == ErrorKind.EXPECTED
    assert exc.value.details == _PLUGIN_PAYLOAD
    assert exc.value.message.startswith("PythonScriptPlugin plugin is not enabled.")
    assert "recovery: Enable it in Edit > Plugins" in exc.value.message


def test_client_keeps_unexpected_kind_for_other_json_errors(monkeypatch):
    raw = {
        "jsonrpc": "2.0",
        "id": "1",
        "result": {
            "isError": True,
            "content": [{"type": "text", "text": json.dumps({"error_code": "something_else"})}],
        },
    }
    request = httpx.Request("POST", "http://127.0.0.1:8080/bridge")
    monkeypatch.setattr(
        "soft_ue_cli.client.httpx.post",
        lambda *_args, **_kwargs: httpx.Response(200, json=raw, request=request),
    )
    monkeypatch.setattr("soft_ue_cli.client.get_server_url", lambda: "http://127.0.0.1:8080")

    with pytest.raises(BridgeError) as exc:
        client_mod.call_tool("query-level", {})

    assert exc.value.kind == ErrorKind.UNEXPECTED
    assert exc.value.details == {"error_code": "something_else"}


def test_cli_exits_non_zero_with_recovery_and_no_bug_hint(plugin_unavailable_bridge, capsys):
    with pytest.raises(SystemExit) as exc:
        main_mod._run_tool("run-python-script", {"script": "print(1)"})

    assert exc.value.code == 1
    captured = capsys.readouterr()
    assert captured.out == ""
    assert "error: PythonScriptPlugin plugin is not enabled." in captured.err
    assert "recovery: Enable it in Edit > Plugins" in captured.err
    assert "report-bug" not in captured.err


def test_mcp_returns_structured_fields(plugin_unavailable_bridge):
    pytest.importorskip("mcp")
    server = create_server()
    tool_fn = next(
        tool.fn for tool in server._tool_manager._tools.values() if tool.name == "run-python-script"
    )

    payload = json.loads(tool_fn(script="print(1)"))

    assert payload["error"].startswith("Tool 'run-python-script' failed: PythonScriptPlugin")
    for key in ("error_code", "plugin", "command", "message", "recovery"):
        assert payload[key] == _PLUGIN_PAYLOAD[key]
    assert "bug_report_hint" not in payload
