#include "CireChampionArt.h"
#include "CireGame.h"
#include "CireWeaponPresentation.h"
#include "CireCreatureArt.h"
#include "CireChampionActions.h" // creature-anim
#include "CireMonsterAnim.h" // creature-anim
#include "CireMobility.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSingleNodeInstanceProxy.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "Animation/BlendSpace.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h" // new-champions: body tint
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/CharacterMovementComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireChampionArt, Log, All);
bool GCireForceTripoChampionArt = false; // creature-anim

namespace
{
void ApplyMobilityPose(FCompactPose& Pose,float Air,float Roll,FVector PitchAxis)
{
    if(Air<=.001f && Roll<0)return;
    const auto& Bones=Pose.GetBoneContainer();
    const auto World=[&](FCompactPoseBoneIndex Bone)
    {
        FTransform T=Pose[Bone];for(Bone=Bones.GetParentBoneIndex(Bone);Bone.IsValid();Bone=Bones.GetParentBoneIndex(Bone))T*=Pose[Bone];return T;
    };
    const auto Index=[&](const TCHAR* Name)
    {
        const int32 I=Bones.GetReferenceSkeleton().FindBoneIndex(Name);return I==INDEX_NONE?FCompactPoseBoneIndex(INDEX_NONE):Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(I));
    };
    const auto Rotate=[&](const TCHAR* Name,float Degrees)
    {
        const auto Bone=Index(Name);if(!Bone.IsValid())return;
        FVector Axis=PitchAxis;const auto Parent=Bones.GetParentBoneIndex(Bone);if(Parent.IsValid())Axis=World(Parent).GetRotation().UnrotateVector(Axis);
        auto& T=Pose[Bone];T.SetRotation((FQuat(Axis.GetSafeNormal(),FMath::DegreesToRadians(Degrees))*T.GetRotation()).GetNormalized());
    };
    const float Fold=Roll>=0?FMath::Sin(PI*FMath::Clamp(Roll,0.f,1.f)):0.f;
    Rotate(TEXT("spine_01"),Air*7+Fold*22);Rotate(TEXT("spine_02"),Fold*20);
    Rotate(TEXT("thigh_l"),-Air*17-Fold*52);Rotate(TEXT("thigh_r"),-Air*12-Fold*52);
    Rotate(TEXT("calf_l"),Air*30+Fold*76);Rotate(TEXT("calf_r"),Air*24+Fold*76);
    Rotate(TEXT("upperarm_l"),-Air*15-Fold*35);Rotate(TEXT("upperarm_r"),-Air*15-Fold*35);
    if(Roll>=0)
    {
        // Rotate around the pelvis in pose space, preserving the cached mesh offset used by network smoothing.
        const auto Root=Index(TEXT("root")),Pelvis=Index(TEXT("pelvis"));
        if(Root.IsValid() && Pelvis.IsValid())
        {
            const FVector Pivot=World(Pelvis).GetLocation();const FQuat Turn(PitchAxis.GetSafeNormal(),2*PI*FMath::Clamp(Roll,0.f,1.f));
            FVector Shift(0,0,-18*Fold);double Lowest=TNumericLimits<double>::Max();
            for(const TCHAR* Name:{TEXT("head"),TEXT("foot_l"),TEXT("foot_r"),TEXT("hand_l"),TEXT("hand_r")})
            {
                const auto Sample=Index(Name);if(Sample.IsValid())Lowest=FMath::Min(Lowest,(Pivot+Turn.RotateVector(World(Sample).GetLocation()-Pivot)+Shift).Z);
            }
            // A tucked pose must not drive the head or hands into the floor at half-turn.
            Shift.Z+=FMath::Max(0.,8*static_cast<double>(Fold)-Lowest);
            auto& T=Pose[Root];T.SetLocation(Pivot+Turn.RotateVector(T.GetLocation()-Pivot)+Shift);
            T.SetRotation((Turn*T.GetRotation()).GetNormalized());
        }
    }
}
}

