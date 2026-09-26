// WoW-style action bars (main bar + two extra bars), rich ability tooltips, drag and
// drop placement, Quick Keybind mode and the Keybindings options page. Keys, slot
// placement and dispatch come from CireKeybindings (Docs/Keybindings.md); this file
// only draws and edits them.
#include "CireHUD.h"
#include "CireClassTraits.h"
#include "CireAbilityIcons.h"
#include "CireGame.h"
#include "CireKeybindings.h"
#include "CireTargeting.h"
#include "CireAbilityLibrary.h"
#include "CireSkillTuning.h"
#include "CireNPCState.h"
#include "CireUIStyle.h"
#include "CireSkillShop.h" // XP tooltip follows the progression mode
#include "CireItems.h"
#include "CireAbilityDB.h" // items-v2
#include "CireSkillShop.h" // items-v2
#include "CireShopUI.h"
#include "CireScalingKits.h" // readability: tooltip scaling math
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

namespace
{
// ui-themes: themed colours are references to CireUIColors so they follow the active UI theme.
const FLinearColor &Gold=CireUIColors::Gold, &Parchment=CireUIColors::Parchment, &Muted=CireUIColors::Muted, &BrightGold=CireUIColors::BrightGold;
const FLinearColor Orange=CireUIColors::Orange;
struct FFacts { float Mana = 0, Energy = 0, Cooldown = 0, Range = 0, Windup = 0; bool bKnown = false; };
FFacts AbilityFacts(const FString& Id)
{
    FFacts F;
    if (const auto* A = CireAbilityLibrary::Find(Id)) { F = {A->ManaCost, A->EnergyCost, A->CooldownSeconds, A->CastRange, A->Area.WarningSeconds, true}; return F; }
    if (const auto* S = CireSkillTuning::FindSkillshot(Id)) { F = {S->ManaCost, S->EnergyCost, S->CooldownSeconds, S->CastRange, S->WarningSeconds, true}; return F; }
    if (const auto* S = CireSkillTuning::FindConstruct(Id)) { F = {S->ManaCost, S->EnergyCost, S->CooldownSeconds, S->CastRange, 0, true}; return F; }
    if (const auto* S = CireSkillTuning::FindSummon(Id)) { F = {S->ManaCost, S->EnergyCost, S->CooldownSeconds, S->CastRange, 0, true}; return F; }
    if (const auto* S = CireSkillTuning::FindRoleSkill(Id)) { F = {S->ManaCost, S->EnergyCost, S->CooldownSeconds, S->CastRange, S->WarningSeconds, true}; return F; }
    // Legacy abilities (mirrors ACireHero::Cast).
    struct FRow { const TCHAR* Id; float Mana, Energy, Cooldown, Range; };
    static const FRow Rows[] = {
        {TEXT("iron_guard"), 0, 25, 14, 0}, {TEXT("shield_slam"), 0, 25, 7, 240}, {TEXT("war_cry"), 0, 30, 18, 0},
        {TEXT("chain_spark"), 45, 0, 10, 1200}, {TEXT("ember_lance"), 40, 0, 6, 1000}, {TEXT("frost_bind"), 35, 0, 12, 850},
        {TEXT("cleaving_strike"), 0, 30, 8, 300}, {TEXT("piercing_shot"), 0, 25, 9, 1000}, {TEXT("shadow_step"), 0, 35, 14, 850},
        {TEXT("restoring_light"), 45, 0, 6, 1200}, {TEXT("sanctuary"), 70, 0, 16, 0}, {TEXT("purify"), 25, 0, 8, 1200},
        {TEXT("bastion_of_dawn"), 0, 45, 75, 0}, {TEXT("cataclysm"), 150, 0, 80, 1500}, {TEXT("executioners_verdict"), 0, 60, 60, 1500},
        {TEXT("renewal"), 140, 0, 90, 0}};
    for (const FRow& R : Rows) if (Id == R.Id) { F = {R.Mana, R.Energy, R.Cooldown, R.Range, 0, true}; break; }
    return F;
}
FLinearColor SchoolTint(const FString& Id, const FString& Name)
{
    const FString N = (Id + TEXT(" ") + Name).ToLower();
    auto Any = [&](std::initializer_list<const TCHAR*> Words) { for (const TCHAR* W : Words) if (N.Contains(W)) return true; return false; };
    if (Any({TEXT("ember"), TEXT("fire"), TEXT("flame"), TEXT("cataclysm")})) return FLinearColor(1.f, .5f, .16f, 1);
    if (Any({TEXT("frost"), TEXT("ice"), TEXT("cold")})) return FLinearColor(.5f, .82f, 1.f, 1);
    if (Any({TEXT("venom"), TEXT("poison"), TEXT("blight"), TEXT("thorn")})) return FLinearColor(.58f, 1.f, .3f, 1);
    if (Any({TEXT("shadow"), TEXT("void"), TEXT("soul"), TEXT("spectral"), TEXT("rift")})) return FLinearColor(.76f, .5f, 1.f, 1);
    if (Any({TEXT("light"), TEXT("sanct"), TEXT("dawn"), TEXT("purif"), TEXT("renew"), TEXT("restor")})) return FLinearColor(1.f, .92f, .55f, 1);
    if (Any({TEXT("spark"), TEXT("storm"), TEXT("chain"), TEXT("lightning")})) return FLinearColor(.55f, .78f, 1.f, 1);
    if (Any({TEXT("guard"), TEXT("shield"), TEXT("wall"), TEXT("stone"), TEXT("bastion")})) return FLinearColor(.62f, .74f, .9f, 1);
    return FLinearColor(.95f, .78f, .45f, 1);
}
FString SchoolName(const FLinearColor& C)
{
    if (C.Equals(FLinearColor(1.f, .5f, .16f, 1), .01f)) return TEXT("Fire");
    if (C.Equals(FLinearColor(.5f, .82f, 1.f, 1), .01f)) return TEXT("Frost");
    if (C.Equals(FLinearColor(.58f, 1.f, .3f, 1), .01f)) return TEXT("Nature");
    if (C.Equals(FLinearColor(.76f, .5f, 1.f, 1), .01f)) return TEXT("Shadow");
    if (C.Equals(FLinearColor(1.f, .92f, .55f, 1), .01f)) return TEXT("Holy");
    if (C.Equals(FLinearColor(.55f, .78f, 1.f, 1), .01f)) return TEXT("Storm");
    if (C.Equals(FLinearColor(.62f, .74f, .9f, 1), .01f)) return TEXT("Protection");
    return TEXT("Physical");
}
FName MainSlot(int32 Index) { return CireKeybindings::SlotAction(1, Index + 1); }
}

