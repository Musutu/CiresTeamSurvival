#pragma once

#include "CoreMinimal.h"
#include "CireSkillTuning.generated.h"

// Ignore: no impact. Stop: resolve the eligible hit and end travel. Pierce:
// resolve an eligible actor once, then continue through that collision category.
UENUM()
enum class ECireProjectileCollision : uint8 { Ignore, Stop, Pierce, Reflect };

// new-champions: Turret/Trap/Pylon/Skitter are the Aetheri "Constructs" (CireTechConstructs.h).
UENUM()
enum class ECireConstructKind : uint8 { Wall, Protection, Turret, Trap, Pylon, Skitter };

USTRUCT()
struct CIRESTEAMSURVIVAL_API FCireSkillshotSpec
{
    GENERATED_BODY()
    UPROPERTY() float Speed = 1600.f;
    UPROPERTY() float Radius = 24.f;
    UPROPERTY() float MaxRange = 1200.f;
    UPROPERTY() float LifetimeSeconds = 3.f;
    UPROPERTY() float Damage = 70.f;
    UPROPERTY() float WarningSeconds = .35f;
    UPROPERTY() float ManaCost = 0.f;
    UPROPERTY() float EnergyCost = 0.f;
    UPROPERTY() float CooldownSeconds = 0.f;
    UPROPERTY() float CastRange = 1200.f;
    UPROPERTY() bool bCanCrit = true;
    UPROPERTY() int32 HitLimit = 1;
    UPROPERTY() int32 ReflectionLimit = 2;
    UPROPERTY() bool bHitSameTargetAgain = false;
    UPROPERTY() FString AbilityName;
    UPROPERTY() ECireProjectileCollision WorldCollision = ECireProjectileCollision::Stop;
    UPROPERTY() ECireProjectileCollision PlayerCollision = ECireProjectileCollision::Stop;
    UPROPERTY() ECireProjectileCollision MonsterCollision = ECireProjectileCollision::Stop;
    UPROPERTY() ECireProjectileCollision ProtectionCollision = ECireProjectileCollision::Stop;
    UPROPERTY() ECireProjectileCollision WallCollision = ECireProjectileCollision::Stop;
    UPROPERTY() FString VisualStyle = TEXT("arcane");
    UPROPERTY() FLinearColor Color = FLinearColor(.3f, .7f, .85f, .85f);
};

USTRUCT()
struct CIRESTEAMSURVIVAL_API FCireConstructSpec
{
    GENERATED_BODY()
    UPROPERTY() ECireConstructKind Kind = ECireConstructKind::Wall;
    UPROPERTY() float MaxHealth = 250.f;
    UPROPERTY() float LifetimeSeconds = 8.f;
    UPROPERTY() float Width = 400.f;
    UPROPERTY() float Depth = 60.f;
    UPROPERTY() float Height = 240.f;
    UPROPERTY() float ManaCost = 60.f;
    UPROPERTY() float EnergyCost = 0.f;
    UPROPERTY() float CooldownSeconds = 22.f;
    UPROPERTY() float CastRange = 750.f;
    UPROPERTY() bool bBlockMovement = true;
    UPROPERTY() bool bBlockProjectiles = true;
    UPROPERTY() bool bDestructible = true;
    UPROPERTY() bool bBlockFriendly = true;
    UPROPERTY() ECireProjectileCollision ProtectionResponse = ECireProjectileCollision::Stop;
    UPROPERTY() FLinearColor Color = FLinearColor(.28f, .42f, .38f, .85f);
    // new-champions: tech constructs (turret/trap/pylon/skitter). Ignored by walls and protection.
    UPROPERTY() FName Recipe;                 // CireTechConstructs recipe id (per-owner limits, visuals)
    UPROPERTY() FName Effect;                 // trap: stasis|mine|silence; pylon: shield|haste|weaken|slow|nexus|empower
    UPROPERTY() float AttackRange = 0.f;      // turret reach / skitter seek radius (cm)
    UPROPERTY() float AttackInterval = 1.f;   // turret seconds between bolts
    UPROPERTY() float AttackDamage = 0.f;     // turret bolt / trap / skitter blast damage
    UPROPERTY() float TriggerRadius = 0.f;    // trap / skitter trigger distance (cm)
    UPROPERTY() float EffectRadius = 0.f;     // blast radius, pylon field radius (cm)
    UPROPERTY() float EffectMagnitude = 0.f;  // pylon fraction (slow .35, haste .25...), trap control seconds
    UPROPERTY() float MoveSpeed = 0.f;        // skitter run speed (cm/s)
    UPROPERTY() int32 OwnerLimit = 0;         // live constructs of this recipe per owner; the oldest is replaced
    UPROPERTY() float SplashRadius = 0.f;     // turret bolt splash (cm), 0 = single target
    bool IsTech() const { return Kind == ECireConstructKind::Turret || Kind == ECireConstructKind::Trap || Kind == ECireConstructKind::Pylon || Kind == ECireConstructKind::Skitter; }
};

