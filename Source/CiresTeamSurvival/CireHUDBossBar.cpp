// bosses-spacing: the raid-boss health bar (WoW encounter style) at the top of the screen, layout panel "RaidBoss".
// The most important boss (lane boss first, then the one you target / fight, then the nearest) gets the big ornate
// bar: portrait with the boss spikes, name in the Display face, a subtitle, a tall health bar with its damage trail,
// phase ticks at the boss's authored health thresholds (enrage / shield wall / heal) and the current phase, the
// cast bar or what it is doing. Further bosses stack below as slimmer bars. Every piece is drawn with the CireUIStyle
// theme layer (frames, bar frame, ornament, divider, portrait ring, medallion, theme colours), so all four HUD themes
// restyle it. Shown with the boss frames (Options: "Boss / pack leader frames"); click a bar to target that boss.
#include "CireHUD.h"
#include "CireGame.h"
#include "CireNPCArchetypes.h"
#include "CireNPCState.h"
#include "CireOutdoorBosses.h"
#include "CireUIStyle.h"
#include "CireUnitSpacing.h"
#include "CireActorIterator.h"
#include "Engine/World.h"

namespace
{
float RaidFrac(float A, float B) { return B > 0 ? FMath::Clamp(A / B, 0.f, 1.f) : 0.f; }
FString RaidNumber(float V)
{
    const double D = FMath::Max(0.0, static_cast<double>(V));
    if (D >= 1.e6) return FString::Printf(TEXT("%.2fM"), D / 1.e6);
    if (D >= 1.e4) return FString::Printf(TEXT("%.1fK"), D / 1.e3);
    return FString::Printf(TEXT("%.0f"), D);
}
bool RaidBoss(const ACireMonster* M) { return CireUnitSpacing::IsBossBody(M); }
FString RaidSubtitle(const ACireMonster* M)
{
    if (M->IsLaneBoss()) return TEXT("Siege Host  -  a leak costs 10 lives");
    if (CireOutdoorBosses::IsOutdoorBoss(M)) return M->bNeutral ? TEXT("World Boss  -  neutral until provoked") : TEXT("World Boss");
    if (M->PackId >= 0) return TEXT("Pack Leader");
    return TEXT("Boss");
}
}

TArray<float> ACireHUD::RaidBossPhases(const ACireMonster* M)
{
    TArray<float> Out;
    if (const FCireNPCArchetype* A = M && M->NPCState ? M->NPCState->Archetype() : nullptr)
        for (const FCireNPCAbility& Ab : A->Abilities)
            if (Ab.HealthThreshold > .02f && Ab.HealthThreshold < .98f && (Ab.Kind == ECireNPCAbilityKind::Enrage || Ab.Kind == ECireNPCAbilityKind::ShieldWall))
                Out.AddUnique(FMath::RoundToFloat(Ab.HealthThreshold * 100.f) / 100.f);
    if (Out.IsEmpty()) Out = {.75f, .5f, .25f}; // no authored phase: the classic raid quarter marks
    Out.Sort([](float A, float B) { return A > B; });
    return Out;
}

