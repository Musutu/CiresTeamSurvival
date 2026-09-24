#include "CireLoot.h"
// progression-shop: see CireLoot.h, Docs/Progression.md.
#include "CireGame.h"
#include "CireItems.h"
#include "CireLanePath.h"
#include "CireNPCArchetypes.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"
#include "CireRaces.h" // monster-races
#include "CireSummon.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireLoot, Log, All);

namespace CI = Cires::Items;

namespace
{
FCireLootData LootData;
bool bLootLoaded = false;

std::string Utf8(const FString& Text) { return std::string(TCHAR_TO_UTF8(*Text)); }
double Num(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, double Default)
{
    double Value = Default;
    return Object.IsValid() && Object->TryGetNumberField(Key, Value) && FMath::IsFinite(Value) ? Value : Default;
}

bool ParseSources(const TSharedPtr<FJsonObject>& Sources, const TCHAR* Key, TArray<FCireLootSource>& Out, FString& Error)
{
    const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
    if (!Sources.IsValid() || !Sources->TryGetArrayField(Key, List) || List->Num() == 0)
    { Error = FString::Printf(TEXT("LootTables.json sources.%s is missing"), Key); return false; }
    for (const auto& Value : *List)
    {
        const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
        if (!Object.IsValid()) { Error = TEXT("Loot source entry is not an object"); return false; }
        FCireLootSource Source;
        Source.MinTier = FMath::Clamp(static_cast<int32>(Num(Object, TEXT("minTier"), 1)), 1, 10);
        Source.MinRound = FMath::Clamp(static_cast<int32>(Num(Object, TEXT("minRound"), 1)), 1, 100);
        Object->TryGetStringField(TEXT("table"), Source.Table);
        Out.Add(Source);
    }
    return true;
}

// Unique per-drop seed: pack id, round and a monotonic counter.
uint64 DropCounter = 0;
uint64 MakeSeed(uint64 A, uint64 B)
{
    return (A * 0x9E3779B97F4A7C15ull) ^ (B << 17) ^ (++DropCounter * 0xD1B54A32D192ED03ull) ^ static_cast<uint64>(FPlatformTime::Cycles64());
}

TArray<ACireHero*> TeamOf(ACireGameMode* Mode, int32 Team)
{
    TArray<ACireHero*> Members;
    for (ACireHero* Hero : Mode->Heroes)
        if (IsValid(Hero) && Hero->TeamId == Team && Hero->bDrafted && !Hero->IsA<ACireSummon>()) Members.Add(Hero);
    return Members;
}

FString TomeName(int32 Points)
{
    return Points >= 3 ? FString::Printf(TEXT("Greater Tome of Ascendance (+%d)"), Points) : FString::Printf(TEXT("Tome of Ascendance (+%d)"), Points);
}

// Prep/arena/recovery pause bookkeeping (server only).
TMap<TWeakObjectPtr<ACireMonster>, double> PausedMonsters;
} // namespace

