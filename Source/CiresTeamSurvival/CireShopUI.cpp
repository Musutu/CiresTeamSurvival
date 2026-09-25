#include "CireShopUI.h"
#include "CireShopArt.h" // progression-shop: scroll cards and ornate framing
#include "CireSkillShop.h" // progression-shop: Skill Shop tab
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
#include "CireAudio.h" // audio:

namespace CI = Cires::Items;
using namespace CireUIColors;

namespace
{
// ------------------------------------------------------------------ local UI state
struct FFly { FName Id; FVector2D From, To; double Start = 0; float Size = 40; bool bSell = false; int32 bLootRow = -1; int32 ToSlot = -1; bool bSkill = false; int32 ToSkillSlot = -1; };
struct FWorldGold { FVector Where = FVector::ZeroVector; int32 Amount = 0; uint8 Kind = 0; double Start = 0; };
struct FToast { FString Title, Body; FName Icon; double Start = 0; float Life = 4.f; FLinearColor Accent = Gold; };
struct FFloater { FString Text; FVector2D Pos; double Start = 0; FLinearColor Color = Gold; };
struct FFlash { FName Id; int32 Slot = -1; bool bBelt = false; double Start = -10; bool bError = false; };
struct FLootWindow { FCireLootReport Report; double Start = 0; bool bClosed = false; };
struct FLootLogEntry { FString When, Text, Why; FName Icon; FLinearColor Color = Parchment; };

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
    // Personal loot presentation
    TArray<FLootWindow> LootWindows;
    TArray<FLootLogEntry> LootLog;
    FVector2D LootRowPos[12];
    // Skill Shop
    int32 Tab = 0;              // 0 items, 1 skills
    int32 PendingTab = -1;      // tab to show when the shop next opens
    FString SelectedSkill;
    TMap<FString, FVector2D> SkillGridPos;
    FVector2D SkillSlotPos[8];
    FVector2D SkillDetailPos = FVector2D::ZeroVector;
    FString StampId; double StampStart = -10; int32 StampLevel = 1;   // wax-seal purchase moment
    TMap<FString, float> SkillLift;  // hover lift 0..1 per scroll
    double LastSkillFrame = 0;
    int32 LastPhaseSeen = -1;
    int32 LastClearedSeen = -1;
    TArray<FWorldGold> WorldGold;
    double LastClickTime = 0; FName LastClickId;
    // Stats window drag
    bool bDragging = false; FVector2D DragOffset = FVector2D::ZeroVector;
    double DebugNow = -1;
};
FShopState State;
bool bSuppressFlash = false;

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
    if (CireAudio::PlayShopSound(&HUD, Name, Volume)) return; // audio: recorded CC0 cue (AudioCues.json shopLegacy)
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
        case ECireShopAction::SkillBuy:
        case ECireShopAction::SkillLevel:
        {
            if (!F.bOk) { ShowError(HUD, F.ItemId, -1, false, F.Message); break; }
            const bool bLevel = F.Action == ECireShopAction::SkillLevel;
            const FString SkillId = F.ItemId.ToString();
            State.Flash = {F.ItemId, -1, false, Now(), false};
            State.StampId = F.ItemId.ToString(); State.StampStart = Now(); // wax seal on the scroll
            FFly Fly; Fly.Id = F.ItemId; Fly.Start = Now() + .32; Fly.bSkill = true; Fly.Size = 44; // after the seal lands
            const FVector2D* From = State.SkillGridPos.Find(SkillId);
            Fly.From = From ? *From : State.SkillDetailPos;
            Fly.ToSkillSlot = FMath::Clamp(F.Slot, 0, 7);
            State.Flies.Add(Fly);
            AddToast(bLevel ? TEXT("Skill levelled up") : TEXT("Skill learned"), F.Message, F.ItemId, bLevel ? Teal : Purple, 3.5f);
            Play(HUD, TEXT("S_ShopBuy"));
            if (bLevel) Play(HUD, TEXT("S_LootPickup"), .5f);
            break;
        }
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

bool AnySkillAffordable(const ACireHero* Hero)
{
    for (const FCireShopSkill& Skill : CireSkillShop::CatalogFor(Hero))
    {
        if (CireSkillShop::BuyBlocker(Hero, Skill.Id).IsEmpty()) return true;
        if (Hero->Skills.Contains(Skill.Id) && Hero->Gold >= CireSkillShop::LevelPrice(Hero, Skill.Id)) return true;
    }
    return false;
}

// Kill bounties: "+3g" floats over the kill; big bounties also get a toast.
void ProcessGold(ACireHUD& HUD, ACireHero* Hero)
{
    if (!Hero || !Hero->Inventory || Hero->Inventory->PendingGold.Num() == 0) return;
    for (const auto& Gain : Hero->Inventory->PendingGold)
    {
        FWorldGold W; W.Where = Gain.Where; W.Amount = Gain.Amount; W.Kind = Gain.Kind; W.Start = Now();
        State.WorldGold.Add(W);
        const auto Kind = static_cast<CI::BountyKind>(Gain.Kind);
        if (Kind == CI::BountyKind::Boss || Kind == CI::BountyKind::PackLeader)
        {
            AddToast(Kind == CI::BountyKind::Boss ? TEXT("Boss bounty") : TEXT("Pack Leader bounty"), FString::Printf(TEXT("+%d gold for you (%d x the wave's mob value)"),
                Gain.Amount, Kind == CI::BountyKind::Boss ? 10 : 100), TEXT("gold"), BrightGold, 4.f);
            Play(HUD, TEXT("S_ShopSell"), .7f);
        }
    }
    Hero->Inventory->PendingGold.Reset();
    if (State.WorldGold.Num() > 40) State.WorldGold.RemoveAt(0, State.WorldGold.Num() - 40);
}

FLinearColor RarityColor(int32 Rarity)
{
    switch (Rarity)
    {
    case 3: return Orange;
    case 2: return FLinearColor(.72f, .45f, 1.f, 1);
    case 1: return FLinearColor(.35f, .65f, 1.f, 1);
    default: return Parchment;
    }
}

FString WorldClock(const ACireHUD& HUD)
{
    const float T = HUD.GetWorld() ? HUD.GetWorld()->GetTimeSeconds() : 0.f;
    return FString::Printf(TEXT("%02d:%02d"), FMath::FloorToInt(T / 60.f), FMath::FloorToInt(T) % 60);
}

