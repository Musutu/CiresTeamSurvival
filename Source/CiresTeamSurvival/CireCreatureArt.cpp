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
#include "CireChampionArt.h" // new-champions: rider combat layer
#include "CireGrip.h" // new-champions: prop grips
#include "CireMonsterAnim.h" // new-champions: native monster / mount clips
#include "CireChampionActions.h" // fab-integration: skill kinds and release leads
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Materials/MaterialInterface.h" // fab-integration: variant material swap
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
bool UCireCreatureArt::HandlesMotion(const FString& Motion){return Motion==TEXT("monster_native")||Motion==TEXT("mounted")||Motion==TEXT("quadruped_procedural");} // new-champions; pets: procedural quadruped
UMeshComponent* UCireCreatureArt::VisualMesh() const{if(Quad)return Quad;if(Native)return Native;return Bear?static_cast<UMeshComponent*>(Bear.Get()):Centaur?static_cast<UMeshComponent*>(Centaur.Get()):StaticBody.Get();}
void UCireCreatureArt::Clear()
{
    if(StaticBody)StaticBody->DestroyComponent();if(Centaur)Centaur->DestroyComponent();StaticBody=nullptr;Centaur=nullptr;Bear=nullptr;
    // new-champions: the rider and props are ours; the native body is the hero's own mesh (restored by the champion art).
    if(Rider)Rider->DestroyComponent();Rider=nullptr;for(auto& Part:Props)if(Part)Part->DestroyComponent();Props.Reset();Native=nullptr;
    Quad=nullptr;DeathClip=nullptr;DeadAge=DeadWeight=0; // pets
    // fab-integration: leader-pose parts and the champion reaction state.
    for(auto& Part:Parts)if(Part)Part->DestroyComponent();Parts.Reset();
    AttackAltClip=nullptr;HitClip=nullptr;CastClips.Reset();ActionClip=nullptr;Contacts.Reset();ActionStartedAt=-100;ActionContact=ActionRelease=0;ActionWeight=1;
    bActionIsHit=false;bActionBasic=false;bReactions=false;LastHealth=-1;LastHitAt=-100;LastCooldowns.Reset();
    AttackClip=nullptr;RiderAttack=nullptr;RiderLocomotion=nullptr;SeatBone=NAME_None;NativeWalkRaw=NativeRunRaw=NativePhase=NativeIdleTime=0;SeenAttackSerial=0;AttackSeenAt=-100;
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
    if(Quad){UpdateQuad(Hero,Dt);return;} // pets
    if(Native){UpdateNative(Hero,Dt);return;} // new-champions
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

// ================================================================================== new-champions
namespace
{
UAnimSequence* Clip(const TSharedPtr<FJsonObject>& Object,const TCHAR* Role)
{
    FString Path;
    if(!Object||!Object->TryGetStringField(Role,Path)||!Path.StartsWith(TEXT("/Game/")))return nullptr;
    return LoadObject<UAnimSequence>(nullptr,*Path);
}
FTransform RefComponent(const USkeletalMesh& Mesh,FName Bone)
{
    const auto& Ref=Mesh.GetRefSkeleton();int32 I=Ref.FindBoneIndex(Bone);FTransform T=FTransform::Identity;
    while(I!=INDEX_NONE){T=T*Ref.GetRefBonePose()[I];I=Ref.GetParentIndex(I);}return T;
}
FName FindSeat(const USkeletalMesh& Mesh)
{
    // Quadruped rigs name the back differently: prefer a mid-torso bone, then the body/spine root.
    const auto& Ref=Mesh.GetRefSkeleton();
    for(const TCHAR* Name:{TEXT("Torso2"),TEXT("Torso1"),TEXT("Torso"),TEXT("Body"),TEXT("Spine_1"),TEXT("spine_02"),TEXT("Spine_0"),TEXT("spine_01"),TEXT("pelvis")})
        if(Ref.FindBoneIndex(Name)!=INDEX_NONE)return FName(Name);
    return NAME_None;
}
bool FacingYaw(const USkeletalMesh& Mesh,float& Out)
{
    const FVector LF=RefComponent(Mesh,TEXT("foot_l")).GetLocation(),LT=RefComponent(Mesh,TEXT("ball_l")).GetLocation();
    const FVector RF=RefComponent(Mesh,TEXT("foot_r")).GetLocation(),RT=RefComponent(Mesh,TEXT("ball_r")).GetLocation();
    const FVector Forward=((LT-LF)+(RT-RF)).GetSafeNormal2D();
    if(Forward.IsNearlyZero())return false;
    Out=-Forward.Rotation().Yaw;return FMath::IsFinite(Out);
}
}

bool UCireCreatureArt::ApplyBinding(ACireHero& Hero,const FString& Profile,const FString& Motion,const FString& MeshPath,float HeightCm,const TSharedPtr<FJsonObject>& Binding)
{
    Clear();if(!HandlesMotion(Motion)||!Binding.IsValid()||Hero.GetNetMode()==NM_DedicatedServer)return false;
    if(Motion==TEXT("quadruped_procedural"))return ApplyQuad(Hero,MeshPath,HeightCm,Binding); // pets: clip-less quadruped
    auto* Mesh=LoadObject<USkeletalMesh>(nullptr,*MeshPath);if(!Mesh)return false;
    const TSharedPtr<FJsonObject>* Animations=nullptr;if(!Binding->TryGetObjectField(TEXT("animations"),Animations))return false;
    UAnimSequence* Idle=Clip(*Animations,TEXT("idle"));UAnimSequence* Walk=Clip(*Animations,TEXT("walk"));UAnimSequence* Run=Clip(*Animations,TEXT("run"));
    if(!Idle||Idle->GetSkeleton()!=Mesh->GetSkeleton())return false;
    Kind=Motion;SourceAsset=Mesh;
    auto* Parent=Hero.GetMesh();Parent->SetAnimInstanceClass(nullptr);Parent->EmptyOverrideMaterials();
    const float Capsule=Hero.GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
    const auto Bounds=Mesh->GetImportedBounds();const float Height=static_cast<float>(Bounds.BoxExtent.Z*2);
    if(Height<1||Height>100000)return false;
    float Scale=HeightCm/Height;
    // fab-integration: vendor skeletal bounds are loose (physics asset); a measured head-height scale is exact.
    if(double Measured=0;Binding->TryGetNumberField(TEXT("meshScale"),Measured)&&FMath::IsFinite(Measured)&&Measured>.01&&Measured<100)Scale=static_cast<float>(Measured);
    MeshScale=Scale;
    double Yaw=-90;Binding->TryGetNumberField(TEXT("yaw"),Yaw);
    Parent->SetSkeletalMesh(Mesh);Parent->SetRelativeScale3D(FVector(Scale));
    BasePosition=FVector(0,0,-Capsule-(Bounds.Origin.Z-Bounds.BoxExtent.Z)*Scale);
    // fab-integration: the vendor packs pivot at the ground between the feet.
    if(bool bPivot=false;Binding->TryGetBoolField(TEXT("groundAtPivot"),bPivot)&&bPivot)BasePosition=FVector(0,0,-Capsule);
    Parent->SetRelativeLocation(BasePosition);Parent->SetRelativeRotation(FRotator(0,static_cast<float>(Yaw),0));
    Parent->SetAnimInstanceClass(UCireMonsterAnimInstance::StaticClass());
    auto* Anim=Cast<UCireMonsterAnimInstance>(Parent->GetAnimInstance());
    if(!Anim){Parent->SetSkeletalMesh(nullptr);Kind.Reset();return false;}
    bool bLockRoot=false;Binding->TryGetBoolField(TEXT("lockRoot"),bLockRoot);
    Anim->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);Anim->bLockRootToReference=bLockRoot||Motion==TEXT("mounted");
    Anim->Idle.Sequence=Idle;Anim->Idle.Weight=1.f;
    Anim->Walk.Sequence=Walk&&Walk->GetSkeleton()==Mesh->GetSkeleton()?Walk:nullptr;Anim->Walk.bRemoveDrift=Motion==TEXT("monster_native");
    Anim->Run.Sequence=Run&&Run->GetSkeleton()==Mesh->GetSkeleton()?Run:nullptr;Anim->Run.bRemoveDrift=Motion==TEXT("monster_native");
    const CireAnimClips::FClipInfo& Stance=CireAnimClips::Analyze(Idle);
    Anim->Walk.PelvisTarget=Anim->Run.PelvisTarget=Stance.bValid?Stance.DriftOffset:Stance.ReferencePelvis;
    Anim->Action=FCireAnimLayer();Anim->Death=FCireAnimLayer();Anim->MoveAlpha=Anim->RunAlpha=0.f;
    AttackClip=Clip(*Animations,TEXT("attack"));if(AttackClip&&AttackClip->GetSkeleton()!=Mesh->GetSkeleton())AttackClip=nullptr;
    DeathClip=Clip(*Animations,TEXT("death"));if(DeathClip&&DeathClip->GetSkeleton()!=Mesh->GetSkeleton())DeathClip=nullptr; // pets: the corpse stays down
    double Raw=0;if(Binding->TryGetNumberField(TEXT("walkSpeedRaw"),Raw))NativeWalkRaw=static_cast<float>(Raw);
    if(Binding->TryGetNumberField(TEXT("runSpeedRaw"),Raw))NativeRunRaw=static_cast<float>(Raw);
    Native=Parent;
    // Mount tint (the wolf reads as a dark sabercat).
    {
        const TSharedPtr<FJsonObject>* Tint=nullptr;const TArray<TSharedPtr<FJsonValue>>* B=nullptr;const TArray<TSharedPtr<FJsonValue>>* A=nullptr;
        if(Binding->TryGetObjectField(TEXT("tint"),Tint)&&(*Tint)->TryGetArrayField(TEXT("base"),B)&&(*Tint)->TryGetArrayField(TEXT("accent"),A)&&B->Num()>=3&&A->Num()>=3)
        {
            const FLinearColor Base((*B)[0]->AsNumber(),(*B)[1]->AsNumber(),(*B)[2]->AsNumber()),Accent((*A)[0]->AsNumber(),(*A)[1]->AsNumber(),(*A)[2]->AsNumber());
            double Strength=.6;(*Tint)->TryGetNumberField(TEXT("strength"),Strength);
            if(!UCireChampionArt::TintBody(Parent,&Hero,Base,Accent,static_cast<float>(Strength),FLinearColor::Black))
                for(int32 I=0;I<Parent->GetNumMaterials();++I)if(auto* MID=Parent->CreateDynamicMaterialInstance(I))
                { MID->SetVectorParameterValue(TEXT("Tint"),Base);MID->SetVectorParameterValue(TEXT("BaseColor"),Base);MID->SetVectorParameterValue(TEXT("Color"),Base); }
        }
    }
    const TArray<TSharedPtr<FJsonValue>>* PropList=nullptr;
    if(Binding->TryGetArrayField(TEXT("props"),PropList))AttachProps(Hero,Parent,PropList,Scale);
    // fab-integration: champion bodies from ChampionArtBindings.fab.json react like the Fab humanoids: alternating
    // strikes, a clip per cast (skill id, then shout/spell/ability), an upper-body flinch and a held death pose.
    Binding->TryGetBoolField(TEXT("reactions"),bReactions);bReactions&=Motion==TEXT("monster_native");
    auto Same=[Mesh](UAnimSequence* S){return S&&S->GetSkeleton()==Mesh->GetSkeleton()?S:nullptr;};
    AttackAltClip=Same(Clip(*Animations,TEXT("attackAlt")));HitClip=Same(Clip(*Animations,TEXT("hit")));
    if(const TSharedPtr<FJsonObject>* Casts=nullptr;(*Animations)->TryGetObjectField(TEXT("casts"),Casts))
        for(const auto& Pair:(*Casts)->Values)if(UAnimSequence* S=Same(Clip(*Casts,*FString(Pair.Key.ToView()))))CastClips.Add(FString(Pair.Key.ToView()),S);
    // "contact": seconds into each clip (keyed like animations / casts) where the blow or the release lands.
    if(const TSharedPtr<FJsonObject>* Contact=nullptr;(*Animations)->TryGetObjectField(TEXT("contact"),Contact))
    {
        auto Put=[&](const FString& Key,const UAnimSequence* S){double V=0;if(S&&(*Contact)->TryGetNumberField(Key,V)&&FMath::IsFinite(V))Contacts.Add(S,FMath::Clamp(static_cast<float>(V),0.f,S->GetPlayLength()));};
        Put(TEXT("attack"),AttackClip);Put(TEXT("attackAlt"),AttackAltClip);
        for(const auto& Pair:CastClips)Put(Pair.Key,Pair.Value);
    }
    LastCooldowns=Hero.Cooldowns;LastHealth=Hero.Health;SeenAttackSerial=Hero.AttackSerial;
    // Leader-pose parts on the same skeleton (the Centaur's armour, mane and bow are separate skeletal meshes).
    if(const TArray<TSharedPtr<FJsonValue>>* PartList=nullptr;Binding->TryGetArrayField(TEXT("parts"),PartList))
        for(const auto& Value:*PartList)
        {
            FString PartPath;if(!Value->TryGetString(PartPath)||!PartPath.StartsWith(TEXT("/Game/"))||Parts.Num()>=8)continue;
            auto* PartMesh=LoadObject<USkeletalMesh>(nullptr,*PartPath,nullptr,LOAD_Quiet|LOAD_NoWarn);
            if(!PartMesh||PartMesh->GetSkeleton()!=Mesh->GetSkeleton())continue;
            auto* Part=NewObject<USkeletalMeshComponent>(&Hero);Hero.AddInstanceComponent(Part);
            Part->SetSkeletalMesh(PartMesh);Prepare(*Part);Part->SetCastShadow(true);Part->SetupAttachment(Parent);
            Part->RegisterComponent();Part->SetLeaderPoseComponent(Parent);Part->SetVisibility(Parent->IsVisible());Parts.Add(Part);
        }
    // "materialSwap": vendor variant materials (the Centaur's coat / hair colours) on the body and its parts, so the
    // champion reads apart from the same pack's monsters.
    if(const TSharedPtr<FJsonObject>* Swap=nullptr;Binding->TryGetObjectField(TEXT("materialSwap"),Swap))
    {
        TMap<FString,UMaterialInterface*> Map;
        for(const auto& Pair:(*Swap)->Values)
        {
            FString To;if(!Pair.Value->TryGetString(To)||!To.StartsWith(TEXT("/Game/")))continue;
            if(auto* M=LoadObject<UMaterialInterface>(nullptr,*To,nullptr,LOAD_Quiet|LOAD_NoWarn))Map.Add(FString(Pair.Key.ToView()),M);
        }
        TArray<USkeletalMeshComponent*> Targets={Parent};for(auto& Part:Parts)Targets.Add(Part);
        for(USkeletalMeshComponent* Target:Targets)
            for(int32 I=0;I<Target->GetNumMaterials();++I)
                if(UMaterialInterface* Current=Target->GetMaterial(I))if(auto* const* To=Map.Find(Current->GetPathName()))Target->SetMaterial(I,*To);
    }
    if(Motion==TEXT("mounted"))
    {
        const TSharedPtr<FJsonObject>* R=nullptr;FString RiderPath,LocoPath,AttackPath;double RiderHeight=170,SeatHeight=.62,SeatForward=0;
        if(!Binding->TryGetObjectField(TEXT("rider"),R)||!(*R)->TryGetStringField(TEXT("mesh"),RiderPath))return VisualMesh()!=nullptr;
        (*R)->TryGetStringField(TEXT("locomotion"),LocoPath);(*R)->TryGetStringField(TEXT("attack"),AttackPath);
        (*R)->TryGetNumberField(TEXT("heightCm"),RiderHeight);(*R)->TryGetNumberField(TEXT("seatHeight"),SeatHeight);(*R)->TryGetNumberField(TEXT("seatForward"),SeatForward);
        auto* RiderMesh=LoadObject<USkeletalMesh>(nullptr,*RiderPath);SeatBone=FindSeat(*Mesh);
        if(RiderMesh&&!SeatBone.IsNone())
        {
            Rider=NewObject<USkeletalMeshComponent>(&Hero,TEXT("MountedRider"));Hero.AddInstanceComponent(Rider);
            Rider->SetSkeletalMesh(RiderMesh);Prepare(*Rider);
            const auto RB=RiderMesh->GetImportedBounds();const float RScale=static_cast<float>(RiderHeight)/FMath::Max(1.f,static_cast<float>(RB.BoxExtent.Z*2));
            float RiderYaw=0;FacingYaw(*RiderMesh,RiderYaw);
            Rider->SetAnimInstanceClass(UCireCombatAnimInstance::StaticClass());
            Rider->RegisterComponent();
            // Seat: the rider's pelvis sits SeatHeight x mount height above the ground, over the seat bone (component space).
            const FTransform SeatRef=RefComponent(*Mesh,SeatBone);
            const FVector PelvisRef=RefComponent(*RiderMesh,TEXT("pelvis")).GetLocation()*RScale;
            const FVector SeatWorld=Parent->GetComponentTransform().TransformPosition(SeatRef.GetLocation());
            const FVector Ground=Hero.GetActorLocation()-FVector(0,0,Capsule);
            const FVector Forward=Hero.GetActorForwardVector();
            FVector Target=FVector(SeatWorld.X,SeatWorld.Y,Ground.Z+HeightCm*static_cast<float>(SeatHeight))+Forward*HeightCm*static_cast<float>(SeatForward);
            const FRotator RiderRotation(0,Hero.GetActorRotation().Yaw+RiderYaw,0);
            Rider->SetWorldScale3D(FVector(RScale));Rider->SetWorldRotation(RiderRotation);
            Rider->SetWorldLocation(Target-RiderRotation.RotateVector(PelvisRef));
            Rider->AttachToComponent(Parent,FAttachmentTransformRules::KeepWorldTransform,SeatBone);
            if(auto* Blend=LoadObject<UBlendSpace>(nullptr,*LocoPath);Blend&&Blend->GetSkeleton()==RiderMesh->GetSkeleton())
            {RiderLocomotion=Blend;if(auto* Single=Rider->GetSingleNodeInstance()){Single->SetAnimationAsset(Blend,true);Single->SetBlendSpacePosition(FVector::ZeroVector);Single->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);}}
            bool bRelax=false;(*R)->TryGetBoolField(TEXT("relaxArms"),bRelax);bRiderRelax=bRelax;
            RiderAttack=LoadObject<UAnimSequence>(nullptr,*AttackPath);if(RiderAttack&&RiderAttack->GetSkeleton()!=RiderMesh->GetSkeleton())RiderAttack=nullptr;
            const TArray<TSharedPtr<FJsonValue>>* RiderProps=nullptr;
            if((*R)->TryGetArrayField(TEXT("props"),RiderProps))AttachProps(Hero,Rider,RiderProps,RScale);
        }
    }
    Hero.CacheInitialMeshOffset(Parent->GetRelativeLocation(),Parent->GetRelativeRotation());
    return VisualMesh()!=nullptr;
}

