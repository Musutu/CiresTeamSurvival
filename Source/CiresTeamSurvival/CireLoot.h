#pragma once
// progression-shop: data-driven bonus drops (Content/Data/LootTables.json), the
// glowing auto-pickup loot chest, team-fair distribution, and the challenge-pack
// progression schedule / NPC pause helpers. See Docs/Progression.md.
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Rules/CireItemRules.h"
#include "CireLoot.generated.h"

class ACireGameMode;
class ACireHero;
class ACireMonster;
class UStaticMeshComponent;
class UPointLightComponent;
class UProceduralMeshComponent;
class USceneComponent;

struct FCireLootSource
{
    int32 MinTier = 1;
    int32 MinRound = 1;
    FString Table;
};

struct CIRESTEAMSURVIVAL_API FCireLootData
{
    TMap<FString, Cires::Items::LootTable> Tables;
    Cires::Items::LootScaling Scaling;
    Cires::Items::PackSchedule Schedule;
    TArray<FCireLootSource> PackCompletion, PackLeader, LaneBoss;
    float PickupRadius = 320.f;
    bool bAutoCollectOnPrep = true;
    // Personal loot (default): every eligible player gets an independent roll and a chest only they see.
    bool bPersonal = true;
    float EligibleRadius = 4000.f;   // alive teammates this close to the kill are eligible even without damage
    double PersonalFactor = 1.0;     // team-wide tome/item multiplier (1 = same team total as one shared roll)
    bool bBotsAutoLoot = true;
    // Gold economy (Eric's playtest-2 ruling; LootTables.json "economy").
    Cires::Items::Economy Economy;
    bool bLootGoldInMobValues = true;
    FString Error;
    bool bValid = false;
};

// One line of the replicated chest manifest ("+85 gold", "Nightfall Reaver -> Ember 3").
USTRUCT()
struct CIRESTEAMSURVIVAL_API FCireLootLine
{
    GENERATED_BODY()
    UPROPERTY() FName ItemId;       // None for gold/xp lines; tome lines carry the tome item id
    UPROPERTY() FString Text;       // "+3 Strength (primary attribute)", "Bone Dagger"
    UPROPERTY() uint8 Kind = 0;     // Cires::Items::LootKind
    UPROPERTY() int32 Amount = 0;   // gold, XP or tome points
    UPROPERTY() int32 Rarity = 0;   // 0 common .. 3 legendary
    UPROPERTY() int32 Slot = -1;    // bag/belt slot the item landed in (-1: converted or none)
    UPROPERTY() bool bBelt = false;
    UPROPERTY() int32 ConvertedGold = 0;
};

// What one player received from one (or several auto-collected) personal chests.
USTRUCT()
struct CIRESTEAMSURVIVAL_API FCireLootReport
{
    GENERATED_BODY()
    UPROPERTY() FString Source;     // "Gravemaw, Pack Leader"
    UPROPERTY() FString Why;        // "Personal loot: you helped clear the Tier 3 pack"
    UPROPERTY() TArray<FCireLootLine> Lines;
    UPROPERTY() int32 Gold = 0;
    UPROPERTY() int32 Experience = 0;
    UPROPERTY() int32 Chests = 1;
    UPROPERTY() int32 Rarity = 0;
    UPROPERTY() bool bAutoCollected = false;
};

