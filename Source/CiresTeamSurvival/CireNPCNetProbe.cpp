#include "CireNPCNetProbe.h"
#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"
#include "CireNPCArchetypes.h"
#include "CireThreat.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireNPCNet,Log,All);
namespace
{
struct FServerState{bool bEnabled=false,bSpawned=false,bDone=false;double Started=0,SpawnedAt=0;TWeakObjectPtr<ACireMonster> Leader;};
FServerState Server;

struct FClientState{bool bDraftSent=false,bDone=false,bAggroEvent=false;double Started=0;FDelegateHandle Handle;};
FClientState Client;

bool ClientTick(float)
{
    if(Client.bDone||!GEngine)return !Client.bDone;
    const double Now=FPlatformTime::Seconds();if(Client.Started==0)Client.Started=Now;
    auto Finish=[&](bool bPass,const TCHAR* Why)
    {
        Client.bDone=true;UCireNPCState::OnAggroChanged().Remove(Client.Handle);
        UE_LOG(LogCireNPCNet,Display,TEXT("CIRE_NPC_NET_CLIENT_%s %s"),bPass?TEXT("PASS"):TEXT("FAIL"),Why);
        FPlatformMisc::RequestExitWithStatus(false,bPass?0:1);
    };
    if(Now-Client.Started>80){Finish(false,TEXT("timeout waiting for replicated NPC state"));return false;}
    UWorld* World=nullptr;
    for(const auto& Context:GEngine->GetWorldContexts())if(Context.World()&&Context.World()->GetNetMode()==NM_Client)World=Context.World();
    if(!World)return true;
    auto* Controller=Cast<ACireController>(World->GetFirstPlayerController());
    auto* Hero=Controller?Cast<ACireHero>(Controller->GetPawn()):nullptr;
    if(!Hero||Hero->TeamId<0)return true;
    if(!Hero->bDrafted){if(!Client.bDraftSent){Controller->ServerAction(5,0,nullptr);Client.bDraftSent=true;}return true;}
    const FName LeaderId=CireNPCArchetypes::Get().PackLeader;
    for(TActorIterator<ACireMonster> It(World);It;++It)
    {
        auto* M=*It;auto* S=M->NPCState.Get();
        if(!S||S->ArchetypeId!=LeaderId||M->PackId!=77077||M->HasAuthority())continue; // ignore regular challenge-pack leaders
        const auto Abilities=S->Abilities();const auto Cast=S->CastInfo();
        const bool bThreat=S->ThreatTable.ContainsByPredicate([Hero](const FCireThreatEntry& E){return E.Hero==Hero&&E.Threat>0;});
        const bool bReady=M->GetNPCRole()==ECireNPCRole::Bruiser&&M->GetNPCClassification()==ECireNPCClass::Boss&&Abilities.Num()>=4&&
            Cast.bCasting&&!Cast.bInterruptible&&Cast.Name==TEXT("Sundering Cleave")&&bThreat&&S->Aggro.Target==Hero&&M->Victim==Hero&&
            FMath::IsNearlyEqual(S->ThreatPercent(Hero),100.f)&&(S->StatusFlags&CireNPCStatus::Rallied)!=0;
        if(!bReady)
        {
            static double LastDiag=0;
            if(Now-LastDiag>2)
            {
                LastDiag=Now;
                UE_LOG(LogCireNPCNet,Display,TEXT("CIRE_NPC_NET_CLIENT_WAIT role=%d class=%d abilities=%d casting=%d name=%s interruptible=%d threat=%d aggro=%d victim=%d pct=%.1f flags=%d event=%d"),
                    (int32)M->GetNPCRole(),(int32)M->GetNPCClassification(),Abilities.Num(),Cast.bCasting,*Cast.Name,Cast.bInterruptible,bThreat,
                    S->Aggro.Target==Hero,M->Victim==Hero,S->ThreatPercent(Hero),S->StatusFlags,Client.bAggroEvent);
            }
            return true;
        }
        UE_LOG(LogCireNPCNet,Display,TEXT("CIRE_NPC_NET_CLIENT_STATE name=%s role=%s class=%s abilities=%d cast=%s progress=%.2f threat_rows=%d aggro_event=%d text=\"%s\""),
            *M->GetNPCDisplayName(),*CireNPCArchetypes::RoleLabel(M->GetNPCRole()),*CireNPCArchetypes::ClassLabel(M->GetNPCClassification()),
            Abilities.Num(),*Cast.Name,Cast.Progress,S->ThreatTable.Num(),Client.bAggroEvent?1:0,*UCireNPCState::DescribeFocus(M));
        Finish(Client.bAggroEvent,Client.bAggroEvent?TEXT("role/boss/cast/threat/aggro replicated"):TEXT("aggro delegate did not fire on the client"));
        return false;
    }
    return true;
}

struct FClientBootstrap
{
    FClientBootstrap()
    {
        FCoreDelegates::OnPostEngineInit.AddLambda([]
        {
            if(!FParse::Param(FCommandLine::Get(),TEXT("CireNPCNetClient")))return;
            Client.Handle=UCireNPCState::OnAggroChanged().AddLambda([](const FCireAggroEvent& E)
            {
                if(E.Monster.IsValid()&&!E.Monster->HasAuthority()&&E.Monster->PackId==77077&&E.NewTarget.IsValid())Client.bAggroEvent=true;
            });
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&ClientTick),.1f);
            UE_LOG(LogCireNPCNet,Display,TEXT("CIRE_NPC_NET_CLIENT_READY"));
        });
    }
};
FClientBootstrap Bootstrap;
}

