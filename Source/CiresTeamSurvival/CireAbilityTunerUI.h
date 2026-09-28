#pragma once
// ability-tuner: the in-game Ability Tuner window (F7 by default, "ToggleAbilityTuner"). WoW-style, CireUIStyle themed.
// Search the whole ability pool, edit every number of the row with sliders + typed values, rename, rewrite the
// tooltip text, disable, scale / tint the VFX, and watch the tooltip preview update live. The host's edits apply to
// the running match and replicate; other players get a read-only view. Ships in the release build.
#include "CoreMinimal.h"

class ACireHUD;
class ACireHero;
class ACireController;

namespace CireAbilityTunerUI
{
    CIRESTEAMSURVIVAL_API void Toggle(ACireController* Controller);
    CIRESTEAMSURVIVAL_API bool IsOpen();
    CIRESTEAMSURVIVAL_API void Close();
    /** Logical HUD point inside the window (pointer-over-interface). */
    CIRESTEAMSURVIVAL_API bool ContainsPoint(FVector2D Logical);
    /** A text box has focus: the window owns the keyboard (the controller skips gameplay keys). */
    CIRESTEAMSURVIVAL_API bool OwnsKeyboard();
    /** Typed character (UCireViewportClient::InputChar); true when consumed. */
    CIRESTEAMSURVIVAL_API bool HandleChar(TCHAR Character);
    /** Backspace / Enter / Tab / Escape while a text box has focus. */
    CIRESTEAMSURVIVAL_API void TickKeys(ACireController* Controller);
    /** Escape: drops text focus, else closes the window. True when consumed. */
    CIRESTEAMSURVIVAL_API bool HandleEscape();
    CIRESTEAMSURVIVAL_API void Draw(ACireHUD& HUD, ACireHero* Hero, ACireController* Controller);
    /** Tests / galleries: open on an ability. */
    CIRESTEAMSURVIVAL_API void Select(const FString& AbilityId);
    CIRESTEAMSURVIVAL_API FString Selected();
    /** Rows drawn in the ability list last frame (tests). */
    CIRESTEAMSURVIVAL_API int32 LastListRows();
}
