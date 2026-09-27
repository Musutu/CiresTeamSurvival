// outdoor-bosses: the world bosses at Eric's Boss markers. See CireOutdoorBosses.h and Docs/OutdoorBosses.md.
#include "CireOutdoorBosses.h"
#include "CireGame.h"
#include "CireItems.h"
#include "CireLeash.h"
#include "CireLoot.h"
#include "CireLanePath.h"
#include "CireMapLayout.h"
#include "CireNav.h"
#include "CireNPCArchetypes.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"
#include "CireRaces.h"
#include "CireThreat.h"
#include "CireWaves.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireOutdoorBosses, Log, All);

namespace
{
FCireOutdoorBossRules GRules;
bool bRulesLoaded = false;
constexpr int32 RealmBlock = 50000, BossBlock = 40000, MaxBosses = 10000;

/** Per match: the respawn clock (sum of the ticks it was given, so tests step it deterministically) and each dead boss's
 *  return time (< 0: it stays dead). */
struct FRuntime
{
    bool bSpawned = false, bSuddenDeath = false;
    double Clock = 0, ScanIn = 0;
    TMap<int32, double> ReturnAt;
};
TMap<TWeakObjectPtr<UWorld>, FRuntime> GRuntime;
FRuntime& RuntimeOf(const UWorld* World) { return GRuntime.FindOrAdd(TWeakObjectPtr<UWorld>(const_cast<UWorld*>(World))); }

FString DataPath() { return FPaths::ProjectContentDir() / TEXT("Data/OutdoorBosses.json"); }

ACireMonster* AliveBoss(const ACireGameMode* Mode, int32 PackId)
{
    if (!Mode) return nullptr;
    for (ACireMonster* M : Mode->Monsters) if (IsValid(M) && !M->IsActorBeingDestroyed() && M->PackId == PackId && M->Health > 0) return M;
    return nullptr;
}
FString MarkerName(const FCireRouteSpot& Spot, int32 Index) { return Spot.Name.IsEmpty() ? FString::Printf(TEXT("Boss %d"), Index + 1) : Spot.Name; }
void Announce(ACireGameMode* Mode, const FString& Text)
{
    if (!Mode) return;
    for (ACireHero* Hero : Mode->Heroes) if (IsValid(Hero) && Hero->Inventory) Hero->Inventory->SendFeedback(ECireShopAction::Announce, true, NAME_None, -1, false, 0, Text);
    UE_LOG(LogCireOutdoorBosses, Display, TEXT("CIRE_OUTDOOR_BOSS_ANNOUNCE %s"), *Text);
}
bool Num(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double& Out, double Min, double Max, FString& Error)
{
    if (!O->HasField(Key)) return true;
    double V = 0;
    const TSharedPtr<FJsonValue> Field = O->TryGetField(Key);
    if (!Field.IsValid() || Field->Type != EJson::Number || !O->TryGetNumberField(Key, V) || !FMath::IsFinite(V) || V < Min || V > Max) { Error = FString::Printf(TEXT("%s must be a number %g..%g"), Key, Min, Max); return false; }
    Out = V; return true;
}
bool Flag(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, bool& Out, FString& Error)
{
    if (!O->HasField(Key)) return true;
    if (!O->TryGetBoolField(Key, Out)) { Error = FString::Printf(TEXT("%s must be true or false"), Key); return false; }
    return true;
}
}

