#include "CireShopUI.h"
// progression-shop: League-style shop, bag/belt/teleport bar and purchase/loot feedback.
#include "CireHUD.h"
#include "CireGame.h"
#include "CireItems.h"
#include "CireLoot.h"
#include "CireKeybindings.h"
#include "CireBanners.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Sound/SoundBase.h"

namespace CI = Cires::Items;
using namespace CireUIColors;

namespace
{
// ------------------------------------------------------------------ local UI state
struct FFly { FName Id; FVector2D From, To; double Start = 0; float Size = 40; bool bSell = false; };
struct FToast { FString Title, Body; FName Icon; double Start = 0; float Life = 4.f; FLinearColor Accent = Gold; };
struct FFloater { FString Text; FVector2D Pos; double Start = 0; FLinearColor Color = Gold; };
struct FFlash { FName Id; int32 Slot = -1; bool bBelt = false; double Start = -10; bool bError = false; };

struct FShopState
{
    FName Selected = TEXT("nightfall_reaver");
    int32 SelectedSlot = -1; bool bSelectedBelt = false;
    int32 Category = 0;          // 0 recommended, 1 all items
    uint32 Filters = 0;
    FName Hovered;
    TMap<FName, FVector2D> GridPos; // last drawn grid icon position (fly source)
    FVector2D DetailIconPos = FVector2D::ZeroVector;
    FVector2D SlotPos[9];           // 6 bag + 3 belt, shop strip (logical)
    FVector2D HudSlotPos[9];        // same on the HUD bag bar
    FVector2D GoldPos = FVector2D::ZeroVector;
    bool bShopDrawn = false;
    TArray<FFly> Flies;
    TArray<FToast> Toasts;
    TArray<FFloater> Floaters;
    FFlash Flash, Shake;
    FString ShakeReason;
    FVector2D PendingFrom = FVector2D::ZeroVector;
    FName PendingId;
    // Gold counter
    float ShownGold = -1;
    int32 LastGold = 0;
    double GoldChangeAt = -10;
    float GoldFrom = 0;
    bool bWasShopOpen = false;
    double LastClickTime = 0; FName LastClickId;
    // Stats window drag
    bool bDragging = false; FVector2D DragOffset = FVector2D::ZeroVector;
    double DebugNow = -1;
};
FShopState State;

double Now() { return State.DebugNow >= 0 ? State.DebugNow : FPlatformTime::Seconds(); }
FVector2D VirtualPointer(-1, -1);

const TCHAR* FilterTags[] = {TEXT("attack"), TEXT("attackspeed"), TEXT("crit"), TEXT("lifesteal"), TEXT("spell"), TEXT("mana"),
    TEXT("health"), TEXT("armor"), TEXT("ward"), TEXT("cooldown"), TEXT("speed"), TEXT("support"), TEXT("active"), TEXT("consumable")};
const TCHAR* FilterNames[] = {TEXT("Attack Damage"), TEXT("Attack Speed"), TEXT("Critical Strike"), TEXT("Lifesteal"), TEXT("Spell Power"), TEXT("Mana & Regen"),
    TEXT("Health"), TEXT("Armor"), TEXT("Spell Ward"), TEXT("Cooldowns"), TEXT("Movement"), TEXT("Support"), TEXT("Active Use"), TEXT("Consumables")};
constexpr int32 FilterCount = UE_ARRAY_COUNT(FilterTags);

std::string Utf8(FName Id) { return std::string(TCHAR_TO_UTF8(*Id.ToString())); }
FName ToName(const std::string& Id) { return FName(UTF8_TO_TCHAR(Id.c_str())); }
FString Str(const std::string& Text) { return UTF8_TO_TCHAR(Text.c_str()); }

bool In(FVector2D M, float X, float Y, float W, float H) { return M.X >= X && M.X <= X + W && M.Y >= Y && M.Y <= Y + H; }

USoundBase* ShopSound(const TCHAR* Name)
{
    static TMap<FString, TWeakObjectPtr<USoundBase>> Cache;
    if (auto* Found = Cache.Find(Name); Found && Found->IsValid()) return Found->Get();
    const FString Package = FString::Printf(TEXT("/Game/UI/Shop/%s"), Name);
    USoundBase* Sound = FPackageName::DoesPackageExist(Package) ? LoadObject<USoundBase>(nullptr, *(Package + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
    if (Sound) { Sound->AddToRoot(); Cache.Add(Name, Sound); }
    return Sound;
}
void Play(ACireHUD& HUD, const TCHAR* Name, float Volume = 1.f)
{
    const auto& O = HUD.UISettings;
    if (O.bMuteAudio) return;
    if (USoundBase* Sound = ShopSound(Name)) UGameplayStatics::PlaySound2D(&HUD, Sound, O.MasterVolume * O.UIVolume * Volume);
    else HUD.PlayInterfaceSound(1, .6f * Volume);
}

int32 PriceFor(const ACireHero* Hero, FName Id)
{
    const auto& D = CireItems::Get();
    if (!Hero || !Hero->Inventory) return 0;
    const CI::PurchasePlan Plan = CI::PlanPurchase(D.Catalog, Hero->Inventory->ToRules(), Utf8(Id), MAX_int32 / 2);
    const CI::ItemDef* Item = CireItems::Find(Id);
    return Plan.Ok ? Plan.Cost : Item ? Item->TotalCost : 0;
}

// Client mirror of the server shop access rule (phase + town distance), for instant feedback.
CI::ShopAccess ClientAccess(const ACireHero* Hero, const ACireGameState* GameState)
{
    if (!Hero || !GameState) return CI::ShopAccess::WrongPhase;
    const FVector Base = GetDefault<ACireGameMode>()->BasePosition(Hero->TeamId);
    return CI::CheckShopAccess(CireItems::Get().Shop, GameState->Phase, FVector::Dist2D(Hero->GetActorLocation(), Base), Hero->bDead);
}

void AddToast(const FString& Title, const FString& Body, FName Icon, FLinearColor Accent, float Life = 4.f)
{
    FToast Toast; Toast.Title = Title; Toast.Body = Body; Toast.Icon = Icon; Toast.Start = Now(); Toast.Accent = Accent; Toast.Life = Life;
    State.Toasts.Add(Toast);
    if (State.Toasts.Num() > 5) State.Toasts.RemoveAt(0);
}

void ShowError(ACireHUD& HUD, FName Id, int32 Slot, bool bBelt, const FString& Reason)
{
    State.Shake.Id = Id; State.Shake.Slot = Slot; State.Shake.bBelt = bBelt; State.Shake.Start = Now(); State.Shake.bError = true;
    State.ShakeReason = Reason;
    AddToast(TEXT("Cannot do that"), Reason, Id, Red, 3.2f);
    Play(HUD, TEXT("S_ShopError"), .8f);
}

float ShakeOffset(FName Id, int32 Slot = -2, bool bBelt = false)
{
    const float Age = static_cast<float>(Now() - State.Shake.Start);
    if (Age > .45f) return 0.f;
    const bool bMatch = (!Id.IsNone() && State.Shake.Id == Id) || (Slot >= 0 && State.Shake.Slot == Slot && State.Shake.bBelt == bBelt);
    return bMatch ? FMath::Sin(Age * 55.f) * 7.f * (1.f - Age / .45f) : 0.f;
}

float FlashAmount(FName Id, int32 Slot = -2, bool bBelt = false)
{
    const float Age = static_cast<float>(Now() - State.Flash.Start);
    if (Age > .6f) return 0.f;
    const bool bMatch = (!Id.IsNone() && State.Flash.Id == Id) || (Slot >= 0 && State.Flash.Slot == Slot && State.Flash.bBelt == bBelt);
    return bMatch ? 1.f - Age / .6f : 0.f;
}

void RequestBuy(ACireHUD& HUD, ACireHero* Hero, ACireGameState* GameState, FName Id, FVector2D From)
{
    if (!Hero || !Hero->Inventory) return;
    const CI::ShopAccess Access = ClientAccess(Hero, GameState);
    if (Access != CI::ShopAccess::Allowed) { ShowError(HUD, Id, -1, false, Str(CI::ShopAccessMessage(Access))); return; }
    const CI::PurchasePlan Plan = CI::PlanPurchase(CireItems::Get().Catalog, Hero->Inventory->ToRules(), Utf8(Id), Hero->Gold);
    if (!Plan.Ok) { ShowError(HUD, Id, -1, false, Str(Plan.Error)); return; }
    State.PendingId = Id;
    State.PendingFrom = From;
    Hero->Inventory->ServerBuy(Id);
}

void RequestSell(ACireHUD& HUD, ACireHero* Hero, ACireGameState* GameState, int32 Slot, bool bBelt)
{
    if (!Hero || !Hero->Inventory) return;
    const auto& Cells = bBelt ? Hero->Inventory->Belt : Hero->Inventory->Equipment;
    if (!Cells.IsValidIndex(Slot) || Cells[Slot].Id.IsNone()) return;
    const CI::ShopAccess Access = ClientAccess(Hero, GameState);
    if (Access != CI::ShopAccess::Allowed) { ShowError(HUD, NAME_None, Slot, bBelt, Str(CI::ShopAccessMessage(Access))); return; }
    Hero->Inventory->ServerSell(Slot, bBelt);
}

FVector2D SlotTarget(int32 Slot, bool bBelt)
{
    const int32 Index = FMath::Clamp(Slot, 0, bBelt ? 2 : 5) + (bBelt ? 6 : 0);
    return State.bShopDrawn ? State.SlotPos[Index] : State.HudSlotPos[Index];
}

// Consumes replicated feedback from the server and turns it into the visible moments.
void ProcessFeedback(ACireHUD& HUD, ACireHero* Hero)
{
    if (!Hero || !Hero->Inventory) return;
    TArray<FCireShopFeedback> Feedback = MoveTemp(Hero->Inventory->PendingFeedback);
    Hero->Inventory->PendingFeedback.Reset();
    for (const FCireShopFeedback& F : Feedback)
    {
        const CI::ItemDef* Item = CireItems::Find(F.ItemId);
        const FLinearColor Accent = Item ? CireShopUI::TierColor(static_cast<int32>(Item->Tier)) : Gold;
        switch (F.Action)
        {
        case ECireShopAction::Buy:
            if (!F.bOk) { ShowError(HUD, F.ItemId, -1, false, F.Message); break; }
            State.Flash = {F.ItemId, F.Slot, F.bBelt, Now(), false};
            if (F.Slot >= 0)
            {
                FFly Fly; Fly.Id = F.ItemId; Fly.Start = Now();
                Fly.From = State.PendingId == F.ItemId && !State.PendingFrom.IsZero() ? State.PendingFrom : State.DetailIconPos;
                Fly.To = SlotTarget(F.Slot, F.bBelt);
                State.Flies.Add(Fly);
            }
            AddToast(TEXT("Purchased"), FString::Printf(TEXT("%s   %dg"), *CireItems::DisplayName(F.ItemId), F.GoldDelta), F.ItemId, Accent, 3.f);
            Play(HUD, TEXT("S_ShopBuy"));
            State.PendingId = NAME_None;
            break;
        case ECireShopAction::Sell:
            if (!F.bOk) { ShowError(HUD, F.ItemId, F.Slot, F.bBelt, F.Message); break; }
            {
                FFly Fly; Fly.Id = F.ItemId; Fly.Start = Now(); Fly.bSell = true;
                Fly.From = SlotTarget(F.Slot, F.bBelt); Fly.To = State.GoldPos;
                State.Flies.Add(Fly);
                FFloater Floater; Floater.Text = FString::Printf(TEXT("+%dg"), F.GoldDelta); Floater.Pos = State.GoldPos; Floater.Start = Now() + .35; Floater.Color = BrightGold;
                State.Floaters.Add(Floater);
            }
            if (State.SelectedSlot == F.Slot && State.bSelectedBelt == F.bBelt) State.SelectedSlot = -1;
            AddToast(TEXT("Sold"), FString::Printf(TEXT("%s   +%dg"), *CireItems::DisplayName(F.ItemId), F.GoldDelta), F.ItemId, Gold, 3.f);
            Play(HUD, TEXT("S_ShopSell"));
            break;
        case ECireShopAction::Undo:
            if (!F.bOk) { ShowError(HUD, NAME_None, -1, false, F.Message); break; }
            AddToast(TEXT("Undone"), F.Message.Replace(TEXT("Undone: "), TEXT("")), NAME_None, Teal, 3.f);
            Play(HUD, TEXT("S_ShopUndo"));
            break;
        case ECireShopAction::Use:
            if (!F.bOk) { ShowError(HUD, F.ItemId, F.Slot, F.bBelt, F.Message); break; }
            State.Flash = {F.ItemId, F.Slot, F.bBelt, Now(), false};
            break;
        case ECireShopAction::Loot:
            if (F.Slot == static_cast<int32>(CI::LootKind::Gold) && F.GoldDelta > 0)
            {
                FFloater Floater; Floater.Text = FString::Printf(TEXT("+%dg"), F.GoldDelta); Floater.Pos = State.GoldPos; Floater.Start = Now(); Floater.Color = BrightGold;
                State.Floaters.Add(Floater);
            }
            {
                FString Source, Body = F.Message;
                if (F.Message.Split(TEXT(": "), &Source, &Body))
                {
                    int32 Paren = INDEX_NONE;
                    if (Source.FindChar(TEXT('('), Paren)) Source = Source.Left(Paren).TrimEnd();
                }
                const bool bYou = Body.Contains(TEXT("-> you"));
                AddToast(Source.IsEmpty() ? TEXT("Loot") : Source.ToUpper(), Body, F.ItemId.IsNone() ? FName(TEXT("gold")) : F.ItemId,
                    bYou ? BrightGold : Item ? Accent : Gold, bYou ? 5.f : 4.f);
            }
            Play(HUD, TEXT("S_LootPickup"), .7f);
            break;
        case ECireShopAction::Teleport:
            AddToast(F.bOk ? TEXT("Teleport to Base") : TEXT("Teleport"), F.Message, TEXT("teleport"), F.bOk ? Teal : Orange, 3.f);
            if (F.bOk && F.Slot == 2) Play(HUD, TEXT("S_TeleportArrive"));
            else if (F.bOk && F.Slot == 0) Play(HUD, TEXT("S_TeleportChannel"), .7f);
            break;
        case ECireShopAction::Announce:
            AddToast(TEXT("CHALLENGE OUTPOSTS"), F.Message, TEXT("challenge"), Purple, 7.f);
            break;
        default: break;
        }
    }
}

void DrawGoldCounter(const FCireUIPainter& P, ACireHero* Hero, float X, float Y, float Size, bool bRightAlign)
{
    const float Amount = State.ShownGold < 0 ? static_cast<float>(Hero->Gold) : State.ShownGold;
    const bool bRising = Hero->Gold > FMath::RoundToInt(Amount), bFalling = Hero->Gold < FMath::RoundToInt(Amount);
    const FString Text = FString::Printf(TEXT("%d"), FMath::RoundToInt(Amount));
    const float W = P.TextWidth(Text, Size, ECireFont::Numbers);
    const float Left = bRightAlign ? X - W - Size * 1.25f : X;
    // Coin: stacked discs with a rim.
    const float R = Size * .42f, CX = Left + R, CY = Y + Size * .55f;
    P.Disc(CX + 1, CY + 2, R, FLinearColor(0, 0, 0, .5f), 20);
    P.Disc(CX, CY, R, FLinearColor(.62f, .43f, .1f, 1), 20);
    P.Disc(CX, CY - 1, R * .82f, FLinearColor(1.f, .8f, .28f, 1), 20);
    P.Text(TEXT("g"), CX - R * .38f, CY - R * .95f, R * 1.4f, FLinearColor(.55f, .36f, .06f, 1), ECireFont::Bold, false, false);
    const FLinearColor Color = bFalling ? FLinearColor(1.f, .45f, .35f, 1) : bRising ? FLinearColor(.55f, 1.f, .5f, 1) : BrightGold;
    P.Text(Text, Left + R * 2 + Size * .25f, Y, Size, Color, ECireFont::Numbers, true, true);
    State.GoldPos = P.Origin + FVector2D(CX, CY);
}

void UpdateGold(ACireHero* Hero)
{
    if (State.ShownGold < 0) { State.ShownGold = static_cast<float>(Hero->Gold); State.LastGold = Hero->Gold; }
    if (Hero->Gold != State.LastGold) { State.GoldFrom = State.ShownGold; State.GoldChangeAt = Now(); State.LastGold = Hero->Gold; }
    const float T = FMath::Clamp(static_cast<float>(Now() - State.GoldChangeAt) / .7f, 0.f, 1.f);
    State.ShownGold = FMath::Lerp(State.GoldFrom, static_cast<float>(Hero->Gold), 1.f - FMath::Pow(1.f - T, 3.f));
}

FString KeyLabel(const ACireHUD& HUD, FName Action)
{
    return HUD.UISettings.Keybindings.Label(Action);
}

float ServerTime(const ACireHUD& HUD)
{
    const auto* GameState = HUD.GetWorld() ? HUD.GetWorld()->GetGameState<ACireGameState>() : nullptr;
    return GameState ? static_cast<float>(GameState->GetServerWorldTimeSeconds()) : HUD.GetWorld()->GetTimeSeconds();
}

// Hand-drawn glyph for the teleport button: a hearth-portal arch with a rising spark.
void DrawTeleportGlyph(const FCireUIPainter& P, float X, float Y, float S, FLinearColor Color)
{
    P.Rect(X, Y, S, S, FLinearColor(.02f, .05f, .07f, 1));
    for (int32 I = 0; I < 6; ++I) P.Disc(X + S * .5f, Y + S * .58f, S * (.44f - I * .06f), FLinearColor(.1f + I * .06f, .3f + I * .08f, .42f + I * .08f, .35f), 24);
    const FVector2D C(X + S * .5f, Y + S * .62f);
    for (int32 I = 0; I < 12; ++I)
    {
        const float A0 = PI + PI * I / 12.f, A1 = PI + PI * (I + 1) / 12.f;
        P.Line(C.X + FMath::Cos(A0) * S * .3f, C.Y + FMath::Sin(A0) * S * .36f, C.X + FMath::Cos(A1) * S * .3f, C.Y + FMath::Sin(A1) * S * .36f, Color, 2.f);
    }
    P.Line(X + S * .2f, C.Y, X + S * .2f, Y + S * .86f, Color, 2.f);
    P.Line(X + S * .8f, C.Y, X + S * .8f, Y + S * .86f, Color, 2.f);
    P.Line(X + S * .14f, Y + S * .86f, X + S * .86f, Y + S * .86f, Color, 2.f);
    P.Disc(X + S * .5f, Y + S * .5f, S * .07f, FLinearColor(.85f, 1.f, 1.f, 1), 12);
    P.Line(X + S * .5f, Y + S * .8f, X + S * .5f, Y + S * .56f, FLinearColor(.6f, .95f, 1.f, .8f), 1.5f);
}
} // namespace

// ------------------------------------------------------------------ helpers
FVector2D CireShopUI::Pointer(const ACireHUD& HUD)
{
    return VirtualPointer.X >= 0 ? VirtualPointer : HUD.LogicalMouse();
}

void CireShopUI::Tip(ACireHUD& HUD, const FString& Title, const FString& Body)
{
    HUD.SetTooltip(Title, Body);
#if !UE_BUILD_SHIPPING
    if (VirtualPointer.X >= 0) HUD.DebugTooltip(Title, Body, VirtualPointer + FVector2D(18, 18));
#endif
}

FLinearColor CireShopUI::TierColor(int32 Tier)
{
    switch (static_cast<CI::ItemTier>(Tier))
    {
    case CI::ItemTier::Consumable: return FLinearColor(.45f, .85f, .5f, 1);
    case CI::ItemTier::Basic: return Silver;
    case CI::ItemTier::Epic: return FLinearColor(.72f, .45f, 1.f, 1);
    default: return Orange;
    }
}

UTexture2D* CireShopUI::FindItemIcon(FName ItemId)
{
    static TMap<FName, TWeakObjectPtr<UTexture2D>> Cache;
    static TSet<FName> Missing;
    if (ItemId.IsNone() || Missing.Contains(ItemId)) return nullptr;
    if (auto* Found = Cache.Find(ItemId); Found && Found->IsValid()) return Found->Get();
    const FString Name = TEXT("T_Item_") + ItemId.ToString();
    const FString Package = TEXT("/Game/UI/Items/") + Name;
    UTexture2D* Texture = FPackageName::DoesPackageExist(Package) ? LoadObject<UTexture2D>(nullptr, *(Package + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
    if (!Texture) { Missing.Add(ItemId); return nullptr; }
    Texture->AddToRoot();
    Cache.Add(ItemId, Texture);
    return Texture;
}

void CireShopUI::DrawItemIcon(const FCireUIPainter& P, FName ItemId, float X, float Y, float Size, bool bHover,
    float CooldownFraction, float CooldownRemaining, int32 Charges, const FString& Key, bool bDim)
{
    FCireIconSlot Slot;
    Slot.bEmpty = ItemId.IsNone();
    Slot.IconTexture = FindItemIcon(ItemId);
    Slot.IconId = ItemId.ToString();
    const CI::ItemDef* Item = CireItems::Find(ItemId);
    Slot.Tint = Item ? TierColor(static_cast<int32>(Item->Tier)) : Muted;
    Slot.Kind = Item && Item->Tier == CI::ItemTier::Legendary ? ECireSlotKind::Ultimate : ECireSlotKind::Normal;
    Slot.KeyLabel = Key;
    Slot.CooldownFraction = CooldownFraction;
    Slot.CooldownRemaining = CooldownRemaining;
    Slot.Charges = Charges;
    Slot.bHover = bHover;
    Slot.Flash = FlashAmount(ItemId);
    CireUIStyle::IconSlot(P, X, Y, Size, Slot, Now());
    if (bDim) P.Rect(X + 2, Y + 2, Size - 4, Size - 4, FLinearColor(0, 0, 0, .55f));
}

FString CireShopUI::StatLines(FName ItemId)
{
    const CI::ItemDef* Item = CireItems::Find(ItemId);
    if (!Item) return FString();
    FString Lines;
    for (int32 Index = 0; Index < CI::StatCount; ++Index)
    {
        const auto Stat = static_cast<CI::ItemStat>(Index);
        const double Value = Item->Stats.Get(Stat);
        if (Value == 0) continue;
        const bool bWhole = FMath::IsNearlyEqual(Value, FMath::RoundToDouble(Value));
        const FString Number = bWhole ? FString::Printf(TEXT("%.0f"), Value) : FString::Printf(TEXT("%.1f"), Value);
        Lines += FString::Printf(TEXT("+%s%s %s\n"), *Number, CI::StatIsPercent(Stat) ? TEXT("%") : TEXT(""), UTF8_TO_TCHAR(CI::StatLabel(Stat)));
    }
    return Lines.TrimEnd();
}

FString CireShopUI::ItemTooltip(FName ItemId, int32 PriceForYou)
{
    const CI::ItemDef* Item = CireItems::Find(ItemId);
    if (!Item) return FString();
    const auto& D = CireItems::Get();
    FString Body = FString::Printf(TEXT("%s item  |  %dg"), UTF8_TO_TCHAR(CI::TierName(Item->Tier)), Item->TotalCost);
    if (PriceForYou >= 0 && PriceForYou != Item->TotalCost) Body += FString::Printf(TEXT("  (your price %dg)"), PriceForYou);
    if (!Item->Purchasable) Body += TEXT("  |  loot only");
    const FString Stats = StatLines(ItemId);
    if (!Stats.IsEmpty()) Body += TEXT("\n") + Stats.Replace(TEXT("\n"), TEXT("   "));
    if (const auto* Passives = D.PassiveText.Find(ItemId)) for (const FString& Line : *Passives) Body += TEXT("\nUNIQUE PASSIVE  ") + Line;
    if (const FString* Use = D.UseText.Find(ItemId); Use && !Use->IsEmpty())
        Body += (Item->Belt || Item->Instant ? TEXT("\nUSE  ") : TEXT("\nACTIVE  ")) + *Use;
    if (Item->Unique) Body += TEXT("\nUnique: you may carry only one.");
    if (!Item->UniqueGroup.empty()) Body += FString::Printf(TEXT("\nOnly one %s item may be carried."), UTF8_TO_TCHAR(Item->UniqueGroup.c_str()));
    return Body;
}

// ------------------------------------------------------------------ HUD elements (bag bar)
void CireShopUI::DrawHUDElements(ACireHUD& HUD, ACireHero* Hero, ACireController* Controller, ACireGameState* GameState)
{
    // Fly targets: the shop's bag strip while the shop is open, else the HUD bag bar.
    State.bShopDrawn = Controller && Controller->bShop;
    if (!Hero || !Hero->bDrafted || !Hero->Inventory) return;
    UpdateGold(Hero);
    ProcessFeedback(HUD, Hero);
    // Tell the server when the shop window opens/closes: a visit bounds the undo history.
    const bool bShopOpen = Controller && Controller->bShop;
    if (bShopOpen != State.bWasShopOpen) { Hero->Inventory->ServerShopOpen(bShopOpen); State.bWasShopOpen = bShopOpen; if (bShopOpen) Play(HUD, TEXT("S_ShopOpen"), .6f); }

    constexpr float DW = 306, DH = 102;
    const FCireUIRect R = HUD.LayoutRect(TEXT("Inventory"));
    HUD.RegisterPanel(TEXT("Inventory"));
    FCireUIPainter P = HUD.ScreenPainter();
    P.Origin = FVector2D(R.X, R.Y); P.Stretch = FVector2D(R.W / DW, R.H / DH);
    const FVector2D M = (Pointer(HUD) - P.Origin) / P.Stretch;
    const bool bInteractive = HUD.IsInteractive() && !HUD.IsModalOpen();
    CireUIStyle::Frame(P, 0, 0, DW, DH, Gold, ECireFrame::Panel);
    P.Text(TEXT("BAG"), 10, 5, 9, Gold, ECireFont::Heading);
    P.Text(TEXT("BELT"), 10, 55, 9, Gold, ECireFont::Heading);
    const float STime = ServerTime(HUD);
    FString HoverTitle, HoverBody;
    auto SlotLogic = [&](int32 Index, bool bBelt, float X, float Y, float S, const FString& Key)
    {
        const auto& Cells = bBelt ? Hero->Inventory->Belt : Hero->Inventory->Equipment;
        const FCireItemSlot Cell = Cells.IsValidIndex(Index) ? Cells[Index] : FCireItemSlot();
        const bool bOver = bInteractive && In(M, X, Y, S, S);
        const float Remaining = FMath::Max(0.f, Cell.ReadyAt - STime);
        const float Fraction = Cell.Cooldown > 0 ? FMath::Clamp(Remaining / Cell.Cooldown, 0.f, 1.f) : 0.f;
        const float Shake = ShakeOffset(NAME_None, Index, bBelt);
        FCireIconSlot Slot;
        Slot.bEmpty = Cell.Id.IsNone(); Slot.IconTexture = FindItemIcon(Cell.Id); Slot.IconId = Cell.Id.ToString();
        const CI::ItemDef* Item = CireItems::Find(Cell.Id);
        Slot.Tint = Item ? TierColor(static_cast<int32>(Item->Tier)) : Muted;
        Slot.KeyLabel = Key; Slot.CooldownFraction = Fraction; Slot.CooldownRemaining = Remaining;
        Slot.Charges = bBelt ? Cell.Charges : 0; Slot.bHover = bOver;
        Slot.Flash = FlashAmount(NAME_None, Index, bBelt);
        CireUIStyle::IconSlot(P, X + Shake, Y, S, Slot, Now());
        if (bBelt && Cell.Charges > 1) P.Text(FString::FromInt(Cell.Charges), X + S - 10, Y + S - 13, 10, Parchment, ECireFont::Numbers, true);
        State.HudSlotPos[Index + (bBelt ? 6 : 0)] = P.Origin + FVector2D(X, Y) * P.Stretch;
        if (bOver && Item)
        {
            HoverTitle = CireItems::DisplayName(Cell.Id);
            HoverBody = ItemTooltip(Cell.Id) + (Item->Use.IsSet() && !Item->Instant ? TEXT("\nClick to use.") : TEXT(""));
            if (HUD.HasClick() && Item->Use.IsSet() && !Item->Instant) { HUD.TakeClick(); Hero->Inventory->ServerUse(Index, bBelt); }
        }
        else if (bOver) { HoverTitle = bBelt ? TEXT("Consumable belt") : TEXT("Bag slot"); HoverBody = bBelt ? TEXT("Potions, elixirs and lantern wards stack here. Buy them in the shop.") : TEXT("Six item slots. Components combine into completed items automatically when you buy the recipe."); }
    };
    for (int32 Index = 0; Index < 6; ++Index)
    {
        FName Action = CireItems::ItemAction(Index);
        FString Key = KeyLabel(HUD, Action);
        if (Key.IsEmpty())
        {
            // Show the action-bar key an active item landed on (bar 1 slots 9-12 -> 7,8,9,0).
            const FName ItemId = Hero->Inventory->Equipment.IsValidIndex(Index) ? Hero->Inventory->Equipment[Index].Id : NAME_None;
            for (int32 Bar = 9; Bar <= 12 && !ItemId.IsNone(); ++Bar)
            {
                const FName SlotName = CireKeybindings::SlotAction(1, Bar);
                if (CireItems::ResolveItemSlot(HUD.UISettings.Keybindings, *Hero, SlotName) == Index) { Key = KeyLabel(HUD, SlotName); break; }
            }
        }
        SlotLogic(Index, false, 10 + Index * 40, 17, 36, Key);
    }
    for (int32 Index = 0; Index < 3; ++Index) SlotLogic(Index, true, 44 + Index * 34, 66, 30, KeyLabel(HUD, CireItems::BeltAction(Index)));
    // Gold.
    DrawGoldCounter(P, Hero, 150, 72, 15, false);
    P.Text(FString::Printf(TEXT("[%s] SHOP"), *KeyLabel(HUD, TEXT("ToggleShop"))), 150, 57, 8, Muted, ECireFont::Heading);
    if (bInteractive && In(M, 146, 56, 90, 36))
    {
        HoverTitle = TEXT("Gold");
        HoverBody = FString::Printf(TEXT("Spend it in the shop (%s). During the prep intermission you can buy anywhere; in recovery, buy in town. Kills, challenge chests and arena wins pay gold."), *KeyLabel(HUD, TEXT("ToggleShop")));
        if (HUD.HasClick() && Controller) { HUD.TakeClick(); Controller->bShop = true; }
    }
    // Teleport to Base (merged town recall).
    {
        const float X = 256, Y = 52, S = 42;
        const UCireInventory* Inv = Hero->Inventory;
        const float Remaining = FMath::Max(0.f, Inv->TeleportReadyAt - STime);
        const float Cooldown = static_cast<float>(CireItems::Get().Teleport.CooldownSeconds);
        const bool bOver = bInteractive && In(M, X, Y, S, S);
        const bool bFree = GameState && (GameState->Phase == 1 || GameState->Phase == 4);
        P.Rect(X + 2, Y + 3, S, S, FLinearColor(0, 0, 0, .45f));
        DrawTeleportGlyph(P, X + 2, Y + 2, S - 4, Remaining > 0 && !bFree ? Muted : FLinearColor(.55f, .95f, 1.f, 1));
        if (Remaining > 0 && !bFree && Cooldown > 0) CireUIStyle::CooldownSweep(P, X + 2, Y + 2, S - 4, FMath::Clamp(Remaining / Cooldown, 0.f, 1.f));
        if (const auto& Kit = CireUIStyle::Assets(); Kit.Button) P.Tex(Kit.Button, X, Y, S, S, FLinearColor::White);
        if (Inv->IsChanneling())
        {
            const float T = FMath::Clamp((STime - Inv->TeleportChannelStart) / FMath::Max(.1f, Inv->TeleportChannelEnd - Inv->TeleportChannelStart), 0.f, 1.f);
            CireUIStyle::Glow(P, X - 2, Y - 2, S + 4, S + 4, FLinearColor(.4f, .9f, 1.f, .5f + .3f * FMath::Sin(Now() * 8.f)));
            P.Rect(X + 3, Y + S - 7, (S - 6) * T, 4, FLinearColor(.5f, .95f, 1.f, 1));
        }
        if (Remaining > 0 && !bFree)
        {
            const FString CD = Remaining >= 60 ? FString::Printf(TEXT("%d:%02d"), FMath::FloorToInt(Remaining / 60), FMath::FloorToInt(Remaining) % 60) : FString::Printf(TEXT("%d"), FMath::CeilToInt(Remaining));
            P.Text(CD, X + (S - P.TextWidth(CD, 13, ECireFont::Numbers)) * .5f, Y + 13, 13, Parchment, ECireFont::Numbers, true);
        }
        const FString Key = KeyLabel(HUD, TEXT("RecallToTown"));
        P.Text(Key, X + S - 4 - P.TextWidth(Key, 9, ECireFont::Numbers), Y + 2, 9, Parchment, ECireFont::Numbers, true, false);
        P.Text(TEXT("TELEPORT"), X - 3, Y - 12, 8, bFree ? Teal : Gold, ECireFont::Heading);
        if (bOver) CireUIStyle::Glow(P, X, Y, S, S, FLinearColor(.5f, .9f, 1.f, .3f));
        if (bOver)
        {
            HoverTitle = FString::Printf(TEXT("Teleport to Base  [%s]"), *Key);
            HoverBody = bFree ? TEXT("Prep / recovery: instant and free recall to your town.")
                : FString::Printf(TEXT("Channel %.0f s, then return to town. Taking damage or moving cancels it (no cooldown spent). %.0f s cooldown after a successful teleport. Sealed during the arena.%s"),
                    CireItems::Get().Teleport.ChannelSeconds, Cooldown, Remaining > 0 ? *FString::Printf(TEXT("\nReady in %.0f s."), Remaining) : TEXT(""));
            if (HUD.HasClick() && Controller) { HUD.TakeClick(); Controller->ServerAction(8, 0, nullptr); }
        }
    }
    if (!HoverTitle.IsEmpty()) Tip(HUD, HoverTitle, HoverBody);

    // Loot chest labels in the world (your lane only; relevancy already filters).
    if (APlayerController* PC = HUD.GetOwningPlayerController())
    {
        FCireUIPainter W = HUD.ScreenPainter();
        const float Scale = FMath::Max(.01f, W.Scale);
        for (TActorIterator<ACireLootDrop> It(HUD.GetWorld()); It; ++It)
        {
            if (It->bOpened || It->TeamId != Hero->TeamId) continue;
            FVector2D Screen;
            if (!PC->ProjectWorldLocationToScreen(It->GetActorLocation() + FVector(0, 0, 140), Screen)) continue;
            const float Distance = FVector::Dist(Hero->GetActorLocation(), It->GetActorLocation());
            if (Distance > 6000) continue;
            const FVector2D L = Screen / Scale;
            const FLinearColor C = ACireLootDrop::RarityColor(It->Rarity);
            const FString Title = It->Label.IsEmpty() ? TEXT("Loot") : It->Label;
            const float TW = W.TextWidth(Title, 11, ECireFont::Bold);
            W.Rect(L.X - TW * .5f - 8, L.Y - 3, TW + 16, 30, FLinearColor(0, 0, 0, .55f));
            W.Text(Title, L.X - TW * .5f, L.Y, 11, C, ECireFont::Bold, true);
            const FString Hint = FString::Printf(TEXT("Tier %d  |  walk over to loot  %.0fm"), It->Tier, Distance / 100.f);
            W.Text(Hint, L.X - W.TextWidth(Hint, 8, ECireFont::Body) * .5f, L.Y + 15, 8, Parchment, ECireFont::Body, true);
        }
    }
    if (HUD.UISettings.bShowStats) DrawStatsWindow(HUD, Hero);
}

// ------------------------------------------------------------------ the shop window
void CireShopUI::DrawShop(ACireHUD& HUD, ACireHero* Hero, ACireController* Controller, ACireGameState* GameState)
{
    if (!Hero || !Hero->Inventory) return;
    const auto& D = CireItems::Get();
    UpdateGold(Hero);
    ProcessFeedback(HUD, Hero);
    State.bShopDrawn = true;
    const FVector2D View = HUD.LogicalViewport();
    const float W = 1010, H = 574;
    const float X = FMath::RoundToFloat((View.X - W) * .5f), Y = FMath::RoundToFloat(FMath::Max(40.f, (View.Y - H) * .5f - 14.f));
    FCireUIPainter P = HUD.ScreenPainter();
    P.Rect(0, 0, View.X, View.Y, FLinearColor(0, 0, 0, .45f));
    const FVector2D M = Pointer(HUD);
    const bool bInteractive = HUD.IsInteractive();
    const bool bRightClick = bInteractive && HUD.GetOwningPlayerController() && HUD.GetOwningPlayerController()->WasInputKeyJustPressed(EKeys::RightMouseButton);
    const bool bCtrl = HUD.GetOwningPlayerController() && (HUD.GetOwningPlayerController()->IsInputKeyDown(EKeys::LeftControl) || HUD.GetOwningPlayerController()->IsInputKeyDown(EKeys::RightControl));
    if (bInteractive && bCtrl && HUD.GetOwningPlayerController()->WasInputKeyJustPressed(EKeys::Z)) Hero->Inventory->ServerUndo();
    auto Click = [&](float BX, float BY, float BW, float BH) { if (bInteractive && HUD.HasClick() && In(M, BX, BY, BW, BH)) { HUD.TakeClick(); return true; } return false; };
    CireUIStyle::Frame(P, X, Y, W, H, Gold, ECireFrame::Panel);

    // Title bar: name, access status, gold, close.
    P.Text(TEXT("THE QUARTERMASTER"), X + 20, Y + 12, 20, Parchment, ECireFont::Heading);
    const CI::ShopAccess Access = ClientAccess(Hero, GameState);
    FString Status; FLinearColor StatusColor = Teal;
    if (Access == CI::ShopAccess::Allowed && GameState && GameState->Phase == 1)
        Status = FString::Printf(TEXT("PREP  |  BUY ANYWHERE  |  %d:%02d"), FMath::FloorToInt(FMath::Max(0.f, GameState->SecondsLeft) / 60), FMath::FloorToInt(FMath::Max(0.f, GameState->SecondsLeft)) % 60);
    else if (Access == CI::ShopAccess::Allowed) Status = TEXT("IN TOWN  |  TRADING OPEN");
    else { Status = Access == CI::ShopAccess::NotInTown ? TEXT("CLOSED  |  RETURN TO TOWN") : TEXT("CLOSED  |  OPENS IN PREP"); StatusColor = Red; }
    const float SW = P.TextWidth(Status, 10, ECireFont::Heading) + 22;
    P.Rect(X + 262, Y + 15, SW, 20, StatusColor * FLinearColor(.25f, .25f, .25f, .8f));
    P.Line(X + 262, Y + 35, X + 262 + SW, Y + 35, StatusColor, 1.5f);
    P.Text(Status, X + 273, Y + 18, 10, StatusColor * 1.2f, ECireFont::Heading);
    DrawGoldCounter(P, Hero, X + W - 70, Y + 13, 20, true);
    {
        const bool bOver = In(M, X + W - 60, Y + 12, 44, 24);
        CireUIStyle::Button(P, X + W - 60, Y + 12, 44, 24, KeyLabel(HUD, TEXT("ToggleShop")).IsEmpty() ? TEXT("X") : KeyLabel(HUD, TEXT("ToggleShop")), bOver ? ECireButtonState::Hover : ECireButtonState::Normal, Gold, 10);
        if (Click(X + W - 60, Y + 12, 44, 24) && Controller) Controller->bShop = false;
    }
    CireUIStyle::Header(P, X + 14, Y + 42, W - 28, TEXT(""), Gold, 1);

    // Left column: categories and stat filters.
    const float LX = X + 14, LY = Y + 56;
    const FString RoleKey = CireItems::RoleKey(Hero);
    const FString RoleCaption = RoleKey == TEXT("tank") ? TEXT("TANK") : RoleKey == TEXT("support") ? TEXT("SUPPORT") : RoleKey == TEXT("caster") ? TEXT("SPELL DAMAGE") : TEXT("PHYSICAL DAMAGE");
    const TCHAR* Categories[] = {TEXT("RECOMMENDED"), TEXT("ALL ITEMS")};
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const float BY = LY + Index * 30;
        const bool bOver = In(M, LX, BY, 150, 26);
        CireUIStyle::Button(P, LX, BY, 150, 26, Categories[Index], State.Category == Index ? ECireButtonState::Selected : bOver ? ECireButtonState::Hover : ECireButtonState::Normal, Gold, 10);
        if (Click(LX, BY, 150, 26)) { State.Category = Index; Play(HUD, TEXT("S_ShopTab"), .4f); }
    }
    P.Text(TEXT("FILTER BY STAT"), LX + 2, LY + 70, 9, Gold, ECireFont::Heading);
    for (int32 Index = 0; Index < FilterCount; ++Index)
    {
        const float BY = LY + 86 + Index * 23;
        const bool bOn = (State.Filters & (1u << Index)) != 0;
        const bool bOver = In(M, LX, BY, 150, 20);
        P.Rect(LX, BY, 150, 20, bOn ? FLinearColor(.16f, .13f, .06f, .95f) : bOver ? Hover : FLinearColor(.03f, .04f, .05f, .8f));
        P.Rect(LX + 5, BY + 5, 10, 10, bOn ? BrightGold : FLinearColor(.1f, .12f, .13f, 1));
        if (bOn) P.Line(LX + 6, BY + 10, LX + 14, BY + 10, Ink, 2);
        P.Text(FilterNames[Index], LX + 22, BY + 3, 10, bOn ? Parchment : Muted, ECireFont::Body);
        if (Click(LX, BY, 150, 20)) { State.Filters ^= 1u << Index; State.Category = 1; }
    }
    if (State.Filters != 0)
    {
        const float BY = LY + 86 + FilterCount * 23 + 4;
        const bool bOver = In(M, LX, BY, 150, 22);
        CireUIStyle::Button(P, LX, BY, 150, 22, TEXT("CLEAR FILTERS"), bOver ? ECireButtonState::Hover : ECireButtonState::Normal, Muted, 9);
        if (Click(LX, BY, 150, 22)) State.Filters = 0;
    }

    // Centre: item grid by tier (or the recommended build), with prices and affordability.
    const float GX = X + 176, GY = Y + 56, GW = 470;
    CireUIStyle::Frame(P, GX - 4, GY - 4, GW + 8, H - 136, Gold * .6f, ECireFrame::Inset);
    constexpr float Icon = 42, Step = 51, RowH = 62;
    const int32 PerRow = FMath::FloorToInt((GW - 8) / Step);
    FName HoverId;
    FVector2D HoverPos;
    float CY = GY + 4;
    const CI::Inventory Rules = Hero->Inventory->ToRules();
    auto Matches = [&](const CI::ItemDef& Item)
    {
        if (State.Filters == 0) return true;
        for (int32 Index = 0; Index < FilterCount; ++Index)
            if ((State.Filters & (1u << Index)) && Item.HasTag(Utf8(FName(FilterTags[Index])))) return true;
        return false;
    };
    auto Section = [&](const FString& Caption, const TArray<FName>& Ids, FLinearColor Color)
    {
        if (Ids.Num() == 0) return;
        P.Text(Caption, GX + 4, CY, 9, Color, ECireFont::Heading);
        P.Line(GX + 8 + P.TextWidth(Caption, 9, ECireFont::Heading), CY + 6, GX + GW - 6, CY + 6, Color * FLinearColor(1, 1, 1, .35f), 1);
        CY += 14;
        for (int32 Index = 0; Index < Ids.Num(); ++Index)
        {
            const FName Id = Ids[Index];
            const CI::ItemDef* Item = CireItems::Find(Id);
            if (!Item) continue;
            const float IX = GX + 6 + (Index % PerRow) * Step + ShakeOffset(Id), IY = CY + (Index / PerRow) * RowH;
            const bool bOver = bInteractive && In(M, IX, IY, Icon, Icon + 14);
            const int32 Price = PriceFor(Hero, Id);
            const bool bAffordable = Hero->Gold >= Price;
            const bool bOwned = Rules.CountOf(Item->Id) > 0;
            if (State.Selected == Id) CireUIStyle::Glow(P, IX - 3, IY - 3, Icon + 6, Icon + 6, FLinearColor(1.f, .85f, .4f, .55f));
            DrawItemIcon(P, Id, IX, IY, Icon, bOver, 0, 0, 0, FString(), !bAffordable);
            if (bOwned)
            {
                P.Disc(IX + Icon - 6, IY + Icon - 6, 6, FLinearColor(.08f, .4f, .15f, 1), 12);
                P.Line(IX + Icon - 9, IY + Icon - 6, IX + Icon - 6, IY + Icon - 3, Parchment, 1.5f);
                P.Line(IX + Icon - 6, IY + Icon - 3, IX + Icon - 2, IY + Icon - 10, Parchment, 1.5f);
            }
            const FString Cost = FString::Printf(TEXT("%d"), Price);
            const FLinearColor CostColor = bAffordable ? (Price < Item->TotalCost ? FLinearColor(.55f, 1.f, .5f, 1) : BrightGold) : FLinearColor(.85f, .32f, .3f, 1);
            P.Text(Cost, IX + (Icon - P.TextWidth(Cost, 10, ECireFont::Numbers)) * .5f, IY + Icon + 1, 10, CostColor, ECireFont::Numbers, true, false);
            State.GridPos.Add(Id, FVector2D(IX, IY));
            if (bOver) { HoverId = Id; HoverPos = FVector2D(IX, IY); }
            if (bOver && HUD.HasClick())
            {
                HUD.TakeClick();
                const double T = FPlatformTime::Seconds();
                if (State.LastClickId == Id && T - State.LastClickTime < .35) RequestBuy(HUD, Hero, GameState, Id, FVector2D(IX, IY));
                State.Selected = Id; State.SelectedSlot = -1; State.LastClickId = Id; State.LastClickTime = T;
                Play(HUD, TEXT("S_ShopTab"), .3f);
            }
            if (bOver && bRightClick) { State.Selected = Id; State.SelectedSlot = -1; RequestBuy(HUD, Hero, GameState, Id, FVector2D(IX, IY)); }
        }
        CY += FMath::DivideAndRoundUp(Ids.Num(), PerRow) * RowH + 4;
    };
    if (State.Category == 0)
    {
        P.Text(FString::Printf(TEXT("RECOMMENDED FOR  %s"), *RoleCaption), GX + 4, CY, 11, BrightGold, ECireFont::Heading);
        CY += 20;
        if (const auto* Lists = D.Recommended.Find(RoleKey))
        {
            const TCHAR* Names[] = {TEXT("STARTING ITEMS"), TEXT("CORE BUILD"), TEXT("SITUATIONAL")};
            const FLinearColor Colors[] = {FLinearColor(.45f, .85f, .5f, 1), Orange, Silver};
            for (int32 Index = 0; Index < Lists->Num() && Index < 3; ++Index) Section(Names[Index], (*Lists)[Index], Colors[Index]);
        }
        P.Wrapped(TEXT("Recommendations follow your champion's role. Items build from components: owned parts are consumed and discounted from the price (green)."),
            GX + 6, CY + 4, GW - 16, 9, Muted, 3);
    }
    else
    {
        TArray<FName> Tiers[4];
        for (const FName Id : D.Order)
            if (const CI::ItemDef* Item = CireItems::Find(Id); Item && Item->Purchasable && Matches(*Item)) Tiers[static_cast<int32>(Item->Tier)].Add(Id);
        Section(TEXT("CONSUMABLES & TOMES"), Tiers[0], TierColor(0));
        Section(TEXT("BASIC COMPONENTS"), Tiers[1], TierColor(1));
        Section(TEXT("EPIC COMPONENTS"), Tiers[2], TierColor(2));
        Section(TEXT("LEGENDARY"), Tiers[3], TierColor(3));
        if (Tiers[0].Num() + Tiers[1].Num() + Tiers[2].Num() + Tiers[3].Num() == 0) P.Text(TEXT("No item matches every filter."), GX + 8, CY, 11, Muted);
    }
    if (!HoverId.IsNone()) Tip(HUD, CireItems::DisplayName(HoverId), ItemTooltip(HoverId, PriceFor(Hero, HoverId)));

    // Right: selected item detail with build path tree.
    const float DX = X + 660, DY = Y + 52, DWd = 336, DHt = H - 132;
    CireUIStyle::Frame(P, DX, DY, DWd, DHt, Gold * .6f, ECireFrame::Inset);
    FName Shown = State.Selected;
    int32 SellSlot = -1; bool bSellBelt = false;
    if (State.SelectedSlot >= 0)
    {
        const auto& Cells = State.bSelectedBelt ? Hero->Inventory->Belt : Hero->Inventory->Equipment;
        if (Cells.IsValidIndex(State.SelectedSlot) && !Cells[State.SelectedSlot].Id.IsNone()) { Shown = Cells[State.SelectedSlot].Id; SellSlot = State.SelectedSlot; bSellBelt = State.bSelectedBelt; }
        else State.SelectedSlot = -1;
    }
    if (const CI::ItemDef* Item = CireItems::Find(Shown))
    {
        // Build tree: item -> components -> their components.
        const float TreeY = DY + 10;
        const float RootX = DX + DWd * .5f - 22;
        State.DetailIconPos = FVector2D(RootX, TreeY);
        DrawItemIcon(P, Shown, RootX + ShakeOffset(Shown), TreeY, 44, false);
        const int32 Count = static_cast<int32>(Item->Components.size());
        const float ChildStep = Count > 0 ? FMath::Min(90.f, (DWd - 40) / Count) : 0;
        TArray<FName> Owned;
        for (const auto& Cell : Hero->Inventory->Equipment) if (!Cell.Id.IsNone()) Owned.Add(Cell.Id);
        for (int32 Index = 0; Index < Count; ++Index)
        {
            const FName Child = ToName(Item->Components[Index]);
            const float CX = DX + DWd * .5f + (Index - (Count - 1) * .5f) * ChildStep - 17, CYc = TreeY + 64;
            P.Line(RootX + 22, TreeY + 44, RootX + 22, TreeY + 54, Gold * .7f, 1.5f);
            P.Line(RootX + 22, TreeY + 54, CX + 17, TreeY + 54, Gold * .7f, 1.5f);
            P.Line(CX + 17, TreeY + 54, CX + 17, CYc, Gold * .7f, 1.5f);
            const bool bHave = Owned.Contains(Child);
            if (bHave) Owned.RemoveSingle(Child);
            const bool bOver = bInteractive && In(M, CX, CYc, 34, 34);
            DrawItemIcon(P, Child, CX, CYc, 34, bOver, 0, 0, 0, FString(), !bHave);
            if (bHave) P.Rect(CX, CYc + 36, 34, 2, FLinearColor(.4f, 1.f, .45f, 1));
            if (bOver) { Tip(HUD, CireItems::DisplayName(Child), ItemTooltip(Child, PriceFor(Hero, Child)) + (bHave ? TEXT("\nOwned: consumed by the recipe.") : TEXT(""))); if (HUD.HasClick()) { HUD.TakeClick(); State.Selected = Child; State.SelectedSlot = -1; } }
            if (const CI::ItemDef* Sub = CireItems::Find(Child))
            {
                const int32 SubCount = static_cast<int32>(Sub->Components.size());
                for (int32 S = 0; S < SubCount; ++S)
                {
                    const FName Grand = ToName(Sub->Components[S]);
                    const float GXs = CX + 17 + (S - (SubCount - 1) * .5f) * 30 - 13, GYs = CYc + 50;
                    P.Line(CX + 17, CYc + 38, GXs + 13, GYs, Gold * .45f, 1.f);
                    const bool bGrand = Owned.Contains(Grand) || bHave;
                    if (Owned.Contains(Grand) && !bHave) Owned.RemoveSingle(Grand);
                    const bool bOverGrand = bInteractive && In(M, GXs, GYs, 26, 26);
                    DrawItemIcon(P, Grand, GXs, GYs, 26, bOverGrand, 0, 0, 0, FString(), !bGrand);
                    if (bOverGrand) { Tip(HUD, CireItems::DisplayName(Grand), ItemTooltip(Grand, PriceFor(Hero, Grand))); if (HUD.HasClick()) { HUD.TakeClick(); State.Selected = Grand; State.SelectedSlot = -1; } }
                }
            }
        }
        float TY = TreeY + (Count > 0 ? 150 : 56);
        const FLinearColor Tier = TierColor(static_cast<int32>(Item->Tier));
        P.Text(CireItems::DisplayName(Shown), DX + 12, TY, 15, Tier, ECireFont::Bold);
        TY += 20;
        const int32 Price = PriceFor(Hero, Shown);
        FString CostLine = FString::Printf(TEXT("%s  |  COST %dg"), *FString(UTF8_TO_TCHAR(CI::TierName(Item->Tier))).ToUpper(), Item->TotalCost);
        if (Count > 0) CostLine += FString::Printf(TEXT("  (recipe %dg)"), Item->RecipeCost);
        P.Text(CostLine, DX + 12, TY, 9, Muted, ECireFont::Heading);
        TY += 14;
        if (Price != Item->TotalCost && Item->Purchasable) { P.Text(FString::Printf(TEXT("Your price: %dg (owned parts count)"), Price), DX + 12, TY, 10, FLinearColor(.55f, 1.f, .5f, 1), ECireFont::Bold); TY += 15; }
        TArray<FString> Lines;
        StatLines(Shown).ParseIntoArrayLines(Lines);
        for (const FString& Line : Lines) { P.Text(Line, DX + 12, TY, 11, FLinearColor(.45f, .95f, .5f, 1), ECireFont::Bold); TY += 14; }
        if (const auto* Passives = D.PassiveText.Find(Shown))
            for (const FString& Line : *Passives)
            {
                P.Text(TEXT("UNIQUE PASSIVE"), DX + 12, TY + 2, 8, BrightGold, ECireFont::Heading);
                TY += 12 + (P.Wrapped(Line, DX + 12, TY + 12, DWd - 24, 10, Parchment, 3) * 14);
            }
        if (const FString* Use = D.UseText.Find(Shown); Use && !Use->IsEmpty())
        {
            P.Text(Item->Belt || Item->Instant ? TEXT("USE") : TEXT("ACTIVE  |  CLICK THE BAG SLOT OR PRESS ITS KEY"), DX + 12, TY + 2, 8, Teal, ECireFont::Heading);
            TY += 12 + (P.Wrapped(*Use, DX + 12, TY + 12, DWd - 24, 10, Parchment, 3) * 14);
        }
        if (Item->Unique || !Item->UniqueGroup.empty())
        { P.Text(Item->Unique ? TEXT("Unique: carry only one.") : TEXT("Only one pair of boots."), DX + 12, TY + 2, 9, Muted, ECireFont::Body); TY += 14; }
        if (!Item->Lore.empty() && TY < DY + DHt - 110) P.Wrapped(Str(Item->Lore), DX + 12, TY + 4, DWd - 24, 9, Muted * .9f, 2);
        // Builds into.
        const std::vector<std::string> Into = D.Catalog.BuildsInto(Item->Id);
        if (!Into.empty())
        {
            const float BY = DY + DHt - 98;
            P.Text(TEXT("BUILDS INTO"), DX + 12, BY, 8, Gold, ECireFont::Heading);
            for (int32 Index = 0; Index < static_cast<int32>(Into.size()) && Index < 8; ++Index)
            {
                const FName Parent = ToName(Into[Index]);
                const float IX = DX + 12 + Index * 36, IY = BY + 12;
                const bool bOver = bInteractive && In(M, IX, IY, 32, 32);
                DrawItemIcon(P, Parent, IX, IY, 32, bOver);
                if (bOver) { Tip(HUD, CireItems::DisplayName(Parent), ItemTooltip(Parent, PriceFor(Hero, Parent))); if (HUD.HasClick()) { HUD.TakeClick(); State.Selected = Parent; State.SelectedSlot = -1; } }
            }
        }
        // Buy / sell buttons.
        const float BY = DY + DHt - 44;
        if (SellSlot >= 0)
        {
            const CI::Slot SlotRule = {Utf8(Shown), bSellBelt ? Hero->Inventory->Belt[SellSlot].Charges : 1, 0};
            const int32 Value = CI::SellValue(D.Catalog, SlotRule, D.Shop);
            const bool bOver = In(M, DX + 12, BY, DWd - 24, 34);
            CireUIStyle::Button(P, DX + 12 + ShakeOffset(NAME_None, SellSlot, bSellBelt), BY, DWd - 24, 34, FString::Printf(TEXT("SELL  +%dg"), Value), bOver ? ECireButtonState::Hover : ECireButtonState::Normal, Orange, 13);
            if (Click(DX + 12, BY, DWd - 24, 34)) RequestSell(HUD, Hero, GameState, SellSlot, bSellBelt);
        }
        else if (Item->Purchasable)
        {
            const CI::PurchasePlan Plan = CI::PlanPurchase(D.Catalog, Rules, Item->Id, Hero->Gold);
            const bool bOver = In(M, DX + 12, BY, DWd - 24, 34);
            const FString Label = Plan.Ok ? FString::Printf(TEXT("BUY  %dg"), Plan.Cost) : FString::Printf(TEXT("BUY  %dg  |  %s"), Price, Plan.Error.find("gold") != std::string::npos ? TEXT("NEED GOLD") : TEXT("UNAVAILABLE"));
            CireUIStyle::Button(P, DX + 12 + ShakeOffset(Shown), BY, DWd - 24, 34, Label, !Plan.Ok ? ECireButtonState::Disabled : bOver ? ECireButtonState::Hover : ECireButtonState::Normal, BrightGold, 13);
            if (Click(DX + 12, BY, DWd - 24, 34)) RequestBuy(HUD, Hero, GameState, Shown, State.DetailIconPos);
        }
        else P.Text(TEXT("Loot only: drops from challenge chests and bosses."), DX + 12, BY + 10, 10, Muted);
    }

    // Bottom strip: bag + belt, undo, hotkeys.
    const float BX = X + 176, BYs = Y + H - 70;
    CireUIStyle::Frame(P, X + 14, BYs - 4, W - 28, 62, Gold * .6f, ECireFrame::Inset);
    P.Text(TEXT("YOUR BAG"), X + 26, BYs + 4, 9, Gold, ECireFont::Heading);
    P.Text(TEXT("RIGHT-CLICK: SELL"), X + 26, BYs + 20, 8, Muted, ECireFont::Heading);
    P.Text(FString::Printf(TEXT("%d/6 slots"), 6 - Rules.FreeEquipment()), X + 26, BYs + 34, 9, Parchment, ECireFont::Body);
    const float STime = ServerTime(HUD);
    for (int32 Index = 0; Index < 9; ++Index)
    {
        const bool bBelt = Index >= 6;
        const int32 SlotIndex = bBelt ? Index - 6 : Index;
        const auto& Cells = bBelt ? Hero->Inventory->Belt : Hero->Inventory->Equipment;
        const FCireItemSlot Cell = Cells.IsValidIndex(SlotIndex) ? Cells[SlotIndex] : FCireItemSlot();
        const float S = bBelt ? 40 : 46;
        const float IX = BX + (bBelt ? 6 * 52 + 22 + SlotIndex * 46 : Index * 52) + ShakeOffset(NAME_None, SlotIndex, bBelt), IY = BYs + (bBelt ? 5 : 2);
        const bool bOver = bInteractive && In(M, IX, IY, S, S);
        const float Remaining = FMath::Max(0.f, Cell.ReadyAt - STime);
        if (State.SelectedSlot == SlotIndex && State.bSelectedBelt == bBelt) CireUIStyle::Glow(P, IX - 3, IY - 3, S + 6, S + 6, FLinearColor(1.f, .6f, .2f, .6f));
        FCireIconSlot Slot;
        Slot.bEmpty = Cell.Id.IsNone(); Slot.IconTexture = FindItemIcon(Cell.Id); Slot.IconId = Cell.Id.ToString();
        if (const CI::ItemDef* Item = CireItems::Find(Cell.Id)) Slot.Tint = TierColor(static_cast<int32>(Item->Tier));
        Slot.bHover = bOver; Slot.Charges = bBelt ? Cell.Charges : 0;
        Slot.CooldownRemaining = Remaining; Slot.CooldownFraction = Cell.Cooldown > 0 ? FMath::Clamp(Remaining / Cell.Cooldown, 0.f, 1.f) : 0.f;
        Slot.Flash = FlashAmount(NAME_None, SlotIndex, bBelt);
        Slot.KeyLabel = bBelt ? KeyLabel(HUD, CireItems::BeltAction(SlotIndex)) : KeyLabel(HUD, CireItems::ItemAction(SlotIndex));
        CireUIStyle::IconSlot(P, IX, IY, S, Slot, Now());
        if (bBelt && Cell.Charges > 1) P.Text(FString::FromInt(Cell.Charges), IX + S - 11, IY + S - 14, 11, Parchment, ECireFont::Numbers, true);
        State.SlotPos[Index] = FVector2D(IX, IY);
        if (!bOver || Cell.Id.IsNone()) continue;
        const CI::Slot SlotRule = {Utf8(Cell.Id), Cell.Charges, 0};
        Tip(HUD, CireItems::DisplayName(Cell.Id), ItemTooltip(Cell.Id) + FString::Printf(TEXT("\nSells for %dg (right-click)."), CI::SellValue(D.Catalog, SlotRule, D.Shop)));
        if (HUD.HasClick()) { HUD.TakeClick(); State.SelectedSlot = SlotIndex; State.bSelectedBelt = bBelt; }
        if (bRightClick) RequestSell(HUD, Hero, GameState, SlotIndex, bBelt);
    }
    P.Text(TEXT("BELT"), BX + 6 * 52 + 22, BYs - 9, 8, Gold, ECireFont::Heading);
    // Undo.
    {
        const float UX = BX + 6 * 52 + 22 + 3 * 46 + 12, UY = BYs + 8;
        const bool bCan = Hero->Inventory->UndoDepth > 0;
        const bool bOver = In(M, UX, UY, 118, 34);
        CireUIStyle::Button(P, UX, UY, 118, 34, bCan ? FString::Printf(TEXT("UNDO (%d)"), Hero->Inventory->UndoDepth) : TEXT("UNDO"), !bCan ? ECireButtonState::Disabled : bOver ? ECireButtonState::Hover : ECireButtonState::Normal, Teal, 11);
        if (Click(UX, UY, 118, 34)) { if (bCan) Hero->Inventory->ServerUndo(); else ShowError(HUD, NAME_None, -1, false, TEXT("Nothing to undo in this shop visit.")); }
        if (bOver) Tip(HUD, TEXT("Undo  [Ctrl+Z]"), TEXT("Reverts your last purchase or sale in this shop visit, refunding the full price. The visit ends when you close the shop, leave town, use an item, or the phase changes."));
    }
    const FString Keys = FString::Printf(TEXT("LEFT-CLICK select   |   DOUBLE-CLICK / RIGHT-CLICK buy   |   CTRL+Z undo   |   %s / ESC close"), *KeyLabel(HUD, TEXT("ToggleShop")));
    P.Text(Keys, X + W - 14 - P.TextWidth(Keys, 8, ECireFont::Heading), Y + H - 14, 8, Muted, ECireFont::Heading);
}

// ------------------------------------------------------------------ overlay (toasts, flights, channel)
void CireShopUI::DrawOverlay(ACireHUD& HUD, ACireHero* Hero, ACireController* Controller)
{
    if (!Hero) return;
    FCireUIPainter P = HUD.ScreenPainter();
    const FVector2D View = HUD.LogicalViewport();
    const double T = Now();
    // Flying icons (buy: grid -> bag slot, sell: slot -> gold counter).
    for (int32 Index = State.Flies.Num() - 1; Index >= 0; --Index)
    {
        const FFly& Fly = State.Flies[Index];
        const float Age = static_cast<float>(T - Fly.Start), Life = .5f;
        if (Age > Life + .25f) { State.Flies.RemoveAt(Index); continue; }
        const float K = FMath::Clamp(Age / Life, 0.f, 1.f);
        const float E = 1.f - FMath::Pow(1.f - K, 3.f);
        FVector2D Pos = FMath::Lerp(Fly.From, Fly.To, E);
        Pos.Y -= FMath::Sin(K * PI) * 70.f;
        const float Size = (Fly.bSell ? FMath::Lerp(40.f, 18.f, E) : FMath::Lerp(46.f, 40.f, E)) * (1.f + .15f * FMath::Sin(K * PI));
        if (K < 1.f)
        {
            FCireUIPainter Q = P; Q.Alpha = Fly.bSell ? 1.f - .6f * K : 1.f;
            for (int32 Trail = 1; Trail <= 3; ++Trail)
            {
                const float TK = FMath::Max(0.f, K - Trail * .06f), TE = 1.f - FMath::Pow(1.f - TK, 3.f);
                FVector2D TP = FMath::Lerp(Fly.From, Fly.To, TE); TP.Y -= FMath::Sin(TK * PI) * 70.f;
                CireUIStyle::Glow(Q, TP.X, TP.Y, Size, Size, FLinearColor(1.f, .8f, .35f, .25f / Trail));
            }
            DrawItemIcon(Q, Fly.Id, Pos.X, Pos.Y, Size, false);
        }
        else
        {
            // Landing ring.
            const float L = FMath::Clamp((Age - Life) / .25f, 0.f, 1.f);
            P.Circle(Fly.To.X + 20, Fly.To.Y + 20, 22 + 18 * L, FLinearColor(1.f, .85f, .4f, 1.f - L), 2.5f);
        }
    }
    // Gold floaters.
    for (int32 Index = State.Floaters.Num() - 1; Index >= 0; --Index)
    {
        const FFloater& F = State.Floaters[Index];
        const float Age = static_cast<float>(T - F.Start);
        if (Age > 1.4f) { State.Floaters.RemoveAt(Index); continue; }
        if (Age < 0) continue;
        FCireUIPainter Q = P; Q.Alpha = FMath::Clamp(1.4f - Age, 0.f, 1.f);
        Q.Text(F.Text, F.Pos.X + 12, F.Pos.Y - 12 - Age * 36, 16, F.Color, ECireFont::Numbers, true);
    }
    // Toasts (right edge).
    float TY = View.Y * .30f;
    for (int32 Index = State.Toasts.Num() - 1; Index >= 0; --Index)
    {
        const FToast& Toast = State.Toasts[Index];
        const float Age = static_cast<float>(T - Toast.Start);
        if (Age > Toast.Life) { State.Toasts.RemoveAt(Index); continue; }
        const float In = FMath::Clamp(Age / .25f, 0.f, 1.f), Out = FMath::Clamp((Toast.Life - Age) / .5f, 0.f, 1.f);
        FCireUIPainter Q = P; Q.Alpha = FMath::Min(In, Out);
        const float W = 300, X = View.X - W - 16 + (1.f - (1.f - FMath::Square(1.f - In))) * 60.f;
        const bool bLong = Toast.Body.Len() > 46;
        const float H = bLong ? 56 : 46;
        CireUIStyle::Frame(Q, X, TY, W, H, Toast.Accent, ECireFrame::Card);
        if (Toast.Icon == FName(TEXT("gold"))) { Q.Disc(X + 23, TY + 23, 14, FLinearColor(.62f, .43f, .1f, 1), 20); Q.Disc(X + 23, TY + 22, 11.5f, FLinearColor(1.f, .8f, .28f, 1), 20); }
        else if (Toast.Icon == FName(TEXT("teleport"))) DrawTeleportGlyph(Q, X + 6, TY + 6, 34, FLinearColor(.55f, .95f, 1.f, 1));
        else if (Toast.Icon.IsNone() || Toast.Icon == FName(TEXT("challenge")))
        {
            FCireIconSlot Slot; Slot.IconId = Toast.Icon.IsNone() ? TEXT("role2") : TEXT("role4"); Slot.Tint = Toast.Accent;
            CireUIStyle::IconSlot(Q, X + 6, TY + 6, 34, Slot, T);
        }
        else DrawItemIcon(Q, Toast.Icon, X + 6, TY + 6, 34, false);
        Q.Text(Toast.Title, X + 48, TY + 6, 11, Toast.Accent * 1.15f, ECireFont::Bold, false, true);
        Q.Wrapped(Toast.Body, X + 48, TY + 22, W - 56, 9, Parchment, 3, ECireFont::Body, 2.f);
        TY += H + 6;
    }
    // Teleport channel bar (WoW cast-bar look).
    if (Hero->Inventory && Hero->Inventory->IsChanneling())
    {
        const float STime = ServerTime(HUD);
        const float Frac = FMath::Clamp((STime - Hero->Inventory->TeleportChannelStart) / FMath::Max(.1f, Hero->Inventory->TeleportChannelEnd - Hero->Inventory->TeleportChannelStart), 0.f, 1.f);
        const float W = 300, X = (View.X - W) * .5f, Y = View.Y - 292;
        CireUIStyle::Frame(P, X - 6, Y - 6, W + 12, 36, Teal, ECireFrame::Unit);
        CireUIStyle::Bar(P, X, Y, W, 20, Frac, FLinearColor(.3f, .8f, 1.f, 1), nullptr, T,
            FString::Printf(TEXT("Teleporting to base   %.1f"), FMath::Max(0.f, Hero->Inventory->TeleportChannelEnd - STime)), 10);
        P.Text(TEXT("Moving or taking damage cancels"), X + (W - P.TextWidth(TEXT("Moving or taking damage cancels"), 8)) * .5f, Y + 26, 8, Muted);
    }
}

#if !UE_BUILD_SHIPPING
void CireShopUI::DebugSelect(FName ItemId, int32 Category) { State.Selected = ItemId; State.SelectedSlot = -1; State.Category = Category; }
void CireShopUI::DebugHover(FName ItemId) { State.Hovered = ItemId; }
void CireShopUI::DebugPurchaseMoment(FName ItemId, int32 Slot, int32 GoldBefore, int32 Cost, float Age)
{
    const double Base = FPlatformTime::Seconds() - Age;
    State.Flash = {ItemId, Slot, false, Base, false};
    State.Flies.Reset();
    FFly Fly; Fly.Id = ItemId; Fly.Start = Base;
    Fly.From = State.GridPos.Contains(ItemId) ? State.GridPos[ItemId] : State.DetailIconPos;
    Fly.To = State.SlotPos[FMath::Clamp(Slot, 0, 5)];
    State.Flies.Add(Fly);
    State.GoldFrom = static_cast<float>(GoldBefore); State.GoldChangeAt = Base; State.LastGold = GoldBefore - Cost; State.ShownGold = static_cast<float>(GoldBefore);
    AddToast(TEXT("Purchased"), FString::Printf(TEXT("%s   -%dg"), *CireItems::DisplayName(ItemId), Cost), ItemId, TierColor(3), 3.f);
    State.Toasts.Last().Start = Base;
}
void CireShopUI::DebugErrorMoment(FName ItemId, const FString& Reason, float Age)
{
    const double Base = FPlatformTime::Seconds() - Age;
    State.Shake.Id = ItemId; State.Shake.Start = Base; State.Shake.Slot = -1;
    AddToast(TEXT("Cannot do that"), Reason, ItemId, Red, 3.2f);
    State.Toasts.Last().Start = Base;
}
void CireShopUI::DebugSellMoment(FName ItemId, int32 Slot, int32 Value, float Age)
{
    const double Base = FPlatformTime::Seconds() - Age;
    FFly Fly; Fly.Id = ItemId; Fly.Start = Base; Fly.bSell = true; Fly.From = State.SlotPos[FMath::Clamp(Slot, 0, 8)]; Fly.To = State.GoldPos;
    State.Flies.Add(Fly);
    FFloater Floater; Floater.Text = FString::Printf(TEXT("+%dg"), Value); Floater.Pos = State.GoldPos; Floater.Start = Base + .2; Floater.Color = BrightGold;
    State.Floaters.Add(Floater);
    AddToast(TEXT("Sold"), FString::Printf(TEXT("%s   +%dg"), *CireItems::DisplayName(ItemId), Value), ItemId, Gold, 3.f);
    State.Toasts.Last().Start = Base;
}
void CireShopUI::DebugMouse(FVector2D Logical) { VirtualPointer = Logical; }
FVector2D CireShopUI::DebugGridPos(FName ItemId) { const FVector2D* P = State.GridPos.Find(ItemId); return P ? *P + FVector2D(21, 21) : FVector2D(-1, -1); }
void CireShopUI::DebugReset() { const FName Keep = State.Selected; State = FShopState(); State.Selected = Keep; }
#endif
