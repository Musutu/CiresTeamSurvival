#pragma once
#include "CoreMinimal.h"

class AActor;
class ACharacter;
class UAudioComponent;
class UCireAudioSubsystem;
class UWorld;
enum class ECireSpellCue : uint8;

/**
 * Sound-event table (Content/Data/AudioEvents.json, Docs/Audio.md "Sound events").
 *
 * Every spell/attack presentation cue (CireCombat::PlayCue -> ACireSpellVisual) is resolved here to one or more
 * AudioCues.json cues from data only:
 *
 *   ability id / basic-attack style -> { element, kind, weapon }   (the "abilities" table, generated for every
 *                                                                    Ability DB entry by Tools/BuildAudioEvents.py)
 *   kind x slot -> cue template ("spell.{element}.impact", "weapon.{weapon}.swing", ...)
 *   + layers: critical ring, flesh/armour/stone impact by the target's armour class
 *
 * Unknown ids (monster abilities, item procs) fall back to their school (CireAbilityShapes::SchoolFor), so no
 * presentation cue is ever silent. Cues prefer installed Fab pack members and fall back to the shipped sounds.
 */
namespace CireSoundEvents
{
    /** One row of the table: what an ability sounds like. */
    struct FAbilitySound
    {
        FName Element;   // physical, fire, frost, nature, shadow, arcane, holy, earth, water, lightning
        FName Kind;      // spell, heal, buff, guard, shout, melee, shot, summon, passive, ...
        FName Weapon;    // melee/shot kinds: sword, axe, mace, dagger, glaive, spear, claws, staff, bow, crossbow, pistol, gunblade, blunderbuss, shield
        TMap<FName, FName> Overrides; // slot -> cue
    };
    struct FWeapon
    {
        FName Kind = TEXT("melee");
        TMap<FName, FName> Slots;     // swing, draw, release, projectile, impact
        FName Ranged;                 // e.g. gunblade -> pistol when the cue spans more than RangedAbove
        float RangedAbove = 0.f;
    };
    struct FData
    {
        bool bValid = false;
        TArray<FName> Elements;
        TMap<FName, FName> SchoolElements;          // ECireSchool name / DB school -> element
        TMap<FName, TMap<FName, FString>> Kinds;    // kind -> slot -> cue template
        TMap<FName, FWeapon> Weapons;
        TMap<FName, FName> WeaponAliases;           // normalized skill id / strike name / attack style -> weapon
        TMap<FName, FAbilitySound> Abilities;       // normalized id AND normalized display name -> row
        TMap<FName, FName> ArmorLayers;             // footstep armour class -> impact layer ("armor", "flesh", ...)
        TMap<FName, FName> ArmorWeapons;            // footstep armour class -> weapon for monsters ("claws" for beasts)
        FString ImpactLayerTemplate = TEXT("impact.{armor}");
        TArray<FName> CriticalLayers;
        TMap<FName, FName> Outcomes;                // block, deflect, dodge, miss, resist
        TMap<FName, FName> Deaths;                  // hero, player, humanoid, creature, golem, boss, ethereal
        TMap<FName, FName> DeathClasses;            // footstep armour class -> death key
        TMap<FName, FName> Ui;                      // ready, error, errorMana, errorGold, errorEnergy
        FName PlayerHit, PlayerHitHeavy, CastLoopDefault;
        float HeavyHitFraction = .12f, OtherVolume = .8f, LocalPriorityBoost = 30.f, IncomingPriorityBoost = 20.f;
        float CastLoopRadius = 3200.f, DeathRadius = 5200.f, AreaLoopVolume = .6f;
        int32 MaxCastLoops = 6;
    };
    CIRESTEAMSURVIVAL_API const FData& Data(bool bReload = false);

