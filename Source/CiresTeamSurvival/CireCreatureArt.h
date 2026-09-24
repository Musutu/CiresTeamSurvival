#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Components/ActorComponent.h"
#include "ProceduralMeshComponent.h"
#include "CireCreatureArt.generated.h"

class ACireHero;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class UMeshComponent;

/** Imported custom quadruped rig, evaluated in local space without root motion. */
UCLASS(Transient)
class CIRESTEAMSURVIVAL_API UCireBearAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    float Phase=0,Stride=0,Attack=0,Time=0,Air=0,Roll=0;
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
    bool Apply(ACireHero& Hero,const FString& Profile,const FString& MeshPath,float HeightCm);
    void Update(ACireHero& Hero,float Delta);
    void Clear();
    UMeshComponent* VisualMesh() const;
    UObject* GetSourceAsset() const { return SourceAsset; }
    float MotionPhase() const { return Phase; }
private:
    UPROPERTY(Transient) TObjectPtr<UObject> SourceAsset;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> StaticBody;
    UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> Centaur;
    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> Bear;
    TArray<FCireCreatureSection> Sections;
    FString Kind;
    FVector BasePosition=FVector::ZeroVector;
    float Phase=0,SmoothedSpeed=0,AnimationTime=0,UpdateBudget=0;
    void DeformCentaur(float Stride,float Attack,float Air,float Roll);
};