// new-champions: seated rider. Thighs swing forward and apart, knees bend down around the mount's back.
static void ApplySeatPose(FCompactPose& Pose,float Weight,FVector PitchAxis)
{
    const auto& Bones=Pose.GetBoneContainer();
    const auto World=[&](FCompactPoseBoneIndex Bone)
    {
        FTransform T=Pose[Bone];for(Bone=Bones.GetParentBoneIndex(Bone);Bone.IsValid();Bone=Bones.GetParentBoneIndex(Bone))T*=Pose[Bone];return T;
    };
    const auto Rotate=[&](const TCHAR* Name,FVector Axis,float Degrees)
    {
        const int32 I=Bones.GetReferenceSkeleton().FindBoneIndex(Name);if(I==INDEX_NONE)return;
        const auto Bone=Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(I));if(!Bone.IsValid())return;
        const auto Parent=Bones.GetParentBoneIndex(Bone);if(Parent.IsValid())Axis=World(Parent).GetRotation().UnrotateVector(Axis);
        auto& T=Pose[Bone];T.SetRotation((FQuat(Axis.GetSafeNormal(),FMath::DegreesToRadians(Degrees*Weight))*T.GetRotation()).GetNormalized());
    };
    const FVector Up(0,0,1);
    Rotate(TEXT("thigh_l"),PitchAxis,-78);Rotate(TEXT("thigh_r"),PitchAxis,-78);
    Rotate(TEXT("thigh_l"),Up,-14);Rotate(TEXT("thigh_r"),Up,14);
    Rotate(TEXT("calf_l"),PitchAxis,84);Rotate(TEXT("calf_r"),PitchAxis,84);
    Rotate(TEXT("spine_01"),PitchAxis,6);
}

// Game-thread values are copied during PreUpdate, then evaluated on the animation worker.
struct FCireCombatAnimProxy : public FAnimSingleNodeInstanceProxy
{
    explicit FCireCombatAnimProxy(UAnimInstance* Instance) : FAnimSingleNodeInstanceProxy(Instance) {}
    UAnimSequence* AttackSequence = nullptr;
    float AttackTime = 0.f;
    float AttackWeight = 0.f;
    float AttackLowerBody = 1.f; // creature-anim
    CireGrip::FHands Hands; // creature-anim
    float SpineTwist = 0.f; // creature-anim
    float AirWeight = 0.f, RollProgress = -1.f;
    FVector MotionPitchAxis=FVector(1,0,0);
    float SeatWeight = 0.f; // new-champions
    virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
    {
        FAnimSingleNodeInstanceProxy::PreUpdate(Instance, DeltaSeconds);
        const auto* Combat = CastChecked<UCireCombatAnimInstance>(Instance);
        AttackSequence = Combat->AttackSequence;
        AttackTime = Combat->AttackTime;
        AttackWeight = Combat->AttackWeight;
        AttackLowerBody = FMath::Clamp(Combat->AttackLowerBody, 0.f, 1.f); // creature-anim
        Hands = Combat->Hands; // creature-anim
        SpineTwist = Combat->SpineTwist; // creature-anim
        AirWeight=Combat->AirWeight;RollProgress=Combat->RollProgress;MotionPitchAxis=Combat->MotionPitchAxis;
        SeatWeight=Combat->SeatWeight; // new-champions
    }
    virtual bool Evaluate(FPoseContext& Output) override
    {
        const bool bEvaluated = FAnimSingleNodeInstanceProxy::Evaluate(Output);
        if (bEvaluated && AttackSequence && AttackWeight > KINDA_SMALL_NUMBER)
        {
            FPoseContext AttackPose(Output);
            FAnimationPoseData AttackData(AttackPose);
            AttackSequence->GetAnimationPose(AttackData, FAnimExtractContext(static_cast<double>(AttackTime), false));
            // creature-anim: per-bone blend so a champion can swing or cast while its legs keep walking.
            TArray<float> Weights;
            CireAnimClips::UpperBodyMask(Output.Pose, Weights);
            for (float& W : Weights) W = AttackWeight * (W + (1.f - W) * AttackLowerBody);
            FPoseContext Blended(Output);
            FAnimationPoseData OutputData(Output), BlendedData(Blended);
            FAnimationRuntime::BlendTwoPosesTogetherPerBone(OutputData, AttackData, Weights, BlendedData);
            Output.Pose.CopyBonesFrom(Blended.Pose);
            Output.Curve.CopyFrom(Blended.Curve);
        }
        if(bEvaluated)ApplyMobilityPose(Output.Pose,AirWeight,RollProgress,MotionPitchAxis);
        if(bEvaluated&&SeatWeight>0)ApplySeatPose(Output.Pose,SeatWeight,MotionPitchAxis); // new-champions: mounted rider
        if(bEvaluated)CireGrip::TwistSpine(Output.Pose,SpineTwist); // creature-anim: sweeping swings
        if(bEvaluated&&Hands.Any())CireGrip::Apply(Output.Pose,Hands); // creature-anim: grips
        return bEvaluated;
    }
};

