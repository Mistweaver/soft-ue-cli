"""Guards for public package metadata and README freshness."""

from __future__ import annotations

import sys
import tomllib
from pathlib import Path

FORK_REPO_URL = "https://github.com/Mistweaver/soft-ue-cli"
UPSTREAM_REPO_URL = "https://github.com/softdaddy-o/soft-ue-cli"
UPSTREAM_AUTHOR_URL = "https://github.com/softdaddy-o"


def _repo_root() -> Path:
    for parent in Path(__file__).resolve().parents:
        if (parent / "cli" / "pyproject.toml").exists():
            return parent
        if (parent / "pyproject.toml").exists() and (parent / "soft_ue_cli").exists():
            return parent
    raise AssertionError("Could not locate repository root")


def _cli_root() -> Path:
    root = _repo_root()
    if (root / "cli" / "pyproject.toml").exists():
        return root / "cli"
    return root


def test_project_readme_metadata_points_at_public_readme():
    cli_root = _cli_root()
    pyproject = cli_root / "pyproject.toml"
    metadata = tomllib.loads(pyproject.read_text(encoding="utf-8"))
    readme_name = metadata["project"]["readme"]
    readme = cli_root / readme_name

    assert readme_name == "README.md"
    assert readme.exists()

    text = readme.read_text(encoding="utf-8")
    assert text.startswith("# soft-ue-cli")
    assert "Command Discovery And Taxonomy" in text
    assert "UE 5.8 MCP Positioning" in text
    assert "main development target is now **Unreal Engine 5.8**" in text
    assert "UE 5.7 compatibility remains maintained" in text


def test_readme_tool_count_claim_matches_current_mcp_surface():
    cli_root = _cli_root()
    sys.path.insert(0, str(cli_root))

    from soft_ue_cli.mcp_schema import extract_tools  # noqa: PLC0415

    readme = (cli_root / "README.md").read_text(encoding="utf-8")
    tool_count = len(extract_tools())

    assert tool_count >= 120
    assert "60+" not in readme
    assert "tools-60%2B" not in readme
    assert "commands-120%2B" in readme
    assert "120+" in readme


def _published_markdown_files() -> list[Path]:
    cli_root = _cli_root()
    return [
        cli_root / "README.md",
        cli_root / "CHANGELOG.md",
        *sorted((cli_root / "docs").rglob("*.md")),
        *sorted((cli_root / "soft_ue_cli").rglob("*.md")),
    ]


def test_markdown_files_have_no_encoding_artifacts():
    offenders = [
        f"{path.relative_to(_cli_root())}:{line_number}"
        for path in _published_markdown_files()
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1)
        if "??" in line
    ]

    assert offenders == []


def _readme_text() -> str:
    return (_cli_root() / "README.md").read_text(encoding="utf-8")


def _readme_section(heading: str) -> str:
    after_heading = _readme_text().split(f"\n## {heading}\n", 1)[1]
    return after_heading.split("\n## ", 1)[0]


def _table_row_value(section: str, label: str) -> str:
    row_prefix = f"| **{label}** |"
    row = next(line for line in section.splitlines() if line.startswith(row_prefix))
    return row.removeprefix(row_prefix).rstrip(" |")


def test_compatibility_table_lists_supported_engines_and_dependency_pins():
    pyproject = tomllib.loads((_cli_root() / "pyproject.toml").read_text(encoding="utf-8"))["project"]
    dependency_specs = [*pyproject["dependencies"], *pyproject["optional-dependencies"]["mcp"]]
    compatibility = _readme_section("Compatibility")

    assert _table_row_value(compatibility, "Unreal Engine").strip() == "5.7, 5.8"
    dependencies_row = _table_row_value(compatibility, "Dependencies")
    for spec in dependency_specs:
        assert f"`{spec}`" in dependencies_row


def _fork_notice() -> str:
    intro = _readme_text().split("\n## ", 1)[0]
    return "\n".join(line for line in intro.splitlines() if line.startswith(">"))


def test_readme_opens_with_fork_notice():
    notice = _fork_notice()

    assert f"]({FORK_REPO_URL})" in notice
    assert f"]({UPSTREAM_REPO_URL})" in notice
    assert f"]({UPSTREAM_AUTHOR_URL})" in notice
    assert "v1.45.0" in notice


def test_upstream_links_appear_only_in_credit_sections():
    upstream_markers = [
        UPSTREAM_AUTHOR_URL,
        "https://github.com/sponsors/softdaddy-o",
        "https://ko-fi.com/softdaddy",
        "https://pypi.org/project/soft-ue-cli",
        "https://img.shields.io/pypi/",
    ]
    readme_outside_credits = (
        _readme_text()
        .replace(_fork_notice(), "")
        .replace(_readme_section("Support this project"), "")
    )

    assert [marker for marker in upstream_markers if marker in readme_outside_credits] == []
