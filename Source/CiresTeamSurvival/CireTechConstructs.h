#pragma once
// new-champions: the Aetheri "Constructs" skill category (Docs/NewChampions.md).
//
// Turrets (auto-attacking energy bolts), traps (stasis snare, arc mine, spirit lantern),
// buff/debuff pylons (shield-regen, haste, damage-down, slow, nexus, monster empower) and
// skitter bombs (tiny mechanical runners that explode on contact) are ACireConstruct kinds:
// server-authoritative, replicated, destructible, owner-limited per recipe, placed with the
// ground-aim system and presented with arcane (energy) school runes. Champions and monsters
// (the Aetheri monster race's engineers and Hierarch) both deploy them; they fight in the
// survival lanes and in the arena.
#include "CoreMinimal.h"
#include "CireSkillTuning.h"

class AActor;
class ACireConstruct;
class ACireHero;
class ACireMonster;
class ACireGameMode;

struct CIRESTEAMSURVIVAL_API FCireTechRecipe
{
    FName Id;                         // champion skill id or monster recipe id (npc_*)
    FString Name;                     // display / combat-log name (champion recipes use the Ability DB name)
    ECireConstructKind Kind = ECireConstructKind::Turret;
    FName Effect;                     // trap: stasis|mine|silence, pylon: shield|haste|weaken|slow|nexus|empower
    float Health = 200, Lifetime = 20, Footprint = 70, Height = 150;
    float Range = 0, Interval = 1, Damage = 0, Scaling = 0; // Damage + Scaling x primary attribute (champions)
    float Trigger = 0, Radius = 0, Magnitude = 0, Speed = 0, Splash = 0;
    int32 Limit = 1, Count = 1;       // live per owner (oldest replaced) / constructs per cast
    FLinearColor Color = FLinearColor(.45f, .55f, 1.6f, 1);
    bool bMonster = false;
};

namespace CireTechConstructs
{
    CIRESTEAMSURVIVAL_API const TArray<FCireTechRecipe>& Recipes();
    CIRESTEAMSURVIVAL_API const FCireTechRecipe* FindRecipe(FName Id);
    CIRESTEAMSURVIVAL_API bool IsConstructSkill(const FString& Id);
    /** Scaled spec for this owner: champion primary stat, level, passives and team power; monster damage x multiplier. */
    CIRESTEAMSURVIVAL_API bool BuildSpec(AActor* Owner, FName Recipe, FCireConstructSpec& Out, float MonsterDamageMultiplier = 1.f);
    /** Server: place Count constructs around Aim (ground-aimed). Returns the ones that fit (none on a blocked spot). */
    CIRESTEAMSURVIVAL_API TArray<ACireConstruct*> Deploy(AActor* Owner, FName Recipe, FVector Aim, FString* Why = nullptr, float MonsterDamageMultiplier = 1.f);
    CIRESTEAMSURVIVAL_API int32 CountOwned(const AActor* Owner, FName Recipe);
    /** Turrets of this owner within Radius fire twice as fast for Seconds and regain RepairFraction of max health. */
    CIRESTEAMSURVIVAL_API int32 Overcharge(AActor* Owner, FVector Center, float Radius, float Seconds, float RepairFraction);

    // ---- ACireConstruct hooks ----
    void OnSpawned(ACireConstruct* Construct);
    void OnExpired(ACireConstruct* Construct);
    /** Server tick. False when the construct destroyed itself (trap sprung, skitter exploded). */
    bool TickConstruct(ACireConstruct* Construct, float DeltaSeconds);
    float ModifyIncomingDamage(ACireConstruct* Construct, AActor* Causer, float Amount);
    void ApplyAppearance(ACireConstruct* Construct);
    void AnimateAppearance(ACireConstruct* Construct, float Time);
    /** Detonates a trap or skitter now (tests, expiry of armed skitters). Returns units hit. */
    CIRESTEAMSURVIVAL_API int32 Detonate(ACireConstruct* Construct);

    // ---- monster AI: smash a champion's turret/pylon in reach, or walk to one close by ----
    CIRESTEAMSURVIVAL_API bool MonsterHandleConstructs(ACireMonster* Monster, bool bHasVictimInReach);

    /** Buff record ids the constructs apply (BuffVisuals.json rows exist for each). */
    CIRESTEAMSURVIVAL_API const TArray<FName>& FieldBuffIds();
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
#endif
}
