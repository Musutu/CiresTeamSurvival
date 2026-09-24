// wave-director: Waves.json data model, templates, validation and (de)serialization.
#include "CireWaves.h"
#include "CireNPCArchetypes.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/PrettyJsonPrintPolicy.h"

namespace
{
const TCHAR* TypeIds[] = {TEXT("normal"), TEXT("armored"), TEXT("armored_escort"), TEXT("boss"), TEXT("caster_pack"),
    TEXT("melee_pack"), TEXT("ranged_pack"), TEXT("hybrid_pack"), TEXT("custom")};
const TCHAR* TypeLabels[] = {TEXT("Normal"), TEXT("Armored"), TEXT("Armored Escort"), TEXT("Boss"), TEXT("Caster Pack"),
    TEXT("Melee Pack"), TEXT("Ranged Pack"), TEXT("Hybrid Pack"), TEXT("Custom")};
static_assert(UE_ARRAY_COUNT(TypeIds) == static_cast<int32>(ECireWaveType::Count), "wave type table");

FCireWaveUnit Unit(const TCHAR* Id, int32 Count, float Health = 1.f, float Damage = 1.f)
{
    FCireWaveUnit U; U.Archetype = Id; U.Count = Count; U.HealthScale = Health; U.DamageScale = Damage; return U;
}
bool Near(float A, float B) { return FMath::IsNearlyEqual(A, B, 1.e-4f); }
float ClampF(float V, float Lo, float Hi, float Fallback) { return FMath::IsFinite(V) ? FMath::Clamp(V, Lo, Hi) : Fallback; }
}

bool FCireWaveUnit::operator==(const FCireWaveUnit& O) const
{
    return Archetype == O.Archetype && Count == O.Count && Near(HealthScale, O.HealthScale) && Near(DamageScale, O.DamageScale) &&
        Near(SizeScale, O.SizeScale) && bElite == O.bElite && bNonAttacking == O.bNonAttacking && bEscortee == O.bEscortee &&
        bBoss == O.bBoss && LeakCost == O.LeakCost;
}
int32 FCireWaveDef::UnitsPerLane() const { int32 N = 0; for (const auto& U : Units) N += U.Count; return N; }
bool FCireWaveDef::operator==(const FCireWaveDef& O) const
{
    return Label == O.Label && Type == O.Type && Units == O.Units && Near(SpawnInterval, O.SpawnInterval) && Near(DelayBefore, O.DelayBefore) &&
        bMustClear == O.bMustClear && Near(RewardMultiplier, O.RewardMultiplier);
}
bool FCireWaveConfig::operator==(const FCireWaveConfig& O) const
{
    return Near(BreatherSeconds, O.BreatherSeconds) && WavesPerCycle == O.WavesPerCycle && Cycles == O.Cycles &&
        Near(CycleHealthGrowth, O.CycleHealthGrowth) && Near(CycleDamageGrowth, O.CycleDamageGrowth) && CycleExtraUnits == O.CycleExtraUnits &&
        bStallFailsafe == O.bStallFailsafe && Near(MaxWaveSeconds, O.MaxWaveSeconds) && FailsafeAction == O.FailsafeAction &&
        Near(FailsafeGraceSeconds, O.FailsafeGraceSeconds) && Near(StuckSeconds, O.StuckSeconds) && Waves == O.Waves;
}

const TCHAR* CireWaveDirector::TypeName(ECireWaveType Type)
{
    const int32 I = static_cast<int32>(Type);
    return I >= 0 && I < UE_ARRAY_COUNT(TypeIds) ? TypeIds[I] : TEXT("custom");
}
FString CireWaveDirector::TypeLabel(ECireWaveType Type)
{
    const int32 I = static_cast<int32>(Type);
    return I >= 0 && I < UE_ARRAY_COUNT(TypeLabels) ? TypeLabels[I] : TEXT("Custom");
}
bool CireWaveDirector::ParseType(const FString& Name, ECireWaveType& Out)
{
    for (int32 I = 0; I < UE_ARRAY_COUNT(TypeIds); ++I)
        if (Name.Equals(TypeIds[I], ESearchCase::IgnoreCase)) { Out = static_cast<ECireWaveType>(I); return true; }
    return false;
}

