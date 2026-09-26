#include "CireShopUI.h"
#include "CireScalingKits.h" // scaling-kits
#include "CireShopArt.h" // progression-shop: scroll cards and ornate framing
#include "CireVendors.h" // vendors: merchant tabs
#include "CireSkillShop.h" // progression-shop: Skill Shop tab
#include "CireAbilityDB.h" // progression-shop: scroll card numbers
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
#include "CireSummon.h" // progression-shop: ready panel skips summons
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
    int32 SkillFilter = 0;      // new-champions: 0 all skills, 1 Constructs only (Aetheri champions)
    int32 Vendor = -1;          // vendors: merchant tab (-1 = every merchant, the B key)
    int32 PendingVendor = -1;   // vendors: merchant tab to show when the shop next opens
    uint32 HiddenSections = 0;  // Skill Shop filter chips (bit per section)
    int32 SkillPage = 0;        // first visible shelf row
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
    // readability: the last buy / sell / undo / error, shown as a strip above the BUY button.
    FString LastEvent; FLinearColor LastEventColor = FLinearColor::White; double LastEventAt = -10; bool bLastEventError = false;
};
FShopState State;
bool bSuppressFlash = false;

double Now() { return State.DebugNow >= 0 ? State.DebugNow : FPlatformTime::Seconds(); }
FVector2D VirtualPointer(-1, -1);

// items-v2 / rules-conformance: items grant the primary stat and flat stats (completed items add damage
// reduction); path-defining uniques get their own filter.
const TCHAR* FilterTags[] = {TEXT("path"), TEXT("primary"), TEXT("attack"), TEXT("block"), TEXT("mana"), TEXT("health"), TEXT("armor"),
    TEXT("ward"), TEXT("defense"), TEXT("heal"), TEXT("boots"), TEXT("support"), TEXT("active"), TEXT("consumable")};
const TCHAR* FilterNames[] = {TEXT("Path Uniques"), TEXT("Primary Stat"), TEXT("Attack"), TEXT("Damage Reduction"), TEXT("Mana"),
    TEXT("Health"), TEXT("Armor"), TEXT("Spell Ward"), TEXT("Mitigation"), TEXT("Healing"), TEXT("Boots"), TEXT("Support"), TEXT("Active Use"), TEXT("Consumables")};
TWeakObjectPtr<const ACireHero> GShopViewer; // items-v2: whose primary stat "+X Primary Stat" names
bool IsPathUnique(const CI::ItemDef& Item) { return Item.UniqueGroup == "path"; }
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
    State.LastEvent = Reason; State.LastEventColor = FLinearColor(1.f, .42f, .36f, 1); State.LastEventAt = Now(); State.bLastEventError = true;
    AddToast(TEXT("Cannot do that"), Reason, Id, Red, 3.2f);
    Play(HUD, Reason.Contains(TEXT("gold")) ? TEXT("S_ShopErrorGold") : TEXT("S_ShopError"), .8f); // audio: not-enough-gold has its own cue
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
            Play(HUD, TEXT("S_SkillLearn")); // audio: Skill Shop buy (AudioCues.json shopLegacy -> ui_skill_buy)
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
            State.LastEvent = FString::Printf(TEXT("BOUGHT  %s   %dg"), *CireItems::DisplayName(F.ItemId), F.GoldDelta); State.LastEventColor = FLinearColor(.5f, 1.f, .55f, 1); State.LastEventAt = Now(); State.bLastEventError = false;
            Play(HUD, TEXT("S_ShopBuy"));
            CireVendors::OnPurchased(HUD.GetWorld(), F.ItemId); // vendors: the merchant who sold it nods
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
            State.LastEvent = FString::Printf(TEXT("SOLD  %s   +%dg"), *CireItems::DisplayName(F.ItemId), F.GoldDelta); State.LastEventColor = BrightGold; State.LastEventAt = Now(); State.bLastEventError = false;
            Play(HUD, TEXT("S_ShopSell"));
            break;
        case ECireShopAction::Undo:
            if (!F.bOk) { ShowError(HUD, NAME_None, -1, false, F.Message); break; }
            AddToast(TEXT("Undone"), F.Message.Replace(TEXT("Undone: "), TEXT("")), NAME_None, Teal, 3.f);
            State.LastEvent = TEXT("UNDONE  ") + F.Message.Replace(TEXT("Undone: "), TEXT("")); State.LastEventColor = FLinearColor(.45f, .95f, .85f, 1); State.LastEventAt = Now(); State.bLastEventError = false;
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
        const TCHAR* Sounds[] = {TEXT("S_LootCommon"), TEXT("S_LootMagic"), TEXT("S_LootRare"), TEXT("S_LootEpic")}; // audio: loot window by rarity
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
    // readability: the kit button draws the hover / selected states itself (readable face, accent bar).
    CireUIStyle::Button(P, X, Y, W, H, Label, bDisabled ? ECireButtonState::Disabled : bSelected ? ECireButtonState::Selected : bHover ? ECireButtonState::Hover : ECireButtonState::Normal,
        bSelected ? BrightGold : Accent, Size);
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
        P.Text(Line, CX - P.TextWidth(Line, Size, Font) * .5f, Y + I * (CireUIStyle::ReadableSize(Size) * 1.12f + Gap), Size, Color, Font, false, false);
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

FString Num(float Value) { return FMath::Abs(Value - FMath::RoundToFloat(Value)) < .05f ? FString::FromInt(FMath::RoundToInt(Value)) : FString::Printf(TEXT("%.1f"), Value); }

// Short effect text for a scroll: the Ability DB prose with its numbers, else the skill description.
FString CardBlurb(const FString& Id)
{
    if (const FCireAbilityDef* Def = CireAbilityDB::Find(Id); Def && !Def->Description.IsEmpty())
        return FirstSentence(Def->Description.Replace(TEXT("{effect}"), *Num(static_cast<float>(Def->Base.Effect))));
    return FirstSentence(ACireHero::SkillDescription(Id));
}

