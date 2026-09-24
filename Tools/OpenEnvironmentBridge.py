"""Open vendor transfer UI; a task-specific stop file requests clean editor exit."""
from pathlib import Path
import unreal

project = Path(unreal.Paths.project_dir()).resolve()
bridge = project / 'Tools/OpenTripoBridge.py'
exec(compile(bridge.read_text(encoding='utf-8'), str(bridge), 'exec'),
     {'__file__': str(bridge), '__name__': '__main__'})
stop_file = project / 'Saved/EnvironmentBridgeStop.request'
exit_handle = None

def finish_environment_transfer(delta):
    if not stop_file.exists():
        return
    if not unreal.EditorAssetLibrary.save_directory('/Game/TripoModels', only_if_is_dirty=True, recursive=True):
        unreal.log_warning('CIRE_ENVIRONMENT_BRIDGE_EXIT_WAIT: asset save failed')
        return
    inspector = project / 'Tools/InspectTripoAssets.py'
    exec(compile(inspector.read_text(encoding='utf-8'), str(inspector), 'exec'),
         {'__file__': str(inspector), '__name__': '__main__'})
    unreal.unregister_slate_post_tick_callback(exit_handle)
    unreal.log('CIRE_ENVIRONMENT_BRIDGE_CLEAN_EXIT: imported packages saved; staging map not published')
    unreal.SystemLibrary.quit_editor()

exit_handle = unreal.register_slate_post_tick_callback(finish_environment_transfer)
