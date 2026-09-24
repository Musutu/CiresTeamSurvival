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
    FString Error;
    bool bValid = false;
};

// One line of the replicated chest manifest ("+85 gold", "Nightfall Reaver -> Ember 3").
USTRUCT()
struct CIRESTEAMSURVIVAL_API FCireLootLine
{
    GENERATED_BODY()
    UPROPERTY() FName ItemId;       // None for gold/xp/tome lines
    UPROPERTY() FString Text;
    UPROPERTY() uint8 Kind = 0;     // Cires::Items::LootKind
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
    // Opens every unopened chest (e.g. when prep begins, so no loot is lost to phase changes).
    CIRESTEAMSURVIVAL_API int32 CollectAll(ACireGameMode* Mode);
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
    bool Open(ACireHero* Opener);

    UPROPERTY(Replicated) int32 TeamId = -1;
    UPROPERTY(Replicated) int32 Rarity = 0;
    UPROPERTY(Replicated) int32 Tier = 1;
    UPROPERTY(Replicated) FString Label;
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