// Level -> next numbers from CireAbilityDB::EffectiveStats: "34 » 37 damage", "32 energy · 17.3s cd".
bool CardNumbers(const FString& Id, int32 Level, FString& Effect, FString& Cost)
{
    const FCireAbilityDef* Def = CireAbilityDB::Find(Id);
    if (!Def) return false;
    const FCireAbilityStats Now = CireAbilityDB::EffectiveStats(Id, FMath::Max(1, Level)), Next = CireAbilityDB::EffectiveStats(Id, FMath::Max(1, Level) + 1);
    if (Now.Effect > 0) Effect = FString::Printf(TEXT("%s » %s %s"), *Num(Now.Effect), *Num(Next.Effect), *Def->EffectLabel).TrimEnd();
    TArray<FString> Parts;
    if (Next.ManaCost > 0) Parts.Add(FString::Printf(TEXT("%s mana"), *Num(Next.ManaCost)));
    if (Next.EnergyCost > 0) Parts.Add(FString::Printf(TEXT("%s energy"), *Num(Next.EnergyCost)));
    if (Next.Cooldown > 0) Parts.Add(FString::Printf(TEXT("%ss cd"), *Num(Next.Cooldown)));
    Cost = FString::Join(Parts, TEXT("  ·  "));
    return !Effect.IsEmpty() || !Cost.IsEmpty();
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

// ------------------------------------------------------------------ Skill Shop sections ("periodic table")
struct FSectionDef { const TCHAR* Id; const TCHAR* Label; const TCHAR* Chip; const TCHAR* Sub; FLinearColor Color; };
const FSectionDef SkillSections[] = {
    {TEXT("spell"), TEXT("OFFENSIVE  ·  SPELL DAMAGE"), TEXT("SPELL DAMAGE"), nullptr, FLinearColor(.42f, .55f, 1.f, 1)},
    {TEXT("attack"), TEXT("OFFENSIVE  ·  ATTACK DAMAGE"), TEXT("ATTACK DAMAGE"), nullptr, FLinearColor(1.f, .42f, .28f, 1)},
    {TEXT("defensive"), TEXT("DEFENSIVE"), TEXT("DEFENSIVE"), nullptr, FLinearColor(.36f, .88f, .46f, 1)},
    {TEXT("control"), TEXT("CROWD CONTROL"), TEXT("CROWD CONTROL"), TEXT("STUNS · SILENCES · SLOWS · ROOTS · POLYMORPH"), FLinearColor(.93f, .42f, .86f, 1)},
    {TEXT("summon"), TEXT("SUMMONS"), TEXT("SUMMONS"), nullptr, FLinearColor(.25f, .85f, .82f, 1)},
    {TEXT("construct"), TEXT("CONSTRUCTS"), TEXT("CONSTRUCTS"), nullptr, FLinearColor(.95f, .62f, .22f, 1)},
    {TEXT("passive"), TEXT("PASSIVES"), TEXT("PASSIVES"), nullptr, FLinearColor(.84f, .80f, .68f, 1)},
    {TEXT("ultimate"), TEXT("ULTIMATES"), TEXT("ULTIMATES"), nullptr, FLinearColor(.78f, .56f, 1.f, 1)},
};
constexpr int32 SectionCount = UE_ARRAY_COUNT(SkillSections);

int32 SectionIndex(const FString& Id)
{
    for (int32 S = 0; S < SectionCount; ++S) if (Id == SkillSections[S].Id) return S;
    return 0;
}

int32 SectionOf(const FString& SkillId)
{
    if (const FCireAbilityDef* Def = CireAbilityDB::Find(SkillId); Def && !Def->Section.IsEmpty()) return SectionIndex(Def->Section);
    const CI::ShopSkillKind Kind = CireSkillShop::KindOf(SkillId);
    return Kind == CI::ShopSkillKind::Ultimate ? SectionIndex(TEXT("ultimate")) : Kind == CI::ShopSkillKind::Passive ? SectionIndex(TEXT("passive")) : 0;
}

FLinearColor TagColor(const FString& Tag)
{
    if (Tag == TEXT("Stun")) return FLinearColor(.62f, .45f, .02f, 1);
    if (Tag == TEXT("Slow") || Tag == TEXT("Root")) return FLinearColor(.05f, .36f, .62f, 1);
    if (Tag == TEXT("Silence") || Tag == TEXT("Interrupt") || Tag == TEXT("Purge")) return FLinearColor(.40f, .14f, .62f, 1);
    if (Tag == TEXT("Polymorph") || Tag == TEXT("Banish")) return FLinearColor(.66f, .12f, .52f, 1);
    if (Tag == TEXT("Heal") || Tag == TEXT("Cleanse")) return FLinearColor(.08f, .45f, .16f, 1);
    if (Tag == TEXT("Summon") || Tag == TEXT("Construct")) return FLinearColor(.04f, .42f, .40f, 1);
    if (Tag == TEXT("Guard") || Tag == TEXT("Shield") || Tag == TEXT("Taunt")) return FLinearColor(.25f, .30f, .40f, 1);
    if (Tag == TEXT("Damage") || Tag == TEXT("Execute") || Tag == TEXT("Armor Break")) return FLinearColor(.58f, .09f, .06f, 1);
    return FLinearColor(.35f, .26f, .14f, 1);
}

// "damage per hit" -> "dmg"; keeps the key-number line short.
FString ShortLabel(const FString& Label)
{
    FString L = Label.ToLower();
    if (L.Contains(TEXT("heal"))) return TEXT("heal");
    if (L.Contains(TEXT("damage"))) return L.Contains(TEXT("reduction")) ? TEXT("% less dmg") : TEXT("dmg");
    if (L.Contains(TEXT("health"))) return TEXT("hp");
    if (L.Contains(TEXT("second"))) return TEXT("s");
    return L.Len() > 10 ? L.Left(10) : L;
}

// Team Ready to Continue: portraits with check marks, the waiting banner, the button and (in the
// safety cap's last 30 s) the countdown. Server truth: UCireInventory::bReadyToContinue (replicated).
void DrawReadyPanel(ACireHUD& HUD, ACireHero* Hero, ACireController* Controller, ACireGameState* GameState, const FCireUIPainter& P,
    float X, float Y, float W, FVector2D M, bool bInteractive)
{
    using namespace CireShopArt;
    if (!GameState || !Hero) return;
    const bool bBreather = CireSkillShop::IsBreather(HUD.GetWorld());
    TArray<ACireHero*> Team;
    for (TActorIterator<ACireHero> It(HUD.GetWorld()); It; ++It)
        if (It->TeamId == Hero->TeamId && It->bDrafted && !It->IsA<ACireSummon>()) Team.Add(*It);
    Team.Sort([](const ACireHero& A, const ACireHero& B) { return A.bBot != B.bBot ? !A.bBot : A.HeroName < B.HeroName; });
    // Portraits.
    const float R = 16.f, Step = 40.f;
    float PX = X + R + 2;
    for (ACireHero* Mate : Team)
    {
        const bool bReady = Mate->Inventory && Mate->Inventory->bReadyToContinue;
        const float PY = Y + 28;
        P.Disc(PX, PY, R + 2, FLinearColor(0, 0, 0, .8f), 24);
        const FString Path = FString::Printf(TEXT("/Game/UI/Draft/Portraits/T_Portrait_%s.T_Portrait_%s"), *Mate->ChampionProfileId, *Mate->ChampionProfileId);
        UTexture2D* Portrait = Mate->ChampionProfileId.IsEmpty() ? nullptr : LoadObject<UTexture2D>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (Portrait) P.Tex(Portrait, PX - R, PY - R, R * 2, R * 2, bReady || !bBreather ? FLinearColor::White : FLinearColor(.45f, .45f, .45f, 1), .2f, .08f, .8f, .68f);
        else
        {
            P.Disc(PX, PY, R, FLinearColor(.08f, .07f, .06f, 1), 24);
            const FString Initial = Mate->HeroName.Left(1).ToUpper();
            P.Text(Initial, PX - P.TextWidth(Initial, 14, ECireFont::Display) * .5f, PY - 9, 14, Parchment, ECireFont::Display, false, false);
        }
        P.Circle(PX, PY, R, bReady ? FLinearColor(.35f, 1.f, .45f, 1) : Filigree * .8f, bReady ? 2.2f : 1.2f, 28);
        if (bReady)
        {
            P.Disc(PX + R * .7f, PY + R * .7f, 7, FLinearColor(.08f, .45f, .15f, 1), 14);
            P.Line(PX + R * .7f - 3.5f, PY + R * .7f, PX + R * .7f - 1, PY + R * .7f + 3, FLinearColor::White, 1.8f);
            P.Line(PX + R * .7f - 1, PY + R * .7f + 3, PX + R * .7f + 4, PY + R * .7f - 3.5f, FLinearColor::White, 1.8f);
        }
        const FString Name = P.Fit(Mate->bBot ? FString(TEXT("BOT")) : Mate->HeroName, 7.f, Step - 2, ECireFont::Heading);
        P.Text(Name, PX - P.TextWidth(Name, 7.f, ECireFont::Heading) * .5f, PY + R + 3, 7.f, Mate == Hero ? BrightGold : Muted * 1.3f, ECireFont::Heading, false, false);
        if (In(M, PX - R, PY - R, R * 2, R * 2))
            CireShopUI::Tip(HUD, Mate->HeroName, Mate->bBot ? TEXT("Bot: always ready.") : bReady ? TEXT("Ready to continue.") : TEXT("Still choosing skills."));
        PX += Step;
    }
    // Banner + button.
    const float BX = X + FMath::Max(Team.Num(), 1) * Step + 10, BW = FMath::Max(160.f, W - (BX - X));
    if (!bBreather || !CireSkillShop::IsSkillShopMode(HUD.GetWorld()))
    {
        Spaced(P, GameState->Phase == 1 ? TEXT("PREP  ·  THE NEXT WAVE FOLLOWS THE ARENA") : TEXT("THE SHOP STAYS OPEN BETWEEN WAVES"), BX, Y + 22, 8.f, .25f, Filigree, ECireFont::Display, false, false);
        return;
    }
    const int32 Humans = GameState->BreatherPlayers, ReadyCount = GameState->BreatherReady;
    const bool bMeReady = Hero->Inventory && Hero->Inventory->bReadyToContinue;
    FString Banner = Humans <= 0 ? FString::Printf(TEXT("BREATHER  ·  NEXT WAVE IN %.0fs"), FMath::Max(0.f, GameState->NextWaveSeconds)) : GameState->bReadyGateHold ? CireSkillShop::WaitingLabel(Humans, ReadyCount)
                                               : FString::Printf(TEXT("ALL READY  ·  NEXT WAVE IN %.0fs"), FMath::Max(0.f, GameState->NextWaveSeconds));
    Spaced(P, Banner, BX, Y + 4, 9.f, .2f, GameState->bReadyGateHold ? BrightGold : FLinearColor(.4f, 1.f, .5f, 1), ECireFont::Display, false, false);
    if (GameState->bReadyGateHold && GameState->ReadyGateLeft >= 0 && GameState->ReadyGateLeft <= 30.f)
    {
        const FString Cap = FString::Printf(TEXT("AFK LIMIT  ·  WAVE STARTS IN %d s"), FMath::CeilToInt(GameState->ReadyGateLeft));
        Spaced(P, Cap, BX, Y + 18, 7.5f, .2f, FLinearColor(1.f, .45f, .4f, 1), ECireFont::Display, false, false);
    }
    const float ButtonW = FMath::Min(BW, 230.f), ButtonY = Y + 32;
    const bool bOver = In(M, BX, ButtonY, ButtonW, 30);
    ShopButton(P, BX, ButtonY, ButtonW, 30, bMeReady ? TEXT("READY  ·  CLICK TO CANCEL") : TEXT("READY TO CONTINUE"), bOver, bMeReady, false,
        bMeReady ? FLinearColor(.35f, 1.f, .45f, 1) : BrightGold, 11);
    if (bOver) CireShopUI::Tip(HUD, TEXT("Ready to Continue"), TEXT("The next wave waits until every player is ready (bots always are). Take your time and plan your picks together."));
    if (bInteractive && HUD.HasClick() && bOver && Controller)
    {
        HUD.TakeClick();
        HUD.SetBreatherReadyLocal(GameState->Wave, !bMeReady);
        Controller->ServerAction(10, bMeReady ? 0 : 1, nullptr);
        Play(HUD, bMeReady ? TEXT("S_ShopTab") : TEXT("S_ShopBuy"), .5f);
    }
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

    // "Periodic table": labelled, bordered sections by affinity (Abilities.json "section"), filter
    // chips, big scroll cards (golden = active, plain = passive, prismatic = ultimate), paged by
    // shelf rows (mouse wheel / arrows) instead of shrinking cards.
    const TArray<FCireShopSkill> Catalog = CireSkillShop::CatalogFor(Hero);
    TArray<const FCireShopSkill*> BySection[SectionCount];
    for (const FCireShopSkill& Skill : Catalog) BySection[SectionOf(Skill.Id)].Add(&Skill);
    // new-champions / pets: the old CONSTRUCTS / COMPANION filter maps onto the section chips.
    if (State.SkillFilter == 1) State.HiddenSections = ~(1u << SectionIndex(TEXT("construct")));
    if (State.SkillFilter == 2) State.HiddenSections = ~(1u << SectionIndex(TEXT("summon")));
    State.SkillFilter = 0;
    // Filter chips.
    {
        float CX = X + 26;
        const float CY = Y + 80, CH = 20;
        auto Chip = [&](const FString& Label, FLinearColor Color, bool bOn) -> bool
        {
            const float CW = P.TextWidth(Label, 9.5f, ECireFont::Heading) + 26;
            const bool bOver = In(M, CX, CY, CW, CH);
            P.Rect(CX, CY, CW, CH, bOn ? Color * FLinearColor(.32f, .32f, .32f, .95f) : FLinearColor(.03f, .03f, .035f, .9f));
            Border(P, CX, CY, CW, CH, bOn ? Color : Color * FLinearColor(.5f, .5f, .5f, .6f), bOver ? 1.8f : 1.f);
            Diamond(P, CX + 9, CY + CH * .5f, 3.5f, bOn ? Color : Muted, bOn);
            P.Text(Label, CX + 17, CY + 3, 9.5f, bOn ? Parchment : Muted * 1.2f, ECireFont::Heading, false, false);
            const bool bHit = Click(CX, CY, CW, CH);
            CX += CW + 6;
            return bHit;
        };
        if (Chip(TEXT("ALL"), Gold, State.HiddenSections == 0)) { State.HiddenSections = 0; State.SkillPage = 0; Play(HUD, TEXT("S_ShopTab"), .4f); }
        for (int32 S = 0; S < SectionCount; ++S)
        {
            if (BySection[S].Num() == 0) continue;
            const bool bOn = (State.HiddenSections & (1u << S)) == 0;
            if (Chip(FString::Printf(TEXT("%s  %d"), SkillSections[S].Chip, BySection[S].Num()), SkillSections[S].Color, bOn))
            {
                // First click on a section while showing ALL isolates it; later clicks toggle.
                if (State.HiddenSections == 0) State.HiddenSections = ~(1u << S);
                else State.HiddenSections ^= 1u << S;
                if (State.HiddenSections == 0xFFFFFFFFu) State.HiddenSections = 0;
                State.SkillPage = 0;
                Play(HUD, TEXT("S_ShopTab"), .4f);
            }
        }
        // Tabs (right of the chips).
        const TCHAR* Tabs[] = {TEXT("ITEMS"), TEXT("SKILLS")};
        const FName TabKeys[] = {TEXT("ToggleShop"), TEXT("ToggleSkillShop")};
        for (int32 Index = 0; Index < 2; ++Index)
        {
            const float TX = X + W - 26 - 2 * 96 + Index * 96;
            const bool bOver = In(M, TX, CY, 90, CH);
            ShopButton(P, TX, CY, 90, CH, FString::Printf(TEXT("%s [%s]"), Tabs[Index], *KeyLabel(HUD, TabKeys[Index])), bOver, State.Tab == Index, false, Gold, 8.5f);
            if (Click(TX, CY, 90, CH)) { State.Tab = Index; Play(HUD, TEXT("S_ShopTab"), .4f); }
        }
    }

    // Shelf layout: each section is a bordered block of cards; blocks wider than a row continue on
    // the next row; rows are pages of the view (no card ever shrinks below its readable size).
    constexpr float CardW = 150.f, CardH = 198.f, Gap = 10.f, Pad = 8.f, Head = 20.f;
    const float BandH = 92.f;
    const float AreaX = X + 22, AreaW = W - 44 - 14, AreaY = Y + 108, AreaH = Y + H - BandH - 8 - AreaY;
    const float RowH = CardH + Head + Pad * 2 + 6;
    const int32 MaxCols = FMath::Max(1, FMath::FloorToInt((AreaW - Pad * 2 + Gap) / (CardW + Gap)));
    struct FBlock { int32 Section; TArray<const FCireShopSkill*> Cards; bool bCont; float X, W; };
    TArray<TArray<FBlock>> Rows;
    {
        TArray<FBlock> Row; float RX = 0;
        for (int32 S = 0; S < SectionCount; ++S)
        {
            if (BySection[S].Num() == 0 || (State.HiddenSections & (1u << S))) continue;
            for (int32 Start = 0; Start < BySection[S].Num();)
            {
                const float Free = AreaW - RX;
                int32 Fit = FMath::FloorToInt((Free - Pad * 2 + Gap) / (CardW + Gap));
                if (Fit < 1 || (Fit < BySection[S].Num() - Start && Fit < 2 && RX > 0)) { Rows.Add(Row); Row.Reset(); RX = 0; continue; }
                const int32 Take = FMath::Min(Fit, BySection[S].Num() - Start);
                FBlock B; B.Section = S; B.bCont = Start > 0; B.X = RX; B.W = Pad * 2 + Take * CardW + (Take - 1) * Gap;
                for (int32 I = 0; I < Take; ++I) B.Cards.Add(BySection[S][Start + I]);
                Row.Add(B); RX += B.W + 12; Start += Take;
            }
        }
        if (Row.Num()) Rows.Add(Row);
    }
    const int32 PerPage = FMath::Max(1, FMath::FloorToInt(AreaH / RowH));
    const int32 MaxPage = FMath::Max(0, Rows.Num() - PerPage);
    if (APlayerController* PC = HUD.GetOwningPlayerController(); PC && bInteractive && In(M, AreaX, AreaY, AreaW + 14, AreaH))
    {
        if (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp)) --State.SkillPage;
        if (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown)) ++State.SkillPage;
    }
    State.SkillPage = FMath::Clamp(State.SkillPage, 0, MaxPage);
    // Scroll bar with arrows.
    if (MaxPage > 0)
    {
        const float SX = X + W - 32, SY = AreaY, SH = AreaH;
        P.Rect(SX, SY + 16, 8, SH - 32, FLinearColor(.05f, .045f, .04f, .9f));
        const float Thumb = FMath::Max(30.f, (SH - 32) * PerPage / static_cast<float>(Rows.Num()));
        const float TY = SY + 16 + (SH - 32 - Thumb) * State.SkillPage / static_cast<float>(MaxPage);
        P.Rect(SX, TY, 8, Thumb, Filigree * FLinearColor(1, 1, 1, .8f));
        for (int32 Dir = 0; Dir < 2; ++Dir)
        {
            const float AY = Dir == 0 ? SY : SY + SH - 14;
            const bool bOver = In(M, SX - 4, AY, 16, 14);
            const FLinearColor C = bOver ? BrightGold : Filigree;
            if (Dir == 0) P.Tri(FVector2D(SX + 4, AY + 2), FVector2D(SX + 10, AY + 12), FVector2D(SX - 2, AY + 12), C);
            else P.Tri(FVector2D(SX + 4, AY + 12), FVector2D(SX + 10, AY + 2), FVector2D(SX - 2, AY + 2), C);
            if (Click(SX - 4, AY, 16, 14)) State.SkillPage = FMath::Clamp(State.SkillPage + (Dir == 0 ? -1 : 1), 0, MaxPage);
        }
        const FString More = FString::Printf(TEXT("ROWS %d-%d OF %d  ·  SCROLL FOR MORE"), State.SkillPage + 1, FMath::Min(Rows.Num(), State.SkillPage + PerPage), Rows.Num());
        Spaced(P, More, X + W * .5f, AreaY + AreaH - 2, 7.f, .3f, Filigree * .8f, ECireFont::Display, true, false);
    }
    struct FCard { FString Id; float X, Y, W, H; EScroll Tier; };
    TArray<FCard> Cards;
    for (int32 RowIndex = State.SkillPage; RowIndex < Rows.Num() && RowIndex < State.SkillPage + PerPage; ++RowIndex)
    {
        const float RY = AreaY + (RowIndex - State.SkillPage) * RowH;
        for (const FBlock& B : Rows[RowIndex])
        {
            const FSectionDef& Def = SkillSections[B.Section];
            const float BX = AreaX + B.X, BH = RowH - 6;
            // Periodic-table block: tinted ground, coloured border, labelled tab.
            P.Rect(BX, RY, B.W, BH, Def.Color * FLinearColor(.06f, .06f, .06f, .55f));
            Border(P, BX, RY, B.W, BH, Def.Color * FLinearColor(1, 1, 1, .85f), 1.6f);
            P.Rect(BX, RY, B.W, Head, Def.Color * FLinearColor(.28f, .28f, .28f, .95f));
            FString Label = FString(Def.Label) + (B.bCont ? TEXT("  (CONT.)") : TEXT(""));
            Spaced(P, P.Fit(Label, 9.5f, B.W - 16, ECireFont::Display), BX + 8, RY + 4, 9.5f, .18f, Parchment, ECireFont::Display, false, false);
            if (Def.Sub && B.W > 300 && !B.bCont)
            {
                const float SW = SpacedWidth(P, Def.Sub, 7.f, .2f, ECireFont::Display);
                Spaced(P, Def.Sub, BX + B.W - 8 - SW, RY + 6, 7.f, .2f, Def.Color * 1.2f, ECireFont::Display, false, false);
            }
            for (int32 I = 0; I < B.Cards.Num(); ++I)
                Cards.Add({B.Cards[I]->Id, BX + Pad + I * (CardW + Gap), RY + Head + Pad, CardW, CardH, ScrollOf(B.Cards[I]->Kind)});
        }
    }
    if (Cards.Num() == 0) Spaced(P, TEXT("NO SKILLS IN THE SELECTED GROUPS"), X + W * .5f, AreaY + 60, 10.f, .3f, Muted, ECireFont::Display, true, false);

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
        const float Grow = 1.f + .06f * Lift;
        const float CW = Sc.W * Grow, CH = Sc.H * Grow;
        const float CX = Sc.X - (CW - Sc.W) * .5f + ShakeOffset(FName(*Id)), CY = Sc.Y - (CH - Sc.H) * .5f - 5.f * Lift;
        const FRectF Pr = CireShopArt::Scroll(P, Sc.Tier, CX, CY, CW, CH, Time, GetTypeHash(Id), bBlocked ? .7f : 0.f, Lift);
        State.SkillGridPos.Add(Id, FVector2D(CX + CW * .5f - 22, CY + CH * .35f - 22));
        const float Mid = Pr.X + Pr.W * .5f;
        // A cream wash under the ink keeps text readable on every tier (the prismatic sheen included).
        P.Rect(Pr.X - 3, Pr.Y + 2, Pr.W + 6, Pr.H - 4, FLinearColor(.93f, .86f, .70f, Sc.Tier == EScroll::Prismatic ? .78f : .45f));
        const float Ring = FMath::Min(Pr.W * .34f, 30.f);
        float TY = Pr.Y + 4;
        SkillMedallion(P, Id, Mid, TY + Ring, Ring, Level, Time);
        TY += Ring * 2 + 5;
        TY += CentredWrap(P, ACireHero::SkillName(Id), Mid, TY, Pr.W + 8, 11.f, CireShopArt::Ink, ECireFont::Bold, 2, 0.f) * 11.5f + 1;
        const FString LevelLine = bOwned ? FString::Printf(TEXT("LEVEL %d  »  %d"), Level, Level + 1) : TEXT("NEW SKILL");
        P.Text(LevelLine, Mid - P.TextWidth(LevelLine, 8.5f, ECireFont::Heading) * .5f, TY, 8.5f, InkRed, ECireFont::Heading, false, false);
        TY += 11.5f;
        // Affinity tags as coloured chips (at most two on the card; all of them in the tooltip).
        if (const FCireAbilityDef* Def = CireAbilityDB::Find(Id))
        {
            TArray<FString> Tags;
            for (const FString& Tag : Def->EffectTags) if (Tag != TEXT("Damage") && Tags.Num() < 2) Tags.Add(Tag);
            if (Tags.IsEmpty() && Def->EffectTags.Contains(TEXT("Damage"))) Tags.Add(TEXT("Damage"));
            float TW = 0;
            for (const FString& Tag : Tags) TW += P.TextWidth(Tag.ToUpper(), 7.5f, ECireFont::Heading) + 10 + 3;
            float TX = Mid - (TW - 3) * .5f;
            for (const FString& Tag : Tags)
            {
                const FString Upper = Tag.ToUpper();
                const float W1 = P.TextWidth(Upper, 7.5f, ECireFont::Heading) + 10;
                P.Rect(TX, TY, W1, 12, TagColor(Tag));
                P.Text(Upper, TX + 5, TY + .5f, 7.5f, FLinearColor::White, ECireFont::Heading, false, false);
                TX += W1 + 3;
            }
            if (Tags.Num()) TY += 15;
        }
        // One key number line: the effect (level -> next when owned) and the cooldown.
        if (const FCireAbilityDef* Def = CireAbilityDB::Find(Id); Def && Pr.Y + Pr.H - TY > 10)
        {
            const FCireAbilityStats Now1 = CireAbilityDB::EffectiveStats(Id, FMath::Max(1, Level));
            FString Key;
            if (Now1.Effect > 0) Key = FString::Printf(TEXT("%s %s"), *Num(Now1.Effect), *ShortLabel(Def->EffectLabel));
            if (Now1.Cooldown > 0) Key += (Key.IsEmpty() ? TEXT("") : TEXT("  ·  ")) + FString::Printf(TEXT("%ss"), *Num(Now1.Cooldown));
            Key = P.Fit(Key, 9.f, Pr.W + 10, ECireFont::Bold);
            P.Text(Key, Mid - P.TextWidth(Key, 9.f, ECireFont::Bold) * .5f, TY, 9.f, FLinearColor(.01f, .06f, .01f, 1), ECireFont::Bold, false, false);
        }
        // Price on the lower roll; the reason ribbon when it cannot be bought.
        const float RollH = CH - (Pr.Y - CY) - Pr.H;
        const float PY = Pr.Y + Pr.H + RollH * .5f;
        const FString PriceText = FString::Printf(TEXT("%s  %dg"), bOwned ? TEXT("LEVEL UP") : TEXT("LEARN"), Price);
        const float PS = 11.f;
        const float PW = P.TextWidth(PriceText, PS, ECireFont::Numbers);
        P.Rect(CX + CW * .5f - PW * .5f - 8, PY - PS * .75f, PW + 16, PS * 1.5f, FLinearColor(.05f, .035f, .02f, .92f));
        P.Line(CX + CW * .5f - PW * .5f - 8, PY - PS * .75f, CX + CW * .5f + PW * .5f + 8, PY - PS * .75f, Filigree * .8f, .8f);
        P.Line(CX + CW * .5f - PW * .5f - 8, PY + PS * .75f, CX + CW * .5f + PW * .5f + 8, PY + PS * .75f, Filigree * .8f, .8f);
        P.Text(PriceText, CX + CW * .5f - PW * .5f, PY - PS * .64f, PS, bBlocked ? FLinearColor(1.f, .55f, .47f, 1) : BrightGold, ECireFont::Numbers, false, false);
        if (bBlocked && !Short.IsEmpty())
        {
            const float SS = 8.f;
            const float SW = SpacedWidth(P, Short, SS, .15f, ECireFont::Heading) + 12;
            const float RY = Pr.Y + Pr.H - SS * 2.1f;
            P.Rect(Mid - SW * .5f, RY, SW, SS * 1.8f, FLinearColor(.40f, .05f, .04f, .95f));
            Diamond(P, Mid - SW * .5f, RY + SS * .9f, SS * .9f, FLinearColor(.40f, .05f, .04f, .95f));
            Diamond(P, Mid + SW * .5f, RY + SS * .9f, SS * .9f, FLinearColor(.40f, .05f, .04f, .95f));
            Spaced(P, Short, Mid, RY + SS * .3f, SS, .15f, FLinearColor(1.f, .9f, .82f, 1), ECireFont::Heading, true, false);
        }
        if (ShakeOffset(FName(*Id)) != 0.f) Glow(P, CX - CW * .1f, CY - CH * .05f, CW * 1.2f, CH * 1.1f, FLinearColor(.6f, .05f, .03f, 1));
        if (const float Flash = FlashAmount(FName(*Id)); Flash > 0) Glow(P, CX - CW * .15f, CY - CH * .1f, CW * 1.3f, CH * 1.2f, FLinearColor(1.f, .8f, .45f, 1) * (Flash * .6f));
        if (State.StampId == Id && StampAge >= 0 && StampAge < 1.5)
            WaxSeal(P, Mid, Pr.Y + Pr.H * .5f, FMath::Clamp(Pr.W * .3f, 14.f, 28.f), static_cast<float>(StampAge / 1.5), FString::FromInt(FMath::Max(1, Level)));
        if (Id == HoverId)
        {
            // Full numbers live in the tooltip (scaling-kits: primary-stat scaling and the Lv 15 bonus).
            const FString Db = CireKits::DescribeFor(Hero, Id, FMath::Max(1, Level));
            FString Body = Db.IsEmpty() ? ACireHero::SkillDescription(Id) : Db;
            const FCireAbilityDef* Def = CireAbilityDB::Find(Id);
            Body += FString::Printf(TEXT("\n%s  |  %s  |  %s"), *FString(SkillSections[SectionOf(Id)].Label), *CireSkillShop::SchoolOf(Id), *CireSkillShop::RoleTags(Id));
            if (Def && Def->EffectTags.Num()) Body += TEXT("\nTags: ") + FString::Join(Def->EffectTags, TEXT(", "));
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

    // Bottom band: your skill bar (where bought scrolls fly) and the team's Ready to Continue panel.
    const float BandY = Y + H - BandH;
    Rule(P, X + 24, X + W - 24, BandY + 2, Filigree * FLinearColor(1, 1, 1, .7f), true);
    {
        Spaced(P, TEXT("YOUR SKILLS"), X + 30, BandY + 12, 7.5f, .3f, Filigree, ECireFont::Display, false, false);
        const float S = 42, Step = 48;
        const float BX = X + 30, BY = BandY + 26;
        for (int32 Index = 0; Index < Cires::MaxSkills && Index < 8; ++Index)
        {
            const FString Id = Hero->Skills.IsValidIndex(Index) ? Hero->Skills[Index] : FString();
            const float IX = BX + Index * Step;
            const bool bOver = bInteractive && !Id.IsEmpty() && In(M, IX, BY, S, S);
            DrawSkillIcon(P, Id, IX, BY, S, bOver, false, Id.IsEmpty() ? 0 : CireSkillShop::Level(Hero, Id));
            State.SkillSlotPos[Index] = FVector2D(IX, BY);
            if (bOver) CireShopUI::Tip(HUD, ACireHero::SkillName(Id), CireAbilityDB::Describe(Id, CireSkillShop::Level(Hero, Id)));
        }
    }
    DrawReadyPanel(HUD, Hero, Controller, GameState, P, X + W * .5f - 20, BandY + 8, X + W - 26 - (X + W * .5f - 20), M, bInteractive);
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
        FString Label = UTF8_TO_TCHAR(CI::StatLabel(Stat));
        if (Stat == CI::ItemStat::Primary) // items-v2: adaptive primary stat
        {
            const ACireHero* Viewer = GShopViewer.Get();
            const TCHAR* Mine = !Viewer ? nullptr : Viewer->PrimaryStat() == Cires::PrimaryStat::Strength ? TEXT("STR") : Viewer->PrimaryStat() == Cires::PrimaryStat::Agility ? TEXT("AGI") : TEXT("INT");
            if (Mine) Label += FString::Printf(TEXT(" (%s for you)"), Mine);
        }
        Lines += FString::Printf(TEXT("+%s%s %s\n"), *Number, CI::StatIsPercent(Stat) ? TEXT("%") : TEXT(""), *Label);
    }
    return Lines.TrimEnd();
}

