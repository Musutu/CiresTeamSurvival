// progression-shop: development fixtures.
//  -CireShopGallery          offscreen 1920x1080 captures of the shop, buy/sell/error feedback,
//                            stats window, loot chest and Teleport to Base (Tools/RunProgressionChecks.py --only gallery)
//  -CireShopNetServer/Client dedicated server + remote client: server-enforced shop, replicated
//                            inventory/gold/feedback, undo, consumables and teleport (--only network)
//  -CireProgressionProbe     runs only the progression smoke suites and exits (--only native)
#include "CireShopFixtures.h"
#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireHUD.h"
#include "CireItems.h"
#include "CireLoot.h"
#include "CireShopUI.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireShopFixtures, Log, All);

namespace
{
enum class EFixture : uint8 { None, Gallery, NetServer, Probe };
struct FGallery
{
    EFixture Kind = EFixture::None;
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACireHUD> HUD;
    TWeakObjectPtr<ACireController> PC;
    TWeakObjectPtr<ACireHero> Hero;
    TWeakObjectPtr<ACireLootDrop> Chest;
    FString Directory;
    TArray<FString> Files;
    double Start = 0, Ready = -1, StageStart = 0;
    int32 Stage = -1;
    bool bCaptured = false, bDone = false, bPass = true;
    // network
    int32 NetStep = 0;
    double NetStepAt = 0;
};
FGallery G;

void Finish(bool bPass, const TCHAR* Marker)
{
    if (G.bDone) return;
    G.bDone = true;
    UE_LOG(LogCireShopFixtures, Display, TEXT("%s_%s captures=%d directory=%s"), Marker, bPass ? TEXT("PASS") : TEXT("FAIL"), G.Files.Num(), *G.Directory);
    FPlatformMisc::RequestExitWithStatus(false, bPass ? 0 : 1);
}

UCireInventory* Inv() { return G.Hero.IsValid() ? G.Hero->Inventory.Get() : nullptr; }

void Give(FName Id, int32 Slot, bool bBelt = false, int32 Charges = 1)
{
    if (UCireInventory* I = Inv())
    {
        auto& Cells = bBelt ? I->Belt : I->Equipment;
        Cells[Slot].Id = Id; Cells[Slot].Charges = Charges; Cells[Slot].ReadyAt = 0;
        I->Invalidate();
    }
}

void SetPhase(ACireGameMode* Mode, int32 Phase)
{
    Mode->Clock = Cires::MatchClock();
    if (Phase == 1) Mode->Clock.BeginIntermission();
    if (auto* S = Mode->GetGameState<ACireGameState>()) { S->Phase = Phase; S->SecondsLeft = Phase == 1 ? 42.f : -1.f; }
}

bool SetupGallery(ACireGameMode* Mode, ACireController* PC, ACireHUD* HUD)
{
    G.PC = PC; G.HUD = HUD; G.Hero = Cast<ACireHero>(PC->GetPawn());
    if (!G.Hero.IsValid() || !Inv()) return false;
    HUD->UISettings.Load(FPaths::Combine(G.Directory, TEXT("preview-profile.ini")));
    HUD->UISettings.bShowStats = true;
    HUD->UISettings.TooltipDelay = 0.f;
    ACireHero* H = G.Hero.Get();
    H->Draft(0); H->Offers.Reset(); H->bBot = false; H->bAutoAttack = false;
    H->Progression.Level = 9; H->Progression.Stats = {30, 18, 18};
    H->Skills = {TEXT("shield_slam"), TEXT("war_cry"), TEXT("iron_guard"), TEXT("stone_skin")};
    H->Cooldowns.Init(0, H->Skills.Num());
    SetPhase(Mode, 1);
    // A believable mid-game bag: parts of a Nightfall Reaver, boots, potions and lanterns.
    Give(TEXT("serrated_cleaver"), 0); Give(TEXT("bloodstone_shard"), 1); Give(TEXT("road_worn_boots"), 2); Give(TEXT("vampire_fang"), 3);
    Give(TEXT("vial_of_crimson"), 0, true, 3); Give(TEXT("watchers_lantern"), 1, true, 2);
    Inv()->Buffs.Reset();
    Inv()->BeginShopVisit(true);
    H->Recalculate(true);
    H->Gold = 1275;
    PC->bShop = true;
    CireShopUI::DebugReset();
    CireShopUI::DebugSelect(TEXT("nightfall_reaver"), 1);
    G.Ready = FPlatformTime::Seconds();
    return true;
}

void Capture(const TCHAR* Name)
{
    const FString File = FPaths::Combine(G.Directory, FString::Printf(TEXT("%02d_%s.png"), G.Files.Num() + 1, Name));
    FScreenshotRequest::RequestScreenshot(File, false, false, false, FIntRect(), true);
    G.Files.Add(File);
    UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_GALLERY_CAPTURE %s"), *File);
}

// Each stage: prepare at entry, capture after Delay seconds, move on after Delay + .6 s.
struct FStage { const TCHAR* Name; float Delay; };
const FStage Stages[] = {
    {TEXT("shop_all_items_hover"), 2.2f}, {TEXT("shop_recommended"), 1.2f}, {TEXT("shop_buy_feedback"), .24f},
    {TEXT("shop_error_shake"), .12f}, {TEXT("shop_sell_feedback"), .3f}, {TEXT("hud_stats_window_hover"), 1.2f},
    {TEXT("loot_chest_drop"), 1.6f}, {TEXT("loot_chest_opened"), .9f}, {TEXT("teleport_channel"), 2.2f}, {TEXT("teleport_cooldown"), 1.0f}};
constexpr int32 StageCount = UE_ARRAY_COUNT(Stages);

void EnterStage(ACireGameMode* Mode, int32 Stage)
{
    ACireHero* H = G.Hero.Get();
    UCireInventory* I = Inv();
    ACireHUD* HUD = G.HUD.Get();
    ACireController* PC = G.PC.Get();
    FString Message;
    HUD->DebugTooltipClear();
    CireShopUI::DebugMouse(FVector2D(-1, -1));
    CireShopUI::DebugHoverStat(-1);
    switch (Stage)
    {
    case 0: CireShopUI::DebugSelect(TEXT("nightfall_reaver"), 1); break;
    case 1: CireShopUI::DebugSelect(TEXT("greaves_of_the_undying"), 0); break;
    case 2: // real purchase through the server path: recipe consumes the cleaver
        CireShopUI::DebugSelect(TEXT("nightfall_reaver"), 1);
        I->Buy(TEXT("ravens_eye"), Message);
        I->Buy(TEXT("nightfall_reaver"), Message);
        break;
    case 3: // unaffordable legendary -> server rejects with the reason, icon shakes
        H->Gold = 90;
        CireShopUI::DebugSelect(TEXT("crown_of_cinders"), 1);
        I->Buy(TEXT("crown_of_cinders"), Message);
        break;
    case 4:
        CireShopUI::DebugSelect(TEXT("nightfall_reaver"), 1);
        I->SellSlot(1, false, Message); // Bloodstone Shard
        break;
    case 5:
        PC->bShop = false;
        CireShopUI::DebugReset();
        H->Gold = 715;
        CireShopUI::DebugHoverStat(0);
        break;
    case 6:
    {
        CireShopUI::DebugReset();
        SetPhase(Mode, 0);
        Cires::Items::LootBundle Bundle;
        Bundle.Gold = 96; Bundle.Experience = 200; Bundle.PrimaryTomes = {3}; Bundle.Items = {"grimoire_of_whispers"};
        const FVector Ahead = H->GetActorLocation() + H->GetActorForwardVector() * 520.f + FVector(0, 120, 0);
        G.Chest = CireLoot::SpawnDrop(Mode, H->TeamId, Ahead, Bundle, 3, TEXT("Outpost strongbox"), 7);
        break;
    }
    case 7: if (G.Chest.IsValid()) G.Chest->Open(H); break;
    case 8:
        CireShopUI::DebugReset();
        SetPhase(Mode, 0);
        H->SetActorLocation(Mode->BasePosition(H->TeamId) + FVector(2600, 0, 0));
        I->TeleportReadyAt = 0;
        I->Teleport();
        break;
    case 9:
        I->CompleteTeleportNow();
        I->TeleportReadyAt = static_cast<float>(I->Now()) + 74.f;
        CireShopUI::DebugReset();
        break;
    default: break;
    }
}

bool TickGallery(ACireGameMode* Mode)
{
    if (G.bDone) return true;
    if (FPlatformTime::Seconds() - G.Start > 80) { Finish(false, TEXT("CIRE_SHOP_GALLERY")); return true; }
    if (G.Ready < 0)
    {
        auto* PC = Cast<ACireController>(Mode->GetWorld()->GetFirstPlayerController());
        auto* HUD = PC ? Cast<ACireHUD>(PC->GetHUD()) : nullptr;
        if (PC && HUD && PC->GetPawn() && !SetupGallery(Mode, PC, HUD)) Finish(false, TEXT("CIRE_SHOP_GALLERY"));
        return true;
    }
    const double Now = FPlatformTime::Seconds();
    if (Now - G.Ready < 3.0) return true; // fonts/textures streaming, first layout pass
    if (G.Stage < 0 || (G.bCaptured && Now - G.StageStart > Stages[G.Stage].Delay + .6f))
    {
        if (++G.Stage >= StageCount)
        {
            for (const FString& File : G.Files) G.bPass &= IFileManager::Get().FileSize(*File) > 10000;
            Finish(G.bPass && G.Files.Num() == StageCount, TEXT("CIRE_SHOP_GALLERY"));
            return true;
        }
        EnterStage(Mode, G.Stage);
        G.StageStart = Now;
        G.bCaptured = false;
    }
    // Hover shots: put the virtual pointer on the grid icon once its position is known.
    if (G.Stage == 0)
    {
        const FVector2D Pos = CireShopUI::DebugGridPos(TEXT("sanguine_sabre"));
        if (Pos.X >= 0) CireShopUI::DebugMouse(Pos);
    }
    if (!G.bCaptured && Now - G.StageStart >= Stages[G.Stage].Delay)
    {
        int32 W = 0, H = 0;
        G.PC->GetViewportSize(W, H);
        G.bPass &= W >= 1280 && H >= 720;
        Capture(Stages[G.Stage].Name);
        G.bCaptured = true;
    }
    return true;
}

// ------------------------------------------------------------------ network probe (server side)
TWeakObjectPtr<ACireHero> NetHero;
bool TickNetServer(ACireGameMode* Mode)
{
    if (G.bDone) return true;
    const double Now = FPlatformTime::Seconds();
    auto Fail = [&](const TCHAR* Reason) { UE_LOG(LogCireShopFixtures, Error, TEXT("CIRE_SHOP_NET_SERVER_FAIL step=%d %s"), G.NetStep, Reason); Finish(false, TEXT("CIRE_SHOP_NET_SERVER")); };
    if (Now - G.Start > 110) { Fail(TEXT("timed out")); return true; }
    if (!NetHero.IsValid())
    {
        for (ACireHero* Hero : Mode->Heroes) if (IsValid(Hero) && Hero->IsPlayerControlled() && Hero->bDrafted) NetHero = Hero;
        if (!NetHero.IsValid()) return true;
        // Prep phase, far from town: the shop must still accept purchases.
        SetPhase(Mode, 1);
        NetHero->Gold = 2000;
        NetHero->SetActorLocation(Mode->BasePosition(NetHero->TeamId) + FVector(3000, 0, 0));
        G.NetStep = 1; G.NetStepAt = Now;
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_SERVER_PREP hero=%s gold=2000 distance_from_town=3000"), *NetHero->GetName());
        return true;
    }
    ACireHero* Hero = NetHero.Get();
    UCireInventory* I = Hero->Inventory;
    if (G.NetStep == 1 && I->Belt[0].Id == FName(TEXT("vial_of_crimson")) && I->Belt[0].Charges == 1 && I->Restores.Num() > 0)
    {
        // Client finished its prep script (bought, sold, undid, used a potion). Waves resume.
        const bool bState = I->ToRules().CountOf("serrated_cleaver") == 1 && Hero->Gold == 2000 - 390 - 100;
        if (!bState) { Fail(TEXT("authoritative inventory/gold mismatch after prep script")); return true; }
        SetPhase(Mode, 0);
        Hero->SetActorLocation(Mode->BasePosition(Hero->TeamId) + FVector(3000, 0, 0));
        Hero->GetCharacterMovement()->StopMovementImmediately();
        G.NetStep = 2; G.NetStepAt = Now;
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_SERVER_SURVIVAL gold=%d items_ok=1"), Hero->Gold);
    }
    else if (G.NetStep == 2 && I->TeleportReadyAt > I->Now() + 60 && FVector::Dist2D(Hero->GetActorLocation(), Mode->BasePosition(Hero->TeamId)) < 600)
    {
        if (Hero->Gold != 2000 - 490) { Fail(TEXT("survival purchase was not rejected")); return true; }
        G.NetStep = 3;
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_SERVER_PASS teleport_cooldown=%.0f"), I->TeleportCooldownRemaining());
    }
    else if (G.NetStep == 3 && Mode->GetNumPlayers() == 0) Finish(true, TEXT("CIRE_SHOP_NET_SERVER"));
    return true;
}
} // namespace