void CireNPCNetProbe::InitializeServer(ACireGameMode* Mode)
{
    Server={};
    Server.bEnabled=Mode&&Mode->GetNetMode()==NM_DedicatedServer&&FParse::Param(FCommandLine::Get(),TEXT("CireNPCNetServer"));
    if(!Server.bEnabled)return;
    Server.Started=FPlatformTime::Seconds();Mode->bBotsFilled=true;Mode->BotFillTimer=MAX_flt;Mode->WaveTimer=MAX_flt;
    UE_LOG(LogCireNPCNet,Display,TEXT("CIRE_NPC_NET_SERVER_READY"));
}

bool CireNPCNetProbe::TickServer(ACireGameMode* Mode)
{
    if(!Server.bEnabled)return false;
    if(Server.bDone)return true;
    const double Now=FPlatformTime::Seconds();
    auto Finish=[&](bool bPass,const TCHAR* Why)
    {Server.bDone=true;UE_LOG(LogCireNPCNet,Display,TEXT("CIRE_NPC_NET_SERVER_%s %s"),bPass?TEXT("PASS"):TEXT("FAIL"),Why);FPlatformMisc::RequestExitWithStatus(false,bPass?0:1);};
    if(Now-Server.Started>85){Finish(false,TEXT("client never drafted"));return true;}
    if(!Server.bSpawned)
    {
        for(auto* Hero:Mode->Heroes)
        {
            if(!IsValid(Hero)||Hero->bBot||!Hero->IsPlayerControlled()||!Hero->bDrafted)continue;
            FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            const FVector At=Hero->GetActorLocation()+Hero->GetActorForwardVector()*300.f+FVector(0,0,60);
            auto* M=Mode->GetWorld()->SpawnActor<ACireMonster>(At,FRotator::ZeroRotator,P);
            if(!M){Finish(false,TEXT("leader spawn failed"));return true;}
            M->Lane=Hero->TeamId;M->PackId=77077;Mode->Monsters.Add(M);
            CireNPCCombat::ConfigureArchetype(M,CireNPCArchetypes::Get().PackLeader,1,2,1);
            M->SetActorLocation(At);M->SpawnPosition=At;
            Hero->Health=Hero->MaxHealth=1.e6f;
            CireThreat::Damage(M,Hero,50.f);
            // One authoritative tick starts a telegraphed ability; then freeze so the cast stays visible.
            M->NPCState->ReadyAt.Reset();M->NPCState->ReadyAt.Add(TEXT("boss_leader_rally"),M->GetWorld()->GetTimeSeconds()+600);
            M->NPCState->RallyUntil=M->GetWorld()->GetTimeSeconds()+600;M->NPCState->RallyBonus=.25f;
            M->AbilityTimer=0;M->AttackTimer=10;CireNPCCombat::Tick(M,.01f);
            M->SetActorTickEnabled(false);M->GetCharacterMovement()->DisableMovement();
            M->CastEndsAt=M->GetWorld()->GetTimeSeconds()+600;M->ForceNetUpdate();
            Server.Leader=M;Server.bSpawned=true;Server.SpawnedAt=Now;
            UE_LOG(LogCireNPCNet,Display,TEXT("CIRE_NPC_NET_SERVER_SPAWNED hero=%s cast=%s victim=%d"),*Hero->HeroName,*M->CastingAbility,M->Victim==Hero);
            break;
        }
        return true;
    }
    if(auto* M=Server.Leader.Get())M->NPCState->PublishThreat(true);
    // The server stays up long enough for the client to verify, then exits.
    if(Now-Server.SpawnedAt>45)Finish(Server.Leader.IsValid()&&!Server.Leader->CastingAbility.IsEmpty(),TEXT("leader state served"));
    return true;
}
#endif
