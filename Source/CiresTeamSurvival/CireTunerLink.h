#pragma once
// kit-editor (Playtest 6 section M follow-up): the EDIT badge that opens the Ability Tuner (CireAbilityTunerUI) on one
// ability. One look and one placement everywhere (UI consistency rule): a small bevelled pencil badge in the top-right
// corner of the hovered ability card / button, tooltip "Edit in Ability Tuner (<key>)". Used by the Hero Creator cards
// and loadout buttons, the Skill Shop cards, and the action bar (tooltips cannot hold clicks there: Alt+click a slot).
// Shown only when CireAbilityTuner::CanTune passes (the host / single player while tuning is allowed; never remote
// clients, never a shipping build without the tuning flag).
#include "CoreMinimal.h"

class ACireHUD;
class APlayerController;
struct FCireUIPainter;

namespace CireTunerLink
{
    /** Badge size (logical px) and its inset from the card's top-right corner. */
    inline constexpr float Size = 22.f, Inset = 5.f;
    /** The tuner may be opened from this controller (CanTune; tests can force it). */
    CIRESTEAMSURVIVAL_API bool Available(const APlayerController* Controller);
    /** Opens the Ability Tuner on the ability when Available; false (nothing changes) otherwise. */
    CIRESTEAMSURVIVAL_API bool Open(const APlayerController* Controller, const FString& AbilityId);
    /** "Edit in Ability Tuner (F7)" with the live keybind of ToggleAbilityTuner. */
    CIRESTEAMSURVIVAL_API FString TooltipTitle(const ACireHUD& HUD);
    /** Action-bar tooltip footer hint ("Alt+click: Edit in Ability Tuner (F7)"); empty when not Available. */
    CIRESTEAMSURVIVAL_API FString ActionBarHint(const ACireHUD& HUD);
    /** Alt held (the action-bar modifier). */
    CIRESTEAMSURVIVAL_API bool ModifierDown(const APlayerController* Controller);
    /** Top-left of the badge for a card / button whose top-right corner is (Right, Top). */
    CIRESTEAMSURVIVAL_API FVector2D BadgeAt(float Right, float Top);
    CIRESTEAMSURVIVAL_API bool OverBadge(FVector2D Pointer, float Right, float Top);
    /** Just the icon (pencil on a bevelled chip). */
    CIRESTEAMSURVIVAL_API void DrawIcon(const FCireUIPainter& P, float X, float Y, bool bHover);
    /** Draws the badge on a hovered card / button (card top-right corner = Right, Top) when Available; the badge's own
     *  click opens the tuner (the click is taken) and its hover sets the tooltip. True while the pointer is over the badge
     *  (the caller skips its own click / tooltip then). */
    CIRESTEAMSURVIVAL_API bool Badge(ACireHUD& HUD, const FCireUIPainter& P, FVector2D Pointer, float Right, float Top, const FString& AbilityId, bool bInteractive = true);
    /** Tests: -1 = real CanTune, 0 = never, 1 = always. */
    CIRESTEAMSURVIVAL_API void DebugForce(int32 Mode);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunTests();
#endif
}
