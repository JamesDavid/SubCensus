"""Repo-root discovery so the tools work regardless of CWD.

The repo root is the nearest ancestor containing `shared/taxonomy.yaml` — the single
source of truth. Everything else is derived from it, so no path needs hard-coding.
"""

from __future__ import annotations

from pathlib import Path


class RepoNotFound(RuntimeError):
    pass


def find_repo_root(start: Path | None = None) -> Path:
    """Walk up from `start` (default: this file) to the SubCensus repo root."""
    here = (start or Path(__file__)).resolve()
    for candidate in (here, *here.parents):
        if (candidate / "shared" / "taxonomy.yaml").is_file():
            return candidate
    raise RepoNotFound(
        "Could not locate the SubCensus repo root "
        "(no ancestor contains shared/taxonomy.yaml)."
    )


def repo_paths(start: Path | None = None) -> dict[str, Path]:
    root = find_repo_root(start)
    return {
        "root": root,
        "shared": root / "shared",
        "taxonomy": root / "shared" / "taxonomy.yaml",
        "schema_dir": root / "shared" / "schema",
        "core": root / "shared" / "core",
        "tools": root / "tools",
        "test": root / "test",
        "zero": root / "zero",
        "esp": root / "esp",
        # Generated artifacts (System §10) — regenerated, never hand-edited. Both the Zero FAP
        # and the ESP firmware are consumers, regenerated together (System §10).
        "zero_taxonomy_h": root / "zero" / "census_taxonomy.h",
        "zero_schema_h": root / "zero" / "census_schema.h",
        "esp_taxonomy_h": root / "esp" / "src" / "census_taxonomy.h",
        "esp_schema_h": root / "esp" / "src" / "census_schema.h",
    }