namespace CireLoot
{
    CIRESTEAMSURVIVAL_API const FCireLootData& Get();
    CIRESTEAMSURVIVAL_API bool Reload();
    CIRESTEAMSURVIVAL_API bool ParseJson(const FString& Json, FCireLootData& Out, FString& Error);
    CIRESTEAMSURVIVAL_API const Cires::Items::LootTable* TableFor(const TArray<FCireLootSource>& Sources, int32 Tier, int32 Round);
    // Rarity 0 common .. 3 legendary: drives chest glow colour.
    CIRESTEAMSURVIVAL_API int32 BundleRarity(const Cires::Items::LootBundle& Bundle);
    CIRESTEAMSURVIVAL_API class ACireLootDrop* SpawnDrop(ACireGameMode* Mode, int32 Team, FVector Location,
        const Cires::Items::LootBundle& Bundle, int32 Tier, const FString& Label, uint64 Seed);
    // Called by ACireGameMode::MonsterKilled. Rolls pack/leader/boss tables and spawns chests.
    CIRESTEAMSURVIVAL_API void OnMonsterKilled(ACireGameMode* Mode, ACireMonster* Monster, ACireHero* Killer, bool bPackCompleted);
    // Gold/XP to every team member; tomes and items rotate to the lowest loot score.
    CIRESTEAMSURVIVAL_API TArray<FCireLootLine> Distribute(ACireGameMode* Mode, int32 Team,
        const Cires::Items::LootBundle& Bundle, const FString& Source, uint64 Seed);
    // Opens every unopened chest (e.g. when prep begins, so no loot is lost to phase changes);
    // each owner gets one merged "auto-collected" report.
    CIRESTEAMSURVIVAL_API int32 CollectAll(ACireGameMode* Mode, TMap<TWeakObjectPtr<ACireHero>, FCireLootReport>* OutReports = nullptr);
    // Personal loot: records who damaged a loot source (a pack or a boss) for eligibility.
    CIRESTEAMSURVIVAL_API void NoteContribution(AActor* Source, AActor* Target);
    // Eligible teammates: contributed to the source, or alive within EligibleRadius of the kill.
    CIRESTEAMSURVIVAL_API TArray<ACireHero*> EligibleFor(ACireGameMode* Mode, ACireMonster* Monster, FVector Location);
    // Grants one player's bundle (gold, XP, tomes, items) and returns what they received.
    CIRESTEAMSURVIVAL_API FCireLootReport GrantPersonal(ACireHero* Hero, const Cires::Items::LootBundle& Bundle,
        const FString& Source, const FString& Why, bool bSendReport);
    CIRESTEAMSURVIVAL_API class ACireLootDrop* SpawnPersonalDrop(ACireGameMode* Mode, ACireHero* Owner, FVector Location,
        const Cires::Items::LootBundle& Bundle, int32 Tier, const FString& Label, const FString& Why, uint64 Seed);
    CIRESTEAMSURVIVAL_API void ForgetContributions(ACireGameMode* Mode);
    // Kill bounty: mob value by wave, x2 armored, x10 boss, x10 pack unit, x100 Pack Leader.
    CIRESTEAMSURVIVAL_API Cires::Items::BountyKind BountyKindOf(const ACireMonster* Monster);
    CIRESTEAMSURVIVAL_API int32 KillBounty(ACireGameMode* Mode, const ACireMonster* Monster, float RewardMultiplier = 1.f);
    // Pays the bounty: wave kills to every teammate, pack kills to eligible teammates. Returns gold per recipient.
    CIRESTEAMSURVIVAL_API int32 AwardKillGold(ACireGameMode* Mode, ACireMonster* Monster, float RewardMultiplier = 1.f);
    CIRESTEAMSURVIVAL_API int32 MobValueNow(const UWorld* World);
    CIRESTEAMSURVIVAL_API bool SaveEconomy(FString* Error = nullptr);
    CIRESTEAMSURVIVAL_API Cires::Items::Economy& MutableEconomy();
}

namespace CireProgression
{
    // Replaces ACireGameMode::SpawnPacks: spawns every bay unlocked for the current round/wave.
    CIRESTEAMSURVIVAL_API void SpawnPacks(ACireGameMode* Mode, int32 WaveInCycle);
    // Called after each wave spawn: bays that unlock mid-cycle appear and are announced.
    CIRESTEAMSURVIVAL_API void OnWaveSpawned(ACireGameMode* Mode, int32 WaveInCycle);
    CIRESTEAMSURVIVAL_API void SpawnBay(ACireGameMode* Mode, int32 Team, int32 Bay, int32 Tier);
    // Prep / arena / recovery pause: freezes NPC timers so they resume exactly.
    CIRESTEAMSURVIVAL_API void PauseNPC(ACireMonster* Monster, double Now);
    CIRESTEAMSURVIVAL_API void ResumeNPC(ACireMonster* Monster, double Now);
    CIRESTEAMSURVIVAL_API bool IsPaused(const ACireMonster* Monster);
    // ChangePhase hook: loot auto-collection on prep and item phase cleanup.
    CIRESTEAMSURVIVAL_API void OnPhaseChanged(ACireGameMode* Mode, int32 NewPhase);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
#endif
}

UCLASS()
class CIRESTEAMSURVIVAL_API ACireLootDrop : public AActor
{
    GENERATED_BODY()
public:
    ACireLootDrop();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;
    // Only the owner (or the server's auto-collect, Opener == nullptr) can open a personal chest.
    bool Open(ACireHero* Opener, FCireLootReport* OutReport = nullptr);
    bool CanBeOpenedBy(const ACireHero* Opener) const;

    UPROPERTY(Replicated) int32 TeamId = -1;
    UPROPERTY(Replicated) int32 Rarity = 0;
    UPROPERTY(Replicated) int32 Tier = 1;
    UPROPERTY(Replicated) FString Label;
    UPROPERTY(Replicated) FString Why;
    UPROPERTY(Replicated) TObjectPtr<ACireHero> OwnerHero; // personal chest owner (null = legacy team chest)
    UPROPERTY(ReplicatedUsing=OnRep_Opened) bool bOpened = false;
    UPROPERTY(Replicated) TArray<FCireLootLine> Manifest; // filled on open for the pickup banner
    Cires::Items::LootBundle Bundle;                       // server only
    uint64 Seed = 0;
    float OpenedAt = -1;
    float SpawnedAt = 0;

    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Hinge;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Lid;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BandA;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BandB;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Lock;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Glow;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Light;
    static FLinearColor RarityColor(int32 Rarity);
private:
    UFUNCTION() void OnRep_Opened();
    void BuildGlow();
    int32 BuiltRarity = -1;
};
