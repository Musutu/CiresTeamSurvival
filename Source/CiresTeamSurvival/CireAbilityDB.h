#pragma once
// Unified ability database (Content/Data/Abilities.json, built by Tools/BuildAbilityDB.py).
// Every champion ability and passive: identity, type (DPS/TANK/HEAL, hybrids allowed),
// kind, school, targeting, cast time, base numbers, uncapped per-level scaling curve,
// crowd-control effects, void zones, and which champions can learn it. Read-only;
// Reload() swaps the whole table transactionally. See Docs/Abilities.md.
#include "CoreMinimal.h"
#include "Rules/CiresRules.h"

/** One gameplay effect an ability applies (CC, guard, execute...). */
struct CIRESTEAMSURVIVAL_API FCireAbilityEffect
{
    FName Type;              // stun, slow, silence, interrupt, healCut, healCutDone, armorBreak, taunt, guard, lethal, cleanse
    FName Zone;              // target, area, self, inner, outer
    float Duration = 0;      // seconds
    float Magnitude = 0;     // fraction (slow 0.35, healCut 0.5, armorBreak 0.5, guard 0.4)
    float Radius = 0;        // cm (area effects)
    float LockoutSeconds = 0;// interrupt school lockout
    FString Label;           // UI modifier summary, e.g. "Healing -50%"
};

/** Void component of blink/portal skills: inner circle stuns, outer ring slows. */
struct CIRESTEAMSURVIVAL_API FCireVoidZone
{
    bool bValid = false;
    float InnerRadius = 0, OuterRadius = 0;     // cm, from the impact point
    float InnerDuration = 0, OuterDuration = 0; // stun / slow seconds
    float OuterMagnitude = 0;                   // slow fraction
    float Damage = 0;                           // void damage to everyone in either zone
    float SelfHealMaxHealthFraction = 0;        // caster mends this fraction of max health
};

struct CIRESTEAMSURVIVAL_API FCireAbilityDef
{
    FString Id, Name, Icon, Kind, School, Targeting, Status, Description, EffectLabel;
    FString Category;                    // new-champions: "construct" for Aetheri Constructs (Skill Shop tab); empty otherwise
    TArray<FString> Types;               // "DPS", "TANK", "HEAL"
    float CastTime = 0, Range = 0, Radius = 0, Duration = 0;
    Cires::Abilities::Base Base;
    Cires::Abilities::Curve Curve;
    TArray<FCireAbilityEffect> Effects;
    FCireVoidZone Void;
    TArray<FString> Champions;           // champions that can learn it
    TArray<FString> SignatureOf;         // champions whose identity kit lists it
    // scaling-kits (Docs/Abilities.md): damage/heal/shield/DoT = ScaleBase + ScalePrimary x caster PRIMARY stat.
    FString ScaleComponent;              // damage, heal, shield, summon, construct, none
    float ScaleBase = 0, ScalePrimary = 0, DotPerSecondPrimary = 0;
    FString Requires;                    // "shield" (shield users only), "ranged" (ranged basic attack), empty
    FName Level15Bonus;                  // dot, healCut, stun, slow, damageAmp, vulnerability, purge (actives/ultimates)
    FString Level15Special, Level15Label, Level15Trigger; // special: mechSlam, artilleryBomb, headshotTriple; trigger: hit|pulse
    FName Aura15;                        // passives: attackSpeed, doubleAttack, crit, ... (team aura at level 15)
    FString Aura15Label;
    bool IsImplemented() const { return Status == TEXT("implemented"); }
    bool IsPassive() const { return Kind == TEXT("passive"); }
    bool IsUltimate() const { return Kind == TEXT("ultimate"); }
    bool IsConstruct() const { return Category == TEXT("construct"); }
};

/** Scaled numbers at a level (level >= 1, no cap). */
struct CIRESTEAMSURVIVAL_API FCireAbilityStats
{
    int32 Level = 1;
    float Effect = 0, ManaCost = 0, EnergyCost = 0, Cooldown = 0, CastTime = 0, Range = 0, Radius = 0, Duration = 0;
};

struct CIRESTEAMSURVIVAL_API FCireChampionKit
{
    FString Name, PrimaryRole;
    TArray<FString> Roles, Signature, Purchasable, PurchasableImplemented;
};

struct CIRESTEAMSURVIVAL_API FCireModifier { FString Stat; float Value = 0; FString Label; };

namespace CireAbilityDB
{
    CIRESTEAMSURVIVAL_API const TArray<FCireAbilityDef>& All();
    CIRESTEAMSURVIVAL_API const FCireAbilityDef* Find(const FString& Id);
    /** Lookup by display name (combat events carry names). */
    CIRESTEAMSURVIVAL_API const FCireAbilityDef* FindByName(const FString& Name);
    CIRESTEAMSURVIVAL_API FCireAbilityStats EffectiveStats(const FString& Id, int32 Level);
    /** Tooltip text: current values at Level and what Level+1 adds. */
    CIRESTEAMSURVIVAL_API FString Describe(const FString& Id, int32 Level);
    /** scaling-kits: "damage", "healing", "barrier health", "damage per hit". */
    CIRESTEAMSURVIVAL_API FString ScalingWord(const FCireAbilityDef& Def);
    /** Identity kit of a champion profile (ChampionRoster id). */
    CIRESTEAMSURVIVAL_API const FCireChampionKit* Kit(const FString& ProfileId);
    /** Everything this champion may buy (role pool + hybrid roles + signature). */
    CIRESTEAMSURVIVAL_API TArray<FString> PurchasableSkills(const FString& ProfileId, bool bImplementedOnly = true);
    CIRESTEAMSURVIVAL_API bool CanLearn(const FString& ProfileId, const FString& AbilityId);
    /** The free opening pick (level 1): four primary-role actives, see Cires::IsOpeningSkill. */
    CIRESTEAMSURVIVAL_API TArray<FString> OpeningSkills(const FString& ProfileId);
    /** UI modifier summaries for a buff/debuff id ("Healing -50%", "Stunned"...). */
    CIRESTEAMSURVIVAL_API const TArray<FCireModifier>* BuffModifiers(FName BuffId);
    CIRESTEAMSURVIVAL_API FString ModifierSummary(FName BuffId);
    CIRESTEAMSURVIVAL_API bool ParseJson(const FString& Json, TArray<FCireAbilityDef>& OutAbilities, TMap<FString, FCireChampionKit>& OutKits,
        TMap<FName, TArray<FCireModifier>>& OutModifiers, FString& Error);
    CIRESTEAMSURVIVAL_API bool Reload();
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke();
#endif
}
