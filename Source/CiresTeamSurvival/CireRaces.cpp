// monster-races: race data, ranks, per-match skill draws, wave-gated unlocks, race-skill riders and the
// race/rank skin. See CireRaces.h and Docs/Races.md.
#include "CireRaces.h"
#include "CireActorIterator.h" // town-perf: fast actor iteration in editor-binary -game
#include "CireTechConstructs.h" // new-champions
#include "CireGame.h"
#include "CireNPCState.h"
#include "CireNPCCombat.h"
#include "CireMonsterArt.h"
#include "CireBuffs.h"
#include "CireAudio.h"
#include "CireRealm.h"
#include "CireThreat.h"
#include "CireWaves.h"
#include "CireMonsterExpansion.h" // monster-expansion
#include "Engine/Texture.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireRaces, Log, All);

namespace
{
FCireRaceDatabase GRaces;
TMap<TWeakObjectPtr<UWorld>, int32> GSeeds;
// Summoner -> its live summons (cap per caster).
TMap<TWeakObjectPtr<ACireMonster>, TArray<TWeakObjectPtr<ACireMonster>>> GSummons;
FCireRaceStats GStats;
TArray<FName> GSeenRaces;
const TCHAR* const RankIds[] = {TEXT("normal"), TEXT("veteran"), TEXT("elite"), TEXT("champion"), TEXT("warlord"), TEXT("mythic")};
static_assert(UE_ARRAY_COUNT(RankIds) == static_cast<int32>(ECireNPCRank::Count), "rank ids");
const TCHAR* const SkinPath = TEXT("/Game/Art/Materials/M_CireMonsterSkin.M_CireMonsterSkin");
const FName RootedId(TEXT("npc_rooted")), SilencedId(TEXT("npc_silenced"));

const TArray<FName>& Slots6()
{
    static const TArray<FName> S = {TEXT("line"), TEXT("bruiser"), TEXT("tank"), TEXT("caster"), TEXT("ranged"), TEXT("special")};
    return S;
}

FLinearColor ReadColor(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const FLinearColor& Default)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!O.IsValid() || !O->TryGetArrayField(Key, Values) || Values->Num() < 3) return Default;
    return FLinearColor(FMath::Clamp((*Values)[0]->AsNumber(), 0., 4.), FMath::Clamp((*Values)[1]->AsNumber(), 0., 4.),
        FMath::Clamp((*Values)[2]->AsNumber(), 0., 4.), 1.f);
}
float ReadNum(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, float Default, float Lo, float Hi)
{
    double V = Default;
    if (O.IsValid()) O->TryGetNumberField(Key, V);
    return FMath::IsFinite(V) ? FMath::Clamp(static_cast<float>(V), Lo, Hi) : Default;
}
TArray<FString> ReadStrings(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
{
    TArray<FString> Out; const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (O.IsValid() && O->TryGetArrayField(Key, Values)) for (const auto& V : *Values) { FString S; if (V->TryGetString(S)) Out.Add(S); }
    return Out;
}

void DefaultRanks(FCireRaceDatabase& D)
{
    struct FRow { const TCHAR* Label; FLinearColor Color; float Health, Damage, Size; int32 Bonus; float Armor, Body, Glow, Rim; ECireNPCClass Class; };
    const FRow Rows[] = {
        {TEXT("Normal"), FLinearColor(.82f, .8f, .74f), 1, 1, 1, 0, 0, 0, 0, 0, ECireNPCClass::Normal},
        {TEXT("Veteran"), FLinearColor(.12f, 1, .1f), 1.3f, 1.1f, 1.05f, 0, .7f, .1f, .9f, .55f, ECireNPCClass::Normal},
        {TEXT("Elite"), FLinearColor(.05f, .45f, 1), 1.6f, 1.25f, 1.1f, 1, .8f, .14f, 1.3f, .8f, ECireNPCClass::Elite},
        {TEXT("Champion"), FLinearColor(.7f, .22f, 1), 2.2f, 1.4f, 1.18f, 1, .9f, .18f, 1.8f, 1, ECireNPCClass::Elite},
        {TEXT("Warlord"), FLinearColor(1, .5f, .02f), 1, 1, 1, 2, .9f, .16f, 2.2f, 1.15f, ECireNPCClass::Boss},
        {TEXT("Mythic"), FLinearColor(1, .12f, .08f), 1.5f, 1.3f, 1.12f, 3, .9f, .22f, 2.8f, 1.4f, ECireNPCClass::Boss}};
    for (int32 I = 0; I < UE_ARRAY_COUNT(Rows); ++I)
    {
        FCireRankStyle& R = D.Ranks[I]; const FRow& W = Rows[I];
        R.Label = W.Label; R.Color = W.Color; R.Trim = W.Color; R.Health = W.Health; R.Damage = W.Damage; R.Size = W.Size; R.SkillBonus = W.Bonus;
        R.ArmorTint = W.Armor; R.BodyTint = W.Body; R.Glow = W.Glow; R.Rim = W.Rim; R.Classification = W.Class;
    }
}

UCireNPCState* St(const ACireMonster* M) { return M ? M->NPCState.Get() : nullptr; }
const FCireNPCArchetype* Arch(const ACireMonster* M) { const auto* S = St(M); return S ? S->Archetype() : nullptr; }
bool Targetable(const ACireMonster* M, const ACireHero* H)
{
    return IsValid(H) && !H->bDead && H->bDrafted && H->Health > 0 && H->TeamId == M->Lane && CireRealm::CanObserve(H, M);
}
const FCireSkillProgression& Rules(const UWorld* World) { return CireWaveDirector::Config(World).Skills; }
int32 CurrentWave(const ACireMonster* M)
{
    const auto* State = M && M->GetWorld() ? M->GetWorld()->GetGameState<ACireGameState>() : nullptr;
    return State ? State->Wave : 1;
}
UMaterialInterface* SkinMaterial()
{
    static TWeakObjectPtr<UMaterialInterface> Cached;
    static bool bTried = false;
    if (!Cached.IsValid() && !bTried)
    {
        bTried = true;
        if (FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(FString(SkinPath))))
            Cached = LoadObject<UMaterialInterface>(nullptr, SkinPath);
        if (!Cached.IsValid()) UE_LOG(LogCireRaces, Warning, TEXT("CIRE_RACE_SKIN_MISSING %s (race palettes fall back to the rim overlay)"), SkinPath);
    }
    return Cached.Get();
}
}

