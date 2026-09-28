// game-profiles: the PROFILE bar under every F8 editor page (Match, Spawn/stats, Effects, Economy, Packs + spacing,
// Movement) and the game type bundle on F8 > Waves > MODES & SCALE. Same bar, same buttons everywhere:
//   [DOMAIN] [profile name (click cycles)] [LOAD] [SAVE AS] [RENAME] [DELETE]
// "Default" is the page's own data file (its SAVE button still writes it). Docs/RESUME-game-profiles.md.
#include "CireProfileBar.h"
#include "CireProfiles.h"
#include "CireHUD.h"
#include "CireGame.h"
#include "CireWaves.h"
#include "CireDeveloperTools.h"
#include "CireMobility.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#if !UE_BUILD_SHIPPING
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#endif

namespace
{
enum class EProfileTyping : uint8 { None, SaveAs, Rename, GameType };
struct FProfileBarState
{
    EProfileTyping Typing = EProfileTyping::None;
    FName TypingDomain;
    FString Buffer;
    double LastDrawn = -100;
    TMap<FName, FString> Selected;
    TMap<FName, TArray<FString>> Names;
    TMap<FName, double> NamesAt;
    FName DeleteArmed; double DeleteArmedAt = -100;
    bool bSpacingTab = false;
};
FProfileBarState GBar;
const FName GameTypeDomain(TEXT("GameType"));

double BarNow() { return FPlatformTime::Seconds(); }
const TArray<FString>& CachedNames(FName Domain, bool bRefresh = false)
{
    const double Now = BarNow();
    if (bRefresh || !GBar.Names.Contains(Domain) || Now - GBar.NamesAt.FindRef(Domain) > 2.0)
    { GBar.Names.Add(Domain, CireProfiles::List(Domain)); GBar.NamesAt.Add(Domain, Now); }
    return GBar.Names[Domain];
}
void BeginTyping(EProfileTyping Mode, FName Domain, const FString& Initial) { GBar.Typing = Mode; GBar.TypingDomain = Domain; GBar.Buffer = Initial.Left(40); GBar.LastDrawn = BarNow(); }
bool TypingFor(FName Domain) { return GBar.Typing != EProfileTyping::None && GBar.TypingDomain == Domain; }
}

#if !UE_BUILD_SHIPPING
namespace
{
// Review capture (console cire.ProfilesCapture, e.g. -ExecCmds="cire.ProfilesCapture"): opens F8 and screenshots the PROFILE bar
// on the Economy and Packs / Spacing pages into Saved/ProfilesCapture, then quits. Standalone development game only.
int32 GForcePage = -1; bool GForceSpacing = false; bool GForceWaveModes = false;
FAutoConsoleCommand GProfilesCaptureCommand(TEXT("cire.ProfilesCapture"), TEXT("game-profiles: screenshot the F8 PROFILE bar pages, then quit."), FConsoleCommandDelegate::CreateLambda([]()
{
    const double Start = FPlatformTime::Seconds();
    TSharedRef<int32> Step = MakeShared<int32>(0);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Start, Step](float)
    {
        const double Age = FPlatformTime::Seconds() - Start;
        const FString Dir = FPaths::ProjectSavedDir() / TEXT("ProfilesCapture");
        APlayerController* PC = nullptr;
        if (GEngine) for (const FWorldContext& C : GEngine->GetWorldContexts()) if (C.World() && C.World()->IsGameWorld()) PC = C.World()->GetFirstPlayerController();
        ACireHUD* HUD = PC ? Cast<ACireHUD>(PC->GetHUD()) : nullptr;
        if (!HUD) return Age < 60;
        struct FShot { double At; int32 Page; bool bSpacing; const TCHAR* Name; };
        static const FShot Shots[] = {{4, 9, false, TEXT("economy")}, {7, 10, false, TEXT("packs")}, {10, 10, true, TEXT("spacing")}, {13, 1, false, TEXT("spawn")}, {16, 6, false, TEXT("movement")}, {19, 7, false, TEXT("gametype")}};
        if (*Step == 0 && Age > 2) { HUD->ToggleDeveloperTools(); ++*Step; }
        for (int32 I = 0; I < UE_ARRAY_COUNT(Shots); ++I)
        {
            if (*Step == 1 + I * 2 && Age > Shots[I].At - 1.5) { GForcePage = Shots[I].Page; GForceSpacing = Shots[I].bSpacing; GForceWaveModes = Shots[I].Page == 7; CireProfileUI::WaveBundleView() = true; ++*Step; }
            if (*Step == 2 + I * 2 && Age > Shots[I].At) { FScreenshotRequest::RequestScreenshot(Dir / FString::Printf(TEXT("profiles_%s.png"), Shots[I].Name), true, false); ++*Step; }
        }
        if (*Step >= 1 + UE_ARRAY_COUNT(Shots) * 2 && Age > 21) { UE_LOG(LogTemp, Display, TEXT("CIRE_PROFILES_CAPTURE_DONE %s"), *Dir); FPlatformMisc::RequestExit(false); return false; }
        return true;
    }));
}));
}
#endif

