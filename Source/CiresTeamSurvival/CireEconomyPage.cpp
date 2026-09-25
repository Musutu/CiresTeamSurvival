// progression-shop: F8 > Economy. Live-edit the kill-gold economy (LootTables.json "economy") and
// the Skill Shop tunables (SkillShop.json); changes apply immediately, SAVE writes the data files.
#include "CireHUD.h"
#include "CireGame.h"
#include "CireLoot.h"
#include "CireSkillShop.h"
#include "CireDeveloperTools.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

void ACireHUD::DrawEconomyPage(float X, float Y)
{
#if !UE_BUILD_SHIPPING
    if (!CireDeveloperTools::CanEdit(GetWorld())) return;
    const FLinearColor GoldC(.77f, .61f, .34f, 1), Text(.91f, .9f, .83f, 1), MutedC(.5f, .57f, .59f, 1);
    auto Row = [&](const FString& Title, double& Value, double Min, double Max, double Step, float BX, float BY, const FString& Help)
    {
        Label(Title, BX, BY, 9, Text);
        Label(Step >= 1 ? FString::Printf(TEXT("%.0f"), Value) : FString::Printf(TEXT("%.2f"), Value), BX + 240, BY, 9, GoldC);
        const bool bOver = Hit(BX, BY + 10, 285, 18);
        CireUIStyle::Slider(Painter(), BX, BY + 15, 282, static_cast<float>(FMath::Clamp((Value - Min) / (Max - Min), 0., 1.)), true, bOver);
        Tip(Title, Help, BX, BY, 285, 28);
        if (bOver && PlayerOwner->IsInputKeyDown(EKeys::LeftMouseButton))
        {
            Value = FMath::Clamp(FMath::RoundToDouble((Min + (Max - Min) * FMath::Clamp((MX - Origin.X - BX * Stretch.X) / (282 * Stretch.X), 0.f, 1.f)) / Step) * Step, Min, Max);
            Clicked = false;
        }
    };
    auto IntRow = [&](const FString& Title, int& Value, int Min, int Max, float BX, float BY, const FString& Help)
    { double V = Value; Row(Title, V, Min, Max, 1, BX, BY, Help); Value = static_cast<int>(FMath::RoundToDouble(V)); };
    auto Button = [&](const FString& Title, float BX, float BY, float W)
    {
        const bool bOver = Hit(BX, BY, W, 24);
        CireUIStyle::Button(Painter(), BX, BY, W, 24, Title, bOver ? ECireButtonState::Hover : ECireButtonState::Normal, GoldC, 9.f);
        if (bOver && Clicked) { Clicked = false; PlayUIFeedback(); return true; }
        return false;
    };
    Cires::Items::Economy& E = CireLoot::MutableEconomy();
    FCireSkillShopData& S = CireSkillShop::Mutable();
    auto& R = S.Rules;
    const float L = X, Rt = X + 310, T = Y + 40, Step = 29;
    Label(TEXT("KILL GOLD (per teammate)"), L, T - 4, 10, GoldC);
    IntRow(TEXT("Mob gold at wave 1"), E.MobBase, 0, 20, L, T + 12, TEXT("Normal mob value at the start (Eric: 1)."));
    IntRow(TEXT("+ gold per step"), E.MobStep, 0, 20, L, T + 12 + Step, TEXT("Mob value increase each step (Eric: +1)."));
    IntRow(TEXT("Step every N waves"), E.StepEveryWaves, 1, 20, L, T + 12 + Step * 2, TEXT("Waves per step (Eric: 3, so wave 6 = 3 gold)."));
    Row(TEXT("Armored x"), E.ArmoredMultiplier, 0, 10, .5, L, T + 12 + Step * 3, TEXT("Armored units pay this multiple of the mob value (Eric: 2)."));
    Row(TEXT("Boss x"), E.BossMultiplier, 0, 50, 1, L, T + 12 + Step * 4, TEXT("Lane bosses (Eric: 10)."));
    Row(TEXT("Challenge pack unit x"), E.PackUnitMultiplier, 0, 50, 1, L, T + 12 + Step * 5, TEXT("Each challenge-pack unit (Eric: 10)."));
    Row(TEXT("Pack Leader x"), E.PackLeaderMultiplier, 0, 500, 5, L, T + 12 + Step * 6, TEXT("Pack Leader (Eric: 100 = another x10)."));
    Label(FString::Printf(TEXT("Mob value: wave 1 = %d, 6 = %d, 12 = %d, 20 = %d"), Cires::Items::MobValue(E, 1), Cires::Items::MobValue(E, 6),
        Cires::Items::MobValue(E, 12), Cires::Items::MobValue(E, 20)), L, T + 12 + Step * 7 + 4, 9, MutedC);
    Label(TEXT("SLOT GATE"), L, T + 12 + Step * 8, 10, GoldC);
    IntRow(TEXT("Active slots at start"), R.ActiveSlotsStart, 1, 6, L, T + 28 + Step * 8, TEXT("Active skills allowed before any wave."));
    IntRow(TEXT("+1 active slot every N waves"), R.ActiveSlotEveryWaves, 1, 10, L, T + 28 + Step * 9, TEXT("Spell kits fill up slowly."));
    IntRow(TEXT("Passive from wave"), R.PassiveFromWave, 0, 50, L, T + 28 + Step * 10, TEXT("Wave that opens the passive slot."));
    IntRow(TEXT("Ultimate from wave"), R.UltimateFromWave, 0, 50, L, T + 28 + Step * 11, TEXT("Wave that opens the ultimate slot."));
    Label(TEXT("SKILL PRICES (in mob values)"), Rt, T - 4, 10, GoldC);
    Row(TEXT("Active skill"), R.ActivePrice, 1, 100, 1, Rt, T + 12, TEXT("Price of a new active, times the current mob value."));
    Row(TEXT("Each owned active +"), R.ActiveOwnedGrowth, 0, 2, .05, Rt, T + 12 + Step, TEXT("Every active you own makes the next one this much dearer."));
    Row(TEXT("Passive skill"), R.PassivePrice, 1, 200, 1, Rt, T + 12 + Step * 2, TEXT("Price of the passive."));
    Row(TEXT("Ultimate skill"), R.UltimatePrice, 1, 300, 1, Rt, T + 12 + Step * 3, TEXT("Price of the ultimate."));
    Row(TEXT("Level-up base"), R.LevelUpBase, 1, 50, 1, Rt, T + 12 + Step * 4, TEXT("Level 1 -> 2 price, times the mob value."));
    Row(TEXT("Level-up growth"), R.LevelUpGrowth, 1, 3, .05, Rt, T + 12 + Step * 5, TEXT("Each further level costs this multiple of the previous one."));
    Label(TEXT("PER-LEVEL SCALING (no cap)"), Rt, T + 12 + Step * 6 + 4, 10, GoldC);
    Row(TEXT("Effect + per level"), R.EffectPerLevel, 0, .5, .01, Rt, T + 28 + Step * 6, TEXT("Damage/healing gained per level."));
    Row(TEXT("Cost + per level"), R.CostPerLevel, 0, .5, .01, Rt, T + 28 + Step * 7, TEXT("Mana/energy cost increase per level."));
    Row(TEXT("Cooldown - per level"), R.CooldownPerLevel, 0, .2, .01, Rt, T + 28 + Step * 8, TEXT("Cooldown trimmed per level (multiplicative, floored)."));
    Row(TEXT("Cooldown floor"), R.MinCooldownFactor, .1, 1, .05, Rt, T + 28 + Step * 9, TEXT("Cooldown never drops below this fraction."));
    FString Error;
    if (Button(TEXT("SAVE TO DATA FILES"), Rt, T + 34 + Step * 10, 140))
        DeveloperMessage = CireLoot::SaveEconomy(&Error) && CireSkillShop::Save(&Error) ? TEXT("Saved LootTables.json economy and SkillShop.json.") : Error;
    if (Button(TEXT("RELOAD FROM DATA"), Rt + 145, T + 34 + Step * 10, 140))
    { CireLoot::Reload(); CireSkillShop::Reload(); DeveloperMessage = TEXT("Reloaded economy and Skill Shop data."); }
#endif
}
