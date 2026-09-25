// progression-shop: development fixtures.
//  -CireShopGallery          offscreen 1920x1080 captures of the shop, buy/sell/error feedback,
//                            stats window, loot chest and Teleport to Base (Tools/RunProgressionChecks.py --only gallery)
//  -CireShopNetServer/Client dedicated server + remote client: server-enforced shop, replicated
//                            inventory/gold/feedback, undo, consumables and teleport, then the Skill
//                            Shop: buy + level-up through the Server RPCs, rejections, and a remote
//                            client that cannot change the host's progression mode (--only network)
//  -CireProgressionProbe     runs only the progression smoke suites and exits (--only native)
//  -CireShopGalleryShopOnly  with -CireShopGallery: only the shop screens (Skill Shop + Armory), for a second resolution
#include "CireShopFixtures.h"
#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireHUD.h"
#include "CireItems.h"
#include "CireLoot.h"
#include "CireShopUI.h"
#include "CireSkillShop.h"
#include "CirePolymorph.h"
#include "Camera/PlayerCameraManager.h"
#include "CireCrowdControl.h"
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
    TWeakObjectPtr<ACireLootDrop> MateChest;
    TWeakObjectPtr<ACireHero> Mate;
    FString Directory;
    TArray<FString> Files;
    double Start = 0, Ready = -1, StageStart = 0;
    int32 Stage = -1;
    bool bCaptured = false, bDone = false, bPass = true;
    bool bShopOnly = false;   // -CireShopGalleryShopOnly
    int32 Expected = 0;
    bool bAutoOpenArmed = false, bAutoOpenChecked = false;
    TArray<TWeakObjectPtr<ACireMonster>> Critters;
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

// Prices come from Items.json (they follow the economy), never literals.
int32 ItemTotal(const char* Id) { const auto* Def = CireItems::Get().Catalog.Find(Id); return Def ? Def->TotalCost : -100000; }
int32 ItemRecipe(const char* Id) { const auto* Def = CireItems::Get().Catalog.Find(Id); return Def ? Def->RecipeCost : -100000; }
int32 AfterParts() { return 2000 - ItemTotal("rusted_longsword") - ItemTotal("bone_dagger"); }
int32 AfterCleaver() { return AfterParts() - ItemRecipe("serrated_cleaver"); }
int32 AfterVials() { return AfterCleaver() - 2 * ItemTotal("vial_of_crimson"); }

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
    {TEXT("shop_all_items_hover"), 2.2f}, {TEXT("shop_recommended"), 1.2f}, {TEXT("shop_buy_feedback"), .8f},
    {TEXT("shop_error_shake"), .8f}, {TEXT("shop_sell_feedback"), .8f}, {TEXT("hud_stats_window_hover"), 1.2f},
    {TEXT("loot_chest_drop"), 1.6f}, {TEXT("loot_chest_opened"), .9f}, {TEXT("teleport_channel"), 2.2f}, {TEXT("teleport_cooldown"), 1.0f},
    {TEXT("loot_personal_own_vs_teammate"), 1.6f}, {TEXT("loot_window_and_toasts"), .9f}, {TEXT("loot_autocollect_summary_and_log"), 1.0f},
    // progression-shop: the Skill Shop (Eric's target image) and its purchase moments.
    {TEXT("skill_shop_hover"), 2.4f}, {TEXT("skill_shop_seal_stamp"), .9f}, {TEXT("skill_shop_scroll_flight"), .9f},
    {TEXT("skill_shop_unaffordable_error"), .8f}, {TEXT("skill_shop_auto_open_after_wave"), 1.8f},
    {TEXT("skill_polymorph_critters"), 4.2f}};
bool InSubset(int32 Stage) { return !G.bShopOnly || FString(Stages[Stage].Name).StartsWith(TEXT("shop_")) || FString(Stages[Stage].Name).StartsWith(TEXT("skill_")); }
constexpr int32 StageCount = UE_ARRAY_COUNT(Stages);

