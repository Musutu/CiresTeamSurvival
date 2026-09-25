#pragma once
// progression-shop: data-driven items (Content/Data/Items.json), the replicated
// hero inventory, item actives/consumables, teleport-to-base, and the small
// combat hooks the hero/combat code calls. Rules live in Rules/CireItemRules.*.
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Rules/CiresRules.h"
#include "Rules/CireItemRules.h"
#include "CireLoot.h"
#include "CireItems.generated.h"

class ACireHero;
class ACireGameMode;
class FCireKeybindings;
class UStaticMeshComponent;
class UPointLightComponent;

struct CIRESTEAMSURVIVAL_API FCireItemData
{
    Cires::Items::Catalog Catalog;
    Cires::Items::ShopRules Shop;
    Cires::Items::TeleportRules Teleport;
    // role key (tank/physical/caster/support) -> [starting, core, situational]
    TMap<FString, TArray<TArray<FName>>> Recommended;
    TMap<FName, FString> UseText;
    TMap<FName, TArray<FString>> PassiveText;
    TArray<FName> Order; // catalog order for the shop grid
    TMap<FName, FString> EffectLine;          // items-v2: one-line effect per item ("effect")
    Cires::Items::ManaRules Mana;             // items-v2: "manaEconomy"
    TArray<FString> RoleOrder;                // recommended keys in file order
    FString Error;
    bool bValid = false;
};

namespace CireItems
{
    CIRESTEAMSURVIVAL_API const FCireItemData& Get();
    CIRESTEAMSURVIVAL_API bool Reload();
    CIRESTEAMSURVIVAL_API bool ParseJson(const FString& Json, FCireItemData& Out, FString& Error);
    CIRESTEAMSURVIVAL_API const Cires::Items::ItemDef* Find(FName Id);
    CIRESTEAMSURVIVAL_API FString DisplayName(FName Id);
    // Recommended-build key for a champion: tank, physical, caster or support.
    CIRESTEAMSURVIVAL_API FString RoleKey(const ACireHero* Hero);
    // Legacy ServerAction(4, 0..3) purchases map onto catalog items.
    CIRESTEAMSURVIVAL_API FName LegacyItem(int32 Index);
    CIRESTEAMSURVIVAL_API class UCireInventory* InventoryOf(const AActor* Actor);
    CIRESTEAMSURVIVAL_API Cires::Items::Totals TotalsOf(const ACireHero* Hero);
    CIRESTEAMSURVIVAL_API bool IsBasicAttack(const AActor* Source, const FString& AbilityName);

    // ---- stat pipeline hooks (ACireHero::Recalculate / AttackDamage / BasicAttack / Tick) ----
    CIRESTEAMSURVIVAL_API void AddAttributes(const ACireHero* Hero, Cires::StatBlock& Attributes);
    // Returns the pure cooldown reduction to feed CalculateStats (base + items, capped).
    CIRESTEAMSURVIVAL_API double CooldownReductionFor(ACireHero* Hero, float CurrentCDR);
    CIRESTEAMSURVIVAL_API void ApplyDerived(ACireHero* Hero);
    CIRESTEAMSURVIVAL_API float AttackDamageBonus(const ACireHero* Hero);
    CIRESTEAMSURVIVAL_API float AttackSpeedBonus(const ACireHero* Hero);   // fraction (0.12 = +12%)
    CIRESTEAMSURVIVAL_API float MoveSpeedMultiplier(const ACireHero* Hero);
    CIRESTEAMSURVIVAL_API void ApplyRegen(ACireHero* Hero, float DeltaSeconds);

    // ---- combat hooks (CireCombatEvents / ACireHero::TakeDamage) ----
    CIRESTEAMSURVIVAL_API float ModifyOutgoingDamage(AActor* Source, AActor* Target, float Amount, const FString& AbilityName);
    CIRESTEAMSURVIVAL_API void OnDamageDealt(AActor* Source, AActor* Target, float Applied, const FString& AbilityName);
    CIRESTEAMSURVIVAL_API float ModifyIncomingDamage(ACireHero* Hero, AActor* Causer, const FString& AbilityName, float Amount);
    CIRESTEAMSURVIVAL_API void OnHeroDamaged(ACireHero* Hero, AActor* Causer, const FString& AbilityName, float Taken);
    CIRESTEAMSURVIVAL_API float HealingMultiplier(const ACireHero* Source, const FString& AbilityName = FString());

