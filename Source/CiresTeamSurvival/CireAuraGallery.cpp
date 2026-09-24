#include "CireAuraGallery.h"
#if !UE_BUILD_SHIPPING
#include "CireAuraVisuals.h"
#include "CireBuffs.h"
#include "CireChampionArt.h"
#include "CireGame.h"
#include "CireItems.h"
#include "CireNPCArchetypes.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
struct FUnit
{
    bool bMonster=false;
    FName Body;                 // champion profile id or NPC archetype id
    TArray<FName> Buffs;        // records applied with CireBuffs::Apply
    TArray<FName> Items;        // replicated inventory timed buffs (item actives, consumables)
    uint8 Flags=0;              // NPC status flags
    int32 Poison=0;
    int32 Link=INDEX_NONE;      // unit index used as the record source (tethers)
    int32 AttackTarget=INDEX_NONE;
    FString Label;
    float Yaw=0;
};
struct FPage
{
    FString Title,File;
    TArray<FUnit> Units;
    float Spacing=330,Distance=1500,Pitch=-24;
    bool bGameplay=false,bCloseUp=false;
};
struct FState
{
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACireController> PC;
    TWeakObjectPtr<ACameraActor> Camera;
    TArray<TWeakObjectPtr<AActor>> PageActors;
    TArray<TWeakObjectPtr<ACireHero>> Heroes;
    TArray<FPage> Pages;
    TArray<FString> Files;
    FString Directory;
    double Start=0,Ready=-1,PageAt=-1;
    int32 Page=-1;
    bool bDone=false,bCaptured=false,bChecks=true,bStriked=false;
};
FState G;
const FVector Stage(0,-60000,12000);
void Finish(bool bPass)
{
    if(G.bDone)return;G.bDone=true;
    UE_LOG(LogTemp,Display,TEXT("CIRE_AURA_GALLERY_%s captures=%d directory=%s"),bPass?TEXT("PASS"):TEXT("FAIL"),G.Files.Num(),*G.Directory);
    FPlatformMisc::RequestExitWithStatus(false,bPass?0:1);
}
FUnit Hero(const TCHAR* Body,std::initializer_list<FName> Buffs,const TCHAR* Label){FUnit U;U.Body=Body;U.Buffs=Buffs;U.Label=Label;return U;}
FUnit Monster(const TCHAR* Body,uint8 Flags,const TCHAR* Label){FUnit U;U.bMonster=true;U.Body=Body;U.Flags=Flags;U.Label=Label;return U;}
void BuildPages()
{
    using namespace CireNPCStatus;
    auto Add=[&](const TCHAR* Title,const TCHAR* File,TArray<FUnit> Units){FPage P;P.Title=Title;P.File=File;P.Units=MoveTemp(Units);G.Pages.Add(P);return G.Pages.Num()-1;};
    Add(TEXT("CHAMPION BUFFS I"),TEXT("champion_buffs_1"),{Hero(TEXT("knight"),{TEXT("iron_guard")},TEXT("IRON GUARD")),Hero(TEXT("paladin_righteous"),{TEXT("war_cry")},TEXT("WAR CRY")),
        Hero(TEXT("drakish_footman"),{TEXT("challenge_of_iron")},TEXT("CHALLENGE OF IRON")),Hero(TEXT("scholar"),{TEXT("sanctuary")},TEXT("SANCTUARY"))});
    Add(TEXT("CHAMPION BUFFS II"),TEXT("champion_buffs_2"),{Hero(TEXT("paladin_holy"),{TEXT("bastion_of_dawn")},TEXT("BASTION OF DAWN")),Hero(TEXT("keeper_of_light"),{TEXT("mass_aegis")},TEXT("MASS AEGIS")),
        Hero(TEXT("dryad"),{TEXT("wellspring")},TEXT("WELLSPRING")),Hero(TEXT("ranger"),{},TEXT("GUARDED (GENERIC)"))});
    G.Pages.Last().Units[3].Poison=-1; // marker: generic guard
    {
        TArray<FUnit> U={Hero(TEXT("lancer"),{TEXT("frost_bind")},TEXT("FROST BIND (SLOW)")),Hero(TEXT("summoner"),{TEXT("shield_slam")},TEXT("SHIELD SLAM DAZE")),
            Hero(TEXT("wizard"),{},TEXT("SLOWED (GENERIC)")),Hero(TEXT("orc_chieftain"),{},TEXT("POISONED x3"))};
        U[2].Poison=-2;U[3].Poison=3;Add(TEXT("DEBUFFS"),TEXT("debuffs"),U);
    }
    {
        TArray<FUnit> U={Hero(TEXT("knight"),{TEXT("npc_tank_provoke_debuff")},TEXT("PROVOKED -> SHIELDBEARER")),Monster(TEXT("hollow_shieldbearer"),Provoking,TEXT("CHALLENGING ROAR")),
            Hero(TEXT("dwarf_miner"),{},TEXT("TAUNTING (GENERIC)")),Hero(TEXT("troll_berserker_melee"),{TEXT("stunned")},TEXT("STUNNED (DATA-READY)"))};
        U[0].Link=1;U[2].Poison=-3;Add(TEXT("CONTROL"),TEXT("control"),U);
    }
    Add(TEXT("ITEM-READY BUFFS + PASSIVES"),TEXT("items_passives"),{Hero(TEXT("troll_berserker_melee"),{TEXT("blood_rage")},TEXT("BLOOD RAGE")),Hero(TEXT("knight"),{TEXT("frost_weapon")},TEXT("FROST WEAPON")),
        Hero(TEXT("paladin_holy"),{TEXT("blessing")},TEXT("BLESSING")),Hero(TEXT("dryad"),{TEXT("regeneration")},TEXT("REGENERATION (HOT)")),Hero(TEXT("ranger"),{TEXT("battle_rhythm"),TEXT("soul_conduit")},TEXT("BATTLE RHYTHM + SOUL CONDUIT"))});
    G.Pages.Last().Spacing=300;
    Add(TEXT("MONSTER STANCES I"),TEXT("monster_stances_1"),{Monster(TEXT("gravemaw_pack_leader"),Enraged,TEXT("BLOOD FRENZY")),Monster(TEXT("hollow_siegebreaker"),Enraged,TEXT("SIEGE FURY")),
        Monster(TEXT("hollow_infantry"),Rallied,TEXT("RALLIED")),Monster(TEXT("ironbound_bruiser"),Enraged,TEXT("ENRAGED (GENERIC)"))});
    G.Pages.Last().Spacing=420;G.Pages.Last().Distance=1750;
    {
        TArray<FUnit> U={Monster(TEXT("hollow_shieldbearer"),ShieldWall,TEXT("SHIELD WALL")),Monster(TEXT("blight_caster"),Guarded,TEXT("GUARDIAN'S OATH")),
            Monster(TEXT("barbed_hunter"),0,TEXT("TAUNTED -> KNIGHT")),Hero(TEXT("knight"),{},TEXT("TAUNTING TANK"))};
        U[2].Buffs={TEXT("taunted")};U[2].Link=3;U[0].Yaw=35;Add(TEXT("MONSTER STANCES II"),TEXT("monster_stances_2"),U);
    }
    {
        TArray<FUnit> U={Hero(TEXT("troll_berserker_melee"),{TEXT("blood_rage")},TEXT("BLOOD RAGE: BLOODY SWIPE + SPLASH")),Monster(TEXT("hollow_infantry"),0,TEXT("")),
            Hero(TEXT("knight"),{TEXT("frost_weapon")},TEXT("FROST WEAPON: FROST TRAIL + SHATTER")),Monster(TEXT("hollow_infantry"),0,TEXT("")),
            Hero(TEXT("paladin_holy"),{TEXT("bastion_of_dawn")},TEXT("BASTION: HOLY GLINTS")),Monster(TEXT("hollow_infantry"),0,TEXT("")),
            Hero(TEXT("ranger"),{TEXT("blessing")},TEXT("BLESSING: RANGED GLINT TRAIL")),Monster(TEXT("hollow_infantry"),0,TEXT(""))};
        for(int32 I=0;I<8;I+=2){U[I].AttackTarget=I+1;U[I].Yaw=90;U[I+1].Yaw=-90;}
        U[4].Poison=-1;
        const int32 Index=Add(TEXT("EMPOWERED ATTACKS: CHAMPIONS"),TEXT("attacks_champions"),U);G.Pages[Index].Spacing=235;G.Pages[Index].Distance=1850;
    }
    {
        TArray<FUnit> U={Monster(TEXT("gravemaw_pack_leader"),Enraged,TEXT("BLOOD FRENZY CLEAVER")),Hero(TEXT("knight"),{},TEXT("")),
            Monster(TEXT("hollow_siegebreaker"),Enraged,TEXT("SIEGE FURY HAMMER")),Hero(TEXT("knight"),{},TEXT("")),
            Monster(TEXT("hollow_infantry"),Rallied,TEXT("RALLIED BLADE")),Hero(TEXT("knight"),{},TEXT(""))};
        for(int32 I=0;I<6;I+=2){U[I].AttackTarget=I+1;U[I].Yaw=90;U[I+1].Yaw=-90;}
        const int32 Index=Add(TEXT("EMPOWERED ATTACKS: MONSTERS"),TEXT("attacks_monsters"),U);G.Pages[Index].Spacing=270;G.Pages[Index].Distance=1750;
    }
    {
        TArray<FUnit> U={Hero(TEXT("troll_berserker_melee"),{TEXT("blood_rage")},TEXT("BLOOD RAGE")),Monster(TEXT("hollow_infantry"),0,TEXT(""))};
        U[0].AttackTarget=1;U[0].Yaw=90;U[1].Yaw=-90;
        const int32 Index=Add(TEXT("CLOSE-UP: BLOOD RAGE ATTACK"),TEXT("closeup_blood_rage"),U);G.Pages[Index].Spacing=230;G.Pages[Index].Distance=620;G.Pages[Index].Pitch=-14;G.Pages[Index].bCloseUp=true;
    }
    {
        TArray<FUnit> U={Hero(TEXT("ranger"),{},TEXT("BORROWED TIME (HOURGLASS)")),Hero(TEXT("knight"),{TEXT("oathshield")},TEXT("OATHSHIELD (AEGIS OF THE LAST OATH)")),
            Hero(TEXT("dwarf_miner"),{TEXT("toll_of_the_grave")},TEXT("TOLL OF THE GRAVE (GRAVEBELL)")),Hero(TEXT("summoner"),{},TEXT("SCATTER (RAVENFEATHER)")),Hero(TEXT("scholar"),{},TEXT("AETHER PHIAL (MANA)"))};
        U[0].Items={TEXT("hourglass_of_ages")};U[3].Items={TEXT("ravenfeather_mantle")};U[4].Items={TEXT("aether_phial")};
        const int32 Index=Add(TEXT("ITEM ACTIVES + CONSUMABLES"),TEXT("item_actives"),U);G.Pages[Index].Spacing=320;G.Pages[Index].Distance=1650;
    }
    {
        // Gameplay camera: 650 cm boom behind the player, looking into a lane skirmish.
        TArray<FUnit> U={Hero(TEXT("knight"),{TEXT("war_cry")},TEXT("")),Monster(TEXT("gravemaw_pack_leader"),Enraged,TEXT("")),Monster(TEXT("hollow_shieldbearer"),ShieldWall,TEXT("")),
            Monster(TEXT("hollow_infantry"),Rallied,TEXT("")),Hero(TEXT("paladin_holy"),{TEXT("bastion_of_dawn")},TEXT("")),Hero(TEXT("ranger"),{TEXT("frost_weapon")},TEXT("")),Monster(TEXT("blight_caster"),0,TEXT(""))};
        U[4].Poison=-1;U[6].Buffs={TEXT("frost_bind")};
        const int32 Index=Add(TEXT("GAMEPLAY CAMERA DISTANCE"),TEXT("gameplay_view"),U);G.Pages[Index].bGameplay=true;
    }
}
void Label(UWorld* World,const FString& Text,FVector At,float Size,FColor Color=FColor(235,222,196))
{
    if(Text.IsEmpty())return;
    auto* Actor=World->SpawnActor<ATextRenderActor>(At,FRotator::ZeroRotator);if(!Actor){G.bChecks=false;return;}
    Actor->SetActorRotation((G.Camera->GetActorLocation()-At).GetSafeNormal2D().Rotation());
    auto* T=Actor->GetTextRender();T->SetText(FText::FromString(Text));T->SetWorldSize(Size);T->SetTextRenderColor(Color);T->SetHorizontalAlignment(EHTA_Center);
    G.PageActors.Add(Actor);
}
bool Setup(ACireGameMode* Mode,ACireController* PC)
{
    UWorld* World=Mode->GetWorld();G.PC=PC;
    auto* Player=Cast<ACireHero>(PC->GetPawn());if(!Player)return false;
    Player->TeamId=0;Player->bDrafted=true;Player->SetActorHiddenInGame(true);Player->SetActorEnableCollision(false);Player->SetActorTickEnabled(false);
    Player->GetCharacterMovement()->DisableMovement();Player->SetActorLocation(Stage+FVector(-4000,0,200));
    PC->SetIgnoreMoveInput(true);PC->SetIgnoreLookInput(true);PC->bShowMouseCursor=false;if(PC->GetHUD())PC->GetHUD()->bShowHUD=false;
    for(auto* M:Mode->Monsters)if(IsValid(M))M->Destroy();Mode->Monsters.Reset();
    auto* Floor=World->SpawnActor<AStaticMeshActor>(Stage-FVector(0,0,50),FRotator::ZeroRotator);if(!Floor)return false;
    Floor->SetMobility(EComponentMobility::Movable);Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Floor->GetStaticMeshComponent()->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/M_Slate.M_Slate")));
    Floor->SetActorScale3D(FVector(60,60,1));Floor->SetActorEnableCollision(false);
    auto* Moon=World->SpawnActor<ADirectionalLight>(Stage+FVector(0,0,3000),FRotator(-42,150,0));if(!Moon)return false;
    Moon->GetLightComponent()->SetMobility(EComponentMobility::Movable);Moon->GetLightComponent()->SetIntensity(2.4f);Moon->GetLightComponent()->SetLightColor(FLinearColor(.78f,.84f,1.f));
    G.Camera=World->SpawnActor<ACameraActor>();if(!G.Camera.IsValid())return false;
    auto* Camera=G.Camera->GetCameraComponent();Camera->SetFieldOfView(50);Camera->SetAspectRatio(16.f/9.f);Camera->bConstrainAspectRatio=true;
    auto& Post=Camera->PostProcessSettings;
    Post.bOverride_AutoExposureMethod=true;Post.AutoExposureMethod=EAutoExposureMethod::AEM_Manual;
    Post.bOverride_AutoExposureApplyPhysicalCameraExposure=true;Post.AutoExposureApplyPhysicalCameraExposure=false;
    Post.bOverride_AutoExposureBias=true;Post.AutoExposureBias=-.2f;
    Post.bOverride_BloomIntensity=true;Post.BloomIntensity=.55f;Post.bOverride_MotionBlurAmount=true;Post.MotionBlurAmount=0;
    PC->SetViewTarget(G.Camera.Get());
    if(auto* Auras=CireAuraVisuals::Get(World))Auras->ObserverOverride=Player;
    BuildPages();G.Ready=FPlatformTime::Seconds();return true;
}
void ClearPage()
{
    for(auto& A:G.PageActors)if(A.IsValid())A->Destroy();
    G.PageActors.Reset();G.Heroes.Reset();
    if(G.Mode.IsValid())G.Mode->Monsters.Reset();
    for(TActorIterator<ACireAuraStrike> It(G.Mode->GetWorld());It;++It)It->Destroy();
}
FVector UnitPosition(const FPage& P,int32 I)
{
    if(P.bGameplay)
    {
        const FVector Spots[]={FVector(0,0,0),FVector(-620,40,0),FVector(-820,-330,0),FVector(-1050,300,0),FVector(-260,-430,0),FVector(-300,450,0),FVector(-1500,-80,0)};
        return Stage+Spots[FMath::Clamp(I,0,6)];
    }
    return Stage+FVector(0,(I-(P.Units.Num()-1)*.5f)*P.Spacing,0);
}
void MakePage(int32 Index)
{
    ClearPage();UWorld* World=G.Mode->GetWorld();G.Page=Index;G.PageAt=FPlatformTime::Seconds();G.bCaptured=false;G.bStriked=false;
    const FPage& P=G.Pages[Index];
    auto* Auras=CireAuraVisuals::Get(World);
    // Camera first so labels can face it.
    const FVector Center=Stage+FVector(0,0,100);
    FVector View;FRotator Look;
    if(P.bGameplay){View=Stage+FVector(620,0,330);Look=FRotator(-15,180,0);}
    else{const float Pa=FMath::DegreesToRadians(-P.Pitch);View=Center+FVector(FMath::Cos(Pa),0,FMath::Sin(Pa))*P.Distance;Look=(Center-View).Rotation();}
    G.Camera->SetActorLocation(View);G.Camera->SetActorRotation(Look);
    if(Auras)Auras->ObserverOverride=Cast<AActor>(G.PC.IsValid()?G.PC->GetPawn():nullptr);
    TArray<AActor*> Spawned;Spawned.SetNum(P.Units.Num());
    FActorSpawnParameters SP;SP.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    for(int32 I=0;I<P.Units.Num();++I)
    {
        const FUnit& U=P.Units[I];const FVector At=UnitPosition(P,I);
        const float Yaw=P.bGameplay?(I==0?180.f:0.f):U.Yaw;
        if(U.bMonster)
        {
            auto* M=World->SpawnActor<ACireMonster>(At+FVector(0,0,95),FRotator(0,Yaw,0),SP);if(!M){G.bChecks=false;continue;}
            M->Lane=0;G.Mode->Monsters.Add(M);
            G.bChecks&=CireNPCCombat::ConfigureArchetype(M,U.Body,5);
            M->SetActorTickEnabled(false);M->GetCharacterMovement()->DisableMovement();M->SetActorEnableCollision(false);
            if(const auto* A=CireNPCArchetypes::Find(U.Body))M->SetActorScale3D(FVector(A->Scale*((U.Flags&CireNPCStatus::Enraged)?1.08f:1.f)));
            M->SetActorLocation(At+FVector(0,0,M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2));
            if(M->NPCState)M->NPCState->StatusFlags=U.Flags;
            M->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            Spawned[I]=M;G.PageActors.Add(M);
        }
        else
        {
            auto* H=World->SpawnActor<ACireHero>(At+FVector(0,0,95),FRotator(0,Yaw,0),SP);if(!H){G.bChecks=false;continue;}
            H->TeamId=0;G.bChecks&=H->DraftProfile(U.Body.ToString());
            H->bBot=false;H->bAutoAttack=false;H->Target=nullptr;H->SetActorEnableCollision(false);H->SetActorTickEnabled(false);
            H->GetCharacterMovement()->StopMovementImmediately();H->GetCharacterMovement()->SetComponentTickEnabled(false);
            if(H->ChampionArt)H->ChampionArt->UpdateVisuals(*H,.1f);
            H->SetActorLocation(At+FVector(0,0,H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2));H->SetActorRotation(FRotator(0,Yaw,0));
            H->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            H->GetMesh()->SetVisibility(true,true);H->SetActorHiddenInGame(false);
            Spawned[I]=H;G.PageActors.Add(H);G.Heroes.Add(H);
        }
    }
    if(P.bGameplay&&Auras&&Spawned.Num()>0&&Spawned[0])Auras->ObserverOverride=Spawned[0]; // the knight is "you"
    const float Now=World->GetTimeSeconds();
    for(int32 I=0;I<P.Units.Num();++I)
    {
        AActor* Unit=Spawned[I];if(!Unit)continue;const FUnit& U=P.Units[I];
        AActor* Link=U.Link!=INDEX_NONE&&Spawned.IsValidIndex(U.Link)?Spawned[U.Link]:Unit;
        if(auto* H=Cast<ACireHero>(Unit))
        {
            H->ShieldUntil=H->TauntUntil=H->SlowUntil=0;H->PoisonAreaCount=0;
            for(const FName Id:U.Buffs)
            {
                if(Id==TEXT("oathshield")||Id==TEXT("iron_guard")||Id==TEXT("sanctuary")||Id==TEXT("bastion_of_dawn")||Id==TEXT("mass_aegis")||Id==TEXT("wellspring")||Id==TEXT("challenge_of_iron")||Id==TEXT("war_cry"))H->ShieldUntil=Now+600;
                if(Id==TEXT("war_cry")||Id==TEXT("challenge_of_iron")||Id==TEXT("toll_of_the_grave"))H->TauntUntil=Now+600;
                if(Id==TEXT("frost_bind")||Id==TEXT("shield_slam"))H->SlowUntil=Now+600;
                G.bChecks&=CireBuffs::Apply(H,Id,600,Link);
            }
            if(U.Poison==-1)H->ShieldUntil=FMath::Max(H->ShieldUntil,Now+600);
            if(U.Poison==-2)H->SlowUntil=Now+600;
            if(U.Poison==-3)H->TauntUntil=Now+600;
            if(U.Poison>0){H->PoisonAreaCount=U.Poison;H->PoisonEndsAt=Now+600;}
            if(H->Inventory)for(const FName Item:U.Items){FCireTimedBuff Timed;Timed.Id=Item;Timed.Duration=600;Timed.EndsAt=Now+600;H->Inventory->Buffs.Add(Timed);}
        }
        else if(auto* M=Cast<ACireMonster>(Unit))
        {
            for(const FName Id:U.Buffs){if(Id==TEXT("frost_bind"))M->SlowUntil=Now+600;G.bChecks&=CireBuffs::Apply(M,Id,600,Link);}
        }
        if(U.AttackTarget!=INDEX_NONE&&Spawned.IsValidIndex(U.AttackTarget)&&Spawned[U.AttackTarget])
        {
            const FVector To=Spawned[U.AttackTarget]->GetActorLocation();
            Unit->SetActorRotation((To-Unit->GetActorLocation()).GetSafeNormal2D().Rotation());
            if(auto* H=Cast<ACireHero>(Unit)){H->AttackAimLocation=To;H->AttackDuration=.65f;H->AttackSerial=1;H->AttackStartedServerTime=Now-.3f;}
        }
        const auto* Character=Cast<ACharacter>(Unit);const float Top=Character?Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()*2:180;
        if(!P.bGameplay)Label(World,U.Label,FVector(Unit->GetActorLocation().X,Unit->GetActorLocation().Y,Stage.Z)+FVector(150,0,4),P.bCloseUp?9:13.f);
        (void)Top;
    }
    if(Auras){Auras->SetPreviewClock(-1);Auras->UpdateNow();for(AActor* Unit:Spawned)if(Unit)if(auto* A=Unit->FindComponentByClass<UCireAuraComponent>())A->AgeForPreview(3.f);}
    Label(World,FString::Printf(TEXT("CIRE'S TEAM SURVIVAL  |  AURA & BUFF SIGNATURES  |  %d/%d  %s"),Index+1,G.Pages.Num(),*P.Title),
        P.bGameplay?View+Look.Vector()*420+FRotationMatrix(Look).GetUnitAxis(EAxis::Z)*150:Center+FVector(0,0,P.bCloseUp?260:420),P.bGameplay?11:(P.bCloseUp?11:24),FColor(245,220,170));
    UE_LOG(LogTemp,Display,TEXT("CIRE_AURA_GALLERY_PAGE index=%d units=%d"),Index,P.Units.Num());
}
void SpawnStrikes()
{
    const FPage& P=G.Pages[G.Page];UWorld* World=G.Mode->GetWorld();auto* Auras=CireAuraVisuals::Get(World);if(!Auras)return;
    TArray<AActor*> Units;for(auto& A:G.PageActors)if(A.IsValid()&&Cast<ACharacter>(A.Get()))Units.Add(A.Get());
    const float ServerNow=CireBuffs::ServerNow(World);
    for(int32 I=0;I<P.Units.Num()&&I<Units.Num();++I)
    {
        const FUnit& U=P.Units[I];if(U.AttackTarget==INDEX_NONE||!Units.IsValidIndex(U.AttackTarget))continue;
        auto* Aura=Units[I]->FindComponentByClass<UCireAuraComponent>();const FCireAuraDef* Mod=Aura?Aura->AttackModifier(ServerNow):nullptr;
        G.bChecks&=Mod!=nullptr;if(!Mod)continue;
        const auto* Attacker=Cast<ACharacter>(Units[I]);const auto* Victim=Cast<ACharacter>(Units[U.AttackTarget]);
        const float Scale=FMath::Clamp(Attacker->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()/92.f,.6f,2.6f);
        const FVector From=Attacker->GetActorLocation()+FVector(0,0,15*Scale),To=Victim->GetActorLocation();
        const auto* Shooter=Cast<ACireHero>(Attacker);
        if(Shooter&&Shooter->IsRangedBasicAttack())
        {
            // Ranged: muzzle burst at the hand and a frozen trail along the projectile path.
            FVector Hand=From+(To-From).GetSafeNormal2D()*35;
            if(Shooter->GetMesh()->GetBoneIndex(TEXT("hand_r"))!=INDEX_NONE)Hand=Shooter->GetMesh()->GetSocketLocation(TEXT("hand_r"));
            if(auto* Muzzle=Auras->SpawnStrike(ACireAuraStrike::EMode::Muzzle,Mod->Attack,Hand,To,Scale*.8f)){Muzzle->SetPreviewAge(.07f);}else G.bChecks=false;
            TArray<FVector> Path;for(int32 K=0;K<10;++K){const float T=.15f+.6f*K/9.f;Path.Add(FMath::Lerp(Hand,To+FVector(0,0,20),T)+FVector(0,0,FMath::Sin(T*PI)*18));}
            if(auto* Trail=Auras->SpawnStrike(ACireAuraStrike::EMode::Trail,Mod->Attack,Path[0],Path.Last(),Scale*.8f)){Trail->SetPreviewTrail(Path);}else G.bChecks=false;
        }
        else if(auto* Swipe=Auras->SpawnStrike(ACireAuraStrike::EMode::Swipe,Mod->Attack,From,To,Scale,1)){Swipe->SetPreviewAge(.16f);}else G.bChecks=false;
        const FVector Impact=To+FVector(0,0,Victim->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()*.35f);
        if(auto* Hit=Auras->SpawnStrike(ACireAuraStrike::EMode::Hit,Mod->Attack,Impact,Impact,Scale*.9f)){Hit->SetPreviewAge(.12f);}else G.bChecks=false;
    }
}
void Capture()
{
    int32 W=0,H=0;G.PC->GetViewportSize(W,H);G.bChecks&=W>=1280&&H>=720;
    auto* Auras=CireAuraVisuals::Get(G.Mode->GetWorld());
    int32 Rendered=0;for(auto& A:G.PageActors)if(A.IsValid())if(auto* C=A->FindComponentByClass<UCireAuraComponent>())Rendered+=C->CountVertices()>0;
    UE_LOG(LogTemp,Display,TEXT("CIRE_AURA_GALLERY_CAPTURE page=%d rendered_units=%d lit=%d strikes=%d"),G.Page,Rendered,Auras?Auras->LitUnits:-1,Auras?Auras->LiveStrikes:-1);
    G.bChecks&=Rendered>0;
    const FString File=FPaths::Combine(G.Directory,FString::Printf(TEXT("%02d_%s.png"),G.Page+1,*G.Pages[G.Page].File));
    FScreenshotRequest::RequestScreenshot(File,false,false,false,FIntRect(),true);G.Files.Add(File);G.bCaptured=true;
}
}

