"""Validate and merge an Astra Unreal 5.8.3 ground bundle into the Cire project.

Usage: python Tools/ImportAstraAbilities.py PATH/TO/AstraAbilities.json [--check]
No Unreal process is launched. Existing unrelated recipes are preserved. A
timestamped backup is saved before replacing an existing runtime data file.
"""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parent.parent
BOUNDS = {"radius": (1, 2000), "length": (1, 2000), "width": (1, 2000),
          "coneAngleDegrees": (1, 179), "warningSeconds": (0, 10),
          "durationSeconds": (.05, 60), "tickInterval": (.05, 5),
          "damagePerSecond": (0, 10000), "burstDamage": (0, 10000), "verticalTolerance": (1, 150)}


def number(value, lower, upper):
    return type(value) in (int, float) and math.isfinite(value) and lower <= value <= upper


def cross(a, b, c):
    return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])


def on(a, b, p):
    return abs(cross(a, b, p)) < .01 and sum((p[i]-a[i])*(p[i]-b[i]) for i in range(2)) <= .01


def intersects(a, b, c, d):
    return ((cross(a, b, c)*cross(a, b, d) < 0 and cross(c, d, a)*cross(c, d, b) < 0)
            or on(a, b, c) or on(a, b, d) or on(c, d, a) or on(c, d, b))


def validate(bundle):
    expected = {"schemaVersion": 1, "engine": "Unreal", "engineVersion": "5.8.3",
                "profile": "CireGroundAreas", "units": "centimeters"}
    if not isinstance(bundle, dict) or any(bundle.get(k) != v for k, v in expected.items()):
        raise ValueError("Expected CireGroundAreas schema 1, Unreal 5.8.3, centimetres")
    abilities = bundle.get("abilities")
    if not isinstance(abilities, list) or not 1 <= len(abilities) <= 128:
        raise ValueError("Bundle must contain 1..128 abilities")
    ids = set()
    for ability in abilities:
        identity = ability.get("id", "")
        if not isinstance(identity, str) or not 1 <= len(identity) <= 80 or any(not (c.isascii() and (c.isalnum() or c in "_-")) for c in identity) or identity in ids:
            raise ValueError("Ability IDs must be unique simple identifiers")
        ids.add(identity)
        if not isinstance(ability.get("name"), str) or len(ability["name"]) > 80:
            raise ValueError("Invalid ability name")
        for key, maximum in {"manaCost": 10000, "energyCost": 100, "cooldownSeconds": 300, "castRange": 3000}.items():
            if not number(ability.get(key), 0, maximum):
                raise ValueError(f"Invalid {identity} cast {key}")
        if ability.get("targeting") != "ground":
            raise ValueError("Only ground targeting is supported")
        area = ability.get("area")
        if not isinstance(area, dict) or area.get("shape") not in ("circle", "cone", "line", "square", "custom"):
            raise ValueError("Invalid ground area")
        for key, limits in BOUNDS.items():
            if not number(area.get(key), *limits):
                raise ValueError(f"Invalid {identity} area {key}")
        if type(area.get("persistent")) is not bool or type(area.get("poison")) is not bool:
            raise ValueError("Persistent and poison must be booleans")
        if not isinstance(area.get("abilityName"), str) or len(area["abilityName"]) > 80:
            raise ValueError("Invalid area name")
        color = area.get("color")
        if not isinstance(color, list) or len(color) != 4 or any(not number(v, .03 if i == 3 else 0, 1 if i == 3 else 8) for i, v in enumerate(color)):
            raise ValueError("Invalid linear RGBA color")
        polygon = area.get("customPolygon")
        if not isinstance(polygon, list) or len(polygon) > 32 or any(not isinstance(p, list) or len(p) != 2 or any(not number(v, -2000, 2000) for v in p) or math.hypot(*p) > 2000 for p in polygon):
            raise ValueError("Invalid custom polygon")
        if area["shape"] == "custom":
            if len(polygon) < 3:
                raise ValueError("Custom polygon needs three vertices")
            signed_area = 0
            for i, a in enumerate(polygon):
                b = polygon[(i+1) % len(polygon)]
                if math.dist(a, b) < 1:
                    raise ValueError("Custom polygon edge too short")
                signed_area += a[0]*b[1]-a[1]*b[0]
                for j in range(i+1, len(polygon)):
                    if j == i+1 or (i == 0 and j == len(polygon)-1):
                        continue
                    if intersects(a, b, polygon[j], polygon[(j+1) % len(polygon)]):
                        raise ValueError("Custom polygon edges intersect")
            if abs(signed_area) < 2:
                raise ValueError("Degenerate custom polygon")
    return bundle


def load(path):
    if path.stat().st_size > 2*1024*1024:
        raise ValueError("Gameplay bundle exceeds 2 MB; keep cosmetic timelines separate")
    return validate(json.loads(path.read_text(encoding="utf-8-sig")))


def install(source, destination, check=False):
    incoming = load(source)
    result = incoming
    if destination.exists():
        previous = load(destination)
        replacements = {a["id"] for a in incoming["abilities"]}
        result = {**previous, "abilities": [a for a in previous["abilities"] if a["id"] not in replacements] + incoming["abilities"]}
        validate(result)
    if not check:
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.exists():
            stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S-%f")
            shutil.copy2(destination, destination.with_suffix(".backup-"+stamp+".json"))
        temporary = destination.with_suffix(".json.tmp")
        temporary.write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
        temporary.replace(destination)
    return {"status": "validated" if check else "imported", "abilities": len(result["abilities"]), "output": str(destination)}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT/"Content/Data/AstraAbilities.json")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    print(json.dumps(install(args.bundle, args.output, args.check)))
