// kit-editor: the Skill Assignment editor screen (Champion Select > KIT EDITOR). Three columns:
//   ABILITY POOL  : every Ability Database row, grouped in the Skill Shop's periodic-table sections, search + filters.
//   PREVIEW       : the champion's real in-game body on the draft stage, the selected ability's cast effect looping live
//                   at its placement, CAST PREVIEW (cast clip + effect on the release frame), drag to turn.
//   KIT + EFFECT  : 6 active slots, the ultimate and the passive; start-with-kit toggle; SAVE / REVERT / CLEAR;
//                   the selected ability's placement (attach point, offset, scale, tint).
// Drawn with the shared style kit (CireUIStyle) so it follows the HUD theme. Docs/KitEditor.md.
#include "CireKitEditor.h"
#include "CireAbilityDB.h"
#include "CireAbilityIcons.h"
#include "CireAbilityVFX.h"
#include "CireChampionActions.h"
#include "CireChampionProfiles.h"
#include "CireChampionRoster.h"
#include "CireDraftStage.h"
#include "CireFabVFX.h"
#include "CireGame.h"
#include "CireHUD.h"
#include "CireScalingKits.h"
#include "CireUIStyle.h"
#include "Components/SkeletalMeshComponent.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Particles/ParticleSystemComponent.h"
#include "NiagaraComponent.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireKitEditorUI, Log, All);

