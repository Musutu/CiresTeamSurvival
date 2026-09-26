// jungle-packs: tiers, pack types, compositions, the pack monster pool and the kit fill. See CireJunglePacks.h and
// Docs/JunglePacks.md.
#include "CireJunglePacks.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireMonsterArt.h"
#include "CireMonsterExpansion.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "CireNPCArchetypes.h"
#include "CireNPCState.h"
#include "CireRaces.h"
#include "Dom/JsonObject.h"
#include "Math/RandomStream.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireJungle, Log, All);

const FName CireJunglePacks::Mixed(TEXT("mixed"));

namespace
{
TArray<FString> GAudit;

uint32 Mix(uint32 A, uint32 B) { return HashCombine(A * 2654435761u + 0x9e3779b9u, B); }
uint32 NameCrc(FName N) { return FCrc::StrCrc32(*N.ToString()); }

bool OwnsHeal(const FCireNPCArchetype& A)
{
    for (const FCireNPCAbility& Ab : A.Abilities) if (!Ab.bBorrowed && Ab.Kind == ECireNPCAbilityKind::HealAlly) return true;
    return false;
}
int32 OwnKit(const FCireNPCArchetype& A)
{
    int32 N = 0; for (const FCireNPCAbility& Ab : A.Abilities) N += !Ab.bBasic && !Ab.bBorrowed ? 1 : 0; return N;
}
/** The race a unit belongs to for packs: its race, else its family (JunglePacks.json "families"). */
FName PackRaceOf(const FCireNPCArchetype& A, const FCireJungleRules& R)
{
    if (!A.RaceId.IsNone()) return A.RaceId;
    const FName* Family = R.Families.Find(A.Id);
    return Family ? *Family : NAME_None;
}
bool Eligible(const FCireNPCArchetype& A, const FCireJungleRules& R)
{
    return A.Classification != ECireNPCClass::Boss && !R.Excluded.Contains(A.Id) && !PackRaceOf(A, R).IsNone();
}
TArray<FName> SortedIds(const FCireNPCDatabase& D)
{
    TArray<FName> Ids; D.Archetypes.GetKeys(Ids);
    Ids.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
    return Ids;
}
float JsonNum(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, float Default, float Min, float Max)
{
    double V = Default; if (O) O->TryGetNumberField(Key, V);
    return FMath::IsFinite(V) ? FMath::Clamp(static_cast<float>(V), Min, Max) : Default;
}
}