FAnimInstanceProxy* UCireCombatAnimInstance::CreateAnimInstanceProxy()
{
    return new FCireCombatAnimProxy(this);
}

namespace
{
struct FChampionArtDefinition
{
    FString MeshPath;
    FString LocomotionPath;
    FString AttackPath;
    float HeightCm = 0;
    FString Motion;
    TSharedPtr<FJsonObject> Raw; // new-champions: the whole binding row (animations, props, rider, tint)
};
bool ReadColor(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,FLinearColor& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* A=nullptr;if(!O||!O->TryGetArrayField(Key,A)||A->Num()<3)return false;
    double C[3]={};for(int32 I=0;I<3;++I)if(!(*A)[I]->TryGetNumber(C[I])||!FMath::IsFinite(C[I])||C[I]<0||C[I]>8)return false;
    Out=FLinearColor(C[0],C[1],C[2],1);return true;
}

// Opt-in review assets: no live champion mapping changes without the launch flag.
const FChampionArtDefinition Definitions[] = {
    {TEXT("/Game/TripoModels/medieval_knight_armor_3d_model/medieval_knight_armor_3d_model.medieval_knight_armor_3d_model"),
     TEXT("/Game/Art/Characters/TripoRetarget/Preview02/Warden/Animations/BS_Idle_Walk_Run_Warden.BS_Idle_Walk_Run_Warden"),
     TEXT("/Game/Art/Characters/CombatPrototype01/Warden/A_Warden_Attack.A_Warden_Attack"), 184.f},
    {TEXT("/Game/TripoModels/armored_archer_3d_model/armored_archer_3d_model.armored_archer_3d_model"),
     TEXT("/Game/Art/Characters/TripoRetarget/Preview02/Ranger/Animations/BS_Idle_Walk_Run_Ranger.BS_Idle_Walk_Run_Ranger"),
     TEXT("/Game/Art/Characters/CombatPrototype01/Ranger/A_Ranger_Attack.A_Ranger_Attack"), 178.f},
    {TEXT("/Game/TripoModels/battlefield_healer_3d_model/battlefield_healer_3d_model.battlefield_healer_3d_model"),
     TEXT("/Game/Art/Characters/TripoRetarget/Preview02/Scholar/Animations/BS_Idle_Walk_Run_Scholar.BS_Idle_Walk_Run_Scholar"),
     TEXT("/Game/Art/Characters/CombatPrototype01/Scholar/A_Scholar_Attack.A_Scholar_Attack"), 176.f},
    // Lancer is a distinct class; its archer body is explicitly temporary prototype art.
    {TEXT("/Game/TripoModels/armored_archer_3d_model/armored_archer_3d_model.armored_archer_3d_model"),
     TEXT("/Game/Art/Characters/TripoRetarget/Preview02/Ranger/Animations/BS_Idle_Walk_Run_Ranger.BS_Idle_Walk_Run_Ranger"),
     TEXT("/Game/Art/Characters/CombatPrototype01/Lancer/A_Lancer_Attack.A_Lancer_Attack"), 182.f},
    // Summoner temporarily shares the saved Scholar rig while dedicated art is authored.
    {TEXT("/Game/TripoModels/battlefield_healer_3d_model/battlefield_healer_3d_model.battlefield_healer_3d_model"),
     TEXT("/Game/Art/Characters/TripoRetarget/Preview02/Scholar/Animations/BS_Idle_Walk_Run_Scholar.BS_Idle_Walk_Run_Scholar"),
     TEXT("/Game/Art/Characters/CombatPrototype01/Scholar/A_Scholar_Attack.A_Scholar_Attack"), 176.f}
};

bool ReviewEnabled()
{
    static const bool bEnabled = FParse::Param(FCommandLine::Get(), TEXT("CireTripoChampions"));
    return bEnabled || GCireForceTripoChampionArt; // creature-anim: native grip tests
}

const FChampionArtDefinition* ProfileArt(const FString& Id)
{
    static bool bLoaded=false;
    static TMap<FString,FChampionArtDefinition> Bindings;
    if(!bLoaded) {
        bLoaded=true;FString Json;TSharedPtr<FJsonObject> Root;
        const FString File=FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/ChampionArtBindings.json"));
        if(FFileHelper::LoadFileToString(Json,*File)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)&&Root) {
            double Version=0;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
            if(Root->TryGetNumberField(TEXT("schemaVersion"),Version)&&Version==1&&Root->TryGetArrayField(TEXT("bindings"),Rows))for(const auto& Row:*Rows) {
                const TSharedPtr<FJsonObject>* O=nullptr;
                if(!Row->TryGetObject(O)||!O||!O->IsValid())continue;
                FString Profile,Status;FChampionArtDefinition D;double Height=0;
                if((*O)->TryGetStringField(TEXT("profileId"),Profile)&&(*O)->TryGetStringField(TEXT("status"),Status)&&Status==TEXT("custom_ready")&&
                   (*O)->TryGetStringField(TEXT("motion"),D.Motion)&&(UCireCreatureArt::Handles(Profile)||UCireCreatureArt::HandlesMotion(D.Motion))&&
                   (*O)->TryGetStringField(TEXT("mesh"),D.MeshPath)&&(*O)->TryGetNumberField(TEXT("heightCm"),Height)&&
                   Height>=50&&Height<=400&&D.MeshPath.StartsWith(TEXT("/Game/")))
                {D.HeightCm=static_cast<float>(Height);D.Raw=*O;Bindings.Add(Profile,MoveTemp(D));continue;}
                if((*O)->TryGetStringField(TEXT("profileId"),Profile)&&(*O)->TryGetStringField(TEXT("status"),Status)&&Status==TEXT("ready")&&
                   (*O)->TryGetStringField(TEXT("mesh"),D.MeshPath)&&(*O)->TryGetStringField(TEXT("locomotion"),D.LocomotionPath)&&
                   (*O)->TryGetStringField(TEXT("attack"),D.AttackPath)&&(*O)->TryGetNumberField(TEXT("heightCm"),Height)&&
                   Height>=80&&Height<=400&&D.MeshPath.StartsWith(TEXT("/Game/"))&&D.LocomotionPath.StartsWith(TEXT("/Game/"))&&D.AttackPath.StartsWith(TEXT("/Game/"))) {
                    D.HeightCm=static_cast<float>(Height);D.Raw=*O;Bindings.Add(Profile,MoveTemp(D));
                }
            }
        }
    }
    return Bindings.Find(Id);
}

