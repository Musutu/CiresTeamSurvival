#include "CireChampionArt.h"
#include "CireFabAnimation.h" // fab-integration
#include "CireGame.h"
#include "CirePets.h" // pets
#include "CireSummon.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h" // champion-hq: summon bodies
#include "CireWeaponPresentation.h"
#include "CireCreatureArt.h"
#include "CireChampionActions.h" // creature-anim
#include "CireMonsterAnim.h" // creature-anim
#include "CireMobility.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h" // fab-integration: local-only pack presence
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

// new-champions: swing each upper arm from where the pose holds it toward "down at the side, slightly out and
// forward". Works in component space from the bone positions, so it needs no knowledge of the rig's axes.
static void ApplyRelaxedArms(FCompactPose& Pose,float Weight)
{
    if(Weight<=.001f)return;
    const auto& Bones=Pose.GetBoneContainer();
    const auto Index=[&](const TCHAR* Name){const int32 I=Bones.GetReferenceSkeleton().FindBoneIndex(Name);return I==INDEX_NONE?FCompactPoseBoneIndex(INDEX_NONE):Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(I));};
    const auto World=[&](FCompactPoseBoneIndex Bone){FTransform T=Pose[Bone];for(Bone=Bones.GetParentBoneIndex(Bone);Bone.IsValid();Bone=Bones.GetParentBoneIndex(Bone))T*=Pose[Bone];return T;};
    const auto Pelvis=Index(TEXT("pelvis")),Head=Index(TEXT("head"));
    if(!Pelvis.IsValid()||!Head.IsValid())return;
    const FVector Up=(World(Head).GetLocation()-World(Pelvis).GetLocation()).GetSafeNormal();
    if(Up.IsNearlyZero())return;
    for(const TCHAR* Side:{TEXT("_l"),TEXT("_r")})
    {
        const auto Upper=Index(*(FString(TEXT("upperarm"))+Side)),Lower=Index(*(FString(TEXT("lowerarm"))+Side));
        if(!Upper.IsValid()||!Lower.IsValid())continue;
        const FTransform UpperWorld=World(Upper);
        const FVector Dir=(World(Lower).GetLocation()-UpperWorld.GetLocation()).GetSafeNormal();
        if(Dir.IsNearlyZero())continue;
        // Only arms raised well above hanging are relaxed; a pose that already hangs is left alone.
        const float Hang=static_cast<float>(FVector::DotProduct(Dir,-Up));
        if(Hang>.6f)continue;
        const FVector Out=(Dir-Up*FVector::DotProduct(Dir,Up)).GetSafeNormal();
        const FVector Target=(-Up*.94f+Out*.32f).GetSafeNormal();
        const FQuat Swing=FQuat::Slerp(FQuat::Identity,FQuat::FindBetweenNormals(Dir,Target),FMath::Clamp(Weight,0.f,1.f));
        const auto Parent=Bones.GetParentBoneIndex(Upper);
        const FQuat ParentRot=Parent.IsValid()?World(Parent).GetRotation():FQuat::Identity;
        const FQuat NewWorld=Swing*UpperWorld.GetRotation();
        Pose[Upper].SetRotation((ParentRot.Inverse()*NewWorld).GetNormalized());
    }
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
    float RelaxArms = 0.f; // new-champions
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
        SeatWeight=Combat->SeatWeight; RelaxArms=Combat->RelaxArms; // new-champions
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
        if(bEvaluated&&RelaxArms>0)ApplyRelaxedArms(Output.Pose,RelaxArms*(1.f-FMath::Clamp(AttackWeight,0.f,1.f))); // new-champions: T-pose idles
        if(bEvaluated)CireGrip::TwistSpine(Output.Pose,SpineTwist); // creature-anim: sweeping swings
        if(bEvaluated&&Hands.Any())CireGrip::Apply(Output.Pose,Hands); // creature-anim: grips
        return bEvaluated;
    }
};

FAnimInstanceProxy* UCireCombatAnimInstance::CreateAnimInstanceProxy()
{
    return new FCireCombatAnimProxy(this);
}

