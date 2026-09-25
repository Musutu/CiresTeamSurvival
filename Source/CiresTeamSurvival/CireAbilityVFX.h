#pragma once
// ability-vfx: ground telegraph painter and ability-presentation helpers shared by the spell renderer
// (ACireSpellVisual), the cursor aim preview (CireTargeting) and the native tests.
//
// Every telegraph is painted from the ability's true hit boundary (ACireAreaEffect::BoundaryPoints of
// the gameplay spec or CireAbilityShapes), in the shape's local frame (+X = aim direction; lines and
// cones start at the caster). Decorations (arrowhead, chevrons, centre mark) are drawn INSIDE that
// boundary so the indicator never over-promises the hit area.
#include "CoreMinimal.h"
#include "CireAreaEffects.h"
#include "CireAbilityShapes.h"

struct FCireGroundMesh;
class ACireGameMode;
class UWorld;

namespace CireAbilityVFX
{
    enum class ETone : uint8 { Hostile, Friendly, AimValid, AimInvalid };
    struct FStyle { FLinearColor Fill, Edge, Progress, Accent; };
    CIRESTEAMSURVIVAL_API FStyle StyleFor(ETone Tone, FLinearColor School);

    enum EPaint : uint32
    {
        PaintNone = 0,
        PaintArrow = 1 << 0,      // lines: arrowhead at the far end + travelling chevrons
        PaintCenter = 1 << 1,     // circles/squares: designated-spot marker at the centre
        PaintProgress = 1 << 2,   // warning fill that grows until the hit resolves
        PaintPulse = 1 << 3,      // breathing border
        PaintNoFill = 1 << 4,     // decorations only (the caller already draws the base fill)
    };
    struct FPaintResult
    {
        FBox2D FillBounds = FBox2D(ForceInit);   // exact footprint (no feather)
        FVector2D ArrowTip = FVector2D::ZeroVector;
        int32 Chevrons = 0;
        int32 Vertices = 0;
    };
    // Paints a warning / preview telegraph for the footprint. Progress in [0,1] (negative: none),
    // Time drives animation, Alpha fades the whole telegraph.
    CIRESTEAMSURVIVAL_API FPaintResult PaintTelegraph(FCireGroundMesh& Ground, const FCireAreaSpec& Spec, const FStyle& Style,
        float Progress, float Time, float Alpha, uint32 Flags);
    // Paints an active zone (persistent pool) or a detonation flash (Burst in [0,1], 1 = the release frame).
    CIRESTEAMSURVIVAL_API FPaintResult PaintActive(FCireGroundMesh& Ground, const FCireAreaSpec& Spec, FLinearColor Color,
        float Time, float Alpha, float Burst, bool bPersistent);
    // Expanding ground shockwave to Radius (U in [0,1] over the effect life).
    CIRESTEAMSURVIVAL_API void PaintShock(FCireGroundMesh& Ground, FVector2D Center, float Radius, FLinearColor Color, float U, float Alpha);

    // Seconds between a champion's accepted cast and the moment its effect is released, which is where
    // the cast/slash clip's contact frame lands (CireChampionActions) and where the cast cue appears.
    // Skillshots release after their authored warning; everything else resolves on the cast frame and
    // uses a short snap-in so the clip's contact coincides with the effect.
    CIRESTEAMSURVIVAL_API float ReleaseLead(const UWorld* World, const FString& SkillId);
    constexpr float InstantLead = .12f;
    // cire.AbilityVFX 0 (or -CireLegacyVFX) restores the previous presentation for A/B comparison.
    CIRESTEAMSURVIVAL_API bool Enabled();

    // Local impact camera kick (UISettings.bImpactCameraShake); only near the local champion.
    CIRESTEAMSURVIVAL_API void ImpactShake(UWorld* World, FVector At, float Strength);

#if !UE_BUILD_SHIPPING
    // Native suite: shape parity for every ability, line indicator dimensions, telegraph lifecycles,
    // cast/animation release sync, budgets. Logs CIRE_ABILITY_VFX_TESTS_PASS/FAIL.
    CIRESTEAMSURVIVAL_API bool RunTests(ACireGameMode* Mode);
#endif
}
