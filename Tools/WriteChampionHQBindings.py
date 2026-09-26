"""champion-hq: publish the ready HQ bodies (Content/Data/ChampionArt.hq.json) into the runtime data.

* Content/Data/ChampionArtBindings.json: every profile of a ready HQ row ("profileIds", default [profileId]) gets a
  status-ready row on the HQ mesh / BlendSpace / attack. The row it replaces is kept as "fallback" (mesh, locomotion,
  attack, heightCm, relaxArms), so UCireChampionArt falls back to the previous body if an HQ asset is missing.
  The five new-champions profiles are written by Tools/AuthorNewChampions.py, which reads the same file.
* Content/Data/ChampionAttacks02.json "bodies": HQ mesh -> its attacksFolder (the RetargetChampionAttacks output).
Plain Python; idempotent. --check exits 1 when the data is stale.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "Content" / "Data"
HQ = DATA / "ChampionArt.hq.json"
BINDINGS = DATA / "ChampionArtBindings.json"
ATTACKS = DATA / "ChampionAttacks02.json"
SUMMON_ART = DATA / "SummonArt.json"
NEW_CHAMPIONS = {"gunblade", "witch_slayer", "huntress", "aetheri_artificer", "aetheri_warden"}
FALLBACK_KEYS = ("mesh", "locomotion", "attack", "heightCm", "relaxArms", "yaw")
# The first three classes had no binding row: their previous body is CireChampionArt's built-in definition.
LEGACY = {
    "ranger": {"status": "ready", "mesh": "/Game/TripoModels/armored_archer_3d_model/armored_archer_3d_model.armored_archer_3d_model",
               "locomotion": "/Game/Art/Characters/TripoRetarget/Preview02/Ranger/Animations/BS_Idle_Walk_Run_Ranger.BS_Idle_Walk_Run_Ranger",
               "attack": "/Game/Art/Characters/CombatPrototype01/Ranger/A_Ranger_Attack.A_Ranger_Attack", "heightCm": 178},
    "scholar": {"status": "ready", "mesh": "/Game/TripoModels/battlefield_healer_3d_model/battlefield_healer_3d_model.battlefield_healer_3d_model",
                "locomotion": "/Game/Art/Characters/TripoRetarget/Preview02/Scholar/Animations/BS_Idle_Walk_Run_Scholar.BS_Idle_Walk_Run_Scholar",
                "attack": "/Game/Art/Characters/CombatPrototype01/Scholar/A_Scholar_Attack.A_Scholar_Attack", "heightCm": 176},
    "knight": {"status": "ready", "mesh": "/Game/TripoModels/medieval_knight_armor_3d_model/medieval_knight_armor_3d_model.medieval_knight_armor_3d_model",
               "locomotion": "/Game/Art/Characters/TripoRetarget/Preview02/Warden/Animations/BS_Idle_Walk_Run_Warden.BS_Idle_Walk_Run_Warden",
               "attack": "/Game/Art/Characters/CombatPrototype01/Warden/A_Warden_Attack.A_Warden_Attack", "heightCm": 184},
}


def hq_rows() -> list[dict]:
    if not HQ.is_file():
        return []
    return [r for r in json.loads(HQ.read_text(encoding="utf-8"))["champions"] if r.get("status") == "ready"]


def binding_for(row: dict, profile: str, previous: dict | None) -> dict:
    out = {"profileId": profile, "status": "ready", "mesh": row["mesh"], "locomotion": row["locomotion"],
           "attack": row["attack"], "heightCm": row.get("heightByProfile", {}).get(profile, row["heightCm"])}
    if "yaw" in row:
        out["yaw"] = row["yaw"]
    if row.get("relaxArms"):
        out["relaxArms"] = True
    out["note"] = "champion-hq: Tripo H3.1 multi-view HQ body (Art/ChampionHQ/TripoChampionHQ.json); fallback = previous body."
    prior = None
    if previous and previous.get("mesh", "").startswith("/Game/Tripo/ChampionsHQ/"):
        prior = previous.get("fallback") or LEGACY.get(profile)
    elif previous and previous.get("status") == "ready":
        prior = previous
    if prior:
        out["fallback"] = {k: prior[k] for k in FALLBACK_KEYS if k in prior}
    return out


def updated_bindings(text: str) -> str:
    # The five new-champions rows come from Tools/AuthorNewChampions.py (its final_bindings overlays the HQ rows and
    # keeps the tripo-races body as fallback); only that document section is regenerated here.
    import importlib.util
    spec = importlib.util.spec_from_file_location("AuthorNewChampions", str(ROOT / "Tools" / "AuthorNewChampions.py"))
    author = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(author)
    text = author.upsert_bindings(text)
    data = json.loads(text)
    rows = data["bindings"]
    index = {r["profileId"]: i for i, r in enumerate(rows)}
    for row in hq_rows():
        if row.get("summonId"):
            continue
        for profile in row.get("profileIds", [row["profileId"]]):
            if profile in NEW_CHAMPIONS:
                continue
            previous = rows[index[profile]] if profile in index else LEGACY.get(profile)
            new = binding_for(row, profile, previous)
            if profile in index:
                rows[index[profile]] = new
            else:
                index[profile] = len(rows)
                rows.append(new)
    return json.dumps(data, indent=2, ensure_ascii=False) + "\n"


def updated_attacks(text: str) -> str:
    """Text edit of the hand-formatted file: only new or changed "bodies" lines are touched."""
    data = json.loads(text)
    lines = text.split("\n")
    start = next(i for i, l in enumerate(lines) if l.strip().startswith('"bodies"'))
    end = next(i for i in range(start, len(lines)) if lines[i].strip().startswith("}"))
    for row in hq_rows():
        folder, mesh = row.get("attacksFolder"), row["mesh"]
        if not folder or data["bodies"].get(mesh) == folder:
            continue
        entry = "    %s: %s" % (json.dumps(mesh), json.dumps(folder))
        existing = [i for i in range(start + 1, end) if lines[i].strip().startswith(json.dumps(mesh) + ":")]
        if existing:
            lines[existing[0]] = entry + ("," if lines[existing[0]].rstrip().endswith(",") else "")
            continue
        if not lines[end - 1].rstrip().endswith(","):
            lines[end - 1] = lines[end - 1].rstrip() + ","
        lines.insert(end, entry)
        end += 1
    return "\n".join(lines)


def updated_summons(text: str) -> str:
    data = json.loads(text)
    rows = {r["id"]: r for r in data.get("summons", [])}
    for row in hq_rows():
        if not row.get("summonId"):
            continue
        rows[row["summonId"]] = {"id": row["summonId"], "status": "ready", "mesh": row["mesh"], "locomotion": row["locomotion"],
                                 "attack": row["attack"], "heightCm": row["heightCm"], "yaw": row.get("yaw", -90),
                                 "note": "champion-hq HQ summon body; no row = the summon's archetype body"}
    data["summons"] = [rows[k] for k in sorted(rows)]
    return json.dumps(data, indent=2, ensure_ascii=False) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    stale = []
    for path, fn in ((BINDINGS, updated_bindings), (ATTACKS, updated_attacks), (SUMMON_ART, updated_summons)):
        old = path.read_text(encoding="utf-8")
        new = fn(old)
        if json.loads(new) != json.loads(old):
            stale.append(path.name)
            if not args.check:
                path.write_text(new, encoding="utf-8", newline="\n")
    print(("stale: " if args.check else "updated: ") + (", ".join(stale) or "nothing"))
    return 1 if args.check and stale else 0


if __name__ == "__main__":
    raise SystemExit(main())
