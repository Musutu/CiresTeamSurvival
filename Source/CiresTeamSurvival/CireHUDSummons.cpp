// fix/summons: the summons bar in the HUD "Pet" panel (Docs/Pets.md "Summons bar").
// Up to three units draw as named rows (icon, name, timer, health bar); more switch to compact
// icon tiles (icon, timer, health strip), five per row, with a "+N" overflow badge. Commandable
// summons (the Oathbound Guardian) get ATTACK / FOLLOW / STAY buttons in the header; the pet
// attack/follow/stay keys order them too. Painted with the HUD theme (CireUIStyle).
#include "CireHUD.h"
#include "CireGame.h"
#include "CireSummon.h"
#include "CireSummonsBar.h"
#include "CireUIStyle.h"
#include "Engine/World.h"

namespace
{
float SummonFraction(float A, float B) { return B > 0 ? FMath::Clamp(A / B, 0.f, 1.f) : 0.f; }
FString SummonHelp(const FCireSummonBarEntry& E)
{
    FString Body = FString::Printf(TEXT("Health %.0f / %.0f. %s."), E.Health, E.MaxHealth, *E.State.Left(1).ToUpper().Append(E.State.Mid(1).ToLower()));
    if (E.Remaining >= 0) Body += FString::Printf(TEXT(" Expires in %s."), *CireSummonsBar::TimerText(E.Remaining));
    Body += E.Kind == ECireSummonBarKind::Summon
        ? (E.bCommandable ? TEXT(" Fights on its own: your target first, then anything attacking you or your summons. Use the buttons or your pet keys to order it.")
                          : TEXT(" Fights on its own: your target first, then anything attacking you or your summons."))
        : (E.bFights ? TEXT(" Attacks enemies in range by itself. Monsters can smash it.") : TEXT(" Monsters can smash it."));
    return Body;
}
}