FString CireShopUI::ItemTooltip(FName ItemId, int32 PriceForYou)
{
    const CI::ItemDef* Item = CireItems::Find(ItemId);
    if (!Item) return FString();
    const auto& D = CireItems::Get();
    FString Body = FString::Printf(TEXT("%s%s item  |  %dg"), IsPathUnique(*Item) ? TEXT("Path-defining ") : TEXT(""), UTF8_TO_TCHAR(CI::TierName(Item->Tier)), Item->TotalCost);
    if (const FString* Effect = D.EffectLine.Find(ItemId)) Body += TEXT("\n") + *Effect; // items-v2: one-line effect
    if (PriceForYou >= 0 && PriceForYou != Item->TotalCost) Body += FString::Printf(TEXT("  (your price %dg)"), PriceForYou);
    if (!Item->Purchasable) Body += TEXT("  |  loot only");
    const FString Stats = StatLines(ItemId);
    if (!Stats.IsEmpty()) Body += TEXT("\n") + Stats.Replace(TEXT("\n"), TEXT("   "));
    if (const auto* Passives = D.PassiveText.Find(ItemId)) for (const FString& Line : *Passives) Body += TEXT("\nUNIQUE PASSIVE  ") + Line;
    if (const FString* Use = D.UseText.Find(ItemId); Use && !Use->IsEmpty())
        Body += (Item->Belt || Item->Instant ? TEXT("\nUSE  ") : TEXT("\nACTIVE  ")) + *Use;
    if (!Item->UniqueGroup.empty()) Body += FString::Printf(TEXT("\nOnly one %s may be carried."), UTF8_TO_TCHAR(CI::UniqueGroupLabel(Item->UniqueGroup).c_str()));
    else if (Item->Unique) Body += TEXT("\nUnique: you may carry only one.");
    return Body;
}

