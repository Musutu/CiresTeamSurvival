"""Copy Art/Fab/FabAnimMap.json "replace" into Content/Data/FabAnimations.json without an editor run (fab-coverage).

RetargetFabAnimations.py writes the same rows after retargeting; this is for row-only edits (reordering, a new style
that reuses clips already retargeted). Same filter: a clip name is kept only when FabAnimations.json has its timing
(it retargeted onto at least one body), and empty kinds are dropped.

  python Tools/SyncFabAnimReplace.py
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    cfg = json.loads((ROOT / "Art/Fab/FabAnimMap.json").read_text(encoding="utf-8"))
    path = ROOT / "Content/Data/FabAnimations.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    clips = data.get("clips", {})
    replace = {row: {kind: [c for c in names if c in clips] for kind, names in kinds.items()} for row, kinds in cfg.get("replace", {}).items()}
    data["replace"] = {row: {k: v for k, v in kinds.items() if v} for row, kinds in replace.items()}
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    print("replace rows: %d" % len(data["replace"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
