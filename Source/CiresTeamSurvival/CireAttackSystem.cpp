#include "CireAttackSystem.h"
#include "CireGame.h"
#include "CireRealm.h"
#include "CireSummon.h"
#include "CireSignatureSkills.h" // new-champions
#include "Engine/World.h"
#include "Rules/CireAttackRules.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "CireSpellPresentation.h" // ability-vfx
#include "CireAbilityVFX.h" // ability-vfx

namespace CireAttacks {
float FeetZ(const AActor* Actor) {
    if(!Actor)return 0;
    const auto* Character=Cast<ACharacter>(Actor);
    return Actor->GetActorLocation().Z-(Character?Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight():0.f);
}
float MissChance(const AActor* Source,const AActor* Target,bool bRanged) {
    return static_cast<float>(Cires::MissChance(bRanged,FeetZ(Source),FeetZ(Target)));
}
ECireHitOutcome Roll(const AActor* Source,const AActor* Target,bool bRanged) {
    // FRand can include its upper endpoint on some platforms. Clamp to [0,1).
    const auto R=Cires::ResolveAttack(MissChance(Source,Target,bRanged),FMath::Min(FMath::FRand(),.999999f),FMath::Min(FMath::FRand(),.999999f));
    return R==Cires::AttackResult::Miss?ECireHitOutcome::Miss:R==Cires::AttackResult::Dodge?ECireHitOutcome::Dodge:ECireHitOutcome::Hit;
}
void Resolve(AActor* Source,AActor* Target,float Damage,ECireHitOutcome Result,const FString& Name) {
    if(!IsValid(Source)||!Source->HasAuthority()||!IsValid(Target))return;
    if(Result==ECireHitOutcome::Hit)CireCombat::ApplyStrike(Source,Target,Damage,Name);
    else CireCombat::BroadcastAvoidance(Source,Target,Result,Name);
}
void Release(ACireHero* Source,AActor* Target,float Damage) {
    if(!IsValid(Source)||!Source->HasAuthority()||!Source->IsHostile(Target))return;
    bool bRanged=Source->IsRangedBasicAttack();
    const bool bSummon=Source->IsA<ACireSummon>();
    const float ReleaseRange=Source->BasicAttackRange()*(bRanged||bSummon?1.2f:240.f/220.f);
    FString StrikeName=Source->BasicAttackStyle()+TEXT(" strike");
    Damage=CireSignatureSkills::ModifyBasicAttack(Source,Target,Damage,bRanged,StrikeName); // new-champions: gunblade falchion/pistol switch, Hunter's Stride
    if(!Source->InRange(Target,ReleaseRange))return;
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CireAttackRelease),false,Source);
    if(Source->GetWorld()->LineTraceSingleByChannel(Hit,Source->GetActorLocation()+FVector(0,0,40),
       Target->GetActorLocation()+FVector(0,0,35),ECC_Visibility,Query)&&Hit.GetActor()!=Target)return;
    const ECireHitOutcome Result=Roll(Source,Target,bRanged);
    const FString Style=Source->BasicAttackStyle();
    CireCombat::PlayCue(Source,Target,FName(*Style),Source->GetActorLocation(),Target->GetActorLocation(),ECireSpellCue::Launch);
    if(bRanged)ACireTargetProjectile::Launch(Source,Target,Damage,Result);
    else Resolve(Source,Target,Damage,Result,StrikeName);
}
}

