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
#include "GameFramework/SpringArmComponent.h"

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
        if(Now-Probe.StepStarted<0.75) Hero->AddMovementInput(FVector(0,1,0));
        else {
            const double Displacement=FVector::Dist2D(Probe.MovementOrigin,Hero->GetActorLocation());
            if(Displacement<100) {Fail(TEXT("remote character movement did not advance"));return true;}
            UE_LOG(LogCireNetClient,Display,TEXT("CIRE_NET_CLIENT_MOVEMENT_PASS distance_cm=%.1f"),Displacement);
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
    Super::EndPlay(EndPlayReason);
}
void ACireController::CycleTarget(bool bFriendly) {
    auto* H=Cast<ACireHero>(GetPawn()); if(!H){CireTargeting::Cleanup(this);return;}
    TArray<AActor*> Candidates;
    for(TActorIterator<ACireMonster> It(GetWorld());It;++It) if(!bFriendly&&H->IsHostile(*It)&&H->InRange(*It,2500))Candidates.Add(*It);
    for(TActorIterator<ACireHero> It(GetWorld());It;++It) if(!It->bDead&&*It!=H&&H->InRange(*It,2500)&&((bFriendly&&It->TeamId==H->TeamId)||(!bFriendly&&H->IsHostile(*It))))Candidates.Add(*It);
    Candidates.Sort([H](const AActor& A,const AActor& B){return FVector::DistSquared(H->GetActorLocation(),A.GetActorLocation())<FVector::DistSquared(H->GetActorLocation(),B.GetActorLocation());});
    if(Candidates.Num()) {int I=Candidates.IndexOfByKey(H->Target);ServerAction(0,(I+1)%Candidates.Num(),Candidates[(I+1)%Candidates.Num()]);}
}
void ACireController::PlayerTick(float Dt) {
    Super::PlayerTick(Dt); if(!IsLocalController())return;
#if !UE_BUILD_SHIPPING
    if(CireExpansionNetProbe::TickClient(this))return;
    if(CireInterfaceProbe::TickClient(this))return;
    if(TickClientProbe(this))return;
#endif
    auto* H=Cast<ACireHero>(GetPawn()); if(!H){CireTargeting::Cleanup(this);return;}
    auto* Interface=Cast<ACireHUD>(GetHUD());
    // Release state even if a menu/chat consumes the rest of this frame.
    if(WasInputKeyJustReleased(EKeys::E))H->StopJumping();
    if(H->Mobility&&H->Mobility->bStrafing&&!IsInputKeyDown(EKeys::RightMouseButton))
    {H->Mobility->bStrafing=false;H->Mobility->ServerSetStrafe(false);}
    if(Interface) {
        const auto& Options=Interface->UISettings;
        H->Camera->SetFieldOfView(Options.CameraFOV);
        H->Arm->TargetArmLength=Options.CameraDistance;
        H->Camera->PostProcessSettings.bOverride_BloomIntensity=true;
        H->Camera->PostProcessSettings.BloomIntensity=Options.bBloom?.65f:0.f;
        H->Camera->PostProcessSettings.bOverride_MotionBlurAmount=true;
        H->Camera->PostProcessSettings.MotionBlurAmount=Options.bMotionBlur?.35f:0.f;
    }
    CireSelection::Update(this);
    const bool bAimInputConsumed=CireTargeting::Tick(this);
    if(!IsValid(FocusTarget)||!CireRealm::CanObserve(H,FocusTarget))FocusTarget=nullptr;
    if(bChatInput) {
        CireTargeting::Cancel(this);
        if(WasInputKeyJustPressed(EKeys::Escape))CancelChat();
        else if(WasInputKeyJustPressed(EKeys::Enter))SubmitChat();
        else if(WasInputKeyJustPressed(EKeys::Tab))bChatTeamOnly=!bChatTeamOnly;
        else if(WasInputKeyJustPressed(EKeys::BackSpace)&&!ChatDraft.IsEmpty())ChatDraft.LeftChopInline(1);
        return;
    }
    if(WasInputKeyJustPressed(EKeys::Enter)) {CireTargeting::Cancel(this);BeginChat();return;}
    if(WasInputKeyJustPressed(EKeys::F10)&&Interface)Interface->ToggleLayoutEditor();
    if(WasInputKeyJustPressed(EKeys::F9)&&Interface)Interface->ToggleSettings();
    if(WasInputKeyJustPressed(EKeys::F8)&&Interface)Interface->ToggleDeveloperTools();
    if(WasInputKeyJustPressed(EKeys::Escape)) {
        if(bAimInputConsumed)return;
        if(bSummonMoveTargeting){bSummonMoveTargeting=false;H->Notice=TEXT("Summon order cancelled.");return;}
        if(Interface&&Interface->HandleEscape())return;
        bShop=false;bHelp=false;
    }
    if(Interface) {
        if(WasInputKeyJustPressed(EKeys::MouseScrollUp))Interface->HandleMouseWheel(1);
        if(WasInputKeyJustPressed(EKeys::MouseScrollDown))Interface->HandleMouseWheel(-1);
        if(Interface->IsBlockingGameplayInput()){CireTargeting::Cancel(this);return;}
    }
    if(WasInputKeyJustPressed(EKeys::H))bHelp=!bHelp;
    if(WasInputKeyJustPressed(EKeys::B))bShop=!bShop;
    if(WasInputKeyJustPressed(EKeys::Tab))CycleTarget(false);
    if(WasInputKeyJustPressed(EKeys::F))CycleTarget(true);
    if(WasInputKeyJustPressed(EKeys::F1))ServerAction(0,0,H);
    if(WasInputKeyJustPressed(EKeys::SpaceBar))ServerAction(1,0,nullptr);
    if(WasInputKeyJustPressed(EKeys::R))ServerAction(8,0,nullptr);
    if(WasInputKeyJustPressed(EKeys::Q)&&H->bDrafted&&H->Offers.IsEmpty()&&!bShop) {
        const int32 Slot=H->UltimateSkillSlot();if(Slot!=INDEX_NONE)RequestCast(Slot);
    }
    const FKey Keys[]={EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Six};
    if(!H->bDrafted&&Interface) {
        if(WasInputKeyJustPressed(EKeys::Left)||WasInputKeyJustPressed(EKeys::PageUp))Interface->ChangeDraftRosterPage(-1);
        if(WasInputKeyJustPressed(EKeys::Right)||WasInputKeyJustPressed(EKeys::PageDown))Interface->ChangeDraftRosterPage(1);
    }
    for(int I=0;I<6;++I)if(WasInputKeyJustPressed(Keys[I])) {
        if(!H->bDrafted) {if(Interface)Interface->DraftRosterSlot(I);else if(I<5)ServerAction(5,I,nullptr);}
        else if(H->Offers.Num()>0&&I<4)ServerAction(3,I,nullptr);
        else if(H->Offers.IsEmpty()&&!bShop) {const int32 Slot=H->ActiveSkillSlot(I);if(Slot!=INDEX_NONE)RequestCast(Slot);}
    }
    if(H->bDrafted&&!bShop&&H->Offers.IsEmpty()&&WasInputKeyJustPressed(EKeys::LeftMouseButton)
        &&!bAimInputConsumed&&!CireTargeting::Snapshot(this).bActive
        &&!IsInputKeyDown(EKeys::RightMouseButton)&&(!Interface||!Interface->IsPointerOverInterface())) {
        if(bSummonMoveTargeting||IsInputKeyDown(EKeys::LeftShift)||IsInputKeyDown(EKeys::RightShift)) {
            ServerSummonCommand(1,nullptr,CursorAim());bSummonMoveTargeting=false;return;
        }
        FHitResult CursorHit;
        if(GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_GameTraceChannel1),false,CursorHit)) {
            AActor* Selected=CursorHit.GetActor();
            if((Cast<ACireHero>(Selected)||Cast<ACireMonster>(Selected)||Cast<ACireConstruct>(Selected))&&CireRealm::CanObserve(H,Selected))ServerAction(0,0,Selected);
            else ServerAction(6,0,nullptr);
        }
    }
    if(H->bDead||!H->bDrafted||bShop)return;
    if(WasInputKeyJustPressed(EKeys::E))H->Jump();
    if(H->Mobility)
    {
        if(WasInputKeyJustPressed(EKeys::CapsLock)){H->Mobility->bWalking=!H->Mobility->bWalking;H->Mobility->ServerSetWalk(H->Mobility->bWalking);}
        const bool Strafe=IsInputKeyDown(EKeys::RightMouseButton);
        if(H->Mobility->bStrafing!=Strafe){H->Mobility->bStrafing=Strafe;H->Mobility->ServerSetStrafe(Strafe);}
        if(WasInputKeyJustPressed(EKeys::LeftControl)||WasInputKeyJustPressed(EKeys::RightControl))
        {
            const FRotator Facing(0,GetControlRotation().Yaw,0);
            const FVector Move=FRotationMatrix(Facing).GetUnitAxis(EAxis::X)*((IsInputKeyDown(EKeys::W)?1.f:0.f)-(IsInputKeyDown(EKeys::S)?1.f:0.f))+
                FRotationMatrix(Facing).GetUnitAxis(EAxis::Y)*((IsInputKeyDown(EKeys::D)?1.f:0.f)-(IsInputKeyDown(EKeys::A)?1.f:0.f));
            H->Mobility->ServerRoll(Move.IsNearlyZero()?H->GetActorForwardVector():Move.GetSafeNormal());
        }
    }
    if(IsInputKeyDown(EKeys::RightMouseButton)) {
        float X,Y; GetInputMouseDelta(X,Y);
        const float YawSpeed=Interface?Interface->UISettings.CameraYawSensitivity:1.f;
        const float PitchSpeed=Interface?Interface->UISettings.CameraPitchSensitivity:1.f;
        const float Invert=Interface&&Interface->UISettings.bInvertMouseY?-1.f:1.f;
        auto R=GetControlRotation(); R.Yaw+=X*.24f*YawSpeed; R.Pitch=FMath::Clamp(FRotator::NormalizeAxis(R.Pitch)-Y*.2f*PitchSpeed*Invert,-65.f,-5.f);SetControlRotation(R);
    }
    const FRotator Yaw(0,GetControlRotation().Yaw,0);
    H->AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X),(IsInputKeyDown(EKeys::W)?1.f:0.f)-(IsInputKeyDown(EKeys::S)?1.f:0.f));
    H->AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y),(IsInputKeyDown(EKeys::D)?1.f:0.f)-(IsInputKeyDown(EKeys::A)?1.f:0.f));
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
