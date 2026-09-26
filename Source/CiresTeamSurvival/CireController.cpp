#include "CireVideoSettings.h"
#include "CireLocomotionLab.h" // movement-feel
#include "CireWaves.h" // wave-director
#include "CireGame.h"
#include "CireChampionRoster.h"
#include "CireShopUI.h" // progression-shop: Skill Shop key
#include "CireVendors.h" // vendors: Interact key and merchant clicks
#include "CireCrowdControl.h" // champion-draft: crowd control, timed casts, execute skills
#include "CireShopFixtures.h" // progression-shop
#include "CireItems.h" // progression-shop
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "CireRealm.h"
#include "CireInterfaceProbe.h"
#include "CireExpansionNetProbe.h"
#include "CireSelection.h"
#include "CireConstruct.h"
#include "CireSummon.h"
#include "CireCombatEvents.h"
#include "CireMobility.h"
#include "CireTargeting.h"
#include "CireChampionProfiles.h"
#include "GameFramework/SpringArmComponent.h"
#include "CireCamera.h"
#include "CireKeybindings.h"
#include "CirePets.h" // pets
#include "CireSummonsBar.h" // fix/summons
#include "CirePlaySession.h"

#if !UE_BUILD_SHIPPING
DEFINE_LOG_CATEGORY_STATIC(LogCireNetClient, Log, All);
namespace {
struct FCireClientProbe {
    double Started = 0;
    double StepStarted = 0;
    int32 Step = 0;
    int32 Gold = 0;
    bool Done = false;
    FVector MovementOrigin=FVector::ZeroVector;
    bool bStrafeStarted=false;
    float StrafeYaw=0;
    uint64 StrafeStartFrame=0;
    TWeakObjectPtr<ACireMonster> Selected;
};
FCireClientProbe ClientProbe;

bool TickClientProbe(ACireController* Controller) {
    if(!FParse::Param(FCommandLine::Get(),TEXT("CireClientProbe"))) return false;
    auto& Probe=ClientProbe;
    if(Probe.Done) return true;
    const double Now=FPlatformTime::Seconds();
    if(Probe.Started==0) {Probe.Started=Now;Probe.StepStarted=Now;}
    const auto Fail=[&](const TCHAR* Reason) {
        UE_LOG(LogCireNetClient,Error,TEXT("CIRE_NET_CLIENT_FAIL step=%d reason=%s"),Probe.Step,Reason);
        Probe.Done=true;FPlatformMisc::RequestExitWithStatus(false,1);
    };
    if(Now-Probe.Started>30) {Fail(TEXT("remote probe timed out"));return true;}
    auto* Hero=Cast<ACireHero>(Controller->GetPawn());
    auto* State=Controller->GetWorld()->GetGameState<ACireGameState>();
    if(!Hero||!State) return true;
    if(Controller->GetNetMode()!=NM_Client||Hero->HasAuthority()) {Fail(TEXT("not a remote client"));return true;}
    if(Probe.Step==0) {
        if(State->Announcement.IsEmpty()) return true; // wait for an actual replicated state
        if(Hero->bDrafted||Hero->TeamId<0||Hero->TeamId>1||State->Phase!=0) {Fail(TEXT("unexpected initial replicated state"));return true;}
        UE_LOG(LogCireNetClient,Display,TEXT("CIRE_NET_CLIENT_CONNECTED team=%d phase=%d round=%d authority=0"),Hero->TeamId,State->Phase,State->Round);
        Controller->ServerAction(5,-1,nullptr);
        Probe.Step=1;Probe.StepStarted=Now;
    } else if(Probe.Step==1&&Now-Probe.StepStarted>0.5) {
        if(Hero->bDrafted) {Fail(TEXT("invalid draft was accepted"));return true;}
        Controller->ServerAction(5,2,nullptr);
        Probe.Step=2;Probe.StepStarted=Now;
    } else if(Probe.Step==2&&Hero->bDrafted) {
        if(Hero->Archetype!=2||Hero->Skills.Num()!=0||Hero->Cooldowns.Num()!=0||
            !FMath::IsNearlyEqual(Hero->MaxHealth,Hero->Strength*25.f)|| // level 1: base 15 x STR + 10 x STR (str-scaling)
            !FMath::IsNearlyEqual(Hero->MaxMana,Hero->Intelligence*30.f)) {Fail(TEXT("draft or stat replication mismatch"));return true;}
        Probe.MovementOrigin=Hero->GetActorLocation();
        Probe.Step=5;Probe.StepStarted=Now;
        UE_LOG(LogCireNetClient,Display,TEXT("CIRE_NET_CLIENT_DRAFT_PASS archetype=%d hp=%.0f mana=%.0f"),Hero->Archetype,Hero->MaxHealth,Hero->MaxMana);
    } else if(Probe.Step==5) {
        // WoW strafe (StrafeRight): face-control keeps the heading while input goes along the right vector.
        const FRotator Heading(0,Probe.StrafeYaw,0);
        if(!Probe.bStrafeStarted) {
            Probe.bStrafeStarted=true;Probe.StrafeYaw=static_cast<float>(Hero->GetActorRotation().Yaw);
            Controller->SetControlRotation(FRotator(-20,Probe.StrafeYaw,0));
            if(Hero->Mobility){Hero->Mobility->bFaceControl=true;Hero->Mobility->ServerSetFaceControl(true);}
            Probe.MovementOrigin=Hero->GetActorLocation();Probe.StrafeStartFrame=GFrameCounter;
        }
        // 0.75 s AND at least 20 frames: a client starved by build load can tick only 2 frames in 0.75 s.
        if(Now-Probe.StepStarted<0.75||GFrameCounter-Probe.StrafeStartFrame<20) Hero->AddMovementInput(FRotationMatrix(FRotator(0,Probe.StrafeYaw,0)).GetUnitAxis(EAxis::Y));
        else {
            const FVector Delta=Hero->GetActorLocation()-Probe.MovementOrigin;
            const double Lateral=FVector::DotProduct(Delta,FRotationMatrix(Heading).GetUnitAxis(EAxis::Y));
            const double Forward=FVector::DotProduct(Delta,FRotationMatrix(Heading).GetUnitAxis(EAxis::X));
            const float Turned=FMath::Abs(FRotator::NormalizeAxis(static_cast<float>(Hero->GetActorRotation().Yaw)-Probe.StrafeYaw));
            if(Lateral<50||FMath::Abs(Forward)>Lateral*.2||Turned>3.f) { // 0.75 s wall-clock window: low FPS under build load covers less ground; direction is what is tested
                UE_LOG(LogCireNetClient,Error,TEXT("CIRE_NET_CLIENT_STRAFE lateral=%.1f forward=%.1f turned=%.1f"),Lateral,Forward,Turned);
                Fail(TEXT("remote strafe did not move sideways without rotating"));return true;}
            if(Hero->Mobility){Hero->Mobility->bFaceControl=false;Hero->Mobility->ServerSetFaceControl(false);}
            UE_LOG(LogCireNetClient,Display,TEXT("CIRE_NET_CLIENT_MOVEMENT_PASS distance_cm=%.1f strafe_lateral_cm=%.1f forward_cm=%.1f yaw_change=%.2f"),Delta.Size2D(),Lateral,Forward,Turned);
            Probe.Step=6;Probe.StepStarted=Now;
        }
    } else if(Probe.Step==6) {
        static double LastDiag=0;
        if(Now-Probe.StepStarted>3&&Now-LastDiag>3) { // diagnose a missing probe target instead of a bare timeout
            LastDiag=Now;
            UE_LOG(LogCireNetClient,Warning,TEXT("CIRE_NET_CLIENT_TARGET_WAIT phase=%d team=%d drafted=%d dead=%d"),State->Phase,Hero->TeamId,Hero->bDrafted?1:0,Hero->bDead?1:0);
            for(TActorIterator<ACireMonster> It(Controller->GetWorld());It;++It)
                UE_LOG(LogCireNetClient,Warning,TEXT("  monster=%s lane=%d health=%.0f dist=%.0f hostile=%d"),*It->MonsterName,It->Lane,It->Health,
                    FVector::Dist2D(Hero->GetActorLocation(),It->GetActorLocation()),Hero->IsHostile(*It)?1:0);
        }
        for(TActorIterator<ACireMonster> It(Controller->GetWorld());It;++It) {
            if(It->MonsterName==TEXT("CIRE_NETWORK_PROBE_TARGET")&&Hero->IsHostile(*It)&&Hero->InRange(*It,2500)) {
                Probe.Selected=*It;
                Controller->ServerAction(0,0,*It);
                Probe.Step=3;Probe.StepStarted=Now;
                break;
            }
        }
    } else if(Probe.Step==3&&Probe.Selected.IsValid()&&Hero->Target==Probe.Selected.Get()) {
        Probe.Gold=Hero->Gold;
        Controller->ServerAction(4,0,nullptr); // valid item, forbidden survival phase
        Controller->ServerAction(2,5,nullptr); // empty skill slot
        Controller->ServerAction(2,-1,nullptr); // invalid skill index
        Controller->ServerAction(3,99,nullptr); // invalid offer index
        Controller->ServerAction(5,0,nullptr); // forbidden champion redraft
        Controller->ServerAction(0,0,nullptr); // invalid target must preserve selection
        Probe.Step=4;Probe.StepStarted=Now;
        UE_LOG(LogCireNetClient,Display,TEXT("CIRE_NET_CLIENT_TARGET_PASS hostile=1 actor=%s"),*Hero->Target->GetName());
    } else if(Probe.Step==4&&Now-Probe.StepStarted>1.0&&Hero->Notice.Contains(TEXT("intermission"))) {
        const bool Valid=Hero->Gold==Probe.Gold&&Hero->GearRank==0&&Hero->Archetype==2&&
            Hero->Skills.Num()==0&&Hero->Cooldowns.Num()==0&&FMath::IsNearlyZero(Hero->CDR)&&
            FMath::IsNearlyEqual(Hero->Mana,Hero->MaxMana)&&FMath::IsNearlyEqual(Hero->Energy,100.f)&&
            Hero->Target==Probe.Selected.Get()&&Hero->IsHostile(Hero->Target);
        if(!Valid) {Fail(TEXT("invalid action mutated authoritative state"));return true;}
        UE_LOG(LogCireNetClient,Display,TEXT("CIRE_NET_CLIENT_PASS team=%d phase=%d gold=%d skills=%d target_replicated=1 rejection_ack=1"),Hero->TeamId,State->Phase,Hero->Gold,Hero->Skills.Num());
        Probe.Done=true;FPlatformMisc::RequestExitWithStatus(false,0);
    }
    return true;
}
} // namespace
#endif