// ---------------------------------------------------------------------------
// Action bars
// ---------------------------------------------------------------------------
float ACireHUD::ActionBarsTop() const
{
    float Top = PanelRect(TEXT("Skills")).Y;
    if (VisiblePanels.Contains(FName(TEXT("Bar2")))) Top = FMath::Min(Top, PanelRect(TEXT("Bar2")).Y);
    if (VisiblePanels.Contains(FName(TEXT("Bar3")))) Top = FMath::Min(Top, PanelRect(TEXT("Bar3")).Y);
    return Top;
}
bool ACireHUD::DrawActionButton(ACireHero* Hero, ACireController* Controller, int32 Bar, int32 Index, float X, float Y, float S)
{
    const FName Action = CireKeybindings::SlotAction(Bar, Index + 1);
    const FString Id = CireKeybindings::SlotAbilityId(UISettings.Keybindings, *Hero, Action);
    // progression-shop: a slot may hold an active item ("item:<id>"), drawn with the shop's icon.
    FName ItemId;
    if (CireItems::ParseItemSlotId(Id, ItemId))
    {
        const bool bOverItem = Hit(X, Y, S, S) && (!bModal && !bSettings && !bEditLayout || bQuickKeybind);
        int32 Cell = INDEX_NONE;
        if (Hero->Inventory) Cell = Hero->Inventory->Equipment.IndexOfByPredicate([&](const FCireItemSlot& C) { return C.Id == ItemId; });
        float Remaining = 0.f, Fraction = 0.f; int32 Charges = 0;
        if (Cell != INDEX_NONE)
        {
            const FCireItemSlot& C = Hero->Inventory->Equipment[Cell];
            const auto* State = GetWorld()->GetGameState<ACireGameState>();
            const float Now = State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
            Remaining = FMath::Max(0.f, C.ReadyAt - Now); Fraction = C.Cooldown > 0 ? FMath::Clamp(Remaining / C.Cooldown, 0.f, 1.f) : 0.f; Charges = C.Charges;
        }
        FCireUIPainter P = Painter();
        if (bBarDragging && DragSlot == Action) P.Alpha *= .35f;
        const FString ItemKey = UISettings.Keybindings.Label(Action);
        CireShopUI::DrawItemIcon(P, ItemId, X, Y, S, bOverItem, Fraction, Remaining, Charges, ItemKey.IsEmpty() ? FString(TEXT("\u2014")) : ItemKey, Cell == INDEX_NONE);
        if (bOverItem)
        {
            HoverSlot = Action;
            { FCireTooltipSpec Spec = CireShopUI::ItemTooltipSpec(ItemId, -1, Cell == INDEX_NONE ? TEXT("Not in your bag.") : TEXT("Click or press its key to use.")); Spec.Tag = UISettings.Keybindings.FullLabel(Action); SetRichTooltip(Spec); } // readability: rich item card
            if (!bQuickKeybind && Clicked && !bModal && !bSettings && !bEditLayout)
            {
                DragSlot = Action; DragAbility = Id; DragStart = FVector2D(MX, MY); bBarDragging = false; bBarPressCandidate = true; Clicked = false;
            }
        }
        return bOverItem;
    }
    const int32 Skill = Id.IsEmpty() ? INDEX_NONE : Hero->Skills.IndexOfByKey(Id);
    const bool bLearned = Skill != INDEX_NONE;
    const bool bInteractive = !bModal && !bSettings && !bEditLayout;
    const bool bOver = Hit(X, Y, S, S) && (bInteractive || bQuickKeybind);
    const double Now = GetWorld()->GetRealTimeSeconds();
    FCireIconSlot Slot;
    Slot.KeyLabel = UISettings.Keybindings.Label(Action);
    if (Slot.KeyLabel.IsEmpty() && !Id.IsEmpty()) Slot.KeyLabel = TEXT("\u2014"); // unbound
    Slot.bHover = bOver;
    Slot.bEmpty = Id.IsEmpty();
    if (!Slot.bEmpty)
    {
        Slot.IconId = Id;
        Slot.IconTexture = CireUIStyle::FindAbilityIcon(Id);
        Slot.Tint = SchoolTint(Id, ACireHero::SkillName(Id));
        Slot.Kind = ACireHero::IsPassive(Id) ? ECireSlotKind::Passive : ACireHero::IsUltimate(Id) ? ECireSlotKind::Ultimate : ECireSlotKind::Normal;
        if (!bLearned) { Slot.Tint = Slot.Tint * .35f; }
        const float CD = bLearned && Hero->Cooldowns.IsValidIndex(Skill) ? Hero->Cooldowns[Skill] : 0.f;
        float& Last = LastCooldown.FindOrAdd(Id); float& Max = CooldownMax.FindOrAdd(Id);
        if (CD > Last + .2f) { Max = CD; PressFlashAt.Add(Id, Now); }        // just cast
        if (Last > .05f && CD <= .05f) ReadyFlashAt.Add(Id, Now);             // came off cooldown
        Last = CD;
        if (CD > .05f) { Slot.CooldownRemaining = CD; Slot.CooldownFraction = CD / FMath::Max(Max, CD); }
        if (bLearned && Slot.Kind != ECireSlotKind::Passive)
        {
            const FFacts F = AbilityFacts(Id);
            float NeedMana = F.Mana, NeedEnergy = F.Energy; CireSkillShop::ScaledCost(Hero, Id, F.Mana, F.Energy, NeedMana, NeedEnergy); // items-v2
            Slot.bNoResource = Hero->Mana < NeedMana || Hero->Energy < NeedEnergy;
            const auto D = CireTargeting::Describe(Id);
            if (bOver) if (auto* PC = Cast<ACireController>(PlayerOwner)) CireTargeting::HoverPreview(PC, Id); // ability-vfx: void rift preview
            if (D.bNeedsHostile && IsValid(Hero->Target) && D.Range > 0 && Hero->IsHostile(Hero->Target))
                Slot.bOutOfRange = FVector::Dist2D(Hero->GetActorLocation(), Hero->Target->GetActorLocation()) > D.Range + 40.f;
            // Proc highlights: ultimate ready; an interrupt while the target channels an interruptible cast.
            if (Slot.Kind == ECireSlotKind::Ultimate && CD <= .05f && !Slot.bNoResource) Slot.bGlow = true;
            if (Id == TEXT("shield_slam") && CD <= .05f)
                if (const auto* M = Cast<ACireMonster>(Hero->Target); M && M->NPCState && M->NPCState->CastInfo().bCasting && M->NPCState->CastInfo().bInterruptible) Slot.bGlow = true;
        }
        if (const double* At = ReadyFlashAt.Find(Id); At && Now - *At < 1.2) { Slot.bGlow = true; Slot.Flash = FMath::Max(Slot.Flash, 1.f - static_cast<float>(Now - *At) / .5f); }
        if (const double* At = PressFlashAt.Find(Id); At && Now - *At < .3) Slot.Flash = FMath::Max(Slot.Flash, 1.f - static_cast<float>(Now - *At) / .3f);
    }
    const bool bDragSource = bBarDragging && DragSlot == Action;
    Slot.bPressed = bOver && PlayerOwner->IsInputKeyDown(EKeys::LeftMouseButton) && !bQuickKeybind;
    if (bQuickKeybind && bOver) Slot.bGlow = true;
    // Empty slots on extra bars only show while dragging or in layout/keybind modes (WoW grid).
    const bool bShowEmpty = Bar == 1 || bBarDragging || bQuickKeybind || bEditLayout;
    if (!Slot.bEmpty || bShowEmpty)
    {
        FCireUIPainter P = Painter();
        if (bDragSource) P.Alpha *= .35f;
        CireUIStyle::IconSlot(P, X, Y, S, Slot, Now);
    }
    if (bOver)
    {
        HoverSlot = Action;
        if (!Slot.bEmpty) { TooltipAbility = Id; TooltipAbilitySlot = Action; }
        else if (!bQuickKeybind) { TooltipTitle = TEXT("Empty action slot"); TooltipBody = TEXT("Drag an ability here from another slot. Key: ") + UISettings.Keybindings.FullLabel(Action) + TEXT("."); }
        if (bInteractive && !bQuickKeybind && Clicked)
        {
            // Press starts a potential drag; a release without movement casts.
            DragSlot = Action; DragAbility = Id; DragStart = FVector2D(MX, MY); bBarDragging = false; bBarPressCandidate = true;
            Clicked = false;
        }
    }
    return bOver;
}

