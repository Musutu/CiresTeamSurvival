#pragma once
// dev-route-tools: the clean MAP LAYOUT EDIT MODE (-CireRouteEdit; RouteEditor.cmd). A launch mode, not a toggle on a
// live match: every game system stays dormant (no waves, spawns or monsters, no draft, shop, economy, prep timer,
// threat, combat HUD or win/lose). What remains is the environment, the author's champion (free to walk and run the
// town in both realms) and the map layout editor (CireLayoutEditorHUD.cpp). Game code checks CireRouteEditMode::IsActive().
// -CireLayoutGallery runs the editor through a scripted authoring session and captures it (Tools/RunLayoutGallery.py).
#include "CoreMinimal.h"

class ACireGameMode;
class ACireHero;

namespace CireRouteEditMode
{
    /** The process runs the map layout edit mode (-CireRouteEdit or -CireLayoutGallery); development builds only. */
    CIRESTEAMSURVIVAL_API bool IsActive();
    /** Server BeginPlay: true when the match must not start (no packs, waves, bots or draft timers). */
    CIRESTEAMSURVIVAL_API bool InitializeServer(ACireGameMode* Mode);
    /** Server tick: keeps the author's champion drafted, safe and alone and the editor open. Returns true while active:
     *  the match loop (clock, waves, bots, shop, win/lose) never runs. */
    CIRESTEAMSURVIVAL_API bool TickServer(ACireGameMode* Mode, float DeltaSeconds);
    /** Move the champion to the same realm-local spot in the other realm (both realms share one layout). */
    CIRESTEAMSURVIVAL_API bool SwitchRealm(ACireHero* Hero);
    /** Put the champion at a realm-local spot of a realm (the list panel's "fly there" in walk view). */
    CIRESTEAMSURVIVAL_API bool TeleportTo(ACireHero* Hero, int32 Realm, const FVector2D& Local, float Yaw);
}
#if !UE_BUILD_SHIPPING
namespace CireLayoutGallery
{
    /** -CireLayoutGallery: the scripted, captured authoring session (CireLayoutGallery.cpp). */
    bool Tick(ACireGameMode* Mode);
}
#endif