    // ---- shop access / teleport (ServerAction 4 and 8) ----
    CIRESTEAMSURVIVAL_API Cires::Items::ShopAccess ShopAccessFor(const ACireHero* Hero);
    CIRESTEAMSURVIVAL_API void RequestTeleport(ACireHero* Hero);
    CIRESTEAMSURVIVAL_API void BotShop(ACireHero* Hero);
    // ---- key map (CireKeybindings) ----
    CIRESTEAMSURVIVAL_API FName BeltAction(int32 Index);   // "UseBelt1".."UseBelt3"
    CIRESTEAMSURVIVAL_API FName ItemAction(int32 Index);   // "UseItem1".."UseItem6"
    // Action-bar entries "item:<id>" resolve to the equipment slot holding that item.
    CIRESTEAMSURVIVAL_API FString ItemSlotId(FName ItemId);
    CIRESTEAMSURVIVAL_API bool ParseItemSlotId(const FString& SlotId, FName& OutItem);
    // Automatic bar-1 slots 9..12 (keys 7,8,9,0): the owned active items in bag order.
    CIRESTEAMSURVIVAL_API FString AutoActionBarItem(const ACireHero& Hero, int32 Ordinal);
    CIRESTEAMSURVIVAL_API int32 ResolveItemSlot(const FCireKeybindings& Bindings, const ACireHero& Hero, FName Slot);
    // Phase change: cancel channels and end every shop visit.
    CIRESTEAMSURVIVAL_API void OnPhaseChanged(ACireGameMode* Mode, int32 NewPhase);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
    CIRESTEAMSURVIVAL_API bool RunV2Smoke(ACireGameMode* Mode);   // items-v2 (CireItemsV2Tests.cpp)
#endif

    // ---- items-v2 (CireItemsV2.cpp) ----
    /** The champion that owns a unit: itself, a summon's/pet's owner, a construct's source. */
    CIRESTEAMSURVIVAL_API ACireHero* OwningHero(const AActor* Unit);
    /** Mana economy: base regen per second (without item regen) and the level cost multiplier. */
    CIRESTEAMSURVIVAL_API float BaseManaRegen(const ACireHero* Hero, float RegenMultiplier = 1.f);
    CIRESTEAMSURVIVAL_API float ManaCostScale(const ACireHero* Hero);
    /** "Not enough mana (32 / 48)." plus the HUD flash counter (server). */
    CIRESTEAMSURVIVAL_API FString NoteShortfall(const ACireHero* Hero, float NeedMana, float NeedEnergy);
    /** Every paid ability cast (CireSkillShop::ApplyCastLevel): mana refund, ultimate upgrade. */
    CIRESTEAMSURVIVAL_API void OnAbilityCast(ACireHero* Hero, const FString& Id, float ManaSpent);
    CIRESTEAMSURVIVAL_API int32 ConstructLimitBonus(const AActor* Source);
    CIRESTEAMSURVIVAL_API float ConstructHealthMultiplier(const AActor* Source);
    CIRESTEAMSURVIVAL_API float ConstructShieldFraction(const AActor* Source);
    /** Summons and pets: health/damage multiplier from the owner's path unique (pets may call this too). */
    CIRESTEAMSURVIVAL_API float SummonMultiplier(const AActor* OwnerOrSummon);
    CIRESTEAMSURVIVAL_API float AreaRadiusMultiplier(const AActor* Source);
    CIRESTEAMSURVIVAL_API float ControlDurationMultiplier(const AActor* Source);
    CIRESTEAMSURVIVAL_API bool IsAreaAbility(const FString& AbilityName);
    CIRESTEAMSURVIVAL_API bool IsControlled(const AActor* Unit);
    /** Dodge-roll charges (1 + boots). */
    CIRESTEAMSURVIVAL_API int32 MaxDodgeCharges(const ACireHero* Hero);
    CIRESTEAMSURVIVAL_API void OnDodgeRoll(ACireHero* Hero);
    /** Absorb shield on a champion (party shields, ultimate upgrades). Returns the shield now held. */
    CIRESTEAMSURVIVAL_API float GrantBarrier(ACireHero* Hero, float Amount, float Duration, AActor* Source);
    /** Timed stat buff: an item id with stats (party buffs) or "ult:<abilityId>" (ultimate upgrade). */
    CIRESTEAMSURVIVAL_API void GrantStatBuff(ACireHero* Hero, FName BuffId, float Duration, AActor* Source);
    CIRESTEAMSURVIVAL_API bool BuffStats(FName BuffId, Cires::Items::StatBlock& Out);
    /** Upgrade/effect helpers shared with CireUltimateUpgrades. */
    CIRESTEAMSURVIVAL_API TArray<ACireHero*> AlliesNear(const ACireHero* Source, FVector Center, float Radius);
    CIRESTEAMSURVIVAL_API TArray<AActor*> EnemiesNear(const ACireHero* Source, FVector Center, float Radius);
}

