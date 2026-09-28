// bonus-loot (playtest 6): native checks for Bonus Loot Stages (wave replacement, tier rolls, rewards, free skill
// points) and the ten PvP uniques. Run from CireMonsterExpansion::RunSmoke (Tools/RunExpansionChecks.py --only native).
#include "CireBonusStage.h"
#include "CireBuffs.h"
#include "CireCrowdControl.h"
#include "CireGame.h"
#include "CireItems.h"
#include "CireItemsPvP.h"
#include "CireLoot.h"
#include "CireSkillShop.h"
#include "CireWaves.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

#if !UE_BUILD_SHIPPING
DEFINE_LOG_CATEGORY_STATIC(LogCireBonusStageTests, Log, All);

namespace CI = Cires::Items;

bool CireBonusStage::RunSmoke(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    bool bPass = true; int32 Checks = 0;
    auto Check = [&](bool bValue, const FString& Why) { ++Checks; if (!bValue) { bPass = false; UE_LOG(LogCireBonusStageTests, Error, TEXT("CIRE_BONUS_LOOT_CHECK_FAIL %s"), *Why); } };
    UWorld* World = Mode->GetWorld();

    // ------------------------------------------------------------ PvP uniques: data
    const TArray<FName> Ids = CireItemsPvP::Ids();
    Check(Ids.Num() == 10, FString::Printf(TEXT("ten PvP uniques load (%d)"), Ids.Num()));
    TSet<uint8> Kinds;
    for (const FName Id : Ids)
    {
        const CI::ItemDef* Item = CireItems::Find(Id);
        const FCirePvPEffectDef* Def = CireItemsPvP::EffectOf(Id);
        Check(Item && !Item->Purchasable && Item->Unique && Item->Tier == CI::ItemTier::Legendary, Id.ToString() + TEXT(" is a unique, loot-only completed item"));
        Check(Item && Item->TotalCost >= 350 && Item->TotalCost <= 500, Id.ToString() + TEXT(" is mid value (350-500 g)"));
        Check(Def && Def->Kind != ECirePvPEffect::None && !Def->Text.IsEmpty() && CireItems::Get().EffectLine.Contains(Id), Id.ToString() + TEXT(" has a PvP effect and tooltip line"));
        Check(!CI::PlanPurchase(CireItems::Get().Catalog, CI::Inventory(), TCHAR_TO_UTF8(*Id.ToString()), 100000).Ok, Id.ToString() + TEXT(" cannot be bought in the shop"));
        if (Def) Kinds.Add(static_cast<uint8>(Def->Kind));
    }
    Check(Kinds.Num() == 10, TEXT("every PvP unique has its own effect"));
    Check(CireItems::Find(TEXT("serrated_cleaver")) != nullptr, TEXT("the shop catalog is intact after the merge"));

    // ------------------------------------------------------------ stage rolls
    const FCireBonusStageRules& Rules = CireLoot::Get().BonusStage;
    int32 Tiers[4] = {0, 0, 0, 0};
    for (uint64 Seed = 1; Seed <= 4000; ++Seed) ++Tiers[static_cast<int32>(RollTier(Rules, Seed * 7919))];
    Check(Tiers[1] > Tiers[2] && Tiers[2] > Tiers[3] && Tiers[3] > 0 && Tiers[0] == 0,
        FString::Printf(TEXT("tier roll: low > mid > rare > 0 (%d / %d / %d)"), Tiers[1], Tiers[2], Tiers[3]));
    Check(RollOutcome(Rules, ECireBonusTier::Rare, 5) == ECireBonusOutcome::Chests, TEXT("rare stages always pay chests"));
    {
        TSet<uint8> Low, Mid;
        for (uint64 Seed = 1; Seed <= 300; ++Seed) { Low.Add(static_cast<uint8>(RollOutcome(Rules, ECireBonusTier::Low, Seed))); Mid.Add(static_cast<uint8>(RollOutcome(Rules, ECireBonusTier::Mid, Seed))); }
        Check(Low.Num() == 3 && Mid.Num() == 3, TEXT("low and mid stages roll all three of their outcomes"));
    }

    // ------------------------------------------------------------ wave replacement
    {
        const FCireWaveConfig& Live = CireWaveDirector::Config(World);
        Check(Live.Bonus.ReplaceChance > 0.f && Live.Bonus.ReplaceChance <= .15f, FString::Printf(TEXT("stages are low frequency (%.2f)"), Live.Bonus.ReplaceChance));
        Check(Live.Bonus.bEscapeTimerOnHit && Live.Bonus.EscapeSeconds >= 52.f, TEXT("the escape clock waits for the first hit and is doubled (>= 52 s)"));
        FCireWaveConfig C = Live; C.Bonus.bEnabled = true; C.Bonus.ReplaceChance = 1.f; C.Bonus.FromWave = 1; C.Bonus.MaxPerCycle = 1;
        const FCireWaveDef Boss = CireWaveDirector::Template(ECireWaveType::Boss);
        FCireWaveDef BossRow = CireWaveDirector::Template(ECireWaveType::Normal); if (!BossRow.Units.IsEmpty()) BossRow.Units[0].bBoss = true;
        const FCireWaveDef Normal = CireWaveDirector::Template(ECireWaveType::Normal);
        const FCireWaveDef Armored = CireWaveDirector::Template(ECireWaveType::Armored);
        Check(!CireWaveDirector::RollBonusStage(C, Boss, 5, 1, 0) && !CireWaveDirector::RollBonusStage(C, Boss, 5, 1, 0, true) && !CireWaveDirector::RollBonusStage(C, BossRow, 5, 1, 0, true),
            TEXT("a stage never replaces a boss wave (or a wave with a boss row)"));
        Check(CireWaveDirector::RollBonusStage(C, Normal, 5, 1, 0) && CireWaveDirector::RollBonusStage(C, Armored, 5, 1, 0), TEXT("any other wave type can be replaced"));
        Check(!CireWaveDirector::RollBonusStage(C, Normal, 5, 1, 1), TEXT("stages are capped per cycle"));
        C.Bonus.FromWave = 6; Check(!CireWaveDirector::RollBonusStage(C, Normal, 5, 1, 0), TEXT("no stage before fromWave"));
        int32 Hits = 0;
        for (int32 Wave = 2; Wave < 402; ++Wave) Hits += CireWaveDirector::RollBonusStage(Live, Normal, Wave, 1234, 0);
        Check(Hits > 0 && Hits < 80, FString::Printf(TEXT("the default chance replaces few waves (%d of 400)"), Hits));
        const int32 Mob = CI::MobValue(CireLoot::Get().Economy, 7);
        Check(WaveGoldValue(Normal, 7) == Normal.UnitsPerLane() * Mob, TEXT("a wave's value is what its kills pay each player"));
    }

    // ------------------------------------------------------------ rewards
    {
        auto Single = [&](ECireBonusTier Tier, ECireBonusOutcome Outcome, float Caught, uint64 Seed = 3) { return BuildReward(Rules, Tier, Outcome, 100, Caught, Seed); };
        TArray<CI::LootBundle> R = Single(ECireBonusTier::Low, ECireBonusOutcome::Gold, 1.f);
        Check(R.Num() == 1 && R[0].Gold == FMath::RoundToInt(100 * Rules.LowGoldMultiplier), TEXT("low gold = wave value x2"));
        R = Single(ECireBonusTier::Mid, ECireBonusOutcome::Gold, 1.f);
        Check(R.Num() == 1 && R[0].Gold == FMath::RoundToInt(100 * Rules.MidGoldMultiplier), TEXT("mid gold = wave value x5"));
        R = Single(ECireBonusTier::Mid, ECireBonusOutcome::Gold, .5f);
        Check(R.Num() == 1 && R[0].Gold == FMath::RoundToInt(100 * Rules.MidGoldMultiplier * .5f), TEXT("gold scales with the share of the hoard caught"));
        Check(Single(ECireBonusTier::Low, ECireBonusOutcome::Gold, 0.f).IsEmpty(), TEXT("nothing caught, nothing paid"));
        const TArray<FName> Components = ComponentPool();
        R = Single(ECireBonusTier::Low, ECireBonusOutcome::Components, 1.f);
        bool bComponents = R.Num() == 1 && static_cast<int32>(R[0].Items.size()) == Rules.Components;
        if (bComponents) for (const auto& Id : R[0].Items) bComponents &= Components.Contains(FName(UTF8_TO_TCHAR(Id.c_str())));
        Check(!Components.IsEmpty() && bComponents, TEXT("low components are basic recipe components"));
        R = Single(ECireBonusTier::Low, ECireBonusOutcome::Consumables, 1.f);
        Check(R.Num() == 1 && static_cast<int32>(R[0].Items.size() + R[0].PrimaryTomes.size()) + (R[0].Experience > 0 ? 1 : 0) >= 1, TEXT("low consumables pay shop consumables or tomes"));
        bool bTome = false;
        for (uint64 Seed = 1; Seed < 60 && !bTome; ++Seed) { R = Single(ECireBonusTier::Low, ECireBonusOutcome::Consumables, 1.f, Seed); bTome = R.Num() == 1 && !R[0].PrimaryTomes.empty(); }
        Check(bTome, TEXT("primary-stat tomes are in the consumable pool"));
        R = Single(ECireBonusTier::Mid, ECireBonusOutcome::ShopItem, 1.f);
        const CI::ItemDef* ShopItem = R.Num() == 1 && R[0].Items.size() == 1 ? CireItems::Find(FName(UTF8_TO_TCHAR(R[0].Items[0].c_str()))) : nullptr;
        Check(ShopItem && ShopItem->Purchasable && ShopItem->TotalCost >= Rules.MidItemMinCost && ShopItem->TotalCost <= Rules.MidItemMaxCost, TEXT("mid shop item is worth 350-500 g"));
        R = Single(ECireBonusTier::Mid, ECireBonusOutcome::PvPUnique, 1.f);
        Check(R.Num() == 1 && R[0].Items.size() == 1 && CireItemsPvP::IsPvPUnique(FName(UTF8_TO_TCHAR(R[0].Items[0].c_str()))), TEXT("mid PvP outcome drops a PvP unique"));
        R = Single(ECireBonusTier::Mid, ECireBonusOutcome::PvPUnique, .2f);
        Check(R.Num() == 1 && R[0].Items.empty() && R[0].Gold > 0, TEXT("too few caught: item outcomes pay the tier's gold instead"));
        R = Single(ECireBonusTier::Rare, ECireBonusOutcome::Chests, 1.f);
        bool bChests = R.Num() == Rules.RareChests;
        for (const auto& B : R) bChests &= (B.Gold == Rules.ChestGold && B.SkillPoints == 0) || (B.Gold == 0 && B.SkillPoints == 1);
        Check(bChests, TEXT("rare: 3 chests, each 100 g or a free skill point"));
        Check(Single(ECireBonusTier::Rare, ECireBonusOutcome::Chests, .34f).Num() == 2, TEXT("rare: fewer chests when part of the hoard escaped"));
        int32 Points = 0, Purses = 0;
        for (uint64 Seed = 1; Seed < 40; ++Seed) for (const auto& B : Single(ECireBonusTier::Rare, ECireBonusOutcome::Chests, 1.f, Seed)) { Points += B.SkillPoints; Purses += B.Gold > 0; }
        Check(Points > 0 && Purses > 0, TEXT("rare chests hold both gold and free skill points"));
    }

    // ------------------------------------------------------------ heroes: free skill points and PvP effects
    const auto SavedClock = Mode->Clock; const auto SavedHeroes = Mode->Heroes;
    TArray<AActor*> Spawned;
    ON_SCOPE_EXIT
    {
        for (AActor* A : Spawned) if (IsValid(A)) A->Destroy();
        Mode->Clock = SavedClock; Mode->Heroes = SavedHeroes;
    };
    auto MakeHero = [&](int32 Team, FVector Offset) -> ACireHero*
    {
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* H = World->SpawnActor<ACireHero>(Mode->BasePosition(0) + FVector(2600, 0, 0) + Offset, FRotator::ZeroRotator, Params);
        if (!H) return nullptr;
        Spawned.Add(H); H->SetActorTickEnabled(false);
        if (H->Inventory) H->Inventory->SetComponentTickEnabled(false);
        H->TeamId = Team; H->bBot = true; H->Draft(0); H->CriticalChance = 0;
        H->Health = H->MaxHealth = 1000;
        Mode->Heroes.Add(H);
        return H;
    };
    ACireHero* A = MakeHero(0, FVector::ZeroVector);
    ACireHero* B = MakeHero(1, FVector(300, 0, 0));
    Check(A && B && A->Inventory && B->Inventory, TEXT("hero fixtures spawn"));
    if (!A || !B || !A->Inventory || !B->Inventory) return false;

    // Free skill point: granted by a chest, waives the next price, spent once.
    {
        CI::LootBundle Chest; Chest.SkillPoints = 1;
        const FCireLootReport Report = CireLoot::GrantPersonal(A, Chest, TEXT("test chest"), TEXT("test"), false);
        Check(CireLoot::FreeSkillPoints(A) == 1 && Report.Lines.ContainsByPredicate([](const FCireLootLine& L) { return L.Kind == static_cast<uint8>(CI::LootKind::SkillPoint); }),
            TEXT("a chest's free skill point lands on the champion with a loot line"));
        const FString Skill = TEXT("decimating_strike");
        Check(CireSkillShop::BuyPrice(A, Skill) == 0 && CireSkillShop::LevelPrice(A, Skill) == 0, TEXT("a free skill point waives the next purchase / level-up price"));
        Check(CireLoot::SpendFreeSkillPoint(A) && CireLoot::FreeSkillPoints(A) == 0 && CireSkillShop::LevelPrice(A, Skill) > 0, TEXT("the point is spent once, then prices return"));
    }

    // PvP effects: only champion vs champion in the arena.
    int32 Converted = 0;
    for (const TCHAR* Id : {TEXT("gladiators_crest"), TEXT("headhunters_mark"), TEXT("grievous_thorn"), TEXT("hushblade_signet"), TEXT("rimebrand"), TEXT("hunters_pursuit")})
        Check(A->Inventory->GrantItem(FName(Id), Converted), FString(TEXT("attacker equips ")) + Id);
    for (const TCHAR* Id : {TEXT("colosseum_bulwark"), TEXT("spellbreaker_veil"), TEXT("talisman_of_the_last_stand"), TEXT("vengeance_sigil")})
        Check(B->Inventory->GrantItem(FName(Id), Converted), FString(TEXT("defender equips ")) + Id);
    Check(CireItemsPvP::ModifyOutgoing(A, B, 100.f, TEXT("Strike")) == 100.f, TEXT("no PvP effect outside the arena"));
    Mode->Clock = Cires::MatchClock(); Mode->Clock.BeginIntermission(); Mode->Clock.Advance(Mode->Clock.GetDurations().Intermission);
    Check(Mode->Clock.Phase() == Cires::MatchPhase::Arena && CireItemsPvP::IsChampionDuel(A, B), TEXT("arena fixture: the champions duel"));
    Check(FMath::IsNearlyEqual(CireItemsPvP::ModifyOutgoing(A, B, 100.f, TEXT("Strike")), 112.f, .01f), TEXT("Gladiator's Crest: +12% damage to champions"));
    B->MaxHealth = 1000; B->Health = 200; // items raised the pools; pin them for exact thresholds
    Check(FMath::IsNearlyEqual(CireItemsPvP::ModifyOutgoing(A, B, 100.f, TEXT("Strike")), 134.4f, .05f), TEXT("Headhunter's Mark: +20% below 35% health"));
    B->Health = 1000;
    Check(FMath::IsNearlyEqual(CireItemsPvP::ModifyIncoming(B, A, TEXT("Basic attack"), 100.f), 88.f, .01f), TEXT("Colosseum Bulwark: -12% damage from champions"));
    const float Spell1 = CireItemsPvP::ModifyIncoming(B, A, TEXT("Fireball"), 100.f), Spell2 = CireItemsPvP::ModifyIncoming(B, A, TEXT("Fireball"), 100.f);
    Check(FMath::IsNearlyEqual(Spell1, 88.f * .4f, .05f) && FMath::IsNearlyEqual(Spell2, 88.f, .05f), FString::Printf(TEXT("Spellbreaker Veil: the first champion spell is broken, then its cooldown (%.1f, %.1f)"), Spell1, Spell2));
    const float Now = CireBuffs::ServerNow(World);
    CireItemsPvP::OnDamageDealt(A, B, 50.f, TEXT("Strike"));
    Check(CireCrowdControl::HealingReceivedCut(B) >= .39f, TEXT("Grievous Thorn: champions hit heal 40% less"));
    Check(CireCrowdControl::IsSilenced(B), TEXT("Hushblade Signet: the first hit silences"));
    Check(B->SlowUntil > Now, TEXT("Rimebrand: champion hits slow"));
    Check(CireItemsPvP::MoveSpeedBonus(A->Inventory) >= .149f, TEXT("Hunter's Pursuit: +15% move speed after hitting a champion"));
    const float AttackerHealth = A->Health = A->MaxHealth;
    B->MaxHealth = 1000; B->Health = 250;
    CireItemsPvP::OnHeroDamaged(B, A, TEXT("Strike"), 100.f);
    Check(A->Health < AttackerHealth, TEXT("Vengeance Sigil: champion damage is reflected"));
    Check(B->Inventory->BarrierHP > 0, TEXT("Talisman of the Last Stand: a barrier when a champion drops you below 30%"));
    // Monsters never trigger PvP effects.
    Check(!CireItemsPvP::IsChampionDuel(A, nullptr), TEXT("no duel without a champion target"));
    UE_LOG(LogCireBonusStageTests, Display, TEXT("CIRE_BONUS_LOOT_%s checks=%d pvp_uniques=%d"), bPass ? TEXT("PASS") : TEXT("FAIL"), Checks, Ids.Num());
    return bPass;
}
#endif
