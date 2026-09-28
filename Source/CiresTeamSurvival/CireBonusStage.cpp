// bonus-loot (playtest 6): Bonus Loot Stages. See CireBonusStage.h and Docs/Items.md.
#include "CireBonusStage.h"
#include "CireGame.h"
#include "CireItems.h"
#include "CireItemsPvP.h"
#include "CireLoot.h"
#include "CireMonsterExpansion.h"
#include "CireRaces.h"
#include "CireSummon.h"
#include "CireWaves.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireBonusStage, Log, All);

namespace CI = Cires::Items;

namespace
{
struct FStage
{
    CireBonusStage::FStageInfo Info;
    TArray<TWeakObjectPtr<ACireMonster>> Creatures;
    FVector LastCatch[2] = {FVector::ZeroVector, FVector::ZeroVector};
    float BeganAt = 0;
    FString ReplacedLabel;
    uint64 Seed = 0;
};
TMap<TWeakObjectPtr<UWorld>, TArray<FStage>> GStages;
int32 GStageCounter = 0;

uint64 Mix(uint64 X)
{
    X += 0x9E3779B97F4A7C15ull;
    X = (X ^ (X >> 30)) * 0xBF58476D1CE4E5B9ull;
    X = (X ^ (X >> 27)) * 0x94D049BB133111EBull;
    return X ^ (X >> 31);
}
// Uniform [0, 1) from a seed.
double Unit(uint64 Seed) { return static_cast<double>(Mix(Seed) >> 11) * (1.0 / 9007199254740992.0); }
int32 Pick(uint64 Seed, int32 Count) { return Count <= 0 ? 0 : static_cast<int32>(Mix(Seed) % static_cast<uint64>(Count)); }

float StageNow(const UWorld* World) { return World ? World->GetTimeSeconds() : 0.f; }

TArray<FStage>* StagesOf(const UWorld* World) { return World ? GStages.Find(TWeakObjectPtr<UWorld>(const_cast<UWorld*>(World))) : nullptr; }

bool Alive(const TWeakObjectPtr<ACireMonster>& M) { return M.IsValid() && !M->IsActorBeingDestroyed() && M->Health > 0; }

FName ToName(const std::string& Id) { return FName(UTF8_TO_TCHAR(Id.c_str())); }

void PayLane(ACireGameMode* Mode, FStage& Stage, int32 Lane)
{
    Stage.Info.bPaid[Lane] = true;
    const FCireBonusStageRules& Rules = CireLoot::Get().BonusStage;
    const int32 Spawned = FMath::Max(1, Stage.Info.Spawned[Lane]);
    const float Caught = FMath::Clamp(static_cast<float>(Stage.Info.Caught[Lane]) / Spawned, 0.f, 1.f);
    const FString Tier = CireBonusStage::TierName(Stage.Info.Tier);
    UE_LOG(LogCireBonusStage, Display, TEXT("CIRE_BONUS_STAGE_END stage=%d lane=%d tier=%s caught=%d/%d value=%d"), Stage.Info.Id, Lane, *Tier,
        Stage.Info.Caught[Lane], Stage.Info.Spawned[Lane], Stage.Info.WaveValue);
    if (Caught <= 0) return;
    const FString Label = FString::Printf(TEXT("Bonus Loot Stage: %s tier"), *Tier);
    for (ACireHero* Hero : Mode->Heroes)
    {
        if (!IsValid(Hero) || Hero->IsA<ACireSummon>() || !Hero->bDrafted || Hero->TeamId != Lane) continue;
        const uint64 Seed = Stage.Seed ^ Mix(static_cast<uint64>(Hero->GetUniqueID()) + 17 * static_cast<uint64>(Lane + 1));
        const ECireBonusOutcome Outcome = CireBonusStage::RollOutcome(Rules, Stage.Info.Tier, Seed);
        const TArray<CI::LootBundle> Bundles = CireBonusStage::BuildReward(Rules, Stage.Info.Tier, Outcome, Stage.Info.WaveValue, Caught, Seed ^ 0x5bd1e995ull, Hero);
        const FString Why = FString::Printf(TEXT("Personal loot: your lane caught %d of %d treasure creatures (%s tier, %s)"),
            Stage.Info.Caught[Lane], Stage.Info.Spawned[Lane], *Tier, CireBonusStage::OutcomeName(Outcome));
        // The chest lands at the champion's feet (in front of him) so it is never lost down the road.
        const FVector Base = Hero->bDead ? Stage.LastCatch[Lane] : Hero->GetActorLocation() + Hero->GetActorForwardVector() * 160.f;
        for (int32 Chest = 0; Chest < Bundles.Num(); ++Chest)
        {
            if (Bundles[Chest].Empty()) continue;
            const FVector Where = Base + FVector(0, (Chest - (Bundles.Num() - 1) * .5f) * 140.f, 0);
            if (Hero->bBot && CireLoot::Get().bBotsAutoLoot) CireLoot::GrantPersonal(Hero, Bundles[Chest], Label, Why + TEXT(" (auto-looted)"), false);
            else CireLoot::SpawnPersonalDrop(Mode, Hero, Where, Bundles[Chest], static_cast<int32>(Stage.Info.Tier), Label, Why, Seed + Chest);
            ++Stage.Info.Chests[Lane];
        }
        if (!Hero->bBot) Hero->Notice = FString::Printf(TEXT("Bonus Loot Stage (%s): %d chest%s for you!"), *Tier, Bundles.Num(), Bundles.Num() == 1 ? TEXT("") : TEXT("s"));
        UE_LOG(LogCireBonusStage, Display, TEXT("CIRE_BONUS_STAGE_REWARD hero=%s tier=%s outcome=%s chests=%d caught=%.2f"), *Hero->HeroName, *Tier,
            CireBonusStage::OutcomeName(Outcome), Bundles.Num(), Caught);
    }
}
} // namespace

