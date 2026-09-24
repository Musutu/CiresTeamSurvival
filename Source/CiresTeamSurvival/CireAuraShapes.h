#pragma once
#include "CoreMinimal.h"
#include "CireAuraVisuals.h"

// aura-vfx: procedural geometry for aura layers and attack-modifier strikes.
// All geometry is emitted in a world-aligned space centred on the unit's capsule
// (or the strike actor). "Core" triangles are additive vertex-coloured shapes;
// "Soft" quads use a radial-falloff material for feathered glows.
namespace CireAuraShapes
{
struct FBuffers
{
    TArray<FVector>& CV; TArray<int32>& CI; TArray<FLinearColor>& CC;
    TArray<FVector>& SV; TArray<int32>& SI; TArray<FLinearColor>& SC; TArray<FVector2D>& SUV;
    int32 MaxCore=7000, MaxSoft=1600;
    void Reset(){CV.Reset();CI.Reset();CC.Reset();SV.Reset();SI.Reset();SC.Reset();SUV.Reset();}
};
struct FContext
{
    float Radius=40,Height=184,Feet=-92,Top=92,Unit=1; // Unit = Height/184
    FVector Forward=FVector::ForwardVector;
    FVector CamRight=FVector::RightVector,CamUp=FVector::UpVector,CamLook=FVector::ForwardVector,CamLocal=FVector(-600,0,300);
    TArray<FVector,TInlineAllocator<2>> Hands;
    TArray<FVector,TInlineAllocator<2>> Weapon;
    bool bHasLink=false; FVector Link=FVector::ZeroVector;
    float Time=0,Age=0,Alpha=1,Intensity=1,Pop=1;
    int32 Detail=2; // 2 full, 1 reduced
    FLinearColor Primary=FLinearColor::White,Secondary=FLinearColor::White,Core=FLinearColor::White;
    uint32 Seed=0;
};
void DrawLayer(const FCireAuraLayer& Layer,const FContext& C,FBuffers& B);
void DrawAllegianceRim(const FContext& C,FLinearColor Rim,float Alpha,FBuffers& B);
// Strikes: local space is centred on the strike actor, world-aligned.
void DrawSwipe(const FCireAuraAttack& A,float Age,float Scale,float Mirror,FVector Forward,const FContext& Camera,FBuffers& B);
void DrawHit(const FCireAuraAttack& A,float Age,float Scale,const FContext& Camera,FBuffers& B);
void DrawTrail(const FCireAuraAttack& A,const TArray<FVector>& Points,float Time,float Scale,float Fade,const FContext& Camera,FBuffers& B);
void DrawMuzzle(const FCireAuraAttack& A,float Age,float Scale,const FContext& Camera,FBuffers& B);
bool ParseShape(const FString& Name,ECireAuraShape& Out);
const TCHAR* ShapeName(ECireAuraShape Shape);
}