void UCireCreatureArt::AttachProps(ACireHero& Hero,USkeletalMeshComponent* Body,const TArray<TSharedPtr<FJsonValue>>* List,float BodyScale)
{
    if(!Body||!List||!Body->GetSkeletalMeshAsset())return;
    const USkeletalMesh& Skeletal=*Body->GetSkeletalMeshAsset();
    for(const auto& Value:*List)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;FString Asset,BoneName;
        if(!Value->TryGetObject(O)||!(*O)->TryGetStringField(TEXT("asset"),Asset)||!(*O)->TryGetStringField(TEXT("bone"),BoneName))continue;
        const FName Bone(*BoneName);if(Skeletal.GetRefSkeleton().FindBoneIndex(Bone)==INDEX_NONE)continue;
        auto* PropMesh=LoadObject<UStaticMesh>(nullptr,*Asset,nullptr,LOAD_Quiet|LOAD_NoWarn);if(!PropMesh)continue;
        auto* Part=NewObject<UStaticMeshComponent>(&Hero);Hero.AddInstanceComponent(Part);
        Part->SetStaticMesh(PropMesh);Prepare(*Part);Part->SetCastShadow(true);Part->ComponentTags.AddUnique(TEXT("CireWeaponProp"));
        const CireGrip::FWeapon* Grip=(Bone==TEXT("hand_l")||Bone==TEXT("hand_r"))?CireGrip::FindWeapon(PropMesh):nullptr;
        const CireGrip::FPlacement Placement=Grip?CireGrip::Place(Skeletal,Bone,*Grip,1.f,BodyScale):CireGrip::FPlacement();
        if(Placement.bValid){Part->SetupAttachment(Body,Placement.Bone);Part->SetRelativeTransform(Placement.Relative);}
        else
        {
            // Holstered / belt props: an offset in real centimetres on the bone, props at world scale 1.
            FVector Offset=FVector::ZeroVector;FRotator Rotation=FRotator::ZeroRotator;
            const TArray<TSharedPtr<FJsonValue>>* V=nullptr;
            if((*O)->TryGetArrayField(TEXT("offsetCm"),V)&&V->Num()==3)Offset=FVector((*V)[0]->AsNumber(),(*V)[1]->AsNumber(),(*V)[2]->AsNumber());
            if((*O)->TryGetArrayField(TEXT("rotation"),V)&&V->Num()==3)Rotation=FRotator((*V)[0]->AsNumber(),(*V)[1]->AsNumber(),(*V)[2]->AsNumber());
            Part->SetupAttachment(Body,Bone);
            const FTransform BoneRef=RefComponent(Skeletal,Bone);
            const float BoneScale=FMath::Max(.0001f,static_cast<float>(BoneRef.GetScale3D().GetAbsMax())*BodyScale);
            Part->SetRelativeLocation(Offset/BoneScale);Part->SetRelativeRotation(Rotation);
        }
        Part->SetAbsolute(false,false,true);Part->SetWorldScale3D(FVector::OneVector);
        Part->RegisterComponent();Props.Add(Part);
    }
}

