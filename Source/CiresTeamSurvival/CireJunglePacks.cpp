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
#include "CireDeveloperTools.h"
#include "CireNav.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
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
    // pack-formations: authored packs spawn at their tier from wave 1 (the unlocks gated T3 / T4 to later cycles, so a layout's
    // T3 / T4 packs never appeared in a playtest); health / damage are the per-tier multipliers on the challenge-mob stats.
    R.Tiers = {{1, 2, 1.f, 1.f, 1.f, 1, 1}, {2, 3, 1.65f, 1.1f, 1.5f, 1, 1}, {3, 5, 2.3f, 1.25f, 2.25f, 1, 1}, {4, -1, 2.95f, 1.4f, 3.f, 1, 1}};
    R.KitFloor = 6; R.LeaderHealth = 1.5f;
    R.Families.Add(TEXT("storm_griffon"), TEXT("feral_kin"));
    R.Families.Add(TEXT("frostfang_alpha"), TEXT("feral_kin"));
    R.Families.Add(TEXT("cinder_drake"), TEXT("drakkari"));
    R.Excluded.Add(TEXT("treasure_goblin")); R.Excluded.Add(TEXT("gilded_stag"));
    // pack-formations: Eric's formations (Docs/EricFeedback/2026-09-27/pack-formations.png): forward = toward the facing.
    using P = ECirePackRole;
    const P T = P::Tank, H = P::Healer, D = P::Dps;
    R.Formations[3] = {{T, .35f, 0.f}, {H, -.18f, .53f}, {D, -.18f, -.53f}};
    R.Formations[4] = {{T, .26f, .31f}, {T, .26f, -.83f}, {H, -.26f, .79f}, {D, -.27f, -.26f}};
    R.Formations[5] = {{T, .57f, 0.f}, {D, .28f, -.85f}, {D, .28f, .85f}, {H, -.55f, 1.23f}, {H, -.55f, -1.23f}};
    R.Formations[6] = {{D, .42f, -1.5f}, {D, .42f, 1.5f}, {T, .12f, .57f}, {T, .12f, -.57f}, {H, -.55f, -1.15f}, {H, -.55f, 1.15f}};
    R.Formations[7] = {{D, .5f, -1.5f}, {D, .5f, 1.5f}, {T, .2f, .57f}, {T, .2f, -.57f}, {H, -.47f, -1.15f}, {H, -.47f, 1.15f}, {D, -.47f, 0.f}};
    R.Formations[8] = {{T, .78f, 0.f}, {D, .4f, -1.5f}, {D, .4f, 1.5f}, {T, .1f, .62f}, {T, .1f, -.62f}, {H, -.58f, 1.15f}, {D, -.58f, 0.f}, {H, -.58f, -1.15f}};
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
    // pack-formations: challenge-mob stats.
    if (const TSharedPtr<FJsonObject>* Stats = nullptr; Root->TryGetObjectField(TEXT("stats"), Stats) && Stats)
    {
        FCirePackStats& S = R.Stats;
        S.BaseDamage = JsonNum(*Stats, TEXT("baseDamage"), S.BaseDamage, 0.f, 100000.f);
        S.TankHealth = JsonNum(*Stats, TEXT("tankHealth"), S.TankHealth, 1.f, 1.e7f);
        S.HealerShare = JsonNum(*Stats, TEXT("healerShare"), S.HealerShare, .01f, 10.f);
        S.MeleeShare = JsonNum(*Stats, TEXT("meleeShare"), S.MeleeShare, .01f, 10.f);
        S.RangedShare = JsonNum(*Stats, TEXT("rangedShare"), S.RangedShare, .01f, 10.f);
        S.CasterShare = JsonNum(*Stats, TEXT("casterShare"), S.CasterShare, .01f, 10.f);
        S.GlobalHealth = JsonNum(*Stats, TEXT("globalHealth"), S.GlobalHealth, .01f, 100.f);
        S.GlobalDamage = JsonNum(*Stats, TEXT("globalDamage"), S.GlobalDamage, .01f, 100.f);
    }
    // pack-formations: "formations": { "3": [ { "role": "tank", "forward": .35, "right": 0 }, ... ], ... "8": [...] }.
    if (const TSharedPtr<FJsonObject>* Formations = nullptr; Root->TryGetObjectField(TEXT("formations"), Formations) && Formations)
        for (int32 Size = MinSize; Size <= MaxSize; ++Size)
        {
            const TArray<TSharedPtr<FJsonValue>>* Slots = nullptr;
            if (!(*Formations)->TryGetArrayField(FString::FromInt(Size), Slots) || !Slots) continue;
            if (Slots->Num() != Size) { Error = FString::Printf(TEXT("formation %d needs %d slots"), Size, Size); return false; }
            TArray<FCireFormationSlot> Out;
            for (const TSharedPtr<FJsonValue>& V : *Slots)
            {
                const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
                if (!O) { Error = TEXT("formation slots are objects"); return false; }
                FCireFormationSlot Slot; FString Role; O->TryGetStringField(TEXT("role"), Role);
                Slot.Role = Role.StartsWith(TEXT("t")) ? ECirePackRole::Tank : Role.StartsWith(TEXT("h")) ? ECirePackRole::Healer : ECirePackRole::Dps;
                Slot.Forward = JsonNum(O, TEXT("forward"), 0.f, -5.f, 5.f); Slot.Right = JsonNum(O, TEXT("right"), 0.f, -5.f, 5.f);
                Out.Add(Slot);
            }
            R.Formations[Size] = MoveTemp(Out);
        }
    Out = MoveTemp(R); Error.Reset(); return true;
}
namespace
{
FCireJungleRules& MutableRules()
{
    static FCireJungleRules Table = []
    {
        FCireJungleRules Loaded; FString Json, Error;
        if (FFileHelper::LoadFileToString(Json, *(FPaths::ProjectContentDir() / TEXT("Data/JunglePacks.json"))) && CireJunglePacks::ParseRules(Json, Loaded, Error)) return Loaded;
        UE_LOG(LogCireJungle, Warning, TEXT("JunglePacks.json not used (%s); built-in jungle rules"), Error.IsEmpty() ? TEXT("missing") : *Error);
        return CireJunglePacks::BuiltInRules();
    }();
    return Table;
}
FString RulesPath() { return FPaths::ProjectContentDir() / TEXT("Data/JunglePacks.json"); }
}
const FCireJungleRules& CireJunglePacks::Rules() { return MutableRules(); }
bool CireJunglePacks::IsDps(ECirePackRole Role) { return Role != ECirePackRole::Tank && Role != ECirePackRole::Healer; }
float CireJunglePacks::UnitHealth(ECirePackRole Role, int32 Tier, bool bLeader)
{
    const FCireJungleRules& R = Rules(); const FCirePackStats& S = R.Stats;
    const float Share = Role == ECirePackRole::Tank ? 1.f : Role == ECirePackRole::Healer ? S.HealerShare : Role == ECirePackRole::Melee ? S.MeleeShare :
        Role == ECirePackRole::Ranged ? S.RangedShare : Role == ECirePackRole::Caster ? S.CasterShare : S.MeleeShare;
    return FMath::Clamp(S.TankHealth * Share * TierRules(Tier).Health * S.GlobalHealth * (bLeader ? R.LeaderHealth : 1.f), 1.f, 1.e9f);
}
float CireJunglePacks::UnitDamage(ECirePackRole, int32 Tier)
{
    const FCirePackStats& S = Rules().Stats;
    return FMath::Clamp(S.BaseDamage * TierRules(Tier).Damage * S.GlobalDamage, 0.f, 1.e6f);
}
void CireJunglePacks::SetStats(UWorld* World, const FCirePackStats& Stats, const float TierHealth[4], const float TierDamage[4])
{
    FCireJungleRules& R = MutableRules();
    R.Stats = Stats;
    for (int32 I = 0; I < MaxTier; ++I) { R.Tiers[I].Health = FMath::Clamp(TierHealth[I], .01f, 100.f); R.Tiers[I].Damage = FMath::Clamp(TierDamage[I], .01f, 100.f); }
    // Every living pack monster takes the new numbers now (its health fraction is kept).
    ACireGameMode* Mode = World ? World->GetAuthGameMode<ACireGameMode>() : nullptr;
    if (!Mode) return;
    int32 Changed = 0;
    for (ACireMonster* M : Mode->Monsters)
    {
        const FCireNPCArchetype* A = ::IsValid(M) && M->PackId >= 0 && M->NPCState ? M->NPCState->Archetype() : nullptr;
        if (!A || M->Health <= 0) continue;
        const float Fraction = M->MaxHealth > 0 ? FMath::Clamp(M->Health / M->MaxHealth, 0.f, 1.f) : 1.f;
        const bool bLeader = M->NPCState->Classification == ECireNPCClass::Boss;
        M->MaxHealth = UnitHealth(RoleOf(*A), M->Tier, bLeader); M->Health = FMath::Max(1.f, M->MaxHealth * Fraction);
        M->Damage = UnitDamage(RoleOf(*A), M->Tier);
        M->ForceNetUpdate(); ++Changed;
    }
    UE_LOG(LogCireJungle, Display, TEXT("CIRE_JUNGLE_STATS dmg=%.0f tank=%.0f global=%.2f/%.2f rescaled=%d"), Stats.BaseDamage, Stats.TankHealth, Stats.GlobalHealth, Stats.GlobalDamage, Changed);
}
bool CireJunglePacks::SaveStats(FString* Error)
{
    FString Json; TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Json, *RulesPath()) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root)
    { if (Error) *Error = TEXT("JunglePacks.json could not be read"); return false; }
    const FCireJungleRules& R = Rules(); const FCirePackStats& S = R.Stats;
    TSharedPtr<FJsonObject> Stats = MakeShared<FJsonObject>();
    Stats->SetNumberField(TEXT("baseDamage"), S.BaseDamage); Stats->SetNumberField(TEXT("tankHealth"), S.TankHealth);
    Stats->SetNumberField(TEXT("healerShare"), S.HealerShare); Stats->SetNumberField(TEXT("meleeShare"), S.MeleeShare);
    Stats->SetNumberField(TEXT("rangedShare"), S.RangedShare); Stats->SetNumberField(TEXT("casterShare"), S.CasterShare);
    Stats->SetNumberField(TEXT("globalHealth"), S.GlobalHealth); Stats->SetNumberField(TEXT("globalDamage"), S.GlobalDamage);
    Root->SetObjectField(TEXT("stats"), Stats);
    const TArray<TSharedPtr<FJsonValue>>* Tiers = nullptr;
    if (Root->TryGetArrayField(TEXT("tiers"), Tiers) && Tiers)
        for (int32 I = 0; I < FMath::Min(Tiers->Num(), MaxTier); ++I)
            if (const TSharedPtr<FJsonObject> O = (*Tiers)[I].IsValid() ? (*Tiers)[I]->AsObject() : nullptr)
            { O->SetNumberField(TEXT("health"), FMath::RoundToFloat(R.Tiers[I].Health * 1000.f) / 1000.f); O->SetNumberField(TEXT("damage"), FMath::RoundToFloat(R.Tiers[I].Damage * 1000.f) / 1000.f); }
    FString Out;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
    if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer) || !FFileHelper::SaveStringToFile(Out + TEXT("\n"), *RulesPath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    { if (Error) *Error = TEXT("JunglePacks.json could not be written"); return false; }
    return true;
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
    return C.Tanks >= MinTanks && C.Tanks <= MaxTanks && C.Healers >= MinHealers && C.Healers <= MaxHealers && C.DpsTotal() >= MinDps && C.DpsTotal() <= MaxDps &&
        C.Dps >= 0 && C.Melee >= 0 && C.Ranged >= 0 && C.Casters >= 0 && C.Total() >= MinSize && C.Total() <= MaxSize;
}
FCirePackComposition CireJunglePacks::Clamp(const FCirePackComposition& In)
{
    FCirePackComposition C(FMath::Clamp(In.Tanks, MinTanks, MaxTanks), FMath::Clamp(In.Healers, MinHealers, MaxHealers),
        FMath::Clamp(In.Dps, 0, MaxDps), FMath::Clamp(In.Melee, 0, MaxDps), FMath::Clamp(In.Ranged, 0, MaxDps), FMath::Clamp(In.Casters, 0, MaxDps));
    if (C.DpsTotal() < MinDps) C.Dps = MinDps;
    // Too many DPS, or too many in all: trim DPS (any kind first), then healers, then tanks.
    auto TrimDps = [&]() { for (int32* N : {&C.Dps, &C.Melee, &C.Ranged, &C.Casters}) if (*N > 0) { --*N; return; } };
    while (C.DpsTotal() > MaxDps) TrimDps();
    while (C.Total() > MaxSize)
    {
        if (C.DpsTotal() > MinDps) TrimDps(); else if (C.Healers > MinHealers) --C.Healers; else --C.Tanks;
    }
    return C;
}
FString CireJunglePacks::CompJson(const FCirePackComposition& C)
{
    if (C.Melee == 0 && C.Ranged == 0 && C.Casters == 0) return FString::Printf(TEXT("[%d,%d,%d]"), C.Tanks, C.Healers, C.Dps);
    return FString::Printf(TEXT("[%d,%d,%d,%d,%d,%d]"), C.Tanks, C.Healers, C.Dps, C.Melee, C.Ranged, C.Casters);
}
bool CireJunglePacks::CompFromJson(const TArray<TSharedPtr<FJsonValue>>& V, FCirePackComposition& Out)
{
    if (V.Num() != 3 && V.Num() != 6) return false;
    auto At = [&](int32 I) { double N = 0; return V.IsValidIndex(I) && V[I].IsValid() && V[I]->TryGetNumber(N) && FMath::IsFinite(N) ? FMath::RoundToInt32(N) : 0; };
    Out = FCirePackComposition(At(0), At(1), At(2), At(3), At(4), At(5));
    return true;
}
FCirePackComposition CireJunglePacks::DefaultComposition(uint32 Seed, FName Type, int32 Tier)
{
    Tier = ClampTier(Tier);
    FRandomStream R(static_cast<int32>(Mix(Mix(Seed, NameCrc(Type)), static_cast<uint32>(Tier))));
    // pack-formations: T1 3-4 monsters, T2 4-6, T3 5-7, T4 6-8; the roles are the preset formation's for that size.
    static const int32 MinOf[MaxTier] = {3, 4, 5, 6}, MaxOf[MaxTier] = {4, 6, 7, 8};
    const int32 Size = FMath::Clamp(R.RandRange(MinOf[Tier - 1], MaxOf[Tier - 1]), MinSize, MaxSize);
    FCirePackComposition C(0, 0, 0);
    for (const FCireFormationSlot& Slot : Rules().Formations[Size]) C.CountRef(Slot.Role == ECirePackRole::Tank || Slot.Role == ECirePackRole::Healer ? Slot.Role : ECirePackRole::Dps) += 1;
    return C.Total() == Size ? Clamp(C) : Clamp(FCirePackComposition(1, 1, Size - 2));
}
FCirePackComposition CireJunglePacks::Resolve(const FCirePackComposition* Override, uint32 Seed, FName Type, int32 Tier)
{
    if (Override && !Override->IsZero()) return Clamp(*Override);
    return DefaultComposition(Seed, Type, Tier);
}
FString CireJunglePacks::Summary(int32 Tier, FName Type, const FCirePackComposition& C)
{
    Tier = ClampTier(Tier);
    TArray<FString> Kinds;
    if (C.Melee) Kinds.Add(FString::Printf(TEXT("%d melee"), C.Melee));
    if (C.Ranged) Kinds.Add(FString::Printf(TEXT("%d ranged"), C.Ranged));
    if (C.Casters) Kinds.Add(FString::Printf(TEXT("%d caster"), C.Casters));
    if (C.Dps && Kinds.Num()) Kinds.Add(FString::Printf(TEXT("%d any"), C.Dps));
    return FString::Printf(TEXT("T%d %s: %d tank · %d healer · %d DPS%s · %s"), Tier, *TypeLabel(Type), C.Tanks, C.Healers, C.DpsTotal(),
        Kinds.Num() ? *FString::Printf(TEXT(" (%s)"), *FString::Join(Kinds, TEXT(", "))) : TEXT(""),
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
    if (OwnsHeal(A)) return ECirePackRole::Healer;
    // pack-formations: DPS kinds: ranged casters, physical ranged, melee.
    return A.Role == ECireNPCRole::Caster || A.Role == ECireNPCRole::Support ? ECirePackRole::Caster : A.Role == ECireNPCRole::Ranged ? ECirePackRole::Ranged : ECirePackRole::Melee;
}
const TCHAR* CireJunglePacks::RoleName(ECirePackRole Role)
{
    switch (Role)
    {
    case ECirePackRole::Tank: return TEXT("tank");
    case ECirePackRole::Healer: return TEXT("healer");
    case ECirePackRole::Melee: return TEXT("melee DPS");
    case ECirePackRole::Ranged: return TEXT("ranged DPS");
    case ECirePackRole::Caster: return TEXT("caster DPS");
    default: return TEXT("DPS");
    }
}
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
    TArray<FName> Out, Any, RaceDps;
    for (const FCirePackUnit& U : Inventory())
    {
        // Mixed draws from every race; a race keeps its own units (rare creatures stay Mixed-only).
        const bool bInType = Type == Mixed || (U.Race == Type && !(U.bCreature && CireNPCArchetypes::Find(U.Id) && CireNPCArchetypes::Find(U.Id)->RaceId.IsNone()));
        if (bInType && IsDps(Role) && IsDps(U.Role)) RaceDps.Add(U.Id);
        if (U.Role != Role && !(Role == ECirePackRole::Dps && IsDps(U.Role))) continue;
        Any.Add(U.Id);
        if (bInType) Out.Add(U.Id);
    }
    if (Out.Num() == 0 && RaceDps.Num() > 0) return RaceDps; // pack-formations: a DPS kind the race lacks: its other DPS
    if (Out.Num() == 0) { if (bOutStandIn) *bOutStandIn = Any.Num() > 0; return Any; }
    return Out;
}
TArray<FName> CireJunglePacks::Members(uint32 Seed, FName Type, const FCirePackComposition& In, TArray<ECirePackRole>* OutRoles)
{
    const FCirePackComposition C = Clamp(In);
    TArray<FName> Out;
    if (OutRoles) OutRoles->Reset();
    for (const ECirePackRole Role : {ECirePackRole::Tank, ECirePackRole::Healer, ECirePackRole::Melee, ECirePackRole::Ranged, ECirePackRole::Caster, ECirePackRole::Dps})
    {
        if (C.Count(Role) <= 0) continue;
        TArray<FName> Candidates = Pool(Type, Role);
        if (Candidates.Num() == 0) continue;
        FRandomStream R(static_cast<int32>(Mix(Seed, 17u + static_cast<uint32>(Role))));
        for (int32 I = Candidates.Num() - 1; I > 0; --I) Candidates.Swap(I, R.RandRange(0, I));
        for (int32 I = 0; I < C.Count(Role); ++I)
        {
            const FName Id = Candidates[I % Candidates.Num()];
            const FCireNPCArchetype* A = CireNPCArchetypes::Find(Id);
            Out.Add(Id); if (OutRoles) OutRoles->Add(A ? RoleOf(*A) : Role);
        }
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
            for (const FName Id : Ids) { const FCireNPCArchetype& A = D.Archetypes[Id]; bHas |= Eligible(A, R) && A.RaceId == Race && (RoleOf(A) == Role || (Role == ECirePackRole::Dps && IsDps(RoleOf(A)))); }
            if (!bHas) GAudit.Add(FString::Printf(TEXT("STAND-IN %s has no %s: that slot draws a %s from the Mixed pool"), *Race.ToString(), CireJunglePacks::RoleName(Role), CireJunglePacks::RoleName(Role)));
        }
    UE_LOG(LogCireJungle, Display, TEXT("CIRE_JUNGLE_POOL units=%d filled=%d kitFloor=%d"), Units, Filled, R.KitFloor);
    for (const FString& L : GAudit) UE_LOG(LogCireJungle, Verbose, TEXT("CIRE_JUNGLE_AUDIT %s"), *L);
}
const TArray<FString>& CireJunglePacks::Audit() { return GAudit; }

