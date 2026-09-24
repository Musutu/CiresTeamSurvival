#pragma once
#include "CoreMinimal.h"
#include "CireNPCArchetypes.generated.h"

// Data-driven monster definitions loaded from Content/Data/NPCArchetypes.json.
// See Docs/NPCs.md for the document format and the UI read API.

// Every monster has exactly one combat role; the role selects movement policy.
UENUM(BlueprintType)
// monster-races: Support (ranged healer/warder, caster movement) and Swarm (small fast melee, bruiser movement).
enum class ECireNPCRole : uint8 { Bruiser, Tank, Caster, Ranged, Support, Swarm };

// Classification drives the UI frame (normal / elite dragon / boss frame).
UENUM(BlueprintType)
enum class ECireNPCClass : uint8 { Normal, Elite, Boss };

// Behaviour primitive executed by CireNPCCombat for one ability entry.
UENUM(BlueprintType)
enum class ECireNPCAbilityKind : uint8
{
    Melee,        // basic weapon swing (basic attack)
    Projectile,   // cast bar, then an authored CombatTuning skillshot
    Cone,         // telegraphed frontal cone
    TargetCircle, // telegraphed circle at the victim's feet (optionally a lingering pool)
    SelfCircle,   // telegraphed circle around the caster
    Charge,       // telegraphed line, then a dash along it
    Guard,        // redirect part of an ally's damage to this unit
    Provoke,      // nearby heroes deal reduced damage to anyone but this unit
    Rally,        // buff nearby allies' damage and attack speed
    Enrage,       // one-shot trigger at a health threshold
    HealAlly,     // interruptible heal on the most injured ally
    ShieldWall,   // self damage reduction below a health threshold
    Disengage,    // leap away from a victim in melee range
    // monster-races: generic race-skill behaviours (riders root/silence/slow/knockback work on every telegraph)
    Pull,         // telegraphed line to the victim (or farthest champion); champions in it are dragged to the caster
    Summon        // interruptible cast; spawns Count units of SummonId beside the caster (joins its wave)
};

struct CIRESTEAMSURVIVAL_API FCireNPCAbility
{
    FName Id;
    FString Name;
    FString Description;
    ECireNPCAbilityKind Kind = ECireNPCAbilityKind::Melee;
    bool bBasic = false;           // basic attack: not gated by the ability global cooldown
    bool bInterruptible = false;   // cast bar can be kicked by hero interrupts
    FString Targeting = TEXT("victim"); // victim | farthest
    FString Skillshot;             // Projectile: CombatTuning skillshot id
    float Cooldown = 8.f;
    float CastTime = 0.f;          // cast bar / telegraph warning seconds
    float Range = 600.f;           // maximum victim distance for use
    float MinRange = 0.f;          // minimum victim distance for use
    float Radius = 300.f;
    float Angle = 80.f;
    float Length = 700.f;
    float Width = 170.f;
    float DamageMultiplier = 1.f;  // x unit damage
    float DamagePerSecond = 0.f;   // lingering pools
    float Duration = 0.f;          // pools, buffs, guard, provoke
    float Magnitude = 0.f;         // buff / reduction / heal fraction
    float HealthThreshold = 0.f;   // Enrage/ShieldWall/HealAlly trigger fraction
    float InitialCooldown = 0.f;
    FLinearColor Color = FLinearColor(.8f,.24f,.06f,.35f);
    // monster-races: riders applied to champions inside the telegraph when it lands, plus presentation ids.
    float Root = 0.f;              // seconds rooted (cannot move)
    float Silence = 0.f;           // seconds silenced (cannot cast skills)
    float Slow = 0.f;              // seconds slowed (existing slow)
    float Knockback = 0.f;         // cm pushed away from the caster / circle centre
    FName SummonId;                // Summon: archetype id
    int32 Count = 1;               // Summon: units per cast
    FName Buff;                    // BuffVisuals.json id shown on champions hit (or allies buffed)
    FName Cue;                     // AudioCues.json id played when the cast starts
    bool bCore = false;            // always in the kit once skills unlock (never drawn out of the pool)
    bool HasRiders() const { return Root>0||Silence>0||Slow>0||Knockback>0||!Buff.IsNone(); }
};

struct CIRESTEAMSURVIVAL_API FCireNPCProp
{
    FString Asset;
    FName Bone = TEXT("hand_r");
    FVector Offset = FVector::ZeroVector;
    FRotator Rotation = FRotator::ZeroRotator;
    float Scale = 1.f;
};

