#include "CireBalanceLab.h"
#include "CireGame.h"
#include "CireChampionRoster.h"
#include "CireChampionProfiles.h"
#include "CireNPCCombat.h"
#include "CireThreat.h"
#include "CireAreaEffects.h"
#include "CireSkillshot.h"
#include "CireConstruct.h"
#include "CireSummon.h"
#include "CirePets.h"
#include "CireSkillTuning.h"
#include "CireDeveloperTools.h"
#include "CireAttackSystem.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireBalanceLab,Log,All);

namespace
{
struct FFrozen
{
    TWeakObjectPtr<AActor> Actor;bool bTick=false,bHidden=false,bCollision=false,bMovementTick=false;
    EMovementMode Movement=MOVE_None;uint8 CustomMovement=0;FVector Velocity=FVector::ZeroVector;
    float GlobalCooldown=0;TWeakObjectPtr<AActor> Target;
};
struct FRun
{
    TWeakObjectPtr<ACireGameMode> Mode;
    FCireBalanceSnapshot View;bool bArena=false,bPlayer=false;FString Loadout=TEXT("baseline");
    Cires::MatchClock SavedClock;Cires::TeamRewards SavedRewards[2];
    TArray<ACireHero*> SavedHeroes;TArray<ACireMonster*> SavedMonsters;
    TArray<FFrozen> Frozen;TArray<TWeakObjectPtr<AActor>> Fixtures;
    TArray<TWeakObjectPtr<ACireHero>> Heroes;TArray<TWeakObjectPtr<ACireMonster>> Monsters;
    TWeakObjectPtr<APlayerController> Controller;TWeakObjectPtr<APawn> SavedPawn;TWeakObjectPtr<AActor> SavedView;
    FRotator SavedRotation;float SampleTimer=0,TankTargetSeconds=0,TargetSeconds=0,StartWorldTime=0;
    double ThreatRatioIntegral=0,ThreatRatioSeconds=0;
    int32 SavedPhase=0,SavedRound=1,SavedWave=0,SavedEmberLives=100,SavedDuskLives=100;
    float SavedSeconds=0;FString SavedAnnouncement,TuningJson;
    TArray<TSharedPtr<FJsonValue>> Samples,Participants;
    TMap<TWeakObjectPtr<ACireMonster>,TWeakObjectPtr<ACireHero>> LastVictim;
    // balance: one roster champion replaces a fixture slot; survival is measured as health lost per hero.
    FString Champion;int32 ChampionSlot=INDEX_NONE;
    TMap<TWeakObjectPtr<ACireHero>,float> LastHealth,DamageTaken,DiedAt;
    TMap<TWeakObjectPtr<ACireHero>,float> PetLastHealth,PetDamageTaken,PetTargetSeconds;TMap<TWeakObjectPtr<ACireHero>,int32> PetDeaths;TSet<TWeakObjectPtr<ACireHero>> PetWasDead;
};
TUniquePtr<FRun> Active;
FCireBalanceSnapshot Last;
bool bAutoRequested=false,bAutoStarted=false,bAutoPlayer=false,bAutoArena=false;
int32 AutoWave=1,AutoKind=-1,AutoSize=5,AutoEnemies=5;float AutoSeconds=60;
FString AutoLoadout=TEXT("baseline"),AutoChampion;int32 AutoSlot=INDEX_NONE;
bool ValidLoadout(const FString& Value)
{
    return Value==TEXT("baseline")||Value==TEXT("thematic")||Value==TEXT("tank_last_stand")||
        Value==TEXT("tank_challenge")||Value==TEXT("tank_seismic")||Value==TEXT("support_aegis")||
        Value==TEXT("support_wellspring")||Value==TEXT("dps_starfall")||Value==TEXT("dps_hunt");
}

TSharedPtr<FJsonObject> Metrics(const FCireBalanceSnapshot& V)
{
    auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("seconds"),V.ElapsedSeconds);
    J->SetNumberField(TEXT("damage"),V.Damage);J->SetNumberField(TEXT("healing"),V.Healing);
    J->SetNumberField(TEXT("dps"),V.DPS);J->SetNumberField(TEXT("hps"),V.HPS);
    J->SetNumberField(TEXT("enemyHealthRemaining"),V.EnemyHealthRemaining);
    J->SetNumberField(TEXT("alliesAlive"),V.AlliesAlive);J->SetNumberField(TEXT("enemiesAlive"),V.EnemiesAlive);
    J->SetNumberField(TEXT("tankTargetShare"),V.TankTargetShare);J->SetNumberField(TEXT("threatLeadRatio"),V.ThreatLeadRatio);
    J->SetNumberField(TEXT("victimSwitches"),V.VictimSwitches);return J;
}
void Sample(FRun& R,float Delta)
{
    auto& V=R.View;V.Damage=V.Healing=V.EnemyHealthRemaining=0;V.AlliesAlive=V.EnemiesAlive=0;
    for(const auto& Ref:R.Heroes)if(auto* H=Ref.Get())
    {
        if(H->TeamId==0){V.Damage+=H->DamageDone;V.Healing+=H->HealingDone;if(!H->bDead&&H->Health>0)++V.AlliesAlive;}
        else if(R.bArena&&!H->bDead&&H->Health>0){++V.EnemiesAlive;V.EnemyHealthRemaining+=H->Health;}
        {
            // Health lost between frames (net of same-frame healing): the survival pressure each hero absorbed.
            float& Previous=R.LastHealth.FindOrAdd(H,H->Health);
            if(H->Health<Previous)R.DamageTaken.FindOrAdd(H)+=Previous-H->Health;
            Previous=H->Health;
            if((H->bDead||H->Health<=0)&&!R.DiedAt.Contains(H))R.DiedAt.Add(H,R.View.ElapsedSeconds);
            // Companion survivability (pets): health lost and deaths, credited to the owner.
            if(const ACirePet* Pet=H->TeamId==0?CirePets::PetOf(H):nullptr)
            {
                float& PetPrevious=R.PetLastHealth.FindOrAdd(H,Pet->Health);
                if(Pet->Health<PetPrevious)R.PetDamageTaken.FindOrAdd(H)+=PetPrevious-Pet->Health;
                PetPrevious=Pet->Health;
                if(Pet->bDead&&!R.PetWasDead.Contains(H)){R.PetWasDead.Add(H);R.PetDeaths.FindOrAdd(H)++;}
                else if(!Pet->bDead)R.PetWasDead.Remove(H);
            }
        }
        // The lab measures one encounter, with no resurrection after elimination.
        if(H->bDead)H->RespawnTimer=600;
    }
    for(const auto& Ref:R.Monsters)if(auto* M=Ref.Get();IsValid(M)&&!M->IsActorBeingDestroyed()&&M->Health>0)
    {
        ++V.EnemiesAlive;V.EnemyHealthRemaining+=M->Health;
        if(IsValid(M->Victim))
        {
            R.TargetSeconds+=Delta;if(CireChampionProfiles::DraftRole(M->Victim)==Cires::SkillDraftRole::Tank)R.TankTargetSeconds+=Delta;
            if(const auto* Pet=Cast<ACirePet>(M->Victim);Pet&&Pet->GetOwnerHero())R.PetTargetSeconds.FindOrAdd(Pet->GetOwnerHero())+=Delta;
            auto& Previous=R.LastVictim.FindOrAdd(M);
            if(Previous.IsValid()&&Previous.Get()!=M->Victim)++V.VictimSwitches;Previous=M->Victim;
        }
        float Tank=0,Other=0;
        for(const auto& Pair:M->Threat)if(Pair.Key.IsValid())
        {
            if(CireChampionProfiles::DraftRole(Pair.Key.Get())==Cires::SkillDraftRole::Tank)Tank=FMath::Max(Tank,Pair.Value);else Other=FMath::Max(Other,Pair.Value);
        }
        if((Tank>0||Other>0)&&Delta>0)
        {
            R.ThreatRatioIntegral+=FMath::Min(10000.f,Tank/FMath::Max(1.f,Other))*Delta;
            R.ThreatRatioSeconds+=Delta;
        }
    }
    V.ThreatLeadRatio=R.ThreatRatioSeconds>0?R.ThreatRatioIntegral/R.ThreatRatioSeconds:0;
    V.TankTargetShare=R.TargetSeconds>0?R.TankTargetSeconds/R.TargetSeconds:0;
    V.DPS=V.Damage/FMath::Max(.001f,V.ElapsedSeconds);V.HPS=V.Healing/FMath::Max(.001f,V.ElapsedSeconds);
}
void WriteReport(FRun& R)
{
    auto J=Metrics(R.View);J->SetStringField(TEXT("kind"),TEXT("measured-runtime"));
    J->SetStringField(TEXT("scenario"),R.View.Scenario);J->SetStringField(TEXT("result"),R.View.Result);
    J->SetStringField(TEXT("loadoutPreset"),R.Loadout);
    J->SetStringField(TEXT("champion"),R.Champion);J->SetNumberField(TEXT("championSlot"),R.ChampionSlot);
    J->SetNumberField(TEXT("schemaVersion"),1);J->SetStringField(TEXT("engineVersion"),TEXT("5.8.3"));
    J->SetNumberField(TEXT("enemyInitialHealth"),R.View.EnemyInitialHealth);J->SetNumberField(TEXT("limitSeconds"),R.View.LimitSeconds);
    J->SetStringField(TEXT("method"),TEXT("Actual authoritative combat actors, role fixtures, world collision, live hit/crit rolls and effective combat meters; not deterministic. Threat sampled each frame. Threat lead ratio is the enemy-seconds weighted mean of tank threat divided by highest non-tank threat (denominator at least 1, ratio at most 10000). Fixed fixture loadouts bypass normal drafting only inside the lab."));
    J->SetBoolField(TEXT("playerControlled"),R.bPlayer);J->SetBoolField(TEXT("arena"),R.bArena);
    const auto& D=CireDeveloperTools::Get(R.Mode->GetWorld());auto Profile=MakeShared<FJsonObject>();
    Profile->SetBoolField(TEXT("enabled"),D.bEnabled);Profile->SetNumberField(TEXT("monsterHealthScale"),D.MonsterHealthScale);
    Profile->SetNumberField(TEXT("monsterDamageScale"),D.MonsterDamageScale);Profile->SetNumberField(TEXT("projectileSpeedScale"),D.ProjectileSpeedScale);
    Profile->SetNumberField(TEXT("projectileCollisionScale"),D.ProjectileCollisionScale);Profile->SetNumberField(TEXT("cooldownScale"),D.CooldownScale);
    Profile->SetNumberField(TEXT("effectDurationScale"),D.EffectDurationScale);Profile->SetNumberField(TEXT("telegraphScale"),D.TelegraphScale);
    Profile->SetNumberField(TEXT("summonHealthScale"),D.SummonHealthScale);J->SetObjectField(TEXT("developerProfileAtCompletion"),Profile);
    J->SetArrayField(TEXT("samples"),R.Samples);J->SetArrayField(TEXT("initialParticipants"),R.Participants);
    TSharedPtr<FJsonObject> Tuning;
    if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(R.TuningJson),Tuning)&&Tuning.IsValid())J->SetObjectField(TEXT("tuningFileSnapshot"),Tuning);
    TArray<TSharedPtr<FJsonValue>> End;
    for(const auto& Ref:R.Heroes)if(auto* H=Ref.Get())
    {
        auto P=MakeShared<FJsonObject>();P->SetStringField(TEXT("name"),H->HeroName);P->SetNumberField(TEXT("team"),H->TeamId);
        P->SetNumberField(TEXT("damage"),H->DamageDone);P->SetNumberField(TEXT("healing"),H->HealingDone);P->SetNumberField(TEXT("health"),H->Health);
        P->SetStringField(TEXT("draftRole"),UTF8_TO_TCHAR(Cires::DraftRoleName(CireChampionProfiles::DraftRole(H))));
        P->SetNumberField(TEXT("dps"),H->DamageDone/FMath::Max(.001f,R.View.ElapsedSeconds));P->SetNumberField(TEXT("hps"),H->HealingDone/FMath::Max(.001f,R.View.ElapsedSeconds));
        P->SetNumberField(TEXT("teamDamageShare"),H->TeamId==0?H->DamageDone/FMath::Max(1.f,R.View.Damage):0);
        P->SetNumberField(TEXT("level"),H->Level);P->SetBoolField(TEXT("dead"),H->bDead);
        P->SetStringField(TEXT("champion"),H->ChampionProfileId);P->SetNumberField(TEXT("maxHealth"),H->MaxHealth);
        const float Taken=R.DamageTaken.FindRef(H);P->SetNumberField(TEXT("damageTaken"),Taken);
        P->SetNumberField(TEXT("dtps"),Taken/FMath::Max(.001f,R.View.ElapsedSeconds));
        P->SetNumberField(TEXT("diedAtSeconds"),R.DiedAt.Contains(H)?R.DiedAt.FindRef(H):-1.f);
        if(CirePets::ForOwner(H))
        {
            P->SetNumberField(TEXT("petDamageTaken"),R.PetDamageTaken.FindRef(H));P->SetNumberField(TEXT("petDeaths"),R.PetDeaths.FindRef(H));
            P->SetNumberField(TEXT("petTargetShare"),R.TargetSeconds>0?R.PetTargetSeconds.FindRef(H)/R.TargetSeconds:0.f);
        }
        P->SetBoolField(TEXT("labChampion"),H->TeamId==0&&!R.Champion.IsEmpty()&&H->ChampionProfileId==R.Champion);
        End.Add(MakeShared<FJsonValueObject>(P));
    }
    J->SetArrayField(TEXT("finalHeroes"),End);
    TArray<TSharedPtr<FJsonValue>> Gates;
    if(R.View.Result==TEXT("timeout"))Gates.Add(MakeShared<FJsonValueString>(TEXT("encounter_timeout")));
    if(R.View.Damage<=0)Gates.Add(MakeShared<FJsonValueString>(TEXT("no_effective_damage")));
    if(R.View.Result==TEXT("allies_won")&&R.View.ElapsedSeconds<5)Gates.Add(MakeShared<FJsonValueString>(TEXT("kill_time_under_5_seconds")));
    if(R.View.ElapsedSeconds>90)Gates.Add(MakeShared<FJsonValueString>(TEXT("encounter_over_90_seconds")));
    J->SetArrayField(TEXT("reviewFlags"),Gates);
    const FString Directory=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("BalanceLab"));IFileManager::Get().MakeDirectory(*Directory,true);
    R.View.ReportPath=FPaths::Combine(Directory,FString::Printf(TEXT("runtime-%s-%s.json"),*FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")),*FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8)));
    FString Text;FJsonSerializer::Serialize(J.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));
    if(!FFileHelper::SaveStringToFile(Text,*R.View.ReportPath)){UE_LOG(LogCireBalanceLab,Error,TEXT("Balance report could not be saved"));R.View.ReportPath.Reset();}
    UE_LOG(LogCireBalanceLab,Display,TEXT("CIRE_BALANCE_LAB_RESULT result=%s seconds=%.2f dps=%.2f hps=%.2f tank_share=%.2f switches=%d report=%s"),*R.View.Result,R.View.ElapsedSeconds,R.View.DPS,R.View.HPS,R.View.TankTargetShare,R.View.VictimSwitches,*R.View.ReportPath);
}
void Freeze(FRun& R,AActor* Actor)
{
    FFrozen Saved;Saved.Actor=Actor;Saved.bTick=Actor->IsActorTickEnabled();Saved.bHidden=Actor->IsHidden();Saved.bCollision=Actor->GetActorEnableCollision();
    if(auto* C=Cast<ACharacter>(Actor))
    {
        auto* Move=C->GetCharacterMovement();Saved.Movement=Move->MovementMode;Saved.CustomMovement=Move->CustomMovementMode;
        Saved.Velocity=Move->Velocity;Saved.bMovementTick=Move->IsComponentTickEnabled();Move->StopMovementImmediately();Move->SetComponentTickEnabled(false);
    }
    if(auto* H=Cast<ACireHero>(Actor)){Saved.GlobalCooldown=H->GlobalCooldown;Saved.Target=H->Target;H->GlobalCooldown=1000000;H->Target=nullptr;}
    Actor->SetActorTickEnabled(false);Actor->SetActorEnableCollision(false);Actor->SetActorHiddenInGame(true);R.Frozen.Add(Saved);
}
ACireHero* CreateHero(FRun& R,int32 Team,int32 Index,FVector Ground,int32 Level)
{
    auto* Mode=R.Mode.Get();const int32 Roles[]={0,2,1,3,4};const int32 Role=Roles[Index%5];
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FVector Location=Ground+FVector(Team==0?-480:480,(Index-2)*140.f,92);
    auto* H=Mode->GetWorld()->SpawnActor<ACireHero>(Location,FRotator(0,Team?180:0,0),P);if(!H)return nullptr;
    R.Fixtures.Add(H);R.Heroes.Add(H);Mode->Heroes.Add(H);H->TeamId=Team;H->bBot=true;
    const bool bChampion=Team==0&&Index==R.ChampionSlot&&!R.Champion.IsEmpty();
    if(bChampion){if(!H->DraftProfile(R.Champion))return nullptr;}else H->Draft(Role);
    Cires::GainLevels(H->Progression,FMath::Clamp(Level,1,1000)-1);H->Progression.NextAugmentLevel=10001;H->Recalculate(true);
    if(Role==0)H->Skills={TEXT("shield_slam"),TEXT("war_cry"),TEXT("iron_guard")};
    else if(Role==2)H->Skills={TEXT("restoring_light"),TEXT("sanctuary"),TEXT("ember_lance")};
    else if(Role==1)H->Skills={TEXT("piercing_shot"),TEXT("frost_bind")};
    else if(Role==3)H->Skills={TEXT("cleaving_strike"),TEXT("piercing_shot")};
    else H->Skills={TEXT("oathbound_guardian"),TEXT("spectral_pack"),TEXT("ember_lance")};
    if(R.Loadout!=TEXT("baseline")||bChampion)
    {
        const auto* Profile=H->ChampionProfile();if(!Profile)return nullptr;
        H->Skills.Reset();
        for(const auto& Skill:Profile->Actives){if(!Skill.IsImplemented())return nullptr;H->Skills.Add(Skill.Id);}
        H->Skills.Add(Profile->Passive.Id);H->Skills.Add(Profile->Ultimate.Id);
        FString Replacement;
        if(Role==0)
        {
            if(R.Loadout==TEXT("tank_last_stand"))Replacement=TEXT("last_stand");
            if(R.Loadout==TEXT("tank_challenge"))Replacement=TEXT("challenge_of_iron");
            if(R.Loadout==TEXT("tank_seismic"))Replacement=TEXT("seismic_reprisal");
        }
        else if(Role==2)
        {
            if(R.Loadout==TEXT("support_aegis"))Replacement=TEXT("mass_aegis");
            if(R.Loadout==TEXT("support_wellspring"))Replacement=TEXT("wellspring");
        }
        else
        {
            if(R.Loadout==TEXT("dps_starfall"))Replacement=TEXT("starfall");
            if(R.Loadout==TEXT("dps_hunt"))Replacement=TEXT("spectral_hunt");
        }
        if(!Replacement.IsEmpty()&&!bChampion)H->Skills.Last()=Replacement;
        // Signature kits belong to their champion, not to a generic role pool.
        if(!bChampion)for(const auto& Id:H->Skills)if(!Cires::IsSkillAllowedForRole(TCHAR_TO_UTF8(*Id),CireChampionProfiles::DraftRole(H)))return nullptr;
    }
    H->Cooldowns.Init(0,H->Skills.Num());H->HeroName=FString::Printf(TEXT("Lab %s %d | %s"),Team?TEXT("Dusk"):TEXT("Ember"),Index+1,*H->HeroName);
    H->HomePosition=Location;H->Gold=0;H->DamageDone=H->HealingDone=0;
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("name"),H->HeroName);J->SetNumberField(TEXT("team"),Team);
    J->SetNumberField(TEXT("archetype"),Role);J->SetNumberField(TEXT("level"),H->Level);J->SetNumberField(TEXT("health"),H->MaxHealth);
    J->SetNumberField(TEXT("basicDamage"),H->AttackDamage());J->SetNumberField(TEXT("agility"),H->Agility);
    J->SetStringField(TEXT("draftRole"),UTF8_TO_TCHAR(Cires::DraftRoleName(CireChampionProfiles::DraftRole(H))));
    J->SetNumberField(TEXT("basicAttackRange"),H->BasicAttackRange());J->SetStringField(TEXT("loadoutPreset"),R.Loadout);
    J->SetStringField(TEXT("champion"),H->ChampionProfileId);
    J->SetNumberField(TEXT("critChance"),H->CriticalChance);J->SetNumberField(TEXT("critMultiplier"),H->CriticalMultiplier);
    TArray<TSharedPtr<FJsonValue>> Skills;for(const auto& Skill:H->Skills)Skills.Add(MakeShared<FJsonValueString>(Skill));J->SetArrayField(TEXT("skills"),Skills);
    R.Participants.Add(MakeShared<FJsonValueObject>(J));return H;
}
bool Begin(ACireGameMode* Mode,int32 Wave,int32 Kind,bool bPlayer,int32 TeamSize,int32 EnemyCount,float Seconds,bool bArena,const FString& Loadout=TEXT("baseline"),
    const FString& Champion=FString(),int32 ChampionSlot=INDEX_NONE)
{
#if UE_BUILD_SHIPPING
    return false;
#else
    if(!IsValid(Mode)||!Mode->HasAuthority()||!ValidLoadout(Loadout)||!FMath::IsFinite(Seconds)||Seconds<5||Seconds>300||Wave<1||Wave>1000||Kind<-1||Kind>3||TeamSize<1||TeamSize>5||EnemyCount<1||EnemyCount>20)return false;
    if(Active){if(Active->Mode.IsValid())CireBalanceLab::Stop(Active->Mode.Get());else Active.Reset();}
    auto* PC=Mode->GetWorld()->GetFirstPlayerController();if(bPlayer&&(!PC||!PC->GetPawn()))return false;
    const FCireChampionProfile* LabProfile=Champion.IsEmpty()?nullptr:CireChampionRoster::Find(Champion);
    if(!Champion.IsEmpty()&&(!LabProfile||bPlayer))return false;
    Active=MakeUnique<FRun>();auto& R=*Active;R.Mode=Mode;R.bArena=bArena;R.bPlayer=bPlayer;R.Loadout=Loadout;
    if(LabProfile)
    {
        // Default slot: the fixture of the champion's threat role (tank 0, healer 1, damage 2 = the ranger).
        R.Champion=LabProfile->Id;
        R.ChampionSlot=ChampionSlot>=0&&ChampionSlot<TeamSize?ChampionSlot:LabProfile->ThreatRole==TEXT("tank")?0:LabProfile->ThreatRole==TEXT("healer")?1:FMath::Min(2,TeamSize-1);
    }
    R.StartWorldTime=Mode->GetWorld()->GetTimeSeconds();
    R.SavedClock=Mode->Clock;R.SavedHeroes=Mode->Heroes;R.SavedMonsters=Mode->Monsters;
    for(int32 T=0;T<2;++T){R.SavedRewards[T]=Mode->Rewards[T];Mode->Rewards[T]=Cires::TeamRewards{};}
    if(auto* State=Mode->GetGameState<ACireGameState>())
    {
        R.SavedPhase=State->Phase;R.SavedRound=State->Round;R.SavedWave=State->Wave;R.SavedSeconds=State->SecondsLeft;
        R.SavedEmberLives=State->EmberLives;R.SavedDuskLives=State->DuskLives;R.SavedAnnouncement=State->Announcement;
        State->Round=1;State->Wave=Wave;
    }
    for(TActorIterator<AActor> It(Mode->GetWorld());It;++It)if(Cast<ACireHero>(*It)||Cast<ACireMonster>(*It)||Cast<ACireAreaEffect>(*It)||Cast<ACireSkillshot>(*It)||Cast<ACireConstruct>(*It)||Cast<ACireTargetProjectile>(*It))Freeze(R,*It);
    Mode->Heroes.Reset();Mode->Monsters.Reset();Mode->Clock=Cires::MatchClock();
    if(bArena){Mode->Clock.BeginIntermission();Mode->Clock.Advance(60);}
    FVector Ground(0,bArena?Mode->ArenaPosition(0,2).Y:-2100,3000);
    auto* Platform=Mode->GetWorld()->SpawnActor<AActor>();
    if(!Platform){R.View.Result=TEXT("spawn_failed");CireBalanceLab::Stop(Mode);return false;}
    R.Fixtures.Add(Platform);auto* Box=NewObject<UBoxComponent>(Platform);Platform->SetRootComponent(Box);Platform->AddInstanceComponent(Box);
    Box->SetBoxExtent(FVector(1800,900,50));Box->SetCollisionObjectType(ECC_WorldStatic);Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Box->SetCollisionResponseToAllChannels(ECR_Block);Box->RegisterComponent();Platform->SetActorLocation(Ground-FVector(0,0,50));
    auto* Mesh=NewObject<UStaticMeshComponent>(Platform);Platform->AddInstanceComponent(Mesh);Mesh->SetupAttachment(Box);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Mesh->SetRelativeScale3D(FVector(36,18,1));Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->RegisterComponent();
    for(int32 Team=0;Team<(bArena?2:1);++Team)for(int32 I=0;I<TeamSize;++I)
        if(!CreateHero(R,Team,I,Ground,FMath::Clamp(Wave,1,1000))){R.View.Result=TEXT("spawn_failed");CireBalanceLab::Stop(Mode);return false;}
    if(!bArena)for(int32 I=0;I<EnemyCount;++I)
    {
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* M=Mode->GetWorld()->SpawnActor<ACireMonster>(Ground+FVector(280+(I/5)*130,(I%5-2)*150,88),FRotator(0,180,0),P);
        if(!M){R.View.Result=TEXT("spawn_failed");CireBalanceLab::Stop(Mode);return false;}
        M->Lane=0;R.Fixtures.Add(M);R.Monsters.Add(M);Mode->Monsters.Add(M);CireNPCCombat::Configure(M,Kind<0?I%4:Kind,Wave);
        CireThreat::Engage(M,R.Heroes[0].Get());R.View.EnemyInitialHealth+=M->MaxHealth;
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("name"),M->MonsterName);J->SetNumberField(TEXT("archetype"),M->CombatArchetype);
        J->SetNumberField(TEXT("health"),M->MaxHealth);J->SetNumberField(TEXT("damage"),M->Damage);R.Participants.Add(MakeShared<FJsonValueObject>(J));
    }
    else for(const auto& H:R.Heroes)if(H.IsValid()&&H->TeamId==1)R.View.EnemyInitialHealth+=H->MaxHealth;
    if(PC)
    {
        R.Controller=PC;R.SavedPawn=PC->GetPawn();R.SavedView=PC->GetViewTarget();R.SavedRotation=PC->GetControlRotation();
        if(bPlayer){auto* H=R.Heroes[0].Get();H->bBot=false;PC->Possess(H);H->Notice=TEXT("Balance lab: temporary level-scaled tank loadout. Stop restores your champion.");}
        else PC->SetViewTarget(R.Heroes[0].Get());
        PC->SetControlRotation(FRotator(-45,0,0));
    }
    R.View.bActive=true;R.View.LimitSeconds=Seconds;
    R.View.Scenario=FString::Printf(TEXT("%s %dv%d | wave %d | %s%s"),bArena?TEXT("arena"):TEXT("wave"),TeamSize,bArena?TeamSize:EnemyCount,Wave,bPlayer?TEXT("player + AI"):TEXT("all AI"),
        R.Champion.IsEmpty()?TEXT(""):*FString::Printf(TEXT(" | champion %s slot %d"),*R.Champion,R.ChampionSlot));
    FFileHelper::LoadFileToString(R.TuningJson,*FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/CombatTuning.json")));
    Sample(R,0);UE_LOG(LogCireBalanceLab,Display,TEXT("CIRE_BALANCE_LAB_STARTED %s limit=%.0f"),*R.View.Scenario,Seconds);return true;
#endif
}
}

