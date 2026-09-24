#include "CireNPCState.h"
#include "CireGame.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"
#include "Net/UnrealNetwork.h"

namespace
{
const FCireNPCThreatRules& Rules(){return CireNPCArchetypes::Get().Threat;}
ACireMonster* OwnerMonster(const UCireNPCState* State){return State?Cast<ACireMonster>(State->GetOwner()):nullptr;}
// Objects are only loaded if their package exists, so an art slot pointing at a
// model that has not been merged yet silently keeps the fallback body.
template<class T> T* LoadIfPresent(const FString& Path)
{
    if(Path.IsEmpty())return nullptr;
    const FString Package=FPackageName::ObjectPathToPackageName(Path);
    if(!FPackageName::DoesPackageExist(Package))return nullptr;
    return LoadObject<T>(nullptr,*Path);
}
}

UCireNPCState::UCireNPCState()
{
    PrimaryComponentTick.bCanEverTick=false;
    SetIsReplicatedByDefault(true);
}

void UCireNPCState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UCireNPCState,ArchetypeId);DOREPLIFETIME(UCireNPCState,Role);DOREPLIFETIME(UCireNPCState,Classification);
    DOREPLIFETIME(UCireNPCState,StatusFlags);DOREPLIFETIME(UCireNPCState,CastAbilityId);DOREPLIFETIME(UCireNPCState,bCastInterruptible);
    DOREPLIFETIME(UCireNPCState,ThreatTable);DOREPLIFETIME(UCireNPCState,Aggro);
}

FCireAggroChanged& UCireNPCState::OnAggroChanged(){static FCireAggroChanged Delegate;return Delegate;}

const FCireNPCArchetype* UCireNPCState::Archetype() const{return ArchetypeId.IsNone()?nullptr:CireNPCArchetypes::Find(ArchetypeId);}

TArray<FCireNPCAbilityInfo> UCireNPCState::Abilities() const
{
    TArray<FCireNPCAbilityInfo> Out;
    if(const auto* A=Archetype())for(const auto& Ab:A->Abilities)
    {
        FCireNPCAbilityInfo I;I.Id=Ab.Id;I.Name=Ab.Name;I.Description=Ab.Description;I.TypeLabel=CireNPCArchetypes::KindLabel(Ab);
        I.Kind=Ab.Kind;I.Cooldown=Ab.Cooldown;I.CastTime=Ab.CastTime;I.bInterruptible=Ab.bInterruptible;I.bBasic=Ab.bBasic;Out.Add(I);
    }
    return Out;
}

FCireNPCCastInfo UCireNPCState::CastInfo() const
{
    FCireNPCCastInfo Info;const auto* M=OwnerMonster(this);
    if(!M||M->CastingAbility.IsEmpty()||!M->GetWorld())return Info;
    const auto* GameState=M->GetWorld()->GetGameState();
    const float Now=GameState?GameState->GetServerWorldTimeSeconds():M->GetWorld()->GetTimeSeconds();
    Info.bCasting=true;Info.AbilityId=CastAbilityId.IsNone()?FName(*M->CastingAbility):CastAbilityId;
    const auto* A=Archetype();const auto* Ability=A?A->FindAbility(Info.AbilityId):nullptr;
    Info.Name=Ability?Ability->Name:M->CastingAbility;
    const float Total=FMath::Max(.01f,M->CastEndsAt-M->CastStartedAt);
    Info.Progress=FMath::Clamp((Now-M->CastStartedAt)/Total,0.f,1.f);
    Info.Remaining=FMath::Max(0.f,M->CastEndsAt-Now);Info.bInterruptible=bCastInterruptible;
    return Info;
}

float UCireNPCState::ThreatOf(const ACireHero* Hero) const
{
    for(const auto& E:ThreatTable)if(E.Hero==Hero)return E.Threat;
    return 0.f;
}
float UCireNPCState::ThreatPercent(const ACireHero* Hero) const
{
    const auto* M=OwnerMonster(this);if(!M||!Hero)return 0.f;
    const float Top=M->Victim?ThreatOf(M->Victim):ThreatTable.IsEmpty()?0.f:ThreatTable[0].Threat;
    if(Top<=0)return M->Victim==Hero?100.f:0.f;
    return 100.f*ThreatOf(Hero)/Top;
}
float UCireNPCState::PullPercent(const ACireHero* Hero) const
{
    const auto* M=OwnerMonster(this);if(!M||!Hero)return 0.f;
    if(M->Victim==Hero)return 100.f;
    const bool bMelee=FVector::DistSquared2D(M->GetActorLocation(),Hero->GetActorLocation())<=FMath::Square(Rules().MeleeRangeCm);
    return ThreatPercent(Hero)/(bMelee?Rules().MeleePullRatio:Rules().RangedPullRatio);
}