// ================================================================================================ rules
FCireJungleRules CireJunglePacks::BuiltInRules()
{
    FCireJungleRules R;
    // abilities, health, damage, gold, unlock round, unlock wave
    R.Tiers = {{1, 2, 1.f, 1.f, 1.f, 1, 1}, {2, 3, 1.f, 1.1f, 1.5f, 1, 3}, {3, 5, 1.f, 1.25f, 2.25f, 2, 1}, {4, -1, 1.f, 1.4f, 3.f, 3, 1}};
    R.KitFloor = 6; R.LeaderHealth = 1.5f;
    R.Families.Add(TEXT("storm_griffon"), TEXT("feral_kin"));
    R.Families.Add(TEXT("frostfang_alpha"), TEXT("feral_kin"));
    R.Families.Add(TEXT("cinder_drake"), TEXT("drakkari"));
    R.Excluded.Add(TEXT("treasure_goblin")); R.Excluded.Add(TEXT("gilded_stag"));
    return R;
}
bool CireJunglePacks::ParseRules(const FString& Json, FCireJungleRules& Out, FString& Error)
{
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) { Error = TEXT("JunglePacks.json is not valid JSON"); return false; }
    FCireJungleRules R = BuiltInRules();
    const TArray<TSharedPtr<FJsonValue>>* Tiers = nullptr;
    if (!Root->TryGetArrayField(TEXT("tiers"), Tiers) || !Tiers || Tiers->Num() != MaxTier) { Error = TEXT("JunglePacks.json needs 4 tiers"); return false; }
    for (int32 I = 0; I < MaxTier; ++I)
    {
        const TSharedPtr<FJsonObject> O = (*Tiers)[I].IsValid() ? (*Tiers)[I]->AsObject() : nullptr;
        if (!O) { Error = TEXT("every tier must be an object"); return false; }
        FCirePackTier& T = R.Tiers[I]; T.Tier = I + 1;
        const TSharedPtr<FJsonValue> Abilities = O->TryGetField(TEXT("abilities"));
        if (Abilities.IsValid() && Abilities->Type == EJson::String) T.Abilities = -1; // "full": the complete kit
        else T.Abilities = FMath::RoundToInt(JsonNum(O, TEXT("abilities"), T.Abilities, 1, 20));
        T.Health = JsonNum(O, TEXT("health"), T.Health, .1f, 20.f); T.Damage = JsonNum(O, TEXT("damage"), T.Damage, .1f, 20.f);
        T.Gold = JsonNum(O, TEXT("gold"), T.Gold, 0.f, 100.f);
        T.UnlockRound = FMath::RoundToInt(JsonNum(O, TEXT("unlockRound"), T.UnlockRound, 1, 100));
        T.UnlockWave = FMath::RoundToInt(JsonNum(O, TEXT("unlockWave"), T.UnlockWave, 1, 10));
    }
    for (int32 I = 1; I < MaxTier; ++I)
        if (R.Tiers[I - 1].Abilities < 0 || (R.Tiers[I].Abilities >= 0 && R.Tiers[I].Abilities < R.Tiers[I - 1].Abilities))
        { Error = TEXT("tier abilities must not shrink, and only the last tier may be the full kit"); return false; }
    R.KitFloor = FMath::RoundToInt(JsonNum(Root, TEXT("kitFloor"), R.KitFloor, 1, 20));
    if (R.Tiers[2].Abilities >= 0 && R.KitFloor < R.Tiers[2].Abilities) { Error = TEXT("kitFloor must cover tier 3"); return false; }
    if (const TSharedPtr<FJsonObject>* Leader = nullptr; Root->TryGetObjectField(TEXT("leader"), Leader) && Leader)
        R.LeaderHealth = JsonNum(*Leader, TEXT("healthMultiplier"), R.LeaderHealth, 1.f, 10.f);
    if (const TSharedPtr<FJsonObject>* Families = nullptr; Root->TryGetObjectField(TEXT("families"), Families) && Families)
    {
        R.Families.Reset();
        for (const auto& Pair : (*Families)->Values) R.Families.Add(FName(*Pair.Key), FName(*Pair.Value->AsString()));
    }
    if (const TArray<TSharedPtr<FJsonValue>>* Excluded = nullptr; Root->TryGetArrayField(TEXT("exclude"), Excluded) && Excluded)
    {
        R.Excluded.Reset(); for (const auto& V : *Excluded) R.Excluded.Add(FName(*V->AsString()));
    }
    Out = MoveTemp(R); Error.Reset(); return true;
}
const FCireJungleRules& CireJunglePacks::Rules()
{
    static FCireJungleRules Table = []
    {
        FCireJungleRules Loaded; FString Json, Error;
        if (FFileHelper::LoadFileToString(Json, *(FPaths::ProjectContentDir() / TEXT("Data/JunglePacks.json"))) && ParseRules(Json, Loaded, Error)) return Loaded;
        UE_LOG(LogCireJungle, Warning, TEXT("JunglePacks.json not used (%s); built-in jungle rules"), Error.IsEmpty() ? TEXT("missing") : *Error);
        return BuiltInRules();
    }();
    return Table;
}
int32 CireJunglePacks::ClampTier(int32 Tier) { return FMath::Clamp(Tier, MinTier, MaxTier); }
const FCirePackTier& CireJunglePacks::TierRules(int32 Tier) { return Rules().Tiers[ClampTier(Tier) - 1]; }
int32 CireJunglePacks::AbilityCount(int32 Tier, int32 KitSize)
{
    const int32 Want = TierRules(Tier).Abilities;
    return FMath::Max(0, Want < 0 ? KitSize : FMath::Min(Want, KitSize));
}
FString CireJunglePacks::AbilityLabel(int32 Tier)
{
    const int32 Want = TierRules(Tier).Abilities;
    return Want < 0 ? FString(TEXT("full kit")) : FString::Printf(TEXT("%d abilities"), Want);
}

