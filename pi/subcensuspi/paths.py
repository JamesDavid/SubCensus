"""Locate the repo root / shared contract from within the installed pi package."""

from __future__ import annotations

from pathlib import Path


def find_repo_root(start: Path | None = None) -> Path | None:
    here = (start or Path(__file__)).resolve()
    for candidate in (here, *here.parents):
        if (candidate / "shared" / "taxonomy.yaml").is_file():
            return candidate
    return None


def shared_taxonomy_path() -> Path | None:
    root = find_repo_root()
    return (root / "shared" / "taxonomy.yaml") if root else None