bool& CireProfileUI::WaveBundleView() { static bool bView = false; return bView; }
bool CireProfileUI::ConsumeForceWaveModes()
{
#if !UE_BUILD_SHIPPING
    if (GForceWaveModes) { GForceWaveModes = false; return true; }
#endif
    return false;
}
bool CireProfileUI::OwnsKeyboard() { return GBar.Typing != EProfileTyping::None && BarNow() - GBar.LastDrawn < .3; }
bool CireProfileUI::HandleChar(TCHAR Ch)
{
    if (!OwnsKeyboard()) return false;
    if ((FChar::IsAlnum(Ch) || Ch == TEXT(' ') || Ch == TEXT('-') || Ch == TEXT('_')) && GBar.Buffer.Len() < 40) GBar.Buffer.AppendChar(Ch);
    return true;
}
bool CireProfileUI::CancelTyping()
{
    if (!OwnsKeyboard()) return false;
    GBar.Typing = EProfileTyping::None; GBar.Buffer.Reset();
    return true;
}

void ACireHUD::DrawProfileFooter(float X, float Y)
{
#if !UE_BUILD_SHIPPING
    UWorld* World = GetWorld();
    if (!World || !CireDeveloperTools::CanEdit(World)) return;
    if (GForcePage >= 0) { DeveloperPage = GForcePage; GBar.bSpacingTab = GForceSpacing; GForcePage = -1; } // review capture
    FName Domain;
    if (DeveloperPage >= 0 && DeveloperPage <= 2) Domain = CireProfiles::Match;
    else if (DeveloperPage == 9) Domain = CireProfiles::Economy;
    else if (DeveloperPage == 10) Domain = GBar.bSpacingTab ? CireProfiles::Spacing : CireProfiles::Packs;
    else if (DeveloperPage == 6) Domain = CireProfiles::Movement;
    else { if (GBar.TypingDomain != GameTypeDomain) GBar.Typing = EProfileTyping::None; return; }
    if (GBar.Typing != EProfileTyping::None && GBar.TypingDomain != Domain && GBar.TypingDomain != GameTypeDomain) GBar.Typing = EProfileTyping::None;
    if (TypingFor(Domain)) GBar.LastDrawn = BarNow();

    const float H = 24, Gap = 4;
    auto Btn = [&](const FString& Title, float BX, float BW, const FString& Help, bool bEnabled = true, bool bSelected = false, FLinearColor Accent = CireUIColors::Gold)
    {
        const bool bOver = bEnabled && Hit(BX, Y, BW, H);
        CireUIStyle::Button(Painter(), BX, Y, BW, H, Painter().Fit(Title, 9.f, BW - 10, ECireFont::Bold),
            !bEnabled ? ECireButtonState::Disabled : bSelected ? ECireButtonState::Selected : bOver ? ECireButtonState::Hover : ECireButtonState::Normal, Accent, 9.f);
        Tip(Title, Help, BX, Y, BW, H);
        if (bOver && Clicked) { Clicked = false; PlayUIFeedback(); return true; }
        return false;
    };
    const TArray<FString>& Names = CachedNames(Domain);
    const FString Active = CireProfiles::Active(Domain);
    FString& Sel = GBar.Selected.FindOrAdd(Domain);
    if (Sel.IsEmpty() || !Names.ContainsByPredicate([&](const FString& N) { return N.Equals(Sel, ESearchCase::IgnoreCase); })) Sel = Active;
    const bool bSelDefault = CireProfiles::IsDefault(Sel);
    FString Error;

    // After a load the page's draft follows the live values.
    auto SyncDrafts = [&]()
    {
        if (Domain == CireProfiles::Match) DeveloperDraft = CireDeveloperTools::Get(World);
        if (Domain == CireProfiles::Movement) MovementDraft = CireMovement::Tuning();
    };
    auto Commit = [&]()
    {
        const FString Name = CireProfiles::SanitizeName(GBar.Buffer);
        bool bOk = false;
        if (GBar.Typing == EProfileTyping::SaveAs)
        {
            if (Domain == CireProfiles::Match) { bOk = CireProfiles::SaveValues(Domain, Name, CireProfiles::MatchToJson(DeveloperDraft), &Error); if (bOk) CireProfiles::SetActive(Domain, Name); }
            else
            {
                if (Domain == CireProfiles::Movement && !CireMovement::Apply(MovementDraft, Error)) { DeveloperMessage = Error; return; } // the sliders are what gets saved
                bOk = CireProfiles::SaveAs(Domain, World, Name, &Error);
            }
            DeveloperMessage = bOk ? FString::Printf(TEXT("Saved %s profile \"%s\" (Content/Data/Profiles/%s). Name it in a game type on Waves > MODES & SCALE."), *CireProfiles::DomainLabel(Domain).ToLower(), *Name, *Domain.ToString()) : Error;
        }
        else if (GBar.Typing == EProfileTyping::Rename)
        {
            bOk = CireProfiles::Rename(Domain, Sel, Name, &Error);
            DeveloperMessage = bOk ? FString::Printf(TEXT("Renamed \"%s\" to \"%s\". Game types that named the old name now use Default."), *Sel, *Name) : Error;
        }
        if (bOk) { Sel = Name; CachedNames(Domain, true); }
        GBar.Typing = EProfileTyping::None;
    };

    float BX = X;
    // Domain tag (the Packs page switches between pack stats and boss sizes / spacing).
    {
        const bool bPacksPage = DeveloperPage == 10;
        const FString TagHelp = FString::Printf(TEXT("Named %s profiles (Content/Data/Profiles/%s/<name>.json). Active: %s.\nDefault = %s.%s"),
            *CireProfiles::DomainLabel(Domain).ToLower(), *Domain.ToString(), *Active, *CireProfiles::DefaultSource(Domain),
            bPacksPage ? TEXT("\nClick: switch between PACKS (challenge-mob stats) and SPACING (boss sizes, capsules, separation; console cire.Spacing).") : TEXT(""));
        if (Btn(CireProfiles::DomainLabel(Domain) + (bPacksPage ? TEXT(" <>") : TEXT("")), BX, 84, TagHelp, true, true, CireUIColors::Teal) && bPacksPage)
        { GBar.bSpacingTab = !GBar.bSpacingTab; GBar.Typing = EProfileTyping::None; }
        BX += 84 + Gap;
    }
    // Name box: cycles through the saved profiles, or is the text field while naming.
    const float NameW = 104;
    if (TypingFor(Domain))
    {
        if (PlayerOwner && PlayerOwner->WasInputKeyJustPressed(EKeys::BackSpace) && !GBar.Buffer.IsEmpty()) GBar.Buffer.LeftChopInline(1);
        const bool bEnter = PlayerOwner && PlayerOwner->WasInputKeyJustPressed(EKeys::Enter);
        if (PlayerOwner && PlayerOwner->WasInputKeyJustPressed(EKeys::Escape)) { GBar.Typing = EProfileTyping::None; return; }
        Painter().Rect(BX, Y, NameW, H, FLinearColor(0, 0, 0, .6f));
        Painter().Rect(BX, Y + H - 2, NameW, 2, CireUIColors::Teal);
        const FString Shown = GBar.Buffer + (FMath::Fmod(BarNow(), 1.0) < .5 ? TEXT("_") : TEXT(" "));
        Label(Painter().Fit(Shown.IsEmpty() ? FString(TEXT("_")) : Shown, 10.f, NameW - 10, ECireFont::Body), BX + 5, Y + 5, 10.f, CireUIColors::Parchment);
        Tip(TEXT("Profile name"), TEXT("Type a name, Enter to keep it, Esc to cancel. Letters, digits, space, - and _."), BX, Y, NameW, H);
        BX += NameW + Gap;
        if (Btn(TEXT("OK"), BX, 52, TEXT("Keep this name (Enter)."), !CireProfiles::SanitizeName(GBar.Buffer).IsEmpty(), false, CireUIColors::Teal) || (bEnter && !CireProfiles::SanitizeName(GBar.Buffer).IsEmpty())) Commit();
        BX += 52 + Gap;
        if (Btn(TEXT("CANCEL"), BX, 60, TEXT("Stop naming (Esc)."))) GBar.Typing = EProfileTyping::None;
        return;
    }
    {
        const bool bIsActive = Sel.Equals(Active, ESearchCase::IgnoreCase);
        FString NameHelp = TEXT("Click for the next profile, then LOAD. Profiles:\n");
        for (const FString& N : Names) NameHelp += (N.Equals(Active, ESearchCase::IgnoreCase) ? TEXT("  > ") : TEXT("    ")) + N + TEXT("\n");
        NameHelp += TEXT("> = the one live now.");
        if (Btn((bIsActive ? TEXT("> ") : TEXT("")) + Sel, BX, NameW, NameHelp, Names.Num() > 1, bIsActive, bIsActive ? CireUIColors::Teal : CireUIColors::Gold))
        {
            const int32 I = Names.IndexOfByPredicate([&](const FString& N) { return N.Equals(Sel, ESearchCase::IgnoreCase); });
            Sel = Names[(I + 1) % FMath::Max(1, Names.Num())];
        }
        BX += NameW + Gap;
    }
    if (Btn(TEXT("LOAD"), BX, 52, FString::Printf(TEXT("Make \"%s\" live now%s."), *Sel,
        Domain == CireProfiles::Match ? TEXT(" (turns the F8 match overrides on; Default turns them off)") : bSelDefault ? *FString::Printf(TEXT(": re-read %s"), *CireProfiles::DefaultSource(Domain)) : TEXT(""))))
    {
        const bool bOk = CireProfiles::Load(Domain, World, Sel, &Error);
        if (bOk) SyncDrafts();
        DeveloperMessage = bOk ? FString::Printf(TEXT("%s profile \"%s\" is live."), *CireProfiles::DomainLabel(Domain), *CireProfiles::Active(Domain)) : Error;
    }
    BX += 52 + Gap;
    if (Btn(TEXT("SAVE AS"), BX, 60, Domain == CireProfiles::Match ? TEXT("Save this page's values (all three Match / Spawn / Effects pages) as a named profile.") :
        TEXT("Save the values on this page as a named profile. Saving under an existing name replaces it.")))
        BeginTyping(EProfileTyping::SaveAs, Domain, bSelDefault ? FString() : Sel);
    BX += 60 + Gap;
    if (Btn(TEXT("RENAME"), BX, 60, TEXT("Rename the selected profile (not Default)."), !bSelDefault)) BeginTyping(EProfileTyping::Rename, Domain, Sel);
    BX += 60 + Gap;
    const bool bArmed = GBar.DeleteArmed == Domain && BarNow() - GBar.DeleteArmedAt < 3.0;
    if (Btn(bArmed ? TEXT("SURE?") : TEXT("DELETE"), BX, 52, TEXT("Delete the selected profile file (click twice). Default is the data file and cannot be deleted."), !bSelDefault, bArmed, CireUIColors::Red))
    {
        if (!bArmed) { GBar.DeleteArmed = Domain; GBar.DeleteArmedAt = BarNow(); }
        else
        {
            GBar.DeleteArmed = NAME_None;
            DeveloperMessage = CireProfiles::Delete(Domain, Sel, &Error) ? FString::Printf(TEXT("Deleted profile \"%s\"."), *Sel) : Error;
            Sel = CireProfiles::Active(Domain); CachedNames(Domain, true);
        }
    }
#endif
}