void CireBalanceLab::Initialize(ACireGameMode* Mode)
{
#if !UE_BUILD_SHIPPING
    FString Choice;
    AutoWave=1;AutoKind=-1;AutoSize=5;AutoEnemies=5;AutoSeconds=60;AutoLoadout=TEXT("baseline");
    bAutoRequested=FParse::Param(FCommandLine::Get(),TEXT("CireBalanceLab"))||FParse::Value(FCommandLine::Get(),TEXT("CireBalanceLab="),Choice);
    bAutoPlayer=Choice.Contains(TEXT("player"),ESearchCase::IgnoreCase);bAutoArena=Choice.Contains(TEXT("arena"),ESearchCase::IgnoreCase);bAutoStarted=false;
    FParse::Value(FCommandLine::Get(),TEXT("CireBalanceWave="),AutoWave);FParse::Value(FCommandLine::Get(),TEXT("CireBalanceKind="),AutoKind);
    FParse::Value(FCommandLine::Get(),TEXT("CireBalanceBots="),AutoSize);FParse::Value(FCommandLine::Get(),TEXT("CireBalanceEnemies="),AutoEnemies);
    FParse::Value(FCommandLine::Get(),TEXT("CireBalanceSeconds="),AutoSeconds);
    FParse::Value(FCommandLine::Get(),TEXT("CireBalanceLoadout="),AutoLoadout);
    AutoChampion.Reset();AutoSlot=INDEX_NONE;
    FParse::Value(FCommandLine::Get(),TEXT("CireBalanceChampion="),AutoChampion);FParse::Value(FCommandLine::Get(),TEXT("CireBalanceSlot="),AutoSlot);
#endif
}
bool CireBalanceLab::Start(ACireGameMode* M,int32 Wave,int32 Kind,bool Player,int32 Size,int32 Enemies,float Seconds){return Begin(M,Wave,Kind,Player,Size,Enemies,Seconds,false);}
bool CireBalanceLab::StartArena(ACireGameMode* M,bool Player,int32 Size,float Seconds){return Begin(M,10,-1,Player,Size,Size,Seconds,true);}
bool CireBalanceLab::StartLoadout(ACireGameMode* M,const FString& Preset,int32 Wave,int32 Kind,int32 Size,int32 Enemies,float Seconds){return Begin(M,Wave,Kind,false,Size,Enemies,Seconds,false,Preset);}
bool CireBalanceLab::IsActive(const ACireGameMode* M){return Active&&Active->Mode.Get()==M&&Active->View.bActive;}
FCireBalanceSnapshot CireBalanceLab::Snapshot(const ACireGameMode* M){return IsActive(M)?Active->View:Last;}
FString CireBalanceLab::Summary(const ACireGameMode* M)
{
    const auto S=Snapshot(M);return FString::Printf(TEXT("%s | %s %.1fs | DPS %.1f HPS %.1f | alive %d:%d | tank %.0f%% | %s"),*S.Scenario,S.bActive?TEXT("running"):*S.Result,S.ElapsedSeconds,S.DPS,S.HPS,S.AlliesAlive,S.EnemiesAlive,S.TankTargetShare*100,*S.ReportPath);
}
bool CireBalanceLab::Tick(ACireGameMode* Mode,float Delta)
{
    if(!Mode||!Mode->HasAuthority())return false;
    if(bAutoRequested&&!bAutoStarted&&(!bAutoPlayer||Mode->GetWorld()->GetFirstPlayerController()&&Mode->GetWorld()->GetFirstPlayerController()->GetPawn()))
    {
        bAutoStarted=true;if(!Begin(Mode,AutoWave,AutoKind,bAutoPlayer,AutoSize,AutoEnemies,AutoSeconds,bAutoArena,AutoLoadout,AutoChampion,AutoSlot))UE_LOG(LogCireBalanceLab,Warning,TEXT("Balance lab rejected its launch parameters"));
    }
    if(!IsActive(Mode))return false;
    if(Mode->Clock.Phase()!=(Active->bArena?Cires::MatchPhase::Arena:Cires::MatchPhase::Survival))
    {Active->View.Result=TEXT("phase_changed");Stop(Mode);return true;}
    auto& R=*Active;Delta=FMath::IsFinite(Delta)?FMath::Max(Delta,0.f):0;
    R.View.ElapsedSeconds=FMath::Max(0.f,Mode->GetWorld()->GetTimeSeconds()-R.StartWorldTime);Sample(R,Delta);R.SampleTimer+=Delta;
    if(R.SampleTimer>=1){R.SampleTimer=0;R.Samples.Add(MakeShared<FJsonValueObject>(Metrics(R.View)));}
    if(auto* State=Mode->GetGameState<ACireGameState>())
    {
        State->Phase=static_cast<int32>(Mode->Clock.Phase());State->SecondsLeft=FMath::Max(0.f,R.View.LimitSeconds-R.View.ElapsedSeconds);
        State->Announcement=TEXT("BALANCE LAB | ")+Summary(Mode);State->ForceNetUpdate();
    }
    if(R.View.EnemiesAlive==0)R.View.Result=TEXT("allies_won");
    else if(R.View.AlliesAlive==0)R.View.Result=TEXT("allies_lost");
    else if(R.View.ElapsedSeconds>=R.View.LimitSeconds)R.View.Result=TEXT("timeout");
    if(!R.View.Result.IsEmpty())Stop(Mode);
    return true;
}
void CireBalanceLab::Stop(ACireGameMode* Mode)
{
    if(!Active||Active->Mode.Get()!=Mode)return;
    auto& R=*Active;if(R.View.Result.IsEmpty())R.View.Result=TEXT("stopped");Sample(R,0);WriteReport(R);
    if(auto* PC=R.Controller.Get())
    {
        if(R.bPlayer&&R.SavedPawn.IsValid())PC->Possess(R.SavedPawn.Get());
        if(R.SavedView.IsValid())PC->SetViewTarget(R.SavedView.Get());PC->SetControlRotation(R.SavedRotation);
    }
    for(const auto& H:R.Heroes)if(H.IsValid()){ACireSummon::ClearForActor(H.Get());ACireAreaEffect::ClearForActor(H.Get());ACireSkillshot::ClearForActor(H.Get());ACireConstruct::ClearForActor(H.Get());CireThreat::Remove(H.Get());}
    for(const auto& M:R.Monsters)if(M.IsValid())CireNPCCombat::Interrupt(M.Get());
    for(int32 I=R.Fixtures.Num()-1;I>=0;--I)if(R.Fixtures[I].IsValid())R.Fixtures[I]->Destroy();
    Mode->Clock=R.SavedClock;Mode->Heroes=R.SavedHeroes;Mode->Monsters=R.SavedMonsters;
    for(int32 T=0;T<2;++T)Mode->Rewards[T]=R.SavedRewards[T];
    for(const auto& Saved:R.Frozen)if(auto* A=Saved.Actor.Get())
    {
        A->SetActorTickEnabled(Saved.bTick);A->SetActorHiddenInGame(Saved.bHidden);A->SetActorEnableCollision(Saved.bCollision);
        if(auto* C=Cast<ACharacter>(A)){auto* Move=C->GetCharacterMovement();Move->SetMovementMode(Saved.Movement,Saved.CustomMovement);Move->SetComponentTickEnabled(Saved.bMovementTick);Move->Velocity=Saved.Velocity;}
        const float Paused=FMath::Max(0.f,Mode->GetWorld()->GetTimeSeconds()-R.StartWorldTime);
        if(auto* H=Cast<ACireHero>(A))
        {
            H->GlobalCooldown=Saved.GlobalCooldown;H->Target=Saved.Target.Get();
            if(H->ShieldUntil>R.StartWorldTime)H->ShieldUntil+=Paused;if(H->TauntUntil>R.StartWorldTime)H->TauntUntil+=Paused;if(H->SlowUntil>R.StartWorldTime)H->SlowUntil+=Paused;
            if(H->AttackStartedServerTime>0)H->AttackStartedServerTime+=Paused;
        }
        if(auto* M=Cast<ACireMonster>(A))
        {
            if(M->SlowUntil>R.StartWorldTime)M->SlowUntil+=Paused;if(M->ForcedVictimUntil>R.StartWorldTime)M->ForcedVictimUntil+=Paused;
            if(!M->CastingAbility.IsEmpty()){M->CastStartedAt+=Paused;M->CastEndsAt+=Paused;}
        }
        if(auto* Area=Cast<ACireAreaEffect>(A))Area->StartServerTime+=Paused;
        if(auto* Shot=Cast<ACireSkillshot>(A))Shot->StartServerTime+=Paused;
        if(auto* Summon=Cast<ACireSummon>(A))Summon->ExpiresServerTime+=Paused;
    }
    if(auto* S=Mode->GetGameState<ACireGameState>())
    {
        S->Phase=R.SavedPhase;S->Round=R.SavedRound;S->Wave=R.SavedWave;S->SecondsLeft=R.SavedSeconds;
        S->EmberLives=R.SavedEmberLives;S->DuskLives=R.SavedDuskLives;S->Announcement=R.SavedAnnouncement;S->ForceNetUpdate();
    }
    R.View.bActive=false;Last=R.View;Active.Reset();
    if(FParse::Param(FCommandLine::Get(),TEXT("CireBalanceExit")))
        FPlatformMisc::RequestExitWithStatus(false,Last.ReportPath.IsEmpty()||Last.Result==TEXT("spawn_failed")?1:0);
}