// Turns server loot reports into the loot window, toasts, bag flights, log lines and a rarity sound.
void ProcessLoot(ACireHUD& HUD, ACireHero* Hero)
{
    if (!Hero || !Hero->Inventory || Hero->Inventory->PendingLoot.Num() == 0) return;
    TArray<FCireLootReport> Reports = MoveTemp(Hero->Inventory->PendingLoot);
    Hero->Inventory->PendingLoot.Reset();
    for (const FCireLootReport& Report : Reports)
    {
        FLootWindow Window; Window.Report = Report; Window.Start = Now();
        State.LootWindows.Add(Window);
        if (State.LootWindows.Num() > 4) State.LootWindows.RemoveAt(0);
        for (int32 Index = 0; Index < Report.Lines.Num(); ++Index)
        {
            const FCireLootLine& Line = Report.Lines[Index];
            const bool bThing = Line.Kind == static_cast<uint8>(CI::LootKind::Item) || Line.Kind == static_cast<uint8>(CI::LootKind::PrimaryTome);
            FLootLogEntry Entry;
            Entry.When = WorldClock(HUD);
            Entry.Icon = Line.ItemId.IsNone() ? FName(TEXT("gold")) : Line.ItemId;
            Entry.Color = RarityColor(Line.Rarity);
            Entry.Text = Line.Kind == static_cast<uint8>(CI::LootKind::PrimaryTome) ? CireItems::DisplayName(Line.ItemId) + TEXT(": ") + Line.Text
                : Line.ConvertedGold > 0 ? FString::Printf(TEXT("%s (no room: +%d gold)"), *Line.Text, Line.ConvertedGold) : Line.Text;
            Entry.Why = Report.Source;
            State.LootLog.Insert(Entry, 0);
            if (bThing)
                AddToast(Line.Kind == static_cast<uint8>(CI::LootKind::PrimaryTome) ? CireItems::DisplayName(Line.ItemId) : Line.Text,
                    Line.Kind == static_cast<uint8>(CI::LootKind::PrimaryTome) ? Line.Text + TEXT("  |  ") + Report.Source : Report.Why,
                    Line.ItemId, RarityColor(FMath::Max(1, Line.Rarity)), 5.f);
            if (Line.Kind == static_cast<uint8>(CI::LootKind::Item) && Line.Slot >= 0)
            {
                FFly Fly; Fly.Id = Line.ItemId; Fly.Start = Now() + .25 + Index * .08; Fly.From = FVector2D(-1, -1); Fly.Size = 34;
                Fly.ToSlot = FMath::Clamp(Line.Slot, 0, Line.bBelt ? 2 : 5) + (Line.bBelt ? 6 : 0); // resolved at draw time
                Fly.bLootRow = Index;
                State.Flies.Add(Fly);
            }
        }
        if (State.LootLog.Num() > 40) State.LootLog.SetNum(40);
        if (Report.Gold > 0)
        {
            FFloater Floater; Floater.Text = FString::Printf(TEXT("+%dg"), Report.Gold); Floater.Pos = State.GoldPos; Floater.Start = Now() + .3; Floater.Color = BrightGold;
            State.Floaters.Add(Floater);
        }
        const TCHAR* Sounds[] = {TEXT("S_LootPickup"), TEXT("S_LootPickup"), TEXT("S_LootPickup"), TEXT("S_TeleportArrive")};
        Play(HUD, Sounds[FMath::Clamp(Report.Rarity, 0, 3)], .65f + .15f * Report.Rarity);
        if (Report.Rarity >= 2) Play(HUD, TEXT("S_ShopBuy"), .5f);
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
    State.GoldPos = P.Origin + FVector2D(CX, CY) * P.Stretch;
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


// Soft halo: stacked translucent fills that grow outward (draw it behind the icon).
void SoftGlow(const FCireUIPainter& P, float X, float Y, float W, float H, FLinearColor Color)
{
    for (int32 Ring = 4; Ring >= 1; --Ring)
    {
        const float O = Ring * 2.5f;
        P.Rect(X - O, Y - O, W + 2 * O, H + 2 * O, FLinearColor(Color.R, Color.G, Color.B, Color.A * .16f));
    }
}
void Border(const FCireUIPainter& P, float X, float Y, float W, float H, FLinearColor Color, float Width)
{
    P.Line(X, Y, X + W, Y, Color, Width); P.Line(X, Y + H, X + W, Y + H, Color, Width);
    P.Line(X, Y, X, Y + H, Color, Width); P.Line(X + W, Y, X + W, Y + H, Color, Width);
}
void HoverFrame(const FCireUIPainter& P, float X, float Y, float S, FLinearColor Color)
{
    Border(P, X - 2, Y - 2, S + 4, S + 4, Color * FLinearColor(1, 1, 1, .35f), 1.f);
    Border(P, X, Y, S, S, Color, 2.f);
    P.Rect(X + 2, Y + 2, S - 4, S - 4, FLinearColor(1.f, .95f, .8f, .07f));
}
void FlashOver(const FCireUIPainter& P, float X, float Y, float S, float Amount)
{
    if (Amount <= 0) return;
    P.Rect(X + 2, Y + 2, S - 4, S - 4, FLinearColor(1.f, .92f, .65f, .38f * Amount * Amount));
    Border(P, X - 1, Y - 1, S + 2, S + 2, FLinearColor(1.f, .9f, .5f, Amount), 2.5f);
    P.Circle(X + S * .5f, Y + S * .5f, S * (.5f + (1.f - Amount) * .45f), FLinearColor(1.f, .85f, .45f, .7f * Amount), 1.5f);
}
// Kit button with this screen's hover/selection treatment (flat highlight + gold rule).
void ShopButton(const FCireUIPainter& P, float X, float Y, float W, float H, const FString& Label, bool bHover, bool bSelected, bool bDisabled, FLinearColor Accent, float Size)
{
    CireUIStyle::Button(P, X, Y, W, H, Label, bDisabled ? ECireButtonState::Disabled : ECireButtonState::Normal, Accent, Size);
    if (bDisabled) return;
    if (bHover || bSelected) P.Rect(X + 3, Y + 3, W - 6, H - 6, FLinearColor(1.f, .92f, .7f, bSelected ? .10f : .06f));
    if (bSelected) { P.Rect(X + 6, Y + H - 4, W - 12, 2, BrightGold); P.Line(X + 6, Y + 3, X + W - 6, Y + 3, Gold * FLinearColor(1, 1, 1, .6f), 1.f); }
    else if (bHover) P.Rect(X + 10, Y + H - 4, W - 20, 1.5f, Accent);
}
// WoW-style personal loot window: what you got, why, and where it went.
void DrawLootWindow(ACireHUD& HUD, const FCireUIPainter& Base, FVector2D View, double T)
{
    State.LootWindows.RemoveAll([&](const FLootWindow& W) { return W.bClosed || T - W.Start > 10.0; });
    if (State.LootWindows.Num() == 0) return;
    const FLootWindow& Window = State.LootWindows.Last();
    const FCireLootReport& R = Window.Report;
    const float Age = static_cast<float>(T - Window.Start);
    FCireUIPainter P = Base;
    P.Alpha = FMath::Min(FMath::Clamp(Age / .2f, 0.f, 1.f), FMath::Clamp((10.f - Age) / .8f, 0.f, 1.f));
    const float W = 330, RowH = 40, X = FMath::Max(300.f, View.X * .5f - W - 30), Y = View.Y * .24f;
    const int32 Rows = FMath::Min(R.Lines.Num(), 12);
    const float H = 70 + Rows * RowH + 34;
    const FLinearColor Accent = RarityColor(FMath::Max(1, R.Rarity));
    CireUIStyle::Frame(P, X, Y, W, H, Accent, ECireFrame::Panel);
    P.Text(R.bAutoCollected ? TEXT("PERSONAL LOOT  |  AUTO-COLLECTED") : TEXT("PERSONAL LOOT"), X + 14, Y + 9, 9, Gold, ECireFont::Heading);
    const FVector2D M = CireShopUI::Pointer(HUD);
    const bool bOverClose = M.X >= X + W - 26 && M.X <= X + W - 6 && M.Y >= Y + 6 && M.Y <= Y + 24;
    P.Text(TEXT("x"), X + W - 20, Y + 6, 12, bOverClose ? Parchment : Muted, ECireFont::Bold);
    if (bOverClose && HUD.HasClick()) { HUD.TakeClick(); State.LootWindows.Last().bClosed = true; }
    P.Text(R.Source, X + 14, Y + 23, 14, Accent, ECireFont::Bold);
    P.Wrapped(R.Why, X + 14, Y + 42, W - 28, 9, Muted, 2, ECireFont::Body, 2.f);
    for (int32 Index = 0; Index < Rows; ++Index)
    {
        const FCireLootLine& Line = R.Lines[Index];
        const float RY = Y + 66 + Index * RowH;
        const float Slide = FMath::Clamp((Age - .05f * Index) / .25f, 0.f, 1.f);
        FCireUIPainter Q = P; Q.Alpha *= Slide;
        const float RX = X + 10 + (1.f - Slide) * 20.f;
        Q.Rect(RX, RY, W - 20, RowH - 4, FLinearColor(0, 0, 0, .28f));
        const bool bGold = Line.Kind == static_cast<uint8>(CI::LootKind::Gold) || Line.Kind == static_cast<uint8>(CI::LootKind::Experience);
        if (bGold)
        {
            const bool bXP = Line.Kind == static_cast<uint8>(CI::LootKind::Experience);
            Q.Disc(RX + 18, RY + 18, 12, bXP ? FLinearColor(.2f, .45f, .8f, 1) : FLinearColor(.62f, .43f, .1f, 1), 20);
            Q.Disc(RX + 18, RY + 17, 9.5f, bXP ? FLinearColor(.45f, .75f, 1.f, 1) : FLinearColor(1.f, .8f, .28f, 1), 20);
            Q.Text(Line.Text, RX + 40, RY + 4, 12, bXP ? FLinearColor(.6f, .82f, 1.f, 1) : BrightGold, ECireFont::Bold);
            Q.Text(bXP ? TEXT("Experience: your own roll") : TEXT("Gold: your own roll (every eligible player gets one)"), RX + 40, RY + 20, 8, Muted, ECireFont::Body);
        }
        else
        {
            bSuppressFlash = true; CireShopUI::DrawItemIcon(Q, Line.ItemId, RX + 2, RY + 1, 34, false); bSuppressFlash = false;
            const bool bTome = Line.Kind == static_cast<uint8>(CI::LootKind::PrimaryTome);
            const FString Name = bTome ? CireItems::DisplayName(Line.ItemId) : Line.Text;
            Q.Text(Name, RX + 42, RY + 3, 12, RarityColor(FMath::Max(1, Line.Rarity)), ECireFont::Bold);
            FString Detail = bTome ? Line.Text : CireShopUI::StatLines(Line.ItemId).Replace(TEXT("\n"), TEXT("  "));
            if (!bTome && Detail.IsEmpty()) if (const FString* Use = CireItems::Get().UseText.Find(Line.ItemId)) Detail = *Use;
            if (Line.ConvertedGold > 0) Detail = FString::Printf(TEXT("No room (bags full or unique owned): +%d gold instead"), Line.ConvertedGold);
            else if (!bTome && Line.Slot >= 0) Detail += FString::Printf(TEXT("   -> %s %d"), Line.bBelt ? TEXT("belt") : TEXT("bag"), Line.Slot + 1);
            Q.Text(Detail.Left(60), RX + 42, RY + 20, 8, bTome ? FLinearColor(.55f, 1.f, .5f, 1) : Parchment, ECireFont::Body);
            State.LootRowPos[Index] = FVector2D(RX + 2, RY + 1);
        }
    }
    const FString Foot = FString::Printf(TEXT("Only you can see and open your chest.  Loot log: %s"), *KeyLabel(HUD, TEXT("ToggleLootLog")));
    P.Text(Foot, X + 14, Y + H - 22, 8, Muted, ECireFont::Body);
    if (State.LootWindows.Num() > 1) P.Text(FString::Printf(TEXT("+%d more"), State.LootWindows.Num() - 1), X + W - 60, Y + H - 22, 8, Gold, ECireFont::Heading);
}

FLinearColor SkillKindColor(CI::ShopSkillKind Kind)
{
    return Kind == CI::ShopSkillKind::Ultimate ? Purple : Kind == CI::ShopSkillKind::Passive ? Parchment : BrightGold;
}

CireShopArt::EScroll ScrollOf(CI::ShopSkillKind Kind)
{
    return Kind == CI::ShopSkillKind::Ultimate ? CireShopArt::EScroll::Prismatic : Kind == CI::ShopSkillKind::Passive ? CireShopArt::EScroll::Plain : CireShopArt::EScroll::Golden;
}

void DrawSkillIcon(const FCireUIPainter& P, const FString& Id, float X, float Y, float S, bool bHover, bool bDim, int32 Level, const FString& Key = FString())
{
    FCireIconSlot Slot;
    Slot.bEmpty = Id.IsEmpty();
    Slot.IconId = Id;
    Slot.IconTexture = CireUIStyle::FindAbilityIcon(Id);
    const CI::ShopSkillKind Kind = CireSkillShop::KindOf(Id);
    Slot.Tint = Kind == CI::ShopSkillKind::Ultimate ? Purple : Kind == CI::ShopSkillKind::Passive ? Silver : Gold;
    Slot.Kind = Kind == CI::ShopSkillKind::Ultimate ? ECireSlotKind::Ultimate : Kind == CI::ShopSkillKind::Passive ? ECireSlotKind::Passive : ECireSlotKind::Normal;
    Slot.KeyLabel = Key;
    CireUIStyle::IconSlot(P, X, Y, S, Slot, Now());
    if (bDim) P.Rect(X + 2, Y + 2, S - 4, S - 4, FLinearColor(0, 0, 0, .55f));
    if (bHover) HoverFrame(P, X, Y, S, FLinearColor(1.f, .88f, .5f, 1));
    if (!bSuppressFlash && !Id.IsEmpty()) FlashOver(P, X, Y, S, FlashAmount(FName(*Id)));
    if (Level > 0)
    {
        const FString Tag = FString::Printf(TEXT("%d"), Level);
        const float TW = FMath::Max(12.f, P.TextWidth(Tag, S * .26f, ECireFont::Numbers) + 6);
        P.Rect(X + 1, Y + S - S * .32f, TW, S * .32f, FLinearColor(.05f, .04f, .02f, .92f));
        P.Line(X + 1, Y + S - S * .32f, X + 1 + TW, Y + S - S * .32f, Gold, 1.f);
        P.Text(Tag, X + 4, Y + S - S * .32f, S * .26f, BrightGold, ECireFont::Numbers, false, false);
    }
}

// Icon in a small gold crest ring (the scroll cards' medallion).
void SkillMedallion(const FCireUIPainter& P, const FString& Id, float CX, float CY, float R, int32 Level, float Time)
{
    const CI::ShopSkillKind Kind = CireSkillShop::KindOf(Id);
    CireShopArt::CrestRing(P, CX, CY, R, Kind == CI::ShopSkillKind::Ultimate ? Purple : Kind == CI::ShopSkillKind::Active ? Orange : FLinearColor(0, 0, 0, 0), Time);
    const float S = R * 1.44f;
    if (UTexture2D* Icon = CireUIStyle::FindAbilityIcon(Id)) P.Tex(Icon, CX - S * .5f, CY - S * .5f, S, S, FLinearColor::White);
    else CireShopArt::Diamond(P, CX, CY, R * .5f, SkillKindColor(Kind));
    if (Level > 0)
    {
        const FString Tag = FString::FromInt(Level);
        const float BR = FMath::Max(6.f, R * .42f);
        const float BX = CX + R * .78f, BY = CY + R * .72f;
        P.Disc(BX, BY, BR, FLinearColor(.07f, .05f, .03f, 1), 16);
        P.Circle(BX, BY, BR, CireShopArt::Filigree, 1.f, 16);
        const float TS = BR * 1.2f;
        P.Text(Tag, BX - P.TextWidth(Tag, TS, ECireFont::Numbers) * .5f, BY - TS * .62f, TS, BrightGold, ECireFont::Numbers, false, false);
    }
}

// A small rolled scroll with the skill medallion: what flies from the card to the skill bar.
void DrawMiniScroll(const FCireUIPainter& P, const FString& Id, float X, float Y, float Size)
{
    const CireShopArt::EScroll Tier = ScrollOf(CireSkillShop::KindOf(Id));
    const float H = CireShopArt::NaturalHeight(Tier, Size) * 1.15f;
    const float Time = static_cast<float>(FMath::Fmod(Now(), 10000.0));
    const CireShopArt::FRectF Pr = CireShopArt::Scroll(P, Tier, X, Y + (Size - H) * .5f, Size, H, Time, GetTypeHash(Id), 0.f, 1.f);
    SkillMedallion(P, Id, Pr.X + Pr.W * .5f, Pr.Y + Pr.H * .5f, FMath::Max(6.f, Size * .2f), 0, Time);
}

// Word-wrapped text centred in a column; returns the lines drawn.
int32 CentredWrap(const FCireUIPainter& P, const FString& Text, float CX, float Y, float Width, float Size, FLinearColor Color, ECireFont Font, int32 MaxLines, float Gap = 1.5f)
{
    TArray<FString> Words, Lines;
    Text.ParseIntoArray(Words, TEXT(" "));
    FString Row;
    for (const FString& Word : Words)
    {
        const FString Next = Row.IsEmpty() ? Word : Row + TEXT(" ") + Word;
        if (!Row.IsEmpty() && P.TextWidth(Next, Size, Font) > Width) { Lines.Add(Row); Row = Word; }
        else Row = Next;
    }
    if (!Row.IsEmpty()) Lines.Add(Row);
    if (Lines.Num() > MaxLines) { Lines.SetNum(MaxLines); Lines.Last() = P.Fit(Lines.Last() + TEXT(" ..."), Size, Width, Font); }
    for (int32 I = 0; I < Lines.Num(); ++I)
    {
        const FString Line = P.Fit(Lines[I], Size, Width, Font);
        P.Text(Line, CX - P.TextWidth(Line, Size, Font) * .5f, Y + I * (Size + Gap), Size, Color, Font, false, false);
    }
    return Lines.Num();
}

FString FirstSentence(const FString& Text)
{
    int32 At = INDEX_NONE;
    if (Text.FindChar(TEXT('.'), At) && At > 0) return Text.Left(At + 1);
    return Text;
}

// Why a scroll cannot be bought/levelled now (empty when it can), plus a short ribbon label.
FString SkillBlocker(const ACireHero* Hero, const FString& Id, bool bOwned, int32 Price, FString& Short)
{
    FString Why;
    if (!CireSkillShop::IsOpen(Hero, &Why)) { Short = TEXT("BETWEEN WAVES"); return Why; }
    if (bOwned)
    {
        if (Hero->Gold < Price) { Short = FString::Printf(TEXT("NEED %dg"), Price - Hero->Gold); return FString::Printf(TEXT("Not enough gold: %d more needed."), Price - Hero->Gold); }
        return FString();
    }
    Why = CireSkillShop::BuyBlocker(Hero, Id);
    if (Why.IsEmpty()) return Why;
    if (Why.StartsWith(TEXT("Not enough gold"))) Short = FString::Printf(TEXT("NEED %dg"), Price - Hero->Gold);
    else if (Why.Contains(TEXT("opens at wave")))
    {
        const int32 Next = CI::NextSlotWave(CireSkillShop::Get().Rules, CireSkillShop::KindOf(Id), CireSkillShop::CurrentWave(Hero->GetWorld()));
        Short = FString::Printf(TEXT("SLOT AT WAVE %d"), Next);
    }
    else if (Why.Contains(TEXT("slots are filled"))) Short = TEXT("SLOTS FULL");
    else if (Why.Contains(TEXT("book is full"))) Short = TEXT("BOOK FULL");
    else Short = TEXT("UNAVAILABLE");
    return Why;
}

struct FSkillGrid { int32 Cols = 1; float CardW = 0, CardH = 0, GapX = 10, GapY = 10; };

// Picks the column count that gives the most readable scroll cards in the area.
FSkillGrid SolveGrid(int32 Count, float AW, float AH, CireShopArt::EScroll Tier)
{
    FSkillGrid Best;
    float BestScore = -1;
    const float Aspect = CireShopArt::NaturalHeight(Tier, 100.f) / 100.f;
    for (int32 Cols = 1; Cols <= 8; ++Cols)
    {
        const int32 Rows = FMath::DivideAndRoundUp(FMath::Max(1, Count), Cols);
        const float GapX = 8, GapY = 8;
        const float W = (AW - GapX * (Cols - 1)) / Cols, H = (AH - GapY * (Rows - 1)) / Rows;
        if (W <= 20 || H <= 20) continue;
        const float CardW = FMath::Min3(W, H / (Aspect * .95f), 176.f);
        const float CardH = FMath::Min(H, CardW * Aspect * 1.3f); // keep the scroll close to its painted shape
        const float ParchH = CardH - CardW * Aspect * .42f;
        const float Score = FMath::Min(CardW, ParchH * 1.35f);
        if (Score > BestScore) { BestScore = Score; Best.Cols = Cols; Best.CardW = CardW; Best.CardH = CardH; Best.GapX = GapX; Best.GapY = GapY; }
    }
    return Best;
}

// The Skill Shop screen (Eric's target image): three columns of scroll cards, one per skill tier.
void DrawSkillScreen(ACireHUD& HUD, ACireHero* Hero, ACireController* Controller, ACireGameState* GameState, const FCireUIPainter& P, FVector2D View,
    FVector2D M, bool bInteractive, bool bRightClick)
{
    using namespace CireShopArt;
    const double T = Now();
    const float Time = static_cast<float>(FMath::Fmod(T, 10000.0));
    const float Dt = FMath::Clamp(static_cast<float>(T - State.LastSkillFrame), 0.f, .1f);
    State.LastSkillFrame = T;
    const auto& R = CireSkillShop::Get().Rules;
    const int32 Wave = CireSkillShop::CurrentWave(HUD.GetWorld());
    const float W = FMath::Min(View.X - 20.f, 1256.f), H = FMath::Min(View.Y - 16.f, 704.f);
    const float X = FMath::RoundToFloat((View.X - W) * .5f), Y = FMath::RoundToFloat((View.Y - H) * .5f);
    auto Click = [&](float BX, float BY, float BW, float BH) { if (bInteractive && HUD.HasClick() && In(M, BX, BY, BW, BH)) { HUD.TakeClick(); return true; } return false; };
    P.Rect(0, 0, View.X, View.Y, FLinearColor(0, 0, 0, .35f));
    Panel(P, X, Y, W, H, 330);
    CireShopArt::Title(P, X + W * .5f, Y + 13, TEXT("SKILLS"), TEXT("POWER LIVES WITHIN"), 31);

    // Corners: champion + shop status (left), gold (right), in the target's small spaced caps.
    FString Why;
    const bool bOpen = CireSkillShop::IsOpen(Hero, &Why);
    FString Status;
    if (!bOpen) Status = TEXT("CLOSED  ·  OPENS BETWEEN WAVES");
    else if (CireSkillShop::IsBreather(HUD.GetWorld())) Status = FString::Printf(TEXT("BREATHER  ·  NEXT WAVE IN %.0fs"), GameState ? GameState->NextWaveSeconds : 0.f);
    else Status = GameState && GameState->Phase == 1 ? TEXT("PREP  ·  THE SHOP IS OPEN") : TEXT("RECOVERY  ·  THE SHOP IS OPEN");
    CompassStar(P, X + 40, Y + 46, 15, Filigree * FLinearColor(1, 1, 1, .8f));
    Spaced(P, TEXT("YOUR CHAMPION"), X + 66, Y + 28, 7.f, .38f, Filigree * .85f, ECireFont::Display, false, false);
    P.Text(P.Fit(Hero->HeroName, 12, 230, ECireFont::Bold), X + 66, Y + 39, 12, Parchment, ECireFont::Bold);
    Spaced(P, Status, X + 66, Y + 57, 7.f, .3f, bOpen ? Teal : FLinearColor(1.f, .45f, .4f, 1), ECireFont::Display, false, false);
    CompassStar(P, X + W - 40, Y + 46, 15, Filigree * FLinearColor(1, 1, 1, .8f));
    {
        const float RX = X + W - 66;
        const float GW = SpacedWidth(P, TEXT("YOUR GOLD"), 7.f, .38f);
        Spaced(P, TEXT("YOUR GOLD"), RX - GW, Y + 28, 7.f, .38f, Filigree * .85f, ECireFont::Display, false, false);
        State.GoldPos = FVector2D(RX - 90, Y + 38);
        DrawGoldCounter(P, Hero, RX, Y + 37, 15, true);
        const FString Mob = FString::Printf(TEXT("WAVE %d  ·  A MOB IS WORTH %dg"), FMath::Max(1, Wave), CireLoot::MobValueNow(HUD.GetWorld()));
        const float MW = SpacedWidth(P, Mob, 7.f, .3f);
        Spaced(P, Mob, RX - MW, Y + 57, 7.f, .3f, Muted * 1.25f, ECireFont::Display, false, false);
    }
    // Close.
    {
        const float CX = X + W - 30, CY = Y + 22;
        const bool bOver = In(M, CX - 11, CY - 11, 22, 22);
        const FLinearColor C = bOver ? BrightGold : Filigree * .8f;
        P.Line(CX - 6, CY - 6, CX + 6, CY + 6, C, 1.6f); P.Line(CX - 6, CY + 6, CX + 6, CY - 6, C, 1.6f);
        if (Click(CX - 11, CY - 11, 22, 22) && Controller) Controller->bShop = false;
    }

    // Columns: Active (golden) | Passive (plain) | Ultimate (prismatic).
    const TArray<FCireShopSkill> Catalog = CireSkillShop::CatalogFor(Hero);
    const CI::ShopSkillKind Kinds[] = {CI::ShopSkillKind::Active, CI::ShopSkillKind::Passive, CI::ShopSkillKind::Ultimate};
    const TCHAR* Names[] = {TEXT("ACTIVE SKILLS"), TEXT("PASSIVE SKILLS"), TEXT("ULTIMATE SKILLS")};
    const TCHAR* Keywords[] = {TEXT("STRIKE  ·  CAST  ·  UNLEASH"), TEXT("ENDURE  ·  ADAPT  ·  PERSEVERE"), TEXT("TRANSCEND  ·  DOMINATE  ·  ASCEND")};
    const TCHAR* Taglines[] = {TEXT("TAKE ACTION. MAKE AN IMPACT."), TEXT("QUIET STRENGTH, LASTING POWER."), TEXT("UNLEASH THE EXTRAORDINARY.")};
    const float Shares[] = {.54f, .23f, .23f};
    const float BandH = 60, FootH = 98;
    const float Left = X + 20, Inner = W - 40;
    const float ColTop = Y + 80, ColBottom = Y + H - BandH - FootH;
    float ColX = Left;
    struct FCard { FString Id; float X, Y, W, H; EScroll Tier; int32 Column; };
    TArray<FCard> Cards;
    for (int32 K = 0; K < 3; ++K)
    {
        const float ColW = Inner * Shares[K];
        if (K > 0) Divider(P, ColX - 1, ColTop - 6, Y + H - BandH - 8, Filigree * FLinearColor(1, 1, 1, .55f));
        TArray<const FCireShopSkill*> Skills;
        for (const FCireShopSkill& Skill : Catalog) if (Skill.Kind == Kinds[K]) Skills.Add(&Skill);
        // Slot status above the cards.
        const int32 Owned = CireSkillShop::OwnedOfKind(Hero, Kinds[K]);
        const int32 Open = CI::SlotsAvailable(R, Kinds[K], Wave);
        const int32 Max = K == 0 ? R.MaxActive : K == 1 ? R.MaxPassive : R.MaxUltimate;
        const int32 Next = CI::NextSlotWave(R, Kinds[K], Wave);
        FString Slots = FString::Printf(TEXT("%d / %d LEARNED"), Owned, Open);
        if (Open < Max && Next > 0) Slots += FString::Printf(TEXT("  ·  NEXT SLOT AT WAVE %d"), Next);
        else if (Open >= Max) Slots += TEXT("  ·  ALL SLOTS OPEN");
        Spaced(P, Slots, ColX + ColW * .5f, ColTop - 2, 7.f, .28f, Owned < Open ? Teal * 1.1f : Filigree * .8f, ECireFont::Display, true, false);
        const float AreaY = ColTop + 14, AreaH = ColBottom - AreaY - 6, AreaX = ColX + 8, AreaW = ColW - 16;
        const EScroll Tier = ScrollOf(Kinds[K]);
        const FSkillGrid G = SolveGrid(Skills.Num(), AreaW, AreaH, Tier);
        const int32 Rows = FMath::DivideAndRoundUp(FMath::Max(1, Skills.Num()), G.Cols);
        const float GridW = G.Cols * G.CardW + (G.Cols - 1) * G.GapX, GridH = Rows * G.CardH + (Rows - 1) * G.GapY;
        const float GX = AreaX + (AreaW - GridW) * .5f, GY = AreaY + FMath::Max(0.f, (AreaH - GridH) * .5f);
        for (int32 Index = 0; Index < Skills.Num(); ++Index)
        {
            const int32 Row = Index / G.Cols, Col = Index % G.Cols;
            const int32 InRow = FMath::Min(G.Cols, Skills.Num() - Row * G.Cols);
            const float RowOffset = (G.Cols - InRow) * (G.CardW + G.GapX) * .5f; // centre a short last row
            Cards.Add({Skills[Index]->Id, GX + RowOffset + Col * (G.CardW + G.GapX), GY + Row * (G.CardH + G.GapY), G.CardW, G.CardH, Tier, K});
        }
        if (Skills.Num() == 0) Spaced(P, TEXT("NONE FOR YOUR CLASS"), ColX + ColW * .5f, AreaY + AreaH * .45f, 8.f, .3f, Muted, ECireFont::Display, true, false);
        // Category footer: crest, name, keywords, tagline.
        const float CX = ColX + ColW * .5f, FY = ColBottom + 4;
        Crest(P, Tier, CX, FY + 22, 21, FMath::Min(110.f, ColW * .28f));
        Spaced(P, Names[K], CX, FY + 48, 15.5f, .12f, FLinearColor(.95f, .83f, .58f, 1), ECireFont::Display, true, true);
        Spaced(P, Keywords[K], CX, FY + 70, 7.5f, .42f, Parchment * .9f, ECireFont::Display, true, false);
        Spaced(P, Taglines[K], CX, FY + 84, 6.5f, .42f, Filigree * .75f, ECireFont::Display, true, false);
        ColX += ColW;
    }

    // Hover lift (eased), hovered card drawn last so it rises above its neighbours.
    FString HoverId;
    for (const FCard& Sc : Cards) if (bInteractive && In(M, Sc.X, Sc.Y, Sc.W, Sc.H)) HoverId = Sc.Id;
    for (const FCard& Sc : Cards)
    {
        float& Lift = State.SkillLift.FindOrAdd(Sc.Id);
        Lift = FMath::FInterpTo(Lift, Sc.Id == HoverId ? 1.f : 0.f, Dt > 0 ? Dt : .016f, 14.f);
    }
    Cards.StableSort([&](const FCard& A, const FCard& B) { return State.SkillLift.FindRef(A.Id) < State.SkillLift.FindRef(B.Id); });
    const double StampAge = T - State.StampStart;
    for (const FCard& Sc : Cards)
    {
        const FString& Id = Sc.Id;
        const float Lift = State.SkillLift.FindRef(Id);
        const int32 Level = CireSkillShop::Level(Hero, Id);
        const bool bOwned = Level > 0;
        const int32 Price = bOwned ? CireSkillShop::LevelPrice(Hero, Id) : CireSkillShop::BuyPrice(Hero, Id);
        FString Short;
        const FString Blocker = SkillBlocker(Hero, Id, bOwned, Price, Short);
        const bool bBlocked = !Blocker.IsEmpty();
        const float Grow = 1.f + .1f * Lift;
        const float CW = Sc.W * Grow, CH = Sc.H * Grow;
        const float CX = Sc.X - (CW - Sc.W) * .5f + ShakeOffset(FName(*Id)), CY = Sc.Y - (CH - Sc.H) * .5f - 7.f * Lift;
        const uint32 Seed = GetTypeHash(Id);
        const FRectF Pr = CireShopArt::Scroll(P, Sc.Tier, CX, CY, CW, CH, Time, Seed, bBlocked ? 1.f : 0.f, Lift);
        State.SkillGridPos.Add(Id, FVector2D(CX + CW * .5f - 22, CY + CH * .35f - 22));
        const float Mid = Pr.X + Pr.W * .5f;
        // Ink scales with the card; passives/ultimates are taller and show more of the description.
        const float K = FMath::Clamp(Pr.W / 62.f, .85f, 1.5f);
        const float Ring = FMath::Clamp(Pr.W * .24f, 11.f, 20.f);
        float TY = Pr.Y + 2;
        SkillMedallion(P, Id, Mid, TY + Ring, Ring, Level, Time);
        TY += Ring * 2 + 4;
        const FLinearColor InkC = CireShopArt::Ink;
        TY += CentredWrap(P, ACireHero::SkillName(Id), Mid, TY, Pr.W + 6, 8.5f * K, InkC, ECireFont::Bold, 2, 0.f) * (8.5f * K + .5f) + 1;
        // School and types from the Ability Database ("FIRE  ·  DPS / HEAL").
        {
            const FString School = CireSkillShop::SchoolOf(Id).ToUpper(), Types = CireSkillShop::RoleTags(Id);
            const FString Tags = School.IsEmpty() ? Types : Types.IsEmpty() ? School : School + TEXT("  ·  ") + Types;
            if (!Tags.IsEmpty())
            {
                const float TS = 6.f * K;
                const FString Fit = P.Fit(Tags, TS, Pr.W + 8, ECireFont::Heading);
                P.Text(Fit, Mid - P.TextWidth(Fit, TS, ECireFont::Heading) * .5f, TY, TS, InkSoft, ECireFont::Heading, false, false);
                TY += TS + 2.5f;
            }
        }
        const FString LevelLine = bOwned ? FString::Printf(TEXT("LV %d » %d"), Level, Level + 1) : TEXT("NEW  ·  LV 1");
        P.Text(LevelLine, Mid - P.TextWidth(LevelLine, 7.f * K, ECireFont::Heading) * .5f, TY, 7.f * K, InkRed, ECireFont::Heading, false, false);
        TY += 7.f * K + 3;
        const float Room = Pr.Y + Pr.H - TY;
        if (bOwned)
        {
            const int32 Nx = Level + 1;
            const FString Effect = FString::Printf(TEXT("+%.0f%% effect"), (CI::SkillEffectScale(R, Nx) - 1) * 100);
            const FString Cost = FString::Printf(TEXT("cd -%.0f%%  ·  cost +%.0f%%"), (1 - CI::SkillCooldownScale(R, Nx)) * 100, (CI::SkillCostScale(R, Nx) - 1) * 100);
            P.Text(Effect, Mid - P.TextWidth(Effect, 7.2f * K, ECireFont::Bold) * .5f, TY, 7.2f * K, FLinearColor(.12f, .32f, .1f, 1), ECireFont::Bold, false, false);
            TY += 7.2f * K + 1.5f;
            if (Room > 16 * K) P.Text(P.Fit(Cost, 6.6f * K, Pr.W + 4, ECireFont::Body), Mid - FMath::Min(Pr.W + 4, P.TextWidth(Cost, 6.6f * K, ECireFont::Body)) * .5f, TY, 6.6f * K, InkSoft, ECireFont::Body, false, false);
        }
        else if (Room > 9 * K)
        {
            const int32 Lines = FMath::Clamp(FMath::FloorToInt(Room / (6.8f * K + 1.5f)), 1, 5);
            CentredWrap(P, FirstSentence(ACireHero::SkillDescription(Id)), Mid, TY, Pr.W + 4, 6.8f * K, InkSoft, ECireFont::Body, Lines, 1.5f);
        }
        // Price on the lower roll; the reason ribbon when it cannot be bought.
        const float RollH = CH - (Pr.Y - CY) - Pr.H;
        const float PY = Pr.Y + Pr.H + RollH * .5f;
        const FString PriceText = FString::Printf(TEXT("%s  %dg"), bOwned ? TEXT("LEVEL UP") : TEXT("LEARN"), Price);
        const float PS = FMath::Clamp(CW * .082f, 7.5f, 10.5f);
        const float PW = P.TextWidth(PriceText, PS, ECireFont::Numbers);
        P.Rect(CX + CW * .5f - PW * .5f - 7, PY - PS * .72f, PW + 14, PS * 1.44f, FLinearColor(.06f, .04f, .02f, bBlocked ? .55f : .78f));
        P.Line(CX + CW * .5f - PW * .5f - 7, PY - PS * .72f, CX + CW * .5f + PW * .5f + 7, PY - PS * .72f, Filigree * .8f, .8f);
        P.Line(CX + CW * .5f - PW * .5f - 7, PY + PS * .72f, CX + CW * .5f + PW * .5f + 7, PY + PS * .72f, Filigree * .8f, .8f);
        P.Text(PriceText, CX + CW * .5f - PW * .5f, PY - PS * .62f, PS, bBlocked ? FLinearColor(1.f, .5f, .42f, 1) : BrightGold, ECireFont::Numbers, false, false);
        if (bBlocked && !Short.IsEmpty())
        {
            const float SS = FMath::Clamp(CW * .06f, 6.2f, 8.f);
            const float SW = SpacedWidth(P, Short, SS, .2f, ECireFont::Heading) + 12;
            const float RY = Pr.Y + Pr.H - SS * 1.9f;
            P.Rect(Mid - SW * .5f, RY, SW, SS * 1.7f, FLinearColor(.36f, .05f, .04f, .92f));
            Diamond(P, Mid - SW * .5f, RY + SS * .85f, SS * .85f, FLinearColor(.36f, .05f, .04f, .92f));
            Diamond(P, Mid + SW * .5f, RY + SS * .85f, SS * .85f, FLinearColor(.36f, .05f, .04f, .92f));
            Spaced(P, Short, Mid, RY + SS * .3f, SS, .2f, FLinearColor(1.f, .86f, .78f, 1), ECireFont::Heading, true, false);
        }
        if (ShakeOffset(FName(*Id)) != 0.f) Glow(P, CX - CW * .1f, CY - CH * .05f, CW * 1.2f, CH * 1.1f, FLinearColor(.6f, .05f, .03f, 1));
        FlashOver(P, Pr.X, Pr.Y, FMath::Min(Pr.W, Pr.H), FlashAmount(FName(*Id)) * .6f);
        // Seal stamp on purchase / level-up.
        if (State.StampId == Id && StampAge >= 0 && StampAge < 1.5)
            WaxSeal(P, Mid, Pr.Y + Pr.H * .5f, FMath::Clamp(Pr.W * .3f, 12.f, 26.f), static_cast<float>(StampAge / 1.5), FString::FromInt(FMath::Max(1, Level)));
        if (Id == HoverId)
        {
            const float Nx = bOwned ? Level + 1 : 1;
            FString Body = ACireHero::SkillDescription(Id);
            Body += bOwned ? FString::Printf(TEXT("\n\nLevel %d -> %d: effect +%.0f%%, cooldown -%.0f%%, mana/energy cost +%.0f%% (totals). No level cap."), Level, Level + 1,
                (CI::SkillEffectScale(R, Nx) - 1) * 100, (1 - CI::SkillCooldownScale(R, Nx)) * 100, (CI::SkillCostScale(R, Nx) - 1) * 100)
                : FString::Printf(TEXT("\n\n%s skill  |  %s"), *FString(Sc.Tier == EScroll::Golden ? TEXT("Active") : Sc.Tier == EScroll::Plain ? TEXT("Passive") : TEXT("Ultimate")), *CireSkillShop::RoleTags(Id));
            Body += FString::Printf(TEXT("\n%s: %dg"), bOwned ? TEXT("Level up") : TEXT("Learn"), Price);
            if (bBlocked) Body += TEXT("\n") + Blocker;
            CireShopUI::Tip(HUD, ACireHero::SkillName(Id), Body);
            if (Click(Sc.X, Sc.Y, Sc.W, Sc.H) || bRightClick)
            {
                if (bBlocked) ShowError(HUD, FName(*Id), -1, false, Blocker);
                else if (bOwned) Hero->Inventory->ServerLevelSkill(Id);
                else Hero->Inventory->ServerBuySkill(Id);
                State.SelectedSkill = Id;
            }
        }
    }

    // Bottom band: your skill bar (where bought scrolls fly), controls, tabs, motto.
    const float BandY = Y + H - BandH;
    Rule(P, X + 190, X + W - 190, BandY + 2, Filigree * FLinearColor(1, 1, 1, .7f), true);
    {
        const int32 SlotsShown = Cires::MaxSkills;
        const float S = 30, Step = 36;
        const float BX = X + W * .5f - (SlotsShown * Step - (Step - S)) * .5f, BY = BandY + 11;
        for (int32 Index = 0; Index < SlotsShown && Index < 8; ++Index)
        {
            const FString Id = Hero->Skills.IsValidIndex(Index) ? Hero->Skills[Index] : FString();
            const float IX = BX + Index * Step;
            const bool bOver = bInteractive && !Id.IsEmpty() && In(M, IX, BY, S, S);
            DrawSkillIcon(P, Id, IX, BY, S, bOver, false, Id.IsEmpty() ? 0 : CireSkillShop::Level(Hero, Id));
            State.SkillSlotPos[Index] = FVector2D(IX, BY);
            if (bOver) CireShopUI::Tip(HUD, ACireHero::SkillName(Id), FString::Printf(TEXT("Level %d. %s"), CireSkillShop::Level(Hero, Id), *ACireHero::SkillDescription(Id)));
        }
        Spaced(P, TEXT("CHAMPIONS ARE BUILT, NOT BORN."), X + W * .5f, BandY + 41, 6.5f, .45f, Filigree * .7f, ECireFont::Display, true, false);
    }
    const FString Keys[] = {TEXT("CLICK A SCROLL  ·  LEARN OR LEVEL UP"), TEXT("HOVER  ·  FULL DETAILS"), FString::Printf(TEXT("%s  ·  CLOSE"), *KeyLabel(HUD, TEXT("ToggleSkillShop")))};
    for (int32 Index = 0; Index < 3; ++Index) Spaced(P, Keys[Index], X + 26, BandY + 12 + Index * 12, 6.5f, .3f, Muted * 1.2f, ECireFont::Display, false, false);
    // READY: the breather's ready-up (wave director), same state as the match plate's button.
    if (GameState && Controller && CireSkillShop::IsBreather(HUD.GetWorld()) && GameState->BreatherPlayers > 0)
    {
        const bool bReady = HUD.IsBreatherReadyLocal(GameState->Wave);
        const float RX = X + W - 26 - 2 * 104 - 132, RY = BandY + 14;
        const bool bOver = In(M, RX, RY, 124, 24);
        ShopButton(P, RX, RY, 124, 24, FString::Printf(TEXT("%s  %d/%d"), bReady ? TEXT("READY") : TEXT("READY UP"), GameState->BreatherReady, GameState->BreatherPlayers),
            bOver, bReady, false, Teal, 9);
        if (bOver) CireShopUI::Tip(HUD, TEXT("Ready up"), TEXT("When every player is ready, the next wave starts in 1 second."));
        if (Click(RX, RY, 124, 24)) { HUD.SetBreatherReadyLocal(GameState->Wave, !bReady); Controller->ServerAction(10, bReady ? 0 : 1, nullptr); Play(HUD, TEXT("S_ShopTab"), .4f); }
    }
    const TCHAR* Tabs[] = {TEXT("ITEMS"), TEXT("SKILLS")};
    const FName TabKeys[] = {TEXT("ToggleShop"), TEXT("ToggleSkillShop")};
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const float TX = X + W - 26 - 2 * 104 + Index * 104, TYb = BandY + 14;
        const bool bOver = In(M, TX, TYb, 98, 24);
        ShopButton(P, TX, TYb, 98, 24, FString::Printf(TEXT("%s  [%s]"), Tabs[Index], *KeyLabel(HUD, TabKeys[Index])), bOver, State.Tab == Index, false, Gold, 9);
        if (Click(TX, TYb, 98, 24)) { State.Tab = Index; Play(HUD, TEXT("S_ShopTab"), .4f); }
    }
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
    CireUIStyle::IconSlot(P, X, Y, Size, Slot, Now());
    if (bHover) HoverFrame(P, X, Y, Size, FLinearColor(1.f, .88f, .5f, 1));
    if (!bSuppressFlash) FlashOver(P, X, Y, Size, FlashAmount(ItemId));
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
    ProcessLoot(HUD, Hero);
    ProcessGold(HUD, Hero);
    // Skill Shop mode: the shop opens after every cleared wave (the breather) and when prep
    // begins (the cycle's last clear), on the skills tab, when something is buyable.
    if (GameState && Controller)
    {
        const bool bCleared = State.LastClearedSeen >= 0 && GameState->CycleWavesDone > State.LastClearedSeen && GameState->Phase == 0 &&
            GameState->CycleWavesDone < GameState->WavesPerCycle;
        const bool bPrepBegan = State.LastPhaseSeen >= 0 && State.LastPhaseSeen != 1 && GameState->Phase == 1;
        if ((bCleared || bPrepBegan) && Hero->bDrafted && CireSkillShop::IsSkillShopMode(HUD.GetWorld()))
        {
            AddToast(TEXT("SKILL SHOP OPEN"), FString::Printf(TEXT("%s: learn or level skills (%s)."), bPrepBegan ? TEXT("Prep") : TEXT("Wave cleared"),
                *KeyLabel(HUD, TEXT("ToggleSkillShop"))), TEXT("challenge"), BrightGold, 6.f);
            if (CireSkillShop::Get().bAutoOpen && AnySkillAffordable(Hero))
            {
                if (!Controller->bShop) { Controller->bShop = true; State.PendingTab = 1; }
                else State.Tab = 1;
                UE_LOG(LogTemp, Display, TEXT("CIRE_SKILLSHOP_AUTO_OPEN reason=%s wave=%d"), bPrepBegan ? TEXT("prep") : TEXT("wave_clear"), GameState->Wave);
            }
        }
        State.LastClearedSeen = GameState->Phase == 0 ? GameState->CycleWavesDone : 0;
        State.LastPhaseSeen = GameState->Phase;
    }
    // Tell the server when the shop window opens/closes: a visit bounds the undo history.
    const bool bShopOpen = Controller && Controller->bShop;
    if (bShopOpen != State.bWasShopOpen)
    {
        Hero->Inventory->ServerShopOpen(bShopOpen); State.bWasShopOpen = bShopOpen;
        if (bShopOpen) { Play(HUD, TEXT("S_ShopOpen"), .6f); State.Tab = State.PendingTab >= 0 ? State.PendingTab : 0; }
        State.PendingTab = -1;
    }

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
        Slot.Charges = bBelt ? Cell.Charges : 0;
        CireUIStyle::IconSlot(P, X + Shake, Y, S, Slot, Now());
        if (bOver) HoverFrame(P, X + Shake, Y, S, FLinearColor(1.f, .88f, .5f, 1));
        FlashOver(P, X + Shake, Y, S, FlashAmount(NAME_None, Index, bBelt));
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
            Border(P, X - 2, Y - 2, S + 4, S + 4, FLinearColor(.45f, .92f, 1.f, .6f + .4f * FMath::Sin(Now() * 8.f)), 2.f);
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
        if (bOver) HoverFrame(P, X, Y, S, FLinearColor(.55f, .92f, 1.f, 1));
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
            if (It->bOpened || (It->OwnerHero ? It->OwnerHero != Hero : It->TeamId != Hero->TeamId)) continue;
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
            const FString Hint = It->OwnerHero ? FString::Printf(TEXT("YOUR personal loot  |  walk over to open  %.0fm"), Distance / 100.f)
                : FString::Printf(TEXT("Tier %d  |  walk over to loot  %.0fm"), It->Tier, Distance / 100.f);
            W.Text(Hint, L.X - W.TextWidth(Hint, 8, ECireFont::Body) * .5f, L.Y + 15, 8, Parchment, ECireFont::Body, true);
        }
    }
    if (HUD.UISettings.bShowStats) DrawStatsWindow(HUD, Hero);
    if (HUD.UISettings.bShowLootLog) DrawLootLog(HUD, Hero);
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
    if (State.Tab == 1) { DrawSkillScreen(HUD, Hero, Controller, GameState, P, View, M, bInteractive, bRightClick); return; }
    auto Click = [&](float BX, float BY, float BW, float BH) { if (bInteractive && HUD.HasClick() && In(M, BX, BY, BW, BH)) { HUD.TakeClick(); return true; } return false; };
    CireUIStyle::Frame(P, X, Y, W, H, Gold, ECireFrame::Panel);

    // Title bar: name, access status, tabs, gold, close.
    P.Text(State.Tab == 1 ? TEXT("SKILL SHOP") : TEXT("THE QUARTERMASTER"), X + 20, Y + 12, 20, Parchment, ECireFont::Heading);
    const CI::ShopAccess Access = ClientAccess(Hero, GameState);
    FString Status; FLinearColor StatusColor = Teal;
    FString SkillWhy;
    if (State.Tab == 1)
    {
        if (!CireSkillShop::IsOpen(Hero, &SkillWhy)) { Status = TEXT("CLOSED  |  OPENS BETWEEN WAVES"); StatusColor = Red; }
        else if (CireSkillShop::IsBreather(HUD.GetWorld())) Status = FString::Printf(TEXT("BREATHER  |  NEXT WAVE IN %.0fs"), GameState ? GameState->NextWaveSeconds : 0.f);
        else Status = GameState && GameState->Phase == 1 ? TEXT("PREP  |  SKILL SHOP OPEN") : TEXT("RECOVERY  |  SKILL SHOP OPEN");
    }
    else if (Access == CI::ShopAccess::Allowed && GameState && GameState->Phase == 1)
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
        ShopButton(P, X + W - 60, Y + 12, 44, 24, KeyLabel(HUD, TEXT("ToggleShop")).IsEmpty() ? TEXT("X") : KeyLabel(HUD, TEXT("ToggleShop")), bOver, false, false, Gold, 10);
        if (Click(X + W - 60, Y + 12, 44, 24) && Controller) Controller->bShop = false;
    }
    CireUIStyle::Header(P, X + 14, Y + 42, W - 28, TEXT(""), Gold, 1);
    // Tabs: ITEMS | SKILLS.
    {
        const TCHAR* Tabs[] = {TEXT("ITEMS"), TEXT("SKILLS")};
        const FName TabKeys[] = {TEXT("ToggleShop"), TEXT("ToggleSkillShop")};
        for (int32 Index = 0; Index < 2; ++Index)
        {
            const float TX = X + 560 + Index * 118;
            const bool bOver = In(M, TX, Y + 12, 112, 26);
            ShopButton(P, TX, Y + 12, 112, 26, FString::Printf(TEXT("%s  [%s]"), Tabs[Index], *KeyLabel(HUD, TabKeys[Index])), bOver, State.Tab == Index, false, Index == 1 ? Teal : Gold, 10);
            if (Click(TX, Y + 12, 112, 26)) { State.Tab = Index; Play(HUD, TEXT("S_ShopTab"), .4f); }
        }
    }

    // Left column: categories and stat filters.
    const float LX = X + 14, LY = Y + 56;
    const FString RoleKey = CireItems::RoleKey(Hero);
    const FString RoleCaption = RoleKey == TEXT("tank") ? TEXT("TANK") : RoleKey == TEXT("support") ? TEXT("SUPPORT") : RoleKey == TEXT("caster") ? TEXT("SPELL DAMAGE") : TEXT("PHYSICAL DAMAGE");
    const TCHAR* Categories[] = {TEXT("RECOMMENDED"), TEXT("ALL ITEMS")};
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const float BY = LY + Index * 30;
        const bool bOver = In(M, LX, BY, 150, 26);
        ShopButton(P, LX, BY, 150, 26, Categories[Index], bOver, State.Category == Index, false, Gold, 10);
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
        ShopButton(P, LX, BY, 150, 22, TEXT("CLEAR FILTERS"), bOver, false, false, Muted, 9);
        if (Click(LX, BY, 150, 22)) State.Filters = 0;
    }

    // Centre: item grid by tier (or the recommended build), with prices and affordability.
    const float GX = X + 176, GY = Y + 56, GW = 470;
    CireUIStyle::Frame(P, GX - 4, GY - 4, GW + 8, H - 144, Gold * .6f, ECireFrame::Inset);
    constexpr float Icon = 42, Step = 51, RowH = 60;
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
            if (State.Selected == Id) SoftGlow(P, IX - 3, IY - 3, Icon + 6, Icon + 6, FLinearColor(1.f, .85f, .4f, .55f));
            DrawItemIcon(P, Id, IX, IY, Icon, bOver, 0, 0, 0, FString(), !bAffordable);
            if (ShakeOffset(Id) != 0.f) { P.Rect(IX + 2, IY + 2, Icon - 4, Icon - 4, FLinearColor(.9f, .1f, .08f, .25f)); Border(P, IX - 1, IY - 1, Icon + 2, Icon + 2, FLinearColor(1.f, .25f, .2f, 1), 2.5f); }
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
        P.Text(FString::Printf(TEXT("RECOMMENDED BUILD  |  %s"), *RoleCaption), GX + 4, CY, 11, BrightGold, ECireFont::Heading);
        P.Text(TEXT("STARTER  ->  CORE  ->  SITUATIONAL"), GX + GW - 8 - P.TextWidth(TEXT("STARTER  ->  CORE  ->  SITUATIONAL"), 8, ECireFont::Heading), CY + 3, 8, Muted, ECireFont::Heading);
        CY += 20;
        if (const auto* Lists = D.Recommended.Find(RoleKey))
        {
            const TCHAR* Names[] = {TEXT("1  STARTER  |  buy first"), TEXT("2  CORE BUILD  |  in this order"), TEXT("3  SITUATIONAL  |  when you need it")};
            const FLinearColor Colors[] = {FLinearColor(.45f, .85f, .5f, 1), Orange, Silver};
            for (int32 Index = 0; Index < Lists->Num() && Index < 3; ++Index)
            {
                const float Top = CY + 14;
                Section(Names[Index], (*Lists)[Index], Colors[Index]);
                // Build-order arrows between consecutive items of a step.
                for (int32 Item = 0; Item + 1 < (*Lists)[Index].Num() && Item + 1 < PerRow; ++Item)
                {
                    const float AX = GX + 6 + Item * Step + Icon + 1, AY = Top + Icon * .5f;
                    P.Line(AX, AY, AX + 6, AY, Colors[Index], 1.5f);
                    P.Line(AX + 3, AY - 3, AX + 6, AY, Colors[Index], 1.5f); P.Line(AX + 3, AY + 3, AX + 6, AY, Colors[Index], 1.5f);
                }
            }
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
    const float DX = X + 660, DY = Y + 52, DWd = 336, DHt = H - 140;
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
            ShopButton(P, DX + 12 + ShakeOffset(NAME_None, SellSlot, bSellBelt), BY, DWd - 24, 34, FString::Printf(TEXT("SELL  +%dg"), Value), bOver, false, false, Orange, 13);
            if (Click(DX + 12, BY, DWd - 24, 34)) RequestSell(HUD, Hero, GameState, SellSlot, bSellBelt);
        }
        else if (Item->Purchasable)
        {
            const CI::PurchasePlan Plan = CI::PlanPurchase(D.Catalog, Rules, Item->Id, Hero->Gold);
            const bool bOver = In(M, DX + 12, BY, DWd - 24, 34);
            const FString Label = Plan.Ok ? FString::Printf(TEXT("BUY  %dg"), Plan.Cost) : FString::Printf(TEXT("BUY  %dg  |  %s"), Price, Plan.Error.find("gold") != std::string::npos ? TEXT("NEED GOLD") : TEXT("UNAVAILABLE"));
            ShopButton(P, DX + 12 + ShakeOffset(Shown), BY, DWd - 24, 34, Label, bOver, false, !Plan.Ok, BrightGold, 13);
            if (Click(DX + 12, BY, DWd - 24, 34)) RequestBuy(HUD, Hero, GameState, Shown, State.DetailIconPos);
        }
        else P.Text(TEXT("Loot only: drops from challenge chests and bosses."), DX + 12, BY + 10, 10, Muted);
    }

    // Bottom strip: bag + belt, undo, hotkeys.
    const float BX = X + 176, BYs = Y + H - 78;
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
        if (State.SelectedSlot == SlotIndex && State.bSelectedBelt == bBelt) SoftGlow(P, IX - 3, IY - 3, S + 6, S + 6, FLinearColor(1.f, .6f, .2f, .6f));
        FCireIconSlot Slot;
        Slot.bEmpty = Cell.Id.IsNone(); Slot.IconTexture = FindItemIcon(Cell.Id); Slot.IconId = Cell.Id.ToString();
        if (const CI::ItemDef* Item = CireItems::Find(Cell.Id)) Slot.Tint = TierColor(static_cast<int32>(Item->Tier));
        Slot.Charges = bBelt ? Cell.Charges : 0;
        Slot.CooldownRemaining = Remaining; Slot.CooldownFraction = Cell.Cooldown > 0 ? FMath::Clamp(Remaining / Cell.Cooldown, 0.f, 1.f) : 0.f;
        Slot.KeyLabel = bBelt ? KeyLabel(HUD, CireItems::BeltAction(SlotIndex)) : KeyLabel(HUD, CireItems::ItemAction(SlotIndex));
        CireUIStyle::IconSlot(P, IX, IY, S, Slot, Now());
        if (bOver) HoverFrame(P, IX, IY, S, FLinearColor(1.f, .88f, .5f, 1));
        FlashOver(P, IX, IY, S, FlashAmount(NAME_None, SlotIndex, bBelt));
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
        ShopButton(P, UX, UY, 118, 34, bCan ? FString::Printf(TEXT("UNDO (%d)"), Hero->Inventory->UndoDepth) : TEXT("UNDO"), bOver, false, !bCan, Teal, 11);
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
        FFly& Fly = State.Flies[Index];
        if (Fly.bLootRow >= 0 && Fly.From.X < 0) Fly.From = State.LootRowPos[FMath::Clamp(Fly.bLootRow, 0, 11)];
        if (Fly.ToSlot >= 0) Fly.To = State.HudSlotPos[Fly.ToSlot];
        if (Fly.ToSkillSlot >= 0) Fly.To = State.SkillSlotPos[Fly.ToSkillSlot];
        const float Age = static_cast<float>(T - Fly.Start), Life = .5f;
        if (Age < 0) continue;
        if (Age > Life + .25f) { State.Flies.RemoveAt(Index); continue; }
        const float K = FMath::Clamp(Age / Life, 0.f, 1.f);
        const float E = 1.f - FMath::Pow(1.f - K, 3.f);
        FVector2D Pos = FMath::Lerp(Fly.From, Fly.To, E);
        Pos.Y -= FMath::Sin(K * PI) * 70.f;
        const float Size = (Fly.bSell ? FMath::Lerp(40.f, 18.f, E) : FMath::Lerp(46.f, 40.f, E)) * (1.f + .15f * FMath::Sin(K * PI));
        if (K < 1.f)
        {
            FCireUIPainter Q = P; Q.Alpha = Fly.bSell ? 1.f - .6f * K : 1.f;
            // Ghost trail: fading copies of the icon along the arc.
            bSuppressFlash = true;
            for (int32 Trail = 3; Trail >= 1; --Trail)
            {
                const float TK = FMath::Max(0.f, K - Trail * .07f), TE = 1.f - FMath::Pow(1.f - TK, 3.f);
                FVector2D TP = FMath::Lerp(Fly.From, Fly.To, TE); TP.Y -= FMath::Sin(TK * PI) * 70.f;
                FCireUIPainter Ghost = Q; Ghost.Alpha *= .28f / Trail;
                if (Fly.bSkill) DrawMiniScroll(Ghost, Fly.Id.ToString(), TP.X, TP.Y, Size * (1.f - .08f * Trail));
                else DrawItemIcon(Ghost, Fly.Id, TP.X, TP.Y, Size * (1.f - .08f * Trail), false);
            }
            bSuppressFlash = false;
            SoftGlow(Q, Pos.X, Pos.Y, Size, Size, FLinearColor(1.f, .8f, .35f, .5f));
            bSuppressFlash = true;
            if (Fly.bSkill) DrawMiniScroll(Q, Fly.Id.ToString(), Pos.X, Pos.Y, Size);
            else DrawItemIcon(Q, Fly.Id, Pos.X, Pos.Y, Size, false);
            bSuppressFlash = false;
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
    DrawLootWindow(HUD, P, View, T);
    // Kill bounties: "+3g" rising over the kill.
    if (APlayerController* PC = HUD.GetOwningPlayerController())
        for (int32 Index = State.WorldGold.Num() - 1; Index >= 0; --Index)
        {
            const FWorldGold& G = State.WorldGold[Index];
            const float Age = static_cast<float>(T - G.Start);
            if (Age > 1.6f) { State.WorldGold.RemoveAt(Index); continue; }
            FVector2D Screen;
            if (!PC->ProjectWorldLocationToScreen(G.Where, Screen)) continue;
            const FVector2D L = Screen / FMath::Max(.01f, P.Scale);
            const bool bBig = G.Kind >= static_cast<uint8>(CI::BountyKind::Boss);
            const float Size = bBig ? 20.f : 14.f;
            FCireUIPainter Q = P; Q.Alpha = FMath::Clamp((1.6f - Age) / .5f, 0.f, 1.f);
            const FString Text = FString::Printf(TEXT("+%dg"), G.Amount);
            Q.Text(Text, L.X - Q.TextWidth(Text, Size, ECireFont::Numbers) * .5f, L.Y - Age * 40.f, Size, BrightGold, ECireFont::Numbers, true, true);
        }
    // Toasts (right edge).
    const bool bShopTop = Controller && Controller->bShop;
    float TY = bShopTop ? 4.f : View.Y * .28f;
    for (int32 Index = State.Toasts.Num() - 1; Index >= 0; --Index)
    {
        const FToast& Toast = State.Toasts[Index];
        const float Age = static_cast<float>(T - Toast.Start);
        if (Age > Toast.Life) { State.Toasts.RemoveAt(Index); continue; }
        const float In = FMath::Clamp(Age / .25f, 0.f, 1.f), Out = FMath::Clamp((Toast.Life - Age) / .5f, 0.f, 1.f);
        FCireUIPainter Q = P; Q.Alpha = FMath::Min(In, Out);
        // Right of centre, over the world view (clear of the minimap/boss/threat column).
        // Shop open: over the empty middle of the title bar (newest two); otherwise right of centre.
        const bool bShopOpen = Controller && Controller->bShop;
        if (bShopOpen && TY > 60.f) break;
        const float ShopX = FMath::RoundToFloat((View.X - 1010) * .5f);
        const float BaseX = bShopOpen ? ShopX + 600 : FMath::Min(View.X - 316, View.X * .5f + 230);
        const float W = 300, X = BaseX + (1.f - (1.f - FMath::Square(1.f - In))) * 60.f;
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
        else { bSuppressFlash = true; DrawItemIcon(Q, Toast.Icon, X + 6, TY + 6, 34, false); bSuppressFlash = false; }
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

void CireShopUI::ToggleSkillShop(ACireController* Controller)
{
    if (!Controller) return;
    if (Controller->bShop && State.Tab == 1) { Controller->bShop = false; return; }
    if (Controller->bShop) State.Tab = 1;
    else { Controller->bShop = true; State.PendingTab = 1; }
}

void CireShopUI::DrawLootLog(ACireHUD& HUD, ACireHero* Hero)
{
    if (!Hero) return;
    constexpr float DW = 270, DH = 164;
    const FName Id(TEXT("LootLog"));
    HUD.RegisterPanel(Id);
    const FCireUIRect R = HUD.LayoutRect(Id);
    FCireUIPainter P = HUD.ScreenPainter();
    P.Origin = FVector2D(R.X, R.Y); P.Stretch = FVector2D(R.W / DW, R.H / DH);
    CireUIStyle::Frame(P, 0, 0, DW, DH, Gold, ECireFrame::Panel);
    P.Text(TEXT("LOOT LOG"), 10, 6, 10, Gold, ECireFont::Heading);
    P.Text(FString::Printf(TEXT("%s  hide"), *KeyLabel(HUD, TEXT("ToggleLootLog"))), DW - 58, 7, 8, Muted, ECireFont::Heading);
    if (State.LootLog.Num() == 0) { P.Wrapped(TEXT("Personal loot you receive is listed here: what it was, when, and where it came from."), 10, 28, DW - 20, 9, Muted, 3); return; }
    const FVector2D M = (CireShopUI::Pointer(HUD) - P.Origin) / P.Stretch;
    for (int32 Index = 0; Index < FMath::Min(State.LootLog.Num(), 8); ++Index)
    {
        const FLootLogEntry& E = State.LootLog[Index];
        const float Y = 24 + Index * 18;
        P.Text(E.When, 8, Y + 2, 8, Muted, ECireFont::Numbers);
        if (E.Icon == FName(TEXT("gold"))) P.Disc(44, Y + 7, 5, FLinearColor(1.f, .8f, .28f, 1), 12);
        else { bSuppressFlash = true; DrawItemIcon(P, E.Icon, 37, Y, 15, false); bSuppressFlash = false; }
        P.Text(E.Text.Left(34), 56, Y + 1, 9, E.Color, ECireFont::Body);
        if (HUD.IsInteractive() && M.X >= 0 && M.X <= DW && M.Y >= Y && M.Y < Y + 18) CireShopUI::Tip(HUD, E.Text, TEXT("From: ") + E.Why + TEXT("\nPersonal loot: your own roll, visible only to you."));
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
void CireShopUI::DebugFreezeAfterLastEvent(float Age)
{
    if (Age < 0) { State.DebugNow = -1; return; }
    double Latest = -1;
    for (const auto& Fly : State.Flies) Latest = FMath::Max(Latest, Fly.Start);
    for (const auto& Toast : State.Toasts) Latest = FMath::Max(Latest, Toast.Start);
    Latest = FMath::Max(Latest, State.Shake.Start);
    if (Latest > 0) State.DebugNow = Latest + Age;
}
void CireShopUI::DebugSkillTab(const FString& SkillId) { State.Tab = 1; State.PendingTab = 1; State.SelectedSkill = SkillId; }
FVector2D CireShopUI::DebugSkillGridPos(const FString& SkillId) { const FVector2D* P = State.SkillGridPos.Find(SkillId); return P ? *P + FVector2D(23, 23) : FVector2D(-1, -1); }
void CireShopUI::DebugItemTab() { State.Tab = 0; State.PendingTab = 0; }
void CireShopUI::DebugFreezeAfterStamp(float Age) { if (State.StampStart > 0) State.DebugNow = State.StampStart + Age; }
int32 CireShopUI::DebugTab() { return State.Tab; }
void CireShopUI::DebugMouse(FVector2D Logical) { VirtualPointer = Logical; }
FVector2D CireShopUI::DebugGridPos(FName ItemId) { const FVector2D* P = State.GridPos.Find(ItemId); return P ? *P + FVector2D(21, 21) : FVector2D(-1, -1); }
void CireShopUI::DebugReset() { const FName Keep = State.Selected; State = FShopState(); State.Selected = Keep; }
#endif
