// ability-tuner: native checks (run by -CireCombatExpansionProbe / Tools/RunExpansionChecks.py --only native).
// Apply / reset per field and per ability, validation, CastRules precedence, disabled abilities, name aliases, VFX,
// level-15 scales, DB reload persistence, JSON + profile save/load + export/import round trips, authority, and the
// shipping-safe access defaults.
#include "CireAbilityTuner.h"
#include "CireAbilityTunerState.h"
#include "CireAbilityTunerUI.h"
#include "CireAbilityDB.h"
#include "CireAbilityExpansion.h"
#include "CireAbilityShapes.h"
#include "CireGame.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireTunerTests, Log, All);

namespace
{
struct FTunerChecks
{
    int32 Count = 0; bool bPassed = true;
    void Check(bool bCondition, const FString& Message)
    {
        ++Count;
        if (!bCondition) { bPassed = false; UE_LOG(LogCireTunerTests, Error, TEXT("CIRE_ABILITY_TUNER_CHECK_FAIL %s"), *Message); }
    }
};
FString FirstAbility(TFunctionRef<bool(const FCireAbilityDef&)> Pred)
{
    for (const FCireAbilityDef& D : CireAbilityDB::All()) if (CireAbilityTuner::FileRow(D.Id) && Pred(D)) return D.Id;
    return FString();
}
}