void EnterStage(ACireGameMode* Mode, int32 Stage)
{
    ACireHero* H = G.Hero.Get();
    UCireInventory* I = Inv();
    ACireHUD* HUD = G.HUD.Get();
    ACireController* PC = G.PC.Get();
    FString Message;
    HUD->DebugTooltipClear();
    CireShopUI::DebugFreezeAfterLastEvent(-1);
    HUD->UISettings.bTooltips = Stage == 0 || Stage == 5; // only the hover shots show a tooltip
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
    case 10:
    {
        // Personal loot: your chest and a teammate's chest drop side by side; only yours is visible.
        CireShopUI::DebugReset();
        SetPhase(Mode, 0);
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        const FVector Ahead = H->GetActorLocation() + H->GetActorForwardVector() * 560.f;
        const FVector Side = FVector::CrossProduct(H->GetActorForwardVector(), FVector::UpVector) * 170.f;
        G.Mate = Mode->GetWorld()->SpawnActor<ACireHero>(Ahead - Side * 3.f, FRotator::ZeroRotator, Params);
        if (G.Mate.IsValid()) { G.Mate->TeamId = H->TeamId; G.Mate->Draft(2); G.Mate->HeroName = TEXT("Veil Scholar"); G.Mate->SetActorTickEnabled(false); }
        Cires::Items::LootBundle Mine;
        Mine.Gold = 88; Mine.Experience = 200; Mine.PrimaryTomes = {3}; Mine.Items = {"crown_of_cinders", "vial_of_crimson"};
        Cires::Items::LootBundle Theirs; Theirs.Gold = 74; Theirs.Items = {"censer_of_dawn"};
        const FString Why = TEXT("Personal loot from Gravemaw, Pack Leader (Tier 4)");
        G.Chest = CireLoot::SpawnPersonalDrop(Mode, H, Ahead - Side, Mine, 4, TEXT("Gravemaw, Pack Leader"), Why, 11);
        if (G.Mate.IsValid()) G.MateChest = CireLoot::SpawnPersonalDrop(Mode, G.Mate.Get(), Ahead + Side, Theirs, 4, TEXT("Gravemaw, Pack Leader"), Why, 12);
        break;
    }
    case 11:
        HUD->UISettings.bShowLootLog = false;
        if (G.Chest.IsValid()) G.Chest->Open(H);
        break;
    case 12:
    {
        CireShopUI::DebugReset();
        Cires::Items::LootBundle A; A.Gold = 61; A.Items = {"bone_dagger"};
        Cires::Items::LootBundle B; B.Gold = 97; B.Experience = 200; B.PrimaryTomes = {1};
        const FVector Near = H->GetActorLocation() + H->GetActorForwardVector() * 2500.f;
        CireLoot::SpawnPersonalDrop(Mode, H, Near, A, 2, TEXT("Outpost cache"), TEXT("Personal loot: you helped clear a Tier 2 pack"), 21);
        CireLoot::SpawnPersonalDrop(Mode, H, Near + FVector(300, 0, 0), B, 3, TEXT("Hollow Siegebreaker"), TEXT("Personal loot from Hollow Siegebreaker"), 22);
        CireLoot::CollectAll(Mode);
        HUD->UISettings.bShowLootLog = true;
        break;
    }
    case 13:
    {
        // Wave 7 prep: 4 active / 1 passive slots open, the ultimate opens at wave 10.
        SetPhase(Mode, 1);
        if (auto* S = Mode->GetGameState<ACireGameState>()) S->Wave = 7;
        HUD->UISettings.bShowLootLog = false;
        HUD->UISettings.bTooltips = true;
        H->Skills = {TEXT("shield_slam"), TEXT("war_cry"), TEXT("cleaving_strike"), TEXT("stone_skin")};
        H->Cooldowns.Init(0, H->Skills.Num());
        I->SkillRanks.Reset();
        const TPair<const TCHAR*, int32> Ranks[] = {{TEXT("shield_slam"), 3}, {TEXT("war_cry"), 2}, {TEXT("cleaving_strike"), 1}, {TEXT("stone_skin"), 2}};
        for (const auto& Rank : Ranks) { FCireSkillRank R; R.Id = Rank.Key; R.Level = Rank.Value; I->SkillRanks.Add(R); }
        H->Gold = 70;
        CireShopUI::DebugReset();
        PC->bShop = true;
        CireShopUI::DebugSkillTab(FString());
        break;
    }
    case 14: // real level-up through the server path: seal stamp on the scroll
        I->ServerLevelSkill(TEXT("war_cry"));
        break;
    case 15: // real purchase of a new active: the scroll flies to the skill bar
        H->Gold += 120;
        I->ServerBuySkill(TEXT("iron_guard"));
        break;
    case 16: // the ultimate slot opens at wave 10: rejected with the reason, the scroll shakes
        I->ServerBuySkill(TEXT("last_stand"));
        break;
    case 17:
    {
        // A wave is cleared (breather): the Skill Shop opens by itself on the skills tab.
        PC->bShop = false;
        CireShopUI::DebugItemTab();
        SetPhase(Mode, 0);
        Mode->CycleWavesSpawned = 1;
        if (auto* S = Mode->GetGameState<ACireGameState>()) { S->CycleWavesDone = 1; S->NextWaveSeconds = 8.f; S->Wave = 7; }
        H->Gold = 400;
        G.bAutoOpenArmed = true;
        break;
    }
    case 18:
    {
        // Polymorph: three monsters in front of the camera, one per critter (Chicken, Piglet, Frog).
        PC->bShop = false;
        HUD->UISettings.bTooltips = false;
        SetPhase(Mode, 0);
        // Along the camera's view, out on the lit road beyond the town gate.
        FVector Forward = H->GetActorForwardVector();
        if (PC->PlayerCameraManager) Forward = PC->PlayerCameraManager->GetCameraRotation().Vector().GetSafeNormal2D();
        const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
        for (int32 Critter = 0; Critter < 3; ++Critter)
        {
            FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            const FVector At = H->GetActorLocation() + Forward * 720.f + Right * ((Critter - 1) * 120.f);
            ACireMonster* M = Mode->GetWorld()->SpawnActor<ACireMonster>(At, (-Forward).Rotation(), Params);
            if (!M) continue;
            // The other lane: the hero stands in town, where its own lane's units would leak at the goal.
            M->Lane = 1 - H->TeamId; M->Health = M->MaxHealth = 5000; Mode->Monsters.Add(M);
            CirePolymorph::Apply(M, 30.f, H, Critter);
            G.Critters.Add(M);
        }
        break;
    }
    default: break;
    }
}