USTRUCT()
struct CIRESTEAMSURVIVAL_API FCireItemSlot
{
    GENERATED_BODY()
    UPROPERTY() FName Id;
    UPROPERTY() int32 Charges = 0;
    UPROPERTY() float ReadyAt = 0;     // server world time
    UPROPERTY() float Cooldown = 0;    // full duration of the last cooldown (UI sweep)
};

// progression-shop: a skill's Skill Shop level (1 = learned; no cap).
USTRUCT()
struct CIRESTEAMSURVIVAL_API FCireSkillRank
{
    GENERATED_BODY()
    UPROPERTY() FString Id;
    UPROPERTY() int32 Level = 1;
};

USTRUCT()
struct CIRESTEAMSURVIVAL_API FCireTimedBuff
{
    GENERATED_BODY()
    UPROPERTY() FName Id;
    UPROPERTY() float EndsAt = 0;
    UPROPERTY() float Duration = 0;
};

UENUM()
enum class ECireShopAction : uint8 { Buy, Sell, Undo, Use, Loot, Teleport, Swap, Announce, SkillBuy, SkillLevel };

USTRUCT()
struct CIRESTEAMSURVIVAL_API FCireShopFeedback
{
    GENERATED_BODY()
    UPROPERTY() ECireShopAction Action = ECireShopAction::Buy;
    UPROPERTY() bool bOk = false;
    UPROPERTY() FName ItemId;
    UPROPERTY() int32 Slot = -1;
    UPROPERTY() bool bBelt = false;
    UPROPERTY() int32 GoldDelta = 0;
    UPROPERTY() FString Message;
};

// Server-side heal/mana over time from potions; not replicated (health/mana are).
struct FCireRestore
{
    float PerSecond = 0;
    float EnergyPerSecond = 0;
    float Remaining = 0;
    bool bMana = false;
};

UCLASS(ClassGroup=(Cire))
class CIRESTEAMSURVIVAL_API UCireInventory : public UActorComponent
{
    GENERATED_BODY()
public:
    UCireInventory();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // ---- replicated state ----
    UPROPERTY(ReplicatedUsing=OnRep_Items) TArray<FCireItemSlot> Equipment;
    UPROPERTY(ReplicatedUsing=OnRep_Items) TArray<FCireItemSlot> Belt;
    UPROPERTY(ReplicatedUsing=OnRep_Items) TArray<FCireTimedBuff> Buffs; // items-v2: stat buffs change totals on clients too
    // items-v2: absorb shield (party shields, ultimate upgrades) and the "not enough mana" flash.
    UPROPERTY(Replicated) float BarrierHP = 0;
    UPROPERTY(Replicated) float BarrierMax = 0;
    UPROPERTY(Replicated) float BarrierEndsAt = 0;
    UPROPERTY(Replicated) int32 ResourceFailSerial = 0;
    UPROPERTY(Replicated) uint8 ResourceFailKind = 0;   // 1 mana, 2 energy
    UPROPERTY(Replicated) float ResourceFailNeed = 0;
    UPROPERTY(Replicated) float TeleportChannelStart = -1;
    UPROPERTY(Replicated) float TeleportChannelEnd = -1;
    UPROPERTY(Replicated) float TeleportReadyAt = 0;
    UPROPERTY(Replicated) int32 PrimaryTomePoints = 0;   // stats-window derivation
    UPROPERTY(Replicated) int32 LootScore = 0;
    UPROPERTY(Replicated) int32 UndoDepth = 0;
    UPROPERTY(Replicated) bool bShopVisit = false;
    UPROPERTY(Replicated) TArray<FCireSkillRank> SkillRanks;   // Skill Shop levels

