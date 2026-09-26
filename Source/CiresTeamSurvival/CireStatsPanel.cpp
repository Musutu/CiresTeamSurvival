// progression-shop: compact, movable League-style character stats window.
// Hovering a row explains how the number is built (base, level, tomes, items, buffs).
#include "CireShopUI.h"
#include "CireHUD.h"
#include "CireGame.h"
#include "CireItems.h"
#include "CireMobility.h"
#include "CireKeybindings.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace CI = Cires::Items;
using namespace CireUIColors;

namespace
{
struct FStatsState
{
    bool bDragging = false;
    FVector2D Grab = FVector2D::ZeroVector;
    int32 DebugRow = -1;
};
FStatsState Stats;

FString Num(double Value, int32 Decimals = 0)
{
    return Decimals == 0 ? FString::Printf(TEXT("%.0f"), Value) : Decimals == 1 ? FString::Printf(TEXT("%.1f"), Value) : FString::Printf(TEXT("%.2f"), Value);
}

// "Rusted Longsword +6, Elixir of Wrath +8" for one stat.
FString ItemSources(const ACireHero* Hero, CI::ItemStat Stat, bool bPercent)
{
    FString Out;
    if (!Hero || !Hero->Inventory) return Out;
    auto Add = [&](FName Id, double Value, const TCHAR* Suffix)
    {
        if (Value == 0) return;
        Out += FString::Printf(TEXT("\n   %s  %+g%s%s"), *CireItems::DisplayName(Id), Value, bPercent ? TEXT("%") : TEXT(""), Suffix);
    };
    for (const auto& Cell : Hero->Inventory->Equipment)
        if (const CI::ItemDef* Item = CireItems::Find(Cell.Id)) Add(Cell.Id, Item->Stats.Get(Stat), TEXT(""));
    for (const auto& Buff : Hero->Inventory->Buffs)
        if (const CI::ItemDef* Item = CireItems::Find(Buff.Id); Item && Item->Use.Kind == CI::EffectKind::Elixir) Add(Buff.Id, Item->Use.Buff.Get(Stat), TEXT("  (elixir)"));
    return Out.IsEmpty() ? TEXT("\n   (none)") : Out;
}

struct FRow { FString Label, Value, Title, Body; FLinearColor Color; };
}

