"""pie-record: the CLI command that records a sampler script's output on every PIE world tick."""

from __future__ import annotations

import json
from unittest.mock import patch

import pytest

from soft_ue_cli.__main__ import build_parser


def test_parses_duration_and_script_with_defaults():
    args = build_parser().parse_args(["pie-record", "--duration", "2.5", "--script", "sampler.py"])

    assert args.command == "pie-record"
    assert args.duration == 2.5
    assert args.script == "sampler.py"
    assert args.delta is None
    assert args.output is None
    assert args.pie_instance is None
    assert args.timeout is None


def test_forwards_required_arguments_with_an_absolute_script_path(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    args = build_parser().parse_args(["pie-record", "--duration", "2", "--script", "sampler.py"])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"status": "ok"}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "pie-record",
        {"duration": 2.0, "script": str(tmp_path / "sampler.py")},
        timeout=180.0,
    )


def test_forwards_every_option_and_widens_the_http_timeout_past_the_tool_budget(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    args = build_parser().parse_args([
        "pie-record",
        "--duration", "1",
        "--script", "s.py",
        "--delta", "0.0111111",
        "--output", "out/rec.jsonl",
        "--pie-instance", "1",
        "--timeout", "30",
    ])

    with patch("soft_ue_cli.__main__._run_tool", return_value={"status": "ok"}) as mock_run:
        args.func(args)

    mock_run.assert_called_once_with(
        "pie-record",
        {
            "duration": 1.0,
            "script": str(tmp_path / "s.py"),
            "delta": 0.0111111,
            "output": str(tmp_path / "out" / "rec.jsonl"),
            "pie_instance": 1,
            "timeout": 30.0,
        },
        timeout=90.0,
    )


def test_a_failed_recording_prints_its_result_and_exits_nonzero(tmp_path, monkeypatch, capsys):
    monkeypatch.chdir(tmp_path)
    args = build_parser().parse_args(["pie-record", "--duration", "1", "--script", "s.py"])
    failure = {"success": False, "failed_frame": 3, "output_path": "C:/rec.jsonl", "traceback": "RuntimeError"}

    with patch("soft_ue_cli.__main__._run_tool", return_value=failure):
        with pytest.raises(SystemExit) as exc:
            args.func(args)

    assert exc.value.code == 1
    assert json.loads(capsys.readouterr().out) == failure
