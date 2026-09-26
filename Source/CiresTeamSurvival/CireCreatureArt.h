#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Components/ActorComponent.h"
#include "ProceduralMeshComponent.h"
#include "Dom/JsonObject.h"
#include "CireLocomotion.h" // movement-feel
#include "CireCreatureArt.generated.h"

class ACireHero;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class UMeshComponent;
class UStaticMeshComponent;
class UAnimSequence;
class UBlendSpace;
class UCireMonsterAnimInstance;
class USkeletalMesh;

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

/**
 * pets: a quadruped rig found from the reference skeleton alone (no bone-name table): the four
 * ground chains become legs (front/rear, left/right by the mesh's facing), the path between the
 * hip and shoulder branches is the spine, and the forward-most and rear-most free chains are the
 * neck/head and the tail. Positions are in mesh (component) space.
 */
struct CIRESTEAMSURVIVAL_API FCireQuadRig
{
    FName Legs[4][3];            // FL, FR, RL, RR: upper, middle, lower joint (None when the chain is shorter)
    TArray<FName> Spine, Neck, Tail;
    FName Root;
    FVector Forward = FVector::ForwardVector, Lateral = FVector::RightVector;
    float SwingSign = 1.f;       // sign that swings a hanging leg forward about Lateral
    float LegUnits = 30.f;       // hip-to-paw height, mesh units
    bool bValid = false;
    static bool Analyze(const USkeletalMesh& Mesh, float MeshYaw, FCireQuadRig& Out, FString* Why = nullptr);
};

/** pets: procedural trot / idle / lunge / collapse for any FCireQuadRig (the clip-less Tripo sabercat). */
UCLASS(Transient)
class CIRESTEAMSURVIVAL_API UCireQuadrupedAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    FCireQuadRig Rig;
    float Phase = 0, Stride = 0, Amplitude = 0, Attack = -1, Time = 0, Air = 0, Dead = 0;
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
     *  mounted         a quadruped mount (idle/walk/run/attack) with a humanoid rider seated on its back (kept for future riding units)
     *  quadruped_procedural  pets: a clip-less quadruped driven procedurally (FCireQuadRig)
     */
    bool ApplyBinding(ACireHero& Hero,const FString& Profile,const FString& Motion,const FString& MeshPath,float HeightCm,const TSharedPtr<FJsonObject>& Binding);
    USkeletalMeshComponent* GetRider() const { return Rider; }
    USkeletalMeshComponent* GetNativeBody() const { return Native; }
    FName GetSeatBone() const { return SeatBone; }
    int32 GetPropCount() const { return Props.Num(); }
    /** fab-integration: skeletal parts (armour, mane, bow...) following the native body through leader pose. */
    int32 GetPartCount() const { return Parts.Num(); }
    /** fab-integration: the native body plays hit / cast / death clips (champion bodies from ChampionArtBindings.fab.json). */
    bool HasReactions() const { return bReactions; }
    /** fab-integration: the clip on the native action layer right now (attack, cast or hit), or null. */
    const UAnimSequence* GetActionClip() const { return ActionClip; }
    /** pets: the procedural quadruped body and its rig (null / invalid for other kinds). */
    USkeletalMeshComponent* GetQuadBody() const { return Quad; }
    const FCireQuadRig* GetQuadRig() const;
    const FString& GetKind() const { return Kind; }
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
    /**
     * fab-integration: creature champions on their Fab bodies (ChampionArtBindings.fab.json) when the packs are installed:
     * native body + parts, flinch, alternating strike, cast clip, held death. Without the packs: the committed binding.
     * Logs CIRE_FAB_CREATURE_CHAMPIONS_PASS / _FAIL.
     */
    static bool RunFabChampionSmoke(UWorld* World);
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
    // pets: procedural quadruped body, and the native death clip (the corpse stays down).
    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> Quad;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> DeathClip;
    float DeadAge = 0, DeadWeight = 0;
    bool ApplyQuad(ACireHero& Hero, const FString& MeshPath, float HeightCm, const TSharedPtr<FJsonObject>& Binding);
    void UpdateQuad(ACireHero& Hero, float Delta);
    // fab-integration: champion reactions on the native body (attack alternation, casts, hits, death) and leader-pose parts.
    UPROPERTY(Transient) TArray<TObjectPtr<USkeletalMeshComponent>> Parts;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> AttackAltClip;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> HitClip;
    UPROPERTY(Transient) TMap<FString,TObjectPtr<UAnimSequence>> CastClips;
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> ActionClip;
    /** Contact frame (seconds into the clip) per clip; the strike reaches it when the server releases the blow. */
    TMap<const UAnimSequence*,float> Contacts;
    double ActionStartedAt=-100;
    float ActionContact=0,ActionRelease=0,ActionWeight=1;
    bool bActionIsHit=false,bActionBasic=false,bReactions=false;
    float LastHealth=-1;double LastHitAt=-100;
    TArray<float> LastCooldowns;
    void StartAction(UAnimSequence* Clip,double StartedAt,float Release,float Weight,bool bHit);
    void UpdateReactions(ACireHero& Hero,UCireMonsterAnimInstance& Anim,float Dt,double Now);
    UPROPERTY(Transient) TObjectPtr<UAnimSequence> RiderAttack;
    UPROPERTY(Transient) TObjectPtr<UBlendSpace> RiderLocomotion;
    FName SeatBone;
    float NativeWalkRaw=0,NativeRunRaw=0,NativePhase=0,NativeIdleTime=0;
    CireLocomotion::FVisualTurn NativeTurn; // movement-feel
    CireLocomotion::FLegIK NativeLegs;
    bool bNativeTicksOrdered=false;
    uint32 SeenAttackSerial=0;
    bool bRiderRelax=false;
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