namespace
{
using namespace CireUIColors;

struct FKitAnchorChip { const TCHAR* Key; const TCHAR* Label; };
const FKitAnchorChip KitAnchorChips[] = {
    {TEXT(""), TEXT("DEFAULT")}, {TEXT("root"), TEXT("FEET")}, {TEXT("pelvis"), TEXT("PELVIS")}, {TEXT("chest"), TEXT("CHEST")},
    {TEXT("head"), TEXT("HEAD")}, {TEXT("hand_r"), TEXT("R HAND")}, {TEXT("hand_l"), TEXT("L HAND")}, {TEXT("foot_r"), TEXT("R FOOT")},
};
const FLinearColor KitSwatches[] = {
    FLinearColor(1.f, .32f, .12f, 1), FLinearColor(1.f, .72f, .18f, 1), FLinearColor(.98f, .95f, .55f, 1), FLinearColor(.35f, 1.f, .38f, 1),
    FLinearColor(.18f, .92f, .88f, 1), FLinearColor(.25f, .5f, 1.f, 1), FLinearColor(.62f, .32f, 1.f, 1), FLinearColor(1.f, .3f, .78f, 1),
    FLinearColor(.9f, .08f, .1f, 1), FLinearColor(.92f, .94f, 1.f, 1),
};

struct FKitEditorState
{
    bool bOpen = false;
    FString Champion;
    FCireKitTemplate Draft, Saved;
    FString Selected;                        // kit ability whose effect is being placed
    CireKitEditor::FPoolFilter Filter;
    bool bOnlyChampion = false;
    int32 PoolRow = 0, StripFirst = 0, BoneCursor = -1;
    TWeakObjectPtr<ACireDraftStage> Stage;
    TWeakObjectPtr<UFXSystemComponent> Live;
    FString LiveKey;
    double LiveAt = -100, CastAt = -100, CastReleaseAt = -1;
    float Yaw = -20.f;
    bool bRotating = false;
    float RotateFromX = 0, RotateFromYaw = 0;
    int32 DragSlider = -1;
    FString Status;
    FLinearColor StatusColor = Parchment;
    double StatusAt = -100;
    FString PendingSwitch;                   // unsaved-changes confirmation
    FString SavedSearch;                     // champion-select search, restored on close
    bool bGallery = false, bGalleryDone = false;
    double GalleryStart = 0;
    int32 GalleryStage = 0;
    double GalleryShotAt = 0;
};
TMap<TWeakObjectPtr<const ACireHUD>, FKitEditorState> GKitEditorStates;
FKitEditorState& KitState(const ACireHUD* HUD)
{
    for (auto It = GKitEditorStates.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
    return GKitEditorStates.FindOrAdd(HUD);
}

FString KitKindWord(CireKitEditor::EKind K)
{
    return K == CireKitEditor::EKind::Ultimate ? TEXT("ULTIMATE") : K == CireKitEditor::EKind::Passive ? TEXT("PASSIVE") : TEXT("ACTIVE");
}
FLinearColor KitKindColor(CireKitEditor::EKind K)
{
    return K == CireKitEditor::EKind::Ultimate ? FLinearColor(.78f, .56f, 1.f, 1) : K == CireKitEditor::EKind::Passive ? FLinearColor(.84f, .80f, .68f, 1) : Gold;
}
FLinearColor KitRoleColor(const FCireChampionProfile& P)
{
    const auto Role = CireChampionProfiles::PrimaryRole(P);
    return Role == Cires::SkillDraftRole::Tank ? FLinearColor(.36f, .58f, .89f, 1) : Role == Cires::SkillDraftRole::Support ? FLinearColor(.35f, .78f, .49f, 1) : FLinearColor(.85f, .31f, .25f, 1);
}

FCireKitTemplate KitLoad(const FString& Champion)
{
    FCireKitTemplate T;
    if (const FCireKitTemplate* Saved = CireKitEditor::Find(Champion)) T = *Saved;
    T.ChampionId = Champion;
    T.BaseKit = CireKitEditor::Normalize(T.BaseKit);
    T.Updated.Reset();
    return T;
}
bool KitDirty(const FKitEditorState& S)
{
    FCireKitTemplate A = S.Draft, B = S.Saved;
    A.BaseKit = CireKitEditor::Normalize(A.BaseKit); B.BaseKit = CireKitEditor::Normalize(B.BaseKit);
    A.Updated.Reset(); B.Updated.Reset();
    for (auto It = A.Effects.CreateIterator(); It; ++It) if (It.Value().IsDefault()) It.RemoveCurrent();
    for (auto It = B.Effects.CreateIterator(); It; ++It) if (It.Value().IsDefault()) It.RemoveCurrent();
    return !(A == B);
}
void KitSetStatus(FKitEditorState& S, const FString& Text, FLinearColor Color)
{
    S.Status = Text; S.StatusColor = Color; S.StatusAt = FPlatformTime::Seconds();
}
void KitSelectChampion(FKitEditorState& S, const FString& Id)
{
    S.Champion = Id;
    S.Saved = KitLoad(Id);
    S.Draft = S.Saved;
    S.Selected = S.Draft.BaseKit.Num() ? S.Draft.BaseKit[0] : FString();
    S.PendingSwitch.Reset();
    S.BoneCursor = -1;
    S.LiveKey.Reset();
    if (UFXSystemComponent* C = S.Live.Get()) C->DestroyComponent();
    S.Live.Reset();
}

FCireTooltipSpec KitAbilityTip(const FCireAbilityDef& Def, const FString& Footer)
{
    FCireTooltipSpec Spec;
    Spec.Icon = CireUIStyle::FindAbilityIcon(Def.Id);
    Spec.Sigil = Def.Id;
    Spec.IconTint = CireAbilityIcons::Accent(Def.Id);
    const auto Kind = CireKitEditor::KindOf(Def.Id);
    Spec.IconKind = Kind == CireKitEditor::EKind::Ultimate ? ECireSlotKind::Ultimate : Kind == CireKitEditor::EKind::Passive ? ECireSlotKind::Passive : ECireSlotKind::Normal;
    Spec.Title = Def.Name;
    Spec.Tag = KitKindWord(Kind);
    Spec.TagColor = KitKindColor(Kind);
    Spec.Subtitle = FString::Printf(TEXT("%s  ·  %s"), *(Def.School.IsEmpty() ? FString(TEXT("Physical")) : Def.School), *FString::Join(Def.Types, TEXT(" / ")));
    Spec.Accent = KitKindColor(Kind);
    Spec.Text(CireAbilityDB::Describe(Def.Id, 1));
    if (Def.EffectTags.Num()) Spec.Divider().Text(FString::Join(Def.EffectTags, TEXT("  ·  ")), FLinearColor(.72f, .80f, .95f, 1));
    if (Def.Champions.Num()) Spec.Text(FString::Printf(TEXT("Learnable today by %d champion%s"), Def.Champions.Num(), Def.Champions.Num() == 1 ? TEXT("") : TEXT("s")), Muted);
    Spec.Footer = Footer;
    return Spec;
}

void KitDrawTarget(const FCireUIPainter& P, UTextureRenderTarget2D* Target, float X, float Y, float W, float H)
{
    if (!P.Canvas || !Target || !Target->GetResource()) return;
    FCanvasTileItem Item(P.ToScreen(X, Y), Target->GetResource(), FVector2D(W * P.Scale, H * P.Scale), FLinearColor::White);
    Item.BlendMode = SE_BLEND_Opaque;
    P.Canvas->DrawItem(Item);
}
void KitBorder(const FCireUIPainter& P, float X, float Y, float W, float H, FLinearColor C, float T = 1.f)
{
    P.Rect(X, Y, W, T, C); P.Rect(X, Y + H - T, W, T, C); P.Rect(X, Y, T, H, C); P.Rect(X + W - T, Y, T, H, C);
}
}

// ------------------------------------------------------------------ open / close
bool CireKitEditor::IsOpen(const ACireHUD* HUD)
{
    if (!HUD || !IsAvailable()) return false;
    const FKitEditorState* S = GKitEditorStates.Find(HUD);
#if !UE_BUILD_SHIPPING
    if ((!S || !S->bOpen) && FParse::Param(FCommandLine::Get(), TEXT("CireKitEditorGallery")))
    {
        FKitEditorState& G = KitState(HUD);
        if (!G.bGalleryDone) { G.bGallery = true; G.bOpen = true; return true; }
    }
#endif
    return S && S->bOpen;
}

void CireKitEditor::Open(ACireHUD* HUD, bool bOpen, const FString& ChampionId)
{
    if (!HUD || !IsAvailable()) return;
    FKitEditorState& S = KitState(HUD);
    ACireController* Controller = Cast<ACireController>(HUD->GetOwningPlayerController());
    if (bOpen && !S.bOpen)
    {
        S.bOpen = true;
        if (Controller) { S.SavedSearch = Controller->DraftSearch; Controller->DraftSearch.Reset(); Controller->bDraftSearch = false; }
        const FString Want = !ChampionId.IsEmpty() && CireChampionRoster::Find(ChampionId) ? ChampionId
            : !S.Champion.IsEmpty() ? S.Champion : CireChampionRoster::Count() ? CireChampionRoster::All()[0].Id : FString();
        if (Want != S.Champion || S.Saved.ChampionId.IsEmpty()) KitSelectChampion(S, Want);
        HUD->PlayInterfaceSound(4, .5f);
    }
    else if (!bOpen && S.bOpen)
    {
        S.bOpen = false;
        if (Controller) { Controller->DraftSearch = S.SavedSearch; Controller->bDraftSearch = false; }
        if (UFXSystemComponent* C = S.Live.Get()) C->DestroyComponent();
        S.Live.Reset(); S.LiveKey.Reset();
        if (ACireDraftStage* Stage = S.Stage.Get()) Stage->Destroy();
        S.Stage.Reset();
    }
}

// ------------------------------------------------------------------ the screen
void CireKitEditor::Draw(ACireHUD& HUD, ACireHero* Hero, ACireController* Controller)
{
    FKitEditorState& S = KitState(&HUD);
    UWorld* World = HUD.GetWorld();
    APlayerController* PC = HUD.GetOwningPlayerController();
    if (!World || !Hero) return;
    if (S.Champion.IsEmpty() && CireChampionRoster::Count()) KitSelectChampion(S, CireChampionRoster::All()[0].Id);
    const FCireChampionProfile* Profile = CireChampionRoster::Find(S.Champion);
    const double Now = FPlatformTime::Seconds();
    const float Time = static_cast<float>(Now);
    FCireUIPainter P = HUD.ScreenPainter();
    const FVector2D View = HUD.LogicalViewport();
    const float VW = View.X, VH = View.Y;
    FVector2D M = HUD.LogicalMouse();
    const bool bInteractive = HUD.IsInteractive() && !S.bGallery;
    if (!bInteractive) M = FVector2D(-1000, -1000);
    const auto In = [&](float X, float Y, float W, float H) { return M.X >= X && M.X < X + W && M.Y >= Y && M.Y < Y + H; };
    const auto Click = [&](float X, float Y, float W, float H) { if (!bInteractive || !HUD.HasClick() || !In(X, Y, W, H)) return false; HUD.TakeClick(); return true; };
    const bool bRight = bInteractive && PC && PC->WasInputKeyJustPressed(EKeys::RightMouseButton);
    const bool bMouseDown = bInteractive && PC && PC->IsInputKeyDown(EKeys::LeftMouseButton);
    const int32 Wheel = bInteractive && PC ? (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown) ? 1 : PC->WasInputKeyJustPressed(EKeys::MouseScrollUp) ? -1 : 0) : 0;
    if (!bMouseDown) { S.DragSlider = -1; S.bRotating = false; }
    const auto Button = [&](float X, float Y, float W, float H, const FString& Label, bool bEnabled, FLinearColor Accent = Gold, bool bSelected = false, float Size = 10.f)
    {
        const bool bOver = bEnabled && In(X, Y, W, H);
        CireUIStyle::Button(P, X, Y, W, H, Label, !bEnabled ? ECireButtonState::Disabled : bSelected ? ECireButtonState::Selected : bOver ? (bMouseDown ? ECireButtonState::Pressed : ECireButtonState::Hover) : ECireButtonState::Normal, Accent, Size);
        return bEnabled && Click(X, Y, W, H);
    };
    const auto Chip = [&](float& X, float Y, const FString& Label, FLinearColor Color, bool bOn, float H = 20.f) -> bool
    {
        const float W = P.TextWidth(Label, 9.f, ECireFont::Heading) + 24;
        const bool bOver = In(X, Y, W, H);
        P.Rect(X, Y, W, H, bOn ? Color * FLinearColor(.34f, .34f, .34f, .95f) : FLinearColor(.03f, .035f, .045f, .92f));
        KitBorder(P, X, Y, W, H, bOn ? Color : Color * FLinearColor(.55f, .55f, .55f, .7f), bOver ? 1.6f : 1.f);
        P.Disc(X + 9, Y + H * .5f, 3.f, bOn ? Color : Muted, 12);
        P.Text(Label, X + 16, Y + (H - CireUIStyle::ReadableSize(9.f)) * .5f - 1, 9.f, bOn ? Parchment : Muted * 1.25f, ECireFont::Heading, false, false);
        const bool bHit = Click(X, Y, W, H);
        X += W + 5;
        return bHit;
    };
    // Keep the pick timer from locking a champion while the host edits (standalone / listen server only).
    if (Hero->HasAuthority() && Hero->DraftDeadline > 0)
    {
        const float ServerNow = World->GetTimeSeconds();
        Hero->DraftDeadline = FMath::Max(Hero->DraftDeadline, ServerNow + 30.f);
    }

    // ---------------- backdrop + header ----------------
    P.Rect(0, 0, VW, VH, FLinearColor(.010f, .013f, .020f, .985f));
    for (int32 I = 0; I < 12; ++I) P.Rect(0, VH - (I + 1) * 18, VW, 18, FLinearColor(.05f, .035f, .015f, .012f * (12 - I)));
    const float Pad = FMath::Clamp(VW * .012f, 10.f, 20.f);
    const float HeadH = 44.f;
    P.Text(TEXT("SKILL ASSIGNMENT EDITOR"), Pad, Pad - 2, 17.f, TitleText, ECireFont::Display, true, true);
    P.Text(TEXT("Build each champion's base kit from the whole ability pool, place its effects on the body, save it as that champion's template."),
        Pad, Pad + 24, 8.5f, Muted * 1.3f, ECireFont::Body, false, false);
    {
        const float BW = 208, BH = 30, BX = VW - Pad - BW, BY = Pad;
        if (Button(BX, BY, BW, BH, TEXT("BACK TO CHAMPION SELECT"), true, Gold, false, 9.5f))
        {
            if (KitDirty(S) && S.PendingSwitch != TEXT("__close"))
            {
                S.PendingSwitch = TEXT("__close");
                KitSetStatus(S, TEXT("Unsaved changes. Click BACK again to discard them, or SAVE TEMPLATE."), FLinearColor(1.f, .62f, .25f, 1));
            }
            else { Open(&HUD, false); return; }
        }
        if (Hero->DraftDeadline > 0 && !Hero->HasAuthority())
            P.Text(TEXT("Client: the pick timer keeps running"), BX, BY + BH + 3, 8.f, FLinearColor(1.f, .55f, .3f, 1), ECireFont::Body, false, false);
    }