bool HasMatchingLocomotion(const USkeletalMesh* Mesh, const UBlendSpace* Blend)
{
    if (!Mesh || !Blend || !Mesh->GetSkeleton() || Blend->GetSkeleton() != Mesh->GetSkeleton() ||
        Blend->GetNumberOfBlendSamples() < 3) return false;
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const auto& Axis = Blend->GetBlendParameter(Index);
        if (!FMath::IsFinite(Axis.Min) || !FMath::IsFinite(Axis.Max) || Axis.Max <= Axis.Min) return false;
    }
    for (const FBlendSample& Sample : Blend->GetBlendSamples())
        if (!Sample.Animation || Sample.Animation->GetSkeleton() != Mesh->GetSkeleton() ||
            Sample.Animation->GetPlayLength() <= 0.f) return false;
    return true;
}

bool GetFacingYaw(const USkeletalMesh& Mesh, float& OutYaw)
{
    const auto& Skeleton = Mesh.GetRefSkeleton();
    const auto Position = [&Skeleton](const TCHAR* Name, FVector& OutPosition)
    {
        int32 Index = Skeleton.FindBoneIndex(FName(Name));
        if (Index == INDEX_NONE) return false;
        FTransform Transform = Skeleton.GetRefBonePose()[Index];
        while ((Index = Skeleton.GetParentIndex(Index)) != INDEX_NONE)
            Transform = Transform * Skeleton.GetRefBonePose()[Index];
        OutPosition = Transform.GetLocation();
        return !OutPosition.ContainsNaN();
    };
    FVector LeftFoot, LeftToe, RightFoot, RightToe;
    if (!Position(TEXT("foot_l"), LeftFoot) || !Position(TEXT("ball_l"), LeftToe) ||
        !Position(TEXT("foot_r"), RightFoot) || !Position(TEXT("ball_r"), RightToe)) return false;
    const FVector Forward = ((LeftToe - LeftFoot) + (RightToe - RightFoot)).GetSafeNormal2D();
    if (Forward.IsNearlyZero()) return false;
    // The imported skeleton's toes define its forward axis; actor forward is +X.
    OutYaw = -Forward.Rotation().Yaw;
    return FMath::IsFinite(OutYaw);
}
}