// ============================================================================================ data
FCireRacePalette FCireRace::Palette(int32 Index) const
{
    if (Variants.IsValidIndex(Index)) return Variants[Index];
    FCireRacePalette P; P.Name = Name; P.Base = Base; P.Accent = Accent;
    return Variants.IsEmpty() ? P : Variants[FMath::Max(0, Index) % Variants.Num()];
}

bool FCireSkillProgression::operator==(const FCireSkillProgression& O) const
{
    return FirstSkillWave == O.FirstSkillWave && UnlockEveryWaves == O.UnlockEveryWaves && MaxSkills == O.MaxSkills && TierEveryWaves == O.TierEveryWaves &&
        MaxTier == O.MaxTier && FMath::IsNearlyEqual(TierDamage, O.TierDamage) && FMath::IsNearlyEqual(TierCooldown, O.TierCooldown) &&
        FMath::IsNearlyEqual(TierDuration, O.TierDuration);
}
bool FCireCampaign::operator==(const FCireCampaign& O) const
{
    return RaceRotation == O.RaceRotation && bReskinOnWrap == O.bReskinOnWrap && VeteranFromCycle == O.VeteranFromCycle && EliteFromCycle == O.EliteFromCycle &&
        ChampionFromCycle == O.ChampionFromCycle && MythicBossFromCycle == O.MythicBossFromCycle && PromoteEvery == O.PromoteEvery &&
        bRotatePerWave == O.bRotatePerWave;
}

