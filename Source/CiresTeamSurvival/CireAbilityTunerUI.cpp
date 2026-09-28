// ability-tuner: the in-game Ability Tuner window. See CireAbilityTunerUI.h and Docs/AbilityTuner.md.
#include "CireAbilityTunerUI.h"
#include "CireAbilityTuner.h"
#include "CireAbilityDB.h"
#include "CireAudio.h"
#include "CireGame.h"
#include "CireHUD.h"
#include "CireUIStyle.h"
#include "Engine/Texture2D.h"
#include "InputCoreTypes.h"
#include "Misc/Paths.h"

namespace
{
enum class EFocus : uint8 { None, Search, Name, Description, Field, Profile };

struct FTunerUIState
{
    bool bOpen = false;
    FString Search, Selected;
    int32 Filter = 0;              // 0 all, 1 tuned, 2 disabled, 3 actives, 4 ultimates, 5 passives
    int32 ListScroll = 0, FieldScroll = 0, PreviewLevel = 1;
    EFocus Focus = EFocus::None;
    FString FocusPath, Buffer;
    FString Message; bool bMessageError = false; double MessageAt = -100;
    FString ProfileName = TEXT("Playtest tuning");
    int32 ProfileCursor = -1, ImportCursor = -1;
    FString DragPath;              // slider being dragged ("", "vfx.scale", "vfx.hue" or a field path)
    double ResetAllArmedAt = -100;
    FCireUIRect Rect;
    int32 LastRows = 0;
    // caches
    TArray<FString> Filtered; FString CachedSearch; int32 CachedFilter = -1; uint32 CachedVersion = 0;
    TArray<FCireTunerField> Fields; FString FieldsFor; uint32 FieldsVersion = 0;
};
FTunerUIState GTunerUI;

double NowSeconds() { return FPlatformTime::Seconds(); }
bool In(FVector2D M, float X, float Y, float W, float H) { return M.X >= X && M.X <= X + W && M.Y >= Y && M.Y <= Y + H; }
void Say(const FString& Text, bool bError = false) { GTunerUI.Message = Text; GTunerUI.bMessageError = bError; GTunerUI.MessageAt = NowSeconds(); }

FLinearColor SchoolTint(const FString& School)
{
    static const TMap<FString, FLinearColor> Tints = {
        {TEXT("fire"), FLinearColor(1.f, .45f, .15f, 1)}, {TEXT("cold"), FLinearColor(.45f, .75f, 1.f, 1)}, {TEXT("frost"), FLinearColor(.45f, .75f, 1.f, 1)},
        {TEXT("nature"), FLinearColor(.35f, .85f, .35f, 1)}, {TEXT("holy"), FLinearColor(1.f, .85f, .4f, 1)}, {TEXT("shadow"), FLinearColor(.7f, .45f, .95f, 1)},
        {TEXT("arcane"), FLinearColor(.55f, .6f, 1.f, 1)}, {TEXT("lightning"), FLinearColor(.6f, .85f, 1.f, 1)}, {TEXT("physical"), FLinearColor(.8f, .8f, .82f, 1)},
        {TEXT("earth"), FLinearColor(.75f, .6f, .4f, 1)}, {TEXT("poison"), FLinearColor(.55f, .9f, .3f, 1)}, {TEXT("blood"), FLinearColor(.9f, .25f, .25f, 1)},
        {TEXT("void"), FLinearColor(.6f, .35f, .85f, 1)}, {TEXT("tech"), FLinearColor(.35f, .9f, .85f, 1)}};
    const FLinearColor* T = Tints.Find(School.ToLower());
    return T ? *T : CireUIColors::Teal;
}
FString KindLabel(const FCireAbilityDef& D) { return D.IsUltimate() ? TEXT("ULTIMATE") : D.IsPassive() ? TEXT("PASSIVE") : D.IsConstruct() ? TEXT("CONSTRUCT") : TEXT("ACTIVE"); }
FString Num(double V, double Step)
{
    if (Step >= 1) return FString::Printf(TEXT("%.0f"), V);
    if (Step >= .1) return FString::Printf(TEXT("%.1f"), V);
    return FString::Printf(TEXT("%.2f"), V);
}

void Click(ACireHUD& HUD) { if (!HUD.UISettings.bMuteAudio) CireAudio::PlayCue2D(&HUD, TEXT("ui_click"), .8f); }

FCireAbilityOverride CurrentOverride(const FString& Id) { const FCireAbilityOverride* O = CireAbilityTuner::Find(Id); return O ? *O : FCireAbilityOverride(); }

bool Commit(ACireController* Controller, const FString& Id, const FCireAbilityOverride& O)
{
    FString Why;
    if (CireAbilityTuner::Request(Controller, Id, O, &Why)) return true;
    Say(Why, true); return false;
}

void RefreshList()
{
    if (GTunerUI.CachedSearch == GTunerUI.Search && GTunerUI.CachedFilter == GTunerUI.Filter && GTunerUI.CachedVersion == CireAbilityTuner::Version() && !GTunerUI.Filtered.IsEmpty()) return;
    GTunerUI.CachedSearch = GTunerUI.Search; GTunerUI.CachedFilter = GTunerUI.Filter; GTunerUI.CachedVersion = CireAbilityTuner::Version();
    GTunerUI.Filtered.Reset();
    const FString Q = GTunerUI.Search.TrimStartAndEnd();
    TArray<const FCireAbilityDef*> Rows;
    for (const FCireAbilityDef& D : CireAbilityDB::All())
    {
        if (!CireAbilityTuner::FileRow(D.Id)) continue;
        const FCireAbilityOverride* O = CireAbilityTuner::Find(D.Id);
        if (GTunerUI.Filter == 1 && !O) continue;
        if (GTunerUI.Filter == 2 && !CireAbilityTuner::IsDisabled(D.Id)) continue;
        if (GTunerUI.Filter == 3 && (D.IsPassive() || D.IsUltimate())) continue;
        if (GTunerUI.Filter == 4 && !D.IsUltimate()) continue;
        if (GTunerUI.Filter == 5 && !D.IsPassive()) continue;
        if (!Q.IsEmpty() && !D.Name.Contains(Q) && !D.Id.Contains(Q) && !D.School.Contains(Q) && !FString::Join(D.EffectTags, TEXT(" ")).Contains(Q) && !FString::Join(D.Champions, TEXT(" ")).Contains(Q)) continue;
        Rows.Add(&D);
    }
    Rows.Sort([](const FCireAbilityDef& A, const FCireAbilityDef& B) { return A.Name < B.Name; });
    for (const FCireAbilityDef* D : Rows) GTunerUI.Filtered.Add(D->Id);
}
const TArray<FCireTunerField>& FieldsOf(const FString& Id)
{
    if (GTunerUI.FieldsFor != Id || GTunerUI.FieldsVersion != CireAbilityTuner::Version()) { GTunerUI.Fields = CireAbilityTuner::Fields(Id); GTunerUI.FieldsFor = Id; GTunerUI.FieldsVersion = CireAbilityTuner::Version(); }
    return GTunerUI.Fields;
}

void FocusOn(EFocus F, const FString& Value, const FString& Path = FString()) { GTunerUI.Focus = F; GTunerUI.Buffer = Value; GTunerUI.FocusPath = Path; }

/** Applies the focused text box (Enter / Tab / click elsewhere). */
void CommitFocus(ACireController* Controller)
{
    const EFocus F = GTunerUI.Focus; GTunerUI.Focus = EFocus::None;
    if (F == EFocus::None || F == EFocus::Search || GTunerUI.Selected.IsEmpty())
    { if (F == EFocus::Profile && !GTunerUI.Buffer.TrimStartAndEnd().IsEmpty()) GTunerUI.ProfileName = GTunerUI.Buffer.TrimStartAndEnd().Left(40); return; }
    if (F == EFocus::Profile) { if (!GTunerUI.Buffer.TrimStartAndEnd().IsEmpty()) GTunerUI.ProfileName = GTunerUI.Buffer.TrimStartAndEnd().Left(40); return; }
    FCireAbilityOverride O = CurrentOverride(GTunerUI.Selected);
    const TSharedPtr<FJsonObject> Row = CireAbilityTuner::FileRow(GTunerUI.Selected);
    FString FileName, FileText; if (Row) { Row->TryGetStringField(TEXT("name"), FileName); Row->TryGetStringField(TEXT("description"), FileText); }
    if (F == EFocus::Name) { const FString N = GTunerUI.Buffer.TrimStartAndEnd(); if (N.IsEmpty() || N == FileName) O.Name.Reset(); else O.Name = N; }
    else if (F == EFocus::Description) { if (GTunerUI.Buffer == FileText) O.Description.Reset(); else O.Description = GTunerUI.Buffer; }
    else if (F == EFocus::Field)
    {
        if (!GTunerUI.Buffer.IsNumeric()) { Say(TEXT("Type a number."), true); return; }
        const double V = FCString::Atod(*GTunerUI.Buffer);
        for (const FCireTunerField& Fd : FieldsOf(GTunerUI.Selected))
            if (Fd.Path == GTunerUI.FocusPath)
            {
                const double Q = Fd.bInteger ? FMath::RoundToDouble(V) : V;
                if (FMath::IsNearlyEqual(Q, Fd.Default, 1e-9)) O.Fields.Remove(Fd.Path); else O.Fields.Add(Fd.Path, Q);
            }
    }
    if (Commit(Controller, GTunerUI.Selected, O)) Say(TEXT("Applied live."));
}

// ------------------------------------------------------------------------------------------------ drawing helpers
struct FCtx
{
    ACireHUD& HUD; ACireController* Controller; FCireUIPainter P; FVector2D M; bool bCanEdit; bool bMouseDown;
    bool Clicked(float X, float Y, float W, float H) const { if (HUD.HasClick() && In(M, X, Y, W, H)) { HUD.TakeClick(); return true; } return false; }
    bool Button(const FString& Label, float X, float Y, float W, float H, bool bEnabled = true, FLinearColor Accent = CireUIColors::Gold, bool bSelected = false, float Size = 9.f) const
    {
        const bool bOver = In(M, X, Y, W, H);
        CireUIStyle::Button(P, X, Y, W, H, Label, !bEnabled ? ECireButtonState::Disabled : bSelected ? ECireButtonState::Selected : bOver ? ECireButtonState::Hover : ECireButtonState::Normal, Accent, Size);
        if (bEnabled && Clicked(X, Y, W, H)) { Click(HUD); return true; }
        return false;
    }
    void Tip(const FString& Title, const FString& Body, float X, float Y, float W, float H) const { if (In(M, X, Y, W, H)) HUD.SetTooltip(Title, Body); }
    /** A one-line text box; click focuses it (and commits whatever had focus before). */
    void TextBox(float X, float Y, float W, float H, const FString& Value, EFocus Kind, const FString& Path, const FString& Placeholder, bool bEnabled, FLinearColor Color = CireUIColors::Parchment) const
    {
        const bool bFocused = GTunerUI.Focus == Kind && GTunerUI.FocusPath == Path;
        const bool bOver = In(M, X, Y, W, H);
        P.Rect(X, Y, W, H, FLinearColor(0, 0, 0, .55f));
        const FLinearColor Edge = bFocused ? CireUIColors::ThemeGlow : bOver && bEnabled ? CireUIColors::Gold : FLinearColor(.3f, .32f, .34f, 1);
        P.Rect(X, Y, W, 1, Edge); P.Rect(X, Y + H - 1, W, 1, Edge); P.Rect(X, Y, 1, H, Edge); P.Rect(X + W - 1, Y, 1, H, Edge);
        const FString Shown = bFocused ? GTunerUI.Buffer : Value;
        const float Size = 9.f, TY = Y + (H - CireUIStyle::ReadableSize(Size) * 1.25f) * .5f;
        if (Shown.IsEmpty() && !bFocused) P.Text(Placeholder, X + 6, TY, Size, CireUIColors::Muted, ECireFont::Body);
        else
        {
            // Keep the caret end visible: show the tail when the text overflows.
            FString Visible = Shown;
            while (Visible.Len() > 1 && P.TextWidth(Visible, Size, ECireFont::Body) > W - 16) Visible.RightChopInline(1);
            P.Text(Visible, X + 6, TY, Size, Color, ECireFont::Body);
            if (bFocused && FMath::Fmod(NowSeconds(), 1.0) < .55) P.Rect(X + 7 + P.TextWidth(Visible, Size, ECireFont::Body), Y + 4, 1.5f, H - 8, CireUIColors::ThemeGlow);
        }
        if (bEnabled && HUD.HasClick() && bOver && !bFocused) { HUD.TakeClick(); CommitFocus(Controller); FocusOn(Kind, Value, Path); Click(HUD); }
    }
};

void DrawIcon(const FCtx& C, const FCireAbilityDef& D, float X, float Y, float S)
{
    const FLinearColor Tint = SchoolTint(D.School);
    C.P.Rect(X, Y, S, S, FLinearColor(0, 0, 0, .6f));
    if (UTexture2D* Tex = CireUIStyle::FindAbilityIcon(D.Id)) C.P.Tex(Tex, X + 1, Y + 1, S - 2, S - 2, FLinearColor::White);
    else CireUIStyle::Sigil(C.P, D.Id, X + 2, Y + 2, S - 4, Tint);
    const FLinearColor Rim = D.IsUltimate() ? CireUIColors::BrightGold : D.IsPassive() ? CireUIColors::Silver : Tint * .8f;
    C.P.Rect(X, Y, S, 1, Rim); C.P.Rect(X, Y + S - 1, S, 1, Rim); C.P.Rect(X, Y, 1, S, Rim); C.P.Rect(X + S - 1, Y, 1, S, Rim);
}

FCireTooltipSpec PreviewSpec(const FCireAbilityDef& D, int32 Level)
{
    const FLinearColor Tint = SchoolTint(D.School);
    const FCireAbilityStats S = CireAbilityDB::EffectiveStats(D.Id, Level);
    FCireTooltipSpec T;
    T.Icon = CireUIStyle::FindAbilityIcon(D.Id); T.Sigil = D.Id; T.IconTint = Tint;
    T.IconKind = D.IsUltimate() ? ECireSlotKind::Ultimate : D.IsPassive() ? ECireSlotKind::Passive : ECireSlotKind::Normal;
    T.Title = D.Name; T.TitleColor = D.IsUltimate() ? CireUIColors::BrightGold : FMath::Lerp(Tint, FLinearColor::White, .3f);
    T.Tag = KindLabel(D); T.Subtitle = FString::Printf(TEXT("%s  ·  %s  ·  Level %d"), *D.School.Left(1).ToUpper().Append(D.School.RightChop(1)), *D.Targeting, Level);
    T.Accent = D.IsUltimate() ? CireUIColors::BrightGold * .9f : Tint * .8f;
    const FString Cost = S.ManaCost > 0 ? FString::Printf(TEXT("%.0f Mana"), S.ManaCost) : S.EnergyCost > 0 ? FString::Printf(TEXT("%.0f Energy"), S.EnergyCost) : FString(TEXT("No cost"));
    const FString Range = S.Range > 0 ? FString::Printf(TEXT("%.1f m range"), S.Range / 100) : FString(TEXT("Self"));
    const FString Cast = D.IsPassive() ? FString(TEXT("Always active")) : S.CastTime > .05f ? FString::Printf(TEXT("%.1f sec cast"), S.CastTime) : FString(TEXT("Instant"));
    const FString Cd = S.Cooldown > 0 ? FString::Printf(TEXT("%.1f sec cooldown"), S.Cooldown) : FString();
    if (!D.IsPassive()) { T.Pair(Cost, Range, FLinearColor(.72f, .84f, 1.f, 1), FLinearColor::White); T.Pair(Cast, Cd); }
    if (S.Radius > 0) T.Pair(FString::Printf(TEXT("%.1f m radius"), S.Radius / 100), S.Duration > 0 ? FString::Printf(TEXT("%.1f sec duration"), S.Duration) : FString(), FLinearColor(.8f, .8f, .8f, 1), FLinearColor(.8f, .8f, .8f, 1));
    T.Divider();
    T.Text(CireAbilityDB::Describe(D.Id, Level), FLinearColor(1.f, .84f, .4f, 1));
    bool bHeader = false;
    for (const FCireAbilityEffect& E : D.Effects)
    {
        const FString Label = E.Label.IsEmpty() ? E.Type.ToString() : E.Label;
        if (!bHeader) { T.Divider(); T.Header(TEXT("EFFECTS"), FLinearColor(1.f, .7f, .35f, 1)); bHeader = true; }
        T.Stat(E.Duration > 0 ? FString::Printf(TEXT("%s  ·  %.1fs"), *Label, E.Duration) : Label, CireUIStyle::StatColor(Label));
    }
    if (CireAbilityTuner::IsDisabled(D.Id)) { T.Divider(); T.Text(TEXT("DISABLED in this match: hidden from shops, cannot be cast."), CireUIColors::Red); }
    T.Footer = FString::Printf(TEXT("%s  ·  cast rule: %s"), *D.Id, *D.CastRule.ToString());
    return T;
}

// ------------------------------------------------------------------------------------------------ sections
void DrawHeader(FCtx& C, float X, float Y, float W)
{
    const auto& P = C.P;
    P.Text(TEXT("ABILITY TUNER"), X + 22, Y + 12, 17, CireUIColors::TitleText, ECireFont::Display, false, true);
    const UWorld* World = C.HUD.GetWorld();
    const FString Profile = CireAbilityTuner::ActiveProfileName(World);
    const int32 Tuned = CireAbilityTuner::Active().Num(), Viewers = CireAbilityTuner::RemoteViewers(World);
    FString Why; const bool bHost = CireAbilityTuner::CanTune(C.Controller, &Why);
    const FString Status = bHost ? FString::Printf(TEXT("LIVE  ·  %d tuned  ·  profile: %s  ·  %s"), Tuned, Profile.IsEmpty() ? TEXT("hand edits") : *Profile,
        Viewers > 0 ? *FString::Printf(TEXT("replicating to %d player%s"), Viewers, Viewers == 1 ? TEXT("") : TEXT("s")) : TEXT("single player"))
        : FString::Printf(TEXT("VIEW ONLY  ·  %d tuned  ·  %s"), Tuned, *Why);
    P.Text(Status, X + 24, Y + 38, 8.5f, bHost ? CireUIColors::Teal : CireUIColors::Muted, ECireFont::Body);
    // "Allow ability tuning" game option (host).
    FString NoChange; const bool bCanFlip = CireAbilityTuner::CanChangeAllowed(C.Controller, &NoChange);
    const bool bAllowed = CireAbilityTuner::IsAllowed(World);
    const float BX = X + W - 330;
    if (C.Button(bAllowed ? TEXT("ALLOW ABILITY TUNING: ON") : TEXT("ALLOW ABILITY TUNING: OFF"), BX, Y + 14, 250, 26, bCanFlip, bAllowed ? CireUIColors::Teal : CireUIColors::Gold, bAllowed))
    { FString E; if (!CireAbilityTuner::SetAllowed(C.Controller, !bAllowed, &E)) Say(E, true); }
    C.Tip(TEXT("Allow ability tuning"), bCanFlip ? TEXT("Game option. On: the host edits abilities live for everyone in this match. Off: the tuner is read-only.") : NoChange, BX, Y + 14, 250, 26);
    if (C.Button(TEXT("CLOSE"), X + W - 72, Y + 14, 54, 26)) { CommitFocus(C.Controller); GTunerUI.bOpen = false; }
    CireUIStyle::Divider(P, X + 14, Y + 58, W - 28);
}

void DrawList(FCtx& C, float X, float Y, float W, float H)
{
    const auto& P = C.P;
    CireUIStyle::Frame(P, X, Y, W, H, CireUIColors::Gold, ECireFrame::Inset);
    C.TextBox(X + 10, Y + 10, W - 20, 26, GTunerUI.Search, EFocus::Search, TEXT(""), TEXT("Search name, id, school, tag, champion"), true);
    static const TCHAR* Filters[] = {TEXT("ALL"), TEXT("TUNED"), TEXT("OFF"), TEXT("ACT"), TEXT("ULT"), TEXT("PAS")};
    static const TCHAR* FilterHelp[] = {TEXT("Every ability"), TEXT("Abilities with overrides"), TEXT("Disabled abilities"), TEXT("Actives"), TEXT("Ultimates"), TEXT("Passives")};
    const float FW = (W - 20 - 5 * 3) / 6.f;
    for (int32 I = 0; I < 6; ++I)
    {
        const float FX = X + 10 + I * (FW + 3);
        if (C.Button(Filters[I], FX, Y + 42, FW, 22, true, CireUIColors::Gold, GTunerUI.Filter == I, 8)) { GTunerUI.Filter = I; GTunerUI.ListScroll = 0; }
        C.Tip(FilterHelp[I], TEXT("Filter the ability list."), FX, Y + 42, FW, 22);
    }
    RefreshList();
    const float RowH = 30, Top = Y + 72, ListH = H - 82;
    const int32 Visible = FMath::Max(1, FMath::FloorToInt(ListH / RowH));
    if (In(C.M, X, Top, W, ListH) && C.Controller)
    {
        if (C.Controller->WasInputKeyJustPressed(EKeys::MouseScrollUp)) GTunerUI.ListScroll -= 3;
        if (C.Controller->WasInputKeyJustPressed(EKeys::MouseScrollDown)) GTunerUI.ListScroll += 3;
    }
    GTunerUI.ListScroll = FMath::Clamp(GTunerUI.ListScroll, 0, FMath::Max(0, GTunerUI.Filtered.Num() - Visible));
    GTunerUI.LastRows = 0;
    for (int32 I = 0; I < Visible && GTunerUI.ListScroll + I < GTunerUI.Filtered.Num(); ++I)
    {
        const FString& Id = GTunerUI.Filtered[GTunerUI.ListScroll + I];
        const FCireAbilityDef* D = CireAbilityDB::Find(Id); if (!D) continue;
        const float RY = Top + I * RowH;
        const bool bSel = Id == GTunerUI.Selected, bOver = In(C.M, X + 6, RY, W - 12, RowH - 2);
        if (bSel || bOver) P.Rect(X + 6, RY, W - 12, RowH - 2, bSel ? CireUIColors::ThemeAccent * FLinearColor(1, 1, 1, .28f) : CireUIColors::Hover);
        if (bSel) P.Rect(X + 6, RY, 3, RowH - 2, CireUIColors::ThemeGlow);
        DrawIcon(C, *D, X + 12, RY + 3, 22);
        const bool bOff = CireAbilityTuner::IsDisabled(Id), bTuned = CireAbilityTuner::Find(Id) != nullptr;
        const FLinearColor NameC = bOff ? CireUIColors::Muted : D->IsUltimate() ? CireUIColors::BrightGold : CireUIColors::Parchment;
        P.Text(P.Fit(D->Name, 9, W - 110, ECireFont::Body), X + 40, RY + 6, 9, NameC, ECireFont::Body);
        if (bOff) P.Rect(X + 40, RY + 14, FMath::Min(P.TextWidth(D->Name, 9, ECireFont::Body), W - 110), 1, CireUIColors::Red);
        const FString Tag = KindLabel(*D).Left(3);
        P.Text(Tag, X + W - 44, RY + 8, 7.5f, D->IsUltimate() ? CireUIColors::BrightGold : CireUIColors::Muted, ECireFont::Heading);
        if (bTuned) { const float DX = X + W - 56, DY = RY + 14; C.P.Tri(FVector2D(DX, DY - 5), FVector2D(DX + 5, DY), FVector2D(DX - 5, DY), CireUIColors::BrightGold); C.P.Tri(FVector2D(DX - 5, DY), FVector2D(DX + 5, DY), FVector2D(DX, DY + 5), CireUIColors::BrightGold); }
        if (bOver) C.HUD.SetTooltip(D->Name, FString::Printf(TEXT("%s  ·  %s  ·  %s%s"), *D->Id, *KindLabel(*D), *D->School, bTuned ? TEXT("\nTuned in this match.") : TEXT("")));
        if (bOver && C.Clicked(X + 6, RY, W - 12, RowH - 2)) { CommitFocus(C.Controller); GTunerUI.Selected = Id; GTunerUI.FieldScroll = 0; Click(C.HUD); }
        ++GTunerUI.LastRows;
    }
    if (GTunerUI.Filtered.Num() > Visible) // scroll bar
    {
        const float Frac = static_cast<float>(Visible) / GTunerUI.Filtered.Num(), BarH = FMath::Max(24.f, ListH * Frac);
        const float BarY = Top + (ListH - BarH) * (GTunerUI.ListScroll / static_cast<float>(FMath::Max(1, GTunerUI.Filtered.Num() - Visible)));
        P.Rect(X + W - 5, Top, 2, ListH, FLinearColor(1, 1, 1, .06f)); P.Rect(X + W - 6, BarY, 4, BarH, CireUIColors::Gold * FLinearColor(1, 1, 1, .7f));
    }
    P.Text(FString::Printf(TEXT("%d / %d abilities"), GTunerUI.Filtered.Num(), CireAbilityDB::All().Num()), X + 12, Y + H - 16, 7.5f, CireUIColors::Muted, ECireFont::Body);
}

void DrawEditor(FCtx& C, float X, float Y, float W, float H)
{
    const auto& P = C.P;
    CireUIStyle::Frame(P, X, Y, W, H, CireUIColors::Gold, ECireFrame::Inset);
    const FCireAbilityDef* D = CireAbilityDB::Find(GTunerUI.Selected);
    if (!D)
    {
        P.Wrapped(TEXT("Select an ability on the left. Every number of its row appears here with a slider and a typed value; changes apply to the running match at once."), X + 24, Y + 40, W - 48, 10, CireUIColors::Muted, 6);
        return;
    }
    const TSharedPtr<FJsonObject> Row = CireAbilityTuner::FileRow(D->Id);
    FString FileName, FileText; if (Row) { Row->TryGetStringField(TEXT("name"), FileName); Row->TryGetStringField(TEXT("description"), FileText); }
    FCireAbilityOverride O = CurrentOverride(D->Id);
    const bool bEdit = C.bCanEdit;
    // Identity: icon, name, description, enabled.
    DrawIcon(C, *D, X + 14, Y + 12, 44);
    P.Text(TEXT("NAME"), X + 68, Y + 10, 7.5f, CireUIColors::Gold, ECireFont::Heading);
    C.TextBox(X + 68, Y + 22, W - 196, 24, D->Name, EFocus::Name, TEXT(""), FileName, bEdit, O.Name.IsSet() ? CireUIColors::BrightGold : CireUIColors::Parchment);
    const bool bEnabled = !CireAbilityTuner::IsDisabled(D->Id);
    if (C.Button(bEnabled ? TEXT("ENABLED") : TEXT("DISABLED"), X + W - 120, Y + 22, 106, 24, bEdit, bEnabled ? CireUIColors::Teal : CireUIColors::Red, !bEnabled))
    { if (bEnabled) O.bEnabled = false; else O.bEnabled.Reset(); if (Commit(C.Controller, D->Id, O)) Say(bEnabled ? D->Name + TEXT(" disabled: gone from every shop, casts refused.") : D->Name + TEXT(" enabled.")); }
    C.Tip(TEXT("Enabled"), TEXT("Disabled abilities leave the Skill Shop and champion offers, and casts are refused."), X + W - 120, Y + 22, 106, 24);
    P.Text(TEXT("TOOLTIP TEXT  ({effect} = scaled number)"), X + 68, Y + 52, 7.5f, CireUIColors::Gold, ECireFont::Heading);
    C.TextBox(X + 14, Y + 64, W - 28, 24, O.Description.Get(FileText), EFocus::Description, TEXT(""), FileText, bEdit, O.Description.IsSet() ? CireUIColors::BrightGold : CireUIColors::Parchment);
    // Field list.
    const TArray<FCireTunerField>& Fields = FieldsOf(D->Id);
    struct FLine { const FCireTunerField* F = nullptr; FString Group; };
    TArray<FLine> Lines; FString Last;
    for (const FCireTunerField& F : Fields) { if (F.Group != Last) { Lines.Add({nullptr, F.Group}); Last = F.Group; } Lines.Add({&F, FString()}); }
    const float Top = Y + 98, ListH = H - 140, LineH = 38;
    const int32 Visible = FMath::Max(1, FMath::FloorToInt(ListH / LineH));
    if (In(C.M, X, Top, W, ListH) && C.Controller && GTunerUI.DragPath.IsEmpty())
    {
        if (C.Controller->WasInputKeyJustPressed(EKeys::MouseScrollUp)) GTunerUI.FieldScroll -= 2;
        if (C.Controller->WasInputKeyJustPressed(EKeys::MouseScrollDown)) GTunerUI.FieldScroll += 2;
    }
    GTunerUI.FieldScroll = FMath::Clamp(GTunerUI.FieldScroll, 0, FMath::Max(0, Lines.Num() - Visible));
    if (!C.bMouseDown) GTunerUI.DragPath.Reset();
    for (int32 I = 0; I < Visible && GTunerUI.FieldScroll + I < Lines.Num(); ++I)
    {
        const FLine& L = Lines[GTunerUI.FieldScroll + I];
        const float LY = Top + I * LineH;
        if (!L.F) { CireUIStyle::Header(P, X + 14, LY + 10, W - 28, L.Group.ToUpper(), CireUIColors::Gold, 9); continue; }
        const FCireTunerField& F = *L.F;
        const FLinearColor ValueC = F.bOverridden ? CireUIColors::BrightGold : CireUIColors::Parchment;
        P.Text(P.Fit(F.Label, 9, W - 230, ECireFont::Body), X + 18, LY + 4, 9, CireUIColors::Parchment, ECireFont::Body);
        if (F.bOverridden) P.Text(FString::Printf(TEXT("default %s"), *Num(F.Default, F.Step)), X + W - 212, LY + 5, 7.5f, CireUIColors::Muted, ECireFont::Body);
        C.TextBox(X + W - 132, LY + 1, 66, 20, Num(F.Value, F.Step), EFocus::Field, F.Path, TEXT(""), bEdit, ValueC);
        C.Tip(F.Label, F.Help + FString::Printf(TEXT("\nRow path: %s\nDefault %s. Slider %s-%s; type any value."), *F.Path, *Num(F.Default, F.Step), *Num(F.Min, F.Step), *Num(F.Max, F.Step)), X + 14, LY, W - 150, 18);
        if (F.bOverridden && C.Button(TEXT("RESET"), X + W - 60, LY + 1, 46, 20, bEdit, CireUIColors::Gold, false, 7.5f))
        { O.Fields.Remove(F.Path); if (Commit(C.Controller, D->Id, O)) Say(F.Label + TEXT(" back to default.")); }
        const float SX = X + 18, SY = LY + 25, SW = W - 36;
        const bool bOverSlider = In(C.M, SX - 4, SY - 8, SW + 8, 18);
        CireUIStyle::Slider(P, SX, SY, SW, static_cast<float>(FMath::Clamp((F.Value - F.Min) / FMath::Max(1e-9, F.Max - F.Min), 0.0, 1.0)), bEdit, bOverSlider || GTunerUI.DragPath == F.Path);
        if (bEdit && C.bMouseDown && ((bOverSlider && GTunerUI.DragPath.IsEmpty() && GTunerUI.Focus == EFocus::None) || GTunerUI.DragPath == F.Path))
        {
            GTunerUI.DragPath = F.Path; C.HUD.TakeClick();
            const double Frac = FMath::Clamp((C.M.X - SX) / SW, 0.0, 1.0);
            double V = FMath::RoundToDouble((F.Min + (F.Max - F.Min) * Frac) / F.Step) * F.Step;
            if (F.bInteger) V = FMath::RoundToDouble(V);
            if (!FMath::IsNearlyEqual(V, F.Value, F.Step * .01))
            {
                if (FMath::IsNearlyEqual(V, F.Default, F.Step * .5)) { V = F.Default; O.Fields.Remove(F.Path); } else O.Fields.Add(F.Path, V);
                Commit(C.Controller, D->Id, O);
            }
        }
    }
    if (Lines.Num() > Visible)
    {
        const float Frac = static_cast<float>(Visible) / Lines.Num(), BarH = FMath::Max(24.f, ListH * Frac);
        const float BarY = Top + (ListH - BarH) * (GTunerUI.FieldScroll / static_cast<float>(FMath::Max(1, Lines.Num() - Visible)));
        P.Rect(X + W - 5, Top, 2, ListH, FLinearColor(1, 1, 1, .06f)); P.Rect(X + W - 6, BarY, 4, BarH, CireUIColors::Gold * FLinearColor(1, 1, 1, .7f));
    }
    // Footer: reset this ability.
    const float FY = Y + H - 36;
    CireUIStyle::Divider(P, X + 10, FY - 6, W - 20);
    P.Text(FString::Printf(TEXT("%d numbers  ·  %d tuned"), Fields.Num(), O.Fields.Num()), X + 16, FY + 6, 8, CireUIColors::Muted, ECireFont::Body);
    if (C.Button(TEXT("RESET THIS ABILITY"), X + W - 176, FY, 162, 26, bEdit && !O.IsEmpty()))
    { if (Commit(C.Controller, D->Id, FCireAbilityOverride())) Say(D->Name + TEXT(": every field back to the file values.")); }
}

void DrawSide(FCtx& C, float X, float Y, float W, float H)
{
    const auto& P = C.P;
    const FCireAbilityDef* D = CireAbilityDB::Find(GTunerUI.Selected);
    CireUIStyle::Frame(P, X, Y, W, H, CireUIColors::Gold, ECireFrame::Inset);
    P.Text(TEXT("LIVE TOOLTIP"), X + 14, Y + 10, 9, CireUIColors::Gold, ECireFont::Heading);
    static const int32 Levels[] = {1, 5, 10, 15};
    for (int32 I = 0; I < 4; ++I)
        if (C.Button(FString::Printf(TEXT("LV %d"), Levels[I]), X + W - 186 + I * 44, Y + 6, 41, 20, true, CireUIColors::Gold, GTunerUI.PreviewLevel == Levels[I], 7.5f)) GTunerUI.PreviewLevel = Levels[I];
    const float VfxH = 150, TipH = H - 36 - VfxH;
    if (D) CireUIStyle::RichTooltip(P, X + 10, Y + 32, W - 20, PreviewSpec(*D, GTunerUI.PreviewLevel), .92f, .96f, true, TipH - 6);
    // VFX scale / tint + test helpers.
    const float VY = Y + H - VfxH;
    CireUIStyle::Divider(P, X + 10, VY - 4, W - 20);
    P.Text(TEXT("VFX"), X + 14, VY + 4, 9, CireUIColors::Gold, ECireFont::Heading);
    if (!D) return;
    FCireAbilityOverride O = CurrentOverride(D->Id);
    const bool bEdit = C.bCanEdit;
    const float Scale = O.VfxScale.Get(1.f);
    P.Text(FString::Printf(TEXT("Scale  x%.2f"), Scale), X + 14, VY + 22, 8.5f, O.VfxScale.IsSet() ? CireUIColors::BrightGold : CireUIColors::Parchment, ECireFont::Body);
    const float SX = X + 110, SW = W - 124;
    const bool bOverScale = In(C.M, SX - 4, VY + 18, SW + 8, 18);
    CireUIStyle::Slider(P, SX, VY + 27, SW, (Scale - .25f) / 2.75f, bEdit, bOverScale || GTunerUI.DragPath == TEXT("vfx.scale"));
    if (bEdit && C.bMouseDown && ((bOverScale && GTunerUI.DragPath.IsEmpty() && GTunerUI.Focus == EFocus::None) || GTunerUI.DragPath == TEXT("vfx.scale")))
    {
        GTunerUI.DragPath = TEXT("vfx.scale"); C.HUD.TakeClick();
        const float V = FMath::RoundToFloat((.25f + 2.75f * FMath::Clamp((C.M.X - SX) / SW, 0.f, 1.f)) * 20.f) / 20.f;
        if (!FMath::IsNearlyEqual(V, Scale)) { if (FMath::IsNearlyEqual(V, 1.f)) O.VfxScale.Reset(); else O.VfxScale = V; Commit(C.Controller, D->Id, O); }
    }
    // Hue strip.
    const bool bTint = O.VfxTint.IsSet();
    const float Hue = bTint ? O.VfxTint->LinearRGBToHSV().R : 0.f;
    P.Text(TEXT("Tint"), X + 14, VY + 50, 8.5f, bTint ? CireUIColors::BrightGold : CireUIColors::Parchment, ECireFont::Body);
    const float HX = X + 110, HY = VY + 50, HW = W - 124, HH = 14;
    for (int32 I = 0; I < 36; ++I) P.Rect(HX + HW * I / 36.f, HY, HW / 36.f + .5f, HH, FLinearColor(I * 10.f, .85f, 1.f).HSVToLinearRGB() * FLinearColor(1, 1, 1, bTint ? 1.f : .45f));
    if (bTint) { const float KX = HX + HW * Hue / 360.f; P.Rect(KX - 2, HY - 3, 4, HH + 6, FLinearColor::White); }
    const bool bOverHue = In(C.M, HX, HY - 3, HW, HH + 6);
    C.Tip(TEXT("VFX tint"), TEXT("Recolours the ability's pack VFX (hue; white flashes and smoke keep their read). NONE restores the art's own colours."), HX, HY, HW, HH);
    if (bEdit && C.bMouseDown && ((bOverHue && GTunerUI.DragPath.IsEmpty() && GTunerUI.Focus == EFocus::None) || GTunerUI.DragPath == TEXT("vfx.hue")))
    {
        GTunerUI.DragPath = TEXT("vfx.hue"); C.HUD.TakeClick();
        const float H2 = FMath::RoundToFloat(FMath::Clamp((C.M.X - HX) / HW, 0.f, 1.f) * 72.f) * 5.f;
        const FLinearColor Want = FLinearColor(H2, .85f, 1.f).HSVToLinearRGB();
        if (!bTint || !FMath::IsNearlyEqual(H2, Hue, 1.f)) { O.VfxTint = Want; Commit(C.Controller, D->Id, O); }
    }
    if (bTint && C.Button(TEXT("NONE"), X + 14, VY + 68, 60, 18, bEdit, CireUIColors::Gold, false, 7.5f)) { O.VfxTint.Reset(); Commit(C.Controller, D->Id, O); }
    // Test helpers.
    const float BY = VY + 96, BW = (W - 34) / 2;
    FString Why;
    if (C.Button(TEXT("CAST IT NOW"), X + 12, BY, BW, 26, bEdit && !D->IsPassive(), CireUIColors::Teal))
    { if (CireAbilityTuner::CastNow(C.Controller, D->Id, &Why)) Say(D->Name + TEXT(" cast (cost and cooldown refunded).")); else Say(Why, true); }
    C.Tip(TEXT("Cast it now"), TEXT("Casts the selected ability at your target (or ahead of you) with full mana and no cooldown. Unknown skills are added to your bar for the test."), X + 12, BY, BW, 26);
    if (C.Button(TEXT("TARGET DUMMY"), X + 22 + BW, BY, BW, 26, bEdit, CireUIColors::Gold))
    { if (CireAbilityTuner::SpawnTargetDummy(C.Controller, &Why)) Say(TEXT("Target dummy spawned and targeted.")); else Say(Why, true); }
    C.Tip(TEXT("Target dummy"), TEXT("Spawns an immobile, harmless monster with a million health in front of you and targets it."), X + 22 + BW, BY, BW, 26);
}

void DrawProfiles(FCtx& C, float X, float Y, float W)
{
    const auto& P = C.P;
    CireUIStyle::Divider(P, X + 14, Y - 6, W - 28);
    P.Text(TEXT("PROFILE"), X + 22, Y + 8, 8, CireUIColors::Gold, ECireFont::Heading);
    C.TextBox(X + 84, Y + 2, 200, 24, GTunerUI.ProfileName, EFocus::Profile, TEXT(""), TEXT("Profile name"), true);
    const UWorld* World = C.HUD.GetWorld();
    FString Why; const bool bHost = CireAbilityTuner::CanTune(C.Controller, &Why);
    float BX = X + 294;
    auto Btn = [&](const FString& L, float BW, bool bOn, const FString& Help) { const bool b = C.Button(L, BX, Y + 2, BW, 24, bOn, CireUIColors::Gold, false, 8); C.Tip(L, Help, BX, Y + 2, BW, 24); BX += BW + 6; return b; };
    const FString Path = CireAbilityTuner::DefaultProfilesPath();
    if (Btn(TEXT("SAVE"), 64, bHost, FString::Printf(TEXT("Saves the current tuning as \"%s\" in %s."), *GTunerUI.ProfileName, *Path)))
    {
        FCireTuningProfile Pr; Pr.Name = GTunerUI.ProfileName; Pr.Abilities = CireAbilityTuner::Active(); FString E;
        if (CireAbilityTuner::SaveProfile(Path, Pr, &E)) Say(FString::Printf(TEXT("Saved \"%s\" (%d abilities)."), *Pr.Name, Pr.Abilities.Num())); else Say(E, true);
    }
    const TArray<FCireTuningProfile> Profiles = CireAbilityTuner::AllProfiles();
    if (Btn(TEXT("LOAD"), 64, bHost && Profiles.Num() > 0, TEXT("Cycles through saved profiles and applies the next one live.")))
    {
        GTunerUI.ProfileCursor = (GTunerUI.ProfileCursor + 1) % FMath::Max(1, Profiles.Num());
        const FCireTuningProfile& Pr = Profiles[GTunerUI.ProfileCursor]; FString E;
        if (CireAbilityTuner::RequestSet(C.Controller, Pr.Abilities, Pr.Name, &E)) { GTunerUI.ProfileName = Pr.Name; Say(FString::Printf(TEXT("Loaded \"%s\" (%d abilities)."), *Pr.Name, Pr.Abilities.Num())); }
        else Say(E, true);
    }
    if (Btn(TEXT("STARTUP"), 78, bHost, TEXT("Makes the saved profile of this name load with the ability database (every match). Clear it with an empty name.")))
    { FString E; if (CireAbilityTuner::SetStartupProfile(Path, GTunerUI.ProfileName, &E)) Say(FString::Printf(TEXT("\"%s\" loads at startup from now on."), *GTunerUI.ProfileName)); else Say(E, true); }
    if (Btn(TEXT("EXPORT"), 72, true, FString::Printf(TEXT("Writes the current tuning to %s/<name>.json to share."), *CireAbilityTuner::ExportDirectory())))
    {
        FCireTuningProfile Pr; Pr.Name = GTunerUI.ProfileName; Pr.Abilities = CireAbilityTuner::Active(); FString E;
        const FString File = FPaths::Combine(CireAbilityTuner::ExportDirectory(), FPaths::MakeValidFileName(Pr.Name) + TEXT(".json"));
        if (CireAbilityTuner::ExportProfile(Pr, File, &E)) Say(TEXT("Exported ") + File); else Say(E, true);
    }
    const TArray<FString> Files = CireAbilityTuner::ImportCandidates();
    if (Btn(TEXT("IMPORT"), 72, bHost && Files.Num() > 0, FString::Printf(TEXT("Cycles through profile files in %s (and Exports) and applies the next one live."), *CireAbilityTuner::ImportDirectory())))
    {
        GTunerUI.ImportCursor = (GTunerUI.ImportCursor + 1) % FMath::Max(1, Files.Num());
        FCireTuningProfile Pr; FString E;
        if (CireAbilityTuner::ImportProfile(Files[GTunerUI.ImportCursor], Pr, &E) && CireAbilityTuner::RequestSet(C.Controller, Pr.Abilities, Pr.Name, &E))
        { GTunerUI.ProfileName = Pr.Name; Say(FString::Printf(TEXT("Imported \"%s\" from %s."), *Pr.Name, *FPaths::GetCleanFilename(Files[GTunerUI.ImportCursor]))); }
        else Say(E, true);
    }
    const bool bArmed = NowSeconds() - GTunerUI.ResetAllArmedAt < 3.0;
    if (Btn(bArmed ? TEXT("CONFIRM RESET") : TEXT("RESET ALL"), 110, bHost && !CireAbilityTuner::Active().IsEmpty(), TEXT("Every ability back to the file values (click twice).")))
    {
        if (!bArmed) GTunerUI.ResetAllArmedAt = NowSeconds();
        else { FString E; GTunerUI.ResetAllArmedAt = -100; if (CireAbilityTuner::RequestSet(C.Controller, FCireTuningSet(), FString(), &E)) Say(TEXT("All abilities reset.")); else Say(E, true); }
    }
    (void)World;
    // Message line.
    if (NowSeconds() - GTunerUI.MessageAt < 8.0 && !GTunerUI.Message.IsEmpty())
        P.Text(P.Fit(GTunerUI.Message, 8.5f, W - 40, ECireFont::Body), X + 22, Y + 32, 8.5f, GTunerUI.bMessageError ? CireUIColors::Red : CireUIColors::Teal, ECireFont::Body);
}
} // namespace

