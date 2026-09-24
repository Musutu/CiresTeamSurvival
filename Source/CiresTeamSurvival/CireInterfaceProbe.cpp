#include "CireInterfaceProbe.h"
#include "CireGame.h"
#include "CireCombatEvents.h"
#include "CireSelection.h"
#include "CireChampionArt.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/BlendSpace.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Materials/MaterialInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireInterface, Log, All);

#if !UE_BUILD_SHIPPING
namespace {
struct FServerFixture {
    double Started = 0, StageStarted = 0;
    int32 Stage = 0;
    int32 Acks[5] = {0,0,0,0,0};
    bool bCombatSent = false, bDone = false;
} Server;
struct FClientFixture {
    double Started = 0, StepStarted = 0;
    double ArtWaitStarted = 0;
    int32 Step = 0;
    bool bDraftSent = false, bDone = false, bArtWaitLogged = false;
    TWeakObjectPtr<ACireHero> Enemy;
    TWeakObjectPtr<ACireHero> Ally;
    TWeakObjectPtr<UMaterialInterface> PreviousSelfOverlay, PreviousAllyOverlay;
} Client;

void Fail(const TCHAR* Side, const TCHAR* Reason) {
    UE_LOG(LogCireInterface, Error, TEXT("CIRE_INTERFACE_%s_FAIL reason=%s"), Side, Reason);
    FPlatformMisc::RequestExitWithStatus(false, 1);
}
bool Enabled(const TCHAR* Flag) { return FParse::Param(FCommandLine::Get(), Flag); }
void CountHeroes(UWorld* World, int32 Team, int32& Own, int32& Opposing, int32& VisibleOpposing) {
    Own = Opposing = VisibleOpposing = 0;
    for (TActorIterator<ACireHero> It(World); It; ++It) {
        if (!IsValid(*It)) continue;
        if (It->TeamId == Team) ++Own;
        else { ++Opposing; if (It->GetMesh()->IsVisible()) ++VisibleOpposing; }
    }
}
bool HasChat(const ACireController* Controller, const FString& Text, bool bTeam, int32 Team) {
    return Controller->ChatMessages.ContainsByPredicate([&](const FCireChatMessage& Item) {
        return Item.Text == Text && Item.bTeamOnly == bTeam && Item.SenderTeam == Team;
    });
}
int32 SelectionRingCount(UWorld* World) {
    int32 Count=0;
    for(TActorIterator<AActor> It(World);It;++It) {
        const auto* Segments=It->FindComponentByClass<UInstancedStaticMeshComponent>();
        if(Segments&&Segments->GetFName()==TEXT("SelectionRingSegments"))++Count;
    }
    return Count;
}
bool HasPrivateRing(ACireController* Controller, bool bVisible) {
    int32 Count=0;
    for(TActorIterator<AActor> It(Controller->GetWorld());It;++It) {
        const auto* Segments=It->FindComponentByClass<UInstancedStaticMeshComponent>();
        if(!Segments||Segments->GetFName()!=TEXT("SelectionRingSegments"))continue;
        ++Count;
        if(It->GetOwner()!=Controller||It->GetIsReplicated()||It->IsHidden()==bVisible||
            Segments->GetCollisionEnabled()!=ECollisionEnabled::NoCollision)return false;
    }
    return Count==1;
}
bool HasTripoChampionArt(const ACireHero* Hero, FString& Reason) {
    const auto Reject=[&](const TCHAR* Message){Reason=Message;return false;};
    if(!Hero||!Hero->bDrafted||!Hero->ChampionArt||!Hero->ChampionArt->IsApplied()||
        Hero->ChampionArt->GetAppliedArchetype()!=Hero->Archetype)
        return Reject(TEXT("drafted Tripo art was not applied"));
    const TCHAR* Names[]={TEXT("medieval_knight_armor_3d_model"),TEXT("armored_archer_3d_model"),TEXT("battlefield_healer_3d_model")};
    const float Heights[]={184.f,178.f,176.f};
    if(Hero->Archetype<0||Hero->Archetype>2)return Reject(TEXT("unexpected champion archetype"));
    const auto* Mesh=Hero->GetMesh();
    const auto* Asset=Mesh->GetSkeletalMeshAsset();
    const auto* SingleNode=Mesh->GetSingleNodeInstance();
    const auto* Blend=SingleNode?Cast<UBlendSpace>(SingleNode->GetAnimationAsset()):nullptr;
    const FString Expected=FString::Printf(TEXT("/Game/TripoModels/%s/%s.%s"),Names[Hero->Archetype],Names[Hero->Archetype],Names[Hero->Archetype]);
    if(!Asset||Asset->GetPathName()!=Expected||!Blend||!Asset->GetSkeleton()||Blend->GetSkeleton()!=Asset->GetSkeleton())
        return Reject(TEXT("Tripo mesh or locomotion skeleton does not match"));
    if(Blend->GetNumberOfBlendSamples()<3)return Reject(TEXT("locomotion sample set is incomplete"));
    for(const auto& Sample:Blend->GetBlendSamples())
        if(!Sample.Animation||Sample.Animation->GetSkeleton()!=Asset->GetSkeleton())
            return Reject(TEXT("locomotion contains an incompatible animation sample"));
    const auto Bounds=Asset->GetImportedBounds();
    const FVector Feet=Mesh->GetComponentTransform().TransformPosition(FVector(0,0,Bounds.Origin.Z-Bounds.BoxExtent.Z));
    const double CapsuleFeet=Hero->GetActorLocation().Z-Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const double Height=2.0*Bounds.BoxExtent.Z*Mesh->GetComponentScale().Z;
    if(Mesh->GetComponentTransform().ContainsNaN()||Feet.ContainsNaN()||!FMath::IsFinite(CapsuleFeet)||
        !FMath::IsNearlyEqual(Feet.Z,CapsuleFeet,3.0)||!FMath::IsNearlyEqual(Height,static_cast<double>(Heights[Hero->Archetype])*Hero->GetActorScale3D().Z,0.5)) // tank body scale (MovementTuning TankBodyScale) enlarges mesh and capsule together
        return Reject(TEXT("Tripo feet or height do not align with the unchanged capsule"));
    if(Mesh->GetNumMaterials()!=Asset->GetMaterials().Num())return Reject(TEXT("Tripo material slots changed"));
    for(int32 Index=0;Index<Asset->GetMaterials().Num();++Index)
        if(!Mesh->GetMaterial(Index)||Mesh->GetMaterial(Index)!=Asset->GetMaterials()[Index].MaterialInterface)
            return Reject(TEXT("selection or art swap replaced an imported PBR material"));
    return true;
}
} // namespace
#endif

