// ability-vfx: ground telegraph painter, release timing and impact shake (see CireAbilityVFX.h).
#include "CireAbilityVFX.h"
#include "CireSpellMesh.h"
#include "CireAudio.h"
#include "CireDeveloperTools.h"
#include "CireGame.h"
#include "CireHUD.h"
#include "CireSkillTuning.h"
#include "Algo/Reverse.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

using namespace CireSpellMesh;

static TAutoConsoleVariable<int32> CVarCireAbilityVFX(TEXT("cire.AbilityVFX"),1,
    TEXT("1: shape-true ability telegraphs, impacts and release sync (default). 0: previous presentation, for A/B captures."));
bool CireAbilityVFX::Enabled()
{
    static const bool bLegacyFlag=FParse::Param(FCommandLine::Get(),TEXT("CireLegacyVFX"));
    return !bLegacyFlag&&CVarCireAbilityVFX.GetValueOnAnyThread()!=0;
}

namespace
{
double Cross2(FVector2D A,FVector2D B){return A.X*B.Y-A.Y*B.X;}
// Ear clipping for simple (possibly concave) polygons; indices into P.
TArray<int32> Triangulate(const TArray<FVector2D>& P)
{
    TArray<int32> Remaining,Out;double Area=0;
    for(int32 J=0;J<P.Num();++J){Remaining.Add(J);Area+=Cross2(P[J],P[(J+1)%P.Num()]);}
    if(Area<0)Algo::Reverse(Remaining);
    for(int32 Budget=P.Num()*P.Num();Remaining.Num()>2&&Budget>0;--Budget)
    {
        bool bClipped=false;
        for(int32 J=0;J<Remaining.Num();++J)
        {
            const int32 A=Remaining[(J+Remaining.Num()-1)%Remaining.Num()],B=Remaining[J],C=Remaining[(J+1)%Remaining.Num()];
            if(Cross2(P[B]-P[A],P[C]-P[B])<=.001)continue;
            bool bInside=false;
            for(int32 K:Remaining)if(K!=A&&K!=B&&K!=C&&Cross2(P[B]-P[A],P[K]-P[A])>=-.001&&Cross2(P[C]-P[B],P[K]-P[B])>=-.001&&Cross2(P[A]-P[C],P[K]-P[C])>=-.001){bInside=true;break;}
            if(bInside)continue;Out.Append({A,B,C});Remaining.RemoveAt(J);bClipped=true;break;
        }
        if(!bClipped)break;
    }
    return Out;
}
bool Convex(const FCireAreaSpec& S){return S.Shape!=ECireAreaShape::Custom;}
FVector2D PivotOf(const FCireAreaSpec& S,const TArray<FVector2D>& B)
{
    if(S.Shape==ECireAreaShape::Line)return FVector2D(S.Length*.5f,0);
    if(S.Shape==ECireAreaShape::Cone)return FVector2D(S.Radius*.45f,0);
    return Centroid(B);
}
float MinDimension(const FCireAreaSpec& S,const TArray<FVector2D>& B)
{
    switch(S.Shape)
    {
    case ECireAreaShape::Circle:case ECireAreaShape::Cone:return S.Radius;
    case ECireAreaShape::Line:return FMath::Min(S.Width,S.Length);
    case ECireAreaShape::Square:return S.Width;
    default:
    {
        FBox2D Box(B);return FMath::Min(Box.GetSize().X,Box.GetSize().Y);
    }
    }
}
// Uniform or radial fill of a loop (convex: fan from pivot; concave: ear clipped, uniform).
void FillLoop(FCireGroundMesh& G,const TArray<FVector2D>& Loop,bool bConvex,FVector2D Pivot,FLinearColor Center,FLinearColor Edge,float Lift=0)
{
    if(Loop.Num()<3)return;
    if(bConvex){G.Fan(Pivot,Loop,Center,Edge,true,Lift);return;}
    const auto Tris=Triangulate(Loop);if(Tris.IsEmpty()||!G.Room(Loop.Num()))return;
    const int32 Base=G.V.Num();for(const auto& P:Loop)G.Add(P,Edge,Lift);
    for(int32 J=0;J+2<Tris.Num();J+=3)G.Tri(Base+Tris[J],Base+Tris[J+1],Base+Tris[J+2]);
}
// Border along a closed loop: solid core of 2*Half with feathered glow on both sides.
void Outline(FCireGroundMesh& G,const TArray<FVector2D>& Loop,float Half,float Glow,FLinearColor Color,float Lift=0)
{
    const auto In=Offset(Loop,-Half),Out=Offset(Loop,Half);
    const auto InGlow=Offset(Loop,-Half-Glow*.6f),OutGlow=Offset(Loop,Half+Glow);
    const FLinearColor Clear=WithAlpha(Color,0);
    G.Band(InGlow,In,Clear,WithAlpha(Color,Color.A*.55f),true,Lift);
    G.Band(In,Out,Color,Color,true,Lift);
    G.Band(Out,OutGlow,WithAlpha(Color,Color.A*.55f),Clear,true,Lift);
}
TArray<FVector2D> LaneRect(float X0,float X1,float H){return {FVector2D(X0,-H),FVector2D(X1,-H),FVector2D(X1,H),FVector2D(X0,H)};}
TArray<FVector2D> ConeLoop(float Radius,float Angle,int32 Steps=24)
{
    TArray<FVector2D> L;L.Add(FVector2D::ZeroVector);
    for(int32 J=0;J<=Steps;++J){const float A=FMath::DegreesToRadians(-Angle*.5f+Angle*J/Steps);L.Add(Polar2(Radius,A));}
    return L;
}
float Ease(float U){U=FMath::Clamp(U,0.f,1.f);return 1-FMath::Square(1-U);}
}