void ACireHUD::DrawActionBars(ACireHero* Hero, ACireController* Controller)
{
    if (!Hero) return;
    HoverSlot = NAME_None;
    const double Now = GetWorld()->GetRealTimeSeconds();
    // ---- main bar (panel "Skills") ----
    // jungle-packs: the bar is 48 px wider for the Recall button (not a skill slot).
    UsePanel(TEXT("Skills"), 632, 155);
    FCireUIPainter P = Painter();
    CireUIStyle::Frame(P, 0, 0, 632, 155, Gold);
    // Experience bar (WoW purple), level on hover.
    const float Need = 120.f + FMath::Max(0, Hero->Level) * 60.f;
    CireUIStyle::Bar(P, 12, 7, 608, 8, Hero->Experience / Need, FLinearColor(.55f, .3f, .85f, 1), &BarTrails.FindOrAdd(0xE0E0), Now);
    // Skill Shop mode: levels only raise attributes; skills come from the shop between waves.
    const bool bShopMode = CireSkillShop::IsSkillShopMode(Hero->GetWorld());
    Tip(FString::Printf(TEXT("Level %d"), Hero->Level), FString::Printf(TEXT("Experience %d / %.0f to level %d. Levels grant +2 primary and +1 other attributes%s"), Hero->Experience, Need, Hero->Level + 1,
        bShopMode ? TEXT(". New skills come from the Skill Shop between waves.") : TEXT(", and new ability choices.")), 12, 5, 608, 12);
    // Auto-attack button.
    {
        FCireIconSlot Attack; Attack.IconId = TEXT("basic"); Attack.Kind = ECireSlotKind::Attack; Attack.Tint = FLinearColor(.95f, .8f, .5f, 1);
        Attack.KeyLabel = UISettings.Keybindings.Label(TEXT("ToggleAutoAttack")); Attack.bGlow = Hero->bAutoAttack;
        const bool bOver = Hit(10, 22, 44, 44) && !bModal && !bSettings && !bEditLayout;
        Attack.bHover = bOver; Attack.bPressed = bOver && PlayerOwner->IsInputKeyDown(EKeys::LeftMouseButton);
        CireUIStyle::IconSlot(P, 10, 22, 44, Attack, Now);
        if (bOver)
        {
            HoverSlot = TEXT("ToggleAutoAttack");
            TooltipTitle = TEXT("Auto Attack  (") + UISettings.Keybindings.FullLabel(TEXT("ToggleAutoAttack")) + TEXT(")");
            TooltipBody = FString::Printf(TEXT("Toggle basic attacks against your hostile target. %.1fm range, %s. Glows while active."), Hero->BasicAttackRange() / 100, *Hero->BasicAttackStyle());
            if (Clicked && !bQuickKeybind && Controller) { Controller->ServerAction(1, 0, nullptr); Clicked = false; }
        }
    }
    for (int32 I = 0; I < FCireKeybindings::SlotsPerBar; ++I) DrawActionButton(Hero, Controller, 1, I, 60.f + I * 42.6f, 24.f, 40.f);
    // jungle-packs: Recall (Teleport to Base) on the action bar: every champion has it, outside the skill slots. It shows
    // the cooldown sweep and countdown, the channel progress, and is free (teal) during prep / recovery.
    if (const UCireInventory* Inv = Hero->Inventory)
    {
        const float RX = 582.f, RY = 24.f, RS = 40.f;
        const ACireGameState* GS = GetWorld()->GetGameState<ACireGameState>();
        const float ServerNow = GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
        const float Remaining = FMath::Max(0.f, Inv->TeleportReadyAt - ServerNow);
        const float Cooldown = static_cast<float>(CireItems::Get().Teleport.CooldownSeconds);
        const bool bFree = GS && (GS->Phase == 1 || GS->Phase == 4);
        FCireIconSlot Recall; Recall.IconId = TEXT("warp_obelisk"); Recall.IconTexture = CireUIStyle::FindAbilityIcon(TEXT("warp_obelisk"));
        Recall.Tint = FLinearColor(.45f, .95f, 1.f, 1); Recall.KeyLabel = UISettings.Keybindings.Label(TEXT("RecallToTown"));
        if (Remaining > 0 && !bFree && Cooldown > 0) { Recall.CooldownRemaining = Remaining; Recall.CooldownFraction = FMath::Clamp(Remaining / Cooldown, 0.f, 1.f); }
        Recall.bGlow = Inv->IsChanneling();
        const bool bOver = Hit(RX, RY, RS, RS) && !bModal && !bSettings && !bEditLayout;
        Recall.bHover = bOver; Recall.bPressed = bOver && PlayerOwner->IsInputKeyDown(EKeys::LeftMouseButton);
        CireUIStyle::IconSlot(P, RX, RY, RS, Recall, Now);
        if (Inv->IsChanneling())
        {
            const float T = FMath::Clamp((ServerNow - Inv->TeleportChannelStart) / FMath::Max(.1f, Inv->TeleportChannelEnd - Inv->TeleportChannelStart), 0.f, 1.f);
            P.Rect(RX + 3, RY + RS - 6, (RS - 6) * T, 3, FLinearColor(.5f, .95f, 1.f, 1));
        }
        P.Text(TEXT("RECALL"), RX + RS * .5f - P.TextWidth(TEXT("RECALL"), 6.5f, ECireFont::Heading) * .5f, 64.5f, 6.5f, Remaining > 0 && !bFree ? Muted : FLinearColor(.55f, .95f, 1.f, 1), ECireFont::Heading, true, false);
        if (bOver)
        {
            HoverSlot = TEXT("RecallToTown");
            TooltipTitle = TEXT("Recall  (") + UISettings.Keybindings.FullLabel(TEXT("RecallToTown")) + TEXT(")");
            TooltipBody = bFree ? FString(TEXT("Prep / recovery: instant and free recall to your team's recall point (or your base)."))
                : FString::Printf(TEXT("Channel %.0f s, then return to your team's nearest recall point (or your base). Taking damage or moving cancels it (no cooldown spent). %.0f s cooldown.%s"),
                    CireItems::Get().Teleport.ChannelSeconds, Cooldown, Remaining > 0 ? *FString::Printf(TEXT("\nReady in %.0f s."), Remaining) : TEXT(""));
            if (Clicked && !bQuickKeybind && Controller) { Controller->ServerAction(8, 0, nullptr); Clicked = false; }
        }
    }
    P.Line(12, 72, 620, 72, Gold * FLinearColor(1, 1, 1, .35f), 1.f);
    // Slot captions under the passive and ultimate buttons (the old Arsenal panel's state).
    {
        const FString Ult = CireKeybindings::SlotAbilityId(UISettings.Keybindings, *Hero, MainSlot(7));
        const int32 UltSkill = Ult.IsEmpty() ? INDEX_NONE : Hero->Skills.IndexOfByKey(Ult);
        const float UltCD = Hero->Cooldowns.IsValidIndex(UltSkill) ? Hero->Cooldowns[UltSkill] : 0.f;
        const FString UltText = Ult.IsEmpty() ? FString(TEXT("NO ULT")) : UltCD > .05f ? FString(TEXT("ULT CD")) : FString(TEXT("ULT READY"));
        const FString PasText = CireKeybindings::SlotAbilityId(UISettings.Keybindings, *Hero, MainSlot(6)).IsEmpty() ? FString(TEXT("NO PASSIVE")) : FString(TEXT("PASSIVE"));
        P.Text(PasText, 60 + 6 * 42.6f + 20 - P.TextWidth(PasText, 6.5f, ECireFont::Heading) * .5f, 64.5f, 6.5f, FLinearColor(.75f, .65f, 1.f, 1), ECireFont::Heading, true, false);
        P.Text(UltText, 60 + 7 * 42.6f + 20 - P.TextWidth(UltText, 6.5f, ECireFont::Heading) * .5f, 64.5f, 6.5f, UltCD > .05f || Ult.IsEmpty() ? Muted : BrightGold, ECireFont::Heading, true, false);
    }
    // Stats and gold.
    P.Text(FString::Printf(TEXT("ATK %.0f"), Hero->AttackDamage()), 14, 80, 10, Parchment, ECireFont::Heading);
    P.Text(FString::Printf(TEXT("CDR %.0f%%"), Hero->CDR * 100), 84, 80, 10, Parchment, ECireFont::Heading);
    P.Text(FString::Printf(TEXT("CRIT %.0f%%"), Hero->CriticalChance * 100), 156, 80, 10, Parchment, ECireFont::Heading);
    P.Text(FString::Printf(TEXT("LEVEL %d"), Hero->Level), 234, 80, 10, FLinearColor(.75f, .6f, 1.f, 1), ECireFont::Heading);
    // champion-draft: class baseline trait (icon + name, passive-style tooltip).
    {
        const FCireClassTrait Trait = CireClassTraits::Info(CireClassTraits::Role(Hero));
        if (!Trait.Id.IsEmpty())
        {
            CireAbilityIcons::Draw(P, Trait.Id, 300, 77, 14);
            P.Text(Trait.Name.ToUpper(), 318, 80, 9, Trait.Color, ECireFont::Heading);
            if (Hit(298, 75, 160, 18) && !bModal) { TooltipTitle = Trait.Name + TEXT("  (class trait)"); TooltipBody = Trait.Tooltip; }
        }
    }
    if (const auto& A = CireUIStyle::Assets(); A.Gem) P.Tex(A.Gem, 470, 79, 12, 12, FLinearColor(1.f, .8f, .25f, 1));
    P.Text(FString::Printf(TEXT("%d"), Hero->Gold), 488, 78, 12, FLinearColor(1.f, .85f, .35f, 1), ECireFont::Numbers);
    P.Text(TEXT("GOLD"), 530, 80, 9, Muted, ECireFont::Heading);
    // Micro menu.
    struct FMicro { const TCHAR* Caption; FName Action; const TCHAR* Help; };
    const FMicro Micro[] = {
        {TEXT("SHOP"), TEXT("ToggleShop"), TEXT("Open the shop. During Prep Phase you can buy anywhere; otherwise at your town. Buy gear, tomes and consumables.")},
        {TEXT("OPTIONS"), TEXT("ToggleOptions"), TEXT("Camera, keybindings, interface scale, tooltips, combat text, threat, video and audio.")},
        {TEXT("LAYOUT"), TEXT("ToggleLayoutEditor"), TEXT("Move and resize every interface panel, including the action bars.")},
        {TEXT("KEYBINDS"), NAME_None, TEXT("Quick Keybind mode: hover any action button and press a key to bind it. Backspace/Delete unbinds, Esc exits.")},
        {TEXT("HELP"), TEXT("ToggleHelp"), TEXT("Show the battlefield controls.")}};
    for (int32 I = 0; I < 5; ++I)
    {
        const float BX = 12.f + I * 113.f, BY = 108.f, BW = 106.f, BH = 34.f;
        const bool bOver = Hit(BX, BY, BW, BH) && !bModal && !bSettings && !bEditLayout && !bQuickKeybind;
        const bool bOn = (I == 3 && bQuickKeybind) || (I == 0 && Controller && Controller->bShop);
        CireUIStyle::Button(P, BX, BY, BW, BH, Micro[I].Caption, bOn ? ECireButtonState::Selected : bOver ? (PlayerOwner->IsInputKeyDown(EKeys::LeftMouseButton) ? ECireButtonState::Pressed : ECireButtonState::Hover) : ECireButtonState::Normal, Gold, 11.5f);
        const FString Key = Micro[I].Action.IsNone() ? FString() : UISettings.Keybindings.Label(Micro[I].Action);
        if (!Key.IsEmpty()) { const float KW = P.TextWidth(Key, 8, ECireFont::Numbers) + 8; P.Rect(BX + BW - KW - 4, BY - 7, KW, 13, FLinearColor(0, 0, 0, .85f)); P.Line(BX + BW - KW - 4, BY - 7, BX + BW - 4, BY - 7, Gold * .8f, 1.f); P.Text(Key, BX + BW - KW, BY - 7.5f, 8, Parchment, ECireFont::Numbers, true, false); } // readability: key badge on the frame, clear of the label
        Tip(FString(Micro[I].Caption) + (Key.IsEmpty() ? FString() : TEXT("  (") + UISettings.Keybindings.FullLabel(Micro[I].Action) + TEXT(")")), Micro[I].Help, BX, BY, BW, BH);
        if (bOver && Clicked)
        {
            Clicked = false; PlayUIFeedback();
            if (I == 0 && Controller) Controller->bShop = !Controller->bShop;
            else if (I == 1) ToggleSettings();
            else if (I == 2) ToggleLayoutEditor();
            else if (I == 3) ToggleQuickKeybind();
            else if (I == 4 && Controller) Controller->bHelp = !Controller->bHelp;
        }
    }
    // ---- extra bars ----
    for (int32 Bar = 2; Bar <= 3; ++Bar)
    {
        const bool bShow = Bar == 2 ? UISettings.bShowActionBar2 : UISettings.bShowActionBar3;
        if (!bShow && !bEditLayout) continue;
        UsePanel(Bar == 2 ? TEXT("Bar2") : TEXT("Bar3"), 536, 48);
        bool bAny = false;
        for (int32 I = 0; I < FCireKeybindings::SlotsPerBar; ++I)
            bAny |= !CireKeybindings::SlotAbilityId(UISettings.Keybindings, *Hero, CireKeybindings::SlotAction(Bar, I + 1)).IsEmpty();
        if (bAny || bBarDragging || bQuickKeybind || bEditLayout)
        {
            FCireUIPainter Q = Painter(); Q.Alpha *= bAny ? 1.f : .6f;
            CireUIStyle::Frame(Q, 0, 0, 536, 48, Gold * .8f, ECireFrame::Inset);
        }
        for (int32 I = 0; I < FCireKeybindings::SlotsPerBar; ++I) DrawActionButton(Hero, Controller, Bar, I, 6.f + I * 44.f, 4.f, 40.f);
        // An empty, hidden bar must not swallow world clicks.
        if (!(bAny || bBarDragging || bQuickKeybind || bEditLayout)) VisiblePanels.Remove(Bar == 2 ? FName(TEXT("Bar2")) : FName(TEXT("Bar3")));
    }
    ResetTransform();
    // ---- drag and drop / click-to-cast resolution ----
    const bool bDown = PlayerOwner->IsInputKeyDown(EKeys::LeftMouseButton);
    if (bBarPressCandidate && bDown && !bBarDragging && FVector2D::Distance(DragStart, FVector2D(MX, MY)) > 6.f && !DragAbility.IsEmpty())
    {
        const bool bShift = PlayerOwner->IsInputKeyDown(EKeys::LeftShift) || PlayerOwner->IsInputKeyDown(EKeys::RightShift);
        if (!UISettings.bLockActionBars || bShift) bBarDragging = true;
    }
    if (bBarDragging)
    {
        FCireUIPainter G = Painter(); G.Alpha = .85f;
        FName GhostItem;
        if (CireItems::ParseItemSlotId(DragAbility, GhostItem)) CireShopUI::DrawItemIcon(G, GhostItem, MX - 20, MY - 20, 40, false);
        else
        {
            FCireIconSlot Ghost; Ghost.IconId = DragAbility; Ghost.IconTexture = CireUIStyle::FindAbilityIcon(DragAbility);
            Ghost.Tint = SchoolTint(DragAbility, ACireHero::SkillName(DragAbility));
            CireUIStyle::IconSlot(G, MX - 20, MY - 20, 40, Ghost, Now);
        }
    }
    if (bBarPressCandidate && !bDown)
    {
        const FString ProfileId = Hero->ChampionProfileId;
        if (bBarDragging)
        {
            if (!HoverSlot.IsNone() && HoverSlot != DragSlot && HoverSlot != TEXT("ToggleAutoAttack"))
            {
                // Swap: the target's ability (if any) moves back to the source slot.
                const FString Target = CireKeybindings::SlotAbilityId(UISettings.Keybindings, *Hero, HoverSlot);
                UISettings.Keybindings.AssignSlot(ProfileId, HoverSlot, DragAbility);
                if (Target.IsEmpty()) UISettings.Keybindings.ClearSlot(ProfileId, DragSlot); else UISettings.Keybindings.AssignSlot(ProfileId, DragSlot, Target);
            }
            else if (HoverSlot.IsNone() && !IsPointerOverInterface()) UISettings.Keybindings.ClearSlot(ProfileId, DragSlot); // dropped on the world: remove
            UISettings.Save(); PlayUIFeedback();
        }
        else if (HoverSlot == DragSlot && Controller && !bModal && !bSettings)
        {
            const int32 Item = CireItems::ResolveItemSlot(UISettings.Keybindings, *Hero, DragSlot);
            const int32 Skill = CireKeybindings::ResolveSlot(UISettings.Keybindings, *Hero, DragSlot);
            if (Item != INDEX_NONE) { if (Hero->Inventory) Hero->Inventory->ServerUse(Item, false); }
            else if (Skill != INDEX_NONE) Controller->RequestCast(Skill);
            PressFlashAt.Add(DragAbility, Now);
        }
        bBarPressCandidate = false; bBarDragging = false; DragSlot = NAME_None; DragAbility.Reset();
    }
}

