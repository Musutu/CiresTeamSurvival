"""Open the installed vendor Bridge through Unreal's editor API.

Run with the editor's -ExecCmds="py <this file>" option. This does not enable
remote Python execution, modify browser settings, or initiate any transfer.
After each stable import batch, save the imported Tripo folder and run the
read-only inspection. The bounded callback stops after one hour.
"""
from pathlib import Path
import time
import unreal

project = Path(unreal.Paths.project_dir()).resolve()
subsystem = unreal.get_editor_subsystem(unreal.EditorUtilitySubsystem)
# The API's return value is false for non-utility nomad tabs even when opened.
subsystem.spawn_registered_tab_by_id("TripoWebSocketTab")
if not subsystem.does_tab_exist("TripoWebSocketTab"):
    raise RuntimeError("The installed Tripo Bridge tab did not open")
unreal.log("CIRE_TRIPO_BRIDGE_OPEN: vendor panel active; awaiting Studio transfer")

started = time.monotonic()
last_check = 0.0
candidate_at = None
last_paths = set()
saved_paths = set()
handle = None


def after_transfer(delta_seconds):
    global last_check, candidate_at, last_paths, saved_paths, handle
    now = time.monotonic()
    if now - last_check < 2:
        return
    last_check = now
    if now - started > 3600:
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.log_warning("CIRE_TRIPO_BRIDGE_WATCH_TIMEOUT: panel remains open")
        return
    assets = unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(
        "/Game/TripoModels", recursive=True, include_only_on_disk_assets=False)
    mesh_assets = [a for a in assets if str(a.asset_class_path.asset_name) in {"SkeletalMesh", "StaticMesh"}]
    paths = {str(a.package_name) for a in assets}
    if not mesh_assets:
        return
    if paths == saved_paths:
        return
    if paths != last_paths:
        candidate_at, last_paths = now, paths
        return
    if candidate_at is None or now - candidate_at < 10:
        return
    saved = unreal.EditorAssetLibrary.save_directory("/Game/TripoModels", only_if_is_dirty=True, recursive=True)
    unreal.log("CIRE_TRIPO_BRIDGE_SAVED: meshes=%d assets=%d success=%s" % (len(mesh_assets), len(assets), saved))
    if saved:
        saved_paths = paths
    else:
        candidate_at = now
    inspector = project / "Tools" / "InspectTripoAssets.py"
    exec(compile(inspector.read_text(encoding="utf-8"), str(inspector), "exec"), {"__file__": str(inspector), "__name__": "__main__"})


handle = unreal.register_slate_post_tick_callback(after_transfer)
