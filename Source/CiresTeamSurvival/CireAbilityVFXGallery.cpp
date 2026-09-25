// ability-vfx: one-by-one ability audit harness (see CireAbilityVFXGallery.h, Docs/AbilityVFXAudit.md).
#include "CireAbilityVFXGallery.h"
#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireAreaEffects.h"
#include "CireAbilityLibrary.h"
#include "CireChampionRoster.h"
#include "CireConstruct.h"
#include "CireNPCArchetypes.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"
#include "CireRaces.h"
#include "CireSkillshot.h"
#include "CireSkillTuning.h"
#include "CireSpellPresentation.h"
#include "CireSummon.h"
#include "CireTargeting.h"
#include "CireAbilityShapes.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
struct FEntry
{
    FString Id,Label,Group;       // Group: champion | basic | passive | monster race id
    bool bMonster=false;
    FName Archetype;              // monster caster archetype
    FString Profile;              // champion profile that owns the skill
};
struct FFrame { FString Name; float At=0; };
struct FState
{
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACireController> PC;
    TWeakObjectPtr<ACameraActor> Camera;
    TWeakObjectPtr<AStaticMeshActor> Floor;
    TWeakObjectPtr<ACireHero> Hero;
    TArray<TWeakObjectPtr<AActor>> Actors;
    TArray<FEntry> Entries;
    TArray<FFrame> Frames;
    TArray<FString> Files,ManifestLines;
    FString Directory,Tag;
    int32 Index=-1,FrameIndex=0,Refused=0;
    double Start=0,FinishAt=-1;float EntryAt=0,CastAt=-1;
    bool bDone=false,bChecks=true,bAimArmed=false,bCastRefused=false;
};
FState G;
const FVector Stage(4000,-2100,5200);
float Now(){return G.Mode.IsValid()?G.Mode->GetWorld()->GetTimeSeconds():0.f;}
void Finish(bool bPass)
{
    if(G.bDone)return;G.bDone=true;
    CireTargeting::DebugSetAimOverride({});
    FFileHelper::SaveStringToFile(FString::Join(G.ManifestLines,TEXT("\n")),*FPaths::Combine(G.Directory,TEXT("manifest.jsonl")));
    UE_LOG(LogTemp,Display,TEXT("CIRE_ABILITY_VFX_GALLERY_%s abilities=%d refused=%d captures=%d directory=%s"),bPass?TEXT("PASS"):TEXT("FAIL"),
        G.Entries.Num(),G.Refused,G.Files.Num(),*G.Directory);
    FPlatformMisc::RequestExitWithStatus(false,bPass?0:1);
}
FString ProfileFor(const FString& Id)
{
    static const TMap<FString,FString> Fixed={{TEXT("second_wind"),TEXT("knight")},{TEXT("last_stand"),TEXT("knight")},{TEXT("challenge_of_iron"),TEXT("knight")},
        {TEXT("seismic_reprisal"),TEXT("knight")},{TEXT("starfall"),TEXT("wizard")},{TEXT("spectral_hunt"),TEXT("ranger")},{TEXT("mass_aegis"),TEXT("paladin_holy")},
        {TEXT("wellspring"),TEXT("dryad")},{TEXT("blight_sigil"),TEXT("summoner")},{TEXT("basic_sword"),TEXT("knight")},{TEXT("basic_bow"),TEXT("ranger")},
        {TEXT("basic_lance"),TEXT("lancer")},{TEXT("basic_arcane"),TEXT("scholar")}};
    if(const FString* P=Fixed.Find(Id))return *P;
    for(const auto& Profile:CireChampionRoster::All())
    {
        if(Profile.Passive.Id==Id||Profile.Ultimate.Id==Id)return Profile.Id;
        for(const auto& A:Profile.Actives)if(A.Id==Id)return Profile.Id;
    }
    return TEXT("knight");
}
void BuildEntries(const FString& Set,const TArray<FString>& Only)
{
    TArray<FString> Champion;
    for(const auto& Profile:CireChampionRoster::All())
    {
        for(const auto& A:Profile.Actives)if(A.IsImplemented())Champion.AddUnique(A.Id);
        if(Profile.Ultimate.IsImplemented())Champion.AddUnique(Profile.Ultimate.Id);
        if(Profile.Passive.IsImplemented())Champion.AddUnique(Profile.Passive.Id);
    }
    for(const TCHAR* Id:{TEXT("blight_sigil"),TEXT("second_wind"),TEXT("last_stand"),TEXT("challenge_of_iron"),TEXT("seismic_reprisal"),TEXT("starfall"),
        TEXT("spectral_hunt"),TEXT("mass_aegis"),TEXT("wellspring")})Champion.AddUnique(Id);
    for(const TCHAR* Id:{TEXT("basic_sword"),TEXT("basic_bow"),TEXT("basic_lance"),TEXT("basic_arcane")})Champion.AddUnique(Id);
    auto Wanted=[&](const FString& Id){return Only.IsEmpty()||Only.Contains(Id);};
    if(Set!=TEXT("monster"))
        for(const FString& Id:Champion)if(Wanted(Id))
        {
            FEntry E;E.Id=Id;E.Profile=ProfileFor(Id);E.Label=Id.StartsWith(TEXT("basic_"))?Id:ACireHero::SkillName(Id);
            E.Group=Id.StartsWith(TEXT("basic_"))?TEXT("basic"):ACireHero::IsPassive(Id)?TEXT("passive"):TEXT("champion");G.Entries.Add(E);
        }
    if(Set==TEXT("champion"))return;
    // Monster abilities: every distinct authored id, cast by the first archetype that owns it (races first).
    TSet<FName> Seen;const auto& Db=CireNPCArchetypes::Get();
    TArray<FName> Order;
    for(const FName Race:CireRaces::Get().Order)if(const auto* R=CireRaces::FindRace(Race))Order.Append(R->Units);
    TArray<FName> Keys;Db.Archetypes.GetKeys(Keys);Keys.Sort([](FName A,FName B){return A.LexicalLess(B);});
    for(FName K:Keys)Order.AddUnique(K);
    for(const FName ArchId:Order)
    {
        const auto* A=Db.Archetypes.Find(ArchId);if(!A)continue;
        for(const auto& Ab:A->Abilities)
        {
            if(Seen.Contains(Ab.Id)||!Wanted(Ab.Id.ToString())||(Set!=TEXT("monster")&&Set!=TEXT("all")&&Set!=A->RaceId.ToString()))continue;
            Seen.Add(Ab.Id);
            FEntry E;E.Id=Ab.Id.ToString();E.bMonster=true;E.Archetype=ArchId;E.Label=Ab.Name;
            E.Group=A->RaceId.IsNone()?FString(TEXT("npc")):A->RaceId.ToString();G.Entries.Add(E);
        }
    }
}
void Clear()
{
    CireTargeting::DebugSetAimOverride({});
    if(G.PC.IsValid())CireTargeting::Cancel(G.PC.Get());
    UWorld* World=G.Mode->GetWorld();
    ACireAreaEffect::ClearAll(World);ACireSkillshot::ClearAll(World);
    for(TActorIterator<ACireSummon> It(World);It;++It)It->Destroy();
    for(TActorIterator<ACireConstruct> It(World);It;++It)It->Destroy();
    for(auto& A:G.Actors)if(A.IsValid())
    {
        if(auto* M=Cast<ACireMonster>(A.Get()))G.Mode->Monsters.Remove(M);
        A->Destroy();
    }
    G.Actors.Reset();
    for(TActorIterator<ACireSpellVisual> It(World);It;++It)It->Destroy();
}
ACireMonster* SpawnMonster(FName Arch,FVector At,FRotator Facing,bool bFrozen)
{
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* M=G.Mode->GetWorld()->SpawnActor<ACireMonster>(ACireMonster::StaticClass(),At,Facing,P);
    if(!M)return nullptr;
    M->Lane=0;CireNPCCombat::ConfigureArchetype(M,Arch,5,0,1);M->SpawnPosition=At;
    M->MaxHealth=M->Health=100000;
    if(bFrozen){M->Damage=0;} // paused NPC: never moves, attacks or casts
    G.Mode->Monsters.Add(M);G.Actors.Add(M);
    return M;
}
ACireHero* SpawnHero(const FString& Profile,FVector At,FRotator Facing)
{
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* H=G.Mode->GetWorld()->SpawnActor<ACireHero>(At,Facing,P);if(!H)return nullptr;
    H->TeamId=0;H->bBot=false;if(!H->DraftProfile(Profile))H->Draft(0);
    H->Offers.Reset();H->CurrentOffer={};H->MaxHealth=H->Health=100000;H->MaxMana=H->Mana=5000;H->Energy=100;
    H->Intelligence=H->Strength=H->Agility=40;
    G.Actors.Add(H);return H;
}
void PlaceCamera(FVector From,FVector LookAt)
{
    G.Camera->SetActorLocation(From);G.Camera->SetActorRotation((LookAt-From).Rotation());
}
void Schedule(std::initializer_list<FFrame> Frames){G.Frames=Frames;G.FrameIndex=0;}
// Champion: caster at the stage origin facing +X, dummies downrange.
void BeginChampion(const FEntry& E)
{
    const FVector Origin=Stage+FVector(0,0,95);
    auto* H=SpawnHero(E.Profile,Origin,FRotator::ZeroRotator);if(!H){G.bChecks=false;return;}
    G.Hero=H;G.PC->Possess(H);G.PC->SetViewTarget(G.Camera.Get());
    auto* Dummy=SpawnMonster(TEXT("hollow_infantry"),Stage+FVector(720,0,95),FRotator(0,180,0),true);
    const bool bMulti=E.Id==TEXT("chain_spark")||E.Id==TEXT("cataclysm")||E.Id==TEXT("cleaving_strike")||E.Id==TEXT("war_cry")||
        E.Id==TEXT("challenge_of_iron")||E.Id==TEXT("seismic_reprisal")||E.Id==TEXT("piercing_shot")||E.Id==TEXT("starfall");
    if(bMulti)
    {
        const bool bNear=E.Id==TEXT("cleaving_strike")||E.Id==TEXT("seismic_reprisal");
        if(bNear&&Dummy)Dummy->SetActorLocation(Stage+FVector(230,0,95));
        SpawnMonster(TEXT("hollow_infantry"),Stage+(bNear?FVector(120,210,95):FVector(1000,170,95)),FRotator(0,180,0),true);
        SpawnMonster(TEXT("hollow_infantry"),Stage+(bNear?FVector(60,-240,95):FVector(900,-260,95)),FRotator(0,180,0),true);
    }
    H->Skills.Reset();H->Cooldowns.Reset();
    if(!E.Id.StartsWith(TEXT("basic_"))){H->Skills={E.Id};H->Cooldowns={0};}
    H->Target=Dummy;H->GlobalCooldown=0;
    PlaceCamera(Origin+FVector(-720,-300,560),Origin+FVector(560,0,-60));
    const auto D=CireTargeting::Describe(E.Id);
    G.bAimArmed=D.Kind==ECireTargetKind::Ground;
    const auto Shape=CireAbilityShapes::Describe(FName(*E.Id));
    const float Impact=FMath::Max(.3f,Shape.ImpactSeconds(720.f));
    const float Linger=FMath::Clamp(Shape.LingerSeconds,.5f,2.2f);
    Schedule({{TEXT("1_aim"),-.05f},{TEXT("2_cast"),.1f},{TEXT("3_travel"),FMath::Max(.22f,Impact*.55f)},{TEXT("4_impact"),Impact+.06f},
        {TEXT("5_linger"),Impact+Linger*.5f},{TEXT("6_end"),Impact+Linger+.25f}});
}
void CastChampion(const FEntry& E)
{
    auto* H=G.Hero.Get();if(!H)return;
    const FVector Target=H->Target?H->Target->GetActorLocation():H->GetActorLocation()+FVector(600,0,0);
    FVector Aim=FVector(Target.X,Target.Y,Stage.Z);
    if(E.Id==TEXT("summoned_wall")||E.Id==TEXT("protection_dome")||E.Id==TEXT("oathbound_guardian"))Aim=Stage+FVector(380,0,0);
    CireTargeting::DebugSetAimOverride({});CireTargeting::Cancel(G.PC.Get());
    if(E.Id.StartsWith(TEXT("basic_"))){H->BasicTimer=0;H->BasicAttack();G.bCastRefused=H->AttackSerial==0;return;}
    const float Before=H->Cooldowns.IsValidIndex(0)?H->Cooldowns[0]:0;
    if(ACireHero::IsPassive(E.Id)){H->BasicTimer=0;H->BasicAttack();return;}
    H->CastAt(0,Aim);
    G.bCastRefused=!(H->Cooldowns.IsValidIndex(0)&&H->Cooldowns[0]>Before);
    if(G.bCastRefused)UE_LOG(LogTemp,Warning,TEXT("CIRE_ABILITY_VFX_REFUSED %s notice=%s"),*E.Id,*H->Notice);
}
// Monster: the player's champion stands at the origin (as the victim); the monster faces it from downrange.
void BeginMonster(const FEntry& E)
{
    const auto* Arch=CireNPCArchetypes::Find(E.Archetype);const auto* A=Arch?Arch->FindAbility(FName(*E.Id)):nullptr;
    if(!A){G.bChecks=false;return;}
    const FVector Origin=Stage+FVector(0,0,95);
    auto* H=SpawnHero(TEXT("knight"),Origin,FRotator::ZeroRotator);if(!H){G.bChecks=false;return;}
    G.Hero=H;G.PC->Possess(H);G.PC->SetViewTarget(G.Camera.Get());
    float Range=A->Range>0?A->Range:600.f;
    if(A->bBasic&&A->Kind==ECireNPCAbilityKind::Melee)Range=Arch->AttackRange;
    const float Distance=FMath::Clamp(FMath::Min(Range*.7f,760.f),FMath::Max(A->MinRange+60.f,150.f),FMath::Max(Range-15.f,160.f));
    auto* M=SpawnMonster(E.Archetype,Stage+FVector(Distance,0,95),FRotator(0,180,0),false);
    if(!M){G.bChecks=false;return;}
    if(A->Kind==ECireNPCAbilityKind::Enrage||A->Kind==ECireNPCAbilityKind::ShieldWall)M->Health=M->MaxHealth*FMath::Clamp(A->HealthThreshold*.8f,.05f,.95f);
    if(A->Kind==ECireNPCAbilityKind::Guard||A->Kind==ECireNPCAbilityKind::HealAlly||A->Kind==ECireNPCAbilityKind::Rally)
        if(auto* Ally=SpawnMonster(E.Archetype,Stage+FVector(Distance+120,260,95),FRotator(0,180,0),true))Ally->Health=Ally->MaxHealth*.2f;
    const float Cast=FMath::Max(A->CastTime,.05f);
    const float Linger=A->Duration>0&&A->DamagePerSecond>0?FMath::Min(A->Duration,2.f):1.1f;
    PlaceCamera(Origin+FVector(-680,-320,560),Origin+FVector(Distance*.55f,0,-60));
    G.bAimArmed=false;
    Schedule({{TEXT("1_telegraph"),.12f},{TEXT("2_telegraph_late"),FMath::Max(.2f,Cast*.75f)},{TEXT("3_release"),Cast+.06f},{TEXT("4_impact"),Cast+.3f},
        {TEXT("5_linger"),Cast+.3f+Linger*.5f},{TEXT("6_end"),Cast+.3f+Linger+.2f}});
}
void CastMonster(const FEntry& E)
{
    ACireMonster* M=nullptr;for(auto& A:G.Actors)if(auto* Mon=Cast<ACireMonster>(A.Get());Mon&&Mon->Damage>0){M=Mon;break;}
    G.bCastRefused=!M||!CireNPCCombat::DebugStartAbility(M,FName(*E.Id),G.Hero.Get());
    if(G.bCastRefused)UE_LOG(LogTemp,Warning,TEXT("CIRE_ABILITY_VFX_REFUSED %s (monster %s)"),*E.Id,*E.Archetype.ToString());
}
void BeginEntry(int32 Index)
{
    Clear();G.Index=Index;G.EntryAt=Now();G.CastAt=-1;G.bCastRefused=false;G.Frames.Reset();
    const FEntry& E=G.Entries[Index];
    if(E.bMonster)BeginMonster(E);else BeginChampion(E);
    UE_LOG(LogTemp,Display,TEXT("CIRE_ABILITY_VFX_ENTRY %d/%d %s"),Index+1,G.Entries.Num(),*E.Id);
}
FString Escape(const FString& S){return S.Replace(TEXT("\\"),TEXT("\\\\")).Replace(TEXT("\""),TEXT("\\\""));}
void EndEntry()
{
    const FEntry& E=G.Entries[G.Index];
    const auto Shape=CireAbilityShapes::Describe(FName(*E.Id),E.bMonster?CireNPCArchetypes::Find(E.Archetype):nullptr);
    TArray<FString> Frames;for(const auto& F:G.Frames)Frames.Add(FString::Printf(TEXT("\"%s\""),*F.Name));
    G.ManifestLines.Add(FString::Printf(TEXT("{\"index\":%d,\"id\":\"%s\",\"label\":\"%s\",\"group\":\"%s\",\"monster\":%s,\"caster\":\"%s\",\"shape\":\"%s\",\"school\":\"%s\",\"refused\":%s,\"frames\":[%s]}"),
        G.Index,*Escape(E.Id),*Escape(E.Label),*Escape(E.Group),E.bMonster?TEXT("true"):TEXT("false"),*Escape(E.bMonster?E.Archetype.ToString():E.Profile),
        *CireAbilityShapes::ShapeName(Shape.Kind),*CireAbilityShapes::SchoolName(Shape.School),G.bCastRefused?TEXT("true"):TEXT("false"),*FString::Join(Frames,TEXT(","))));
    if(G.bCastRefused)++G.Refused;
}
bool Setup(ACireGameMode* Mode,ACireController* PC)
{
    UWorld* World=Mode->GetWorld();G.PC=PC;
    if(auto* H=Cast<ACireHero>(PC->GetPawn())){H->SetActorHiddenInGame(true);H->SetActorEnableCollision(false);H->SetActorTickEnabled(false);H->SetActorLocation(Stage+FVector(-3000,0,-2000));}
    PC->SetIgnoreMoveInput(true);PC->SetIgnoreLookInput(true);PC->bShowMouseCursor=false;
    if(PC->GetHUD())PC->GetHUD()->bShowHUD=false;
    auto* Floor=World->SpawnActor<AStaticMeshActor>(Stage+FVector(500,0,-20),FRotator::ZeroRotator);if(!Floor)return false;
    Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
    Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    if(!Floor->GetStaticMeshComponent()->GetStaticMesh())return false;
    Floor->SetActorScale3D(FVector(44,30,.4f));Floor->SetActorLocation(Stage+FVector(500,0,-20));
    Floor->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Floor->GetStaticMeshComponent()->SetCollisionObjectType(ECC_WorldStatic);Floor->GetStaticMeshComponent()->SetCollisionResponseToAllChannels(ECR_Block);
    // Lane-like worn earth so telegraph contrast matches the battlefield rather than a bright test slab.
    UMaterialInterface* Ground=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Environment/Materials/M_WornEarth.M_WornEarth"));
    if(!Ground)Ground=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Effects/CireSpell/M_Runestone.M_Runestone"));
    Floor->GetStaticMeshComponent()->SetMaterial(0,Ground);
    G.Floor=Floor;
    if(auto* Light=World->SpawnActor<ADirectionalLight>(Stage+FVector(400,0,2000),FRotator(-50,-130,0)))
    {Light->GetLightComponent()->SetIntensity(3.2f);Light->GetLightComponent()->SetLightColor(FLinearColor(.82f,.86f,1.f));}
    G.Camera=World->SpawnActor<ACameraActor>(Stage+FVector(-700,0,600),FRotator::ZeroRotator);if(!G.Camera.IsValid())return false;
    auto* Camera=G.Camera->GetCameraComponent();Camera->SetFieldOfView(80);Camera->SetAspectRatio(16.f/9.f);Camera->bConstrainAspectRatio=true;
    auto& Post=Camera->PostProcessSettings;
    Post.bOverride_AutoExposureMethod=true;Post.AutoExposureMethod=EAutoExposureMethod::AEM_Manual;
    Post.bOverride_AutoExposureApplyPhysicalCameraExposure=true;Post.AutoExposureApplyPhysicalCameraExposure=false;
    Post.bOverride_AutoExposureBias=true;Post.AutoExposureBias=.4f;
    Post.bOverride_BloomIntensity=true;Post.BloomIntensity=.55f;
    Post.bOverride_MotionBlurAmount=true;Post.MotionBlurAmount=0;
    PC->SetViewTarget(G.Camera.Get());
    return true;
}
void Capture(const FString& Frame)
{
    const FEntry& E=G.Entries[G.Index];
    const FString File=FPaths::Combine(G.Directory,FString::Printf(TEXT("%03d_%s_%s.png"),G.Index,*E.Id,*Frame));
    FScreenshotRequest::RequestScreenshot(File,false,false,false,FIntRect(),true);G.Files.Add(File);
}
}

