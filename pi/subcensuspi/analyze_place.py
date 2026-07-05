"""analyze_place — provider-agnostic structured analysis of a place bundle (System §8).

Feeds the export_place bundle to a model and requests structured JSON:
  { inventory, identifications, field_maps, anomalies, coverage_gaps, recommended_actions }
Provider-agnostic (anthropic | openai-compatible | local) so it runs against an API OR local
inference — default LOCAL, because a map of a home's wireless devices is sensitive (cloud is
opt-in, System §8). The model call is injected, so the round-trip is testable with no network;
real providers are constructed from config (a real key/endpoint = the only external need).

**Label feedback is human-in-the-loop**: high-confidence identifications become *proposed*
labels; a confirm step writes confirmed ones into the brain via build_signatures.py. Never a
silent auto-relabel (System §6).
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
from pathlib import Path
from typing import Callable

from . import taxonomy
from .export_place import render_prompt

# call_model(messages) -> raw assistant text
CallModel = Callable[[list[dict]], str]

REQUIRED_KEYS = ("inventory", "identifications", "field_maps", "anomalies",
                 "coverage_gaps", "recommended_actions")

# Shared protocol_map schema header (System §6; shared/schema/protocol_map.schema.yaml).
PROTOCOL_MAP_HEADER = ("protocol", "friendly_name", "device_class", "typical_use", "notes")


def _extract_json(text: str) -> dict:
    """Pull the first JSON object out of a model response (handles ```json fences)."""
    fence = re.search(r"```(?:json)?\s*(\{.*?\})\s*```", text, re.DOTALL)
    blob = fence.group(1) if fence else text
    start = blob.find("{")
    if start < 0:
        raise ValueError("no JSON object in model response")
    depth = 0
    for i in range(start, len(blob)):
        if blob[i] == "{":
            depth += 1
        elif blob[i] == "}":
            depth -= 1
            if depth == 0:
                return json.loads(blob[start : i + 1])
    raise ValueError("unterminated JSON object")


def analyze_bundle(bundle: dict, call_model: CallModel) -> dict:
    """Run the analysis round-trip. Returns the parsed structured result (missing keys
    defaulted to [] so downstream code is robust)."""
    prompt = render_prompt(bundle)
    messages = [
        {"role": "system", "content": "You are an RF/ISM analyst. Return one JSON object."},
        {"role": "user", "content": prompt},
    ]
    raw = call_model(messages)
    data = _extract_json(raw)
    for k in REQUIRED_KEYS:
        data.setdefault(k, [])
    return data


def proposed_labels(analysis: dict, confidence_floor: float = 0.8) -> list[dict]:
    """High-confidence identifications -> *proposed* labels (never auto-applied, System §6)."""
    return [
        idn for idn in analysis.get("identifications", [])
        if isinstance(idn, dict) and float(idn.get("confidence", 0)) >= confidence_floor
    ]


def apply_labels(analysis: dict, signatures_dir: str | Path, confidence_floor: float = 0.8,
                 *, valid_classes: set[str] | None = None) -> list[dict]:
    """`--apply` write-back into the GLOBAL brain (System §8). Takes the high-confidence
    identifications and merges them into the shared `protocol_map.csv` (signature ->
    device_class, System §6 tier 1) — the same artifact build_signatures.py maintains. RX-only
    (Pi §invariants); this is a bookkeeping write, not a transmission. Never silent — returns
    what was applied so the CLI can print it (System §6: explicit confirmation, gated by a floor).

    `valid_classes`, when given, skips any candidate outside the taxonomy (System §5)."""
    proposed = proposed_labels(analysis, confidence_floor)
    sig = Path(signatures_dir)
    sig.mkdir(parents=True, exist_ok=True)
    pm_path = sig / "protocol_map.csv"

    existing: dict[str, dict] = {}
    if pm_path.exists():
        with pm_path.open("r", newline="", encoding="utf-8") as fh:
            for r in csv.DictReader(fh):
                proto = (r.get("protocol") or "").strip()
                if proto:
                    existing[proto] = {k: (r.get(k) or "") for k in PROTOCOL_MAP_HEADER}

    applied: list[dict] = []
    for idn in proposed:
        proto = str(idn.get("signature", "")).strip()
        cls = str(idn.get("candidate", "")).strip()
        if not proto or not cls:
            continue
        if valid_classes is not None and cls not in valid_classes:
            continue
        conf = float(idn.get("confidence", 0))
        row = existing.get(proto, {k: "" for k in PROTOCOL_MAP_HEADER})
        row["protocol"] = proto
        row["device_class"] = cls
        if not row.get("friendly_name"):
            row["friendly_name"] = str(idn.get("name") or proto)
        row["notes"] = f"applied via analyze_place --apply (confidence {conf:.2f}, source=user)"
        existing[proto] = row
        applied.append(row)

    if applied:
        with pm_path.open("w", newline="", encoding="utf-8") as fh:
            w = csv.DictWriter(fh, fieldnames=list(PROTOCOL_MAP_HEADER))
            w.writeheader()
            for row in sorted(existing.values(), key=lambda r: r["protocol"]):
                w.writerow({k: row.get(k, "") for k in PROTOCOL_MAP_HEADER})
    return applied


def diff_analyses(prev: dict | None, curr: dict) -> dict:
    """Diff a prior analysis vs the current one (System §8: re-runnable, diffs vs the prior).
    Reports what identifications / field_maps / anomalies changed."""
    prev = prev or {}

    def _keyed(items: list, key: str) -> dict:
        return {str(it.get(key, "")): it for it in items if isinstance(it, dict)}

    out: dict = {}
    for section in ("identifications", "field_maps"):
        p = _keyed(prev.get(section, []), "signature")
        c = _keyed(curr.get(section, []), "signature")
        out[section] = {
            "added": [c[k] for k in c if k not in p],
            "removed": [p[k] for k in p if k not in c],
            "changed": [{"signature": k, "before": p[k], "after": c[k]}
                        for k in c if k in p and c[k] != p[k]],
        }
    pa = {json.dumps(x, sort_keys=True) for x in prev.get("anomalies", [])}
    ca = {json.dumps(x, sort_keys=True) for x in curr.get("anomalies", [])}
    out["anomalies"] = {
        "added": [json.loads(x) for x in sorted(ca - pa)],
        "removed": [json.loads(x) for x in sorted(pa - ca)],
    }
    return out


def write_analysis(place_dir: str | Path, analysis: dict, raw_markdown: str | None = None) -> Path:
    out = Path(place_dir)
    out.mkdir(parents=True, exist_ok=True)
    json_path = out / "analysis.json"
    prior = None
    if json_path.exists():
        try:
            prior = json.loads(json_path.read_text(encoding="utf-8"))
        except (ValueError, OSError):
            prior = None
    diff = diff_analyses(prior, analysis) if prior is not None else None

    json_path.write_text(json.dumps(analysis, indent=2, sort_keys=True), encoding="utf-8")
    md = raw_markdown or _render_analysis_md(analysis)
    if diff is not None:
        md = md.rstrip("\n") + "\n\n" + _render_diff_md(diff)
    (out / "analysis.md").write_text(md, encoding="utf-8")
    return out


def _render_analysis_md(analysis: dict) -> str:
    lines = ["# SubCensus place analysis", ""]
    for key in REQUIRED_KEYS:
        lines.append(f"## {key.replace('_', ' ').title()}")
        items = analysis.get(key, [])
        if not items:
            lines.append("_none_")
        for it in items:
            lines.append(f"- {json.dumps(it) if not isinstance(it, str) else it}")
        lines.append("")
    return "\n".join(lines)


def _render_diff_md(diff: dict) -> str:
    lines = ["## Diff vs prior analysis", ""]
    empty = True
    for section in ("identifications", "field_maps", "anomalies"):
        d = diff.get(section, {})
        for kind in ("added", "removed", "changed"):
            for it in d.get(kind, []):
                empty = False
                lines.append(f"- **{section} {kind}**: {json.dumps(it)}")
    if empty:
        lines.append("_no changes vs the prior analysis_")
    lines.append("")
    return "\n".join(lines)


def make_call_model(provider: str, base_url: str, model: str, api_key: str | None) -> CallModel:
    """Provider-agnostic call_model (default local; cloud is opt-in, System §8). openai-compatible
    also covers local servers (Ollama/llama.cpp)."""
    import httpx  # pragma: no cover - needs a provider

    def call(messages: list[dict]) -> str:  # pragma: no cover - needs a provider
        if provider == "anthropic":
            r = httpx.post(
                f"{base_url}/v1/messages",
                headers={"x-api-key": api_key or "", "anthropic-version": "2023-06-01"},
                json={"model": model, "max_tokens": 2048,
                      "messages": [m for m in messages if m["role"] != "system"],
                      "system": next((m["content"] for m in messages if m["role"] == "system"), "")},
                timeout=120,
            )
            return r.json()["content"][0]["text"]
        headers = {"Authorization": f"Bearer {api_key}"} if api_key else {}
        r = httpx.post(f"{base_url}/chat/completions", headers=headers,
                       json={"model": model, "messages": messages}, timeout=120)
        return r.json()["choices"][0]["message"]["content"]

    return call


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="analyze a Pi place bundle (System §8)")
    ap.add_argument("--bundle", required=True, help="bundle.json from export_place")
    ap.add_argument("--out", help="output dir for analysis.json/.md (default: bundle's dir)")
    ap.add_argument("--provider", choices=["anthropic", "openai-compatible", "local"], default="local")
    ap.add_argument("--base-url", default="http://localhost:11434/v1")
    ap.add_argument("--model", default="llama3")
    ap.add_argument("--api-key", default=os.environ.get("SUBCENSUS_API_KEY"))
    ap.add_argument("--print-prompt", action="store_true", help="just print the prompt (no model call)")
    ap.add_argument("--apply", action="store_true",
                    help="write high-confidence IDs into the global brain (System §8; explicit)")
    ap.add_argument("--confidence-floor", type=float, default=0.8,
                    help="min confidence for a proposed/applied label (System §8, default 0.8)")
    ap.add_argument("--signatures-dir", help="global signatures/ dir to --apply into (System §4)")
    args = ap.parse_args(argv)

    bundle = json.loads(Path(args.bundle).read_text(encoding="utf-8"))
    if args.print_prompt:
        print(render_prompt(bundle))
        return 0

    call = make_call_model(args.provider, args.base_url, args.model, args.api_key)
    analysis = analyze_bundle(bundle, call)
    out = Path(args.out or Path(args.bundle).parent)
    write_analysis(out, analysis)
    proposed = proposed_labels(analysis, args.confidence_floor)
    print(f"wrote {out/'analysis.json'} ({len(proposed)} proposed labels >= {args.confidence_floor})")

    if args.apply:
        if not args.signatures_dir:
            ap.error("--apply requires --signatures-dir (the global brain to write into)")
        valid = set(taxonomy.class_ids())
        applied = apply_labels(analysis, args.signatures_dir, args.confidence_floor,
                               valid_classes=valid)
        for row in applied:
            print(f"applied: {row['protocol']} -> {row['device_class']} ({row['notes']})")
        skipped = len(proposed) - len(applied)
        print(f"applied {len(applied)} label(s) into {Path(args.signatures_dir)/'protocol_map.csv'}"
              + (f"; skipped {skipped} (empty/off-taxonomy)" if skipped else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