    // ---- client requests (owning client only) ----
    UFUNCTION(Server, Reliable) void ServerBuy(FName ItemId);
    UFUNCTION(Server, Reliable) void ServerSell(int32 Index, bool bBeltSlot);
    UFUNCTION(Server, Reliable) void ServerUndo();
    UFUNCTION(Server, Reliable) void ServerUse(int32 Index, bool bBeltSlot);
    UFUNCTION(Server, Reliable) void ServerSwap(int32 From, int32 To);
    UFUNCTION(Server, Reliable) void ServerShopOpen(bool bOpen);
    UFUNCTION(Client, Reliable) void ClientFeedback(const FCireShopFeedback& Feedback);
    UFUNCTION(Client, Reliable) void ClientLootReport(const FCireLootReport& Report);
    UFUNCTION(Server, Reliable) void ServerBuySkill(const FString& SkillId);
    UFUNCTION(Server, Reliable) void ServerLevelSkill(const FString& SkillId);
    // Host/standalone only, before the first wave: 0 Classic Draft, 1 Skill Shop.
    UFUNCTION(Server, Reliable) void ServerSetProgressionMode(uint8 NewMode);
    // Kill bounty feedback ("+3g" over the kill; Kind = Cires::Items::BountyKind).
    UFUNCTION(Client, Unreliable) void ClientGoldGain(int32 Amount, FVector_NetQuantize Where, uint8 Kind);

    // ---- authoritative operations (also used by bots, loot and tests) ----
    bool Buy(FName ItemId, FString& Message);
    bool SellSlot(int32 Index, bool bBeltSlot, FString& Message);
    bool UndoLast(FString& Message);
    bool UseSlot(int32 Index, bool bBeltSlot, FString& Message);
    bool SwapSlots(int32 From, int32 To);
    // Loot: places an item (or converts it to gold if there is no room). Returns false when converted.
    bool GrantItem(FName ItemId, int32& ConvertedGold, int32* OutSlot = nullptr, bool* OutBelt = nullptr);
    bool HasRoomFor(FName ItemId) const;
    void BeginShopVisit(bool bOpen);
    void EndShopVisit();
    void Teleport();
    void InterruptTeleport(const TCHAR* Reason);
    void CompleteTeleportNow(); // tests
    bool IsChanneling() const { return TeleportChannelEnd >= 0; }
    float TeleportCooldownRemaining() const;
    void ApplyPrimaryTome(int32 Points);

    Cires::Items::Inventory ToRules() const;
    void FromRules(const Cires::Items::Inventory& Rules);
    const Cires::Items::Totals& Totals() const;
    void Invalidate() { bTotalsDirty = true; }
    ACireHero* Hero() const;
    double Now() const;

    // Local (client) feedback queue consumed by the shop UI.
    TArray<FCireShopFeedback> PendingFeedback;
    TArray<FCireLootReport> PendingLoot;
    struct FGoldGain { int32 Amount = 0; FVector Where = FVector::ZeroVector; uint8 Kind = 0; };
    TArray<FGoldGain> PendingGold;
    float BotSkillTimer = 0.f;   // Skill Shop: bots shop between waves (server)
    // Server-only runtime
    float ItemCDRApplied = 0;
    int32 BasicHitCounter = 0;
    float LowHealthReadyAt = 0;
    float BarrierUntil = 0, BarrierReduction = 0;
    float HasteUntil = 0, HasteAmount = 0;
    TArray<FCireRestore> Restores;
    // Sends a feedback line to the owning player (toast / banner in the shop UI).
    void SendFeedback(ECireShopAction Action, bool bOk, FName ItemId, int32 Slot, bool bBeltSlot, int32 GoldDelta, const FString& Message);
private:
    UFUNCTION() void OnRep_Items();
    void AfterChange();
    bool ApplyEffect(const Cires::Items::Effect& Effect, FName ItemId, FString& Message);
    Cires::Items::ShopSession Session;
    FVector ChannelOrigin = FVector::ZeroVector;
    mutable Cires::Items::Totals CachedTotals;
    mutable bool bTotalsDirty = true;
    float AuraTimer = 0;
};

// Lantern ward from Watcher's Lantern: slows and marks monsters of its lane.
UCLASS()
class CIRESTEAMSURVIVAL_API ACireLanternWard : public AActor
{
    GENERATED_BODY()
public:
    ACireLanternWard();
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;
    UPROPERTY(Replicated) int32 TeamId = -1;
    UPROPERTY(Replicated) float Radius = 700;
    UPROPERTY(Replicated) float ExpiresAt = 0;
    float SlowPercent = 20, MarkPercent = 10;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Post;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Flame;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Light;
    // Damage bonus against a monster standing inside any of the team's wards.
    static float MarkBonus(const AActor* Target, int32 AttackerTeam);
};
