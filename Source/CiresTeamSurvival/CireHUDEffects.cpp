// wow-ui: buff/debuff gain callouts, crowd-control alerts (centre callout + screen edge),
// cast bars (player, frames, nameplates) with INTERRUPTED / SILENCED flashes and CC badges.
#include "CireHUD.h"
#include "CireGame.h"
#include "CireBuffs.h"
#include "CireEffects.h"
#include "CireUIStyle.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"

namespace
{
const FLinearColor CastGold(1.f, .70f, .05f, 1.f), CastGrey(.40f, .42f, .48f, 1.f), HealGreen(.25f, .9f, .4f, 1.f);
FLinearColor ControlColor(ECireControl C)
{
    switch (C)
    {
    case ECireControl::Stun: return FLinearColor(1.f, .85f, .3f, 1);
    case ECireControl::Silence: return FLinearColor(.72f, .42f, 1.f, 1);
    case ECireControl::HealCut: return FLinearColor(1.f, .25f, .2f, 1);
    case ECireControl::Root: return FLinearColor(.5f, .85f, .25f, 1);
    case ECireControl::Fear: return FLinearColor(.6f, .3f, .9f, 1);
    case ECireControl::Disarm: return FLinearColor(.8f, .8f, .85f, 1);
    default: return FLinearColor(1.f, .5f, .2f, 1);
    }
}
bool IsHardCallout(ECireControl C) { return C == ECireControl::Stun || C == ECireControl::Silence || C == ECireControl::HealCut || C == ECireControl::Fear || C == ECireControl::Disarm || C == ECireControl::Root; }
}