CireAbilityVFX::FStyle CireAbilityVFX::StyleFor(ETone Tone,FLinearColor School)
{
    FStyle S;
    switch(Tone)
    {
    case ETone::Hostile:
        // Amber enemy warning: warm fill, hot orange edge, red-orange progress that reads as "about to hit".
        S.Fill=FLinearColor(1.25f,.42f,.04f,.16f);S.Edge=FLinearColor(2.1f,.72f,.08f,.95f);S.Progress=FLinearColor(1.7f,.28f,.03f,.30f);S.Accent=FLinearColor(2.4f,1.05f,.2f,.9f);break;
    case ETone::AimValid:
        S.Fill=FLinearColor(.08f,1.f,.6f,.13f);S.Edge=FLinearColor(.2f,1.6f,1.f,.92f);S.Progress=S.Fill;S.Accent=FLinearColor(.5f,2.f,1.3f,.9f);break;
    case ETone::AimInvalid:
        S.Fill=FLinearColor(1.3f,.1f,.06f,.13f);S.Edge=FLinearColor(1.9f,.2f,.12f,.92f);S.Progress=S.Fill;S.Accent=FLinearColor(2.2f,.4f,.3f,.9f);break;
    default:
    {
        // Own-team telegraph in the ability's school colour, brighter core for the border.
        const FLinearColor C=School.GetClamped(0,3);
        S.Fill=WithAlpha(C*.7f,.14f);S.Edge=WithAlpha(FMath::Lerp(C,FLinearColor(2,2,2,1),.2f),.9f);S.Progress=WithAlpha(C,.26f);S.Accent=WithAlpha(FMath::Lerp(C,FLinearColor(2.4f,2.4f,2.2f,1),.35f),.9f);
    }
    }
    return S;
}