// ================================================================================================ pack types
TArray<FName> CireJunglePacks::PackTypes()
{
    TArray<FName> Out = CireRaces::Get().Order;
    Out.Add(Mixed);
    return Out;
}
FName CireJunglePacks::NormalizeType(const FString& Type)
{
    const FName Id(*Type.TrimStartAndEnd().ToLower());
    return !Id.IsNone() && CireRaces::FindRace(Id) ? Id : Mixed;
}
FString CireJunglePacks::TypeLabel(FName Type)
{
    const FCireRace* Race = Type == Mixed ? nullptr : CireRaces::FindRace(Type);
    if (!Race) return TEXT("Mixed");
    FString Name = Race->Name; Name.RemoveFromStart(TEXT("The "));
    return Name.IsEmpty() ? Type.ToString() : Name;
}
int32 CireJunglePacks::TypeIndex(FName Type) { const TArray<FName> T = PackTypes(); const int32 I = T.IndexOfByKey(Type); return I == INDEX_NONE ? T.Num() - 1 : I; }
FName CireJunglePacks::TypeAt(int32 Index) { const TArray<FName> T = PackTypes(); return T[((Index % T.Num()) + T.Num()) % T.Num()]; }

// ================================================================================================ compositions
bool CireJunglePacks::IsValid(const FCirePackComposition& C)
{
    return C.Tanks >= MinTanks && C.Tanks <= MaxTanks && C.Healers >= MinHealers && C.Healers <= MaxHealers && C.Dps >= MinDps && C.Dps <= MaxDps &&
        C.Total() >= MinSize && C.Total() <= MaxSize;
}
FCirePackComposition CireJunglePacks::Clamp(const FCirePackComposition& In)
{
    FCirePackComposition C;
    C.Tanks = FMath::Clamp(In.Tanks, MinTanks, MaxTanks); C.Healers = FMath::Clamp(In.Healers, MinHealers, MaxHealers); C.Dps = FMath::Clamp(In.Dps, MinDps, MaxDps);
    while (C.Total() > MaxSize)
    {
        if (C.Dps > MinDps) --C.Dps; else if (C.Healers > MinHealers) --C.Healers; else --C.Tanks;
    }
    return C;
}
FCirePackComposition CireJunglePacks::DefaultComposition(uint32 Seed, FName Type, int32 Tier)
{
    Tier = ClampTier(Tier);
    FRandomStream R(static_cast<int32>(Mix(Mix(Seed, NameCrc(Type)), static_cast<uint32>(Tier))));
    // T1 3-4 monsters, T2 4-5, T3 5-6, T4 6.
    static const int32 BaseSize[MaxTier] = {3, 4, 5, 6};
    const int32 Size = FMath::Min(MaxSize, BaseSize[Tier - 1] + (Tier < MaxTier && R.FRand() < .5f ? 1 : 0));
    FCirePackComposition C;
    while (C.Total() < Size)
    {
        // DPS weigh double; a role at its maximum is skipped.
        TArray<int32> Choices;
        if (C.Tanks < MaxTanks) Choices.Add(0);
        if (C.Healers < MaxHealers) Choices.Add(1);
        if (C.Dps < MaxDps) { Choices.Add(2); Choices.Add(2); }
        if (Choices.Num() == 0) break;
        const int32 Pick = Choices[R.RandRange(0, Choices.Num() - 1)];
        (Pick == 0 ? C.Tanks : Pick == 1 ? C.Healers : C.Dps) += 1;
    }
    return C;
}
FCirePackComposition CireJunglePacks::Resolve(const FCirePackComposition* Override, uint32 Seed, FName Type, int32 Tier)
{
    if (Override && (Override->Tanks > 0 || Override->Healers > 0 || Override->Dps > 0)) return Clamp(*Override);
    return DefaultComposition(Seed, Type, Tier);
}
FString CireJunglePacks::Summary(int32 Tier, FName Type, const FCirePackComposition& C)
{
    Tier = ClampTier(Tier);
    return FString::Printf(TEXT("T%d %s: %d tank · %d healer · %d DPS · %s"), Tier, *TypeLabel(Type), C.Tanks, C.Healers, C.Dps,
        TierRules(Tier).Abilities < 0 ? TEXT("full kit each") : *FString::Printf(TEXT("%d abilities each"), TierRules(Tier).Abilities));
}
uint32 CireJunglePacks::SeedFor(const FVector2D& Local)
{
    const int32 X = FMath::RoundToInt32(Local.X / 10.), Y = FMath::RoundToInt32(Local.Y / 10.);
    return Mix(static_cast<uint32>(X), static_cast<uint32>(Y)) & 0x7fffffffu;
}