bool CireAuraGallery::Initialize(ACireGameMode* Mode)
{
    G={};if(!FParse::Param(FCommandLine::Get(),TEXT("CireAuraGallery")))return false;
    G.Mode=Mode;G.Start=FPlatformTime::Seconds();
    G.Directory=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("AuraGallery"),FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));
    if(!Mode||Mode->GetNetMode()!=NM_Standalone||!IFileManager::Get().MakeDirectory(*G.Directory,true)){Finish(false);return true;}
    Mode->bBotsFilled=true;Mode->BotFillTimer=MAX_flt;Mode->WaveTimer=MAX_flt;return true;
}
bool CireAuraGallery::Tick(ACireGameMode* Mode)
{
    if(G.Mode.Get()!=Mode)return false;
    if(G.bDone)return true;
    if(FPlatformTime::Seconds()-G.Start>150){Finish(false);return true;}
    if(G.Ready<0)
    {
        auto* PC=Cast<ACireController>(Mode->GetWorld()->GetFirstPlayerController());
        if(PC&&PC->GetPawn()&&PC->GetHUD()&&!Setup(Mode,PC))Finish(false);
        return true;
    }
    if(G.Page<0){if(FPlatformTime::Seconds()-G.Ready>3)MakePage(0);return true;}
    for(auto& H:G.Heroes)if(H.IsValid()&&H->ChampionArt)
    {
        if(H->AttackSerial>0)H->AttackStartedServerTime=CireBuffs::ServerNow(Mode->GetWorld())-.3f; // hold the swing pose
        H->ChampionArt->UpdateVisuals(*H.Get(),Mode->GetWorld()->GetDeltaSeconds());
    }
    const double Age=FPlatformTime::Seconds()-G.PageAt;const double Settle=G.Page==0?4.5:2.6;
    if(Age>Settle-.35&&!G.bStriked){G.bStriked=true;SpawnStrikes();}
    if(Age>Settle&&!G.bCaptured)Capture();
    if(Age>Settle+.8)
    {
        if(G.Page+1<G.Pages.Num())MakePage(G.Page+1);
        else
        {
            for(const auto& File:G.Files)G.bChecks&=IFileManager::Get().FileSize(*File)>10000;
            Finish(G.bChecks&&G.Files.Num()==G.Pages.Num());
        }
    }
    return true;
}
#endif
