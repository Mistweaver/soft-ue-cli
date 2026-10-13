"""Error classification and bug-report nudge helpers for soft-ue-cli."""

from __future__ import annotations

import enum
import json


class ErrorKind(enum.Enum):
    """Whether an error is expected (operational) or unexpected (likely a bug)."""

    EXPECTED = "expected"
    UNEXPECTED = "unexpected"


class BridgeError(Exception):
    """Raised by call_tool() on any failure, carrying classification metadata."""

    def __init__(
        self,
        kind: ErrorKind,
        message: str,
        tool_name: str,
        arguments: dict,
        notices: list | None = None,
        details: dict | None = None,
    ) -> None:
        super().__init__(message)
        self.kind = kind
        self.message = message
        self.tool_name = tool_name
        self.arguments = arguments
        self.notices = list(notices or [])
        # Structured fields from the bridge, such as a plugin_unavailable payload.
        self.details = dict(details or {})


PLUGIN_UNAVAILABLE = "plugin_unavailable"


def parse_structured_error(text: str) -> dict | None:
    """Return the JSON object in a tool error's text when it carries an error_code."""
    try:
        payload = json.loads(text)
    except (TypeError, ValueError):
        return None
    if isinstance(payload, dict) and payload.get("error_code"):
        return payload
    return None


def format_plugin_unavailable(payload: dict) -> str:
    """Render a plugin_unavailable payload as a one-line message plus recovery."""
    message = payload.get("message") or f"{payload.get('plugin', 'A required')} plugin is not enabled."
    recovery = payload.get("recovery")
    return f"{message}\n  recovery: {recovery}" if recovery else message


def format_bug_nudge(tool_name: str, error_message: str) -> str:
    """Return a CLI-formatted hint suggesting the user file a bug report."""
    safe_title = f"error in {tool_name}"
    safe_desc = error_message.replace('"', '\\"')
    return (
        "\n  Looks like a bug? Run:\n"
        f'    soft-ue-cli report-bug --title "{safe_title}" '
        f'--description "{safe_desc}"'
    )


def bug_nudge_payload(tool_name: str, error_message: str) -> dict:
    """Return a structured dict for MCP error responses."""
    return {
        "suggested_command": "report-bug",
        "suggested_args": {
            "title": f"error in {tool_name}",
            "description": error_message,
            "severity": "major",
        },
    }
