#pragma once
// town-perf: a local cache of the Medieval Kingdom town's runtime navmesh (Docs/CastleTown.md "Load time").
//
// The town navmesh is generated at runtime (RuntimeGeneration=Dynamic) over both realms for both agents: ~9,000 tiles
// and about a minute of CPU on every start. The geometry only changes when the pack, the realm layout, the route/vendor
// spots or the navigation settings change, so after the first build the tiles are written to
// Saved/NavCache/CastleTown-<key>.navcache and attached directly on later starts (a second or two).
//
// The key hashes everything that shapes the navmesh: CastleTown.json, CastleTownRoutes.json, TownVendors.json,
// DefaultEngine.ini (agents, cell sizes), the pack's level files (size + timestamp) and this module's build stamp,
// so any change simply misses the cache and rebuilds. The file is derived from Fab-licensed geometry: it lives in
// Saved/ and is never committed. -CireNoNavCache forces a fresh build.
#include "CoreMinimal.h"

class UWorld;
class UNavigationSystemV1;

namespace CireNavCache
{
    /** The cache applies to this process (the town is active and -CireNoNavCache is absent). */
    bool Enabled();
    /** Before the bounds volumes are spawned: lock the build so registering them does not dirty the whole realm. */
    void BeginLoad(UNavigationSystemV1* NS);
    /** After the bounds are registered (NS->Tick): attach the cached tiles. On a miss or any mismatch the lock is released
        and false is returned; the caller then builds as usual. */
    bool FinishLoad(UWorld* World, UNavigationSystemV1* NS, double& OutMs, int32& OutTiles);
    /** After a full build: write the tiles for the next start. */
    void Save(UWorld* World, UNavigationSystemV1* NS);
}