FCireWaveDef CireWaveDirector::Template(ECireWaveType Type)
{
    FCireWaveDef W; W.Type = Type; W.Label = TypeLabel(Type);
    switch (Type)
    {
    case ECireWaveType::Normal:
        W.Label = TEXT("Breach Vanguard");
        W.Units = {Unit(TEXT("hollow_infantry"), 3, 1.1f, 1.25f), Unit(TEXT("ironbound_bruiser"), 2, 1.1f, 1.25f),
                   Unit(TEXT("barbed_hunter"), 1, 1.1f, 1.25f), Unit(TEXT("blight_caster"), 1, 1.1f, 1.25f)};
        break;
    case ECireWaveType::Armored:
    {
        W.Label = TEXT("Iron Procession"); W.SpawnInterval = 1.2f;
        FCireWaveUnit A = Unit(TEXT("hollow_shieldbearer"), 4, 1.3f, 1.f); A.bNonAttacking = true; A.SizeScale = 1.2f; A.LeakCost = 2;
        W.Units = {A};
        break;
    }
    case ECireWaveType::ArmoredEscort:
    {
        W.Label = TEXT("Armored Escort"); W.SpawnInterval = .5f;
        FCireWaveUnit Tank = Unit(TEXT("hollow_shieldbearer"), 1, 5.f, 1.f);
        Tank.bNonAttacking = true; Tank.bEscortee = true; Tank.SizeScale = 1.45f; Tank.LeakCost = 5;
        W.Units = {Tank, Unit(TEXT("hollow_infantry"), 2, 1.1f, 1.2f), Unit(TEXT("ironbound_bruiser"), 1, 1.1f, 1.2f),
                   Unit(TEXT("barbed_hunter"), 1, 1.1f, 1.2f)};
        W.RewardMultiplier = 1.25f;
        break;
    }
    case ECireWaveType::Boss:
    {
        W.Label = TEXT("Siege Host"); W.SpawnInterval = .7f;
        FCireWaveUnit Boss = Unit(TEXT("hollow_siegebreaker"), 1); Boss.bBoss = true;
        W.Units = {Unit(TEXT("hollow_infantry"), 2, 1.25f, 1.2f), Unit(TEXT("ironbound_bruiser"), 2, 1.25f, 1.2f),
                   Unit(TEXT("blight_caster"), 1, 1.25f, 1.2f), Boss};
        W.RewardMultiplier = 1.5f;
        break;
    }
    case ECireWaveType::CasterPack:
        W.Units = {Unit(TEXT("hollow_shieldbearer"), 1), Unit(TEXT("blight_caster"), 4)};
        break;
    case ECireWaveType::MeleePack:
        W.Units = {Unit(TEXT("hollow_shieldbearer"), 1), Unit(TEXT("hollow_infantry"), 2), Unit(TEXT("ironbound_bruiser"), 2)};
        break;
    case ECireWaveType::RangedPack:
        W.Units = {Unit(TEXT("hollow_shieldbearer"), 1), Unit(TEXT("barbed_hunter"), 4)};
        break;
    case ECireWaveType::HybridPack:
        W.Units = {Unit(TEXT("hollow_shieldbearer"), 1), Unit(TEXT("hollow_infantry"), 1), Unit(TEXT("ironbound_bruiser"), 1),
                   Unit(TEXT("blight_caster"), 1), Unit(TEXT("barbed_hunter"), 1)};
        break;
    default:
        W.Type = ECireWaveType::Custom;
        W.Units = {Unit(TEXT("hollow_infantry"), 4)};
        break;
    }
    return W;
}

