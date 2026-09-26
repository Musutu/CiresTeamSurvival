#pragma once
// layout-wiring: checks for the map layout -> game wiring (Docs/MapLayout.md "What the game reads").
//  RunTests: native checks (path split, compile, marker-driven spawns, realm transforms, replication, validation sync);
//            part of -CireCombatExpansionProbe, logs CIRE_LAYOUT_WIRING_PASS.
//  Probe:    -CireLayoutProbe [-CireTown] (Tools/RunLayoutProbe.py). Builds a layout with two monster spawns and three
//            paths (one detours and merges, one comes from a second spawn and merges) on the running map, applies it
//            through the real pipeline (MapLayout -> CompileRoutes -> ApplyLive), marches director waves down every path
//            in both realms until all arrive, then kites a unit past its leash in each realm and checks that it walks back
//            to its path and marches on. Logs CIRE_LAYOUT_PROBE_PASS / _FAIL and writes Saved/LayoutProbe/<stamp>/probe.txt.
#include "CoreMinimal.h"

class ACireGameMode;

namespace CireLayoutWiring
{
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunTests(ACireGameMode* Mode);
    CIRESTEAMSURVIVAL_API void InitializeProbe(ACireGameMode* Mode);
    /** True while the probe owns the match loop. */
    CIRESTEAMSURVIVAL_API bool TickProbe(ACireGameMode* Mode, float DeltaSeconds);
#endif
}