// ================================================================================================ pool
ECirePackRole CireJunglePacks::RoleOf(const FCireNPCArchetype& A)
{
    if (A.Role == ECireNPCRole::Tank) return ECirePackRole::Tank;
    return OwnsHeal(A) ? ECirePackRole::Healer : ECirePackRole::Dps;
}
const TCHAR* CireJunglePacks::RoleName(ECirePackRole Role) { return Role == ECirePackRole::Tank ? TEXT("tank") : Role == ECirePackRole::Healer ? TEXT("healer") : TEXT("DPS"); }
TArray<FCirePackUnit> CireJunglePacks::Inventory()
{
    TArray<FCirePackUnit> Out;
    const FCireNPCDatabase& D = CireNPCArchetypes::Get();
    const FCireJungleRules& R = Rules();
    for (const FName Id : SortedIds(D))
    {
        const FCireNPCArchetype& A = D.Archetypes[Id];
        if (!Eligible(A, R)) continue;
        FCirePackUnit U; U.Id = Id; U.Race = PackRaceOf(A, R); U.Role = RoleOf(A); U.OwnKit = OwnKit(A);
        for (const FCireNPCAbility& Ab : A.Abilities) U.Borrowed += Ab.bBorrowed ? 1 : 0;
        U.bCreature = CireMonsterExpansion::Find(Id) != nullptr;
        Out.Add(U);
    }
    return Out;
}
TArray<FName> CireJunglePacks::Pool(FName Type, ECirePackRole Role, bool* bOutStandIn)
{
    if (bOutStandIn) *bOutStandIn = false;
    TArray<FName> Out, Any;
    for (const FCirePackUnit& U : Inventory())
    {
        if (U.Role != Role) continue;
        Any.Add(U.Id);
        // Mixed draws from every race; a race keeps its own units (rare creatures stay Mixed-only).
        if (Type == Mixed || (U.Race == Type && !(U.bCreature && CireNPCArchetypes::Find(U.Id) && CireNPCArchetypes::Find(U.Id)->RaceId.IsNone()))) Out.Add(U.Id);
    }
    if (Out.Num() == 0) { if (bOutStandIn) *bOutStandIn = Any.Num() > 0; return Any; }
    return Out;
}
TArray<FName> CireJunglePacks::Members(uint32 Seed, FName Type, const FCirePackComposition& In, TArray<ECirePackRole>* OutRoles)
{
    const FCirePackComposition C = Clamp(In);
    TArray<FName> Out;
    if (OutRoles) OutRoles->Reset();
    for (const ECirePackRole Role : {ECirePackRole::Tank, ECirePackRole::Healer, ECirePackRole::Dps})
    {
        TArray<FName> Candidates = Pool(Type, Role);
        if (Candidates.Num() == 0) continue;
        FRandomStream R(static_cast<int32>(Mix(Seed, 17u + static_cast<uint32>(Role))));
        for (int32 I = Candidates.Num() - 1; I > 0; --I) Candidates.Swap(I, R.RandRange(0, I));
        for (int32 I = 0; I < C.Count(Role); ++I) { Out.Add(Candidates[I % Candidates.Num()]); if (OutRoles) OutRoles->Add(Role); }
    }
    return Out;
}
int32 CireJunglePacks::KitSize(const FCireNPCArchetype& A)
{
    int32 N = 0; for (const FCireNPCAbility& Ab : A.Abilities) N += Ab.bBasic ? 0 : 1; return N;
}
TArray<FName> CireJunglePacks::TierLoadout(const FCireNPCArchetype& A, int32 Tier, int32 Seed)
{
    TArray<FName> Order;
    for (const FCireNPCAbility& Ab : A.Abilities) if (!Ab.bBasic && Ab.bCore && !Ab.bBorrowed) Order.Add(Ab.Id);
    for (const FName Id : CireRaces::MatchOrder(Seed, A)) Order.AddUnique(Id);
    for (const FCireNPCAbility& Ab : A.Abilities) if (!Ab.bBasic && Ab.bBorrowed) Order.AddUnique(Ab.Id);
    Order.SetNum(FMath::Min(Order.Num(), AbilityCount(Tier, KitSize(A))));
    return Order;
}
void CireJunglePacks::MergeInto(FCireNPCDatabase& D)
{
    GAudit.Reset();
    const FCireJungleRules& R = Rules();
    const TArray<FName> Ids = SortedIds(D);
    int32 Filled = 0, Units = 0;
    for (const FName Id : Ids)
    {
        FCireNPCArchetype& A = D.Archetypes[Id];
        if (!Eligible(A, R)) continue;
        ++Units;
        A.Abilities.RemoveAll([](const FCireNPCAbility& Ab) { return Ab.bBorrowed; }); // a reload starts clean
        const int32 Own = OwnKit(A);
        if (Own >= R.KitFloor) continue;
        const FName Race = PackRaceOf(A, R);
        const ECirePackRole Role = RoleOf(A);
        // Donors: the race's units and bosses (Races.json slots), same pack role first (then the rest), in id order.
        TArray<const FCireNPCArchetype*> Donors;
        // Same combat role first (a bruiser borrows from bruisers), then the same pack role, then the rest.
        for (int32 Pass = 0; Pass < 3; ++Pass)
            for (const FName Other : Ids)
            {
                const FCireNPCArchetype& O = D.Archetypes[Other];
                // Race units and bosses only (a race slot): creatures' skills are off the race's theme.
                if (Other == Id || O.RaceId != Race || O.Slot.IsNone() || R.Excluded.Contains(Other)) continue;
                const int32 Rank = O.Role == A.Role ? 0 : RoleOf(O) == Role ? 1 : 2;
                if (Rank == Pass) Donors.Add(&O);
            }
        TArray<FString> Took;
        for (const FCireNPCArchetype* O : Donors)
        {
            for (const FCireNPCAbility& Ab : O->Abilities)
            {
                if (Own + Took.Num() >= R.KitFloor) break;
                if (Ab.bBasic || Ab.bBorrowed || A.FindAbility(Ab.Id)) continue;
                // Summons and constructs belong to their own unit; heals stay with the healers.
                if (Ab.Kind == ECireNPCAbilityKind::Summon || Ab.Kind == ECireNPCAbilityKind::Deploy) continue;
                if (Ab.Kind == ECireNPCAbilityKind::HealAlly && Role != ECirePackRole::Healer) continue;
                if (Ab.Kind == ECireNPCAbilityKind::Disengage && !CireNPCArchetypes::IsRangedRole(A.Role)) continue; // melee units hold their ground
                FCireNPCAbility Copy = Ab; Copy.bBorrowed = true; Copy.bCore = false;
                A.Abilities.Add(Copy);
                Took.Add(FString::Printf(TEXT("%s (%s)"), *Ab.Id.ToString(), *O->Id.ToString()));
            }
            if (Own + Took.Num() >= R.KitFloor) break;
        }
        ++Filled;
        const FString Line = FString::Printf(TEXT("%s [%s %s]: own kit %d, borrowed %d from %s%s%s: %s"), *Id.ToString(), *Race.ToString(), CireJunglePacks::RoleName(Role),
            Own, Took.Num(), A.RaceId.IsNone() ? TEXT("its family race ") : TEXT("its race "), *Race.ToString(),
            Own + Took.Num() < R.KitFloor ? TEXT(" (STILL SHORT)") : TEXT(""), *FString::Join(Took, TEXT(", ")));
        GAudit.Add(Line);
    }
    // Stand-ins: a race without a unit in a role borrows that slot from the Mixed pool.
    TSet<FName> Races;
    for (const FName Id : Ids) { const FCireNPCArchetype& A = D.Archetypes[Id]; if (Eligible(A, R) && !A.RaceId.IsNone()) Races.Add(A.RaceId); }
    for (const FName Race : Races)
        for (const ECirePackRole Role : {ECirePackRole::Tank, ECirePackRole::Healer, ECirePackRole::Dps})
        {
            bool bHas = false;
            for (const FName Id : Ids) { const FCireNPCArchetype& A = D.Archetypes[Id]; bHas |= Eligible(A, R) && A.RaceId == Race && RoleOf(A) == Role; }
            if (!bHas) GAudit.Add(FString::Printf(TEXT("STAND-IN %s has no %s: that slot draws a %s from the Mixed pool"), *Race.ToString(), CireJunglePacks::RoleName(Role), CireJunglePacks::RoleName(Role)));
        }
    UE_LOG(LogCireJungle, Display, TEXT("CIRE_JUNGLE_POOL units=%d filled=%d kitFloor=%d"), Units, Filled, R.KitFloor);
    for (const FString& L : GAudit) UE_LOG(LogCireJungle, Verbose, TEXT("CIRE_JUNGLE_AUDIT %s"), *L);
}
const TArray<FString>& CireJunglePacks::Audit() { return GAudit; }