// ---------------------------------------------------------------------------
// Callouts
// ---------------------------------------------------------------------------
void ACireHUD::UpdateEffectCallouts(ACireHero* Hero)
{
    if (!Hero) return;
    const float Now = CireBuffs::ServerNow(GetWorld());
    TArray<FCireActiveEffect> Effects; CireEffects::Gather(Hero, Now, Effects, Hero);
    TSet<FName> Current;
    for (const FCireActiveEffect& E : Effects)
    {
        Current.Add(E.Id);
        if (!bEffectsSeeded || SeenEffectIds.Contains(E.Id)) continue;
        const FCireEffectInfo* I = CireEffects::Find(E.Id);
        if (!I || !I->bCallout) continue;
        const bool bHard = IsHardCallout(I->Control);
        if (bHard ? !UISettings.bControlAlerts : !UISettings.bEffectCallouts) continue;
        EffectCallouts.Push(E.Id, E.End > Now ? E.End - Now : 0.f, bHard, GetWorld()->GetRealTimeSeconds());
    }
    SeenEffectIds = MoveTemp(Current); bEffectsSeeded = true;
}
void ACireHUD::DrawEffectCallouts(ACireHero* Hero)
{
    const double Real = GetWorld()->GetRealTimeSeconds();
    const FCireCallout* C = EffectCallouts.Tick(Real);
    if (!C) return;
    const FCireEffectInfo* I = CireEffects::Find(C->Id);
    if (!I) return;
    ResetTransform();
    const float Age = static_cast<float>(Real - C->Shown), Life = EffectCallouts.Life;
    const float In = FMath::Clamp(Age / .18f, 0.f, 1.f), Out = FMath::Clamp((Life - Age) / .45f, 0.f, 1.f);
    FCireUIPainter P = Painter(); P.Alpha = FMath::Min(In, Out);
    const FLinearColor Accent = I->IsHarmful() ? (I->Dispel == ECireDispel::Curse || I->Control == ECireControl::Silence ? FLinearColor(.75f, .4f, 1.f, 1) : FLinearColor(1.f, .3f, .25f, 1))
                                                : FLinearColor(1.f, .82f, .3f, 1);
    const FString Symbols = CireEffects::Symbols(*I, C->Duration);
    if (C->bControl)
    {
        // Prominent centre callout: "STUNNED" with the duration, pop-in scale.
        const FLinearColor CC = ControlColor(I->Control);
        const FString Word = CireEffects::ControlWord(I->Control);
        const float Pop = 1.f + .35f * (1.f - In), TS = 34.f * Pop, Y = ViewH * .36f;
        const float CX = ViewW * .5f; // hard CC: dead centre, above the character
        const float TW = P.TextWidth(Word, TS, ECireFont::Heading);
        for (int32 K = 0; K < 6; ++K) { const float Inset = K * 18.f; P.Rect(CX - TW * .5f - 90 + Inset, Y - 10, TW + 180 - 2 * Inset, TS + 40, FLinearColor(0, 0, 0, .1f)); }
        P.Text(Word, CX - TW * .5f, Y, TS, CC, ECireFont::Heading, true, true);
        const FString Sub = I->Name + (C->Duration > 0 ? TEXT("  ·  ") + CireEffects::DurationText(C->Duration) : FString()) + TEXT("  ·  ") + I->Line;
        const FString Fitted = P.Fit(Sub, 12, 560, ECireFont::Body);
        P.Text(Fitted, CX - P.TextWidth(Fitted, 12, ECireFont::Body) * .5f, Y + TS + 6, 12, FLinearColor(.95f, .93f, .88f, 1), ECireFont::Body, true, true);
        return;
    }
    // Light callout: icon + name + one-line symbols, a smaller cousin of the zone banner.
    const float Slide = 1.f - FMath::Square(1.f - In);
    const float Y = ViewH * .30f - (1.f - Slide) * 10.f;
    const FString Title = I->Name, Line = Symbols.IsEmpty() ? I->Line : Symbols;
    const float W = FMath::Clamp(FMath::Max(P.TextWidth(Title, 15, ECireFont::Bold), P.TextWidth(Line, 11, ECireFont::Body)) + 78.f, 220.f, 460.f);
    const float CX = CentreGapX(Y - 10, Y + 50), X = CX - W * .5f;
    for (int32 K = 0; K < 5; ++K) { const float Inset = K * W * .07f; P.Rect(X + Inset, Y - 4, W - 2 * Inset, 50, FLinearColor(0, 0, 0, .12f)); }
    P.Line(X + W * .1f, Y - 4, X + W * .9f, Y - 4, Accent * FLinearColor(1, 1, 1, .8f), 1.2f);
    P.Line(X + W * .1f, Y + 46, X + W * .9f, Y + 46, Accent * FLinearColor(1, 1, 1, .8f), 1.2f);
    FCireActiveEffect Fake; Fake.Id = C->Id;
    DrawEffectIcon(Fake, *I, X + 18, Y + 5, 32, -1.f, 0.f);
    P.Text(I->IsHarmful() ? TEXT("DEBUFF") : TEXT("BUFF"), X + 60, Y, 8, Accent, ECireFont::Heading, true, false);
    P.Text(P.Fit(Title, 15, W - 70, ECireFont::Bold), X + 60, Y + 9, 15, Accent, ECireFont::Bold, true, true);
    P.Text(P.Fit(Line, 11, W - 70, ECireFont::Body), X + 60, Y + 29, 11, FLinearColor(.95f, .93f, .88f, 1), ECireFont::Body, true, true);
}
void ACireHUD::DrawControlEdge(ACireHero* Hero)
{
    if (!Hero || !UISettings.bControlAlerts) return;
    float Remaining = 0.f;
    const ECireControl C = CireEffects::HardControl(Hero, CireBuffs::ServerNow(GetWorld()), &Remaining);
    if (C != ECireControl::Stun && C != ECireControl::Silence && C != ECireControl::Fear && C != ECireControl::Disarm) return;
    ResetTransform();
    FCireUIPainter P = Painter();
    const FLinearColor Col = ControlColor(C);
    const float Pulse = .65f + .35f * FMath::Sin(static_cast<float>(GetWorld()->GetRealTimeSeconds()) * 5.f);
    for (int32 K = 0; K < 8; ++K)
    {
        const float T = 5.f + K * 6.f; const float A = .07f * Pulse * (1.f - K / 8.f);
        const FLinearColor F(Col.R, Col.G, Col.B, A);
        P.Rect(0, 0, ViewW, T, F); P.Rect(0, ViewH - T, ViewW, T, F); P.Rect(0, T, T, ViewH - 2 * T, F); P.Rect(ViewW - T, T, T, ViewH - 2 * T, F);
    }
    // A small persistent status plate under the centre while the control lasts.
    const FString Text = CireEffects::FormatControl(C, Remaining);
    const float TS = 14.f, TW = P.TextWidth(Text, TS, ECireFont::Heading), Y = ViewH * .62f;
    P.Rect(ViewW * .5f - TW * .5f - 14, Y - 4, TW + 28, TS + 12, FLinearColor(0, 0, 0, .55f));
    P.Text(Text, ViewW * .5f - TW * .5f, Y, TS, Col, ECireFont::Heading, true, true);
}