// ================================================================================================= data
bool CireOutdoorBosses::ParseJson(const FString& Json, FCireOutdoorBossRules& Out, FString& Error)
{
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) { Error = TEXT("OutdoorBosses.json is not valid JSON"); return false; }
    double Schema = 0;
    if (!Root->TryGetNumberField(TEXT("schemaVersion"), Schema) || Schema != 1) { Error = TEXT("OutdoorBosses.json needs schemaVersion 1"); return false; }
    FCireOutdoorBossRules R;
    double Respawn = R.RespawnSeconds, Wave = R.StrengthWave, Damage = R.DamageMultiplier, Tier = R.LootTier, Gold = R.GoldMultiplier;
    double Sudden = R.SuddenDeathMinutes, Aggro = R.HostileAggroRadius, Base = R.BaseHealth;
    if (!Num(Root, TEXT("suddenDeathMinutes"), Sudden, 0, 1440, Error) || !Num(Root, TEXT("hostileAggroRadius"), Aggro, 300, 5000, Error) ||
        !Num(Root, TEXT("baseHealth"), Base, 100, 10000000, Error) || !Flag(Root, TEXT("suddenDeathHostile"), R.bSuddenDeathHostile, Error)) return false;
    R.SuddenDeathMinutes = static_cast<float>(Sudden); R.HostileAggroRadius = static_cast<float>(Aggro); R.BaseHealth = static_cast<float>(Base);
    if (const TSharedPtr<FJsonObject>* Hp = nullptr; Root->TryGetObjectField(TEXT("bossHealth"), Hp) && Hp)
    {
        for (const auto& Pair : (*Hp)->Values)
        {
            double V = 0;
            if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Number || !Pair.Value->TryGetNumber(V) || !FMath::IsFinite(V) || V < .05 || V > 100)
            { Error = FString::Printf(TEXT("bossHealth \"%s\" must be a multiplier 0.05..100"), *FString(Pair.Key)); return false; }
            R.BossHealth.Add(FName(*FString(Pair.Key).TrimStartAndEnd().ToLower()), static_cast<float>(V));
        }
    }
    else if (Root->HasField(TEXT("bossHealth"))) { Error = TEXT("bossHealth is { \"boss id\": multiplier }"); return false; }
    if (!Flag(Root, TEXT("enabled"), R.bEnabled, Error) || !Flag(Root, TEXT("waveBossesAtMarkers"), R.bWaveBossesAtMarkers, Error) ||
        !Num(Root, TEXT("respawnSeconds"), Respawn, -1, 36000, Error) || !Num(Root, TEXT("strengthWave"), Wave, 1, 1000, Error) ||
        !Num(Root, TEXT("damageMultiplier"), Damage, .05, 100, Error) ||
        !Num(Root, TEXT("lootTier"), Tier, 1, 10, Error) || !Num(Root, TEXT("goldMultiplier"), Gold, 0, 100, Error)) return false;
    R.RespawnSeconds = static_cast<float>(Respawn); R.StrengthWave = FMath::RoundToInt32(Wave);
    R.DamageMultiplier = static_cast<float>(Damage); R.LootTier = FMath::RoundToInt32(Tier); R.GoldMultiplier = static_cast<float>(Gold);
    if (const TSharedPtr<FJsonObject>* By = nullptr; Root->TryGetObjectField(TEXT("byMarker"), By) && By)
    {
        for (const auto& Pair : (*By)->Values)
        {
            FString Id;
            if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::String || !Pair.Value->TryGetString(Id) || Id.IsEmpty()) { Error = FString::Printf(TEXT("byMarker \"%s\" must name a boss id"), *FString(Pair.Key)); return false; }
            R.ByMarker.Add(FString(Pair.Key).TrimStartAndEnd(), FName(*Id.TrimStartAndEnd().ToLower()));
        }
    }
    else if (Root->HasField(TEXT("byMarker"))) { Error = TEXT("byMarker is { \"marker name\": \"boss id\" }"); return false; }
    if (const TArray<TSharedPtr<FJsonValue>>* Roster = nullptr; Root->TryGetArrayField(TEXT("roster"), Roster) && Roster)
    {
        for (const auto& V : *Roster)
        {
            FString Id;
            if (!V.IsValid() || V->Type != EJson::String || !V->TryGetString(Id) || Id.IsEmpty()) { Error = TEXT("roster is a list of boss ids"); return false; }
            R.Roster.Add(FName(*Id.TrimStartAndEnd().ToLower()));
        }
    }
    else if (Root->HasField(TEXT("roster"))) { Error = TEXT("roster is a list of boss ids"); return false; }
    Out = MoveTemp(R); Error.Reset(); return true;
}
bool CireOutdoorBosses::Reload(FString* Error)
{
    bRulesLoaded = true;
    FString Json, Why; FCireOutdoorBossRules Parsed;
    if (!FFileHelper::LoadFileToString(Json, *DataPath())) { GRules = FCireOutdoorBossRules(); if (Error) *Error = TEXT("OutdoorBosses.json is missing; built-in defaults"); return false; }
    if (!ParseJson(Json, Parsed, Why)) { UE_LOG(LogCireOutdoorBosses, Warning, TEXT("CIRE_OUTDOOR_BOSS_DEFAULTS %s"), *Why); GRules = FCireOutdoorBossRules(); if (Error) *Error = Why; return false; }
    GRules = MoveTemp(Parsed);
#if !UE_BUILD_SHIPPING
    float Respawn = 0; // probes: -CireOutdoorBossRespawn=<seconds>
    if (FParse::Value(FCommandLine::Get(), TEXT("CireOutdoorBossRespawn="), Respawn)) GRules.RespawnSeconds = Respawn;
#endif
    float Sudden = 0; // probes: -CireOutdoorBossSuddenDeath=<minutes>
    if (FParse::Value(FCommandLine::Get(), TEXT("CireOutdoorBossSuddenDeath="), Sudden)) GRules.SuddenDeathMinutes = Sudden;
    UE_LOG(LogCireOutdoorBosses, Display, TEXT("CIRE_OUTDOOR_BOSS_RULES enabled=%d respawn=%.0fs suddenDeath=%.1fmin hostile=%d waveBossesAtMarkers=%d baseHealth=%.0f damage=x%.2f lootTier=%d gold=x%.2f byMarker=%d roster=%d"),
        GRules.bEnabled ? 1 : 0, GRules.RespawnSeconds, GRules.SuddenDeathMinutes, GRules.bSuddenDeathHostile ? 1 : 0, GRules.bWaveBossesAtMarkers ? 1 : 0, GRules.BaseHealth, GRules.DamageMultiplier, GRules.LootTier,
        GRules.GoldMultiplier, GRules.ByMarker.Num(), GRules.Roster.Num());
    if (Error) Error->Reset();
    return true;
}
const FCireOutdoorBossRules& CireOutdoorBosses::Rules() { if (!bRulesLoaded) Reload(); return GRules; }
void CireOutdoorBosses::OverrideRules(const FCireOutdoorBossRules* R)
{
    if (R) { bRulesLoaded = true; GRules = *R; } else Reload();
}

TArray<FName> CireOutdoorBosses::BossIds()
{
    TArray<FName> Out;
    const FCireRaceDatabase& D = CireRaces::Get();
    for (const FName& RaceId : D.Order)
        if (const FCireRace* Race = D.Races.Find(RaceId))
            for (const TCHAR* Slot : {TEXT("warlord"), TEXT("colossus")})
                if (const FName* Id = Race->Bosses.Find(FName(Slot)); Id && CireNPCArchetypes::Find(*Id)) Out.AddUnique(*Id);
    return Out;
}
bool CireOutdoorBosses::IsBossId(FName Id) { return !Id.IsNone() && BossIds().Contains(Id); }
FString CireOutdoorBosses::BossName(FName Id)
{
    const FCireNPCArchetype* A = CireNPCArchetypes::Find(Id);
    return A ? A->DisplayName : Id.ToString();
}
FName CireOutdoorBosses::Resolve(const FString& Kind, const FString& MarkerName, int32 Index, const FCireOutdoorBossRules& R)
{
    const FName Own(*Kind.TrimStartAndEnd().ToLower());
    if (!Kind.IsEmpty() && IsBossId(Own)) return Own;
    if (const FName* By = R.ByMarker.Find(MarkerName.TrimStartAndEnd()); By && IsBossId(*By)) return *By;
    TArray<FName> Roster;
    for (const FName& Id : R.Roster) if (IsBossId(Id)) Roster.Add(Id);
    if (Roster.IsEmpty()) Roster = BossIds();
    return Roster.IsEmpty() ? NAME_None : Roster[FMath::Abs(Index) % Roster.Num()];
}
float CireOutdoorBosses::HealthFor(FName BossId, float MarkerScale, const FCireOutdoorBossRules& R)
{
    const float* Boss = R.BossHealth.Find(BossId);
    return FMath::Clamp(R.BaseHealth * (Boss ? *Boss : 1.f) * FMath::Clamp(FMath::IsFinite(MarkerScale) ? MarkerScale : 1.f, .1f, 20.f), 1.f, 1.e9f);
}
FName CireOutdoorBosses::ResolveSpot(const FCireBattlefieldRoutes& Routes, int32 Realm, int32 Index)
{
    const TArray<FCireRouteSpot>& Spots = Routes.Bosses[FMath::Clamp(Realm, 0, 1)];
    if (!Spots.IsValidIndex(Index)) return NAME_None;
    return Resolve(Spots[Index].Kind, MarkerName(Spots[Index], Index), Index, Rules());
}