struct FCireChampionArtDefinition
{
    FString MeshPath;
    FString LocomotionPath;
    FString AttackPath;
    float HeightCm = 0;
    FString Motion;
    TSharedPtr<FJsonObject> Raw; // new-champions: the whole binding row (animations, props, rider, tint)
};
namespace
{
using FChampionArtDefinition = FCireChampionArtDefinition;
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
// fab-integration: Content/Data/ChampionArtBindings.fab.json puts purchased Fab creature bodies (ROG Bear, the
// Quadruped Fantasy Centaur) on creature champions through the native monster path. The packs are licensed and never
// committed, so a row is used only when its mesh and every locomotion clip exist locally; otherwise, and with
// -CireNoFab / -CireNoFabCreatures, the committed binding (procedural bear, spatial-skin centaur) stays in charge.
const FChampionArtDefinition* FabProfileArt(const FString& Id)
{
    static bool bLoaded=false;
    static TMap<FString,FChampionArtDefinition> Bindings;
    if(!bLoaded)
    {
        bLoaded=true;
        if(FParse::Param(FCommandLine::Get(),TEXT("CireNoFab"))||FParse::Param(FCommandLine::Get(),TEXT("CireNoFabCreatures")))return nullptr;
        FString Json;TSharedPtr<FJsonObject> Root;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;double Version=0;
        auto Present=[](const FString& Path){const FString Package=FPackageName::ObjectPathToPackageName(Path);
            return Path.StartsWith(TEXT("/Game/"))&&FPackageName::IsValidLongPackageName(Package)&&FPackageName::DoesPackageExist(Package);};
        if(FFileHelper::LoadFileToString(Json,*FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/ChampionArtBindings.fab.json")))&&
           FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)&&Root&&Root->TryGetNumberField(TEXT("schemaVersion"),Version)&&Version==1&&
           Root->TryGetArrayField(TEXT("bindings"),Rows))
            for(const auto& Row:*Rows)
            {
                const TSharedPtr<FJsonObject>* O=nullptr;const TSharedPtr<FJsonObject>* Animations=nullptr;FString Profile,Status,Idle,Walk,Run;FChampionArtDefinition D;double Height=0;
                // paladin-hq: "humanoid" rows put a purchased humanoid body (leader mesh + leader-posed parts) on the
                // regular champion path: Fab locomotion BlendSpace, Fab action clips, weapons and grips. Every mesh
                // must exist locally, otherwise the committed Tripo body stays.
                if(Row->TryGetObject(O)&&(*O)->TryGetStringField(TEXT("profileId"),Profile)&&(*O)->TryGetStringField(TEXT("status"),Status)&&Status==TEXT("custom_ready")&&
                   (*O)->TryGetStringField(TEXT("motion"),D.Motion)&&D.Motion==TEXT("humanoid"))
                {
                    if(!(*O)->TryGetStringField(TEXT("mesh"),D.MeshPath)||!(*O)->TryGetStringField(TEXT("locomotion"),D.LocomotionPath)||
                       !(*O)->TryGetNumberField(TEXT("heightCm"),Height)||Height<80||Height>400||!Present(D.MeshPath)||!Present(D.LocomotionPath))continue;
                    bool bParts=true;const TArray<TSharedPtr<FJsonValue>>* Parts=nullptr;
                    if((*O)->TryGetArrayField(TEXT("parts"),Parts))for(const auto& Part:*Parts)
                    {
                        const TSharedPtr<FJsonObject>* PO=nullptr;FString PartMesh;
                        bParts&=Part->TryGetObject(PO)&&(*PO)->TryGetStringField(TEXT("mesh"),PartMesh)&&Present(PartMesh);
                    }
                    if(!bParts)continue;
                    (*O)->TryGetStringField(TEXT("attack"),D.AttackPath);
                    D.HeightCm=static_cast<float>(Height);D.Raw=*O;Bindings.Add(Profile,MoveTemp(D));continue;
                }
                O=nullptr;D=FChampionArtDefinition();Height=0;
                if(!Row->TryGetObject(O)||!(*O)->TryGetStringField(TEXT("profileId"),Profile)||!(*O)->TryGetStringField(TEXT("status"),Status)||Status!=TEXT("custom_ready")||
                   !(*O)->TryGetStringField(TEXT("motion"),D.Motion)||D.Motion!=TEXT("monster_native")||!(*O)->TryGetStringField(TEXT("mesh"),D.MeshPath)||
                   !(*O)->TryGetNumberField(TEXT("heightCm"),Height)||Height<50||Height>400||!(*O)->TryGetObjectField(TEXT("animations"),Animations)||
                   !(*Animations)->TryGetStringField(TEXT("idle"),Idle)||!(*Animations)->TryGetStringField(TEXT("walk"),Walk)||!(*Animations)->TryGetStringField(TEXT("run"),Run))continue;
                if(!Present(D.MeshPath)||!Present(Idle)||!Present(Walk)||!Present(Run))continue;
                D.HeightCm=static_cast<float>(Height);D.Raw=*O;Bindings.Add(Profile,MoveTemp(D));
            }
    }
    return Bindings.Find(Id);
}

const FChampionArtDefinition* ProfileArt(const FString& Id);
/** fab-integration: the committed binding, ignoring the Fab overlay (fallback when a Fab body fails to apply). */
const FChampionArtDefinition* BaseProfileArt(const FString& Id);

// champion-hq: summons (ACireSummon, not pets) look up "summon:<name slug>" (e.g. summon:oathbound_guardian) in
// SummonArt.json first; their gameplay profile (knight / scholar / ether_golem_tank) only picks the fallback body.
FString SummonArtKey(const ACireHero& Hero)
{
    if(!Hero.IsA<ACireSummon>()||Hero.ChampionProfileId.StartsWith(TEXT("pet:"))||Hero.HeroName.IsEmpty())return FString();
    return TEXT("summon:")+Hero.HeroName.ToLower().Replace(TEXT(" "),TEXT("_")).Replace(TEXT("-"),TEXT("_"));
}
const FChampionArtDefinition* BaseProfileArt(const FString& Id);
const FChampionArtDefinition* ArtFor(const ACireHero& Hero)
{
    const FString Key=SummonArtKey(Hero);
    if(!Key.IsEmpty())if(const FChampionArtDefinition* Summon=BaseProfileArt(Key))return Summon;
    return ProfileArt(Hero.ChampionProfileId);
}
FString ArtAttemptKey(const ACireHero& Hero){return Hero.ChampionProfileId+TEXT("|")+SummonArtKey(Hero);}

const FChampionArtDefinition* ProfileArt(const FString& Id)
{
    if(!Id.StartsWith(TEXT("pet:")))if(const auto* Fab=FabProfileArt(Id))return Fab; // fab-integration
    return BaseProfileArt(Id);
}

const FChampionArtDefinition* BaseProfileArt(const FString& Id)
{
    // pets: companion bodies come from Pets.json (key "pet:<id>"), never from the champion bindings.
    if(Id.StartsWith(TEXT("pet:")))
    {
        static TMap<FString,FChampionArtDefinition> PetBindings;
        if(const auto* Found=PetBindings.Find(Id))return Found;
        const FCirePetDef* Pet=CirePets::Find(FName(*Id.RightChop(4)));
        FChampionArtDefinition D;double Height=0;
        if(!Pet||!Pet->Art.IsValid()||!Pet->Art->TryGetStringField(TEXT("motion"),D.Motion)||!Pet->Art->TryGetStringField(TEXT("mesh"),D.MeshPath)||
           !Pet->Art->TryGetNumberField(TEXT("heightCm"),Height)||Height<20||Height>400)return nullptr;
        D.HeightCm=static_cast<float>(Height);D.Raw=Pet->Art;
        return &PetBindings.Add(Id,MoveTemp(D));
    }
    // champion-hq: summoned units (key "summon:<id>", set by ACireSummon / ACireMechTank) read Content/Data/SummonArt.json
    // rows with the ChampionArtBindings "ready" shape (mesh, locomotion, attack, heightCm, optional yaw / fallback).
    // No row: nullptr, and the summon keeps its archetype body (Definitions[] / Manny), exactly as before.
    if(Id.StartsWith(TEXT("summon:")))
    {
        static bool bSummonsLoaded=false;
        static TMap<FString,FChampionArtDefinition> SummonBindings;
        if(!bSummonsLoaded)
        {
            bSummonsLoaded=true;FString Json;TSharedPtr<FJsonObject> Root;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;double Version=0;
            if(FFileHelper::LoadFileToString(Json,*FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/SummonArt.json")))&&
               FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)&&Root&&Root->TryGetNumberField(TEXT("schemaVersion"),Version)&&Version==1&&
               Root->TryGetArrayField(TEXT("summons"),Rows))
                for(const auto& Row:*Rows)
                {
                    const TSharedPtr<FJsonObject>* O=nullptr;FString Key,Status;FChampionArtDefinition D;double Height=0;
                    if(!Row->TryGetObject(O)||!(*O)->TryGetStringField(TEXT("id"),Key)||!(*O)->TryGetStringField(TEXT("status"),Status)||Status!=TEXT("ready")||
                       !(*O)->TryGetStringField(TEXT("mesh"),D.MeshPath)||!(*O)->TryGetStringField(TEXT("locomotion"),D.LocomotionPath)||
                       !(*O)->TryGetStringField(TEXT("attack"),D.AttackPath)||!(*O)->TryGetNumberField(TEXT("heightCm"),Height)||Height<40||Height>500)continue;
                    D.HeightCm=static_cast<float>(Height);D.Raw=*O;SummonBindings.Add(TEXT("summon:")+Key,MoveTemp(D));
                }
        }
        return SummonBindings.Find(Id);
    }

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
    if(!IsApplied() && Hero.GetMesh()->GetSkeletalMeshAsset()==FallbackMesh && BodyParts.IsEmpty())return;
    if(Creature)Creature->Clear();
    ClearBodyParts(); // paladin-hq
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
    const auto* Profile=ArtFor(Hero); // champion-hq: summon bodies first
    if(IsCreatureProfile(Hero.ChampionProfileId))
    {
        CaptureFallback(Hero);
        if(!Creature){Creature=NewObject<UCireCreatureArt>(&Hero,TEXT("CreaturePresentation"));Creature->RegisterComponent();}
        if(Weapons)Weapons->Clear();
        const bool bBinding=Profile&&UCireCreatureArt::HandlesMotion(Profile->Motion); // new-champions: monster_native / mounted
        if(Profile && (bBinding?Creature->ApplyBinding(Hero,Hero.ChampionProfileId,Profile->Motion,Profile->MeshPath,Profile->HeightCm,Profile->Raw):
            Creature->Apply(Hero,Hero.ChampionProfileId,Profile->MeshPath,Profile->HeightCm)))
        {AppliedArchetype=Archetype;return true;}
        // fab-integration: a Fab body that fails to apply falls back to the committed creature binding.
        if(const auto* Base=BaseProfileArt(Hero.ChampionProfileId);Profile&&Base&&Base!=Profile)
        {
            UE_LOG(LogCireChampionArt,Warning,TEXT("%s: Fab creature body failed (%s); using the committed binding."),*Hero.ChampionProfileId,*Profile->MeshPath);
            Profile=Base;
            if(UCireCreatureArt::HandlesMotion(Base->Motion)?Creature->ApplyBinding(Hero,Hero.ChampionProfileId,Base->Motion,Base->MeshPath,Base->HeightCm,Base->Raw):
                Creature->Apply(Hero,Hero.ChampionProfileId,Base->MeshPath,Base->HeightCm))
            {AppliedArchetype=Archetype;return true;}
        }
        // pets: a binding may carry a "fallback" body (the procedural sabercat falls back to the animated wolf).
        const TSharedPtr<FJsonObject>* Fallback=nullptr;FString FallbackMotion,FallbackMeshPath;double FallbackHeight=0;
        if(Profile&&Profile->Raw.IsValid()&&Profile->Raw->TryGetObjectField(TEXT("fallback"),Fallback)&&(*Fallback)->TryGetStringField(TEXT("motion"),FallbackMotion)&&
           (*Fallback)->TryGetStringField(TEXT("mesh"),FallbackMeshPath)&&(*Fallback)->TryGetNumberField(TEXT("heightCm"),FallbackHeight)&&UCireCreatureArt::HandlesMotion(FallbackMotion)&&
           Creature->ApplyBinding(Hero,Hero.ChampionProfileId,FallbackMotion,FallbackMeshPath,static_cast<float>(FallbackHeight),*Fallback))
        {UE_LOG(LogCireChampionArt,Warning,TEXT("%s: primary creature body failed; using its fallback (%s)."),*Hero.ChampionProfileId,*FallbackMeshPath);AppliedArchetype=Archetype;return true;}
        // Never silently substitute a humanoid for a creature whose body failed to load.
        Hero.GetMesh()->SetAnimInstanceClass(nullptr);Hero.GetMesh()->SetSkeletalMesh(nullptr);
        UE_LOG(LogCireChampionArt,Error,TEXT("Creature art unavailable for %s; humanoid fallback suppressed."),*Hero.ChampionProfileId);return false;
    }
    // paladin-hq: a Fab humanoid body that fails to apply falls back to the committed binding (or the archetype art).
    if (Profile && Profile->Motion == TEXT("humanoid"))
    {
        if (ApplyHumanoid(Hero, Archetype, *Profile)) return true;
        UE_LOG(LogCireChampionArt, Warning, TEXT("%s: Fab humanoid body failed (%s); using the committed art."), *Hero.ChampionProfileId, *Profile->MeshPath);
        ClearBodyParts();
        const auto* Base = BaseProfileArt(Hero.ChampionProfileId);
        return ApplyHumanoid(Hero, Archetype, Base ? *Base : Definitions[Archetype]);
    }
    return ApplyHumanoid(Hero, Archetype, Profile ? *Profile : Definitions[Archetype]);
}

bool UCireChampionArt::ApplyHumanoid(ACireHero& Hero, int32 Archetype, const FChampionArtDefinition& Requested)
{
    const FChampionArtDefinition* Profile = Requested.Raw.IsValid() ? &Requested : nullptr;
    // champion-hq: a binding row may carry a "fallback" body (the previous art); it is used when the primary body,
    // its locomotion or its skeleton pairing is unavailable, so a missing HQ import never drops to the mannequin.
    FChampionArtDefinition FallbackDefinition;
    const FChampionArtDefinition* Chosen = &Requested;
    USkeletalMesh* Body = nullptr;
    UBlendSpace* Blend = nullptr;
    const auto Resolve = [&Body, &Blend](const FChampionArtDefinition& D)
    {
        const auto Exists = [](const FString& Path)
        {
            const FString Package = FPackageName::ObjectPathToPackageName(Path);
            return FPackageName::IsValidLongPackageName(Package) && FPackageName::DoesPackageExist(Package);
        };
        Body = Exists(D.MeshPath) ? LoadObject<USkeletalMesh>(nullptr, *D.MeshPath) : nullptr;
        Blend = Exists(D.LocomotionPath) ? LoadObject<UBlendSpace>(nullptr, *D.LocomotionPath) : nullptr;
        // fab-integration: locomotion retargeted from the Fab packs (true strafe/backpedal) when installed for this body.
        if (UBlendSpace* FabBlend = Body ? CireFabAnimation::Locomotion(Body, CireFabAnimation::FolderFor(Body)) : nullptr; FabBlend && HasMatchingLocomotion(Body, FabBlend)) Blend = FabBlend;
        return HasMatchingLocomotion(Body, Blend);
    };
    // champion-hq: -CireChampionHQOff forces every row's "fallback" body (before/after review captures).
    static const bool bHQOff = FParse::Param(FCommandLine::Get(), TEXT("CireChampionHQOff"));
    const bool bForceFallback = bHQOff && Profile && Profile->Raw.IsValid() && Profile->Raw->HasField(TEXT("fallback"));
    if (bForceFallback || !Resolve(*Chosen))
    {
        const TSharedPtr<FJsonObject>* Fallback = nullptr;
        double FallbackHeight = 0;
        if (Profile && Profile->Raw.IsValid() && Profile->Raw->TryGetObjectField(TEXT("fallback"), Fallback) &&
            (*Fallback)->TryGetStringField(TEXT("mesh"), FallbackDefinition.MeshPath) &&
            (*Fallback)->TryGetStringField(TEXT("locomotion"), FallbackDefinition.LocomotionPath) &&
            (*Fallback)->TryGetStringField(TEXT("attack"), FallbackDefinition.AttackPath) &&
            (*Fallback)->TryGetNumberField(TEXT("heightCm"), FallbackHeight) && FallbackHeight >= 80 && FallbackHeight <= 400)
        {
            FallbackDefinition.HeightCm = static_cast<float>(FallbackHeight);
            FallbackDefinition.Raw = *Fallback;
            if (Resolve(FallbackDefinition))
            {
                UE_LOG(LogCireChampionArt, Warning, TEXT("%s: primary body unavailable (%s); using its fallback body (%s)."),
                    *Hero.ChampionProfileId, *Chosen->MeshPath, *FallbackDefinition.MeshPath);
                Chosen = &FallbackDefinition;
            }
        }
        if (Chosen != &FallbackDefinition)
        {
            UE_LOG(LogCireChampionArt, Warning, TEXT("Keeping original hero art: missing or mismatched locomotion for archetype %d (%s)."), Archetype, *Chosen->LocomotionPath);
            return false;
        }
        Profile = Chosen; // relaxArms / tint follow the body actually in use
    }
    const auto& Definition = *Chosen;
    const bool bFabHumanoid = Definition.Motion == TEXT("humanoid"); // paladin-hq (of the body actually in use)
    const FBoxSphereBounds Bounds = Body->GetImportedBounds();
    const double Height = Bounds.BoxExtent.Z * 2.0;
    if (!FMath::IsFinite(Height) || Height < 20.0 || Height > 1000.0) return false;
    float Scale = Definition.HeightCm / static_cast<float>(Height);
    double Bottom = Bounds.Origin.Z - Bounds.BoxExtent.Z;
    // paladin-hq: a modular Fab body is authored in real centimetres: its leader's bounds miss the head and helmet
    // parts, so the row gives the mesh scale and the sole height explicitly.
    if (bFabHumanoid)
    {
        double RowScale = 1, Floor = Bottom;
        if (Definition.Raw->TryGetNumberField(TEXT("meshScale"), RowScale) && FMath::IsFinite(RowScale) && RowScale > .2 && RowScale < 5) Scale = static_cast<float>(RowScale);
        if (Definition.Raw->TryGetNumberField(TEXT("floorZ"), Floor) && FMath::IsFinite(Floor) && FMath::Abs(Floor) < 50) Bottom = Floor;
    }
    if (!FMath::IsFinite(Bottom)) return false;
    float FacingYaw = 0.f;
    double YawOverride = 0.0; // champion-hq: Tripo UE5-preset rigs face +Y; their toe bones do not give a clean forward axis
    if (Definition.Raw.IsValid() && Definition.Raw->TryGetNumberField(TEXT("yaw"), YawOverride) && FMath::IsFinite(YawOverride))
        FacingYaw = static_cast<float>(YawOverride);
    else if (!GetFacingYaw(*Body, FacingYaw))
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
    if (auto* Combat = Cast<UCireCombatAnimInstance>(Mesh->GetSingleNodeInstance()))
    {   // new-champions: bodies whose idle keeps the arms up (ChampionArtBindings "relaxArms").
        bool bRelax = false; if (Profile && Profile->Raw.IsValid()) Profile->Raw->TryGetBoolField(TEXT("relaxArms"), bRelax);
        Combat->RelaxArms = bRelax ? 1.f : 0.f;
    }
    Mesh->SetOverlayMaterial(Overlay);
    ApplyStaticParts(Hero, Definition.Raw); // champion-hq: segmented props (quiver)
    // paladin-hq: leader-posed parts (head, helmet, armour pieces) and the per-champion material identity.
    if (bFabHumanoid && !ApplyFabBody(Hero, Definition.Raw))
    {
        ClearBodyParts();
        RestoreFallback(Hero);
        return false;
    }
    Locomotion = Blend;
    AttackAnimation = Definition.AttackPath.IsEmpty() ? nullptr : LoadObject<UAnimSequence>(nullptr, *Definition.AttackPath);
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
    // pets: companions always wear their creature body (it is not an opt-in champion review asset).
    if ((!ReviewEnabled() && !Hero.ChampionProfileId.StartsWith(TEXT("pet:"))) || Hero.GetNetMode() == NM_DedicatedServer) return;
    if (!Hero.bDrafted || Hero.Archetype < 0 || Hero.Archetype >= UE_ARRAY_COUNT(Definitions))
    {
        RestoreFallback(Hero);
        AttemptedArchetype = INDEX_NONE;
        AttemptedProfile.Reset();
        return;
    }
    if (AttemptedArchetype != Hero.Archetype || AttemptedProfile != ArtAttemptKey(Hero))
    {
        RestoreFallback(Hero);
        AttemptedArchetype = Hero.Archetype;
        AttemptedProfile = ArtAttemptKey(Hero);
        Apply(Hero, Hero.Archetype);
    }
    if(IsApplied() && IsCreatureProfile(Hero.ChampionProfileId))
    {if(Creature)Creature->Update(Hero,DeltaSeconds);return;}
    if (!IsApplied() || !Locomotion) return;
    // paladin-hq: leader-posed parts share the leader's selection/rim overlay.
    for (USkeletalMeshComponent* Part : BodyParts)
        if (Part && Part->GetOverlayMaterial() != Hero.GetMesh()->GetOverlayMaterial()) Part->SetOverlayMaterial(Hero.GetMesh()->GetOverlayMaterial());
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
            // weapon-grips: a carried staff stays upright at the side while the other hand casts (the spell clips
            // swung it through the torso); only rolls and deaths let go of the carry.
            if (Weapons->GripHands.bCarry && CireChampionActions::MotionFor(Hero) == TEXT("cast"))
                Combat->Hands.CarryWeight = Hero.bDead || Combat->RollProgress >= 0.f || (Hero.Mobility && Hero.Mobility->IsRolling()) ? 0.f : 1.f;
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
bool UCireChampionArt::EffectiveCreatureBinding(const FString& ProfileId,FString& OutMesh,FString& OutMotion,bool& bOutFab)
{
    const auto* Profile=ProfileArt(ProfileId);
    if(!Profile)return false;
    OutMesh=Profile->MeshPath;OutMotion=Profile->Motion;bOutFab=!ProfileId.StartsWith(TEXT("pet:"))&&FabProfileArt(ProfileId)==Profile;
    return true;
}
bool UCireChampionArt::DebugApply(ACireHero& Hero)
{
    RestoreFallback(Hero);AttemptedArchetype=Hero.Archetype;AttemptedProfile=ArtAttemptKey(Hero);
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

// ------------------------------------------------------------------------------------ paladin-hq
namespace
{
bool ReadColor4(const TSharedPtr<FJsonValue>& V, FLinearColor& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
    if (!V.IsValid() || !V->TryGetArray(A) || A->Num() < 3 || A->Num() > 4) return false;
    double C[4] = {0, 0, 0, 1};
    for (int32 I = 0; I < A->Num(); ++I) if (!(*A)[I]->TryGetNumber(C[I]) || !FMath::IsFinite(C[I]) || C[I] < -1 || C[I] > 16) return false;
    Out = FLinearColor(C[0], C[1], C[2], C[3]); return true;
}
UMaterialInterface* LoadMaterialQuiet(const FString& Path)
{
    const FString Package = FPackageName::ObjectPathToPackageName(Path);
    if (!Path.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Package) || !FPackageName::DoesPackageExist(Package)) return nullptr;
    return LoadObject<UMaterialInterface>(nullptr, *Path, nullptr, LOAD_Quiet | LOAD_NoWarn);
}
}

int32 UCireChampionArt::ApplyMaterialSpec(UMeshComponent* Mesh, const TSharedPtr<FJsonObject>& Spec, UObject* Outer)
{
    if (!Mesh || !Spec.IsValid() || !Outer) return 0;
    int32 Changed = 0;
    for (const auto& Pair : Spec->Values)
    {
        const FString Key(Pair.Key.ToView());
        int32 Slot = Mesh->GetMaterialIndex(FName(*Key));
        if (Slot == INDEX_NONE && Key.IsNumeric()) Slot = FCString::Atoi(*Key);
        if (Slot < 0 || Slot >= Mesh->GetNumMaterials()) { UE_LOG(LogCireChampionArt, Warning, TEXT("Material slot %s not on %s"), *Key, *Mesh->GetName()); continue; }
        FString BasePath; const TSharedPtr<FJsonObject>* Row = nullptr;
        if (Pair.Value->TryGetString(BasePath)) { if (UMaterialInterface* M = LoadMaterialQuiet(BasePath)) { Mesh->SetMaterial(Slot, M); ++Changed; } continue; }
        if (!Pair.Value->TryGetObject(Row)) continue;
        UMaterialInterface* Base = Mesh->GetMaterial(Slot);
        if ((*Row)->TryGetStringField(TEXT("base"), BasePath)) if (UMaterialInterface* M = LoadMaterialQuiet(BasePath)) Base = M;
        if (!Base) continue;
        const TSharedPtr<FJsonObject>* Vectors = nullptr; const TSharedPtr<FJsonObject>* Scalars = nullptr;
        const bool bParams = (*Row)->TryGetObjectField(TEXT("vectors"), Vectors) | (*Row)->TryGetObjectField(TEXT("scalars"), Scalars);
        if (!bParams) { Mesh->SetMaterial(Slot, Base); ++Changed; continue; }
        UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Outer);
        if (!MID) continue;
        if (Vectors) for (const auto& V : (*Vectors)->Values) { FLinearColor C; if (ReadColor4(V.Value, C)) MID->SetVectorParameterValue(FName(V.Key.ToView()), C); }
        if (Scalars) for (const auto& V : (*Scalars)->Values) { double X = 0; if (V.Value->TryGetNumber(X) && FMath::IsFinite(X)) MID->SetScalarParameterValue(FName(V.Key.ToView()), static_cast<float>(X)); }
        Mesh->SetMaterial(Slot, MID); ++Changed;
    }
    return Changed;
}

void UCireChampionArt::ClearBodyParts()
{
    for (USkeletalMeshComponent* Part : BodyParts) if (Part) Part->DestroyComponent();
    BodyParts.Reset();
    for (UStaticMeshComponent* Part : StaticParts) if (Part) Part->DestroyComponent(); // champion-hq
    StaticParts.Reset();
}

// champion-hq: "staticParts" of an HQ row are props Tripo segmented off the body (the Ranger's quiver). Each row gives
// "mesh" (a static mesh authored in the body's own mesh space), "bone"; the part follows that bone from its bind pose.
// Optional "offsetCm" / "rotation" nudge it in bone space. A missing asset only drops that part.
void UCireChampionArt::ApplyStaticParts(ACireHero& Hero, const TSharedPtr<FJsonObject>& Raw)
{
    USkeletalMeshComponent* Leader = Hero.GetMesh();
    const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
    if (!Leader || !Leader->GetSkeletalMeshAsset() || !Raw.IsValid() || !Raw->TryGetArrayField(TEXT("staticParts"), Parts)) return;
    const FReferenceSkeleton& Ref = Leader->GetSkeletalMeshAsset()->GetRefSkeleton();
    for (const auto& Value : *Parts)
    {
        const TSharedPtr<FJsonObject>* Row = nullptr; FString Path, Bone;
        if (!Value->TryGetObject(Row) || !(*Row)->TryGetStringField(TEXT("mesh"), Path) || !(*Row)->TryGetStringField(TEXT("bone"), Bone)) continue;
        UStaticMesh* Asset = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_Quiet | LOAD_NoWarn);
        int32 Index = Ref.FindBoneIndex(FName(*Bone));
        if (!Asset || Index == INDEX_NONE) { UE_LOG(LogCireChampionArt, Warning, TEXT("HQ static part skipped: %s on %s"), *Path, *Bone); continue; }
        FTransform BoneInMesh = FTransform::Identity; // bind pose of the bone in mesh space
        for (int32 I = Index; I != INDEX_NONE; I = Ref.GetParentIndex(I)) BoneInMesh = BoneInMesh * Ref.GetRefBonePose()[I];
        FTransform Relative = BoneInMesh.Inverse();
        const TArray<TSharedPtr<FJsonValue>>* V = nullptr;
        if ((*Row)->TryGetArrayField(TEXT("offsetCm"), V) && V->Num() == 3) Relative.AddToTranslation(FVector((*V)[0]->AsNumber(), (*V)[1]->AsNumber(), (*V)[2]->AsNumber()));
        if ((*Row)->TryGetArrayField(TEXT("rotation"), V) && V->Num() == 3) Relative.ConcatenateRotation(FRotator((*V)[0]->AsNumber(), (*V)[1]->AsNumber(), (*V)[2]->AsNumber()).Quaternion());
        auto* Part = NewObject<UStaticMeshComponent>(&Hero, NAME_None, RF_Transient);
        Hero.AddInstanceComponent(Part);
        Part->SetupAttachment(Leader, FName(*Bone));
        Part->SetStaticMesh(Asset);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision); Part->SetGenerateOverlapEvents(false);
        Part->SetCanEverAffectNavigation(false); Part->SetCastShadow(true);
        Part->ComponentTags.AddUnique(TEXT("CireBodyPart"));
        Part->RegisterComponent();
        Part->SetRelativeTransform(Relative);
        Part->SetOverlayMaterial(Leader->GetOverlayMaterial());
        Part->SetVisibility(Leader->IsVisible());
        StaticParts.Add(Part);
    }
}

