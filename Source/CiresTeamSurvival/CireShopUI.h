#pragma once
// progression-shop: League-style item shop, bag/belt/teleport bar, purchase and
// loot feedback, and the compact character stats window. Drawn with the shared
// style kit (CireUIStyle); hooked from ACireHUD::DrawHUD / DrawModal.
#include "CoreMinimal.h"
#include "CireUIStyle.h"

class ACireHUD;
class ACireHero;
class ACireController;
class ACireGameState;
class UTexture2D;

namespace CireShopUI
{
    // Always-on HUD: bag + belt + Teleport to Base button, stats window, chest labels.
    CIRESTEAMSURVIVAL_API void DrawHUDElements(ACireHUD& HUD, ACireHero* Hero, ACireController* Controller, ACireGameState* State);
    // The modal shop window (B).
    CIRESTEAMSURVIVAL_API void DrawShop(ACireHUD& HUD, ACireHero* Hero, ACireController* Controller, ACireGameState* State);
    // Toasts, flying icons, gold floaters and the teleport channel bar (drawn on top).
    CIRESTEAMSURVIVAL_API void DrawOverlay(ACireHUD& HUD, ACireHero* Hero, ACireController* Controller);
    // Compact movable stats window (CireStatsPanel.cpp).
    CIRESTEAMSURVIVAL_API void DrawStatsWindow(ACireHUD& HUD, ACireHero* Hero);
    // Personal loot history (toggle: L), newest first.
    CIRESTEAMSURVIVAL_API void DrawLootLog(ACireHUD& HUD, ACireHero* Hero);

    // Shared helpers.
    CIRESTEAMSURVIVAL_API UTexture2D* FindItemIcon(FName ItemId);
    CIRESTEAMSURVIVAL_API FLinearColor TierColor(int32 Tier);
    CIRESTEAMSURVIVAL_API void DrawItemIcon(const FCireUIPainter& P, FName ItemId, float X, float Y, float Size,
        bool bHover, float CooldownFraction = 0.f, float CooldownRemaining = 0.f, int32 Charges = 0,
        const FString& Key = FString(), bool bDim = false);
    CIRESTEAMSURVIVAL_API FString ItemTooltip(FName ItemId, int32 PriceForYou = -1);
    CIRESTEAMSURVIVAL_API FString StatLines(FName ItemId);

#if !UE_BUILD_SHIPPING
    // Gallery/test hooks: select an item, choose a category, and inject feedback moments.
    CIRESTEAMSURVIVAL_API void DebugSelect(FName ItemId, int32 Category = 1);
    CIRESTEAMSURVIVAL_API void DebugHover(FName ItemId);
    CIRESTEAMSURVIVAL_API void DebugPurchaseMoment(FName ItemId, int32 Slot, int32 GoldBefore, int32 Cost, float Age);
    CIRESTEAMSURVIVAL_API void DebugErrorMoment(FName ItemId, const FString& Reason, float Age);
    CIRESTEAMSURVIVAL_API void DebugSellMoment(FName ItemId, int32 Slot, int32 Value, float Age);
    CIRESTEAMSURVIVAL_API void DebugHoverStat(int32 Row);
    CIRESTEAMSURVIVAL_API void DebugReset();
    // Virtual pointer for offscreen captures (logical units); (-1,-1) clears.
    CIRESTEAMSURVIVAL_API void DebugMouse(FVector2D Logical);
    // Freezes UI animation time at (latest feedback event + Age); Age < 0 unfreezes.
    CIRESTEAMSURVIVAL_API void DebugFreezeAfterLastEvent(float Age);
    CIRESTEAMSURVIVAL_API FVector2D DebugGridPos(FName ItemId);
#endif
    // Pointer in logical units (the capture fixture's virtual pointer when set).
    CIRESTEAMSURVIVAL_API FVector2D Pointer(const ACireHUD& HUD);
    // Sets the HUD tooltip (and pins it at the virtual pointer during captures).
    CIRESTEAMSURVIVAL_API void Tip(ACireHUD& HUD, const FString& Title, const FString& Body);
}
