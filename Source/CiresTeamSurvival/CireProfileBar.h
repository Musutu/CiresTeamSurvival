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
}
