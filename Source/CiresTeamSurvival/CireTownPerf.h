#pragma once
// town-perf: -CireTown -CireTownPerfProbe, the Medieval Kingdom town's frame-rate and load-time probe (Docs/CastleTown.md).
//
// Standalone -game run of the real map (both realms streamed, the runtime navmesh built):
//   1. load-to-playable: seconds from process start until the realms are visible, the navmesh is ready and the local
//      champion stands in its realm (CIRE_TOWN_PERF_LOAD);
//   2. a real fight in each realm: 10 bot champions and 24 monsters per realm (48 alive, topped up), viewed from the
//      local champion first in DAYLIGHT (team 0), then moved to DARKNIGHT (team 1); 5 s warm-up, 30 s measured each;
//   3. frame stats per realm: fps avg, frame ms avg / p95 / max, game / render / RHI thread ms and GPU ms
//      (CIRE_TOWN_PERF_REALM), then CIRE_TOWN_PERF_PASS / _FAIL against the targets:
//      avg >= 60 fps and p95 <= 33.3 ms (>= 30 fps worst case) in both realms, playable <= 60 s.
// Summary: Saved/TownPerf/<stamp>/perf.txt plus one screenshot per realm. -CireTownPerfProfileGPU also dumps a
// ProfileGPU pass breakdown into the log in each realm.
#include "CoreMinimal.h"

class UWorld;

namespace CireTownPerf
{
    /** Server/standalone: arm the probe when -CireTownPerfProbe is on the command line (called from ACireWorld). */
    CIRESTEAMSURVIVAL_API void Initialize(UWorld* World);
    CIRESTEAMSURVIVAL_API bool IsEnabled();
}