void UCireCreatureArt::UpdateNative(ACireHero& Hero,float Dt)
{
    auto* Anim=Native?Cast<UCireMonsterAnimInstance>(Native->GetAnimInstance()):nullptr;if(!Anim)return;
    const float Scale=static_cast<float>(Native->GetComponentScale().X);
    const float Speed=Hero.bDead?0.f:static_cast<float>(Hero.GetVelocity().Size2D());
    SmoothedSpeed=FMath::FInterpTo(SmoothedSpeed,Speed,Dt,10.f);AnimationTime+=Dt;
    UAnimSequence* WalkClip=Anim->Walk.Sequence;UAnimSequence* RunClip=Anim->Run.Sequence;
    const auto& WalkInfo=CireAnimClips::Analyze(WalkClip);const auto& RunInfo=CireAnimClips::Analyze(RunClip);
    float WalkSpeed=FMath::Max(20.f,WalkInfo.GroundSpeed()*Scale),RunSpeed=FMath::Max(WalkSpeed+50.f,RunInfo.GroundSpeed()*Scale);
    if(NativeWalkRaw>0){WalkSpeed=NativeWalkRaw*Scale;RunSpeed=FMath::Max(WalkSpeed+50.f,NativeRunRaw*Scale);}
    const float RunAlpha=RunClip?FMath::Clamp((SmoothedSpeed-WalkSpeed)/(RunSpeed-WalkSpeed),0.f,1.f):0.f;
    Anim->MoveAlpha=FMath::FInterpTo(Anim->MoveAlpha,FMath::Clamp(SmoothedSpeed/(WalkSpeed*.35f),0.f,1.f),Dt,8.f);Anim->RunAlpha=RunAlpha;
    const float WalkLength=WalkClip?WalkClip->GetPlayLength():1.f,RunLength=RunClip?RunClip->GetPlayLength():WalkLength;
    const float Cycle=FMath::Lerp(WalkLength,RunLength,RunAlpha),Natural=FMath::Lerp(WalkSpeed,RunSpeed,RunAlpha);
    if(Anim->MoveAlpha>.01f)NativePhase=FMath::Frac(NativePhase+Dt*FMath::Clamp(SmoothedSpeed/FMath::Max(1.f,Natural),.35f,2.2f)/FMath::Max(.1f,Cycle));
    if(WalkClip)Anim->Walk.Time=FMath::Frac(NativePhase+WalkInfo.LeftFootApexPhase)*WalkLength;
    if(RunClip)Anim->Run.Time=FMath::Frac(NativePhase+RunInfo.LeftFootApexPhase)*RunLength;
    if(UAnimSequence* Idle=Anim->Idle.Sequence){NativeIdleTime=FMath::Fmod(NativeIdleTime+Dt,FMath::Max(.01f,Idle->GetPlayLength()));Anim->Idle.Time=NativeIdleTime;}
    // Attack: every basic attack serial plays the body's attack clip (mount: the sabercat's swipe; gunblade: the aimed shot).
    const double Now=Hero.GetWorld()->GetTimeSeconds();
    if(bReactions){const auto* GS=Hero.GetWorld()->GetGameState();UpdateReactions(Hero,*Anim,Dt,GS?GS->GetServerWorldTimeSeconds():Now);for(auto& Part:Parts)if(Part)Part->SetOverlayMaterial(Native->GetOverlayMaterial());return;} // fab-integration
    if(Hero.AttackSerial!=SeenAttackSerial){SeenAttackSerial=Hero.AttackSerial;AttackSeenAt=Now;}
    const float Age=static_cast<float>(Now-AttackSeenAt),Window=.85f;
    if(AttackClip&&Age>=0&&Age<Window&&!Hero.bDead)
    {
        Anim->Action.Sequence=AttackClip;Anim->Action.Time=FMath::Clamp(Age/Window,0.f,1.f)*AttackClip->GetPlayLength();
        Anim->Action.Weight=FMath::SmoothStep(0.f,1.f,FMath::Min(Age/.08f,(Window-Age)/.2f));Anim->Action.LowerBody=Kind==TEXT("mounted")?0.f:(1.f-Anim->MoveAlpha);
    }
    else{Anim->Action.Weight=0.f;Anim->Action.Sequence=nullptr;}
    // pets: a fallen companion plays its death clip and stays down until revived.
    if(Hero.bDead&&DeathClip&&Hero.ChampionProfileId.StartsWith(TEXT("pet:"))){DeadAge+=Dt;Anim->Death.Sequence=DeathClip;Anim->Death.Time=FMath::Min(DeadAge,DeathClip->GetPlayLength()-.01f);Anim->Death.Weight=1.f;}
    else{DeadAge=0;Anim->Death.Weight=0.f;Anim->Death.Sequence=nullptr;}
    Native->SetOverlayMaterial(Native->GetOverlayMaterial());
    if(Rider)
    {
        if(auto* Combat=Cast<UCireCombatAnimInstance>(Rider->GetSingleNodeInstance()))
        {
            Combat->SeatWeight=1.f;Combat->RelaxArms=bRiderRelax?1.f:0.f;
            const FVector Lateral=FVector::CrossProduct(FVector::UpVector,Hero.GetActorForwardVector()).GetSafeNormal();
            Combat->MotionPitchAxis=Rider->GetComponentQuat().UnrotateVector(Lateral);
            Combat->SetPlaying(!Hero.bDead);
            Combat->AttackSequence=RiderAttack;
            const bool bAttacking=RiderAttack&&Age>=0&&Age<Window&&!Hero.bDead;
            Combat->AttackTime=RiderAttack?FMath::Clamp(Age/Window,0.f,1.f)*RiderAttack->GetPlayLength():0.f;
            Combat->AttackWeight=bAttacking?FMath::SmoothStep(0.f,1.f,FMath::Min(Age/.07f,(Window-Age)/.16f)):0.f;
            Combat->AttackLowerBody=0.f;
        }
        Rider->SetOverlayMaterial(Native->GetOverlayMaterial());
    }
}