FName CireOutdoorBosses::ResolveMarker(const FCireMapLayout& L, const FCireMapMarker& Marker, bool* bOwn)
{
    // The compile lists a realm's Boss Spawn markers in layout order (CireMapLayout::CompileRoutes, Collect).
    const int32 Realm = Marker.Owner == ECireMarkerOwner::Shared ? 0 : CireMapLayout::RealmOf(Marker.Owner);
    int32 Index = 0;
    for (const FCireMapMarker& M : L.Markers)
    {
        if (&M == &Marker || M.Id == Marker.Id) break;
        Index += M.Type == CireMapLayout::BossSpawn && CireMapLayout::ShownInRealm(M, Realm) ? 1 : 0;
    }
    if (bOwn) *bOwn = !Marker.Kind.IsEmpty() && IsBossId(FName(*Marker.Kind));
    return Resolve(Marker.Kind, Marker.Name.IsEmpty() ? FString::Printf(TEXT("Boss %d"), Index + 1) : Marker.Name, Index, Rules());
}

// ================================================================================================= identity
int32 CireOutdoorBosses::PackIdFor(int32 Realm, int32 Index) { return FMath::Clamp(Realm, 0, 1) * RealmBlock + BossBlock + FMath::Clamp(Index, 0, MaxBosses - 1); }
bool CireOutdoorBosses::IsOutdoorPackId(int32 PackId) { return PackId >= BossBlock && PackId < 2 * RealmBlock && PackId % RealmBlock >= BossBlock; }
int32 CireOutdoorBosses::IndexOf(int32 PackId) { return IsOutdoorPackId(PackId) ? PackId % RealmBlock - BossBlock : INDEX_NONE; }
bool CireOutdoorBosses::IsOutdoorBoss(const ACireMonster* M) { return IsValid(M) && IsOutdoorPackId(M->PackId); }

