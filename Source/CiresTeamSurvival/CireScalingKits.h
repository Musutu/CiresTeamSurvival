#pragma once
// scaling-kits: Eric's combat-scaling overhaul and new kits (maths: Rules/CireKitRules.h,
// data: Content/Data/Abilities.json "scaling" / "level15" / "aura15" / "requires").
//
//   * Universal primary scaling: Amount(Hero, Id) = base + coefficient x PRIMARY for every ability.
//   * Summons and constructs inherit the owner's attack speed and CDR and hit off the owner's primary.
//   * Monsters hold threat on (and attack) summons and constructs; turrets build construct threat.
//   * Shield-bearing tanks: -10% armour / magic resist, 30% chance to block 50% of physical damage
//     ("BLOCK" floating text + combat log).
//   * Skill level 15: actives gain a bonus (DoT, heal cut, stun, slow, damage amp, Vulnerability, purge);
//     passives grant a party aura (buff registry records "aura15_*").
//   * New skills: Shield Bash / Toss / Wall, Construct: Pavise, Construct: Mechanical Tank (CireMechTank),
//     Artillery (active + Artillery Training passive), Eagle Eye, Longshot Stance, Headshot.
// Server-authoritative hooks are called from the existing damage / stat / cast pipeline.
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Rules/CireKitRules.h"
#include "CireScalingKits.generated.h"

class AActor;
class ACireHero;
class ACireMonster;
class ACireConstruct;
class ACireGameMode;
struct FCireAbilityDef;

/** scaling-kits: marks damage dealt by ground areas (AoE-resist aura). */
struct CIRESTEAMSURVIVAL_API FCireAreaDamageScope
{
    FCireAreaDamageScope();
    ~FCireAreaDamageScope();
    static bool Active();
};

namespace CireKits
{
    // ---- universal primary-stat scaling ----
    CIRESTEAMSURVIVAL_API int32 PrimaryOf(const ACireHero* Hero);
    CIRESTEAMSURVIVAL_API FString PrimaryName(const ACireHero* Hero);                 // "STR" / "AGI" / "INT"
    /** base + coefficient x PRIMARY from the Ability DB (FallbackBase / FallbackCoef when the row has no scaling). */
    CIRESTEAMSURVIVAL_API float Amount(const ACireHero* Hero, const FString& Id, float FallbackBase = 0.f, float FallbackCoef = 0.f);
    /** kits-complete: potency multiplier of a utility skill / passive (1 + potency% x PRIMARY, capped); 1 for skills
     *  whose damage / heal / shield already scales with PRIMARY. */
    CIRESTEAMSURVIVAL_API float Potency(const ACireHero* Hero, const FString& Id);
    /** kits-complete: the skill's headline effect at the hero's skill level, times its potency. */
    CIRESTEAMSURVIVAL_API float ScaledEffect(const ACireHero* Hero, const FString& Id, float Fallback);
    /** kits-complete: crowd-control duration multiplier from the source champion's PRIMARY (+0.25% per point, max +25%). */
    CIRESTEAMSURVIVAL_API float ControlScale(const AActor* Source);
    /** Extra DoT per second from primary for ground areas (dotPerSecond x PRIMARY). */
    CIRESTEAMSURVIVAL_API float DotPerSecondBonus(const ACireHero* Hero, const FString& Id);
    /** "Deals 40 + 1.2x Primary (STR 30) damage" (empty when the ability has no scaled component). */
    CIRESTEAMSURVIVAL_API FString ScalingLine(const ACireHero* Hero, const FString& Id);
    /** Ability DB tooltip + the hero's scaling line + the level-15 line. */
    CIRESTEAMSURVIVAL_API FString DescribeFor(const ACireHero* Hero, const FString& Id, int32 Level);

    // ---- owner inheritance ----
    /** Total basic-attack speed multiplier of a champion (agility, items, class, auras, Battle Rhythm, Artillery). */
    CIRESTEAMSURVIVAL_API float AttackSpeedMultiplier(const ACireHero* Hero);
    /** Owner of a summon/construct (the hero itself for a champion). */
    CIRESTEAMSURVIVAL_API ACireHero* OwnerOf(const AActor* Unit);
    /** Interval of a summon / construct attack after the owner's attack speed. */
    CIRESTEAMSURVIVAL_API float InheritedInterval(const AActor* Unit, float BaseInterval);
    /** Base cooldown after an owner's CDR (pets, summons, constructs share this). */
    CIRESTEAMSURVIVAL_API float OwnerCooldown(const ACireHero* Owner, float BaseSeconds);
    /** A summon / construct ability cooldown after the owner's CDR. */
    CIRESTEAMSURVIVAL_API float InheritedCooldown(const AActor* Unit, float BaseSeconds);

    // ---- stat hooks (champion pipeline) ----
    CIRESTEAMSURVIVAL_API float AttackSpeedBonus(const ACireHero* Hero);   // auras + Artillery (+1.0)
    CIRESTEAMSURVIVAL_API float BasicRange(const ACireHero* Hero, float BaseRange);
    CIRESTEAMSURVIVAL_API float CritBonus(const AActor* Source);
    CIRESTEAMSURVIVAL_API bool BlocksCasting(ACireHero* Hero, const FString& Id);   // Artillery: basic attacks only
    CIRESTEAMSURVIVAL_API bool IsArtilleryActive(const ACireHero* Hero);

