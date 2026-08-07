from pathlib import Path


CHANGELOG = Path(__file__).resolve().parents[1] / "CHANGELOG.md"


def test_changelog_has_one_empty_unreleased_section_and_1450_release_notes():
    content = CHANGELOG.read_text(encoding="utf-8")

    assert content.count("## Unreleased") == 1
    unreleased = content.split("## Unreleased", 1)[1].split("\n## [", 1)[0]
    assert not unreleased.strip()

    release = content.split("## [1.45.0] - 2026-08-03", 1)[1].split("\n## [", 1)[0]
    assert "runtime-supported Chaos legacy cloth weight-map targets" in release
    assert "repeatable/comma-separated `--section-index`" in release
    assert "AnimGraph property-path diagnostics" in release
    assert "incomplete Find in Blueprints results" in release
    assert "additive `binding_warnings`" in release
    assert "`worlds` and `world_count`" in release

    release_1440 = content.split("## [1.44.0] - 2026-07-28", 1)[1].split("\n## [", 1)[0]
    assert "`blueprint node property` and `blueprint node add`" not in release_1440
    assert "Inner anim node property resolution" not in release_1440
    assert content.count("AnimGraph property-path diagnostics") == 1
