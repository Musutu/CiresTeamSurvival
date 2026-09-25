"""Write Docs/AbilityVFXAudit.md from a BEFORE (legacy) and AFTER capture of Tools/RunAbilityVFXGallery.py.

    python Tools/BuildAbilityVFXAudit.py BEFORE_DIR AFTER_DIR [COMPARE_DIR]

Each row is one ability cast in isolation. "Before" and "Problems" come from the review of the legacy
captures (per behaviour class, plus per-ability notes for champion skills and every refused cast);
"Fix" names what the new presentation draws. Evidence: the before/after comparison sheet.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Per-ability review notes for the champion pool (legacy captures, one by one).
CHAMPION = {
    "shield_slam": ("Holy sigil and helix drawn on the target; no link to the caster.", "No indication of who struck whom; reads like a heal.",
                    "Ground streak caster->target, 75 cm hit ring, holy impact with rays; clip contact on release."),
    "iron_guard": ("Holy sigil/helix spawned on the SELECTED ENEMY when one was targeted.", "Wrong placement: the self buff appeared on the enemy.",
                   "Anchored on the caster; personal pulse ring; buff aura (BuffVisuals) unchanged."),
    "war_cry": ("Two amber rings growing to ~230 cm around the caster.", "True taunt radius is 850 cm; the rings under-sold the reach by 3.7x.",
                "Ground shockwave that expands exactly to 850 cm and holds the edge; legacy rings kept at the caster."),
    "cleaving_strike": ("Small forged crescent at the target, no area.", "Hits every enemy within 320 cm of the caster; nothing showed that disc.",
                        "Shockwave to the true 320 cm radius around the caster plus steel sparks/debris impacts."),
    "second_wind": ("Holy sigil on the selected unit (enemy when targeted).", "Wrong placement; small.", "On the caster, personal pulse ring."),
    "protection_dome": ("Placement preview only; cage trims fine.", "Preview had no centre mark.", "Aim footprint with animated spot marker; cage unchanged."),
    "bastion_of_dawn": ("Large holy sigil at the caster.", "Guard reach (650 cm) not shown.", "Shockwave to 650 cm, holy flourish kept."),
    "stone_skin": ("Passive; basic swing only.", "None (passive).", "No telegraph by design; basic swing impact improved (sparks/debris)."),
    "chain_spark": ("Lightning from caster to target; later victims only got a star.", "Hops invisible; chain reach (500 cm) unknown.",
                    "Bolt to the first target, arcs hop to each further victim, faint 500 cm hop ring."),
    "ember_lance": ("Aim: flat green strip. Cast: fire ring + embers at the AIM POINT. Projectile sat unannounced for 0.35 s.",
                    "Point marker at the designated spot instead of a line; no enemy-visible telegraph; no direction cue.",
                    "Aim lane with arrowhead + travelling chevrons; 0.35 s warning lane of the true 60 cm x 1200 cm corridor that fills; caster flare; fire head/wake; ground glow under the head; burst on end."),
    "frost_bind": ("Same as Ember Lance with frost crystals at the aim point.", "Point marker instead of a line; no warning lane.",
                   "Aim lane + arrow; 68 cm x 1200 cm warning lane; crystal spear head; frost impact spikes."),
    "piercing_shot": ("Thin strip aim; 18 cm projectile nearly invisible; steel cast ring at aim.", "Unreadable projectile; point marker.",
                      "Lane + arrow (36 cm x 1500 cm); arrow shaft head with 20 cm readability floor, white streak, bursts."),
    "shadow_step": ("Shadow ring/helix at the target.", "No dash read.", "Violet ground streak caster->target, hit ring, shadow implosion impact."),
    "venom_ground": ("Flat green disc + thin tubes; amber flat warning.", "No timing cue; edge hard to see on terrain.",
                     "Soft feathered disc, pulsing border, fill that grows to detonation, spot marker; bubbling poison vapour while active."),
    "grave_line": ("Flat line strip, amber warning, shadow ring at the caster.", "No direction/arrow, no timing.", "Lane with arrowhead + chevrons, progress fill from the caster, detonation front, dissolve."),
    "spectral_pack": ("Summon circle preview; wolves appear.", "Cast refused in the old harness (620 cm aim > 600 cast range).", "Placement preview with spot marker; spirit shock ring on arrival."),
    "executioners_verdict": ("Blood sigil rings on the target.", "Weak for an ultimate; no strike read.", "Ground streak + hit ring, blood impact with debris, camera kick when near you."),
    "battle_rhythm": ("Passive; bow shot.", "Arrow barely visible.", "Basic arrow gets a lit head + wake (ranged basics)."),
    "restoring_light": ("Life helix drawn on the selected ENEMY when one was targeted (heal went to self).", "Wrong placement.",
                        "Re-anchored on the real heal target; ground streak to allies; life rays impact."),
    "sanctuary": ("Holy sigil at the caster.", "600 cm heal reach not shown.", "Shockwave to 600 cm, holy flourish."),
    "purify": ("Life helix on the selected enemy.", "Wrong placement.", "Re-anchored on the ally / self."),
    "renewal": ("Life sigil at caster.", "1000 cm reach not shown.", "Shockwave to 1000 cm."),
    "soul_conduit": ("Passive.", "None.", "No telegraph by design."),
    "ashen_square": ("Flat square fill, amber warning.", "No timing cue.", "Soft square, pulsing border, growing fill, centre marker, detonation."),
    "cataclysm": ("Fire ring (~140 cm) at the target.", "True radius 550 cm around the target not shown.", "Shockwave to 550 cm + fire flourish, camera kick."),
    "oathbound_guardian": ("Small placement circle.", "-", "Spot marker preview; spirit arrival pulse."),
    "summoned_wall": ("Rectangle preview.", "-", "Preview with animated border; runic trims unchanged."),
    "deep_reserves": ("Passive.", "None.", "No telegraph by design."),
    "cinder_cone": ("Flat cone; amber warning; fire ring at the caster.", "No direction cue or timing.", "Cone with chevrons, progress wedge, detonation front, ember spikes."),
    "blight_sigil": ("Flat concave polygon.", "Edges hard to see.", "Feathered concave fill (ear-clipped), pulsing border, marker, vapour."),
    "last_stand": ("Earth ring at the selected unit.", "Wrong placement when an enemy is selected.", "On the caster with a personal pulse."),
    "challenge_of_iron": ("Amber rings ~230 cm.", "True 850 cm reach not shown.", "Shockwave to 850 cm."),
    "seismic_reprisal": ("Flat amber warning then earth ring.", "No timing cue.", "School-coloured warning with growing fill, detonation spikes."),
    "starfall": ("Flat circle, starfall sigil at aim.", "No timing cue.", "Designated-spot marker kept; growing fill, meteor flourish, detonation front."),
    "spectral_hunt": ("Spirit wisps.", "-", "Streak to target; hunters unchanged."),
    "mass_aegis": ("Shield sigil at caster.", "900 cm reach not shown.", "Shockwave to 900 cm."),
    "wellspring": ("Nature bloom on the selection.", "-", "Streak to ally, bloom kept."),
    "basic_sword": ("Crescent on hit.", "-", "Steel sparks/debris impact."),
    "basic_bow": ("Prototype arrow mesh only.", "Hard to follow at distance.", "Lit arrow head + wake following the real flight."),
    "basic_lance": ("Prototype lance mesh only.", "Hard to follow.", "Lit head + wake."),
    "basic_arcane": ("Small ember sphere.", "Hard to follow.", "Arcane orb head with orbiting sparks + wake."),
}

MONSTER_KIND = {
    "cone": ("Flat amber cone; cast cue drew a family ring (often the steel crescent) at the victim.",
             "No timing or direction cue; the ring at the victim read like a separate circle attack; school colour lost.",
             "Amber cone with outward chevrons and a wedge that grows to release, pulsing border; release flash + school spikes; wind-up motes at the caster instead of a mark on the victim."),
    "circle": ("Flat amber disc; family ring at the victim.", "No timing cue; duplicate marker.",
               "Amber disc with growing fill, spot marker, detonation front, school particles; pools ripple while active."),
    "line": ("Flat amber rectangle from the caster.", "No arrow/direction or timing cue.",
             "Amber lane with arrowhead, travelling chevrons and progress fill of the true width/length."),
    "projectile": ("No telegraph at all during the cast bar; small projectile.", "Enemy skillshot undodgeable by sight.",
                   "Amber lane of the skillshot's true corridor for the whole cast bar (dropped on interrupt), readable head + wake + burst."),
    "self": ("Cast cue ring drawn at the victim's feet or the steel crescent.", "Wrong place; unclear it is a self buff.",
             "School wind-up motes and rune ring at the caster; aura from BuffVisuals on release."),
    "unit": ("Family visual at the target unit.", "-", "School impact at the ally/target; wind-up at the caster."),
}

MONSTER_SELF_CIRCLE_FIX = "Amber disc centred on the caster with growing fill and detonation; wind-up at the caster (the old cue put a ring on the victim)."


def load(directory: Path) -> dict:
    rows = {}
    for line in (directory / "manifest.jsonl").read_text(encoding="utf-8").splitlines():
        if line.strip():
            entry = json.loads(line)
            rows[entry["id"]] = entry
    return rows


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    before, after = Path(sys.argv[1]), Path(sys.argv[2])
    compare = Path(sys.argv[3]) if len(sys.argv) > 3 else None
    old, new = load(before), load(after)
    lines = []
    for ability, entry in sorted(new.items(), key=lambda kv: kv[1]["index"]):
        prior = old.get(ability, {})
        shape, school = entry["shape"], entry["school"]
        caster = entry["caster"]
        if not entry["monster"]:
            text = CHAMPION.get(ability, ("-", "-", "-"))
            kind = f"{entry['group']} / {shape} / {school}"
        else:
            key = "projectile" if shape == "line" and ability in ("npc_shadow_bolt", "npc_barbed_shot", "npc_bolt", "npc_shot") else shape
            text = MONSTER_KIND.get(key, MONSTER_KIND["self"])
            kind = f"{entry['group']} {shape} / {school}"
        problems = text[1]
        if prior.get("refused"):
            problems += " (legacy harness run refused this cast; re-captured.)"
        if entry.get("refused"):
            problems += " **AFTER CAST REFUSED**"
        evidence = f"{entry['index']:03d}_{ability}.jpg" if compare else ""
        lines.append(f"| {entry['index'] + 1} | {entry['label']} `{ability}` | {kind} ({caster}) | {text[0]} | {problems} | {text[2]} | {evidence} |")
    header = [
        "| # | Ability | Type (caster) | Before | Problems | Fix | Evidence |",
        "|---|---|---|---|---|---|---|",
    ]
    table = "\n".join(header + lines)
    doc = ROOT / "Docs/AbilityVFXAudit.md"
    text = doc.read_text(encoding="utf-8") if doc.exists() else ""
    marker_start, marker_end = "<!-- audit-table:start -->", "<!-- audit-table:end -->"
    if marker_start in text:
        head, rest = text.split(marker_start, 1)
        tail = rest.split(marker_end, 1)[1] if marker_end in rest else ""
        text = f"{head}{marker_start}\n{table}\n{marker_end}{tail}"
    else:
        text += f"\n{marker_start}\n{table}\n{marker_end}\n"
    doc.write_text(text, encoding="utf-8")
    print(f"CIRE_ABILITY_VFX_AUDIT rows={len(lines)} -> {doc}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
