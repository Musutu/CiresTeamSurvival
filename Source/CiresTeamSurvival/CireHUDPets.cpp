// pets: the companion frame in the HUD "Pet" panel (Docs/Pets.md).
#include "CireHUD.h"
#include "CireAbilityDB.h"
#include "CireGame.h"
#include "CirePets.h"
#include "CireUIStyle.h"
#include "Engine/World.h"

namespace
{
float PetFraction(float A, float B) { return B > 0 ? FMath::Clamp(A / B, 0.f, 1.f) : 0.f; }
}

// Health, level, stance, order, ability cooldowns and every command with its key. A fallen or
// absent pet shows its revive and return timers from the owner's replicated state.
void ACireHUD::DrawCompanion(ACireHero* Hero, ACireController* Controller, const FCirePetDef& Def)
{
    ACirePet* Pet = CirePets::PetOf(Hero);
    UsePanel(TEXT("Pet"), 250, 112);
    FCireUIPainter P = Painter();
    const double Now = GetWorld()->GetRealTimeSeconds();
    const auto* State = GetWorld()->GetGameState<ACireGameState>();
    const float Server = State ? static_cast<float>(State->GetServerWorldTimeSeconds()) : GetWorld()->GetTimeSeconds();
    const bool bAlive = Pet && !Pet->bDead;
    const FLinearColor Accent(.93f, .55f, .22f, 1.f); // sabercat amber
    const FLinearColor Muted = CireUIColors::Muted;
    CireUIStyle::Frame(P, 0, 0, 250, 112, bAlive ? Accent : Muted, ECireFrame::Unit);
    // Portrait: the pet's painted ability art inside the themed ring, level medallion below.
    P.Disc(27, 30, 21, FLinearColor(0, 0, 0, .9f)); P.Disc(27, 30, 19, FLinearColor(.05f, .04f, .03f, 1));
    if (UTexture2D* Tex = CireUIStyle::FindAbilityIcon(Def.Portrait)) P.Tex(Tex, 27 - 16.f, 30 - 16.f, 32, 32, bAlive ? FLinearColor::White : FLinearColor(.35f, .35f, .35f, 1));
    else CireUIStyle::Sigil(P, Def.Portrait, 27 - 15.f, 30 - 15.f, 30, Accent);
    CireUIStyle::PortraitRing(P, 27, 30, 19, bAlive ? FLinearColor::White : FLinearColor(.5f, .5f, .5f, 1));
    CireUIStyle::Medallion(P, 12, 48, 9.f, FString::FromInt(Pet ? Pet->Level : Hero->Level), CireUIColors::BrightGold);
    const ECirePetStance Stance = Pet ? Pet->Stance : static_cast<ECirePetStance>(FMath::Min<uint8>(Hero->PetStance, 2));
    P.Text(Def.DisplayName.ToUpper(), 54, 5, 13, bAlive ? CireUIColors::Parchment : Muted, ECireFont::Heading);
    const FString Order = !Pet ? TEXT("AWAY") : Pet->bDead ? TEXT("FALLEN") : Pet->Order == ECirePetOrder::Attack ? TEXT("ATTACKING") :
        Pet->Order == ECirePetOrder::Stay ? TEXT("STAYING") : TEXT("FOLLOWING");
    Label(FString::Printf(TEXT("%s  /  %s  /  %s"), *Def.Family.ToUpper(), *CirePets::StanceName(Stance).ToUpper(), *Order), 54, 21, 8, Muted);
    const auto& Keys = UISettings.Keybindings;
    auto Key = [&Keys](ECirePetCommand C) { return Keys.Label(CirePets::CommandAction(C)); };
    const FCirePetRules& Rules = CirePets::Rules();
    if (bAlive)
    {
        CireUIStyle::Bar(P, 54, 33, 186, 13, PetFraction(Pet->Health, Pet->MaxHealth), FLinearColor(.10f, .70f, .14f, 1), &BarTrails.FindOrAdd(0xBE70), Now,
            FString::Printf(TEXT("%.0f / %.0f"), Pet->Health, Pet->MaxHealth), 9.f);
        DrawStatuses(Pet, 54, 49, 11, 8);
        Tip(Def.DisplayName, FString::Printf(TEXT("%s Attack %.0f every %.1fs. Scales with your level, health and primary attribute. Stance: %s."),
            *Def.Description, Pet->CurrentDamage(), Def.AttackSeconds, *CirePets::StanceName(Stance)), 0, 0, 250, 50);
    }
    else
    {
        const float Revive = FMath::Max(0.f, Hero->PetReviveReadyAt - Server), Return = FMath::Max(0.f, Hero->PetResummonAt - Server);
        CireUIStyle::Bar(P, 54, 33, 186, 13, 0.f, Muted, &BarTrails.FindOrAdd(0xBE70), Now, Pet ? TEXT("FALLEN") : TEXT("RESTING"), 9.f);
        const FString Line = Pet ? (Revive > 0 ? FString::Printf(TEXT("Revive in %.0fs  -  returns in %.0fs"), Revive, Return) :
            FString::Printf(TEXT("%s revive now  -  returns in %.0fs"), *Key(ECirePetCommand::Revive), Return)) :
            (Return > 0 ? FString::Printf(TEXT("Returns in %.0fs"), Return) : FString::Printf(TEXT("%s call"), *Key(ECirePetCommand::Revive)));
        Label(Line, 54, 49, 9, CireUIColors::Orange);
        Tip(TEXT("Companion down"), FString::Printf(TEXT("Revive the corpse (within %.0fm) at %.0f%% health, once every %.0fs, or wait: it returns at full health %.0fs after falling."),
            Rules.ReviveRange / 100.f, Rules.ReviveHealthFraction * 100.f, Rules.ReviveCooldown, Rules.ResummonCooldown), 0, 0, 250, 62);
    }
    const bool bInteractive = !bModal && !bSettings && !bEditLayout;
    auto Send = [&](ECirePetCommand C) { if (Controller) Controller->ServerPetCommand(static_cast<uint8>(C)); Clicked = false; PlayUIFeedback(); };
    // Orders.
    const ECirePetCommand Orders[] = {ECirePetCommand::Attack, ECirePetCommand::Follow, ECirePetCommand::Stay};
    const TCHAR* OrderNames[] = {TEXT("ATTACK"), TEXT("FOLLOW"), TEXT("STAY")};
    const TCHAR* OrderHelp[] = {TEXT("Send your companion at your selected hostile target. It fights until the target dies or leaves your leash range."),
        TEXT("Your companion returns to your side and resumes its stance."), TEXT("Your companion holds this spot. It still fights anything that reaches it unless Passive.")};
    for (int32 I = 0; I < 3; ++I)
    {
        const float X = 8 + I * 56, Y = 64, W = 52, H = 20;
        const bool bActive = bAlive && ((I == 0 && Pet->Order == ECirePetOrder::Attack) || (I == 1 && Pet->Order == ECirePetOrder::Follow) || (I == 2 && Pet->Order == ECirePetOrder::Stay));
        const bool bOver = Hit(X, Y, W, H);
        CireUIStyle::Button(P, X, Y, W, H, OrderNames[I], !bAlive ? ECireButtonState::Disabled : bActive ? ECireButtonState::Selected : bOver ? ECireButtonState::Hover : ECireButtonState::Normal, Accent, 8.5f);
        const FString K = Key(Orders[I]);
        P.Text(K, X + W - 3 - P.TextWidth(K, 7, ECireFont::Numbers), Y - 9, 7, Muted, ECireFont::Numbers);
        Tip(FString::Printf(TEXT("%s (%s)"), OrderNames[I], *K), OrderHelp[I], X, Y, W, H);
        if (bAlive && bInteractive && Clicked && bOver) Send(Orders[I]);
    }
    // Special ability and revive / call.
    const int32 Special = Def.SpecialIndex();
    if (Def.Abilities.IsValidIndex(Special))
    {
        const FString& Id = Def.Abilities[Special].Id; const FCireAbilityDef* Row = CireAbilityDB::Find(Id);
        FCireIconSlot S; S.IconId = Id; S.IconTexture = CireUIStyle::FindAbilityIcon(Id); S.Tint = Accent; S.KeyLabel = Key(ECirePetCommand::Special);
        const float Ready = Pet && Pet->AbilityReadyAt.IsValidIndex(Special) ? Pet->AbilityReadyAt[Special] - Server : 0.f;
        if (Ready > .05f && Row) { S.CooldownRemaining = Ready; S.CooldownFraction = FMath::Clamp(Ready / FMath::Max(1.f, Row->Base.Cooldown), 0.f, 1.f); }
        S.bNoResource = !bAlive; S.bHover = Hit(178, 58, 28, 28);
        CireUIStyle::IconSlot(P, 178, 58, 28, S, Now);
        Tip(Row ? Row->Name + TEXT(" (") + Key(ECirePetCommand::Special) + TEXT(")") : Id, Row ? Row->Description : FString(), 178, 58, 28, 28);
        if (bAlive && bInteractive && Clicked && S.bHover) Send(ECirePetCommand::Special);
    }
    {
        FCireIconSlot S; S.IconId = TEXT("second_wind"); S.IconTexture = CireUIStyle::FindAbilityIcon(TEXT("second_wind")); S.Tint = FLinearColor(.5f, .9f, .45f, 1);
        S.KeyLabel = Key(ECirePetCommand::Revive);
        const float Revive = Hero->PetReviveReadyAt - Server;
        if (Pet && Pet->bDead && Revive > .05f) { S.CooldownRemaining = Revive; S.CooldownFraction = FMath::Clamp(Revive / FMath::Max(1.f, Rules.ReviveCooldown), 0.f, 1.f); }
        S.bGlow = Pet && Pet->bDead && Revive <= .05f; S.bHover = Hit(212, 58, 28, 28);
        CireUIStyle::IconSlot(P, 212, 58, 28, S, Now);
        Tip(FString::Printf(TEXT("Revive / call (%s)"), *Key(ECirePetCommand::Revive)),
            TEXT("Fallen: revive the corpse where it lies at half health. Away: call it once its return timer is over. Alive: call it back to your side."), 212, 58, 28, 28);
        if (bInteractive && Clicked && S.bHover) Send(ECirePetCommand::Revive);
    }
    // Stances.
    const ECirePetCommand Stances[] = {ECirePetCommand::StanceAggressive, ECirePetCommand::StanceDefensive, ECirePetCommand::StancePassive};
    const TCHAR* StanceHelp[] = {TEXT("Also attacks enemies that come near it, on top of defending and assisting you. Never opens on a neutral pack."),
        TEXT("Assists your target and defends you and itself."), TEXT("Never attacks on its own: only your attack orders and skills.")};
    for (int32 I = 0; I < 3; ++I)
    {
        const float X = 8 + I * 78, Y = 91, W = 74, H = 16; const bool bOver = Hit(X, Y, W, H);
        const ECirePetStance S = static_cast<ECirePetStance>(I);
        CireUIStyle::Button(P, X, Y, W, H, CirePets::StanceName(S).ToUpper(), Stance == S ? ECireButtonState::Selected : bOver ? ECireButtonState::Hover : ECireButtonState::Normal, Accent, 7.5f);
        Tip(CirePets::StanceName(S) + TEXT(" (") + Key(Stances[I]) + TEXT(")"), StanceHelp[I], X, Y, W, H);
        if (bInteractive && Clicked && bOver) Send(Stances[I]);
    }
}