UCireChampionArt::UCireChampionArt()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(false);
}

UMeshComponent* UCireChampionArt::GetVisualMesh() const
{
    if(Creature && Creature->VisualMesh())return Creature->VisualMesh();
    const auto* Hero=Cast<ACireHero>(GetOwner());return Hero?Hero->GetMesh():nullptr;
}

void UCireChampionArt::CaptureFallback(ACireHero& Hero)
{
    if (bFallbackCaptured) return;
    auto* Mesh = Hero.GetMesh();
    FallbackMesh = Mesh->GetSkeletalMeshAsset();
    FallbackAnimClass = Mesh->GetAnimClass();
    FallbackAnimationMode = static_cast<uint8>(Mesh->GetAnimationMode());
    if (const auto* SingleNode = Mesh->GetSingleNodeInstance()) FallbackAnimation = SingleNode->GetAnimationAsset();
    FallbackTransform = Mesh->GetRelativeTransform();
    for (UMaterialInterface* Material : Mesh->GetMaterials()) FallbackMaterials.Add(Material);
    bFallbackCaptured = true;
}

void UCireChampionArt::RestoreFallback(ACireHero& Hero)
{
    if (!bFallbackCaptured || !FallbackMesh) return;
    if(!IsApplied() && Hero.GetMesh()->GetSkeletalMeshAsset()==FallbackMesh)return;
    if(Creature)Creature->Clear();
    auto* Mesh = Hero.GetMesh();
    UMaterialInterface* Overlay = Mesh->GetOverlayMaterial();
    Mesh->SetAnimInstanceClass(nullptr);
    Mesh->SetSkeletalMesh(FallbackMesh);
    Mesh->EmptyOverrideMaterials();
    for (int32 Index = 0; Index < FallbackMaterials.Num(); ++Index) Mesh->SetMaterial(Index, FallbackMaterials[Index]);
    Mesh->SetRelativeTransform(FallbackTransform);
    Hero.CacheInitialMeshOffset(Mesh->GetRelativeLocation(), Mesh->GetRelativeRotation());
    Mesh->SetAnimationMode(static_cast<EAnimationMode::Type>(FallbackAnimationMode));
    if (FallbackAnimClass) Mesh->SetAnimInstanceClass(FallbackAnimClass);
    else if (FallbackAnimation) Mesh->PlayAnimation(FallbackAnimation, true);
    Mesh->SetOverlayMaterial(Overlay);
    Locomotion = nullptr;
    AttackAnimation = nullptr;
    if (Weapons) Weapons->Clear();
    AppliedArchetype = INDEX_NONE;
    SmoothedSpeed = 0.f;
}