bool CireRaces::MergeInto(FCireNPCDatabase& Database, FString& Error)
{
    FCireRaceDatabase D; DefaultRanks(D);
    FString Json;
    const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/Races.json"));
    if (!FFileHelper::LoadFileToString(Json, *Path)) { Error = TEXT("Cannot read Content/Data/Races.json"); GRaces = D; return false; }
    TSharedPtr<FJsonObject> Root;
    if (Json.Len() > 2 * 1024 * 1024 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
    { Error = TEXT("Races.json is not a bounded JSON object"); GRaces = D; return false; }
    double Version = 0;
    if (!Root->TryGetNumberField(TEXT("schemaVersion"), Version) || Version != 1) { Error = TEXT("Races.json schemaVersion must be 1"); GRaces = D; return false; }
    if (const TSharedPtr<FJsonObject>* Ranks = nullptr; Root->TryGetObjectField(TEXT("ranks"), Ranks))
        for (int32 I = 0; I < static_cast<int32>(ECireNPCRank::Count); ++I)
        {
            const TSharedPtr<FJsonObject>* R = nullptr;
            if (!(*Ranks)->TryGetObjectField(RankIds[I], R)) continue;
            FCireRankStyle& S = D.Ranks[I];
            (*R)->TryGetStringField(TEXT("label"), S.Label);
            S.Color = ReadColor(*R, TEXT("color"), S.Color); S.Trim = ReadColor(*R, TEXT("trim"), S.Color);
            S.Health = ReadNum(*R, TEXT("health"), S.Health, .1f, 20); S.Damage = ReadNum(*R, TEXT("damage"), S.Damage, .1f, 20);
            S.Size = ReadNum(*R, TEXT("size"), S.Size, .5f, 2); S.SkillBonus = FMath::RoundToInt(ReadNum(*R, TEXT("skillBonus"), S.SkillBonus, 0, 6));
            S.ArmorTint = ReadNum(*R, TEXT("armorTint"), S.ArmorTint, 0, 1); S.BodyTint = ReadNum(*R, TEXT("bodyTint"), S.BodyTint, 0, 1);
            S.Glow = ReadNum(*R, TEXT("glow"), S.Glow, 0, 10); S.Rim = ReadNum(*R, TEXT("rim"), S.Rim, 0, 5);
            FString Class; if ((*R)->TryGetStringField(TEXT("classification"), Class))
                S.Classification = Class == TEXT("boss") ? ECireNPCClass::Boss : Class == TEXT("elite") ? ECireNPCClass::Elite : ECireNPCClass::Normal;
        }
    const TSharedPtr<FJsonObject>* Races = nullptr;
    if (!Root->TryGetObjectField(TEXT("races"), Races) || (*Races)->Values.IsEmpty()) { Error = TEXT("Races.json needs a races object"); GRaces = D; return false; }
    TArray<FString> Order = ReadStrings(Root, TEXT("raceOrder"));
    if (Order.IsEmpty()) for (const auto& Pair : (*Races)->Values) Order.Add(FString(Pair.Key));
    FCireNPCDatabase Work = Database;
    TSet<FName> Seen;
    for (const FString& RaceKey : Order)
    {
        const TSharedPtr<FJsonObject>* RO = nullptr;
        if (!(*Races)->TryGetObjectField(RaceKey, RO)) { Error = TEXT("raceOrder names a missing race ") + RaceKey; GRaces = D; return false; }
        FCireRace Race; Race.Id = FName(*RaceKey);
        const FString Where = TEXT("races.") + RaceKey;
        if (!(*RO)->TryGetStringField(TEXT("name"), Race.Name) || Race.Name.IsEmpty()) { Error = Where + TEXT(" needs a name"); GRaces = D; return false; }
        (*RO)->TryGetStringField(TEXT("short"), Race.Short); (*RO)->TryGetStringField(TEXT("lore"), Race.Lore);
        if (Race.Short.IsEmpty()) Race.Short = Race.Name;
        if (const TSharedPtr<FJsonObject>* Origin = nullptr; (*RO)->TryGetObjectField(TEXT("origin"), Origin))
        {
            (*Origin)->TryGetStringField(TEXT("note"), Race.OriginNote);
            Race.PlayerRaces = ReadStrings(*Origin, TEXT("playerRaces")); Race.Profiles = ReadStrings(*Origin, TEXT("profiles"));
        }
        const TSharedPtr<FJsonObject>* Palette = nullptr;
        if (!(*RO)->TryGetObjectField(TEXT("palette"), Palette)) { Error = Where + TEXT(" needs a palette"); GRaces = D; return false; }
        Race.Base = ReadColor(*Palette, TEXT("base"), Race.Base); Race.Accent = ReadColor(*Palette, TEXT("accent"), Race.Accent);
        Race.Secondary = ReadColor(*Palette, TEXT("secondary"), Race.Secondary); Race.Glow = ReadColor(*Palette, TEXT("glow"), Race.Glow);
        if (const TArray<TSharedPtr<FJsonValue>>* Variants = nullptr; (*Palette)->TryGetArrayField(TEXT("variants"), Variants))
            for (const auto& V : *Variants)
            {
                const TSharedPtr<FJsonObject>* VO = nullptr; if (!V->TryGetObject(VO)) continue;
                FCireRacePalette P; (*VO)->TryGetStringField(TEXT("name"), P.Name);
                P.Base = ReadColor(*VO, TEXT("base"), Race.Base); P.Accent = ReadColor(*VO, TEXT("accent"), Race.Accent); Race.Variants.Add(P);
            }
        FString Text;
        if ((*RO)->TryGetStringField(TEXT("footsteps"), Text)) Race.Footsteps = FName(*Text);
        if ((*RO)->TryGetStringField(TEXT("ambienceCue"), Text)) Race.AmbienceCue = FName(*Text);
        if ((*RO)->TryGetStringField(TEXT("voiceCue"), Text)) Race.VoiceCue = FName(*Text);
        const TSharedPtr<FJsonObject>* Units = nullptr;
        if (!(*RO)->TryGetObjectField(TEXT("units"), Units)) { Error = Where + TEXT(" needs units"); GRaces = D; return false; }
        for (const auto& Pair : (*Units)->Values)
        {
            const FString UnitKey(Pair.Key); const FName UnitId(*UnitKey);
            const FString UW = Where + TEXT(".units.") + UnitKey;
            const TSharedPtr<FJsonObject>* UO = nullptr;
            if (!Pair.Value->TryGetObject(UO)) { Error = UW + TEXT(" must be an object"); GRaces = D; return false; }
            if (Seen.Contains(UnitId)) { Error = UW + TEXT(" belongs to two races"); GRaces = D; return false; }
            Seen.Add(UnitId);
            FString Slot, Fallback, Look; double Draw = 99; bool bExtends = false;
            (*UO)->TryGetStringField(TEXT("slot"), Slot); (*UO)->TryGetStringField(TEXT("fallback"), Fallback); (*UO)->TryGetStringField(TEXT("look"), Look);
            (*UO)->TryGetNumberField(TEXT("poolDraw"), Draw); (*UO)->TryGetBoolField(TEXT("extends"), bExtends);
            FCireNPCArchetype* Target = nullptr;
            if (bExtends)
            {
                Target = Work.Archetypes.Find(UnitId);
                if (!Target) { Error = UW + TEXT(" extends an archetype missing from NPCArchetypes.json"); GRaces = D; return false; }
                if (const TArray<TSharedPtr<FJsonValue>>* Extra = nullptr; (*UO)->TryGetArrayField(TEXT("abilities"), Extra))
                    for (int32 I = 0; I < Extra->Num(); ++I)
                    {
                        const TSharedPtr<FJsonObject>* AO = nullptr; FCireNPCAbility Ability;
                        if (!(*Extra)[I]->TryGetObject(AO) || !CireNPCArchetypes::ParseAbilityObject(*AO, Ability, Error, FString::Printf(TEXT("%s.abilities[%d]"), *UW, I)))
                        { if (Error.IsEmpty()) Error = UW + TEXT(" ability must be an object"); GRaces = D; return false; }
                        if (Ability.bBasic || Target->FindAbility(Ability.Id)) { Error = UW + TEXT(" adds a basic or duplicate ability ") + Ability.Id.ToString(); GRaces = D; return false; }
                        Target->Abilities.Add(Ability);
                    }
                float LaneBoss = 0; LaneBoss = ReadNum(*UO, TEXT("laneBossHealthMultiplier"), 0, 0, 100);
                if (LaneBoss > 0) Target->LaneBossHealthMultiplier = LaneBoss;
                Target->FallbackBody = UnitId;
            }
            else
            {
                const TSharedPtr<FJsonObject>* AO = nullptr; FCireNPCArchetype Parsed;
                if (!(*UO)->TryGetObjectField(TEXT("archetype"), AO) || !CireNPCArchetypes::ParseArchetypeObject(UnitKey, *AO, Parsed, Error))
                { if (Error.IsEmpty()) Error = UW + TEXT(" needs an archetype object"); else Error = UW + TEXT(": ") + Error; GRaces = D; return false; }
                if (Work.Archetypes.Contains(UnitId)) { Error = UW + TEXT(" duplicates an NPCArchetypes.json id (use extends)"); GRaces = D; return false; }
                Parsed.FallbackBody = Fallback.IsEmpty() ? NAME_None : FName(*Fallback);
                Target = &Work.Archetypes.Add(UnitId, Parsed);
            }
            Target->RaceId = Race.Id; Target->Slot = FName(*Slot); Target->PoolDraw = FMath::Clamp(static_cast<int32>(Draw), 0, 99); Target->Look = Look;
            if (Target->Slot == TEXT("warlord") || Target->Slot == TEXT("colossus")) Race.Bosses.Add(Target->Slot, UnitId);
            else Race.Slots.Add(Target->Slot, UnitId);
        }
        for (const FName S : Slots6()) if (!Race.Slots.Contains(S)) { Error = Where + TEXT(" has no unit for slot ") + S.ToString(); GRaces = D; return false; }
        if (Race.Slots.Num() != 6 || Race.Bosses.Num() != 2 || !Race.Bosses.Contains(TEXT("warlord")) || !Race.Bosses.Contains(TEXT("colossus")))
        { Error = Where + TEXT(" needs exactly six unit slots and a warlord and a colossus boss"); GRaces = D; return false; }
        for (const FName S : Slots6()) Race.Units.Add(Race.Slots[S]);
        Race.Units.Add(Race.Bosses[TEXT("warlord")]); Race.Units.Add(Race.Bosses[TEXT("colossus")]);
        for (const auto& Pair : Race.Bosses)
            if (Work.Archetypes[Pair.Value].Classification != ECireNPCClass::Boss) { Error = Where + TEXT(" boss ") + Pair.Value.ToString() + TEXT(" must be boss classification"); GRaces = D; return false; }
        D.Order.Add(Race.Id); D.Races.Add(Race.Id, Race);
    }
    // Cross checks: fallback bodies and summons name real archetypes.
    for (const auto& Pair : Work.Archetypes)
    {
        const FCireNPCArchetype& A = Pair.Value;
        if (!A.FallbackBody.IsNone() && !Work.Archetypes.Contains(A.FallbackBody)) { Error = A.Id.ToString() + TEXT(" falls back to unknown body ") + A.FallbackBody.ToString(); GRaces = D; return false; }
        for (const FCireNPCAbility& Ab : A.Abilities)
        {
            if (Ab.Kind == ECireNPCAbilityKind::Deploy && !CireTechConstructs::FindRecipe(Ab.DeployRecipe)) { Error = A.Id.ToString() + TEXT(" deploys unknown construct ") + Ab.DeployRecipe.ToString(); GRaces = D; return false; } // new-champions
            if (Ab.Kind == ECireNPCAbilityKind::Summon && !Work.Archetypes.Contains(Ab.SummonId)) { Error = A.Id.ToString() + TEXT(" summons unknown ") + Ab.SummonId.ToString(); GRaces = D; return false; }
        }
    }
    D.bValid = true;
    Database = MoveTemp(Work);
    GRaces = MoveTemp(D);
    UE_LOG(LogCireRaces, Display, TEXT("CIRE_RACE_DATA_LOADED races=%d archetypes=%d"), GRaces.Races.Num(), Database.Archetypes.Num());
    return true;
}

const FCireRaceDatabase& CireRaces::Get() { CireNPCArchetypes::Get(); if (!GRaces.bValid && GRaces.Order.IsEmpty()) DefaultRanks(GRaces); return GRaces; }
const FCireRace* CireRaces::FindRace(FName RaceId) { return Get().Races.Find(RaceId); }
FName CireRaces::RaceOf(FName ArchetypeId) { const auto* A = CireNPCArchetypes::Find(ArchetypeId); return A ? A->RaceId : NAME_None; }
FName CireRaces::SlotOf(FName ArchetypeId) { const auto* A = CireNPCArchetypes::Find(ArchetypeId); return A ? A->Slot : NAME_None; }
const TArray<FName>& CireRaces::SlotNames()
{
    static const TArray<FName> All = {TEXT("line"), TEXT("bruiser"), TEXT("tank"), TEXT("caster"), TEXT("ranged"), TEXT("special"),
        TEXT("warlord"), TEXT("colossus"), TEXT("boss")};
    return All;
}
FName CireRaces::UnitFor(FName RaceId, FName Slot, int32 Cycle)
{
    const FCireRace* R = FindRace(RaceId);
    if (!R) return NAME_None;
    if (Slot == TEXT("boss")) Slot = (FMath::Max(0, Cycle) % 2 == 0) ? FName(TEXT("colossus")) : FName(TEXT("warlord"));
    if (const FName* U = R->Slots.Find(Slot)) return *U;
    if (const FName* B = R->Bosses.Find(Slot)) return *B;
    return NAME_None;
}

// ============================================================================================ ranks
const FCireRankStyle& CireRaces::Rank(ECireNPCRank R)
{
    const int32 I = FMath::Clamp(static_cast<int32>(R), 0, static_cast<int32>(ECireNPCRank::Count) - 1);
    return Get().Ranks[I];
}
const TCHAR* CireRaces::RankId(ECireNPCRank R)
{
    const int32 I = static_cast<int32>(R);
    return I >= 0 && I < UE_ARRAY_COUNT(RankIds) ? RankIds[I] : RankIds[0];
}
bool CireRaces::ParseRank(const FString& Text, ECireNPCRank& Out)
{
    for (int32 I = 0; I < UE_ARRAY_COUNT(RankIds); ++I) if (Text.Equals(RankIds[I], ESearchCase::IgnoreCase)) { Out = static_cast<ECireNPCRank>(I); return true; }
    return false;
}
ECireNPCRank CireRaces::RankFromClass(ECireNPCClass Class)
{
    return Class == ECireNPCClass::Boss ? ECireNPCRank::Warlord : Class == ECireNPCClass::Elite ? ECireNPCRank::Elite : ECireNPCRank::Normal;
}
ECireNPCRank CireRaces::RankOf(const ACireMonster* M)
{
    const auto* S = St(M);
    return S ? static_cast<ECireNPCRank>(FMath::Min<uint8>(S->Rank, static_cast<uint8>(ECireNPCRank::Mythic))) : ECireNPCRank::Normal;
}
FLinearColor CireRaces::RankColor(const ACireMonster* M)
{
    // monster-expansion: Rare Spawns and Bonus Loot creatures wear their own colour (plate, frame, rim, skin).
    if (M && M->SpecialSpawn != 0) return CireMonsterExpansion::SpecialColor(M->SpecialSpawn);
    return Rank(RankOf(M)).Color;
}

// ============================================================================================ skills
FCireSkillPlan CireRaces::Plan(const FCireSkillProgression& R, int32 Wave, ECireNPCRank RankValue)
{
    FCireSkillPlan P;
    if (Wave < R.FirstSkillWave) return P;
    const int32 Since = Wave - FMath::Max(1, R.FirstSkillWave);
    P.Count = FMath::Min(1 + Since / FMath::Max(1, R.UnlockEveryWaves), FMath::Max(1, R.MaxSkills)) + Rank(RankValue).SkillBonus;
    P.Tier = FMath::Clamp(1 + Since / FMath::Max(1, R.TierEveryWaves), 1, FMath::Max(1, R.MaxTier));
    return P;
}
TArray<FName> CireRaces::MatchOrder(int32 Seed, const FCireNPCArchetype& A)
{
    TArray<FName> Pool;
    for (const FCireNPCAbility& Ab : A.Abilities) if (!Ab.bBasic && !Ab.bCore && !Ab.bBorrowed) Pool.Add(Ab.Id); // jungle-packs: borrowed skills are pack-only
    // String CRC, not FName hashes: the order must match on every machine and every run for a seed.
    FRandomStream Stream(static_cast<int32>(HashCombine(static_cast<uint32>(Seed), FCrc::StrCrc32(*A.Id.ToString()))));
    for (int32 I = Pool.Num() - 1; I > 0; --I) Pool.Swap(I, Stream.RandRange(0, I));
    return Pool;
}
TArray<FName> CireRaces::Loadout(int32 Seed, const FCireNPCArchetype& A, ECireNPCRank RankValue, int32 Count)
{
    TArray<FName> Out;
    if (Count <= 0) return Out;
    for (const FCireNPCAbility& Ab : A.Abilities) if (Ab.bCore && !Ab.bBorrowed) Out.Add(Ab.Id);
    const TArray<FName> Order = MatchOrder(Seed, A);
    // Normal and veteran units only ever use the match's drawn hand; higher ranks reach into the rest of the pool.
    const int32 Cap = RankValue <= ECireNPCRank::Veteran ? FMath::Min(A.PoolDraw, Order.Num()) : Order.Num();
    for (int32 I = 0; I < FMath::Min(Count, Cap); ++I) Out.Add(Order[I]);
    return Out;
}
FString CireRaces::TierSuffix(int32 Tier) { return Tier >= 3 ? TEXT(" III") : Tier == 2 ? TEXT(" II") : FString(); }

// ============================================================================================ runtime
void CireRaces::BeginMatch(ACireGameMode* Mode, int32 Seed)
{
    if (!Mode || !Mode->GetWorld()) return;
    int32 Parsed = 0;
    if (Seed < 0 && FParse::Value(FCommandLine::Get(), TEXT("CireRaceSeed="), Parsed) && Parsed > 0) Seed = Parsed;
    if (Seed <= 0) Seed = 1 + static_cast<int32>((FPlatformTime::Cycles() ^ (static_cast<uint32>(FMath::Rand()) << 11)) % 2000000000u);
    for (auto It = GSeeds.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
    GSeeds.Add(Mode->GetWorld(), Seed);
    if (auto* State = Mode->GetGameState<ACireGameState>()) { State->MonsterSkillSeed = Seed; State->ForceNetUpdate(); }
    UE_LOG(LogCireRaces, Display, TEXT("CIRE_RACE_MATCH_SEED %d"), Seed);
}
int32 CireRaces::MatchSeed(const UWorld* World)
{
    if (const auto* State = World ? World->GetGameState<ACireGameState>() : nullptr) if (State->MonsterSkillSeed != 0) return State->MonsterSkillSeed;
    if (const int32* Seed = World ? GSeeds.Find(const_cast<UWorld*>(World)) : nullptr) return *Seed;
    return 1;
}

void CireRaces::ApplyRank(ACireMonster* M, ECireNPCRank RankValue, int32 Palette)
{
    auto* S = St(M); const auto* A = Arch(M);
    if (!S || !A || !M->HasAuthority()) return;
    {
        const FCireRankStyle& Style = Rank(RankValue);
        S->Rank = static_cast<uint8>(RankValue); S->PaletteIndex = static_cast<uint8>(FMath::Clamp(Palette, 0, 15));
        // Stats: warlord is the boss's own kit; every other rank scales the unit.
        if (RankValue != ECireNPCRank::Warlord && RankValue != ECireNPCRank::Normal)
        {
            M->MaxHealth = M->Health = FMath::Clamp(M->MaxHealth * Style.Health, 1.f, 1.e9f);
            M->Damage = FMath::Clamp(M->Damage * Style.Damage, 0.f, 100000.f);
        }
        if (S->Classification != ECireNPCClass::Boss)
        {
            // Boss-coloured ranks on non-boss bodies read as elites (the boss frame stays for real bosses).
            S->Classification = Style.Classification == ECireNPCClass::Boss ? ECireNPCClass::Elite : Style.Classification;
            if (RankValue != ECireNPCRank::Normal) M->MonsterName = FString::Printf(TEXT("%s | %s"), *Style.Label, *A->DisplayName);
        }
        else if (RankValue == ECireNPCRank::Mythic) M->MonsterName = FString::Printf(TEXT("MYTHIC BOSS | %s"), *A->DisplayName);
    }
    M->ForceNetUpdate();
    ApplySkin(M);
}

void CireRaces::ApplyLoadout(ACireMonster* M, const FCireSkillProgression& R, int32 Wave, int32 CountOverride, int32 TierOverride)
{
    auto* S = St(M); const auto* A = Arch(M);
    if (!S || !A || !M->HasAuthority()) return;
    const ECireNPCRank RankValue = RankOf(M);
    const FCireSkillPlan P = Plan(R, Wave, RankValue);
    const int32 Count = CountOverride >= 0 ? CountOverride : P.Count;
    int32 Tier = TierOverride > 0 ? TierOverride : P.Tier;
    if (Count > 0 && Tier <= 0) Tier = 1;
    S->Loadout = Loadout(MatchSeed(M->GetWorld()), *A, RankValue, Count);
    if (!A->RaceId.IsNone()) GSeenRaces.AddUnique(A->RaceId);
    S->bLoadoutSet = true;
    S->SkillTier = static_cast<uint8>(S->Loadout.IsEmpty() ? 0 : FMath::Clamp(Tier, 1, 5));
    M->ForceNetUpdate();
}

void CireRaces::ApplyPackUnit(ACireMonster* M, int32 Tier, bool bLeader, int32 Wave)
{
    if (!IsValid(M)) return;
    // Rank colour only: challenge packs keep their challenge health/damage scaling.
    auto* S = St(M); if (!S) return;
    const ECireNPCRank RankValue = bLeader ? ECireNPCRank::Warlord : Tier >= 3 ? ECireNPCRank::Champion : ECireNPCRank::Elite;
    S->Rank = static_cast<uint8>(RankValue);
    // Packs are optional elite camps: they always fight with skills, on the wave schedule's count and tier.
    const FCireSkillProgression& R = Rules(M->GetWorld());
    ApplyLoadout(M, R, FMath::Max(Wave, R.FirstSkillWave));
    ApplySkin(M);
}

// ============================================================================================ combat
bool CireRaces::IsAbilityActive(const ACireMonster* M, FName Id) { const auto* S = St(M); return !S || S->IsAbilityActive(Id); }
namespace
{
int32 TierOf(const ACireMonster* M) { const auto* S = St(M); return S && S->bLoadoutSet ? FMath::Max<int32>(1, S->SkillTier) : 1; }
}
float CireRaces::TierDamage(const ACireMonster* M) { return 1.f + Rules(M ? M->GetWorld() : nullptr).TierDamage * (TierOf(M) - 1); }
float CireRaces::TierCooldown(const ACireMonster* M) { return FMath::Max(.3f, 1.f - Rules(M ? M->GetWorld() : nullptr).TierCooldown * (TierOf(M) - 1)); }
float CireRaces::TierDuration(const ACireMonster* M) { return 1.f + Rules(M ? M->GetWorld() : nullptr).TierDuration * (TierOf(M) - 1); }
float CireRaces::RankSize(const ACireMonster* M) { return Rank(RankOf(M)).Size; }

bool CireRaces::CanSummon(const ACireMonster* M, const FCireNPCAbility& A)
{
    if (!IsValid(M) || A.SummonId.IsNone() || !CireNPCArchetypes::Find(A.SummonId)) return false;
    int32 Alive = 0;
    if (const auto* List = GSummons.Find(const_cast<ACireMonster*>(M)))
        for (const auto& S : *List) Alive += S.IsValid() && S->Health > 0 && !S->IsActorBeingDestroyed();
    return Alive + A.Count <= A.Count * 2;
}

int32 CireRaces::SpawnSummons(ACireMonster* M, const FCireNPCAbility& A)
{
    auto* Mode = M && M->GetWorld() ? M->GetWorld()->GetAuthGameMode<ACireGameMode>() : nullptr;
    if (!Mode || !CanSummon(M, A)) return 0;
    auto& List = GSummons.FindOrAdd(M);
    List.RemoveAll([](const TWeakObjectPtr<ACireMonster>& S) { return !S.IsValid() || S->Health <= 0; });
    const auto* Parent = St(M);
    int32 Spawned = 0;
    for (int32 I = 0; I < A.Count; ++I)
    {
        const float Angle = 2.f * PI * (I + .5f) / FMath::Max(1, A.Count);
        const FVector At = M->GetActorLocation() + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * 220.f + FVector(0, 0, 20);
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
        auto* S = M->GetWorld()->SpawnActor<ACireMonster>(ACireMonster::StaticClass(), At, M->GetActorRotation(), Params);
        if (!S) continue;
        S->Lane = M->Lane; S->PackId = M->PackId; S->Tier = M->Tier;
        CireNPCCombat::ConfigureArchetype(S, A.SummonId, CurrentWave(M), M->Tier, Mode->Clock.Round());
        ApplyRank(S, ECireNPCRank::Normal, Parent ? Parent->PaletteIndex : 0);
        ApplyLoadout(S, Rules(M->GetWorld()), CurrentWave(M));
        S->MonsterName = FString::Printf(TEXT("Summoned | %s"), *S->GetNPCDisplayName());
        Mode->Monsters.Add(S);
        CireWaveDirector::AdoptSummon(Mode, S, M);
        if (IsValid(M->Victim)) { CireThreat::Engage(S, M->Victim); CireThreat::Select(S); }
        List.Add(S); ++Spawned; ++GStats.Summoned;
    }
    UE_LOG(LogCireRaces, Display, TEXT("CIRE_RACE_SUMMON %s summoned %d x %s"), *M->GetNPCDisplayName(), Spawned, *A.SummonId.ToString());
    return Spawned;
}

FCireRaceStats CireRaces::Stats()
{
    FCireRaceStats S = GStats;
    TArray<FString> Names; for (FName N : GSeenRaces) Names.Add(N.ToString());
    S.Races = FString::Join(Names, TEXT(","));
    return S;
}

int32 CireRaces::OnAbilityReleased(ACireMonster* M, const FCireNPCAbility& A, FVector Aim)
{
    if (!IsValid(M) || !M->HasAuthority()) return 0;
    if (!A.bBasic) { ++GStats.Casts; if (GStats.EarliestCastWave == 0) GStats.EarliestCastWave = FMath::Max(1, CurrentWave(M)); }
    if (A.Kind == ECireNPCAbilityKind::Summon) { SpawnSummons(M, A); if (!A.Buff.IsNone()) CireBuffs::Apply(M, A.Buff, 3.f, M); return 0; }
    if (A.Kind == ECireNPCAbilityKind::Deploy) // new-champions: Aetheri engineers and the Hierarch place constructs
    {
        FVector Ground = Aim; FHitResult Floor; FCollisionQueryParams Q(SCENE_QUERY_STAT(CireDeployFloor), false, M);
        if (M->GetWorld()->LineTraceSingleByObjectType(Floor, Aim + FVector(0, 0, 300), Aim - FVector(0, 0, 600), FCollisionObjectQueryParams(ECC_WorldStatic), Q)) Ground = Floor.ImpactPoint;
        const int32 Placed = CireTechConstructs::Deploy(M, A.DeployRecipe, Ground, nullptr, FMath::Max(.1f, A.DamageMultiplier) * TierDamage(M)).Num();
        if (!A.Buff.IsNone()) CireBuffs::Apply(M, A.Buff, 3.f, M);
        UE_LOG(LogCireRaces, Display, TEXT("CIRE_RACE_DEPLOY %s placed %d x %s"), *M->GetNPCDisplayName(), Placed, *A.DeployRecipe.ToString());
        return Placed;
    }
    // Buff-style skills show their themed visual on the caster.
    switch (A.Kind)
    {
    case ECireNPCAbilityKind::Rally: case ECireNPCAbilityKind::Enrage: case ECireNPCAbilityKind::ShieldWall: case ECireNPCAbilityKind::Guard:
    case ECireNPCAbilityKind::HealAlly: case ECireNPCAbilityKind::Provoke:
        if (!A.Buff.IsNone()) CireBuffs::Apply(M, A.Buff, A.Kind == ECireNPCAbilityKind::Enrage ? 60.f : FMath::Max(3.f, A.Duration), M);
        return 0;
    default: break;
    }
    const bool bShape = A.Kind == ECireNPCAbilityKind::Cone || A.Kind == ECireNPCAbilityKind::TargetCircle || A.Kind == ECireNPCAbilityKind::SelfCircle ||
        A.Kind == ECireNPCAbilityKind::Charge || A.Kind == ECireNPCAbilityKind::Pull;
    if (!bShape || (!A.HasRiders() && A.Kind != ECireNPCAbilityKind::Pull)) return 0;
    const FVector From = M->GetActorLocation();
    const FVector Toward = (Aim - From).GetSafeNormal2D();
    const float Scale = TierDuration(M);
    int32 Affected = 0;
    for (TCireActorIterator<ACireHero> It(M->GetWorld()); It; ++It)
    {
        ACireHero* H = *It;
        if (!Targetable(M, H)) continue;
        const FVector P = H->GetActorLocation();
        const float Pad = H->GetCapsuleComponent()->GetScaledCapsuleRadius();
        bool bInside = false; FVector Center = From;
        switch (A.Kind)
        {
        case ECireNPCAbilityKind::TargetCircle: Center = Aim; bInside = FVector::Dist2D(P, Aim) <= A.Radius + Pad; break;
        case ECireNPCAbilityKind::SelfCircle: bInside = FVector::Dist2D(P, From) <= A.Radius + Pad; break;
        case ECireNPCAbilityKind::Cone:
        {
            const FVector To = P - From; const float D = static_cast<float>(To.Size2D());
            bInside = D <= A.Radius + Pad && (D < Pad || FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(To.GetSafeNormal2D(), Toward))) <= A.Angle * .5f + 4.f);
            break;
        }
        default: // charge / pull: a line from the caster toward the aim
        {
            const FVector To = P - From;
            const float Along = static_cast<float>(FVector::DotProduct(To, Toward));
            const float Side = static_cast<float>((To - Toward * Along).Size2D());
            const float Length = A.Kind == ECireNPCAbilityKind::Pull ? FMath::Min(A.Length, static_cast<float>(FVector::Dist2D(From, Aim)) + 150.f) : A.Length;
            bInside = !Toward.IsNearlyZero() && Along >= -Pad && Along <= Length + Pad && Side <= A.Width * .5f + Pad;
            break;
        }
        }
        if (!bInside) continue;
        ++Affected; ++GStats.RiderHits;
        const float Now = M->GetWorld()->GetTimeSeconds();
        float Mark = 1.5f;
        if (A.Root > 0) { CireBuffs::Apply(H, RootedId, A.Root * Scale, M); Mark = FMath::Max(Mark, A.Root * Scale); H->GetCharacterMovement()->StopMovementImmediately(); }
        if (A.Silence > 0) { CireBuffs::Apply(H, SilencedId, A.Silence * Scale, M); Mark = FMath::Max(Mark, A.Silence * Scale); }
        if (A.Slow > 0) { H->SlowUntil = FMath::Max(H->SlowUntil, Now + A.Slow * Scale); Mark = FMath::Max(Mark, A.Slow * Scale); }
        if (A.Kind == ECireNPCAbilityKind::Pull)
        {
            // Drag to just in front of the caster.
            const FVector Back = From - P; const float Dist = static_cast<float>(Back.Size2D());
            if (Dist > 160.f) H->LaunchCharacter(Back.GetSafeNormal2D() * FMath::Clamp((Dist - 140.f) * 2.4f, 500.f, 3200.f) + FVector(0, 0, 220), true, true);
        }
        else if (A.Knockback > 0)
        {
            FVector Away = (P - Center).GetSafeNormal2D();
            if (Away.IsNearlyZero()) Away = Toward.IsNearlyZero() ? M->GetActorForwardVector() : Toward;
            H->LaunchCharacter(Away * A.Knockback * 2.2f + FVector(0, 0, 300), true, true);
        }
        if (!A.Buff.IsNone() && A.Buff != RootedId && A.Buff != SilencedId) CireBuffs::Apply(H, A.Buff, Mark, M);
    }
    if (Affected > 0) UE_LOG(LogCireRaces, Verbose, TEXT("CIRE_RACE_RIDER %s %s affected=%d"), *M->GetNPCDisplayName(), *A.Id.ToString(), Affected);
    return Affected;
}

