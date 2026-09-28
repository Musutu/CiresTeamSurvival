// game-profiles: named profiles for the F8 editors and the game type bundle (CireProfiles.h, Docs/RESUME-game-profiles.md).
#include "CireProfiles.h"
#include "CireGame.h"
#include "CireWaves.h"
#include "CireDeveloperTools.h"
#include "CireLoot.h"
#include "CireSkillShop.h"
#include "CireVendors.h"
#include "CireJunglePacks.h"
#include "CireMobility.h"
#include "CireUnitSpacing.h"
#include "CireMapLayout.h"
#include "CireLanePath.h"
#include "CireLayoutRuntime.h"
#include "CireAbilityTuner.h"
#include "CireKitEditor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireProfiles, Log, All);

const FName CireProfiles::Match(TEXT("Match"));
const FName CireProfiles::Economy(TEXT("Economy"));
const FName CireProfiles::Packs(TEXT("Packs"));
const FName CireProfiles::Movement(TEXT("Movement"));
const FName CireProfiles::Spacing(TEXT("Spacing"));

namespace
{
FString GProfileRootOverride;
TMap<FName, FString> GActiveProfiles;
FString GLayoutOverride;
FString GActiveWorldEdit;
FString GLastBundleKey; // the last game type (and bundle) applied by ApplyGameType
CireGameProfiles::FWorldEditApply GWorldEditApply;
CireGameProfiles::FWorldEditList GWorldEditList;

bool ProfilesFail(FString* Error, const FString& Text) { if (Error) *Error = Text; return false; }
double ProfNum(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double Default)
{
    double V = Default; return O && O->TryGetNumberField(Key, V) && FMath::IsFinite(V) ? V : Default;
}
bool ProfBool(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, bool Default)
{
    bool V = Default; return O && O->TryGetBoolField(Key, V) ? V : Default;
}
TSharedPtr<FJsonObject> ProfObj(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
{
    const TSharedPtr<FJsonObject>* Out = nullptr; return O && O->TryGetObjectField(Key, Out) && Out ? *Out : nullptr;
}
FString ProfJsonText(const TSharedRef<FJsonObject>& O)
{
    FString Text; FJsonSerializer::Serialize(O, TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text)); return Text;
}
TSharedPtr<FJsonObject> ProfReadFile(const FString& Path)
{
    FString Text; TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root)) return nullptr;
    return Root;
}
TArray<ACireHero*> WorldHeroes(UWorld* World)
{
    TArray<ACireHero*> Out;
    if (World) for (TActorIterator<ACireHero> It(World); It; ++It) if (IsValid(*It)) Out.Add(*It);
    return Out;
}
ACireGameMode* AuthMode(UWorld* World) { return World ? World->GetAuthGameMode<ACireGameMode>() : nullptr; }

// ---------------------------------------------------------------- Match (FCireDeveloperSettings)
#define CIRE_PROFILE_MATCH_FLOATS(X) X(PrepSeconds) X(ArenaSeconds) X(RecoverySeconds) X(WaveBreatherSeconds) X(MonsterHealthScale) X(MonsterDamageScale) \
    X(ProjectileSpeedScale) X(ProjectileCollisionScale) X(EffectDurationScale) X(TelegraphScale) X(ConstructHealthScale) X(SummonHealthScale) X(CooldownScale) \
    X(SpawnX) X(SpawnSpread) X(SpawnSpacing) X(SpawnOffsetY) X(SimulationSpeed)
#define CIRE_PROFILE_MATCH_INTS(X) X(WavesPerCycle) X(WaveUnitsOverride) X(TeamLives) X(WorldPolicy) X(PlayerPolicy) X(MonsterPolicy) X(ProtectionPolicy) X(WallPolicy)
#define CIRE_PROFILE_MATCH_BOOLS(X) X(bPauseWaveSpawns) X(bFreezePhaseClock) X(bOverrideCollisionPolicies)