bool UCireChampionArt::Apply(ACireHero& Hero, int32 Archetype)
{
    if (Archetype < 0 || Archetype >= UE_ARRAY_COUNT(Definitions)) return false;
    const auto* Profile=ProfileArt(Hero.ChampionProfileId);
    if(IsCreatureProfile(Hero.ChampionProfileId))
    {
        CaptureFallback(Hero);
        if(!Creature){Creature=NewObject<UCireCreatureArt>(&Hero,TEXT("CreaturePresentation"));Creature->RegisterComponent();}
        if(Weapons)Weapons->Clear();
        const bool bBinding=Profile&&UCireCreatureArt::HandlesMotion(Profile->Motion); // new-champions: monster_native / mounted
        if(Profile && (bBinding?Creature->ApplyBinding(Hero,Hero.ChampionProfileId,Profile->Motion,Profile->MeshPath,Profile->HeightCm,Profile->Raw):
            Creature->Apply(Hero,Hero.ChampionProfileId,Profile->MeshPath,Profile->HeightCm)))
        {AppliedArchetype=Archetype;return true;}
        // Never silently substitute a humanoid for a creature whose body failed to load.
        Hero.GetMesh()->SetAnimInstanceClass(nullptr);Hero.GetMesh()->SetSkeletalMesh(nullptr);
        UE_LOG(LogCireChampionArt,Error,TEXT("Creature art unavailable for %s; humanoid fallback suppressed."),*Hero.ChampionProfileId);return false;
    }
    const auto& Definition = Profile?*Profile:Definitions[Archetype];
    auto* Body = LoadObject<USkeletalMesh>(nullptr, *Definition.MeshPath);
    auto* Blend = LoadObject<UBlendSpace>(nullptr, *Definition.LocomotionPath);
    if (!HasMatchingLocomotion(Body, Blend))
    {
        UE_LOG(LogCireChampionArt, Warning, TEXT("Keeping original hero art: missing or mismatched locomotion for archetype %d (%s)."), Archetype, *Definition.LocomotionPath);
        return false;
    }
    const FBoxSphereBounds Bounds = Body->GetImportedBounds();
    const double Height = Bounds.BoxExtent.Z * 2.0;
    if (!FMath::IsFinite(Height) || Height < 20.0 || Height > 1000.0) return false;
    const float Scale = Definition.HeightCm / static_cast<float>(Height);
    const double Bottom = Bounds.Origin.Z - Bounds.BoxExtent.Z;
    if (!FMath::IsFinite(Bottom)) return false;
    float FacingYaw = 0.f;
    if (!GetFacingYaw(*Body, FacingYaw))
    {
        UE_LOG(LogCireChampionArt, Warning, TEXT("Keeping original hero art: cannot establish foot-facing axis for %s."), *Body->GetName());
        return false;
    }

    CaptureFallback(Hero);
    auto* Mesh = Hero.GetMesh();
    UMaterialInterface* Overlay = Mesh->GetOverlayMaterial();
    // Clear the Manny animation instance before assigning an incompatible skeleton.
    Mesh->SetAnimInstanceClass(nullptr);
    Mesh->SetSkeletalMesh(Body);
    Mesh->EmptyOverrideMaterials();
    Mesh->SetRelativeScale3D(FVector(Scale));
    Mesh->SetRelativeLocation(FVector(0, 0, -Hero.GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() - Bottom * Scale));
    Mesh->SetRelativeRotation(FRotator(0, FacingYaw, 0));
    Hero.CacheInitialMeshOffset(Mesh->GetRelativeLocation(), Mesh->GetRelativeRotation());
    Mesh->SetAnimInstanceClass(UCireCombatAnimInstance::StaticClass());
    if (auto* Animation = Mesh->GetSingleNodeInstance()) Animation->SetAnimationAsset(Blend, true);
    Mesh->SetOverlayMaterial(Overlay);
    Locomotion = Blend;
    AttackAnimation = LoadObject<UAnimSequence>(nullptr, *Definition.AttackPath);
    if (AttackAnimation && AttackAnimation->GetSkeleton() != Body->GetSkeleton()) AttackAnimation = nullptr;
    LastAttackSerial = Hero.AttackSerial;
    if (!Weapons)
    {
        Weapons = NewObject<UCireWeaponPresentation>(&Hero, TEXT("PrototypeWeapons"));
        Weapons->RegisterComponent();
    }
    Weapons->Apply(Hero, Archetype);
    AppliedArchetype = Archetype;
    auto* SingleNode = Mesh->GetSingleNodeInstance();
    if (!SingleNode)
    {
        RestoreFallback(Hero);
        return false;
    }
    // The movement component remains authoritative; animation never drives the capsule.
    SingleNode->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
    SingleNode->SetBlendSpacePosition(FVector::ZeroVector);
    // new-champions: temporary bodies re-tinted toward the champion's palette (ChampionArtBindings "tint").
    if(Profile && Profile->Raw.IsValid())
    {
        const TSharedPtr<FJsonObject>* Tint=nullptr;FLinearColor Base,Accent,Rim=FLinearColor::Black;double Strength=.6;
        if(Profile->Raw->TryGetObjectField(TEXT("tint"),Tint)&&ReadColor(*Tint,TEXT("base"),Base)&&ReadColor(*Tint,TEXT("accent"),Accent))
        {(*Tint)->TryGetNumberField(TEXT("strength"),Strength);ReadColor(*Tint,TEXT("rim"),Rim);TintBody(Mesh,&Hero,Base,Accent,FMath::Clamp(static_cast<float>(Strength),0.f,1.f),Rim);}
    }
    UE_LOG(LogCireChampionArt, Log, TEXT("Applied archetype %d: %s, height %.1fcm, scale %.4f, facing_yaw %.2f, matching locomotion %s"),
        Archetype, *Body->GetName(), Definition.HeightCm, Scale, FacingYaw, *Blend->GetName());
    return true;
}

