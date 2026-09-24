#pragma once
// Shared ability icon lookup (draft screen, skill-offer cards, action bars, tooltips).
// Painted icons: /Game/UI/Abilities/T_<id> (Tools/RunAbilityIcons.py), the same
// convention as CireUIStyle::FindAbilityIcon. School + accent colour per id come
// from Content/Data/AbilityIcons.json. Every draft-pool skill and every planned
// roster skill has an icon; unknown ids fall back to the procedural sigil.
#include "CoreMinimal.h"

class UTexture2D;
struct FCireUIPainter;

namespace CireAbilityIcons
{
    /** Painted icon texture, or null (use Draw for the automatic sigil fallback). */
    CIRESTEAMSURVIVAL_API UTexture2D* Texture(const FString& Id);
    /** Magic school / theme ("fire", "holy", "frost"...), empty when unknown. */
    CIRESTEAMSURVIVAL_API FString School(const FString& Id);
    /** School glow colour, for card trims, tints and sigil fallbacks. Gold when unknown. */
    CIRESTEAMSURVIVAL_API FLinearColor Accent(const FString& Id);
    /** Draws the painted icon (or a tinted sigil) filling the square. */
    CIRESTEAMSURVIVAL_API void Draw(const FCireUIPainter& P, const FString& Id, float X, float Y, float Size, float Alpha = 1.f);
    /** Number of ids with a school entry (validation / tests). */
    CIRESTEAMSURVIVAL_API int32 KnownCount();
}