// ---------------------------------------------------------------- Economy
TSharedRef<FJsonObject> EconomyCapture()
{
    const Cires::Items::Economy& E = CireLoot::MutableEconomy();
    const FCireSkillShopData& S = CireSkillShop::Get(); const auto& R = S.Rules;
    const FCireVendorPricing& P = CireVendors::Pricing();
    auto Kill = MakeShared<FJsonObject>();
    Kill->SetNumberField(TEXT("mobBase"), E.MobBase); Kill->SetNumberField(TEXT("mobStep"), E.MobStep); Kill->SetNumberField(TEXT("stepEveryWaves"), E.StepEveryWaves);
    Kill->SetNumberField(TEXT("armoredMultiplier"), E.ArmoredMultiplier); Kill->SetNumberField(TEXT("bossMultiplier"), E.BossMultiplier);
    Kill->SetNumberField(TEXT("packUnitMultiplier"), E.PackUnitMultiplier); Kill->SetNumberField(TEXT("packLeaderMultiplier"), E.PackLeaderMultiplier);
    auto Shop = MakeShared<FJsonObject>();
    Shop->SetNumberField(TEXT("activePrice"), R.ActivePrice); Shop->SetNumberField(TEXT("passivePrice"), R.PassivePrice); Shop->SetNumberField(TEXT("ultimatePrice"), R.UltimatePrice);
    Shop->SetNumberField(TEXT("activeOwnedGrowth"), R.ActiveOwnedGrowth); Shop->SetNumberField(TEXT("levelUpBase"), R.LevelUpBase); Shop->SetNumberField(TEXT("levelUpGrowth"), R.LevelUpGrowth);
    Shop->SetNumberField(TEXT("effectPerLevel"), R.EffectPerLevel); Shop->SetNumberField(TEXT("costPerLevel"), R.CostPerLevel);
    Shop->SetNumberField(TEXT("cooldownPerLevel"), R.CooldownPerLevel); Shop->SetNumberField(TEXT("minCooldownFactor"), R.MinCooldownFactor);
    Shop->SetNumberField(TEXT("activeSlotsStart"), R.ActiveSlotsStart); Shop->SetNumberField(TEXT("activeSlotEveryWaves"), R.ActiveSlotEveryWaves);
    Shop->SetNumberField(TEXT("passiveFromWave"), R.PassiveFromWave); Shop->SetNumberField(TEXT("ultimateFromWave"), R.UltimateFromWave);
    Shop->SetNumberField(TEXT("readyMaxSeconds"), S.ReadyMaxSeconds);
    auto Price = MakeShared<FJsonObject>();
    Price->SetBoolField(TEXT("shopAnywhere"), P.bShopAnywhere); Price->SetNumberField(TEXT("vendorDiscount"), P.VendorDiscount);
    Price->SetNumberField(TEXT("outOfTownSurcharge"), P.FieldSurcharge); Price->SetNumberField(TEXT("townVendorRadius"), P.TownVendorRadius);
    Price->SetNumberField(TEXT("vendorReachSlack"), P.VendorReachSlack);
    auto Root = MakeShared<FJsonObject>();
    Root->SetObjectField(TEXT("killGold"), Kill); Root->SetObjectField(TEXT("skillShop"), Shop); Root->SetObjectField(TEXT("pricing"), Price);
    return Root;
}
bool EconomyApply(const TSharedRef<FJsonObject>& V)
{
    Cires::Items::Economy& E = CireLoot::MutableEconomy();
    if (const TSharedPtr<FJsonObject> K = ProfObj(V, TEXT("killGold")))
    {
        E.MobBase = FMath::Clamp(static_cast<int>(ProfNum(K, TEXT("mobBase"), E.MobBase)), 0, 1000);
        E.MobStep = FMath::Clamp(static_cast<int>(ProfNum(K, TEXT("mobStep"), E.MobStep)), 0, 1000);
        E.StepEveryWaves = FMath::Clamp(static_cast<int>(ProfNum(K, TEXT("stepEveryWaves"), E.StepEveryWaves)), 1, 100);
        E.ArmoredMultiplier = FMath::Clamp(ProfNum(K, TEXT("armoredMultiplier"), E.ArmoredMultiplier), 0., 1000.);
        E.BossMultiplier = FMath::Clamp(ProfNum(K, TEXT("bossMultiplier"), E.BossMultiplier), 0., 1000.);
        E.PackUnitMultiplier = FMath::Clamp(ProfNum(K, TEXT("packUnitMultiplier"), E.PackUnitMultiplier), 0., 1000.);
        E.PackLeaderMultiplier = FMath::Clamp(ProfNum(K, TEXT("packLeaderMultiplier"), E.PackLeaderMultiplier), 0., 10000.);
    }
    FCireSkillShopData& S = CireSkillShop::Mutable(); auto& R = S.Rules;
    if (const TSharedPtr<FJsonObject> Sh = ProfObj(V, TEXT("skillShop")))
    {
        R.ActivePrice = FMath::Clamp(ProfNum(Sh, TEXT("activePrice"), R.ActivePrice), 0., 1000.);
        R.PassivePrice = FMath::Clamp(ProfNum(Sh, TEXT("passivePrice"), R.PassivePrice), 0., 1000.);
        R.UltimatePrice = FMath::Clamp(ProfNum(Sh, TEXT("ultimatePrice"), R.UltimatePrice), 0., 1000.);
        R.ActiveOwnedGrowth = FMath::Clamp(ProfNum(Sh, TEXT("activeOwnedGrowth"), R.ActiveOwnedGrowth), 0., 10.);
        R.LevelUpBase = FMath::Clamp(ProfNum(Sh, TEXT("levelUpBase"), R.LevelUpBase), 0., 1000.);
        R.LevelUpGrowth = FMath::Clamp(ProfNum(Sh, TEXT("levelUpGrowth"), R.LevelUpGrowth), 1., 10.);
        R.EffectPerLevel = FMath::Clamp(ProfNum(Sh, TEXT("effectPerLevel"), R.EffectPerLevel), 0., 5.);
        R.CostPerLevel = FMath::Clamp(ProfNum(Sh, TEXT("costPerLevel"), R.CostPerLevel), 0., 5.);
        R.CooldownPerLevel = FMath::Clamp(ProfNum(Sh, TEXT("cooldownPerLevel"), R.CooldownPerLevel), 0., 1.);
        R.MinCooldownFactor = FMath::Clamp(ProfNum(Sh, TEXT("minCooldownFactor"), R.MinCooldownFactor), .05, 1.);
        R.ActiveSlotsStart = FMath::Clamp(static_cast<int>(ProfNum(Sh, TEXT("activeSlotsStart"), R.ActiveSlotsStart)), 1, 6);
        R.ActiveSlotEveryWaves = FMath::Clamp(static_cast<int>(ProfNum(Sh, TEXT("activeSlotEveryWaves"), R.ActiveSlotEveryWaves)), 1, 50);
        R.PassiveFromWave = FMath::Clamp(static_cast<int>(ProfNum(Sh, TEXT("passiveFromWave"), R.PassiveFromWave)), 0, 200);
        R.UltimateFromWave = FMath::Clamp(static_cast<int>(ProfNum(Sh, TEXT("ultimateFromWave"), R.UltimateFromWave)), 0, 200);
        S.ReadyMaxSeconds = FMath::Clamp(static_cast<float>(ProfNum(Sh, TEXT("readyMaxSeconds"), S.ReadyMaxSeconds)), 0.f, 3600.f);
    }
    FCireVendorPricing& P = CireVendors::MutablePricing();
    if (const TSharedPtr<FJsonObject> Pr = ProfObj(V, TEXT("pricing")))
    {
        P.bShopAnywhere = ProfBool(Pr, TEXT("shopAnywhere"), P.bShopAnywhere);
        P.VendorDiscount = FMath::Clamp(static_cast<float>(ProfNum(Pr, TEXT("vendorDiscount"), P.VendorDiscount)), 0.f, .9f);
        P.FieldSurcharge = FMath::Clamp(static_cast<float>(ProfNum(Pr, TEXT("outOfTownSurcharge"), P.FieldSurcharge)), 0.f, 5.f);
        P.TownVendorRadius = FMath::Clamp(static_cast<float>(ProfNum(Pr, TEXT("townVendorRadius"), P.TownVendorRadius)), 0.f, 100000.f);
        P.VendorReachSlack = FMath::Clamp(static_cast<float>(ProfNum(Pr, TEXT("vendorReachSlack"), P.VendorReachSlack)), 0.f, 5000.f);
    }
    return true;
}
bool EconomyDefault(FString* Error)
{
    bool bOk = CireLoot::Reload();
    bOk = CireSkillShop::Reload() && bOk;
    FString Json, Why; FCireVendorData D;
    if (FFileHelper::LoadFileToString(Json, *(FPaths::ProjectContentDir() / TEXT("Data/Vendors.json"))) && CireVendors::ParseVendors(Json, D, Why)) CireVendors::MutablePricing() = D.Pricing;
    else { CireVendors::MutablePricing() = FCireVendorPricing(); bOk = false; }
    if (!bOk && Error) *Error = TEXT("An economy data file could not be read (built-in values kept for it).");
    return bOk;
}

