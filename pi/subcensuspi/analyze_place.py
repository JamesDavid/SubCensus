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

import json
import re
from pathlib import Path
from typing import Callable

from .export_place import render_prompt

# call_model(messages) -> raw assistant text
CallModel = Callable[[list[dict]], str]

REQUIRED_KEYS = ("inventory", "identifications", "field_maps", "anomalies",
                 "coverage_gaps", "recommended_actions")


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


def write_analysis(place_dir: str | Path, analysis: dict, raw_markdown: str | None = None) -> Path:
    out = Path(place_dir)
    out.mkdir(parents=True, exist_ok=True)
    (out / "analysis.json").write_text(json.dumps(analysis, indent=2, sort_keys=True), encoding="utf-8")
    md = raw_markdown or _render_analysis_md(analysis)
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
