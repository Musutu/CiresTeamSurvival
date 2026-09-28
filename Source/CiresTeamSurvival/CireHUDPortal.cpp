// arena-portal: shadow-portal plates (Docs/Arenas.md "Shadow portals"). The same grammar as the merchant plates: a
// coloured emblem disc, the destination arena's name, a <Shadow Portal> title, and a prompt capsule with the countdown
// when the champion is close. Painted with the HUD theme so the portal label follows the UI scale and themes.
#include "CireHUD.h"
#include "CireArenaPortal.h"
#include "CireArenas.h"
#include "CireGame.h"
#include "CireUIStyle.h"
#include "CireBanners.h"
#include "EngineUtils.h"
#include "Engine/World.h"

void ACireHUD::DrawPortalPlates(ACireHero* Hero)
{
    if (!Hero || !GetWorld()) return;
    const auto* State = GetWorld()->GetGameState<ACireGameState>();
    const float Pulse = .5f + .5f * FMath::Sin(GetWorld()->GetRealTimeSeconds() * 4.f);
    const CireArenaPortal::FConfig& C = CireArenaPortal::Config();
    for (TActorIterator<ACireArenaPortal> It(GetWorld()); It; ++It)
    {
        ACireArenaPortal* Portal = *It;
        if (!IsValid(Portal) || !Portal->bVisualsBuilt || Portal->Open < .3f) continue;
        const float Dist = FVector::Dist(Hero->GetActorLocation(), Portal->GetActorLocation());
        if (Dist > 4200.f) continue;
        const FVector Anchor = Portal->GetActorLocation() + FVector(0, 0, C.Height + C.Radius * Portal->Open + 30.f);
        FVector2D Screen; if (!PlayerOwner->ProjectWorldLocationToScreen(Anchor, Screen, false)) continue;
        const float X = Screen.X / Scale, Y = Screen.Y / Scale; if (X < 35 || X > ViewW - 35 || Y < 90 || Y > ViewH - 160) continue;
        { const FBox2D Box(FVector2D(X - 110, Y - 44), FVector2D(X + 110, Y + 36)); bool bUnder = false; for (const FBox2D& B : LastPanelBoxes) if (Box.Intersect(B)) bUnder = true; if (bUnder) continue; }
        const CireArenaPortal::FLook L = CireArenaPortal::LookFor(Portal->ArenaIndex);
        const float Fade = FMath::Clamp(1.5f - Dist / 4200.f, .55f, 1.f) * FMath::Clamp(Portal->Open, 0.f, 1.f);
        FCireUIPainter P = Painter(); P.Alpha = Fade;
        const float R = Dist < 1400.f ? 13.f : 10.f;
        P.Disc(X, Y - R - 27, R + 2, FLinearColor(L.Tint.R, L.Tint.G, L.Tint.B, 1), 32);
        P.Disc(X, Y - R - 27, R * .62f, FLinearColor(.02f, .01f, .04f, 1), 32); // the dark eye of the rift
        const FString Name = Portal->LabelText;
        const bool bEntry = Portal->Kind == static_cast<uint8>(CireArenaPortal::EKind::Entry);
        const FString Title = bEntry ? FString(TEXT("<Shadow Portal>")) : Portal->Kind == static_cast<uint8>(CireArenaPortal::EKind::Arrival) ? FString(TEXT("<The portal closes>")) : FString(TEXT("<Returned from the arena>"));
        const FLinearColor NameColor = FMath::Lerp(L.Accent, FLinearColor::White, .25f) * FLinearColor(1, 1, 1, Fade);
        TextFx(Name, X - TextWidthFont(Name, 14, ECireFont::Bold) * .5f, Y - 19, 14, NameColor, ECireFont::Bold, true, false);
        TextFx(Title, X - TextWidthFont(Title, 10.5f, ECireFont::Heading) * .5f, Y - 1, 10.5f, FLinearColor(.86f, .82f, .92f, Fade), ECireFont::Heading, true, false);
        if (bEntry && State && State->Phase == 1 && Portal->TeamId == Hero->TeamId && Dist < 1600.f && !Portal->bCollapsing)
        {
            const FString Prompt = State->ArenaStage == 2 ? FString(TEXT("Everyone is through  ·  walk in now"))
                : FString::Printf(TEXT("Walk in to enter  ·  drawn through in 0:%02d"), FMath::Clamp(FMath::CeilToInt(CireArenaFlow::PrepSecondsLeft(State)), 0, 59));
            const float PW = TextWidthFont(Prompt, 11.5f, ECireFont::Heading) + 24, PX = X - PW * .5f, PY = Y + 17;
            CireUIStyle::Capsule(P, PX - 1.5f, PY - 1.5f, PW + 3, 23, 1.f, FLinearColor(L.Tint.R, L.Tint.G, L.Tint.B, .55f + .4f * Pulse));
            CireUIStyle::Capsule(P, PX, PY, PW, 20, 1.f, FLinearColor(.02f, .02f, .03f, .9f));
            TextFx(Prompt, PX + 12, PY + 1.5f, 11.5f, FLinearColor(1.f, .93f, .82f, 1), ECireFont::Heading, true, false);
        }
    }
}

// arena-flow: once every champion is in the arena, a big centre countdown (the banner names the arena; this carries
// the seconds) in the Arena banner's colours, with a pulse on each new second.
void ACireHUD::DrawArenaCountdown()
{
    const auto* State = GetWorld() ? GetWorld()->GetGameState<ACireGameState>() : nullptr;
    if (!State || State->Phase != 1 || State->ArenaStage != 2 || !FMath::IsFinite(State->SecondsLeft)) return;
    const float Left = FMath::Max(0.f, State->SecondsLeft);
    const int32 Whole = FMath::Max(1, FMath::CeilToInt(Left));
    const float Frac = 1.f - (static_cast<float>(Whole) - Left); // 1 at the start of a second, 0 at its end
    const FCireBannerSpec Spec = CireBanners::DefaultSpec(ECireBanner::Arena);
    FCireUIPainter P = Painter();
    const float CX = ViewW * .5f, CY = ViewH * .36f;
    const float Size = 64.f + 22.f * FMath::Clamp(Frac, 0.f, 1.f);
    const float Ring = 58.f + 8.f * Frac;
    P.Alpha = .92f;
    P.Disc(CX, CY, Ring + 3.f, FLinearColor(Spec.Color.R, Spec.Color.G, Spec.Color.B, .85f), 48);
    P.Disc(CX, CY, Ring, FLinearColor(.02f, .015f, .03f, .88f), 48);
    const FString Num = FString::FromInt(Whole);
    TextFx(Num, CX - TextWidthFont(Num, Size, ECireFont::Numbers) * .5f, CY - Size * .62f, Size, FMath::Lerp(FLinearColor(1.f, .93f, .8f, 1), FLinearColor::White, Frac), ECireFont::Numbers, true, false);
    const FString Kicker = TEXT("THE FIGHT BEGINS");
    TextFx(Kicker, CX - TextWidthFont(Kicker, 13.f, ECireFont::Heading) * .5f, CY + Ring + 10.f, 13.f, FLinearColor(Spec.Color.R, Spec.Color.G, Spec.Color.B, 1), ECireFont::Heading, true, false);
    const FString Arena = CireArenas::DisplayName(State->ArenaIndex);
    TextFx(Arena, CX - TextWidthFont(Arena, 11.f, ECireFont::Body) * .5f, CY + Ring + 30.f, 11.f, FLinearColor(.86f, .82f, .92f, 1), ECireFont::Body, true, false);
}