// ---------------------------------------------------------------- Packs
TSharedRef<FJsonObject> PacksCapture()
{
    const FCireJungleRules& R = CireJunglePacks::Rules(); const FCirePackStats& S = R.Stats;
    auto Stats = MakeShared<FJsonObject>();
    Stats->SetNumberField(TEXT("baseDamage"), S.BaseDamage); Stats->SetNumberField(TEXT("tankHealth"), S.TankHealth);
    Stats->SetNumberField(TEXT("healerShare"), S.HealerShare); Stats->SetNumberField(TEXT("meleeShare"), S.MeleeShare);
    Stats->SetNumberField(TEXT("rangedShare"), S.RangedShare); Stats->SetNumberField(TEXT("casterShare"), S.CasterShare);
    Stats->SetNumberField(TEXT("globalHealth"), S.GlobalHealth); Stats->SetNumberField(TEXT("globalDamage"), S.GlobalDamage);
    TArray<TSharedPtr<FJsonValue>> Tiers;
    for (int32 I = 0; I < FMath::Min(R.Tiers.Num(), CireJunglePacks::MaxTier); ++I)
    {
        auto T = MakeShared<FJsonObject>(); T->SetNumberField(TEXT("health"), R.Tiers[I].Health); T->SetNumberField(TEXT("damage"), R.Tiers[I].Damage);
        Tiers.Add(MakeShared<FJsonValueObject>(T));
    }
    auto Root = MakeShared<FJsonObject>(); Root->SetObjectField(TEXT("stats"), Stats); Root->SetArrayField(TEXT("tiers"), Tiers);
    return Root;
}
void PacksSet(UWorld* World, const FCirePackStats& Stats, const TArray<FCirePackTier>& Tiers)
{
    float H[4] = {1, 1, 1, 1}, D[4] = {1, 1, 1, 1};
    for (int32 I = 0; I < 4 && I < Tiers.Num(); ++I) { H[I] = Tiers[I].Health; D[I] = Tiers[I].Damage; }
    CireJunglePacks::SetStats(World, Stats, H, D);
}
bool PacksApply(UWorld* World, const TSharedRef<FJsonObject>& V)
{
    const FCireJungleRules& R = CireJunglePacks::Rules();
    FCirePackStats S = R.Stats; TArray<FCirePackTier> Tiers = R.Tiers;
    if (const TSharedPtr<FJsonObject> St = ProfObj(V, TEXT("stats")))
    {
        auto F = [&](const TCHAR* Key, float& Field, float Lo, float Hi) { Field = FMath::Clamp(static_cast<float>(ProfNum(St, Key, Field)), Lo, Hi); };
        F(TEXT("baseDamage"), S.BaseDamage, 0, 100000); F(TEXT("tankHealth"), S.TankHealth, 1, 1000000);
        F(TEXT("healerShare"), S.HealerShare, .01f, 10); F(TEXT("meleeShare"), S.MeleeShare, .01f, 10); F(TEXT("rangedShare"), S.RangedShare, .01f, 10); F(TEXT("casterShare"), S.CasterShare, .01f, 10);
        F(TEXT("globalHealth"), S.GlobalHealth, .01f, 100); F(TEXT("globalDamage"), S.GlobalDamage, .01f, 100);
    }
    const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
    if (V->TryGetArrayField(TEXT("tiers"), List) && List)
        for (int32 I = 0; I < List->Num() && I < Tiers.Num(); ++I)
            if (const TSharedPtr<FJsonObject> T = (*List)[I].IsValid() ? (*List)[I]->AsObject() : nullptr)
            { Tiers[I].Health = static_cast<float>(ProfNum(T, TEXT("health"), Tiers[I].Health)); Tiers[I].Damage = static_cast<float>(ProfNum(T, TEXT("damage"), Tiers[I].Damage)); }
    PacksSet(World, S, Tiers);
    return true;
}
bool PacksDefault(UWorld* World, FString* Error)
{
    FString Json, Why; FCireJungleRules R;
    const bool bOk = FFileHelper::LoadFileToString(Json, *(FPaths::ProjectContentDir() / TEXT("Data/JunglePacks.json"))) && CireJunglePacks::ParseRules(Json, R, Why);
    if (!bOk) { R = CireJunglePacks::BuiltInRules(); if (Error) *Error = TEXT("JunglePacks.json could not be read (built-in pack stats)."); }
    PacksSet(World, R.Stats, R.Tiers);
    return bOk;
}

// ---------------------------------------------------------------- Movement
#define CIRE_PROFILE_MOVE_FIELDS(X) X(RunSpeed) X(WalkSpeed) X(JumpVelocity) X(RollSpeed) X(RollDuration) X(RollCooldown) X(RollEnergy) X(InvulnerableStart) \
    X(InvulnerableEnd) X(Acceleration) X(BrakingDeceleration) X(GroundFriction) X(RotationRate) X(AirControl) X(KeyboardTurnRate) X(BackpedalScale) X(TankBodyScale) X(BearBodyScale)
TSharedRef<FJsonObject> MovementCapture()
{
    const FCireMovementTuning& V = CireMovement::Tuning(); auto O = MakeShared<FJsonObject>();
#define CIRE_W(Name) O->SetNumberField(TEXT(#Name), V.Name);
    CIRE_PROFILE_MOVE_FIELDS(CIRE_W)
#undef CIRE_W
    return O;
}
void MovementToHeroes(UWorld* World) { for (ACireHero* H : WorldHeroes(World)) if (H->GetCharacterMovement()) CireMovement::ApplyToHero(*H); }
bool MovementApply(UWorld* World, const TSharedRef<FJsonObject>& O, FString* Error)
{
    FCireMovementTuning V = CireMovement::Tuning();
#define CIRE_R(Name) V.Name = static_cast<float>(ProfNum(O, TEXT(#Name), V.Name));
    CIRE_PROFILE_MOVE_FIELDS(CIRE_R)
#undef CIRE_R
    FString Why;
    if (!CireMovement::Apply(V, Why)) return ProfilesFail(Error, Why);
    MovementToHeroes(World);
    return true;
}
bool MovementDefault(UWorld* World, FString* Error)
{
    FString Why; const bool bOk = CireMovement::Reload(Why);
    if (!bOk && Error) *Error = Why;
    MovementToHeroes(World);
    return bOk;
}