CireAbilityVFX::FPaintResult CireAbilityVFX::PaintTelegraph(FCireGroundMesh& G,const FCireAreaSpec& Spec,const FStyle& Style,float Progress,float Time,float Alpha,uint32 Flags)
{
    FPaintResult R;const int32 Start=G.V.Num();
    const TArray<FVector2D> Boundary=ACireAreaEffect::BoundaryPoints(Spec);
    if(Boundary.Num()<3||Alpha<=.001f)return R;
    R.FillBounds=FBox2D(Boundary);
    const bool bConvex=Convex(Spec);const FVector2D Pivot=PivotOf(Spec,Boundary);
    const float Size=MinDimension(Spec,Boundary);
    const float Feather=FMath::Clamp(Size*.2f,10.f,70.f);
    auto A=[&](FLinearColor C,float Scale=1.f){C.A*=Alpha*Scale;return C;};
    // 1. Soft fill: faint interior, denser toward the edge (reads as a disc/lane, not a flat sticker).
    if(Flags&PaintNoFill){}
    else if(bConvex)
    {
        const auto Inner=Offset(Boundary,-Feather);
        FillLoop(G,Inner,true,Pivot,A(Style.Fill,.55f),A(Style.Fill,.9f));
        G.Band(Inner,Boundary,A(Style.Fill,.9f),A(Style.Fill,1.9f),true);
    }
    else FillLoop(G,Boundary,false,Pivot,A(Style.Fill),A(Style.Fill,1.2f));
    // 2. Progress: fills from the caster (lines/cones) or the centre (circles) until release.
    if(Progress>=0)
    {
        const float P=FMath::Clamp(Progress,0.f,1.f);TArray<FVector2D> Loop;
        if(Spec.Shape==ECireAreaShape::Line)Loop=LaneRect(0,FMath::Max(1.f,Spec.Length*P),Spec.Width*.5f);
        else if(Spec.Shape==ECireAreaShape::Cone)Loop=ConeLoop(FMath::Max(1.f,Spec.Radius*P),Spec.ConeAngleDegrees);
        else Loop=Scaled(Boundary,Pivot,FMath::Max(.02f,P));
        FillLoop(G,Loop,bConvex,Spec.Shape==ECireAreaShape::Cone?FVector2D(Spec.Radius*P*.45f,0):Spec.Shape==ECireAreaShape::Line?FVector2D(Spec.Length*P*.5f,0):Pivot,
            A(Style.Progress,.8f),A(Style.Progress,1.2f),.4f);
        // Leading edge: the wave front that will reach the border exactly when the hit resolves.
        if(P>.02f&&P<.995f)
        {
            if(Spec.Shape==ECireAreaShape::Line)G.Stroke(FVector2D(Spec.Length*P,-Spec.Width*.5f),FVector2D(Spec.Length*P,Spec.Width*.5f),2.5f,7.f,A(Style.Accent,.8f),.8f);
            else if(Spec.Shape==ECireAreaShape::Cone)G.Ring(FVector2D::ZeroVector,Spec.Radius*P,2.5f,7.f,A(Style.Accent,.8f),20,FMath::DegreesToRadians(-Spec.ConeAngleDegrees*.5f),FMath::DegreesToRadians(Spec.ConeAngleDegrees),.8f);
            else Outline(G,Loop,2.f,6.f,A(Style.Accent,.75f),.8f);
        }
    }
    // 3. Border: crisp line on the true boundary with a soft glow, breathing while armed.
    const float Pulse=(Flags&PaintPulse)?.72f+.28f*FMath::Sin(Time*2*PI*1.6f):1.f;
    const float Half=FMath::Clamp(Size*.012f,2.2f,4.5f);
    Outline(G,Boundary,Half,FMath::Clamp(Size*.05f,8.f,22.f),A(Style.Edge,Pulse),1.2f);
    // 4. Shape language.
    if(Spec.Shape==ECireAreaShape::Line&&(Flags&PaintArrow))
    {
        const float W=Spec.Width,L=Spec.Length;
        const float Head=FMath::Clamp(W*.95f,36.f,FMath::Max(36.f,L*.28f));
        const float Tip=L-FMath::Min(6.f,L*.02f),Base=FMath::Max(0.f,Tip-Head),HalfHead=W*.5f-FMath::Min(4.f,W*.08f);
        G.Triangle(FVector2D(Base,-HalfHead),FVector2D(Tip,0),FVector2D(Base,HalfHead),A(Style.Accent,.42f),1.6f);
        G.Stroke(FVector2D(Base,-HalfHead),FVector2D(Tip,0),2.f,5.f,A(Style.Accent,.95f),1.8f);
        G.Stroke(FVector2D(Base,HalfHead),FVector2D(Tip,0),2.f,5.f,A(Style.Accent,.95f),1.8f);
        R.ArrowTip=FVector2D(Tip,0);
        // Travel chevrons flowing caster -> tip: the direction of the attack at a glance.
        const float Run=FMath::Max(1.f,Base-6.f);const int32 Count=FMath::Clamp(FMath::RoundToInt(Run/230.f),1,6);
        const float Phase=Fract(Time*.85f);const float Chevron=FMath::Clamp(W*.42f,16.f,70.f);
        for(int32 J=0;J<Count;++J)
        {
            const float U=(J+Phase)/Count;const float X=6.f+Chevron*.5f+U*FMath::Max(0.f,Run-Chevron);
            const float Fade=FMath::Min(1.f,FMath::Min(U*4.f,(1-U)*4.f));
            G.Chevron(FVector2D(X,0),FVector2D(1,0),Chevron*.8f,FMath::Clamp(W*.035f,1.6f,4.5f),A(Style.Accent,.7f*Fade),1.4f);++R.Chevrons;
        }
    }
    if(Spec.Shape==ECireAreaShape::Cone&&(Flags&PaintArrow))
    {
        const float Phase=Fract(Time*.8f);const float Chevron=FMath::Clamp(Spec.Radius*.16f,18.f,60.f);
        for(int32 J=0;J<2;++J)
        {
            const float U=(J+Phase)/2;const float X=Spec.Radius*(.2f+.62f*U);const float Fade=FMath::Min(1.f,FMath::Min(U*4.f,(1-U)*4.f));
            G.Chevron(FVector2D(X,0),FVector2D(1,0),Chevron,2.4f,A(Style.Accent,.65f*Fade),1.4f);++R.Chevrons;
        }
    }
    if((Flags&PaintCenter)&&(Spec.Shape==ECireAreaShape::Circle||Spec.Shape==ECireAreaShape::Square||Spec.Shape==ECireAreaShape::Custom))
    {
        // Designated-spot marker: ring + crosshair ticks + dot, rotating slowly.
        const FVector2D C=Spec.Shape==ECireAreaShape::Circle?FVector2D::ZeroVector:Centroid(Boundary);
        const float Mark=FMath::Clamp(Size*.11f,12.f,42.f);
        G.Ring(C,Mark,1.8f,4.f,A(Style.Accent,.85f),28,0,2*PI,1.8f);
        for(int32 J=0;J<4;++J)
        {
            const float Ang=Time*.6f+J*PI*.5f;
            G.Stroke(C+Polar2(Mark*1.35f,Ang),C+Polar2(Mark*2.1f,Ang),1.8f,3.f,A(Style.Accent,.75f),1.8f);
        }
        G.Disc(C,FMath::Max(3.f,Mark*.18f),A(Style.Accent,.9f),A(Style.Accent,.5f),12,1.9f);
    }
    R.Vertices=G.V.Num()-Start;return R;
}

