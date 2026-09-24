#include "CireCreatureArt.h"
#include "CireGame.h"
#include "CireMobility.h"
#include "Animation/AnimInstanceProxy.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "KismetProceduralMeshLibrary.h"

namespace
{
FTransform PoseWorld(const FCompactPose& Pose,FCompactPoseBoneIndex Bone)
{
    FTransform Value=Pose[Bone];
    for(Bone=Pose.GetBoneContainer().GetParentBoneIndex(Bone);Bone.IsValid();Bone=Pose.GetBoneContainer().GetParentBoneIndex(Bone))Value*=Pose[Bone];
    return Value;
}
FCompactPoseBoneIndex BoneIndex(const FCompactPose& Pose,const TCHAR* Name)
{
    const auto& C=Pose.GetBoneContainer();const int32 Index=C.GetReferenceSkeleton().FindBoneIndex(Name);
    return Index==INDEX_NONE?FCompactPoseBoneIndex(INDEX_NONE):C.MakeCompactPoseIndex(FMeshPoseBoneIndex(Index));
}
void RotateBone(FCompactPose& Pose,const TCHAR* Name,FVector Axis,float Degrees)
{
    const auto Bone=BoneIndex(Pose,Name);if(!Bone.IsValid())return;
    const auto Parent=Pose.GetBoneContainer().GetParentBoneIndex(Bone);
    if(Parent.IsValid())Axis=PoseWorld(Pose,Parent).GetRotation().UnrotateVector(Axis);
    auto& Transform=Pose[Bone];Transform.SetRotation((FQuat(Axis.GetSafeNormal(),FMath::DegreesToRadians(Degrees))*Transform.GetRotation()).GetNormalized());
}
float AttackPulse(float Progress)
{
    if(Progress<0||Progress>1)return 0;
    return FMath::Sin(PI*FMath::Clamp((Progress-.08f)/.70f,0.f,1.f));
}
struct FBearProxy : FAnimInstanceProxy
{
    explicit FBearProxy(UAnimInstance* In):FAnimInstanceProxy(In){}
    float Phase=0,Stride=0,Attack=0,Time=0,Air=0,Roll=0;
    virtual void PreUpdate(UAnimInstance* Instance,float Delta) override
    {
        FAnimInstanceProxy::PreUpdate(Instance,Delta);const auto* A=CastChecked<UCireBearAnimInstance>(Instance);
        Phase=A->Phase;Stride=A->Stride;Attack=A->Attack;Time=A->Time;Air=A->Air;Roll=A->Roll;
    }
    virtual bool Evaluate(FPoseContext& Output) override
    {
        Output.ResetToRefPose();auto& Pose=Output.Pose;
        // Diagonal pairs advance together. Preserve every imported bone scale and translation.
        const float Swing=FMath::Sin(Phase)*15.f*Stride*(1-Air);
        const float Pulse=AttackPulse(Attack);
        RotateBone(Pose,TEXT("0_Left_Limb_0"),FVector(1,0,0),Swing-Pulse*28+Air*20);
        RotateBone(Pose,TEXT("0_Right_Limb_0"),FVector(1,0,0),-Swing+Air*20);
        RotateBone(Pose,TEXT("1_Left_Limb_0"),FVector(1,0,0),-Swing*.82f-Air*12);
        // The imported right rear limb has only one weighted hip; keep that limb a rigid swing.
        RotateBone(Pose,TEXT("1_Right_Limb_0"),FVector(1,0,0),Swing*.82f-Air*12);
        RotateBone(Pose,TEXT("0_Left_Limb_2"),FVector(1,0,0),FMath::Max(0.f,Swing)*.85f+Pulse*24);
        RotateBone(Pose,TEXT("0_Right_Limb_2"),FVector(1,0,0),FMath::Max(0.f,-Swing)*.85f);
        RotateBone(Pose,TEXT("1_Left_Limb_1"),FVector(1,0,0),-FMath::Max(0.f,-Swing)*.55f);
        RotateBone(Pose,TEXT("Spine_0"),FVector(0,1,0),FMath::Sin(Phase)*Stride*1.5f);
        RotateBone(Pose,TEXT("Head_0"),FVector(1,0,0),FMath::Sin(Time*1.8f)*1.1f+Pulse*11+Roll*7);
        RotateBone(Pose,TEXT("Head_1"),FVector(0,0,1),-Pulse*9);
        // Modest body clearance compensates planted-paw rotation; no root-motion translation.
        const auto Root=BoneIndex(Pose,TEXT("Root"));
        if(Root.IsValid())Pose[Root].AddToTranslation(FVector(0,0,Stride*FMath::Abs(FMath::Sin(Phase))*1.1f));
        return true;
    }
};
void Prepare(UMeshComponent& Mesh)
{
    Mesh.SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh.SetGenerateOverlapEvents(false);Mesh.SetCanEverAffectNavigation(false);
}
}

