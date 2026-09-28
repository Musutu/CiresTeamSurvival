// kit-editor: the EDIT badge -> Ability Tuner on one ability (see CireTunerLink.h).
#include "CireTunerLink.h"
#include "CireAbilityDB.h"
#include "CireAbilityTuner.h"
#include "CireAbilityTunerUI.h"
#include "CireHUD.h"
#include "CireUIStyle.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireTunerLink, Log, All);

namespace
{
int32 GTunerLinkForce = -1;
}

void CireTunerLink::DebugForce(int32 Mode) { GTunerLinkForce = FMath::Clamp(Mode, -1, 1); }

bool CireTunerLink::Available(const APlayerController* Controller)
{
    if (GTunerLinkForce >= 0) return GTunerLinkForce == 1;
    return Controller && CireAbilityTuner::CanTune(Controller);
}

bool CireTunerLink::Open(const APlayerController* Controller, const FString& AbilityId)
{
    if (AbilityId.IsEmpty() || !Available(Controller) || !CireAbilityDB::Find(AbilityId)) return false;
    CireAbilityTunerUI::Select(AbilityId);
    return true;
}

FString CireTunerLink::TooltipTitle(const ACireHUD& HUD)
{
    const FString Key = HUD.UISettings.Keybindings.Label(FName(TEXT("ToggleAbilityTuner")));
    return Key.IsEmpty() ? FString(TEXT("Edit in Ability Tuner")) : FString::Printf(TEXT("Edit in Ability Tuner (%s)"), *Key);
}

FString CireTunerLink::ActionBarHint(const ACireHUD& HUD)
{
    return Available(HUD.GetOwningPlayerController()) ? TEXT("Alt+click: ") + TooltipTitle(HUD) : FString();
}

bool CireTunerLink::ModifierDown(const APlayerController* Controller)
{
    return Controller && (Controller->IsInputKeyDown(EKeys::LeftAlt) || Controller->IsInputKeyDown(EKeys::RightAlt));
}

FVector2D CireTunerLink::BadgeAt(float Right, float Top) { return FVector2D(Right - Inset - Size, Top + Inset); }

bool CireTunerLink::OverBadge(FVector2D Pointer, float Right, float Top)
{
    const FVector2D At = BadgeAt(Right, Top);
    return Pointer.X >= At.X && Pointer.X < At.X + Size && Pointer.Y >= At.Y && Pointer.Y < At.Y + Size;
}

void CireTunerLink::DrawIcon(const FCireUIPainter& P, float X, float Y, bool bHover)
{
    using namespace CireUIColors;
    const float S = Size;
    if (bHover) CireUIStyle::Glow(P, X - 6, Y - 6, S + 12, S + 12, FLinearColor(1.f, .8f, .35f, .55f));
    CireUIStyle::Bevel(P, X, Y, S, S, 4.f, FLinearColor(.035f, .03f, .025f, .94f));
    CireUIStyle::BevelOutline(P, X, Y, S, S, 4.f, bHover ? BrightGold : Gold, bHover ? 1.6f : 1.f);
    // Pencil, 45 degrees: eraser (top right), gold body, parchment point (bottom left).
    const FVector2D A(X + S - 6.f, Y + 6.f), B(X + 7.5f, Y + S - 7.5f);
    const FLinearColor Body = bHover ? BrightGold : Gold;
    P.Line(A.X - 2.5f, A.Y + 2.5f, B.X + 2.f, B.Y - 2.f, Body, 3.2f);
    P.Line(A.X, A.Y, A.X - 2.5f, A.Y + 2.5f, FLinearColor(.85f, .45f, .4f, 1), 3.2f);
    P.Tri(FVector2D(B.X + 3.2f, B.Y - .8f), FVector2D(B.X + .8f, B.Y - 3.2f), FVector2D(B.X - 1.f, B.Y + 1.f), Parchment);
}

bool CireTunerLink::Badge(ACireHUD& HUD, const FCireUIPainter& P, FVector2D Pointer, float Right, float Top, const FString& AbilityId, bool bInteractive)
{
    APlayerController* PC = HUD.GetOwningPlayerController();
    if (AbilityId.IsEmpty() || !Available(PC)) return false;
    const FVector2D At = BadgeAt(Right, Top);
    const bool bOver = bInteractive && OverBadge(Pointer, Right, Top);
    DrawIcon(P, At.X, At.Y, bOver);
    if (!bOver) return false;
    const FCireAbilityDef* Def = CireAbilityDB::Find(AbilityId);
    HUD.SetTooltip(TooltipTitle(HUD), FString::Printf(TEXT("Open the Ability Tuner on %s: rename it, retune its numbers, durations and effects live."), Def ? *Def->Name : *AbilityId));
    if (HUD.HasClick())
    {
        HUD.TakeClick();
        if (Open(PC, AbilityId)) HUD.PlayInterfaceSound(4, .5f);
    }
    return true;
}

#if !UE_BUILD_SHIPPING
bool CireTunerLink::RunTests()
{
    int32 Count = 0; bool bPassed = true;
    const auto Check = [&](bool bOk, const TCHAR* What) { ++Count; if (!bOk) { bPassed = false; UE_LOG(LogCireTunerLink, Error, TEXT("CIRE_TUNER_LINK_CHECK_FAIL %s"), What); } };
    const bool bWasOpen = CireAbilityTunerUI::IsOpen();
    const FString WasSelected = CireAbilityTunerUI::Selected();
    const TArray<FCireAbilityDef>& All = CireAbilityDB::All();
    const FString Id = All.Num() ? All[All.Num() / 4].Id : FString();

    DebugForce(-1);
    Check(!Available(nullptr), TEXT("no controller (remote / dedicated): no badge"));
    DebugForce(0);
    CireAbilityTunerUI::Close();
    Check(!Available(nullptr) && !Open(nullptr, Id) && !CireAbilityTunerUI::IsOpen(), TEXT("tuning not allowed: badge hidden, click does nothing"));
    DebugForce(1);
    Check(Open(nullptr, Id) && CireAbilityTunerUI::IsOpen() && CireAbilityTunerUI::Selected() == Id, TEXT("badge click opens the tuner on that ability"));
    Check(!Open(nullptr, TEXT("no_such_ability_xyz")) && CireAbilityTunerUI::Selected() == Id, TEXT("unknown ability: no change"));
    const FVector2D At = BadgeAt(200.f, 100.f);
    Check(FMath::IsNearlyEqual(At.X, 200.f - Inset - Size) && FMath::IsNearlyEqual(At.Y, 100.f + Inset), TEXT("badge sits in the top-right corner"));
    Check(OverBadge(At + FVector2D(Size * .5f, Size * .5f), 200.f, 100.f) && !OverBadge(FVector2D(150.f, 150.f), 200.f, 100.f), TEXT("badge hit test"));
    DebugForce(-1);

    CireAbilityTunerUI::Close();
    if (!WasSelected.IsEmpty()) { CireAbilityTunerUI::Select(WasSelected); if (!bWasOpen) CireAbilityTunerUI::Close(); }
    else if (bWasOpen) CireAbilityTunerUI::Select(Id);
    UE_LOG(LogCireTunerLink, Display, TEXT("CIRE_TUNER_LINK_TESTS_%s checks=%d"), bPassed ? TEXT("PASS") : TEXT("FAIL"), Count);
    return bPassed;
}
#endif