// ---------------------------------------------------------------------------
// Ability tooltip (WoW layout: name / type, cost / range, cast / cooldown, tags, text)
// ---------------------------------------------------------------------------
void ACireHUD::DrawAbilityTooltip(const FString& Id, FVector2D Cursor)
{
    auto* Hero = Cast<ACireHero>(PlayerOwner ? PlayerOwner->GetPawn() : nullptr);
    const FString Name = ACireHero::SkillName(Id);
    const bool bUlt = ACireHero::IsUltimate(Id), bPassive = ACireHero::IsPassive(Id);
    const FString Kind = bUlt ? TEXT("Ultimate") : bPassive ? TEXT("Passive") : TEXT("Active");
    const FFacts F = AbilityFacts(Id);
    const auto D = CireTargeting::Describe(Id);
    const FLinearColor Tint = SchoolTint(Id, Name);
    float NeedMana = F.Mana, NeedEnergy = F.Energy; if (Hero) CireSkillShop::ScaledCost(Hero, Id, F.Mana, F.Energy, NeedMana, NeedEnergy); // items-v2: level-scaled mana
    FString CostText = NeedMana > 0 ? FString::Printf(TEXT("%.0f Mana"), NeedMana) : NeedEnergy > 0 ? FString::Printf(TEXT("%.0f Energy"), NeedEnergy) : FString(TEXT("No cost"));
    const FString RangeText = D.Range > 0 ? FString::Printf(TEXT("%.1f m range"), D.Range / 100) : D.Kind == ECireTargetKind::Self ? FString(TEXT("Self")) : FString(TEXT("Melee"));
    const FString CastText = ACireHero::IsPassive(Id) ? FString(TEXT("Always active")) : F.Windup > .05f ? FString::Printf(TEXT("%.1f sec telegraph"), F.Windup) : FString(TEXT("Instant"));
    const float CDR = Hero ? Hero->CDR : 0.f;
    const FString CooldownText = F.Cooldown > 0 ? FString::Printf(TEXT("%.0f sec cooldown"), F.Cooldown * (1.f - CDR)) : FString();
    // readability: rich WoW ability card: icon, name in its school colour (gold ultimate, silver passive),
    // the type line (school, target, area / projectile), cost | range, cast | cooldown, the description,
    // the scaling math with this hero's numbers, the effects as symbol lines, the Apotheosis upgrade.
    TArray<FString> TagList;
    TagList.Add(SchoolName(Tint));
    TagList.Add(D.Label);
    if (D.bHasFootprint) TagList.Add(TEXT("Area"));
    if (D.bProjectile) TagList.Add(TEXT("Projectile"));
    FCireTooltipSpec T;
    T.Icon = CireUIStyle::FindAbilityIcon(Id); T.Sigil = Id; T.IconTint = Tint;
    T.IconKind = bUlt ? ECireSlotKind::Ultimate : bPassive ? ECireSlotKind::Passive : ECireSlotKind::Normal;
    T.Title = Name;
    T.TitleColor = bUlt ? BrightGold : bPassive ? FLinearColor(.86f, .88f, .96f, 1) : FMath::Lerp(Tint, FLinearColor::White, .3f);
    T.Tag = Kind.ToUpper(); T.TagColor = bUlt ? BrightGold : bPassive ? FLinearColor(.75f, .8f, .95f, 1) : FLinearColor(.8f, .82f, .86f, 1);
    T.Subtitle = FString::Join(TagList, TEXT("  ·  "));
    T.SubtitleColor = FMath::Lerp(Tint, FLinearColor(.85f, .85f, .85f, 1), .35f);
    T.Accent = bUlt ? BrightGold * .9f : Tint * .8f;
    const bool bShort = Hero && ((NeedMana > 0 && Hero->Mana < NeedMana) || (NeedEnergy > 0 && Hero->Energy < NeedEnergy));
    T.Pair(CostText, RangeText, bShort ? FLinearColor(1.f, .35f, .3f, 1) : FLinearColor(.72f, .84f, 1.f, 1), FLinearColor::White);
    T.Pair(CastText, CooldownText, FLinearColor::White, FLinearColor::White);
    T.Divider();
    T.Text(ACireHero::SkillDescription(Id), FLinearColor(1.f, .84f, .4f, 1));
    const FCireAbilityDef* Def = CireAbilityDB::Find(Id);
    const FString Scaling = CireKits::ScalingLine(Hero, Id);
    if (!Scaling.IsEmpty()) { T.Divider(); T.Header(TEXT("SCALING"), FLinearColor(.55f, .8f, 1.f, 1)); T.Stat(Scaling, FLinearColor(.6f, .86f, 1.f, 1)); }
    if (Def)
    {
        bool bHeader = false;
        for (const FCireAbilityEffect& E : Def->Effects)
        {
            if (E.Label.IsEmpty()) continue;
            if (!bHeader) { T.Divider(); T.Header(TEXT("EFFECTS"), FLinearColor(1.f, .7f, .35f, 1)); bHeader = true; }
            T.Stat(E.Duration > 0 ? FString::Printf(TEXT("%s  ·  %.1fs"), *E.Label, E.Duration) : E.Label, CireUIStyle::StatColor(E.Label));
        }
        if (Def->Upgrade.bValid) // items-v2: what the Sigil of Apotheosis adds
        {
            T.Divider();
            T.Header(FString::Printf(TEXT("APOTHEOSIS  ·  %s"), *Def->Upgrade.Name.ToUpper()), BrightGold);
            T.Text(Def->Upgrade.Text + (Hero && CireItems::TotalsOf(Hero).UltimateUpgrade ? TEXT("  (active)") : TEXT("  (with Sigil of Apotheosis)")), FLinearColor(1.f, .9f, .6f, 1), 10.f);
        }
    }
    T.Footer = UISettings.bLockActionBars ? TEXT("Shift-drag to move (bars locked)") : TEXT("Drag to move  ·  drop on the world to remove");
    const float S = FMath::Clamp(UISettings.TooltipScale, .6f, 1.4f) * 1.25f;
    ResetTransform();
    DrawRichTooltip(T, Cursor, 310 * S, false);
}

