#include "CirePlaySession.h"
#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireCamera.h"
#include "CireTargeting.h"
#include "CireKeybindings.h"
#include "CireAreaEffects.h"
#include "CireSummon.h"
#include "CireMobility.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogCirePlaySession, Log, All);

namespace
{
struct FSession
{
    int32 Stage=0,F=0,Checks=0;bool bPass=true,bDone=false,bSetup=false,bSawCast=false;
    TWeakObjectPtr<ACireMonster> A,B;
    FVector Pos0=FVector::ZeroVector,LastAim=FVector::ZeroVector;float Yaw0=0,CamYaw0=0,MinSpeed=1e9f,SpeedAtCast=-1;
    int32 Areas0=0,Summons0=0;double Started=0;
};
FSession G;

void Key(ACireController* C,const FKey& K,bool bDown){C->InputKey(FInputKeyEventArgs::CreateSimulated(K,bDown?IE_Pressed:IE_Released,bDown?1.f:0.f));}
void Mouse(ACireController* C,float X,float Y)
{
    C->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseX,IE_Axis,X,1));
    C->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseY,IE_Axis,Y,1));
}
void Check(bool bOk,const FString& Name)
{
    ++G.Checks;G.bPass&=bOk;
    if(bOk){UE_LOG(LogCirePlaySession,Display,TEXT("CIRE_PLAY_CHECK_PASS %s"),*Name);}
    else{UE_LOG(LogCirePlaySession,Error,TEXT("CIRE_PLAY_CHECK_FAIL %s"),*Name);}
}
int32 Areas(UWorld* W){int32 N=0;for(TActorIterator<ACireAreaEffect> It(W);It;++It)if(!It->IsActorBeingDestroyed())++N;return N;}
int32 Summons(UWorld* W,ACireHero* H){int32 N=0;for(TActorIterator<ACireSummon> It(W);It;++It)if(!It->IsActorBeingDestroyed()&&It->GetOwnerHero()==H&&!It->bDead)++N;return N;}
FKey SlotKey(const FCireKeybindings& K,int32 Slot){return K.Get(CireKeybindings::SlotAction(1,Slot),0).Key;}
void Next(){++G.Stage;G.F=0;}
void Ready(ACireHero* H)
{
    for(float& Cd:H->Cooldowns)Cd=0;H->GlobalCooldown=0;H->Mana=H->MaxMana;H->Energy=100;
}
}

bool CirePlaySession::IsActive()
{
    static const bool bActive=FParse::Param(FCommandLine::Get(),TEXT("CirePlaySession"));
    return bActive;
}

