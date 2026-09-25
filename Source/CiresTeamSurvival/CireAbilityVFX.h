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

    // Themed rune sets: glyph, edge treatment and animated motif per damage school. Heal is its own set
    // (green/gold radiant crosses); Buff is calm (rounded motifs, slow breathing).
    enum class ERuneSet : uint8 { Physical, Fire, Frost, Earth, Tide, Holy, Shadow, Void, Poison, Nature, Storm, Arcane, Blood, Spirit, Heal, Count };
    struct FRuneTheme
    {
        ERuneSet Set = ERuneSet::Physical;
        FLinearColor Glyph = FLinearColor::White, Edge = FLinearColor::White;
        bool bSharp = true;   // damage: pointed motifs, quick pulse; buffs/heals: calm
        float Underlay = .38f; // dark backing under each glyph (enemy warnings use more, to stand out on amber)
        bool bValid() const { return Set != ERuneSet::Count; }
    };
    CIRESTEAMSURVIVAL_API ERuneSet RuneSetFor(const FCireHitShape& Shape);
    CIRESTEAMSURVIVAL_API ERuneSet RuneSetForSchool(ECireSchool School);
    CIRESTEAMSURVIVAL_API FLinearColor RuneColor(ERuneSet Set);
    CIRESTEAMSURVIVAL_API FString RuneSetName(ERuneSet Set);
    CIRESTEAMSURVIVAL_API FRuneTheme ThemeFor(const FCireHitShape& Shape);

    struct FStyle { FLinearColor Fill, Edge, Progress, Accent; FRuneTheme Runes; bool bRunes = false; };
    // Tone style plus the ability's rune theme. Hostile keeps its amber edge/fill; the runes inside carry the school.
    CIRESTEAMSURVIVAL_API FStyle ThemedStyle(ETone Tone, const FCireHitShape& Shape);
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
        float Time, float Alpha, float Burst, bool bPersistent, const FRuneTheme* Theme = nullptr);
    struct FRuneResult { int32 Glyphs = 0, EdgeMotifs = 0, Vertices = 0; float MaxExtent = 0; };
    // School runes inside the boundary: a framed glyph band, edge motif, centre sigil. Everything stays inside.
    CIRESTEAMSURVIVAL_API FRuneResult PaintRunes(FCireGroundMesh& Ground, const FCireAreaSpec& Spec, const FRuneTheme& Theme,
        float Time, float Alpha, bool bCenterSigil = true);
    // One glyph of a set (unit size ~ Size cm, rotated by Angle).
    CIRESTEAMSURVIVAL_API void PaintGlyph(FCireGroundMesh& Ground, ERuneSet Set, FVector2D Center, float Size, float Angle, FLinearColor Color, float Time, int32 Seed, float Lift = 1.5f, float Underlay = .38f);
    // Rune ring for circular shocks/marks (glyphs riding a ring of Radius).
    CIRESTEAMSURVIVAL_API int32 PaintRuneRing(FCireGroundMesh& Ground, FVector2D Center, float Radius, const FRuneTheme& Theme, float Time, float Alpha, float GlyphSize = 0.f);

    // Void zone (teleport/portal): outer ring slows, inner circle stuns. Distinct rune rings and small slow/stun icons.
    struct FVoidResult { float Outer = 0, Inner = 0; int32 SlowIcons = 0, StunIcons = 0; FBox2D Bounds = FBox2D(ForceInit); };
    CIRESTEAMSURVIVAL_API FVoidResult PaintVoidZone(FCireGroundMesh& Ground, FVector2D Center, float Outer, float Inner, ETone Tone,
        float Time, float Alpha, bool bHeal);

    // Expanding ground shockwave to Radius (U in [0,1] over the effect life).
    CIRESTEAMSURVIVAL_API void PaintShock(FCireGroundMesh& Ground, FVector2D Center, float Radius, FLinearColor Color, float U, float Alpha, const FRuneTheme* Theme = nullptr);

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
