// game-profiles: native checks for the named-profile store and the game type bundle. Run by the combat expansion probe.
#include "CireProfiles.h"

#if !UE_BUILD_SHIPPING
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
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireProfilesTests, Log, All);

namespace
{
struct FProfileChecks
{
    int32 Count = 0; bool bPassed = true;
    void Check(bool bCondition, const FString& Message)
    {
        ++Count;
        if (!bCondition) { bPassed = false; UE_LOG(LogCireProfilesTests, Error, TEXT("CIRE_PROFILES_CHECK_FAIL %s"), *Message); }
    }
};
FString ProfTestText(const TSharedRef<FJsonObject>& O)
{
    FString Text; FJsonSerializer::Serialize(O, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text)); return Text;
}
TSharedRef<FJsonObject> ProfTestSection(const TCHAR* Section, const TCHAR* Key, double Value)
{
    auto Inner = MakeShared<FJsonObject>(); Inner->SetNumberField(Key, Value);
    auto Outer = MakeShared<FJsonObject>(); Outer->SetObjectField(Section, Inner); return Outer;
}
}

bool CireProfiles::RunTests(ACireGameMode* Mode)
{
    FProfileChecks T;
    UWorld* World = IsValid(Mode) ? Mode->GetWorld() : nullptr;
    if (!World) return false;
    const FString TestRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("GameProfilesTests") / FGuid::NewGuid().ToString(EGuidFormats::Digits));
    const FString PreviousRoot = Root() == FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Data/Profiles")) ? FString() : Root();
    // Start from Default everywhere (the live data files), and end there too.
    const bool bStandalone = CireDeveloperTools::CanEdit(World);
    for (const FName D : Domains()) { ApplyDefault(D, World); SetActive(D, DefaultName); }
    SetRootOverride(TestRoot);
    ON_SCOPE_EXIT
    {
        for (const FName D : Domains()) { ApplyDefault(D, World); SetActive(D, DefaultName); }
        SetRootOverride(PreviousRoot);
        IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
    };
    TMap<FName, FString> Defaults;
    for (const FName D : Domains()) Defaults.Add(D, ProfTestText(Capture(D, World)));

    // ---------------- the store: save as / list / load / rename / delete ----------------
    {
        FString Error;
        T.Check(SanitizeName(TEXT("  Rich/Start*!  ")) == TEXT("RichStart") && SanitizeName(TEXT("Hard - mode_2")) == TEXT("Hard - mode_2"), TEXT("names are sanitized"));
        T.Check(List(Economy).Num() == 1 && List(Economy)[0] == DefaultName, TEXT("an empty store lists only Default"));
        T.Check(!SaveValues(Economy, TEXT("Default"), MakeShared<FJsonObject>(), &Error) && !Error.IsEmpty(), TEXT("Default cannot be saved over (it is the data file)"));
        T.Check(!SaveValues(Economy, TEXT("***"), MakeShared<FJsonObject>(), &Error), TEXT("an empty name is refused"));
        T.Check(SaveValues(Economy, TEXT("Rich Start"), ProfTestSection(TEXT("killGold"), TEXT("bossMultiplier"), 17), &Error), TEXT("save a profile: ") + Error);
        T.Check(FPaths::FileExists(TestRoot / TEXT("Economy/Rich_Start.json")), TEXT("stored as Profiles/<Domain>/<name>.json"));
        const TArray<FString> Names = List(Economy);
        T.Check(Names.Num() == 2 && Names[0] == DefaultName && Names[1] == TEXT("Rich Start"), TEXT("list: Default first, then the display names"));
        TSharedPtr<FJsonObject> Values;
        T.Check(LoadValues(Economy, TEXT("rich start"), Values, &Error) && Values && ProfTestText(Values.ToSharedRef()).Contains(TEXT("17")), TEXT("load values back (name case-insensitive)"));
        T.Check(Rename(Economy, TEXT("Rich Start"), TEXT("Richer"), &Error) && Exists(Economy, TEXT("Richer")) && !Exists(Economy, TEXT("Rich Start")), TEXT("rename: ") + Error);
        T.Check(!Rename(Economy, TEXT("Default"), TEXT("X"), &Error) && !Delete(Economy, TEXT("Default"), &Error), TEXT("Default cannot be renamed or deleted"));
        T.Check(Delete(Economy, TEXT("Richer"), &Error) && List(Economy).Num() == 1, TEXT("delete: ") + Error);
        T.Check(!Delete(Economy, TEXT("Richer"), &Error), TEXT("deleting a missing profile fails"));
    }

    // ---------------- every domain: capture -> save as -> Default -> load gives the same live values ----------------
    for (const FName D : Domains())
    {
        FString Error;
        // A changed live state to capture (so the round trip is not Default == Default).
        if (D == Match && !bStandalone) continue; // F8 match overrides exist in a development standalone game only
        TSharedRef<FJsonObject> Change = MakeShared<FJsonObject>();
        if (D == Match) { Change = MakeShared<FJsonObject>(); Change->SetNumberField(TEXT("CooldownScale"), .7); Change->SetNumberField(TEXT("MonsterHealthScale"), 1.6); }
        if (D == Economy) Change = ProfTestSection(TEXT("skillShop"), TEXT("activePrice"), 21);
        if (D == Packs) Change = ProfTestSection(TEXT("stats"), TEXT("globalDamage"), 1.4);
        if (D == Movement) Change->SetNumberField(TEXT("RollCooldown"), 4.5);
        if (D == Spacing) Change = ProfTestSection(TEXT("boss"), TEXT("waveBoss"), 1.6);
        T.Check(ApplyValues(D, World, Change, &Error), FString::Printf(TEXT("%s: apply values live: %s"), *D.ToString(), *Error));
        const FString Live = ProfTestText(Capture(D, World));
        T.Check(Live != Defaults[D], FString::Printf(TEXT("%s: the live values changed"), *D.ToString()));
        T.Check(SaveAs(D, World, TEXT("Round Trip"), &Error) && Active(D) == TEXT("Round Trip"), FString::Printf(TEXT("%s: save as makes it active: %s"), *D.ToString(), *Error));
        T.Check(Load(D, World, DefaultName, &Error) && Active(D) == DefaultName, FString::Printf(TEXT("%s: load Default: %s"), *D.ToString(), *Error));
        T.Check(ProfTestText(Capture(D, World)) == Defaults[D], FString::Printf(TEXT("%s: Default is the data file again"), *D.ToString()));
        T.Check(Load(D, World, TEXT("Round Trip"), &Error) && ProfTestText(Capture(D, World)) == Live, FString::Printf(TEXT("%s: the profile round trips: %s"), *D.ToString(), *Error));
        T.Check(Load(D, World, DefaultName, &Error), FString::Printf(TEXT("%s: back to Default"), *D.ToString()));
    }
    {
        FCireDeveloperSettings S; FString Error;
        S.CooldownScale = .5f; S.PrepSeconds = 42;
        FCireDeveloperSettings Back;
        T.Check(MatchFromJson(MatchToJson(S), Back, &Error) && FMath::IsNearlyEqual(Back.CooldownScale, .5f) && FMath::IsNearlyEqual(Back.PrepSeconds, 42.f) && Back.bEnabled, TEXT("match settings JSON round trip (loading turns the overrides on)"));
        auto Bad = MakeShared<FJsonObject>(); Bad->SetNumberField(TEXT("CooldownScale"), 99);
        T.Check(!MatchFromJson(Bad, Back, &Error), TEXT("out-of-bounds match values are refused"));
    }

    // ---------------- a game type applies each domain's profile, and none = Default ----------------
    {
        FString Error;
        auto MatchV = MakeShared<FJsonObject>(); MatchV->SetNumberField(TEXT("CooldownScale"), .8);
        auto MoveV = MakeShared<FJsonObject>(); MoveV->SetNumberField(TEXT("RollEnergy"), 33);
        T.Check(SaveValues(Economy, TEXT("GT Econ"), ProfTestSection(TEXT("killGold"), TEXT("bossMultiplier"), 17)) && SaveValues(Packs, TEXT("GT Packs"), ProfTestSection(TEXT("stats"), TEXT("globalHealth"), 1.7)) &&
            SaveValues(Movement, TEXT("GT Move"), MoveV) && SaveValues(Spacing, TEXT("GT Space"), ProfTestSection(TEXT("units"), TEXT("meleeReachBonus"), 27)) && SaveValues(Match, TEXT("GT Match"), MatchV),
            TEXT("game type fixture profiles saved"));
        FCireWavePreset P;
        if (const FCireWavePreset* Std = CireWaveDirector::FindPreset(TEXT("standard"))) P = *Std;
        P.Id = TEXT("gp_test"); P.Label = TEXT("GP Test"); P.bBuiltIn = false;
        CireGameProfiles::Set(P, CireGameProfiles::KeyEconomy, TEXT("GT Econ"));
        CireGameProfiles::Set(P, CireGameProfiles::KeyPacks, TEXT("GT Packs"));
        CireGameProfiles::Set(P, CireGameProfiles::KeyMovement, TEXT("GT Move"));
        CireGameProfiles::Set(P, CireGameProfiles::KeySpacing, TEXT("GT Space"));
        CireGameProfiles::Set(P, CireGameProfiles::KeyMatch, TEXT("GT Match"));
        CireGameProfiles::Set(P, CireGameProfiles::KeyWorldEdit, TEXT("Night Market"));
        CireGameProfiles::Set(P, CireGameProfiles::KeyLayout, TEXT("Default"));
        T.Check(!P.Bundle.Contains(CireGameProfiles::KeyLayout) && CireGameProfiles::Get(P, CireGameProfiles::KeyLayout).IsEmpty(), TEXT("Default is stored as a missing key"));
        FString Report;
        const int32 Changed = CireGameProfiles::ApplyDataProfiles(World, &P, &Report);
        T.Check(Changed == (bStandalone ? 5 : 4), FString::Printf(TEXT("a game type loads all five data profiles (%d): %s"), Changed, *Report));
        T.Check(FMath::IsNearlyEqual(CireLoot::MutableEconomy().BossMultiplier, 17.), TEXT("economyProfile live (boss kill gold x17)"));
        T.Check(FMath::IsNearlyEqual(CireJunglePacks::Rules().Stats.GlobalHealth, 1.7f), TEXT("packProfile live (pack health x1.7)"));
        T.Check(FMath::IsNearlyEqual(CireMovement::Tuning().RollEnergy, 33.f), TEXT("movementProfile live (roll energy 33)"));
        T.Check(FMath::IsNearlyEqual(CireUnitSpacing::Get().MeleeReachBonus, 27.f), TEXT("spacingProfile live (melee reach bonus 27)"));
        T.Check(!bStandalone || (CireDeveloperTools::Get(World).bEnabled && FMath::IsNearlyEqual(CireDeveloperTools::Get(World).CooldownScale, .8f)), TEXT("matchProfile live (F8 overrides on, cooldown x0.8)"));
        T.Check(Active(Economy) == TEXT("GT Econ") && Active(Packs) == TEXT("GT Packs") && Active(Movement) == TEXT("GT Move") && Active(Spacing) == TEXT("GT Space") && (!bStandalone || Active(Match) == TEXT("GT Match")), TEXT("active profile names follow the game type"));
        T.Check(CireGameProfiles::ApplyDataProfiles(World, &P) == 0, TEXT("applying the same game type again changes nothing"));
        const FString Summary = CireGameProfiles::Summary(P);
        T.Check(Summary.Contains(TEXT("Economy: GT Econ")) && Summary.Contains(TEXT("World edits: Night Market")) && !Summary.Contains(TEXT("Layout")), TEXT("picker summary: ") + Summary);
        FCireWavePreset Captured; CireGameProfiles::CaptureActive(World, Captured);
        T.Check(CireGameProfiles::Get(Captured, CireGameProfiles::KeyEconomy) == TEXT("GT Econ") && (!bStandalone || CireGameProfiles::Get(Captured, CireGameProfiles::KeyMatch) == TEXT("GT Match")), TEXT("SAVE CURRENT AS GAME TYPE captures the live profile names"));
        // A missing profile falls back to Default for that domain only.
        FCireWavePreset Missing = P; CireGameProfiles::Set(Missing, CireGameProfiles::KeyEconomy, TEXT("No Such Profile"));
        CireGameProfiles::ApplyDataProfiles(World, &Missing);
        T.Check(Active(Economy) == DefaultName && ProfTestText(Capture(Economy, World)) == Defaults[Economy] && Active(Packs) == TEXT("GT Packs"), TEXT("a missing profile falls back to Default"));
        // No game type (or a game type without keys) = every editor on Default.
        CireGameProfiles::ApplyDataProfiles(World, nullptr);
        bool bAllDefault = true;
        for (const FName D : Domains()) bAllDefault = bAllDefault && Active(D) == DefaultName && ProfTestText(Capture(D, World)) == Defaults[D];
        T.Check(bAllDefault && !CireDeveloperTools::Get(World).bEnabled, TEXT("no game type: every editor back on its data file"));
        // The bundle survives WavePresets.json serialization (and allowTuning with it).
        P.AllowTuning = 0; CireGameProfiles::Set(P, CireGameProfiles::KeyTuning, TEXT("Playtest tuning"));
        TArray<FCireWavePreset> Parsed;
        T.Check(CireWaveDirector::ParsePresets(CireWaveDirector::PresetsToJson({P}), Parsed, Error) && Parsed.Num() == 1 && Parsed[0].Bundle.OrderIndependentCompareEqual(P.Bundle) && Parsed[0].AllowTuning == 0,
            TEXT("bundle keys round trip through WavePresets.json: ") + Error);
        T.Check(CireGameProfiles::RuntimeLayoutPath() == CireMapLayout::ActivePath() || !CireGameProfiles::LayoutOverride().IsEmpty(), TEXT("no layout key = MapLayout.json"));
        T.Check(CireGameProfiles::Choices(CireGameProfiles::KeyEconomy, World).Contains(TEXT("GT Econ")) && CireGameProfiles::Choices(CireGameProfiles::KeyLayout, World)[0] == DefaultName, TEXT("editor dropdown choices"));
    }

    UE_LOG(LogCireProfilesTests, Display, TEXT("CIRE_PROFILES_TESTS %s checks=%d"), T.bPassed ? TEXT("PASS") : TEXT("FAIL"), T.Count);
    return T.bPassed;
}
#endif