// ================================================================================================ runtime
FVector2D CireJunglePacks::FormationOffset(int32 Index, const TArray<ECirePackRole>& Roles, float Radius, uint32 Seed)
{
    if (!Roles.IsValidIndex(Index)) return FVector2D::ZeroVector;
    const ECirePackRole Role = Roles[Index];
    int32 InRole = 0, Count = 0;
    for (int32 I = 0; I < Roles.Num(); ++I) if (Roles[I] == Role) { if (I < Index) ++InRole; ++Count; }
    // Rows: tanks in front, DPS in the middle, healers behind; each row spread across the facing.
    const float R = FMath::Max(Radius, 200.f);
    const float Row = Role == ECirePackRole::Tank ? .3f : Role == ECirePackRole::Healer ? -.4f : -.02f;
    const float Spacing = FMath::Clamp(R * .32f, 110.f, 260.f);
    FVector2D Local(Row * R, (InRole - (Count - 1) * .5f) * Spacing);
    if (Local.Size() > R * .8f) Local *= R * .8f / Local.Size();
    const float Yaw = FMath::DegreesToRadians(static_cast<float>(Seed % 360u));
    return FVector2D(Local.X * FMath::Cos(Yaw) - Local.Y * FMath::Sin(Yaw), Local.X * FMath::Sin(Yaw) + Local.Y * FMath::Cos(Yaw));
}
void CireJunglePacks::PrewarmBodies(const UWorld* World)
{
    static FStreamableManager Streamable;
    static TSet<FName> Warmed;
    static TArray<TSharedPtr<FStreamableHandle>> Handles;
    // Bodies only matter where monsters are drawn (clients, listen servers, standalone): never on a dedicated server.
    if (!World || IsRunningCommandlet() || World->GetNetMode() == NM_DedicatedServer) return;
    // Exactly the units the realm's packs will spawn (their composition at every tier they can reach), not the whole
    // pool: a huge async queue is flushed by the first synchronous body load and hitches the frame far longer.
    TSet<FName> Units;
    const FCireBattlefieldRoutes& Routes = CireLanePath::Get(World);
    for (int32 Team = 0; Team < 2; ++Team)
        for (int32 Bay = 1, Count = CireLanePath::BayCount(Routes, Team); Bay <= Count; ++Bay)
        {
            const FCireChallengeBay Pack = CireLanePath::BayAt(Routes, Team, Bay);
            for (int32 Tier = Pack.Tier; Tier <= MaxTier; ++Tier)
                for (const FName Id : Members(Pack.EffectiveSeed(), Pack.PackType, Resolve(Pack.HasCompOverride() ? &Pack.Comp : nullptr, Pack.EffectiveSeed(), Pack.PackType, Tier)))
                    Units.Add(Id);
        }
    TArray<FSoftObjectPath> Paths;
    for (const FName Id : Units)
    {
        if (Warmed.Contains(Id)) continue;
        Warmed.Add(Id);
        if (const CireMonsterArt::FArchetypeArt* Art = CireMonsterArt::Find(Id))
            for (const CireMonsterArt::FBody& Body : Art->Bodies)
            {
                for (const FString* P : {&Body.MeshPath, &Body.MeshOverride}) if (!P->IsEmpty()) Paths.AddUnique(FSoftObjectPath(*P));
                for (const auto& Clip : Body.Roles) if (!Clip.Value.IsEmpty()) Paths.AddUnique(FSoftObjectPath(Clip.Value));
            }
    }
    if (Paths.Num() == 0) return;
    if (TSharedPtr<FStreamableHandle> Handle = Streamable.RequestAsyncLoad(Paths, FStreamableDelegate())) Handles.Add(Handle);
    UE_LOG(LogCireJungle, Display, TEXT("CIRE_JUNGLE_PREWARM units=%d assets=%d"), Units.Num(), Paths.Num());
}
void CireJunglePacks::ApplyTier(ACireMonster* M, int32 Tier, int32 Seed)
{
    UCireNPCState* S = M ? M->NPCState.Get() : nullptr;
    const FCireNPCArchetype* A = S ? S->Archetype() : nullptr;
    if (!S || !A || !M->HasAuthority()) return;
    Tier = ClampTier(Tier);
    const FCirePackTier& T = TierRules(Tier);
    S->Loadout = TierLoadout(*A, Tier, Seed);
    S->bLoadoutSet = true;
    S->SkillTier = static_cast<uint8>(S->Loadout.IsEmpty() ? 0 : FMath::Clamp(Tier, 1, 3));
    M->Damage = FMath::Clamp(M->Damage * T.Damage, 0.f, 100000.f);
    M->MaxHealth = M->Health = FMath::Clamp(M->MaxHealth * T.Health, 1.f, 1.e9f);
    M->ForceNetUpdate();
}