bool CireAbilityTuner::RunSmoke(ACireGameMode* Mode)
{
    FTunerChecks T;
    ResetAllLocal();
    const uint32 V0 = Version();
    int32 Broadcasts = 0;
    const FDelegateHandle Handle = OnChanged().AddLambda([&Broadcasts] { ++Broadcasts; });

    // 1) Generic field editor: every numeric leaf of the row, defaults = file values.
    const FString Lance = TEXT("ember_lance");
    const FCireAbilityDef Original = *CireAbilityDB::Find(Lance);
    const TArray<FCireTunerField> Fields = CireAbilityTuner::Fields(Lance);
    auto Has = [&](const TCHAR* Path) { return Fields.ContainsByPredicate([&](const FCireTunerField& F) { return F.Path == Path; }); };
    T.Check(Has(TEXT("base.cooldown")) && Has(TEXT("base.manaCost")) && Has(TEXT("castTime")) && Has(TEXT("scaling.primary")) && Has(TEXT("curve.effectGrowth")) &&
        Has(TEXT("level15.scale")) && Has(TEXT("level15.durationScale")) && !Has(TEXT("base.castTime")), TEXT("field editor lists the row's numbers (+ level-15 scales, no ignored keys)"));
    bool bDefaults = true; for (const FCireTunerField& F : Fields) bDefaults &= !F.bOverridden && F.Value == F.Default && F.Max >= F.Min && F.Step > 0;
    T.Check(bDefaults && Fields.Num() >= 15, FString::Printf(TEXT("%d fields at their defaults with sane ranges"), Fields.Num()));

    // 2) Apply: name, text, cooldown, mana, radius, level-15 scale; old and new names both resolve.
    FCireAbilityOverride O;
    O.Name = TEXT("Tuner Lance"); O.Description = TEXT("Tuned: {effect} fire.");
    O.Fields.Add(TEXT("base.cooldown"), 2.5); O.Fields.Add(TEXT("base.manaCost"), 11); O.Fields.Add(TEXT("level15.scale"), 2); O.Fields.Add(TEXT("base.effect"), 150);
    FString Error;
    T.Check(ApplyLocal(Lance, O, &Error), TEXT("override applies: ") + Error);
    const FCireAbilityDef* D = CireAbilityDB::Find(Lance);
    T.Check(D == CireAbilityDB::Find(Lance) && D->Name == TEXT("Tuner Lance") && FMath::IsNearlyEqual(D->Base.Cooldown, 2.5f) && FMath::IsNearlyEqual(D->Base.ManaCost, 11.f) &&
        FMath::IsNearlyEqual(D->Base.Effect, 150.f) && FMath::IsNearlyEqual(D->Level15Scale, 2.f), TEXT("row numbers replaced in place"));
    T.Check(CireAbilityDB::Describe(Lance, 1).StartsWith(TEXT("Tuned: ")) && CireAbilityDB::EffectiveStats(Lance, 1).Cooldown <= 2.5f + .01f, TEXT("tooltip text and scaled stats follow"));
    T.Check(CireAbilityDB::FindByName(TEXT("Tuner Lance")) == D && CireAbilityDB::FindByName(TEXT("Ember Lance")) == D, TEXT("new and original names resolve"));
    T.Check(Version() > V0 && Broadcasts >= 1, TEXT("version bumps and OnChanged fires"));
    const TArray<FCireTunerField> After = CireAbilityTuner::Fields(Lance);
    const FCireTunerField* Cd = After.FindByPredicate([](const FCireTunerField& F) { return F.Path == TEXT("base.cooldown"); });
    T.Check(Cd && Cd->bOverridden && Cd->Value == 2.5 && Cd->Default == Original.Base.Cooldown, TEXT("field editor shows value vs default"));

    // 3) Per-field reset, then per-ability reset back to the file row.
    O.Fields.Remove(TEXT("base.cooldown"));
    T.Check(ApplyLocal(Lance, O) && FMath::IsNearlyEqual(CireAbilityDB::Find(Lance)->Base.Cooldown, Original.Base.Cooldown) && FMath::IsNearlyEqual(CireAbilityDB::Find(Lance)->Base.ManaCost, 11.f), TEXT("reset one field"));
    T.Check(ApplyLocal(Lance, FCireAbilityOverride()) && !Find(Lance), TEXT("reset the ability"));
    D = CireAbilityDB::Find(Lance);
    T.Check(D->Name == Original.Name && D->Description == Original.Description && D->Base.ManaCost == Original.Base.ManaCost && D->Base.Effect == Original.Base.Effect &&
        D->CastTime == Original.CastTime && D->Radius == Original.Radius && D->Level15Scale == 1.f, TEXT("reset rebuilds the exact file row"));

    // 4) Validation: an out-of-range magnitude or unknown path is refused and nothing changes.
    const FString Slow = FirstAbility([](const FCireAbilityDef& X) { return X.Effects.Num() > 0 && X.Effects[0].Magnitude > 0; });
    if (!Slow.IsEmpty())
    {
        const float Mag = CireAbilityDB::Find(Slow)->Effects[0].Magnitude, Dur = CireAbilityDB::Find(Slow)->Effects[0].Duration;
        FCireAbilityOverride Bad; Bad.Fields.Add(TEXT("effects.0.magnitude"), 5);
        T.Check(!ApplyLocal(Slow, Bad, &Error) && !Error.IsEmpty() && CireAbilityDB::Find(Slow)->Effects[0].Magnitude == Mag && !Find(Slow), TEXT("invalid magnitude refused: ") + Error);
        FCireAbilityOverride Unknown; Unknown.Fields.Add(TEXT("base.noSuchNumber"), 1);
        T.Check(!ApplyLocal(Slow, Unknown), TEXT("unknown path refused"));
        FCireAbilityOverride Longer; Longer.Fields.Add(TEXT("effects.0.duration"), Dur + 1.5);
        T.Check(ApplyLocal(Slow, Longer) && FMath::IsNearlyEqual(CireAbilityDB::Find(Slow)->Effects[0].Duration, Dur + 1.5f), Slow + TEXT(": CC duration tuned"));
        ApplyLocal(Slow, FCireAbilityOverride());
    }
    else T.Check(false, TEXT("an ability with a CC effect exists"));

    // 5) A cast-time override beats CastRules; reset restores the rule.
    const FString Ruled = FirstAbility([](const FCireAbilityDef& X) { return X.CastRule == TEXT("aoeDamage"); });
    if (!Ruled.IsEmpty())
    {
        const float RuleCast = CireAbilityDB::Find(Ruled)->CastTime;
        FCireAbilityOverride Fast; Fast.Fields.Add(TEXT("castTime"), .3);
        T.Check(ApplyLocal(Ruled, Fast) && FMath::IsNearlyEqual(CireAbilityDB::Find(Ruled)->CastTime, .3f) && FMath::IsNearlyEqual(CireAbilityDB::Find(Ruled)->Base.CastTime, .3f) &&
            CireAbilityDB::Find(Ruled)->CastRule == TEXT("tuned"), Ruled + TEXT(": tuned cast time beats the AoE cast rule"));
        ApplyLocal(Ruled, FCireAbilityOverride());
        T.Check(FMath::IsNearlyEqual(CireAbilityDB::Find(Ruled)->CastTime, RuleCast) && CireAbilityDB::Find(Ruled)->CastRule == TEXT("aoeDamage"), TEXT("reset restores the rule's cast time"));
    }
    else T.Check(false, TEXT("an AoE-rule ability exists"));

    // 6) Expansion recipe numbers (DoT / buff durations, telegraphs) apply live.
    const FString Recipe = FirstAbility([](const FCireAbilityDef& X) { const auto R = CireAbilityTuner::FileRow(X.Id); return R && R->HasField(TEXT("recipe")) && R->GetObjectField(TEXT("recipe"))->HasField(TEXT("warning")); });
    if (!Recipe.IsEmpty())
    {
        FCireHitShape Before; CireAbilityExpansion::DescribeShape(Recipe, Before);
        FCireAbilityOverride W; W.Fields.Add(TEXT("recipe.warning"), 1.7);
        FCireHitShape Tuned; const bool bOk = ApplyLocal(Recipe, W) && CireAbilityExpansion::DescribeShape(Recipe, Tuned);
        T.Check(bOk && FMath::IsNearlyEqual(Tuned.WarningSeconds, 1.7f), Recipe + TEXT(": recipe telegraph tuned live"));
        ApplyLocal(Recipe, FCireAbilityOverride());
        FCireHitShape Reset; CireAbilityExpansion::DescribeShape(Recipe, Reset);
        T.Check(FMath::IsNearlyEqual(Reset.WarningSeconds, Before.WarningSeconds), TEXT("recipe reset"));
    }

    // 7) Disabled: gone from purchasable lists and CanLearn; VFX lookups by id and name.
    FString Champion; for (const FCireAbilityDef& X : CireAbilityDB::All()) if (X.Id == Lance && X.Champions.Num()) Champion = X.Champions[0];
    FCireAbilityOverride Off; Off.bEnabled = false; Off.VfxScale = 1.6f; Off.VfxTint = FLinearColor(1, 0, 0, 1);
    T.Check(ApplyLocal(Lance, Off) && IsDisabled(Lance) && (Champion.IsEmpty() || (!CireAbilityDB::CanLearn(Champion, Lance) && !CireAbilityDB::PurchasableSkills(Champion, false).Contains(Lance))),
        TEXT("disabled ability leaves the shops"));
    float Scale = 0; FLinearColor Tint;
    T.Check(VfxFor(Lance, Scale, Tint) && FMath::IsNearlyEqual(Scale, 1.6f) && Tint.R == 1 && VfxFor(Original.Name, Scale, Tint), TEXT("VFX override by id and by name"));

    // 8) The override survives a database reload.
    CireAbilityDB::Reload();
    T.Check(IsDisabled(Lance) && Find(Lance) && CireAbilityDB::Find(Lance)->Name == Original.Name, TEXT("overrides re-apply after Reload"));

    // 9) JSON round trip of a set.
    FCireTuningSet Set; Set.Add(Lance, O); Set.Add(TEXT("x_placeholder"), Off);
    FCireTuningSet Back; T.Check(DeserializeSet(SerializeSet(Set), Back) && Back.Num() == 2 && Back[Lance] == O && Back[TEXT("x_placeholder")] == Off, TEXT("set JSON round trip"));

    // 10) Profiles: save two, load, startup, delete; export / import.
    const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/AbilityTuner"));
    const FString File = FPaths::Combine(Dir, TEXT("AbilityOverrides.json")), Export = FPaths::Combine(Dir, TEXT("Exported.json"));
    IFileManager::Get().Delete(*File, false, true, true); IFileManager::Get().Delete(*Export, false, true, true);
    FCireTuningProfile A; A.Name = TEXT("Alpha"); A.Notes = TEXT("test"); A.Abilities.Add(Lance, O);
    FCireTuningProfile B; B.Name = TEXT("Beta"); B.Abilities.Add(Lance, Off);
    TArray<FCireTuningProfile> Loaded; FString Startup;
    T.Check(SaveProfile(File, A, &Error) && SaveProfile(File, B, &Error) && LoadProfiles(File, Loaded, &Startup, &Error) && Loaded.Num() == 2 &&
        Loaded[0].Name == TEXT("Alpha") && Loaded[0].Notes == TEXT("test") && Loaded[0].Abilities[Lance] == O && Loaded[1].Abilities[Lance] == Off && Startup.IsEmpty(), TEXT("profile save/load round trip ") + Error);
    T.Check(SetStartupProfile(File, TEXT("Beta")) && LoadProfiles(File, Loaded, &Startup) && Startup == TEXT("Beta") && !SetStartupProfile(File, TEXT("Nope")), TEXT("startup profile"));
    T.Check(DeleteProfile(File, TEXT("Beta")) && LoadProfiles(File, Loaded, &Startup) && Loaded.Num() == 1 && Startup.IsEmpty(), TEXT("delete profile clears startup"));
    T.Check(!SaveProfile(File, FCireTuningProfile(), &Error), TEXT("unnamed profile refused"));
    FCireTuningProfile Imported;
    T.Check(ExportProfile(A, Export) && ImportProfile(Export, Imported) && Imported.Name == TEXT("Alpha") && Imported.Abilities.Num() == 1 && Imported.Abilities[Lance] == O, TEXT("export / import round trip"));
    T.Check(!ImportProfile(File, Imported), TEXT("a profiles file is not an export"));
    IFileManager::Get().DeleteDirectory(*Dir, false, true);

    // 11) Shipping-safe paths: access defaults and where a packaged build writes.
    T.Check(!AllowedByDefault(true, TEXT("")) && AllowedByDefault(true, TEXT("-CireAllowTuning")) && AllowedByDefault(false, TEXT("")) && !AllowedByDefault(false, TEXT("-CireNoTuning")),
        TEXT("Allow ability tuning: off in shipping unless -CireAllowTuning, on in dev unless -CireNoTuning"));
    T.Check(FPaths::IsUnderDirectory(ProfilesPath(true), FPaths::ProjectSavedDir()) && FPaths::IsUnderDirectory(ProfilesPath(false), FPaths::ProjectContentDir()) &&
        FPaths::IsUnderDirectory(ExportDirectory(), FPaths::ProjectSavedDir()), TEXT("packaged builds write under Saved, the editor under Content/Data"));
    T.Check(!CanTune(nullptr) && !CanChangeAllowed(nullptr) && !CastNow(nullptr, Lance) && !SpawnTargetDummy(nullptr), TEXT("no controller, no access"));

    // 12) Authority: the match's replicated state carries the set; presets apply profiles.
    UWorld* World = Mode ? Mode->GetWorld() : nullptr;
    ACireAbilityTunerState* S = ACireAbilityTunerState::Get(World);
    T.Check(S != nullptr && S->bAllowTuning == AllowedByDefault(UE_BUILD_SHIPPING != 0, TEXT("")), TEXT("match spawned the tuner state with the default option"));
    if (S)
    {
        const int32 Rev = S->Revision;
        T.Check(CommitAuthority(World, Lance, O, &Error) && S->Revision > Rev && S->TuningJson.Contains(TEXT("Tuner Lance")), TEXT("authoritative commit publishes the set"));
        FCireTuningSet FromWire; T.Check(DeserializeSet(S->TuningJson, FromWire) && FromWire.Num() == Active().Num() && FromWire[Lance] == O, TEXT("published JSON = active set"));
        auto Preset = MakeShared<FJsonObject>(); Preset->SetBoolField(TEXT("allowTuning"), false);
        T.Check(ApplyModePresetJson(World, Preset) && !S->bAllowTuning && S->bAllowLocked && Active().IsEmpty(), TEXT("ranked-style preset: tuning off, locked, startup set"));
        T.Check(!ApplyModePreset(World, TEXT("NoSuchProfile"), 1), TEXT("missing preset profile reported"));
        S->bAllowTuning = AllowedByDefault(UE_BUILD_SHIPPING != 0, TEXT("")); S->bAllowLocked = false;
    }
    // 13) The UI model opens on an ability (drawn by the interface gallery / play).
    CireAbilityTunerUI::Select(Lance);
    T.Check(CireAbilityTunerUI::IsOpen() && CireAbilityTunerUI::Selected() == Lance, TEXT("tuner window selects an ability"));
    CireAbilityTunerUI::Close();

    // Cleanup: every row back to the file values (and the state republished).
    if (World) CommitSetAuthority(World, FCireTuningSet(), FString()); else ResetAllLocal();
    T.Check(Active().IsEmpty() && CireAbilityDB::Find(Lance)->Name == Original.Name && !IsDisabled(Lance), TEXT("cleanup restores the file rows"));
    OnChanged().Remove(Handle);
    UE_LOG(LogCireTunerTests, Display, TEXT("CIRE_ABILITY_TUNER_%s checks=%d abilities=%d"), T.bPassed ? TEXT("PASS") : TEXT("FAIL"), T.Count, CireAbilityDB::All().Num());
    return T.bPassed;
}