ACireController::ACireController() { bShowMouseCursor=true; bEnableClickEvents=true; }
void ACireController::PawnLeavingGame() {
    if(auto* H=Cast<ACireHero>(GetPawn())) {
        H->bBot=true; H->bAutoAttack=true;
        if(!H->bDrafted)H->Draft(H->Archetype);
        UnPossess();
    } else Super::PawnLeavingGame();
}
void ACireController::BeginPlay() {
    Super::BeginPlay();
    if(IsLocalController()) {FInputModeGameAndUI Mode;Mode.SetHideCursorDuringCapture(false);SetInputMode(Mode);SetControlRotation(FRotator(-25,0,0));}
}
void ACireController::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    CireTargeting::Cleanup(this);
    CireSelection::Cleanup(this);
    CireCamera::Cleanup(this);
    Super::EndPlay(EndPlayReason);
}
namespace {
void CycleTargetDirected(ACireController* C,bool bFriendly,bool bReverse) {
    auto* H=Cast<ACireHero>(C->GetPawn()); if(!H){CireTargeting::Cleanup(C);return;}
    // WoW tab targeting: camera cone first, nearest outward; TargetPreviousEnemy walks the tab history back.
    if(AActor* Next=CireSelection::NextTarget(C,bFriendly,bReverse);Next&&Next!=H->Target)C->ServerAction(0,0,Next);
}
}
void ACireController::CycleTarget(bool bFriendly) {CycleTargetDirected(this,bFriendly,false);}
void ACireController::PlayerTick(float Dt) {
    Super::PlayerTick(Dt); if(!IsLocalController())return;
    CirePlaySession::Tick(this,Dt); // feat/camera-movement: -CirePlaySession simulated inputs (development only)
    CireVideoCycle::Tick(this); // video-crash: -CireVideoCycle preset/resolution regression (development only)
#if !UE_BUILD_SHIPPING
    if(CireExpansionNetProbe::TickClient(this))return;
    if(CireInterfaceProbe::TickClient(this))return;
    if(TickClientProbe(this))return;
    if(CireLocomotionLab::TickClient(this))return; // movement-feel: network locomotion check
    if(CireShopFixtures::TickClient(this))return; // progression-shop
#endif
    auto* H=Cast<ACireHero>(GetPawn()); if(!H){CireTargeting::Cleanup(this);return;}
    auto* Interface=Cast<ACireHUD>(GetHUD());
    // Every gameplay key comes from the rebindable action map (CireKeybindings.h).
    FCireKeybindings* MutableKeys=Interface?&Interface->UISettings.Keybindings:nullptr;
    const FCireKeybindings& Keys=MutableKeys?*MutableKeys:CireKeybindings::Defaults();
    // Release state even if a menu/chat consumes the rest of this frame.
    if(Keys.WasReleased(this,TEXT("Jump")))H->StopJumping();
    if(Interface) {
        const auto& Options=Interface->UISettings;
        H->Camera->SetFieldOfView(Options.CameraFOV);
        H->Camera->PostProcessSettings.bOverride_BloomIntensity=true;
        H->Camera->PostProcessSettings.BloomIntensity=Options.bBloom?.65f:0.f;
        H->Camera->PostProcessSettings.bOverride_MotionBlurAmount=true;
        H->Camera->PostProcessSettings.MotionBlurAmount=Options.bMotionBlur?.35f:0.f;
    }
    CireSelection::Update(this);
    const bool bAimInputConsumed=CireTargeting::Tick(this);
    if(!IsValid(FocusTarget)||!CireRealm::CanObserve(H,FocusTarget))FocusTarget=nullptr;
    // WoW camera/steering runs every frame so the boom, zoom and facing stay consistent in menus.
    const bool bBlockingUI=Interface&&Interface->IsBlockingGameplayInput();
    const bool bOverUI=Interface&&Interface->IsPointerOverInterface()&&!CirePlaySession::IsActive(); // offscreen session: pointer is over the world
    CireCamera::FFrame CameraFrame;
    CameraFrame.bMouseAllowed=!bBlockingUI;
    CameraFrame.bSteeringAllowed=!bChatInput&&!bBlockingUI&&H->bDrafted&&!H->bDead&&!bShop;
    CameraFrame.bPressEligible=!bAimInputConsumed&&!CireTargeting::Snapshot(this).bActive&&!bSummonMoveTargeting&&
        !IsInputKeyDown(EKeys::LeftShift)&&!IsInputKeyDown(EKeys::RightShift);
    CameraFrame.bPointerOverInterface=bOverUI;
    if(!bOverUI&&!bBlockingUI)CameraFrame.WheelSteps=(WasInputKeyJustPressed(EKeys::MouseScrollUp)?1.f:0.f)-(WasInputKeyJustPressed(EKeys::MouseScrollDown)?1.f:0.f);
    CameraFrame.Options=Interface?&Interface->UISettings:nullptr;
    CameraFrame.Bindings=&Keys;
    CameraFrame.bAiming=CireTargeting::Snapshot(this).bActive;            // feat/camera-movement
    CameraFrame.bHoldMovement=CireTargeting::HoldsMovement(this);         // feat/camera-movement: stop to cast
    // Rebinding capture (keybinding screen) owns the keyboard until it binds, unbinds or cancels.
    if(MutableKeys&&MutableKeys->IsCapturing())CameraFrame.bSteeringAllowed=false;
    const auto Camera=CireCamera::Tick(this,H,Dt,CameraFrame);
    if(MutableKeys&&MutableKeys->IsCapturing()) {
        FCireCaptureResult Captured;
        if(MutableKeys->TickCapture(this,Captured,ECireBindPolicy::UnbindOther)/* wow-ui: conflicts leave the other action unbound */&&(Captured.Kind==FCireCaptureResult::Bound||Captured.Kind==FCireCaptureResult::Unbound))Interface->UISettings.Save();
        return;
    }
    CireSelection::HandleTargetLoss(this,Interface&&Interface->UISettings.bAutoReacquireTarget);
    // feat/camera-movement: armed ground aim survives movement and right-drag steering.
    // A clean left click confirms at the reticle (movement continues); a clean right click cancels (option).
    if(Camera.bMovementPressed)CireTargeting::ReleaseMovementHold(this);
    bool bClickUsed=false;
    if(CameraFrame.bAiming&&!bChatInput&&!bBlockingUI) {
        if(Camera.bClick){CireTargeting::Confirm(this);bClickUsed=true;}
        else if(Camera.bRightClick&&(!Interface||Interface->UISettings.bRightClickCancelsAim)){CireTargeting::Cancel(this);H->Notice=TEXT("Aim cancelled.");}
    }
    // champion-select: while the draft search box is focused it owns the keyboard.
    if(bDraftSearch) {
        if(H->bDrafted){bDraftSearch=false;}
        else {
            if(WasInputKeyJustPressed(EKeys::Escape)){DraftSearch.Empty();bDraftSearch=false;}
            else if(WasInputKeyJustPressed(EKeys::Enter))bDraftSearch=false;
            else if(WasInputKeyJustPressed(EKeys::BackSpace)&&!DraftSearch.IsEmpty())DraftSearch.LeftChopInline(1);
            return;
        }
    }
    if(bChatInput) {
        CireTargeting::Cancel(this);
        if(WasInputKeyJustPressed(EKeys::Escape))CancelChat();
        else if(WasInputKeyJustPressed(EKeys::Enter))SubmitChat();
        else if(WasInputKeyJustPressed(EKeys::Tab))bChatTeamOnly=!bChatTeamOnly;
        else if(WasInputKeyJustPressed(EKeys::BackSpace)&&!ChatDraft.IsEmpty())ChatDraft.LeftChopInline(1);
        return;
    }
    if(Keys.WasPressed(this,TEXT("OpenChat"))) {CireTargeting::Cancel(this);BeginChat();return;}
    if(Keys.WasPressed(this,TEXT("ToggleLayoutEditor"))&&Interface)Interface->ToggleLayoutEditor();
    if(Keys.WasPressed(this,TEXT("ToggleOptions"))&&Interface)Interface->ToggleSettings();
    if(Keys.WasPressed(this,TEXT("ToggleDeveloperTools"))&&Interface)Interface->ToggleDeveloperTools();
    if(WasInputKeyJustPressed(EKeys::Escape)) {
        if(bAimInputConsumed)return;
        if(bSummonMoveTargeting){bSummonMoveTargeting=false;H->Notice=TEXT("Summon order cancelled.");return;}
        if(Interface&&Interface->HandleEscape())return;
        // feat/camera-movement: WoW Escape clears the target once nothing else is open.
        const bool bHadPanel=bShop||bHelp;bShop=false;bHelp=false;
        if(!bHadPanel&&IsValid(H->Target))ServerAction(6,0,nullptr);
    }
    if(Interface) {
        // Wheel over HUD panels scrolls them (chat); over the world it zooms the camera (CireCamera).
        if(bOverUI&&WasInputKeyJustPressed(EKeys::MouseScrollUp))Interface->HandleMouseWheel(1);
        if(bOverUI&&WasInputKeyJustPressed(EKeys::MouseScrollDown))Interface->HandleMouseWheel(-1);
        if(Interface->IsBlockingGameplayInput()){CireTargeting::Cancel(this);return;}
    }
    if(Keys.WasPressed(this,TEXT("ToggleHelp")))bHelp=!bHelp;
    if(Keys.WasPressed(this,TEXT("ToggleShop")))bShop=!bShop;
    if(Keys.WasPressed(this,TEXT("Interact"))&&H->bDrafted) { // vendors: open the merchant you stand at (again: close)
        ACireVendor* Near=CireVendors::NearestInRange(H);
        if(bShop&&Near&&CireShopUI::CurrentVendor()==Near->VendorId)bShop=false;
        else if(!CireVendors::Interact(this,Near))H->Notice=FString::Printf(TEXT("No merchant in reach. %s opens every merchant's wares."),*Keys.Label(TEXT("ToggleShop")));
    }
    if(Keys.WasPressed(this,TEXT("TargetNextEnemy")))CycleTargetDirected(this,false,false);
    if(Keys.WasPressed(this,TEXT("TargetPreviousEnemy")))CycleTargetDirected(this,false,true);
    if(Keys.WasPressed(this,TEXT("TargetNextAlly")))CycleTarget(true);
    if(Keys.WasPressed(this,TEXT("TargetSelf")))ServerAction(0,0,H);
    if(Keys.WasPressed(this,TEXT("ToggleAutoAttack")))ServerAction(1,0,nullptr);
    if(Keys.WasPressed(this,TEXT("RecallToTown")))ServerAction(8,0,nullptr); // progression-shop: Teleport to Base (hearthstone channel)
    // progression-shop: stats window, consumable belt and item-use keys (CireItems / CireShopUI).
    if(Keys.WasPressed(this,TEXT("ToggleStats"))&&Interface){Interface->UISettings.bShowStats=!Interface->UISettings.bShowStats;Interface->UISettings.Save();}
    if(Keys.WasPressed(this,TEXT("ToggleLootLog"))&&Interface){Interface->UISettings.bShowLootLog=!Interface->UISettings.bShowLootLog;Interface->UISettings.Save();}
    if(Keys.WasPressed(this,TEXT("ToggleSkillShop")))CireShopUI::ToggleSkillShop(this);
    if(H->bDrafted&&H->Inventory&&H->Offers.IsEmpty()) {
        for(int32 Index=0;Index<3;++Index)if(Keys.WasPressed(this,CireItems::BeltAction(Index)))H->Inventory->ServerUse(Index,true);
        for(int32 Index=0;Index<6;++Index)if(Keys.WasPressed(this,CireItems::ItemAction(Index)))H->Inventory->ServerUse(Index,false);
    }
    if(!H->bDrafted&&Interface) {
        if(Keys.WasPressed(this,TEXT("RosterPreviousPage")))Interface->ChangeDraftRosterPage(-1);
        if(Keys.WasPressed(this,TEXT("RosterNextPage")))Interface->ChangeDraftRosterPage(1);
    }
    // Action bars: bar 1 slots 1..6 also pick draft roster entries and augment offers.
    for(int32 Bar=1;Bar<=FCireKeybindings::NumBars;++Bar)for(int32 Index=1;Index<=FCireKeybindings::SlotsPerBar;++Index) {
        const FName Slot=CireKeybindings::SlotAction(Bar,Index);
        if(!Keys.WasPressed(this,Slot))continue;
        const int32 I=Index-1;
        if(!H->bDrafted) {if(Bar==1&&I<6){if(Interface)Interface->DraftRosterSlot(I);else if(I<5)ServerAction(5,I,nullptr);}}
        else if(H->Offers.Num()>0&&(!Interface||Interface->IsSkillOfferOpen())) {if(Bar==1&&I<4)ServerAction(3,I,nullptr);} // champion-draft: deferred offers keep casting
        else if(!bShop) {
            // progression-shop: an action-bar slot may hold an active item ("item:<id>").
            const int32 Item=CireItems::ResolveItemSlot(Keys,*H,Slot);
            if(Item!=INDEX_NONE){if(H->Inventory)H->Inventory->ServerUse(Item,false);continue;}
            const int32 Skill=CireKeybindings::ResolveSlot(Keys,*H,Slot);if(Skill!=INDEX_NONE)RequestCast(Skill);
        }
    }
    // pets: companion commands and stances (CireKeybindings "Pet*" actions).
    if(H->bDrafted&&!bShop&&CirePets::ForOwner(H))
        for(uint8 Command=0;Command<static_cast<uint8>(ECirePetCommand::Count);++Command)
            if(Keys.WasPressed(this,CirePets::CommandAction(static_cast<ECirePetCommand>(Command))))ServerPetCommand(Command);
    // fix/summons: the pet attack / follow / stay keys also order commandable summons (the Oathbound Guardian).
    if(H->bDrafted&&!bShop&&CireSummonsBar::HasCommandable(H))
    {
        if(Keys.WasPressed(this,CirePets::CommandAction(ECirePetCommand::Attack)))ServerSummonCommand(2,H->Target,H->GetActorLocation());
        if(Keys.WasPressed(this,CirePets::CommandAction(ECirePetCommand::Follow)))ServerSummonCommand(0,nullptr,H->GetActorLocation());
        if(Keys.WasPressed(this,CirePets::CommandAction(ECirePetCommand::Stay)))ServerSummonCommand(3,nullptr,H->GetActorLocation());
    }
    const bool bOfferModal=H->Offers.Num()>0&&(!Interface||Interface->IsSkillOfferOpen()); // champion-draft
    if(H->bDrafted&&!bShop&&!bOfferModal&&WasInputKeyJustPressed(EKeys::LeftMouseButton)
        &&!bAimInputConsumed&&!CireTargeting::Snapshot(this).bActive&&(!Interface||!Interface->IsPointerOverInterface())
        &&(bSummonMoveTargeting||IsInputKeyDown(EKeys::LeftShift)||IsInputKeyDown(EKeys::RightShift))) {
        ServerSummonCommand(1,nullptr,CursorAim());bSummonMoveTargeting=false;return;
    }
    // WoW: a left click (released without dragging the camera) selects; a left drag only orbits.
    if(Camera.bClick&&!bClickUsed&&H->bDrafted&&!bShop&&!bOfferModal&&!CireTargeting::Snapshot(this).bActive
        &&!IsInputKeyDown(EKeys::RightMouseButton)) {
        FHitResult CursorHit;
        if(GetHitResultAtScreenPosition(Camera.ClickPosition,UEngineTypes::ConvertToTraceType(ECC_GameTraceChannel1),false,CursorHit)) {
            AActor* Selected=CursorHit.GetActor();
            // feat/camera-movement: clicking empty ground keeps the target (a quick camera tap while
            // strafing must never drop it); Escape clears it.
            if((Cast<ACireHero>(Selected)||Cast<ACireMonster>(Selected)||Cast<ACireConstruct>(Selected))&&CireRealm::CanObserve(H,Selected))ServerAction(0,0,Selected);
            else if(auto* Vendor=Cast<ACireVendor>(Selected);Vendor&&Vendor->Team==H->TeamId) { // vendors: click a merchant to trade
                if(CireVendors::NearestInRange(H,250.f)==Vendor)CireVendors::Interact(this,Vendor);
                else if(const FCireVendorDef* Def=CireVendors::Find(Vendor->VendorId))H->Notice=FString::Printf(TEXT("Walk up to %s's stall to trade."),*Def->Keeper);
            }
        }
    }
    if(H->bDead||!H->bDrafted||bShop||CireCrowdControl::IsStunned(H))return; // champion-draft: stunned: no movement, jump or dodge
    if(Keys.WasPressed(this,TEXT("Jump")))H->Jump();
    if(H->Mobility)
    {
        if(Keys.WasPressed(this,TEXT("ToggleWalk"))){H->Mobility->bWalking=!H->Mobility->bWalking;H->Mobility->ServerSetWalk(H->Mobility->bWalking);}
        if(Keys.WasPressed(this,TEXT("DodgeRoll")))
        {
            const FRotator Facing(0,GetControlRotation().Yaw,0);
            const auto Held=[&](const TCHAR* A){return Keys.IsDown(this,A)?1.f:0.f;};
            const float Side=FMath::Clamp(Held(TEXT("StrafeRight"))+Held(TEXT("TurnRight"))-Held(TEXT("StrafeLeft"))-Held(TEXT("TurnLeft")),-1.f,1.f);
            const FVector Move=FRotationMatrix(Facing).GetUnitAxis(EAxis::X)*(Held(TEXT("MoveForward"))-Held(TEXT("MoveBackward")))+
                FRotationMatrix(Facing).GetUnitAxis(EAxis::Y)*Side;
            H->Mobility->ServerRoll(Move.IsNearlyZero()?H->GetActorForwardVector():Move.GetSafeNormal());
        }
    }
    // Movement input itself is applied by CireCamera::Tick (W/S drive, A/D turn or strafe).
}
void ACireController::ServerAction_Implementation(int32 Action,int32 Value,AActor* Selected) {
    auto* H=Cast<ACireHero>(GetPawn()); auto* M=GetWorld()->GetAuthGameMode<ACireGameMode>();if(!H||!M)return;
    if(Action==10) {CireWaveDirector::SetPlayerReady(H,Value!=0);return;} // wave-director: breather Ready (Skill Shop window)
    if(Action==9) {if(M->Clock.Phase()==Cires::MatchPhase::Finished)GetWorld()->ServerTravel(TEXT("/Game/Maps/Citadel"));return;}
    if(Action==5) {if(Value>=0&&Value<5&&!H->bDrafted)H->Draft(Value);return;}
    if(!H->bDrafted)return;
    if(Action==3) {if(Value>=0&&Value<4)H->Learn(Value);return;}
    if(Action==6) {H->Target=nullptr;return;}
    if(Action==0) {
        if(IsValid(Selected)&&(Cast<ACireHero>(Selected)||Cast<ACireMonster>(Selected)||Cast<ACireConstruct>(Selected))&&CireRealm::CanObserve(H,Selected))H->Target=Selected;
        return;
    }
    if(H->bDead)return;
    switch(Action) {
        case 1: H->bAutoAttack=!H->bAutoAttack;H->Notice=H->bAutoAttack?TEXT("Basic attack enabled"):TEXT("Basic attack stopped");break;
        case 2: if(Value>=0&&Value<Cires::MaxSkills&&H->Skills.IsValidIndex(Value)&&CireTargeting::Describe(H->Skills[Value]).Kind!=ECireTargetKind::Ground) {
            // feat/camera-movement: smart/mouseover casts name an explicit unit; it is used for this cast only
            // (timed casts capture it at start) and the selection is restored afterwards.
            AActor* Previous=H->Target;
            const bool bExplicit=IsValid(Selected)&&Selected!=Previous&&(Cast<ACireHero>(Selected)||Cast<ACireMonster>(Selected)||Cast<ACireConstruct>(Selected))&&CireRealm::CanObserve(H,Selected);
            if(bExplicit)H->Target=Selected;
            H->Cast(Value);
            if(bExplicit&&H->Target==Selected)H->Target=Previous;
        } break;
        case 4: if(Value>=0&&Value<4)H->Purchase(Value);break;
        // progression-shop: town recall merged into Teleport to Base: instant during prep/recovery,
        // a 6 s hearthstone channel during waves (damage or moving cancels), 120 s cooldown.
        case 8: CireItems::RequestTeleport(H);break;
        default: break;
    }
}

