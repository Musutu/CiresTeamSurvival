"""Keep Bridge's overlapping origin actors as editor references, outside gameplay.

Backs up the map first, touches only the three exact imported mesh actors at
world origin, and does not modify the meshes or their animation assets.
"""
import json
from pathlib import Path
import shutil

import unreal

root = Path(unreal.Paths.project_dir()).resolve()
names = {"medieval_knight_armor_3d_model", "armored_archer_3d_model", "battlefield_healer_3d_model"}
expected = {f"/Game/TripoModels/{name}/{name}.{name}" for name in names}
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if world and unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
    raise RuntimeError("Save editor map changes before this script.")
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level("/Game/Maps/Citadel"):
    raise RuntimeError("Could not load Citadel.")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
matches = []
seen = set()
for actor in actors:
    meshes = [c.get_skeletal_mesh_asset() for c in actor.get_components_by_class(unreal.SkeletalMeshComponent)]
    paths = {m.get_path_name() for m in meshes if m}
    if not paths.intersection(expected):
        continue
    if paths - expected or actor.get_class().get_name() != "Actor" or actor.get_actor_location().length() > 0.01:
        raise RuntimeError(f"Unexpected gameplay/positioned Tripo actor: {actor.get_path_name()}")
    actor.get_editor_property("is_editor_only_actor")  # Validate API before changing anything.
    seen.update(paths)
    matches.append(actor)
if len(matches) != 3 or seen != expected:
    raise RuntimeError(f"Expected the exact three Bridge staging actors, got {len(matches)}.")
backup = root / "Art/Imports/BridgeOriginalMap/Citadel.umap"
backup.parent.mkdir(parents=True, exist_ok=True)
if not backup.exists():
    shutil.copy2(root / "Content/Maps/Citadel.umap", backup)
report = []
for actor in matches:
    actor.set_editor_property("is_editor_only_actor", True)
    actor.set_actor_hidden_in_game(True)
    actor.set_actor_enable_collision(False)
    report.append({"actor": actor.get_path_name(), "label": actor.get_actor_label(), "editor_only": True, "hidden_in_game": True, "collision_enabled": False})
if not level.save_current_level():
    raise RuntimeError("Could not save staging flags in Citadel.")
(root / "Saved/TripoMapStaging.json").write_text(json.dumps({"backup": str(backup), "actors": report}, indent=2), encoding="utf-8")
unreal.log("CIRE_TRIPO_MAP_STAGING_PASS actors=3 editor_references_preserved=1")
