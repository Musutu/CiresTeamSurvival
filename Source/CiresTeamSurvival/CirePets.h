#pragma once
// pets: persistent companions (Docs/Pets.md). A companion is an ACireSummon subclass, so every
// system that already treats summons as "not a player" (roster, lives, team elimination, loot,
// class traits, party frames, meters credit the owner) keeps doing so. Unlike a timed summon it
// never expires: it follows its owner through the match, fights by stance and orders, dies into
// a corpse the owner can revive, and returns on its own after a resummon cooldown.
//
// Everything is server-authoritative. Clients see the replicated pet actor (health, stance,
// order, ability cooldowns) and the owner's replicated pet timers (ACireHero::PetResummonAt,
// PetReviveReadyAt, PetStance) while the pet is away. Pets.json holds the data.
#include "CoreMinimal.h"
#include "CireSummon.h"
#include "Dom/JsonObject.h"
#include "CirePets.generated.h"

class ACireMonster;
class ACireGameMode;

UENUM()
enum class ECirePetStance : uint8
{
    Aggressive, // attacks enemies near itself, plus everything Defensive does
    Defensive,  // assists the owner's target and defends the owner and itself
    Passive     // never attacks on its own; only direct attack orders
};

UENUM()
enum class ECirePetOrder : uint8 { Follow, Stay, Attack };

/** Owner commands (ACireController::ServerPetCommand). */
enum class ECirePetCommand : uint8
{
    Attack, Follow, Stay, Special, Revive, StanceAggressive, StanceDefensive, StancePassive, Count
};

struct CIRESTEAMSURVIVAL_API FCirePetAbility
{
    FString Id;               // Ability Database row (name, icon, effect, cooldown, range, radius, duration)
    FString Kind;             // leap | strike | roar
    float Scaling = 0.f;      // x the owner's primary attribute
    bool bAutocast = false;   // the pet uses it by itself (not in Passive stance)
    bool bSpecial = false;    // bound to the "pet special" command
    float MinRange = 0.f;     // leap: only when at least this far from the target
    float ThreatBonus = 1.f;  // strike: extra threat multiplier
    float TauntSeconds = 0.f; // roar: monsters hit turn on the pet
};

struct CIRESTEAMSURVIVAL_API FCirePetDef
{
    FName Id;
    FString DisplayName, Family, Description, Portrait;
    float Health = 400, HealthPerLevel = 50, OwnerHealthShare = .3f;
    float Damage = 20, DamagePerLevel = 3, OwnerPrimaryScale = .4f;
    float AttackSeconds = 1.3f, AttackRange = 190, MoveSpeed = 600;
    float ThreatMultiplier = 2.f, DamageTakenMultiplier = 1.f;
    float CapsuleRadius = 44, CapsuleHalfHeight = 62;
    TArray<FCirePetAbility> Abilities;
    /** Body binding for UCireCreatureArt (motion, mesh, heightCm, yaw, animations, fallback). */
    TSharedPtr<FJsonObject> Art;
    int32 SpecialIndex() const;
    int32 AbilityIndex(const FString& AbilityId) const;
};

struct CIRESTEAMSURVIVAL_API FCirePetRules
{
    float FollowDistance = 190, Leash = 2400, Teleport = 3600, AssistRange = 2000, DefendRadius = 1100, AggressiveRadius = 900;
    float ReviveRange = 1500, ReviveHealthFraction = .5f, ReviveCooldown = 30, ResummonCooldown = 20, OrderedAbilitySeconds = 4;
    float OwnerThreatShare = .3f;
};

UCLASS()
class CIRESTEAMSURVIVAL_API ACirePet : public ACireSummon
{
    GENERATED_BODY()
public:
    ACirePet();
    virtual void Tick(float DeltaSeconds) override;
    virtual float TakeDamage(float Amount, const FDamageEvent& Event, AController* Instigator, AActor* Causer) override;
    virtual void FellOutOfWorld(const UDamageType& DamageType) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(Replicated) FName PetId;
    UPROPERTY(Replicated) ECirePetStance Stance = ECirePetStance::Defensive;
    UPROPERTY(Replicated) ECirePetOrder Order = ECirePetOrder::Follow;
    UPROPERTY(Replicated) FVector_NetQuantize StayPoint;
    /** Server time each Pets.json ability is ready again (HUD cooldown sweeps). */
    UPROPERTY(Replicated) TArray<float> AbilityReadyAt;
    UPROPERTY(Replicated) float DiedAt = 0;
    /** Bumps on every ability use; the body plays its attack pose (clients). */
    UPROPERTY(Replicated) uint8 AbilitySerial = 0;
    UPROPERTY(Replicated) int32 LastAbility = INDEX_NONE;

