#pragma once
// layout-wiring: the map layout at match time (Docs/MapLayout.md "What the game reads", "Apply, Test This Layout").
//  - Play Bounds: the third-person camera stays inside the polygon; a hero outside it is warned, then set back inside.
//  - Apply -> live: MapLayout.json is the startup document of every match (CireLanePath::LoadActive). A running match
//    re-reads it with "cire.Layout restart" or Alt+F5 (host): routes, spots, packs, vendors, waves and heroes restart.
//  - TEST THIS LAYOUT (the editor): Apply, then launch a real match on the layout in a new window.
#include "CoreMinimal.h"

class ACireHero;
class ACireGameMode;
class APlayerController;
class UWorld;

namespace CireLayoutRuntime
{
    /** Server hero tick: the out-of-bounds flag (Play Bounds). */
    CIRESTEAMSURVIVAL_API void TickHero(ACireHero* Hero, float DeltaSeconds);
    /** Longest boom (<= Boom) that keeps the camera inside the play bounds; Boom when there is no polygon. */
    CIRESTEAMSURVIVAL_API float ClampBoom(const ACireHero* Hero, const FVector& Pivot, const FRotator& Rotation, float Boom);
    /** Local merchants re-read TownVendors.json and stand at their new spots. */
    CIRESTEAMSURVIVAL_API int32 RespawnVendors(UWorld* World);
    /** Host: re-read MapLayout.json and restart the match on it (no rebuild, no map reload). */
    CIRESTEAMSURVIVAL_API bool RestartOnLayout(ACireGameMode* Mode, FString& Out);
    /** Alt+F5 from a player controller (host or standalone only). */
    CIRESTEAMSURVIVAL_API void RequestRestart(APlayerController* Controller);
    /** Launch a real match on the applied layout in a new window (-CireLayoutTest). */
    CIRESTEAMSURVIVAL_API bool LaunchTestMatch(FString& Out);
    /** -CireLayoutTest: the match announces the layout it runs. */
    CIRESTEAMSURVIVAL_API bool IsLayoutTest();
}