    // ---------------- champion strip ----------------
    const float StripY = Pad + HeadH, StripH = 70.f;
    {
        const TArray<FCireChampionProfile>& All = CireChampionRoster::All();
        const float Cell = 64.f, SX = Pad + 24, SW = VW - 2 * Pad - 48;
        const int32 Visible = FMath::Max(1, FMath::FloorToInt(SW / Cell));
        const int32 MaxFirst = FMath::Max(0, All.Num() - Visible);
        if (In(Pad, StripY, VW - 2 * Pad, StripH) && Wheel) S.StripFirst += Wheel * 3;
        S.StripFirst = FMath::Clamp(S.StripFirst, 0, MaxFirst);
        CireUIStyle::Frame(P, Pad, StripY, VW - 2 * Pad, StripH, Gold, ECireFrame::Inset);
        for (int32 Dir = 0; Dir < 2; ++Dir)
        {
            const float AX = Dir == 0 ? Pad + 4 : VW - Pad - 20;
            const bool bOn = Dir == 0 ? S.StripFirst > 0 : S.StripFirst < MaxFirst;
            const FLinearColor C = !bOn ? Muted * .5f : In(AX, StripY, 16, StripH) ? BrightGold : Gold;
            const float CY = StripY + StripH * .5f;
            if (Dir == 0) P.Tri(FVector2D(AX + 2, CY), FVector2D(AX + 14, CY - 10), FVector2D(AX + 14, CY + 10), C);
            else P.Tri(FVector2D(AX + 14, CY), FVector2D(AX + 2, CY - 10), FVector2D(AX + 2, CY + 10), C);
            if (bOn && Click(AX, StripY, 16, StripH)) S.StripFirst = FMath::Clamp(S.StripFirst + (Dir == 0 ? -Visible : Visible), 0, MaxFirst);
        }
        for (int32 I = S.StripFirst; I < All.Num() && I < S.StripFirst + Visible; ++I)
        {
            const FCireChampionProfile& C = All[I];
            const float CX = SX + (I - S.StripFirst) * Cell + Cell * .5f, CY = StripY + 26;
            const bool bSel = C.Id == S.Champion, bOver = In(CX - Cell * .5f, StripY, Cell, StripH);
            const FLinearColor Ring = bSel ? BrightGold : bOver ? ThemeGlow : KitRoleColor(C) * FLinearColor(1, 1, 1, .8f);
            if (bSel) CireUIStyle::Glow(P, CX - 28, CY - 28, 56, 56, FLinearColor(1.f, .8f, .3f, .55f));
            if (!CireUIStyle::PortraitFace(P, C.Id, CX, CY, 20.f)) { P.Disc(CX, CY, 20.f, Card); P.Text(C.DisplayName.Left(1), CX - 5, CY - 9, 13.f, Parchment, ECireFont::Heading); }
            P.Circle(CX, CY, 21.f, Ring, bSel ? 2.4f : 1.4f, 40);
            if (CireKitEditor::Find(C.Id)) { P.Disc(CX + 15, CY - 15, 5.5f, Ink, 16); P.Disc(CX + 15, CY - 15, 4.f, FLinearColor(.35f, 1.f, .45f, 1), 16); }
            const FString Name = P.Fit(C.DisplayName, 8.f, Cell - 4, ECireFont::Heading);
            P.Text(Name, CX - P.TextWidth(Name, 8.f, ECireFont::Heading) * .5f, StripY + 48, 8.f, bSel ? BrightGold : bOver ? Parchment : Muted * 1.3f, ECireFont::Heading, false, false);
            if (bOver)
            {
                FCireTooltipSpec Tip; Tip.PortraitId = C.Id; Tip.Title = C.DisplayName; Tip.Tag = C.ClassType.ToUpper(); Tip.Accent = KitRoleColor(C);
                const FCireKitTemplate* T = CireKitEditor::Find(C.Id);
                Tip.Text(T ? FString::Printf(TEXT("Saved template: %d skills, %d placed effects%s"), T->BaseKit.Num(), T->Effects.Num(), T->bGrantOnDraft ? TEXT(", starts with the kit") : TEXT(", sold in the Skill Shop"))
                           : FString(TEXT("No template yet: the champion uses the default opening pick and Skill Shop list.")), T ? FLinearColor(.45f, 1.f, .55f, 1) : Muted);
                Tip.Footer = TEXT("Click to edit this champion");
                HUD.SetRichTooltip(Tip);
            }
            if (Click(CX - Cell * .5f, StripY, Cell, StripH) && C.Id != S.Champion)
            {
                if (KitDirty(S) && S.PendingSwitch != C.Id)
                {
                    S.PendingSwitch = C.Id;
                    KitSetStatus(S, FString::Printf(TEXT("Unsaved changes on %s. Click %s again to discard them."), Profile ? *Profile->DisplayName : TEXT("this champion"), *C.DisplayName), FLinearColor(1.f, .62f, .25f, 1));
                }
                else { KitSelectChampion(S, C.Id); Profile = CireChampionRoster::Find(S.Champion); HUD.PlayInterfaceSound(4, .45f); }
            }
        }
    }
    if (!Profile) return;

    // ---------------- columns ----------------
    const float BodyY = StripY + StripH + 10, BodyB = VH - Pad;
    const float Gap = 10.f;
    const float RightW = FMath::Clamp(VW * .29f, 330.f, 470.f);
    const float MidW = FMath::Clamp((BodyB - BodyY - 70.f) * .75f, 200.f, VW * .24f);
    const float LeftW = VW - 2 * Pad - RightW - MidW - 2 * Gap;
    const float LX = Pad, MX0 = LX + LeftW + Gap, RX = MX0 + MidW + Gap;
    ACireDraftStage* Stage = S.Stage.Get();
    if (!Stage) { Stage = ACireDraftStage::SpawnStage(World); S.Stage = Stage; }
    ACireHero* PreviewHero = nullptr;
    if (Stage)
    {
        Stage->Touch();
        Stage->SetCutout(false);
        Stage->SetTurntable(false, S.Yaw);
        Stage->ShowProfile(S.Champion);
        PreviewHero = Stage->GetPreviewHero();
    }