// ---------------------------------------------------------------------------
// Cast bars
// ---------------------------------------------------------------------------
bool ACireHUD::DrawCastBar(const AActor* Unit, float X, float Y, float W, float H, float TS, bool bShowTime)
{
    const FCireCastView V = CireCasts::Get(Unit, CireBuffs::ServerNow(GetWorld()));
    FCireUIPainter P = Painter();
    if (!V.bCasting)
    {
        if ((V.Result != ECireCastResult::Interrupted && V.Result != ECireCastResult::Silenced) || V.ResultAge > 1.1f) return false;
        // Flash: the bar turns red and reads INTERRUPTED / SILENCED for about a second.
        const float A = FMath::Clamp((1.1f - V.ResultAge) / .4f, 0.f, 1.f);
        const FLinearColor Col = V.Result == ECireCastResult::Silenced ? FLinearColor(.7f, .35f, 1.f, A) : FLinearColor(.95f, .18f, .12f, A);
        if (!(CireUIStyle::HasThemeArt() && CireUIStyle::Capsule(P, X - 1, Y - 1, W + 2, H + 2, 1, FLinearColor(0, 0, 0, .85f * A)) && CireUIStyle::Capsule(P, X, Y, W, H, 1, Col * FLinearColor(.75f, .75f, .75f, 1))))
        { P.Rect(X, Y, W, H, FLinearColor(0, 0, 0, .85f * A)); P.Rect(X + 1, Y + 1, W - 2, H - 2, Col * FLinearColor(.75f, .75f, .75f, 1)); }
        const FString Word = V.Result == ECireCastResult::Silenced ? TEXT("SILENCED") : TEXT("INTERRUPTED");
        if (H >= 9.f) P.Text(Word, X + (W - P.TextWidth(Word, TS, ECireFont::Heading)) * .5f, Y + (H - TS) * .5f - 1.5f, TS, FLinearColor(1, 1, 1, A), ECireFont::Heading, true, false);
        return true;
    }
    const FLinearColor Fill = V.bHeal ? HealGreen : V.bInterruptible ? CastGold : CastGrey;
    // ui-themes: the kit cast bar (themed frame + fill); the name/time are drawn below.
    CireUIStyle::CastBar(P, X, Y, W, H, V.Progress, Fill, FString(), FString());
    float TextX = X + 4;
    if (!V.bInterruptible)
    {
        // Shield icon: this cast cannot be interrupted (WoW).
        const float S = H - 2, SX = X - S - 2, SY = Y + 1;
        P.Tri(FVector2D(SX, SY), FVector2D(SX + S, SY), FVector2D(SX + S * .5f, SY + S), FLinearColor(.78f, .8f, .86f, 1));
        P.Rect(SX, SY, S, S * .45f, FLinearColor(.78f, .8f, .86f, 1));
        P.Line(SX + S * .5f, SY + 1, SX + S * .5f, SY + S - 2, FLinearColor(.3f, .32f, .38f, 1), 1.f);
    }
    const FString Rem = bShowTime ? FString::Printf(TEXT("%.1f"), V.Remaining) : FString();
    const float RemW = Rem.IsEmpty() ? 0.f : P.TextWidth(Rem, TS, ECireFont::Numbers);
    if (H >= 9.f) P.Text(P.Fit(V.Name, TS, W - 12 - RemW, ECireFont::Bold), TextX, Y + (H - TS) * .5f - 1.5f, TS, FLinearColor::White, ECireFont::Bold, true, false);
    if (!Rem.IsEmpty()) P.Text(Rem, X + W - 4 - RemW, Y + (H - TS) * .5f - 1.5f, TS, FLinearColor::White, ECireFont::Numbers, true, false);
    return true;
}
void ACireHUD::DrawPlayerCastBar(ACireHero* Hero)
{
    if (!Hero || !UISettings.bPlayerCastBar) return;
    ResetTransform();
    // Centred above the highest action bar and the movement hint.
    const float W = 300, H = 18, X = (ViewW - W) * .5f, Y = ActionBarsTop() - 40;
    const FCireCastView V = CireCasts::Get(Hero, CireBuffs::ServerNow(GetWorld()));
    if (!V.bCasting && V.Result == ECireCastResult::None) return;
    if (V.bCasting || V.ResultAge < 1.1f)
    {
        FCireUIPainter P = Painter();
        if (!CireUIStyle::HasThemeArt()) CireUIStyle::Frame(P, X - 4, Y - 4, W + 8, H + 8, CastGold, ECireFrame::Inset); // themed cast frame carries its own border
        DrawCastBar(Hero, X, Y, W, H, 11.f);
    }
}
void ACireHUD::DrawControlBadge(const AActor* Unit, float X, float Y, float Size)
{
    float Remaining = 0.f;
    const ECireControl C = CireEffects::HardControl(Unit, CireBuffs::ServerNow(GetWorld()), &Remaining);
    if (C == ECireControl::None) return;
    FCireUIPainter P = Painter();
    const FString Badge = CireEffects::ControlBadge(C);
    const float TW = P.TextWidth(Badge, Size, ECireFont::Heading);
    const FLinearColor Col = ControlColor(C);
    P.Rect(X, Y, TW + 8, Size + 5, FLinearColor(0, 0, 0, .85f));
    P.Line(X, Y, X + TW + 8, Y, Col, 1.2f); P.Line(X, Y + Size + 5, X + TW + 8, Y + Size + 5, Col, 1.2f);
    P.Text(Badge, X + 4, Y + 1, Size, Col, ECireFont::Heading, true, false);
}