FString UCireNPCState::DescribeAggro(const FCireAggroEvent& E)
{
    const FString Monster=E.Monster.IsValid()?E.Monster->GetNPCDisplayName():TEXT("A monster");
    const FString Hero=E.NewTarget.IsValid()?E.NewTarget->HeroName:TEXT("nobody");
    switch(E.Reason)
    {
    case ECireAggroReason::Taunted:return FString::Printf(TEXT("%s taunted %s"),*Hero,*Monster);
    case ECireAggroReason::Reset:return FString::Printf(TEXT("%s dropped aggro"),*Monster);
    default:return E.NewTarget.IsValid()?FString::Printf(TEXT("%s gained aggro on %s"),*Hero,*Monster):FString::Printf(TEXT("%s lost its target"),*Monster);
    }
}
FString UCireNPCState::DescribeFocus(const ACireMonster* M)
{
    if(!M)return FString();
    return M->Victim?FString::Printf(TEXT("%s is focusing %s"),*M->GetNPCDisplayName(),*M->Victim->HeroName):FString::Printf(TEXT("%s has no target"),*M->GetNPCDisplayName());
}

void UCireNPCState::ResetRuntime()
{
    ReadyAt.Reset();LastThreatAt.Reset();ProvokedUntil.Reset();Guardian.Reset();
    GuardUntil=GuardFraction=RallyUntil=RallyBonus=ShieldWallUntil=ShieldWallReduction=EnrageBonus=0;
    bEnraged=false;bForcedLastSelect=false;ThreatPublishAt=0;KiteUntil=KiteReadyAt=0;DashUntil=DashSpeed=0;DashAbility=NAME_None;
    StatusFlags=0;CastAbilityId=NAME_None;bCastInterruptible=false;ThreatTable.Reset();
}

void UCireNPCState::SetAggro(ACireHero* NewTarget,ACireHero* OldTarget,ECireAggroReason Reason)
{
    auto* M=OwnerMonster(this);if(!M||!M->HasAuthority())return;
    Aggro.Target=NewTarget;Aggro.Previous=OldTarget;Aggro.Reason=Reason;++Aggro.Serial;
    PublishThreat(true);M->ForceNetUpdate();
    FCireAggroEvent Event;Event.Monster=M;Event.NewTarget=NewTarget;Event.OldTarget=OldTarget;Event.Reason=Reason;
    OnAggroChanged().Broadcast(Event);
}

void UCireNPCState::OnRep_Aggro()
{
    FCireAggroEvent Event;Event.Monster=OwnerMonster(this);Event.NewTarget=Aggro.Target;Event.OldTarget=Aggro.Previous;Event.Reason=Aggro.Reason;
    OnAggroChanged().Broadcast(Event);
}

void UCireNPCState::PublishThreat(bool bForce)
{
    auto* M=OwnerMonster(this);if(!M||!M->HasAuthority()||!M->GetWorld())return;
    const float Now=M->GetWorld()->GetTimeSeconds();
    if(!bForce&&Now<ThreatPublishAt)return;
    ThreatPublishAt=Now+Rules().PublishInterval;
    TArray<FCireThreatEntry> Rows;
    for(const auto& Pair:M->Threat)if(Pair.Key.IsValid()){FCireThreatEntry E;E.Hero=Pair.Key.Get();E.Threat=FMath::RoundToFloat(Pair.Value*10.f)/10.f;Rows.Add(E);}
    Rows.Sort([](const FCireThreatEntry& A,const FCireThreatEntry& B){return A.Threat>B.Threat;});
    if(Rows.Num()>10)Rows.SetNum(10);
    bool bSame=Rows.Num()==ThreatTable.Num();
    for(int32 I=0;bSame&&I<Rows.Num();++I)bSame=Rows[I].Hero==ThreatTable[I].Hero&&Rows[I].Threat==ThreatTable[I].Threat;
    if(!bSame)ThreatTable=MoveTemp(Rows);
}

void UCireNPCState::RefreshStatusFlags(float Now)
{
    uint8 Flags=0;
    if(bEnraged)Flags|=CireNPCStatus::Enraged;
    if(RallyUntil>Now)Flags|=CireNPCStatus::Rallied;
    if(ShieldWallUntil>Now)Flags|=CireNPCStatus::ShieldWall;
    if(GuardUntil>Now&&Guardian.IsValid()&&Guardian->Health>0)Flags|=CireNPCStatus::Guarded;
    for(const auto& Pair:ProvokedUntil)if(Pair.Value>Now){Flags|=CireNPCStatus::Provoking;break;}
    if(DashUntil>Now)Flags|=CireNPCStatus::Charging;
    StatusFlags=Flags;
}