    // ================= LEFT: ability pool =================
    {
        CireUIStyle::Frame(P, LX, BodyY, LeftW, BodyB - BodyY, Gold, ECireFrame::Panel);
        const float IX = LX + 12, IW = LeftW - 24;
        float Y = BodyY + 10;
        P.Text(TEXT("ABILITY POOL"), IX, Y, 12.f, TitleText, ECireFont::Display, false, true);
        const FString Count = FString::Printf(TEXT("%d abilities in the database"), CireAbilityDB::All().Num());
        P.Text(Count, IX + IW - P.TextWidth(Count, 8.5f, ECireFont::Body), Y + 3, 8.5f, Muted * 1.3f, ECireFont::Body, false, false);
        Y += 24;
        // Search box (champion-select typing: click to type, Enter / Esc to leave).
        {
            const float SW = FMath::Min(230.f, IW * .42f), SH = 24;
            const bool bFocus = Controller && Controller->bDraftSearch;
            P.Rect(IX, Y, SW, SH, FLinearColor(.02f, .025f, .035f, .95f));
            KitBorder(P, IX, Y, SW, SH, bFocus ? BrightGold : In(IX, Y, SW, SH) ? Gold : Gold * FLinearColor(1, 1, 1, .45f), bFocus ? 1.6f : 1.f);
            P.Circle(IX + 12, Y + 11, 5.f, Muted * 1.4f, 1.3f, 16); P.Line(IX + 15.5f, Y + 14.5f, IX + 19, Y + 18, Muted * 1.4f, 1.5f);
            const FString Typed = Controller ? Controller->DraftSearch : FString();
            S.Filter.Search = Typed;
            const FString Shown = Typed.IsEmpty() && !bFocus ? FString(TEXT("Search name, school, tag, role...")) : Typed + (bFocus && FMath::Fmod(Time, 1.f) < .55f ? TEXT("|") : TEXT(""));
            P.Text(P.Fit(Shown, 9.f, SW - 34, ECireFont::Body), IX + 26, Y + 4, 9.f, Typed.IsEmpty() && !bFocus ? Muted : Parchment, ECireFont::Body, false, false);
            if (!Typed.IsEmpty())
            {
                const float XX = IX + SW - 16, XY = Y + 12;
                P.Line(XX - 4, XY - 4, XX + 4, XY + 4, Muted * 1.5f, 1.4f); P.Line(XX - 4, XY + 4, XX + 4, XY - 4, Muted * 1.5f, 1.4f);
                if (Click(XX - 8, Y, 16, SH) && Controller) { Controller->DraftSearch.Reset(); S.PoolRow = 0; }
            }
            if (bInteractive && HUD.HasClick() && Controller)
            {
                if (In(IX, Y, SW, SH)) { Controller->bDraftSearch = true; HUD.TakeClick(); }
                else Controller->bDraftSearch = false;
            }
            // Kind chips.
            float CX = IX + SW + 10;
            static const TCHAR* Kinds[] = {TEXT("ALL"), TEXT("ACTIVES"), TEXT("PASSIVES"), TEXT("ULTIMATES")};
            for (int32 K = -1; K < 3; ++K)
                if (Chip(CX, Y + 2, Kinds[K + 1], K < 0 ? Gold : KitKindColor(static_cast<EKind>(K)), S.Filter.Kind == K)) { S.Filter.Kind = K; S.PoolRow = 0; }
            Y += SH + 6;
        }
        // Role chips + "only learnable" toggle.
        {
            float CX = IX;
            static const TCHAR* Roles[] = {TEXT("DPS"), TEXT("TANK"), TEXT("HEAL")};
            static const FLinearColor RoleColors[] = {FLinearColor(.85f, .31f, .25f, 1), FLinearColor(.36f, .58f, .89f, 1), FLinearColor(.35f, .78f, .49f, 1)};
            if (Chip(CX, Y, TEXT("ANY ROLE"), Gold, S.Filter.Role.IsEmpty())) { S.Filter.Role.Reset(); S.PoolRow = 0; }
            for (int32 R = 0; R < 3; ++R) if (Chip(CX, Y, Roles[R], RoleColors[R], S.Filter.Role == Roles[R])) { S.Filter.Role = S.Filter.Role == Roles[R] ? FString() : FString(Roles[R]); S.PoolRow = 0; }
            CX += 8;
            const FString Only = FString::Printf(TEXT("ONLY %s'S CURRENT LIST"), *Profile->DisplayName.ToUpper());
            if (Chip(CX, Y, P.Fit(Only, 9.f, FMath::Max(60.f, IX + IW - CX - 30), ECireFont::Heading), FLinearColor(.4f, .85f, .95f, 1), S.bOnlyChampion)) { S.bOnlyChampion = !S.bOnlyChampion; S.PoolRow = 0; }
            Y += 26;
        }
        S.Filter.OnlyChampion = S.bOnlyChampion ? S.Champion : FString();
        // Section chips with counts (Skill Shop periodic table). First click isolates a section, later clicks toggle.
        const TArray<FPoolSection> AllSections = Sections();
        FPoolFilter Unhidden = S.Filter; Unhidden.HiddenSections.Reset();
        const auto Counted = Pool(Unhidden);
        {
            float CX = IX, CY = Y;
            const bool bAll = S.Filter.HiddenSections.IsEmpty();
            if (Chip(CX, CY, TEXT("ALL GROUPS"), Gold, bAll)) { S.Filter.HiddenSections.Reset(); S.PoolRow = 0; }
            for (const auto& Group : Counted)
            {
                const FString Label = FString::Printf(TEXT("%s  %d"), *Group.Key.Label.Replace(TEXT("OFFENSIVE  ·  "), TEXT("")), Group.Value.Num());
                const float W = P.TextWidth(Label, 9.f, ECireFont::Heading) + 29;
                if (CX + W > IX + IW) { CX = IX; CY += 24; }
                const bool bOn = !S.Filter.HiddenSections.Contains(Group.Key.Id);
                if (Chip(CX, CY, Label, Group.Key.Color, bOn))
                {
                    if (bAll) { for (const FPoolSection& Sec : AllSections) if (Sec.Id != Group.Key.Id) S.Filter.HiddenSections.Add(Sec.Id); }
                    else if (bOn) S.Filter.HiddenSections.Add(Group.Key.Id);
                    else S.Filter.HiddenSections.Remove(Group.Key.Id);
                    bool bAny = false;
                    for (const auto& G2 : Counted) bAny |= !S.Filter.HiddenSections.Contains(G2.Key.Id);
                    if (!bAny) S.Filter.HiddenSections.Reset();
                    S.PoolRow = 0;
                }
            }
            Y = CY + 28;
        }
        // Periodic-table blocks: each section is a bordered block of tiles; a block wider than the row continues below.
        const auto Groups = Pool(S.Filter);
        constexpr float TileW = 74.f, TileH = 86.f, TGap = 6.f, BPad = 7.f, BHead = 18.f;
        const float AreaY = Y, AreaH = BodyB - 10 - AreaY, AreaW = IW - 12;
        const float RowH = BHead + BPad * 2 + TileH + 8;
        struct FBlock { int32 Group; int32 Start, Count; bool bCont; float X, W; };
        TArray<TArray<FBlock>> Rows;
        {
            TArray<FBlock> Row; float RXp = 0;
            for (int32 G = 0; G < Groups.Num(); ++G)
                for (int32 Start = 0; Start < Groups[G].Value.Num();)
                {
                    const int32 Left = Groups[G].Value.Num() - Start;
                    const int32 Fit = FMath::FloorToInt((AreaW - RXp - BPad * 2 + TGap) / (TileW + TGap));
                    if (Fit < 1 || (Fit < Left && Fit < 2 && RXp > 0)) { Rows.Add(Row); Row.Reset(); RXp = 0; continue; }
                    const int32 Take = FMath::Min(Fit, Left);
                    FBlock B{G, Start, Take, Start > 0, RXp, BPad * 2 + Take * TileW + (Take - 1) * TGap};
                    Row.Add(B); RXp += B.W + 8; Start += Take;
                }
            if (Row.Num()) Rows.Add(Row);
        }
        const int32 PerPage = FMath::Max(1, FMath::FloorToInt(AreaH / RowH));
        const int32 MaxRow = FMath::Max(0, Rows.Num() - PerPage);
        if (In(IX, AreaY, IW, AreaH) && Wheel) S.PoolRow += Wheel;
        S.PoolRow = FMath::Clamp(S.PoolRow, 0, MaxRow);
        if (MaxRow > 0)
        {
            const float SBX = IX + IW - 6, SBH = AreaH - 4;
            P.Rect(SBX, AreaY, 5, SBH, FLinearColor(.06f, .05f, .04f, .9f));
            const float Thumb = FMath::Max(24.f, SBH * PerPage / float(Rows.Num()));
            P.Rect(SBX, AreaY + (SBH - Thumb) * S.PoolRow / float(MaxRow), 5, Thumb, Gold * FLinearColor(1, 1, 1, .8f));
        }
        if (Rows.IsEmpty()) P.Text(TEXT("No ability matches the search and filters."), IX + 10, AreaY + 20, 10.f, Muted * 1.3f, ECireFont::Body);
        FString HoverId;
        for (int32 R = S.PoolRow; R < Rows.Num() && R < S.PoolRow + PerPage; ++R)
        {
            const float RY = AreaY + (R - S.PoolRow) * RowH;
            for (const FBlock& B : Rows[R])
            {
                const auto& Group = Groups[B.Group];
                const FLinearColor Col = Group.Key.Color;
                const float BX = IX + B.X, BH = RowH - 8;
                P.Rect(BX, RY, B.W, BH, Col * FLinearColor(.07f, .07f, .07f, .6f));
                KitBorder(P, BX, RY, B.W, BH, Col * FLinearColor(1, 1, 1, .85f), 1.4f);
                P.Rect(BX, RY, B.W, BHead, Col * FLinearColor(.3f, .3f, .3f, .95f));
                const FString Label = Group.Key.Label + (B.bCont ? TEXT("  (CONT.)") : TEXT(""));
                P.Text(P.Fit(Label, 8.5f, B.W - 12, ECireFont::Display), BX + 6, RY + 2, 8.5f, Parchment, ECireFont::Display, false, false);
                for (int32 I = 0; I < B.Count; ++I)
                {
                    const FCireAbilityDef& Def = *Group.Value[B.Start + I];
                    const float TX = BX + BPad + I * (TileW + TGap), TY = RY + BHead + BPad;
                    const bool bInKit = S.Draft.BaseKit.Contains(Def.Id), bSel = Def.Id == S.Selected, bOver = In(TX, TY, TileW, TileH);
                    const FString Blocker = bInKit ? FString() : AddBlocker(S.Draft.BaseKit, Def.Id);
                    const bool bFull = !Blocker.IsEmpty();
                    P.Rect(TX, TY, TileW, TileH, bOver ? Hover : FLinearColor(.025f, .03f, .04f, .95f));
                    if (bInKit) CireUIStyle::Glow(P, TX - 4, TY - 4, TileW + 8, TileH + 8, FLinearColor(1.f, .8f, .3f, .35f));
                    KitBorder(P, TX, TY, TileW, TileH, bSel ? FLinearColor(.3f, 1.f, .95f, 1) : bInKit ? BrightGold : bOver ? ThemeGlow : Col * FLinearColor(.6f, .6f, .6f, .8f), bSel || bInKit ? 2.f : 1.f);
                    const float Icon = 44.f;
                    CireAbilityIcons::Draw(P, Def.Id, TX + (TileW - Icon) * .5f, TY + 6, Icon, bFull && !bOver ? .45f : 1.f);
                    if (bInKit)
                    {
                        const float BXc = TX + TileW - 11, BYc = TY + 10;
                        P.Disc(BXc, BYc, 8.f, FLinearColor(.1f, .08f, .02f, 1), 16); P.Circle(BXc, BYc, 8.f, BrightGold, 1.2f, 16);
                        P.Line(BXc - 4, BYc, BXc - 1, BYc + 3.5f, BrightGold, 1.8f); P.Line(BXc - 1, BYc + 3.5f, BXc + 4.5f, BYc - 3.5f, BrightGold, 1.8f);
                    }
                    const auto Kind = KindOf(Def.Id);
                    if (Kind != EKind::Active) P.Rect(TX + 4, TY + 4, 5, 5, KitKindColor(Kind));
                    // Name (2 lines, centred).
                    TArray<FString> Words; Def.Name.ParseIntoArrayWS(Words);
                    FString L1, L2;
                    for (const FString& W : Words)
                    {
                        const FString Try = L1.IsEmpty() ? W : L1 + TEXT(" ") + W;
                        if (L2.IsEmpty() && P.TextWidth(Try, 8.f, ECireFont::Heading) <= TileW - 6) L1 = Try; else L2 = L2.IsEmpty() ? W : L2 + TEXT(" ") + W;
                    }
                    L2 = P.Fit(L2, 8.f, TileW - 6, ECireFont::Heading);
                    const FLinearColor NameC = bInKit ? BrightGold : bFull ? Muted : Parchment;
                    P.Text(L1, TX + (TileW - P.TextWidth(L1, 8.f, ECireFont::Heading)) * .5f, TY + 53, 8.f, NameC, ECireFont::Heading, false, false);
                    if (!L2.IsEmpty()) P.Text(L2, TX + (TileW - P.TextWidth(L2, 8.f, ECireFont::Heading)) * .5f, TY + 67, 8.f, NameC, ECireFont::Heading, false, false);
                    if (bOver)
                    {
                        HoverId = Def.Id;
                        HUD.SetRichTooltip(KitAbilityTip(Def, bInKit ? TEXT("In the kit  ·  click: place its effect  ·  right-click: remove")
                            : bFull ? Blocker : TEXT("Click: add to the base kit")));
                    }
                    if (Click(TX, TY, TileW, TileH))
                    {
                        if (!bInKit && bFull) { KitSetStatus(S, Blocker, FLinearColor(1.f, .45f, .35f, 1)); HUD.PlayInterfaceSound(4, .3f); }
                        else
                        {
                            if (!bInKit) { S.Draft.BaseKit.Add(Def.Id); S.Draft.BaseKit = Normalize(S.Draft.BaseKit); KitSetStatus(S, FString::Printf(TEXT("Added %s to the kit."), *Def.Name), FLinearColor(.5f, 1.f, .6f, 1)); }
                            S.Selected = Def.Id; S.BoneCursor = -1;
                            HUD.PlayInterfaceSound(4, .45f);
                        }
                    }
                    if (bOver && bRight && bInKit)
                    {
                        S.Draft.BaseKit.Remove(Def.Id);
                        if (S.Selected == Def.Id) S.Selected = S.Draft.BaseKit.Num() ? S.Draft.BaseKit[0] : FString();
                        KitSetStatus(S, FString::Printf(TEXT("Removed %s."), *Def.Name), Muted * 1.4f);
                    }
                }
            }
        }
    }