void ACireHUD::DrawGameTypeBundle(float X, float Y, float W)
{
#if !UE_BUILD_SHIPPING
    UWorld* World = GetWorld();
    if (!World) return;
    const FCireWavePreset* Current = CireWaveDirector::FindPreset(WaveDraft.Preset);
    auto Btn = [&](const FString& Title, float BX, float BY, float BW, float BH, const FString& Help, bool bEnabled = true, bool bSelected = false, FLinearColor Accent = CireUIColors::Gold)
    {
        const bool bOver = bEnabled && Hit(BX, BY, BW, BH);
        CireUIStyle::Button(Painter(), BX, BY, BW, BH, Painter().Fit(Title, 8.5f, BW - 8, ECireFont::Bold),
            !bEnabled ? ECireButtonState::Disabled : bSelected ? ECireButtonState::Selected : bOver ? ECireButtonState::Hover : ECireButtonState::Normal, Accent, 8.5f);
        Tip(Title, Help, BX, BY, BW, BH);
        if (bOver && Clicked) { Clicked = false; PlayUIFeedback(); return true; }
        return false;
    };
    Label(Painter().Fit(FString::Printf(TEXT("%s  |  click a slot for the next profile (saved at once)"), Current ? *Current->Label.ToUpper() : TEXT("SELECT A PRESET BELOW")), 8.5f, W, ECireFont::Heading), X, Y, 8.5f, CireUIColors::Muted);
    // One cycling button per bundle key; a click saves the choice into WavePresets.json at once.
    const TArray<FString>& Keys = CireGameProfiles::Keys();
    const float CW = (W - 8) / 3.f, RowH = 24;
    FString Error;
    for (int32 I = 0; I < Keys.Num(); ++I)
    {
        const FString& Key = Keys[I];
        const float BX = X + (I % 3) * (CW + 4), BY = Y + 14 + (I / 3) * RowH;
        const FString Now = Current ? CireGameProfiles::Get(*Current, Key) : FString();
        const FString Shown = Now.IsEmpty() ? FString(CireProfiles::DefaultName) : Now;
        const TArray<FString> Choices = CireGameProfiles::Choices(Key, World);
        const FString Help = FString::Printf(TEXT("%s for this game type (WavePresets.json \"%s\"): %s.\nChoices: %s.\nClick for the next one; it is saved at once and applies when the game type is picked (host, before wave 1).%s"),
            *CireGameProfiles::KeyLabel(Key), *Key, *Shown, *FString::Join(Choices, TEXT(", ")),
            Key == CireGameProfiles::KeyWorldEdit ? TEXT("\nWorld edit sets come from the world editor (feat/world-editor); until it is merged the choice is kept but not applied.") :
            Key == CireGameProfiles::KeyMatch ? TEXT("\nMatch profiles are F8 development overrides: they apply in a standalone game only.") : TEXT(""));
        if (Btn(FString::Printf(TEXT("%s: %s"), *CireGameProfiles::KeyLabel(Key), *Shown), BX, BY, CW, 20, Help, Current != nullptr && Choices.Num() > 1, !Now.IsEmpty(), CireUIColors::Teal) && Current)
        {
            const int32 At = Choices.IndexOfByPredicate([&](const FString& C) { return C.Equals(Shown, ESearchCase::IgnoreCase); });
            FCireWavePreset P = *Current;
            CireGameProfiles::Set(P, Key, Choices[(At + 1) % Choices.Num()]);
            DeveloperMessage = CireWaveDirector::SavePreset(P, &Error) ? FString::Printf(TEXT("%s: %s = %s (saved to WavePresets.json)."), *P.Label, *CireGameProfiles::KeyLabel(Key),
                CireGameProfiles::Get(P, Key).IsEmpty() ? CireProfiles::DefaultName : *CireGameProfiles::Get(P, Key)) : Error;
        }
    }
    const float BY = Y + 14 + 3 * RowH + 2;
    if (TypingFor(GameTypeDomain))
    {
        GBar.LastDrawn = BarNow();
        if (PlayerOwner && PlayerOwner->WasInputKeyJustPressed(EKeys::BackSpace) && !GBar.Buffer.IsEmpty()) GBar.Buffer.LeftChopInline(1);
        const bool bEnter = PlayerOwner && PlayerOwner->WasInputKeyJustPressed(EKeys::Enter);
        if (PlayerOwner && PlayerOwner->WasInputKeyJustPressed(EKeys::Escape)) { GBar.Typing = EProfileTyping::None; return; }
        Label(TEXT("NAME"), X, BY + 5, 8.5f, CireUIColors::Muted);
        Painter().Rect(X + 38, BY, 200, 22, FLinearColor(0, 0, 0, .6f)); Painter().Rect(X + 38, BY + 20, 200, 2, CireUIColors::Teal);
        Label(Painter().Fit(GBar.Buffer + (FMath::Fmod(BarNow(), 1.0) < .5 ? TEXT("_") : TEXT(" ")), 10.f, 190, ECireFont::Body), X + 43, BY + 4, 10.f, CireUIColors::Parchment);
        const FString Name = CireProfiles::SanitizeName(GBar.Buffer);
        if (Btn(TEXT("CREATE"), X + 242, BY, 80, 22, TEXT("Create the game type (Enter)."), !Name.IsEmpty(), false, CireUIColors::Teal) || (bEnter && !Name.IsEmpty()))
        {
            FCireWavePreset P = CireWaveDirector::CapturePreset(WaveDraft, FName(*Name), Name, FString());
            CireGameProfiles::CaptureActive(World, P); // the profiles live now: layout, tuning, kits, economy, packs, movement, match, spacing, world edits
            P.Description = FString::Printf(TEXT("Saved from F8. %s."), *CireGameProfiles::Summary(P));
            if (CireWaveDirector::SavePreset(P, &Error))
            {
                for (const FCireWavePreset& Saved : CireWaveDirector::Presets()) if (Saved.Label == P.Label.Left(32).TrimStartAndEnd()) WaveDraft.Preset = Saved.Id;
                DeveloperMessage = FString::Printf(TEXT("Game type \"%s\" saved: %s. Hosts pick it under GAME TYPE."), *Name, *CireGameProfiles::Summary(P));
            }
            else DeveloperMessage = Error;
            GBar.Typing = EProfileTyping::None;
        }
        if (Btn(TEXT("CANCEL"), X + 326, BY, 80, 22, TEXT("Stop naming (Esc)."))) GBar.Typing = EProfileTyping::None;
    }
    else if (Btn(TEXT("SAVE CURRENT AS GAME TYPE"), X, BY, 200, 22, TEXT("A new game type from this draft (damage toggles, fight-back packs, live scale, pack modifier, PvP rounds) plus the profile live now in every editor (layout, ability tuning, hero kits, economy, packs, movement, match, spacing, world edits)."),
        true, false, CireUIColors::Teal))
        BeginTyping(EProfileTyping::GameType, GameTypeDomain, FString());
    if (Current && !TypingFor(GameTypeDomain))
        Label(Painter().Fit(CireGameProfiles::Summary(*Current), 8.f, W - 208, ECireFont::Body), X + 208, BY + 6, 8.f, CireUIColors::Muted);
#endif
}
