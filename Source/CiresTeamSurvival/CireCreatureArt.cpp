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
#if WITH_EDITOR
#include "SkinnedAssetCompiler.h"
#endif

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
/** 0..1 gait cycle: first half is stance (paw planted, linear sweep back), second half swing. */
void GaitSample(float Phase01,float Amplitude,float& OutAngle,float& OutLift)
{
    const float P=Phase01-FMath::FloorToFloat(Phase01);
    if(P<.5f){OutAngle=Amplitude*(1.f-4.f*P);OutLift=0.f;return;}
    const float Q=(P-.5f)*2.f;
    OutAngle=-Amplitude+2.f*Amplitude*(.5f-.5f*FMath::Cos(PI*Q));OutLift=FMath::Sin(PI*Q);
}
struct FBearProxy : FAnimInstanceProxy
{
    explicit FBearProxy(UAnimInstance* In):FAnimInstanceProxy(In){}
    float Phase=0,Stride=0,Attack=0,Time=0,Air=0,Roll=0,Amplitude=0,LegUnits=34;
    virtual void PreUpdate(UAnimInstance* Instance,float Delta) override
    {
        FAnimInstanceProxy::PreUpdate(Instance,Delta);const auto* A=CastChecked<UCireBearAnimInstance>(Instance);
        Phase=A->Phase;Stride=A->Stride;Attack=A->Attack;Time=A->Time;Air=A->Air;Roll=A->Roll;Amplitude=A->Amplitude;LegUnits=A->LegUnits;
    }
    virtual bool Evaluate(FPoseContext& Output) override
    {
        Output.ResetToRefPose();auto& Pose=Output.Pose;
        // Diagonal trot: front-left pairs with rear-right. Stance legs sweep back linearly so the
        // planted paw travels with the ground (phase rate is derived from leg length and speed).
        const float Cycle=Phase/(2*PI);const float Ground=1-Air;const float Pulse=AttackPulse(Attack);
        float A1,L1,A2,L2;GaitSample(Cycle,Amplitude*Ground,A1,L1);GaitSample(Cycle+.5f,Amplitude*Ground,A2,L2);
        L1*=Stride*Ground;L2*=Stride*Ground;
        const FVector X(1,0,0);
        // Front legs: shoulder swing, elbow/wrist fold while the paw is in the air.
        RotateBone(Pose,TEXT("0_Left_Limb_0"),X,A1-Pulse*28+Air*20);
        RotateBone(Pose,TEXT("0_Left_Limb_2"),X,L1*30+Pulse*24);
        RotateBone(Pose,TEXT("0_Left_Limb_3"),X,-L1*22);
        RotateBone(Pose,TEXT("0_Right_Limb_0"),X,A2+Air*20);
        RotateBone(Pose,TEXT("0_Right_Limb_2"),X,L2*30);
        RotateBone(Pose,TEXT("0_Right_Limb_3"),X,-L2*22);
        // Rear legs (diagonal partners): hip swing, knee/hock fold during swing.
        RotateBone(Pose,TEXT("1_Left_Limb_0"),X,A2-Air*12);
        RotateBone(Pose,TEXT("1_Left_Limb_1"),X,-L2*26);
        RotateBone(Pose,TEXT("1_Left_Limb_2"),X,L2*20);
        RotateBone(Pose,TEXT("1_Right_Limb_0"),X,A1-Air*12);
        // Motion03 rig adds the missing right-rear knee/hock; the original import keeps a rigid swing.
        RotateBone(Pose,TEXT("1_Right_Limb_1"),X,-L1*26);
        RotateBone(Pose,TEXT("1_Right_Limb_2"),X,L1*20);
        RotateBone(Pose,TEXT("Spine_0"),FVector(0,1,0),FMath::Sin(Phase*2)*Stride*1.2f);
        RotateBone(Pose,TEXT("Spine_1"),X,FMath::Sin(Time*1.6f)*.6f*(1-Stride));
        RotateBone(Pose,TEXT("Head_0"),X,FMath::Sin(Time*1.8f)*1.1f*(1-Stride)+FMath::Sin(Phase*2)*Stride*2.f+Pulse*11+Roll*7);
        RotateBone(Pose,TEXT("Head_1"),FVector(0,0,1),-Pulse*9);
        // A straight leg at angle a is shorter vertically by L(1-cos a): lower the body so the
        // stance paws stay planted instead of floating at the ends of each stride.
        const auto Root=BoneIndex(Pose,TEXT("Root"));
        if(Root.IsValid())
        {
            const float Stance=FMath::Min(FMath::Cos(FMath::DegreesToRadians(A1)),FMath::Cos(FMath::DegreesToRadians(A2)));
            Pose[Root].AddToTranslation(FVector(0,0,-LegUnits*(1-Stance)*Ground));
        }
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
    Sections.Reset();SourceAsset=nullptr;Kind.Reset();Phase=0;SmoothedSpeed=0;AnimationTime=0;UpdateBudget=0;bHasLastYaw=false;
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
        MeshScale=Scale;LegUnits=34;
        {
            // Hip-to-paw height from the reference skeleton (component space) drives stride length.
            const auto& Ref=Mesh->GetRefSkeleton();
            const auto Global=[&Ref](const TCHAR* Name){int32 I=Ref.FindBoneIndex(Name);FTransform T=FTransform::Identity;
                while(I!=INDEX_NONE){T=T*Ref.GetRefBonePose()[I];I=Ref.GetParentIndex(I);}return T.GetLocation();};
            if(Ref.FindBoneIndex(TEXT("1_Left_Limb_0"))!=INDEX_NONE&&Ref.FindBoneIndex(TEXT("1_Left_Limb_3"))!=INDEX_NONE)
            {const float Leg=static_cast<float>(Global(TEXT("1_Left_Limb_0")).Z-Global(TEXT("1_Left_Limb_3")).Z);if(FMath::IsFinite(Leg)&&Leg>5)LegUnits=Leg;}
        }
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
    float Stride=FMath::Clamp(SmoothedSpeed/280.f,0.f,1.f);
    if(Kind==TEXT("bear"))
    {
        // Turning in place still steps: treat the yaw rate at the paws (40% of leg length out) as travel.
        const float Yaw=static_cast<float>(Hero.GetActorRotation().Yaw);
        const float YawRate=bHasLastYaw&&Dt>0?FMath::Abs(FRotator::NormalizeAxis(Yaw-LastYaw))/Dt:0.f;LastYaw=Yaw;bHasLastYaw=true;
        const float World=MeshScale*static_cast<float>(Hero.GetActorScale3D().Z);
        const float LegCm=FMath::Max(10.f,LegUnits*World);
        const float TurnTravel=FMath::DegreesToRadians(FMath::Min(YawRate,360.f))*LegCm*.4f;
        const float Travel=Hero.bDead?0.f:FMath::Max(SmoothedSpeed,TurnTravel);
        Stride=FMath::Clamp(Travel/140.f,0.f,1.f);
        // Walk about 15 degrees, run/charge up to about 30 degrees of hip swing.
        const float Amplitude=FMath::Lerp(15.f,30.f,FMath::Clamp((Travel-150.f)/370.f,0.f,1.f))*Stride;
        // A planted paw sweeps 2*L*sin(a) per half cycle, so one full cycle covers 4*L*sin(a).
        const float CycleCm=FMath::Max(20.f,4.f*LegCm*FMath::Sin(FMath::DegreesToRadians(FMath::Max(Amplitude,4.f))));
        const float Forward=static_cast<float>(FVector::DotProduct(Hero.GetVelocity(),Hero.GetActorForwardVector()));
        const float Direction=Forward<-20.f&&-Forward>SmoothedSpeed*.5f?-1.f:1.f; // backpedal reverses the cycle
        Phase=FMath::Fmod(Phase+Direction*Dt*Travel/CycleCm*2*PI+2*PI,2*PI);
        if(auto* A=Bear?Cast<UCireBearAnimInstance>(Bear->GetAnimInstance()):nullptr){A->Amplitude=Amplitude;A->LegUnits=LegUnits;}
    }
    else Phase=FMath::Fmod(Phase+Dt*SmoothedSpeed/220.f*2*PI,2*PI);
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

#if !UE_BUILD_SHIPPING
bool UCireCreatureArt::RunGaitSmoke(UWorld* World)
{
    if(!World)return false;
    bool Pass=true;int32 Checks=0;
    auto Check=[&](bool Value,const FString& Why){++Checks;if(!Value){Pass=false;UE_LOG(LogTemp,Error,TEXT("CIRE_BEAR_GAIT_FAIL %s"),*Why);}};
    const TCHAR* MeshPath=TEXT("/Game/Art/Characters/Motion03/SK_BearMotion.SK_BearMotion");
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Hero=World->SpawnActor<ACireHero>(FVector(0,-9000,9000),FRotator::ZeroRotator,Params);
    if(!Hero){Check(false,TEXT("fixture hero spawned"));return false;}
    Hero->SetActorTickEnabled(false);Hero->SetActorEnableCollision(false);Hero->GetCharacterMovement()->SetComponentTickEnabled(false);
    Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking); // grounded gait, not the airborne pose
    auto* Art=NewObject<UCireCreatureArt>(Hero,TEXT("GaitFixture"));Art->RegisterComponent();
    const bool bApplied=Art->Apply(*Hero,TEXT("bear"),MeshPath,148.f);
    Check(bApplied&&Art->Bear!=nullptr,TEXT("Motion03 bear body applies"));
    if(!bApplied||!Art->Bear){Hero->Destroy();return false;}
    auto* Mesh=Art->Bear.Get();Mesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    // The body may finish async compilation after Apply; rebuild the pose state before sampling.
#if WITH_EDITOR
    // Editor builds compile skinned assets asynchronously; animation does not tick until that finishes.
    if(auto* Asset=Mesh->GetSkeletalMeshAsset();Asset&&Asset->IsCompiling())FSkinnedAssetCompilingManager::Get().FinishCompilation({Asset});
#endif
    Mesh->bEnableUpdateRateOptimizations=false;Mesh->InitAnim(true); // never rendered here: evaluate every step
    for(const TCHAR* Bone:{TEXT("1_Right_Limb_1"),TEXT("1_Right_Limb_2"),TEXT("1_Right_Limb_3")})
        Check(Mesh->GetBoneIndex(Bone)!=INDEX_NONE,FString::Printf(TEXT("right rear leg bone %s exists"),Bone));
    const TCHAR* Paws[]={TEXT("0_Left_Limb_4"),TEXT("0_Right_Limb_4"),TEXT("1_Left_Limb_3"),TEXT("1_Right_Limb_3")};
    FString Summary;
    for(const float Speed:{240.f,520.f})
    {
        Hero->SetActorLocation(FVector(0,-9000,9000));Art->Phase=0;Art->SmoothedSpeed=Speed;Art->bHasLastYaw=false;
        Hero->GetCharacterMovement()->Velocity=FVector(Speed,0,0);
        const float Dt=1.f/120.f;const int32 Frames=240;
        TArray<TArray<FVector>> Track;Track.SetNum(UE_ARRAY_COUNT(Paws));
        TArray<float> KneeRange;KneeRange.Init(0,2);float KneeMin[2]={1e9f,1e9f},KneeMax[2]={-1e9f,-1e9f};
        for(int32 F=0;F<Frames;++F)
        {
            Hero->SetActorLocation(Hero->GetActorLocation()+FVector(Speed*Dt,0,0));
            Art->Update(*Hero,Dt);Mesh->TickAnimation(Dt,false);Mesh->RefreshBoneTransforms(nullptr);Mesh->FinalizeBoneTransform();
            if(F<60)continue; // settle smoothing
            for(int32 P=0;P<UE_ARRAY_COUNT(Paws);++P)Track[P].Add(Mesh->GetBoneLocation(Paws[P],EBoneSpaces::WorldSpace));
            // Knee-to-paw vector relative to hip: a rigid leg keeps the knee angle constant.
            for(int32 Side=0;Side<2;++Side)
            {
                const FVector Hip=Mesh->GetBoneLocation(Side?TEXT("1_Right_Limb_0"):TEXT("1_Left_Limb_0"),EBoneSpaces::WorldSpace);
                const FVector Knee=Mesh->GetBoneLocation(Side?TEXT("1_Right_Limb_1"):TEXT("1_Left_Limb_1"),EBoneSpaces::WorldSpace);
                const FVector Paw=Mesh->GetBoneLocation(Side?TEXT("1_Right_Limb_3"):TEXT("1_Left_Limb_3"),EBoneSpaces::WorldSpace);
                const float Angle=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct((Knee-Hip).GetSafeNormal(),(Paw-Knee).GetSafeNormal()),-1.0,1.0)));
                KneeMin[Side]=FMath::Min(KneeMin[Side],Angle);KneeMax[Side]=FMath::Max(KneeMax[Side],Angle);
            }
        }
        for(int32 P=0;P<UE_ARRAY_COUNT(Paws);++P)
        {
            const auto& T=Track[P];double MinZ=TNumericLimits<double>::Max();
            for(const FVector& V:T)MinZ=FMath::Min(MinZ,V.Z);
            // Stance frames: the paw is within 1.5 cm of its lowest point. Measure its horizontal ground speed there.
            double Slip=0;int32 N=0;
            for(int32 I=1;I<T.Num();++I)if(T[I].Z<MinZ+1.5&&T[I-1].Z<MinZ+1.5){Slip+=FVector::Dist2D(T[I],T[I-1])/Dt;++N;}
            const float Ratio=N?static_cast<float>(Slip/N/Speed):1.f;
            Summary+=FString::Printf(TEXT(" %s@%.0f=%.2f(n%d)"),Paws[P],Speed,Ratio,N);
            Check(N>=6,FString::Printf(TEXT("%s has stance frames at %.0f cm/s"),Paws[P],Speed));
            Check(Ratio<.3f,FString::Printf(TEXT("%s planted-paw slip %.2f of body speed at %.0f cm/s"),Paws[P],Ratio,Speed));
        }
        Summary+=FString::Printf(TEXT(" knee_range_l=%.1f knee_range_r=%.1f"),KneeMax[0]-KneeMin[0],KneeMax[1]-KneeMin[1]);
        Check(KneeMax[1]-KneeMin[1]>8.f,FString::Printf(TEXT("right rear knee bends while walking at %.0f cm/s"),Speed));
    }
    Hero->Destroy();
    UE_LOG(LogTemp,Display,TEXT("CIRE_BEAR_GAIT_%s checks=%d%s"),Pass?TEXT("PASS"):TEXT("FAIL"),Checks,*Summary);
    return Pass;
}
#endif