bool UCireChampionArt::ApplyFabBody(ACireHero& Hero, const TSharedPtr<FJsonObject>& Raw)
{
    ClearBodyParts();
    USkeletalMeshComponent* Leader = Hero.GetMesh();
    if (!Leader || !Raw.IsValid()) return false;
    const TSharedPtr<FJsonObject>* Materials = nullptr;
    if (Raw->TryGetObjectField(TEXT("materials"), Materials)) ApplyMaterialSpec(Leader, *Materials, &Hero);
    const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
    if (!Raw->TryGetArrayField(TEXT("parts"), Parts)) return true;
    for (const auto& Value : *Parts)
    {
        const TSharedPtr<FJsonObject>* Row = nullptr; FString Path;
        if (!Value->TryGetObject(Row) || !(*Row)->TryGetStringField(TEXT("mesh"), Path)) return false;
        USkeletalMesh* Asset = LoadObject<USkeletalMesh>(nullptr, *Path, nullptr, LOAD_Quiet | LOAD_NoWarn);
        if (!Asset) { UE_LOG(LogCireChampionArt, Warning, TEXT("Fab body part missing: %s"), *Path); return false; }
        auto* Part = NewObject<USkeletalMeshComponent>(&Hero, NAME_None, RF_Transient);
        Hero.AddInstanceComponent(Part);
        Part->SetupAttachment(Leader);
        Part->SetSkeletalMesh(Asset);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision); Part->SetGenerateOverlapEvents(false);
        Part->SetCanEverAffectNavigation(false); Part->SetCastShadow(true);
        Part->bUseBoundsFromLeaderPoseComponent = true;
        Part->ComponentTags.AddUnique(TEXT("CireBodyPart"));
        Part->RegisterComponent();
        Part->SetLeaderPoseComponent(Leader);
        if ((*Row)->TryGetObjectField(TEXT("materials"), Materials)) ApplyMaterialSpec(Part, *Materials, &Hero);
        Part->SetOverlayMaterial(Leader->GetOverlayMaterial());
        Part->SetVisibility(Leader->IsVisible());
        BodyParts.Add(Part);
    }
    return true;
}

bool UCireChampionArt::FabHumanoidBody(const FString& ProfileId, FString& OutMesh, float& OutHeightCm, float& OutScale)
{
    const auto* Fab = ProfileId.StartsWith(TEXT("pet:")) ? nullptr : FabProfileArt(ProfileId);
    if (!Fab || Fab->Motion != TEXT("humanoid")) return false;
    double Scale = 1; Fab->Raw->TryGetNumberField(TEXT("meshScale"), Scale);
    OutMesh = Fab->MeshPath; OutHeightCm = Fab->HeightCm; OutScale = static_cast<float>(Scale);
    return true;
}
