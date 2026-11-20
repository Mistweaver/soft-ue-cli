"""Sampler runtime for the pie-record bridge tool.

The tool loads this file by path, calls start() once, sample() after every PIE world tick, and finish()
at the end, whatever happened. Each recording writes a header line and then one JSON line per tick.
Nothing here imports unreal at module scope, so the tests run it without the engine.
"""

from __future__ import annotations

import importlib.util
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable

_SAMPLER_MODULE_NAME = "soft_ue_pie_record_sampler"


@dataclass(frozen=True)
class SampleContext:
    """What sample(ctx) receives after each tick."""

    frame: int
    world_time: float
    delta_seconds: float
    world: Any


def _find_world_in_editor(world_path: str) -> Any:
    import unreal

    return unreal.find_object(None, world_path)


def _load_sampler(script_path: str) -> Callable[[SampleContext], Any]:
    """Loads the script fresh and keeps it in sys.modules until finish(), as a normal import would.

    dataclasses, typing, and pickle look a class's module up in sys.modules, so a script that is not
    registered there can fail in ways unrelated to its own code.
    """
    spec = importlib.util.spec_from_file_location(_SAMPLER_MODULE_NAME, script_path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[_SAMPLER_MODULE_NAME] = module
    try:
        spec.loader.exec_module(module)
        sampler = getattr(module, "sample", None)
        if not callable(sampler):
            raise ValueError(f"{script_path} does not define a callable sample(ctx)")
    except BaseException:
        sys.modules.pop(_SAMPLER_MODULE_NAME, None)
        raise
    return sampler


class _Recording:
    def __init__(self, output_path: Path, sampler: Callable[[SampleContext], Any], world: Any) -> None:
        self.sampler = sampler
        self.world = world
        output_path.parent.mkdir(parents=True, exist_ok=True)
        self._file = output_path.open("w", encoding="utf-8", newline="\n")

    def write(self, line: dict[str, Any]) -> None:
        """Encodes the whole line before writing, so a value JSON rejects never leaves a partial line.

        NaN and infinity are rejected rather than written as the NaN and Infinity tokens, which are not
        JSON and which strict readers refuse.
        """
        self._file.write(json.dumps(line, ensure_ascii=False, allow_nan=False) + "\n")
        self._file.flush()

    def close(self) -> None:
        self._file.close()


# The bridge drives one recording at a time through module-level calls, so the open one lives here.
_active: _Recording | None = None


def start(
    script_path: str,
    output_path: str,
    header: dict[str, Any],
    world_path: str,
    find_world: Callable[[str], Any] | None = None,
) -> None:
    """Loads the sampler script, finds the world, and writes the header line. Ends any earlier recording."""
    global _active
    finish()
    sampler = _load_sampler(script_path)
    world = (find_world or _find_world_in_editor)(world_path)
    if world is None:
        sys.modules.pop(_SAMPLER_MODULE_NAME, None)
        raise LookupError(f"PIE world {world_path} was not found")
    _active = _Recording(Path(output_path), sampler, world)
    _active.write({"type": "header", "script": script_path, **header})


def sample(frame: int, world_time: float, delta_seconds: float) -> None:
    """Calls the sampler for one tick and writes its result as a sample line."""
    recording = _active
    if recording is None:
        raise RuntimeError("no pie-record recording is active; call start() first")
    context = SampleContext(frame=frame, world_time=world_time, delta_seconds=delta_seconds, world=recording.world)
    data = recording.sampler(context)
    line = {
        "type": "sample",
        "frame": frame,
        "world_time": world_time,
        "delta_seconds": delta_seconds,
        "data": data,
    }
    try:
        recording.write(line)
    except (TypeError, ValueError) as exc:
        raise TypeError(f"frame {frame}: sample(ctx) returned a value JSON cannot encode: {exc}") from exc


def finish() -> None:
    global _active
    sys.modules.pop(_SAMPLER_MODULE_NAME, None)
    if _active is not None:
        _active.close()
        _active = None
