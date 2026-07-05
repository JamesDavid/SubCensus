"""Drift guard for the Pi taxonomy fallback (System §5, §10).

`subcensuspi.taxonomy._FALLBACK` is a hand-copy of the shared/taxonomy.yaml vocabulary, used
only when the yaml can't be located (packaged without the repo). The yaml is the single source
of truth (System §10), so this codegen-style `--check` fails if a future yaml edit isn't mirrored
into `_FALLBACK` — the constant can't silently diverge.
"""

import yaml

from subcensuspi import taxonomy
from subcensuspi.paths import shared_taxonomy_path


def test_fallback_matches_shared_taxonomy_vocabulary():
    path = shared_taxonomy_path()
    assert path is not None, "shared/taxonomy.yaml must be locatable from the repo checkout"
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    yaml_ids = [str(c["id"]) for c in data["classes"]]
    assert taxonomy._FALLBACK == yaml_ids


def test_load_classes_agrees_with_fallback():
    # With the yaml present, the loaded vocabulary equals the fallback (both are the §5 list),
    # so the yaml-present and yaml-absent paths return the same class ids.
    assert [c["id"] for c in taxonomy.load_classes()] == taxonomy._FALLBACK


def test_class_ids_and_is_valid():
    ids = taxonomy.class_ids()
    assert "weather" in ids and "unknown" in ids and "other" in ids
    assert taxonomy.is_valid("weather")
    assert taxonomy.is_valid("")          # blank = unset/optional
    assert not taxonomy.is_valid("nope")