    // ================= MIDDLE: live preview =================
    {
        CireUIStyle::Frame(P, MX0, BodyY, MidW, BodyB - BodyY, Gold, ECireFrame::Panel);
        const float IX = MX0 + 10, IW = MidW - 20;
        P.Text(TEXT("PREVIEW"), IX, BodyY + 10, 12.f, TitleText, ECireFont::Display, false, true);
        const FString Who = Profile->DisplayName;
        P.Text(P.Fit(Who, 9.f, IW - 90, ECireFont::Heading), IX + IW - P.TextWidth(P.Fit(Who, 9.f, IW - 90, ECireFont::Heading), 9.f, ECireFont::Heading), BodyY + 13, 9.f, KitRoleColor(*Profile), ECireFont::Heading, false, false);
        const float ImgY = BodyY + 34, ImgBottom = BodyB - 78;
        float ImgH = ImgBottom - ImgY, ImgW = ImgH * .75f;
        if (ImgW > IW) { ImgW = IW; ImgH = ImgW / .75f; }
        const float ImgX = IX + (IW - ImgW) * .5f;
        P.Rect(ImgX - 1, ImgY - 1, ImgW + 2, ImgH + 2, FLinearColor(0, 0, 0, 1));
        if (Stage && Stage->GetRenderTarget() && PreviewHero) KitDrawTarget(P, Stage->GetRenderTarget(), ImgX, ImgY, ImgW, ImgH);
        else P.Text(TEXT("Loading champion..."), ImgX + 12, ImgY + ImgH * .5f, 10.f, Muted, ECireFont::Body);
        KitBorder(P, ImgX, ImgY, ImgW, ImgH, Gold * FLinearColor(1, 1, 1, .6f), 1.f);
        // Drag to turn the champion.
        if (bInteractive && HUD.HasClick() && In(ImgX, ImgY, ImgW, ImgH)) { HUD.TakeClick(); S.bRotating = true; S.RotateFromX = M.X; S.RotateFromYaw = S.Yaw; }
        if (S.bRotating && bMouseDown) S.Yaw = S.RotateFromYaw - (M.X - S.RotateFromX) * .6f;
        if (In(ImgX, ImgY, ImgW, ImgH)) P.Text(TEXT("drag to turn"), ImgX + 6, ImgY + ImgH - 16, 8.f, Muted * 1.2f, ECireFont::Body, false, true);
        // Selected ability caption.
        const FCireAbilityDef* SelDef = CireAbilityDB::Find(S.Selected);
        const float CapY = ImgBottom + 6;
        if (SelDef)
        {
            CireAbilityIcons::Draw(P, SelDef->Id, IX, CapY, 26.f);
            P.Text(P.Fit(SelDef->Name, 10.f, IW - 34, ECireFont::Bold), IX + 32, CapY + 1, 10.f, Parchment, ECireFont::Bold, false, true);
            float BaseScale = 1.f;
            UFXSystemAsset* System = CastSystem(SelDef->Id, &BaseScale);
            const FString Sys = System ? System->GetName() : FString(TEXT("no cast effect (procedural only)"));
            P.Text(P.Fit(Sys, 7.5f, IW - 34, ECireFont::Body), IX + 32, CapY + 16, 7.5f, Muted * 1.2f, ECireFont::Body, false, false);
        }
        else P.Text(TEXT("Select a kit ability to place its effect."), IX, CapY + 6, 9.f, Muted * 1.2f, ECireFont::Body, false, false);
        // Turn + cast buttons.
        const float BY = BodyB - 40, BH = 30;
        if (Button(IX, BY, 32, BH, TEXT("<"), true)) S.Yaw -= 45.f;
        if (Button(IX + 36, BY, 32, BH, TEXT(">"), true)) S.Yaw += 45.f;
        if (Button(IX + 74, BY, IW - 74, BH, TEXT("CAST PREVIEW"), SelDef && PreviewHero, FLinearColor(.3f, .9f, 1.f, 1)) && PreviewHero && SelDef)
        {
            // The cast clip plays when a skill cooldown starts (CireChampionActions): one learned slot, bumped each click.
            PreviewHero->Skills = {SelDef->Id};
            if (PreviewHero->Cooldowns.Num() != 1) PreviewHero->Cooldowns = {0.f};
            PreviewHero->Cooldowns[0] += 1.f;
            S.CastAt = Now;
            S.CastReleaseAt = Now + CireChampionActions::SkillWindup(World, SelDef->Id);
            if (UFXSystemComponent* C = S.Live.Get()) C->DestroyComponent();
            S.Live.Reset();
            HUD.PlayInterfaceSound(4, .4f);
        }
    }