FAnimInstanceProxy* UCireBearAnimInstance::CreateAnimInstanceProxy(){return new FBearProxy(this);}
bool UCireCreatureArt::Handles(const FString& Profile){return Profile==TEXT("bear")||Profile==TEXT("whisp")||Profile==TEXT("evergrove_centaur");}
UMeshComponent* UCireCreatureArt::VisualMesh() const{return Bear?static_cast<UMeshComponent*>(Bear.Get()):Centaur?static_cast<UMeshComponent*>(Centaur.Get()):StaticBody.Get();}
void UCireCreatureArt::Clear()
{
    if(StaticBody)StaticBody->DestroyComponent();if(Centaur)Centaur->DestroyComponent();StaticBody=nullptr;Centaur=nullptr;Bear=nullptr;
    Sections.Reset();SourceAsset=nullptr;Kind.Reset();Phase=0;SmoothedSpeed=0;AnimationTime=0;UpdateBudget=0;
}
bool UCireCreatureArt::Apply(ACireHero& Hero,const FString& Profile,const FString& MeshPath,float HeightCm)
{
    Clear();if(!Handles(Profile)||Hero.GetNetMode()==NM_DedicatedServer)return false;
    Kind=Profile;auto* Parent=Hero.GetMesh();Parent->SetAnimInstanceClass(nullptr);Parent->EmptyOverrideMaterials();
    const float Capsule=Hero.GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
    if(Profile==TEXT("bear"))
    {
        auto* Mesh=LoadObject<USkeletalMesh>(nullptr,*MeshPath);if(!Mesh)return false;
        const auto Bounds=Mesh->GetImportedBounds();const float Height=static_cast<float>(Bounds.BoxExtent.Z*2);
        if(Height<20||Height>1000||Mesh->GetRefSkeleton().FindBoneIndex(TEXT("0_Left_Limb_0"))==INDEX_NONE)return false;
        const float Scale=HeightCm/Height;Parent->SetSkeletalMesh(Mesh);Parent->SetRelativeScale3D(FVector(Scale));
        BasePosition=FVector(0,0,-Capsule-(Bounds.Origin.Z-Bounds.BoxExtent.Z)*Scale);
        Parent->SetRelativeLocation(BasePosition);Parent->SetRelativeRotation(FRotator(0,-90,0));
        Parent->SetAnimInstanceClass(UCireBearAnimInstance::StaticClass());Bear=Parent;SourceAsset=Mesh;
    }
    else
    {
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,*MeshPath);if(!Mesh)return false;SourceAsset=Mesh;const auto Bounds=Mesh->GetBounds();
        const float Height=static_cast<float>(Bounds.BoxExtent.Z*2);if(Height<20||Height>1000)return false;
        const float Scale=HeightCm/Height;
        // The empty character mesh stays as the replicated network-smoothing / realm-visibility parent.
        Parent->SetSkeletalMesh(nullptr);Parent->SetRelativeScale3D(FVector::OneVector);
        Parent->SetRelativeRotation(FRotator::ZeroRotator);Parent->SetRelativeLocation(FVector(0,0,-Capsule));
        BasePosition=FVector(0,0,-(Bounds.Origin.Z-Bounds.BoxExtent.Z)*Scale+(Profile==TEXT("whisp")?65:0));
        if(Profile==TEXT("whisp"))
        {
            StaticBody=NewObject<UStaticMeshComponent>(&Hero,TEXT("WhispBody"));Hero.AddInstanceComponent(StaticBody);
            StaticBody->SetupAttachment(Parent);StaticBody->SetStaticMesh(Mesh);Prepare(*StaticBody);StaticBody->SetRelativeScale3D(FVector(Scale));
            StaticBody->SetRelativeLocation(BasePosition);StaticBody->SetRelativeRotation(FRotator(0,-90,0));StaticBody->RegisterComponent();
        }
        else
        {
            if(!Mesh->bAllowCPUAccess||Mesh->GetNumSections(0)>8)return false;
            Centaur=NewObject<UProceduralMeshComponent>(&Hero,TEXT("CentaurBody"));Hero.AddInstanceComponent(Centaur);Centaur->SetupAttachment(Parent);Prepare(*Centaur);
            Centaur->SetRelativeScale3D(FVector(Scale));Centaur->SetRelativeLocation(BasePosition);Centaur->SetRelativeRotation(FRotator(0,-90,0));Centaur->RegisterComponent();
            int32 Total=0;
            for(int32 I=0;I<Mesh->GetNumSections(0);++I)
            {
                auto& S=Sections.AddDefaulted_GetRef();UKismetProceduralMeshLibrary::GetSectionFromStaticMesh(Mesh,0,I,S.Rest,S.Triangles,S.RestNormals,S.UV,S.RestTangents);
                Total+=S.Rest.Num();if(Total>30000||S.Rest.IsEmpty()){Clear();return false;}
                S.Positions=S.Rest;S.Normals=S.RestNormals;S.Tangents=S.RestTangents;
                Centaur->CreateMeshSection_LinearColor(I,S.Positions,S.Triangles,S.Normals,S.UV,TArray<FLinearColor>(),S.Tangents,false);
                Centaur->SetMaterial(I,Mesh->GetMaterial(I));
            }
            DeformCentaur(0,-1,0,0);
        }
    }
    Hero.CacheInitialMeshOffset(Parent->GetRelativeLocation(),Parent->GetRelativeRotation());
    return VisualMesh()!=nullptr;
}
void UCireCreatureArt::DeformCentaur(float Stride,float Attack,float Air,float Roll)
{
    if(!Centaur)return;
    const float Pulse=AttackPulse(Attack);
    for(int32 Section=0;Section<Sections.Num();++Section)
    {
        auto& S=Sections[Section];
        for(int32 I=0;I<S.Rest.Num();++I)
        {
            const FVector P=S.Rest[I];FVector Out=P;FQuat Turn=FQuat::Identity;
            // Four independent lower-leg volumes blended smoothly into the unchanged horse torso.
            // Original material, UVs and topology remain intact; this is a prototype spatial skin.
            const float LegWeight=1.f-FMath::SmoothStep(22.f,40.f,static_cast<float>(P.Z));
            if(LegWeight>0 && P.Y>-12)
            {
                const bool Front=P.Y>12,Left=P.X>0;const float Offset=(Front==Left)?0.f:PI;
                const float Cycle=FMath::Sin(Phase+Offset),Lift=FMath::Max(0.f,Cycle);
                const FVector Pivot(Left?9.f:-9.f,Front?24.f:-3.f,37.f);
                const float Degrees=Cycle*13.f*Stride*(1-Air)+(Front?12.f:-8.f)*Air+Roll*(Front?9.f:-5.f);
                Turn=FQuat(FVector(1,0,0),FMath::DegreesToRadians(Degrees*LegWeight));
                Out=Pivot+Turn.RotateVector(P-Pivot);
                Out.Z+=Lift*3.5f*Stride*LegWeight;
                // Never pull the lowest hoof points through the local floor during the stance phase.
                if(P.Z<4)Out.Z=FMath::Max(P.Z,Out.Z);
            }
            // The imported upper body is T-posed: relax both arms and add the cast/throw extension.
            const float ArmWeight=FMath::SmoothStep(10.f,17.f,static_cast<float>(FMath::Abs(P.X))) *
                FMath::SmoothStep(56.f,64.f,static_cast<float>(P.Z))*(1-FMath::SmoothStep(77.f,83.f,static_cast<float>(P.Z)));
            if(ArmWeight>0)
            {
                const float Side=P.X>0?1.f:-1.f;const FVector Pivot(11.f*Side,23.f,70.f);
                const float Raise=P.X>0?Pulse:Pulse*.35f;
                const FQuat Arm(FVector(0,1,0),FMath::DegreesToRadians(Side*(57-Raise*49)*ArmWeight));
                Out=Pivot+Arm.RotateVector(Out-Pivot);Turn=Arm*Turn;
                Out.Y+=Raise*8*ArmWeight;
            }
            S.Positions[I]=Out;S.Normals[I]=Turn.RotateVector(S.RestNormals[I]);
            S.Tangents[I]=FProcMeshTangent(Turn.RotateVector(S.RestTangents[I].TangentX),S.RestTangents[I].bFlipTangentY);
        }
        Centaur->UpdateMeshSection_LinearColor(Section,S.Positions,S.Normals,S.UV,TArray<FLinearColor>(),S.Tangents);
    }
}
void UCireCreatureArt::Update(ACireHero& Hero,float Delta)
{
    if(Kind.IsEmpty())return;const float Dt=FMath::Clamp(Delta,0.f,.1f);
    const float Speed=Hero.bDead?0.f:static_cast<float>(Hero.GetVelocity().Size2D());
    SmoothedSpeed=FMath::FInterpTo(SmoothedSpeed,Speed,Dt,10.f);AnimationTime+=Dt;
    const float Stride=FMath::Clamp(SmoothedSpeed/280.f,0.f,1.f);
    Phase=FMath::Fmod(Phase+Dt*SmoothedSpeed/(Kind==TEXT("bear")?175.f:220.f)*2*PI,2*PI);
    const auto* State=Hero.GetWorld()->GetGameState();const double Now=State?State->GetServerWorldTimeSeconds():Hero.GetWorld()->GetTimeSeconds();
    const float Elapsed=static_cast<float>(Now-Hero.AttackStartedServerTime);
    const float Attack=!Hero.bDead&&Hero.AttackSerial>0&&Elapsed>=0&&Elapsed<Hero.AttackDuration?Elapsed/FMath::Max(.01f,Hero.AttackDuration):-1.f;
    const float Air=Hero.GetCharacterMovement()->IsFalling()?1.f:0.f;
    const float Roll=Hero.Mobility&&Hero.Mobility->IsRolling()?FMath::Sin(PI*Hero.Mobility->RollProgress()):0.f;
    if(Bear)
    {
        if(auto* A=Cast<UCireBearAnimInstance>(Bear->GetAnimInstance())){A->Phase=Phase;A->Stride=Stride;A->Attack=Attack;A->Time=Hero.bDead?0:AnimationTime;A->Air=Air;A->Roll=Roll;}
    }
    else if(StaticBody)
    {
        const float Bob=Hero.bDead?0.f:FMath::Sin(AnimationTime*2.2f)*7;
        StaticBody->SetRelativeLocation(BasePosition+FVector(AttackPulse(Attack)*12,0,Bob+Roll*14));
        StaticBody->SetRelativeRotation(FRotator(Stride*7,-90+FMath::Sin(AnimationTime*1.3f)*5,AttackPulse(Attack)*9));
        StaticBody->SetOverlayMaterial(Hero.GetMesh()->GetOverlayMaterial());
    }
    else if(Centaur)
    {
        // 30 Hz bounded deformation; no per-frame heap allocation and no collision cooking.
        UpdateBudget+=Dt;if(UpdateBudget>=1.f/30){UpdateBudget=0;DeformCentaur(Stride,Attack,Air,Roll);}
        Centaur->SetRelativeLocation(BasePosition+FVector(0,0,Roll*4));
        Centaur->SetOverlayMaterial(Hero.GetMesh()->GetOverlayMaterial());
    }
}