void ACireHUD::DrawRaidBossBars(ACireHero* Hero, ACireController* Controller)
{
    LastRaidBars = 0;
    if (!Hero || (!UISettings.bShowBossFrames && !bEditLayout)) return;
    const auto* State = GetWorld()->GetGameState<ACireGameState>();
    if (State && State->Phase == 2 && !bEditLayout) return;
    TArray<ACireMonster*> Units;
    for (TCireActorIterator<ACireMonster> It(GetWorld()); It; ++It)
    {
        ACireMonster* M = *It;
        if (!IsValid(M) || M->Health <= 0 || M->Lane != Hero->TeamId || !RaidBoss(M)) continue;
        const float Dist = FVector::Dist2D(M->GetActorLocation(), Hero->GetActorLocation());
        if (M->IsLaneBoss() || Hero->Target == M || IsValid(M->Victim) || Dist < 4200.f) Units.Add(M);
    }
    const auto Rank = [&](const ACireMonster& M) { return (M.IsLaneBoss() ? 0 : 4) + (Hero->Target == &M ? 0 : 2) + (IsValid(M.Victim) ? 0 : 1); };
    Units.Sort([&](const ACireMonster& A, const ACireMonster& B) {
        if (Rank(A) != Rank(B)) return Rank(A) < Rank(B);
        return FVector::DistSquared(A.GetActorLocation(), Hero->GetActorLocation()) < FVector::DistSquared(B.GetActorLocation(), Hero->GetActorLocation()); });
    if (Units.IsEmpty() && !bEditLayout) return;
    constexpr float W = 480.f, H = 118.f, BigH = 54.f, SmallH = 30.f;
    UsePanel(TEXT("RaidBoss"), W, H);
    FCireUIPainter P = Painter();
    const double Now = GetWorld()->GetRealTimeSeconds();
    if (Units.IsEmpty())
    {
        CireUIStyle::Frame(P, 0, 0, W, BigH, CireUIColors::ThemeAccent, ECireFrame::Card);
        P.Text(TEXT("RAID BOSS BAR"), 14, 8, 11, CireUIColors::Muted, ECireFont::Display, true);
        P.Text(TEXT("Bosses and pack leaders you fight appear here"), 14, 28, 9, CireUIColors::Muted, ECireFont::Body);
        return;
    }
    const FLinearColor BossRed(.80f, .10f, .07f, 1.f), Accent = CireUIColors::ThemeAccent, Trim = CireUIColors::Gold;
    float Y = 0;
    for (int32 I = 0; I < FMath::Min(Units.Num(), 3); ++I)
    {
        ACireMonster* M = Units[I];
        const bool bBig = I == 0, bSelected = Hero->Target == M;
        const float RowH = bBig ? BigH : SmallH;
        if (Y + RowH > H + .5f) break;
        ++LastRaidBars;
        const float HF = RaidFrac(M->Health, M->MaxHealth);
        const bool bEnraged = M->NPCState && M->NPCState->HasStatus(CireNPCStatus::Enraged);
        // Backplate: the theme's card frame, glowing in the theme accent when this boss is your target.
        CireUIStyle::Frame(P, 0, Y, W, RowH, bSelected ? Accent * 1.3f : Trim, ECireFrame::Card);
        if (bSelected) CireUIStyle::Glow(P, 0, Y, W, RowH, CireUIColors::ThemeGlow * FLinearColor(1, 1, 1, .16f));
        // Portrait (the boss spikes and skull badge come with the shared unit portrait).
        const float PR = bBig ? 21.f : 11.5f, PCX = bBig ? 30.f : 18.f, PCY = Y + RowH * .5f;
        DrawPortrait(M, PCX, PCY, PR, !bBig);
        const float BX = bBig ? 62.f : 36.f, BW = W - BX - 12.f;
        const TArray<float> Phases = RaidBossPhases(M);
        int32 Phase = 1; for (float T : Phases) Phase += HF < T ? 1 : 0;
        if (bBig)
        {
            // Name (Display serif caps), subtitle, phase medallion, percent.
            const FString Pct = FString::Printf(TEXT("%.0f%%"), HF * 100.f);
            const float PctW = P.TextWidth(Pct, 13, ECireFont::Numbers);
            const FString Name = P.Fit(M->GetNPCDisplayName().ToUpper(), 13, BW - PctW - 150.f, ECireFont::Display);
            P.Text(Name, BX, Y + 3, 13, CireUIColors::TitleText, ECireFont::Display, true, true);
            const float AfterName = BX + P.TextWidth(Name, 13, ECireFont::Display) + 8.f;
            P.Text(P.Fit(RaidSubtitle(M), 8.5f, BX + BW - PctW - 60.f - AfterName, ECireFont::Body), AfterName, Y + 7, 8.5f, M->IsLaneBoss() ? CireUIColors::Hostile * .95f : CireUIColors::Muted, ECireFont::Body);
            P.Text(Pct, BX + BW - PctW, Y + 2, 13, FLinearColor::White, ECireFont::Numbers, true, true);
            CireUIStyle::Medallion(P, BX + BW - PctW - 22.f, Y + 11, 9.f, FString::FromInt(Phase), bEnraged ? CireUIColors::Hostile : CireUIColors::BrightGold);
            // The big bar: theme bar frame, damage trail, gloss; the ornament crowns its centre.
            const float BarY = Y + 21, BarH = 17;
            CireUIStyle::Bar(P, BX, BarY, BW, BarH, HF, bEnraged ? FLinearColor(1.f, .22f, .05f, 1) : BossRed, &BarTrails.FindOrAdd(0xB055000000ull ^ M->GetUniqueID()), Now,
                FString::Printf(TEXT("%s / %s"), *RaidNumber(M->Health), *RaidNumber(M->MaxHealth)), 9.5f);
            // Phase ticks: a dark notch and a theme-coloured diamond above and below; spent phases dim.
            for (float T : Phases)
            {
                const float TX = BX + BW * T; const bool bSpent = HF < T;
                const FLinearColor Tick = bSpent ? CireUIColors::Muted * FLinearColor(1, 1, 1, .7f) : Accent * 1.25f;
                P.Rect(TX - 1.f, BarY + 1, 2.f, BarH - 2, FLinearColor(0, 0, 0, .75f));
                P.Rect(TX - .5f, BarY + 1, 1.f, BarH - 2, Tick * FLinearColor(1, 1, 1, .8f));
                for (const float DY : {BarY - 1.f, BarY + BarH + 1.f})
                {
                    const float S = 3.2f, Dir = DY < BarY ? -1.f : 1.f;
                    P.Tri(FVector2D(TX - S, DY), FVector2D(TX + S, DY), FVector2D(TX, DY + Dir * S * 1.3f), FLinearColor(0, 0, 0, .8f));
                    P.Tri(FVector2D(TX - S + 1, DY), FVector2D(TX + S - 1, DY), FVector2D(TX, DY + Dir * (S * 1.3f - 1.f)), Tick);
                }
            }
            CireUIStyle::Ornament(P, BX + BW * .5f, BarY - 1.f, 11.f);
            // Under the bar: its cast (interruptible gold / locked grey), else what it is doing.
            if (!DrawCastBar(M, BX, BarY + BarH + 4, BW, 10, 8.f))
            {
                const FString Doing = bEnraged ? FString(TEXT("ENRAGED")) : IsValid(M->Victim) ? (M->Victim == Hero ? FString(TEXT("Attacking YOU")) : TEXT("Attacking ") + M->Victim->HeroName)
                    : M->bNeutral ? FString(TEXT("Neutral")) : FString(TEXT("Advancing"));
                P.Text(P.Fit(Doing, 8.5f, BW * .6f, ECireFont::Body), BX + 2, BarY + BarH + 2, 8.5f, bEnraged || M->Victim == Hero ? CireUIColors::Hostile : CireUIColors::Muted, bEnraged ? ECireFont::Heading : ECireFont::Body, true);
                const FString PhaseText = FString::Printf(TEXT("PHASE %d / %d"), Phase, Phases.Num() + 1);
                P.Text(PhaseText, BX + BW - P.TextWidth(PhaseText, 8.f, ECireFont::Heading), BarY + BarH + 2.5f, 8.f, CireUIColors::Gold, ECireFont::Heading, true);
            }
        }
        else
        {
            const FString Pct = FString::Printf(TEXT("%.0f%%"), HF * 100.f);
            P.Text(P.Fit(M->GetNPCDisplayName(), 9.5f, BW * .6f, ECireFont::Bold), BX, Y + 2, 9.5f, CireUIColors::TitleText, ECireFont::Bold, true);
            P.Text(Pct, BX + BW - P.TextWidth(Pct, 9.5f, ECireFont::Numbers), Y + 2, 9.5f, FLinearColor::White, ECireFont::Numbers, true);
            const float BarY = Y + 16, BarH = 11;
            CireUIStyle::Bar(P, BX, BarY, BW, BarH, HF, bEnraged ? FLinearColor(1.f, .22f, .05f, 1) : BossRed, &BarTrails.FindOrAdd(0xB055000000ull ^ M->GetUniqueID()), Now);
            for (float T : Phases) P.Rect(BX + BW * T - .75f, BarY + 1, 1.5f, BarH - 2, HF < T ? FLinearColor(0, 0, 0, .6f) : Accent * FLinearColor(1.2f, 1.2f, 1.2f, .9f));
        }
        UnitTip(M, 0, Y, W, RowH);
        if (Clicked && Hit(0, Y, W, RowH) && !bModal && !bSettings && !bEditLayout && Controller) { Controller->ServerAction(0, 0, M); Clicked = false; }
        Y += RowH + 2.f;
        if (bBig && Units.Num() > 1) CireUIStyle::Divider(P, 40, Y - 1.f, W - 80, FLinearColor(1, 1, 1, .8f));
    }
}