struct CIRESTEAMSURVIVAL_API FCireGlobalCombatTuning
{
    float CritChance = .05f;
    float CritMultiplier = 1.5f;
    float TankDamageThreatMultiplier = 5.f;
    float DpsDamageThreatMultiplier = 1.f;
    float HealingThreatMultiplier = .4f;
    float NormalMonsterDamage = 18.f;
    float BossMonsterDamage = 40.f;
    float ChallengeMonsterDamage = 24.f;
    float BruiserMonsterDamage = 24.f;
    float CasterMonsterDamage = 14.f;
    float RangedMonsterDamage = 16.f;
    float WaveHealthBase = 800.f;
    float WaveHealthPerWave = 80.f;
    float BossHealthMultiplier = 6.f;
    float ChallengeHealthBase = 1200.f;
    float BasicHealthMultiplier = 1.f;
    float BruiserHealthMultiplier = 1.5f;
    float CasterHealthMultiplier = .8f;
    float RangedHealthMultiplier = .9f;
};

USTRUCT()
struct CIRESTEAMSURVIVAL_API FCireSummonSpec
{
    GENERATED_BODY()
    UPROPERTY() int32 Count = 1;
    UPROPERTY() float Health = 400.f;
    UPROPERTY() float Damage = 22.f;
    UPROPERTY() float DurationSeconds = 30.f;
    UPROPERTY() float ManaCost = 45.f;
    UPROPERTY() float EnergyCost = 0.f;
    UPROPERTY() float CooldownSeconds = 20.f;
    UPROPERTY() float CastRange = 600.f;
    UPROPERTY() bool bCommandable = true;
    UPROPERTY() float LeashRange = 1600.f;
    UPROPERTY() float MoveSpeed = 430.f;
    UPROPERTY() float AttackRange = 180.f;
    UPROPERTY() int32 ArchetypeVisual = 0;
};

struct CIRESTEAMSURVIVAL_API FCireRoleSkillSpec
{
    float ManaCost=0,EnergyCost=0,CooldownSeconds=30,CastRange=1200;
    float Radius=450,DurationSeconds=0,WarningSeconds=0;
    float FlatPower=0,PrimaryScaling=0,MaxHealthFraction=0;
};

struct CIRESTEAMSURVIVAL_API FCireTuningData
{
    FCireGlobalCombatTuning Globals;
    TMap<FString, FCireSkillshotSpec> Skillshots;
    TMap<FString, FCireConstructSpec> Constructs;
    TMap<FString, FCireSummonSpec> Summons;
    TMap<FString, FCireRoleSkillSpec> RoleSkills;
};

namespace CireSkillTuning
{
    CIRESTEAMSURVIVAL_API const FCireGlobalCombatTuning& Get();
    CIRESTEAMSURVIVAL_API const FCireSkillshotSpec* FindSkillshot(const FString& Id);
    CIRESTEAMSURVIVAL_API const FCireConstructSpec* FindConstruct(const FString& Id);
    CIRESTEAMSURVIVAL_API const FCireSummonSpec* FindSummon(const FString& Id);
    CIRESTEAMSURVIVAL_API const FCireRoleSkillSpec* FindRoleSkill(const FString& Id);
    CIRESTEAMSURVIVAL_API int32 SkillshotCount();
    CIRESTEAMSURVIVAL_API int32 ConstructCount();
    CIRESTEAMSURVIVAL_API int32 SummonCount();
    // Parse and reload are transactional: an invalid document never publishes
    // partially updated globals or recipes. Returned recipe pointers remain
    // stable until a successful explicit reload; copy a spec into spawned actors.
    CIRESTEAMSURVIVAL_API bool ParseJson(const FString& Json, FCireTuningData& Out, FString& Error);
    CIRESTEAMSURVIVAL_API bool Reload(FString* Error = nullptr);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunValidationSmoke();
#endif
}