// readability: the rich item card (icon, name in its tier colour, tier tag, price line, symbol stat lines,
// the one-line effect, UNIQUE PASSIVE / ACTIVE sections, the recipe, lore; Extra is the footer).
FCireTooltipSpec CireShopUI::ItemTooltipSpec(FName ItemId, int32 PriceForYou, const FString& Extra)
{
    FCireTooltipSpec T;
    const CI::ItemDef* Item = CireItems::Find(ItemId);
    if (!Item) return T;
    const auto& D = CireItems::Get();
    const bool bPath = IsPathUnique(*Item);
    const FLinearColor TC = bPath ? FLinearColor(1.f, .62f, .22f, 1) : TierColor(static_cast<int32>(Item->Tier));
    T.Icon = FindItemIcon(ItemId); T.Sigil = ItemId.ToString(); T.IconTint = TC;
    T.IconKind = Item->Tier == CI::ItemTier::Legendary ? ECireSlotKind::Ultimate : ECireSlotKind::Normal;
    T.Title = CireItems::DisplayName(ItemId); T.TitleColor = FMath::Lerp(TC, FLinearColor::White, .15f); T.Accent = TC;
    T.Tag = bPath ? TEXT("PATH UNIQUE") : FString(UTF8_TO_TCHAR(CI::TierName(Item->Tier))).ToUpper(); T.TagColor = TC;
    FString Sub = FString::Printf(TEXT("Cost %dg"), Item->TotalCost);
    if (!Item->Components.empty()) Sub += FString::Printf(TEXT("  ·  recipe %dg"), Item->RecipeCost);
    if (PriceForYou >= 0 && PriceForYou != Item->TotalCost && Item->Purchasable) Sub += FString::Printf(TEXT("  ·  your price %dg"), PriceForYou);
    if (!Item->Purchasable) Sub += TEXT("  ·  loot only");
    T.Subtitle = Sub; T.SubtitleColor = FLinearColor(1.f, .84f, .4f, 1);
    TArray<FString> Stats; StatLines(ItemId).ParseIntoArrayLines(Stats);
    for (const FString& Line : Stats) T.Stat(Line, CireUIStyle::StatColor(Line));
    if (const FString* Effect = D.EffectLine.Find(ItemId)) { if (Stats.Num()) T.Divider(); T.Text(*Effect, bPath ? FLinearColor(1.f, .72f, .35f, 1) : FLinearColor(1.f, .86f, .5f, 1)); }
    if (const auto* Passives = D.PassiveText.Find(ItemId))
        for (const FString& Line : *Passives) { T.Divider(); T.Header(TEXT("UNIQUE PASSIVE"), BrightGold); T.Text(Line); }
    if (const FString* Use = D.UseText.Find(ItemId); Use && !Use->IsEmpty())
    { T.Divider(); T.Header(Item->Belt || Item->Instant ? TEXT("USE") : TEXT("ACTIVE"), FLinearColor(.4f, .92f, .82f, 1)); T.Text(*Use); }
    if (!Item->Components.empty())
    {
        TArray<FString> Parts;
        for (const std::string& Part : Item->Components) Parts.Add(CireItems::DisplayName(ToName(Part)));
        T.Divider(); T.Header(TEXT("RECIPE"), CireShopArt::Filigree);
        T.Text(FString::Join(Parts, TEXT("  +  ")) + FString::Printf(TEXT("  +  %dg"), Item->RecipeCost), FLinearColor(.84f, .86f, .88f, 1), 10.f);
    }
    if (!Item->UniqueGroup.empty()) T.Text(FString::Printf(TEXT("Only one %s may be carried."), UTF8_TO_TCHAR(CI::UniqueGroupLabel(Item->UniqueGroup).c_str())), Muted * 1.3f, 9.5f);
    else if (Item->Unique) T.Text(TEXT("Unique: you may carry only one."), Muted * 1.3f, 9.5f);
    if (!Item->Lore.empty()) T.Text(Str(Item->Lore), FLinearColor(.62f, .64f, .66f, 1), 9.5f);
    T.Footer = Extra;
    return T;
}

void CireShopUI::TipItem(ACireHUD& HUD, FName ItemId, int32 PriceForYou, const FString& Extra)
{
    const FCireTooltipSpec Spec = ItemTooltipSpec(ItemId, PriceForYou, Extra);
    HUD.SetRichTooltip(Spec);
#if !UE_BUILD_SHIPPING
    if (VirtualPointer.X >= 0) HUD.DebugTooltip(Spec.Title, FString(), VirtualPointer + FVector2D(18, 18));
#endif
}