void CireInterfaceProbe::ObserveChat(ACireController* Controller, const FString& Message) {
#if !UE_BUILD_SHIPPING
    if (!Controller || !Enabled(TEXT("CireInterfaceServer"))) return;
    const auto* Hero = Cast<ACireHero>(Controller->GetPawn());
    if (!Hero || Hero->TeamId < 0 || Hero->TeamId > 1) return;
    const TCHAR* Labels[] = {TEXT("PVE"),TEXT("PREP"),TEXT("ARENA"),TEXT("RECOVERY"),TEXT("DONE")};
    for (int32 Index=0;Index<5;++Index)
        if (Message == FString(TEXT("CIRE_PROBE_ACK_")) + Labels[Index]) Server.Acks[Index] |= 1 << Hero->TeamId;
#endif
}

bool CireInterfaceProbe::TickServer(ACireGameMode* Mode) {
#if !UE_BUILD_SHIPPING
    if (!Enabled(TEXT("CireInterfaceServer"))) return false;
    if (Server.bDone) return true;
    const double Now = FPlatformTime::Seconds();
    if (!Server.Started) {
        Server.Started = Server.StageStarted = Now;
        if (Mode->GetNetMode() != NM_DedicatedServer) { Fail(TEXT("SERVER"), TEXT("requires dedicated server")); Server.bDone=true; return true; }
        UE_LOG(LogCireInterface, Display, TEXT("CIRE_INTERFACE_SERVER_READY dedicated=1 clients=2"));
    }
    if (Now-Server.Started>75) { Fail(TEXT("SERVER"), TEXT("stage or client acknowledgement timeout")); Server.bDone=true; return true; }
    auto* State = Mode->GetGameState<ACireGameState>();
    ACireHero* Players[2] = {nullptr,nullptr};
    ACireController* Controllers[2] = {nullptr,nullptr};
    for (auto It=Mode->GetWorld()->GetPlayerControllerIterator();It;++It) {
        auto* Controller=Cast<ACireController>(It->Get());
        auto* Hero=Controller?Cast<ACireHero>(Controller->GetPawn()):nullptr;
        if(Hero&&Hero->bDrafted&&Hero->TeamId>=0&&Hero->TeamId<2) {Players[Hero->TeamId]=Hero;Controllers[Hero->TeamId]=Controller;}
    }
    if (Server.Stage==0) {
        if (!Players[0] || !Players[1]) return true;
        Mode->SpawnBots();
        for (auto* Hero:Mode->Heroes) if(IsValid(Hero)) {
            Hero->SetActorTickEnabled(false);Hero->bAutoAttack=false;Hero->Target=nullptr;
            Hero->GetCharacterMovement()->DisableMovement();
        }
        for (auto* Monster:Mode->Monsters) if(IsValid(Monster)) {
            Monster->SetActorTickEnabled(false);Monster->GetCharacterMovement()->DisableMovement();
        }
        Controllers[0]->ServerAction_Implementation(0,0,Players[1]);
        if(Players[0]->Target==Players[1] || CireCombat::ApplyDamage(Players[0],Players[1],30,TEXT("forbidden probe"))!=0) {
            Fail(TEXT("SERVER"),TEXT("opponent selected or damaged in PvE"));Server.bDone=true;return true;
        }
        // Spawn and destroy before the next replication frame: no target NetGUID
        // can have reached either client, but the owning team's lethal SCT must.
        FActorSpawnParameters LethalParams;
        LethalParams.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Lethal=Mode->GetWorld()->SpawnActor<ACireMonster>(ACireMonster::StaticClass(),FVector(1400,-2100,110),FRotator::ZeroRotator,LethalParams);
        if(!Lethal){Fail(TEXT("SERVER"),TEXT("lethal fixture spawn failed"));Server.bDone=true;return true;}
        Lethal->Lane=0;Lethal->Health=Lethal->MaxHealth=13;Lethal->MonsterName=TEXT("Unmapped lethal fixture");
        const FVector LethalHead=Lethal->GetActorLocation()+FVector(0,0,Lethal->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+20);
        // Character initialization can snap the requested spawn Z to the floor.
        // Replicate the actual pre-impact expectation independently of the event
        // through the source's own-team hero state, never the opposing PvE team.
        UE_LOG(LogCireInterface,Display,TEXT("CIRE_INTERFACE_SERVER_LETHAL_OBSERVED source_id=%s target_id=%s origin=%s head=%s half_height=%.3f"),
            *Players[0]->GetFName().ToString(),*Lethal->GetFName().ToString(),*Lethal->GetActorLocation().ToString(),*LethalHead.ToString(),Lethal->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        const float LethalAmount=CireCombat::ApplyDamage(Players[0],Lethal,100,TEXT("Interface lethal strike"));
        if(!FMath::IsNearlyEqual(LethalAmount,13.f)||!Lethal->IsActorBeingDestroyed()) {
            Fail(TEXT("SERVER"),TEXT("lethal fixture was not destroyed immediately"));Server.bDone=true;return true;
        }
        Players[0]->Notice=FString(TEXT("CIRE_LETHAL_HEAD "))+LethalHead.ToString();
        Players[0]->ForceNetUpdate();
        State->Announcement=TEXT("CIRE_INTERFACE_PVE");State->ForceNetUpdate();
        Server.Stage=1;Server.StageStarted=Now;
        UE_LOG(LogCireInterface,Display,TEXT("CIRE_INTERFACE_SERVER_PVE_PASS enemy_selection_rejected=1 enemy_damage_rejected=1"));
        UE_LOG(LogCireInterface,Display,TEXT("CIRE_INTERFACE_SERVER_LETHAL_PASS effective_damage=13 target_destroyed_before_replication=1"));
    } else if (Server.Stage==1 && Server.Acks[0]==3) {
        if(SelectionRingCount(Mode->GetWorld())!=0) {
            Fail(TEXT("SERVER"),TEXT("client selection ring replicated to server"));Server.bDone=true;return true;
        }
        UE_LOG(LogCireInterface,Display,TEXT("CIRE_INTERFACE_SERVER_SELECTION_PASS replicated_selection_rings=0"));
        Mode->Clock.BeginIntermission();Mode->ChangePhase(1);
        State->Announcement=TEXT("CIRE_INTERFACE_PREP");State->ForceNetUpdate();
        Server.Stage=2;Server.StageStarted=Now;
    } else if (Server.Stage==2 && Server.Acks[1]==3) {
        Mode->Clock.Advance(60);Mode->ChangePhase(2);
        State->Announcement=TEXT("CIRE_INTERFACE_ARENA");State->ForceNetUpdate();
        Server.Stage=3;Server.StageStarted=Now;
    } else if (Server.Stage==3) {
        if(!Players[0]||!Players[1]) {Fail(TEXT("SERVER"),TEXT("client left during arena"));Server.bDone=true;return true;}
        if(!Server.bCombatSent&&Players[0]->Target==Players[1]&&Players[1]->Target==Players[0]) {
            const float Damage=CireCombat::ApplyDamage(Players[0],Players[1],17,TEXT("Interface probe strike"));
            const float Heal=CireCombat::ApplyHealing(Players[1],Players[1],9,TEXT("Interface probe heal"));
            if(!FMath::IsNearlyEqual(Damage,17.f)||!FMath::IsNearlyEqual(Heal,9.f)) {
                Fail(TEXT("SERVER"),TEXT("arena combat fixture wrong effective amounts"));Server.bDone=true;return true;
            }
            Players[0]->ForceNetUpdate();Players[1]->ForceNetUpdate();Server.bCombatSent=true;
        }
        if(Server.Acks[2]==3) {
            Mode->Clock.ResolveArena();Mode->ChangePhase(4);
            State->Announcement=TEXT("CIRE_INTERFACE_RECOVERY");State->ForceNetUpdate();
            Server.Stage=4;Server.StageStarted=Now;
            UE_LOG(LogCireInterface,Display,TEXT("CIRE_INTERFACE_SERVER_ARENA_PASS targeting_rpc=1 damage=17 healing=9"));
        }
    } else if (Server.Stage==4 && Server.Acks[3]==3) {
        if(!Players[0]||!Players[1]||Players[0]->Target==Players[1]||Players[1]->Target==Players[0]||
            CireCombat::ApplyDamage(Players[0],Players[1],30,TEXT("forbidden recovery probe"))!=0) {
            Fail(TEXT("SERVER"),TEXT("opponent selected or damaged in recovery"));Server.bDone=true;return true;
        }
        Mode->Clock.Advance(15);Mode->ChangePhase(0);
        State->Announcement=TEXT("CIRE_INTERFACE_DONE");State->ForceNetUpdate();
        Server.Stage=5;Server.StageStarted=Now;
    } else if (Server.Stage==5 && Server.Acks[4]==3 && Now-Server.StageStarted>2.5) {
        UE_LOG(LogCireInterface,Display,TEXT("CIRE_INTERFACE_SERVER_PASS clients=2 teams=5/5 chat_routing=1 realm_privacy=1 arena_telemetry=1 recovery=1"));
        Server.bDone=true;FPlatformMisc::RequestExitWithStatus(false,0);
    }
    return true;
#else
    return false;
#endif
}

bool CireInterfaceProbe::TickClient(ACireController* Controller) {
#if !UE_BUILD_SHIPPING
    if(!Enabled(TEXT("CireInterfaceClient"))) return false;
    if(Client.bDone) return true;
    const double Now=FPlatformTime::Seconds();
    if(!Client.Started)Client.Started=Client.StepStarted=Now;
    const auto Abort=[&](const TCHAR* Reason){Fail(TEXT("CLIENT"),Reason);Client.bDone=true;};
    if(Now-Client.Started>65){Abort(TEXT("client stage timeout"));return true;}
    auto* Hero=Cast<ACireHero>(Controller->GetPawn());
    const auto* State=Controller->GetWorld()->GetGameState<ACireGameState>();
    if(!Hero||!State)return true;
    if(Controller->GetNetMode()!=NM_Client||Hero->HasAuthority()){Abort(TEXT("requires actual remote client"));return true;}
    if(!Hero->bDrafted){if(!Client.bDraftSent){Controller->ServerAction(5,Hero->TeamId==0?0:2,nullptr);Client.bDraftSent=true;}return true;}
    CireSelection::Update(Controller);
    int32 Own=0,Opposing=0,VisibleOpposing=0;
    CountHeroes(Controller->GetWorld(),Hero->TeamId,Own,Opposing,VisibleOpposing);
    const FString OwnParty=FString::Printf(TEXT("CIRE_PROBE_PARTY_%d"),Hero->TeamId);
    const FString OtherParty=FString::Printf(TEXT("CIRE_PROBE_PARTY_%d"),1-Hero->TeamId);
    const auto* LethalEvent=Controller->CombatEvents.FindByPredicate([](const FCireCombatEvent& Event){return Event.AbilityName==TEXT("Interface lethal strike");});
    if(Hero->TeamId==1&&LethalEvent){Abort(TEXT("other team's lethal PvE event leaked"));return true;}
    if(Client.Step==0&&State->Announcement==TEXT("CIRE_INTERFACE_PVE")&&Own==5&&Now-Client.StepStarted>1) {
        if(Opposing!=0){Abort(TEXT("opposing PvE hero replicated"));return true;}
        if(Enabled(TEXT("CireTripoChampions"))) {
            // Replication creates the new bot actors before their first Hero::Tick;
            // the controller probe can run earlier in that same client frame.
            if(!Client.ArtWaitStarted)Client.ArtWaitStarted=Now;
            int32 Ready=0;
            for(TActorIterator<ACireHero> It(Controller->GetWorld());It;++It)
                if(It->TeamId==Hero->TeamId&&It->bDrafted&&It->ChampionArt&&It->ChampionArt->IsApplied()&&
                    It->ChampionArt->GetAppliedArchetype()==It->Archetype)++Ready;
            if(Ready!=5) {
                if(!Client.bArtWaitLogged) {
                    UE_LOG(LogCireInterface,Display,TEXT("CIRE_INTERFACE_CLIENT_TRIPO_WAIT team=%d ready=%d/5 timeout_seconds=8"),Hero->TeamId,Ready);
                    Client.bArtWaitLogged=true;
                }
                if(Now-Client.ArtWaitStarted>=8.0) {
                    for(TActorIterator<ACireHero> It(Controller->GetWorld());It;++It)if(It->TeamId==Hero->TeamId)
                        UE_LOG(LogCireInterface,Error,TEXT("CIRE_INTERFACE_CLIENT_TRIPO_NOT_READY actor=%s drafted=%d archetype=%d actor_tick=%d component=%d applied=%d applied_archetype=%d mesh=%s"),
                            *It->GetName(),It->bDrafted,It->Archetype,It->IsActorTickEnabled(),It->ChampionArt!=nullptr,
                            It->ChampionArt&&It->ChampionArt->IsApplied(),It->ChampionArt?It->ChampionArt->GetAppliedArchetype():INDEX_NONE,
                            *GetNameSafe(It->GetMesh()->GetSkeletalMeshAsset()));
                    Abort(TEXT("Tripo art did not become ready for all five drafted allies within eight seconds"));
                }
                return true;
            }
            int32 Validated=0,ArchetypeMask=0;
            for(TActorIterator<ACireHero> It(Controller->GetWorld());It;++It)if(It->TeamId==Hero->TeamId) {
                FString Reason;
                if(!HasTripoChampionArt(*It,Reason)){Abort(*Reason);return true;}
                ++Validated;ArchetypeMask|=1<<It->Archetype;
            }
            if(Validated!=5||ArchetypeMask!=7){Abort(TEXT("Tripo fixture did not cover all three champion models"));return true;}
            UE_LOG(LogCireInterface,Display,TEXT("CIRE_INTERFACE_CLIENT_TRIPO_PASS team=%d heroes=5 archetypes=3 skeletons_match=1 feet_aligned=1 heights_valid=1 pbr_materials=1"),Hero->TeamId);
        }
        for(TActorIterator<ACireMonster> It(Controller->GetWorld());It;++It)
            if(It->Lane!=Hero->TeamId){Abort(TEXT("opposing PvE monster replicated"));return true;}
        if(Hero->TeamId==0) {
            if(!LethalEvent)return true;
            const FString HeadPrefix(TEXT("CIRE_LETHAL_HEAD "));
            if(!Hero->Notice.StartsWith(HeadPrefix))return true;
            FVector ExpectedHead;
            if(!ExpectedHead.InitFromString(Hero->Notice.RightChop(HeadPrefix.Len()))) {
                Abort(TEXT("authoritative lethal fixture head snapshot was malformed"));return true;
            }
            if(!FMath::IsNearlyEqual(LethalEvent->Amount,13.f)||!LethalEvent->bLocalSource||LethalEvent->bLocalTarget||
                LethalEvent->Source||LethalEvent->Target||LethalEvent->SourceId.IsNone()||LethalEvent->TargetId.IsNone()||
                LethalEvent->Sequence==0||LethalEvent->TargetName!=TEXT("Unmapped lethal fixture")||
                !LethalEvent->Location.Equals(ExpectedHead,0.1)) {
                UE_LOG(LogCireInterface,Error,TEXT("CIRE_INTERFACE_CLIENT_LETHAL_OBSERVED team=%d amount=%.3f source_local=%d target_local=%d source_ref=%s target_ref=%s source_id=%s target_id=%s seq=%u name=%s location=%s"),
                    Hero->TeamId,LethalEvent->Amount,LethalEvent->bLocalSource,LethalEvent->bLocalTarget,*GetNameSafe(LethalEvent->Source.Get()),*GetNameSafe(LethalEvent->Target.Get()),
                    *LethalEvent->SourceId.ToString(),*LethalEvent->TargetId.ToString(),LethalEvent->Sequence,*LethalEvent->TargetName,*LethalEvent->Location.ToString());
                Abort(TEXT("unmapped killing blow lost identity, ownership, or head position"));return true;
            }
            UE_LOG(LogCireInterface,Display,TEXT("CIRE_INTERFACE_CLIENT_LETHAL_PASS team=0 amount=13 no_actor_refs=1 local_source=1 stable_identity=1 head_position=1"));
        }
        Client.PreviousSelfOverlay=Hero->GetMesh()->GetOverlayMaterial();
        Controller->ServerAction(0,0,Hero);Client.Step=10;Client.StepStarted=Now;
        UE_LOG(LogCireInterface,Display,TEXT("CIRE_INTERFACE_CLIENT_PVE_PASS team=%d own_heroes=%d enemy_heroes=%d"),Hero->TeamId,Own,Opposing);
    } else if(Client.Step==10&&Hero->Target==Hero&&Now-Client.StepStarted>0.25) {
        CireSelection::Update(Controller);
        if(!HasPrivateRing(Controller,true)||!Hero->GetMesh()->GetOverlayMaterial()||
            Hero->GetMesh()->GetOverlayMaterial()==Client.PreviousSelfOverlay.Get()) {
            Abort(TEXT("self selection visual missing or not private"));return true;
        }
        FHitResult SelectionHit,VisibilityHit;
        const FVector End=Hero->GetActorLocation(),Start=End+FVector(0,0,110);
        const bool bSelectionHit=Controller->GetWorld()->LineTraceSingleByChannel(SelectionHit,Start,End,ECC_GameTraceChannel1);
        const bool bVisibilityHit=Controller->GetWorld()->LineTraceSingleByChannel(VisibilityHit,Start,End,ECC_Visibility);
        if(!bSelectionHit||SelectionHit.GetActor()!=Hero||(bVisibilityHit&&VisibilityHit.GetActor()==Hero)) {
            Abort(TEXT("selection trace channel failed or altered combat visibility"));return true;
        }
        Controller->ServerAction(0,0,nullptr);Client.Step=11;Client.StepStarted=Now;
    } else if(Client.Step==11&&Now-Client.StepStarted>0.6) {
        if(Hero->Target!=Hero){Abort(TEXT("invalid null selection cleared existing target"));return true;}
        for(TActorIterator<ACireHero> It(Controller->GetWorld());It;++It)if(*It!=Hero&&It->TeamId==Hero->TeamId) {
            Client.Ally=*It;Client.PreviousAllyOverlay=It->GetMesh()->GetOverlayMaterial();
            Controller->ServerAction(0,0,*It);Client.Step=12;Client.StepStarted=Now;break;
        }
    } else if(Client.Step==12&&Client.Ally.IsValid()&&Hero->Target==Client.Ally.Get()&&Now-Client.StepStarted>0.25) {
        CireSelection::Update(Controller);
        if(!HasPrivateRing(Controller,true)||Hero->GetMesh()->GetOverlayMaterial()!=Client.PreviousSelfOverlay.Get()||
            !Client.Ally->GetMesh()->GetOverlayMaterial()||Client.Ally->GetMesh()->GetOverlayMaterial()==Client.PreviousAllyOverlay.Get()) {
            Abort(TEXT("ally selection did not restore self or highlight ally"));return true;
        }
        Controller->ServerAction(6,0,nullptr);Client.Step=13;Client.StepStarted=Now;
    } else if(Client.Step==13&&!Hero->Target&&Now-Client.StepStarted>0.25) {
        CireSelection::Update(Controller);
        if(!HasPrivateRing(Controller,false)||!Client.Ally.IsValid()||
            Client.Ally->GetMesh()->GetOverlayMaterial()!=Client.PreviousAllyOverlay.Get()) {
            Abort(TEXT("clear selection did not hide ring and restore overlay"));return true;
        }
        if(Enabled(TEXT("CireTripoChampions"))) {
            FString Reason;
            if(!HasTripoChampionArt(Hero,Reason)||!HasTripoChampionArt(Client.Ally.Get(),Reason)){Abort(*Reason);return true;}
            UE_LOG(LogCireInterface,Display,TEXT("CIRE_INTERFACE_CLIENT_TRIPO_SELECTION_PASS team=%d imported_materials_preserved=1 original_overlays_restored=1"),Hero->TeamId);
        }
        UE_LOG(LogCireInterface,Display,TEXT("CIRE_INTERFACE_CLIENT_SELECTION_PASS team=%d self=1 ally=1 null_preserves=1 ground_clear=1 private_ring=1 overlays_restored=1 unit_trace=1 combat_trace_unchanged=1"),Hero->TeamId);
        Controller->ServerSendChat(OwnParty,true);Client.Step=1;Client.StepStarted=Now;
    } else if(Client.Step==1&&Now-Client.StepStarted>1.1) {
        Controller->ServerSendChat(FString::Printf(TEXT("CIRE_PROBE_ALL_%d"),Hero->TeamId),false);Client.Step=2;Client.StepStarted=Now;
    } else if(Client.Step==2&&Now-Client.StepStarted>1.1) {
        if(HasChat(Controller,OtherParty,true,1-Hero->TeamId)){Abort(TEXT("enemy party chat leaked"));return true;}
        if(HasChat(Controller,OwnParty,true,Hero->TeamId)&&HasChat(Controller,TEXT("CIRE_PROBE_ALL_0"),false,0)&&HasChat(Controller,TEXT("CIRE_PROBE_ALL_1"),false,1)) {
            Controller->ServerSendChat(TEXT("CIRE_PROBE_ACK_PVE"),true);Client.Step=3;Client.StepStarted=Now;
            UE_LOG(LogCireInterface,Display,TEXT("CIRE_INTERFACE_CLIENT_CHAT_PASS team=%d own_party=1 other_party=0 all_teams=1"),Hero->TeamId);
        }
    } else if(Client.Step==3&&State->Announcement==TEXT("CIRE_INTERFACE_PREP")&&Now-Client.StepStarted>1.1) {
        if(State->Phase!=1||Opposing!=0){Abort(TEXT("preparation realm visibility wrong"));return true;}
        Controller->ServerSendChat(TEXT("CIRE_PROBE_ACK_PREP"),true);Client.Step=4;Client.StepStarted=Now;
    } else if(Client.Step==4&&State->Announcement==TEXT("CIRE_INTERFACE_ARENA")&&Own==5&&Opposing==5) {
        for(TActorIterator<ACireHero> It(Controller->GetWorld());It;++It)
            if(It->TeamId!=Hero->TeamId&&!It->bBot&&Hero->IsHostile(*It)) {
                Client.Enemy=*It;Controller->ServerAction(0,0,*It);Client.Step=5;Client.StepStarted=Now;break;
            }
    } else if(Client.Step==5&&Client.Enemy.IsValid()&&Hero->Target==Client.Enemy.Get()&&Now-Client.StepStarted>1.1) {
        if(!HasPrivateRing(Controller,true)){Abort(TEXT("enemy selection ring missing or not private"));return true;}
        const bool DamageEvent=Controller->CombatEvents.ContainsByPredicate([Hero](const FCireCombatEvent& Event){return Event.AbilityName==TEXT("Interface probe strike")&&!Event.bHealing&&FMath::IsNearlyEqual(Event.Amount,17.f)&&
            !Event.Source&&!Event.Target&&Event.bLocalSource==(Hero->TeamId==0)&&Event.bLocalTarget==(Hero->TeamId==1)&&Event.Sequence>0;});
        const bool HealEvent=Controller->CombatEvents.ContainsByPredicate([Hero](const FCireCombatEvent& Event){return Event.AbilityName==TEXT("Interface probe heal")&&Event.bHealing&&FMath::IsNearlyEqual(Event.Amount,9.f)&&
            !Event.Source&&!Event.Target&&Event.bLocalSource==(Hero->TeamId==1)&&Event.bLocalTarget==(Hero->TeamId==1)&&Event.Sequence>0;});
        const auto* Attacker=Hero->TeamId==0?Hero:Client.Enemy.Get();
        const auto* Healer=Hero->TeamId==1?Hero:Client.Enemy.Get();
        if(DamageEvent&&HealEvent&&FMath::IsNearlyEqual(Attacker->DamageDone,30.f)&&FMath::IsNearlyEqual(Healer->HealingDone,9.f)) {
            Controller->ServerSendChat(TEXT("CIRE_PROBE_ACK_ARENA"),true);Client.Step=6;Client.StepStarted=Now;
            UE_LOG(LogCireInterface,Display,TEXT("CIRE_INTERFACE_CLIENT_ARENA_PASS team=%d opponents=5 target_rpc=1 damage_event=17 heal_event=9 meter_replication=1 recipient_flags=1 no_actor_refs=1"),Hero->TeamId);
        }
    } else if(Client.Step==6&&State->Announcement==TEXT("CIRE_INTERFACE_RECOVERY")) {
        if(Client.Enemy.IsValid())Controller->ServerAction(0,0,Client.Enemy.Get());
        Client.Step=7;Client.StepStarted=Now;
    } else if(Client.Step==7&&Now-Client.StepStarted>2&&Opposing==0) {
        if(State->Phase!=4||VisibleOpposing!=0||Hero->Target){Abort(TEXT("recovery failed to hide or clear enemy target"));return true;}
        Controller->ServerSendChat(TEXT("CIRE_PROBE_ACK_RECOVERY"),true);Client.Step=8;Client.StepStarted=Now;
        UE_LOG(LogCireInterface,Display,TEXT("CIRE_INTERFACE_CLIENT_RECOVERY_PASS team=%d opposing_actors=0 enemy_target_rejected=1"),Hero->TeamId);
    } else if(Client.Step==8&&State->Announcement==TEXT("CIRE_INTERFACE_DONE")&&Now-Client.StepStarted>1.1) {
        if(State->Phase!=0||Own!=5||Opposing!=0){Abort(TEXT("next cycle realm state incorrect"));return true;}
        Controller->ServerSendChat(TEXT("CIRE_PROBE_ACK_DONE"),true);Client.Step=9;Client.StepStarted=Now;
    } else if(Client.Step==9&&Now-Client.StepStarted>1) {
        UE_LOG(LogCireInterface,Display,TEXT("CIRE_INTERFACE_CLIENT_PASS team=%d phases=survival/prep/arena/recovery/survival"),Hero->TeamId);
        Client.bDone=true;FPlatformMisc::RequestExitWithStatus(false,0);
    }
    return true;
#else
    return false;
#endif
}
