#pragma once
// kits-complete: gameplay for the 63 signature skills of Bear, both Paladins, Dwarf Miner, the three Ether
// Golems, Orc Chieftain, Totemic Behemoth, Drakish Footman, both Troll Berserkers, Dryad, Whisp, Evergrove
// Centaur and Keeper of Light (data: Tools/ChampionKits.py -> Content/Data/Abilities.json).
//
// Same contract as CireSignatureSkills (which routes to this module): every number (cost, cooldown, effect,
// range, radius, duration, crowd control) comes from the Ability Database; this module holds the delivery
// recipe (cone, line, charge, leap, hook, channel, heal zone, tether, orb, construct...) and the rules each
// skill adds. Server-authoritative; clients see replicated buff records, areas, skillshots, constructs,
// summons, barriers and combat events. Telegraphs come from DescribeShape (the true hit shape), the school
// rune set and the per-ability Fab Niagara overlay (Content/Data/FabVFX.json "abilities").
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CireKitSkills.generated.h"

class AActor;
class ACireHero;
class ACireMonster;
class ACireConstruct;
class ACireGameMode;
class ACireAreaEffect;
struct FCireHitShape;

namespace CireKitSkills
{
    CIRESTEAMSURVIVAL_API bool Knows(const FString& Id);
    CIRESTEAMSURVIVAL_API bool Handles(const FString& Id);   // castable (actives / ultimates)
    CIRESTEAMSURVIVAL_API bool IsPassive(const FString& Id);
    CIRESTEAMSURVIVAL_API const TArray<FString>& AllIds();
    CIRESTEAMSURVIVAL_API bool Cast(ACireHero* Hero, int32 Slot, const FString& Id);
    CIRESTEAMSURVIVAL_API bool DescribeShape(const FString& Id, FCireHitShape& Out);

    // ---- combat hooks (server; called from CireSignatureSkills / the damage and healing pipeline) ----
    /** Damage reduction states, marks, Relic Vow redirect, Totem Bulwark cover, Scale Guard thorns, passive hit counters. */
    CIRESTEAMSURVIVAL_API float ModifyOutgoingDamage(AActor* Source, AActor* Target, float Amount, const FString& AbilityName);
    /** Taunt / root / weaken / Vulnerability / knockback / burn / bloom riders authored on the kit skills. */
    CIRESTEAMSURVIVAL_API void OnAbilityHit(AActor* Source, AActor* Target, const FString& AbilityName, float Applied);
    /** Ether Furnace, Dragon Oath slashes, Red Moon cleave, Ember Memory ignition. */
    CIRESTEAMSURVIVAL_API float ModifyBasicAttack(ACireHero* Hero, AActor* Target, float Damage, FString& InOutName);
    /** Steady Gait (next heal stronger). */
    CIRESTEAMSURVIVAL_API float ModifyHealing(ACireHero* Source, ACireHero* Target, float Amount, const FString& AbilityName);
    /** Green Covenant banks part of every effective heal. */
    CIRESTEAMSURVIVAL_API void OnHealingDone(ACireHero* Source, ACireHero* Target, float Applied, const FString& AbilityName);
    CIRESTEAMSURVIVAL_API float MoveSpeedMultiplier(const ACireHero* Hero);
    CIRESTEAMSURVIVAL_API float AttackSpeedBonus(const ACireHero* Hero);
    CIRESTEAMSURVIVAL_API float BasicRangeBonus(const ACireHero* Hero);
    CIRESTEAMSURVIVAL_API float ResourceRegenMultiplier(const ACireHero* Hero);
    /** Elder of the Deepwood: extra whole-body scale while the form lasts (replicated buff, read on every machine). */
    CIRESTEAMSURVIVAL_API float BodyScaleMultiplier(const ACireHero* Hero);
    /** Bot rotation: false while casting this now would be wasted (self heal at full health, no wounded ally...). */
    CIRESTEAMSURVIVAL_API bool BotWantsCast(ACireHero* Hero, const FString& Id);
    /** Construct pylons of this module (rally banner, lantern resist field, ward barrier pulse). */
    CIRESTEAMSURVIVAL_API bool PylonPulse(ACireConstruct* Construct, AActor* Unit, bool bAlly, bool bEnemy, float HoldSeconds);
    /** A visible heal over time (roll-triggered mends: Shield Tumble, Evasive Stance). Total spread over Seconds (1 tick/s). */
    CIRESTEAMSURVIVAL_API void StartHealOverTime(ACireHero* Source, ACireHero* Target, float Total, float Seconds, const FString& Name);
    /** Buff record ids applied by these skills (BuffVisuals.json rows exist for each). */
    CIRESTEAMSURVIVAL_API const TArray<FName>& BuffIds();
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
#endif
}

/** Server-side timers: heal zones, channels, tethers, motes, burns, delayed blooms and the passive trackers. */
UCLASS()
class CIRESTEAMSURVIVAL_API UCireKitSkillsSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Tick(float DeltaSeconds) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UCireKitSkillsSubsystem, STATGROUP_Tickables); }
    static UCireKitSkillsSubsystem* Get(const UWorld* World);

    enum class EZone : uint8 { Heal, Guard, Haste, Beacon, Mountain, Bloom, Aura, Worldstone };
    struct FZone
    {
        TWeakObjectPtr<ACireHero> Owner;
        TWeakObjectPtr<ACireAreaEffect> Area;   // the visible ground (harmless persistent area); zone ends with it
        FString Id, Name;
        EZone Kind = EZone::Heal;
        FVector Center = FVector::ZeroVector, Direction = FVector::ForwardVector;
        float Radius = 300, Length = 0, Width = 0; // Length > 0: a line from Center along Direction
        float PerSecond = 0, Magnitude = 0, StartsAt = 0, EndsAt = 0, Timer = 0, Interval = 1;
        bool bFollowOwner = false;
    };
    struct FHot { TWeakObjectPtr<ACireHero> Source, Target; FString Name; float PerTick = 0; int32 Ticks = 0; float Timer = 1.f; float Interval = 1.f; float MaxDistance = 0; FName Buff; };
    struct FBurn { TWeakObjectPtr<AActor> Source, Target; FString Name; float PerTick = 0; int32 Ticks = 0; float Timer = 1.f; };
    struct FMote { TWeakObjectPtr<ACireHero> Owner; FVector At; float Amount = 0; float EndsAt = 0; FString Name; };
    struct FChannel { TWeakObjectPtr<ACireHero> Hero; FVector Start; float PerTick = 0; float EndsAt = 0; float Timer = .5f; FString Name; };
    struct FDragon { int32 Charges = 0; float EndsAt = 0; bool bPact = false; };

    TArray<FZone> Zones;
    TArray<FHot> Hots;
    TArray<FBurn> Burns;
    TArray<FMote> Motes;
    TArray<FChannel> Channels;
    TMap<TWeakObjectPtr<ACireHero>, FDragon> Dragons;
    TMap<TWeakObjectPtr<ACireHero>, int32> FurnaceCharges, BasicCount, OreHits;
    TMap<TWeakObjectPtr<ACireHero>, TArray<float>> HideHits;
    TMap<TWeakObjectPtr<ACireHero>, float> HideReadyAt, CoreDamage, LastHitAt, StillSince, MovingSince, Reserve, ReserveTimer;
    TMap<TWeakObjectPtr<ACireHero>, FVector> LastPosition;
    TMap<FString, float> LastLightReadyAt;        // "<keeper>/<ally>" -> world seconds
    float PassiveTimer = 0.f;
    int32 RedirectDepth = 0, ThornDepth = 0;
};