void UCireChampionArt::UpdateVisuals(ACireHero& Hero, float DeltaSeconds)
{
    if (!ReviewEnabled() || Hero.GetNetMode() == NM_DedicatedServer) return;
    if (!Hero.bDrafted || Hero.Archetype < 0 || Hero.Archetype >= UE_ARRAY_COUNT(Definitions))
    {
        RestoreFallback(Hero);
        AttemptedArchetype = INDEX_NONE;
        AttemptedProfile.Reset();
        return;
    }
    if (AttemptedArchetype != Hero.Archetype || AttemptedProfile != Hero.ChampionProfileId)
    {
        RestoreFallback(Hero);
        AttemptedArchetype = Hero.Archetype;
        AttemptedProfile = Hero.ChampionProfileId;
        Apply(Hero, Hero.Archetype);
    }
    if(IsApplied() && IsCreatureProfile(Hero.ChampionProfileId))
    {if(Creature)Creature->Update(Hero,DeltaSeconds);return;}
    if (!IsApplied() || !Locomotion) return;
    auto* SingleNode = Hero.GetMesh()->GetSingleNodeInstance();
    if (!SingleNode || SingleNode->GetAnimationAsset() != Locomotion)
    {
        RestoreFallback(Hero);
        return;
    }
    const auto& DirectionAxis = Locomotion->GetBlendParameter(0);
    const auto& SpeedAxis = Locomotion->GetBlendParameter(1);
    const float Speed = Hero.bDead ? 0.f : static_cast<float>(Hero.GetVelocity().Size2D());
    SmoothedSpeed = Hero.bDead ? 0.f : FMath::FInterpTo(SmoothedSpeed, Speed, DeltaSeconds, 12.f);
    const FVector LocalVelocity = Hero.GetActorRotation().UnrotateVector(Hero.GetVelocity());
    const float Direction = Speed > 2.f ? FMath::RadiansToDegrees(FMath::Atan2(LocalVelocity.Y, LocalVelocity.X)) : 0.f;
    SingleNode->SetBlendSpacePosition(FVector(FMath::Clamp(Direction, DirectionAxis.Min, DirectionAxis.Max),
        FMath::Clamp(SmoothedSpeed, SpeedAxis.Min, SpeedAxis.Max), 0));
    SingleNode->SetPlaying(!Hero.bDead);
    if (auto* Combat = Cast<UCireCombatAnimInstance>(SingleNode))
    {
        const AGameStateBase* State = Hero.GetWorld()->GetGameState();
        const double ServerNow = State ? State->GetServerWorldTimeSeconds() : Hero.GetWorld()->GetTimeSeconds();
        const float Elapsed = static_cast<float>(ServerNow - Hero.AttackStartedServerTime);
        const float Duration = FMath::Max(.001f, Hero.AttackDuration);
        const float AuthoredElapsed = Elapsed * (.65f / Duration);
        const bool bAttacking = !Hero.bDead && Hero.AttackSerial > 0 && Elapsed >= 0.f && Elapsed < Duration;
        Combat->AttackSequence = AttackAnimation;
        Combat->AirWeight=FMath::FInterpTo(Combat->AirWeight,Hero.GetCharacterMovement()->IsFalling()?1.f:0.f,DeltaSeconds,10.f);
        Combat->RollProgress=Hero.Mobility&&Hero.Mobility->IsRolling()?Hero.Mobility->RollProgress():-1.f;
        const FVector RollDirection=Hero.Mobility&&Hero.Mobility->IsRolling()?Hero.Mobility->RollDirection:Hero.GetActorForwardVector();
        Combat->MotionPitchAxis=Hero.GetMesh()->GetComponentQuat().UnrotateVector(FVector::CrossProduct(FVector::UpVector,RollDirection).GetSafeNormal());
        Combat->AttackTime = AttackAnimation ? FMath::Clamp(Elapsed / Duration, 0.f, 1.f) * AttackAnimation->GetPlayLength() : 0.f;
        // Match the server's proportionally shortened release when attack speed increases.
        const float InWeight = FMath::Clamp(AuthoredElapsed / .07f, 0.f, 1.f);
        const float OutWeight = FMath::Clamp((.65f - AuthoredElapsed) / .16f, 0.f, 1.f);
        Combat->AttackWeight = bAttacking ? FMath::SmoothStep(0.f, 1.f, FMath::Min(InWeight, OutWeight)) : 0.f;
        Combat->AttackLowerBody = 1.f;
        Combat->SpineTwist = 0.f;
        // creature-anim: Tripo action clips (ChampionAttacks02) replace the prototype attack when the body has them.
        CireChampionActions::Apply(Hero, *Combat, DeltaSeconds, SmoothedSpeed);
        if (Hero.AttackSerial != LastAttackSerial)
        {
            LastAttackSerial = Hero.AttackSerial;
            UE_LOG(LogCireChampionArt, Verbose, TEXT("Prototype attack %u archetype %d age %.3f"), LastAttackSerial, Hero.Archetype, Elapsed);
        }
        if (Weapons) Weapons->Update(Hero, bAttacking ? AuthoredElapsed : -1.f);
        // creature-anim: hands close around the props; the off hand lets go of a two-hander during actions.
        if (Weapons)
        {
            Combat->Hands = Weapons->GripHands;
            Combat->Hands.TwoHandWeight = Weapons->GripHands.bTwoHand || Weapons->GripHands.bCarry ? 1.f - Combat->AttackWeight : 0.f;
        }
    }
}

