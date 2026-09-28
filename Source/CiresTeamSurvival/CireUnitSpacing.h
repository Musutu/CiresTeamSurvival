#pragma once
// bosses-spacing: body size and personal space of NPC units (Docs/RESUME-bosses-spacing.md).
//  - Bosses (wave/lane bosses, outdoor world bosses, boss-classified pack leaders) are drawn
//    BossSizeMultiplier x bigger. Their collision capsule is capped to the "Large" nav agent
//    (radius 72, height 300, Config/DefaultEngine.ini) so they path on the Large navmesh and fit
//    through the town's streets and gates; the huge body is visual. Their melee reach and the reach
//    of attacks against them are measured to the visual body's edge (BodyReachBonus).
//  - Every monster's capsule radius is MonsterCapsuleRadius (was 38) and melee reach gets
//    MeleeReachBonus; melee units standing in reach sidestep crowding neighbours (separation).
// Data: Content/Data/UnitSpacing.json, live: `cire.Spacing reload` / `cire.Spacing <key> <value>`.
#include "CoreMinimal.h"

class AActor;
class ACireMonster;
class ACireGameMode;

struct FCireUnitSpacing
{
    float BossSizeMultiplier = 5.f;         // x the boss's normal (archetype / rank / wave) size
    float BossCapsuleRadiusMax = 72.f;      // scaled cm: the Large nav agent radius
    float BossCapsuleHalfHeightMax = 150.f; // scaled cm: the Large nav agent height / 2 (fits under gates)
    float MonsterCapsuleRadius = 46.f;      // unscaled cm (legacy 38, the mannequin proportion)
    float MeleeReachBonus = 20.f;           // cm added to every melee attackRange (legacy 0)
    float SeparationPadding = 30.f;         // cm of personal space between melee units in reach
    float SeparationStrength = .7f;         // 0..1 movement input used to sidestep
    bool bSeparation = true;
    /** The pre-bosses-spacing values (the probe's "before"). */
    static FCireUnitSpacing Legacy()
    {
        FCireUnitSpacing S; S.BossSizeMultiplier = 1.f; S.MonsterCapsuleRadius = 38.f; S.MeleeReachBonus = 0.f; S.bSeparation = false;
        S.BossCapsuleRadiusMax = 1000.f; S.BossCapsuleHalfHeightMax = 10000.f; return S;
    }
};

namespace CireUnitSpacing
{
    /** Unscaled radius the mannequin-proportioned bodies were built around: the body footprint is this x actor scale. */
    inline constexpr float BodyRadius = 38.f;
    inline constexpr float BaseHalfHeight = 88.f;

    CIRESTEAMSURVIVAL_API const FCireUnitSpacing& Get();
    CIRESTEAMSURVIVAL_API void Set(const FCireUnitSpacing& Spacing);
    CIRESTEAMSURVIVAL_API bool Reload(FString* Error = nullptr);
    CIRESTEAMSURVIVAL_API bool Parse(const FString& Json, FCireUnitSpacing& Out, FString& Error);

    /** Wave/lane bosses, outdoor world bosses and boss-classified pack leaders. */
    CIRESTEAMSURVIVAL_API bool IsBossBody(const ACireMonster* Monster);
    /** Sets the actor scale (Scale already includes the boss multiplier) and the capsule for it; moves the
     *  body so its feet stay on the capsule bottom. Cheap no-op when nothing changed. Runs on every machine. */
    CIRESTEAMSURVIVAL_API void ApplyBody(ACireMonster* Monster, float Scale);
    /** Footprint radius of the drawn body (BodyRadius x actor scale). */
    CIRESTEAMSURVIVAL_API float VisualRadius(const ACireMonster* Monster);
    /** How far the drawn body reaches past the collision capsule (0 for normal units). */
    CIRESTEAMSURVIVAL_API float BodyReachBonus(const AActor* Actor);
    /** Melee reach of a monster: authored attackRange + MeleeReachBonus + its body bonus. */
    CIRESTEAMSURVIVAL_API float MeleeReach(const ACireMonster* Monster, float AttackRange);
    /** Sidestep direction away from crowding lane-mates (zero when there is room). */
    CIRESTEAMSURVIVAL_API FVector Separation(const ACireMonster* Monster, const ACireGameMode* Mode);
    /** Height above the actor for its nameplate: over the drawn head. */
    CIRESTEAMSURVIVAL_API float PlateLift(const ACireMonster* Monster);
    /** Crowd metrics of a set of units (probe): pairs whose drawn footprints overlap, and mean nearest-neighbour gap. */
    struct FCrowd { int32 Units = 0, OverlapPairs = 0; float MeanNearest = 0.f, MeanOverlapDepth = 0.f; };
    CIRESTEAMSURVIVAL_API FCrowd Measure(const TArray<ACireMonster*>& Units);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
    /** -CireBossSpacingProbe (CireBossSpacingProbe.cpp): crowd overlap before/after, giant world bosses, a 5x wave boss march, captures. */
    CIRESTEAMSURVIVAL_API void InitializeProbe(ACireGameMode* Mode);
    /** True while the probe owns the match tick (the outdoor boss probe hook then returns). */
    CIRESTEAMSURVIVAL_API bool TickProbe(ACireGameMode* Mode, float DeltaSeconds);
#endif
}