bool CireAbilityVFXGallery::Initialize(ACireGameMode* Mode)
{
    G={};if(!FParse::Param(FCommandLine::Get(),TEXT("CireAbilityVFXGallery")))return false;
    G.Mode=Mode;G.Start=FPlatformTime::Seconds();
    FString Set=TEXT("all"),Only;FParse::Value(FCommandLine::Get(),TEXT("CireVFXSet="),Set);FParse::Value(FCommandLine::Get(),TEXT("CireVFXOnly="),Only,false);
    G.Tag=TEXT("capture");FParse::Value(FCommandLine::Get(),TEXT("CireVFXTag="),G.Tag);
    TArray<FString> OnlyList;Only.ParseIntoArray(OnlyList,TEXT(","));
    G.Directory=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("AbilityVFX"),
        G.Tag+TEXT("-")+Set+TEXT("-")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));
    if(!Mode||Mode->GetNetMode()!=NM_Standalone||!IFileManager::Get().MakeDirectory(*G.Directory,true)){Finish(false);return true;}
    Mode->bBotsFilled=true;Mode->BotFillTimer=MAX_flt;Mode->WaveTimer=MAX_flt;Mode->Clock=Cires::MatchClock();
    BuildEntries(Set,OnlyList);
    return true;
}

bool CireAbilityVFXGallery::Tick(ACireGameMode* Mode)
{
    if(G.Mode.Get()!=Mode)return false;
    if(G.bDone)return true;
    if(FPlatformTime::Seconds()-G.Start>60.0*40){Finish(false);return true;}
    if(!G.PC.IsValid())
    {
        auto* PC=Cast<ACireController>(Mode->GetWorld()->GetFirstPlayerController());
        if(PC&&PC->GetPawn()&&PC->GetHUD()&&!Setup(Mode,PC))Finish(false);
        if(G.PC.IsValid())G.EntryAt=Now();
        return true;
    }
    if(Mode->Clock.Phase()!=Cires::MatchPhase::Survival)Mode->Clock=Cires::MatchClock();
    if(G.Index<0)
    {
        if(Now()-G.EntryAt<2.5f)return true;
        if(G.Entries.IsEmpty()){Finish(false);return true;}
        BeginEntry(0);return true;
    }
    if(G.FinishAt>0)
    {
        // Screenshots are written asynchronously after their frame; give the last ones time to land.
        if(FPlatformTime::Seconds()-G.FinishAt<2.0)return true;
        for(const auto& File:G.Files)G.bChecks&=IFileManager::Get().FileSize(*File)>5000;
        Finish(G.bChecks);return true;
    }
    const FEntry& E=G.Entries[G.Index];
    const float Settle=.9f;
    const float Local=Now()-G.EntryAt;
    if(G.CastAt<0)
    {
        if(G.bAimArmed&&Local>Settle-.35f&&!CireTargeting::Snapshot(G.PC.Get()).bActive)
        {
            // Aim preview exactly as the player sees it: armed slot, cursor on the aim point.
            auto* H=G.Hero.Get();const FVector T=H&&H->Target?H->Target->GetActorLocation():Stage+FVector(700,0,0);
            FVector Aim(T.X,T.Y,Stage.Z);
            if(E.Id==TEXT("summoned_wall")||E.Id==TEXT("protection_dome")||E.Id==TEXT("oathbound_guardian"))Aim=Stage+FVector(380,0,0);
            CireTargeting::DebugSetAimOverride(Aim);CireTargeting::Request(G.PC.Get(),0);
        }
        if(G.FrameIndex==0&&G.Frames.Num()&&G.Frames[0].At<0&&Local>=Settle-.06f){Capture(G.Frames[0].Name);G.FrameIndex=1;}
        if(Local>=Settle)
        {
            G.CastAt=Now();
            if(E.bMonster)CastMonster(E);else CastChampion(E);
        }
        return true;
    }
    const float Since=Now()-G.CastAt;
    while(G.FrameIndex<G.Frames.Num()&&G.Frames[G.FrameIndex].At<0)++G.FrameIndex;
    if(G.FrameIndex<G.Frames.Num()&&Since>=G.Frames[G.FrameIndex].At){Capture(G.Frames[G.FrameIndex].Name);++G.FrameIndex;return true;}
    if(G.FrameIndex>=G.Frames.Num()&&Since>=(G.Frames.Num()?G.Frames.Last().At:0)+.15f)
    {
        EndEntry();
        if(G.Index+1<G.Entries.Num())BeginEntry(G.Index+1);
        else{Clear();G.FinishAt=FPlatformTime::Seconds();G.Index=G.Entries.Num();}
    }
    return true;
}
#endif