    CIRESTEAMSURVIVAL_API FName Normalize(const FString& Id);
    /** cast, launch, impact, critical, projectile, wall, protection (+ "channel"/"area" for loops). */
    CIRESTEAMSURVIVAL_API FName SlotName(ECireSpellCue Cue);
    /** ECireSchool (as int) -> element via "schools". */
    CIRESTEAMSURVIVAL_API FName ElementForSchool(int32 School);

    struct FQuery
    {
        FName Skill;
        FName Slot;              // SlotName(...) or channel / area
        FName CasterWeapon;      // weapon of the unit found at the cue origin (its basic attack style)
        FName TargetArmor;       // footstep armour class of the unit found at the cue target
        float Distance = 0.f;    // From -> To
        int32 SchoolOverride = -1; // tests: skip SchoolFor
    };
    struct FResolved
    {
        TArray<FName> Cues;      // primary first, then layers
        FName Element, Kind, Weapon;
        FString Source;          // "table", "alias", "school" (diagnostics / coverage)
    };
    CIRESTEAMSURVIVAL_API FResolved Resolve(const FQuery& Query);
    /** The table row for an id (exact or display-name match), nullptr when the id falls back to its school. */
    CIRESTEAMSURVIVAL_API const FAbilitySound* FindAbility(FName Skill);
    /** Attack style ("sword", "axes", "gunblade"...) or strike name -> weapon id. */
    CIRESTEAMSURVIVAL_API FName WeaponFor(FName SkillOrStyle);

    /** ACireSpellVisual hook: plays the resolved one-shots for a presentation cue. False keeps the legacy sound. */
    CIRESTEAMSURVIVAL_API bool PlaySpellCue(UWorld* World, FName Skill, ECireSpellCue Cue, const FVector& From, const FVector& To, float Scale);
    /** Loops on a spell visual's own audio component: projectile flight ("projectile") or a persistent area ("area"). */
    CIRESTEAMSURVIVAL_API bool StartLoop(UAudioComponent* Component, FName Skill, FName Slot);

    /** Nearest living character within Radius of a point (the caster of a cue, or its target). */
    CIRESTEAMSURVIVAL_API ACharacter* CharacterNear(UWorld* World, const FVector& At, float Radius);
    /** Weapon of a character from its basic attack style (heroes) or armour class (monsters). */
    CIRESTEAMSURVIVAL_API FName WeaponOf(const ACharacter* Character);

#if !UE_BUILD_SHIPPING
    /** Native checks: table covers every Ability DB entry for every slot, every cue exists and has a playable fallback. */
    CIRESTEAMSURVIVAL_API bool RunSmoke(UWorld* World, int32& OutChecks);
#endif
}

/** Per-world state for events detected by polling (outcomes, reactions, deaths, casts, UI). */
class CIRESTEAMSURVIVAL_API FCireSoundEventTracker
{
public:
    void Tick(UCireAudioSubsystem& Audio, float DeltaSeconds);
    void Reset();
    FString StatsLine() const;
    int32 Blocks = 0, Deflects = 0, Avoids = 0, Reactions = 0, Deaths = 0, CastLoops = 0, UiEvents = 0, Spells = 0;
private:
    uint32 LastSequence = 0;
    bool bPrimed = false;
    TMap<TWeakObjectPtr<AActor>, float> LastHealth;      // monsters: fraction of max
    TMap<TWeakObjectPtr<AActor>, FVector> LastLocation;
    TMap<TWeakObjectPtr<AActor>, FName> DeathKey;
    TMap<TWeakObjectPtr<AActor>, bool> HeroDead;
    TMap<TWeakObjectPtr<AActor>, TWeakObjectPtr<UAudioComponent>> Casting;
    TMap<TWeakObjectPtr<AActor>, FString> CastingId;
    FString LastNotice;
    bool bWasReady = false;
    int32 ReadyWave = -1;
    float ScanTimer = 0.f;
    void TickCombatEvents(UCireAudioSubsystem& Audio);
    void TickUnits(UCireAudioSubsystem& Audio, float DeltaSeconds);
    void TickUi(UCireAudioSubsystem& Audio);
};