// ------------------------------------------------------------------ rules
const TCHAR* CireBonusStage::TierName(ECireBonusTier Tier)
{
    switch (Tier)
    {
    case ECireBonusTier::Low: return TEXT("Low");
    case ECireBonusTier::Mid: return TEXT("Mid");
    case ECireBonusTier::Rare: return TEXT("Rare");
    default: return TEXT("None");
    }
}

const TCHAR* CireBonusStage::OutcomeName(ECireBonusOutcome Outcome)
{
    switch (Outcome)
    {
    case ECireBonusOutcome::Gold: return TEXT("gold");
    case ECireBonusOutcome::Components: return TEXT("recipe components");
    case ECireBonusOutcome::Consumables: return TEXT("consumables");
    case ECireBonusOutcome::ShopItem: return TEXT("a shop item");
    case ECireBonusOutcome::PvPUnique: return TEXT("a PvP unique");
    case ECireBonusOutcome::Chests: return TEXT("treasure chests");
    default: return TEXT("nothing");
    }
}

ECireBonusTier CireBonusStage::RollTier(const FCireBonusStageRules& Rules, uint64 Seed)
{
    const float Total = FMath::Max(0.f, Rules.LowWeight) + FMath::Max(0.f, Rules.MidWeight) + FMath::Max(0.f, Rules.RareWeight);
    if (Total <= 0) return ECireBonusTier::Low;
    const double Roll = Unit(Seed) * Total;
    if (Roll < Rules.LowWeight) return ECireBonusTier::Low;
    if (Roll < Rules.LowWeight + Rules.MidWeight) return ECireBonusTier::Mid;
    return ECireBonusTier::Rare;
}

ECireBonusOutcome CireBonusStage::RollOutcome(const FCireBonusStageRules& Rules, ECireBonusTier Tier, uint64 Seed)
{
    auto Three = [Seed](float A, float B, float C, ECireBonusOutcome OA, ECireBonusOutcome OB, ECireBonusOutcome OC)
    {
        A = FMath::Max(0.f, A); B = FMath::Max(0.f, B); C = FMath::Max(0.f, C);
        if (A + B + C <= 0) return OA;
        const double Roll = Unit(Seed ^ 0xA24BAED4963EE407ull) * (A + B + C);
        return Roll < A ? OA : Roll < A + B ? OB : OC;
    };
    switch (Tier)
    {
    case ECireBonusTier::Low: return Three(Rules.LowGoldWeight, Rules.LowComponentWeight, Rules.LowConsumableWeight,
        ECireBonusOutcome::Gold, ECireBonusOutcome::Components, ECireBonusOutcome::Consumables);
    case ECireBonusTier::Mid: return Three(Rules.MidGoldWeight, Rules.MidItemWeight, Rules.MidPvPWeight,
        ECireBonusOutcome::Gold, ECireBonusOutcome::ShopItem, ECireBonusOutcome::PvPUnique);
    case ECireBonusTier::Rare: return ECireBonusOutcome::Chests;
    default: return ECireBonusOutcome::None;
    }
}