FCireWaveConfig CireWaveDirector::Defaults()
{
    FCireWaveConfig C;
    FCireWaveDef One = Template(ECireWaveType::Normal);
    FCireWaveDef Two = Template(ECireWaveType::Normal);
    Two.Label = TEXT("Hollow Column");
    Two.Units = {Unit(TEXT("hollow_infantry"), 3, 1.15f, 1.3f), Unit(TEXT("ironbound_bruiser"), 2, 1.15f, 1.3f),
                 Unit(TEXT("barbed_hunter"), 2, 1.15f, 1.3f), Unit(TEXT("blight_caster"), 1, 1.15f, 1.3f)};
    C.Waves = {One, Two, Template(ECireWaveType::Armored), Template(ECireWaveType::ArmoredEscort), Template(ECireWaveType::Boss)};
    C.WavesPerCycle = C.Waves.Num();
    return C;
}

bool CireWaveDirector::Validate(FCireWaveConfig& C, FString* Error, bool bClamp)
{
    auto Fail = [&](const FString& Why) { if (Error) *Error = Why; return false; };
    FCireWaveConfig Before = C;
    C.BreatherSeconds = ClampF(C.BreatherSeconds, 0, 120, 8);
    C.WavesPerCycle = FMath::Clamp(C.WavesPerCycle, 1, 10);
    C.Cycles = FMath::Clamp(C.Cycles, 0, 50);
    C.CycleHealthGrowth = ClampF(C.CycleHealthGrowth, 0, 2, .15f);
    C.CycleDamageGrowth = ClampF(C.CycleDamageGrowth, 0, 2, .1f);
    C.CycleExtraUnits = FMath::Clamp(C.CycleExtraUnits, 0, 5);
    C.MaxWaveSeconds = ClampF(C.MaxWaveSeconds, 30, 900, 210);
    C.FailsafeGraceSeconds = ClampF(C.FailsafeGraceSeconds, 5, 300, 45);
    C.StuckSeconds = ClampF(C.StuckSeconds, 1, 30, 5);
    if (C.Waves.IsEmpty()) return Fail(TEXT("At least one wave is required."));
    if (C.Waves.Num() > 20) return Fail(TEXT("At most 20 waves are allowed."));
    for (int32 WI = 0; WI < C.Waves.Num(); ++WI)
    {
        auto& W = C.Waves[WI];
        W.Label = W.Label.Left(40).TrimStartAndEnd();
        if (W.Label.IsEmpty()) W.Label = TypeLabel(W.Type);
        if (static_cast<int32>(W.Type) >= static_cast<int32>(ECireWaveType::Count)) W.Type = ECireWaveType::Custom;
        W.SpawnInterval = ClampF(W.SpawnInterval, 0, 5, .6f);
        W.DelayBefore = ClampF(W.DelayBefore, 0, 120, 0);
        W.RewardMultiplier = ClampF(W.RewardMultiplier, 0, 10, 1);
        if (W.Units.IsEmpty()) return Fail(FString::Printf(TEXT("Wave %d has no composition rows."), WI + 1));
        if (W.Units.Num() > 8) return Fail(FString::Printf(TEXT("Wave %d has more than 8 composition rows."), WI + 1));
        int32 Total = 0, Bosses = 0, Attackers = 0;
        for (auto& U : W.Units)
        {
            if (U.Archetype.IsNone() || !CireNPCArchetypes::Find(U.Archetype))
                return Fail(FString::Printf(TEXT("Wave %d: unknown archetype '%s'."), WI + 1, *U.Archetype.ToString()));
            U.Count = FMath::Clamp(U.Count, 1, 20);
            U.HealthScale = ClampF(U.HealthScale, .1f, 20, 1);
            U.DamageScale = ClampF(U.DamageScale, .05f, 10, 1);
            U.SizeScale = ClampF(U.SizeScale, .5f, 3, 1);
            U.LeakCost = FMath::Clamp(U.LeakCost, 0, 100);
            if (U.bEscortee) U.bNonAttacking = true;
            if (U.bBoss) { U.bNonAttacking = false; U.bEscortee = false; Bosses += U.Count; }
            if (!U.bNonAttacking) Attackers += U.Count;
            Total += U.Count;
        }
        if (Total > 30) return Fail(FString::Printf(TEXT("Wave %d spawns %d units per lane; the limit is 30."), WI + 1, Total));
        if (Bosses > 3) return Fail(FString::Printf(TEXT("Wave %d has %d lane bosses; the limit is 3."), WI + 1, Bosses));
        (void)Attackers;
    }
    if (!bClamp && !(Before == C)) return Fail(TEXT("Values were outside their limits."));
    if (Error) Error->Reset();
    return true;
}

