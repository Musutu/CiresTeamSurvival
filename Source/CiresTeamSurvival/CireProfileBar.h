#pragma once
// game-profiles: keyboard ownership of the F8 PROFILE bar's name box (SAVE AS / RENAME / SAVE CURRENT AS GAME TYPE).
#include "CoreMinimal.h"

namespace CireProfileUI
{
    /** A profile name box is being typed in (and was drawn this moment). */
    CIRESTEAMSURVIVAL_API bool OwnsKeyboard();
    /** Viewport text input while typing; true = consumed. */
    CIRESTEAMSURVIVAL_API bool HandleChar(TCHAR Ch);
    /** Escape while typing cancels the name box instead of closing Options; true = consumed. */
    CIRESTEAMSURVIVAL_API bool CancelTyping();
    /** F8 > Waves > MODES & SCALE shows the game type bundle (instead of scale & damage). */
    CIRESTEAMSURVIVAL_API bool& WaveBundleView();
    /** Review capture: open Waves > MODES & SCALE once (true once, then false). */
    CIRESTEAMSURVIVAL_API bool ConsumeForceWaveModes();
}
