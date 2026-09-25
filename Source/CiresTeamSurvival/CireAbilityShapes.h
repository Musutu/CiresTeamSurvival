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
    // Finds the first archetype that authors this ability id (monster casts arrive as cue ids).
    CIRESTEAMSURVIVAL_API const FCireNPCArchetype* FindOwner(FName AbilityId, const FCireNPCAbility** OutAbility = nullptr);
    // Every implemented champion ability id (actives, ultimates, passives, role skills) and basic attack style.
    CIRESTEAMSURVIVAL_API TArray<FName> ChampionAbilityIds();
    // Hard-coded champion radii (CireHero.cpp) mirrored here; the behavioural tests cast each one against
    // dummies just inside and outside this radius, so any drift fails loudly.
    constexpr float CleaveRadius = 320.f, WarCryRadius = 850.f, SanctuaryRadius = 600.f, BastionRadius = 650.f,
        RenewalRadius = 1000.f, CataclysmRadius = 550.f, ChainRadius = 500.f;
}