// ------------------------------------------------------------------------------------ new-champions
bool UCireChampionArt::IsCreatureProfile(const FString& ProfileId)
{
    if(UCireCreatureArt::Handles(ProfileId))return true;
    const auto* Profile=ProfileArt(ProfileId);
    return Profile&&UCireCreatureArt::HandlesMotion(Profile->Motion);
}
bool UCireChampionArt::DebugApply(ACireHero& Hero)
{
    RestoreFallback(Hero);AttemptedArchetype=Hero.Archetype;AttemptedProfile=Hero.ChampionProfileId;
    return Apply(Hero,Hero.Archetype);
}
bool UCireChampionArt::TintBody(USkeletalMeshComponent* Mesh,UObject* Outer,FLinearColor Base,FLinearColor Accent,float Strength,FLinearColor Rim)
{
    if(!Mesh||!Outer)return false;
    UMaterialInterface* Skin=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/M_CireMonsterSkin.M_CireMonsterSkin"),nullptr,LOAD_Quiet|LOAD_NoWarn);
    if(!Skin)return false;
    static const FName Textures[]={TEXT("BaseColorTex"),TEXT("NormalTex"),TEXT("MetallicTex"),TEXT("RoughnessTex")};
    int32 Tinted=0;
    for(int32 I=0;I<Mesh->GetNumMaterials();++I)
    {
        UMaterialInterface* Current=Mesh->GetMaterial(I);
        auto* MID=Cast<UMaterialInstanceDynamic>(Current);
        if(!MID||MID->Parent!=Skin)
        {
            UTexture* Probe=nullptr;
            // Only bodies that carry the Tripo PBR set can be reskinned; others keep their authored look.
            if(!Current||!Current->GetTextureParameterValue(FHashedMaterialParameterInfo(Textures[0]),Probe)||!Probe)continue;
            MID=UMaterialInstanceDynamic::Create(Skin,Outer);if(!MID)continue;
            for(const FName& Name:Textures){UTexture* Texture=nullptr;if(Current->GetTextureParameterValue(FHashedMaterialParameterInfo(Name),Texture)&&Texture)MID->SetTextureParameterValue(Name,Texture);}
            Mesh->SetMaterial(I,MID);
        }
        MID->SetVectorParameterValue(TEXT("RaceTint"),Base);MID->SetScalarParameterValue(TEXT("RaceTintStrength"),Strength);
        MID->SetVectorParameterValue(TEXT("RaceAccent"),Accent);MID->SetScalarParameterValue(TEXT("RaceAccentStrength"),Strength*.8f);
        MID->SetVectorParameterValue(TEXT("RankColor"),Accent);MID->SetVectorParameterValue(TEXT("TrimColor"),Rim*.3f);
        MID->SetScalarParameterValue(TEXT("RankArmor"),0.f);MID->SetScalarParameterValue(TEXT("RankBody"),0.f);
        // A faint energy rim only: the skin's trim glow and fresnel wash a whole metallic body out at full strength.
        MID->SetScalarParameterValue(TEXT("RankGlow"),Rim.GetMax()>0?.08f:0.f);
        MID->SetVectorParameterValue(TEXT("RimColor"),Rim*.3f);MID->SetScalarParameterValue(TEXT("RimStrength"),Rim.GetMax()>0?.18f:0.f);
        ++Tinted;
    }
    return Tinted>0;
}