// ---------------------------------------------------------------- JSON
namespace
{
double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double Default)
{
    double V = Default; if (O) O->TryGetNumberField(Key, V); return V;
}
bool Flag(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, bool Default)
{
    bool V = Default; if (O) O->TryGetBoolField(Key, V); return V;
}
}

bool CireWaveDirector::ParseJson(const FString& Json, FCireWaveConfig& Out, FString& Error)
{
    if (Json.Len() > 256 * 1024) { Error = TEXT("Waves.json exceeds 256 KB."); return false; }
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) { Error = TEXT("Waves.json is not valid JSON."); return false; }
    if (static_cast<int32>(Num(Root, TEXT("schemaVersion"), 0)) != 1) { Error = TEXT("Waves.json must declare schemaVersion 1."); return false; }
    FCireWaveConfig C;
    C.BreatherSeconds = static_cast<float>(Num(Root, TEXT("breatherSeconds"), C.BreatherSeconds));
    C.WavesPerCycle = static_cast<int32>(Num(Root, TEXT("wavesPerCycle"), C.WavesPerCycle));
    C.Cycles = static_cast<int32>(Num(Root, TEXT("cycles"), C.Cycles));
    const TSharedPtr<FJsonObject>* Scaling = nullptr;
    if (Root->TryGetObjectField(TEXT("cycleScaling"), Scaling) && Scaling)
    {
        C.CycleHealthGrowth = static_cast<float>(Num(*Scaling, TEXT("healthGrowth"), C.CycleHealthGrowth));
        C.CycleDamageGrowth = static_cast<float>(Num(*Scaling, TEXT("damageGrowth"), C.CycleDamageGrowth));
        C.CycleExtraUnits = static_cast<int32>(Num(*Scaling, TEXT("extraUnits"), C.CycleExtraUnits));
    }
    const TSharedPtr<FJsonObject>* Failsafe = nullptr;
    if (Root->TryGetObjectField(TEXT("failsafe"), Failsafe) && Failsafe)
    {
        C.bStallFailsafe = Flag(*Failsafe, TEXT("enabled"), C.bStallFailsafe);
        C.MaxWaveSeconds = static_cast<float>(Num(*Failsafe, TEXT("maxWaveSeconds"), C.MaxWaveSeconds));
        C.FailsafeGraceSeconds = static_cast<float>(Num(*Failsafe, TEXT("graceSeconds"), C.FailsafeGraceSeconds));
        C.StuckSeconds = static_cast<float>(Num(*Failsafe, TEXT("stuckSeconds"), C.StuckSeconds));
        FString Action;
        if ((*Failsafe)->TryGetStringField(TEXT("action"), Action))
        {
            if (Action == TEXT("march")) C.FailsafeAction = ECireWaveFailsafe::March;
            else if (Action == TEXT("despawn")) C.FailsafeAction = ECireWaveFailsafe::Despawn;
            else { Error = TEXT("failsafe.action must be \"march\" or \"despawn\"."); return false; }
        }
    }
    const TArray<TSharedPtr<FJsonValue>>* Waves = nullptr;
    if (!Root->TryGetArrayField(TEXT("waves"), Waves) || !Waves) { Error = TEXT("Waves.json needs a \"waves\" array."); return false; }
    for (const auto& Value : *Waves)
    {
        const TSharedPtr<FJsonObject>* WO = nullptr;
        if (!Value || !Value->TryGetObject(WO) || !WO) { Error = TEXT("Every wave must be an object."); return false; }
        FCireWaveDef W;
        FString TypeText = TEXT("custom");
        (*WO)->TryGetStringField(TEXT("type"), TypeText);
        if (!ParseType(TypeText, W.Type)) { Error = FString::Printf(TEXT("Unknown wave type '%s'."), *TypeText); return false; }
        (*WO)->TryGetStringField(TEXT("label"), W.Label);
        W.SpawnInterval = static_cast<float>(Num(*WO, TEXT("spawnInterval"), W.SpawnInterval));
        W.DelayBefore = static_cast<float>(Num(*WO, TEXT("delayBefore"), W.DelayBefore));
        W.bMustClear = Flag(*WO, TEXT("mustClear"), W.bMustClear);
        W.RewardMultiplier = static_cast<float>(Num(*WO, TEXT("rewardMultiplier"), W.RewardMultiplier));
        const TArray<TSharedPtr<FJsonValue>>* Units = nullptr;
        if (!(*WO)->TryGetArrayField(TEXT("units"), Units) || !Units) { Error = FString::Printf(TEXT("Wave '%s' needs a \"units\" array."), *W.Label); return false; }
        for (const auto& UV : *Units)
        {
            const TSharedPtr<FJsonObject>* UO = nullptr;
            if (!UV || !UV->TryGetObject(UO) || !UO) { Error = TEXT("Every composition row must be an object."); return false; }
            FCireWaveUnit U; FString Id;
            if (!(*UO)->TryGetStringField(TEXT("archetype"), Id)) { Error = TEXT("Composition rows need an \"archetype\"."); return false; }
            U.Archetype = FName(*Id);
            U.Count = static_cast<int32>(Num(*UO, TEXT("count"), U.Count));
            U.HealthScale = static_cast<float>(Num(*UO, TEXT("health"), U.HealthScale));
            U.DamageScale = static_cast<float>(Num(*UO, TEXT("damage"), U.DamageScale));
            U.SizeScale = static_cast<float>(Num(*UO, TEXT("size"), U.SizeScale));
            U.bElite = Flag(*UO, TEXT("elite"), false);
            U.bNonAttacking = Flag(*UO, TEXT("nonAttacking"), false);
            U.bEscortee = Flag(*UO, TEXT("escortee"), false);
            U.bBoss = Flag(*UO, TEXT("boss"), false);
            U.LeakCost = static_cast<int32>(Num(*UO, TEXT("leakCost"), 0));
            W.Units.Add(U);
        }
        C.Waves.Add(MoveTemp(W));
    }
    if (!Validate(C, &Error, true)) return false;
    Out = MoveTemp(C);
    Error.Reset();
    return true;
}

