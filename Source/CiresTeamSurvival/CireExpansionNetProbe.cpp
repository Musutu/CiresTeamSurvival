#include "CireExpansionNetProbe.h"
#include "CireGame.h"
#include "CireClassTraits.h" // champion-draft: class-trait-aware expectations
#include "CireCombatEvents.h"
#include "CireSkillshot.h"
#include "CireConstruct.h"
#include "CireTechConstructs.h" // new-champions: replicated Aetheri constructs
#include "CireSummon.h"
#include "CirePets.h" // pets: replicated companions
#include "CireLanePath.h"
#include "CireMobility.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "CireBuffs.h" // aura-vfx
#include "CireAuraVisuals.h" // aura-vfx

DEFINE_LOG_CATEGORY_STATIC(LogCireExpansionNet, Log, All);

#if !UE_BUILD_SHIPPING
namespace
{
bool Enabled(const TCHAR* Flag) { return FParse::Param(FCommandLine::Get(), Flag); }
struct FServer
{
    int32 Stage = 0, Acks = 0;
    double Started = 0, ChangedAt = 0;
    bool bDone = false;
    TWeakObjectPtr<ACireExpansionProbeChannel> Channels[2];
    TWeakObjectPtr<ACireConstruct> Walls[2];
    TWeakObjectPtr<ACireSummon> Summons[2];
    TWeakObjectPtr<ACireSkillshot> CombatShot;
    FVector RollOrigin = FVector::ZeroVector;
} Server;
struct FClient
{
    double Started = 0, ChangedAt = 0, FirstShotAt = 0;
    int32 Stage = 0, LastAck = 0;
    FVector FirstShotPosition = FVector::ZeroVector;
    bool bDraftSent = false, bDone = false;
    bool bRollSent = false, bSawRoll = false;
    bool bPetCommandSent = false; // pets
    FVector RollOrigin = FVector::ZeroVector;
} Client;
void Fail(const TCHAR* Side, const TCHAR* Message)
{
    UE_LOG(LogCireExpansionNet, Error, TEXT("CIRE_EXPANSION_NET_%s_FAIL stage=%d reason=%s"), Side, Server.Started ? Server.Stage : Client.Stage, Message);
    FPlatformMisc::RequestExitWithStatus(false, 1);
}
void Stage(int32 NewStage)
{
    Server.Stage = NewStage; Server.Acks = 0; Server.ChangedAt = FPlatformTime::Seconds();
    for (auto Channel : Server.Channels) if (Channel.IsValid()) { Channel->Stage = NewStage; Channel->ForceNetUpdate(); }
    UE_LOG(LogCireExpansionNet, Display, TEXT("CIRE_EXPANSION_NET_SERVER_STAGE stage=%d"), NewStage);
}
void Platform(UWorld* World, FVector Ground)
{
    auto* Actor = World->SpawnActor<AActor>();
    auto* Box = NewObject<UBoxComponent>(Actor); Actor->SetRootComponent(Box); Actor->AddInstanceComponent(Box);
    Box->SetBoxExtent(FVector(1900, 1000, 50)); Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent(); Actor->SetActorLocation(Ground - FVector(0, 0, 50));
}
void FreezeProbeHero(ACireHero* Hero, FVector Position)
{
    Hero->bDead = false; Hero->Health = Hero->MaxHealth = 1000; Hero->ShieldUntil = 0; Hero->Target = nullptr;
    Hero->bAutoAttack = false; Hero->PendingAttackTarget.Reset(); Hero->CriticalChance = 0;
    Hero->SetActorTickEnabled(false); Hero->GetCharacterMovement()->StopMovementImmediately(); Hero->GetCharacterMovement()->DisableMovement();
    Hero->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Hero->SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics); Hero->ForceNetUpdate();
}
bool SpawnObjects(ACireHero* Hero, FVector Ground, bool bArena)
{
    const int32 Team = Hero->TeamId;
    FCireConstructSpec Wall; Wall.Width = 220; Wall.MaxHealth = 200; Wall.LifetimeSeconds = 120; Wall.CastRange = 1600;
    const FVector WallOffset = bArena ? FVector(0, Team == 0 ? -400 : 400, 0) : FVector(500, 0, 0);
    Server.Walls[Team] = ACireConstruct::Spawn(Hero, Wall, Ground + WallOffset, FRotator::ZeroRotator, FString::Printf(TEXT("CIRE_EXP_NET_WALL_%d"), Team));
    FCireSummonSpec Summon; Summon.Health = 100; Summon.Damage = 0; Summon.DurationSeconds = 120; Summon.CastRange = 1600;
    auto Group = ACireSummon::SpawnGroup(Hero, Summon, nullptr, Ground + (bArena ? FVector(Team == 0 ? -350 : 350, Team == 0 ? -550 : 550, 0) : FVector(200, 300, 0)));
    if (Group.Num() != 1 || !Server.Walls[Team].IsValid()) return false;
    // new-champions: an Aetheri turret per team in the survival stage (out of reach of the stage-2 targets).
    if (!bArena && CireTechConstructs::Deploy(Hero, TEXT("photon_turret"), Ground + FVector(-450, 320, 0)).Num() != 1) return false;
    Server.Summons[Team] = Group[0]; Group[0]->HeroName = FString::Printf(TEXT("CIRE_EXP_NET_SUMMON_%d"), Team);
    Group[0]->Command(ECireSummonCommand::Hold, Group[0]->GetActorLocation()); Group[0]->ForceNetUpdate();
    // pets: every probe champion gets the sabercat through the pet-talent grant, Passive so it never touches the stage targets.
    Hero->PetGrant = TEXT("sabercat"); Hero->PetStance = static_cast<uint8>(ECirePetStance::Passive); Hero->ForceNetUpdate();
    if (!CirePets::Summon(Hero)) return false;
    FCireSkillshotSpec Shot; Shot.Speed = 100; Shot.Radius = 12; Shot.MaxRange = 5000; Shot.LifetimeSeconds = 30;
    Shot.WarningSeconds = 0; Shot.Damage = 0; Shot.PlayerCollision = Shot.MonsterCollision = ECireProjectileCollision::Ignore;
    auto* Projectile = ACireSkillshot::Spawn(Hero, Shot, Hero->GetActorLocation() + (bArena ? FVector(0, 900, 0) : FVector(1000, 0, 0)), FString::Printf(TEXT("CIRE_EXP_NET_SHOT_%d"), Team));
    return Projectile != nullptr;
}
struct FCounts { int32 Wall[2] = {0, 0}, Shot[2] = {0, 0}, Summon[2] = {0, 0}, Turret[2] = {0, 0}, Pet[2] = {0, 0}; ACireSkillshot* OwnShot = nullptr; ACireSummon* FirstSummon = nullptr; ACireConstruct* OwnTurret = nullptr; ACirePet* OwnPet = nullptr; ACirePet* FirstPet = nullptr; };
FCounts Count(UWorld* World, int32 Self)
{
    FCounts R;
    for (TActorIterator<ACireConstruct> It(World); It; ++It)
    {
        if (!It->IsActorBeingDestroyed() && It->AbilityName.StartsWith(TEXT("CIRE_EXP_NET_WALL_")) && It->OriginTeam >= 0 && It->OriginTeam < 2) ++R.Wall[It->OriginTeam];
        if (!It->IsActorBeingDestroyed() && It->ConstructSpec.Recipe == TEXT("photon_turret") && It->OriginTeam >= 0 && It->OriginTeam < 2)
        { ++R.Turret[It->OriginTeam]; if (It->OriginTeam == Self) R.OwnTurret = *It; }
    }
    for (TActorIterator<ACireSkillshot> It(World); It; ++It)
        if (!It->IsActorBeingDestroyed() && It->AbilityName.StartsWith(TEXT("CIRE_EXP_NET_SHOT_")) && It->OriginTeam >= 0 && It->OriginTeam < 2)
        { ++R.Shot[It->OriginTeam]; if (It->OriginTeam == Self) R.OwnShot = *It; }
    for (TActorIterator<ACireSummon> It(World); It; ++It)
        if (!It->IsActorBeingDestroyed() && It->HeroName.StartsWith(TEXT("CIRE_EXP_NET_SUMMON_")) && It->TeamId >= 0 && It->TeamId < 2)
        { ++R.Summon[It->TeamId]; if (It->TeamId == 0) R.FirstSummon = *It; }
    for (TActorIterator<ACirePet> It(World); It; ++It) // pets
        if (!It->IsActorBeingDestroyed() && It->TeamId >= 0 && It->TeamId < 2)
        { ++R.Pet[It->TeamId]; if (It->TeamId == Self) R.OwnPet = *It; if (It->TeamId == 0) R.FirstPet = *It; }
    return R;
}
}
#endif