void UCireNPCState::OnRep_Archetype(){ApplyVisuals();}

void UCireNPCState::ApplyVisuals()
{
    auto* M=OwnerMonster(this);
    if(!M||!M->GetWorld()||M->GetNetMode()==NM_DedicatedServer||AppliedVisualArchetype==ArchetypeId)return;
    AppliedVisualArchetype=ArchetypeId;
    for(auto& Part:VisualParts)if(Part)Part->DestroyComponent();
    VisualParts.Reset();
    if(StaticBody){StaticBody->DestroyComponent();StaticBody=nullptr;}
    const auto* A=Archetype();if(!A)return;
    USkeletalMeshComponent* Body=M->GetMesh();
    // Mesh slot: static or skeletal model when present, otherwise the mannequin.
    if(auto* Skeletal=LoadIfPresent<USkeletalMesh>(A->MeshPath))
    {
        Body->SetSkeletalMesh(Skeletal);Body->SetRelativeScale3D(FVector(A->MeshScale));
        Body->SetRelativeRotation(FRotator(0,A->MeshYaw,0));Body->SetVisibility(true);
    }
    else if(auto* Static=LoadIfPresent<UStaticMesh>(A->MeshPath))
    {
        StaticBody=NewObject<UStaticMeshComponent>(M);M->AddInstanceComponent(StaticBody);
        StaticBody->SetupAttachment(M->GetCapsuleComponent());StaticBody->SetStaticMesh(Static);
        StaticBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);StaticBody->SetCanEverAffectNavigation(false);
        StaticBody->SetRelativeLocation(FVector(0,0,-M->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));
        StaticBody->SetRelativeRotation(FRotator(0,A->MeshYaw,0));StaticBody->SetRelativeScale3D(FVector(A->MeshScale));
        StaticBody->RegisterComponent();Body->SetVisibility(false,false);
    }
    if(auto* Material=LoadIfPresent<UMaterialInterface>(A->MaterialPath))
    {
        UMeshComponent* Target=StaticBody?static_cast<UMeshComponent*>(StaticBody):Body;
        if(!StaticBody)for(int32 I=0;I<Target->GetNumMaterials();++I)
        {
            auto* Dynamic=UMaterialInstanceDynamic::Create(Material,M);
            Dynamic->SetVectorParameterValue(TEXT("Color"),A->Tint);Dynamic->SetVectorParameterValue(TEXT("BaseColor"),A->Tint);
            Target->SetMaterial(I,Dynamic);
        }
    }
    if(!StaticBody&&Body->GetSkeletalMeshAsset())
        for(const auto& Prop:A->Props)
        {
            if(Body->GetBoneIndex(Prop.Bone)==INDEX_NONE)continue;
            auto* Asset=LoadIfPresent<UStaticMesh>(Prop.Asset);if(!Asset)continue;
            auto* Part=NewObject<UStaticMeshComponent>(M);M->AddInstanceComponent(Part);
            Part->SetupAttachment(Body,Prop.Bone);Part->SetStaticMesh(Asset);
            Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);Part->SetCanEverAffectNavigation(false);Part->SetCastShadow(true);
            Part->SetRelativeLocation(Prop.Offset);Part->SetRelativeRotation(Prop.Rotation);Part->SetRelativeScale3D(FVector(Prop.Scale));
            Part->RegisterComponent();VisualParts.Add(Part);
        }
}

// ---- ACireMonster read API ----
ECireNPCRole ACireMonster::GetNPCRole() const
{
    if(NPCState&&!NPCState->ArchetypeId.IsNone())return NPCState->Role;
    return CombatArchetype==2?ECireNPCRole::Caster:CombatArchetype==3?ECireNPCRole::Ranged:ECireNPCRole::Bruiser;
}
ECireNPCClass ACireMonster::GetNPCClassification() const
{
    if(NPCState&&!NPCState->ArchetypeId.IsNone())return NPCState->Classification;
    return bBoss?ECireNPCClass::Boss:Tier>0?ECireNPCClass::Elite:ECireNPCClass::Normal;
}
FString ACireMonster::GetNPCDisplayName() const
{
    if(bArmoredEscort)return MonsterName;
    if(const auto* A=NPCState?NPCState->Archetype():nullptr)return A->DisplayName;
    return MonsterName;
}