// ------------------------------------------------------------------ HUD elements (bag bar)
void CireShopUI::DrawHUDElements(ACireHUD& HUD, ACireHero* Hero, ACireController* Controller, ACireGameState* GameState)
{
    // Fly targets: the shop's bag strip while the shop is open, else the HUD bag bar.
    State.bShopDrawn = Controller && Controller->bShop;
    if (!Hero || !Hero->bDrafted || !Hero->Inventory) return;
    GShopViewer = Hero; // items-v2
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
            // One notice at a time: a second clear (or prep) while the last one is still up refreshes it
            // instead of stacking an identical toast.
            State.Toasts.RemoveAll([](const FToast& T) { return T.Title == TEXT("SKILL SHOP OPEN"); });
            AddToast(TEXT("SKILL SHOP OPEN"), FString::Printf(TEXT("%s: learn or level skills (%s)."), bPrepBegan ? TEXT("Prep") : TEXT("Wave cleared"),
                *KeyLabel(HUD, TEXT("ToggleSkillShop"))), TEXT("challenge"), BrightGold, 6.f);
            // monster-expansion: a bonus loot wave runs in this breather: leave the field open for the chase (the key still opens the shop).
            const bool bBonusChase = GameState->Announcement.StartsWith(TEXT("BONUS LOOT WAVE"));
            if (CireSkillShop::Get().bAutoOpen && AnySkillAffordable(Hero) && !bBonusChase)
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
        if (bShopOpen)
        {
            Play(HUD, TEXT("S_ShopOpen"), .6f); State.Tab = State.PendingTab >= 0 ? State.PendingTab : 0;
            // vendors: the B key shows every merchant; Interact / a click on a merchant opens his tab.
            State.Vendor = State.PendingVendor;
            if (State.Vendor >= 0) State.Category = 1;
        }
        State.PendingTab = -1; State.PendingVendor = -1;
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
    FName HoverItem; FString HoverExtra; // readability: rich item card for bag / belt slots
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
            HoverItem = Cell.Id; HoverTitle.Reset();
            HoverExtra = Item->Use.IsSet() && !Item->Instant ? TEXT("Click or press its key to use.") : TEXT("");
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
    if (!HoverItem.IsNone()) TipItem(HUD, HoverItem, -1, HoverExtra);
    else if (!HoverTitle.IsEmpty()) Tip(HUD, HoverTitle, HoverBody);

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
    FCireUIPainter P = HUD.ScreenPainter();
    const FVector2D M = Pointer(HUD);
    const bool bInteractive = HUD.IsInteractive();
    const bool bRightClick = bInteractive && HUD.GetOwningPlayerController() && HUD.GetOwningPlayerController()->WasInputKeyJustPressed(EKeys::RightMouseButton);
    const bool bCtrl = HUD.GetOwningPlayerController() && (HUD.GetOwningPlayerController()->IsInputKeyDown(EKeys::LeftControl) || HUD.GetOwningPlayerController()->IsInputKeyDown(EKeys::RightControl));
    if (bInteractive && bCtrl && HUD.GetOwningPlayerController()->WasInputKeyJustPressed(EKeys::Z)) Hero->Inventory->ServerUndo();
    P.Rect(0, 0, View.X, View.Y, FLinearColor(0, 0, 0, .45f));
    if (State.Tab == 1) { DrawSkillScreen(HUD, Hero, Controller, GameState, P, View, M, bInteractive, bRightClick); return; }
    using CireShopArt::Filigree;
    auto Click = [&](float BX, float BY, float BW, float BH) { if (bInteractive && HUD.HasClick() && In(M, BX, BY, BW, BH)) { HUD.TakeClick(); return true; } return false; };
    const double TNow = Now();
    const float Time = static_cast<float>(FMath::Fmod(TNow, 10000.0));

    // The Armory: same framing language as the Skill Shop (Eric's target image).
    const float W = FMath::Min(View.X - 20.f, 1256.f), H = FMath::Min(View.Y - 16.f, 704.f);
    const float X = FMath::RoundToFloat((View.X - W) * .5f), Y = FMath::RoundToFloat((View.Y - H) * .5f);
    CireShopArt::Panel(P, X, Y, W, H, 330);
    // vendors: the three town merchants split the catalogue; B shows them all, a merchant's own tab shows his wares.
    const auto& VendorData = CireVendors::Get();
    if (!VendorData.Vendors.IsValidIndex(State.Vendor)) State.Vendor = -1;
    if (State.Vendor >= 0 && State.Category == 0) State.Category = 1;
    const FCireVendorDef* ShownVendor = State.Vendor >= 0 ? &VendorData.Vendors[State.Vendor] : nullptr;
    const FName ShownVendorId = ShownVendor ? ShownVendor->Id : NAME_None;
    if (ShownVendor) CireShopArt::Title(P, X + W * .5f, Y + 13, ShownVendor->Name.ToUpper(), FString::Printf(TEXT("%s  ·  %s"), *ShownVendor->StatLabel, *ShownVendor->Keeper.ToUpper()), 31);
    else CireShopArt::Title(P, X + W * .5f, Y + 13, TEXT("MERCHANTS' ROW"), TEXT("THREE MERCHANTS  ·  EVERY WARE"), 31);
    const CI::ShopAccess Access = ClientAccess(Hero, GameState);
    FString Status; bool bOpen = true;
    if (Access == CI::ShopAccess::Allowed && GameState && GameState->Phase == 1)
        Status = FString::Printf(TEXT("PREP  ·  BUY ANYWHERE  ·  %d:%02d"), FMath::FloorToInt(FMath::Max(0.f, GameState->SecondsLeft) / 60), FMath::FloorToInt(FMath::Max(0.f, GameState->SecondsLeft)) % 60);
    else if (Access == CI::ShopAccess::Allowed) Status = TEXT("IN TOWN  ·  TRADING OPEN");
    else { Status = Access == CI::ShopAccess::NotInTown ? TEXT("CLOSED  ·  RETURN TO TOWN") : TEXT("CLOSED  ·  OPENS IN PREP"); bOpen = false; }
    const FString RoleKey = CireItems::RoleKey(Hero);
    const FString RoleCaption = RoleKey == TEXT("tank") ? TEXT("TANK") : RoleKey == TEXT("support") ? TEXT("SUPPORT") : RoleKey == TEXT("caster") ? TEXT("CASTER (AREA)") : RoleKey == TEXT("summoner") ? TEXT("SUMMONER") : RoleKey == TEXT("constructor") ? TEXT("CONSTRUCTS") : TEXT("ATTACKER");
    CireShopArt::CompassStar(P, X + 40, Y + 46, 15, Filigree * FLinearColor(1, 1, 1, .8f));
    CireShopArt::Spaced(P, FString::Printf(TEXT("YOUR CHAMPION  ·  %s"), *RoleCaption), X + 66, Y + 28, 7.f, .34f, Filigree * .85f, ECireFont::Display, false, false);
    P.Text(P.Fit(Hero->HeroName, 12, 230, ECireFont::Bold), X + 66, Y + 39, 12, Parchment, ECireFont::Bold);
    CireShopArt::Spaced(P, Status, X + 66, Y + 57, 7.f, .3f, bOpen ? Teal : FLinearColor(1.f, .45f, .4f, 1), ECireFont::Display, false, false);
    CireShopArt::CompassStar(P, X + W - 40, Y + 46, 15, Filigree * FLinearColor(1, 1, 1, .8f));
    {
        const float RX = X + W - 66;
        const float GW = CireShopArt::SpacedWidth(P, TEXT("YOUR GOLD"), 7.f, .38f);
        CireShopArt::Spaced(P, TEXT("YOUR GOLD"), RX - GW, Y + 28, 7.f, .38f, Filigree * .85f, ECireFont::Display, false, false);
        DrawGoldCounter(P, Hero, RX, Y + 37, 15, true);
        const TCHAR* Tabs[] = {TEXT("ITEMS"), TEXT("SKILLS")};
        const FName TabKeys[] = {TEXT("ToggleShop"), TEXT("ToggleSkillShop")};
        for (int32 Index = 0; Index < 2; ++Index)
        {
            const float TX = RX - 196 + Index * 100, TY = Y + 57;
            const bool bOver = In(M, TX, TY, 94, 20);
            ShopButton(P, TX, TY, 94, 20, FString::Printf(TEXT("%s [%s]"), Tabs[Index], *KeyLabel(HUD, TabKeys[Index])), bOver, State.Tab == Index, false, Gold, 8.5f);
            if (Click(TX, TY, 94, 20)) { State.Tab = Index; Play(HUD, TEXT("S_ShopTab"), .4f); }
        }
    }
    {
        const float CX = X + W - 30, CY = Y + 22;
        const bool bOver = In(M, CX - 11, CY - 11, 22, 22);
        const FLinearColor C = bOver ? BrightGold : Filigree * .8f;
        P.Line(CX - 6, CY - 6, CX + 6, CY + 6, C, 1.6f); P.Line(CX - 6, CY + 6, CX + 6, CY - 6, C, 1.6f);
        if (Click(CX - 11, CY - 11, 22, 22) && Controller) Controller->bShop = false;
    }

    // vendors: merchant tabs across the top (every merchant + one per merchant), emblem, name and stat.
    {
        const int32 Tabs = VendorData.Vendors.Num() + 1;
        const float SX = X + 22, SW = W - 44, TY = Y + 80, TH = 40, Gap = 10;
        const float TW = (SW - Gap * (Tabs - 1)) / Tabs;
        for (int32 Index = 0; Index < Tabs; ++Index)
        {
            const int32 VendorIndex = Index - 1;
            const FCireVendorDef* V = VendorIndex >= 0 ? &VendorData.Vendors[VendorIndex] : nullptr;
            const float TX = SX + Index * (TW + Gap);
            const bool bSel = State.Vendor == VendorIndex;
            const bool bOver = bInteractive && In(M, TX, TY, TW, TH);
            const FLinearColor Accent = V ? V->Accent : BrightGold;
            if (bSel || bOver) CireUIStyle::Glow(P, TX, TY, TW, TH, Accent * FLinearColor(1, 1, 1, bSel ? .32f : .16f));
            CireUIStyle::Bevel(P, TX, TY, TW, TH, 9, bSel ? Accent * FLinearColor(.9f, .9f, .9f, 1) : Filigree * FLinearColor(.55f, .55f, .55f, 1));
            CireUIStyle::Bevel(P, TX + 1.5f, TY + 1.5f, TW - 3, TH - 3, 8.4f, bSel ? FLinearColor(Accent.R * .16f, Accent.G * .16f, Accent.B * .16f, .98f) : bOver ? Hover * 1.4f : FLinearColor(.035f, .035f, .04f, .96f));
            if (bSel) P.Rect(TX + 10, TY + TH - 4, TW - 20, 2, Accent);
            const float R = TH * .5f - 5, ICX = TX + 8 + R, ICY = TY + TH * .5f;
            P.Disc(ICX, ICY, R + 1.5f, bSel ? Accent : Filigree * .7f, 32);
            if (UTexture2D* Emblem = V ? CireVendors::Emblem(V->Id) : nullptr) P.TexDisc(Emblem, ICX, ICY, R, FLinearColor::White);
            else if (UTexture2D* Coin = FindItemIcon(TEXT("gold"))) P.TexDisc(Coin, ICX, ICY, R, FLinearColor::White);
            else P.Disc(ICX, ICY, R, FLinearColor(.2f, .15f, .05f, 1), 32);
            const float TX0 = ICX + R + 10, Room = TX + TW - TX0 - 8;
            const FString Name = V ? V->Name.ToUpper() : FString(TEXT("ALL MERCHANTS"));
            P.Text(P.Fit(Name, 11.f, Room, ECireFont::Heading), TX0, TY + 5, 11.f, bSel ? FLinearColor(1.f, .92f, .7f, 1) : bOver ? FLinearColor(1, .97f, .9f, 1) : Parchment * .92f, ECireFont::Heading, true, true);
            const FString Sub = V ? FString::Printf(TEXT("%s  ·  %s"), *V->StatLabel, *V->Stat) : FString::Printf(TEXT("%s  ·  EVERY WARE"), *KeyLabel(HUD, TEXT("ToggleShop")));
            CireShopArt::Spaced(P, P.Fit(Sub, 7.f, Room, ECireFont::Display), TX0, TY + 23, 7.f, .22f, Accent * FLinearColor(1, 1, 1, bSel ? 1.f : .75f), ECireFont::Display, false, false);
            if (bOver)
                CireShopUI::Tip(HUD, V ? V->Name : FString(TEXT("All merchants")), V ? FString::Printf(TEXT("%s.\n%s keeps this stall in town: walk up and press %s, or click him, to open this tab."), *V->Tagline, *V->Keeper, *KeyLabel(HUD, TEXT("Interact")))
                    : FString::Printf(TEXT("Every merchant's wares in one list. %s opens it from anywhere; during prep you can buy anywhere."), *KeyLabel(HUD, TEXT("ToggleShop"))));
            if (Click(TX, TY, TW, TH) && State.Vendor != VendorIndex) { State.Vendor = VendorIndex; if (VendorIndex >= 0) State.Category = 1; Play(HUD, TEXT("S_ShopTab"), .4f); }
        }
    }
    const float Top = Y + 84 + 50, StripH = 88, Bottom = Y + H - StripH - 10;
    // readability: left column = category tabs with painted icons, then stat filters, each with the painted
    // icon of an item that carries the stat (the ChatGPT item art), in bevelled rows.
    const float LX = X + 22, LW = 176;
    CireShopArt::Spaced(P, TEXT("BROWSE"), LX + LW * .5f, Top - 4, 8.f, .4f, Filigree, ECireFont::Display, true, false);
    const TCHAR* Categories[] = {TEXT("RECOMMENDED"), TEXT("ALL ITEMS")};
    const TCHAR* CategoryHelp[] = {TEXT("The build for your role: starter, core and situational items in buying order."), TEXT("Every item by tier. Filters below narrow it by stat.")};
    const FName RoleCore = [&]() { if (const auto* Lists = D.Recommended.Find(RoleKey); Lists && Lists->Num() > 1 && (*Lists)[1].Num()) return (*Lists)[1][0]; return FName(TEXT("nightfall_reaver")); }();
    const FName CategoryIcons[] = {RoleCore, FName(TEXT("gold"))};
    auto IconTab = [&](float TX, float TY, float TW, float TH, const FString& Label, UTexture2D* Icon, bool bSelected, FLinearColor Accent, float Size) -> bool
    {
        const bool bOver = bInteractive && In(M, TX, TY, TW, TH);
        const float C = FMath::Min(8.f, TH * .3f);
        if (bSelected || bOver) CireUIStyle::Glow(P, TX, TY, TW, TH, Accent * FLinearColor(1, 1, 1, bSelected ? .3f : .16f));
        CireUIStyle::Bevel(P, TX, TY, TW, TH, C, bSelected ? Accent * FLinearColor(.9f, .9f, .9f, 1) : Filigree * FLinearColor(.55f, .55f, .55f, 1));
        CireUIStyle::Bevel(P, TX + 1.5f, TY + 1.5f, TW - 3, TH - 3, C - .6f, bSelected ? FLinearColor(.16f, .12f, .05f, .98f) : bOver ? Hover * 1.4f : FLinearColor(.035f, .035f, .04f, .96f));
        if (bSelected) P.Rect(TX + C, TY + TH - 4, TW - 2 * C, 2, Accent);
        const float IS = TH - 8;
        if (Icon) { P.Rect(TX + 5, TY + 4, IS, IS, FLinearColor(0, 0, 0, .8f)); P.Tex(Icon, TX + 6, TY + 5, IS - 2, IS - 2, FLinearColor::White); }
        else CireShopArt::Diamond(P, TX + 5 + IS * .5f, TY + TH * .5f, IS * .28f, bSelected ? BrightGold : Filigree, bSelected);
        const float LXt = TX + IS + 12;
        const FString Fitted = P.Fit(Label, Size, TW - (LXt - TX) - 6, ECireFont::Heading);
        P.Text(Fitted, LXt, TY + (TH - CireUIStyle::ReadableSize(Size) * 1.28f) * .5f, Size, bSelected ? FLinearColor(1.f, .9f, .6f, 1) : bOver ? FLinearColor(1, .97f, .9f, 1) : Parchment * .9f, ECireFont::Heading, true, true);
        return bOver;
    };
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const float BY = Top + 12 + Index * 40;
        if (IconTab(LX, BY, LW, 34, Categories[Index], FindItemIcon(CategoryIcons[Index]), State.Category == Index, BrightGold, 10.5f))
            CireShopUI::Tip(HUD, Categories[Index], CategoryHelp[Index]);
        if (Click(LX, BY, LW, 34)) { State.Category = Index; if (Index == 0) State.Vendor = -1; Play(HUD, TEXT("S_ShopTab"), .4f); }
    }
    CireShopArt::Rule(P, LX + 6, LX + LW - 6, Top + 98, Filigree * FLinearColor(1, 1, 1, .5f));
    CireShopArt::Spaced(P, TEXT("FILTER BY STAT"), LX + LW * .5f, Top + 104, 7.5f, .34f, Filigree, ECireFont::Display, true, false);
    static TMap<FName, FName> FilterIcon; // tag -> first catalogue item that has it and a painted icon
    const float RowH = FMath::Min(26.f, (Bottom - (Top + 122) - 30) / FilterCount);
    for (int32 Index = 0; Index < FilterCount; ++Index)
    {
        const float BY = Top + 122 + Index * RowH;
        const bool bOn = (State.Filters & (1u << Index)) != 0;
        const FName Tag(FilterTags[Index]);
        if (!FilterIcon.Contains(Tag))
        {
            FName Found;
            for (const FName Id : D.Order) if (const CI::ItemDef* Item = CireItems::Find(Id); Item && Item->HasTag(Utf8(Tag)) && FindItemIcon(Id)) { Found = Id; break; }
            FilterIcon.Add(Tag, Found);
        }
        IconTab(LX, BY, LW, RowH - 3, FilterNames[Index], FindItemIcon(FilterIcon[Tag]), bOn, Gold, 10.5f);
        if (Click(LX, BY, LW, RowH - 3)) { State.Filters ^= 1u << Index; State.Category = 1; Play(HUD, TEXT("S_ShopTab"), .3f); }
    }
    if (State.Filters != 0)
    {
        const float BY = Top + 122 + FilterCount * RowH + 3;
        const bool bOver = In(M, LX, BY, LW, 24);
        ShopButton(P, LX, BY, LW, 24, TEXT("CLEAR FILTERS"), bOver, false, false, Muted, 9);
        if (Click(LX, BY, LW, 24)) State.Filters = 0;
    }

    // Centre: bevelled item cards with rarity glows; recommended build first (starter -> core -> situational).
    const float DWd = 372, DX = X + W - 22 - DWd;
    const float GX = LX + LW + 18, GW = DX - 16 - GX, GY = Top;
    CireShopArt::Divider(P, GX - 9, Top - 4, Bottom, Filigree * FLinearColor(1, 1, 1, .45f));
    CireShopArt::Divider(P, DX - 8, Top - 4, Bottom, Filigree * FLinearColor(1, 1, 1, .45f));
    FName HoverId;
    float CY = GY;
    const CI::Inventory Rules = Hero->Inventory->ToRules();
    auto Matches = [&](const CI::ItemDef& Item)
    {
        if (!ShownVendorId.IsNone() && !CireVendors::Sells(ShownVendorId, ToName(Item.Id))) return false; // vendors
        if (State.Filters == 0) return true;
        for (int32 Index = 0; Index < FilterCount; ++Index)
            if ((State.Filters & (1u << Index)) && Item.HasTag(Utf8(FName(FilterTags[Index])))) return true;
        return false;
    };
    // One item card: bevelled rarity card, painted icon, optional name, price plate; hover lifts it.
    auto DrawCard = [&](FName Id, float IX, float IY, float CW, float CH, bool bNamed)
    {
        const CI::ItemDef* Item = CireItems::Find(Id);
        if (!Item) return;
        const bool bOver = bInteractive && In(M, IX, IY, CW, CH);
        const int32 Price = PriceFor(Hero, Id);
        const bool bAffordable = Hero->Gold >= Price;
        const bool bOwned = Rules.CountOf(Item->Id) > 0;
        const bool bPath = IsPathUnique(*Item);
        const FLinearColor TC = bPath ? FLinearColor(1.f, .62f, .22f, 1) : TierColor(static_cast<int32>(Item->Tier));
        const float Lift = bOver ? 3.f : 0.f;
        const float CX0 = IX + ShakeOffset(Id), CY0 = IY - Lift;
        CireUIStyle::BevelCard(P, CX0, CY0, CW, CH, TC, bOver, State.Selected == Id, !bAffordable);
        const float PS = bNamed ? 12.f : 9.5f, PEff = CireUIStyle::ReadableSize(PS), PlateH = PEff * 1.28f + 2;
        const float S = bNamed ? FMath::Min(CW - 26, 50.f) : FMath::Max(20.f, FMath::Min3(CW - 10, 46.f, CH - PlateH - 6));
        DrawItemIcon(P, Id, CX0 + (CW - S) * .5f, CY0 + 6, S, bOver, 0, 0, 0, FString(), !bAffordable);
        if (ShakeOffset(Id) != 0.f) CireUIStyle::BevelOutline(P, CX0 - 2, CY0 - 2, CW + 4, CH + 4, 9.f, FLinearColor(1.f, .25f, .2f, 1), 2.5f);
        if (bOwned)
        {
            const float OX = CX0 + CW - 9, OY = CY0 + 9;
            P.Disc(OX, OY, 7.f, FLinearColor(0, 0, 0, .8f), 14); P.Disc(OX, OY, 6.f, FLinearColor(.1f, .5f, .2f, 1), 14);
            P.Line(OX - 3, OY, OX, OY + 3, FLinearColor::White, 1.6f); P.Line(OX, OY + 3, OX + 4, OY - 4, FLinearColor::White, 1.6f);
        }
        if (!bPath && ShownVendorId.IsNone()) // vendors: which merchant sells it (every merchant = no badge)
        {
            const FName Seller = CireVendors::VendorOf(Id);
            if (UTexture2D* Emblem = Seller.IsNone() ? nullptr : CireVendors::Emblem(Seller))
            {
                const float BR = FMath::Clamp(CW * .13f, 6.f, 9.f), BX = CX0 + BR + 3, BY = CY0 + BR + 3;
                const FCireVendorDef* SV = CireVendors::Find(Seller);
                P.Disc(BX, BY, BR + 1.2f, SV ? SV->Accent : Filigree, 20);
                P.TexDisc(Emblem, BX, BY, BR, FLinearColor::White, 0, 0, 1, 1, 20);
            }
        }
        if (bPath) // items-v2: path-defining unique ribbon
        {
            const FLinearColor PathGold(1.f, .62f, .22f, 1);
            const float RW = P.TextWidth(TEXT("PATH"), 7.5f, ECireFont::Heading) + 8;
            CireUIStyle::Bevel(P, CX0 + 3, CY0 + 3, RW, 13, 3, FLinearColor(.3f, .12f, .02f, .95f));
            P.Text(TEXT("PATH"), CX0 + 7, CY0 + 2.5f, 7.5f, PathGold, ECireFont::Heading, true, false);
        }
        if (bNamed) { const int32 NameLines = FMath::Clamp(FMath::FloorToInt((CH - S - 11 - PlateH - 6) / (CireUIStyle::ReadableSize(9.5f) * 1.12f)), 1, 2); CentredWrap(P, CireItems::DisplayName(Id), CX0 + CW * .5f, CY0 + S + 10, CW - 10, 9.5f, FMath::Lerp(TC, FLinearColor::White, .2f), ECireFont::Bold, NameLines, 0.f); }
        const FString Cost = FString::Printf(TEXT("%dg"), Price);
        const FLinearColor CostColor = bAffordable ? (Price < Item->TotalCost ? FLinearColor(.55f, 1.f, .5f, 1) : BrightGold) : FLinearColor(1.f, .42f, .38f, 1);
        CireUIStyle::Bevel(P, CX0 + 4, CY0 + CH - PlateH - 4, CW - 8, PlateH, 4.f, FLinearColor(0, 0, 0, .62f));
        P.Text(Cost, CX0 + (CW - P.TextWidth(Cost, PS, ECireFont::Numbers)) * .5f, CY0 + CH - PlateH - 3, PS, CostColor, ECireFont::Numbers, true, false);
        State.GridPos.Add(Id, FVector2D(CX0 + (CW - S) * .5f, CY0 + 7));
        if (bOver) HoverId = Id;
        if (bOver && HUD.HasClick())
        {
            HUD.TakeClick();
            const double T = FPlatformTime::Seconds();
            if (State.LastClickId == Id && T - State.LastClickTime < .35) RequestBuy(HUD, Hero, GameState, Id, FVector2D(CX0 + (CW - S) * .5f, CY0 + 7));
            State.Selected = Id; State.SelectedSlot = -1; State.LastClickId = Id; State.LastClickTime = T;
            Play(HUD, TEXT("S_ShopTab"), .3f);
        }
        if (bOver && bRightClick) { State.Selected = Id; State.SelectedSlot = -1; RequestBuy(HUD, Hero, GameState, Id, FVector2D(CX0 + (CW - S) * .5f, CY0 + 7)); }
    };
    if (State.Category == 0)
    {
        CireShopArt::Spaced(P, FString::Printf(TEXT("RECOMMENDED BUILD  ·  %s"), *RoleCaption), GX + GW * .5f, CY - 4, 9.f, .3f, FLinearColor(.95f, .83f, .58f, 1), ECireFont::Display, true, false);
        CY += 18;
        if (const auto* Lists = D.Recommended.Find(RoleKey))
        {
            const TCHAR* Names[] = {TEXT("I  ·  STARTER"), TEXT("II  ·  CORE BUILD"), TEXT("III  ·  SITUATIONAL")};
            const TCHAR* Hints[] = {TEXT("buy these first"), TEXT("build them in this order"), TEXT("when the match asks for it")};
            const FLinearColor Colors[] = {FLinearColor(.5f, .9f, .55f, 1), Orange, Silver};
            const float CW = 96, Gap = 20;
            const int32 PerRow = FMath::Max(1, FMath::FloorToInt((GW + Gap) / (CW + Gap)));
            int32 RowsNeeded = 0; for (int32 Index = 0; Index < Lists->Num() && Index < 3; ++Index) RowsNeeded += FMath::DivideAndRoundUp((*Lists)[Index].Num(), PerRow);
            const float CH = FMath::Clamp((Bottom - CY - 40.f - FMath::Min(3, Lists->Num()) * 24.f) / FMath::Max(1, RowsNeeded) - 8.f, 96.f, 126.f);
            for (int32 Index = 0; Index < Lists->Num() && Index < 3; ++Index)
            {
                const TArray<FName>& Ids = (*Lists)[Index];
                const float NW = CireShopArt::SpacedWidth(P, Names[Index], 8.5f, .3f);
                CireShopArt::Spaced(P, Names[Index], GX + 4, CY, 8.5f, .3f, Colors[Index], ECireFont::Display, false, false);
                P.Text(Hints[Index], GX + 20 + NW, CY - 1, 10, Muted * 1.4f, ECireFont::Body);
                CireShopArt::Rule(P, GX + 28 + NW + P.TextWidth(Hints[Index], 10, ECireFont::Body), GX + GW - 4, CY + 7, Colors[Index] * FLinearColor(1, 1, 1, .5f));
                CY += 22;
                for (int32 Item = 0; Item < Ids.Num(); ++Item)
                {
                    const float IX = GX + 4 + (Item % PerRow) * (CW + Gap), IY = CY + (Item / PerRow) * (CH + 8);
                    DrawCard(Ids[Item], IX, IY, CW, CH, true);
                    if (Item + 1 < Ids.Num() && (Item + 1) % PerRow != 0)
                    {
                        // Build-order chevron between consecutive cards.
                        const float AX = IX + CW + Gap * .5f, AY = IY + CH * .42f;
                        P.Line(AX - 4, AY - 5, AX + 2, AY, Colors[Index], 2.f); P.Line(AX - 4, AY + 5, AX + 2, AY, Colors[Index], 2.f);
                        CireShopArt::Diamond(P, AX - 7, AY, 1.8f, Colors[Index]);
                    }
                }
                CY += FMath::DivideAndRoundUp(Ids.Num(), PerRow) * (CH + 8) + 4;
            }
        }
        if (CY + 30 < Bottom)
            P.Wrapped(TEXT("Every item is still available under ALL ITEMS. Items build from components: owned parts are consumed and discounted (green price)."),
                GX + 6, CY, GW - 12, 10, Muted * 1.4f, 2);
    }
    else
    {
        TArray<FName> Tiers[5];
        for (const FName Id : D.Order)
            if (const CI::ItemDef* Item = CireItems::Find(Id); Item && Item->Purchasable && Matches(*Item)) Tiers[IsPathUnique(*Item) ? 4 : static_cast<int32>(Item->Tier)].Add(Id);
        const TCHAR* Names[] = {TEXT("CONSUMABLES & TOMES"), TEXT("BASIC COMPONENTS"), TEXT("EPIC COMPONENTS"), TEXT("LEGENDARY"), TEXT("PATH-DEFINING UNIQUES  ·  ONE PER CHAMPION")};
        // readability: solve the card size so every tier AND a recommended-by-role row fit the column (no scrolling,
        // no pagination, no spill into the bag strip): the widest card whose height stays near square wins.
        const float Gap = 6;
        TArray<FName> RoleItems;
        if (State.Filters == 0)
            if (const auto* Lists = D.Recommended.Find(RoleKey))
                for (int32 List = 0; List < Lists->Num() && List < 2; ++List)
                    for (const FName Id : (*Lists)[List]) if (ShownVendorId.IsNone() || CireVendors::Sells(ShownVendorId, Id)) RoleItems.AddUnique(Id);
        float CW = 46, CH = 50; int32 PerRow = 1, Rows = 0, Sections = 0; bool bRecRow = false;
        for (int32 Pass = 0; Pass < 2; ++Pass) // pass 0 with the recommended row, pass 1 without
        {
            bool bFound = false;
            for (float TryW = 64; TryW >= 44; TryW -= 2)
            {
                const int32 N = FMath::Max(1, FMath::FloorToInt((GW + Gap) / (TryW + Gap)));
                int32 R = 0, Sec = 0;
                for (int32 Tier = 0; Tier < 5; ++Tier) if (Tiers[Tier].Num() > 0) { R += FMath::DivideAndRoundUp(Tiers[Tier].Num(), N); ++Sec; }
                const bool bRec = Pass == 0 && RoleItems.Num() > 0;
                const float Avail = Bottom - CY - Sec * 20.f - (bRec ? 26.f : 0.f);
                const float TryH = Avail / FMath::Max(1, R + (bRec ? 1 : 0)) - Gap;
                if (TryH >= TryW * .92f || TryW <= 44) { CW = TryW; CH = FMath::Min(TryH, TryW * 1.25f); PerRow = N; Rows = R; Sections = Sec; bRecRow = bRec; bFound = TryH >= 40.f; break; }
            }
            if (bFound) break;
        }
        TArray<FName> Recommended;
        for (const FName Id : RoleItems) if (Recommended.Num() < PerRow) Recommended.Add(Id);
        const float RecH = CH;
        if (bRecRow)
        {
            const FString Caption = FString::Printf(TEXT("RECOMMENDED FOR YOUR ROLE  ·  %s"), *RoleCaption);
            const float NW = CireShopArt::SpacedWidth(P, Caption, 8.f, .3f);
            CireShopArt::Spaced(P, Caption, GX + 4, CY - 2, 8.f, .3f, BrightGold, ECireFont::Display, false, false);
            CireShopArt::Rule(P, GX + 14 + NW, GX + GW - 4, CY + 5, BrightGold * FLinearColor(1, 1, 1, .45f));
            CY += 18;
            for (int32 Index = 0; Index < Recommended.Num(); ++Index) DrawCard(Recommended[Index], GX + 2 + Index * (CW + Gap), CY, CW, RecH, false);
            CY += RecH + 8;
        }
        for (int32 Tier : {0, 1, 2, 4, 3})
        {
            if (Tiers[Tier].Num() == 0) continue;
            const FLinearColor TC = Tier == 4 ? FLinearColor(1.f, .62f, .22f, 1) : TierColor(Tier);
            const float NW = CireShopArt::SpacedWidth(P, Names[Tier], 8.f, .3f);
            CireShopArt::Spaced(P, Names[Tier], GX + 4, CY - 2, 8.f, .3f, TC, ECireFont::Display, false, false);
            CireShopArt::Rule(P, GX + 14 + NW, GX + GW - 4, CY + 5, TC * FLinearColor(1, 1, 1, .45f));
            CY += 16;
            for (int32 Index = 0; Index < Tiers[Tier].Num(); ++Index)
                DrawCard(Tiers[Tier][Index], GX + 2 + (Index % PerRow) * (CW + Gap), CY + (Index / PerRow) * (CH + Gap), CW, CH, false);
            CY += FMath::DivideAndRoundUp(Tiers[Tier].Num(), PerRow) * (CH + Gap) + 4;
        }
        if (Tiers[0].Num() + Tiers[1].Num() + Tiers[2].Num() + Tiers[3].Num() + Tiers[4].Num() == 0) P.Text(TEXT("No item matches every filter."), GX + 8, CY, 12, Muted);
    }
    if (!HoverId.IsNone()) TipItem(HUD, HoverId, PriceFor(Hero, HoverId), TEXT("Click to inspect  ·  double or right-click to buy"));

    // Right: selected item detail with its build path.
    const float DY = Top, DHt = Bottom - Top;
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
        const bool bPathItem = IsPathUnique(*Item);
        const FLinearColor Tier = bPathItem ? FLinearColor(1.f, .62f, .22f, 1) : TierColor(static_cast<int32>(Item->Tier));
        const int32 Count = static_cast<int32>(Item->Components.size());
        // readability: the recipe tree panel (components -> completed): the finished item on top in a rarity card,
        // its components below with their price or an OWNED check, their own parts under them, arrows upward.
        const float PanelH = Count > 0 ? 196.f : 92.f;
        CireUIStyle::Frame(P, DX - 2, DY - 2, DWd + 4, PanelH, Gold, ECireFrame::Inset);
        CireShopArt::Spaced(P, TEXT("RECIPE"), DX + 8, DY + 2, 7.5f, .34f, Filigree, ECireFont::Display, false, false);
        {
            const FString Flow = Count > 0 ? TEXT("COMPONENTS  »  COMPLETED") : TEXT("BASE COMPONENT");
            const float FW = CireShopArt::SpacedWidth(P, Flow, 7.f, .25f);
            CireShopArt::Spaced(P, Flow, DX + DWd - 8 - FW, DY + 2.5f, 7.f, .25f, Muted * 1.4f, ECireFont::Display, false, false);
        }
        const float RootS = 48, TreeY = DY + 22;
        const float RootX = DX + DWd * .5f - RootS * .5f;
        if (Item->Purchasable) // vendors: who sells it
        {
            const FName Seller = CireVendors::VendorOf(Shown);
            const FCireVendorDef* SV = CireVendors::Find(Seller);
            const float ER = 13, ECX = DX + 10 + ER, ECY = TreeY + 20;
            P.Disc(ECX, ECY, ER + 1.5f, SV ? SV->Accent : Filigree, 28);
            if (UTexture2D* Emblem = SV ? CireVendors::Emblem(Seller) : FindItemIcon(TEXT("gold"))) P.TexDisc(Emblem, ECX, ECY, ER, FLinearColor::White);
            const float TX0 = ECX + ER + 6, Room = RootX - 12 - TX0;
            CireShopArt::Spaced(P, TEXT("SOLD BY"), TX0, ECY - 13, 6.5f, .3f, Muted * 1.5f, ECireFont::Display, false, false);
            P.Text(P.Fit(SV ? SV->Name : FString(TEXT("Every merchant")), 9.5f, Room, ECireFont::Bold), TX0, ECY - 3, 9.5f, SV ? FMath::Lerp(SV->Accent, FLinearColor::White, .35f) : Parchment, ECireFont::Bold, true, false);
        }
        State.DetailIconPos = FVector2D(RootX, TreeY);
        CireUIStyle::BevelCard(P, RootX - 7, TreeY - 5, RootS + 14, RootS + 10, Tier, false, true, false);
        DrawItemIcon(P, Shown, RootX + ShakeOffset(Shown), TreeY, RootS, false);
        if (Count == 0)
        {
            const FString Note = Item->Purchasable ? TEXT("Buy it as it is, or keep it for a recipe below.") : TEXT("Loot only.");
            P.Text(Note, DX + (DWd - P.TextWidth(Note, 9.5f, ECireFont::Body)) * .5f, TreeY + RootS + 8, 9.5f, Muted * 1.4f, ECireFont::Body, false, true);
        }
        const float ChildS = 38, ChildStep = Count > 0 ? FMath::Min(100.f, (DWd - 30) / Count) : 0;
        TArray<FName> Owned;
        for (const auto& Cell : Hero->Inventory->Equipment) if (!Cell.Id.IsNone()) Owned.Add(Cell.Id);
        const float Bus = TreeY + RootS + 13, CYc = TreeY + RootS + 24;
        if (Count > 0)
        {
            // Arrow into the finished item.
            const float AX = RootX + RootS * .5f;
            P.Line(AX, Bus, AX, TreeY + RootS + 6, Filigree * .85f, 2.f);
            P.Tri(FVector2D(AX - 5, TreeY + RootS + 9), FVector2D(AX + 5, TreeY + RootS + 9), FVector2D(AX, TreeY + RootS + 4), Filigree);
        }
        for (int32 Index = 0; Index < Count; ++Index)
        {
            const FName Child = ToName(Item->Components[Index]);
            const float CX = DX + DWd * .5f + (Index - (Count - 1) * .5f) * ChildStep - ChildS * .5f;
            P.Line(CX + ChildS * .5f, CYc, CX + ChildS * .5f, Bus, Filigree * .7f, 1.6f);
            P.Line(FMath::Min(CX + ChildS * .5f, RootX + RootS * .5f), Bus, FMath::Max(CX + ChildS * .5f, RootX + RootS * .5f), Bus, Filigree * .7f, 1.6f);
            const bool bHave = Owned.Contains(Child);
            if (bHave) Owned.RemoveSingle(Child);
            const bool bOver = bInteractive && In(M, CX, CYc, ChildS, ChildS);
            DrawItemIcon(P, Child, CX, CYc, ChildS, bOver, 0, 0, 0, FString(), !bHave);
            // Price or OWNED under the component.
            const FString Tag = bHave ? FString(TEXT("OWNED")) : FString::Printf(TEXT("%dg"), PriceFor(Hero, Child));
            const ECireFont TagFont = bHave ? ECireFont::Heading : ECireFont::Numbers;
            const float TW = P.TextWidth(Tag, 8.5f, TagFont);
            P.Text(Tag, CX + (ChildS - TW) * .5f, CYc + ChildS + 1, 8.5f, bHave ? FLinearColor(.45f, 1.f, .5f, 1) : BrightGold, TagFont, true, false);
            if (bOver) { TipItem(HUD, Child, PriceFor(Hero, Child), bHave ? TEXT("Owned: consumed by the recipe.") : TEXT("Click to inspect this component.")); if (HUD.HasClick()) { HUD.TakeClick(); State.Selected = Child; State.SelectedSlot = -1; } }
            if (const CI::ItemDef* Sub = CireItems::Find(Child))
            {
                const int32 SubCount = static_cast<int32>(Sub->Components.size());
                for (int32 S = 0; S < SubCount; ++S)
                {
                    const FName Grand = ToName(Sub->Components[S]);
                    const float GXs = CX + ChildS * .5f + (S - (SubCount - 1) * .5f) * 30 - 13, GYs = CYc + ChildS + 20;
                    P.Line(CX + ChildS * .5f, CYc + ChildS + 15, GXs + 13, GYs, Filigree * .45f, 1.f);
                    const bool bGrand = Owned.Contains(Grand) || bHave;
                    if (Owned.Contains(Grand) && !bHave) Owned.RemoveSingle(Grand);
                    const bool bOverGrand = bInteractive && In(M, GXs, GYs, 26, 26);
                    DrawItemIcon(P, Grand, GXs, GYs, 26, bOverGrand, 0, 0, 0, FString(), !bGrand);
                    if (bOverGrand) { TipItem(HUD, Grand, PriceFor(Hero, Grand), TEXT("Click to inspect this component.")); if (HUD.HasClick()) { HUD.TakeClick(); State.Selected = Grand; State.SelectedSlot = -1; } }
                }
            }
        }
        // Text: name, tier + cost, your price, effect, symbol stat lines, passive / active sections, lore.
        const std::vector<std::string> Into = D.Catalog.BuildsInto(Item->Id);
        const float ButtonY = DY + DHt - 40, StripY = ButtonY - 24, IntoY = StripY - 54;
        const float TextLimit = Into.empty() ? StripY - 4 : IntoY - 4;
        float TY = DY + PanelH + 6;
        auto Step = [](float Size) { return CireUIStyle::ReadableSize(Size) * 1.22f + 1.f; };
        auto Room = [&](float Need) { return TY + Need <= TextLimit; };
        P.Text(P.Fit(CireItems::DisplayName(Shown), 17, DWd - 20, ECireFont::Bold), DX + 10, TY, 17, FMath::Lerp(Tier, FLinearColor::White, .15f), ECireFont::Bold, true, true);
        TY += Step(17);
        const int32 Price = PriceFor(Hero, Shown);
        FString CostLine = FString::Printf(TEXT("%s  ·  COST %dg"), bPathItem ? TEXT("PATH UNIQUE") : *FString(UTF8_TO_TCHAR(CI::TierName(Item->Tier))).ToUpper(), Item->TotalCost);
        if (Count > 0) CostLine += FString::Printf(TEXT("  ·  RECIPE %dg"), Item->RecipeCost);
        CireShopArt::Spaced(P, CostLine, DX + 10, TY, 7.5f, .2f, Filigree, ECireFont::Display, false, false);
        TY += Step(7.5f) + 2;
        if (Price != Item->TotalCost && Item->Purchasable && Room(Step(11.5f))) { P.Text(FString::Printf(TEXT("Your price: %dg (owned parts count)"), Price), DX + 10, TY, 11.5f, FLinearColor(.55f, 1.f, .5f, 1), ECireFont::Bold, true, true); TY += Step(11.5f); }
        TArray<FString> Lines;
        StatLines(Shown).ParseIntoArrayLines(Lines);
        for (const FString& Line : Lines) { if (!Room(Step(12.5f))) break; P.Text(Line, DX + 10, TY, 12.5f, CireUIStyle::StatColor(Line), ECireFont::Bold, true, true); TY += Step(12.5f); }
        if (const FString* Effect = D.EffectLine.Find(Shown); Effect && Room(Step(11))) // items-v2: the one-line effect
        { TY += 2; TY += P.Wrapped(*Effect, DX + 10, TY, DWd - 20, 11, bPathItem ? FLinearColor(1.f, .72f, .35f, 1) : FLinearColor(1.f, .86f, .5f, 1), FMath::Max(1, FMath::FloorToInt((TextLimit - TY) / (CireUIStyle::ReadableSize(11) + 3))), ECireFont::Body, 3.f) * (CireUIStyle::ReadableSize(11) + 3); }
        auto Section = [&](const TCHAR* Header, FLinearColor Color, const FString& Body)
        {
            if (!Room(Step(7.5f) + Step(11))) return;
            CireShopArt::Spaced(P, Header, DX + 10, TY + 4, 7.5f, .3f, Color, ECireFont::Display, false, false);
            TY += Step(7.5f) + 4;
            const int32 MaxLines = FMath::Max(1, FMath::FloorToInt((TextLimit - TY) / (CireUIStyle::ReadableSize(11) + 3)));
            TY += P.Wrapped(Body, DX + 10, TY, DWd - 20, 11, Parchment, FMath::Min(3, MaxLines), ECireFont::Body, 3.f) * (CireUIStyle::ReadableSize(11) + 3);
        };
        if (const auto* Passives = D.PassiveText.Find(Shown))
            for (const FString& Line : *Passives) Section(TEXT("UNIQUE PASSIVE"), BrightGold, Line);
        if (const FString* Use = D.UseText.Find(Shown); Use && !Use->IsEmpty())
            Section(Item->Belt || Item->Instant ? TEXT("USE") : TEXT("ACTIVE  ·  CLICK THE BAG SLOT OR PRESS ITS KEY"), Teal, *Use);
        if ((Item->Unique || !Item->UniqueGroup.empty()) && Room(Step(10.5f)))
        { P.Text(bPathItem ? TEXT("Path-defining unique: one per champion.") : !Item->UniqueGroup.empty() ? TEXT("Only one pair of boots.") : TEXT("Unique: carry only one."),
            DX + 10, TY + 2, 10.5f, bPathItem ? FLinearColor(1.f, .62f, .22f, 1) : Muted * 1.4f, ECireFont::Body, false, true); TY += Step(10.5f); }
        if (!Item->Lore.empty() && Room(Step(10) * 2)) P.Wrapped(Str(Item->Lore), DX + 10, TY + 4, DWd - 20, 10, Muted * 1.3f, 2);
        if (!Into.empty())
        {
            CireShopArt::Spaced(P, TEXT("BUILDS INTO"), DX + 10, IntoY, 7.f, .3f, Filigree, ECireFont::Display, false, false);
            for (int32 Index = 0; Index < static_cast<int32>(Into.size()) && Index < 9; ++Index)
            {
                const FName Parent = ToName(Into[Index]);
                const float IX = DX + 10 + Index * 39, IY = IntoY + 16;
                const bool bOver = bInteractive && In(M, IX, IY, 34, 34);
                DrawItemIcon(P, Parent, IX, IY, 34, bOver);
                if (bOver) { TipItem(HUD, Parent, PriceFor(Hero, Parent), TEXT("Click to inspect what this builds into.")); if (HUD.HasClick()) { HUD.TakeClick(); State.Selected = Parent; State.SelectedSlot = -1; } }
            }
        }
        // Feedback strip: the last purchase / sale / undo / error, fading after a few seconds.
        {
            const float Age = static_cast<float>(Now() - State.LastEventAt);
            if (!State.LastEvent.IsEmpty() && Age >= 0 && Age < 4.f)
            {
                FCireUIPainter Q = P; Q.Alpha *= FMath::Clamp((4.f - Age) / .6f, 0.f, 1.f) * FMath::Clamp(Age / .12f, 0.f, 1.f);
                CireUIStyle::Bevel(Q, DX + 8, StripY, DWd - 16, 21, 5, State.LastEventColor * FLinearColor(.28f, .28f, .28f, .9f));
                CireUIStyle::BevelOutline(Q, DX + 8, StripY, DWd - 16, 21, 5, State.LastEventColor * FLinearColor(1, 1, 1, .85f), 1.2f);
                const FString Msg = Q.Fit(State.LastEvent, 10, DWd - 30, ECireFont::Bold);
                Q.Text(Msg, DX + (DWd - Q.TextWidth(Msg, 10, ECireFont::Bold)) * .5f, StripY + (21 - CireUIStyle::ReadableSize(10) * 1.28f) * .5f, 10, FMath::Lerp(State.LastEventColor, FLinearColor::White, .25f), ECireFont::Bold, true, true);
            }
        }
        if (SellSlot >= 0)
        {
            const CI::Slot SlotRule = {Utf8(Shown), bSellBelt ? Hero->Inventory->Belt[SellSlot].Charges : 1, 0};
            const int32 Value = CI::SellValue(D.Catalog, SlotRule, D.Shop);
            const bool bOver = In(M, DX + 8, ButtonY, DWd - 16, 38);
            ShopButton(P, DX + 8 + ShakeOffset(NAME_None, SellSlot, bSellBelt), ButtonY, DWd - 16, 38, FString::Printf(TEXT("SELL  +%dg"), Value), bOver, false, false, Orange, 14);
            if (Click(DX + 8, ButtonY, DWd - 16, 38)) RequestSell(HUD, Hero, GameState, SellSlot, bSellBelt);
        }
        else if (Item->Purchasable)
        {
            const CI::PurchasePlan Plan = CI::PlanPurchase(D.Catalog, Rules, Item->Id, Hero->Gold);
            const bool bOver = In(M, DX + 8, ButtonY, DWd - 16, 38);
            const FString Label = Plan.Ok ? FString::Printf(TEXT("BUY  %dg"), Plan.Cost) : FString::Printf(TEXT("BUY  %dg  ·  %s"), Price, Plan.Error.find("gold") != std::string::npos ? TEXT("NEED GOLD") : TEXT("UNAVAILABLE"));
            ShopButton(P, DX + 8 + ShakeOffset(Shown), ButtonY, DWd - 16, 38, Label, bOver, false, !Plan.Ok, BrightGold, 14);
            if (Click(DX + 8, ButtonY, DWd - 16, 38)) RequestBuy(HUD, Hero, GameState, Shown, State.DetailIconPos);
        }
        else P.Text(TEXT("Loot only: drops from challenge chests and bosses."), DX + 10, ButtonY + 10, 11, Muted * 1.4f, ECireFont::Body, false, true);
    }

    // Bottom strip: bag + belt, undo, hotkeys.
    const float BandY = Y + H - StripH;
    CireShopArt::Rule(P, X + 190, X + W - 190, BandY - 4, Filigree * FLinearColor(1, 1, 1, .7f), true);
    const float BX = GX, BYs = BandY + 8;
    CireShopArt::Spaced(P, TEXT("YOUR BAG"), X + 26, BYs + 2, 7.5f, .34f, Filigree, ECireFont::Display, false, false);
    P.Text(FString::Printf(TEXT("%d / 6 slots"), 6 - Rules.FreeEquipment()), X + 26, BYs + 17, 11, Parchment, ECireFont::Body);
    CireShopArt::Spaced(P, TEXT("RIGHT-CLICK · SELL"), X + 26, BYs + 36, 6.5f, .3f, Muted * 1.2f, ECireFont::Display, false, false);
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
        TipItem(HUD, Cell.Id, -1, FString::Printf(TEXT("Sells for %dg  ·  right-click to sell"), CI::SellValue(D.Catalog, SlotRule, D.Shop)));
        if (HUD.HasClick()) { HUD.TakeClick(); State.SelectedSlot = SlotIndex; State.bSelectedBelt = bBelt; }
        if (bRightClick) RequestSell(HUD, Hero, GameState, SlotIndex, bBelt);
    }
    CireShopArt::Spaced(P, TEXT("BELT"), BX + 6 * 52 + 22, BYs - 11, 6.5f, .34f, Filigree, ECireFont::Display, false, false);
    {
        const float UX = BX + 6 * 52 + 22 + 3 * 46 + 12, UY = BYs + 8;
        const bool bCan = Hero->Inventory->UndoDepth > 0;
        const bool bOver = In(M, UX, UY, 118, 34);
        ShopButton(P, UX, UY, 118, 34, bCan ? FString::Printf(TEXT("UNDO (%d)"), Hero->Inventory->UndoDepth) : TEXT("UNDO"), bOver, false, !bCan, Teal, 11);
        if (Click(UX, UY, 118, 34)) { if (bCan) Hero->Inventory->ServerUndo(); else ShowError(HUD, NAME_None, -1, false, TEXT("Nothing to undo in this shop visit.")); }
        if (bOver) Tip(HUD, TEXT("Undo  [Ctrl+Z]"), TEXT("Reverts your last purchase or sale in this shop visit, refunding the full price. The visit ends when you close the shop, leave town, use an item, or the phase changes."));
    }
    const FString Keys[] = {TEXT("CLICK · SELECT"), TEXT("DOUBLE / RIGHT-CLICK · BUY"), FString::Printf(TEXT("CTRL+Z · UNDO   %s · CLOSE"), *KeyLabel(HUD, TEXT("ToggleShop")))};
    for (int32 Index = 0; Index < 3; ++Index)
    {
        const float KW = CireShopArt::SpacedWidth(P, Keys[Index], 6.5f, .3f);
        CireShopArt::Spaced(P, Keys[Index], X + W - 28 - KW, BYs + 4 + Index * 13, 6.5f, .3f, Muted * 1.2f, ECireFont::Display, false, false);
    }
    CireShopArt::Spaced(P, TEXT("FORTUNE FAVOURS THE PREPARED."), X + W * .5f, Y + H - 17, 6.5f, .45f, Filigree * .7f, ECireFont::Display, true, false);
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
        if (UTexture2D* Painted = (Toast.Icon == FName(TEXT("gold")) || Toast.Icon == FName(TEXT("teleport")) || Toast.Icon == FName(TEXT("challenge"))) ? FindItemIcon(Toast.Icon) : nullptr)
        {   // icon-art: painted coin / hearthstone / war horn (T_Item_gold, _teleport, _challenge)
            FCireIconSlot Slot; Slot.IconTexture = Painted; Slot.Tint = Toast.Accent;
            CireUIStyle::IconSlot(Q, X + 6, TY + 6, 34, Slot, T);
        }
        else if (Toast.Icon == FName(TEXT("gold"))) { Q.Disc(X + 23, TY + 23, 14, FLinearColor(.62f, .43f, .1f, 1), 20); Q.Disc(X + 23, TY + 22, 11.5f, FLinearColor(1.f, .8f, .28f, 1), 20); }
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

