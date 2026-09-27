#pragma once
// ability-vfx: the true hit shape of every ability (champion skills, basic attacks, monster race
// abilities), read from the same data the gameplay code uses. Aim previews, enemy telegraphs,
// cast/impact presentation and the native shape-parity tests all read this one descriptor, so a
// telegraph can never silently drift from what actually hits. Presentation only: gameplay still
// validates and resolves hits with its own authoritative code.
#include "CoreMinimal.h"
#include "CireAreaEffects.h"

struct FCireNPCArchetype;
struct FCireNPCAbility;


enum class ECireHitShape : uint8
{
    None,    // passive / no delivery
    Self,    // affects only the caster (buffs, heals, leaps)
    Unit,    // one selected unit (targeted strike, ally heal, tracking projectile)
    Circle,  // disc (centred on caster, target or aimed ground)
    Cone,    // wedge from the caster toward the aim
    Line,    // rectangle from the caster in the aim direction (skillshots, line areas, charges, pulls)
    Square,
    Custom,  // authored polygon (Blight Sigil, construct footprints)
    Chain    // first hop caster->target, then jumps inside Radius around the target
};

// Order matches the procedural renderer families (CireSpellPresentation); Tide/Void are monster schools.

enum class ECireSchool : uint8 { Steel, Fire, Frost, Storm, Shadow, Life, Holy, Poison, Arcane, Earth, Nature, Spirit, Blood, Tide, Void, Count };

struct CIRESTEAMSURVIVAL_API FCireHitShape
{
    FName Id;
    ECireHitShape Kind = ECireHitShape::None;
    ECireSchool School = ECireSchool::Steel;
    float Radius = 0, Length = 0, Width = 0, Angle = 0;
    TArray<FVector2D> Polygon;
    bool bFromCaster = false;  // cone/line originate at the caster; circles centre on the caster
    bool bAtTarget = false;    // circle centred on the selected / victim unit
    bool bGroundAim = false;   // player aims the footprint with the cursor
    bool bProjectile = false;  // moving collision sphere; Line is its swept corridor (width = 2 x radius)
    bool bHostileOnly = true;  // false for heals / ally buffs
    float WarningSeconds = 0;  // harmless telegraph before the hit resolves
    float Speed = 0;           // projectile travel speed (cm/s)
    float LingerSeconds = 0;   // persistent area / summon lifetime shown after release
    bool bHeal = false;        // restores health (heal telegraph style: green/gold cross runes, upward shimmer)
    bool bBuff = false;        // helpful but not a heal (calm style)
    // Void zone of teleport/portal skills: outer ring slows, inner circle stuns (0 = none).
    float VoidOuter = 0, VoidInner = 0, VoidSeconds = 0;
    bool bVoidHeal = false, bVoidAtOrigin = false, bVoidFromDatabase = false;
    bool HasVoidZone() const { return VoidOuter > 0 && VoidInner > 0 && VoidInner < VoidOuter; }
    float ImpactSeconds(float Distance) const;
    bool HasGroundShape() const { return Kind == ECireHitShape::Circle || Kind == ECireHitShape::Cone || Kind == ECireHitShape::Line || Kind == ECireHitShape::Square || Kind == ECireHitShape::Custom; }
    // Boundary in the shape's local frame (+X = aim direction; lines/cones start at the origin).
    FCireAreaSpec AsArea() const;
};