// ------------------------------------------------------------------ data
bool CireLoot::ParseJson(const FString& Json, FCireLootData& Out, FString& Error)
{
    Out = FCireLootData();
    TSharedPtr<FJsonObject> Root;
    if (Json.Len() > 512 * 1024 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
    { Error = TEXT("LootTables.json is not valid JSON"); return false; }
    if (Num(Root, TEXT("schemaVersion"), 0) != 1) { Error = TEXT("LootTables.json schemaVersion must be 1"); return false; }
    const TSharedPtr<FJsonObject>* Scaling = nullptr;
    if (Root->TryGetObjectField(TEXT("scaling"), Scaling))
    {
        Out.Scaling.GoldPerTier = FMath::Clamp(Num(*Scaling, TEXT("goldPerTier"), .25), 0., 2.);
        Out.Scaling.ChancePerTier = FMath::Clamp(Num(*Scaling, TEXT("chancePerTier"), .08), 0., 1.);
        Out.Scaling.MaxChance = FMath::Clamp(Num(*Scaling, TEXT("maxChance"), .95), 0., 1.);
    }
    const TSharedPtr<FJsonObject>* Schedule = nullptr;
    if (!Root->TryGetObjectField(TEXT("packSchedule"), Schedule)) { Error = TEXT("LootTables.json needs packSchedule"); return false; }
    const TArray<TSharedPtr<FJsonValue>>* Bays = nullptr;
    if ((*Schedule)->TryGetArrayField(TEXT("bays"), Bays))
        for (const auto& Value : *Bays)
        {
            const TSharedPtr<FJsonObject> Bay = Value.IsValid() ? Value->AsObject() : nullptr;
            CI::PackBay Entry;
            Entry.Bay = static_cast<int>(Num(Bay, TEXT("bay"), 0));
            Entry.UnlockRound = static_cast<int>(Num(Bay, TEXT("unlockRound"), 1));
            Entry.UnlockWave = static_cast<int>(Num(Bay, TEXT("unlockWave"), 1));
            Out.Schedule.Bays.push_back(Entry);
        }
    Out.Schedule.PromotionStartRound = static_cast<int>(Num(*Schedule, TEXT("promotionStartRound"), 4));
    Out.Schedule.PromotionEveryRounds = static_cast<int>(Num(*Schedule, TEXT("promotionEveryRounds"), 2));
    Out.Schedule.MaxTier = static_cast<int>(Num(*Schedule, TEXT("maxTier"), 8));
    const std::string ScheduleError = CI::ValidateSchedule(Out.Schedule);
    if (!ScheduleError.empty()) { Error = UTF8_TO_TCHAR(ScheduleError.c_str()); return false; }
    const TSharedPtr<FJsonObject>* Pickup = nullptr;
    if (Root->TryGetObjectField(TEXT("pickup"), Pickup))
    {
        Out.PickupRadius = FMath::Clamp(static_cast<float>(Num(*Pickup, TEXT("radius"), 320)), 80.f, 2000.f);
        (*Pickup)->TryGetBoolField(TEXT("autoCollectOnPrep"), Out.bAutoCollectOnPrep);
    }
    const TSharedPtr<FJsonObject>* Tables = nullptr;
    if (!Root->TryGetObjectField(TEXT("tables"), Tables) || (*Tables)->Values.Num() == 0) { Error = TEXT("LootTables.json needs tables"); return false; }
    const auto& Items = CireItems::Get();
    for (const auto& Pair : (*Tables)->Values)
    {
        const FString Key(*Pair.Key);
        const TSharedPtr<FJsonObject> Object = Pair.Value.IsValid() ? Pair.Value->AsObject() : nullptr;
        if (!Object.IsValid()) { Error = TEXT("Loot table is not an object: ") + Key; return false; }
        CI::LootTable Table;
        Table.Id = Utf8(Key);
        FString Label;
        Object->TryGetStringField(TEXT("label"), Label);
        Table.Label = Utf8(Label.IsEmpty() ? Key : Label);
        const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
        if (!Object->TryGetArrayField(TEXT("entries"), Entries) || Entries->Num() == 0) { Error = Key + TEXT(" has no entries"); return false; }
        for (const auto& Value : *Entries)
        {
            const TSharedPtr<FJsonObject> E = Value.IsValid() ? Value->AsObject() : nullptr;
            CI::LootEntry Entry;
            FString Type;
            if (!E.IsValid() || !E->TryGetStringField(TEXT("type"), Type) || !CI::ParseLootKind(Utf8(Type), Entry.Kind))
            { Error = Key + TEXT(" has an unknown entry type"); return false; }
            Entry.Chance = FMath::Clamp(Num(E, TEXT("chance"), 1), 0., 1.);
            Entry.Min = FMath::Clamp(static_cast<int>(Num(E, TEXT("min"), 0)), 0, 5000);
            Entry.Max = FMath::Clamp(static_cast<int>(Num(E, TEXT("max"), Entry.Min)), 0, 5000);
            const TArray<TSharedPtr<FJsonValue>>* Pool = nullptr;
            if (E->TryGetArrayField(TEXT("pool"), Pool))
                for (const auto& Id : *Pool)
                {
                    const FString ItemId = Id->AsString();
                    if (Items.bValid && !Items.Catalog.Find(Utf8(ItemId))) { Error = Key + TEXT(" drops unknown item ") + ItemId; return false; }
                    Entry.Pool.push_back(Utf8(ItemId));
                }
            if (Entry.Kind == CI::LootKind::Item && Entry.Pool.empty()) { Error = Key + TEXT(" item entry has an empty pool"); return false; }
            Table.Entries.push_back(Entry);
        }
        Out.Tables.Add(Key, Table);
    }
    const TSharedPtr<FJsonObject>* Sources = nullptr;
    Root->TryGetObjectField(TEXT("sources"), Sources);
    const TSharedPtr<FJsonObject> SourceObject = Sources ? *Sources : TSharedPtr<FJsonObject>();
    if (!ParseSources(SourceObject, TEXT("packCompletion"), Out.PackCompletion, Error) ||
        !ParseSources(SourceObject, TEXT("packLeader"), Out.PackLeader, Error) ||
        !ParseSources(SourceObject, TEXT("laneBoss"), Out.LaneBoss, Error)) return false;
    for (const TArray<FCireLootSource>* List : {&Out.PackCompletion, &Out.PackLeader, &Out.LaneBoss})
        for (const auto& Source : *List)
            if (!Out.Tables.Contains(Source.Table)) { Error = TEXT("Loot source uses unknown table ") + Source.Table; return false; }
    Out.bValid = true;
    return true;
}

bool CireLoot::Reload()
{
    FString Json, Error;
    FCireLootData Parsed;
    if (!FFileHelper::LoadFileToString(Json, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/LootTables.json")))) Error = TEXT("Cannot read Content/Data/LootTables.json");
    else ParseJson(Json, Parsed, Error);
    bLootLoaded = true;
    if (!Parsed.bValid)
    {
        UE_LOG(LogCireLoot, Error, TEXT("CIRE_LOOT_DATA_ERROR %s"), *Error);
        if (!LootData.bValid)
        {
            // Keep the game playable: fall back to a single safe bay and no tables.
            LootData.Schedule.Bays = {{1, 1, 1}, {2, 1, 1}, {3, 1, 1}};
            LootData.Error = Error;
        }
        return false;
    }
    LootData = MoveTemp(Parsed);
    UE_LOG(LogCireLoot, Display, TEXT("CIRE_LOOT_LOADED tables=%d bays=%d"), LootData.Tables.Num(), static_cast<int32>(LootData.Schedule.Bays.size()));
    return true;
}

const FCireLootData& CireLoot::Get()
{
    if (!bLootLoaded) Reload();
    return LootData;
}

const CI::LootTable* CireLoot::TableFor(const TArray<FCireLootSource>& Sources, int32 Tier, int32 Round)
{
    const FCireLootSource* Best = nullptr;
    for (const auto& Source : Sources)
        if (Tier >= Source.MinTier && Round >= Source.MinRound && (!Best || Source.MinTier > Best->MinTier || Source.MinRound > Best->MinRound)) Best = &Source;
    return Best ? Get().Tables.Find(Best->Table) : nullptr;
}

int32 CireLoot::BundleRarity(const CI::LootBundle& Bundle)
{
    int32 Rarity = 0;
    for (const auto& Id : Bundle.Items)
        if (const CI::ItemDef* Item = CireItems::Get().Catalog.Find(Id))
            Rarity = FMath::Max(Rarity, Item->Tier == CI::ItemTier::Legendary ? 3 : Item->Tier == CI::ItemTier::Epic ? 2 : Item->Tier == CI::ItemTier::Basic ? 1 : 0);
    for (const int Points : Bundle.PrimaryTomes) Rarity = FMath::Max(Rarity, Points >= 3 ? 2 : 1);
    return Rarity;
}

ACireLootDrop* CireLoot::SpawnDrop(ACireGameMode* Mode, int32 Team, FVector Location, const CI::LootBundle& Bundle, int32 Tier, const FString& Label, uint64 Seed)
{
    if (!Mode || Bundle.Empty()) return nullptr;
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CireLootGround), false);
    for (TActorIterator<APawn> It(Mode->GetWorld()); It; ++It) Query.AddIgnoredActor(*It);
    if (Mode->GetWorld()->LineTraceSingleByChannel(Hit, Location + FVector(0, 0, 150), Location - FVector(0, 0, 600), ECC_Visibility, Query))
        Location = Hit.ImpactPoint;
    else Location.Z -= 90.f;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Drop = Mode->GetWorld()->SpawnActor<ACireLootDrop>(Location, FRotator(0, FMath::FRandRange(-180.f, 180.f), 0), Params);
    if (!Drop) return nullptr;
    Drop->TeamId = Team;
    Drop->Tier = Tier;
    Drop->Bundle = Bundle;
    Drop->Seed = Seed;
    Drop->Label = Label;
    Drop->Rarity = BundleRarity(Bundle);
    Drop->ForceNetUpdate();
    UE_LOG(LogCireLoot, Display, TEXT("CIRE_LOOT_DROP team=%d tier=%d rarity=%d gold=%d xp=%d tomes=%d items=%d label=%s"),
        Team, Tier, Drop->Rarity, Bundle.Gold, Bundle.Experience, static_cast<int32>(Bundle.PrimaryTomes.size()), static_cast<int32>(Bundle.Items.size()), *Label);
    return Drop;
}

void CireLoot::OnMonsterKilled(ACireGameMode* Mode, ACireMonster* Monster, ACireHero* Killer, bool bPackCompleted)
{
    if (!Mode || !IsValid(Monster)) return;
    const auto& D = Get();
    if (!D.bValid) return;
    const int32 Round = Mode->Clock.Round();
    const double Loot = Mode->Loot(Monster->Lane);
    CI::LootBundle Bundle;
    FString Label;
    int32 Tier = FMath::Max(1, Monster->Tier);
    auto Merge = [&](const CI::LootTable* Table, int32 RollTier, uint64 Seed)
    {
        if (!Table) return;
        const CI::LootBundle Part = CI::RollLoot(*Table, RollTier, Loot, Seed, D.Scaling);
        Bundle.Gold += Part.Gold;
        Bundle.Experience += Part.Experience;
        Bundle.PrimaryTomes.insert(Bundle.PrimaryTomes.end(), Part.PrimaryTomes.begin(), Part.PrimaryTomes.end());
        Bundle.Items.insert(Bundle.Items.end(), Part.Items.begin(), Part.Items.end());
        if (Label.IsEmpty() || RollTier >= Tier) Label = UTF8_TO_TCHAR(Table->Label.c_str());
    };
    const bool bLeader = Monster->PackId >= 0 && Monster->GetNPCClassification() == ECireNPCClass::Boss;
    if (Monster->PackId < 0 && Monster->IsLaneBoss())
    {
        Tier = FMath::Clamp(1 + (Round - 1) / 2, 1, 10);
        Merge(TableFor(D.LaneBoss, Tier, Round), Tier, MakeSeed(Round, 1000 + Monster->Lane));
    }
    if (bLeader) Merge(TableFor(D.PackLeader, Tier, Round), Tier, MakeSeed(Monster->PackId, 2));
    if (bPackCompleted) Merge(TableFor(D.PackCompletion, Tier, Round), Tier, MakeSeed(Monster->PackId, 3));
    if (Bundle.Empty()) return;
    SpawnDrop(Mode, Monster->Lane, Monster->GetActorLocation(), Bundle, Tier, Label, MakeSeed(Monster->PackId + 7, Round));
}

TArray<FCireLootLine> CireLoot::Distribute(ACireGameMode* Mode, int32 Team, const CI::LootBundle& Bundle, const FString& Source, uint64 Seed)
{
    TArray<FCireLootLine> Lines;
    if (!Mode) return Lines;
    TArray<ACireHero*> Members = TeamOf(Mode, Team);
    if (Members.Num() == 0) return Lines;
    auto AddLine = [&](FName ItemId, CI::LootKind Kind, const FString& Text)
    {
        FCireLootLine Line; Line.ItemId = ItemId; Line.Kind = static_cast<uint8>(Kind); Line.Text = Text;
        Lines.Add(Line);
    };
    if (Bundle.Gold > 0)
    {
        for (ACireHero* Hero : Members) Hero->Gold += Bundle.Gold;
        AddLine(NAME_None, CI::LootKind::Gold, FString::Printf(TEXT("+%d gold each"), Bundle.Gold));
    }
    if (Bundle.Experience > 0)
    {
        for (ACireHero* Hero : Members) Hero->GrantExperience(Bundle.Experience);
        AddLine(NAME_None, CI::LootKind::Experience, FString::Printf(TEXT("+%d experience each"), Bundle.Experience));
    }
    uint64 Step = 0;
    auto Recipients = [&](bool bNeedsItemRoom, FName ItemId)
    {
        std::vector<CI::Recipient> List;
        for (ACireHero* Hero : Members)
        {
            CI::Recipient R;
            R.Eligible = IsValid(Hero) && Hero->Inventory != nullptr;
            R.HasRoom = !bNeedsItemRoom || (R.Eligible && Hero->Inventory->HasRoomFor(ItemId));
            R.LootScore = R.Eligible ? Hero->Inventory->LootScore : 0;
            List.push_back(R);
        }
        return List;
    };
    for (const int Points : Bundle.PrimaryTomes)
    {
        const int32 Index = CI::PickFairRecipient(Recipients(false, NAME_None), false, Seed + ++Step);
        if (!Members.IsValidIndex(Index)) continue;
        ACireHero* Hero = Members[Index];
        Hero->Inventory->ApplyPrimaryTome(Points);
        Hero->Inventory->LootScore += Points * 60;
        AddLine(Points >= 3 ? FName(TEXT("greater_tome_of_ascendance")) : FName(TEXT("tome_of_ascendance")), CI::LootKind::PrimaryTome,
            FString::Printf(TEXT("%s -> %s"), *TomeName(Points), *Hero->HeroName));
    }
    for (const auto& Id : Bundle.Items)
    {
        const FName ItemId(UTF8_TO_TCHAR(Id.c_str()));
        const CI::ItemDef* Item = CireItems::Find(ItemId);
        if (!Item) continue;
        int32 Index = CI::PickFairRecipient(Recipients(true, ItemId), true, Seed + ++Step);
        const bool bRoom = Members.IsValidIndex(Index);
        if (!bRoom) Index = CI::PickFairRecipient(Recipients(false, ItemId), false, Seed + ++Step);
        if (!Members.IsValidIndex(Index)) continue;
        ACireHero* Hero = Members[Index];
        int32 Converted = 0;
        Hero->Inventory->GrantItem(ItemId, Converted);
        Hero->Inventory->LootScore += Item->TotalCost;
        AddLine(ItemId, CI::LootKind::Item, Converted > 0
            ? FString::Printf(TEXT("%s -> %s (bags full: +%d gold)"), *CireItems::DisplayName(ItemId), *Hero->HeroName, Converted)
            : FString::Printf(TEXT("%s -> %s"), *CireItems::DisplayName(ItemId), *Hero->HeroName));
    }
    FString Summary = Source;
    for (const auto& Line : Lines) Summary += TEXT("  |  ") + Line.Text;
    for (ACireHero* Hero : Members)
    {
        Hero->Notice = Summary.Left(120);
        if (Hero->Inventory)
            for (const auto& Line : Lines)
                Hero->Inventory->SendFeedback(ECireShopAction::Loot, true, Line.ItemId, Line.Kind, false, Line.Kind == static_cast<uint8>(CI::LootKind::Gold) ? Bundle.Gold : 0,
                    Source + TEXT(": ") + Line.Text.Replace(*(TEXT("-> ") + Hero->HeroName), TEXT("-> you")));
    }
    UE_LOG(LogCireLoot, Display, TEXT("CIRE_LOOT_DISTRIBUTED team=%d %s"), Team, *Summary);
    return Lines;
}

int32 CireLoot::CollectAll(ACireGameMode* Mode)
{
    int32 Count = 0;
    if (!Mode) return 0;
    for (TActorIterator<ACireLootDrop> It(Mode->GetWorld()); It; ++It)
        if (!It->bOpened && It->Open(nullptr)) ++Count;
    return Count;
}

// ------------------------------------------------------------------ progression
void CireProgression::SpawnBay(ACireGameMode* Mode, int32 Team, int32 Bay, int32 Tier)
{
    if (!Mode || Tier <= 0) return;
    const int32 Round = Mode->Clock.Round();
    const auto& NPCs = CireNPCArchetypes::Get();
    const int32 Wave = Mode->GetGameState<ACireGameState>() ? Mode->GetGameState<ACireGameState>()->Wave : 0;
    const FVector Center = CireLanePath::ChallengePosition(Mode->GetWorld(), Team, Bay);
    const int32 Members = NPCs.PackMembers.Num();
    const bool bLeader = Tier >= NPCs.PackLeaderFromTier;
    const int32 PackId = Round * 100 + Team * 10 + Bay;
    for (ACireMonster* Existing : Mode->Monsters) if (IsValid(Existing) && Existing->PackId == PackId) return; // already spawned
    for (int32 I = 0; I < Members + (bLeader ? 1 : 0); ++I)
    {
        const bool bIsLeader = I == Members;
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        const FVector P = Center + (bIsLeader ? FVector(260, 0, 40) : FVector((I - (Members - 1) * .5f) * 110, 0, 0));
        auto* M = Mode->GetWorld()->SpawnActor<ACireMonster>(ACireMonster::StaticClass(), P, FRotator::ZeroRotator, Params);
        if (!M) { UE_LOG(LogCireLoot, Error, TEXT("Challenge pack spawn failed")); continue; }
        M->Lane = Team; M->Tier = Tier; M->PackId = PackId; M->SpawnPosition = P;
        CireNPCCombat::ConfigureArchetype(M, bIsLeader ? NPCs.PackLeader : NPCs.PackMembers[I], Wave, Tier, Round);
        CireRaces::ApplyPackUnit(M, Tier, bIsLeader, Wave); // monster-races: elite/champion/warlord colours and drawn skills
        M->MonsterName = FString::Printf(TEXT("Challenge %d | %s"), Tier, *M->MonsterName);
        Mode->Monsters.Add(M);
    }
    UE_LOG(LogCireLoot, Display, TEXT("CIRE_PACK_SPAWN round=%d team=%d bay=%d tier=%d leader=%d"), Round, Team, Bay, Tier, bLeader ? 1 : 0);
}

namespace
{
void Announce(ACireGameMode* Mode, const FString& Text)
{
    if (!Mode) return;
    for (ACireHero* Hero : Mode->Heroes)
        if (IsValid(Hero) && Hero->Inventory) Hero->Inventory->SendFeedback(ECireShopAction::Announce, true, NAME_None, -1, false, 0, Text);
    UE_LOG(LogCireLoot, Display, TEXT("CIRE_PACK_ANNOUNCE %s"), *Text);
}
FString BayWhere(int32 Bay) { return Bay <= 1 ? TEXT("near the town road") : Bay == 2 ? TEXT("midway along the route") : TEXT("deep along the route, near the monster gate"); }
}

void CireProgression::SpawnPacks(ACireGameMode* Mode, int32 WaveInCycle)
{
    if (!Mode) return;
    const auto& Schedule = CireLoot::Get().Schedule;
    const int32 Round = Mode->Clock.Round();
    TArray<FString> News;
    for (const auto& Bay : Schedule.Bays)
    {
        const int32 Tier = CI::BayTier(Schedule, Bay.Bay, Round, WaveInCycle);
        if (Tier <= 0) continue;
        for (int32 Team = 0; Team < 2; ++Team) SpawnBay(Mode, Team, Bay.Bay, Tier);
        if (Round > 1 && CI::BayUnlocksAt(Schedule, Bay.Bay, Round, WaveInCycle))
            News.Add(FString::Printf(TEXT("NEW CHALLENGE | Tier %d outpost has appeared %s"), Tier, *BayWhere(Bay.Bay)));
        else if (Round > 1 && Tier > CI::BayTier(Schedule, Bay.Bay, Round - 1, 99) && CI::BayTier(Schedule, Bay.Bay, Round - 1, 99) > 0)
            News.Add(FString::Printf(TEXT("OUTPOSTS STIR | Bay %d now holds a Tier %d pack: stronger foes, richer loot"), Bay.Bay, Tier));
    }
    for (const FString& Line : News) Announce(Mode, Line);
}

void CireProgression::OnWaveSpawned(ACireGameMode* Mode, int32 WaveInCycle)
{
    if (!Mode || WaveInCycle <= 1) return;
    const auto& Schedule = CireLoot::Get().Schedule;
    const int32 Round = Mode->Clock.Round();
    for (const auto& Bay : Schedule.Bays)
        if (CI::BayUnlocksAt(Schedule, Bay.Bay, Round, WaveInCycle))
        {
            const int32 Tier = CI::BayTier(Schedule, Bay.Bay, Round, WaveInCycle);
            for (int32 Team = 0; Team < 2; ++Team) SpawnBay(Mode, Team, Bay.Bay, Tier);
            Announce(Mode, FString::Printf(TEXT("NEW CHALLENGE | Tier %d outpost has appeared %s"), Tier, *BayWhere(Bay.Bay)));
        }
}

bool CireProgression::IsPaused(const ACireMonster* Monster)
{
    return PausedMonsters.Contains(TWeakObjectPtr<ACireMonster>(const_cast<ACireMonster*>(Monster)));
}

void CireProgression::PauseNPC(ACireMonster* Monster, double Now)
{
    if (!IsValid(Monster) || IsPaused(Monster)) return;
    PausedMonsters.Add(Monster, Now);
    if (PausedMonsters.Num() > 512)
        for (auto It = PausedMonsters.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
}

void CireProgression::ResumeNPC(ACireMonster* Monster, double Now)
{
    double PausedAt = 0;
    if (!IsValid(Monster) || !PausedMonsters.RemoveAndCopyValue(Monster, PausedAt)) return;
    const double Length = Now - PausedAt;
    if (Length <= 0) return;
    auto Shift = [&](float& Value) { Value = static_cast<float>(CI::ShiftForPause(Value, PausedAt, Length)); };
    Shift(Monster->SlowUntil);
    Shift(Monster->ForcedVictimUntil);
    Shift(Monster->PoisonEndsAt);
    if (!Monster->CastingAbility.IsEmpty()) { Shift(Monster->CastEndsAt); Shift(Monster->CastStartedAt); }
    if (UCireNPCState* S = Monster->NPCState)
    {
        for (auto& Pair : S->ReadyAt) Shift(Pair.Value);
        for (auto& Pair : S->ProvokedUntil) Shift(Pair.Value);
        Shift(S->GuardUntil); Shift(S->RallyUntil); Shift(S->ShieldWallUntil);
        Shift(S->KiteUntil); Shift(S->KiteReadyAt); Shift(S->DashUntil); Shift(S->ThreatPublishAt);
    }
    Monster->ForceNetUpdate();
}

void CireProgression::OnPhaseChanged(ACireGameMode* Mode, int32 NewPhase)
{
    CireItems::OnPhaseChanged(Mode, NewPhase);
    if (NewPhase == 1 && CireLoot::Get().bAutoCollectOnPrep)
    {
        const int32 Collected = CireLoot::CollectAll(Mode);
        if (Collected > 0) UE_LOG(LogCireLoot, Display, TEXT("CIRE_LOOT_AUTOCOLLECT chests=%d"), Collected);
    }
}

// ------------------------------------------------------------------ chest actor
FLinearColor ACireLootDrop::RarityColor(int32 Rarity)
{
    switch (Rarity)
    {
    case 3: return FLinearColor(1.f, .42f, .08f);
    case 2: return FLinearColor(.72f, .32f, 1.f);
    case 1: return FLinearColor(.28f, .62f, 1.f);
    default: return FLinearColor(1.f, .78f, .34f);
    }
}

ACireLootDrop::ACireLootDrop()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    SetReplicateMovement(false);
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);
    auto MakeMesh = [this](const TCHAR* Name, USceneComponent* Parent)
    {
        auto* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Mesh->SetupAttachment(Parent);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCastShadow(true);
        return Mesh;
    };
    Body = MakeMesh(TEXT("Body"), Root);
    Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
    Hinge->SetupAttachment(Root);
    Lid = MakeMesh(TEXT("Lid"), Hinge);
    BandA = MakeMesh(TEXT("BandA"), Root);
    BandB = MakeMesh(TEXT("BandB"), Root);
    Lock = MakeMesh(TEXT("Lock"), Root);
    Glow = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Glow"));
    Glow->SetupAttachment(Root);
    Glow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Glow->SetCastShadow(false);
    Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
    Light->SetupAttachment(Root);
    Light->SetRelativeLocation(FVector(0, 0, 120));
    Light->SetAttenuationRadius(520.f);
    Light->SetCastShadows(false);
    Light->SetIntensity(12000.f);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    if (Cube.Succeeded())
    {
        Body->SetStaticMesh(Cube.Object); Lid->SetStaticMesh(Cube.Object);
        BandA->SetStaticMesh(Cube.Object); BandB->SetStaticMesh(Cube.Object); Lock->SetStaticMesh(Cube.Object);
    }
    // 90 x 58 x 46 cm iron-bound reliquary; the lid pivots on its back edge.
    Body->SetRelativeLocation(FVector(0, 0, 23)); Body->SetRelativeScale3D(FVector(.9f, .58f, .46f));
    Hinge->SetRelativeLocation(FVector(-45, 0, 46));
    Lid->SetRelativeLocation(FVector(45, 0, 9)); Lid->SetRelativeScale3D(FVector(.94f, .62f, .18f));
    BandA->SetRelativeLocation(FVector(-24, 0, 28)); BandA->SetRelativeScale3D(FVector(.09f, .62f, .5f));
    BandB->SetRelativeLocation(FVector(24, 0, 28)); BandB->SetRelativeScale3D(FVector(.09f, .62f, .5f));
    Lock->SetRelativeLocation(FVector(46, 0, 36)); Lock->SetRelativeScale3D(FVector(.06f, .16f, .18f));
    (void)Cylinder;
}