struct CIRESTEAMSURVIVAL_API FCireNPCArchetype
{
    FName Id;
    FString DisplayName;
    ECireNPCRole Role = ECireNPCRole::Bruiser;
    ECireNPCClass Classification = ECireNPCClass::Normal;
    // 0..3 = basic/bruiser/caster/ranged: health factor and damage come from
    // CombatTuning.json globals instead of HealthMultiplier/Damage. -1 = use this file.
    int32 TuningKind = -1;
    float HealthMultiplier = 1.f;
    float Damage = 18.f;
    float EliteDamageMultiplier = 1.f;
    float MoveSpeed = 200.f;
    float Armor = 0.f;              // fraction of incoming damage ignored
    float AttackRange = 170.f;
    float AttackInterval = 1.8f;
    float PreferredRange = 0.f;     // casters/ranged hold this distance
    float KiteRange = 0.f;          // closer victims make casters/ranged back off
    float Scale = 1.f;
    int32 LeakCost = 1;
    // Mesh slot: an art agent can drop a model in by setting meshPath. Empty or
    // missing assets fall back to the mannequin body so the build never breaks.
    FString MeshSlot;
    FString MeshPath;
    float MeshScale = 1.f;
    float MeshYaw = -90.f;
    FString MaterialPath;
    FLinearColor Tint = FLinearColor::White;
    TArray<FCireNPCProp> Props;
    TArray<FCireNPCAbility> Abilities;
    // monster-races: per-unit stat scaling on top of tuning, lane-boss health override and race membership.
    float HealthScale = 1.f;
    float DamageScale = 1.f;
    float LaneBossHealthMultiplier = 0.f; // >0 replaces health factor x bossHealthMultiplier for lane bosses
    FName RaceId;
    FName Slot;                     // line|bruiser|tank|caster|ranged|special|warlord|colossus
    FName FallbackBody;             // archetype whose Tripo body is drawn until the unit has its own art
    int32 PoolDraw = 99;            // pool skills a normal-rank unit may draw per match
    FString Look;                   // Tripo art prompt (Docs/Races.md)
    const FCireNPCAbility* FindAbility(FName AbilityId) const;
    const FCireNPCAbility* BasicAttack() const;
};

struct CIRESTEAMSURVIVAL_API FCireNPCThreatRules
{
    float MeleePullRatio = 1.1f;    // WoW: 110% of the current target's threat inside melee
    float RangedPullRatio = 1.3f;   // WoW: 130% outside melee
    float MeleeRangeCm = 300.f;
    float PublishInterval = .25f;   // replicated threat table refresh
    float TauntMaxSeconds = 10.f;
};

struct CIRESTEAMSURVIVAL_API FCireNPCDatabase
{
    int32 SchemaVersion = 1;
    TMap<FName,FCireNPCArchetype> Archetypes;
    TArray<FName> LegacyKinds;       // numeric Configure(Kind) compatibility: 0..3
    TArray<FName> WaveComposition;   // cycled by wave spawn slot
    FName WaveBoss;
    TArray<FName> PackMembers;       // challenge-pack members, in spawn order
    FName PackLeader;
    int32 PackLeaderFromTier = 1;
    FCireNPCThreatRules Threat;
};

namespace CireNPCArchetypes
{
    CIRESTEAMSURVIVAL_API const FCireNPCDatabase& Get();
    CIRESTEAMSURVIVAL_API const FCireNPCArchetype* Find(FName Id);
    CIRESTEAMSURVIVAL_API FName LegacyKind(int32 Kind);
    CIRESTEAMSURVIVAL_API bool ParseJson(const FString& Json, FCireNPCDatabase& Out, FString& Error);
    CIRESTEAMSURVIVAL_API bool Reload(FString* Error = nullptr);
    CIRESTEAMSURVIVAL_API FString RoleLabel(ECireNPCRole Role);
    CIRESTEAMSURVIVAL_API FString ClassLabel(ECireNPCClass Class);
    // Player-facing ability type label: Attack, Cast, Telegraph, Buff, Passive...
    CIRESTEAMSURVIVAL_API FString KindLabel(const FCireNPCAbility& Ability);
    // monster-races: shared parsers so Races.json units use the exact NPCArchetypes.json schema.
    CIRESTEAMSURVIVAL_API bool ParseArchetypeObject(const FString& Key, const TSharedPtr<class FJsonObject>& Object, FCireNPCArchetype& Out, FString& Error);
    CIRESTEAMSURVIVAL_API bool ParseAbilityObject(const TSharedPtr<class FJsonObject>& Object, FCireNPCAbility& Out, FString& Error, const FString& Where);
    /** Casters, rangers and supports keep their distance and shoot projectile basics. */
    CIRESTEAMSURVIVAL_API bool IsRangedRole(ECireNPCRole Role);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunValidationSmoke();
#endif
}