FString CireWaveDirector::ToJson(const FCireWaveConfig& C)
{
    auto Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schemaVersion"), 1);
    Root->SetStringField(TEXT("_comment"), TEXT("Wave composer data (Docs/Waves.md). Edited live with F8 > Waves; counts are per lane."));
    Root->SetNumberField(TEXT("breatherSeconds"), C.BreatherSeconds);
    Root->SetNumberField(TEXT("wavesPerCycle"), C.WavesPerCycle);
    Root->SetNumberField(TEXT("cycles"), C.Cycles);
    auto Scaling = MakeShared<FJsonObject>();
    Scaling->SetNumberField(TEXT("healthGrowth"), C.CycleHealthGrowth);
    Scaling->SetNumberField(TEXT("damageGrowth"), C.CycleDamageGrowth);
    Scaling->SetNumberField(TEXT("extraUnits"), C.CycleExtraUnits);
    Root->SetObjectField(TEXT("cycleScaling"), Scaling);
    auto Failsafe = MakeShared<FJsonObject>();
    Failsafe->SetBoolField(TEXT("enabled"), C.bStallFailsafe);
    Failsafe->SetNumberField(TEXT("maxWaveSeconds"), C.MaxWaveSeconds);
    Failsafe->SetStringField(TEXT("action"), C.FailsafeAction == ECireWaveFailsafe::Despawn ? TEXT("despawn") : TEXT("march"));
    Failsafe->SetNumberField(TEXT("graceSeconds"), C.FailsafeGraceSeconds);
    Failsafe->SetNumberField(TEXT("stuckSeconds"), C.StuckSeconds);
    Root->SetObjectField(TEXT("failsafe"), Failsafe);
    TArray<TSharedPtr<FJsonValue>> Waves;
    for (const auto& W : C.Waves)
    {
        auto WO = MakeShared<FJsonObject>();
        WO->SetStringField(TEXT("label"), W.Label);
        WO->SetStringField(TEXT("type"), TypeName(W.Type));
        WO->SetNumberField(TEXT("spawnInterval"), W.SpawnInterval);
        WO->SetNumberField(TEXT("delayBefore"), W.DelayBefore);
        WO->SetBoolField(TEXT("mustClear"), W.bMustClear);
        WO->SetNumberField(TEXT("rewardMultiplier"), W.RewardMultiplier);
        TArray<TSharedPtr<FJsonValue>> Units;
        for (const auto& U : W.Units)
        {
            auto UO = MakeShared<FJsonObject>();
            UO->SetStringField(TEXT("archetype"), U.Archetype.ToString());
            UO->SetNumberField(TEXT("count"), U.Count);
            UO->SetNumberField(TEXT("health"), U.HealthScale);
            UO->SetNumberField(TEXT("damage"), U.DamageScale);
            UO->SetNumberField(TEXT("size"), U.SizeScale);
            if (U.bElite) UO->SetBoolField(TEXT("elite"), true);
            if (U.bNonAttacking) UO->SetBoolField(TEXT("nonAttacking"), true);
            if (U.bEscortee) UO->SetBoolField(TEXT("escortee"), true);
            if (U.bBoss) UO->SetBoolField(TEXT("boss"), true);
            if (U.LeakCost > 0) UO->SetNumberField(TEXT("leakCost"), U.LeakCost);
            Units.Add(MakeShared<FJsonValueObject>(UO));
        }
        WO->SetArrayField(TEXT("units"), Units);
        Waves.Add(MakeShared<FJsonValueObject>(WO));
    }
    Root->SetArrayField(TEXT("waves"), Waves);
    FString Out;
    auto Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
    FJsonSerializer::Serialize(Root, Writer);
    return Out + TEXT("\n");
}

FString CireWaveDirector::DataPath() { return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Data/Waves.json")); }

bool CireWaveDirector::LoadFile(FCireWaveConfig& Out, FString* Error, const FString& Path)
{
    FString Json, Why;
    const FString File = Path.IsEmpty() ? DataPath() : Path;
    if (!FFileHelper::LoadFileToString(Json, *File)) { if (Error) *Error = FString::Printf(TEXT("%s could not be read."), *FPaths::GetCleanFilename(File)); return false; }
    if (!ParseJson(Json, Out, Why)) { if (Error) *Error = Why; return false; }
    if (Error) Error->Reset();
    return true;
}

bool CireWaveDirector::SaveFile(const FCireWaveConfig& Config, FString* Error, const FString& Path)
{
    FCireWaveConfig Copy = Config;
    if (!Validate(Copy, Error, true)) return false;
    const FString File = Path.IsEmpty() ? DataPath() : Path;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    if (!FFileHelper::SaveStringToFile(ToJson(Copy), *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    { if (Error) *Error = TEXT("Waves.json could not be written."); return false; }
    if (Error) Error->Reset();
    return true;
}
