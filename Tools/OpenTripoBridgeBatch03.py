"""Open the vendor Tripo Bridge panel and keep saving its imports until a stop file appears.

UnrealEditor.exe <project> -ExecCmds="py Tools/OpenTripoBridgeBatch03.py"
Unlike OpenTripoBridge.py this watcher has no one-hour limit (batch 03 lost unsaved material instances when that
limit expired mid-transfer) and saves every dirty package under /Game/TripoModels, including material instances.
Create Saved/TripoBridgeBatch03Stop.request to save and exit cleanly. The map is never saved.
"""
from pathlib import Path
import time
import unreal

project = Path(unreal.Paths.project_dir()).resolve()
stop_file = project / "Saved" / "TripoBridgeBatch03Stop.request"
subsystem = unreal.get_editor_subsystem(unreal.EditorUtilitySubsystem)
subsystem.spawn_registered_tab_by_id("TripoWebSocketTab")
unreal.log("CIRE_TRIPO_BRIDGE_OPEN: batch03 watcher active")
state = {"last": 0.0, "paths": set(), "since": None, "handle": None}


def save_dirty():
    return unreal.EditorAssetLibrary.save_directory("/Game/TripoModels", only_if_is_dirty=True, recursive=True)


def tick(_delta):
    now = time.monotonic()
    if now - state["last"] < 3:
        return
    state["last"] = now
    if stop_file.exists():
        save_dirty()
        unreal.unregister_slate_post_tick_callback(state["handle"])
        stop_file.unlink()
        unreal.log("CIRE_TRIPO_BRIDGE_CLEAN_EXIT")
        unreal.SystemLibrary.quit_editor()
        return
    assets = unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(
        "/Game/TripoModels", recursive=True, include_only_on_disk_assets=False)
    paths = {str(a.package_name) for a in assets}
    if paths != state["paths"]:
        state["paths"], state["since"] = paths, now
        return
    if state["since"] is not None and now - state["since"] > 8:
        unreal.log("CIRE_TRIPO_BRIDGE_SAVED success=%s assets=%d" % (save_dirty(), len(paths)))
        state["since"] = None


state["handle"] = unreal.register_slate_post_tick_callback(tick)