// ================================================================================================= runtime
ACireMonster* CireOutdoorBosses::SpawnOne(ACireGameMode* Mode, int32 Realm, int32 Index, bool bHostile)
{
    if (!Mode || !Mode->HasAuthority() || !Rules().bEnabled) return nullptr;
    UWorld* World = Mode->GetWorld();
    Realm = FMath::Clamp(Realm, 0, 1);
    const FCireBattlefieldRoutes& Routes = CireLanePath::Get(World);
    if (!Routes.Bosses[Realm].IsValidIndex(Index) || Index >= MaxBosses) return nullptr;
    const FCireRouteSpot Spot = Routes.Bosses[Realm][Index];
    const int32 PackId = PackIdFor(Realm, Index);
    if (ACireMonster* Alive = AliveBoss(Mode, PackId)) return Alive;
    const FName BossId = ResolveSpot(Routes, Realm, Index);
    const FCireNPCArchetype* A = CireNPCArchetypes::Find(BossId);
    if (!A) { UE_LOG(LogCireOutdoorBosses, Warning, TEXT("CIRE_OUTDOOR_BOSS_SKIPPED realm=%d marker=\"%s\": no boss archetype"), Realm, *MarkerName(Spot, Index)); return nullptr; }
    // The lair: the marker on the ground, projected onto the navmesh (the Large agent first: bosses are big).
    const FVector Ground = CireLanePath::ToWorld(Realm, Spot.Position, 0.f);
    FVector Floor = Ground, OnNav;
    bool bOnNav = false;
    if (CireNav::HasNavigation(World))
        bOnNav = CireNav::Project(World, Ground + FVector(0, 0, 60), OnNav, FVector(250, 250, 800), 72.f) || CireNav::Project(World, Ground + FVector(0, 0, 60), OnNav, FVector(500, 500, 800), 40.f);
    if (bOnNav) Floor = OnNav;
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* M = World->SpawnActor<ACireMonster>(ACireMonster::StaticClass(), Floor + FVector(0, 0, 120), FRotator(0, Spot.Yaw, 0), Params);
    if (!M) { UE_LOG(LogCireOutdoorBosses, Error, TEXT("CIRE_OUTDOOR_BOSS_SPAWN_FAILED realm=%d boss=%s"), Realm, *BossId.ToString()); return nullptr; }
    const FCireOutdoorBossRules& R = Rules();
    const auto* State = Mode->GetGameState<ACireGameState>();
    const int32 Wave = FMath::Max(State ? State->Wave : 1, R.StrengthWave);
    M->Lane = Realm; M->PackId = PackId;
    // Boss-grade stats (the wave boss curve at the strength wave), but it never marches and never leaks.
    CireNPCCombat::ConfigureArchetype(M, BossId, Wave, 0, Mode->Clock.Round(), true);
    M->bBoss = false; M->LeakCostOverride = 0; M->bPathLeash = false;
    CireRaces::ApplyRank(M, ECireNPCRank::Warlord, 0);
    const FCireSkillProgression& Skills = CireWaveDirector::Config(World).Skills;
    CireRaces::ApplyLoadout(M, Skills, Wave, 99, FMath::Max(1, Skills.MaxTier)); // its complete kit at the top skill tier
    M->Tier = FMath::Clamp(R.LootTier, 1, 10);
    M->MaxHealth = M->Health = HealthFor(BossId, Spot.HealthScale, R); // Eric: 10,000 flat x the boss's and the marker's multipliers
    M->Damage = FMath::Clamp(M->Damage * R.DamageMultiplier, 1.f, 100000.f);
    M->MonsterName = FString::Printf(TEXT("BOSS | %s"), *A->DisplayName);
    // Stand on the lair floor at its real (scaled) size, and remember home: the leash anchor and the pack-reset spot.
    const float Half = M->GetCapsuleComponent() ? M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 100.f;
    M->SetActorLocation(Floor + FVector(0, 0, Half + 15.f), false, nullptr, ETeleportType::TeleportPhysics);
    M->SpawnPosition = M->GetActorLocation();
    M->bHomeLeash = true; M->LeashHome = Spot.Position;
    M->bAlwaysHostile = bHostile;
    if (!bHostile) CireWaveDirector::MakeNeutral(M); // neutral until a player attacks it; bots never do
    else M->bNeutral = false; // sudden death: aggroes champions on sight
    M->ForceNetUpdate();
    Mode->Monsters.Add(M);
    Mode->RewardedPacks.Remove(PackId);
    RuntimeOf(World).ReturnAt.Remove(PackId);
    UE_LOG(LogCireOutdoorBosses, Display, TEXT("CIRE_OUTDOOR_BOSS_SPAWN realm=%d index=%d marker=\"%s\" boss=%s health=%.0f damage=%.0f tier=%d at=(%.0f,%.0f) onnav=%d hostile=%d"), Realm, Index,
        *MarkerName(Spot, Index), *BossId.ToString(), M->MaxHealth, M->Damage, M->Tier, Spot.Position.X, Spot.Position.Y, bOnNav ? 1 : 0, bHostile ? 1 : 0);
    return M;
}
int32 CireOutdoorBosses::SpawnAll(ACireGameMode* Mode)
{
    if (!Mode || !Mode->HasAuthority()) return 0;
    FRuntime& Run = RuntimeOf(Mode->GetWorld());
    Run.bSpawned = true;
    if (!Rules().bEnabled) return 0;
    const FCireBattlefieldRoutes& Routes = CireLanePath::Get(Mode->GetWorld());
    int32 Spawned = 0, Alive = 0;
    for (int32 Realm = 0; Realm < 2; ++Realm)
        for (int32 Index = 0; Index < FMath::Min(Routes.Bosses[Realm].Num(), MaxBosses); ++Index)
        {
            const int32 PackId = PackIdFor(Realm, Index);
            if (AliveBoss(Mode, PackId)) { ++Alive; continue; }
            if (Run.ReturnAt.Contains(PackId)) continue; // slain: it keeps its respawn timer (or stays dead)
            Spawned += SpawnOne(Mode, Realm, Index) ? 1 : 0;
        }
    if (Spawned > 0 || Alive > 0)
        UE_LOG(LogCireOutdoorBosses, Display, TEXT("CIRE_OUTDOOR_BOSSES spawned=%d alive=%d markers=%d/%d"), Spawned, Alive, Routes.Bosses[0].Num(), Routes.Bosses[1].Num());
    return Spawned;
}
void CireOutdoorBosses::OnKilled(ACireGameMode* Mode, ACireMonster* M)
{
    if (!Mode || !IsOutdoorBoss(M)) return;
    FRuntime& Run = RuntimeOf(Mode->GetWorld());
    const float Respawn = Rules().RespawnSeconds;
    Run.ReturnAt.Add(M->PackId, Respawn > 0 ? Run.Clock + Respawn : -1.);
    UE_LOG(LogCireOutdoorBosses, Display, TEXT("CIRE_OUTDOOR_BOSS_SLAIN realm=%d index=%d boss=\"%s\" returns=%s"), M->Lane, IndexOf(M->PackId), *M->GetNPCDisplayName(),
        Respawn > 0 ? *FString::Printf(TEXT("in %.0fs"), Respawn) : TEXT("never (stays dead this match)"));
}
void CireOutdoorBosses::Tick(ACireGameMode* Mode, float Delta)
{
    if (!Mode || !Mode->HasAuthority() || !FMath::IsFinite(Delta) || Delta <= 0) return;
    FRuntime& Run = RuntimeOf(Mode->GetWorld());
    Run.Clock += Delta;
    if (!Run.bSpawned || !Rules().bEnabled || (Run.ScanIn -= Delta) > 0) return;
    if (!Run.bSuddenDeath && Rules().SuddenDeathMinutes > 0 && Run.Clock >= Rules().SuddenDeathMinutes * 60.) { BeginSuddenDeath(Mode); return; }
    Run.ScanIn = 1.;
    const FCireBattlefieldRoutes& Routes = CireLanePath::Get(Mode->GetWorld());
    for (int32 Realm = 0; Realm < 2; ++Realm)
        for (int32 Index = 0; Index < FMath::Min(Routes.Bosses[Realm].Num(), MaxBosses); ++Index)
        {
            const int32 PackId = PackIdFor(Realm, Index);
            if (AliveBoss(Mode, PackId)) { Run.ReturnAt.Remove(PackId); continue; }
            const double* At = Run.ReturnAt.Find(PackId);
            if (!At) { const float Respawn = Rules().RespawnSeconds; Run.ReturnAt.Add(PackId, Respawn > 0 ? Run.Clock + Respawn : -1.); continue; } // gone without a kill
            if (*At < 0 || Run.Clock < *At) continue;
            if (ACireMonster* M = SpawnOne(Mode, Realm, Index, Run.bSuddenDeath && Rules().bSuddenDeathHostile))
                Announce(Mode, FString::Printf(TEXT("WORLD BOSS | %s has returned to its lair (%s, %s)"), *M->GetNPCDisplayName(), *MarkerName(Routes.Bosses[Realm][Index], Index),
                    Realm == 0 ? TEXT("Daylight") : TEXT("Darknight")));
            else Run.ReturnAt.Remove(PackId);
        }
}
void CireOutdoorBosses::Reset(ACireGameMode* Mode)
{
    if (!Mode) return;
    FRuntime& Run = RuntimeOf(Mode->GetWorld());
    Run.ReturnAt.Reset(); Run.ScanIn = 0; Run.Clock = 0; Run.bSuddenDeath = false;
    if (auto* State = Mode->GetGameState<ACireGameState>()) State->bSuddenDeath = false;
}
int32 CireOutdoorBosses::BeginSuddenDeath(ACireGameMode* Mode)
{
    if (!Mode || !Mode->HasAuthority()) return 0;
    FRuntime& Run = RuntimeOf(Mode->GetWorld());
    if (Run.bSuddenDeath) return 0;
    Run.bSuddenDeath = true;
    const bool bHostile = Rules().bSuddenDeathHostile;
    const FCireBattlefieldRoutes& Routes = CireLanePath::Get(Mode->GetWorld());
    int32 Returned = 0;
    for (int32 Realm = 0; Realm < 2; ++Realm)
        for (int32 Index = 0; Index < FMath::Min(Routes.Bosses[Realm].Num(), MaxBosses); ++Index)
        {
            const int32 PackId = PackIdFor(Realm, Index);
            if (ACireMonster* Alive = AliveBoss(Mode, PackId))
            {
                // The living ones join the hunt too, so every world boss behaves the same once sudden death begins.
                if (bHostile) { Alive->bAlwaysHostile = true; Alive->bNeutral = false; Alive->ForceNetUpdate(); }
                continue;
            }
            Run.ReturnAt.Remove(PackId);
            Returned += SpawnOne(Mode, Realm, Index, bHostile) ? 1 : 0;
        }
    if (auto* State = Mode->GetGameState<ACireGameState>()) { State->bSuddenDeath = true; State->ForceNetUpdate(); }
    Announce(Mode, FString::Printf(TEXT("SUDDEN DEATH | The world bosses return%s (%d risen)"), bHostile ? TEXT(" and hunt every champion near their lairs") : TEXT(""), Returned));
    UE_LOG(LogCireOutdoorBosses, Display, TEXT("CIRE_OUTDOOR_BOSS_SUDDEN_DEATH at=%.0fs returned=%d hostile=%d"), Run.Clock, Returned, bHostile ? 1 : 0);
    return Returned;
}
bool CireOutdoorBosses::IsSuddenDeath(const ACireGameMode* Mode) { return Mode && RuntimeOf(Mode->GetWorld()).bSuddenDeath; }
double CireOutdoorBosses::MatchSeconds(const ACireGameMode* Mode) { return Mode ? RuntimeOf(Mode->GetWorld()).Clock : 0.; }
float CireOutdoorBosses::RespawnIn(const ACireGameMode* Mode, int32 PackId)
{
    if (!Mode) return -1.f;
    const FRuntime& Run = RuntimeOf(Mode->GetWorld());
    const double* At = Run.ReturnAt.Find(PackId);
    return At && *At >= 0 ? static_cast<float>(FMath::Max(0., *At - Run.Clock)) : -1.f;
}