int32 CireBonusStage::WaveGoldValue(const FCireWaveDef& Wave, int32 GlobalWave)
{
    const CI::Economy& E = CireLoot::Get().Economy;
    int32 Value = 0;
    for (const FCireWaveUnit& U : Wave.Units)
    {
        const CI::BountyKind Kind = U.bBoss ? CI::BountyKind::Boss : U.bNonAttacking ? CI::BountyKind::Armored : CI::BountyKind::Mob;
        Value += FMath::Max(0, U.Count) * CI::KillGold(E, Kind, FMath::Max(1, GlobalWave), Wave.RewardMultiplier);
    }
    return FMath::Max(1, Value);
}

TArray<FName> CireBonusStage::ComponentPool()
{
    TArray<FName> Out;
    const CI::Catalog& Catalog = CireItems::Get().Catalog;
    for (const CI::ItemDef& Item : Catalog.Items)
        if (Item.Tier == CI::ItemTier::Basic && Item.Purchasable && !Catalog.BuildsInto(Item.Id).empty()) Out.Add(ToName(Item.Id));
    return Out;
}

TArray<FName> CireBonusStage::ConsumablePool()
{
    TArray<FName> Out;
    for (const CI::ItemDef& Item : CireItems::Get().Catalog.Items)
        if (Item.Tier == CI::ItemTier::Consumable && Item.Purchasable &&
            (Item.Belt || (Item.Instant && (Item.Use.Kind == CI::EffectKind::PrimaryStat || Item.Use.Kind == CI::EffectKind::Experience))))
            Out.Add(ToName(Item.Id));
    return Out;
}

TArray<FName> CireBonusStage::ShopItemPool(int32 MinCost, int32 MaxCost)
{
    TArray<FName> Out;
    for (const CI::ItemDef& Item : CireItems::Get().Catalog.Items)
        if (Item.Purchasable && !Item.Belt && !Item.Instant && Item.Tier != CI::ItemTier::Consumable &&
            Item.TotalCost >= MinCost && Item.TotalCost <= MaxCost && !CireItemsPvP::IsPvPUnique(ToName(Item.Id)))
            Out.Add(ToName(Item.Id));
    return Out;
}