void ACireHUD::DrawSummonsBar(ACireHero* Hero, ACireController* Controller, const TArray<FCireSummonBarEntry>& Units, float Y0)
{
    FCireUIPainter P = Painter();
    const double Now = GetWorld()->GetRealTimeSeconds();
    const FLinearColor Accent = CireUIColors::Purple;
    const bool bRows = Units.Num() <= CireSummonsBar::MaxRows;
    const int32 Shown = FMath::Min(Units.Num(), CireSummonsBar::MaxShown);
    constexpr int32 Cols = 5; constexpr float Tile = 40.f, Gap = 8.f, RowH = 28.f, Header = 26.f;
    const int32 TileRows = FMath::DivideAndRoundUp(FMath::Max(Shown, 1), Cols);
    const float Body = bRows ? FMath::Max(1, Units.Num()) * RowH : TileRows * (Tile + 8.f);
    const float H = FMath::Max(Y0 > 0 ? 0.f : 112.f, Header + Body + 6.f);
    CireUIStyle::Frame(P, 0, Y0, 250, H, Accent, ECireFrame::Unit);
    const float Left = 8.f;
    // Header: title (and count), then the order buttons when a commandable summon is out.
    bool bCommandable = false; const FCireSummonBarEntry* Lead = nullptr;
    for (const auto& E : Units) if (E.bCommandable) { bCommandable = true; Lead = &E; break; }
    P.Text(TEXT("SUMMONS"), Left, Y0 + 5, 12, CireUIColors::Parchment, ECireFont::Heading);
    if (!bCommandable)
    {
        const FString Count = Units.IsEmpty() ? FString(TEXT("NONE ACTIVE")) : FString::Printf(TEXT("%d ACTIVE"), Units.Num());
        P.Text(Count, 242 - P.TextWidth(Count, 10, ECireFont::Bold), Y0 + 7, 10, CireUIColors::Gold, ECireFont::Bold);
    }
    else
    {
        const auto& Keys = UISettings.Keybindings;
        const TCHAR* Names[] = {TEXT("ATTACK"), TEXT("FOLLOW"), TEXT("STAY")};
        const FName Actions[] = {TEXT("PetAttack"), TEXT("PetFollow"), TEXT("PetStay")};
        const int32 Orders[] = {2, 0, 3}; // ACireController::ServerSummonCommand: 0 follow, 1 move, 2 attack, 3 hold
        const TCHAR* Help[] = {TEXT("Send your commandable summons at your selected hostile target (the nearest enemy if none is selected)."),
            TEXT("Your commandable summons return to your side. They still defend you and assist your target."),
            TEXT("Your commandable summons hold this spot and fight anything that comes into reach. Shift + left click on the ground moves them.")};
        const ACireSummon* Unit = Lead ? Cast<ACireSummon>(Lead->Unit.Get()) : nullptr;
        const ECireSummonCommand Current = Unit ? Unit->CurrentCommand : Lead && Lead->State == TEXT("ATTACKING") ? ECireSummonCommand::Attack : ECireSummonCommand::Follow;
        const bool bInteractive = !bModal && !bSettings && !bEditLayout;
        for (int32 I = 0; I < 3; ++I)
        {
            const float W = 48, X = 242 - (3 - I) * W - (2 - I) * 4, Y = Y0 + 4, BH = 19;
            const bool bOver = Hit(X, Y, W, BH);
            const bool bActive = (I == 0 && Current == ECireSummonCommand::Attack) || (I == 1 && Current == ECireSummonCommand::Follow) ||
                (I == 2 && (Current == ECireSummonCommand::Hold || Current == ECireSummonCommand::Move));
            CireUIStyle::Button(P, X, Y, W, BH, Names[I], bActive ? ECireButtonState::Selected : bOver ? ECireButtonState::Hover : ECireButtonState::Normal, Accent, 9.f);
            const FString Key = Keys.Label(Actions[I]);
            Tip(FString::Printf(TEXT("%s (%s)"), Names[I], *Key), Help[I], X, Y, W, BH);
            if (bInteractive && Clicked && bOver && Controller && Hero)
            { Controller->ServerSummonCommand(Orders[I], Hero->Target, Hero->GetActorLocation()); Clicked = false; PlayUIFeedback(); }
        }
    }
    if (Units.IsEmpty())
    {
        Label(TEXT("No active summons or constructs."), Left, Y0 + Header + 4, 10, CireUIColors::Muted);
        Tip(TEXT("Summons"), TEXT("Every summon and construct you own appears here with its health and time left."), 0, Y0, 250, H);
        return;
    }
    const float Top = Y0 + Header;
    auto Icon = [&](const FCireSummonBarEntry& E, float X, float Y, float Size)
    {
        FCireIconSlot S; S.IconId = E.IconId; S.IconTexture = CireUIStyle::FindAbilityIcon(E.IconId); S.Tint = Accent;
        S.bGlow = E.bCommandable && E.State == TEXT("ATTACKING");
        CireUIStyle::IconSlot(P, X, Y, Size, S, Now);
    };
    auto TimerColor = [](float Remaining) { return Remaining < 5.f ? CireUIColors::Orange : CireUIColors::Parchment; };
    if (bRows)
    {
        for (int32 I = 0; I < Units.Num(); ++I)
        {
            const FCireSummonBarEntry& E = Units[I];
            const float Y = Top + I * RowH;
            Icon(E, Left, Y, 24);
            const FString Timer = E.Remaining >= 0 ? CireSummonsBar::TimerText(E.Remaining) : FString();
            const float TimerW = Timer.IsEmpty() ? 0.f : P.TextWidth(Timer, 11, ECireFont::Numbers);
            P.Text(P.Fit(E.Name, 11, 204 - TimerW - 8, ECireFont::Bold), Left + 30, Y - 2, 11, CireUIColors::Parchment, ECireFont::Bold);
            if (!Timer.IsEmpty()) P.Text(Timer, 242 - TimerW, Y - 2, 11, TimerColor(E.Remaining), ECireFont::Numbers);
            const uint64 TrailKey = 0x5E000000ull + static_cast<uint64>(GetTypeHash(E.Unit));
            CireUIStyle::Bar(P, Left + 30, Y + 14, 204, 10, SummonFraction(E.Health, E.MaxHealth), CireUIColors::Health, &BarTrails.FindOrAdd(TrailKey), Now,
                FString::Printf(TEXT("%.0f"), E.Health), 8.5f);
            Tip(E.Name, SummonHelp(E), Left, Y, 234, RowH - 2);
        }
    }
    else
    {
        for (int32 I = 0; I < Shown; ++I)
        {
            const FCireSummonBarEntry& E = Units[I];
            const float X = Left + (I % Cols) * (Tile + Gap), Y = Top + (I / Cols) * (Tile + 8.f);
            Icon(E, X + 4, Y, Tile - 8);
            if (E.Remaining >= 0)
            {
                const FString Timer = CireSummonsBar::TimerText(E.Remaining);
                P.Text(Timer, X + (Tile - P.TextWidth(Timer, 11, ECireFont::Numbers)) * .5f, Y + Tile - 22, 11, TimerColor(E.Remaining), ECireFont::Numbers, true);
            }
            const uint64 TrailKey = 0x5E000000ull + static_cast<uint64>(GetTypeHash(E.Unit));
            CireUIStyle::Bar(P, X, Y + Tile - 6, Tile, 5, SummonFraction(E.Health, E.MaxHealth), CireUIColors::Health, &BarTrails.FindOrAdd(TrailKey), Now);
            if (I == Shown - 1 && Units.Num() > Shown)
            {
                const FString More = FString::Printf(TEXT("+%d"), Units.Num() - Shown);
                P.Disc(X + Tile - 4, Y + 4, 9, FLinearColor(0, 0, 0, .85f));
                P.Text(More, X + Tile - 4 - P.TextWidth(More, 10, ECireFont::Numbers) * .5f, Y - 3, 10, CireUIColors::BrightGold, ECireFont::Numbers);
            }
            Tip(E.Name, SummonHelp(E), X, Y, Tile, Tile);
        }
    }
    LastSummonsDrawn = bRows ? Units.Num() : Shown;
}
