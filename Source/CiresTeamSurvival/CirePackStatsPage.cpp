// pack-formations: F8 > Packs. Live-edit the challenge-mob stats (JunglePacks.json "stats" and the per-tier health / damage
// multipliers). Every spawned pack monster rescales at once (health fraction kept); SAVE writes JunglePacks.json.
#include "CireHUD.h"
#include "CireGame.h"
#include "CireJunglePacks.h"
#include "CireDeveloperTools.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

void ACireHUD::DrawPackStatsPage(float X, float Y)
{
#if !UE_BUILD_SHIPPING
    if (!CireDeveloperTools::CanEdit(GetWorld())) return;
    const FLinearColor GoldC(.77f, .61f, .34f, 1), Text(.91f, .9f, .83f, 1), MutedC(.5f, .57f, .59f, 1);
    bool bChanged = false;
    auto Row = [&](const FString& Title, float& Value, float Min, float Max, float Step, float BX, float BY, const FString& Help)
    {
        Label(Title, BX, BY, 9, Text);
        Label(Step >= 1 ? FString::Printf(TEXT("%.0f"), Value) : FString::Printf(TEXT("%.2f"), Value), BX + 240, BY, 9, GoldC);
        const bool bOver = Hit(BX, BY + 10, 285, 18);
        CireUIStyle::Slider(Painter(), BX, BY + 15, 282, FMath::Clamp((Value - Min) / (Max - Min), 0.f, 1.f), true, bOver);
        Tip(Title, Help, BX, BY, 285, 28);
        if (bOver && PlayerOwner->IsInputKeyDown(EKeys::LeftMouseButton))
        {
            const float Next = FMath::Clamp(FMath::RoundToFloat((Min + (Max - Min) * FMath::Clamp((MX - Origin.X - BX * Stretch.X) / (282 * Stretch.X), 0.f, 1.f)) / Step) * Step, Min, Max);
            if (Next != Value) { Value = Next; bChanged = true; }
            Clicked = false;
        }
    };
    auto Button = [&](const FString& Title, float BX, float BY, float W)
    {
        const bool bOver = Hit(BX, BY, W, 24);
        CireUIStyle::Button(Painter(), BX, BY, W, 24, Title, bOver ? ECireButtonState::Hover : ECireButtonState::Normal, GoldC, 9.f);
        if (bOver && Clicked) { Clicked = false; PlayUIFeedback(); return true; }
        return false;
    };
    const FCireJungleRules& JR = CireJunglePacks::Rules();
    FCirePackStats S = JR.Stats;
    float TierHealth[4], TierDamage[4];
    for (int32 K = 0; K < 4; ++K) { TierHealth[K] = JR.Tiers[K].Health; TierDamage[K] = JR.Tiers[K].Damage; }
    const float L = X, Rt = X + 310, T = Y + 40, Step = 29;
    Label(TEXT("CHALLENGE MOBS (all packs)"), L, T - 4, 10, GoldC);
    Row(TEXT("Global health x"), S.GlobalHealth, .1f, 10.f, .05f, L, T + 12, TEXT("Every challenge mob's health. Spawned packs rescale now (health fraction kept)."));
    Row(TEXT("Global damage x"), S.GlobalDamage, .1f, 10.f, .05f, L, T + 12 + Step, TEXT("Every challenge mob's damage."));
    Row(TEXT("Base damage"), S.BaseDamage, 0.f, 1000.f, 5.f, L, T + 12 + Step * 2, TEXT("Damage of every role before multipliers (Eric: 200)."));
    Row(TEXT("Tank health"), S.TankHealth, 100.f, 10000.f, 50.f, L, T + 12 + Step * 3, TEXT("Tank base health (Eric: 1500); the other roles are shares of it."));
    Row(TEXT("Healer share"), S.HealerShare, .1f, 2.f, .05f, L, T + 12 + Step * 4, TEXT("Support casters: share of tank health (Eric: 0.50)."));
    Row(TEXT("Melee DPS share"), S.MeleeShare, .1f, 2.f, .05f, L, T + 12 + Step * 5, TEXT("Melee DPS: share of tank health (Eric: 0.75)."));
    Row(TEXT("Ranged DPS share"), S.RangedShare, .1f, 2.f, .05f, L, T + 12 + Step * 6, TEXT("Physical ranged DPS: share of tank health (Eric: 0.65)."));
    Row(TEXT("Caster DPS share"), S.CasterShare, .1f, 2.f, .05f, L, T + 12 + Step * 7, TEXT("Ranged caster DPS: share of tank health (Eric: 0.65)."));
    Label(TEXT("PER TIER"), Rt, T - 4, 10, GoldC);
    for (int32 K = 0; K < 4; ++K)
    {
        Row(FString::Printf(TEXT("T%d health x"), K + 1), TierHealth[K], .1f, 10.f, .05f, Rt, T + 12 + Step * (2 * K), FString::Printf(TEXT("Health multiplier of every tier-%d pack."), K + 1));
        Row(FString::Printf(TEXT("T%d damage x"), K + 1), TierDamage[K], .1f, 10.f, .05f, Rt, T + 12 + Step * (2 * K + 1), FString::Printf(TEXT("Damage multiplier of every tier-%d pack."), K + 1));
    }
    if (bChanged) CireJunglePacks::SetStats(GetWorld(), S, TierHealth, TierDamage);
    const float Sum = T + 12 + Step * 8 + 4;
    for (int32 Tier = 1; Tier <= 4; ++Tier)
        Label(FString::Printf(TEXT("T%d  tank %.0f  melee %.0f  ranged/caster %.0f  healer %.0f  |  damage %.0f  (leader x%.1f HP)"), Tier,
            CireJunglePacks::UnitHealth(ECirePackRole::Tank, Tier, false), CireJunglePacks::UnitHealth(ECirePackRole::Melee, Tier, false),
            CireJunglePacks::UnitHealth(ECirePackRole::Ranged, Tier, false), CireJunglePacks::UnitHealth(ECirePackRole::Healer, Tier, false),
            CireJunglePacks::UnitDamage(ECirePackRole::Melee, Tier), JR.LeaderHealth), L, Sum + (Tier - 1) * 16, 9, MutedC);
    if (Button(TEXT("SAVE TO JunglePacks.json"), L, Y + 386, 285))
    { FString Why; DeveloperMessage = CireJunglePacks::SaveStats(&Why) ? TEXT("Pack stats saved to Content/Data/JunglePacks.json.") : Why; }
    Label(TEXT("Also in the map layout editor: select a Challenge Pack > PACK STATS."), Rt, Y + 392, 9, MutedC);
#endif
}
