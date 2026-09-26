#pragma once
// monster-expansion: the unused creatures of the purchased creature packs as new units (Content/Data/Bestiary.json), the
// Rare Spawn and the Bonus Loot Wave (Waves.json "rareSpawn" / "bonusWave", CireWaves.h). See Docs/MonsterExpansion.md.
//
//  - Bestiary.json archetypes are merged into the NPC database after Races.json (CireNPCArchetypes::Reload). Their Fab
//    bodies come from RaceMeshes.fabx.json; without the packs they draw their `fallback` archetype's body.
//  - Race variants: every `every`-th unit of a race slot in a live wave spawns as the variant creature instead.
//  - Rare Spawn (SpecialSpawn 1): tougher, rare-coloured skin/rim/plate, an aura, "Rare" name, a sting, `bounty` mob values
//    and a personal rare chest (LootTables.json sources.rareSpawn).
//  - Bonus Loot creatures (SpecialSpawn 2): never attack, bolt from nearby champions, never cost lives, escape after
//    escapeSeconds; `bounty` mob values plus a personal purse (sources.bonusWave).
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CireMonsterExpansion.generated.h"

class ACireGameMode;
class ACireMonster;
class UFXSystemComponent;
struct FCireNPCDatabase;
struct FCireRareSpawnRules;
struct FCireBonusWaveRules;

namespace CireMonsterExpansion
{
    struct FCreature
    {
        FName Id;
        TArray<FString> Kinds;          // bonus, rare, variant
        FName Race, Slot;               // race variant: every Every-th unit of Race/Slot
        int32 Every = 0;
        TMap<FName, FName> Sounds;      // attack, hit, death, spawn -> cue id
        FString Look;
    };
    CIRESTEAMSURVIVAL_API bool MergeInto(FCireNPCDatabase& Database, FString& Error);
    CIRESTEAMSURVIVAL_API const TArray<FCreature>& Creatures();
    CIRESTEAMSURVIVAL_API const FCreature* Find(FName ArchetypeId);
    /** 1 = rare, 2 = bonus. */
    CIRESTEAMSURVIVAL_API FLinearColor SpecialColor(uint8 Kind);
    /** Race variant for a wave unit (Counters: per race-slot counts within one wave). Returns the unit unchanged when none. */
    CIRESTEAMSURVIVAL_API FName VariantFor(FName ArchetypeId, TMap<FName, int32>& Counters);

    // ---- server ----
    /** Makes a configured wave unit a Rare Spawn; returns the extra size multiplier. */
    CIRESTEAMSURVIVAL_API float ApplyRare(ACireMonster* Monster, const FCireRareSpawnRules& Rules, int32 GlobalWave);
    /** Makes a configured unit a fleeing Bonus Loot creature; returns the extra size multiplier. */
    CIRESTEAMSURVIVAL_API float ApplyBonus(ACireMonster* Monster, const FCireBonusWaveRules& Rules);
    /** Kill bounty of a special spawn in mob values (0 for ordinary units). */
    CIRESTEAMSURVIVAL_API float BountyMobValues(const ACireMonster* Monster);
    /** Bonus creature behaviour (flee / march / escape). True when it handled the monster's tick. */
    CIRESTEAMSURVIVAL_API bool TickSpecial(ACireMonster* Monster, ACireGameMode* Mode, float Delta);
    /** Bonus creatures that escaped so far in this world (tests, soak summary). */
    CIRESTEAMSURVIVAL_API int32 EscapedCount(const UWorld* World);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
#endif
}

/** Client presentation of special spawns and Bestiary creatures: banners, stings, creature voices, aura, reskin refresh. */
UCLASS()
class CIRESTEAMSURVIVAL_API UCireExpansionPresenter : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UCireExpansionPresenter, STATGROUP_Tickables); }
    virtual bool IsTickable() const override;
    /** Banners/stings shown so far (tests). */
    int32 RareBanners = 0, BonusBanners = 0, SoundsPlayed = 0, Escapes = 0;
private:
    struct FSeen
    {
        float LastHealth = -1.f, LastHitAt = -10.f, LastCastStarted = -1.f;
        uint8 SwingSerial = 0, Special = 0;
        bool bDead = false;
        FVector Location = FVector::ZeroVector;
        TWeakObjectPtr<UFXSystemComponent> Aura; // CireFabVFX spawns Niagara or Cascade
        FName Archetype;
    };
    TMap<TWeakObjectPtr<ACireMonster>, FSeen> Seen;
    float LastBonusBannerAt = -100.f;
};