    // ================= RIGHT: kit + placement =================
    {
        CireUIStyle::Frame(P, RX, BodyY, RightW, BodyB - BodyY, Gold, ECireFrame::Panel);
        const float IX = RX + 12, IW = RightW - 24;
        float Y = BodyY + 10;
        int32 Have[3] = {0, 0, 0};
        for (const FString& Id : S.Draft.BaseKit) ++Have[static_cast<int32>(KindOf(Id))];
        P.Text(TEXT("BASE KIT"), IX, Y, 12.f, TitleText, ECireFont::Display, false, true);
        const FString Tally = FString::Printf(TEXT("%d/6 actives  ·  %d/1 ult  ·  %d/1 passive"), Have[0], Have[2], Have[1]);
        P.Text(Tally, IX + IW - P.TextWidth(Tally, 8.5f, ECireFont::Body), Y + 3, 8.5f, Muted * 1.3f, ECireFont::Body, false, false);
        Y += 24;
        // Slots: 6 actives on one row, then the ultimate and the passive.
        TArray<FString> Actives, Ult, Pas;
        for (const FString& Id : S.Draft.BaseKit) (KindOf(Id) == EKind::Ultimate ? Ult : KindOf(Id) == EKind::Passive ? Pas : Actives).Add(Id);
        const float Slot = FMath::Min(46.f, (IW - 5 * 8) / 6.f);
        const auto DrawSlot = [&](float X, float SY, const FString& Id, ECireSlotKind Kind, const FString& Key)
        {
            FCireIconSlot Icon;
            Icon.IconId = Id.IsEmpty() ? FString(TEXT("empty")) : Id;
            Icon.IconTexture = Id.IsEmpty() ? nullptr : CireUIStyle::FindAbilityIcon(Id);
            Icon.Tint = Id.IsEmpty() ? Muted : CireAbilityIcons::Accent(Id);
            Icon.Kind = Kind; Icon.KeyLabel = Key; Icon.bEmpty = Id.IsEmpty();
            Icon.bHover = In(X, SY, Slot, Slot);
            Icon.bGlow = !Id.IsEmpty() && Id == S.Selected;
            CireUIStyle::IconSlot(P, X, SY, Slot, Icon, Now);
            if (!Id.IsEmpty() && PreviewHero)
            {
                FString Why;
                if (!CireKits::MeetsRequirement(PreviewHero, Id, &Why))
                {
                    P.Disc(X + Slot - 6, SY + 6, 7.f, FLinearColor(.6f, .08f, .06f, 1), 16);
                    P.Text(TEXT("!"), X + Slot - 8, SY - 2, 10.f, Parchment, ECireFont::Bold, false, false);
                }
            }
            if (Icon.bHover)
            {
                if (const FCireAbilityDef* D = CireAbilityDB::Find(Id))
                {
                    FString Why; FString Foot = TEXT("Click: place its effect  ·  right-click: remove");
                    if (PreviewHero && !CireKits::MeetsRequirement(PreviewHero, Id, &Why)) Foot = FString::Printf(TEXT("Not granted on this body: %s"), *Why);
                    HUD.SetRichTooltip(KitAbilityTip(*D, Foot));
                }
                else HUD.SetRichTooltip(FCireTooltipSpec().Text(Kind == ECireSlotKind::Ultimate ? TEXT("Ultimate slot: pick an ultimate from the pool.") : Kind == ECireSlotKind::Passive ? TEXT("Passive slot: pick a passive from the pool.") : TEXT("Active slot: pick an active from the pool.")));
            }
            if (!Id.IsEmpty() && Click(X, SY, Slot, Slot)) { S.Selected = Id; S.BoneCursor = -1; HUD.PlayInterfaceSound(4, .4f); }
            if (!Id.IsEmpty() && Icon.bHover && bRight)
            {
                S.Draft.BaseKit.Remove(Id);
                if (S.Selected == Id) S.Selected = S.Draft.BaseKit.Num() ? S.Draft.BaseKit[0] : FString();
            }
        };
        for (int32 I = 0; I < 6; ++I) DrawSlot(IX + I * (Slot + 8), Y, Actives.IsValidIndex(I) ? Actives[I] : FString(), ECireSlotKind::Normal, FString::FromInt(I + 1));
        Y += Slot + 8;
        DrawSlot(IX, Y, Ult.Num() ? Ult[0] : FString(), ECireSlotKind::Ultimate, TEXT("R"));
        DrawSlot(IX + Slot + 8, Y, Pas.Num() ? Pas[0] : FString(), ECireSlotKind::Passive, FString());
        // Start-with-kit toggle beside the ult / passive.
        {
            const float TX = IX + 2 * (Slot + 8) + 6, TY = Y + 2, TW = IW - (TX - IX);
            const bool bOn = S.Draft.bGrantOnDraft, bOver = In(TX, TY, TW, Slot - 4);
            P.Rect(TX, TY + 4, 16, 16, FLinearColor(.02f, .025f, .035f, 1)); KitBorder(P, TX, TY + 4, 16, 16, bOver ? BrightGold : Gold, 1.2f);
            if (bOn) { P.Line(TX + 3, TY + 12, TX + 7, TY + 16, BrightGold, 2.f); P.Line(TX + 7, TY + 16, TX + 14, TY + 7, BrightGold, 2.f); }
            P.Text(TEXT("START MATCHES WITH THIS KIT"), TX + 22, TY + 3, 9.f, bOn ? Parchment : Muted * 1.3f, ECireFont::Heading, false, false);
            P.Text(P.Fit(bOn ? FString(TEXT("Learned at level 1, levelled in the Skill Shop")) : FString(TEXT("Off: the kit is only sold in the Skill Shop")), 8.f, TW - 22, ECireFont::Body),
                TX + 22, TY + 20, 8.f, Muted * 1.2f, ECireFont::Body, false, false);
            if (Click(TX, TY, TW, Slot - 4)) S.Draft.bGrantOnDraft = !S.Draft.bGrantOnDraft;
        }
        Y += Slot + 10;
        // Save / revert / clear.
        const bool bDirty = KitDirty(S);
        {
            const float BW = (IW - 12) / 3.f, BH = 28;
            if (Button(IX, Y, BW, BH, bDirty ? TEXT("SAVE TEMPLATE") : TEXT("SAVED"), bDirty, FLinearColor(.4f, 1.f, .5f, 1), false, 9.5f))
            {
                FString Error;
                FCireKitTemplate ToSave = S.Draft; ToSave.ChampionId = S.Champion;
                if (SaveTemplate(ToSave, &Error))
                {
                    S.Saved = KitLoad(S.Champion); S.Draft = S.Saved; S.PendingSwitch.Reset();
                    KitSetStatus(S, FString::Printf(TEXT("Saved %s's template: %d skills, %d placed effects."), *Profile->DisplayName, S.Saved.BaseKit.Num(), S.Saved.Effects.Num()), FLinearColor(.45f, 1.f, .55f, 1));
                    HUD.PlayInterfaceSound(4, .6f);
                }
                else KitSetStatus(S, Error, FLinearColor(1.f, .4f, .35f, 1));
            }
            if (Button(IX + BW + 6, Y, BW, BH, TEXT("REVERT"), bDirty, Gold, false, 9.5f)) { S.Draft = S.Saved; KitSetStatus(S, TEXT("Reverted to the saved template."), Muted * 1.4f); }
            if (Button(IX + 2 * (BW + 6), Y, BW, BH, TEXT("CLEAR KIT"), S.Draft.BaseKit.Num() > 0, FLinearColor(1.f, .45f, .35f, 1), false, 9.5f))
            { S.Draft.BaseKit.Reset(); S.Selected.Reset(); KitSetStatus(S, TEXT("Kit cleared (save to apply; an empty template restores the default kit)."), Muted * 1.4f); }
            Y += BH + 5;
            const FString Line = !S.Status.IsEmpty() && Now - S.StatusAt < 8.0 ? S.Status
                : bDirty ? FString(TEXT("Unsaved changes."))
                : CireKitEditor::Find(S.Champion) ? FString::Printf(TEXT("Template saved in Content/Data/ChampionKitTemplates.json")) : FString(TEXT("No template: default opening pick + Skill Shop list."));
            const FLinearColor LineC = !S.Status.IsEmpty() && Now - S.StatusAt < 8.0 ? S.StatusColor : bDirty ? FLinearColor(1.f, .75f, .3f, 1) : Muted * 1.3f;
            P.Text(P.Fit(Line, 8.5f, IW, ECireFont::Body), IX, Y, 8.5f, LineC, ECireFont::Body, false, false);
            Y += 18;
        }
        CireUIStyle::Divider(P, IX, Y, IW);
        Y += 8;
        // ---------- effect placement ----------
        const FCireAbilityDef* SelDef = CireAbilityDB::Find(S.Selected);
        P.Text(TEXT("EFFECT PLACEMENT"), IX, Y, 12.f, TitleText, ECireFont::Display, false, true);
        if (SelDef) { const FString N = P.Fit(SelDef->Name, 9.f, IW - 150, ECireFont::Heading); P.Text(N, IX + IW - P.TextWidth(N, 9.f, ECireFont::Heading), Y + 3, 9.f, BrightGold, ECireFont::Heading, false, false); }
        Y += 22;
        if (!SelDef || !S.Draft.BaseKit.Contains(S.Selected))
        {
            P.Wrapped(TEXT("Add abilities to the kit, then select one (click its slot or tile) to place its cast effect on this champion: attach point, offset, size and colour. The preview loops the effect live."),
                IX, Y, IW, 9.f, Muted * 1.3f, 5, ECireFont::Body);
        }
        else
        {
            FCireKitEffectPlacement& Pl = S.Draft.Effects.FindOrAdd(S.Selected);
            USkeletalMeshComponent* Mesh = PreviewHero ? PreviewHero->GetMesh() : nullptr;
            // Attach chips (anchors resolved on this body; greyed when the body has no such bone).
            P.Text(TEXT("ATTACH TO"), IX, Y + 3, 8.5f, Gold, ECireFont::Heading, false, false);
            {
                float CX = IX + 74, CY = Y;
                for (const FKitAnchorChip& A : KitAnchorChips)
                {
                    const bool bResolved = !*A.Key || !Mesh || ResolveAttach(Mesh, A.Key) != NAME_None;
                    const float W = P.TextWidth(A.Label, 9.f, ECireFont::Heading) + 29;
                    if (CX + W > IX + IW) { CX = IX + 74; CY += 24; }
                    const bool bOn = Pl.Attach == A.Key;
                    if (Chip(CX, CY, A.Label, bResolved ? FLinearColor(.3f, .9f, 1.f, 1) : Muted * .6f, bOn) && bResolved) { Pl.Attach = A.Key; S.BoneCursor = -1; }
                }
                Y = CY + 26;
            }
            // Bone / socket cycler over every name of this body.
            {
                TArray<FName> Names = Mesh ? Mesh->GetAllSocketNames() : TArray<FName>();
                Names.RemoveAll([](const FName& N) { const FString X = N.ToString().ToLower(); return X.Contains(TEXT("twist")) || X.StartsWith(TEXT("ik_")) || X.StartsWith(TEXT("vb ")); });
                const FName Resolved = ResolveAttach(Mesh, Pl.Attach);
                const FString Shown = Pl.Attach.IsEmpty() ? FString(TEXT("default (effect origin)")) : Resolved.IsNone() ? FString::Printf(TEXT("%s (not on this body)"), *Pl.Attach) : Resolved.ToString();
                P.Text(TEXT("BONE"), IX, Y + 5, 8.5f, Gold, ECireFont::Heading, false, false);
                const float CX = IX + 74, CW = IW - 74;
                const bool bHas = Names.Num() > 0;
                if (Button(CX, Y, 24, 22, TEXT("<"), bHas, Gold, false, 9.f) || Button(CX + CW - 24, Y, 24, 22, TEXT(">"), bHas, Gold, false, 9.f))
                {
                    const bool bNext = In(CX + CW - 24, Y, 24, 22);
                    int32 Index = S.BoneCursor >= 0 ? S.BoneCursor : Names.IndexOfByKey(Resolved);
                    Index = Index < 0 ? 0 : (Index + (bNext ? 1 : -1) + Names.Num()) % Names.Num();
                    S.BoneCursor = Index; Pl.Attach = Names[Index].ToString();
                }
                P.Rect(CX + 28, Y, CW - 56, 22, FLinearColor(.02f, .025f, .035f, .95f));
                const FString Fit = P.Fit(Shown, 9.f, CW - 64, ECireFont::Body);
                P.Text(Fit, CX + 28 + (CW - 56 - P.TextWidth(Fit, 9.f, ECireFont::Body)) * .5f, Y + 3, 9.f, Pl.Attach.IsEmpty() ? Muted * 1.3f : Resolved.IsNone() ? FLinearColor(1.f, .5f, .35f, 1) : Parchment, ECireFont::Body, false, false);
                Y += 28;
            }
            // Sliders.
            const auto SliderRow = [&](int32 Id, const TCHAR* Label, float& Value, float Min, float Max, float Default, const FString& Text)
            {
                P.Text(Label, IX, Y + 1, 8.5f, Gold, ECireFont::Heading, false, false);
                const float TX = IX + 74, TW = IW - 74 - 56, TY = Y + 5;
                const bool bOver = In(TX - 6, Y - 2, TW + 12, 20);
                CireUIStyle::Slider(P, TX, TY, TW, (Value - Min) / (Max - Min), true, bOver || S.DragSlider == Id);
                P.Text(Text, TX + TW + 8, Y + 1, 9.f, Parchment, ECireFont::Numbers, false, false);
                if (bInteractive && HUD.HasClick() && bOver) { HUD.TakeClick(); S.DragSlider = Id; }
                if (S.DragSlider == Id && bMouseDown) Value = FMath::Clamp(Min + (M.X - TX) / TW * (Max - Min), Min, Max);
                if (bOver && bRight) Value = Default;
                if (bOver && Wheel) Value = FMath::Clamp(Value - Wheel * (Max - Min) / 60.f, Min, Max);
                Y += 22;
            };
            float OX = Pl.Offset.X, OY = Pl.Offset.Y, OZ = Pl.Offset.Z;
            SliderRow(1, TEXT("FORWARD"), OX, -150, 150, 0, FString::Printf(TEXT("%+.0f"), OX));
            SliderRow(2, TEXT("RIGHT"), OY, -150, 150, 0, FString::Printf(TEXT("%+.0f"), OY));
            SliderRow(3, TEXT("UP"), OZ, -150, 150, 0, FString::Printf(TEXT("%+.0f"), OZ));
            Pl.Offset = FVector(FMath::RoundToFloat(OX), FMath::RoundToFloat(OY), FMath::RoundToFloat(OZ));
            SliderRow(4, TEXT("SIZE"), Pl.Scale, .2f, 3.f, 1.f, FString::Printf(TEXT("%.2fx"), Pl.Scale));
            Pl.Scale = FMath::RoundToFloat(Pl.Scale * 100.f) / 100.f;
            // Tint swatches.
            {
                P.Text(TEXT("TINT"), IX, Y + 3, 8.5f, Gold, ECireFont::Heading, false, false);
                const float SW = FMath::Min(20.f, (IW - 74 - 10 * 3) / 11.f);
                float SX = IX + 74;
                const bool bNone = Pl.Tint.A <= 0.f, bOverNone = In(SX, Y, SW, SW);
                P.Rect(SX, Y, SW, SW, FLinearColor(.03f, .03f, .04f, 1)); P.Line(SX + 2, Y + SW - 2, SX + SW - 2, Y + 2, FLinearColor(.8f, .2f, .2f, 1), 1.5f);
                KitBorder(P, SX, Y, SW, SW, bNone ? BrightGold : bOverNone ? ThemeGlow : Muted, bNone ? 2.f : 1.f);
                if (bOverNone) HUD.SetRichTooltip(FCireTooltipSpec().Text(TEXT("No tint: the effect keeps its own colours.")));
                if (Click(SX, Y, SW, SW)) Pl.Tint = FLinearColor(0, 0, 0, 0);
                SX += SW + 3;
                for (const FLinearColor& C : KitSwatches)
                {
                    const bool bOn = Pl.Tint.A > 0 && Pl.Tint.Equals(C, .01f), bOver = In(SX, Y, SW, SW);
                    P.Rect(SX, Y, SW, SW, C);
                    KitBorder(P, SX, Y, SW, SW, bOn ? BrightGold : bOver ? ThemeGlow : FLinearColor(0, 0, 0, .8f), bOn ? 2.f : 1.f);
                    if (Click(SX, Y, SW, SW)) Pl.Tint = C;
                    SX += SW + 3;
                }
                Y += SW + 6;
            }
            if (Pl.Tint.A > 0) SliderRow(5, TEXT("STRENGTH"), Pl.TintStrength, 0.f, 1.f, 1.f, FString::Printf(TEXT("%.0f%%"), Pl.TintStrength * 100.f));
            Pl.TintStrength = FMath::RoundToFloat(Pl.TintStrength * 100.f) / 100.f;
            if (Button(IX, Y + 2, 150, 24, TEXT("RESET EFFECT"), !Pl.IsDefault(), Gold, false, 9.f)) Pl = FCireKitEffectPlacement();
            P.Text(TEXT("Right-click a slider to reset it"), IX + 160, Y + 7, 8.f, Muted * 1.1f, ECireFont::Body, false, false);

            // ---------- live effect on the preview body ----------
            if (PreviewHero && Mesh)
            {
                float DataScale = 1.f;
                const CireFabVFX::FEntry* Entry = nullptr;
                UFXSystemAsset* System = CastSystem(S.Selected, &DataScale, &Entry);
                const float Base = DataScale * CireAbilityVFX::SpellEffectScale(World);
                const FString Key = FString::Printf(TEXT("%s|%s|%s|%s|%.3f"), *S.Champion, *S.Selected, *ResolveAttach(Mesh, Pl.Attach).ToString(), *Pl.Tint.ToString(), Pl.TintStrength);
                UFXSystemComponent* Live = S.Live.Get();
                const bool bWaitRelease = S.CastReleaseAt > 0 && Now < S.CastReleaseAt;
                const bool bFinished = Live && ((Cast<UNiagaraComponent>(Live) && Cast<UNiagaraComponent>(Live)->IsComplete()) || !Live->IsActive());
                if (!bWaitRelease && System && (Key != S.LiveKey || !Live || (bFinished && Now - S.LiveAt > 1.2) || Now - S.LiveAt > 4.0 || S.CastReleaseAt > 0))
                {
                    if (Live) Live->DestroyComponent();
                    Live = SpawnPlaced(PreviewHero, System, Pl, Base, false, Entry);
                    S.Live = Live; S.LiveKey = Key; S.LiveAt = Now; S.CastReleaseAt = -1;
                }
                if (Live) ApplyPlacement(Live, PreviewHero, Pl, Base, false);
            }
        }
    }
    // Drop placements that were never changed so the dirty check stays honest.
    for (auto It = S.Draft.Effects.CreateIterator(); It; ++It) if (It.Value().IsDefault() && It.Key() != S.Selected) It.RemoveCurrent();

#if !UE_BUILD_SHIPPING
    // Gallery: -CireKitEditorGallery[=<champion>] captures the editor (Saved/KitEditorGallery) and quits.
    if (S.bGallery && !S.bGalleryDone)
    {
        if (S.GalleryStart == 0)
        {
            S.GalleryStart = Now;
            FString Want;
            if (FParse::Value(FCommandLine::Get(), TEXT("CireKitEditorGallery="), Want) && CireChampionRoster::Find(Want)) KitSelectChampion(S, Want);
            // A representative draft: the champion's first purchasable actives + an ultimate + a passive, with one placed effect.
            TArray<FString> Kit;
            if (const FCireChampionKit* K = CireAbilityDB::Kit(S.Champion)) for (const FString& Id : K->PurchasableImplemented) if (AddBlocker(Kit, Id).IsEmpty()) Kit.Add(Id);
            S.Draft.BaseKit = Normalize(Kit);
            S.Selected = S.Draft.BaseKit.Num() ? S.Draft.BaseKit[0] : FString();
            FCireKitEffectPlacement& Pl = S.Draft.Effects.FindOrAdd(S.Selected);
            Pl.Attach = TEXT("hand_r"); Pl.Offset = FVector(20, 0, 10); Pl.Scale = 1.3f; Pl.Tint = KitSwatches[4]; Pl.TintStrength = .8f;
            if (Controller) { Controller->DraftSearch.Reset(); }
        }
        const bool bReady = Stage && Stage->IsPreviewReady() && Stage->SecondsShown() > 3.0 && Now - S.GalleryStart > 6.0;
        FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("KitEditorGallery"));
        if (S.GalleryShotAt == 0 && (bReady || Now - S.GalleryStart > 60.0))
        {
            const FString File = FPaths::Combine(Dir, FString::Printf(TEXT("kit_editor_%d.png"), S.GalleryStage));
            FScreenshotRequest::RequestScreenshot(File, false, false, false, FIntRect(), true);
            S.GalleryShotAt = Now;
            UE_LOG(LogCireKitEditorUI, Display, TEXT("CIRE_KIT_EDITOR_GALLERY_SHOT %s champion=%s kit=%d live=%d"), *File, *S.Champion, S.Draft.BaseKit.Num(), S.Live.IsValid() ? 1 : 0);
        }
        if (S.GalleryShotAt > 0 && Now - S.GalleryShotAt > 1.5)
        {
            ++S.GalleryStage; S.GalleryShotAt = 0;
            if (S.GalleryStage == 1) { S.bOnlyChampion = false; S.Filter.Search = TEXT(""); S.Filter.HiddenSections.Reset(); S.PoolRow = 2; S.Yaw = 30.f; }
            if (S.GalleryStage >= 2)
            {
                S.bGalleryDone = true;
                UE_LOG(LogCireKitEditorUI, Display, TEXT("CIRE_KIT_EDITOR_GALLERY_PASS directory=%s"), *Dir);
                FPlatformMisc::RequestExitWithStatus(false, 0);
            }
        }
    }
#endif
}
