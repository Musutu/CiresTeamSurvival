"""Render the native shape audit (ability -> hit shape -> rendered shape) into Docs/AbilityVFXAudit.md (telegraphs).

The table is produced by CireAbilityVFX::RunTests (section 11, part of `Tools/RunExpansionChecks.py --only native`),
which paints every ability's true hit shape with the real telegraph painter and measures the painted geometry:
circular hit shapes must render round (angular max-radius spread under 8%), rectangles appear only for line hit
shapes and box-collision constructs. It writes Saved/ShapeAudit/shape_audit.json; this script rewrites the block
between <!-- shape-audit:start --> and <!-- shape-audit:end --> in the audit doc.

  python Tools/BuildShapeAudit.py [--json Saved/ShapeAudit/shape_audit.json]
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOC = ROOT / "Docs" / "AbilityVFXAudit.md"
START, END = "<!-- shape-audit:start -->", "<!-- shape-audit:end -->"


def cell(text: str) -> str:
    return str(text).replace("|", "/").replace("\n", " ")


def render(data: dict) -> str:
    rows = data["rows"]
    groups = Counter(r["group"] for r in rows)
    hits = Counter(r["hit"] for r in rows)
    lines = [START, "",
             f"{len(rows)} entries ({', '.join(f'{n} {g}' for g, n in sorted(groups.items()))}). "
             f"Hit shapes: {', '.join(f'{n} {h}' for h, n in sorted(hits.items()))}. "
             f"Square ground shapes: **{int(data['squares'])}**; circles not painted round: **{int(data['notRound'])}**.",
             "",
             "Roundness = largest / smallest painted radius over 72 angular bins (1.00 = perfect circle; a square paints 1.41).",
             "",
             "| # | Ability | Group | Status | Hit shape | Size (cm) | Rendered shape | Roundness | School | Fab ground overlay |",
             "|---|---|---|---|---|---|---|---|---|---|"]
    order = {"ability active": 0, "ability ultimate": 1, "ability passive": 2, "champion extra": 3, "summon / construct": 4,
             "construct": 5, "monster construct": 6, "monster": 7}
    rows = sorted(rows, key=lambda r: (order.get(r["group"], 9), r["group"], r["id"]))
    for i, r in enumerate(rows, 1):
        rnd = f"{r['roundness']:.3f}" if r["roundness"] else "-"
        school = r["school"] + (" (heal)" if r.get("heal") else "")
        lines.append(f"| {i} | {cell(r['name'])} `{cell(r['id'])}` | {cell(r['group'])} | {cell(r['status'])} | {r['hit']} | "
                     f"{cell(r['dims']) or '-'} | {cell(r['rendered'])} | {rnd} | {school} | {cell(r['fab'])} |")
    lines += ["", END]
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--json", type=Path, default=ROOT / "Saved" / "ShapeAudit" / "shape_audit.json")
    args = parser.parse_args()
    data = json.loads(args.json.read_text(encoding="utf-8"))
    text = DOC.read_text(encoding="utf-8")
    block = render(data)
    if START in text and END in text:
        head, rest = text.split(START, 1)
        text = head + block + rest.split(END, 1)[1]
    else:
        text = text.rstrip() + "\n\n## Shape audit (telegraphs, 2026-09-26)\n\n" + block + "\n"
    DOC.write_text(text, encoding="utf-8", newline="\n")
    print(f"CIRE_SHAPE_AUDIT_DOC rows={len(data['rows'])} squares={int(data['squares'])} not_round={int(data['notRound'])} -> {DOC}")


if __name__ == "__main__":
    main()