// ================================================================================================= API
void CireAbilityTunerUI::Toggle(ACireController* Controller)
{
    if (GTunerUI.bOpen) { CommitFocus(Controller); GTunerUI.bOpen = false; return; }
    GTunerUI.bOpen = true;
    if (GTunerUI.Selected.IsEmpty() && CireAbilityDB::All().Num() > 0) { RefreshList(); if (GTunerUI.Filtered.Num()) GTunerUI.Selected = GTunerUI.Filtered[0]; }
}
bool CireAbilityTunerUI::IsOpen() { return GTunerUI.bOpen; }
void CireAbilityTunerUI::Close() { GTunerUI.bOpen = false; GTunerUI.Focus = EFocus::None; GTunerUI.DragPath.Reset(); }
bool CireAbilityTunerUI::ContainsPoint(FVector2D L) { return GTunerUI.bOpen && In(L, GTunerUI.Rect.X, GTunerUI.Rect.Y, GTunerUI.Rect.W, GTunerUI.Rect.H); }
bool CireAbilityTunerUI::OwnsKeyboard() { return GTunerUI.bOpen && GTunerUI.Focus != EFocus::None; }
bool CireAbilityTunerUI::HandleChar(TCHAR Ch)
{
    if (!OwnsKeyboard()) return false;
    if (Ch < 32 || Ch == 127) return true;
    const int32 Max = GTunerUI.Focus == EFocus::Description ? 600 : GTunerUI.Focus == EFocus::Field ? 14 : GTunerUI.Focus == EFocus::Search ? 32 : 48;
    if (GTunerUI.Focus == EFocus::Field && !(FChar::IsDigit(Ch) || Ch == TEXT('.') || Ch == TEXT('-'))) return true;
    if (GTunerUI.Buffer.Len() < Max) GTunerUI.Buffer.AppendChar(Ch);
    if (GTunerUI.Focus == EFocus::Search) { GTunerUI.Search = GTunerUI.Buffer; GTunerUI.ListScroll = 0; }
    return true;
}
void CireAbilityTunerUI::TickKeys(ACireController* Controller)
{
    if (!Controller || !OwnsKeyboard()) return;
    if (Controller->WasInputKeyJustPressed(EKeys::BackSpace) && !GTunerUI.Buffer.IsEmpty())
    { GTunerUI.Buffer.LeftChopInline(1); if (GTunerUI.Focus == EFocus::Search) GTunerUI.Search = GTunerUI.Buffer; }
    if (Controller->WasInputKeyJustPressed(EKeys::Enter) || Controller->WasInputKeyJustPressed(EKeys::Tab)) CommitFocus(Controller);
    else if (Controller->WasInputKeyJustPressed(EKeys::Escape)) { if (GTunerUI.Focus == EFocus::Search) { GTunerUI.Search.Reset(); } GTunerUI.Focus = EFocus::None; }
}
bool CireAbilityTunerUI::HandleEscape()
{
    if (!GTunerUI.bOpen) return false;
    if (GTunerUI.Focus != EFocus::None) { GTunerUI.Focus = EFocus::None; return true; }
    GTunerUI.bOpen = false; return true;
}
void CireAbilityTunerUI::Select(const FString& AbilityId) { GTunerUI.bOpen = true; GTunerUI.Selected = AbilityId; GTunerUI.FieldScroll = 0; }
FString CireAbilityTunerUI::Selected() { return GTunerUI.Selected; }
int32 CireAbilityTunerUI::LastListRows() { return GTunerUI.LastRows; }

