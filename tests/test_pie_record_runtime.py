"""The pie-record sampler runtime: the Python half of the bridge tool, which runs inside the editor.

It loads the user's sampler script once, calls its sample(ctx) after every PIE world tick, and writes
one JSON line per tick. It never imports unreal at module scope, so it runs here without the engine.
"""

from __future__ import annotations

import importlib.util
import json
import sys
from pathlib import Path

import pytest

RUNTIME_PATH = (
    Path(__file__).resolve().parents[1]
    / "soft_ue_cli"
    / "plugin_data"
    / "SoftUEBridge"
    / "Content"
    / "Python"
    / "soft_ue_bridge_pie_record.py"
)


@pytest.fixture
def runtime(monkeypatch):
    # Loaded by path and registered under its name, the way the bridge tool loads it in the editor.
    # No bytecode, so the test never leaves a __pycache__ in the plugin folder for BuildPlugin to package.
    monkeypatch.setattr(sys, "dont_write_bytecode", True)
    name = "soft_ue_bridge_pie_record"
    spec = importlib.util.spec_from_file_location(name, RUNTIME_PATH)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    yield module
    module.finish()
    del sys.modules[name]


def _write_sampler(tmp_path: Path, body: str) -> Path:
    script = tmp_path / "sampler.py"
    script.write_text(body, encoding="utf-8")
    return script


def _read_lines(path: Path) -> list[dict]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]


def _start(runtime, tmp_path: Path, script: Path, **overrides) -> Path:
    output = overrides.pop("output_path", tmp_path / "out" / "rec.jsonl")
    runtime.start(
        script_path=str(script),
        output_path=str(output),
        header={"delta_seconds": 0.5, "requested_frames": 2},
        world_path="/Game/Maps/Test.Test",
        find_world=overrides.pop("find_world", lambda path: f"world:{path}"),
    )
    return output


def test_start_writes_a_header_line_first(runtime, tmp_path):
    script = _write_sampler(tmp_path, "def sample(ctx):\n    return {}\n")

    output = _start(runtime, tmp_path, script)
    runtime.finish()

    assert _read_lines(output) == [
        {
            "type": "header",
            "script": str(script),
            "delta_seconds": 0.5,
            "requested_frames": 2,
        }
    ]


def test_sample_calls_the_script_with_frame_context_and_writes_its_result(runtime, tmp_path):
    script = _write_sampler(
        tmp_path,
        "def sample(ctx):\n"
        "    return {'frame': ctx.frame, 'time': ctx.world_time, 'dt': ctx.delta_seconds, 'world': ctx.world}\n",
    )
    output = _start(runtime, tmp_path, script)

    runtime.sample(0, 10.5, 0.5)
    runtime.sample(1, 11.0, 0.5)
    runtime.finish()

    samples = _read_lines(output)[1:]
    assert samples == [
        {
            "type": "sample",
            "frame": 0,
            "world_time": 10.5,
            "delta_seconds": 0.5,
            "data": {"frame": 0, "time": 10.5, "dt": 0.5, "world": "world:/Game/Maps/Test.Test"},
        },
        {
            "type": "sample",
            "frame": 1,
            "world_time": 11.0,
            "delta_seconds": 0.5,
            "data": {"frame": 1, "time": 11.0, "dt": 0.5, "world": "world:/Game/Maps/Test.Test"},
        },
    ]


def test_start_rejects_a_script_without_a_callable_sample_and_writes_nothing(runtime, tmp_path):
    script = _write_sampler(tmp_path, "sample = 3\n")
    output = tmp_path / "out" / "rec.jsonl"

    with pytest.raises(ValueError, match=r"does not define a callable sample\(ctx\)"):
        _start(runtime, tmp_path, script, output_path=output)

    assert not output.exists()


def test_start_rejects_a_missing_script_and_writes_nothing(runtime, tmp_path):
    missing = tmp_path / "nope.py"
    output = tmp_path / "out" / "rec.jsonl"

    with pytest.raises(FileNotFoundError, match="nope.py"):
        _start(runtime, tmp_path, missing, output_path=output)

    assert not output.exists()


def test_sample_rejects_a_result_json_cannot_encode_and_keeps_earlier_frames(runtime, tmp_path):
    script = _write_sampler(
        tmp_path,
        "def sample(ctx):\n"
        "    return {'ok': True} if ctx.frame == 0 else {'bad': object()}\n",
    )
    output = _start(runtime, tmp_path, script)
    runtime.sample(0, 0.5, 0.5)

    with pytest.raises(TypeError, match=r"frame 1: sample\(ctx\) returned a value JSON cannot encode"):
        runtime.sample(1, 1.0, 0.5)
    runtime.finish()

    lines = _read_lines(output)
    assert [line["type"] for line in lines] == ["header", "sample"]
    assert lines[1]["data"] == {"ok": True}


def test_an_exception_in_sample_reaches_the_caller_and_keeps_earlier_frames(runtime, tmp_path):
    script = _write_sampler(
        tmp_path,
        "def sample(ctx):\n"
        "    if ctx.frame == 1:\n"
        "        raise RuntimeError('bone missing')\n"
        "    return {}\n",
    )
    output = _start(runtime, tmp_path, script)
    runtime.sample(0, 0.5, 0.5)

    with pytest.raises(RuntimeError, match="bone missing"):
        runtime.sample(1, 1.0, 0.5)
    runtime.finish()

    assert [line["type"] for line in _read_lines(output)] == ["header", "sample"]


def test_sample_without_an_active_recording_is_an_error(runtime):
    with pytest.raises(RuntimeError, match="no pie-record recording is active"):
        runtime.sample(0, 0.0, 0.5)


def test_start_rejects_a_world_it_cannot_find_and_writes_nothing(runtime, tmp_path):
    script = _write_sampler(tmp_path, "def sample(ctx):\n    return {}\n")
    output = tmp_path / "out" / "rec.jsonl"

    with pytest.raises(LookupError, match="/Game/Maps/Test.Test"):
        _start(runtime, tmp_path, script, output_path=output, find_world=lambda path: None)

    assert not output.exists()
