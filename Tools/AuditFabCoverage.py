"""Fab coverage audit (fab-coverage): which animation and spell-VFX slots are served by the installed Fab packs.

Reads the committed mapping data (FabAnimations.json, ChampionArtBindings*.json, RaceMeshes*.json, FabVFX.json,
Abilities.json, BuffVisuals.json) and checks every referenced asset on disk in the main checkout (where the packs
and /Game/FabDerived live), mirroring the runtime lookups:
  animation  CireFabAnimation::Pick (style row -> kind -> first clip this body has) and the Fab creature bodies;
  spell VFX  CireFabVFX::FindFor (abilities.<id>.<role> -> schools.<school>.<role>) and FindBuff (id -> kind.school -> kind).

  python Tools/AuditFabCoverage.py                 # current data -> Docs/FabCoverage.md auto sections
  python Tools/AuditFabCoverage.py --ref main      # the same audit on another git ref's data (the "before")
  python Tools/AuditFabCoverage.py --summary       # print the percentages only

Docs/FabCoverage.md keeps its hand-written parts; only the blocks between <!-- AUTO:<name> --> and
<!-- /AUTO:<name> --> are rewritten (anim, vfx, monsters, buffs, summary).
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DOC = REPO / "Docs" / "FabCoverage.md"


def main_checkout() -> Path:
    out = subprocess.run(["git", "worktree", "list", "--porcelain"], cwd=REPO, capture_output=True, text=True).stdout
    return Path(out.splitlines()[0].split(" ", 1)[1].strip())


CONTENT = main_checkout() / "Content"


def load(rel: str, ref: str | None):
    if ref:
        text = subprocess.run(["git", "show", "%s:%s" % (ref, rel)], cwd=REPO, capture_output=True, text=True, encoding="utf-8").stdout
        return json.loads(text) if text.strip() else {}
    p = REPO / rel
    return json.loads(p.read_text(encoding="utf-8")) if p.exists() else {}


def exists(obj_path: str) -> bool:
    if not obj_path or not obj_path.startswith("/Game/"):
        return False
    pkg = obj_path.split(".", 1)[0][len("/Game/"):]
    return (CONTENT / (pkg + ".uasset")).exists()


PACKS = [("GDHBundle", "GDH"), ("MaleLocomotionSet", "Male Locomotion"), ("Gun_and_Sword", "Gun & Sword"),
         ("CrossbowPackAnim", "Crossbow Set"), ("ROG_Creatures", "ROG Creatures"), ("QuadrapedCreatures", "Quadruped Fantasy"),
         ("UndeadPack", "Undead Pack"), ("Big_Pack_Magic_VFX", "Big Pack"), ("Shadow_Magic", "Shadow Magic"),
         ("State_VFX", "State VFX"), ("Earth_Spells", "Earth Spells"), ("Forest_VFX", "Nature VFX"),
         ("RealisticBlood", "Realistic Blood"), ("FXVarietyPack", "FX Variety (Cascade)"), ("Free", "free CC0"), ("Tripo", "Tripo"),
         ("TripoModels", "Tripo"), ("Art", "Tripo/project")]


def pack_of(path: str) -> str:
    top = path[len("/Game/"):].split("/", 1)[0] if path.startswith("/Game/") else ""
    return next((name for folder, name in PACKS if folder == top), top)


def stem(path: str) -> str:
    return path.rsplit(".", 1)[-1].replace("NS_", "")


# ------------------------------------------------------------------ animation
SLOTS = ["idle", "walk", "run", "strafe", "jump", "roll", "attack", "hit", "death"]


def fab_clip_source(anim_map, clip):
    row = anim_map.get("clips", {}).get(clip)
    return pack_of(row["path"]) if row else "Fab"


def audit_champions(ref):
    roster = load("Content/Data/ChampionRoster.json", ref).get("champions", [])
    loadouts = load("Content/Data/WeaponLoadouts.json", ref).get("profiles", {})
    bindings = {b["profileId"]: b for b in load("Content/Data/ChampionArtBindings.json", ref).get("bindings", [])}
    fab_bind = {b["profileId"]: b for b in load("Content/Data/ChampionArtBindings.fab.json", ref).get("bindings", [])}
    attacks02 = load("Content/Data/ChampionAttacks02.json", ref)
    bodies = attacks02.get("bodies", {})
    shouts, spells = set(attacks02.get("shoutSkills", [])), set(attacks02.get("spellSkills", []))
    fab = load("Content/Data/FabAnimations.json", ref)
    anim_map = load("Art/Fab/FabAnimMap.json", ref) or load("Art/Fab/FabAnimMap.json", None)
    replace, windows = fab.get("replace", {}), fab.get("clips", {})
    root = fab.get("root", "/Game/FabDerived/Anim")

    def kind_of(skill):
        if skill in shouts or "roar" in skill or "shout" in skill or skill.endswith("_cry"):
            return "shout"
        return "spell" if skill in spells else "ability"

    rows, counts = [], {"fab": 0, "total": 0, "distinct": 0, "skills": 0}
    for champ in roster:
        pid = champ["id"]
        skills = [a["id"] for a in champ.get("actives", []) if a.get("id")]
        if champ.get("ultimate", {}).get("id"):
            skills.append(champ["ultimate"]["id"])
        cells = {}
        fb = fab_bind.get(pid)
        if fb and exists(fb.get("mesh", "")) and exists(fb.get("animations", {}).get("idle", "")):
            a = fb["animations"]
            src = pack_of(fb["mesh"])
            for s in SLOTS:
                key = {"strafe": None, "jump": None, "roll": None}.get(s, s)
                cells[s] = ("Fab " + src) if key and exists(a.get(key, "")) else "procedural (pack has no clip)"
            for sk in skills:
                clip = a.get("casts", {}).get(sk) or a.get("casts", {}).get(kind_of(sk)) or a.get("casts", {}).get("ability")
                cells["skill:" + sk] = ("Fab " + src + (" own" if a.get("casts", {}).get(sk) else " shared")) if clip and exists(clip) else "attack clip"
        elif pid == "whisp" or bindings.get(pid, {}).get("motion") == "hover_procedural":
            for s in SLOTS:
                cells[s] = "procedural hover (by design)"
            for sk in skills:
                cells["skill:" + sk] = "procedural hover (by design)"
        else:
            # The three launch champions draw the Preview02 bodies hard-wired in CireChampionArt (no binding row).
            b = bindings.get(pid) or {"knight": {"folder": "Warden"}, "ranger": {"folder": "Ranger"}, "scholar": {"folder": "Scholar"}}.get(pid, {})
            if "folder" in b:
                b = dict(b, locomotion="Preview02", attack="Preview02")
            folder = b.get("folder") or bodies.get(b.get("mesh", ""), "")
            style = loadouts.get(pid, "")
            row = replace.get("style:" + style, {})

            def fab_pick(kind, exclude=()):
                for clip in row.get(kind, []):
                    if clip in windows and clip not in exclude and folder and \
                            (CONTENT / ("FabDerived/Anim/%s/A_%s_%s.uasset" % (folder, folder, clip))).exists():
                        return clip
                return None
            bs = folder and (CONTENT / ("FabDerived/Anim/%s/BS_Fab_Locomotion_%s.uasset" % (folder, folder))).exists()
            loco_src = "Fab " + ("Male Locomotion" if style in ("gunblade", "tripo_gunblade", "unarmed") else "GDH") if bs else None
            for s in ("idle", "walk", "run"):
                cells[s] = loco_src or ("Tripo clip" if b.get("locomotion") else "procedural")
            cells["strafe"] = (loco_src + " 8-dir") if bs else "procedural lean"
            for s in ("jump", "roll", "hit", "death"):
                c = fab_pick(s)
                cells[s] = ("Fab " + fab_clip_source(anim_map, c)) if c else "procedural"
            c = fab_pick("attack")
            cells["attack"] = ("Fab " + fab_clip_source(anim_map, c)) if c else ("Tripo clip" if b.get("attack") else "procedural")
            basic = set(row.get("attack", []))
            for sk in skills:
                k = kind_of(sk)
                c = fab_pick(k)
                if not c:
                    cells["skill:" + sk] = "Tripo clip (%s)" % {"shout": "war_cry", "spell": "cast_a_spell"}.get(k, "slash")
                else:
                    own = c not in basic
                    cells["skill:" + sk] = "Fab %s %s" % (fab_clip_source(anim_map, c), "own" if own else "reuses attack combo")
        for key, v in cells.items():
            counts["total"] += 1
            counts["fab"] += v.startswith("Fab")
            if key.startswith("skill:"):
                counts["skills"] += 1
                counts["distinct"] += v.startswith("Fab") and not v.endswith("reuses attack combo") and not v.endswith("shared")
        rows.append((champ["id"], champ.get("displayName", pid), cells, skills))
    return rows, counts


def audit_monsters(ref):
    races = load("Content/Data/Races.json", ref).get("races", {})
    fab = load("Content/Data/RaceMeshes.fab.json", ref).get("units", {})
    free = load("Content/Data/RaceMeshes.free.json", ref).get("archetypes", {})
    tripo = load("Content/Data/RaceMeshes.tripo.json", ref).get("archetypes", {})
    npc = load("Content/Data/NPCMeshes.tripo.json", ref).get("archetypes", {})
    npc_arch = load("Content/Data/NPCArchetypes.json", ref).get("archetypes", {})
    overlay = load("Content/Data/MonsterFabClips.json", ref).get("variants", {})
    rows, counts = [], {"units": 0, "fab": 0, "slots": 0, "fabslots": 0, "castclips": 0, "needcast": 0}
    for rid, race in races.items():
        for uid, unit in race.get("units", {}).items():
            abilities = (unit.get("archetype") or {}).get("abilities") or unit.get("abilities") or npc_arch.get(uid, {}).get("abilities", [])
            kinds = {str(a.get("type", "melee")).lower() for a in abilities if isinstance(a, dict)}
            # CireMonsterArt::ClipForAbility: self circles want ground_slam; rallies, enrages, guards, pulls,
            # summons and target circles want war_cry / cast_a_spell; otherwise the attack swing is right.
            needs = kinds & {"selfcircle", "targetcircle", "guard", "provoke", "rally", "enrage", "healally", "shieldwall", "summon", "deploy", "pull"}
            layer, body = None, None
            # Runtime priority (CireMonsterArt::Load): Fab packs, then Tripo race / NPC bodies, then free CC0 last.
            for name, table in (("Fab", fab), ("Tripo", tripo), ("Tripo", npc), ("free CC0", free)):
                e = table.get(uid)
                if e and exists(e.get("mesh", "")) and exists(e.get("animations", {}).get("idle", "")):
                    layer, body = name, e
                    break
            anims = (body or {}).get("animations", {})
            src = (pack_of(body["mesh"]) if layer == "Fab" else layer) if body else "mannequin fallback"
            cells = {s: (src if exists(anims.get(s, "")) else "-") for s in ("idle", "walk", "run", "attack", "attackAlt", "hit", "death")}
            named = {k for k, v in (anims.get("all") or {}).items() if exists(v)}
            fab_named = set()
            if body and layer != "Fab":
                variant = body.get("variant", uid)
                fab_named = {k for k, v in overlay.get(variant, {}).items() if exists(v)} - named
            casts = sorted(named & {"cast_a_spell", "war_cry", "ground_slam", "fire_breath", "axe_throw", "tentacle_sweep", "attack_bow", "attack_crossbow"})
            casts += ["%s (Fab GDH/G&S)" % k for k in sorted(fab_named)]
            have = named | fab_named
            ok = not needs or (("selfcircle" not in needs or "ground_slam" in have) and
                               (not (needs - {"selfcircle"}) or bool(have & {"war_cry", "cast_a_spell"})))
            cells["casts"] = (", ".join(casts) if casts else "attack clip") + ("" if ok else " **(shout/slam abilities reuse the attack)**")
            counts["units"] += 1
            counts["fab"] += layer == "Fab"
            counts["needcast"] += bool(needs)
            counts["castclips"] += bool(needs) and ok
            for s in ("idle", "walk", "run", "attack", "hit", "death"):
                counts["slots"] += 1
                counts["fabslots"] += cells[s] != "-" and layer == "Fab"
            rows.append((rid, uid, src, cells))
    return rows, counts


# ------------------------------------------------------------------ spell VFX
SCHOOL = {"physical": "steel", "cold": "frost", "storm": "storm", "fire": "fire", "earth": "earth", "tide": "tide", "holy": "holy",
          "shadow": "shadow", "void": "void", "poison": "poison", "nature": "nature", "arcane": "arcane"}
DELIVERY_ROLES = {"targeted": ["cast", "impact"], "self": ["cast"], "ally": ["cast"], "projectile": ["cast", "projectile", "impact"],
                  "chain": ["cast", "projectile", "impact"], "ground_circle": ["cast", "area"], "ground_cone": ["cast", "area"],
                  "ground_line": ["cast", "area"], "ground_square": ["cast", "area"], "construct": ["cast", "area"],
                  "summon": ["cast", "impact"], "transformation": ["cast"], "passive": []}
TARGETING_ROLES = {"enemy": ["cast", "impact"], "self": ["cast"], "ally": ["cast"], "aim": ["cast", "area"], "passive": []}


def first_existing(entry):
    if entry is None:
        return None
    paths = entry if isinstance(entry, list) else [entry] if isinstance(entry, str) else entry.get("paths", [])
    return next((p for p in paths if exists(p)), None)


def audit_vfx(ref):
    fab = load("Content/Data/FabVFX.json", ref)
    abilities = load("Content/Data/Abilities.json", ref).get("abilities", {})
    roster = load("Content/Data/ChampionRoster.json", ref).get("champions", [])
    delivery = {}
    for c in roster:
        for s in c.get("actives", []) + [c.get("ultimate") or {}, c.get("passive") or {}]:
            if s.get("id"):
                delivery.setdefault(s["id"], s.get("delivery"))
    schools, own = fab.get("schools", {}), fab.get("abilities", {})
    rows, counts = [], {"abilities": 0, "visual": 0, "covered": 0, "signature": 0, "slots": 0, "fabslots": 0, "ownslots": 0}
    for aid, a in abilities.items():
        school = SCHOOL.get(a.get("school", "physical"), "steel")
        roles = list(DELIVERY_ROLES.get(delivery.get(aid) or "", TARGETING_ROLES.get(a.get("targeting", ""), ["cast"])))
        passive = a.get("kind") == "passive"
        if passive:
            roles = []
        roles += [r for r in own.get(aid, {}) if r not in roles]
        heal = not passive and a.get("section") == "defensive" and a.get("targeting") == "ally"
        cells = {}
        for role in ("cast", "projectile", "impact", "area"):
            if role not in roles:
                cells[role] = ""
                continue
            p = first_existing(own.get(aid, {}).get(role))
            if p:
                cells[role] = "**%s** (%s)" % (stem(p), pack_of(p))
                continue
            sch = "life" if heal and role in ("cast", "impact") else school
            p = first_existing(schools.get(sch, {}).get(role)) or first_existing(schools.get("default", {}).get(role))
            cells[role] = ("%s (%s school)" % (stem(p), sch)) if p else "procedural"
        counts["abilities"] += 1
        if roles:
            counts["visual"] += 1
            fab_roles = [r for r in roles if cells[r] != "procedural"]
            own_roles = [r for r in roles if cells[r].startswith("**")]
            counts["slots"] += len(roles)
            counts["fabslots"] += len(fab_roles)
            counts["ownslots"] += len(own_roles)
            counts["covered"] += len(fab_roles) == len(roles)
            counts["signature"] += len(own_roles) == len(roles)
        rows.append((aid, a.get("name", aid), a.get("school", ""), a.get("kind", ""), roles, cells))
    return rows, counts


def audit_buffs(ref):
    fab = load("Content/Data/FabVFX.json", ref).get("buffs", {})
    buffs = load("Content/Data/BuffVisuals.json", ref).get("buffs", {})
    rows, counts = [], {"buffs": 0, "own": 0, "shared": 0, "none": 0}
    for bid, b in sorted(buffs.items()):
        e = fab.get(bid.lower())
        how = "own"
        if not first_existing(e):
            e, how = fab.get(("%s.%s" % (b.get("kind", ""), b.get("school", ""))).lower()), "shared kind.school"
            if not first_existing(e):
                e, how = fab.get(b.get("kind", "").lower()), "shared kind"
        p = first_existing(e)
        counts["buffs"] += 1
        counts["own" if p and how == "own" else "shared" if p else "none"] += 1
        rows.append((bid, b.get("name", bid), b.get("kind", ""), b.get("school", ""),
                     ("**%s** (%s)" % (stem(p), pack_of(p)) if how == "own" else "%s (%s)" % (stem(p), how)) if p else "procedural signature only"))
    return rows, counts


def pct(a, b):
    return "%d/%d (%d%%)" % (a, b, round(100.0 * a / b) if b else 0)


def summary(ref):
    _, ca = audit_champions(ref)
    _, cm = audit_monsters(ref)
    _, cv = audit_vfx(ref)
    _, cb = audit_buffs(ref)
    return {
        "Champion animation slots from Fab": pct(ca["fab"], ca["total"]),
        "Champion ability casts with their own Fab clip": pct(ca["distinct"], ca["skills"]),
        "Monster units on a Fab body": pct(cm["fab"], cm["units"]),
        "Monster units whose shout/slam/cast abilities have a matching clip": pct(cm["castclips"], cm["needcast"]),
        "Ability presentation slots with a Fab effect": pct(cv["fabslots"], cv["slots"]),
        "Abilities fully Fab-covered (every shown role)": pct(cv["covered"], cv["visual"]),
        "Abilities with their own signature in every role": pct(cv["signature"], cv["visual"]),
        "Buff/debuff states with their own Fab overlay": pct(cb["own"], cb["buffs"]),
        "Buff/debuff states with any Fab overlay": pct(cb["own"] + cb["shared"], cb["buffs"]),
    }


def render(ref_before):
    out = {}
    before, after = summary(ref_before), summary(None)
    lines = ["| Metric | Before (`%s`) | After |" % ref_before, "|---|---|---|"]
    lines += ["| %s | %s | %s |" % (k, before[k], after[k]) for k in after]
    out["summary"] = "\n".join(lines)

    rows, _ = audit_champions(None)
    lines = ["| Champion | " + " | ".join(SLOTS) + " | Ability casts |", "|---|" + "---|" * (len(SLOTS) + 1)]
    for pid, name, cells, skills in rows:
        sk = "<br>".join("`%s` %s" % (s, cells.get("skill:" + s, "")) for s in skills)
        lines.append("| %s (`%s`) | %s | %s |" % (name, pid, " | ".join(cells.get(s, "") for s in SLOTS), sk))
    out["anim"] = "\n".join(lines)

    rows, _ = audit_monsters(None)
    lines = ["| Race | Unit | Body | idle | walk | run | attack | alt | hit | death | ability casts |", "|---|---|---|---|---|---|---|---|---|---|---|"]
    for rid, uid, src, c in rows:
        lines.append("| %s | `%s` | %s | %s | %s |" % (rid, uid, src, " | ".join("x" if c[s] != "-" else "-" for s in
                                                                           ("idle", "walk", "run", "attack", "attackAlt", "hit", "death")), c["casts"]))
    out["monsters"] = "\n".join(lines)

    rows, _ = audit_vfx(None)
    lines = ["Bold = the ability's own signature system; `(x school)` = shared school set; blank = the role is not shown.", "",
             "| Ability | School | Cast | Projectile | Impact | Area |", "|---|---|---|---|---|---|"]
    for aid, name, school, kind, roles, cells in rows:
        tag = " *(passive)*" if kind == "passive" else ""
        lines.append("| %s (`%s`)%s | %s | %s | %s | %s | %s |" % (name, aid, tag, school, cells["cast"], cells["projectile"], cells["impact"], cells["area"]))
    out["vfx"] = "\n".join(lines)

    rows, _ = audit_buffs(None)
    lines = ["| Buff / debuff | Kind | School | Fab overlay |", "|---|---|---|---|"]
    for bid, name, kind, school, src in rows:
        lines.append("| %s (`%s`) | %s | %s | %s |" % (name, bid, kind, school, src))
    out["buffs"] = "\n".join(lines)
    return out


def write_doc(blocks):
    text = DOC.read_text(encoding="utf-8") if DOC.exists() else ""
    for name, body in blocks.items():
        start, end = "<!-- AUTO:%s -->" % name, "<!-- /AUTO:%s -->" % name
        block = "%s\n%s\n%s" % (start, body, end)
        if start in text:
            text = re.sub(re.escape(start) + r".*?" + re.escape(end), lambda _: block, text, flags=re.S)
        else:
            text += "\n\n" + block + "\n"
    DOC.write_text(text, encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ref", help="audit another git ref's data only (print summary)")
    ap.add_argument("--before", default="main", help="git ref for the 'before' column (default main)")
    ap.add_argument("--summary", action="store_true")
    args = ap.parse_args()
    if args.ref or args.summary:
        for k, v in summary(args.ref).items():
            print("%-52s %s" % (k, v))
        return 0
    write_doc(render(args.before))
    print("wrote " + str(DOC))
    for k, v in summary(None).items():
        print("%-52s %s" % (k, v))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
