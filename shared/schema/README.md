# shared/schema/ — CSV column specs (System §7, §9)

Each `*.schema.yaml` here is the **single source of truth** for one shared CSV artifact.
From these specs the codegen (`tools/subcensus_tools/codegen.py`) produces:

- **C header constants** (`zero/generated/census_schema.h`) — exact header strings +
  column counts + indices the FAP writes/reads against, so the on-device CSVs match the
  contract without hand-maintained magic strings.
- **Python validators** — `tools/subcensus_tools/schema.py` loads these specs to validate
  any CSV (from either tool) column-for-column.

A schema change lands **here** and both consumers regenerate in the same commit (System
§10) — that is the whole point of the monorepo. Never hand-edit a generated header.

## Spec format

```yaml
name: <artifact>              # e.g. fingerprints
description: <one line>
system_ref: "System §7"       # where the contract is defined
scope: global|per-place|catalog
columns:
  - name: freq_bin
    type: int|float|str|bool|enum|device_class|ts
    required: true|false
    values: [OOK, 2-FSK, ...]   # only when type: enum
    desc: <one line>
```

Types:
- `int`, `float`, `str`, `bool` — scalar; `bool` is CSV `0`/`1`.
- `ts` — ISO-8601 wall-clock string (RTC-sourced on the Zero, System §5.2).
- `enum` — one of `values:`; empty allowed only when `required: false`.
- `device_class` — validated against `shared/taxonomy.yaml` (the taxonomy is the enum).

Artifacts:
- `fingerprints.schema.yaml` — global feature-vector library (System §7).
- `protocol_map.schema.yaml` — global decoded-protocol → name/class map (System §6).
- `occupancy.schema.yaml`   — per-place Recon Stage A activity map (System §9).
- `watchlist.schema.yaml`   — per-place Recon Stage C output (System §9).
- `catalog_record.schema.yaml` — shared core catalog fields (System §9); platforms extend.
- `census_log.schema.yaml`  — Zero's per-capture index = catalog core + Zero extensions
  (Zero §5.4). Generated header is what the FAP writes.