TArray<CI::LootBundle> CireBonusStage::BuildReward(const FCireBonusStageRules& Rules, ECireBonusTier Tier, ECireBonusOutcome Outcome,
    int32 WaveValue, float Caught, uint64 Seed, const ACireHero* Hero)
{
    TArray<CI::LootBundle> Out;
    Caught = FMath::Clamp(Caught, 0.f, 1.f);
    if (Caught <= 0 || Tier == ECireBonusTier::None) return Out;
    const float Share = Rules.bScaleByCatch ? Caught : 1.f;
    const float Multiplier = Tier == ECireBonusTier::Mid ? Rules.MidGoldMultiplier : Rules.LowGoldMultiplier;
    auto GoldBundle = [&]()
    {
        CI::LootBundle B;
        B.Gold = FMath::Max(1, FMath::RoundToInt(FMath::Max(1, WaveValue) * Multiplier * Share));
        Out.Add(B);
    };
    // Item outcomes need a fair share of the hoard caught; otherwise the tier's gold (scaled) is paid instead.
    const bool bItems = Caught + KINDA_SMALL_NUMBER >= Rules.ItemCatchShare;
    auto AddItem = [](CI::LootBundle& B, FName Id)
    {
        const CI::ItemDef* Item = CireItems::Find(Id);
        if (!Item) return;
        if (Item->Instant && Item->Use.Kind == CI::EffectKind::PrimaryStat) B.PrimaryTomes.push_back(FMath::Max(1, FMath::RoundToInt(Item->Use.Amount)));
        else if (Item->Instant && Item->Use.Kind == CI::EffectKind::Experience) B.Experience += FMath::Max(1, FMath::RoundToInt(Item->Use.Amount));
        else B.Items.push_back(TCHAR_TO_UTF8(*Id.ToString()));
    };
    switch (Outcome)
    {
    case ECireBonusOutcome::Gold: GoldBundle(); break;
    case ECireBonusOutcome::Components:
    case ECireBonusOutcome::Consumables:
    {
        const TArray<FName> Pool = Outcome == ECireBonusOutcome::Components ? ComponentPool() : ConsumablePool();
        if (!bItems || Pool.IsEmpty()) { GoldBundle(); break; }
        CI::LootBundle B;
        const int32 Count = Outcome == ECireBonusOutcome::Components ? Rules.Components : Rules.Consumables;
        for (int32 I = 0; I < Count; ++I) AddItem(B, Pool[Pick(Seed + 31 * (I + 1), Pool.Num())]);
        Out.Add(B);
        break;
    }
    case ECireBonusOutcome::PvPUnique:
    {
        TArray<FName> Pool = CireItemsPvP::Ids();
        if (const UCireInventory* Inventory = CireItems::InventoryOf(Hero))
            Pool.RemoveAll([&](FName Id) { return Inventory->Equipment.ContainsByPredicate([&](const FCireItemSlot& S) { return S.Id == Id; }); });
        if (bItems && !Pool.IsEmpty())
        {
            CI::LootBundle B; B.Items.push_back(TCHAR_TO_UTF8(*Pool[Pick(Seed + 7, Pool.Num())].ToString()));
            Out.Add(B);
            break;
        }
        // Every PvP unique already owned: a shop item of the same value instead.
    }
    [[fallthrough]];
    case ECireBonusOutcome::ShopItem:
    {
        const TArray<FName> Pool = ShopItemPool(Rules.MidItemMinCost, Rules.MidItemMaxCost);
        if (!bItems || Pool.IsEmpty()) { GoldBundle(); break; }
        CI::LootBundle B; B.Items.push_back(TCHAR_TO_UTF8(*Pool[Pick(Seed + 13, Pool.Num())].ToString()));
        Out.Add(B);
        break;
    }
    case ECireBonusOutcome::Chests:
    {
        const int32 Chests = FMath::Clamp(Rules.bScaleByCatch ? FMath::CeilToInt(Rules.RareChests * Caught - KINDA_SMALL_NUMBER) : Rules.RareChests, 1, Rules.RareChests);
        for (int32 I = 0; I < Chests; ++I)
        {
            CI::LootBundle B;
            if (Unit(Seed + 101 * (I + 1)) < Rules.ChestSkillPointChance) B.SkillPoints = 1;
            else B.Gold = Rules.ChestGold;
            Out.Add(B);
        }
        break;
    }
    default: break;
    }
    return Out;
}

// ------------------------------------------------------------------ runtime
void CireBonusStage::Begin(ACireGameMode* Mode, int32 WaveNumber, int32 WaveValue, int32 CreaturesPerLane, int32 ForcedTier, const FString& ReplacedLabel)
{
    if (!Mode || !Mode->GetWorld()) return;
    TArray<FStage>& Stages = GStages.FindOrAdd(Mode->GetWorld());
    if (Stages.Num() > 8) Stages.RemoveAt(0, Stages.Num() - 8);
    FStage& Stage = Stages.AddDefaulted_GetRef();
    Stage.Info.Id = ++GStageCounter;
    Stage.Info.WaveNumber = WaveNumber;
    Stage.Info.WaveValue = FMath::Max(1, WaveValue);
    Stage.Info.Expected = FMath::Max(1, CreaturesPerLane);
    Stage.Seed = Mix(static_cast<uint64>(static_cast<uint32>(CireRaces::MatchSeed(Mode->GetWorld()))) ^ (static_cast<uint64>(WaveNumber) << 20) ^ static_cast<uint64>(FPlatformTime::Cycles64()));
    Stage.Info.Tier = ForcedTier >= 1 && ForcedTier <= 3 ? static_cast<ECireBonusTier>(ForcedTier) : RollTier(CireLoot::Get().BonusStage, Stage.Seed);
    Stage.BeganAt = StageNow(Mode->GetWorld());
    Stage.ReplacedLabel = ReplacedLabel;
    UE_LOG(LogCireBonusStage, Display, TEXT("CIRE_BONUS_STAGE_START stage=%d wave=%d tier=%s value=%d creatures=%d replaced=\"%s\""), Stage.Info.Id, WaveNumber,
        TierName(Stage.Info.Tier), Stage.Info.WaveValue, Stage.Info.Expected, *ReplacedLabel);
}