void CireShopUI::OpenVendor(ACireController* Controller, FName VendorId)
{
    if (!Controller) return;
    const int32 Index = CireVendors::IndexOf(VendorId);
    if (Controller->bShop) { State.Tab = 0; State.Vendor = Index; if (Index >= 0) State.Category = 1; return; }
    Controller->bShop = true; State.PendingTab = 0; State.PendingVendor = Index;
}

FName CireShopUI::CurrentVendor()
{
    const auto& Vendors = CireVendors::Get().Vendors;
    return Vendors.IsValidIndex(State.Vendor) ? Vendors[State.Vendor].Id : NAME_None;
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
void CireShopUI::DebugFilter(uint32 Mask) { State.Filters = Mask; if (Mask) State.Category = 1; }
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
void CireShopUI::DebugSkillFilter(int32 Filter) { State.SkillFilter = FMath::Clamp(Filter, 0, 1); } // new-champions
FVector2D CireShopUI::DebugSkillGridPos(const FString& SkillId) { const FVector2D* P = State.SkillGridPos.Find(SkillId); return P ? *P + FVector2D(23, 23) : FVector2D(-1, -1); }
void CireShopUI::DebugItemTab() { State.Tab = 0; State.PendingTab = 0; }
void CireShopUI::DebugVendor(FName VendorId) { State.Vendor = State.PendingVendor = CireVendors::IndexOf(VendorId); if (State.Vendor >= 0) State.Category = 1; }
void CireShopUI::DebugFreezeAfterStamp(float Age) { if (State.StampStart > 0) State.DebugNow = State.StampStart + Age; }
int32 CireShopUI::DebugTab() { return State.Tab; }
void CireShopUI::DebugMouse(FVector2D Logical) { VirtualPointer = Logical; }
FVector2D CireShopUI::DebugGridPos(FName ItemId) { const FVector2D* P = State.GridPos.Find(ItemId); return P ? *P + FVector2D(21, 21) : FVector2D(-1, -1); }
void CireShopUI::DebugReset() { const FName Keep = State.Selected; State = FShopState(); State.Selected = Keep; }
#endif