    // ---- defence hooks ----
    CIRESTEAMSURVIVAL_API bool CarriesShield(const ACireHero* Hero);
    CIRESTEAMSURVIVAL_API bool IsShieldTank(const ACireHero* Hero);
    /** Armour / magic-resist multiplier for this defender (shield penalty) against this attacker (Vulnerability). */
    CIRESTEAMSURVIVAL_API float DefenseMultiplier(const AActor* Defender);
    /** Flat armour (physical) / magic resist from party auras. */
    CIRESTEAMSURVIVAL_API float FlatDefense(const ACireHero* Hero, bool bPhysical);
    /** Shield block, Shield Wall, Pavise cover and the AoE-resist aura. Broadcasts BLOCK / RESIST. */
    CIRESTEAMSURVIVAL_API float ModifyIncomingDamage(ACireHero* Hero, AActor* Causer, const FString& AbilityName, float Amount);
    /** Level-15 stun-ignore aura. True = this stun is shrugged off. */
    CIRESTEAMSURVIVAL_API bool IgnoresStun(AActor* Target);
    /** Monster attack-timer rate (0.9 while the Mech Tank's level-15 slam weakens it). */
    CIRESTEAMSURVIVAL_API float MonsterAttackRate(const ACireMonster* Monster);

    // ---- damage pipeline hooks ----
    CIRESTEAMSURVIVAL_API float ModifyOutgoingDamage(AActor* Source, AActor* Target, float Amount, const FString& AbilityName);
    /** Level-15 hit bonuses, lifesteal / stun / double-attack auras, Headshot, Artillery accounting. */
    CIRESTEAMSURVIVAL_API void OnDamageDealt(AActor* Source, AActor* Target, float OriginalAmount, float Applied, const FString& AbilityName);
    /** Cast hook (CireSkillShop::ApplyCastLevel): level-15 pulses for non-damaging skills. */
    CIRESTEAMSURVIVAL_API void OnSkillCast(ACireHero* Hero, const FString& Id);
    /** Level-15 bonus applied to one target (tests and pulses). */
    CIRESTEAMSURVIVAL_API void ApplyLevel15(ACireHero* Hero, const FCireAbilityDef& Def, AActor* Target, float HitDamage);
    CIRESTEAMSURVIVAL_API int32 SkillLevel(const ACireHero* Hero, const FString& Id);

    // ---- party auras ----
    CIRESTEAMSURVIVAL_API Cires::Kits::AuraTotals PartyAuras(const ACireHero* Hero);
    CIRESTEAMSURVIVAL_API FName AuraBuffId(Cires::Kits::Aura Aura);

    // ---- monster targeting of summons and constructs ----
    CIRESTEAMSURVIVAL_API void AddConstructThreat(ACireMonster* Monster, ACireConstruct* Construct, float Amount);
    CIRESTEAMSURVIVAL_API float ConstructThreat(const ACireMonster* Monster, const ACireConstruct* Construct);
    /** NPC tick hook: true when the monster is busy pursuing / smashing a construct that holds its threat. */
    CIRESTEAMSURVIVAL_API bool MonsterPursueConstruct(ACireMonster* Monster);
    CIRESTEAMSURVIVAL_API ACireConstruct* ConstructVictim(const ACireMonster* Monster);

    // ---- skills ----
    CIRESTEAMSURVIVAL_API bool Handles(const FString& Id);
    CIRESTEAMSURVIVAL_API bool Cast(ACireHero* Hero, int32 Slot, const FString& Id);
    CIRESTEAMSURVIVAL_API FString Description(const FString& Id);
    /** Skill Shop gate: "requires": shield / ranged. */
    CIRESTEAMSURVIVAL_API bool MeetsRequirement(const ACireHero* Hero, const FString& Id, FString* Why = nullptr);
    CIRESTEAMSURVIVAL_API const TArray<FName>& BuffIds();
    /** Tests / telemetry: false disables random procs (shield block, Headshot, aura chances). */
    CIRESTEAMSURVIVAL_API void SetRandomProcs(bool bEnabled);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
#endif
}

/** Server-side timers for level-15 DoTs, Artillery windows, aura refreshes and construct threat. */
UCLASS()
class CIRESTEAMSURVIVAL_API UCireKitsSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Tick(float DeltaSeconds) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UCireKitsSubsystem, STATGROUP_Tickables); }
    static UCireKitsSubsystem* Get(const UWorld* World);

    struct FDot { TWeakObjectPtr<AActor> Source, Target; float PerTick = 0; int32 Ticks = 0; float Timer = 1.f; FString Name; };
    TArray<FDot> Dots;
    TMap<TWeakObjectPtr<ACireHero>, Cires::Kits::ArtilleryState> Artillery;
    TMap<TWeakObjectPtr<ACireHero>, TWeakObjectPtr<AActor>> ArtilleryLastTarget;
    TMap<TWeakObjectPtr<ACireMonster>, TMap<TWeakObjectPtr<ACireConstruct>, float>> ConstructThreat;
    TMap<FString, float> StunLockout; // "<target>/<ability>" -> world seconds
    float AuraTimer = 0.f;
    float LastBombDamage = 0.f;       // tests / captures
    FVector LastBombCenter = FVector::ZeroVector;
    void RefreshAuras();
};