ACireTargetProjectile::ACireTargetProjectile() {
    bReplicates=true;SetReplicateMovement(true);SetNetUpdateFrequency(30);SetMinNetUpdateFrequency(15);
    PrimaryActorTick.bCanEverTick=true;
    Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Projectile"));SetRootComponent(Body);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);Body->SetGenerateOverlapEvents(false);
    Body->SetCanEverAffectNavigation(false);Body->SetCastShadow(false);
}
void ACireTargetProjectile::BeginPlay(){Super::BeginPlay();}
void ACireTargetProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps)const{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACireTargetProjectile,Attacker);DOREPLIFETIME(ACireTargetProjectile,Victim);
    DOREPLIFETIME(ACireTargetProjectile,Style);DOREPLIFETIME(ACireTargetProjectile,TeamId);
    DOREPLIFETIME(ACireTargetProjectile,LaunchedPhase);
}
bool ACireTargetProjectile::IsNetRelevantFor(const AActor* Viewer,const AActor* ViewTarget,const FVector&)const{
    const auto* C=Cast<AController>(Viewer);const auto* H=Cast<ACireHero>(C?C->GetPawn():Viewer?Viewer:ViewTarget);
    const auto* S=GetWorld()->GetGameState<ACireGameState>();
    return H&&S&&S->Phase==LaunchedPhase&&(S->Phase==2||H->TeamId==TeamId);
}
ACireTargetProjectile* ACireTargetProjectile::Launch(ACireHero* Source,AActor* Target,float Damage,ECireHitOutcome Outcome){
    if(!IsValid(Source)||!Source->HasAuthority()||!Source->IsHostile(Target)||!FMath::IsFinite(Damage)||Damage<=0)return nullptr;
    const auto* State=Source->GetWorld()->GetGameState<ACireGameState>();if(!State)return nullptr;
    FActorSpawnParameters P;P.Owner=Source;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FVector Start=Source->GetActorLocation()+FVector(0,0,45)+Source->GetActorForwardVector()*45;
    auto* Shot=Source->GetWorld()->SpawnActor<ACireTargetProjectile>(Start,(Target->GetActorLocation()-Start).Rotation(),P);
    if(!Shot)return nullptr;
    Shot->Attacker=Source;Shot->Victim=Target;Shot->Amount=Damage;Shot->Result=Outcome;
    const FString Style=Source->BasicAttackStyle();
    Shot->Style=Style==TEXT("bow")?1:Style==TEXT("lance")?3:2;
    Shot->TeamId=Source->TeamId;Shot->LaunchedPhase=State->Phase;
    Shot->ForceNetUpdate();return Shot;
}
void ACireTargetProjectile::Tick(float Delta){
    Super::Tick(Delta);Age+=Delta;
    const auto* State=GetWorld()->GetGameState<ACireGameState>();
    if(!State||State->Phase!=LaunchedPhase){Body->SetVisibility(false);if(HasAuthority())Destroy();return;}
    if(GetNetMode()!=NM_DedicatedServer){
        const auto* PC=GetWorld()->GetFirstPlayerController();const auto* Self=PC?Cast<ACireHero>(PC->GetPawn()):nullptr;
        Body->SetVisibility(GetWorld()->IsPlayingReplay()||(Self&&(State->Phase==2||Self->TeamId==TeamId)));
        if(AppliedStyle!=Style){
            const TCHAR* Path=Style==3?TEXT("/Game/Art/Weapons/CombatPrototype01/SM_PrototypeLance.SM_PrototypeLance"):
                Style==1?TEXT("/Game/Art/Weapons/CombatPrototype01/SM_PrototypeArrow.SM_PrototypeArrow"):TEXT("/Engine/BasicShapes/Sphere.Sphere");
            Body->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,Path));
            Body->SetRelativeScale3D(Style!=1&&Style!=3?FVector(.10):FVector(1));
            if(Style!=1&&Style!=3)Body->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/M_Ember.M_Ember")));
            AppliedStyle=Style;
        }
        // ability-vfx: readable head and wake on basic ranged attacks; hidden exactly when the body is.
        SetActorHiddenInGame(!Body->IsVisible());
        if(!VFXWake.IsValid()&&CireAbilityVFX::Enabled())
            VFXWake=CireSpellPresentation::AttachProjectile(this,Style==1?FName(TEXT("arrow")):Style==3?FName(TEXT("lance")):FName(TEXT("arcane")),Style==1||Style==3?9.f:15.f);
    }
    if(!IsValid(Attacker)||!IsValid(Victim)) {if(HasAuthority()||Age>6)Destroy();return;}
    if(HasAuthority()&&!Attacker->IsHostile(Victim)){Destroy();return;}
    const FVector Destination=Victim->GetActorLocation()+FVector(0,0,25);
    const FVector Offset=Destination-GetActorLocation();const float Step=2400.f*Delta;
    SetActorRotation(Offset.Rotation());
    if(Offset.Size()<=Step||Age>=6){
        SetActorLocation(Destination);
        if(HasAuthority()){
            const FString AttackStyle=Attacker->BasicAttackStyle();
            CireAttacks::Resolve(Attacker,Victim,Amount,Result,AttackStyle==TEXT("lance")?TEXT("Thrown lance"):
                AttackStyle==TEXT("bow")?TEXT("Bow shot"):AttackStyle==TEXT("axes")?TEXT("Thrown axe"):
                AttackStyle==TEXT("gunblade")?TEXT("Pistol shot"):AttackStyle==TEXT("glaive")?TEXT("Glaive"):AttackStyle==TEXT("blunderbuss")?TEXT("Arcane shot"):TEXT("Arcane bolt")); // new-champions: styles
            CireSignatureSkills::OnBasicProjectileHit(Attacker,Victim,Amount,Result==ECireHitOutcome::Hit); // new-champions: Moon Glaive bounces
            Destroy();
        }
    }else SetActorLocation(GetActorLocation()+Offset.GetSafeNormal()*Step);
}