bool TickGallery(ACireGameMode* Mode)
{
    if (G.bDone) return true;
    if (FPlatformTime::Seconds() - G.Start > 220) { Finish(false, TEXT("CIRE_SHOP_GALLERY")); return true; }
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
        do { ++G.Stage; } while (G.Stage < StageCount && !InSubset(G.Stage));
        if (G.Stage >= StageCount)
        {
            for (const FString& File : G.Files) G.bPass &= IFileManager::Get().FileSize(*File) > 10000;
            int32 Expected = 0;
            for (int32 Stage = 0; Stage < StageCount; ++Stage) Expected += InSubset(Stage);
            Finish(G.bPass && G.Files.Num() == Expected, TEXT("CIRE_SHOP_GALLERY"));
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
    // Skill Shop hover: the pointer rests on a scroll so it lifts and shows its tooltip.
    if (G.Stage == 13)
    {
        const FVector2D Pos = CireShopUI::DebugSkillGridPos(TEXT("frost_bind"));
        if (Pos.X >= 0) CireShopUI::DebugMouse(Pos);
    }
    // Seal stamp mid-impact, then the scroll mid-flight to the skill bar.
    if ((G.Stage == 14 || G.Stage == 15) && !G.bCaptured && Now - G.StageStart >= Stages[G.Stage].Delay - .3f)
        CireShopUI::DebugFreezeAfterStamp(G.Stage == 14 ? .2f : .62f);
    if (G.Stage == 16 && !G.bCaptured && Now - G.StageStart >= Stages[G.Stage].Delay - .3f) CireShopUI::DebugFreezeAfterLastEvent(.1f);
    // Breather with the Ready to Continue gate: the fixture drives the gate like the match tick does.
    if (G.Stage == 17) { float Timer = 8.f; CireSkillShop::HoldBreather(Mode, .016f, Timer); }
    // Auto-open: the second cleared wave arrives a moment later (the HUD has seen the first).
    if (G.Stage == 17 && G.bAutoOpenArmed && Now - G.StageStart >= .4f)
    {
        G.bAutoOpenArmed = false;
        Mode->CycleWavesSpawned = 2;
        if (auto* S = Mode->GetGameState<ACireGameState>()) { S->CycleWavesDone = 2; S->NextWaveSeconds = 8.f; }
    }
    if (G.Stage == 17 && !G.bCaptured && !G.bAutoOpenChecked && Now - G.StageStart >= Stages[G.Stage].Delay - .05f)
    {
        const bool bOpened = G.PC.IsValid() && G.PC->bShop && CireShopUI::DebugTab() == 1;
        if (bOpened) { UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_GALLERY_SKILLSHOP_AUTOOPEN_PASS tab=skills after_wave_clear=1")); }
        else { UE_LOG(LogCireShopFixtures, Error, TEXT("CIRE_SHOP_GALLERY_FAIL the Skill Shop did not open after a cleared wave")); }
        G.bPass &= bOpened;
        G.bAutoOpenChecked = true;
    }
    if (G.Stage == 18 && !G.bCaptured && Now - G.StageStart >= Stages[G.Stage].Delay - .05f)
    {
        int32 Shown = 0;
        for (const auto& M : G.Critters) Shown += M.IsValid() && CirePolymorph::IsPolymorphed(M.Get()) && CirePolymorph::HasCritterVisual(M.Get());
        if (Shown == 3) { UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_GALLERY_POLYMORPH_PASS critters=3")); }
        else { UE_LOG(LogCireShopFixtures, Error, TEXT("CIRE_SHOP_GALLERY_FAIL polymorph critter visuals=%d/3"), Shown); }
        G.bPass &= Shown == 3;
    }
    // Feedback shots freeze the UI clock mid-animation so the capture shows the moment itself.
    if (((G.Stage >= 2 && G.Stage <= 4) || G.Stage == 11) && !G.bCaptured && Now - G.StageStart >= Stages[G.Stage].Delay - .3f)
        CireShopUI::DebugFreezeAfterLastEvent(G.Stage == 2 ? .26f : G.Stage == 3 ? .09f : G.Stage == 11 ? .55f : .3f);
    // Personal loot shot: the teammate's chest must be hidden for us (owner-only visibility).
    if (G.Stage == 10 && !G.bCaptured && Now - G.StageStart >= Stages[G.Stage].Delay - .05f)
    {
        const bool bOwnVisible = G.Chest.IsValid() && !G.Chest->IsHidden();
        const bool bMateHidden = G.MateChest.IsValid() && G.MateChest->IsHidden();
        if (!bOwnVisible || !bMateHidden) { UE_LOG(LogCireShopFixtures, Error, TEXT("CIRE_SHOP_GALLERY_FAIL personal chest visibility own=%d mate_hidden=%d"), bOwnVisible ? 1 : 0, bMateHidden ? 1 : 0); }
        else { UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_GALLERY_PERSONAL_VISIBILITY_PASS own_visible=1 teammate_hidden=1")); }
        G.bPass &= bOwnVisible && bMateHidden;
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
TWeakObjectPtr<ACireHero> NetMate;
TWeakObjectPtr<ACireLootDrop> NetOwnChest, NetMateChest;
bool TickNetServer(ACireGameMode* Mode)
{
    if (G.bDone) return true;
    const double Now = FPlatformTime::Seconds();
    auto Fail = [&](const TCHAR* Reason) { UE_LOG(LogCireShopFixtures, Error, TEXT("CIRE_SHOP_NET_SERVER_FAIL step=%d %s"), G.NetStep, Reason); Finish(false, TEXT("CIRE_SHOP_NET_SERVER")); };
    if (Now - G.Start > 170) { Fail(TEXT("timed out")); return true; }
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
        const bool bState = I->ToRules().CountOf("serrated_cleaver") == 1 && Hero->Gold == AfterVials();
        if (!bState) { Fail(TEXT("authoritative inventory/gold mismatch after prep script")); return true; }
        SetPhase(Mode, 0);
        Hero->SetActorLocation(Mode->BasePosition(Hero->TeamId) + FVector(3000, 0, 0));
        Hero->GetCharacterMovement()->StopMovementImmediately();
        G.NetStep = 2; G.NetStepAt = Now;
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_SERVER_SURVIVAL gold=%d items_ok=1"), Hero->Gold);
    }
    else if (G.NetStep == 2 && I->TeleportReadyAt > I->Now() + 60 && FVector::Dist2D(Hero->GetActorLocation(), Mode->BasePosition(Hero->TeamId)) < 600)
    {
        if (Hero->Gold != AfterVials()) { Fail(TEXT("survival purchase was not rejected")); return true; }
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_SERVER_TELEPORT_PASS teleport_cooldown=%.0f"), I->TeleportCooldownRemaining());
        // Personal loot: a chest for the remote player and one for a bot teammate, side by side.
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        const FVector Here = Hero->GetActorLocation();
        auto* Mate = Mode->GetWorld()->SpawnActor<ACireHero>(Here + FVector(0, 600, 0), FRotator::ZeroRotator, Params);
        if (!Mate) { Fail(TEXT("teammate spawn")); return true; }
        Mate->TeamId = Hero->TeamId; Mate->Draft(1); Mate->bBot = true; Mate->SetActorTickEnabled(false);
        NetMate = Mate;
        Cires::Items::LootBundle Mine; Mine.Gold = 50; Mine.Items = {"bone_dagger"};
        Cires::Items::LootBundle Theirs; Theirs.Gold = 70;
        NetOwnChest = CireLoot::SpawnPersonalDrop(Mode, Hero, Here + FVector(700, 0, 0), Mine, 2, TEXT("Probe pack"), TEXT("Personal loot from the probe"), 31);
        NetMateChest = CireLoot::SpawnPersonalDrop(Mode, Mate, Here + FVector(700, 500, 0), Theirs, 2, TEXT("Probe pack"), TEXT("Personal loot from the probe"), 32);
        if (!NetOwnChest.IsValid() || !NetMateChest.IsValid()) { Fail(TEXT("chest spawn")); return true; }
        G.NetStep = 3; G.NetStepAt = Now;
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_SERVER_CHESTS own=%s teammate=%s"), *NetOwnChest->GetName(), *NetMateChest->GetName());
    }
    else if (G.NetStep == 3 && Now - G.NetStepAt > 3.0)
    {
        // Stand the remote player on the teammate's chest: it must stay closed.
        Hero->SetActorLocation(NetMateChest->GetActorLocation() + FVector(0, 0, 100));
        G.NetStep = 4; G.NetStepAt = Now;
    }
    else if (G.NetStep == 4 && Now - G.NetStepAt > 2.0)
    {
        if (!NetMateChest.IsValid() || NetMateChest->bOpened) { Fail(TEXT("a non-owner opened a personal chest")); return true; }
        Hero->SetActorLocation(NetOwnChest->GetActorLocation() + FVector(0, 0, 100));
        G.NetStep = 5; G.NetStepAt = Now;
    }
    else if (G.NetStep == 5 && NetOwnChest.IsValid() && NetOwnChest->bOpened)
    {
        const bool bMateClosed = NetMateChest.IsValid() && !NetMateChest->bOpened;
        if (!bMateClosed || I->ToRules().CountOf("bone_dagger") != 1) { Fail(TEXT("personal chest contents or teammate chest state")); return true; }
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_SERVER_LOOT_PASS personal_chest_opened=1 teammate_chest_closed=1"));
        // Skill Shop: prep of wave 7 with 500 gold; the client buys and levels a skill.
        SetPhase(Mode, 1);
        if (auto* S = Mode->GetGameState<ACireGameState>()) { S->Wave = 7; S->ProgressionMode = 1; }
        Hero->Gold = 500;
        I->SkillRanks.Reset();
        G.NetStep = 6; G.NetStepAt = Now;
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_SERVER_SKILLSHOP_PREP wave=7 gold=500"));
    }
    else if (G.NetStep == 6 && I->SkillRanks.ContainsByPredicate([](const FCireSkillRank& R) { return R.Level == 2; }))
    {
        const auto* S = Mode->GetGameState<ACireGameState>();
        const FCireSkillRank* Rank = I->SkillRanks.FindByPredicate([](const FCireSkillRank& R) { return R.Level == 2; });
        const int32 Spent = CireSkillShop::BuyPrice(Hero, Rank->Id) > 0 ? 500 - Hero->Gold : -1;
        if (!S || S->ProgressionMode != 1 || !Hero->Skills.Contains(Rank->Id) || Spent <= 0) { Fail(TEXT("server Skill Shop state")); return true; }
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_SERVER_SKILL_PASS skill=%s level=2 spent=%d mode=SkillShop (client could not change it)"), *Rank->Id, Spent);
        // Polymorph replication: a monster next to the client's hero becomes a Piglet.
        SetPhase(Mode, 0);
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ACireMonster* Critter = Mode->GetWorld()->SpawnActor<ACireMonster>(Hero->GetActorLocation() + Hero->GetActorForwardVector() * 450.f, FRotator::ZeroRotator, Params);
        if (Critter) { Critter->Lane = Hero->TeamId; Critter->Health = Critter->MaxHealth = 5000; Mode->Monsters.Add(Critter); }
        if (!Critter || CirePolymorph::Apply(Critter, 60.f, Hero, 1) <= 0) { Fail(TEXT("polymorph fixture")); return true; }
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_SERVER_PASS polymorph=Piglet"));
        G.NetStep = 7;
    }
    else if (G.NetStep == 5 && Now - G.NetStepAt > 8.0) { Fail(TEXT("owner could not open their chest")); return true; }
    else if (G.NetStep == 7 && Mode->GetNumPlayers() == 0) Finish(true, TEXT("CIRE_SHOP_NET_SERVER"));
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
        G.bShopOnly = FParse::Param(Command, TEXT("CireShopGalleryShopOnly"));
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
    FString SkillId;
    int32 SkillPrice = 0, LevelPrice = 0;
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
    if (Now - C.Start > 160) { Fail(TEXT("timed out")); return true; }
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
        if (!(Has(TEXT("rusted_longsword"), 0) && Has(TEXT("bone_dagger"), 1) && Hero->Gold == AfterParts())) return true;
        I->ServerBuy(TEXT("serrated_cleaver"));
        Next();
        break;
    case 3:
        if (!(Has(TEXT("serrated_cleaver"), 0) && I->Equipment[1].Id.IsNone() && Hero->Gold == AfterCleaver() && Saw(ECireShopAction::Buy, true))) return true;
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_CLIENT_RECIPE_PASS gold=%d gear=%d"), Hero->Gold, Hero->GearRank);
        I->ServerSell(0, false);
        Next();
        break;
    case 4:
        if (!(I->Equipment[0].Id.IsNone() && Hero->Gold == AfterCleaver() + FMath::RoundToInt(ItemTotal("serrated_cleaver") * .6f) && Saw(ECireShopAction::Sell, true))) return true;
        I->ServerUndo();
        Next();
        break;
    case 5:
        if (!(Has(TEXT("serrated_cleaver"), 0) && Hero->Gold == AfterCleaver() && Saw(ECireShopAction::Undo, true))) return true;
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_CLIENT_SELL_UNDO_PASS gold=%d"), Hero->Gold);
        I->ServerBuy(TEXT("no_such_item"));
        Next();
        break;
    case 6:
        if (!Saw(ECireShopAction::Buy, false, TEXT("Unknown"))) return true;
        Next();
        break;
    case 7:
        if (!(Has(TEXT("serrated_cleaver"), 0) && Hero->Gold == AfterCleaver())) return true;
        I->ServerBuy(TEXT("vial_of_crimson"));
        I->ServerBuy(TEXT("vial_of_crimson"));
        Next();
        break;
    case 8:
        if (!(I->Belt[0].Id == FName(TEXT("vial_of_crimson")) && I->Belt[0].Charges == 2 && Hero->Gold == AfterVials())) return true;
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
        if (!(!I->IsChanneling() && I->TeleportCooldownRemaining() > 100 && Hero->Gold == AfterVials())) { if (Now - C.StepAt > 15) Fail(TEXT("teleport did not complete")); return true; }
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_CLIENT_TELEPORT_PASS teleport_cooldown=%.0f gold=%d"), I->TeleportCooldownRemaining(), Hero->Gold);
        I->PendingLoot.Reset();
        Next();
        break;
    case 12:
    case 13:
    {
        // Personal loot on a real remote client: we only ever receive our own chest.
        int32 Own = 0;
        for (TActorIterator<ACireLootDrop> It(Controller->GetWorld()); It; ++It)
        {
            if (It->OwnerHero != Hero) { Fail(TEXT("received another player's personal chest")); return true; }
            ++Own;
        }
        if (C.Step == 12)
        {
            if (Own == 0) return true;
            UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_CLIENT_OWN_CHEST_ONLY chests_seen=%d"), Own);
            Next();
            return true;
        }
        if (I->PendingLoot.Num() == 0) return true;
        const FCireLootReport& Report = I->PendingLoot.Last();
        const bool bDagger = Report.Lines.ContainsByPredicate([](const FCireLootLine& L) { return L.ItemId == FName(TEXT("bone_dagger")) && L.Slot >= 0; });
        if (!bDagger || Report.Gold != 50 || Report.Why.IsEmpty()) { Fail(TEXT("loot report contents")); return true; }
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_CLIENT_LOOT_PASS personal_report_lines=%d gold=%d teammate_chest_never_replicated=1"), Report.Lines.Num(), Report.Gold);
        Next();
        break;
    }
    case 14:
    {
        // Skill Shop through the Server RPCs: the host put us in the prep of wave 7 with 500 gold.
        if (!(State->Phase == 1 && State->Wave == 7 && Hero->Gold == 500 && State->ProgressionMode == 1)) return true;
        const TArray<FCireShopSkill> Catalog = CireSkillShop::CatalogFor(Hero);
        const FCireShopSkill* Pick = Catalog.FindByPredicate([&](const FCireShopSkill& K) { return K.Kind == Cires::Items::ShopSkillKind::Active && CireSkillShop::BuyBlocker(Hero, K.Id).IsEmpty(); });
        FString Outside;
        for (const auto& Skill : Cires::StarterSkillPool())
        {
            const FString Id = UTF8_TO_TCHAR(Skill.Id.c_str());
            if (!Catalog.ContainsByPredicate([&](const FCireShopSkill& K) { return K.Id == Id; })) { Outside = Id; break; }
        }
        if (!Pick || Outside.IsEmpty()) { Fail(TEXT("no buyable skill in the replicated catalog")); return true; }
        C.SkillId = Pick->Id; C.SkillPrice = CireSkillShop::BuyPrice(Hero, Pick->Id);
        I->ServerSetProgressionMode(0); // a remote client is not the host: ignored
        I->ServerBuySkill(Outside);     // not in this champion's list: rejected
        I->ServerBuySkill(C.SkillId);
        Next();
        break;
    }
    case 15:
        if (!(Hero->Skills.Contains(C.SkillId) && Hero->Gold == 500 - C.SkillPrice && CireSkillShop::Level(Hero, C.SkillId) == 1 &&
            Saw(ECireShopAction::SkillBuy, true) && Saw(ECireShopAction::SkillBuy, false, TEXT("Not in your")))) return true;
        if (State->ProgressionMode != 1) { Fail(TEXT("a remote client changed the progression mode")); return true; }
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_CLIENT_SKILL_BUY_PASS skill=%s price=%d gold=%d"), *C.SkillId, C.SkillPrice, Hero->Gold);
        C.LevelPrice = CireSkillShop::LevelPrice(Hero, C.SkillId);
        I->ServerLevelSkill(C.SkillId);
        Next();
        break;
    case 16:
        if (!(CireSkillShop::Level(Hero, C.SkillId) == 2 && Hero->Gold == 500 - C.SkillPrice - C.LevelPrice && Saw(ECireShopAction::SkillLevel, true))) return true;
        UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_CLIENT_SKILL_PASS skill=%s level=2 gold=%d replicated_ranks=1 mode_unchanged=1"), *C.SkillId, Hero->Gold);
        Next();
        break;
    case 17:
    {
        // The server polymorphed a monster: the buff record (critter = Piglet) replicates and the
        // client draws the critter body.
        for (TActorIterator<ACireMonster> It(Controller->GetWorld()); It; ++It)
            if (CirePolymorph::CritterOf(*It) == 1 && CirePolymorph::HasCritterVisual(*It) && CireCrowdControl::IsStunned(*It))
            {
                UE_LOG(LogCireShopFixtures, Display, TEXT("CIRE_SHOP_NET_CLIENT_PASS polymorph_replicated=1 critter=Piglet visual=1"));
                C.bDone = true;
                FPlatformMisc::RequestExitWithStatus(false, 0);
                return true;
            }
        return true;
    }
    default: break;
    }
    if (Now - C.StepAt > 20) Fail(TEXT("step timed out"));
    return true;
}
#endif
