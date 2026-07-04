#!/usr/bin/env python3
"""analyze_place — provider-agnostic structured analysis of a place bundle (System §8).

Feeds the export_place bundle to a model and requests structured JSON:
  { inventory, identifications, field_maps, anomalies, coverage_gaps, recommended_actions }
Provider-agnostic (anthropic | openai-compatible | local) so it runs against an API OR local
inference — default LOCAL, because a map of a home's wireless devices is sensitive (cloud is
opt-in, System §8). The model call is injectable so the round-trip is testable with no network.

Label feedback is human-in-the-loop: high-confidence identifications become *proposed* labels;
a confirm step (or --apply gated by a confidence floor) writes confirmed ones into the brain
via build_signatures.py. Never a silent auto-relabel (System §6).

  python tools/analyze_place.py --bundle <dir>/bundle.json --out <dir> \
      --provider openai-compatible --base-url http://localhost:11434/v1 --model llama3
"""

from __future__ import annotations

import argparse
import json
import os
import re
from pathlib import Path
from typing import Callable

from subcensus_tools.place_bundle import render_prompt

CallModel = Callable[[list[dict]], str]
REQUIRED_KEYS = ("inventory", "identifications", "field_maps", "anomalies",
                 "coverage_gaps", "recommended_actions")


def _extract_json(text: str) -> dict:
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
    messages = [
        {"role": "system", "content": "You are an RF/ISM analyst. Return one JSON object."},
        {"role": "user", "content": render_prompt(bundle)},
    ]
    data = _extract_json(call_model(messages))
    for k in REQUIRED_KEYS:
        data.setdefault(k, [])
    return data


def proposed_labels(analysis: dict, confidence_floor: float = 0.8) -> list[dict]:
    """High-confidence identifications -> *proposed* labels (never auto-applied, System §6)."""
    return [
        idn for idn in analysis.get("identifications", [])
        if isinstance(idn, dict) and float(idn.get("confidence", 0)) >= confidence_floor
    ]


def write_analysis(out_dir: str | Path, analysis: dict) -> Path:
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    (out / "analysis.json").write_text(json.dumps(analysis, indent=2, sort_keys=True), encoding="utf-8")
    lines = ["# SubCensus place analysis", ""]
    for key in REQUIRED_KEYS:
        lines.append(f"## {key.replace('_', ' ').title()}")
        items = analysis.get(key, [])
        lines += [f"- {json.dumps(it) if not isinstance(it, str) else it}" for it in items] or ["_none_"]
        lines.append("")
    (out / "analysis.md").write_text("\n".join(lines), encoding="utf-8")
    return out


def make_call_model(provider: str, base_url: str, model: str, api_key: str | None) -> CallModel:
    """Construct a provider-agnostic call_model. Real inference needs network/a running model
    (opt-in). openai-compatible covers local servers (Ollama/llama.cpp) and OpenAI-style APIs."""
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
        # openai-compatible (also local: Ollama/llama.cpp)
        headers = {"Authorization": f"Bearer {api_key}"} if api_key else {}
        r = httpx.post(f"{base_url}/chat/completions", headers=headers,
                       json={"model": model, "messages": messages}, timeout=120)
        return r.json()["choices"][0]["message"]["content"]

    return call


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--bundle", required=True, help="bundle.json from export_place")
    ap.add_argument("--out", help="output dir for analysis.json/.md (default: bundle's dir)")
    ap.add_argument("--provider", choices=["anthropic", "openai-compatible", "local"], default="local")
    ap.add_argument("--base-url", default="http://localhost:11434/v1")
    ap.add_argument("--model", default="llama3")
    ap.add_argument("--api-key", default=os.environ.get("SUBCENSUS_API_KEY"))
    ap.add_argument("--print-prompt", action="store_true", help="just print the prompt (no model call)")
    args = ap.parse_args(argv)

    bundle = json.loads(Path(args.bundle).read_text(encoding="utf-8"))
    if args.print_prompt:
        print(render_prompt(bundle))
        return 0

    call = make_call_model(args.provider, args.base_url, args.model, args.api_key)
    analysis = analyze_bundle(bundle, call)
    out = Path(args.out or Path(args.bundle).parent)
    write_analysis(out, analysis)
    print(f"wrote {out/'analysis.json'} ({len(proposed_labels(analysis))} proposed labels)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