// ---------------------------------------------------------------- Spacing (UnitSpacing.json format)
TSharedRef<FJsonObject> SpacingCapture()
{
    const FCireUnitSpacing& S = CireUnitSpacing::Get();
    auto Boss = MakeShared<FJsonObject>();
    Boss->SetNumberField(TEXT("outdoorBoss"), S.OutdoorBossSize); Boss->SetNumberField(TEXT("waveBoss"), S.WaveBossSize); Boss->SetNumberField(TEXT("packLeaderBoss"), S.PackLeaderBossSize);
    Boss->SetNumberField(TEXT("capsuleRadiusMax"), S.BossCapsuleRadiusMax); Boss->SetNumberField(TEXT("capsuleHalfHeightMax"), S.BossCapsuleHalfHeightMax);
    auto Units = MakeShared<FJsonObject>();
    Units->SetNumberField(TEXT("capsuleRadius"), S.MonsterCapsuleRadius); Units->SetNumberField(TEXT("meleeReachBonus"), S.MeleeReachBonus);
    Units->SetBoolField(TEXT("separation"), S.bSeparation); Units->SetNumberField(TEXT("separationPadding"), S.SeparationPadding); Units->SetNumberField(TEXT("separationStrength"), S.SeparationStrength);
    auto Root = MakeShared<FJsonObject>(); Root->SetObjectField(TEXT("boss"), Boss); Root->SetObjectField(TEXT("units"), Units);
    return Root;
}
bool SpacingApply(const TSharedRef<FJsonObject>& V, FString* Error)
{
    // Default values first, then the profile's keys (UnitSpacing.json semantics: absent keys keep the built-in value).
    FCireUnitSpacing S; FString Why;
    const TSharedRef<FJsonObject> Merged = SpacingCapture();
    for (const TCHAR* Section : {TEXT("boss"), TEXT("units")})
        if (const TSharedPtr<FJsonObject> From = ProfObj(V, Section)) if (const TSharedPtr<FJsonObject> Into = ProfObj(Merged, Section))
            for (const auto& Pair : From->Values) Into->SetField(Pair.Key, Pair.Value);
    if (!CireUnitSpacing::Parse(ProfJsonText(Merged), S, Why)) return ProfilesFail(Error, Why);
    CireUnitSpacing::Set(S);
    return true;
}
}

// ================================================================================================= domains
const TArray<FName>& CireProfiles::Domains()
{
    static const TArray<FName> List = {Match, Economy, Packs, Movement, Spacing};
    return List;
}
FString CireProfiles::DomainLabel(FName D)
{
    return D == Match ? TEXT("MATCH") : D == Economy ? TEXT("ECONOMY") : D == Packs ? TEXT("PACKS") : D == Movement ? TEXT("MOVEMENT") : D == Spacing ? TEXT("SPACING") : D.ToString().ToUpper();
}
FString CireProfiles::DefaultSource(FName D)
{
    return D == Match ? TEXT("no match overrides (F8 Match / Spawn / Effects off)") : D == Economy ? TEXT("LootTables.json + SkillShop.json + Vendors.json pricing") :
        D == Packs ? TEXT("JunglePacks.json") : D == Movement ? TEXT("MovementTuning.json") : D == Spacing ? TEXT("UnitSpacing.json") : TEXT("the data file");
}
bool CireProfiles::IsDefault(const FString& Name) { const FString T = Name.TrimStartAndEnd(); return T.IsEmpty() || T.Equals(DefaultName, ESearchCase::IgnoreCase); }