bool CireRaces::IsRooted(const ACireHero* H) { return H && CireBuffs::IsActive(H, RootedId); }
bool CireRaces::IsSilenced(const ACireHero* H) { return H && CireBuffs::IsActive(H, SilencedId); }

// ============================================================================================ skin
bool CireRaces::HasSkin(const ACireMonster* M)
{
    if (!M || !M->GetMesh() || !M->MonsterArt || !M->MonsterArt->IsTripoApplied()) return false;
    const auto* Skin = SkinMaterial();
    const auto* MID = Cast<UMaterialInstanceDynamic>(M->GetMesh()->GetMaterial(0));
    return Skin && MID && MID->Parent == Skin;
}

namespace
{
TAutoConsoleVariable<int32> CVarSwayDebug(TEXT("cire.Monsters.SwayDebug"), 0,
    TEXT("monster-rig: 1 paints the skin sway mask (tentacles/vines moved by the material) in green on Tripo bodies."));

// monster-rig: tentacles and vines Tripo's humanoid rig gave no bones sway in M_CireMonsterSkin (world position
// offset on the pre-skinned position). Up to two ellipsoid regions per body; SwayAmount 0 = rigid.
void ApplySway(const ACireMonster* M, UMaterialInstanceDynamic* MID)
{
    static const TCHAR* const Centers[] = {TEXT("SwayCenterA"), TEXT("SwayCenterB")};
    static const TCHAR* const Radii[] = {TEXT("SwayRadiiA"), TEXT("SwayRadiiB")};
    static const TCHAR* const Bands[] = {TEXT("SwayBandA"), TEXT("SwayBandB")};
    const TArray<CireMonsterArt::FSwayRegion>* Regions = M->MonsterArt ? &M->MonsterArt->AppliedSway() : nullptr;
    for (int32 I = 0; I < 2; ++I)
    {
        const CireMonsterArt::FSwayRegion* R = Regions && Regions->IsValidIndex(I) ? &(*Regions)[I] : nullptr;
        MID->SetVectorParameterValue(Centers[I], R ? FLinearColor(R->Center.X, R->Center.Y, R->Center.Z, 0.f) : FLinearColor::Black);
        MID->SetVectorParameterValue(Radii[I], R ? FLinearColor(R->Radii.X, R->Radii.Y, R->Radii.Z, 0.f) : FLinearColor(1.f, 1.f, 1.f, 0.f));
        MID->SetVectorParameterValue(Bands[I], R ? FLinearColor(R->Root, R->Tip, R->Amount, 0.f) : FLinearColor(1.f, 0.f, 0.f, 0.f));
    }
    MID->SetScalarParameterValue(TEXT("SwaySpeed"), M->MonsterArt ? M->MonsterArt->AppliedSwaySpeed : 1.6f);
    MID->SetScalarParameterValue(TEXT("SwayWave"), M->MonsterArt ? M->MonsterArt->AppliedSwayWave : 1.f);
    MID->SetScalarParameterValue(TEXT("SwayDebug"), CVarSwayDebug.GetValueOnGameThread() != 0 ? 1.f : 0.f);
}
}