void ACireLootDrop::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACireLootDrop, TeamId);
    DOREPLIFETIME(ACireLootDrop, Rarity);
    DOREPLIFETIME(ACireLootDrop, Tier);
    DOREPLIFETIME(ACireLootDrop, Label);
    DOREPLIFETIME(ACireLootDrop, bOpened);
    DOREPLIFETIME(ACireLootDrop, Manifest);
}

bool ACireLootDrop::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const
{
    const ACireHero* Viewer = Cast<ACireHero>(ViewTarget);
    if (const auto* Controller = Cast<AController>(RealViewer)) Viewer = Cast<ACireHero>(Controller->GetPawn());
    return Viewer && Viewer->TeamId == TeamId;
}

void ACireLootDrop::BeginPlay()
{
    Super::BeginPlay();
    SpawnedAt = GetWorld()->GetTimeSeconds();
    if (GetNetMode() == NM_DedicatedServer) return;
    if (auto* Metal = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/M_Metal.M_Metal"))) { Body->SetMaterial(0, Metal); Lid->SetMaterial(0, Metal); }
    if (auto* Gold = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/M_Gold.M_Gold"))) { BandA->SetMaterial(0, Gold); BandB->SetMaterial(0, Gold); Lock->SetMaterial(0, Gold); }
    BuildGlow();
}

void ACireLootDrop::BuildGlow()
{
    if (!Glow || BuiltRarity == Rarity) return;
    BuiltRarity = Rarity;
    const FLinearColor Color = RarityColor(Rarity);
    TArray<FVector> V; TArray<int32> I; TArray<FLinearColor> C; TArray<FVector2D> UV;
    // Light pillar: an open 16-sided tube that fades out as it rises.
    constexpr int32 Sides = 16;
    const float Height = 380.f + Rarity * 90.f;
    for (int32 Ring = 0; Ring < 2; ++Ring)
        for (int32 S = 0; S <= Sides; ++S)
        {
            const float A = 2.f * PI * S / Sides;
            const float Radius = Ring == 0 ? 34.f : 20.f;
            V.Add(FVector(FMath::Cos(A) * Radius, FMath::Sin(A) * Radius, Ring == 0 ? 10.f : Height));
            C.Add(FLinearColor(Color.R, Color.G, Color.B, Ring == 0 ? .55f : 0.f));
            UV.Add(FVector2D(static_cast<float>(S) / Sides, static_cast<float>(Ring)));
        }
    for (int32 S = 0; S < Sides; ++S)
    {
        const int32 A = S, B = S + 1, Top = S + Sides + 1, TopB = S + Sides + 2;
        I.Append({A, Top, B, B, Top, TopB, A, B, Top, B, TopB, Top});
    }
    // Ground halo: a soft annulus around the chest.
    const int32 Base = V.Num();
    for (int32 S = 0; S <= 32; ++S)
    {
        const float A = 2.f * PI * S / 32;
        V.Add(FVector(FMath::Cos(A) * 58.f, FMath::Sin(A) * 58.f, 3.f)); C.Add(FLinearColor(Color.R, Color.G, Color.B, .75f)); UV.Add(FVector2D(0, 0));
        V.Add(FVector(FMath::Cos(A) * 125.f, FMath::Sin(A) * 125.f, 3.f)); C.Add(FLinearColor(Color.R, Color.G, Color.B, 0.f)); UV.Add(FVector2D(1, 0));
    }
    for (int32 S = 0; S < 32; ++S)
    {
        const int32 A = Base + S * 2;
        I.Append({A, A + 2, A + 1, A + 1, A + 2, A + 3, A, A + 1, A + 2, A + 1, A + 3, A + 2});
    }
    Glow->ClearAllMeshSections();
    Glow->CreateMeshSection_LinearColor(0, V, I, TArray<FVector>(), UV, C, TArray<FProcMeshTangent>(), false);
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Effects/CireSpellPolish01/M_SpellCore.M_SpellCore"));
    if (!Material) Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Effects/CireSpell/M_SpellGlow.M_SpellGlow"));
    if (Material) Glow->SetMaterial(0, Material);
    Light->SetLightColor(Color);
}

void ACireLootDrop::OnRep_Opened()
{
    OpenedAt = GetWorld()->GetTimeSeconds();
}

bool ACireLootDrop::Open(ACireHero* Opener)
{
    if (!HasAuthority() || bOpened) return false;
    auto* Mode = GetWorld()->GetAuthGameMode<ACireGameMode>();
    if (!Mode) return false;
    bOpened = true;
    OpenedAt = GetWorld()->GetTimeSeconds();
    const FString Source = Label.IsEmpty() ? TEXT("Loot") : Label;
    Manifest = CireLoot::Distribute(Mode, TeamId, Bundle, Opener ? Source + TEXT(" (") + Opener->HeroName + TEXT(")") : Source + TEXT(" (auto-collected)"), Seed);
    ForceNetUpdate();
    SetLifeSpan(3.5f);
    return true;
}

void ACireLootDrop::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const float Now = GetWorld()->GetTimeSeconds();
    if (GetNetMode() != NM_DedicatedServer)
    {
        BuildGlow();
        if (const auto* Local = GetWorld()->GetFirstPlayerController())
            if (const auto* Viewer = Cast<ACireHero>(Local->GetPawn())) SetActorHiddenInGame(Viewer->TeamId != TeamId);
        const float Age = Now - SpawnedAt;
        const float Pulse = .75f + .25f * FMath::Sin(Now * 3.2f);
        if (!bOpened)
        {
            Light->SetIntensity((9000.f + Rarity * 3500.f) * Pulse * FMath::Clamp(Age * 2.f, 0.f, 1.f));
            Glow->SetRelativeScale3D(FVector(1.f, 1.f, FMath::Clamp(Age * 1.8f, .05f, 1.f)));
            Glow->SetRelativeRotation(FRotator(0, Now * 25.f, 0));
            Hinge->SetRelativeRotation(FRotator(0, 0, 0));
            // Settle in with a short drop and bounce.
            const float Drop = Age < .45f ? FMath::Square(1.f - Age / .45f) * 140.f : FMath::Abs(FMath::Sin((Age - .45f) * 9.f)) * 12.f * FMath::Max(0.f, 1.f - (Age - .45f) * 2.5f);
            Body->SetRelativeLocation(FVector(0, 0, 23 + Drop)); BandA->SetRelativeLocation(FVector(-24, 0, 28 + Drop));
            BandB->SetRelativeLocation(FVector(24, 0, 28 + Drop)); Lock->SetRelativeLocation(FVector(46, 0, 36 + Drop));
            Hinge->SetRelativeLocation(FVector(-45, 0, 46 + Drop));
        }
        else
        {
            if (OpenedAt < 0) OpenedAt = Now;
            const float T = FMath::Clamp((Now - OpenedAt) / .45f, 0.f, 1.f);
            Hinge->SetRelativeRotation(FRotator(-72.f * FMath::InterpEaseOut(0.f, 1.f, T, 2.f), 0, 0));
            const float Fade = FMath::Clamp(1.f - (Now - OpenedAt - .4f) / 2.5f, 0.f, 1.f);
            Light->SetIntensity((30000.f + Rarity * 8000.f) * (T < 1.f ? T : Fade));
            Glow->SetRelativeScale3D(FVector(1.f + T * .6f, 1.f + T * .6f, FMath::Max(.02f, Fade * 1.4f)));
        }
    }
    if (!HasAuthority() || bOpened) return;
    auto* Mode = GetWorld()->GetAuthGameMode<ACireGameMode>();
    if (!Mode) return;
    const float Radius = CireLoot::Get().PickupRadius;
    for (ACireHero* Hero : Mode->Heroes)
        if (IsValid(Hero) && Hero->TeamId == TeamId && Hero->bDrafted && !Hero->bDead &&
            FVector::DistSquared2D(Hero->GetActorLocation(), GetActorLocation()) <= FMath::Square(Radius) &&
            FMath::Abs(Hero->GetActorLocation().Z - GetActorLocation().Z) < 300.f)
        { Open(Hero); break; }
}
