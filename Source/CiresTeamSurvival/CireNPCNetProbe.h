#pragma once
#include "CoreMinimal.h"
class ACireGameMode;
// Dedicated server (-CireNPCNetServer) + remote client (-CireNPCNetClient) check that
// NPC role/classification/cast/threat/aggro state replicates to clients for the UI.
// Run via Tools/RunNPCChecks.py --only network.
namespace CireNPCNetProbe
{
#if !UE_BUILD_SHIPPING
    void InitializeServer(ACireGameMode* Mode);
    bool TickServer(ACireGameMode* Mode);
#endif
}