// ================================================================================================= store
FString CireProfiles::Root()
{
    return GProfileRootOverride.IsEmpty() ? FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Data/Profiles")) : GProfileRootOverride;
}
void CireProfiles::SetRootOverride(const FString& Dir) { GProfileRootOverride = Dir; }
FString CireProfiles::SanitizeName(const FString& Name)
{
    FString Out;
    for (const TCHAR C : Name.TrimStartAndEnd()) if (FChar::IsAlnum(C) || C == TEXT('_') || C == TEXT('-') || C == TEXT(' ')) Out.AppendChar(C);
    return Out.TrimStartAndEnd().Left(40);
}
FString CireProfiles::PathFor(FName Domain, const FString& Name)
{
    return Root() / Domain.ToString() / (SanitizeName(Name).Replace(TEXT(" "), TEXT("_")) + TEXT(".json"));
}
TArray<FString> CireProfiles::List(FName Domain)
{
    TArray<FString> Files, Names;
    IFileManager::Get().FindFiles(Files, *(Root() / Domain.ToString() / TEXT("*.json")), true, false);
    for (const FString& F : Files)
    {
        FString Name = FPaths::GetBaseFilename(F).Replace(TEXT("_"), TEXT(" "));
        if (const TSharedPtr<FJsonObject> Doc = ProfReadFile(Root() / Domain.ToString() / F)) { FString Stored; if (Doc->TryGetStringField(TEXT("name"), Stored) && !SanitizeName(Stored).IsEmpty()) Name = SanitizeName(Stored); }
        if (!IsDefault(Name)) Names.AddUnique(Name);
    }
    Names.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::IgnoreCase) < 0; });
    Names.Insert(DefaultName, 0);
    return Names;
}
bool CireProfiles::Exists(FName Domain, const FString& Name) { return IsDefault(Name) || FPaths::FileExists(PathFor(Domain, Name)); }
bool CireProfiles::SaveValues(FName Domain, const FString& Name, const TSharedRef<FJsonObject>& Values, FString* Error)
{
    const FString Clean = SanitizeName(Name);
    if (Clean.IsEmpty()) return ProfilesFail(Error, TEXT("Type a profile name (letters, digits, space, - or _)."));
    if (IsDefault(Clean)) return ProfilesFail(Error, FString::Printf(TEXT("\"Default\" is %s itself: use the page's SAVE to change it."), *DefaultSource(Domain)));
    auto Doc = MakeShared<FJsonObject>();
    Doc->SetNumberField(TEXT("schemaVersion"), 1);
    Doc->SetStringField(TEXT("_comment"), TEXT("game-profiles: a named F8 profile (Docs/RESUME-game-profiles.md). Load it in F8, or name it in a game type (WavePresets.json)."));
    Doc->SetStringField(TEXT("domain"), Domain.ToString());
    Doc->SetStringField(TEXT("name"), Clean);
    Doc->SetObjectField(TEXT("values"), Values);
    const FString Path = PathFor(Domain, Clean);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    if (!FFileHelper::SaveStringToFile(ProfJsonText(Doc) + TEXT("\n"), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        return ProfilesFail(Error, FString::Printf(TEXT("Could not write %s"), *Path));
    UE_LOG(LogCireProfiles, Display, TEXT("CIRE_PROFILE_SAVED domain=%s name=\"%s\" path=%s"), *Domain.ToString(), *Clean, *Path);
    if (Error) Error->Reset();
    return true;
}
bool CireProfiles::LoadValues(FName Domain, const FString& Name, TSharedPtr<FJsonObject>& Values, FString* Error)
{
    const FString Path = PathFor(Domain, Name);
    const TSharedPtr<FJsonObject> Doc = ProfReadFile(Path);
    if (!Doc) return ProfilesFail(Error, FString::Printf(TEXT("No %s profile \"%s\" (%s)."), *DomainLabel(Domain).ToLower(), *Name, *Path));
    Values = ProfObj(Doc, TEXT("values"));
    if (!Values) return ProfilesFail(Error, FString::Printf(TEXT("%s has no \"values\" object."), *FPaths::GetCleanFilename(Path)));
    if (Error) Error->Reset();
    return true;
}
bool CireProfiles::Rename(FName Domain, const FString& From, const FString& To, FString* Error)
{
    if (IsDefault(From)) return ProfilesFail(Error, TEXT("Default cannot be renamed."));
    const FString Clean = SanitizeName(To);
    if (Clean.IsEmpty() || IsDefault(Clean)) return ProfilesFail(Error, TEXT("Type a new name (not Default)."));
    TSharedPtr<FJsonObject> Values;
    if (!LoadValues(Domain, From, Values, Error)) return false;
    const bool bSameFile = PathFor(Domain, From).Equals(PathFor(Domain, Clean), ESearchCase::IgnoreCase);
    if (!bSameFile && FPaths::FileExists(PathFor(Domain, Clean))) return ProfilesFail(Error, FString::Printf(TEXT("A profile \"%s\" already exists."), *Clean));
    if (!bSameFile) IFileManager::Get().Delete(*PathFor(Domain, From), false, true, true);
    if (!SaveValues(Domain, Clean, Values.ToSharedRef(), Error)) return false;
    if (Active(Domain).Equals(SanitizeName(From), ESearchCase::IgnoreCase)) SetActive(Domain, Clean);
    return true;
}
bool CireProfiles::Delete(FName Domain, const FString& Name, FString* Error)
{
    if (IsDefault(Name)) return ProfilesFail(Error, TEXT("Default is the data file and cannot be deleted."));
    if (!FPaths::FileExists(PathFor(Domain, Name))) return ProfilesFail(Error, FString::Printf(TEXT("No profile \"%s\"."), *Name));
    if (!IFileManager::Get().Delete(*PathFor(Domain, Name), false, true, true)) return ProfilesFail(Error, TEXT("The profile file could not be deleted."));
    if (Active(Domain).Equals(SanitizeName(Name), ESearchCase::IgnoreCase)) SetActive(Domain, DefaultName);
    UE_LOG(LogCireProfiles, Display, TEXT("CIRE_PROFILE_DELETED domain=%s name=\"%s\""), *Domain.ToString(), *Name);
    return true;
}

// ================================================================================================= live
TSharedRef<FJsonObject> CireProfiles::MatchToJson(const FCireDeveloperSettings& S)
{
    auto O = MakeShared<FJsonObject>();
#define CIRE_W(Name) O->SetNumberField(TEXT(#Name), S.Name);
#define CIRE_WB(Name) O->SetBoolField(TEXT(#Name), S.Name);
    CIRE_PROFILE_MATCH_FLOATS(CIRE_W) CIRE_PROFILE_MATCH_INTS(CIRE_W) CIRE_PROFILE_MATCH_BOOLS(CIRE_WB)
#undef CIRE_W
#undef CIRE_WB
    return O;
}
bool CireProfiles::MatchFromJson(const TSharedRef<FJsonObject>& O, FCireDeveloperSettings& Out, FString* Error)
{
    FCireDeveloperSettings S;
#define CIRE_RF(Name) S.Name = static_cast<float>(ProfNum(O, TEXT(#Name), S.Name));
#define CIRE_RI(Name) S.Name = static_cast<int32>(ProfNum(O, TEXT(#Name), S.Name));
#define CIRE_RB(Name) S.Name = ProfBool(O, TEXT(#Name), S.Name);
    CIRE_PROFILE_MATCH_FLOATS(CIRE_RF) CIRE_PROFILE_MATCH_INTS(CIRE_RI) CIRE_PROFILE_MATCH_BOOLS(CIRE_RB)
#undef CIRE_RF
#undef CIRE_RI
#undef CIRE_RB
    S.bEnabled = true;
    if (!CireDeveloperTools::Validate(S, Error)) return false;
    Out = S;
    return true;
}
TSharedRef<FJsonObject> CireProfiles::Capture(FName D, UWorld* World)
{
    if (D == Match) { const FCireDeveloperSettings& S = CireDeveloperTools::Get(World); return MatchToJson(S.bEnabled ? S : FCireDeveloperSettings()); } // overrides off = the neutral values
    if (D == Economy) return EconomyCapture();
    if (D == Packs) return PacksCapture();
    if (D == Movement) return MovementCapture();
    if (D == Spacing) return SpacingCapture();
    return MakeShared<FJsonObject>();
}
bool CireProfiles::ApplyValues(FName D, UWorld* World, const TSharedRef<FJsonObject>& Values, FString* Error)
{
    if (D == Match)
    {
        ACireGameMode* Mode = AuthMode(World);
        if (!Mode || !CireDeveloperTools::CanEdit(World)) return ProfilesFail(Error, TEXT("Match profiles apply in a development standalone game only (F8 overrides)."));
        FCireDeveloperSettings S;
        return MatchFromJson(Values, S, Error) && CireDeveloperTools::Apply(Mode, S, Error);
    }
    if (D == Economy) return EconomyApply(Values);
    if (D == Packs) return PacksApply(World, Values);
    if (D == Movement) return MovementApply(World, Values, Error);
    if (D == Spacing) return SpacingApply(Values, Error);
    return ProfilesFail(Error, TEXT("Unknown profile domain."));
}
bool CireProfiles::ApplyDefault(FName D, UWorld* World, FString* Error)
{
    if (D == Match)
    {
        ACireGameMode* Mode = AuthMode(World);
        return !Mode || !CireDeveloperTools::CanEdit(World) || !CireDeveloperTools::Get(World).bEnabled || CireDeveloperTools::Restore(Mode, Error);
    }
    if (D == Economy) return EconomyDefault(Error);
    if (D == Packs) return PacksDefault(World, Error);
    if (D == Movement) return MovementDefault(World, Error);
    if (D == Spacing) { const bool bOk = CireUnitSpacing::Reload(Error); return bOk; }
    return ProfilesFail(Error, TEXT("Unknown profile domain."));
}
bool CireProfiles::SaveAs(FName Domain, UWorld* World, const FString& Name, FString* Error)
{
    if (!SaveValues(Domain, Name, Capture(Domain, World), Error)) return false;
    SetActive(Domain, SanitizeName(Name));
    return true;
}
bool CireProfiles::Load(FName Domain, UWorld* World, const FString& Name, FString* Error)
{
    FString Why;
    if (IsDefault(Name))
    {
        const bool bOk = ApplyDefault(Domain, World, &Why);
        SetActive(Domain, DefaultName);
        UE_LOG(LogCireProfiles, Display, TEXT("CIRE_PROFILE_LOADED domain=%s name=Default ok=%d %s"), *Domain.ToString(), bOk ? 1 : 0, *Why);
        if (Error) *Error = Why;
        return bOk;
    }
    TSharedPtr<FJsonObject> Values;
    if (!LoadValues(Domain, Name, Values, Error)) return false;
    // A profile is Default + its values, so a partial profile never inherits the previous profile's numbers.
    ApplyDefault(Domain, World, nullptr);
    if (!ApplyValues(Domain, World, Values.ToSharedRef(), &Why)) { if (Error) *Error = Why; UE_LOG(LogCireProfiles, Warning, TEXT("CIRE_PROFILE_LOAD_FAILED domain=%s name=\"%s\" %s"), *Domain.ToString(), *Name, *Why); return false; }
    SetActive(Domain, SanitizeName(Name));
    UE_LOG(LogCireProfiles, Display, TEXT("CIRE_PROFILE_LOADED domain=%s name=\"%s\" ok=1"), *Domain.ToString(), *SanitizeName(Name));
    if (Error) Error->Reset();
    return true;
}
FString CireProfiles::Active(FName Domain) { const FString* N = GActiveProfiles.Find(Domain); return N && !N->IsEmpty() ? *N : FString(DefaultName); }
void CireProfiles::SetActive(FName Domain, const FString& Name) { GActiveProfiles.Add(Domain, IsDefault(Name) ? FString(DefaultName) : SanitizeName(Name)); }

// ================================================================================================= game type bundle
const TArray<FString>& CireGameProfiles::Keys()
{
    static const TArray<FString> List = {KeyLayout, KeyTuning, KeyKit, KeyEconomy, KeyPacks, KeyMovement, KeyMatch, KeySpacing, KeyWorldEdit};
    return List;
}
FString CireGameProfiles::KeyLabel(const FString& Key)
{
    if (Key == KeyLayout) return TEXT("LAYOUT");
    if (Key == KeyTuning) return TEXT("ABILITY TUNING");
    if (Key == KeyKit) return TEXT("HERO KITS");
    if (Key == KeyWorldEdit) return TEXT("WORLD EDITS");
    const FName D = DomainOfKey(Key);
    return D.IsNone() ? Key.ToUpper() : CireProfiles::DomainLabel(D);
}
FName CireGameProfiles::DomainOfKey(const FString& Key)
{
    return Key == KeyEconomy ? CireProfiles::Economy : Key == KeyPacks ? CireProfiles::Packs : Key == KeyMovement ? CireProfiles::Movement :
        Key == KeyMatch ? CireProfiles::Match : Key == KeySpacing ? CireProfiles::Spacing : NAME_None;
}
FString CireGameProfiles::KeyOfDomain(FName D)
{
    return D == CireProfiles::Economy ? KeyEconomy : D == CireProfiles::Packs ? KeyPacks : D == CireProfiles::Movement ? KeyMovement :
        D == CireProfiles::Match ? KeyMatch : D == CireProfiles::Spacing ? KeySpacing : FString();
}
FString CireGameProfiles::Get(const FCireWavePreset& P, const FString& Key)
{
    const FString V = Key == KeyKit ? P.KitProfile : P.Bundle.FindRef(Key);
    return CireProfiles::IsDefault(V) ? FString() : V.TrimStartAndEnd();
}
void CireGameProfiles::Set(FCireWavePreset& P, const FString& Key, const FString& Name)
{
    const FString V = CireProfiles::IsDefault(Name) ? FString() : Name.TrimStartAndEnd().Left(48);
    if (Key == KeyKit) { P.KitProfile = V; return; }
    if (V.IsEmpty()) P.Bundle.Remove(Key); else P.Bundle.Add(Key, V);
}
TArray<FString> CireGameProfiles::Choices(const FString& Key, UWorld* World)
{
    TArray<FString> Out;
    const FName D = DomainOfKey(Key);
    if (!D.IsNone()) return CireProfiles::List(D);
    Out.Add(CireProfiles::DefaultName);
    if (Key == KeyLayout) for (const FString& N : CireMapLayout::ListNamed()) Out.AddUnique(N);
    else if (Key == KeyTuning) for (const auto& P : CireAbilityTuner::AllProfiles()) Out.AddUnique(P.Name);
    else if (Key == KeyKit) { for (const auto& P : CireKitEditor::Data().Profiles) if (!P.Name.Equals(CireKitEditor::StandardProfile, ESearchCase::IgnoreCase)) Out.AddUnique(P.Name); }
    else if (Key == KeyWorldEdit && GWorldEditList) for (const FString& N : GWorldEditList()) Out.AddUnique(N);
    return Out;
}
FString CireGameProfiles::Summary(const FCireWavePreset& P)
{
    TArray<FString> Parts;
    for (const FString& Key : Keys())
    {
        const FString V = Get(P, Key);
        if (V.IsEmpty() || (Key == KeyKit && V.Equals(CireKitEditor::StandardProfile, ESearchCase::IgnoreCase))) continue;
        FString Label = KeyLabel(Key).ToLower(); Label[0] = FChar::ToUpper(Label[0]);
        Parts.Add(FString::Printf(TEXT("%s: %s"), *Label, *V));
    }
    return Parts.IsEmpty() ? FString(TEXT("Every editor on Default")) : FString::Join(Parts, TEXT("  |  "));
}
void CireGameProfiles::CaptureActive(UWorld* World, FCireWavePreset& Into)
{
    for (const FName D : CireProfiles::Domains()) Set(Into, KeyOfDomain(D), CireProfiles::Active(D));
    Set(Into, KeyTuning, CireAbilityTuner::ActiveProfileName(World));
    const FString Kit = CireKitEditor::ActiveProfile(World);
    Set(Into, KeyKit, Kit.Equals(CireKitEditor::StandardProfile, ESearchCase::IgnoreCase) ? FString() : Kit);
    Set(Into, KeyLayout, GLayoutOverride);
    Set(Into, KeyWorldEdit, GActiveWorldEdit);
}

int32 CireGameProfiles::ApplyDataProfiles(UWorld* World, const FCireWavePreset* P, FString* Report)
{
    int32 Changed = 0; TArray<FString> Lines;
    const bool bStandalone = World && World->GetNetMode() == NM_Standalone;
    for (const FName D : CireProfiles::Domains())
    {
        FString Want = P ? Get(*P, KeyOfDomain(D)) : FString();
        if (!CireProfiles::IsDefault(Want) && !CireProfiles::Exists(D, Want))
        {
            UE_LOG(LogCireProfiles, Warning, TEXT("CIRE_GAME_TYPE_PROFILE_MISSING domain=%s name=\"%s\" (Default used)"), *D.ToString(), *Want);
            Lines.Add(FString::Printf(TEXT("%s: \"%s\" missing, Default"), *CireProfiles::DomainLabel(D), *Want));
            Want.Reset();
        }
        if (CireProfiles::IsDefault(Want)) Want = CireProfiles::DefaultName;
        if (CireProfiles::Active(D).Equals(CireProfiles::SanitizeName(Want), ESearchCase::IgnoreCase)) continue;
        if (D == CireProfiles::Match && !bStandalone)
        {
            // F8 match overrides exist only in a development standalone game (CireDeveloperTools::CanEdit).
            if (!CireProfiles::IsDefault(Want)) Lines.Add(FString::Printf(TEXT("MATCH: \"%s\" skipped (standalone only)"), *Want));
            continue;
        }
        FString Why;
        if (CireProfiles::Load(D, World, Want, &Why)) { ++Changed; Lines.Add(FString::Printf(TEXT("%s: %s"), *CireProfiles::DomainLabel(D), *Want)); }
        else Lines.Add(FString::Printf(TEXT("%s: \"%s\" failed (%s)"), *CireProfiles::DomainLabel(D), *Want, *Why));
    }
    if (Report) *Report = FString::Join(Lines, TEXT("; "));
    return Changed;
}

namespace
{
void ApplyLayoutChoice(ACireGameMode* Mode, const FString& In, bool bAtInit)
{
    FString Want = CireMapLayout::SanitizeName(In);
    if (!Want.IsEmpty() && !FPaths::FileExists(CireMapLayout::NamedPath(Want)))
    {
        UE_LOG(LogCireProfiles, Warning, TEXT("CIRE_GAME_TYPE_LAYOUT_MISSING \"%s\" (MapLayout.json used)"), *In);
        Want.Reset();
    }
    if (Want.Equals(GLayoutOverride, ESearchCase::IgnoreCase)) return;
    GLayoutOverride = Want;
    FString Out;
    bool bOk = true;
    if (bAtInit) bOk = CireLanePath::Reload(Mode->GetWorld(), &Out); // routes already loaded this BeginPlay: swap the paths live
    else bOk = CireLayoutRuntime::RestartOnLayout(Mode, Out);        // the host's pick (before the first wave): restart on the layout
    UE_LOG(LogCireProfiles, Display, TEXT("CIRE_GAME_TYPE_LAYOUT layout=%s ok=%d %s"), Want.IsEmpty() ? TEXT("MapLayout.json") : *Want, bOk ? 1 : 0, *Out);
}
void ApplyWorldEditChoice(UWorld* World, const FString& In)
{
    const FString Want = CireProfiles::IsDefault(In) ? FString() : In.TrimStartAndEnd();
    if (Want.Equals(GActiveWorldEdit, ESearchCase::IgnoreCase)) return;
    if (!GWorldEditApply)
    {
        // feat/world-editor is not merged yet: the key is kept in the preset and applied once that module registers.
        if (!Want.IsEmpty()) UE_LOG(LogCireProfiles, Warning, TEXT("CIRE_GAME_TYPE_WORLD_EDIT_PENDING set=\"%s\" (no world editor registered)"), *Want);
        return;
    }
    FString Why; const bool bOk = GWorldEditApply(World, Want, Why);
    if (bOk) GActiveWorldEdit = Want;
    UE_LOG(LogCireProfiles, Display, TEXT("CIRE_GAME_TYPE_WORLD_EDIT set=\"%s\" ok=%d %s"), *Want, bOk ? 1 : 0, *Why);
}
}

void CireGameProfiles::ApplyGameType(ACireGameMode* Mode, const FCireWavePreset* P, bool bAtInit)
{
    UWorld* World = Mode ? Mode->GetWorld() : nullptr;
    if (!World || !Mode->HasAuthority()) return;
    if (P) CireAbilityTuner::ApplyWavePreset(World, P->Id); // ability-tuner: tuningProfile / allowTuning
    ApplyLayoutChoice(Mode, P ? Get(*P, KeyLayout) : FString(), bAtInit);
    // F8 match overrides reset with every new match (CireDeveloperTools::Initialize): keep the active name honest.
    if (!CireDeveloperTools::Get(World).bEnabled) CireProfiles::SetActive(CireProfiles::Match, CireProfiles::DefaultName);
    // A layout restart (Alt+F5) re-runs match init with the same game type: profiles loaded by hand in F8 stay live then.
    FString BundleKey = P ? P->Id.ToString() : FString(TEXT("none"));
    if (P) for (const FString& Key : Keys()) BundleKey += TEXT("|") + Get(*P, Key);
    FString Report;
    int32 Changed = 0;
    if (bAtInit && BundleKey == GLastBundleKey) Report = TEXT("same game type as before: profiles unchanged");
    else Changed = ApplyDataProfiles(World, P, &Report);
    GLastBundleKey = BundleKey;
    ApplyWorldEditChoice(World, P ? Get(*P, KeyWorldEdit) : FString());
    // Hero kits: CireKitEditor reads the game type's kitProfile from ACireGameState::WavePreset when a champion is drafted.
    UE_LOG(LogCireProfiles, Display, TEXT("CIRE_GAME_TYPE_APPLIED id=%s init=%d layout=%s tuning=%s kit=%s economy=%s packs=%s movement=%s match=%s spacing=%s worldEdit=%s changed=%d %s"),
        P ? *P->Id.ToString() : TEXT("none"), bAtInit ? 1 : 0, GLayoutOverride.IsEmpty() ? TEXT("Default") : *GLayoutOverride,
        P && !Get(*P, KeyTuning).IsEmpty() ? *Get(*P, KeyTuning) : TEXT("Default"), P && !Get(*P, KeyKit).IsEmpty() ? *Get(*P, KeyKit) : TEXT("Default"),
        *CireProfiles::Active(CireProfiles::Economy), *CireProfiles::Active(CireProfiles::Packs), *CireProfiles::Active(CireProfiles::Movement),
        *CireProfiles::Active(CireProfiles::Match), *CireProfiles::Active(CireProfiles::Spacing), GActiveWorldEdit.IsEmpty() ? TEXT("Default") : *GActiveWorldEdit, Changed, *Report);
#if !UE_BUILD_SHIPPING
    if (P && P->Id == NetProbePresetId() && World->GetNetMode() == NM_DedicatedServer)
    {
        FString Why; const bool bOk = VerifyNetProbe(World, &Why);
        if (bOk) { UE_LOG(LogCireProfiles, Display, TEXT("CIRE_NET_SERVER_PROFILES_PASS %s"), *Why); }
        else { UE_LOG(LogCireProfiles, Error, TEXT("CIRE_NET_SERVER_FAIL reason=game type profiles not live on the server %s"), *Why); }
    }
#endif
}

void CireGameProfiles::OnWavePresetReplicated(ACireGameState* State)
{
    UWorld* World = State ? State->GetWorld() : nullptr;
    if (!World || State->HasAuthority() || World->GetNetMode() != NM_Client) return;
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("CireClientProbe"))) EnsureNetProbeFixture();
#endif
    const FCireWavePreset* P = State->WavePreset.IsNone() ? nullptr : CireWaveDirector::FindPreset(State->WavePreset);
    FString Report;
    const int32 Changed = ApplyDataProfiles(World, P, &Report);
    UE_LOG(LogCireProfiles, Display, TEXT("CIRE_GAME_TYPE_CLIENT id=%s found=%d changed=%d %s"), *State->WavePreset.ToString(), P ? 1 : 0, Changed, *Report);
}
void ACireGameState::OnRep_WavePreset() { CireGameProfiles::OnWavePresetReplicated(this); }