// ================================================================================================ runtime
float CireJunglePacks::FormationSpacing(float Radius) { return FMath::Clamp(Radius * .5f, 150.f, 400.f); }
TArray<FVector2D> CireJunglePacks::FormationOffsets(const TArray<ECirePackRole>& Roles, float Radius)
{
    TArray<FVector2D> Out; Out.SetNumZeroed(Roles.Num());
    const TArray<FCireFormationSlot>& Slots = Rules().Formations[FMath::Clamp(Roles.Num(), 0, MaxSize)];
    const float Unit = FormationSpacing(Radius);
    if (Slots.Num() != Roles.Num())
    {
        // Outside 3..8 (never from a valid composition): a ring.
        for (int32 I = 0; I < Roles.Num(); ++I) Out[I] = FVector2D(Unit, 0.f).GetRotated(360.f * I / FMath::Max(1, Roles.Num()));
        return Out;
    }
    auto Class = [](ECirePackRole Role) { return Role == ECirePackRole::Tank || Role == ECirePackRole::Healer ? Role : ECirePackRole::Dps; };
    // Slots front first, so a leftover member takes the most forward free slot.
    TArray<int32> Order; for (int32 I = 0; I < Slots.Num(); ++I) Order.Add(I);
    Order.StableSort([&](int32 A, int32 B) { return Slots[A].Forward > Slots[B].Forward; });
    TArray<bool> Used; Used.Init(false, Slots.Num());
    TArray<int32> Pending;
    for (int32 M = 0; M < Roles.Num(); ++M)
    {
        int32 Pick = INDEX_NONE;
        for (const int32 S : Order) if (!Used[S] && Slots[S].Role == Class(Roles[M])) { Pick = S; break; }
        if (Pick == INDEX_NONE) { Pending.Add(M); continue; }
        Used[Pick] = true; Out[M] = FVector2D(Slots[Pick].Forward, Slots[Pick].Right) * Unit;
    }
    for (const int32 M : Pending)
        for (const int32 S : Order) if (!Used[S]) { Used[S] = true; Out[M] = FVector2D(Slots[S].Forward, Slots[S].Right) * Unit; break; }
    return Out;
}
float CireJunglePacks::FacingYaw(const UWorld* World, int32 Team, const FVector& Center)
{
    const FCireBattlefieldRoutes& R = CireLanePath::Get(World);
    const int32 Realm = FMath::Clamp(Team, 0, 1);
    const FVector2D Local = CireLanePath::ToLocal(Realm, Center);
    // The nearest point on any of the realm's monster paths (else the primary march).
    double Best = TNumericLimits<double>::Max(); FVector2D Near = Local;
    auto Scan = [&](const TArray<FVector2D>& Points)
    {
        for (int32 I = 0; I + 1 < Points.Num(); ++I)
        {
            const FVector2D A = Points[I], B = Points[I + 1], AB = B - A;
            const double T = AB.SizeSquared() > 1. ? FMath::Clamp(FVector2D::DotProduct(Local - A, AB) / AB.SizeSquared(), 0., 1.) : 0.;
            const FVector2D P = A + AB * T; const double D = FVector2D::DistSquared(P, Local);
            if (D < Best) { Best = D; Near = P; }
        }
    };
    for (const FCireRoutePath& Path : R.Paths[Realm]) Scan(Path.Points);
    if (Best == TNumericLimits<double>::Max()) Scan(R.LocalPoints[Realm]);
    FVector2D Dir = Near - Local;
    if (Dir.SizeSquared() < 150. * 150.) Dir = R.BaseLocal - Local; // on the path: face the hero base
    if (Dir.SizeSquared() < 1.) return 0.f;
    // Realm-local to world (realms may be offset or mirrored).
    const FVector W0 = CireLanePath::ToWorld(Realm, Local, 0.f), W1 = CireLanePath::ToWorld(Realm, Local + Dir.GetSafeNormal() * 100., 0.f);
    return FMath::RadiansToDegrees(FMath::Atan2(W1.Y - W0.Y, W1.X - W0.X));
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
void CireJunglePacks::ApplyTier(ACireMonster* M, int32 Tier, int32 Seed, bool bLeader)
{
    UCireNPCState* S = M ? M->NPCState.Get() : nullptr;
    const FCireNPCArchetype* A = S ? S->Archetype() : nullptr;
    if (!S || !A || !M->HasAuthority()) return;
    Tier = ClampTier(Tier);
    const FCirePackTier& T = TierRules(Tier);
    S->Loadout = TierLoadout(*A, Tier, Seed);
    S->bLoadoutSet = true;
    S->SkillTier = static_cast<uint8>(S->Loadout.IsEmpty() ? 0 : FMath::Clamp(Tier, 1, 3));
    (void)T;
    // pack-formations: challenge-mob stats (JunglePacks.json "stats" x the tier's multipliers), then the F8 monster scale.
    const ECirePackRole Role = RoleOf(*A);
    M->MaxHealth = M->Health = UnitHealth(Role, Tier, bLeader);
    M->Damage = UnitDamage(Role, Tier);
    CireDeveloperTools::AdjustMonster(M);
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
    Check(Sizes[3] > 0 && Sizes[6] > 0 && Sizes[8] > 0, TEXT("defaults span 3..8 monsters"));
    for (int32 T = -2; T <= 5; ++T) for (int32 H = -2; H <= 5; ++H) for (int32 D = -2; D <= 9; ++D) for (int32 K = -1; K <= 3; ++K)
        Bad += IsValid(Clamp(FCirePackComposition(T, H, D, K, K > 1 ? 1 : 0, K))) ? 0 : 1;
    Check(Bad == 0, TEXT("every override clamps to a valid composition (at least 1 tank, 1 healer, 1 DPS; 3..8)"));
    Check(Clamp({3, 3, 3}) == FCirePackComposition(3, 3, 2) && Clamp({0, 9, 0}) == FCirePackComposition(1, 3, 1) && Clamp({3, 2, 9}) == FCirePackComposition(3, 2, 3),
        TEXT("clamp trims DPS, then healers, to keep 8"));
    Check(Clamp(FCirePackComposition(1, 1, 0, 0, 0, 2)).Casters == 2 && IsValid(FCirePackComposition(1, 1, 0, 0, 0, 1)), TEXT("a caster-only DPS line is valid"));
    const FCirePackComposition Over{2, 1, 3};
    Check(Resolve(&Over, 1, Mixed, 1) == Over && Resolve(nullptr, 5, Mixed, 2) == DefaultComposition(5, Mixed, 2), TEXT("override beats the default"));
    Check(Summary(3, Mixed, {2, 1, 3}).Contains(TEXT("5 abilities each")) && Summary(4, Mixed, {1, 1, 1}).Contains(TEXT("full kit")) &&
        Summary(1, Mixed, FCirePackComposition(1, 1, 1, 0, 0, 2)).Contains(TEXT("2 caster")), TEXT("summary line"));
    {
        // JSON round trip: legacy [t,h,d] and the 6-number form.
        TArray<TSharedPtr<FJsonValue>> Legacy, Full; FCirePackComposition Got;
        for (const int32 N : {2, 1, 3}) Legacy.Add(MakeShared<FJsonValueNumber>(N));
        for (const int32 N : {1, 2, 0, 1, 1, 2}) Full.Add(MakeShared<FJsonValueNumber>(N));
        Check(CompFromJson(Legacy, Got) && Got == FCirePackComposition(2, 1, 3) && CompJson(Got) == TEXT("[2,1,3]"), TEXT("legacy comp [t,h,d] reads as automatic DPS"));
        Check(CompFromJson(Full, Got) && Got == FCirePackComposition(1, 2, 0, 1, 1, 2) && CompJson(Got) == TEXT("[1,2,0,1,1,2]"), TEXT("comp with DPS kinds round-trips"));
    }
    // Stats: tank 1500, healer 50 %, melee 75 %, ranged / caster 65 %, damage 200 (at x1 multipliers).
    {
        FCireJungleRules Base = BuiltInRules();
        Check(Base.Stats.TankHealth == 1500.f && Base.Stats.BaseDamage == 200.f && Base.Stats.HealerShare == .5f && Base.Stats.MeleeShare == .75f &&
            Base.Stats.RangedShare == .65f && Base.Stats.CasterShare == .65f, TEXT("challenge-mob stat defaults"));
        const FCirePackTier& T1 = TierRules(1);
        const float Scale = T1.Health * Rules().Stats.GlobalHealth;
        Check(FMath::IsNearlyEqual(UnitHealth(ECirePackRole::Healer, 1, false) / UnitHealth(ECirePackRole::Tank, 1, false), Rules().Stats.HealerShare, .001f) &&
            FMath::IsNearlyEqual(UnitHealth(ECirePackRole::Melee, 1, false) / UnitHealth(ECirePackRole::Tank, 1, false), Rules().Stats.MeleeShare, .001f) &&
            FMath::IsNearlyEqual(UnitHealth(ECirePackRole::Caster, 1, false) / UnitHealth(ECirePackRole::Tank, 1, false), Rules().Stats.CasterShare, .001f) &&
            FMath::IsNearlyEqual(UnitHealth(ECirePackRole::Tank, 1, false), Rules().Stats.TankHealth * Scale, .5f) &&
            FMath::IsNearlyEqual(UnitHealth(ECirePackRole::Tank, 1, true), UnitHealth(ECirePackRole::Tank, 1, false) * Rules().LeaderHealth, .5f),
            TEXT("unit health follows the role shares, tier and leader"));
        Check(UnitHealth(ECirePackRole::Tank, 4, false) >= UnitHealth(ECirePackRole::Tank, 1, false) && UnitDamage(ECirePackRole::Melee, 1) == UnitDamage(ECirePackRole::Caster, 1),
            TEXT("tiers never weaken a pack; every role deals the same base damage"));
        Check(ParseRules(TEXT("{\"tiers\":[{\"abilities\":2},{\"abilities\":3},{\"abilities\":5},{\"abilities\":\"full\"}],\"stats\":{\"baseDamage\":150,\"globalHealth\":2}}"), Parsed, Error) &&
            Parsed.Stats.BaseDamage == 150.f && Parsed.Stats.GlobalHealth == 2.f && Parsed.Stats.TankHealth == 1500.f, TEXT("JunglePacks.json stats parse"));
        // Every tier unlocks at cycle 1 wave 1 by default (authored T3 / T4 packs spawn from the start).
        bool bOpen = true; for (const FCirePackTier& T : Base.Tiers) bOpen &= T.UnlockRound == 1 && T.UnlockWave == 1;
        Check(bOpen, TEXT("every tier unlocks at cycle 1 wave 1"));
    }
    // Formations: sizes 3..8, each with at least one tank, healer and DPS slot, tanks in front of healers.
    for (int32 Size = MinSize; Size <= MaxSize; ++Size)
    {
        const TArray<FCireFormationSlot>& F = Rules().Formations[Size];
        int32 Count[3] = {}; float TankFront = -10.f, HealerFront = -10.f;
        for (const FCireFormationSlot& Slot : F)
        {
            ++Count[FMath::Min(static_cast<int32>(Slot.Role), 2)];
            if (Slot.Role == ECirePackRole::Tank) TankFront = FMath::Max(TankFront, Slot.Forward);
            if (Slot.Role == ECirePackRole::Healer) HealerFront = FMath::Max(HealerFront, Slot.Forward);
        }
        Check(F.Num() == Size && Count[0] >= 1 && Count[1] >= 1 && Count[2] >= 1 && TankFront > HealerFront, FString::Printf(TEXT("formation %d: slots, roles, tanks in front"), Size));
        const FCirePackComposition C = [&] { FCirePackComposition X(0, 0, 0); for (const FCireFormationSlot& Slot : F) X.CountRef(Slot.Role) += 1; return X; }();
        Check(IsValid(C), FString::Printf(TEXT("formation %d composition is legal"), Size));
    }
    {
        // Pack of 8 exactly as drawn: 3 tanks (one at the front tip), 2 healers behind, 3 DPS.
        const FCirePackComposition C8 = [&] { FCirePackComposition X(0, 0, 0); for (const FCireFormationSlot& Slot : Rules().Formations[8]) X.CountRef(Slot.Role) += 1; return X; }();
        const FCirePackComposition C5 = [&] { FCirePackComposition X(0, 0, 0); for (const FCireFormationSlot& Slot : Rules().Formations[5]) X.CountRef(Slot.Role) += 1; return X; }();
        Check(C8 == FCirePackComposition(3, 2, 3) && C5 == FCirePackComposition(1, 2, 2), TEXT("formations 8 and 5 match the drawing"));
        TArray<ECirePackRole> Roles = {ECirePackRole::Tank, ECirePackRole::Tank, ECirePackRole::Tank, ECirePackRole::Healer, ECirePackRole::Healer, ECirePackRole::Melee, ECirePackRole::Ranged, ECirePackRole::Caster};
        const TArray<FVector2D> Off = FormationOffsets(Roles, 450.f);
        float MinTank = 1e9f, MaxHealer = -1e9f; bool bDistinct = true;
        for (int32 I = 0; I < Off.Num(); ++I)
        {
            if (Roles[I] == ECirePackRole::Tank) MinTank = FMath::Min(MinTank, static_cast<float>(Off[I].X));
            if (Roles[I] == ECirePackRole::Healer) MaxHealer = FMath::Max(MaxHealer, static_cast<float>(Off[I].X));
            for (int32 J = 0; J < I; ++J) bDistinct &= FVector2D::Distance(Off[I], Off[J]) > 100.f;
        }
        Check(Off.Num() == 8 && MinTank > MaxHealer && bDistinct, TEXT("formation offsets: tanks ahead of healers, members spread apart"));
    }
        Check(NormalizeType(TEXT("")) == Mixed && NormalizeType(TEXT("nonsense")) == Mixed, TEXT("unknown pack types read as Mixed"));
    // The pool: every race fields every role; every unit fills T3 and T4.
    const TArray<FCirePackUnit> Units = Inventory();
    Check(Units.Num() >= 60, FString::Printf(TEXT("pool has %d units"), Units.Num()));
    for (const FName Type : PackTypes())
        for (const ECirePackRole Role : {ECirePackRole::Tank, ECirePackRole::Healer, ECirePackRole::Dps})
            Check(Pool(Type, Role).Num() > 0, FString::Printf(TEXT("%s has a %s"), *Type.ToString(), RoleName(Role)));
    // pack-formations: every DPS kind resolves to DPS units of the race (its other DPS when it lacks the kind).
    for (const FName Type : PackTypes())
        for (const ECirePackRole Role : {ECirePackRole::Melee, ECirePackRole::Ranged, ECirePackRole::Caster})
            for (const FName Id : Pool(Type, Role))
                Check(CireNPCArchetypes::Find(Id) && IsDps(RoleOf(*CireNPCArchetypes::Find(Id))), FString::Printf(TEXT("%s %s pool holds DPS"), *Type.ToString(), RoleName(Role)));
    Check(Pool(Mixed, ECirePackRole::Caster).Num() > 0 && Pool(Mixed, ECirePackRole::Ranged).Num() > 0 && Pool(Mixed, ECirePackRole::Melee).Num() > 0, TEXT("Mixed fields every DPS kind"));
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
            int32 Got[3] = {};
            for (int32 I = 0; I < M.Num(); ++I)
            {
                const FCireNPCArchetype* A = CireNPCArchetypes::Find(M[I]);
                Check(A && RoleOf(*A) == Roles[I], TEXT("each member reports its own role"));
                ++Got[Roles[I] == ECirePackRole::Tank ? 0 : Roles[I] == ECirePackRole::Healer ? 1 : 2];
            }
            Check(Got[0] == C.Tanks && Got[1] == C.Healers && Got[2] == C.DpsTotal(), TEXT("members fill the tank / healer / DPS counts"));
            const TArray<FVector2D> Off = FormationOffsets(Roles, 450.f);
            for (const FVector2D& O : Off) Check(O.Size() <= 450.f, TEXT("formation stays inside the pack radius"));
        }
    // A caster line: a race pack asked for casters gets DPS of the race (casters where it has them).
    {
        const FCirePackComposition C(1, 1, 0, 0, 0, 3);
        TArray<ECirePackRole> Roles;
        const TArray<FName> M = Members(7, Mixed, C, &Roles);
        int32 Casters = 0; for (const ECirePackRole R : Roles) Casters += R == ECirePackRole::Caster ? 1 : 0;
        Check(M.Num() == 5 && Casters == 3, TEXT("a Mixed pack with 3 casters spawns 3 ranged casters"));
    }
    return Failures.Num() == Before;
}
#endif
