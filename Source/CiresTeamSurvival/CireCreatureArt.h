#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Components/ActorComponent.h"
#include "ProceduralMeshComponent.h"
#include "Dom/JsonObject.h"
#include "CireCreatureArt.generated.h"

class ACireHero;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class UMeshComponent;
class UStaticMeshComponent;
class UAnimSequence;
class UBlendSpace;

/** Imported custom quadruped rig, evaluated in local space without root motion. */
UCLASS(Transient)
class CIRESTEAMSURVIVAL_API UCireBearAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    float Phase=0,Stride=0,Attack=0,Time=0,Air=0,Roll=0;
    /** Peak leg swing in degrees for the current speed and the hip-to-paw length in mesh units. */
    float Amplitude=0,LegUnits=34;
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
};

struct FCireCreatureSection
{
    TArray<FVector> Rest,Positions,Normals,RestNormals;
    TArray<FVector2D> UV;
    TArray<FProcMeshTangent> Tangents,RestTangents;
    TArray<int32> Triangles;
};

/** Cosmetic-only body adapter. The hero capsule still owns movement and targeting. */
UCLASS()
class CIRESTEAMSURVIVAL_API UCireCreatureArt : public UActorComponent
{
    GENERATED_BODY()
public:
    static bool Handles(const FString& Profile);
    /** new-champions: binding motions drawn by this adapter ("monster_native", "mounted"). */
    static bool HandlesMotion(const FString& Motion);
    bool Apply(ACireHero& Hero,const FString& Profile,const FString& MeshPath,float HeightCm);
    /**
     * new-champions: data-driven bodies from ChampionArtBindings.json.
     *  monster_native  a Tripo monster body driven natively (idle/walk/run/attack clips) with props on its bones (Gunblade)
     *  mounted         a quadruped mount (idle/walk/run/attack) with a humanoid rider seated on its back (Huntress)
     */
    bool ApplyBinding(ACireHero& Hero,const FString& Profile,const FString& Motion,const FString& MeshPath,float HeightCm,const TSharedPtr<FJsonObject>& Binding);
    USkeletalMeshComponent* GetRider() const { return Rider; }
    USkeletalMeshComponent* GetNativeBody() const { return Native; }
    FName GetSeatBone() const { return SeatBone; }
    int32 GetPropCount() const { return Props.Num(); }
    void Update(ACireHero& Hero,float Delta);
    void Clear();
    UMeshComponent* VisualMesh() const;
    UObject* GetSourceAsset() const { return SourceAsset; }
    float MotionPhase() const { return Phase; }
#if !UE_BUILD_SHIPPING
    /**
     * Native gait check on the real bear rig: walks and runs a bear body in place of the capsule and
     * measures planted-paw slip (paw ground speed while lowest / body speed) plus right-rear knee motion.
     */
    static bool RunGaitSmoke(UWorld* World);
#endif
private:
    UPROPERTY(Transient) TObjectPtr<UObject> SourceAsset;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> StaticBody;
    UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> Centaur;
    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> Bear;
    // new-champions: native monster body / mount, rider and props.
    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> Native;
    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> Rider;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Props;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> AttackClip;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> RiderAttack;
    UPROPERTY(Transient) TObjectPtr<UBlendSpace> RiderLocomotion;
    FName SeatBone;
    float NativeWalkRaw=0,NativeRunRaw=0,NativePhase=0,NativeIdleTime=0;
    uint32 SeenAttackSerial=0;
    double AttackSeenAt=-100;
    void UpdateNative(ACireHero& Hero,float Delta);
    void AttachProps(ACireHero& Hero,USkeletalMeshComponent* Body,const TArray<TSharedPtr<FJsonValue>>* List,float MeshScale);
    TArray<FCireCreatureSection> Sections;
    FString Kind;
    FVector BasePosition=FVector::ZeroVector;
    float Phase=0,SmoothedSpeed=0,AnimationTime=0,UpdateBudget=0;
    /** Bear gait: hip-to-paw length in mesh units and the mesh scale (component-relative). */
    float LegUnits=34,MeshScale=1,LastYaw=0;
    bool bHasLastYaw=false;
    void DeformCentaur(float Stride,float Attack,float Air,float Roll);
};