FString CireGameProfiles::LayoutOverride() { return GLayoutOverride; }
FString CireGameProfiles::RuntimeLayoutPath()
{
    if (!GLayoutOverride.IsEmpty())
    {
        const FString Named = CireMapLayout::NamedPath(GLayoutOverride);
        if (FPaths::FileExists(Named)) return Named;
    }
    return CireMapLayout::ActivePath();
}
void CireGameProfiles::RegisterWorldEdit(FWorldEditApply Apply, FWorldEditList List) { GWorldEditApply = MoveTemp(Apply); GWorldEditList = MoveTemp(List); }
FString CireGameProfiles::ActiveWorldEdit() { return GActiveWorldEdit; }

// ================================================================================================= network probe fixture
#if !UE_BUILD_SHIPPING
namespace
{
const TCHAR* NetProbeProfile = TEXT("NetProbe");
// Values that differ from every shipped data file (verified on server and client).
constexpr double ProbeBossMultiplier = 12.5, ProbePackHealth = 1.35, ProbeRollEnergy = 31, ProbeMeleeBonus = 23;
}
FName CireGameProfiles::NetProbePresetId() { return TEXT("netprobe_profiles"); }
void CireGameProfiles::EnsureNetProbeFixture()
{
    static bool bDone = false;
    if (bDone) return;
    bDone = true;
    CireProfiles::SetRootOverride(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("GameProfilesNetProbe")));
    auto Obj = [](const TCHAR* Section, const TCHAR* Key, double Value)
    {
        auto Inner = MakeShared<FJsonObject>(); Inner->SetNumberField(Key, Value);
        auto Outer = MakeShared<FJsonObject>(); Outer->SetObjectField(Section, Inner); return Outer;
    };
    auto Move = MakeShared<FJsonObject>(); Move->SetNumberField(TEXT("RollEnergy"), ProbeRollEnergy);
    auto MatchV = MakeShared<FJsonObject>(); MatchV->SetNumberField(TEXT("CooldownScale"), .9);
    CireProfiles::SaveValues(CireProfiles::Economy, NetProbeProfile, Obj(TEXT("killGold"), TEXT("bossMultiplier"), ProbeBossMultiplier));
    CireProfiles::SaveValues(CireProfiles::Packs, NetProbeProfile, Obj(TEXT("stats"), TEXT("globalHealth"), ProbePackHealth));
    CireProfiles::SaveValues(CireProfiles::Movement, NetProbeProfile, Move);
    CireProfiles::SaveValues(CireProfiles::Spacing, NetProbeProfile, Obj(TEXT("units"), TEXT("meleeReachBonus"), ProbeMeleeBonus));
    CireProfiles::SaveValues(CireProfiles::Match, NetProbeProfile, MatchV);
    FCireWavePreset P;
    if (const FCireWavePreset* Std = CireWaveDirector::FindPreset(TEXT("standard"))) P = *Std;
    P.Id = NetProbePresetId(); P.Label = TEXT("Net probe profiles"); P.Description = TEXT("game-profiles network probe: non-default profiles."); P.bBuiltIn = false;
    for (const FName D : CireProfiles::Domains()) Set(P, KeyOfDomain(D), NetProbeProfile);
    CireWaveDirector::RegisterRuntimePreset(P);
    UE_LOG(LogCireProfiles, Display, TEXT("CIRE_GAME_PROFILES_PROBE_FIXTURE root=%s preset=%s"), *CireProfiles::Root(), *P.Id.ToString());
}
void CireGameProfiles::InitializeProbePick(ACireGameMode* Mode, FName& Pick)
{
    if (!Mode || Mode->GetNetMode() != NM_DedicatedServer || !FParse::Param(FCommandLine::Get(), TEXT("CireNetServerProbe"))) return;
    EnsureNetProbeFixture();
    Pick = NetProbePresetId();
}
bool CireGameProfiles::VerifyNetProbe(UWorld* World, FString* Why)
{
    const ACireGameState* S = World ? World->GetGameState<ACireGameState>() : nullptr;
    const double Boss = CireLoot::MutableEconomy().BossMultiplier, Health = CireJunglePacks::Rules().Stats.GlobalHealth;
    const double Roll = CireMovement::Tuning().RollEnergy, Melee = CireUnitSpacing::Get().MeleeReachBonus;
    const bool bOk = S && S->WavePreset == NetProbePresetId() && FMath::IsNearlyEqual(Boss, ProbeBossMultiplier, .001) && FMath::IsNearlyEqual(Health, ProbePackHealth, .001) &&
        FMath::IsNearlyEqual(Roll, ProbeRollEnergy, .01) && FMath::IsNearlyEqual(Melee, ProbeMeleeBonus, .01);
    if (Why) *Why = FString::Printf(TEXT("preset=%s economy=%s(boss x%.2f) packs=%s(health x%.2f) movement=%s(roll energy %.0f) spacing=%s(melee bonus %.0f) match=%s"),
        S ? *S->WavePreset.ToString() : TEXT("-"), *CireProfiles::Active(CireProfiles::Economy), Boss, *CireProfiles::Active(CireProfiles::Packs), Health,
        *CireProfiles::Active(CireProfiles::Movement), Roll, *CireProfiles::Active(CireProfiles::Spacing), Melee, *CireProfiles::Active(CireProfiles::Match));
    return bOk;
}
#endif