CireAbilityVFX::FPaintResult CireAbilityVFX::PaintActive(FCireGroundMesh& G,const FCireAreaSpec& Spec,FLinearColor Color,float Time,float Alpha,float Burst,bool bPersistent)
{
    FPaintResult R;const int32 Start=G.V.Num();
    const TArray<FVector2D> Boundary=ACireAreaEffect::BoundaryPoints(Spec);
    if(Boundary.Num()<3||Alpha<=.001f)return R;
    R.FillBounds=FBox2D(Boundary);
    const bool bConvex=Convex(Spec);const FVector2D Pivot=PivotOf(Spec,Boundary);
    const float Size=MinDimension(Spec,Boundary);const float Feather=FMath::Clamp(Size*.22f,12.f,80.f);
    const FLinearColor C=Color.GetClamped(0,4);
    auto A=[&](float Opacity,float Bright=1.f){FLinearColor X=C*Bright;X.A=FMath::Clamp(Opacity*Alpha,0.f,1.f);return X;};
    const float Breathe=.82f+.18f*FMath::Sin(Time*2*PI*.9f);
    const float Flash=FMath::Clamp(Burst,0.f,1.f);
    if(bConvex)
    {
        const auto Inner=Offset(Boundary,-Feather);
        FillLoop(G,Inner,true,Pivot,A(.10f*Breathe+.22f*Flash,1+.4f*Flash),A(.2f*Breathe+.22f*Flash,1+.4f*Flash));
        G.Band(Inner,Boundary,A(.2f*Breathe+.22f*Flash,1+.4f*Flash),A(.36f+.2f*Flash,1.2f+.4f*Flash),true);
    }
    else FillLoop(G,Boundary,false,Pivot,A(.2f+.25f*Flash),A(.26f+.25f*Flash));
    Outline(G,Boundary,FMath::Clamp(Size*.01f,2.f,4.f),FMath::Clamp(Size*.06f,8.f,26.f),A((.7f+.3f*Breathe)*(bPersistent?1.f:.9f),1.5f),1.2f);
    if(bPersistent)
    {
        // Slow ripples from the pivot keep a lingering zone alive without covering the combat underneath.
        for(int32 J=0;J<2;++J)
        {
            const float U=Fract(Time*.33f+J*.5f);
            const auto Loop=Scaled(Boundary,Pivot,.12f+.88f*U);
            Outline(G,Loop,1.6f,6.f,A(.34f*FMath::Sin(U*PI),1.3f),.7f);
        }
    }
    if(Flash>0)
    {
        // Detonation: shock front races from the pivot to the true edge.
        const auto Loop=Scaled(Boundary,Pivot,FMath::Clamp(.15f+.85f*Ease(1-Flash),.05f,1.f));
        Outline(G,Loop,3.f+5*Flash,14.f,A(.95f*Flash,2.2f),1.6f);
    }
    R.Vertices=G.V.Num()-Start;return R;
}