void ACireController::ServerDraftHover_Implementation(const FString& ProfileId) {
    // champion-select: remember the selected (not yet locked) champion for teammates and the timer.
    auto* H=Cast<ACireHero>(GetPawn());
    if(!H||H->bDrafted||ProfileId.Len()>64)return;
    H->DraftHoverId=ProfileId.IsEmpty()||CireChampionRoster::Find(ProfileId)?ProfileId:FString();
}
void ACireController::ServerDraftProfile_Implementation(const FString& ProfileId) {
    auto* H=Cast<ACireHero>(GetPawn());
    auto* M=GetWorld()->GetAuthGameMode<ACireGameMode>();
    if(!H||!M||H->bDrafted||H->bDead||ProfileId.IsEmpty()||ProfileId.Len()>64||
        M->Clock.Phase()==Cires::MatchPhase::Finished)return;
    // champion-draft: a champion locked by a human teammate cannot be locked again (bots never block).
    if(const ACireHero* Taken=CireChampionProfiles::PickedByTeammate(H,ProfileId,true)){H->Notice=Taken->HeroName+TEXT(" already locked that champion.");return;}
    H->DraftProfile(ProfileId);
}

FVector ACireController::CursorAim() const {
    FHitResult Hit;
    if(GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_Visibility),false,Hit))return Hit.ImpactPoint;
    const auto* H=Cast<ACireHero>(GetPawn());
    if(!H)return FVector::ZeroVector;
    return IsValid(H->Target)?H->Target->GetActorLocation():H->GetActorLocation()+H->GetActorForwardVector()*500.f;
}
void ACireController::RequestCast(int32 Slot){CireTargeting::Request(this,Slot);}
void ACireController::ServerCastAt_Implementation(int32 Slot,FVector_NetQuantize Point){
    auto* H=Cast<ACireHero>(GetPawn());
    if(!H||Slot<0||Slot>=Cires::MaxSkills||!H->Skills.IsValidIndex(Slot)||FVector(Point).ContainsNaN())return;
    if(CireTargeting::Describe(H->Skills[Slot]).Kind==ECireTargetKind::Ground)
    {
        FVector Center;FRotator Heading;FString Reason;
        if(!CireTargeting::ValidateGround(H,H->Skills[Slot],Point,Center,Heading,Reason)){H->Notice=Reason;return;}
        H->CastAt(Slot,Point);
    }
    else H->Cast(Slot);
}
void ACireController::ServerSummonCommand_Implementation(int32 Command,AActor* Selected,FVector_NetQuantize Point){
    auto* H=Cast<ACireHero>(GetPawn());
    if(!H||H->bDead||!H->bDrafted||Command<0||Command>3||FVector(Point).ContainsNaN())return;
    if(Command==2&&!H->IsHostile(Selected)) {
        // feat/camera-movement: pet/summon "attack" with no hostile selected takes the nearest hostile
        // the hero can see within tab range (server-side; the client camera is not known here).
        AActor* Best=nullptr;double BestD=FMath::Square(CireSelection::TabRange);
        for(TActorIterator<ACireMonster> It(GetWorld());It;++It)if(H->IsHostile(*It)&&CireRealm::CanObserve(H,*It)){const double D=FVector::DistSquared(H->GetActorLocation(),It->GetActorLocation());if(D<BestD){BestD=D;Best=*It;}}
        for(TActorIterator<ACireHero> It(GetWorld());It;++It)if(H->IsHostile(*It)&&CireRealm::CanObserve(H,*It)){const double D=FVector::DistSquared(H->GetActorLocation(),It->GetActorLocation());if(D<BestD){BestD=D;Best=*It;}}
        if(!Best){H->Notice=TEXT("No enemy nearby to attack.");return;}
        Selected=Best;
    }
    for(TActorIterator<ACireSummon> It(GetWorld());It;++It)
        if(It->GetOwnerHero()==H&&It->bCommandable)It->Command(static_cast<ECireSummonCommand>(Command),Point,Selected);
}
void ACireController::ServerPetCommand_Implementation(uint8 Command){
    // pets: the server acts on its own view of the caller's target (never a client-supplied actor).
    auto* H=Cast<ACireHero>(GetPawn());
    if(!H||Command>=static_cast<uint8>(ECirePetCommand::Count))return;
    CirePets::Command(H,static_cast<ECirePetCommand>(Command),H->Target);
}
void ACireController::ClientSpellEffect_Implementation(FName Skill,FVector_NetQuantize From,FVector_NetQuantize To,ECireSpellCue Cue,float Size,bool bSound){
    if(IsLocalController()&&!FVector(From).ContainsNaN()&&!FVector(To).ContainsNaN()&&FMath::IsFinite(Size))
        CireSpellPresentation::Play(GetWorld(),Skill,From,To,Cue,FMath::Clamp(Size,.1f,8.f),bSound);
}