bool CireRaces::ApplySkin(ACireMonster* M)
{
    if (!IsValid(M) || M->GetNetMode() == NM_DedicatedServer || !M->GetMesh()) return false;
    const auto* S = St(M); const auto* A = Arch(M);
    if (!S || !A) return false;
    const FCireRace* Race = FindRace(A->RaceId);
    // monster-expansion: a special spawn keeps its rank stats but takes the special colour with a strong rim and glow.
    FCireRankStyle Style = Rank(RankOf(M));
    if (M->SpecialSpawn != 0)
    {
        Style.Color = Style.Trim = CireMonsterExpansion::SpecialColor(M->SpecialSpawn);
        Style.Rim = FMath::Max(Style.Rim, 2.4f); Style.Glow = FMath::Max(Style.Glow, 2.5f); Style.BodyTint = FMath::Max(Style.BodyTint, .1f);
    }
    const FCireRacePalette Palette = Race ? Race->Palette(S->PaletteIndex) : FCireRacePalette();
    // A unit drawn on its own art keeps its authored colours on its base palette; borrowed bodies and reskin sets recolour.
    const bool bOwnBody = A->FallbackBody.IsNone() || A->FallbackBody == A->Id || CireMonsterArt::HasOwnBody(A->Id);
    const float RaceStrength = !Race ? 0.f : (bOwnBody && S->PaletteIndex == 0) ? 0.f : .88f;
    USkeletalMeshComponent* Mesh = M->GetMesh();
    // monster-expansion: a reskinned Fab creature (RaceMeshes.fabx.json "reskin") takes the race skin on its listed slots:
    // the vendor's base colour/normal feed M_CireMonsterSkin, recoloured by the reskin tint (no armour mask on vendor maps).
    if (M->MonsterArt && M->MonsterArt->IsFabApplied())
        if (const CireMonsterArt::FBody* Reskin = M->MonsterArt->AppliedReskin())
        {
            UMaterialInterface* Skin = SkinMaterial();
            if (!Skin) return false;
            for (const auto& Slot : Reskin->ReskinTextures)
            {
                if (Slot.Key < 0 || Slot.Key >= Mesh->GetNumMaterials()) continue;
                auto* MID = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(Slot.Key));
                if (!MID || MID->Parent != Skin)
                {
                    MID = UMaterialInstanceDynamic::Create(Skin, M);
                    if (!MID) continue;
                    for (const auto& Tex : Slot.Value)
                        if (UTexture* Texture = LoadObject<UTexture>(nullptr, *Tex.Value)) MID->SetTextureParameterValue(Tex.Key, Texture);
                    Mesh->SetMaterial(Slot.Key, MID);
                }
                const FLinearColor Rim = M->SpecialSpawn != 0 || RankOf(M) != ECireNPCRank::Normal ? Style.Color : Reskin->ReskinRim;
                MID->SetVectorParameterValue(TEXT("RaceTint"), Reskin->ReskinTint);
                MID->SetScalarParameterValue(TEXT("RaceTintStrength"), Reskin->ReskinTintStrength);
                MID->SetScalarParameterValue(TEXT("RaceAccentStrength"), 0.f);
                MID->SetScalarParameterValue(TEXT("ArmorMaskGain"), 0.f);
                MID->SetVectorParameterValue(TEXT("RankColor"), Style.Color);
                MID->SetVectorParameterValue(TEXT("TrimColor"), Style.Trim);
                MID->SetScalarParameterValue(TEXT("RankArmor"), 0.f);
                MID->SetScalarParameterValue(TEXT("RankBody"), FMath::Max(Reskin->ReskinBody, Style.BodyTint));
                MID->SetScalarParameterValue(TEXT("RankGlow"), 0.f);
                MID->SetVectorParameterValue(TEXT("RimColor"), Rim);
                MID->SetScalarParameterValue(TEXT("RimStrength"), FMath::Max(Reskin->ReskinRimStrength, Style.Rim * .5f)); // vendor albedo is brighter than Tripo maps
            }
            return true;
        }
    // fab-integration: purchased Fab bodies keep their authored (non-Tripo) materials; the rank shows as the rim overlay.
    if (M->MonsterArt && M->MonsterArt->IsFabApplied()) return false;
    if (M->MonsterArt && M->MonsterArt->IsTripoApplied())
    {
        UMaterialInterface* Skin = SkinMaterial();
        if (!Skin) return false;
        static const FName Textures[] = {TEXT("BaseColorTex"), TEXT("NormalTex"), TEXT("MetallicTex"), TEXT("RoughnessTex")};
        for (int32 I = 0; I < Mesh->GetNumMaterials(); ++I)
        {
            UMaterialInterface* Current = Mesh->GetMaterial(I);
            auto* MID = Cast<UMaterialInstanceDynamic>(Current);
            if (!MID || MID->Parent != Skin)
            {
                MID = UMaterialInstanceDynamic::Create(Skin, M);
                if (!MID) continue;
                for (const FName& Name : Textures)
                {
                    UTexture* Texture = nullptr;
                    if (Current && Current->GetTextureParameterValue(FHashedMaterialParameterInfo(Name), Texture) && Texture) MID->SetTextureParameterValue(Name, Texture);
                }
                Mesh->SetMaterial(I, MID);
            }
            MID->SetVectorParameterValue(TEXT("RaceTint"), Palette.Base);
            MID->SetScalarParameterValue(TEXT("RaceTintStrength"), RaceStrength);
            MID->SetVectorParameterValue(TEXT("RaceAccent"), Palette.Accent);
            MID->SetScalarParameterValue(TEXT("RaceAccentStrength"), RaceStrength > 0 ? .65f : 0.f);
            MID->SetVectorParameterValue(TEXT("RankColor"), Style.Color);
            MID->SetVectorParameterValue(TEXT("TrimColor"), Style.Trim);
            MID->SetScalarParameterValue(TEXT("RankArmor"), Style.ArmorTint);
            MID->SetScalarParameterValue(TEXT("RankBody"), Style.BodyTint);
            MID->SetScalarParameterValue(TEXT("RankGlow"), Style.Glow);
            MID->SetVectorParameterValue(TEXT("RimColor"), Style.Rim > 0 ? Style.Color : (Race ? Race->Glow : FLinearColor::Black));
            MID->SetScalarParameterValue(TEXT("RimStrength"), Style.Rim);
            ApplySway(M, MID);
        }
        return true;
    }
    // Mannequin fallback: race base colour, pulled toward the rank colour.
    const FLinearColor Tint = RankOf(M) == ECireNPCRank::Normal ? (Race ? Palette.Base : A->Tint) :
        FLinearColor::LerpUsingHSV(Race ? Palette.Base : A->Tint, Style.Color, .55f);
    for (int32 I = 0; I < Mesh->GetNumMaterials(); ++I)
        if (auto* Dynamic = Mesh->CreateDynamicMaterialInstance(I))
        {
            Dynamic->SetVectorParameterValue(TEXT("Paint Tint"), Tint); Dynamic->SetVectorParameterValue(TEXT("LogoTint"), Tint);
            Dynamic->SetVectorParameterValue(TEXT("Global BaseColor"), Tint); Dynamic->SetVectorParameterValue(TEXT("Tint"), Tint);
        }
    return true;
}

void CireRaces::OnCastPresented(ACireMonster* M, FName AbilityId)
{
    const auto* A = Arch(M);
    const FCireNPCAbility* Ability = A ? A->FindAbility(AbilityId) : nullptr;
    if (Ability && !Ability->Cue.IsNone()) CireAudio::PlayCue(M, Ability->Cue, M->GetActorLocation());
}