#if !UE_BUILD_SHIPPING
bool CireJunglePacks::RunTests(TArray<FString>& Failures)
{
    const int32 Before = Failures.Num();
    auto Check = [&](bool bOk, const FString& What) { if (!bOk) Failures.Add(TEXT("jungle: ") + What); };
    // Tiers: 2 / 3 / 5 / the complete kit.
    Check(AbilityCount(1, 9) == 2 && AbilityCount(2, 9) == 3 && AbilityCount(3, 9) == 5 && AbilityCount(4, 9) == 9 && AbilityCount(4, 6) == 6, TEXT("tier ability counts 2/3/5/full"));
    Check(ClampTier(0) == 1 && ClampTier(7) == 4, TEXT("tiers clamp to 1..4"));
    FCireJungleRules Parsed; FString Error;
    Check(ParseRules(TEXT("{\"tiers\":[{\"abilities\":2},{\"abilities\":3},{\"abilities\":5},{\"abilities\":\"full\"}],\"kitFloor\":6}"), Parsed, Error) &&
        Parsed.Tiers[3].Abilities < 0 && Parsed.Tiers[2].Abilities == 5, TEXT("JunglePacks.json parses (") + Error + TEXT(")"));
    Check(!ParseRules(TEXT("{\"tiers\":[{\"abilities\":3},{\"abilities\":2},{\"abilities\":5},{\"abilities\":\"full\"}]}"), Parsed, Error), TEXT("shrinking tiers are rejected"));
    // Compositions: every seed, type and tier gives a valid default; overrides clamp.
    int32 Bad = 0, Sizes[MaxSize + 1] = {};
    for (const FName Type : PackTypes())
        for (int32 Tier = 1; Tier <= MaxTier; ++Tier)
            for (uint32 Seed = 0; Seed < 300; ++Seed)
            {
                const FCirePackComposition C = DefaultComposition(Seed * 7919u, Type, Tier);
                Bad += IsValid(C) ? 0 : 1; if (C.Total() <= MaxSize) ++Sizes[C.Total()];
                if (Seed == 0) Check(DefaultComposition(0, Type, Tier) == C, TEXT("default composition is deterministic"));
            }
    Check(Bad == 0, FString::Printf(TEXT("%d default compositions invalid"), Bad));
    Check(Sizes[3] > 0 && Sizes[6] > 0, TEXT("defaults span 3..6 monsters"));
    for (int32 T = -2; T <= 5; ++T) for (int32 H = -2; H <= 5; ++H) for (int32 D = -2; D <= 6; ++D)
        Bad += IsValid(Clamp({T, H, D})) ? 0 : 1;
    Check(Bad == 0, TEXT("every override clamps to a valid composition"));
    Check(Clamp({2, 2, 3}) == FCirePackComposition{2, 2, 2} && Clamp({0, 9, 0}) == FCirePackComposition{1, 2, 1}, TEXT("clamp trims DPS first to keep 6"));
    const FCirePackComposition Over{2, 1, 3};
    Check(Resolve(&Over, 1, Mixed, 1) == Over && Resolve(nullptr, 5, Mixed, 2) == DefaultComposition(5, Mixed, 2), TEXT("override beats the default"));
    Check(Summary(3, Mixed, {2, 1, 3}).Contains(TEXT("5 abilities each")) && Summary(4, Mixed, {1, 1, 1}).Contains(TEXT("full kit")), TEXT("summary line"));
    Check(NormalizeType(TEXT("")) == Mixed && NormalizeType(TEXT("nonsense")) == Mixed, TEXT("unknown pack types read as Mixed"));
    // The pool: every race fields every role; every unit fills T3 and T4.
    const TArray<FCirePackUnit> Units = Inventory();
    Check(Units.Num() >= 60, FString::Printf(TEXT("pool has %d units"), Units.Num()));
    for (const FName Type : PackTypes())
        for (const ECirePackRole Role : {ECirePackRole::Tank, ECirePackRole::Healer, ECirePackRole::Dps})
            Check(Pool(Type, Role).Num() > 0, FString::Printf(TEXT("%s has a %s"), *Type.ToString(), RoleName(Role)));
    for (const FCirePackUnit& U : Units)
    {
        const FCireNPCArchetype* A = CireNPCArchetypes::Find(U.Id);
        if (!A) { Check(false, U.Id.ToString() + TEXT(" missing")); continue; }
        Check(KitSize(*A) >= Rules().KitFloor, U.Id.ToString() + TEXT(" reaches the kit floor"));
        const int32 N[MaxTier] = {TierLoadout(*A, 1, 3).Num(), TierLoadout(*A, 2, 3).Num(), TierLoadout(*A, 3, 3).Num(), TierLoadout(*A, 4, 3).Num()};
        Check(N[0] == 2 && N[1] == 3 && N[2] == 5 && N[3] == KitSize(*A) && N[3] > N[2], U.Id.ToString() + FString::Printf(TEXT(" tier loadouts %d/%d/%d/%d"), N[0], N[1], N[2], N[3]));
        for (const FCireNPCAbility& Ab : A->Abilities)
            if (Ab.bBorrowed)
            {
                Check(!CireRaces::MatchOrder(1, *A).Contains(Ab.Id), U.Id.ToString() + TEXT(": borrowed skills never enter the wave pool"));
                Check(Ab.Kind != ECireNPCAbilityKind::Summon && Ab.Kind != ECireNPCAbilityKind::Deploy, TEXT("summons are never borrowed"));
            }
    }
    // Members: roles in order, counts match, race packs stay in their race.
    for (const FName Type : PackTypes())
        for (uint32 Seed = 1; Seed < 40; ++Seed)
        {
            const FCirePackComposition C = DefaultComposition(Seed, Type, 1 + Seed % 4);
            TArray<ECirePackRole> Roles;
            const TArray<FName> M = Members(Seed, Type, C, &Roles);
            bool bRace = true; for (const FName Id : M) bRace &= Type == Mixed || (CireNPCArchetypes::Find(Id) && CireNPCArchetypes::Find(Id)->RaceId == Type);
            Check(M.Num() == C.Total() && Roles.Num() == M.Num() && bRace, FString::Printf(TEXT("%s pack members follow the composition"), *Type.ToString()));
            for (int32 I = 0; I < M.Num(); ++I)
            {
                const FCireNPCArchetype* A = CireNPCArchetypes::Find(M[I]);
                Check(A && RoleOf(*A) == Roles[I], TEXT("each member fills its role"));
                Check(FormationOffset(I, Roles, 450.f, Seed).Size() <= 450.f * .81f, TEXT("formation stays inside the pack radius"));
            }
        }
    return Failures.Num() == Before;
}
#endif