#if !UE_BUILD_SHIPPING
bool UCireCreatureArt::RunFabChampionSmoke(UWorld* World)
{
    if(!World)return false;
    bool Pass=true;int32 Checks=0,Fab=0;
    auto Check=[&](bool Value,const FString& Why){++Checks;if(!Value){Pass=false;UE_LOG(LogTemp,Error,TEXT("CIRE_FAB_CREATURE_CHAMPIONS_FAIL %s"),*Why);}};
    const bool bForce=GCireForceTripoChampionArt;GCireForceTripoChampionArt=true;
    const auto* GS=World->GetGameState();
    auto ServerNow=[World,GS](){return GS?GS->GetServerWorldTimeSeconds():World->GetTimeSeconds();};
    struct FCase{const TCHAR* Profile;const TCHAR* Skill;int32 MinParts;};
    for(const FCase& Case:{FCase{TEXT("bear"),TEXT("war_cry"),0},FCase{TEXT("evergrove_centaur"),TEXT("restoring_light"),4}})
    {
        FString Mesh,Motion;bool bFab=false;
        Check(UCireChampionArt::EffectiveCreatureBinding(Case.Profile,Mesh,Motion,bFab),FString(Case.Profile)+TEXT(" has a creature binding"));
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Hero=World->SpawnActor<ACireHero>(FVector(600,-9000,9000),FRotator::ZeroRotator,Params);
        if(!Hero){Check(false,TEXT("fixture hero spawned"));continue;}
        Hero->SetActorTickEnabled(false);Hero->SetActorEnableCollision(false);Hero->GetCharacterMovement()->SetComponentTickEnabled(false);
        Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        Check(Hero->DraftProfile(Case.Profile)&&Hero->ChampionArt&&Hero->ChampionArt->DebugApply(*Hero),FString(Case.Profile)+TEXT(" creature art applies"));
        UCireCreatureArt* Art=Hero->ChampionArt?Hero->ChampionArt->GetCreature():nullptr;
        if(!Art||!bFab)
        {
            // Clean clone / -CireNoFab: the committed body, never the reaction path.
            Check(Art&&Art->VisualMesh()&&!Art->HasReactions(),FString(Case.Profile)+TEXT(" keeps its committed creature body without the packs"));
            Hero->Destroy();continue;
        }
        ++Fab;
        USkeletalMeshComponent* Body=Art->GetNativeBody();
        Check(Body&&Body==Hero->GetMesh()&&Body->GetSkeletalMeshAsset()&&Body->GetSkeletalMeshAsset()->GetPathName()==Mesh&&Art->HasReactions()&&Cast<UCireMonsterAnimInstance>(Body->GetAnimInstance()),
            FString(Case.Profile)+TEXT(" wears its Fab body on the native path: ")+Mesh);
        Check(Art->GetPartCount()>=Case.MinParts,FString::Printf(TEXT("%s leader-pose parts %d >= %d"),Case.Profile,Art->GetPartCount(),Case.MinParts));
        const float Dt=1.f/30;
        Hero->Skills={FString(Case.Skill)};Hero->Cooldowns={0.f};Art->Update(*Hero,Dt);
        // Flinch: a real health drop plays the hit clip.
        Hero->Health=Hero->MaxHealth*.6f;Art->Update(*Hero,Dt);
        const UAnimSequence* Hit=Art->GetActionClip();
        Check(Hit!=nullptr,FString(Case.Profile)+TEXT(" flinches on damage"));
        // Two basic attacks alternate the strikes (when the body has two).
        Hero->AttackDuration=.65f;Hero->AttackSerial+=1;Hero->AttackStartedServerTime=ServerNow();Art->Update(*Hero,Dt);
        const UAnimSequence* First=Art->GetActionClip();
        Hero->AttackSerial+=1;Hero->AttackStartedServerTime=ServerNow();Art->Update(*Hero,Dt);
        const UAnimSequence* Second=Art->GetActionClip();
        Check(First&&Second&&First!=Hit,FString(Case.Profile)+TEXT(" basic attacks play strike clips"));
        // The strike ends with the replicated attack window.
        Hero->AttackStartedServerTime=ServerNow()-4.;Art->Update(*Hero,Dt);
        Check(Art->GetActionClip()==nullptr,FString(Case.Profile)+TEXT(" strike ends and returns to the gait"));
        // A cooldown starting plays that skill's cast clip.
        Hero->Cooldowns={12.f};Art->Update(*Hero,Dt);
        Check(Art->GetActionClip()!=nullptr,FString(Case.Profile)+TEXT(" casts ")+Case.Skill);
        // Death holds the death clip; the respawn clears it.
        auto* Anim=Cast<UCireMonsterAnimInstance>(Body->GetAnimInstance());
        Hero->bDead=true;for(int32 I=0;I<6;++I)Art->Update(*Hero,Dt);
        Check(Anim&&Anim->Death.Sequence&&Anim->Death.Weight>.5f&&Anim->Action.Weight==0.f,FString(Case.Profile)+TEXT(" plays and holds its death clip"));
        Hero->bDead=false;Hero->Health=Hero->MaxHealth;Art->Update(*Hero,Dt);
        Check(Anim&&Anim->Death.Weight==0.f,FString(Case.Profile)+TEXT(" stands again after the respawn"));
        Hero->Destroy();
    }
    GCireForceTripoChampionArt=bForce;
    UE_LOG(LogTemp,Display,TEXT("CIRE_FAB_CREATURE_CHAMPIONS_%s checks=%d fabBodies=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Checks,Fab);
    return Pass;
}
#endif

// ================================================================================ fab-integration
void UCireCreatureArt::StartAction(UAnimSequence* Clip,double StartedAt,float Release,float Weight,bool bHit)
{
    if(!Clip)return;
    ActionClip=Clip;ActionStartedAt=StartedAt;ActionWeight=Weight;bActionIsHit=bHit;
    const float Length=Clip->GetPlayLength();const float* Contact=Contacts.Find(Clip);
    // Unmeasured clips land their blow a third of the way in; a hit reaction simply plays from its start.
    ActionContact=bHit?0.f:FMath::Clamp(Contact?*Contact:Length*.35f,0.f,Length);
    ActionRelease=bHit?0.f:FMath::Max(0.f,Release);
}

void UCireCreatureArt::UpdateReactions(ACireHero& Hero,UCireMonsterAnimInstance& Anim,float Dt,double Now)
{
    // Basic attack: alternate the two strikes; the contact frame meets the server's release
    // (0.25 of the 0.65 s authored attack, scaled by attack speed, like the humanoid champions).
    if(Hero.AttackSerial!=SeenAttackSerial)
    {
        SeenAttackSerial=Hero.AttackSerial;
        UAnimSequence* Strike=(AttackAltClip&&(Hero.AttackSerial&1))?AttackAltClip.Get():AttackClip.Get();
        const double Started=Hero.AttackStartedServerTime>Now-1.?Hero.AttackStartedServerTime:Now;
        StartAction(Strike,Started,FMath::Max(.05f,Hero.AttackDuration)*(.25f/.65f),1.f,false);bActionBasic=true;
    }
    // The strike follows the replicated attack start (a corrected server time moves it, as for the humanoids).
    else if(bActionBasic&&ActionClip&&Hero.AttackStartedServerTime>Now-5.)ActionStartedAt=Hero.AttackStartedServerTime;
    // Casts: a cooldown starting means the server accepted that slot. Skill id first, then shout / spell / ability.
    for(int32 Slot=0;Slot<Hero.Cooldowns.Num();++Slot)
    {
        const float Previous=LastCooldowns.IsValidIndex(Slot)?LastCooldowns[Slot]:0.f;
        if(Hero.Cooldowns[Slot]>Previous+.5f&&Hero.Skills.IsValidIndex(Slot))
        {
            const FString& Skill=Hero.Skills[Slot];const FString SkillKind=CireChampionActions::SkillKind(Skill);
            const TObjectPtr<UAnimSequence>* Found=CastClips.Find(Skill);
            if(!Found)Found=CastClips.Find(SkillKind);
            if(!Found)Found=CastClips.Find(TEXT("ability"));
            UAnimSequence* Cast=Found?Found->Get():AttackAltClip?AttackAltClip.Get():AttackClip.Get();
            StartAction(Cast,Now,FMath::Min(.6f,CireChampionActions::SkillWindup(Hero.GetWorld(),Skill)),1.f,false);bActionBasic=false;
        }
    }
    LastCooldowns=Hero.Cooldowns;
    // Flinch: health dropped by at least 1.5% while nothing plays, at most once per 0.9 s.
    if(!Hero.bDead&&HitClip&&!ActionClip&&LastHealth>=0&&Hero.Health<LastHealth-.015f*FMath::Max(1.f,Hero.MaxHealth)&&Now-LastHitAt>.9)
    {StartAction(HitClip,Now,0.f,.7f,true);bActionBasic=false;LastHitAt=Now;}
    LastHealth=Hero.Health;
    // Death: the clip plays once and holds its last frame until the respawn.
    if(Hero.bDead&&DeathClip)
    {
        DeadAge+=Dt;ActionClip=nullptr;Anim.Action.Weight=0.f;Anim.Action.Sequence=nullptr;
        Anim.Death.Sequence=DeathClip;Anim.Death.Time=FMath::Min(DeadAge,DeathClip->GetPlayLength()-.01f);Anim.Death.Weight=FMath::SmoothStep(0.f,1.f,DeadAge/.12f);
        return;
    }
    DeadAge=0;Anim.Death.Weight=0.f;Anim.Death.Sequence=nullptr;
    if(Hero.bDead)ActionClip=nullptr;
    if(!ActionClip){Anim.Action.Weight=0.f;Anim.Action.Sequence=nullptr;return;}
    const float Length=ActionClip->GetPlayLength(),Age=static_cast<float>(Now-ActionStartedAt);
    // Fast releases skip the slow start of the wind-up (at most 2x speed) instead of racing through it.
    const float From=ActionRelease>0?FMath::Max(0.f,ActionContact-ActionRelease*2.f):ActionContact;
    const float Time=Age<ActionRelease?FMath::Lerp(From,ActionContact,FMath::Max(0.f,Age)/ActionRelease):ActionContact+(Age-ActionRelease);
    if(Time>=Length){ActionClip=nullptr;Anim.Action.Weight=0.f;Anim.Action.Sequence=nullptr;return;}
    const float Weight=FMath::Min(FMath::SmoothStep(0.f,1.f,FMath::Max(0.f,Age)/.08f),FMath::SmoothStep(0.f,1.f,(Length-Time)/.25f));
    Anim.Action.Sequence=ActionClip;Anim.Action.Time=FMath::Clamp(Time,0.f,Length);
    // A flinch while moving reads on the body without freezing the gait.
    Anim.Action.Weight=Weight*ActionWeight*(bActionIsHit?1.f-.6f*Anim.MoveAlpha:1.f);Anim.Action.LowerBody=1.f-Anim.MoveAlpha;
}

// ========================================================================================== pets
namespace
{
struct FQuadProxy : FAnimInstanceProxy
{
    explicit FQuadProxy(UAnimInstance* In) : FAnimInstanceProxy(In) {}
    FCireQuadRig Rig;
    float Phase = 0, Stride = 0, Amplitude = 0, Attack = -1, Time = 0, Air = 0, Dead = 0;
    virtual void PreUpdate(UAnimInstance* Instance, float Delta) override
    {
        FAnimInstanceProxy::PreUpdate(Instance, Delta);
        const auto* A = CastChecked<UCireQuadrupedAnimInstance>(Instance);
        Rig = A->Rig; Phase = A->Phase; Stride = A->Stride; Amplitude = A->Amplitude; Attack = A->Attack; Time = A->Time; Air = A->Air; Dead = A->Dead;
    }
    void Turn(FCompactPose& Pose, const FName& Bone, const FVector& Axis, float Degrees)
    {
        if (!Bone.IsNone() && FMath::Abs(Degrees) > .01f && FMath::IsFinite(Degrees)) RotateBone(Pose, *Bone.ToString(), Axis, Degrees);
    }
    virtual bool Evaluate(FPoseContext& Output) override
    {
        Output.ResetToRefPose();
        if (!Rig.bValid) return true;
        auto& Pose = Output.Pose;
        const FVector L = Rig.Lateral, Up(0, 0, 1), F = Rig.Forward;
        const float S = Rig.SwingSign;
        const float Ground = 1.f - Air, Alive = 1.f - Dead;
        const float Cycle = Phase / (2 * PI);
        const float Pulse = AttackPulse(Attack) * Alive;
        // Diagonal trot: front-left with rear-right. Stance legs sweep back linearly (planted paws).
        const float Offsets[4] = {0.f, .5f, .5f, 0.f};
        float LowestCos = 1.f;
        for (int32 Leg = 0; Leg < 4; ++Leg)
        {
            const bool bFront = Leg < 2;
            float Angle, Lift;
            GaitSample(Cycle + Offsets[Leg], Amplitude * Ground * Alive, Angle, Lift);
            Lift *= Stride * Ground * Alive;
            LowestCos = FMath::Min(LowestCos, FMath::Cos(FMath::DegreesToRadians(Angle)));
            // Leap: front legs reach forward, rear legs trail. Lunge: front legs rise, body coils.
            const float Reach = Air * (bFront ? 28.f : -24.f) + (bFront ? -Pulse * 32.f : Pulse * 10.f);
            // Corpse: legs go slack.
            Turn(Pose, Rig.Legs[Leg][0], L, S * (Angle + Reach + Dead * (bFront ? 18.f : -12.f)));
            Turn(Pose, Rig.Legs[Leg][1], L, S * (bFront ? -1.f : 1.f) * (Lift * 32.f + Air * 18.f + Pulse * (bFront ? 26.f : 0.f)));
            Turn(Pose, Rig.Legs[Leg][2], L, S * (bFront ? 1.f : -1.f) * (Lift * 22.f + Air * 10.f));
        }
        // Spine: stride flex, breathing at rest, coil on the lunge.
        for (int32 I = 0; I < Rig.Spine.Num(); ++I)
            Turn(Pose, Rig.Spine[I], L, S * (FMath::Sin(Phase * 2.f) * Stride * 1.5f + FMath::Sin(Time * 1.7f) * .7f * (1 - Stride) - Pulse * 4.f) * Alive);
        // Neck and head: nod with the gait, look around at rest, strike on the lunge.
        for (int32 I = 0; I < Rig.Neck.Num(); ++I)
        {
            const float W = (I + 1.f) / FMath::Max(1, Rig.Neck.Num());
            Turn(Pose, Rig.Neck[I], L, S * W * (FMath::Sin(Phase * 2.f + .6f) * Stride * 3.f + FMath::Sin(Time * 1.3f) * 1.5f * (1 - Stride) + Pulse * 14.f + Dead * 20.f));
            Turn(Pose, Rig.Neck[I], Up, W * FMath::Sin(Time * .45f) * 9.f * (1 - Stride) * Alive);
        }
        // Tail: a lazy sway that stiffens at a run.
        for (int32 I = 0; I < Rig.Tail.Num(); ++I)
        {
            const float W = (I + 1.f) / FMath::Max(1, Rig.Tail.Num());
            Turn(Pose, Rig.Tail[I], Up, W * FMath::Sin(Time * 2.1f - I * .7f) * (10.f - 5.f * Stride) * Alive);
            Turn(Pose, Rig.Tail[I], L, S * W * (-6.f * Stride + Air * 10.f - Dead * 12.f));
        }
        const auto Root = BoneIndex(Pose, *Rig.Root.ToString());
        if (Root.IsValid())
        {
            // Planted paws: lower the body by the vertical shortening of a swung leg; a corpse rolls onto its side.
            Pose[Root].AddToTranslation(FVector(0, 0, -Rig.LegUnits * (1 - LowestCos) * Ground * Alive - Rig.LegUnits * .45f * Dead));
            if (Dead > 0) Pose[Root].SetRotation((FQuat(F.GetSafeNormal(), FMath::DegreesToRadians(84.f * Dead)) * Pose[Root].GetRotation()).GetNormalized());
        }
        return true;
    }
};
FTransform RefGlobal(const FReferenceSkeleton& Ref, int32 I)
{
    FTransform T = FTransform::Identity;
    while (I != INDEX_NONE) { T = T * Ref.GetRefBonePose()[I]; I = Ref.GetParentIndex(I); }
    return T;
}
}

FAnimInstanceProxy* UCireQuadrupedAnimInstance::CreateAnimInstanceProxy() { return new FQuadProxy(this); }

bool FCireQuadRig::Analyze(const USkeletalMesh& Mesh, float MeshYaw, FCireQuadRig& Out, FString* Why)
{
    Out = FCireQuadRig();
    auto Fail = [&](const FString& Reason) { if (Why) *Why = Reason; return false; };
    const FReferenceSkeleton& Ref = Mesh.GetRefSkeleton();
    const int32 N = Ref.GetNum();
    if (N < 8) return Fail(TEXT("too few bones for a quadruped"));
    TArray<FVector> Pos; Pos.SetNum(N);
    TArray<TArray<int32>> Children; Children.SetNum(N);
    float MinZ = TNumericLimits<float>::Max(), MaxZ = -TNumericLimits<float>::Max();
    for (int32 I = 0; I < N; ++I)
    {
        Pos[I] = RefGlobal(Ref, I).GetLocation();
        MinZ = FMath::Min(MinZ, static_cast<float>(Pos[I].Z)); MaxZ = FMath::Max(MaxZ, static_cast<float>(Pos[I].Z));
        if (Ref.GetParentIndex(I) != INDEX_NONE) Children[Ref.GetParentIndex(I)].Add(I);
    }
    const float Height = MaxZ - MinZ;
    if (Height <= KINDA_SMALL_NUMBER) return Fail(TEXT("flat skeleton"));
    // The mesh is yawed by MeshYaw on the pawn so that its forward becomes actor +X.
    Out.Forward = FRotator(0, -MeshYaw, 0).RotateVector(FVector::ForwardVector);
    Out.Lateral = FVector::CrossProduct(FVector::UpVector, Out.Forward).GetSafeNormal();
    auto IsAncestor = [&](int32 A, int32 B) { for (int32 I = B; I != INDEX_NONE; I = Ref.GetParentIndex(I)) if (I == A) return true; return false; };
    // Ground leaves (paws / toe tips).
    TArray<int32> Feet;
    for (int32 I = 0; I < N; ++I) if (Children[I].IsEmpty() && Pos[I].Z < MinZ + .22f * Height) Feet.Add(I);
    if (Feet.Num() < 2) return Fail(FString::Printf(TEXT("%d ground chains (need at least 2)"), Feet.Num()));
    // A significant child carries more than a single helper leaf (Tripo rigs add twin leaves at hips and shoulders).
    TArray<int32> Size; Size.Init(1, N);
    for (int32 I = N - 1; I >= 0; --I) if (Ref.GetParentIndex(I) != INDEX_NONE) Size[Ref.GetParentIndex(I)] += Size[I];
    auto Significant = [&](int32 Parent) { int32 Count = 0; for (int32 C : Children[Parent]) if (Size[C] >= 2 || Feet.Contains(C)) ++Count; return Count; };
    // Leg top: walk up from the paw while the parent has a single significant child (the hip / shoulder is the branch).
    TArray<int32> LegTops;
    for (int32 Foot : Feet)
    {
        int32 Top = Foot;
        while (Ref.GetParentIndex(Top) != INDEX_NONE && Significant(Ref.GetParentIndex(Top)) == 1) Top = Ref.GetParentIndex(Top);
        if (Ref.GetParentIndex(Top) != INDEX_NONE) LegTops.AddUnique(Top);
    }
    // Front / rear by the tops' positions along the facing; left / right by the lateral axis.
    float MinF = TNumericLimits<float>::Max(), MaxF = -TNumericLimits<float>::Max();
    for (int32 I = 0; I < N; ++I) { const float F = FVector::DotProduct(Pos[I], Out.Forward); MinF = FMath::Min(MinF, F); MaxF = FMath::Max(MaxF, F); }
    const float MidF = (MinF + MaxF) * .5f;
    // The head is the forward-most high leaf; its chain is never a leg.
    int32 HeadLeaf = INDEX_NONE; float Front = -TNumericLimits<float>::Max();
    for (int32 I = 0; I < N; ++I)
        if (Children[I].IsEmpty() && Pos[I].Z > MinZ + .4f * Height && FVector::DotProduct(Pos[I], Out.Forward) > Front) { Front = FVector::DotProduct(Pos[I], Out.Forward); HeadLeaf = I; }
    TArray<int32> Side[2]; // 0 front, 1 rear
    for (int32 Top : LegTops) Side[FVector::DotProduct(Pos[Top], Out.Forward) > MidF ? 0 : 1].Add(Top);
    // A rig can miss a leg's lower bones (the Tripo sabercat's front right leg stops at the shoulder):
    // take the found leg's mirror sibling under the same branch as the short leg.
    for (int32 S = 0; S < 2; ++S)
    {
        if (Side[S].Num() != 1) continue;
        const int32 Found = Side[S][0], Parent = Ref.GetParentIndex(Found);
        const float FoundSide = FVector::DotProduct(Pos[Found] - Pos[Parent], Out.Lateral);
        int32 Mirror = INDEX_NONE; float Best = TNumericLimits<float>::Max();
        for (int32 C : Children[Parent])
        {
            if (C == Found || Size[C] < 2 || (HeadLeaf != INDEX_NONE && IsAncestor(C, HeadLeaf))) continue;
            const float Lat = FVector::DotProduct(Pos[C] - Pos[Parent], Out.Lateral);
            const float Dz = FMath::Abs(static_cast<float>(Pos[C].Z - Pos[Found].Z));
            if (Lat * FoundSide >= 0 || Dz > .25f * Height) continue;
            const float Score = Dz + FMath::Abs(FMath::Abs(Lat) - FMath::Abs(FoundSide));
            if (Score < Best) { Best = Score; Mirror = C; }
        }
        if (Mirror != INDEX_NONE) Side[S].Add(Mirror);
    }
    if (Side[0].Num() != 2 || Side[1].Num() != 2) return Fail(FString::Printf(TEXT("legs front=%d rear=%d (need 2 + 2)"), Side[0].Num(), Side[1].Num()));
    int32 Tops4[4];
    for (int32 S = 0; S < 2; ++S)
    {
        const bool bFirstLeft = FVector::DotProduct(Pos[Side[S][0]], Out.Lateral) < FVector::DotProduct(Pos[Side[S][1]], Out.Lateral);
        Tops4[S * 2] = bFirstLeft ? Side[S][0] : Side[S][1];
        Tops4[S * 2 + 1] = bFirstLeft ? Side[S][1] : Side[S][0];
    }
    TSet<int32> LegBones;
    int32 GroundLegs = 0; Out.LegUnits = 0;
    for (int32 G = 0; G < 4; ++G)
    {
        // Down the leg: always the lowest significant child.
        TArray<int32> Path; int32 Bone = Tops4[G];
        while (Bone != INDEX_NONE && Path.Num() < 6)
        {
            Path.Add(Bone); int32 Next = INDEX_NONE;
            for (int32 C : Children[Bone]) if ((Size[C] >= 2 || Feet.Contains(C) || Children[Bone].Num() == 1) && (Next == INDEX_NONE || Pos[C].Z < Pos[Next].Z)) Next = C;
            Bone = Next;
        }
        Out.Legs[G][0] = Ref.GetBoneName(Path[0]);
        if (Path.Num() >= 3) { Out.Legs[G][1] = Ref.GetBoneName(Path[1]); Out.Legs[G][2] = Ref.GetBoneName(Path[2]); }
        else if (Path.Num() == 2) Out.Legs[G][1] = Ref.GetBoneName(Path[1]);
        for (int32 I = 0; I < N; ++I) if (IsAncestor(Tops4[G], I)) LegBones.Add(I);
        if (Feet.Contains(Path.Last())) { Out.LegUnits += static_cast<float>(Pos[Tops4[G]].Z - MinZ); ++GroundLegs; }
    }
    Out.LegUnits = GroundLegs ? Out.LegUnits / GroundLegs : 0.f;
    const int32 Tops[4] = {Tops4[0], Tops4[1], Tops4[2], Tops4[3]};
    auto Lca = [&](const TArray<int32>& Set)
    {
        int32 A = Set[0];
        for (int32 K = 1; K < Set.Num(); ++K) while (A != INDEX_NONE && !IsAncestor(A, Set[K])) A = Ref.GetParentIndex(A);
        return A;
    };
    FVector Centre = FVector::ZeroVector; for (int32 G = 0; G < 4; ++G) Centre += Pos[Tops[G]]; Centre /= 4.f;
    // Spine: hip branch (common parent of the rear legs) to shoulder branch (common parent of the front legs).
    const int32 Hips = Lca({Tops[2], Tops[3]}), Chest = Lca({Tops[0], Tops[1]});
    if (Hips == INDEX_NONE || Chest == INDEX_NONE) return Fail(TEXT("no hip or shoulder branch"));
    TArray<int32> SpinePath;
    if (IsAncestor(Hips, Chest)) for (int32 I = Chest; I != INDEX_NONE && I != Hips; I = Ref.GetParentIndex(I)) SpinePath.Insert(I, 0);
    else if (IsAncestor(Chest, Hips)) for (int32 I = Hips; I != INDEX_NONE && I != Chest; I = Ref.GetParentIndex(I)) SpinePath.Insert(I, 0);
    for (int32 I : SpinePath) if (Out.Spine.Num() < 4) Out.Spine.Add(Ref.GetBoneName(I));
    // Head: the forward-most free leaf; tail: the rear-most free leaf (each walked back to the body).
    int32 Head = INDEX_NONE, Tail = INDEX_NONE; float HeadF = -TNumericLimits<float>::Max(), Back = TNumericLimits<float>::Max();
    for (int32 I = 0; I < N; ++I)
    {
        if (!Children[I].IsEmpty() || LegBones.Contains(I)) continue;
        const float D = FVector::DotProduct(Pos[I] - Centre, Out.Forward);
        if (D > HeadF && Pos[I].Z > MinZ + .4f * Height) { HeadF = D; Head = I; }
        if (D < Back) { Back = D; Tail = I; }
    }
    auto Walk = [&](int32 Leaf, int32 Body, TArray<FName>& Into, int32 Max)
    {
        TArray<int32> Path;
        for (int32 I = Leaf; I != INDEX_NONE && I != Body && !SpinePath.Contains(I) && I != Hips && I != Chest; I = Ref.GetParentIndex(I)) Path.Insert(I, 0);
        for (int32 I : Path) if (Into.Num() < Max && !LegBones.Contains(I)) Into.Add(Ref.GetBoneName(I));
    };
    if (Head != INDEX_NONE && FVector::DotProduct(Pos[Head] - Pos[Chest], Out.Forward) > 0) Walk(Head, Chest, Out.Neck, 4);
    if (Tail != INDEX_NONE && FVector::DotProduct(Pos[Tail] - Pos[Hips], Out.Forward) < 0) Walk(Tail, Hips, Out.Tail, 4);
    Out.Root = Ref.GetBoneName(0);
    // A hanging leg (pointing down) must swing toward Forward for a positive angle.
    Out.SwingSign = FVector::DotProduct(FQuat(Out.Lateral, FMath::DegreesToRadians(10.f)).RotateVector(FVector(0, 0, -1)), Out.Forward) > 0 ? 1.f : -1.f;
    Out.bValid = Out.LegUnits > 1.f;
    if (!Out.bValid) return Fail(TEXT("legs too short"));
    if (Why) *Why = FString::Printf(TEXT("legs FL=%s FR=%s RL=%s RR=%s spine=%d neck=%d tail=%d leg=%.1f"), *Out.Legs[0][0].ToString(), *Out.Legs[1][0].ToString(),
        *Out.Legs[2][0].ToString(), *Out.Legs[3][0].ToString(), Out.Spine.Num(), Out.Neck.Num(), Out.Tail.Num(), Out.LegUnits);
    return true;
}

const FCireQuadRig* UCireCreatureArt::GetQuadRig() const
{
    const auto* Anim = Quad ? Cast<UCireQuadrupedAnimInstance>(Quad->GetAnimInstance()) : nullptr;
    return Anim ? &Anim->Rig : nullptr;
}

bool UCireCreatureArt::ApplyQuad(ACireHero& Hero, const FString& MeshPath, float HeightCm, const TSharedPtr<FJsonObject>& Binding)
{
    auto* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
    if (!Mesh) return false;
    double Yaw = -90; Binding->TryGetNumberField(TEXT("yaw"), Yaw);
    FCireQuadRig Rig; FString Why;
    if (!FCireQuadRig::Analyze(*Mesh, static_cast<float>(Yaw), Rig, &Why))
    { UE_LOG(LogTemp, Warning, TEXT("CIRE_PET_RIG_REJECTED %s: %s"), *MeshPath, *Why); return false; }
    UE_LOG(LogTemp, Log, TEXT("CIRE_PET_RIG %s: %s"), *MeshPath, *Why);
    auto* Parent = Hero.GetMesh(); Parent->SetAnimInstanceClass(nullptr); Parent->EmptyOverrideMaterials();
    const float Capsule = Hero.GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
    const auto Bounds = Mesh->GetImportedBounds(); const float Height = static_cast<float>(Bounds.BoxExtent.Z * 2);
    if (Height < 1 || Height > 100000) return false;
    const float Scale = HeightCm / Height; MeshScale = Scale; LegUnits = Rig.LegUnits;
    Parent->SetSkeletalMesh(Mesh); Parent->SetRelativeScale3D(FVector(Scale));
    BasePosition = FVector(0, 0, -Capsule - (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale);
    Parent->SetRelativeLocation(BasePosition); Parent->SetRelativeRotation(FRotator(0, static_cast<float>(Yaw), 0));
    Parent->SetAnimInstanceClass(UCireQuadrupedAnimInstance::StaticClass());
    auto* Anim = Cast<UCireQuadrupedAnimInstance>(Parent->GetAnimInstance());
    if (!Anim) { Parent->SetSkeletalMesh(nullptr); return false; }
    Anim->Rig = Rig;
    Kind = TEXT("quadruped_procedural"); SourceAsset = Mesh; Quad = Parent;
    Hero.CacheInitialMeshOffset(Parent->GetRelativeLocation(), Parent->GetRelativeRotation());
    return true;
}

void UCireCreatureArt::UpdateQuad(ACireHero& Hero, float Dt)
{
    auto* Anim = Quad ? Cast<UCireQuadrupedAnimInstance>(Quad->GetAnimInstance()) : nullptr;
    if (!Anim) return;
    const float Speed = Hero.bDead ? 0.f : static_cast<float>(Hero.GetVelocity().Size2D());
    SmoothedSpeed = FMath::FInterpTo(SmoothedSpeed, Speed, Dt, 10.f); AnimationTime += Dt;
    const float Yaw = static_cast<float>(Hero.GetActorRotation().Yaw);
    const float YawRate = bHasLastYaw && Dt > 0 ? FMath::Abs(FRotator::NormalizeAxis(Yaw - LastYaw)) / Dt : 0.f; LastYaw = Yaw; bHasLastYaw = true;
    const float LegCm = FMath::Max(10.f, LegUnits * MeshScale * static_cast<float>(Hero.GetActorScale3D().Z));
    const float Travel = Hero.bDead ? 0.f : FMath::Max(SmoothedSpeed, FMath::DegreesToRadians(FMath::Min(YawRate, 360.f)) * LegCm * .4f);
    const float Stride = FMath::Clamp(Travel / 140.f, 0.f, 1.f);
    // Walk ~16 degrees of hip swing, full gallop ~34.
    const float Amplitude = FMath::Lerp(16.f, 34.f, FMath::Clamp((Travel - 160.f) / 420.f, 0.f, 1.f)) * Stride;
    const float CycleCm = FMath::Max(20.f, 4.f * LegCm * FMath::Sin(FMath::DegreesToRadians(FMath::Max(Amplitude, 4.f))));
    Phase = FMath::Fmod(Phase + Dt * Travel / CycleCm * 2 * PI, 2 * PI);
    const auto* State = Hero.GetWorld()->GetGameState();
    const double Now = State ? State->GetServerWorldTimeSeconds() : Hero.GetWorld()->GetTimeSeconds();
    const float Elapsed = static_cast<float>(Now - Hero.AttackStartedServerTime);
    const float Window = FMath::Max(.35f, Hero.AttackDuration);
    DeadWeight = FMath::FInterpTo(DeadWeight, Hero.bDead ? 1.f : 0.f, Dt, Hero.bDead ? 5.f : 8.f);
    Anim->Phase = Phase; Anim->Stride = Stride; Anim->Amplitude = Amplitude; Anim->Time = AnimationTime;
    Anim->Attack = !Hero.bDead && Hero.AttackSerial > 0 && Elapsed >= 0 && Elapsed < Window ? Elapsed / Window : -1.f;
    Anim->Air = Hero.GetCharacterMovement()->IsFalling() ? 1.f : 0.f;
    Anim->Dead = DeadWeight;
}