#if !UE_BUILD_SHIPPING
// ================================================================================================= tests (-CireCombatExpansionProbe)
bool CireOutdoorBosses::RunTests(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    bool bPass = true; int32 Checks = 0;
    auto Check = [&](bool bValue, const FString& Why) { ++Checks; if (!bValue) { bPass = false; UE_LOG(LogCireOutdoorBosses, Error, TEXT("CIRE_OUTDOOR_BOSS_CHECK_FAIL %s"), *Why); } };
    UWorld* World = Mode->GetWorld();
    // ---- data ----
    FString Json, Error; FCireOutdoorBossRules File;
    Check(FFileHelper::LoadFileToString(Json, *DataPath()) && ParseJson(Json, File, Error), TEXT("OutdoorBosses.json parses: ") + Error);
    const TArray<FName> Bosses = BossIds();
    Check(Bosses.Num() >= 18, FString::Printf(TEXT("every race brings its two bosses (%d)"), Bosses.Num()));
    if (Bosses.Num() < 8) { UE_LOG(LogCireOutdoorBosses, Error, TEXT("CIRE_OUTDOOR_BOSS_TESTS_FAIL no boss roster")); return false; }
    bool bKnown = true;
    for (const auto& Pair : File.ByMarker) bKnown &= IsBossId(Pair.Value);
    for (const FName& Id : File.Roster) bKnown &= IsBossId(Id);
    Check(bKnown, TEXT("every boss the file names is a race boss"));
    TSet<FName> Six;
    for (int32 I = 1; I <= 6; ++I) if (const FName* Id = File.ByMarker.Find(FString::Printf(TEXT("Boss %d"), I))) Six.Add(*Id);
    Check(Six.Num() == 6, TEXT("Boss 1..6 each hold a different boss"));
    Check(File.RespawnSeconds == 0.f && File.SuddenDeathMinutes == 60.f && File.bSuddenDeathHostile && File.BaseHealth == 10000.f && !File.bWaveBossesAtMarkers && File.bEnabled,
        TEXT("Eric's defaults: 10,000 HP, a slain boss stays dead, sudden death at 60 minutes brings them back hostile, wave bosses with their waves"));
    {
        FCireOutdoorBossRules Hp; Hp.BaseHealth = 10000.f; Hp.BossHealth.Add(TEXT("some_boss"), 1.5f);
        Check(FMath::IsNearlyEqual(HealthFor(TEXT("other_boss"), 1.f, Hp), 10000.f) && FMath::IsNearlyEqual(HealthFor(TEXT("some_boss"), 1.f, Hp), 15000.f) &&
            FMath::IsNearlyEqual(HealthFor(TEXT("some_boss"), 2.f, Hp), 30000.f) && FMath::IsNearlyEqual(HealthFor(TEXT("other_boss"), .5f, Hp), 5000.f),
            TEXT("health = 10,000 x the boss's multiplier x the marker's HP x"));
    }
    FCireOutdoorBossRules Bad;
    Check(!ParseJson(TEXT("{\"schemaVersion\":1,\"respawnSeconds\":\"soon\"}"), Bad, Error) && !ParseJson(TEXT("{\"schemaVersion\":1,\"byMarker\":{\"Boss 1\":3}}"), Bad, Error) &&
        !ParseJson(TEXT("{\"schemaVersion\":2}"), Bad, Error), TEXT("bad files are rejected"));
    // Resolution: the marker's own boss, else byMarker by name, else the roster by index; an unknown kind falls through.
    FCireOutdoorBossRules R; R.ByMarker.Add(TEXT("Boss 2"), Bosses[1]); R.Roster = {Bosses[2], Bosses[3]};
    Check(Resolve(Bosses[4].ToString(), TEXT("Boss 2"), 0, R) == Bosses[4], TEXT("a marker's own boss wins"));
    Check(Resolve(FString(), TEXT("Boss 2"), 0, R) == Bosses[1] && Resolve(TEXT("not_a_boss"), TEXT("Boss 2"), 0, R) == Bosses[1], TEXT("byMarker by name next (unknown kinds fall through)"));
    Check(Resolve(FString(), TEXT("Lair"), 3, R) == Bosses[3], TEXT("then the roster in marker order"));
    // Pack-id block: never a jungle pack, never a test fixture id.
    Check(IsOutdoorPackId(PackIdFor(0, 0)) && IsOutdoorPackId(PackIdFor(1, 5)) && IndexOf(PackIdFor(1, 5)) == 5 && PackIdFor(0, 5) != PackIdFor(1, 5), TEXT("outdoor boss ids round-trip per realm"));
    bool bClean = true;
    for (int32 Round = 1; Round < 30; ++Round)
        for (int32 Team = 0; Team < 2; ++Team)
            for (int32 Bay : {0, 1, 17, 4000, 39999, 49999}) bClean &= !IsOutdoorPackId(CireProgression::PackIdFor(Round, Team, Bay));
    for (int32 Fixture : {-1, 0, 7, 999, 9901, 77077}) bClean &= !IsOutdoorPackId(Fixture);
    Check(bClean, TEXT("jungle pack ids and fixture ids are never outdoor bosses"));
    // ---- the world: three Boss spots per realm on the current route ----
    const FCireBattlefieldRoutes Saved = CireLanePath::Get(World);
    const FCireOutdoorBossRules SavedRules = GRules; const bool bSavedLoaded = bRulesLoaded;
    FCireOutdoorBossRules Test = File; Test.RespawnSeconds = 5.f; Test.SuddenDeathMinutes = 0.f; Test.ByMarker.Reset(); Test.Roster = {Bosses[0], Bosses[2], Bosses[4]};
    Test.BossHealth.Reset(); Test.BossHealth.Add(Bosses[4], 1.5f);
    GRules = Test; bRulesLoaded = true;
    const TArray<ACireMonster*> SavedMonsters = Mode->Monsters;
    TArray<AActor*> Spawned;
    FCireBattlefieldRoutes Doc = Saved;
    const TArray<FVector2D>& Route = Saved.LocalPoints[0];
    for (int32 Realm = 0; Realm < 2; ++Realm)
    {
        Doc.Bosses[Realm].Reset();
        for (int32 I = 0; I < 3; ++I)
        {
            FCireRouteSpot S; S.Position = Route[FMath::Clamp(1 + I, 0, Route.Num() - 1)]; S.Yaw = 90.f; S.Name = FString::Printf(TEXT("Lair %d"), I + 1);
            if (I == 1) S.Kind = Bosses[6].ToString(); // the marker's own pick
            if (I == 2) S.HealthScale = 2.f; // the marker's HP x
            Doc.Bosses[Realm].Add(S);
        }
    }
    FString ApplyError;
    const bool bApplied = CireLanePath::ApplyLive(World, Doc, &ApplyError);
    Check(bApplied, TEXT("a route with three Boss spots per realm applies: ") + ApplyError);
    FRuntime& Run = RuntimeOf(World);
    const FRuntime SavedRun = Run;
    Run = FRuntime();
    if (bApplied)
    {
        const int32 Count = SpawnAll(Mode);
        TArray<ACireMonster*> Lairs[2];
        for (ACireMonster* M : Mode->Monsters)
            if (IsOutdoorBoss(M) && !SavedMonsters.Contains(M)) { Spawned.AddUnique(M); M->SetActorTickEnabled(false); Lairs[FMath::Clamp(M->Lane, 0, 1)].Add(M); }
        Check(Count == 6 && Lairs[0].Num() == 3 && Lairs[1].Num() == 3, FString::Printf(TEXT("one world boss per Boss spot in each realm (%d spawned)"), Count));
        Check(SpawnAll(Mode) == 0, TEXT("living bosses are kept (a second spawn adds none)"));
        bool bShape = true, bSame = true;
        TSet<FName> Kinds;
        for (int32 Realm = 0; Realm < 2; ++Realm)
            for (ACireMonster* M : Lairs[Realm])
            {
                const int32 Index = IndexOf(M->PackId);
                const FName Id = M->NPCState ? M->NPCState->ArchetypeId : NAME_None;
                if (Realm == 0) Kinds.Add(Id);
                const FName Want = Index == 1 ? Bosses[6] : Test.Roster[FMath::Clamp(Index, 0, 2)];
                bSame &= Id == Want;
                bShape &= M->bNeutral && !M->bBoss && !M->IsLaneBoss() && M->LeakCostOverride == 0 && M->GetNPCClassification() == ECireNPCClass::Boss && M->bHomeLeash &&
                    M->Tier == Test.LootTier && Doc.Bosses[Realm].IsValidIndex(Index) && M->LeashHome.Equals(Doc.Bosses[Realm][Index].Position, 1.) &&
                    FVector2D::Distance(CireLanePath::ToLocal(Realm, M->GetActorLocation()), Doc.Bosses[Realm][Index].Position) < 600. && CireLeash::Applies(M);
            }
        Check(Kinds.Num() == 3, TEXT("each spot holds a different boss"));
        Check(bSame, TEXT("both realms hold the same boss on the same marker (the marker's own pick, else the roster)"));
        Check(bShape, TEXT("a world boss: neutral, boss-classified, never a lane boss (no leak), its tier, leashed to its lair"));
        if (Lairs[0].Num() == 3)
        {
            TMap<int32, float> Health; for (ACireMonster* M : Lairs[0]) Health.Add(IndexOf(M->PackId), M->MaxHealth);
            Check(FMath::IsNearlyEqual(Health.FindRef(0), 10000.f) && FMath::IsNearlyEqual(Health.FindRef(1), 10000.f) && FMath::IsNearlyEqual(Health.FindRef(2), 30000.f),
                FString::Printf(TEXT("spawned health: 10,000 flat, x1.5 boss x2 marker = 30,000 (got %.0f / %.0f / %.0f)"), Health.FindRef(0), Health.FindRef(1), Health.FindRef(2)));
        }
        if (Lairs[0].Num() == 3 && Lairs[1].Num() == 3)
        {
            ACireMonster* Boss = Lairs[0][0];
            // Bounty: a boss is worth 10x the mob value; those who fought it share it (like the packs).
            namespace CI = Cires::Items;
            Check(CireLoot::BountyKindOf(Boss) == CI::BountyKind::Boss &&
                CireLoot::KillBounty(Mode, Boss) == CI::KillGold(CireLoot::Get().Economy, CI::BountyKind::Boss, CireLoot::BountyWave(Mode, Boss), Test.GoldMultiplier),
                TEXT("the boss bounty is Eric's boss value (10x the mob value)"));
            // Neutral: a bot never opens on it; a player's hit does.
            FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            const FVector Home = Boss->GetActorLocation();
            const FVector2D Lair = Boss->LeashHome;
            auto Near = [&](float Offset) { return CireLanePath::ToWorld(0, Lair + FVector2D(Offset, 0), 0.f) + FVector(0, 0, 120); };
            auto* Bot = World->SpawnActor<ACireHero>(Near(600), FRotator::ZeroRotator, P);
            auto* Player = World->SpawnActor<ACireHero>(Near(900), FRotator::ZeroRotator, P);
            if (Bot && Player)
            {
                Spawned.Add(Bot); Spawned.Add(Player);
                for (ACireHero* H : {Bot, Player}) { H->TeamId = 0; H->Draft(0); H->SetActorTickEnabled(false); H->Health = H->MaxHealth = 5000; }
                Bot->bBot = true; Player->bBot = false;
                Check(!CireWaveDirector::AllowDamage(Boss, Bot) && Boss->bNeutral, TEXT("bots never open on a world boss"));
                Check(CireWaveDirector::AllowDamage(Boss, Player) && !Boss->bNeutral && Boss->Threat.Contains(Player), TEXT("a player's attack provokes it"));
                // Leash: the boss radius around its lair (MonsterLeash.json), threat kept, immune on the way home.
                const FCireLeashRules& L = CireLeash::Rules();
                Check(CireLeash::RadiusFor(L, Boss) == L.RadiusBoss && CireLeash::CanPursue(Boss, Player), TEXT("leashed with the boss radius; a hero near its lair can be pursued"));
                Player->SetActorLocation(Near(L.RadiusBoss + 400.f));
                Check(!CireLeash::CanPursue(Boss, Player), TEXT("a hero far from the lair is not pursued"));
                Player->SetActorLocation(Near(900));
                CireThreat::Select(Boss); CireLeash::Tick(Boss, .05f);
                Check(Boss->Victim == Player && Boss->LeashState == uint8(ECireLeashState::Chase), TEXT("it chases a provoker inside its zone"));
                Boss->SetActorLocation(Near(L.RadiusBoss + 300.f) + FVector(0, 0, 100));
                CireLeash::Tick(Boss, .05f);
                Check(CireLeash::IsReturning(Boss) && !CireLeash::AllowDamage(Boss) && Boss->Threat.Contains(Player) && Boss->LeashReturnPoint.Equals(Lair, 1.),
                    TEXT("kited past the boss radius: it evades home to its lair, immune, threat kept"));
                Boss->SetActorLocation(Home); CireLeash::Tick(Boss, .05f);
                Check(!CireLeash::IsReturning(Boss) && Boss->Threat.Contains(Player), TEXT("home again it waits in its lair with its threat"));
                CireThreat::Clear(Boss); Boss->Victim = nullptr; CireWaveDirector::MakeNeutral(Boss); Boss->LeashState = 0;
            }
            // Death and respawn: the timer (5 s here) brings the same boss back to the same marker; SpawnAll never cuts it short.
            const int32 PackId = Boss->PackId;
            const FName Id = Boss->NPCState ? Boss->NPCState->ArchetypeId : NAME_None;
            OnKilled(Mode, Boss); Mode->Monsters.Remove(Boss); Boss->Destroy();
            Check(RespawnIn(Mode, PackId) > 4.f && SpawnAll(Mode) == 0 && !AliveBoss(Mode, PackId), TEXT("a slain boss waits for its respawn timer (a new cycle does not revive it)"));
            Tick(Mode, 2.f);
            Check(!AliveBoss(Mode, PackId), TEXT("not back before the timer"));
            Tick(Mode, 4.f);
            ACireMonster* Back = AliveBoss(Mode, PackId);
            if (Back) { Spawned.AddUnique(Back); Back->SetActorTickEnabled(false); }
            Check(Back && Back != Boss && Back->NPCState && Back->NPCState->ArchetypeId == Id && Back->bNeutral, TEXT("after respawnSeconds the same boss returns to its lair, neutral"));
            // respawnSeconds 0: it stays dead for the match.
            GRules.RespawnSeconds = 0.f;
            if (Back) { OnKilled(Mode, Back); Mode->Monsters.Remove(Back); Back->Destroy(); }
            Tick(Mode, 5000.f); Tick(Mode, 5000.f);
            Check(!AliveBoss(Mode, PackId) && SpawnAll(Mode) == 0, TEXT("respawnSeconds 0: a slain boss stays dead for the match"));
            // A boss that vanished without a kill (despawned) comes back on the same timer.
            GRules.RespawnSeconds = 3.f; Run.ReturnAt.Remove(PackId);
            Tick(Mode, 1.1f); Tick(Mode, 4.f);
            if (ACireMonster* Again = AliveBoss(Mode, PackId)) { Spawned.AddUnique(Again); Again->SetActorTickEnabled(false); }
            Check(AliveBoss(Mode, PackId) != nullptr, TEXT("a boss gone without a kill comes back on its timer"));
            // SUDDEN DEATH: stays dead (respawn 0) until the match clock passes suddenDeathMinutes; then every dead boss returns HOSTILE.
            GRules.RespawnSeconds = 0.f;
            if (ACireMonster* Dead = AliveBoss(Mode, PackId)) { OnKilled(Mode, Dead); Mode->Monsters.Remove(Dead); Dead->Destroy(); }
            GRules.SuddenDeathMinutes = static_cast<float>((Run.Clock + 120.) / 60.);
            Tick(Mode, 60.f); Tick(Mode, 1.f);
            Check(!AliveBoss(Mode, PackId) && !IsSuddenDeath(Mode), TEXT("a slain boss stays dead before sudden death"));
            Tick(Mode, 70.f); Tick(Mode, 1.f);
            auto* GameState = Mode->GetGameState<ACireGameState>();
            ACireMonster* Risen = AliveBoss(Mode, PackId);
            if (Risen) { Spawned.AddUnique(Risen); Risen->SetActorTickEnabled(false); }
            Check(IsSuddenDeath(Mode) && GameState && GameState->bSuddenDeath, TEXT("sudden death begins at suddenDeathMinutes (replicated for the banner)"));
            Check(Risen && Risen->bAlwaysHostile && !Risen->bNeutral && Risen->bHomeLeash, TEXT("the sudden-death boss returns at its lair, HOSTILE, still leashed"));
            bool bAllHostile = true;
            for (int32 Realm = 0; Realm < 2; ++Realm) for (int32 I = 0; I < 3; ++I) { ACireMonster* B = AliveBoss(Mode, PackIdFor(Realm, I)); bAllHostile &= B && !B->bNeutral && B->bAlwaysHostile; }
            Check(bAllHostile, TEXT("every world boss is hostile in sudden death"));
            if (Risen)
            {
                CireWaveDirector::OnPackReset(Risen);
                Check(!Risen->bNeutral, TEXT("a sudden-death boss resets hostile (never neutral again)"));
                for (AActor* A : Spawned) if (auto* Earlier = Cast<ACireHero>(A)) Earlier->SetActorLocation(Earlier->GetActorLocation() + FVector(0, 0, 200000)); // the earlier fixture heroes leave
                FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                const FVector NearLair = Risen->GetActorLocation() + FVector(1000.f, 0, 0); // farther than the 7 m pack acquisition, inside the hostile radius
                if (auto* Hunter = World->SpawnActor<ACireHero>(NearLair, FRotator::ZeroRotator, SP))
                {
                    Spawned.Add(Hunter); Hunter->TeamId = 0; Hunter->Draft(0); Hunter->SetActorTickEnabled(false); Hunter->bBot = true; Hunter->Health = Hunter->MaxHealth = 5000;
                    Check(CireWaveDirector::AllowDamage(Risen, Hunter), TEXT("a hostile boss can be fought by anyone"));
                    Risen->Victim = nullptr; CireThreat::Clear(Risen); Risen->LeashReengageAt = 0; Risen->LeashState = 0;
                    const auto SavedClock = Mode->Clock; Mode->Clock = Cires::MatchClock(); // survival: NPCs act
                    CireNPCCombat::Tick(Risen, .05f);
                    Mode->Clock = SavedClock;
                    Check(IsValid(Risen->Victim) && Risen->Victim->TeamId == Risen->Lane && FVector::Dist2D(Risen->GetActorLocation(), Risen->Victim->GetActorLocation()) <= GRules.HostileAggroRadius + 1.f, FString::Printf(TEXT("a hostile boss aggroes a champion within its hostile radius on sight (victim=%s threat=%d)"), *GetNameSafe(Risen->Victim), Risen->Threat.Num()));
                }
            }
            Check(BeginSuddenDeath(Mode) == 0, TEXT("sudden death happens once per match"));
            // Disabled: no new world bosses.
            GRules.bEnabled = false;
            Check(SpawnAll(Mode) == 0, TEXT("disabled: no world bosses spawn"));
            GRules.bEnabled = true;
        }
    }
    // Cleanup: the fixture's bosses and heroes, the route, the rules and the timers.
    for (AActor* A : Spawned) if (IsValid(A)) { if (auto* M = Cast<ACireMonster>(A)) Mode->Monsters.Remove(M); A->Destroy(); }
    for (int32 I = Mode->Monsters.Num() - 1; I >= 0; --I)
        if (IsOutdoorBoss(Mode->Monsters[I]) && !SavedMonsters.Contains(Mode->Monsters[I])) { Mode->Monsters[I]->Destroy(); Mode->Monsters.RemoveAt(I); }
    Run = SavedRun;
    GRules = SavedRules; bRulesLoaded = bSavedLoaded;
    if (bApplied) { FString RestoreError; CireLanePath::ApplyLive(World, Saved, &RestoreError); }
    UE_LOG(LogCireOutdoorBosses, Display, TEXT("CIRE_OUTDOOR_BOSS_TESTS_%s checks=%d"), bPass ? TEXT("PASS") : TEXT("FAIL"), Checks);
    return bPass;
}
#endif

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommand OutdoorBossReloadCommand(TEXT("cire.OutdoorBosses"), TEXT("cire.OutdoorBosses reload: re-read Content/Data/OutdoorBosses.json (the next spawn or respawn uses it)."),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>&) { FString Error; CireOutdoorBosses::Reload(&Error); if (!Error.IsEmpty()) UE_LOG(LogCireOutdoorBosses, Warning, TEXT("%s"), *Error); }));
#endif