void CireAbilityTunerUI::Draw(ACireHUD& HUD, ACireHero* Hero, ACireController* Controller)
{
    if (!GTunerUI.bOpen || !HUD.IsInteractive()) { GTunerUI.Rect = {0, 0, 0, 0}; return; }
    const FVector2D View = HUD.LogicalViewport();
    const float W = FMath::Min(1120.f, View.X - 24), H = FMath::Min(680.f, View.Y - 40);
    const float X = FMath::RoundToFloat((View.X - W) * .5f), Y = FMath::RoundToFloat((View.Y - H) * .5f);
    GTunerUI.Rect = {X, Y, W, H};
    FCtx C{HUD, Controller, HUD.ScreenPainter(), HUD.LogicalMouse(), CireAbilityTuner::CanTune(Controller), Controller && Controller->IsInputKeyDown(EKeys::LeftMouseButton)};
    // Click outside every text box commits the focused one.
    if (GTunerUI.Focus != EFocus::None && HUD.HasClick() && !In(C.M, X, Y, W, H)) CommitFocus(Controller);
    C.P.Rect(0, 0, View.X, View.Y, FLinearColor(0, 0, 0, .35f));
    CireUIStyle::Frame(C.P, X, Y, W, H, CireUIColors::Gold, ECireFrame::Panel);
    CireUIStyle::Ornament(C.P, X + W * .5f, Y, 26);
    DrawHeader(C, X, Y, W);
    const float BodyY = Y + 66, BodyH = H - 66 - 64;
    const float ListW = 286, SideW = 322, EdW = W - ListW - SideW - 48;
    DrawList(C, X + 14, BodyY, ListW, BodyH);
    DrawEditor(C, X + 24 + ListW, BodyY, EdW, BodyH);
    DrawSide(C, X + 34 + ListW + EdW, BodyY, SideW, BodyH);
    DrawProfiles(C, X, Y + H - 58, W);
    // Swallow clicks that land on the window's empty space (never reach the world).
    if (HUD.HasClick() && In(C.M, X, Y, W, H)) { HUD.TakeClick(); if (GTunerUI.Focus != EFocus::None) CommitFocus(Controller); }
    (void)Hero;
}