// ---------------------------------------------------------------------------
// Quick Keybind mode
// ---------------------------------------------------------------------------
void ACireHUD::ToggleQuickKeybind()
{
    bQuickKeybind = !bQuickKeybind;
    if (bSettings && bQuickKeybind) { RevertVideoPreview(); bSettings = false; }
    if (bEditLayout && bQuickKeybind) ToggleLayoutEditor();
    if (!bQuickKeybind && bQuickCapturing) { UISettings.Keybindings.CancelCapture(); bQuickCapturing = false; UISettings.Save(); }
    QuickMessage.Reset();
}
void ACireHUD::UpdateQuickKeybind()
{
    if (!bQuickKeybind) return;
    FCireKeybindings& Keys = UISettings.Keybindings;
    // The controller ticks the capture (it owns the keyboard while capturing). Detect finished captures.
    if (bQuickCapturing && !Keys.IsCapturing())
    {
        bQuickCapturing = false;
        const FCireCaptureResult& R = Keys.LastCapture();
        if (R.Kind == FCireCaptureResult::Cancelled) { ToggleQuickKeybind(); return; } // Esc leaves the mode
        if (R.Kind == FCireCaptureResult::Bound)
        {
            QuickMessage = FString::Printf(TEXT("%s bound to %s"), *R.Chord.LongLabel(), *QuickActionName(R.Action));
            if (!R.Bind.ConflictAction.IsNone())
                QuickMessage += FString::Printf(TEXT("  -  %s now: %s"), *QuickActionName(R.Bind.ConflictAction), R.Bind.ConflictNowBound.IsBound() ? *R.Bind.ConflictNowBound.LongLabel() : TEXT("unbound"));
            UISettings.Save(); PlayUIFeedback();
        }
        else if (R.Kind == FCireCaptureResult::Unbound) { QuickMessage = QuickActionName(R.Action) + TEXT(" unbound"); UISettings.Save(); }
        else if (R.Kind == FCireCaptureResult::Rejected) QuickMessage = TEXT("That key cannot be bound.");
        QuickMessageAt = GetWorld()->GetRealTimeSeconds();
        QuickHold = HoverSlot; // don't rebind the same button until the pointer leaves it
    }
    if (HoverSlot != QuickHold) QuickHold = NAME_None;
    // Right-click on a hovered button unbinds it (Backspace/Delete do the same through capture).
    if (!HoverSlot.IsNone() && PlayerOwner->WasInputKeyJustPressed(EKeys::RightMouseButton))
    {
        Keys.CancelCapture(); bQuickCapturing = false;
        Keys.Unbind(HoverSlot, 0); Keys.Unbind(HoverSlot, 1); UISettings.Save(); PlayUIFeedback();
        QuickMessage = QuickActionName(HoverSlot) + TEXT(" unbound"); QuickMessageAt = GetWorld()->GetRealTimeSeconds(); QuickHold = HoverSlot;
        return;
    }
    if (!HoverSlot.IsNone() && HoverSlot != QuickHold && (!Keys.IsCapturing() || Keys.CaptureAction() != HoverSlot))
    {
        Keys.BeginCapture(HoverSlot, 0); bQuickCapturing = true;
    }
    else if (HoverSlot.IsNone() && bQuickCapturing) { Keys.CancelCapture(); bQuickCapturing = false; }
}
FString ACireHUD::QuickActionName(FName Action) const
{
    const FCireActionInfo* Info = CireKeybindings::Find(Action);
    return Info ? Info->DisplayName.ToString() : Action.ToString();
}
void ACireHUD::DrawQuickKeybind()
{
    if (!bQuickKeybind) return;
    ResetTransform();
    FCireUIPainter P = Painter();
    // Dim everything except the action bars, then the instruction card.
    const FCireUIRect Bars = PanelRect(TEXT("Skills"));
    P.Rect(0, 0, ViewW, FMath::Max(0.f, Bars.Y - 120), FLinearColor(0, 0, 0, .45f));
    const float W = 560, X = (ViewW - W) * .5f, Y = 70;
    CireUIStyle::Frame(P, X, Y, W, 96, FLinearColor(.4f, .8f, 1.f, 1));
    CireUIStyle::Header(P, X + 12, Y + 10, W - 24, TEXT("QUICK KEYBIND MODE"), FLinearColor(.55f, .88f, 1.f, 1), 12);
    P.Text(TEXT("Hover an action button and press a key or chord (Shift/Ctrl/Alt + key) to bind it."), X + 20, Y + 38, 11, Parchment, ECireFont::Body);
    P.Text(TEXT("Backspace, Delete or right-click unbinds.   Esc or the KEYBINDS button exits."), X + 20, Y + 56, 11, Muted, ECireFont::Body);
    const double Age = GetWorld()->GetRealTimeSeconds() - QuickMessageAt;
    if (!QuickMessage.IsEmpty() && Age < 4.0)
        P.Text(QuickMessage, X + 20, Y + 74, 11, FLinearColor(1.f, .85f, .35f, FMath::Clamp(static_cast<float>(4.0 - Age), 0.f, 1.f)), ECireFont::Bold);
    else if (UISettings.Keybindings.IsCapturing())
        P.Text(TEXT("Listening: ") + QuickActionName(UISettings.Keybindings.CaptureAction()) + TEXT("  (current: ") + UISettings.Keybindings.FullLabel(UISettings.Keybindings.CaptureAction()) + TEXT(")"), X + 20, Y + 74, 11, FLinearColor(.55f, .88f, 1.f, 1), ECireFont::Bold);
}