bool CireShopFixtures::Initialize(ACireGameMode* Mode)
{
    G = FGallery();
    NetHero.Reset();
    const TCHAR* Command = FCommandLine::Get();
    if (FParse::Param(Command, TEXT("CireShopGallery")))
    {
        G.Kind = EFixture::Gallery;
        G.Mode = Mode; G.Start = FPlatformTime::Seconds();
        G.Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ShopGallery"), FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));
        if (!Mode || Mode->GetNetMode() != NM_Standalone || !IFileManager::Get().MakeDirectory(*G.Directory, true)) { Finish(false, TEXT("CIRE_SHOP_GALLERY")); return true; }
        Mode->bBotsFilled = true; Mode->BotFillTimer = MAX_flt; Mode->WaveTimer = MAX_flt;
        return true;
    }
    if (FParse::Param(Command, TEXT("CireShopNetServer")))
    {
        G.Kind = EFixture::NetServer;
        G.Mode = Mode; G.Start = FPlatformTime::Seconds();
        Mode->bBotsFilled = true; Mode->BotFillTimer = MAX_flt; Mode->WaveTimer = MAX_flt;
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_SERVER_READY"));
        return true;
    }
    if (FParse::Param(Command, TEXT("CireProgressionProbe")))
    {
        G.Kind = EFixture::Probe;
        const bool bPass = CireProgression::RunSmoke(Mode);
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_PROGRESSION_PROBE_%s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
        FPlatformMisc::RequestExitWithStatus(false, bPass ? 0 : 1);
        return true;
    }
    return false;
}

