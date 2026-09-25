#pragma once
#include "CoreMinimal.h"
class ACireController;

/**
 * Development-only simulated play session (-CirePlaySession, Tools/RunPlaySession.py).
 * Drives real inputs through PlayerInput (keys, mouse buttons, raw mouse deltas) on the local
 * controller of a standalone match and checks the WoW casting/camera rules end to end:
 * smart cast, target retention while strafing/steering, ground aim through RMB steering and
 * movement, clean-click confirm/cancel, stop-to-cast, "Can't cast while moving", moving cancels a
 * cast, and summons with no target. Exits the process with 0 (pass) or 1 (fail).
 */
namespace CirePlaySession
{
#if !UE_BUILD_SHIPPING
    bool IsActive();
    /** Called every PlayerTick; injects this frame's inputs (processed by the next frame). */
    void Tick(ACireController* Controller, float DeltaSeconds);
#else
    inline bool IsActive() { return false; }
    inline void Tick(ACireController*, float) {}
#endif
}