// ---------------------------------------------------------------------------
// Options: Keybindings page
// ---------------------------------------------------------------------------
void ACireHUD::DrawKeybindingsPage(float L, float Top)
{
    FCireKeybindings& Keys = UISettings.Keybindings;
    FCireUIPainter P = Painter();
    const ECireBindCategory Cats[] = {ECireBindCategory::Movement, ECireBindCategory::Combat, ECireBindCategory::Targeting, ECireBindCategory::Interface,
        ECireBindCategory::ActionBar1, ECireBindCategory::ActionBar2, ECireBindCategory::ActionBar3};
    // Category tabs.
    for (int32 I = 0; I < 7; ++I)
    {
        const float BX = L + I * 90.f, BW = 86.f;
        const bool bSel = KeybindCategory == I, bOver = Hit(BX, Top + 32, BW, 28);
        CireUIStyle::Button(P, BX, Top + 32, BW, 28, CireKeybindings::CategoryName(Cats[I]).ToString().ToUpper(), bSel ? ECireButtonState::Selected : bOver ? ECireButtonState::Hover : ECireButtonState::Normal, Gold, 8.5f);
        if (bOver && Clicked) { KeybindCategory = I; Clicked = false; Keys.CancelCapture(); PlayUIFeedback(); }
    }
    // Column headers.
    const float ListY = Top + 70, NameX = L + 10, PrimX = L + 300, SecX = L + 470, CellW = 158, RowH = 29; // readability: taller rows, big key labels
    P.Text(TEXT("ACTION"), NameX, ListY - 2, 9.5f, Parchment * .8f, ECireFont::Heading, true, true);
    P.Text(TEXT("PRIMARY"), PrimX + 8, ListY - 2, 9.5f, Parchment * .8f, ECireFont::Heading, true, true);
    P.Text(TEXT("SECONDARY"), SecX + 8, ListY - 2, 9.5f, Parchment * .8f, ECireFont::Heading, true, true);
    TArray<const FCireActionInfo*> Rows;
    for (const FCireActionInfo& A : CireKeybindings::Actions()) if (A.Category == Cats[FMath::Clamp(KeybindCategory, 0, 6)]) Rows.Add(&A);
    const int32 Visible = 11;
    KeybindScroll = FMath::Clamp(KeybindScroll, 0, FMath::Max(0, Rows.Num() - Visible));
    CireUIStyle::Frame(P, L, ListY + 16, 632, Visible * RowH + 8, Gold, ECireFrame::Inset);
    for (int32 R = 0; R < FMath::Min(Visible, Rows.Num() - KeybindScroll); ++R)
    {
        const FCireActionInfo& A = *Rows[R + KeybindScroll];
        const float Y = ListY + 20 + R * RowH;
        if (R % 2) P.Rect(L + 2, Y, 628, RowH, FLinearColor(1, 1, 1, .025f));
        FString Name = A.DisplayName.ToString();
        int32 Bar = 0, Slot = 0;
        if (CireKeybindings::ParseSlotAction(A.Id, Bar, Slot))
            if (auto* Hero = Cast<ACireHero>(PlayerOwner->GetPawn()))
            {
                const FString Id = CireKeybindings::SlotAbilityId(Keys, *Hero, A.Id);
                if (!Id.IsEmpty()) Name += TEXT("  -  ") + ACireHero::SkillName(Id);
            }
        P.Text(P.Fit(Name, 11.f, PrimX - NameX - 12, ECireFont::Body), NameX, Y + (RowH - CireUIStyle::ReadableSize(11.f) * 1.28f) * .5f, 11.f, Parchment, ECireFont::Body, false, true);
        for (int32 Index = 0; Index < 2; ++Index)
        {
            const float CX = Index ? SecX : PrimX;
            const FCireKeyChord& Chord = Keys.Get(A.Id, Index);
            const bool bCapturing = Keys.IsCapturing() && Keys.CaptureAction() == A.Id && Keys.CaptureIndex() == Index;
            const bool bOver = Hit(CX, Y + 2, CellW, RowH - 4);
            int32 ConflictIndex = INDEX_NONE;
            const FName Conflict = Chord.IsBound() ? Keys.FindConflict(Chord, A.Id, Index, &ConflictIndex) : NAME_None;
            const FString Text = bCapturing ? FString(TEXT("Press a key...")) : Chord.IsBound() ? Chord.LongLabel() : FString(TEXT("\u2014"));
            CireUIStyle::Button(P, CX, Y + 2, CellW, RowH - 4, Text, bCapturing ? ECireButtonState::Selected : bOver ? ECireButtonState::Hover : ECireButtonState::Normal,
                !Conflict.IsNone() ? FLinearColor(1.f, .35f, .3f, 1) : Chord.IsBound() ? Gold : Muted, 13.f);
            if (!Conflict.IsNone()) Tip(TEXT("Key conflict"), Chord.LongLabel() + TEXT(" is also bound to ") + QuickActionName(Conflict) + TEXT("."), CX, Y + 2, CellW, RowH - 4);
            else Tip(A.DisplayName.ToString(), TEXT("Click, then press a key or chord. Esc cancels; Backspace, Delete or right-click clears it (an action may have no key). A key already used elsewhere is taken from that action, leaving it unbound."), CX, Y + 2, CellW, RowH - 4);
            if (bOver && Clicked) { Keys.BeginCapture(A.Id, Index); Clicked = false; PlayUIFeedback(); }
            if (bOver && PlayerOwner->WasInputKeyJustPressed(EKeys::RightMouseButton)) { Keys.CancelCapture(); Keys.Unbind(A.Id, Index); UISettings.Save(); PlayUIFeedback(); }
            // While listening, a small UNBIND button beside the cell clears it.
            if (bCapturing)
            {
                const float UX = CX + CellW - 46, UY = Y + 4; const bool bOverU = Hit(UX, UY, 42, RowH - 8);
                CireUIStyle::Button(P, UX, UY, 42, RowH - 8, TEXT("UNBIND"), bOverU ? ECireButtonState::Hover : ECireButtonState::Normal, FLinearColor(1.f, .45f, .4f, 1), 7.f);
                if (bOverU && Clicked) { Clicked = false; Keys.CancelCapture(); Keys.Unbind(A.Id, Index); UISettings.Save(); PlayUIFeedback(); }
            }
        }
    }
    if (Rows.Num() > Visible)
    {
        P.Text(FString::Printf(TEXT("%d-%d of %d  (mouse wheel)"), KeybindScroll + 1, FMath::Min(Rows.Num(), KeybindScroll + Visible), Rows.Num()), L + 470, ListY + 24 + Visible * RowH, 9, Muted, ECireFont::Body);
        if (Hit(L, ListY + 16, 632, Visible * RowH)) KeybindWheelArmed = true;
    }
    // Result of the last capture (conflict/swaps).
    const FCireCaptureResult& Last = Keys.LastCapture();
    float BY = ListY + 30 + Visible * RowH;
    if (Last.Kind == FCireCaptureResult::Bound && !Last.Bind.ConflictAction.IsNone())
        P.Text(FString::Printf(TEXT("%s was used by %s  -  taken (%s now: %s)"), *Last.Chord.LongLabel(), *QuickActionName(Last.Bind.ConflictAction),
            *QuickActionName(Last.Bind.ConflictAction), Last.Bind.ConflictNowBound.IsBound() ? *Last.Bind.ConflictNowBound.LongLabel() : TEXT("unbound")), L, BY, 10, Orange, ECireFont::Bold);
    BY += 18;
    const bool bOverReset = Hit(L, BY, 200, 28), bOverQuick = Hit(L + 214, BY, 220, 28), bOverBars = Hit(L + 448, BY, 184, 28);
    CireUIStyle::Button(P, L, BY, 200, 28, TEXT("RESET TO DEFAULTS"), bOverReset ? ECireButtonState::Hover : ECireButtonState::Normal);
    CireUIStyle::Button(P, L + 214, BY, 220, 28, TEXT("QUICK KEYBIND MODE"), bOverQuick ? ECireButtonState::Hover : ECireButtonState::Normal, FLinearColor(.55f, .88f, 1.f, 1));
    CireUIStyle::Button(P, L + 448, BY, 184, 28, TEXT("RESET BAR LAYOUT"), bOverBars ? ECireButtonState::Hover : ECireButtonState::Normal);
    Tip(TEXT("Reset to defaults"), TEXT("Restores the WoW default layout: WASD / Q E strafe, Space jump, 1-6 and R on the main bar, Shift+1-6 bar 2, Alt+1-6 bar 3."), L, BY, 200, 28);
    Tip(TEXT("Quick Keybind mode"), TEXT("Closes Options: hover any action button and press a key to bind it."), L + 214, BY, 220, 28);
    Tip(TEXT("Reset bar layout"), TEXT("Puts this champion's abilities back in their automatic slots (learn order, passive, ultimate) and empties bars 2 and 3."), L + 448, BY, 184, 28);
    if (Clicked && bOverReset) { Keys.ResetToDefaults(); UISettings.Save(); Clicked = false; PlayUIFeedback(); }
    if (Clicked && bOverQuick) { Clicked = false; ToggleQuickKeybind(); }
    if (Clicked && bOverBars)
    {
        Clicked = false;
        if (auto* Hero = Cast<ACireHero>(PlayerOwner->GetPawn()))
            for (int32 Bar = 1; Bar <= FCireKeybindings::NumBars; ++Bar) for (int32 I = 1; I <= FCireKeybindings::SlotsPerBar; ++I)
                Keys.ResetSlot(Hero->ChampionProfileId, CireKeybindings::SlotAction(Bar, I));
        UISettings.Save(); PlayUIFeedback();
    }
}