void CireShopUI::DrawStatsWindow(ACireHUD& HUD, ACireHero* Hero)
{
    if (!Hero || !Hero->bDrafted) return;
    constexpr float DW = 176, DH = 238;
    const FName Id(TEXT("Stats"));
    HUD.RegisterPanel(Id);
    FCireUIRect R = HUD.LayoutRect(Id);
    const FVector2D Mouse = CireShopUI::Pointer(HUD);
    APlayerController* PC = HUD.GetOwningPlayerController();
    const bool bDown = PC && PC->IsInputKeyDown(EKeys::LeftMouseButton);
    const bool bInteractive = HUD.IsInteractive() && !HUD.IsModalOpen();
    // Drag by the title bar; the position is saved in the layout profile (CireUI.ini).
    const float SX = R.W / DW, SY = R.H / DH;
    if (bInteractive && !Stats.bDragging && HUD.HasClick() && Mouse.X >= R.X && Mouse.X <= R.X + R.W - 22 * SX && Mouse.Y >= R.Y && Mouse.Y <= R.Y + 20 * SY)
    { HUD.TakeClick(); Stats.bDragging = true; Stats.Grab = Mouse - FVector2D(R.X, R.Y); }
    if (Stats.bDragging)
    {
        if (bDown)
        {
            FCireUIRect Moved = R; Moved.X = Mouse.X - Stats.Grab.X; Moved.Y = Mouse.Y - Stats.Grab.Y;
            const bool bLocked = HUD.UISettings.bLayoutLocked;
            HUD.UISettings.bLayoutLocked = false;
            HUD.UISettings.SetRect(Id, Moved, HUD.LogicalViewport());
            HUD.UISettings.bLayoutLocked = bLocked;
            R = HUD.LayoutRect(Id);
        }
        else { Stats.bDragging = false; HUD.UISettings.Save(); }
    }
    FCireUIPainter P = HUD.ScreenPainter();
    P.Origin = FVector2D(R.X, R.Y); P.Stretch = FVector2D(R.W / DW, R.H / DH);
    const FVector2D M = (Mouse - P.Origin) / P.Stretch;
    CireUIStyle::Frame(P, 0, 0, DW, DH, Gold, ECireFrame::Panel);
    P.Text(TEXT("CHARACTER"), 9, 5, 10, Gold, ECireFont::Heading);
    P.Text(FString::Printf(TEXT("LV %d"), Hero->Level), 98, 5, 10, Parchment, ECireFont::Numbers);
    const FString Key = HUD.UISettings.Keybindings.Label(TEXT("ToggleStats"));
    const bool bOverClose = bInteractive && M.X >= DW - 22 && M.X <= DW - 4 && M.Y >= 3 && M.Y <= 19;
    P.Text(Key.IsEmpty() ? TEXT("x") : Key, DW - 18, 5, 9, bOverClose ? Parchment : Muted, ECireFont::Numbers);
    if (bOverClose) CireShopUI::Tip(HUD, TEXT("Character stats"), FString::Printf(TEXT("Click to hide. %s toggles the window; drag the title bar to move it (saved with your layout)."), *Key));
    if (bOverClose && HUD.HasClick()) { HUD.TakeClick(); HUD.UISettings.bShowStats = false; HUD.UISettings.Save(); return; }
    P.Line(8, 20, DW - 8, 20, Gold * FLinearColor(1, 1, 1, .5f), 1);

    // ---- numbers (client-side from replicated state; server stays authoritative) ----
    const CI::Totals T = CireItems::TotalsOf(Hero);
    const auto* GameState = HUD.GetWorld()->GetGameState<ACireGameState>();
    const int32 Wins = GameState ? (Hero->TeamId == 0 ? GameState->EmberWins : GameState->DuskWins) : 0;
    const float Power = 1.f + FMath::Min(Wins, 4) * .03f;
    const int32 Primary = Hero->PrimaryAttribute();
    const TCHAR* PrimaryName = Hero->PrimaryStat() == Cires::PrimaryStat::Strength ? TEXT("STR") : Hero->PrimaryStat() == Cires::PrimaryStat::Agility ? TEXT("AGI") : TEXT("INT");
    const float ItemAD = static_cast<float>(T.Stats.Get(CI::ItemStat::AttackDamage));
    const float AD = Hero->AttackDamage();
    const float Rhythm = Hero->HasSkill(TEXT("battle_rhythm")) ? 1.2f : 1.f;
    const float AttackSpeed = (1.f + Hero->Agility * .01f + static_cast<float>(T.Stats.Get(CI::ItemStat::AttackSpeed)) / 100.f) * Rhythm / FMath::Max(.1f, Hero->BaseAttackSeconds());
    // str-scaling: STR adds 0.1 armor and 0.1 spell ward per point on top of items.
    const float StrArmor = CireItems::StrengthDefense(Hero, true), StrWard = CireItems::StrengthDefense(Hero, false);
    const float Armor = static_cast<float>(T.Stats.Get(CI::ItemStat::Armor)) + StrArmor, Ward = static_cast<float>(T.Stats.Get(CI::ItemStat::Ward)) + StrWard;
    const float Speed = Hero->GetCharacterMovement() ? Hero->GetCharacterMovement()->MaxWalkSpeed : 0.f;
    const float RegenMult = Hero->HasSkill(TEXT("deep_reserves")) ? 1.5f : 1.f;
    const float ManaRegen = CireItems::BaseManaRegen(Hero, RegenMult) + static_cast<float>(T.Stats.Get(CI::ItemStat::ManaRegen)); // items-v2
    const float EnergyRegen = 9.f * RegenMult + static_cast<float>(T.Stats.Get(CI::ItemStat::EnergyRegen));
    const int32 Tomes = Hero->Inventory ? Hero->Inventory->PrimaryTomePoints : 0;
    auto AttributeBody = [&](int32 Total, CI::ItemStat Stat, bool bPrimary, const TCHAR* Benefit)
    {
        const int32 Items = FMath::RoundToInt(T.Stats.Get(Stat));
        const int32 Growth = (Hero->Level - 1) * (bPrimary ? 2 : 1);
        const int32 Tome = bPrimary ? Tomes : 0;
        return FString::Printf(TEXT("%d total = champion base %d + level growth %d (+%d per level) + tomes %d + items %d%s\n%s"),
            Total, Total - Items - Growth - Tome, Growth, bPrimary ? 2 : 1, Tome, Items, *ItemSources(Hero, Stat, false), Benefit);
    };
    TArray<FRow> Rows;
    Rows.Add({TEXT("Attack Damage"), Num(AD), TEXT("Attack Damage"),
        FString::Printf(TEXT("Basic attack damage.\nWeapon 12 + primary %s %d + items %.0f = %.0f\nx team arena power %.2f = %.0f%s"),
            PrimaryName, Primary, ItemAD, 12 + Primary + ItemAD, Power, AD, *ItemSources(Hero, CI::ItemStat::AttackDamage, false)), FLinearColor(1.f, .55f, .3f, 1)});
    Rows.Add({TEXT("Spell Power"), Num(T.Stats.Get(CI::ItemStat::SpellPower)) + TEXT("%"), TEXT("Spell Power"),
        FString::Printf(TEXT("Abilities and item actives deal and heal %.0f%% more.%s%s"), T.Stats.Get(CI::ItemStat::SpellPower),
            *ItemSources(Hero, CI::ItemStat::SpellPower, true), T.HealAmp > 0 ? *FString::Printf(TEXT("\nHealing also +%.0f%% (Consecrated Mending)."), T.HealAmp) : TEXT("")), FLinearColor(.7f, .5f, 1.f, 1)});
    Rows.Add({TEXT("Armor"), FString::Printf(TEXT("%.1f  (%.0f%%)"), Armor, CI::Mitigation(Armor) * 100), TEXT("Armor"),
        FString::Printf(TEXT("Reduces basic-attack damage taken by armor / (armor + 100) = %.0f%% (max 75%%).\nSTR %d x 0.1 = %.1f + items %.0f.%s"), CI::Mitigation(Armor) * 100, Hero->Strength, StrArmor, Armor - StrArmor, *ItemSources(Hero, CI::ItemStat::Armor, false)), FLinearColor(.85f, .7f, .4f, 1)});
    Rows.Add({TEXT("Spell Ward"), FString::Printf(TEXT("%.1f  (%.0f%%)"), Ward, CI::Mitigation(Ward) * 100), TEXT("Spell Ward"),
        FString::Printf(TEXT("Reduces ability damage taken by ward / (ward + 100) = %.0f%% (max 75%%).\nSTR %d x 0.1 = %.1f + items %.0f.%s"), CI::Mitigation(Ward) * 100, Hero->Strength, StrWard, Ward - StrWard, *ItemSources(Hero, CI::ItemStat::Ward, false)), FLinearColor(.45f, .7f, 1.f, 1)});
    Rows.Add({TEXT("Attack Speed"), Num(AttackSpeed, 2) + TEXT("/s"), TEXT("Attack Speed"),
        FString::Printf(TEXT("%.2f attacks per second = (1 + AGI %d%% + items %.0f%%)%s / %.2f s base swing.%s"), AttackSpeed, Hero->Agility,
            T.Stats.Get(CI::ItemStat::AttackSpeed), Rhythm > 1 ? TEXT(" x 1.20 Battle Rhythm") : TEXT(""), Hero->BaseAttackSeconds(), *ItemSources(Hero, CI::ItemStat::AttackSpeed, true)), FLinearColor(1.f, .85f, .35f, 1)});
    Rows.Add({TEXT("Critical"), FString::Printf(TEXT("%.0f%%  x%.2f"), Hero->CriticalChance * 100, Hero->CriticalMultiplier), TEXT("Critical Strike"),
        FString::Printf(TEXT("Chance %.0f%% = base %.0f%% + items %.0f%%. Critical hits deal %.0f%% damage%s.%s"), Hero->CriticalChance * 100,
            Hero->CriticalChance * 100 - T.Stats.Get(CI::ItemStat::CritChance), T.Stats.Get(CI::ItemStat::CritChance), Hero->CriticalMultiplier * 100,
            T.CritMultiplierBonus > 0 ? TEXT(" (Nightfall +25%)") : TEXT(""), *ItemSources(Hero, CI::ItemStat::CritChance, true)), FLinearColor(1.f, .4f, .3f, 1)});
    Rows.Add({TEXT("Cooldown Red."), Num(Hero->CDR * 100) + TEXT("%"), TEXT("Cooldown Reduction"),
        FString::Printf(TEXT("Skill cooldowns are %.0f%% shorter (pure reduction, capped at 60%%). Items give %.0f%%.%s"), Hero->CDR * 100,
            T.Stats.Get(CI::ItemStat::CooldownReduction), *ItemSources(Hero, CI::ItemStat::CooldownReduction, true)), FLinearColor(.4f, .9f, .9f, 1)});
    Rows.Add({TEXT("Move Speed"), Num(Speed), TEXT("Movement Speed"),
        FString::Printf(TEXT("%.0f cm/s now = run speed x item bonus %.0f%%. Slows cut speed by 35%%; walking is slower.%s"), Speed,
            (CireItems::MoveSpeedMultiplier(Hero) - 1) * 100, *ItemSources(Hero, CI::ItemStat::MoveSpeed, true)), FLinearColor(.6f, 1.f, .6f, 1)});
    Rows.Add({TEXT("Lifesteal"), FString::Printf(TEXT("%.0f%%"), T.Stats.Get(CI::ItemStat::Lifesteal)), TEXT("Lifesteal"),
        FString::Printf(TEXT("Basic attacks heal you for %.0f%% of the damage dealt.%s%s"), T.Stats.Get(CI::ItemStat::Lifesteal),
            T.AbilityLifesteal > 0 ? *FString::Printf(TEXT(" Abilities heal %.0f%% (Blood Tithe)."), T.AbilityLifesteal) : TEXT(""), *ItemSources(Hero, CI::ItemStat::Lifesteal, true)), FLinearColor(.9f, .25f, .3f, 1)});
    Rows.Add({TEXT("Health Regen"), Num(T.Stats.Get(CI::ItemStat::HealthRegen), 1) + TEXT("/s"), TEXT("Health Regeneration"),
        FString::Printf(TEXT("Items restore %.1f health per second. At your town during prep or recovery you also regain 15%% health per second.%s"),
            T.Stats.Get(CI::ItemStat::HealthRegen), *ItemSources(Hero, CI::ItemStat::HealthRegen, false)), Health});
    Rows.Add({TEXT("Mana | Energy"), FString::Printf(TEXT("%.1f | %.0f"), ManaRegen, EnergyRegen), TEXT("Resource Regeneration"),
        FString::Printf(TEXT("Mana: 2 + 0.8%% of max mana (%.0f)%s + items %.1f = %.1f/s. Mana costs rise 5%% per level.\nEnergy: 9/s%s + items %.1f = %.1f/s (100 max).%s"), Hero->MaxMana,
            RegenMult > 1 ? TEXT(" x 1.5 Deep Reserves") : TEXT(""), T.Stats.Get(CI::ItemStat::ManaRegen), ManaRegen, RegenMult > 1 ? TEXT(" x 1.5") : TEXT(""),
            T.Stats.Get(CI::ItemStat::EnergyRegen), EnergyRegen, *ItemSources(Hero, CI::ItemStat::ManaRegen, false)), Mana});
    const bool bSTR = Hero->PrimaryStat() == Cires::PrimaryStat::Strength, bAGI = Hero->PrimaryStat() == Cires::PrimaryStat::Agility, bINT = !bSTR && !bAGI;
    Rows.Add({bSTR ? TEXT("STR (primary)") : TEXT("Strength"), FString::FromInt(Hero->Strength), TEXT("Strength"),
        AttributeBody(Hero->Strength, CI::ItemStat::Strength, bSTR, bSTR ? TEXT("+10 health, +0.1 armor and +0.1 spell ward per point; primary: +1 attack damage per point.") : TEXT("+10 health, +0.1 armor and +0.1 spell ward per point.")), FLinearColor(.95f, .45f, .35f, 1)});
    Rows.Add({bAGI ? TEXT("AGI (primary)") : TEXT("Agility"), FString::FromInt(Hero->Agility), TEXT("Agility"),
        AttributeBody(Hero->Agility, CI::ItemStat::Agility, bAGI, bAGI ? TEXT("+1% attack speed per point; primary: +1 attack damage per point.") : TEXT("+1% attack speed per point.")), FLinearColor(.45f, .9f, .45f, 1)});
    Rows.Add({bINT ? TEXT("INT (primary)") : TEXT("Intelligence"), FString::FromInt(Hero->Intelligence), TEXT("Intelligence"),
        AttributeBody(Hero->Intelligence, CI::ItemStat::Intelligence, bINT, bINT ? TEXT("+30 mana per point; primary: +1 attack damage per point.") : TEXT("+30 mana per point.")), FLinearColor(.45f, .6f, 1.f, 1)});

    const float RowH = (DH - 26) / Rows.Num();
    for (int32 Index = 0; Index < Rows.Num(); ++Index)
    {
        const FRow& Row = Rows[Index];
        const float Y = 24 + Index * RowH;
        const bool bOver = (bInteractive && M.X >= 4 && M.X <= DW - 4 && M.Y >= Y && M.Y < Y + RowH && !Stats.bDragging) || Stats.DebugRow == Index;
        if (bOver) P.Rect(4, Y, DW - 8, RowH, FLinearColor(1.f, .85f, .5f, .08f));
        if (Index >= 11) P.Line(8, Y, DW - 8, Y, Gold * FLinearColor(1, 1, 1, Index == 11 ? .35f : 0.f), 1);
        // Stat glyph: a small faceted gem in the stat colour.
        P.Tri(FVector2D(12, Y + RowH * .5f - 4), FVector2D(8, Y + RowH * .5f), FVector2D(12, Y + RowH * .5f + 4), Row.Color);
        P.Tri(FVector2D(12, Y + RowH * .5f - 4), FVector2D(16, Y + RowH * .5f), FVector2D(12, Y + RowH * .5f + 4), Row.Color * .7f);
        const bool bPrimaryRow = Row.Label.Contains(TEXT("primary"));
        P.Text(Row.Label, 20, Y + 1, 9, bPrimaryRow ? BrightGold : Muted, ECireFont::Body, false, false);
        P.Text(Row.Value, DW - 9 - P.TextWidth(Row.Value, 10, ECireFont::Numbers), Y, 10, Parchment, ECireFont::Numbers, false, true);
        if (bOver) CireShopUI::Tip(HUD, Row.Title, Row.Body);
#if !UE_BUILD_SHIPPING
        if (Stats.DebugRow == Index) HUD.DebugTooltip(Row.Title, Row.Body, P.Origin + FVector2D(DW + 10, Y) * P.Stretch);
#endif
    }
}

#if !UE_BUILD_SHIPPING
void CireShopUI::DebugHoverStat(int32 Row) { Stats.DebugRow = Row; }
#endif