void CireAbilityVFX::PaintShock(FCireGroundMesh& G,FVector2D Center,float Radius,FLinearColor Color,float U,float Alpha)
{
    U=FMath::Clamp(U,0.f,1.f);if(Alpha<=.001f||Radius<=1)return;
    const float R=Radius*(.12f+.88f*Ease(U));
    FLinearColor Ring=Color;Ring.A=Alpha*(1-U*.85f);
    G.Ring(Center,R,3.f+6*(1-U),16.f+10*(1-U),Ring,64,0,2*PI,1.4f);
    FLinearColor Soft=Color*.8f;Soft.A=Alpha*.22f*(1-U);
    G.Disc(Center,R,WithAlpha(Soft,Soft.A*.4f),Soft,48,.6f);
    // Final extent marker: the true radius the effect reached, faintly held while the wave travels.
    if(U<.95f){FLinearColor Edge=Color;Edge.A=Alpha*.35f*(1-U);G.Ring(Center,Radius,1.6f,5.f,Edge,64,0,2*PI,1.2f);}
}

float CireAbilityVFX::ReleaseLead(const UWorld* World,const FString& SkillId)
{
    if(!Enabled())return ACireHero::IsPassive(SkillId)?0.f:.28f; // previous fixed clip windup
    if(SkillId==TEXT("ember_lance")||SkillId==TEXT("frost_bind")||SkillId==TEXT("piercing_shot"))
        if(const auto* Authored=CireSkillTuning::FindSkillshot(SkillId))
        {
            auto Spec=*Authored;
            if(World)CireDeveloperTools::AdjustSkillshot(const_cast<UWorld*>(World),Spec);
            return FMath::Clamp(Spec.WarningSeconds,InstantLead,2.f);
        }
    return ACireHero::IsPassive(SkillId)?0.f:InstantLead;
}

void CireAbilityVFX::ImpactShake(UWorld* World,FVector At,float Strength)
{
    if(!World||World->GetNetMode()==NM_DedicatedServer||!FMath::IsFinite(Strength)||Strength<=0)return;
    const auto* PC=World->GetFirstPlayerController();
    const auto* Hero=PC?Cast<ACireHero>(PC->GetPawn()):nullptr;const auto* HUD=PC?Cast<ACireHUD>(PC->GetHUD()):nullptr;
    if(!Hero||!HUD||!HUD->UISettings.bImpactCameraShake)return;
    const float Distance=static_cast<float>(FVector::Dist(Hero->GetActorLocation(),At));
    if(Distance>450.f)return;
    if(auto* Audio=World->GetSubsystem<UCireAudioSubsystem>())Audio->AddLocalShake(FMath::Clamp(Strength,0.f,4.f)*(1.f-Distance/450.f*.6f));
}