ACireExpansionProbeChannel::ACireExpansionProbeChannel() { bReplicates = true; bOnlyRelevantToOwner = true; SetNetUpdateFrequency(20); }
void ACireExpansionProbeChannel::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(ACireExpansionProbeChannel, Team); DOREPLIFETIME(ACireExpansionProbeChannel, Stage);
}
void ACireExpansionProbeChannel::ServerAcknowledge_Implementation(int32 ObservedStage)
{
#if !UE_BUILD_SHIPPING
    const auto* Controller = Cast<ACireController>(GetOwner());
    const auto* Hero = Controller ? Cast<ACireHero>(Controller->GetPawn()) : nullptr;
    if (Enabled(TEXT("CireExpansionNetServer")) && HasAuthority() && Hero && Hero->TeamId == Team && Team >= 0 && Team < 2 && ObservedStage == Server.Stage)
        Server.Acks |= 1 << Team;
#endif
}
bool CireExpansionNetProbe::TickServer(ACireGameMode* Mode)
{
#if !UE_BUILD_SHIPPING
    if (!Enabled(TEXT("CireExpansionNetServer"))) return false;
    if (Server.bDone) return true;
    const double Now = FPlatformTime::Seconds();
    auto Abort = [&](const TCHAR* Message) { Server.bDone = true; Fail(TEXT("SERVER"), Message); };
    if (!Server.Started)
    {
        Server.Started = Now;
        if (Mode->GetNetMode() != NM_DedicatedServer) { Abort(TEXT("requires dedicated server")); return true; }
        Mode->bBotsFilled = true;
        for (auto* Monster : Mode->Monsters) if (IsValid(Monster)) Monster->Destroy(); Mode->Monsters.Reset();
        UE_LOG(LogCireExpansionNet, Display, TEXT("CIRE_EXPANSION_NET_SERVER_READY clients=2 timeout=240"));
    }
    // Generous: clients can take >60s to boot while other editors compile on the same machine.
    if (Now - Server.Started > 240) { Abort(TEXT("stage acknowledgement timeout")); return true; }
    ACireHero* Players[2] = {nullptr, nullptr}; ACireController* Controllers[2] = {nullptr, nullptr};
    for (auto It = Mode->GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        auto* Controller = Cast<ACireController>(It->Get()); auto* Hero = Controller ? Cast<ACireHero>(Controller->GetPawn()) : nullptr;
        if (Hero && Hero->bDrafted && Hero->TeamId >= 0 && Hero->TeamId < 2) { Players[Hero->TeamId] = Hero; Controllers[Hero->TeamId] = Controller; }
    }
    if (Server.Stage == 0)
    {
        if (!Players[0] || !Players[1]) return true;
        for (int32 Team = 0; Team < 2; ++Team)
        {
            const FVector Ground(0, Team == 0 ? -2100 : 2100, 3000); Platform(Mode->GetWorld(), Ground); FreezeProbeHero(Players[Team], Ground + FVector(0, 0, 92));
            CireBuffs::Apply(Players[Team], TEXT("blood_rage"), 600, Players[Team]); // aura-vfx: replicated record + client aura
            FActorSpawnParameters P; P.Owner = Controllers[Team];
            auto* Channel = Mode->GetWorld()->SpawnActor<ACireExpansionProbeChannel>(P);
            if (!Channel) { Abort(TEXT("owner-only probe channel did not spawn")); return true; }
            Channel->Team = Team; Server.Channels[Team] = Channel;
            if (!SpawnObjects(Players[Team], Ground, false)) { Abort(TEXT("survival objects failed to spawn")); return true; }
        }
        Stage(1);
    }
    else if (Server.Stage != 7 && (!Players[0] || !Players[1])) { Abort(TEXT("client disconnected early")); return true; }
    else if (Server.Stage == 1 && Server.Acks == 3)
    {
        for (int32 Team = 0; Team < 2; ++Team)
        {
            FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Monster = Mode->GetWorld()->SpawnActor<ACireMonster>(FVector(900, Team == 0 ? -2100 : 2100, 3092), FRotator::ZeroRotator, P);
            if (!Monster) { Abort(TEXT("critical event target did not spawn")); return true; }
            Monster->Lane = Team; Monster->Health = Monster->MaxHealth = 200; Monster->MonsterName = FString::Printf(TEXT("CIRE_EXP_CRIT_TARGET_%d"), Team);
            Monster->SetActorTickEnabled(false); Monster->GetCharacterMovement()->DisableMovement(); Mode->Monsters.Add(Monster);
            Players[Team]->CriticalChance = 1; Players[Team]->CriticalMultiplier = 2;
            const float Damage = CireCombat::ApplyStrike(Players[Team], Monster, 20, FString::Printf(TEXT("CIRE_EXP_PVE_CRIT_%d"), Team));
            Players[Team]->CriticalChance = 0; Monster->ForceNetUpdate();
            if (!FMath::IsNearlyEqual(Damage, 40.f)) { Abort(TEXT("forced PvE critical did not apply forty damage")); return true; }
        }
        Stage(2);
    }
    else if (Server.Stage == 2 && Server.Acks == 3)
    { Players[0]->bDead = true; Players[0]->Health = 0; Players[0]->ForceNetUpdate(); Stage(3); }
    else if (Server.Stage == 3 && Server.Acks == 3)
    {
        ACireSkillshot::ClearAll(Mode->GetWorld()); ACireConstruct::ClearAll(Mode->GetWorld()); ACireSummon::ClearAll(Mode->GetWorld());
        for (auto* Monster : Mode->Monsters) if (IsValid(Monster)) Monster->Destroy(); Mode->Monsters.Reset();
        Mode->Clock.BeginIntermission(); Mode->ChangePhase(1); Mode->Clock.Advance(61); Mode->ChangePhase(2);
        const FVector Ground(0, Mode->ArenaPosition(0, 2).Y, 3000); Platform(Mode->GetWorld(), Ground);
        for (int32 Team = 0; Team < 2; ++Team)
        { FreezeProbeHero(Players[Team], Ground + FVector(Team == 0 ? -650 : 650, 0, 92)); if (!SpawnObjects(Players[Team], Ground, true)) { Abort(TEXT("arena objects failed to spawn")); return true; } }
        Mode->GetGameState<ACireGameState>()->ForceNetUpdate(); Stage(4);
    }
    else if (Server.Stage == 4 && Server.Acks == 3)
    {
        Players[0]->CriticalChance = 1; Players[0]->CriticalMultiplier = 2;
        const float Critical = CireCombat::ApplyStrike(Players[0], Players[1], 10, TEXT("CIRE_EXP_PVP_CRIT")); Players[0]->CriticalChance = 0;
        if (Critical != CireClassTraits::ModifyIncomingDamage(Players[1], 20) || !Server.Walls[0].IsValid() || !Server.Summons[0].IsValid() ||
            CireCombat::ApplyDamage(Players[0], Server.Walls[0].Get(), 10, TEXT("forbidden friendly wall")) != 0 ||
            CireCombat::ApplyDamage(Players[1], Server.Walls[0].Get(), 1000, TEXT("CIRE_EXP_WALL_BREAK")) != 200 ||
            CireCombat::ApplyDamage(Players[1], Server.Summons[0].Get(), 10, TEXT("CIRE_EXP_SUMMON_HIT")) != 10 ||
            !CirePets::PetOf(Players[0]) || !FMath::IsNearlyEqual(CireCombat::ApplyDamage(Players[1], CirePets::PetOf(Players[0]), 20, TEXT("CIRE_EXP_PET_HIT")), 17.f, .01f)) // pets: 85% damage taken
        { Abort(TEXT("arena crit/wall/summon authorization mismatch")); return true; }
        FCireSkillshotSpec Shot; Shot.Speed = 1800; Shot.Radius = 20; Shot.MaxRange = 1800; Shot.WarningSeconds = 0; Shot.Damage = 30; Shot.bCanCrit = false;
        Server.CombatShot = ACireSkillshot::Spawn(Players[0], Shot, Players[1]->GetActorLocation(), TEXT("CIRE_EXP_PVP_SKILLSHOT"));
        if (!Server.CombatShot.IsValid()) { Abort(TEXT("arena combat projectile did not launch")); return true; }
        CireBuffs::Apply(Players[1], TEXT("bastion_of_dawn"), 600, Players[1]); // aura-vfx
        Players[0]->ForceNetUpdate(); Players[1]->ForceNetUpdate(); Stage(5);
    }
    else if (Server.Stage == 5 && Server.Acks == 3)
    {
        if (!FMath::IsNearlyEqual(Players[1]->Health, 1000.f - CireClassTraits::ModifyIncomingDamage(Players[1], 20) - CireClassTraits::ModifyIncomingDamage(Players[1], 30))) { Abort(TEXT("server final PvP health does not match clients")); return true; }
        // Both clients have the real arena floor. Exercise owner input and
        // simulated proxy movement without the elevated server-only platform.
        const float Y=Mode->ArenaPosition(0,2).Y;
        for(int32 Team=0;Team<2;++Team)
        {
            auto* H=Players[Team];FreezeProbeHero(H,FVector(Team==0?-650:650,Y,110));
            H->Energy=100;H->Mobility->CancelRoll();H->Mobility->ReadyAt=0;
            H->Mobility->bWalking=false;H->Mobility->bStrafing=false;
            H->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            H->GetCharacterMovement()->bForceNextFloorCheck=true;H->ForceNetUpdate();
        }
        Server.RollOrigin=Players[0]->GetActorLocation();Stage(8);
    }
    else if(Server.Stage==8&&Server.Acks==3)Stage(9);
    else if(Server.Stage==9&&Server.Acks==3)
    {
        auto* H=Players[0];auto* M=H->Mobility.Get();
        if(!M||M->IsRolling()||!M->bWalking||!M->bStrafing||M->CooldownRemaining()<=0||
            !FMath::IsNearlyEqual(H->Energy,100-CireMovement::Tuning().RollEnergy)||
            FVector::Dist2D(H->GetActorLocation(),Server.RollOrigin)<100)
        {Abort(TEXT("server authoritative roll motion/resource/cooldown did not match clients"));return true;}
        UE_LOG(LogCireExpansionNet,Display,TEXT("CIRE_MOVEMENT_NET_SERVER_PASS owner_rpc=1 repeat_rejected=1 energy=%.1f travel=%.1f"),
            H->Energy,FVector::Dist2D(H->GetActorLocation(),Server.RollOrigin));
        Mode->Clock.ResolveArena(); Mode->ChangePhase(4); Mode->GetGameState<ACireGameState>()->ForceNetUpdate(); Stage(6);
    }
    else if (Server.Stage == 6 && Server.Acks == 3) Stage(7);
    else if (Server.Stage == 7 && Now - Server.ChangedAt > 3)
    {
        UE_LOG(LogCireExpansionNet, Display, TEXT("CIRE_EXPANSION_NET_SERVER_PASS clients=2 pve_privacy=1 replicated_motion=1 critical_events=1 owner_cleanup=1 pvp_projectile=1 wall_damage=1 summon_damage=1 phase_cleanup=1 movement_rpc=1 pets=1"));
        Server.bDone = true; FPlatformMisc::RequestExitWithStatus(false, 0);
    }
    return true;
#else
    return false;
#endif
}
bool CireExpansionNetProbe::TickClient(ACireController* Controller)
{
#if !UE_BUILD_SHIPPING
    if (!Enabled(TEXT("CireExpansionNetClient"))) return false;
    if (Client.bDone) return true;
    const double Now = FPlatformTime::Seconds(); if (!Client.Started) Client.Started = Now;
    auto Abort = [&](const TCHAR* Message) { Client.bDone = true; Fail(TEXT("CLIENT"), Message); };
    if (Now - Client.Started > 75) { Abort(TEXT("client stage timeout")); return true; }
    auto* Hero = Cast<ACireHero>(Controller->GetPawn()); const auto* State = Controller->GetWorld()->GetGameState<ACireGameState>();
    if (!Hero || !State || Hero->TeamId < 0 || Hero->TeamId > 1) return true;
    if (Controller->GetNetMode() != NM_Client || Hero->HasAuthority()) { Abort(TEXT("requires separate remote client")); return true; }
    if (!Hero->bDrafted) { if (!Client.bDraftSent) { Controller->ServerAction(5, 0, nullptr); Client.bDraftSent = true; } return true; }
    ACireExpansionProbeChannel* Channel = nullptr;
    for (TActorIterator<ACireExpansionProbeChannel> It(Controller->GetWorld()); It; ++It) if (It->Team == Hero->TeamId) Channel = *It;
    if (!Channel || Channel->Stage == 0) return true;
    if (Client.Stage != Channel->Stage) { Client.Stage = Channel->Stage; Client.ChangedAt = Now; Client.FirstShotAt = 0; }
    const int32 Team = Hero->TeamId, Enemy = 1 - Team;
    const auto Counts = Count(Controller->GetWorld(), Team);
    auto Event = [&](const FString& Name) { return Controller->CombatEvents.FindByPredicate([&](const FCireCombatEvent& E) { return E.AbilityName == Name; }); };
    auto Ack = [&]
    {
        if (Client.LastAck == Client.Stage) return;
        Channel->ServerAcknowledge(Client.Stage); Client.LastAck = Client.Stage;
        UE_LOG(LogCireExpansionNet, Display, TEXT("CIRE_EXPANSION_NET_CLIENT_STAGE_PASS team=%d stage=%d"), Team, Client.Stage);
    };
    if (Now - Client.ChangedAt < .3 && Client.Stage != 9) return true;
    if (Client.Stage <= 2 && State->Phase == 0 && (Counts.Wall[Enemy] || Counts.Shot[Enemy] || Counts.Summon[Enemy] || Counts.Turret[Enemy] || Counts.Pet[Enemy] /* pets */ || Event(FString::Printf(TEXT("CIRE_EXP_PVE_CRIT_%d"), Enemy))))
    { Abort(TEXT("opposing survival actors or critical event leaked")); return true; }
    if (Client.Stage == 1)
    {
        if(State->LaneRouteVersion==0 || State->LanePoints0.Num()<3 || State->LanePoints1.Num()<3)return true;
        const auto& Routes=CireLanePath::Get(Controller->GetWorld());
        if(Routes.LocalPoints[0]!=State->LanePoints0 || Routes.LocalPoints[1]!=State->LanePoints1 ||
            !State->LaneBounds.Equals(FVector(Routes.MinX,Routes.MaxX,Routes.HalfWidth)))
        { Abort(TEXT("replicated lane routes did not update the client world cache"));return true; }
        if (Counts.Wall[Team] != 1 || Counts.Shot[Team] != 1 || Counts.Summon[Team] != 1 || !Counts.OwnShot) return true;
        if (Counts.OwnShot->HasAuthority() || !FMath::IsNearlyEqual(Counts.OwnShot->ShotSpec.Speed, 100.f)) { Abort(TEXT("projectile authority/spec did not replicate")); return true; }
        bool bSummonOwnerReady = false;
        for (TActorIterator<ACireSummon> It(Controller->GetWorld()); It; ++It) if (It->TeamId == Team && It->GetOwnerHero() == Hero && It->bCommandable && It->Health == It->MaxHealth && It->MaxHealth >= 100) bSummonOwnerReady = true; // fix/summons: health scales off the owner's primary
        if (!bSummonOwnerReady) return true;
        bool bWallReady = false;
        for (TActorIterator<ACireConstruct> It(Controller->GetWorld()); It; ++It)
            if (It->OriginTeam == Team && It->AbilityName.StartsWith(TEXT("CIRE_EXP_NET_WALL_")) && It->Health == 200 && It->MaxHealth == 200 &&
                It->ConstructSpec.Width == 220 && It->CollisionBox->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block && !It->HasAuthority()) bWallReady = true;
        if (!bWallReady) return true;
        // new-champions: the Aetheri turret replicated with its kind, recipe, health and range, owned by this realm.
        if (!Counts.OwnTurret || Counts.OwnTurret->HasAuthority() || Counts.OwnTurret->ConstructSpec.Kind != ECireConstructKind::Turret ||
            Counts.OwnTurret->ConstructSpec.AttackRange < 900 || Counts.OwnTurret->Health <= 0 || Counts.OwnTurret->Health != Counts.OwnTurret->MaxHealth || Counts.OwnTurret->bMonsterOwned) return true;
        if (!Client.FirstShotAt) { Client.FirstShotAt = Now; Client.FirstShotPosition = Counts.OwnShot->GetActorLocation(); }
        if (Now - Client.FirstShotAt < .6) return true;
        // aura-vfx: the survival buff record replicates to its own client and drives the local aura; opponents never render one.
        if (!CireBuffs::IsActive(Hero, TEXT("blood_rage"))) return true;
        if (const auto* Aura = Hero->FindComponentByClass<UCireAuraComponent>(); !Aura || !Aura->Instances.ContainsByPredicate([](const FCireAuraInstance& I) { return I.Id == TEXT("blood_rage"); })) return true;
        for (TActorIterator<ACireHero> It(Controller->GetWorld()); It; ++It) if (It->TeamId == Enemy) if (const auto* Aura = It->FindComponentByClass<UCireAuraComponent>(); Aura && Aura->CountVertices() > 0) { Abort(TEXT("opposing hero aura rendered outside the arena")); return true; }
        if (FVector::Dist2D(Counts.OwnShot->GetActorLocation(), Client.FirstShotPosition) < 20) return true;
        // pets: the owner's companion replicates (id, stance, cooldowns, health); a Stay order sent over RPC comes back replicated.
        ACirePet* Pet = Counts.OwnPet;
        if (!Pet || Pet->HasAuthority() || Pet->OwnerHero != Hero || Pet->PetId != TEXT("sabercat") || Pet->Stance != ECirePetStance::Passive ||
            Pet->AbilityReadyAt.Num() != 3 || Pet->Health <= 0 || Pet->Health != Pet->MaxHealth || Hero->PetGrant != TEXT("sabercat")) return true;
        if (!Client.bPetCommandSent) { Controller->ServerPetCommand(static_cast<uint8>(ECirePetCommand::Stay)); Client.bPetCommandSent = true; return true; }
        if (Pet->Order != ECirePetOrder::Stay) return true;
        UE_LOG(LogCireExpansionNet, Display, TEXT("CIRE_PET_NET_CLIENT_PASS team=%d hp=%.0f stance=%d order=%d"), Team, Pet->Health, static_cast<int32>(Pet->Stance), static_cast<int32>(Pet->Order));
        Ack();
    }
    else if (Client.Stage == 2)
    {
        const auto* E = Event(FString::Printf(TEXT("CIRE_EXP_PVE_CRIT_%d"), Team));
        if (!E) return true;
        if (!E->bCritical || E->Amount != 40 || !E->bLocalSource || E->Source || E->Target || E->Sequence == 0) { Abort(TEXT("critical combat event payload incorrect")); return true; }
        bool bHealth = false;
        for (TActorIterator<ACireMonster> It(Controller->GetWorld()); It; ++It)
            if (It->Lane == Team && It->MonsterName == FString::Printf(TEXT("CIRE_EXP_CRIT_TARGET_%d"), Team) && It->Health == 160) bHealth = true;
        if (bHealth) Ack();
    }
    else if (Client.Stage == 3)
    {
        if ((Team == 0 && Hero->bDead && CireBuffs::Get(Hero) && CireBuffs::Get(Hero)->Buffs.IsEmpty() /* aura-vfx */ && Counts.Wall[0] == 0 && Counts.Shot[0] == 0 && Counts.Summon[0] == 0 && Counts.Turret[0] == 0 /* new-champions */ && Counts.Pet[0] == 0 /* pets: leaves with its dead owner */) ||
            (Team == 1 && Counts.Wall[1] == 1 && Counts.Shot[1] == 1 && Counts.Summon[1] == 1 && Counts.Turret[1] == 1 && Counts.Pet[1] == 1)) Ack();
    }
    else if (Client.Stage == 4 && State->Phase == 2)
    { if (Counts.Wall[0] == 1 && Counts.Wall[1] == 1 && Counts.Shot[0] == 1 && Counts.Shot[1] == 1 && Counts.Summon[0] == 1 && Counts.Summon[1] == 1 && CireBuffs::Get(Hero) && CireBuffs::Get(Hero)->Buffs.IsEmpty() /* aura-vfx: phase change drops records */ &&
        Counts.Pet[0] == 1 && Counts.Pet[1] == 1 /* pets: both companions are observable in the arena */) Ack(); }
    else if (Client.Stage == 5)
    {
        const auto* Critical = Event(TEXT("CIRE_EXP_PVP_CRIT")); const auto* Impact = Event(TEXT("CIRE_EXP_PVP_SKILLSHOT"));
        if (!Critical || !Impact) return true;
        const ACireHero* PvPTarget = nullptr; // champion-draft: expected amounts follow the target's class trait
        for (TActorIterator<ACireHero> It(Controller->GetWorld()); It; ++It) if (!Cast<ACireSummon>(*It) && It->TeamId == 1) PvPTarget = *It;
        if (!Critical->bCritical || Critical->Amount != CireClassTraits::ModifyIncomingDamage(PvPTarget, 20) || Impact->bCritical || Impact->Amount != CireClassTraits::ModifyIncomingDamage(PvPTarget, 30) ||
            Impact->bLocalSource != (Team == 0) || Impact->bLocalTarget != (Team == 1)) { Abort(TEXT("PvP critical/projectile telemetry payload mismatch")); return true; }
        bool bHealth = false;
        for (TActorIterator<ACireHero> It(Controller->GetWorld()); It; ++It) if (!Cast<ACireSummon>(*It) && It->TeamId == 1 && It->Health == 1000.f - CireClassTraits::ModifyIncomingDamage(*It, 20) - CireClassTraits::ModifyIncomingDamage(*It, 30)) bHealth = true;
        // aura-vfx: an arena buff record on the team-1 hero reaches both clients (opponents are observable in the arena).
        bool bArenaBuff = false; for (TActorIterator<ACireHero> It(Controller->GetWorld()); It; ++It) if (!Cast<ACireSummon>(*It) && It->TeamId == 1 && CireBuffs::IsActive(*It, TEXT("bastion_of_dawn"))) bArenaBuff = true;
        if (!bArenaBuff) return true;
        if (bHealth && Counts.Wall[0] == 0 && Counts.Wall[1] == 1 && Counts.FirstSummon && Counts.FirstSummon->Health == Counts.FirstSummon->MaxHealth - 10 /* fix/summons: primary-scaled health */ &&
            Counts.FirstPet && FMath::IsNearlyEqual(Counts.FirstPet->Health, Counts.FirstPet->MaxHealth - 17.f, .05f) /* pets: enemy champion damage replicated */) Ack();
    }
    else if(Client.Stage==8||Client.Stage==9)
    {
        ACireHero* Roller=nullptr;
        for(TActorIterator<ACireHero> It(Controller->GetWorld());It;++It)
            if(!Cast<ACireSummon>(*It)&&It->TeamId==0)Roller=*It;
        if(!Roller||!Roller->Mobility)return true;
        auto* M=Roller->Mobility.Get();
        if(Client.Stage==8)
        {
            if(Roller->GetActorLocation().Z>250||Hero->GetActorLocation().Z>250||
                !Hero->GetCharacterMovement()->IsMovingOnGround()||M->IsRolling()||M->bWalking||M->bStrafing||Roller->Energy!=100)return true;
            Client.RollOrigin=Roller->GetActorLocation();Ack();
        }
        else
        {
            if(Team==0&&!Client.bRollSent)
            {
                // Real component RPC path, including a repeated request which
                // must not charge twice or reset the accepted roll's timer.
                M->ServerSetWalk(true);M->ServerSetStrafe(true);
                M->ServerRoll(FVector::ForwardVector);M->ServerRoll(FVector::ForwardVector);
                Client.bRollSent=true;
            }
            if(M->IsRolling())Client.bSawRoll=true;
            if(!Client.bSawRoll||M->IsRolling()||!M->bWalking||!M->bStrafing||
                M->CooldownRemaining()<=0||!FMath::IsNearlyEqual(Roller->Energy,100-CireMovement::Tuning().RollEnergy)||
                FVector::Dist2D(Roller->GetActorLocation(),Client.RollOrigin)<100)return true;
            if(Client.LastAck!=Client.Stage)
                UE_LOG(LogCireExpansionNet,Display,TEXT("CIRE_MOVEMENT_NET_CLIENT_PASS team=%d role=%s saw_roll=1 cooldown=%.2f energy=%.1f travel=%.1f"),
                    Team,Team==0?TEXT("autonomous"):TEXT("simulated"),M->CooldownRemaining(),Roller->Energy,
                    FVector::Dist2D(Roller->GetActorLocation(),Client.RollOrigin));
            Ack();
        }
    }
    else if (Client.Stage == 6 && State->Phase == 4)
    { if (Counts.Wall[0] + Counts.Wall[1] + Counts.Shot[0] + Counts.Shot[1] + Counts.Summon[0] + Counts.Summon[1] == 0) Ack(); }
    else if (Client.Stage == 7)
    {
        UE_LOG(LogCireExpansionNet, Display, TEXT("CIRE_EXPANSION_NET_CLIENT_PASS team=%d remote_authority=0 stages=8 pve_privacy=1 critical_events=1 pvp_damage=50 cleanup=1 route_replication=1 movement_replication=1 construct_replication=1"), Team);
        Client.bDone = true; FPlatformMisc::RequestExitWithStatus(false, 0);
    }
    return true;
#else
    return false;
#endif
}

