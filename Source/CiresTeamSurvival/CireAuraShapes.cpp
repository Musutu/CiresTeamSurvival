#include "CireAuraShapes.h"

namespace CireAuraShapes
{
namespace
{
constexpr float Tau=2.f*PI;
float Fract(float N){return N-FMath::FloorToFloat(N);}
float Hash(uint32 A,uint32 B=0)
{
    uint32 H=A*747796405u+B*2891336453u+0x9E3779B9u;
    H=((H>>((H>>28u)+4u))^H)*277803737u;H=(H>>22u)^H;
    return (H&0xFFFFFF)/float(0x1000000);
}
FVector Polar(float R,float A,float Z=0){return FVector(FMath::Cos(A)*R,FMath::Sin(A)*R,Z);}
FLinearColor Col(const FLinearColor& C,float A){return FLinearColor(FMath::Clamp(C.R,0.f,1.f),FMath::Clamp(C.G,0.f,1.f),FMath::Clamp(C.B,0.f,1.f),FMath::Clamp(A,0.f,1.f));}
FLinearColor Mix(const FLinearColor& A,const FLinearColor& B,float T){return A+(B-A)*FMath::Clamp(T,0.f,1.f);}

struct FBuild
{
    FBuffers& B;
    FVector R,U,L,Cam; // camera right/up/look and camera position in local space
    FBuild(FBuffers& In,const FContext& C):B(In),R(C.CamRight),U(C.CamUp),L(C.CamLook),Cam(C.CamLocal){}
    bool Room(int32 N) const {return B.CV.Num()+N<=B.MaxCore;}
    void Tri(const FVector& A,const FVector& P,const FVector& D,const FLinearColor& CA,const FLinearColor& CB,const FLinearColor& CD)
    {
        if(!Room(3)||(CA.A<=.004f&&CB.A<=.004f&&CD.A<=.004f))return;
        const int32 N=B.CV.Num();B.CV.Append({A,P,D});B.CC.Append({CA,CB,CD});B.CI.Append({N,N+1,N+2});
    }
    void Tri(const FVector& A,const FVector& P,const FVector& D,const FLinearColor& C){Tri(A,P,D,C,C,C);}
    void Quad(const FVector& A,const FVector& P,const FVector& D,const FVector& E,const FLinearColor& CA,const FLinearColor& CB,const FLinearColor& CD,const FLinearColor& CE)
    {Tri(A,P,D,CA,CB,CD);Tri(A,D,E,CA,CD,CE);}
    void Quad(const FVector& A,const FVector& P,const FVector& D,const FVector& E,const FLinearColor& C){Quad(A,P,D,E,C,C,C,C);}
    void Tube(const FVector& A,const FVector& P,float Radius,const FLinearColor& C,int32 Sides=3)
    {
        FVector Along=(P-A).GetSafeNormal();if(Along.IsNearlyZero())return;FVector X,Y;Along.FindBestAxisVectors(X,Y);
        for(int32 J=0;J<Sides;++J)
        {
            const float A0=Tau*J/Sides,A1=Tau*(J+1)/Sides;
            const FVector O0=(X*FMath::Cos(A0)+Y*FMath::Sin(A0))*Radius,O1=(X*FMath::Cos(A1)+Y*FMath::Sin(A1))*Radius;
            Quad(A+O0,P+O0,P+O1,A+O1,C);
        }
    }
    // Camera-facing flat band between two points.
    void Ribbon(const FVector& A,const FVector& P,float WA,float WB,const FLinearColor& CA,const FLinearColor& CB)
    {
        FVector Side=FVector::CrossProduct(P-A,L).GetSafeNormal();if(Side.IsNearlyZero())Side=R;
        Quad(A-Side*WA,A+Side*WA,P+Side*WB,P-Side*WB,CA,CA,CB,CB);
    }
    void Ribbon(const FVector& A,const FVector& P,float W,const FLinearColor& C){Ribbon(A,P,W,W,C,C);}
    // Band whose width vector is supplied (for helixes/arcs that must not face the camera).
    void Band(const FVector& A,const FVector& P,const FVector& WA,const FVector& WB,const FLinearColor& CA,const FLinearColor& CB)
    {Quad(A-WA,A+WA,P+WB,P-WB,CA,CA,CB,CB);}
    void FlatRing(const FVector& Center,float Radius,float Width,const FLinearColor& C,float Begin=0,float Span=Tau,int32 Steps=48)
    {
        for(int32 J=0;J<Steps;++J)
        {
            const float A0=Begin+Span*J/Steps,A1=Begin+Span*(J+1)/Steps;
            FLinearColor T=C;if(Span<Tau-.01f)T.A*=FMath::Min(1.f,FMath::Min((J+1)*.3f,(Steps-J)*.3f));
            Quad(Center+Polar(Radius,A0),Center+Polar(Radius,A1),Center+Polar(Radius-Width,A1),Center+Polar(Radius-Width,A0),T);
        }
    }
    void BillRing(const FVector& Center,float Radius,float Width,const FLinearColor& C,int32 Steps=20,float Begin=0,float Span=Tau)
    {
        for(int32 J=0;J<Steps;++J)
        {
            const float A0=Begin+Span*J/Steps,A1=Begin+Span*(J+1)/Steps;
            const FVector D0=R*FMath::Cos(A0)+U*FMath::Sin(A0),D1=R*FMath::Cos(A1)+U*FMath::Sin(A1);
            Quad(Center+D0*Radius,Center+D1*Radius,Center+D1*(Radius-Width),Center+D0*(Radius-Width),C);
        }
    }
    void Shard(const FVector& Base,const FVector& Dir,float Length,float Radius,const FLinearColor& Body,const FLinearColor& Tip,float Twist=0)
    {
        FVector D=Dir.GetSafeNormal();if(D.IsNearlyZero())return;FVector X,Y;D.FindBestAxisVectors(X,Y);
        const FVector Top=Base+D*Length,Bottom=Base-D*Length*.18f;
        for(int32 J=0;J<4;++J)
        {
            const float A0=Twist+PI*.5f*J,A1=A0+PI*.5f;
            const FVector P0=Base+(X*FMath::Cos(A0)+Y*FMath::Sin(A0))*Radius,P1=Base+(X*FMath::Cos(A1)+Y*FMath::Sin(A1))*Radius;
            FLinearColor Shade=Body;Shade.R*=J%2?.62f:1.f;Shade.G*=J%2?.62f:1.f;Shade.B*=J%2?.62f:1.f;
            Tri(P0,P1,Top,Shade,Shade,Tip);Tri(P1,P0,Bottom,Shade,Shade,Shade);
        }
    }
    void Diamond(const FVector& P,float Radius,const FLinearColor& C)
    {
        const FVector A=P+U*Radius*1.4f,Bm=P-U*Radius*1.4f,Rt=P+R*Radius*.7f,Lt=P-R*Radius*.7f;
        FLinearColor Edge=C;Edge.A*=.45f;
        Tri(P,A,Rt,C,Edge,Edge);Tri(P,Rt,Bm,C,Edge,Edge);Tri(P,Bm,Lt,C,Edge,Edge);Tri(P,Lt,A,C,Edge,Edge);
    }
    void Star(const FVector& P,float Radius,const FLinearColor& C,int32 Points=4,float Angle=0,float Inner=.22f)
    {
        FLinearColor Edge=C;Edge.A*=.2f;
        for(int32 J=0;J<Points*2;++J)
        {
            const float A0=Angle+PI*J/Points,A1=Angle+PI*(J+1)/Points;
            const float R0=J%2?Radius*Inner:Radius,R1=J%2?Radius:Radius*Inner;
            Tri(P,P+(R*FMath::Cos(A0)+U*FMath::Sin(A0))*R0,P+(R*FMath::Cos(A1)+U*FMath::Sin(A1))*R1,C,J%2?Edge:C,J%2?C:Edge);
        }
    }
    // Billboard polyline in camera plane coordinates (X=right, Y=up).
    void Line2(const FVector& Center,FVector2D A,FVector2D P,float W,const FLinearColor& C)
    {Ribbon(Center+R*A.X+U*A.Y,Center+R*P.X+U*P.Y,W,C);}
    void Fill2(const FVector& Center,FVector2D A,FVector2D P,FVector2D D,const FLinearColor& C)
    {Tri(Center+R*A.X+U*A.Y,Center+R*P.X+U*P.Y,Center+R*D.X+U*D.Y,C);}
    // Soft section.
    bool SoftRoom() const {return B.SV.Num()+4<=B.MaxSoft;}
    void SoftQuad(const FVector& A,const FVector& P,const FVector& D,const FVector& E,const FLinearColor& C)
    {
        if(!SoftRoom()||C.A<=.004f)return;const int32 N=B.SV.Num();
        B.SV.Append({A,P,D,E});B.SC.Append({C,C,C,C});B.SUV.Append({FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)});
        B.SI.Append({N,N+1,N+2,N,N+2,N+3});
    }
    void Glow(const FVector& P,float Radius,const FLinearColor& C){SoftQuad(P-R*Radius-U*Radius,P+R*Radius-U*Radius,P+R*Radius+U*Radius,P-R*Radius+U*Radius,C);}
    void FlatGlow(const FVector& P,float Radius,const FLinearColor& C)
    {SoftQuad(P+FVector(-Radius,-Radius,0),P+FVector(Radius,-Radius,0),P+FVector(Radius,Radius,0),P+FVector(-Radius,Radius,0),C);}
    float Fresnel(const FVector& Point,const FVector& Normal) const
    {
        const FVector View=(Point-Cam).GetSafeNormal();
        return 1.f-FMath::Abs(FVector::DotProduct(Normal.GetSafeNormal(),View));
    }
};

// Level-one icosphere, generated once.
struct FIco{TArray<FVector> V;TArray<FIntVector> F;TArray<FIntPoint> E;};
const FIco& Ico()
{
    static FIco Result=[]{
        FIco I;const float T=(1.f+FMath::Sqrt(5.f))*.5f;
        TArray<FVector> V={{-1,T,0},{1,T,0},{-1,-T,0},{1,-T,0},{0,-1,T},{0,1,T},{0,-1,-T},{0,1,-T},{T,0,-1},{T,0,1},{-T,0,-1},{-T,0,1}};
        for(auto& P:V)P.Normalize();
        TArray<FIntVector> F={{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},{1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
            {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}};
        TMap<uint64,int32> Mid;
        auto Middle=[&](int32 A,int32 B){const uint64 K=A<B?(uint64(A)<<32|B):(uint64(B)<<32|A);if(int32* Found=Mid.Find(K))return *Found;
            const int32 N=V.Add(((V[A]+V[B])*.5f).GetSafeNormal());Mid.Add(K,N);return N;};
        for(const auto& Face:F)
        {
            const int32 A=Middle(Face.X,Face.Y),B=Middle(Face.Y,Face.Z),C=Middle(Face.Z,Face.X);
            I.F.Append({{Face.X,A,C},{Face.Y,B,A},{Face.Z,C,B},{A,B,C}});
        }
        I.V=V;TSet<uint64> Seen;
        for(const auto& Face:I.F){const int32 P[3]={Face.X,Face.Y,Face.Z};for(int32 K=0;K<3;++K){const int32 A=P[K],B=P[(K+1)%3];
            const uint64 Key=A<B?(uint64(A)<<32|B):(uint64(B)<<32|A);if(!Seen.Contains(Key)){Seen.Add(Key);I.E.Add(FIntPoint(A,B));}}}
        return I;}();
    return Result;
}

void Glyph(FBuild& M,const FContext& C,const FCireAuraLayer& L,float A)
{
    const float S=C.Unit;const float G=FMath::Max(9.f,11.f*S)*L.Size*C.Pop;
    const FVector Center(0,0,C.Top+26*S*L.Height+FMath::Sin(C.Time*2.2f)*2*S);
    const FLinearColor P=Col(L.bSecondary?C.Secondary:C.Primary,A),K=Col(C.Core,A),Dim=Col(C.Secondary,A*.7f);
    M.Glow(Center,G*2.3f,Col(C.Primary,A*.45f));
    const FString Style=L.Style.ToString();const float W=G*.12f;
    if(Style==TEXT("crown")||Style==TEXT("spikes"))
    {
        const bool bIron=Style==TEXT("spikes");const int32 N=bIron?7:5;
        M.Line2(Center,{-G,-.35f*G},{G,-.35f*G},W*1.3f,K);M.Line2(Center,{-G,-.62f*G},{G,-.62f*G},W,P);
        for(int32 I=0;I<N;++I)
        {
            const float X=-G+2*G*(I+.5f)/N;const float H=bIron?(I%2?.55f:.95f)*G:(I==N/2?1.05f:.72f-.12f*FMath::Abs(I-N/2))*G;
            const float Hw=bIron?G*.1f:G*.17f;
            M.Fill2(Center,{X-Hw,-.35f*G},{X+Hw,-.35f*G},{X,-.35f*G+H},bIron?Dim:P);
            M.Line2(Center,{X-Hw,-.35f*G},{X,-.35f*G+H},W*.6f,K);M.Line2(Center,{X+Hw,-.35f*G},{X,-.35f*G+H},W*.6f,K);
            if(!bIron&&I%2==0)M.Star(Center+M.R*X+M.U*(-.35f*G+H+G*.12f),G*.16f,K,4,C.Time);
        }
    }
    else if(Style==TEXT("horn"))
    {
        // A roar: mouth mark with three expanding sound arcs.
        M.Fill2(Center,{-G*1.1f,-G*.25f},{-G*1.1f,G*.25f},{-G*.45f,0},P);
        for(int32 I=0;I<3;++I)
        {
            const float Ph=Fract(C.Time*1.3f+I/3.f);const float Rr=G*(.45f+Ph*1.1f);
            M.BillRing(Center-M.R*G*.9f,Rr,W*(1.4f-Ph*.6f),Col(I==0?C.Core:C.Primary,A*(1-Ph)),12,-PI*.3f,PI*.6f);
        }
    }
    else if(Style==TEXT("stars"))
    {
        for(int32 I=0;I<4;++I)
        {
            const float Ang=C.Time*3.2f+I*Tau/4;const FVector Pos=Center+FVector(FMath::Cos(Ang)*G*1.5f,FMath::Sin(Ang)*G*1.5f,FMath::Sin(Ang*2)*G*.15f-G*.4f);
            M.Star(Pos,G*.42f,K,5,C.Time*4+I);M.Glow(Pos,G*.7f,Col(C.Primary,A*.5f));
        }
    }
    else if(Style==TEXT("eye"))
    {
        for(int32 I=0;I<12;++I)
        {
            const float X0=-G+2*G*I/12.f,X1=-G+2*G*(I+1)/12.f;
            const float Y0=FMath::Sin((X0/G+1)*PI*.5f)*.55f*G,Y1=FMath::Sin((X1/G+1)*PI*.5f)*.55f*G;
            M.Line2(Center,{X0,Y0},{X1,Y1},W,P);M.Line2(Center,{X0,-Y0},{X1,-Y1},W,P);
        }
        M.Diamond(Center,G*.28f,K);M.BillRing(Center,G*.42f,W*.8f,Col(C.Core,A*.8f),14);
    }
    else if(Style==TEXT("chevron_down")||Style==TEXT("chevron_up"))
    {
        const float Dir=Style==TEXT("chevron_up")?1.f:-1.f;
        for(int32 I=0;I<2;++I)
        {
            const float Ph=Fract(C.Time*.9f+I*.5f);const float Y=Dir*(Ph-.5f)*G*1.4f;const float Fade=FMath::Sin(Ph*PI);
            const FLinearColor Cc=Col(I?C.Primary:C.Core,A*Fade);
            M.Line2(Center,{-G*.8f,Y-Dir*G*.35f},{0,Y+Dir*G*.35f},W*1.6f,Cc);M.Line2(Center,{0,Y+Dir*G*.35f},{G*.8f,Y-Dir*G*.35f},W*1.6f,Cc);
        }
    }
    else if(Style==TEXT("skull"))
    {
        M.BillRing(Center+M.U*G*.15f,G*.7f,W*1.1f,P,18);
        M.Diamond(Center+M.U*G*.15f-M.R*G*.28f,G*.16f,K);M.Diamond(Center+M.U*G*.15f+M.R*G*.28f,G*.16f,K);
        for(int32 I=-1;I<=1;++I)M.Line2(Center,{I*G*.22f,-G*.5f},{I*G*.22f,-G*.85f},W*.9f,P);
        M.Line2(Center,{-G*.35f,-G*.5f},{G*.35f,-G*.5f},W,P);
    }
    else if(Style==TEXT("shield"))
    {
        const FVector2D Pts[]={{-G*.8f,G*.8f},{G*.8f,G*.8f},{G*.75f,-G*.1f},{0,-G},{-G*.75f,-G*.1f}};
        for(int32 I=0;I<5;++I){M.Line2(Center,Pts[I],Pts[(I+1)%5],W*1.2f,P);M.Fill2(Center,{0,0},Pts[I],Pts[(I+1)%5],Col(C.Secondary,A*.25f));}
        M.Line2(Center,{0,G*.6f},{0,-G*.7f},W,K);M.Line2(Center,{-G*.5f,G*.25f},{G*.5f,G*.25f},W,K);
    }
    else if(Style==TEXT("exclamation"))
    {
        M.Fill2(Center,{-G*.28f,G*1.1f},{G*.28f,G*1.1f},{0,-G*.3f},P);M.Line2(Center,{0,G*1.05f},{0,-G*.2f},W*.7f,K);
        M.Diamond(Center-M.U*G*.72f,G*.2f,K);
    }
    else if(Style==TEXT("sun"))
    {
        M.BillRing(Center,G*.5f,W*1.4f,K,18);
        for(int32 I=0;I<10;++I){const float Ang=C.Time*.6f+I*Tau/10;const FVector2D D(FMath::Cos(Ang),FMath::Sin(Ang));
            M.Line2(Center,D*G*.62f,D*G*(I%2?.95f:1.2f),W*.9f,P);}
    }
    else // drop
    {
        for(int32 I=0;I<14;++I)
        {
            const float A0=-PI*.5f+Tau*I/14,A1=-PI*.5f+Tau*(I+1)/14;
            auto Shape=[&](float Ang){const float Pinch=FMath::Max(0.f,FMath::Sin(Ang));return FVector2D(FMath::Cos(Ang)*G*.55f*(1-Pinch*.75f),FMath::Sin(Ang)*G*.55f+Pinch*G*.45f);};
            M.Line2(Center,Shape(A0),Shape(A1),W,P);M.Fill2(Center,{0,0},Shape(A0),Shape(A1),Col(C.Secondary,A*.3f));
        }
    }
}

void Motes(FBuild& M,const FContext& C,const FCireAuraLayer& L,float A)
{
    const float S=C.Unit;const FString Style=L.Style.ToString();
    const bool bDown=Style==TEXT("snow")||Style==TEXT("chevron_down")||Style==TEXT("drop");
    const int32 N=FMath::Clamp(FMath::RoundToInt(L.Count*(.55f+.45f*C.Intensity)*(C.Detail>1?1.f:.5f)),1,48);
    const FLinearColor P=L.bSecondary?C.Secondary:C.Primary;
    for(int32 K=0;K<N;++K)
    {
        const float H1=Hash(C.Seed,K*4+1),H2=Hash(C.Seed,K*4+2),H3=Hash(C.Seed,K*4+3),H4=Hash(C.Seed,K*4+4);
        const float Ph=Fract(C.Time*L.Speed*.32f*(.7f+.6f*H1)+H2);
        const float Ang=H3*Tau+Ph*(H4-.5f)*3.f+C.Time*.2f;
        const float Rad=C.Radius*(.55f+.95f*H4)*L.Size;
        const float Z=bDown?C.Top+8*S-Ph*C.Height*1.05f*L.Height:C.Feet+Ph*C.Height*1.15f*L.Height;
        const FVector Pos=Polar(Rad,Ang,Z);
        const float Fade=A*FMath::Sin(Ph*PI);
        const float Sz=S*(.8f+.5f*H1)*L.Size;
        if(Style==TEXT("bubble"))
        {
            M.BillRing(Pos,3.6f*Sz*(.6f+Ph),.9f*Sz,Col(P,Fade),10);M.Diamond(Pos+M.U*1.4f*Sz+M.R*1.2f*Sz,.8f*Sz,Col(C.Core,Fade));
            M.Glow(Pos,6*Sz,Col(P,Fade*.25f));
        }
        else if(Style==TEXT("glint"))
        {M.Star(Pos,5.5f*Sz,Col(C.Core,Fade),4,C.Time*2+K);M.Glow(Pos,7*Sz,Col(P,Fade*.45f));}
        else if(Style==TEXT("chevron_up")||Style==TEXT("chevron_down"))
        {
            const float D=bDown?-1.f:1.f;const FVector Tip=Pos+FVector(0,0,D*4*Sz);
            M.Ribbon(Pos-M.R*5*Sz,Tip,1.4f*Sz,Col(P,Fade));M.Ribbon(Tip,Pos+M.R*5*Sz,1.4f*Sz,Col(P,Fade));
            M.Glow(Pos,6*Sz,Col(P,Fade*.3f));
        }
        else if(Style==TEXT("leaf"))
        {
            const float Spin=C.Time*2+K;const FVector D=(M.R*FMath::Cos(Spin)+M.U*FMath::Sin(Spin))*5*Sz;const FVector Side=(M.U*FMath::Cos(Spin)-M.R*FMath::Sin(Spin))*2.2f*Sz;
            M.Tri(Pos-D,Pos+Side,Pos+D,Col(P,Fade));M.Tri(Pos-D,Pos+D,Pos-Side,Col(C.Core,Fade*.8f));
        }
        else if(Style==TEXT("snow"))
        {
            for(int32 Arm=0;Arm<3;++Arm){const float Ang2=Arm*PI/3+C.Time;const FVector D=(M.R*FMath::Cos(Ang2)+M.U*FMath::Sin(Ang2))*3.6f*Sz;M.Ribbon(Pos-D,Pos+D,.6f*Sz,Col(C.Core,Fade));}
            M.Glow(Pos,5*Sz,Col(P,Fade*.3f));
        }
        else if(Style==TEXT("plus"))
        {M.Ribbon(Pos-M.U*4*Sz,Pos+M.U*4*Sz,1.2f*Sz,Col(P,Fade));M.Ribbon(Pos-M.R*4*Sz,Pos+M.R*4*Sz,1.2f*Sz,Col(P,Fade));M.Glow(Pos,6*Sz,Col(C.Core,Fade*.3f));}
        else if(Style==TEXT("drop"))
        {M.Shard(Pos,FVector(0,0,-1),7*Sz,1.7f*Sz,Col(P,Fade),Col(C.Core,Fade));}
        else // ember / spark
        {M.Diamond(Pos,2.1f*Sz,Col(C.Core,Fade));M.Glow(Pos,6.5f*Sz,Col(P,Fade*.5f));}
    }
}
} // namespace

bool ParseShape(const FString& Name,ECireAuraShape& Out)
{
    static const TMap<FString,ECireAuraShape> Map={{TEXT("ring"),ECireAuraShape::Ring},{TEXT("ripple"),ECireAuraShape::Ripple},
        {TEXT("crystals"),ECireAuraShape::Crystals},{TEXT("pool"),ECireAuraShape::Pool},{TEXT("cracks"),ECireAuraShape::Cracks},
        {TEXT("swirl"),ECireAuraShape::Swirl},{TEXT("shell"),ECireAuraShape::Shell},{TEXT("plates"),ECireAuraShape::Plates},
        {TEXT("aegis"),ECireAuraShape::Aegis},{TEXT("flames"),ECireAuraShape::Flames},{TEXT("motes"),ECireAuraShape::Motes},
        {TEXT("drips"),ECireAuraShape::Drips},{TEXT("chains"),ECireAuraShape::Chains},{TEXT("halo"),ECireAuraShape::Halo},
        {TEXT("hands"),ECireAuraShape::Hands},{TEXT("weapon"),ECireAuraShape::Weapon},{TEXT("glyph"),ECireAuraShape::Glyph},
        {TEXT("tether"),ECireAuraShape::Tether},{TEXT("tower"),ECireAuraShape::Tower}};
    if(const auto* Found=Map.Find(Name.ToLower())){Out=*Found;return true;}
    return false;
}
const TCHAR* ShapeName(ECireAuraShape Shape)
{
    static const TCHAR* Names[]={TEXT("ring"),TEXT("ripple"),TEXT("crystals"),TEXT("pool"),TEXT("cracks"),TEXT("swirl"),TEXT("shell"),TEXT("plates"),
        TEXT("aegis"),TEXT("flames"),TEXT("motes"),TEXT("drips"),TEXT("chains"),TEXT("halo"),TEXT("hands"),TEXT("weapon"),TEXT("glyph"),TEXT("tether"),TEXT("tower")};
    const int32 I=static_cast<int32>(Shape);return I>=0&&I<UE_ARRAY_COUNT(Names)?Names[I]:TEXT("unknown");
}

void DrawAllegianceRim(const FContext& C,FLinearColor Rim,float Alpha,FBuffers& B)
{
    FBuild M(B,C);const float S=C.Unit;
    M.FlatRing(FVector(0,0,C.Feet+1.5f),FMath::Max(C.Radius*1.3f,44*S),1.8f*S,Col(Rim,Alpha*.7f),C.Time*.2f,Tau,40);
}

void DrawLayer(const FCireAuraLayer& L,const FContext& C,FBuffers& B)
{
    FBuild M(B,C);
    const float S=C.Unit,A=FMath::Clamp(C.Alpha*L.Alpha,0.f,1.f),Rad=C.Radius,H=C.Height;
    if(A<=.004f)return;
    const FLinearColor P=L.bSecondary?C.Secondary:C.Primary,Sec=L.bSecondary?C.Primary:C.Secondary,K=C.Core;
    const float T=C.Time*L.Speed;
    const int32 Detail=C.Detail;
    switch(L.Shape)
    {
    case ECireAuraShape::Ring:
    {
        const float Rr=FMath::Max(Rad*1.7f,56*S)*L.Size*C.Pop;const FVector Ctr(0,0,C.Feet+3);const float Spin=T*.6f;
        const bool bDashed=L.Style==TEXT("dashed");
        if(bDashed){const int32 N=FMath::Max(4,L.Count);for(int32 I=0;I<N;++I)M.FlatRing(Ctr,Rr,3.4f*S,Col(P,A*.9f),Spin+I*Tau/N,Tau/N*.62f,8);}
        else M.FlatRing(Ctr,Rr,3.4f*S,Col(P,A*.9f),Spin,Tau,56);
        M.FlatRing(Ctr,Rr*.8f,1.6f*S,Col(Sec,A*.65f),-Spin*1.3f,Tau,48);
        const int32 Ticks=FMath::Clamp(L.Count,0,24);
        for(int32 I=0;I<Ticks;++I)
        {
            const float Ang=Spin+I*Tau/FMath::Max(1,Ticks);
            M.Tube(Ctr+Polar(Rr*.82f,Ang),Ctr+Polar(Rr*.97f,Ang),1.1f*S,Col(K,A*.85f),3);
            if(L.Style==TEXT("runes")&&I%2==0)
            {
                const FVector Pp=Ctr+Polar(Rr*.9f,Ang+PI/Ticks),X=Polar(5*S,Ang),Y=Polar(3*S,Ang+PI*.5f);
                M.Tube(Pp-X,Pp+X,.7f*S,Col(K,A*.7f),3);M.Tube(Pp-X,Pp+Y,.7f*S,Col(K,A*.7f),3);M.Tube(Pp+Y,Pp+X,.7f*S,Col(K,A*.7f),3);
            }
        }
        M.FlatGlow(Ctr+FVector(0,0,.5f),Rr*1.2f,Col(P,A*.28f));
        break;
    }
    case ECireAuraShape::Ripple:
    {
        const FVector Ctr(0,0,C.Feet+4);
        for(int32 I=0;I<3;++I)
        {
            const float Ph=Fract(T*.7f+I/3.f);const float Rr=Rad*(1.1f+Ph*3.3f*L.Size);const float Fade=A*FMath::Pow(1-Ph,1.3f);
            M.FlatRing(Ctr,Rr,(5.5f*(1-Ph)+1.2f)*S,Col(I==0?K:P,Fade),0,Tau,56);
            if(L.Style==TEXT("shock"))M.FlatRing(Ctr,Rr*.94f,1.2f*S,Col(Sec,Fade*.8f),0,Tau,40);
        }
        M.FlatGlow(Ctr,Rad*1.6f*L.Size,Col(P,A*.22f));
        break;
    }
    case ECireAuraShape::Crystals:
    {
        const int32 N=FMath::Clamp(FMath::RoundToInt(L.Count*(.7f+.3f*C.Intensity)),3,16);const float Grow=FMath::Clamp(C.Age/.35f,.15f,1.f);
        for(int32 I=0;I<N;++I)
        {
            const float H1=Hash(C.Seed,I*3+11),H2=Hash(C.Seed,I*3+12),H3=Hash(C.Seed,I*3+13);
            const float Ang=I*2.39996f+H1*.4f;const float Rr=Rad*(.85f+.65f*H2)*L.Size;
            const FVector Base=Polar(Rr,Ang,C.Feet);const FVector Dir=(Polar(.5f+.3f*H3,Ang)+FVector(0,0,1));
            const float Len=(24+26*H3)*S*L.Size*L.Height*Grow;
            M.Shard(Base,Dir,Len,(4.2f+2*H1)*S,Col(P,A*.85f),Col(K,A),Ang);
            M.Shard(Base+Polar(6*S,Ang+1.3f),Dir+Polar(.4f,Ang+1.6f),Len*.55f,2.6f*S,Col(Sec,A*.75f),Col(K,A*.9f),Ang+.7f);
            M.FlatGlow(Base+FVector(0,0,1),10*S,Col(P,A*.35f));
        }
        M.FlatRing(FVector(0,0,C.Feet+1.5f),Rad*1.65f*L.Size,2.2f*S,Col(Sec,A*.6f),T*.1f,Tau,48);
        M.FlatGlow(FVector(0,0,C.Feet+1),Rad*1.9f*L.Size,Col(P,A*.2f));
        break;
    }
    case ECireAuraShape::Pool:
    {
        const FVector Ctr(0,0,C.Feet+1.5f);const float Rr=Rad*1.85f*L.Size*C.Pop;
        M.FlatGlow(Ctr,Rr*1.15f,Col(P,A*.6f));M.FlatGlow(Ctr,Rr*.7f,Col(Sec,A*.45f));
        for(int32 J=0;J<48;++J)
        {
            const float A0=Tau*J/48,A1=Tau*(J+1)/48;
            auto Edge=[&](float Ang){return Rr*(1+.08f*FMath::Sin(Ang*5+T*1.3f)+.05f*FMath::Sin(Ang*11-T));};
            M.Quad(Ctr+Polar(Edge(A0),A0),Ctr+Polar(Edge(A1),A1),Ctr+Polar(Edge(A1)-2.6f*S,A1),Ctr+Polar(Edge(A0)-2.6f*S,A0),Col(K,A*.75f));
        }
        const int32 Blobs=Detail>1?10:5;
        for(int32 I=0;I<Blobs;++I)
        {
            const float H1=Hash(C.Seed,I+40),H2=Hash(C.Seed,I+60);const FVector Bp=Ctr+Polar(Rr*(.3f+.6f*H1),H2*Tau,.3f);
            const float Br=(4+5*H2)*S*(1+.15f*FMath::Sin(T*2+I));
            for(int32 J=0;J<6;++J)M.Tri(Bp,Bp+Polar(Br,Tau*J/6),Bp+Polar(Br,Tau*(J+1)/6),Col(P,A*.55f),Col(P,A*.2f),Col(P,A*.2f));
        }
        for(int32 I=0;I<(Detail>1?5:2);++I)
        {
            const float Ph=Fract(T*.8f+Hash(C.Seed,I+80));const float Ang=Hash(C.Seed,I+90)*Tau;
            const FVector Dp=Ctr+Polar(Rr*(.5f+.4f*Ph),Ang,FMath::Sin(Ph*PI)*14*S);
            M.Shard(Dp,FVector(0,0,Ph<.5f?1:-1),4*S,1.4f*S,Col(P,A*(1-Ph)),Col(K,A*(1-Ph)));
        }
        break;
    }
    case ECireAuraShape::Cracks:
    {
        const int32 N=FMath::Clamp(L.Count,3,12);const FVector Ctr(0,0,C.Feet+1.8f);
        for(int32 I=0;I<N;++I)
        {
            const float Pulse=.55f+.45f*FMath::Sin(T*3+I*1.7f);FVector Prev=Ctr+Polar(Rad*.55f,I*Tau/N);float Ang=I*Tau/N;
            for(int32 J=1;J<=5;++J)
            {
                Ang+=(Hash(C.Seed,I*9+J)-.5f)*.7f;const FVector Next=Ctr+Polar(Rad*(.55f+J*.42f)*L.Size,Ang);
                const float W0=(4.2f-J*.6f)*S,W1=(4.2f-(J+1)*.6f)*S;const FVector Side0=FVector::CrossProduct(Next-Prev,FVector::UpVector).GetSafeNormal();
                M.Quad(Prev-Side0*W0,Prev+Side0*W0,Next+Side0*FMath::Max(.4f,W1),Next-Side0*FMath::Max(.4f,W1),Col(Mix(K,P,J/5.f),A*Pulse),Col(Mix(K,P,J/5.f),A*Pulse),Col(Mix(K,P,(J+1)/5.f),A*Pulse*.7f),Col(Mix(K,P,(J+1)/5.f),A*Pulse*.7f));
                if(J==2&&Detail>1)M.FlatGlow(Next,9*S,Col(P,A*.35f*Pulse));
                Prev=Next;
            }
        }
        M.FlatGlow(Ctr,Rad*1.7f*L.Size,Col(P,A*.3f));
        break;
    }
    case ECireAuraShape::Swirl:
    {
        const int32 Strands=FMath::Clamp(L.Count,1,6);const int32 Steps=Detail>1?26:14;
        for(int32 Sd=0;Sd<Strands;++Sd)
        {
            FVector Prev;FVector PrevW;FLinearColor PrevC;FLinearColor PrevK;
            for(int32 J=0;J<=Steps;++J)
            {
                const float U=J/float(Steps);
                const float Ang=Sd*Tau/Strands+U*PI*2.3f*L.Height+T*2.1f;
                const float Rr=Rad*1.3f*L.Size*(1-.28f*U)*C.Pop+FMath::Sin(U*9+T*3+Sd)*2*S;
                const FVector Pt=Polar(Rr,Ang,C.Feet+4*S+U*H*1.02f*L.Height);
                const float Flow=.45f+.55f*FMath::Pow(.5f+.5f*FMath::Sin(U*13-T*7+Sd*2),2.f);
                const float Env=FMath::Pow(FMath::Sin(U*PI),.7f);
                const FVector Wv=FVector(0,0,1)*(3.5f+4.5f*Env)*S+Polar(1.5f*S,Ang);
                const FLinearColor Cc=Col(Mix(Sec,P,U*1.6f),A*Env*Flow),Kk=Col(Mix(P,K,Flow),A*Env*Flow);
                if(J>0){M.Band(Prev,Pt,PrevW,Wv,PrevC,Cc);M.Band(Prev,Pt,PrevW*.3f,Wv*.3f,PrevK,Kk);}
                Prev=Pt;PrevW=Wv;PrevC=Cc;PrevK=Kk;
            }
            const float Hu=Fract(T*.55f+Sd/float(Strands));const float Ha=Sd*Tau/Strands+Hu*PI*2.3f*L.Height+T*2.1f;
            M.Glow(Polar(Rad*1.3f*L.Size*(1-.28f*Hu),Ha,C.Feet+Hu*H*L.Height),13*S,Col(P,A*.6f*FMath::Sin(Hu*PI)));
        }
        M.Glow(FVector(0,0,C.Feet+H*.45f),Rad*1.9f,Col(P,A*.16f));
        break;
    }
    case ECireAuraShape::Shell:
    {
        const FIco& I=Ico();const FVector Ctr(0,0,C.Feet+H*.53f);
        const FVector Ext(Rad*1.5f*L.Size*C.Pop,Rad*1.5f*L.Size*C.Pop,H*.58f*L.Height*C.Pop);
        const float Yaw=T*.35f,Cy=FMath::Cos(Yaw),Sy=FMath::Sin(Yaw);const float Breath=1+.02f*FMath::Sin(C.Time*2.4f);
        auto World=[&](const FVector& V){return Ctr+FVector((V.X*Cy-V.Y*Sy)*Ext.X,(V.X*Sy+V.Y*Cy)*Ext.Y,V.Z*Ext.Z)*Breath;};
        const bool bHex=L.Style==TEXT("hex");
        for(const auto& E:I.E)
        {
            const FVector A0=World(I.V[E.X]),A1=World(I.V[E.Y]);const FVector Mid=(A0+A1)*.5f;
            const float F=M.Fresnel(Mid,Mid-Ctr);if(bHex&&((E.X+E.Y)%3==0))continue;
            M.Tube(A0,A1,(bHex?1.1f:.8f)*S,Col(Mix(P,K,F*F),A*(.12f+.78f*F*F)),3);
        }
        if(Detail>1)for(const auto& Fc:I.F)
        {
            const FVector A0=World(I.V[Fc.X]),A1=World(I.V[Fc.Y]),A2=World(I.V[Fc.Z]);const FVector Mid=(A0+A1+A2)/3.f;
            const float F=M.Fresnel(Mid,Mid-Ctr);M.Tri(A0,A1,A2,Col(Sec,A*.28f*F*F*F));
        }
        M.Glow(Ctr,FMath::Max(Ext.X,Ext.Z)*1.1f,Col(P,A*.12f));
        break;
    }
    case ECireAuraShape::Plates:
    {
        const int32 N=FMath::Clamp(L.Count,2,8);
        for(int32 I=0;I<N;++I)
        {
            const float Ang=T*.9f+I*Tau/N;const FVector Nrm=Polar(1,Ang),Tan=Polar(1,Ang+PI*.5f),Up(0,0,1);
            const FVector Ctr=Polar(Rad*1.4f*L.Size*C.Pop,Ang,C.Feet+H*.5f*L.Height+FMath::Sin(C.Time*2+I)*3*S);
            const float Wd=13*S*L.Size,Ht=17*S*L.Size;const float F=M.Fresnel(Ctr,Nrm);
            const FVector C00=Ctr-Tan*Wd-Up*Ht,C10=Ctr+Tan*Wd-Up*Ht,C11=Ctr+Tan*Wd+Up*Ht,C01=Ctr-Tan*Wd+Up*Ht,Bulge=Nrm*4*S;
            M.Quad(C00,C10,C11+Bulge*.2f,C01+Bulge*.2f,Col(Sec,A*(.25f+.25f*F)));M.Quad(C00+Bulge,C10+Bulge,C11+Bulge,C01+Bulge,Col(Sec,A*.12f));
            M.Tube(C00,C10,1.3f*S,Col(P,A*.9f));M.Tube(C10,C11,1.3f*S,Col(P,A*.9f));M.Tube(C11,C01,1.3f*S,Col(P,A*.9f));M.Tube(C01,C00,1.3f*S,Col(P,A*.9f));
            M.Tube(Ctr-Up*Ht*.7f,Ctr+Up*Ht*.7f,.8f*S,Col(K,A*.6f));
            M.Diamond(Ctr+Nrm*2*S,2.4f*S,Col(K,A));M.Glow(Ctr,20*S,Col(P,A*.18f));
        }
        break;
    }
    case ECireAuraShape::Aegis:
    {
        const int32 N=FMath::Clamp(L.Count,1,6);
        for(int32 I=0;I<N;++I)
        {
            const float Ang=T*.5f+I*Tau/N;const FVector Nrm=Polar(1,Ang),Tan=Polar(1,Ang+PI*.5f),Up(0,0,1);
            const FVector Ctr=Polar(Rad*1.6f*L.Size*C.Pop,Ang,C.Feet+H*.62f*L.Height+FMath::Sin(C.Time*1.6f+I*2)*4*S);
            const float G=18*S*L.Size;const float F=M.Fresnel(Ctr,Nrm);
            const FVector Pts[]={Ctr-Tan*G*.8f+Up*G*.9f,Ctr+Tan*G*.8f+Up*G*.9f,Ctr+Tan*G*.85f-Up*G*.1f,Ctr-Up*G*1.25f,Ctr-Tan*G*.85f-Up*G*.1f};
            for(int32 J=0;J<5;++J)
            {
                M.Tri(Ctr+Nrm*2*S,Pts[J],Pts[(J+1)%5],Col(K,A*.2f),Col(Sec,A*(.18f+.3f*F)),Col(Sec,A*(.18f+.3f*F)));
                M.Tube(Pts[J],Pts[(J+1)%5],1.4f*S,Col(K,A*.95f));
            }
            M.Tube(Ctr+Up*G*.7f+Nrm*2*S,Ctr-Up*G*.95f+Nrm*2*S,1.1f*S,Col(P,A*.9f));M.Tube(Ctr-Tan*G*.5f+Up*G*.3f+Nrm*2*S,Ctr+Tan*G*.5f+Up*G*.3f+Nrm*2*S,1.1f*S,Col(P,A*.9f));
            M.Glow(Ctr,G*1.7f,Col(P,A*.28f));
        }
        break;
    }
    case ECireAuraShape::Flames:
    {
        const int32 N=FMath::Clamp(FMath::RoundToInt(L.Count*(.7f+.3f*C.Intensity)),3,14);const int32 Steps=Detail>1?7:4;
        for(int32 I=0;I<N;++I)
        {
            const float H1=Hash(C.Seed,I+100);const float Ang=I*Tau/N+FMath::Sin(C.Time*.8f+I)*.2f;
            const FVector Base=Polar(Rad*1.05f*L.Size,Ang,C.Feet+2);const FVector Tan=Polar(1,Ang+PI*.5f),Rd=Polar(1,Ang);
            const float Fh=H*(.3f+.22f*H1)*L.Height*(.82f+.18f*FMath::Sin(C.Time*7.3f+I*3))*C.Pop;const float Fw=(8+4*H1)*S*L.Size;
            FVector Prev=Base;
            for(int32 J=1;J<=Steps;++J)
            {
                const float U0=(J-1)/float(Steps),U=J/float(Steps);
                const FVector Pt=Base+FVector(0,0,U*Fh)+Tan*FMath::Sin(U*4+T*8.5f+I)*5*S*U-Rd*U*7*S;
                const FLinearColor C0=Col(U0<.3f?K:Mix(P,Sec,U0),A*(1-U0*.8f)),C1=Col(U<.3f?K:Mix(P,Sec,U),A*(1-U)*.9f);
                M.Ribbon(Prev,Pt,Fw*FMath::Pow(1-U0,.8f),Fw*FMath::Pow(1-U,.8f),C0,C1);Prev=Pt;
            }
        }
        const int32 Embers=Detail>1?8:3;
        for(int32 I=0;I<Embers;++I)
        {
            const float Ph=Fract(C.Time*.9f+Hash(C.Seed,I+130));const FVector Pos=Polar(Rad*(.8f+.6f*Hash(C.Seed,I+140)),Hash(C.Seed,I+150)*Tau+Ph,C.Feet+Ph*H*.9f);
            M.Diamond(Pos,1.8f*S,Col(K,A*FMath::Sin(Ph*PI)));
        }
        M.Glow(FVector(0,0,C.Feet+H*.25f),Rad*1.8f,Col(P,A*.26f));
        break;
    }
    case ECireAuraShape::Motes: Motes(M,C,L,A); break;
    case ECireAuraShape::Drips:
    {
        const int32 N=FMath::Clamp(FMath::RoundToInt(L.Count*(.6f+.4f*C.Intensity)),1,16);
        for(int32 I=0;I<N;++I)
        {
            const float H1=Hash(C.Seed,I+200),H2=Hash(C.Seed,I+210),H3=Hash(C.Seed,I+220);
            const float Ph=Fract(T*.55f*(.8f+.4f*H1)+H2);const float Ang=H3*Tau;
            const FVector Start=Polar(Rad*(.45f+.45f*H1)*L.Size,Ang,C.Feet+H*(.35f+.35f*H2));
            const float Fall=FMath::Square(Ph);const FVector Pos=FMath::Lerp(Start,FVector(Start.X,Start.Y,C.Feet),Fall);
            if(Ph<.92f)M.Shard(Pos,FVector(0,0,-1),(4+8*Fall)*S*L.Size,1.8f*S*L.Size,Col(P,A*.95f),Col(K,A),Ang);
            else M.FlatRing(FVector(Start.X,Start.Y,C.Feet+1),(3+(Ph-.92f)*110)*S,1.2f*S,Col(P,A*(1-Ph)*8),0,Tau,12);
        }
        break;
    }
    case ECireAuraShape::Chains:
    {
        const int32 N=FMath::Clamp(L.Count,6,24);const float Rr=Rad*1.4f*L.Size*C.Pop;const float Z=C.Feet+H*.45f*L.Height+FMath::Sin(C.Time*1.5f)*2*S;
        const float Spin=T*.7f;
        for(int32 I=0;I<N;++I)
        {
            const float Ang=Spin+I*Tau/N;const FVector Ctr=Polar(Rr,Ang,Z);const FVector Tan=Polar(1,Ang+PI*.5f);
            const FVector Other=I%2?FVector(0,0,1):Polar(1,Ang);const float Len=Rr*Tau/N*.62f,Wd=3.4f*S;
            FVector Prev;
            for(int32 J=0;J<=10;++J)
            {
                const float Bt=Tau*J/10;const FVector Pt=Ctr+Tan*FMath::Cos(Bt)*Len+Other*FMath::Sin(Bt)*Wd;
                if(J>0)M.Tube(Prev,Pt,1.05f*S,Col(I%3==0?K:P,A*.95f),3);Prev=Pt;
            }
        }
        M.Glow(FVector(0,0,Z),Rr*1.1f,Col(P,A*.14f));
        break;
    }
    case ECireAuraShape::Halo:
    {
        const FVector Ctr=FVector(0,0,C.Top+4*S*L.Height)+C.CamLook*Rad*.7f;const float G=14*S*L.Size*C.Pop;
        const int32 N=FMath::Clamp(L.Count,6,28);
        for(int32 I=0;I<N;++I)
        {
            const float Ang=T*.35f+I*Tau/N,Half=PI/N*.45f;const float Outer=G*(I%2?2.1f:2.7f)*(1+.05f*FMath::Sin(C.Time*3+I));
            const FVector D0=M.R*FMath::Cos(Ang-Half)+M.U*FMath::Sin(Ang-Half),D1=M.R*FMath::Cos(Ang+Half)+M.U*FMath::Sin(Ang+Half),Dm=M.R*FMath::Cos(Ang)+M.U*FMath::Sin(Ang);
            M.Tri(Ctr+D0*G*1.05f,Ctr+D1*G*1.05f,Ctr+Dm*Outer,Col(K,A*.9f),Col(K,A*.9f),Col(P,A*.1f));
        }
        M.BillRing(Ctr,G,1.8f*S,Col(K,A),28);M.BillRing(Ctr,G*.8f,.9f*S,Col(P,A*.8f),28);
        M.Glow(Ctr,G*3.4f,Col(P,A*.4f));
        break;
    }
    case ECireAuraShape::Hands:
    case ECireAuraShape::Weapon:
    {
        const bool bWeapon=L.Shape==ECireAuraShape::Weapon&&C.Weapon.Num()>=2;
        if(bWeapon)
        {
            const FVector W0=C.Weapon[0],W1=C.Weapon[1];
            for(int32 I=0;I<=5;++I){const FVector Pt=FMath::Lerp(W0,W1,I/5.f);M.Glow(Pt,(8+3*FMath::Sin(C.Time*6+I))*S*L.Size,Col(P,A*.45f));}
            M.Ribbon(W0,W1,2.6f*S,1.2f*S,Col(K,A*.75f),Col(K,A*.95f));
            for(int32 I=0;I<(Detail>1?6:2);++I)
            {
                const float Ph=Fract(T*1.2f+Hash(C.Seed,I+300));const FVector Pt=FMath::Lerp(W0,W1,Hash(C.Seed,I+310))+FVector(0,0,Ph*16*S)+Polar(Ph*6*S,Hash(C.Seed,I+320)*Tau);
                M.Diamond(Pt,1.5f*S,Col(L.Style==TEXT("frost")?K:P,A*(1-Ph)));
            }
            break;
        }
        TArray<FVector,TInlineAllocator<2>> Pts=C.Hands;
        if(Pts.IsEmpty()){Pts.Add(FVector(0,0,C.Feet+H*.5f)+FVector::CrossProduct(FVector::UpVector,C.Forward)*Rad*.9f);Pts.Add(FVector(0,0,C.Feet+H*.5f)-FVector::CrossProduct(FVector::UpVector,C.Forward)*Rad*.9f);}
        for(int32 Hn=0;Hn<Pts.Num();++Hn)
        {
            const FVector Pt=Pts[Hn];const float Hs=S*L.Size;
            M.Glow(Pt,13*Hs,Col(P,A*.55f));M.Glow(Pt,5.5f*Hs,Col(K,A*.7f));
            for(int32 Rg=0;Rg<2;++Rg)
            {
                const float Tilt=T*(Rg?2.6f:-2.1f)+Hn;const FVector Ax=FVector(FMath::Cos(Tilt),FMath::Sin(Tilt),.8f).GetSafeNormal();FVector X,Y;Ax.FindBestAxisVectors(X,Y);
                FVector Prev;for(int32 J=0;J<=14;++J){const float Bt=Tau*J/14*.8f+T*4;const FVector Q=Pt+(X*FMath::Cos(Bt)+Y*FMath::Sin(Bt))*(7+Rg*2.5f)*Hs;
                    if(J>0)M.Tube(Prev,Q,.65f*Hs,Col(Rg?Sec:P,A*(.35f+.6f*J/14.f)),3);Prev=Q;}
            }
            if(Detail>1)for(int32 Wp=0;Wp<3;++Wp)
            {
                FVector Prev=Pt;
                for(int32 J=1;J<=5;++J)
                {
                    const float U=J/5.f;const float Ph=T*3+Wp*2.1f+Hn;
                    const FVector Q=Pt+FVector(FMath::Cos(Ph+U*3)*5*Hs*U,FMath::Sin(Ph+U*3)*5*Hs*U,U*24*Hs);
                    M.Ribbon(Prev,Q,2.2f*Hs*(1.2f-U),2.2f*Hs*(1-U),Col(Mix(P,Sec,U),A*(1-U+.2f)*.8f),Col(Mix(P,Sec,U),A*(1-U)*.7f));Prev=Q;
                }
            }
        }
        break;
    }
    case ECireAuraShape::Glyph: Glyph(M,C,L,A); break;
    case ECireAuraShape::Tether:
    {
        if(!C.bHasLink)break;
        const FVector From(0,0,C.Feet+H*.6f),To=C.Link;const int32 Steps=16;FVector Prev=From;
        const FVector Side=FVector::CrossProduct(To-From,C.CamLook).GetSafeNormal();
        for(int32 J=1;J<=Steps;++J)
        {
            const float U=J/float(Steps);const float Wob=FMath::Sin(U*PI*7-T*10)*4*S*FMath::Sin(U*PI);
            const FVector Q=FMath::Lerp(From,To,U)+Side*Wob;const float Pulse=.55f+.45f*FMath::Sin(U*20-T*9);
            M.Ribbon(Prev,Q,2.2f*S,Col(P,A*Pulse));M.Ribbon(Prev,Q,.8f*S,Col(K,A*Pulse));
            if(L.Style==TEXT("chain")&&J%2==0)M.Diamond(Q,2.2f*S,Col(K,A));
            Prev=Q;
        }
        M.Glow(To,12*S,Col(P,A*.5f));
        break;
    }
    case ECireAuraShape::Tower:
    {
        const FVector Fw=C.Forward.GetSafeNormal2D().IsNearlyZero()?FVector::ForwardVector:C.Forward.GetSafeNormal2D();
        const FVector Sd=FVector::CrossProduct(FVector::UpVector,Fw),Up(0,0,1);
        const FVector Ctr=Fw*Rad*1.55f+FVector(0,0,C.Feet+H*.44f*L.Height);const float W=Rad*1.25f*L.Size*C.Pop,Hh=H*.4f*L.Height*C.Pop;
        auto Pt=[&](float X,float Y){return Ctr+Sd*X*W+Up*Y*Hh+Fw*(1-X*X)*6*S;};
        const FVector Outline[]={Pt(-1,1),Pt(-.35f,1.08f),Pt(.35f,1.08f),Pt(1,1),Pt(1,-.45f),Pt(.45f,-.85f),Pt(0,-1),Pt(-.45f,-.85f),Pt(-1,-.45f)};
        const int32 N=UE_ARRAY_COUNT(Outline);const float F=M.Fresnel(Ctr,Fw);const float Pulse=.8f+.2f*FMath::Sin(C.Time*3);
        for(int32 J=0;J<N;++J)
        {
            M.Tri(Ctr+Fw*6*S,Outline[J],Outline[(J+1)%N],Col(Sec,A*.14f),Col(Sec,A*(.22f+.25f*F)),Col(Sec,A*(.22f+.25f*F)));
            M.Tube(Outline[J],Outline[(J+1)%N],1.7f*S,Col(P,A*Pulse),3);
        }
        M.Tube(Pt(0,.9f),Pt(0,-.85f),1.1f*S,Col(K,A*.8f));M.Tube(Pt(-.85f,.25f),Pt(.85f,.25f),1.1f*S,Col(K,A*.8f));
        for(int32 J=0;J<4;++J){const float Y=.75f-J*.4f;M.Tube(Pt(-.9f,Y),Pt(.9f,Y),.5f*S,Col(P,A*.35f));}
        M.Diamond(Pt(0,.25f)+Fw*3*S,4*S,Col(K,A));M.Glow(Ctr,W*1.5f,Col(P,A*.25f*Pulse));
        break;
    }
    }
}

namespace
{
FLinearColor HitColor(const FCireAuraAttack& A,float U){return Mix(A.Primary,A.Core,U);}
void Extras(FBuild& M,const FCireAuraAttack& A,const FVector& Pos,const FVector& Vel,float Life,float Scale,uint32 Seed,float Alpha)
{
    const FString Style=A.Swipe.ToString();const float S=Scale;
    if(Style==TEXT("blood")||Style==TEXT("venom"))
    {
        const FVector P=Pos+Vel*Life+FVector(0,0,-420*Life*Life);const FVector Dir=Vel+FVector(0,0,-840*Life);
        M.Shard(P,Dir,(5+4*Hash(Seed))*S,1.8f*S,Col(A.Primary,Alpha),Col(A.Core,Alpha));
    }
    else if(Style==TEXT("frost"))
    {
        const FVector P=Pos+Vel*Life*.6f;M.Shard(P,Vel,6*S,1.7f*S,Col(A.Primary,Alpha),Col(A.Core,Alpha),Life*9);
    }
    else if(Style==TEXT("holy")||Style==TEXT("gold"))
    {
        const FVector P=Pos+Vel*Life*.4f+FVector(0,0,20*Life);M.Star(P,(5+3*Hash(Seed))*S,Col(A.Core,Alpha),4,Life*6);M.Glow(P,7*S,Col(A.Primary,Alpha*.5f));
    }
    else
    {
        const FVector P=Pos+Vel*Life;M.Ribbon(P,P-Vel.GetSafeNormal()*9*S,.9f*S,.2f*S,Col(A.Core,Alpha),Col(A.Primary,0));
    }
}
}

void DrawSwipe(const FCireAuraAttack& A,float Age,float Scale,float Mirror,FVector Forward,const FContext& Camera,FBuffers& B)
{
    FBuild M(B,Camera);const float S=Scale;
    const FVector F=Forward.GetSafeNormal2D().IsNearlyZero()?FVector::ForwardVector:Forward.GetSafeNormal2D();
    const FVector Side=FVector::CrossProduct(FVector::UpVector,F);const float Tilt=FMath::DegreesToRadians(30.f)*Mirror;
    const FVector Sp=(Side*FMath::Cos(Tilt)+FVector::UpVector*FMath::Sin(Tilt)).GetSafeNormal();
    const FVector Normal=FVector::CrossProduct(F,Sp).GetSafeNormal();
    const float Rr=62*S,Sweep=.17f;const float Head=-1.4f+2.8f*FMath::Clamp(Age/Sweep,0.f,1.f);
    const float Fade=1-FMath::SmoothStep(Sweep,.42f,Age);const float Tail=FMath::Max(-1.4f,Head-2.2f);
    const FString Style=A.Swipe.ToString();const int32 Steps=26;
    auto Arc=[&](float Th,float Rad){return (F*FMath::Cos(Th)+Sp*FMath::Sin(Th)*Mirror)*Rad;};
    for(int32 J=0;J<Steps;++J)
    {
        const float U0=J/float(Steps),U1=(J+1)/float(Steps);const float T0=FMath::Lerp(Tail,Head,U0),T1=FMath::Lerp(Tail,Head,U1);
        const float W0=(3+15*U0*U0)*S,W1=(3+15*U1*U1)*S;
        const FLinearColor C0=Col(HitColor(A,U0*U0),Fade*FMath::Pow(U0,1.4f)),C1=Col(HitColor(A,U1*U1),Fade*FMath::Pow(U1,1.4f));
        M.Quad(Arc(T0,Rr-W0),Arc(T1,Rr-W1),Arc(T1,Rr+W1*.35f),Arc(T0,Rr+W0*.35f),C0,C1,C1,C0);
        const FLinearColor E0=Col(A.Core,Fade*U0),E1=Col(A.Core,Fade*U1);
        M.Quad(Arc(T0,Rr+W0*.25f),Arc(T1,Rr+W1*.25f),Arc(T1,Rr+W1*.45f),Arc(T0,Rr+W0*.45f),E0,E1,E1,E0);
        if(Style==TEXT("rhythm")&&J%8==7)M.Quad(Arc(T0,Rr-W0*1.3f),Arc(T1,Rr-W1*1.3f),Arc(T1,Rr+W1*.6f),Arc(T0,Rr+W0*.6f),Col(A.Core,Fade),Col(A.Core,Fade),Col(A.Core,Fade),Col(A.Core,Fade));
    }
    M.Glow(Arc(Head,Rr),18*S,Col(A.Primary,Fade*.6f));M.Glow(Arc(Head,Rr),8*S,Col(A.Core,Fade*.7f));
    // Style particles thrown off the arc.
    for(int32 I=0;I<12;++I)
    {
        const float Born=Sweep*I/12.f;if(Age<Born)continue;const float Life=Age-Born;if(Life>.45f)continue;
        const float Th=-1.4f+2.8f*I/12.f;const FVector Pos=Arc(Th,Rr);
        const FVector Vel=(FVector::CrossProduct(Normal,Pos).GetSafeNormal()*-Mirror*140+Pos.GetSafeNormal()*110)*S;
        Extras(M,A,Pos,Vel,Life,S,I*31+7,FMath::Clamp(1-Life/.45f,0.f,1.f));
    }
}

void DrawHit(const FCireAuraAttack& A,float Age,float Scale,const FContext& Camera,FBuffers& B)
{
    FBuild M(B,Camera);const float S=Scale;const FString Hit=A.OnHit.ToString();const float Life=FMath::Clamp(1-Age/.55f,0.f,1.f);
    const float Flash=FMath::Clamp(1-Age/.18f,0.f,1.f);
    M.Glow(FVector::ZeroVector,(26+30*Age)*S,Col(A.Primary,Life*.55f));M.Glow(FVector::ZeroVector,14*S,Col(A.Core,Flash*.8f));
    if(Hit==TEXT("splash"))
    {
        for(int32 I=0;I<16;++I)
        {
            const float H1=Hash(I,91),H2=Hash(I,92);const FVector Vel=(Polar(1,H1*Tau)*(.6f+.8f*H2)+FVector(0,0,.9f+.6f*H2))*180*S;
            const FVector P=Vel*Age+FVector(0,0,-520*Age*Age*S);const FVector Dir=Vel+FVector(0,0,-1040*Age*S);
            M.Shard(P,Dir,(6+5*H2)*S,(1.6f+.8f*H1)*S,Col(A.Primary,Life),Col(A.Core,Life),H1*6);
        }
        M.BillRing(FVector::ZeroVector,(10+70*Age)*S,3*S*Life,Col(A.Primary,Life*.8f),22);
    }
    else if(Hit==TEXT("shatter"))
    {
        for(int32 I=0;I<12;++I)
        {
            const float H1=Hash(I,71),H2=Hash(I,72);const FVector Dir=(Polar(1,H1*Tau)+FVector(0,0,H2*1.2f-.2f)).GetSafeNormal();
            M.Shard(Dir*(8+150*Age)*S,Dir,(9+6*H2)*S*Life,2.4f*S,Col(A.Primary,Life),Col(A.Core,Life),H1*5);
        }
        M.BillRing(FVector::ZeroVector,(14+60*Age)*S,2.2f*S,Col(A.Core,Life),24);
    }
    else if(Hit==TEXT("glint"))
    {
        M.Star(FVector::ZeroVector,(20+12*Flash)*S,Col(A.Core,Life),4,Age*2,.12f);
        for(int32 I=0;I<7;++I){const FVector Dir=(Polar(1,I*Tau/7+Age*2)+FVector(0,0,.35f)).GetSafeNormal();
            M.Star(Dir*(10+80*Age)*S,6*S,Col(A.Core,Life),4,Age*5+I);M.Glow(Dir*(10+80*Age)*S,8*S,Col(A.Primary,Life*.5f));}
    }
    else if(Hit==TEXT("ripple"))
    {
        for(int32 I=0;I<3;++I){const float Ph=FMath::Clamp(Age*2.2f-I*.25f,0.f,1.f);M.BillRing(FVector::ZeroVector,(8+50*Ph)*S,2.4f*S,Col(I==0?A.Core:A.Primary,(1-Ph)*Life),24);}
    }
    else if(Hit!=TEXT("none"))
    {
        for(int32 I=0;I<18;++I)
        {
            const float H1=Hash(I,51),H2=Hash(I,52);const FVector Dir=(Polar(1,H1*Tau)+FVector(0,0,H2*1.4f-.3f)).GetSafeNormal();
            const FVector P=Dir*(6+200*Age)*S;M.Ribbon(P,P-Dir*(10+14*H2)*S*Life,1.2f*S,.2f*S,Col(A.Core,Life),Col(A.Primary,Life*.3f));
        }
        M.BillRing(FVector::ZeroVector,(10+55*Age)*S,2*S,Col(A.Primary,Life),22);
    }
}

void DrawTrail(const FCireAuraAttack& A,const TArray<FVector>& Points,float Time,float Scale,float Fade,const FContext& Camera,FBuffers& B)
{
    FBuild M(B,Camera);const float S=Scale;const int32 N=Points.Num();if(N<2)return;
    for(int32 I=1;I<N;++I)
    {
        const float U0=(I-1)/float(N-1),U1=I/float(N-1);
        M.Ribbon(Points[I-1],Points[I],5.5f*S*U0,5.5f*S*U1,Col(HitColor(A,U0),Fade*U0),Col(HitColor(A,U1),Fade*U1));
        if(I%2==0)Extras(M,A,Points[I],FVector(0,0,1)*20*S,.08f+.1f*(1-U1),S*.8f,I*13,Fade*U1);
    }
    M.Glow(Points.Last(),14*S,Col(A.Primary,Fade*.7f));M.Glow(Points.Last(),6*S,Col(A.Core,Fade));
    (void)Time;
}

void DrawMuzzle(const FCireAuraAttack& A,float Age,float Scale,const FContext& Camera,FBuffers& B)
{
    FBuild M(B,Camera);const float S=Scale;const float Life=FMath::Clamp(1-Age/.28f,0.f,1.f);
    M.Glow(FVector::ZeroVector,20*S*(1+Age*2),Col(A.Primary,Life*.7f));
    for(int32 I=0;I<8;++I){const FVector Dir=(Polar(1,I*Tau/8)+FVector(0,0,.5f)).GetSafeNormal();Extras(M,A,Dir*6*S,Dir*120*S,Age,S,I*17,Life);}
}
}