    const FCirePetDef* Def() const;
    bool IsCorpse() const { return bDead; }
    float CurrentDamage() const { return BasicDamage; }
    /** Recompute health, damage and speed from the owner's level and stats (keeps the health fraction). */
    void Rescale(bool bFill);
    /** Owner orders. False (with a reason) when the order is not possible. */
    bool Order_Attack(AActor* NewTarget, FString& Why);
    void Order_Follow();
    void Order_Stay();
    void SetStance(ECirePetStance NewStance);
    /** Queue an ability: the pet closes in and uses it (owner skill casts and the special command). */
    bool QueueAbility(int32 Index, AActor* AbilityTarget, float Amount, bool bIgnoreCooldown, FString& Why);
    bool Revive(float HealthFraction);
    float ServerNow() const;
    /** Threat: pet damage x ThreatMultiplier; server only. */
    float PetThreatMultiplier() const;
    /** The unit the pet is fighting (or about to), for tests and the HUD. */
    AActor* GetFightTarget() const { return Target; }
    bool IsLeaping() const { return bLeaping; }
#if !UE_BUILD_SHIPPING
    /** Tests: land a pounce now (synchronous fixtures do not advance time). */
    void DebugLand() { if (bLeaping) Land(); }
    int32 DebugPendingAbility() const { return PendingAbility; }
#endif
private:
    float BasicDamage = 0, RescaleTimer = 0, Think = 0;
    int32 PendingAbility = INDEX_NONE;
    TWeakObjectPtr<AActor> PendingTarget;
    float PendingAmount = 0, PendingUntil = 0;
    bool bLeaping = false;
    float LeapLandAt = 0, LeapAmount = 0;
    int32 LeapAbility = INDEX_NONE;
    TWeakObjectPtr<AActor> LeapTarget;
    AActor* ChooseTarget();
    bool Use(int32 Index, AActor* AbilityTarget, float Amount);
    void Land();
    void Die(AActor* Killer);
    float AbilityAmount(const FCirePetAbility& Ability) const;
};

namespace CirePets
{
    CIRESTEAMSURVIVAL_API const FCirePetRules& Rules();
    CIRESTEAMSURVIVAL_API const TArray<FCirePetDef>& All();
    CIRESTEAMSURVIVAL_API const FCirePetDef* Find(FName PetId);
    /** The companion a champion profile brings (Pets.json "owners"), or null. */
    CIRESTEAMSURVIVAL_API const FCirePetDef* ForOwner(const ACireHero* Owner);
    CIRESTEAMSURVIVAL_API bool Reload(FString* Error = nullptr);
    /** The owner's pet actor (alive or a corpse), or null when it is away. */
    CIRESTEAMSURVIVAL_API ACirePet* PetOf(const ACireHero* Owner);
    /** Server, from the owner's tick: summons the companion when it is due. */
    CIRESTEAMSURVIVAL_API void TickOwner(ACireHero* Owner, float DeltaSeconds);
    /** Server: spawn the owner's companion beside it now (replaces a corpse). */
    CIRESTEAMSURVIVAL_API ACirePet* Summon(ACireHero* Owner);
    /** Server: validated owner command. Writes the owner's Notice on failure. */
    CIRESTEAMSURVIVAL_API bool Command(ACireHero* Owner, ECirePetCommand Command, AActor* Target);
    /** Keybinding action id of a command ("PetAttack", ...). */
    CIRESTEAMSURVIVAL_API FName CommandAction(ECirePetCommand Command);
    CIRESTEAMSURVIVAL_API FText CommandName(ECirePetCommand Command);
    /** Owner skills that command the pet (Ability DB ids listed in a pet's abilities). */
    CIRESTEAMSURVIVAL_API bool IsPetSkill(const FString& SkillId);
    /** Server: the owner casts a pet skill; the pet performs it. False (Why set) keeps the cast uncharged. */
    CIRESTEAMSURVIVAL_API bool OwnerCast(ACireHero* Owner, const FString& SkillId, AActor* Target, float Amount, FString& Why);
    /**
     * Threat share (WoW hunter-pet feel): while the pet is alive, engaged on the monster and not
     * Passive, OwnerThreatShare of the owner's damage threat lands on the pet instead of the owner.
     * Returns the amount moved to the pet (already added); the caller keeps the rest.
     */
    CIRESTEAMSURVIVAL_API float ShareOwnerThreat(ACireMonster* Monster, ACireHero* Owner, float Threat);
    CIRESTEAMSURVIVAL_API FString StanceName(ECirePetStance Stance);
    // ---- owner scaling (Eric's rule: pets, summons and constructs inherit their owner's power) ----
    // Shared helpers for any owned unit (the summon/construct agent reuses them).
    /** The owner's primary attribute (STR, AGI or INT, whichever the champion's primary is). */
    CIRESTEAMSURVIVAL_API float OwnerPrimaryScale(const ACireHero* Owner);
    /** The owner's attack-speed multiplier (1 + AGI% + items + class trait + haste fields, x Battle Rhythm). */
    CIRESTEAMSURVIVAL_API float OwnerAttackSpeed(const ACireHero* Owner);
    /** A base cooldown after the owner's cooldown reduction (same curve and floor as the owner's skills). */
    CIRESTEAMSURVIVAL_API float OwnerCooldown(const ACireHero* Owner, float BaseSeconds);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
#endif
}
