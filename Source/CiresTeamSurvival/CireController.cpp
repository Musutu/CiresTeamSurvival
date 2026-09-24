#include "CireGame.h"
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
            !FMath::IsNearlyEqual(Hero->MaxHealth,Hero->Strength*25.f)||
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
            Probe.MovementOrigin=Hero->GetActorLocation();
        }
        if(Now-Probe.StepStarted<0.75) Hero->AddMovementInput(FRotationMatrix(FRotator(0,Probe.StrafeYaw,0)).GetUnitAxis(EAxis::Y));
        else {
            const FVector Delta=Hero->GetActorLocation()-Probe.MovementOrigin;
            const double Lateral=FVector::DotProduct(Delta,FRotationMatrix(Heading).GetUnitAxis(EAxis::Y));
            const double Forward=FVector::DotProduct(Delta,FRotationMatrix(Heading).GetUnitAxis(EAxis::X));
            const float Turned=FMath::Abs(FRotator::NormalizeAxis(static_cast<float>(Hero->GetActorRotation().Yaw)-Probe.StrafeYaw));
            if(Lateral<100||FMath::Abs(Forward)>Lateral*.2||Turned>3.f) {
                UE_LOG(LogCireNetClient,Error,TEXT("CIRE_NET_CLIENT_STRAFE lateral=%.1f forward=%.1f turned=%.1f"),Lateral,Forward,Turned);
                Fail(TEXT("remote strafe did not move sideways without rotating"));return true;}
            if(Hero->Mobility){Hero->Mobility->bFaceControl=false;Hero->Mobility->ServerSetFaceControl(false);}
            UE_LOG(LogCireNetClient,Display,TEXT("CIRE_NET_CLIENT_MOVEMENT_PASS distance_cm=%.1f strafe_lateral_cm=%.1f forward_cm=%.1f yaw_change=%.2f"),Delta.Size2D(),Lateral,Forward,Turned);
            Probe.Step=6;Probe.StepStarted=Now;
        }
    } else if(Probe.Step==6) {
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
#if !UE_BUILD_SHIPPING
    if(CireExpansionNetProbe::TickClient(this))return;
    if(CireInterfaceProbe::TickClient(this))return;
    if(TickClientProbe(this))return;
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
    const bool bOverUI=Interface&&Interface->IsPointerOverInterface();
    CireCamera::FFrame CameraFrame;
    CameraFrame.bMouseAllowed=!bBlockingUI;
    CameraFrame.bSteeringAllowed=!bChatInput&&!bBlockingUI&&H->bDrafted&&!H->bDead&&!bShop;
    CameraFrame.bPressEligible=!bAimInputConsumed&&!CireTargeting::Snapshot(this).bActive&&!bSummonMoveTargeting&&
        !IsInputKeyDown(EKeys::LeftShift)&&!IsInputKeyDown(EKeys::RightShift);
    CameraFrame.bPointerOverInterface=bOverUI;
    if(!bOverUI&&!bBlockingUI)CameraFrame.WheelSteps=(WasInputKeyJustPressed(EKeys::MouseScrollUp)?1.f:0.f)-(WasInputKeyJustPressed(EKeys::MouseScrollDown)?1.f:0.f);
    CameraFrame.Options=Interface?&Interface->UISettings:nullptr;
    CameraFrame.Bindings=&Keys;
    // Rebinding capture (keybinding screen) owns the keyboard until it binds, unbinds or cancels.
    if(MutableKeys&&MutableKeys->IsCapturing())CameraFrame.bSteeringAllowed=false;
    const auto Camera=CireCamera::Tick(this,H,Dt,CameraFrame);
    if(MutableKeys&&MutableKeys->IsCapturing()) {
        FCireCaptureResult Captured;
        if(MutableKeys->TickCapture(this,Captured)&&(Captured.Kind==FCireCaptureResult::Bound||Captured.Kind==FCireCaptureResult::Unbound))Interface->UISettings.Save();
        return;
    }
    CireSelection::HandleTargetLoss(this,Interface&&Interface->UISettings.bAutoReacquireTarget);
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
        bShop=false;bHelp=false;
    }
    if(Interface) {
        // Wheel over HUD panels scrolls them (chat); over the world it zooms the camera (CireCamera).
        if(bOverUI&&WasInputKeyJustPressed(EKeys::MouseScrollUp))Interface->HandleMouseWheel(1);
        if(bOverUI&&WasInputKeyJustPressed(EKeys::MouseScrollDown))Interface->HandleMouseWheel(-1);
        if(Interface->IsBlockingGameplayInput()){CireTargeting::Cancel(this);return;}
    }
    if(Keys.WasPressed(this,TEXT("ToggleHelp")))bHelp=!bHelp;
    if(Keys.WasPressed(this,TEXT("ToggleShop")))bShop=!bShop;
    if(Keys.WasPressed(this,TEXT("TargetNextEnemy")))CycleTargetDirected(this,false,false);
    if(Keys.WasPressed(this,TEXT("TargetPreviousEnemy")))CycleTargetDirected(this,false,true);
    if(Keys.WasPressed(this,TEXT("TargetNextAlly")))CycleTarget(true);
    if(Keys.WasPressed(this,TEXT("TargetSelf")))ServerAction(0,0,H);
    if(Keys.WasPressed(this,TEXT("ToggleAutoAttack")))ServerAction(1,0,nullptr);
    if(Keys.WasPressed(this,TEXT("RecallToTown")))ServerAction(8,0,nullptr);
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
        else if(!bShop) {const int32 Skill=CireKeybindings::ResolveSlot(Keys,*H,Slot);if(Skill!=INDEX_NONE)RequestCast(Skill);}
    }
    const bool bOfferModal=H->Offers.Num()>0&&(!Interface||Interface->IsSkillOfferOpen()); // champion-draft
    if(H->bDrafted&&!bShop&&!bOfferModal&&WasInputKeyJustPressed(EKeys::LeftMouseButton)
        &&!bAimInputConsumed&&!CireTargeting::Snapshot(this).bActive&&(!Interface||!Interface->IsPointerOverInterface())
        &&(bSummonMoveTargeting||IsInputKeyDown(EKeys::LeftShift)||IsInputKeyDown(EKeys::RightShift))) {
        ServerSummonCommand(1,nullptr,CursorAim());bSummonMoveTargeting=false;return;
    }
    // WoW: a left click (released without dragging the camera) selects; a left drag only orbits.
    if(Camera.bClick&&H->bDrafted&&!bShop&&!bOfferModal&&!CireTargeting::Snapshot(this).bActive
        &&!IsInputKeyDown(EKeys::RightMouseButton)) {
        FHitResult CursorHit;
        if(GetHitResultAtScreenPosition(Camera.ClickPosition,UEngineTypes::ConvertToTraceType(ECC_GameTraceChannel1),false,CursorHit)) {
            AActor* Selected=CursorHit.GetActor();
            if((Cast<ACireHero>(Selected)||Cast<ACireMonster>(Selected)||Cast<ACireConstruct>(Selected))&&CireRealm::CanObserve(H,Selected))ServerAction(0,0,Selected);
            else ServerAction(6,0,nullptr);
        }
    }
    if(H->bDead||!H->bDrafted||bShop)return;
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
        case 2: if(Value>=0&&Value<Cires::MaxSkills&&H->Skills.IsValidIndex(Value)&&CireTargeting::Describe(H->Skills[Value]).Kind!=ECireTargetKind::Ground)H->Cast(Value);break;
        case 4: if(Value>=0&&Value<4)H->Purchase(Value);break;
        case 8: if(M->Clock.Phase()==Cires::MatchPhase::Intermission||M->Clock.Phase()==Cires::MatchPhase::Recovery)H->ReviveAt(M->BasePosition(H->TeamId));else H->Notice=TEXT("Town recall is available during prep or recovery.");break;
        default: break;
    }
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
    if(Command==2&&!H->IsHostile(Selected))return;
    for(TActorIterator<ACireSummon> It(GetWorld());It;++It)
        if(It->GetOwnerHero()==H&&It->bCommandable)It->Command(static_cast<ECireSummonCommand>(Command),Point,Selected);
}
void ACireController::ClientSpellEffect_Implementation(FName Skill,FVector_NetQuantize From,FVector_NetQuantize To,ECireSpellCue Cue,float Size,bool bSound){
    if(IsLocalController()&&!FVector(From).ContainsNaN()&&!FVector(To).ContainsNaN()&&FMath::IsFinite(Size))
        CireSpellPresentation::Play(GetWorld(),Skill,From,To,Cue,FMath::Clamp(Size,.1f,8.f),bSound);
}
