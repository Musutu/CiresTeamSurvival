#pragma once
// pack-usage: offscreen catalogue of every Niagara / Cascade system in the locally installed Fab VFX packs.
//
//   UnrealEditor-Cmd <project> /Game/Maps/Citadel -game -CireFabVFXCatalog=<list.json> -CireFabVFXCatalogOut=<dir> ...
//
// list.json: {"systems": [{"path": "/Game/<Pack>/.../NS_X.NS_X", "label": "..."}, ...]}. Each system is spawned alone on
// a neutral stage and captured twice (an early frame for bursts, a late frame for loops); the measured XY reach and the
// exposed colour parameters are logged per item (CIRE_FAB_CATALOG_ITEM) so Tools/RunFabVFXCatalog.py can compose rated
// contact sheets and Art/Fab/FabVFXInventory.json can carry footprint data. Ends with CIRE_FAB_CATALOG_PASS / _FAIL.
class ACireGameMode;
namespace CireFabVFXCatalog
{
#if !UE_BUILD_SHIPPING
    bool Initialize(ACireGameMode* Mode);
    bool Tick(ACireGameMode* Mode);
#endif
}