FString CireBonusStage::Announcement(ACireGameMode* Mode, int32 WaveNumber, int32 WaveInCycle, int32 WavesPerCycle)
{
    const TArray<FStage>* Stages = Mode ? StagesOf(Mode->GetWorld()) : nullptr;
    const FStage* Stage = Stages && !Stages->IsEmpty() ? &Stages->Last() : nullptr;
    const float Escape = Mode ? CireWaveDirector::Config(Mode->GetWorld()).Bonus.EscapeSeconds : 52.f;
    const FString Tier = Stage ? FString(TierName(Stage->Info.Tier)).ToUpper() : TEXT("LOW");
    return FString::Printf(TEXT("BONUS LOOT STAGE | %s TIER | Wave %d of %d: treasure creatures run for the castle. Their escape clock (%.0f s) starts at the first hit: catch them all!"),
        *Tier, WaveInCycle, WavesPerCycle, Escape);
}

void CireBonusStage::Register(ACireMonster* Monster)
{
    if (!IsValid(Monster) || Monster->Lane < 0 || Monster->Lane > 1) return;
    TArray<FStage>* Stages = StagesOf(Monster->GetWorld());
    if (!Stages || Stages->IsEmpty()) return;
    FStage& Stage = Stages->Last();
    // Only the newest stage, and only while it still has creatures to spawn on this lane.
    if (Stage.Info.bPaid[Monster->Lane] || Stage.Info.Spawned[Monster->Lane] >= Stage.Info.Expected) return;
    ++Stage.Info.Spawned[Monster->Lane];
    Stage.Creatures.Add(Monster);
}

bool CireBonusStage::OnCaught(ACireMonster* Monster, ACireHero* Killer)
{
    if (!IsValid(Monster)) return false;
    TArray<FStage>* Stages = StagesOf(Monster->GetWorld());
    if (!Stages) return false;
    for (FStage& Stage : *Stages)
        if (Stage.Creatures.Contains(Monster))
        {
            const int32 Lane = FMath::Clamp(Monster->Lane, 0, 1);
            ++Stage.Info.Caught[Lane];
            Stage.LastCatch[Lane] = Monster->GetActorLocation();
            Stage.Creatures.Remove(Monster);
            UE_LOG(LogCireBonusStage, Display, TEXT("CIRE_BONUS_STAGE_CAUGHT stage=%d lane=%d caught=%d/%d by=%s"), Stage.Info.Id, Lane,
                Stage.Info.Caught[Lane], Stage.Info.Spawned[Lane], Killer ? *Killer->HeroName : TEXT("-"));
            return true;
        }
    return false;
}

void CireBonusStage::Tick(ACireGameMode* Mode)
{
    TArray<FStage>* Stages = Mode ? StagesOf(Mode->GetWorld()) : nullptr;
    if (!Stages) return;
    const float Now = StageNow(Mode->GetWorld());
    for (FStage& Stage : *Stages)
        for (int32 Lane = 0; Lane < 2; ++Lane)
        {
            if (Stage.Info.bPaid[Lane]) continue;
            bool bAlive = false;
            for (const auto& M : Stage.Creatures) if (Alive(M) && M->Lane == Lane) { bAlive = true; break; }
            if (bAlive) continue;
            // Every creature spawned and none left (caught, escaped or leaked); a spawn shortfall closes after 4 minutes.
            if (Stage.Info.Spawned[Lane] >= Stage.Info.Expected || (Now - Stage.BeganAt > 240.f && !CireWaveDirector::HasPendingSpawns(Mode)))
                PayLane(Mode, Stage, Lane);
        }
}

ECireBonusTier CireBonusStage::AnnouncedTier(const UWorld* World)
{
    const auto* State = World ? World->GetGameState<ACireGameState>() : nullptr;
    if (!State || !State->Announcement.StartsWith(TEXT("BONUS LOOT STAGE | "))) return ECireBonusTier::None;
    const FString Rest = State->Announcement.Mid(19);
    return Rest.StartsWith(TEXT("RARE")) ? ECireBonusTier::Rare : Rest.StartsWith(TEXT("MID")) ? ECireBonusTier::Mid : ECireBonusTier::Low;
}

bool CireBonusStage::LatestStage(const UWorld* World, FStageInfo& Out)
{
    const TArray<FStage>* Stages = StagesOf(World);
    if (!Stages || Stages->IsEmpty()) return false;
    Out = Stages->Last().Info;
    return true;
}

int32 CireBonusStage::StageCount(const UWorld* World)
{
    const TArray<FStage>* Stages = StagesOf(World);
    return Stages ? Stages->Num() : 0;
}