void CirePlaySession::Tick(ACireController* C,float Dt)
{
    if(!IsActive()||G.bDone||!C||!C->IsLocalController())return;
    UWorld* W=C->GetWorld();const double Now=FPlatformTime::Seconds();
    if(G.Started==0)G.Started=Now;
    auto Finish=[&](const TCHAR* Why)
    {
        G.bDone=true;CireTargeting::DebugSetAimOverride({});
        UE_LOG(LogCirePlaySession,Display,TEXT("CIRE_PLAY_SESSION_%s checks=%d stage=%d %s"),G.bPass?TEXT("PASS"):TEXT("FAIL"),G.Checks,G.Stage,Why);
        FPlatformMisc::RequestExitWithStatus(false,G.bPass?0:1);
    };
    if(Now-G.Started>120){G.bPass=false;Finish(TEXT("timeout"));return;}
    auto* H=Cast<ACireHero>(C->GetPawn());auto* HUD=Cast<ACireHUD>(C->GetHUD());auto* Mode=W->GetAuthGameMode<ACireGameMode>();
    if(!H||!HUD||!Mode)return;
    const FCireKeybindings& K=HUD->UISettings.Keybindings;
    const auto Speed=[&]{return static_cast<float>(H->GetVelocity().Size2D());};
    const auto Aim=[&](float Ahead)
    {
        const FVector Fwd=FRotator(0,C->GetControlRotation().Yaw,0).Vector();
        G.LastAim=H->GetActorLocation()+Fwd*Ahead;
        FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(CirePlayAim),false,H);
        if(W->LineTraceSingleByObjectType(Hit,G.LastAim+FVector(0,0,300),G.LastAim-FVector(0,0,900),FCollisionObjectQueryParams(ECC_WorldStatic),Q))G.LastAim=Hit.ImpactPoint;
        CireTargeting::DebugSetAimOverride(G.LastAim);
    };
    const int32 F=G.F++;
    switch(G.Stage)
    {
    case 0: // draft a champion and build an isolated fixture
        if(!H->bDrafted){if(F%30==0)C->ServerAction(5,4,nullptr);return;}
        if(!G.bSetup)
        {
            G.bSetup=true;Mode->WaveTimer=1e6f;Mode->BotFillTimer=1e6f;
            H->Offers.Reset();H->Skills={TEXT("shield_slam"),TEXT("venom_ground"),TEXT("restoring_light"),TEXT("spectral_pack")};
            H->Cooldowns={0,0,0,0};H->MaxMana=H->Mana=5000;H->MaxHealth=H->Health=5000;Ready(H);
            auto& O=HUD->UISettings;O.bSmartCast=true;O.bMouseoverCast=false;O.bRightClickCancelsAim=true;O.bPressAgainToCast=true;
            O.bAutoStopToCast=true;O.bCameraAutoFollow=true;O.bQuickGroundCast=false;
            FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            const FVector Fwd=H->GetActorForwardVector(),Right=H->GetActorRightVector();
            auto Spawn=[&](FVector At){auto* M=W->SpawnActor<ACireMonster>(At,FRotator::ZeroRotator,P);
                if(M){M->SetActorTickEnabled(false);M->Lane=H->TeamId;M->Health=M->MaxHealth=50000;M->GetCharacterMovement()->SetComponentTickEnabled(false);}return M;};
            G.A=Spawn(H->GetActorLocation()+Fwd*180+FVector(0,0,10));G.B=Spawn(H->GetActorLocation()+Fwd*1200+Right*250+FVector(0,0,10));
            Check(G.A.IsValid()&&G.B.IsValid()&&H->IsHostile(G.A.Get()),TEXT("fixture: drafted hero, two hostile monsters in the hero's lane"));
            UE_LOG(LogCirePlaySession,Display,TEXT("CIRE_PLAY_SETUP hero=%s team=%d at=%s"),*H->ChampionProfileId,H->TeamId,*H->GetActorLocation().ToString());
        }
        if(F>30)Next();
        return;
    case 1: // smart cast: no target, enemy spell selects the enemy in front and casts
        if(F==0)C->ServerAction(6,0,nullptr);
        if(F==3)Key(C,SlotKey(K,1),true);
        if(F==4)Key(C,SlotKey(K,1),false);
        if(F==15)
        {
            Check(H->Target==G.A.Get(),TEXT("smart cast: no target -> selects the enemy in front"));
            Check(H->Cooldowns[0]>0,FString::Printf(TEXT("smart cast: the enemy spell was cast (notice: %s)"),*H->Notice));
            if(G.A.IsValid())G.A->SetActorLocation(H->GetActorLocation()+H->GetActorForwardVector()*1600-H->GetActorRightVector()*400+FVector(0,0,10));
            Next();
        }
        return;
    case 2: // target retention: strafe (E), RMB steer with mouse deltas, quick LMB tap, LMB drag orbit
        if(F==0){C->ServerAction(0,0,G.B.Get());}
        if(F==2){Key(C,K.Get(TEXT("StrafeRight"),0).Key,true);}
        if(F==8){G.Pos0=H->GetActorLocation();G.Yaw0=H->GetActorRotation().Yaw;}
        if(F==30)
        {
            const FVector D=H->GetActorLocation()-G.Pos0;const FVector Right=FRotator(0,G.Yaw0,0).Quaternion().GetRightVector();
            const float Lateral=FVector::DotProduct(D,Right),Turned=FMath::Abs(FRotator::NormalizeAxis(H->GetActorRotation().Yaw-G.Yaw0));
            Check(Lateral>100&&FMath::Abs(FVector::DotProduct(D,FRotator(0,G.Yaw0,0).Vector()))<Lateral*.1f&&Turned<.5f,
                FString::Printf(TEXT("E strafes sideways without turning (lateral %.0f cm, yaw %.2f)"),Lateral,Turned));
            Key(C,EKeys::RightMouseButton,true);
        }
        if(F>30&&F<=60)Mouse(C,8,0);
        if(F==61)Key(C,EKeys::RightMouseButton,false);
        if(F==65)Key(C,EKeys::LeftMouseButton,true);
        if(F==66)Key(C,EKeys::LeftMouseButton,false);
        if(F==70)Key(C,EKeys::LeftMouseButton,true);
        if(F>70&&F<=90)Mouse(C,6,1);
        if(F==91)Key(C,EKeys::LeftMouseButton,false);
        if(F==95)Key(C,K.Get(TEXT("StrafeRight"),0).Key,false);
        if(F==110)
        {
            Check(H->Target==G.B.Get(),TEXT("target kept through strafing, RMB steering, a quick LMB tap on the ground and an LMB camera drag"));
            Next();
        }
        return;
    case 3: // Eric's flow: key arms reticle -> W -> RMB down + pan -> RMB up -> LMB click casts at reticle while moving
        if(F==0){Ready(H);G.Areas0=Areas(W);G.MinSpeed=1e9f;}
        if(F>=1)Aim(420.f);
        if(F==2)Key(C,SlotKey(K,2),true);
        if(F==3)Key(C,SlotKey(K,2),false);
        if(F==6){Check(CireTargeting::Snapshot(C).bActive,TEXT("ability key arms the ground reticle"));Key(C,K.Get(TEXT("MoveForward"),0).Key,true);}
        if(F==12)G.CamYaw0=CireCamera::ViewRotation(C).Yaw;
        if(F==30)Check(FMath::Abs(FRotator::NormalizeAxis(CireCamera::ViewRotation(C).Yaw-G.CamYaw0))<.1f&&Speed()>200,
            TEXT("no camera drift/snap while the reticle is armed and the hero runs (auto-follow paused)"));
        if(F==32)Key(C,EKeys::RightMouseButton,true);
        if(F>32&&F<=52)Mouse(C,10,-2);
        if(F==53)Key(C,EKeys::RightMouseButton,false);
        if(F>=20&&F<=75)G.MinSpeed=FMath::Min(G.MinSpeed,Speed());
        if(F==58)Check(CireTargeting::Snapshot(C).bActive,TEXT("reticle stays armed through WASD movement, RMB press, RMB pan and RMB release"));
        if(F==60)Key(C,EKeys::LeftMouseButton,true);
        if(F==61)Key(C,EKeys::LeftMouseButton,false);
        if(F==75)
        {
            bool bNear=false;for(TActorIterator<ACireAreaEffect> It(W);It;++It)if(FVector::Dist2D(It->GetActorLocation(),G.LastAim)<250)bNear=true;
            Check(Areas(W)>G.Areas0&&bNear&&!CireTargeting::Snapshot(C).bActive,FString::Printf(TEXT("clean LMB click casts at the reticle (notice: %s)"),*H->Notice));
            Check(G.MinSpeed>200,FString::Printf(TEXT("the character never stopped while aiming, steering and casting (min speed %.0f)"),G.MinSpeed));
            Key(C,K.Get(TEXT("MoveForward"),0).Key,false);Next();
        }
        return;
    case 4: // LMB drag does not confirm; clean RMB click cancels; pressing the key again casts at the reticle
        if(F==0){Ready(H);G.Areas0=Areas(W);}
        Aim(380.f);
        if(F==2)Key(C,SlotKey(K,2),true);
        if(F==3)Key(C,SlotKey(K,2),false);
        if(F==6)Key(C,EKeys::LeftMouseButton,true);
        if(F>6&&F<=20)Mouse(C,8,0);
        if(F==21)Key(C,EKeys::LeftMouseButton,false);
        if(F==25)Check(CireTargeting::Snapshot(C).bActive&&Areas(W)==G.Areas0,TEXT("LMB camera drag does not confirm the reticle"));
        if(F==27)Key(C,EKeys::RightMouseButton,true);
        if(F==28)Key(C,EKeys::RightMouseButton,false);
        if(F==32)Check(!CireTargeting::Snapshot(C).bActive&&Areas(W)==G.Areas0,TEXT("a clean RMB click cancels the reticle"));
        if(F==34)Key(C,SlotKey(K,2),true);
        if(F==35)Key(C,SlotKey(K,2),false);
        if(F==38)Check(CireTargeting::Snapshot(C).bActive,TEXT("re-armed"));
        if(F==40)Key(C,SlotKey(K,2),true);
        if(F==41)Key(C,SlotKey(K,2),false);
        if(F==50){Check(Areas(W)>G.Areas0&&!CireTargeting::Snapshot(C).bActive,TEXT("pressing the armed ability again casts at the reticle"));Next();}
        return;
    case 5: // cast-time heal while running: stop to cast (default), then movement resumes
        if(F==0){Ready(H);G.bSawCast=false;G.SpeedAtCast=-1;Key(C,K.Get(TEXT("MoveForward"),0).Key,true);}
        if(F==20){Check(Speed()>200,TEXT("running before the heal"));Key(C,SlotKey(K,3),true);}
        if(F==21)Key(C,SlotKey(K,3),false);
        if(F==24)Check(CireTargeting::HoldsMovement(C),TEXT("stop to cast holds the movement keys"));
        if(F>21&&!H->CastSkill.IsNone()&&!G.bSawCast){G.bSawCast=true;G.SpeedAtCast=Speed();}
        if(F==80)Check(G.bSawCast&&G.SpeedAtCast>=0&&G.SpeedAtCast<30,FString::Printf(TEXT("the cast-time heal started once the hero had stopped (speed %.0f)"),G.SpeedAtCast));
        if(F==200)
        {
            Check(G.bSawCast&&H->CastSkill.IsNone()&&H->Cooldowns[2]>0,FString::Printf(TEXT("the heal completed (notice: %s)"),*H->Notice));
            Check(!CireTargeting::HoldsMovement(C)&&Speed()>200,TEXT("movement resumes with W still held after the cast"));
            Key(C,K.Get(TEXT("MoveForward"),0).Key,false);Next();
        }
        return;
    case 6: // option off: "Can't cast while moving"
        if(F==0){Ready(H);HUD->UISettings.bAutoStopToCast=false;Key(C,K.Get(TEXT("MoveForward"),0).Key,true);}
        if(F==20)Key(C,SlotKey(K,3),true);
        if(F==21)Key(C,SlotKey(K,3),false);
        if(F==35)
        {
            Check(H->Notice.Contains(TEXT("Can't cast while moving"))&&H->CastSkill.IsNone()&&Speed()>200,
                FString::Printf(TEXT("stop-to-cast off: moving shows 'Can't cast while moving' and keeps running (notice: %s)"),*H->Notice));
            Key(C,K.Get(TEXT("MoveForward"),0).Key,false);Next();
        }
        return;
    case 7: // standing cast, then moving cancels it (WoW)
        if(F==25){Ready(H);Key(C,SlotKey(K,3),true);}
        if(F==26)Key(C,SlotKey(K,3),false);
        if(F==40){Check(!H->CastSkill.IsNone(),TEXT("standing still starts the cast"));Key(C,K.Get(TEXT("StrafeLeft"),0).Key,true);}
        if(F==55)
        {
            Check(H->CastSkill.IsNone()&&H->Notice.Contains(TEXT("Moved")),FString::Printf(TEXT("strafing cancels the cast-time spell (notice: %s)"),*H->Notice));
            Key(C,K.Get(TEXT("StrafeLeft"),0).Key,false);HUD->UISettings.bAutoStopToCast=true;Next();
        }
        return;
    case 8: // summon that needs an enemy, with no target: smart cast picks one and the reticle arms
        if(F==0)
        {
            Ready(H);C->ServerAction(6,0,nullptr);G.Summons0=Summons(W,H);
            if(G.A.IsValid())G.A->SetActorLocation(H->GetActorLocation()+H->GetActorForwardVector()*450+FVector(0,0,10));
        }
        if(F>=1)Aim(300.f);
        if(F==5)Key(C,SlotKey(K,4),true);
        if(F==6)Key(C,SlotKey(K,4),false);
        if(F==12)Check(H->IsHostile(H->Target)&&CireTargeting::Snapshot(C).bActive,FString::Printf(TEXT("summon with no target auto-selects an enemy and arms placement (notice: %s)"),*H->Notice));
        if(F==14)Key(C,EKeys::LeftMouseButton,true);
        if(F==15)Key(C,EKeys::LeftMouseButton,false);
        if(F==40){Check(Summons(W,H)>G.Summons0,FString::Printf(TEXT("summons placed at the reticle (notice: %s)"),*H->Notice));Next();}
        return;
    default:
        Finish(TEXT("complete"));
        return;
    }
}
#endif