namespace CireAbilityShapes
{
    // Caster: the monster archetype that owns the ability (null: champion skill or first owner in data).
    CIRESTEAMSURVIVAL_API FCireHitShape Describe(FName Id, const FCireNPCArchetype* Caster = nullptr);
    CIRESTEAMSURVIVAL_API FCireHitShape DescribeMonster(const FCireNPCAbility& Ability, const FCireNPCArchetype* Caster);
    CIRESTEAMSURVIVAL_API ECireSchool SchoolFor(FName Id, const FCireNPCArchetype* Caster = nullptr);
    CIRESTEAMSURVIVAL_API FLinearColor SchoolColor(ECireSchool School);
    CIRESTEAMSURVIVAL_API FString ShapeName(ECireHitShape Kind);
    CIRESTEAMSURVIVAL_API FString SchoolName(ECireSchool School);
    CIRESTEAMSURVIVAL_API bool ParseSchool(const FString& Text, ECireSchool& Out);
    // Champion-draft Ability Database (Content/Data/Abilities.json): "school" and "voidZone" per ability.
    // Absent file/entries fall back to the built-in mapping; reloaded on demand.
    CIRESTEAMSURVIVAL_API bool ReloadDatabase(FString* Error = nullptr);
    CIRESTEAMSURVIVAL_API int32 DatabaseCount();
#if !UE_BUILD_SHIPPING
    // Tests: replace the loaded database with this JSON (ReloadDatabase restores the file).
    CIRESTEAMSURVIVAL_API bool DebugUseDatabase(const FString& Json);
#endif
    // Loader used by ReloadDatabase (exposed for tests): tolerant to array or object "abilities".
    CIRESTEAMSURVIVAL_API bool ParseDatabase(const FString& Json, TMap<FName, TPair<ECireSchool, FCireHitShape>>& Out, FString* Error = nullptr);
    // Finds the first archetype that authors this ability id (monster casts arrive as cue ids).
    CIRESTEAMSURVIVAL_API const FCireNPCArchetype* FindOwner(FName AbilityId, const FCireNPCAbility** OutAbility = nullptr);
    // Every implemented champion ability id (actives, ultimates, passives, role skills) and basic attack style.
    CIRESTEAMSURVIVAL_API TArray<FName> ChampionAbilityIds();
    // aoe-scale (Eric 2026-09-26: "AoE hit radius +30% ... keep it coupled to the visuals"): GAMEPLAY area-of-effect hit
    // radii grow by Content/Data/VFXTuning.json "aoeRadiusScale" (1.3): ground zones, self / target circles and cones,
    // line-area and skillshot widths, teleport / portal rings, chain hops, splash and trigger radii, for champions and
    // monsters alike. Ranges, lane lengths and cast ranges do not change. The scale is applied where each radius ORIGINATES
    // (the Ability Database, NPC ability, CombatTuning and AstraAbilities loaders, the tech construct recipes and the
    // champion constants below), so the hit test and the telegraph (Describe / aim preview / zone painter) read the same
    // grown value and always agree. Data only: the Options slider and the presentation cvars never change it.
    constexpr float DefaultAoERadiusScale = 1.3f, MinAoERadiusScale = .5f, MaxAoERadiusScale = 2.f;
    CIRESTEAMSURVIVAL_API float AoERadiusScale();
    // Radius x AoERadiusScale() (an authored / base radius -> the gameplay radius).
    inline float AoE(float Radius) { return Radius * AoERadiusScale(); }

    // Hard-coded champion radii (CireHero.cpp reads these); the behavioural tests cast each one against dummies just
    // inside and outside this radius, so any drift fails loudly. The *Base values are the authored sizes before aoe-scale.
    constexpr float CleaveRadiusBase = 320.f, WarCryRadiusBase = 850.f, SanctuaryRadiusBase = 600.f, BastionRadiusBase = 650.f,
        RenewalRadiusBase = 1000.f, CataclysmRadiusBase = 550.f, ChainRadiusBase = 500.f;
    inline float CleaveRadius() { return AoE(CleaveRadiusBase); }
    inline float WarCryRadius() { return AoE(WarCryRadiusBase); }
    inline float SanctuaryRadius() { return AoE(SanctuaryRadiusBase); }
    inline float BastionRadius() { return AoE(BastionRadiusBase); }
    inline float RenewalRadius() { return AoE(RenewalRadiusBase); }
    inline float CataclysmRadius() { return AoE(CataclysmRadiusBase); }
    inline float ChainRadius() { return AoE(ChainRadiusBase); }
}