bool CireShopFixtures::Tick(ACireGameMode* Mode)
{
    switch (G.Kind)
    {
    case EFixture::Gallery: return G.Mode.Get() == Mode ? TickGallery(Mode) : false;
    case EFixture::NetServer: return G.Mode.Get() == Mode ? TickNetServer(Mode) : false;
    case EFixture::Probe: return true;
    default: return false;
    }
}

// ------------------------------------------------------------------ network probe (client side)
namespace
{
struct FNetClient
{
    int32 Step = 0;
    double Start = 0, StepAt = 0;
    bool bDone = false;
    TArray<FCireShopFeedback> Seen;
};
FNetClient C;
}

bool CireShopFixtures::TickClient(ACireController* Controller)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("CireShopNetClient"))) return false;
    if (C.bDone) return true;
    const double Now = FPlatformTime::Seconds();
    if (C.Start == 0) C.Start = C.StepAt = Now;
    auto Fail = [&](const TCHAR* Reason)
    {
        UE_LOG(LogCireShopFixtures, Error, TEXT("CIRE_SHOP_NET_CLIENT_FAIL step=%d %s"), C.Step, Reason);
        C.bDone = true; FPlatformMisc::RequestExitWithStatus(false, 1);
    };
    if (Now - C.Start > 100) { Fail(TEXT("timed out")); return true; }
    auto* Hero = Cast<ACireHero>(Controller->GetPawn());
    auto* State = Controller->GetWorld()->GetGameState<ACireGameState>();
    if (!Hero || !State) return true;
    if (Controller->GetNetMode() != NM_Client || Hero->HasAuthority()) { Fail(TEXT("not a remote client")); return true; }
    UCireInventory* I = Hero->Inventory;
    if (!I) return true;
    C.Seen.Append(I->PendingFeedback);
    I->PendingFeedback.Reset();
    auto Saw = [&](ECireShopAction Action, bool bOk, const TCHAR* Contains = nullptr)
    {
        return C.Seen.ContainsByPredicate([&](const FCireShopFeedback& F) { return F.Action == Action && F.bOk == bOk && (!Contains || F.Message.Contains(Contains)); });
    };
    auto Next = [&]() { ++C.Step; C.StepAt = Now; C.Seen.Reset(); };
    const auto Has = [&](const TCHAR* Id, int32 Slot) { return I->Equipment.IsValidIndex(Slot) && I->Equipment[Slot].Id == FName(Id); };
    switch (C.Step)
    {
    case 0:
        if (State->Announcement.IsEmpty()) return true;
        Controller->ServerAction(5, 2, nullptr); // draft the INT archetype
        Next();
        break;
    case 1:
        if (!(Hero->bDrafted && State->Phase == 1 && Hero->Gold == 2000)) return true;
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_CLIENT_PREP_REPLICATED gold=%d"), Hero->Gold);
        I->ServerShopOpen(true);
        I->ServerBuy(TEXT("rusted_longsword"));
        I->ServerBuy(TEXT("bone_dagger"));
        Next();
        break;
    case 2:
        if (!(Has(TEXT("rusted_longsword"), 0) && Has(TEXT("bone_dagger"), 1) && Hero->Gold == 1730)) return true;
        I->ServerBuy(TEXT("serrated_cleaver"));
        Next();
        break;
    case 3:
        if (!(Has(TEXT("serrated_cleaver"), 0) && I->Equipment[1].Id.IsNone() && Hero->Gold == 1610 && Saw(ECireShopAction::Buy, true))) return true;
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_CLIENT_RECIPE_PASS gold=%d gear=%d"), Hero->Gold, Hero->GearRank);
        I->ServerSell(0, false);
        Next();
        break;
    case 4:
        if (!(I->Equipment[0].Id.IsNone() && Hero->Gold == 1610 + 234 && Saw(ECireShopAction::Sell, true))) return true;
        I->ServerUndo();
        Next();
        break;
    case 5:
        if (!(Has(TEXT("serrated_cleaver"), 0) && Hero->Gold == 1610 && Saw(ECireShopAction::Undo, true))) return true;
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_CLIENT_SELL_UNDO_PASS gold=%d"), Hero->Gold);
        I->ServerBuy(TEXT("no_such_item"));
        Next();
        break;
    case 6:
        if (!Saw(ECireShopAction::Buy, false, TEXT("Unknown"))) return true;
        Next();
        break;
    case 7:
        if (!(Has(TEXT("serrated_cleaver"), 0) && Hero->Gold == 1610)) return true;
        I->ServerBuy(TEXT("vial_of_crimson"));
        I->ServerBuy(TEXT("vial_of_crimson"));
        Next();
        break;
    case 8:
        if (!(I->Belt[0].Id == FName(TEXT("vial_of_crimson")) && I->Belt[0].Charges == 2 && Hero->Gold == 1510)) return true;
        I->ServerUse(0, true);
        Next();
        break;
    case 9:
        if (!(I->Belt[0].Charges == 1 && State->Phase == 0)) return true;
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_CLIENT_CONSUMABLE_PASS charges=1"));
        I->ServerBuy(TEXT("rusted_longsword")); // waves: must be rejected with the prep hint
        Controller->ServerAction(8, 0, nullptr);  // Teleport to Base: channel
        Next();
        break;
    case 10:
        if (!(Saw(ECireShopAction::Buy, false, TEXT("intermission")) && I->IsChanneling())) return true;
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_CLIENT_REJECT_AND_CHANNEL_PASS"));
        Next();
        break;
    case 11:
        if (!(!I->IsChanneling() && I->TeleportCooldownRemaining() > 100 && Hero->Gold == 1510)) { if (Now - C.StepAt > 15) Fail(TEXT("teleport did not complete")); return true; }
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_CLIENT_PASS teleport_cooldown=%.0f gold=%d"), I->TeleportCooldownRemaining(), Hero->Gold);
        C.bDone = true;
        FPlatformMisc::RequestExitWithStatus(false, 0);
        break;
    default: break;
    }
    if (Now - C.StepAt > 20) Fail(TEXT("step timed out"));
    return true;
}
#endif
