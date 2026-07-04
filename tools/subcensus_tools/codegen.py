"""Codegen: shared/ single source of truth -> generated on-device artifacts (System §10).

Generates:
  - zero/census_taxonomy.h  from shared/taxonomy.yaml
  - zero/census_schema.h    from shared/schema/*.schema.yaml

`--check` regenerates in memory and fails if the on-disk files drift from the sources,
so CI (and the pre-milestone gate) can prove the tools can't have diverged.

Usage:
  python -m subcensus_tools.codegen           # write generated files
  python -m subcensus_tools.codegen --check    # verify up-to-date (exit 1 on drift)
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from .paths import repo_paths
from .schema import Schema, load_all_schemas
from .taxonomy import Taxonomy, c_enum_name

_BANNER = (
    "// ============================================================================\n"
    "// GENERATED FILE — DO NOT EDIT.\n"
    "// Source of truth: {src}\n"
    "// Regenerate: python -m subcensus_tools.codegen   (from tools/)\n"
    "// A schema/taxonomy change lands in shared/ and this file regenerates in the\n"
    "// same commit (System §10) — so the tools cannot drift.\n"
    "// ============================================================================\n"
)


def _macro_prefix(schema_name: str) -> str:
    return re.sub(r"[^A-Za-z0-9]+", "_", schema_name).strip("_").upper()


def _c_escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')


# --- taxonomy header --------------------------------------------------------------

def gen_taxonomy_header(tax: Taxonomy) -> str:
    lines: list[str] = []
    lines.append(_BANNER.format(src="shared/taxonomy.yaml"))
    lines.append("#pragma once")
    lines.append("")
    lines.append('#include <string.h>')
    lines.append("")
    lines.append(f"#define CENSUS_TAXONOMY_VERSION {tax.version}")
    lines.append("")
    lines.append("typedef enum {")
    for i, c in enumerate(tax.classes):
        note = f"  // {c.name}" + ("  [deprecated]" if c.deprecated else "")
        lines.append(f"    {c.enum_name} = {i},{note}")
    lines.append(f"    CENSUS_CLASS_COUNT = {len(tax.classes)},")
    lines.append("} CensusDeviceClass;")
    lines.append("")

    # id accessor (durable on-disk identity)
    lines.append("static inline const char* census_class_id(CensusDeviceClass c) {")
    lines.append("    switch(c) {")
    for c in tax.classes:
        lines.append(f'        case {c.enum_name}: return "{_c_escape(c.id)}";')
    lines.append('        default: return "unknown";')
    lines.append("    }")
    lines.append("}")
    lines.append("")

    # display-name accessor
    lines.append("static inline const char* census_class_name(CensusDeviceClass c) {")
    lines.append("    switch(c) {")
    for c in tax.classes:
        lines.append(f'        case {c.enum_name}: return "{_c_escape(c.name)}";')
    lines.append('        default: return "Unknown";')
    lines.append("    }")
    lines.append("}")
    lines.append("")

    # reverse lookup: id string -> enum (or -1). Empty/blank is a valid "unset".
    lines.append("// Returns the CensusDeviceClass for an id string, or -1 if unknown.")
    lines.append("static inline int census_class_from_id(const char* s) {")
    lines.append("    if(!s) return -1;")
    for i, c in enumerate(tax.classes):
        lines.append(f'    if(strcmp(s, "{_c_escape(c.id)}") == 0) return {i};')
    lines.append("    return -1;")
    lines.append("}")
    lines.append("")
    return "\n".join(lines)


# --- schema header ----------------------------------------------------------------

def gen_schema_header(schemas: dict[str, Schema]) -> str:
    lines: list[str] = []
    lines.append(_BANNER.format(src="shared/schema/*.schema.yaml"))
    lines.append("#pragma once")
    lines.append("")
    lines.append("// Exact CSV headers, column counts, and column indices for each shared")
    lines.append("// artifact. The FAP writes/reads against these so on-disk CSVs match the")
    lines.append("// contract without hand-maintained magic strings.")
    lines.append("")
    for name in sorted(schemas):
        s = schemas[name]
        pfx = _macro_prefix(name)
        header_str = ",".join(s.header())
        lines.append(f"// --- {name} ({s.system_ref}, scope={s.scope}) ---")
        lines.append(f'#define {pfx}_HEADER "{_c_escape(header_str)}"')
        lines.append(f"#define {pfx}_NCOLS {len(s.columns)}")
        lines.append("typedef enum {")
        for i, col in enumerate(s.columns):
            col_token = re.sub(r"[^A-Za-z0-9]+", "_", col.name).strip("_").upper()
            lines.append(f"    {pfx}_COL_{col_token} = {i},")
        lines.append(f"}} {_camel(name)}Col;")
        lines.append("")
    return "\n".join(lines)


def _camel(name: str) -> str:
    return "".join(part.capitalize() for part in re.split(r"[^A-Za-z0-9]+", name) if part)


# --- driver -----------------------------------------------------------------------

def _targets() -> list[tuple[Path, str]]:
    p = repo_paths()
    tax = Taxonomy.load(p["taxonomy"])
    schemas = load_all_schemas(p["schema_dir"])
    return [
        (p["zero_taxonomy_h"], gen_taxonomy_header(tax)),
        (p["zero_schema_h"], gen_schema_header(schemas)),
    ]


def _normalize(text: str) -> str:
    # Compare on content, ignoring trailing-newline / CRLF differences.
    return text.replace("\r\n", "\n").rstrip("\n") + "\n"


def write() -> list[Path]:
    written: list[Path] = []
    for path, content in _targets():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(_normalize(content), encoding="utf-8", newline="\n")
        written.append(path)
    return written


def check() -> list[Path]:
    """Return the list of files that are stale (empty = all up-to-date)."""
    stale: list[Path] = []
    for path, content in _targets():
        current = path.read_text(encoding="utf-8") if path.exists() else ""
        if _normalize(current) != _normalize(content):
            stale.append(path)
    return stale


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="SubCensus codegen (System §10).")
    ap.add_argument("--check", action="store_true", help="verify generated files are up-to-date")
    args = ap.parse_args(argv)
    root = repo_paths()["root"]
    if args.check:
        stale = check()
        if stale:
            print("Generated files are STALE (run `python -m subcensus_tools.codegen`):")
            for p in stale:
                print(f"  - {p.relative_to(root)}")
            return 1
        print("Generated files up-to-date.")
        return 0
    for p in write():
        print(f"wrote {p.relative_to(root)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
