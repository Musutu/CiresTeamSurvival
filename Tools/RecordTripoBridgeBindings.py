"""Record observed Bridge UUID/folder receipts; never imports or edits Unreal assets."""
import json
import re
from datetime import datetime, timezone
from pathlib import Path
import IntegrateTripoBatch as integration

PROJECT = Path(__file__).resolve().parent.parent
log = PROJECT / "Saved/Logs/TripoBatchBridge02.log"
inspection = integration.read_json(PROJECT / "Saved/TripoBridgeInspection.json")
catalog = integration.catalog(PROJECT)
assets = inspection.get("assets", [])
unsaved = set(inspection.get("unsaved_tripo_packages", []))
pattern = re.compile(r"\[([^\]]+)\].*Starting FBX import: .*?/Tripo3D/([0-9a-f-]{36})/[^\r\n]+? -> (/Game/TripoModels/[^\r\n]+)")
receipts = []
bindings = []
seen = set()
saved_seen = set()
heights = {"lancer": 182, "summoner": 176, "paladin_holy": 184, "dwarf_miner": 132,
           "orc_chieftain": 195, "wizard": 178, "keeper_of_light": 178, "drakish_footman": 185,
           "ether_golem_tank": 245, "ether_golem_support": 235, "ether_golem_bruiser": 240,
           "totemic_behemoth": 285, "troll_berserker_melee": 210, "dryad": 180}
log_text = log.read_text(encoding="utf-8-sig", errors="replace")
matches = list(pattern.finditer(log_text))
for receipt_index, match in enumerate(matches):
    stamp, identifier, folder = match.groups()
    folder = folder.strip()
    source = catalog.get(identifier)
    candidates = [a for a in assets if a.get("class") in ("SkeletalMesh", "StaticMesh") and a.get("asset", "").startswith(folder + "/")]
    receipt = {"uuid": identifier, "importFolder": folder, "observedAt": stamp,
               "provenance": "Saved/Logs/TripoBatchBridge02.log exact Starting FBX import UUID and folder",
               "inspectionUtc": inspection.get("created_utc"), "status": "awaiting_stable_saved_inspection"}
    if source:
        receipt["profileIds"] = source["profileIds"]
    end = matches[receipt_index + 1].start() if receipt_index + 1 < len(matches) else len(log_text)
    block = log_text[match.end():end]
    if not candidates and "FBX import failed - no objects imported" in block:
        receipt["status"] = "import_failed_no_objects_duplicate_source_expired" if identifier in saved_seen else "import_failed_no_objects"
        receipt["failureEvidence"] = "Bridge log reports no objects imported; source temp directory was removed by the first simultaneous import. Original saved receipt remains available."
    if len(candidates) == 1:
        asset = candidates[0]
        mesh = asset["asset"]
        package = mesh.split(".")[0]
        file = PROJECT / "Content" / (package[len("/Game/"):] + ".uasset")
        dirty = any(p.startswith(folder + "/") for p in unsaved)
        receipt.update(mesh=mesh, meshClass=asset["class"], boneCount=asset.get("rig", {}).get("bone_count"),
                       savedPackagePresent=file.is_file(), unsavedPackagesInFolder=dirty)
        if file.is_file() and not dirty:
            receipt["status"] = "saved_import_verified_animation_pending"
            if source and source["rigKind"] == "humanoid" and not source["protectedOriginal"] and asset["class"] == "SkeletalMesh":
                if identifier in seen:
                    receipt["status"] = "duplicate_saved_import_preserved"
                else:
                    profiles = source["profileIds"]
                    pose = "Lancer" if "lancer" in profiles else "Scholar" if any(p in profiles for p in ("summoner", "wizard", "keeper_of_light", "ether_golem_support", "dryad")) else "Warden"
                    bindings.append({"uuid": identifier, "importFolder": folder, "mesh": mesh,
                                     "profileIds": profiles, "attackPose": pose,
                                     "heightCm": heights.get(profiles[0], 180),
                                     "provenance": receipt["provenance"], "visualReviewAccepted": False})
                    seen.add(identifier)
            elif source and source["rigKind"] == "custom":
                receipt["status"] = "saved_import_custom_creature_pipeline_pending"
            elif not source:
                receipt["status"] = "saved_import_unmapped_source_uuid"
            if identifier in saved_seen and source and source["rigKind"] == "custom":
                receipt["status"] = "duplicate_saved_custom_import_preserved"
            saved_seen.add(identifier)
    receipts.append(receipt)
path = PROJECT / "Art/TripoImportBindings.json"
document = {"schemaVersion": 1, "batch": "Batch01", "updatedUtc": datetime.now(timezone.utc).isoformat(),
            "status": "observed_saved_imports_only_engine_preflight_and_visual_validation_pending",
            "bindings": bindings, "receipts": receipts,
            "notes": "First saved import per UUID is canonical; duplicates preserved. Bindings do not publish runtime art. Custom creatures and static meshes are receipts only. No asset paths guessed from display names."}
integration.atomic_json(path, document)
print(json.dumps({"bindings": len(bindings), "receipts": len(receipts), "profiles": [b["profileIds"] for b in bindings],
                  "pending": [r["uuid"] for r in receipts if r["status"] == "awaiting_stable_saved_inspection"]}))