// ---------------------------------------------------------------------------
// Overhead status indicators (above every unit's nameplate / head)
// ---------------------------------------------------------------------------
namespace
{
struct FChip
{
    FString Label;          // "STUN", "ATK", "DEF", "SPD", or empty (dot)
    int32 Arrow = 0;        // +1 up, -1 down, 0 none
    FLinearColor Color = FLinearColor::White;
    bool bCC = false, bMine = false, bHarmful = false;
    float Remaining = -1.f, Total = 0.f;
    FName Id;
    FString TipTitle, TipBody;
    int32 Priority = 0;
};
// Stats drawn as arrow chips; everything else becomes a dot chip with its name on hover.
FString ArrowStat(const FString& Stat)
{
    if (Stat == TEXT("ATK") || Stat == TEXT("Damage Dealt")) return TEXT("ATK");
    if (Stat == TEXT("DEF") || Stat == TEXT("Armor")) return TEXT("DEF");
    if (Stat == TEXT("Move")) return TEXT("SPD");
    if (Stat == TEXT("Haste")) return TEXT("AS");
    if (Stat == TEXT("Healing")) return TEXT("HEAL");
    return FString();
}
}
void ACireHUD::DrawOverheadStatus(const AActor* Unit, float CX, float BottomY, float Fade, bool bNear)
{
    if (!IsValid(Unit) || UISettings.OverheadStatusMode >= 2) return;
    const float Now = CireBuffs::ServerNow(GetWorld());
    const AActor* Local = PlayerOwner ? PlayerOwner->GetPawn() : nullptr;
    TArray<FCireActiveEffect> Effects; CireEffects::Gather(Unit, Now, Effects, Local);
    if (Effects.IsEmpty()) return;
    TArray<FChip> Chips; TMap<FString, int32> StatChip;
    for (const FCireActiveEffect& E : Effects)
    {
        const FCireEffectInfo* I = CireEffects::Find(E.Id);
        if (!I || I->Kind == ECireEffectKind::Passive || I->Kind == ECireEffectKind::Aura) continue;
        const float Rem = E.End > Now ? E.End - Now : -1.f, Tot = E.End > E.Start ? E.End - E.Start : 0.f;
        const FString Symbols = CireEffects::Symbols(*I, Rem);
        if (I->Control != ECireControl::None && I->Control != ECireControl::Taunt)
        {
            FChip C; C.Label = CireEffects::ControlBadge(I->Control); C.bCC = true; C.bHarmful = true; C.bMine = E.bFromLocalPlayer;
            C.Color = I->Control == ECireControl::Silence || I->Control == ECireControl::HealCut ? FLinearColor(.75f, .35f, 1.f, 1) : FLinearColor(1.f, .22f, .15f, 1);
            C.Remaining = Rem; C.Total = Tot; C.Id = E.Id; C.TipTitle = I->Name; C.TipBody = Symbols + TEXT("\n") + I->Line;
            C.Priority = 100 + static_cast<int32>(I->Control);
            if (I->Control == ECireControl::Slow)
            {
                // A slow is a speed change: one SPD-down chip, not a CC plate.
                C.Label = TEXT("SPD"); C.Arrow = -1; C.bCC = false; C.Color = FLinearColor(.45f, .7f, 1.f, 1); C.Priority = 45;
                if (StatChip.Contains(TEXT("SPD"))) continue;
                StatChip.Add(TEXT("SPD"), Chips.Num());
            }
            Chips.Add(C);
            continue;
        }
        bool bAny = false;
        for (const FCireStatMod& M : I->Mods)
        {
            const FString Label = ArrowStat(M.Stat);
            if (Label.IsEmpty() || M.Value == 0.f) continue;
            const bool bUp = M.Value > 0;
            if (const int32* Existing = StatChip.Find(Label)) { Chips[*Existing].TipBody += TEXT("\n") + I->Name + TEXT(": ") + CireEffects::FormatMod(M); bAny = true; continue; }
            FChip C; C.Label = Label; C.Arrow = bUp ? 1 : -1; C.bHarmful = !bUp; C.bMine = E.bFromLocalPlayer;
            C.Color = bUp ? (Label == TEXT("DEF") ? FLinearColor(.45f, .9f, .45f, 1) : FLinearColor(1.f, .82f, .3f, 1)) : FLinearColor(1.f, .3f, .25f, 1);
            C.Remaining = Rem; C.Total = Tot; C.Id = E.Id; C.TipTitle = Label + (bUp ? TEXT(" up") : TEXT(" down"));
            C.TipBody = I->Name + TEXT(": ") + CireEffects::FormatMod(M) + (Rem > 0 ? FString(TEXT("  ·  ")) + CireEffects::DurationText(Rem) : FString());
            C.Priority = bUp ? 30 : 40;
            StatChip.Add(Label, Chips.Num()); Chips.Add(C); bAny = true;
        }
        if (!bAny && bNear)
        {
            FChip C; C.bHarmful = I->IsHarmful(); C.bMine = E.bFromLocalPlayer; C.Color = CireEffects::BorderColor(*I);
            C.Remaining = Rem; C.Total = Tot; C.Id = E.Id; C.TipTitle = I->Name; C.TipBody = (Symbols.IsEmpty() ? FString() : Symbols + TEXT("\n")) + I->Line; C.Priority = C.bHarmful ? 15 : 10;
            Chips.Add(C);
        }
    }
    // Far away: only hard crowd control.
    if (!bNear) Chips.RemoveAll([](const FChip& C) { return !C.bCC; });
    if (Chips.IsEmpty()) return;
    Chips.StableSort([](const FChip& A, const FChip& B) { return A.Priority > B.Priority; });
    const int32 Max = 4, Shown = FMath::Min(Chips.Num(), Max);
    FCireUIPainter P = Painter(); P.Alpha = Fade;
    const double Real = GetWorld()->GetRealTimeSeconds();
    TArray<float> Widths; float Total = 0.f;
    for (int32 K = 0; K < Shown; ++K)
    {
        const FChip& C = Chips[K]; const float TS = C.bCC ? 10.5f : 9.f;
        const float W = C.Label.IsEmpty() ? 11.f : P.TextWidth(C.Label, TS, ECireFont::Heading) + (C.Arrow ? 10.f : 0.f) + (C.bCC ? 21.f : 8.f);
        Widths.Add(W); Total += W + 3.f;
    }
    const FString Overflow = Chips.Num() > Max ? FString::Printf(TEXT("+%d"), Chips.Num() - Max) : FString();
    if (!Overflow.IsEmpty()) Total += P.TextWidth(Overflow, 8, ECireFont::Numbers) + 3.f;
    float X = CX - Total * .5f;
    for (int32 K = 0; K < Shown; ++K)
    {
        const FChip& C = Chips[K]; const float W = Widths[K], H = C.bCC ? 19.f : 15.f, Y = BottomY - H; // readability: chips fit the larger text
        const FString Key = FString::Printf(TEXT("%u:%s"), Unit->GetUniqueID(), *C.Id.ToString());
        FVector2D& Seen = OverheadSeen.FindOrAdd(Key); if (Seen.X <= 0.0 || Real - Seen.Y > 1.0) Seen.X = Real; Seen.Y = Real; // re-applied -> pops again
        const float Pop = 1.f + .35f * FMath::Clamp(1.f - static_cast<float>(Real - Seen.X) / .22f, 0.f, 1.f);
        const float PW = W * Pop, PH = H * Pop, PX = X + (W - PW) * .5f, PY = Y + (H - PH);
        P.Rect(PX, PY, PW, PH, FLinearColor(0, 0, 0, .78f));
        P.Rect(PX + 1, PY + 1, PW - 2, PH - 2, C.Color * FLinearColor(.28f, .28f, .28f, .85f));
        const FLinearColor Edge = C.bMine ? FLinearColor(1, 1, 1, 1) : C.Color;
        P.Line(PX, PY, PX + PW, PY, Edge, C.bMine ? 1.6f : 1.f); P.Line(PX, PY + PH, PX + PW, PY + PH, Edge, 1.f);
        P.Line(PX, PY, PX, PY + PH, Edge, 1.f); P.Line(PX + PW, PY, PX + PW, PY + PH, Edge, 1.f);
        float TX = PX + 4;
        if (C.bCC)
        {
            const float R = PH * .32f, RX = PX + 3 + R, RY = PY + PH * .5f;
            P.Circle(RX, RY, R, FLinearColor(0, 0, 0, .8f), 2.2f, 16);
            const float Frac = C.Total > 0 && C.Remaining > 0 ? C.Remaining / C.Total : 1.f;
            const int32 Seg = FMath::Max(1, FMath::RoundToInt(16 * Frac));
            for (int32 S = 0; S < Seg; ++S) { const float A = -PI * .5f + S * 2 * PI / 16, B = -PI * .5f + (S + 1) * 2 * PI / 16; P.Line(RX + FMath::Cos(A) * R, RY + FMath::Sin(A) * R, RX + FMath::Cos(B) * R, RY + FMath::Sin(B) * R, C.Color, 2.f); }
            TX = PX + 2 * R + 7;
        }
        const float TS = (C.bCC ? 10.5f : 9.f) * Pop;
        if (!C.Label.IsEmpty()) P.Text(C.Label, TX, PY + (PH - CireUIStyle::ReadableSize(TS) * 1.28f) * .5f, TS, C.bCC ? C.Color * 1.2f : FLinearColor(1.f, .96f, .88f, 1), ECireFont::Heading, true, false);
        if (C.Arrow)
        {
            const float AX = PX + PW - 7, AY = PY + PH * .5f, S = 3.2f * Pop;
            if (C.Arrow > 0) P.Tri(FVector2D(AX - S, AY + S * .8f), FVector2D(AX + S, AY + S * .8f), FVector2D(AX, AY - S), C.Color * 1.2f);
            else P.Tri(FVector2D(AX - S, AY - S * .8f), FVector2D(AX + S, AY - S * .8f), FVector2D(AX, AY + S), C.Color * 1.2f);
        }
        else if (C.Label.IsEmpty()) P.Disc(PX + PW * .5f, PY + PH * .5f, 3.f, C.Color, 10);
        Tip(C.TipTitle, C.TipBody, PX, PY, PW, PH);
        X += W + 3.f;
    }
    if (!Overflow.IsEmpty()) P.Text(Overflow, X, BottomY - 11, 8, FLinearColor(.9f, .9f, .9f, 1), ECireFont::Numbers, true, false);
    if (OverheadSeen.Num() > 512) OverheadSeen.Reset();
}
