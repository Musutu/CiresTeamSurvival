#include "CireGame.h"
#include "CireLanePath.h"
#include "CireEnvironmentProps.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

ACireWorld::ACireWorld() {
    bReplicates=true; bAlwaysRelevant=true;
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=.25f;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("WorldRoot"));
}
void ACireWorld::BeginPlay() {
    Super::BeginPlay();
    // Built identically on each peer; gameplay actors are replicated independently.
    auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto* Cylinder=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    auto* Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    auto Make=[&](const TCHAR* Name,UStaticMesh* Mesh,const TCHAR* Material,bool Collision) {
        auto* C=NewObject<UInstancedStaticMeshComponent>(this,FName(Name));
        C->SetNetAddressable();
        C->SetupAttachment(RootComponent); C->SetStaticMesh(Mesh);
        C->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,Material));
        C->SetCollisionObjectType(ECC_WorldStatic);
        C->SetCollisionEnabled(Collision?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
        C->SetCollisionResponseToAllChannels(ECR_Block); C->RegisterComponent(); AddInstanceComponent(C); return C;
    };
    auto* Stone=Make(TEXT("Stone"),Cube,TEXT("/Game/Art/Environment/Materials/M_AshenMasonry.M_AshenMasonry"),true);
    auto* Slate=Make(TEXT("Slate"),Cube,TEXT("/Game/Art/Environment/Materials/M_RoofSlate.M_RoofSlate"),false);
    auto* Earth=Make(TEXT("CourtyardGround"),Cube,TEXT("/Game/Art/Environment/Materials/M_WornEarth.M_WornEarth"),true);
    auto* Timber=Make(TEXT("Timber"),Cube,TEXT("/Game/Art/Environment/Materials/M_OldTimber.M_OldTimber"),false);
    auto* Windows=Make(TEXT("WarmWindows"),Cube,TEXT("/Game/Art/Environment/Materials/M_WindowGlow.M_WindowGlow"),false);
    auto* Metal=Make(TEXT("Metal"),Cube,TEXT("/Game/Art/Materials/M_Metal.M_Metal"),false);
    auto* Gold=Make(TEXT("Gold"),Cube,TEXT("/Game/Art/Materials/M_Gold.M_Gold"),false);
    auto* Teal=Make(TEXT("Ember"),Cube,TEXT("/Game/Art/Materials/M_Ember.M_Ember"),false);
    auto* Red=Make(TEXT("Dusk"),Cube,TEXT("/Game/Art/Materials/M_Dusk.M_Dusk"),false);
    auto* Pillar=Make(TEXT("Columns"),Cylinder,TEXT("/Game/Art/Environment/Materials/M_AshenMasonry.M_AshenMasonry"),true);
    auto* Orbs=Make(TEXT("Braziers"),Sphere,TEXT("/Game/Art/Materials/M_Gold.M_Gold"),false);
    auto Add=[](UInstancedStaticMeshComponent* C,FVector P,FVector Size,FRotator R=FRotator::ZeroRotator){C->AddInstance(FTransform(R,P,Size/100.f));};
    // Opaque divider keeps the two PvE realms visually separate even from elevated cameras.
    const auto& Routes=CireLanePath::Get(GetWorld());
    const float CentreX=(Routes.MinX+Routes.MaxX)*.5f,Length=Routes.MaxX-Routes.MinX+1100;
    Add(Stone,FVector(CentreX,0,1600),FVector(Length+500,1100,3400));
    RouteRoad=Make(TEXT("CireRouteRoad"),Cube,TEXT("/Game/Art/Environment/Materials/M_BasaltRoad.M_BasaltRoad"),false);
    RouteEdge=Make(TEXT("CireRouteEdge"),Cube,TEXT("/Game/Art/Environment/Materials/M_AshenMasonry.M_AshenMasonry"),false);
    RouteArrows=Make(TEXT("CireRouteArrows"),Cube,TEXT("/Game/Art/Materials/M_Gold.M_Gold"),false);
    RefreshRouteVisuals();
    auto Text=[&](const FString& Str,FVector P,float Size,FColor Color,FRotator Rotation=FRotator(0,180,0)) {
        auto* T=NewObject<UTextRenderComponent>(this); T->SetupAttachment(RootComponent);
        T->SetWorldLocation(P); T->SetWorldRotation(Rotation); T->SetText(FText::FromString(Str));
        T->SetHorizontalAlignment(EHTA_Center); T->SetWorldSize(Size); T->SetTextRenderColor(Color);
        T->RegisterComponent(); AddInstanceComponent(T);
    };
    auto Light=[&](FVector P,FLinearColor Color,float Intensity,float Radius) {
        auto* L=GetWorld()->SpawnActor<APointLight>(P,FRotator::ZeroRotator);
        L->PointLightComponent->SetIntensity(Intensity);
        L->PointLightComponent->SetLightColor(Color);
        L->PointLightComponent->SetAttenuationRadius(Radius);
        L->PointLightComponent->SetCastShadows(false);
    };
    for(int Team=0;Team<2;++Team) {
        const float Y=Team==0?-2100:2100; auto* Accent=Team==0?Teal:Red;
        const FColor Color=Team==0?FColor(71,208,189):FColor(233,111,83);
        Add(Earth,FVector(CentreX,Y,-70),FVector(Length,3600,140));
        Add(Stone,FVector(Routes.MaxX+140,Y,360),FVector(140,2500,860));
        for(int Side:{-1,1}) {
            Add(Stone,FVector(CentreX,Y+Side*(Routes.HalfWidth+100),130),FVector(Length,90,310));
            Add(Metal,FVector(CentreX,Y+Side*(Routes.HalfWidth+100),290),FVector(Length,115,22));
            for(int X=-2500;X<=Routes.MaxX;X+=1250) {
                Add(Pillar,FVector(X,Y+Side*1190,380),FVector(170,170,900));
                Add(Stone,FVector(X,Y+Side*1190,850),FVector(205,205,80));
                Add(Accent,FVector(X-85,Y+Side*1180,430),FVector(10,20,230));
            }
            for(int X=-800;X<=Routes.MaxX-500;X+=2200) {
                const float StreetY=Y+Side*(Routes.HalfWidth+65);
                Add(Pillar,FVector(X,StreetY,145),FVector(45,45,290));
                Add(Metal,FVector(X,StreetY,305),FVector(75,75,15));
                Add(Orbs,FVector(X,StreetY,332),FVector(42));
                Light(FVector(X,StreetY-Side*65,350),FLinearColor(1.f,.63f,.32f),4500,580);
            }
            // Modular fortified houses form a skyline outside the playable lane.
            // Their colliders never intersect the editable route rectangle.
            for(int N=0;N<12;++N) {
                const float X=-500+N*1050.f;
                if(X>Routes.MaxX-800)break;
                const float B=Y+Side*(Routes.HalfWidth+450),H=440+(N%3)*90;
                const float W=650+(N%2)*110,D=510;
                Add(Stone,FVector(X,B,H*.5f),FVector(W,D,H));
                Add(Stone,FVector(X,B,28),FVector(W+70,D+60,56));
                Add(Timber,FVector(X,B,H*.52f),FVector(W+18,D+18,28));
                Add(Timber,FVector(X,B,H-12),FVector(W+35,D+35,28));
                for(int Edge:{-1,1}) {
                    Add(Timber,FVector(X+Edge*(W*.5f-25),B-Side*(D*.5f+8),H*.5f),FVector(35,26,H));
                    // Two inclined slabs and a ridge cap create a gabled roof.
                    Add(Slate,FVector(X,B+Edge*D*.26f,H+D*.23f),FVector(W+110,D*.74f,30),FRotator(0,0,Edge*42));
                }
                Add(Metal,FVector(X,B,H+D*.50f),FVector(W+125,28,28));
                Add(Stone,FVector(X+W*.28f,B+Side*65,H+D*.37f),FVector(90,110,260));
                Add(Stone,FVector(X+W*.28f,B+Side*65,H+D*.37f+145),FVector(120,140,36));
                for(int Floor=0;Floor<2;++Floor)for(int Win:{-1,1}) {
                    const FVector P(X+Win*W*.26f,B-Side*(D*.5f+3),140+Floor*(H*.48f));
                    Add(Timber,P,FVector(98,18,136));
                    Add(Windows,P-FVector(0,Side*11,0),FVector(71,6,106));
                    Add(Metal,P-FVector(0,Side*16,0),FVector(8,5,110));
                    Add(Metal,P-FVector(0,Side*16,0),FVector(74,5,7));
                }
                Add(Timber,FVector(X,B-Side*(D*.5f+8),96),FVector(110,25,192));
                Add(Metal,FVector(X,B-Side*(D*.5f+23),65),FVector(110,5,12));
                Add(Metal,FVector(X,B-Side*(D*.5f+23),133),FVector(110,5,12));
            }
        }
        // Protected courtyard and a giant luminous gate behind the defended line.
        Add(Stone,FVector(-2600,Y,500),FVector(130,2450,1100));
        for(int Side:{-1,1}) {
            Add(Stone,FVector(-2450,Y+Side*430,375),FVector(240,220,750));
            Add(Metal,FVector(-2450,Y+Side*430,750),FVector(280,250,60));
        }
        Add(Stone,FVector(-2450,Y,810),FVector(240,1080,130));
        Add(Accent,FVector(-2470,Y,390),FVector(30,640,680));
        Add(Gold,FVector(-2370,Y,48),FVector(30,1700,8));
        Text(Team==0?TEXT("EMBER KEEP"):TEXT("DUSK KEEP"),FVector(-2310,Y,890),80,Color,FRotator::ZeroRotator);
        Text(TEXT("TOWN  |  TOMES & RELICS"),FVector(-1850,Y-720,250),36,FColor(218,189,122),FRotator(0,90,0));
        Light(FVector(-2200,Y,420),FLinearColor(Color),60000,1300);
        // Original town-defense landmark. Its footprint matches ACireTownGoal exactly:
        // x [-2300,-1400], y [team centre-900,team centre+900]. The entry is unobstructed.
        Add(Metal,FVector(-1850,Y,5),FVector(900,1800,8));
        for(int Side:{-1,1}) {
            Add(Gold,FVector(-1850,Y+Side*895,12),FVector(900,12,8));
            Add(Accent,FVector(-1850,Y+Side*877,14),FVector(876,10,8));
            Add(Pillar,FVector(-1400,Y+Side*1010,260),FVector(150,150,520));
            Add(Stone,FVector(-1400,Y+Side*1010,530),FVector(215,215,70));
            Add(Accent,FVector(-1318,Y+Side*1010,310),FVector(12,55,245));
            Add(Accent,FVector(-1482,Y+Side*1010,310),FVector(12,55,245));
            Add(Orbs,FVector(-1400,Y+Side*1010,595),FVector(50));
        }
        Add(Gold,FVector(-2300,Y,12),FVector(16,1800,8));
        Add(Gold,FVector(-1400,Y,13),FVector(70,1800,12));
        Add(Accent,FVector(-1400,Y,22),FVector(28,1790,8));
        for(int I=-4;I<=4;++I)Add(Gold,FVector(-1490,Y+I*175,15),FVector(34,88,8),FRotator(0,45,0));
        Add(Stone,FVector(-1400,Y,560),FVector(100,2160,100));
        // Opaque sign backing hides the reverse-facing text instead of letting both faces overlap.
        Add(Stone,FVector(-1400,Y,535),FVector(90,1400,190));
        Add(Metal,FVector(-1400,Y,625),FVector(140,2220,35));
        Add(Gold,FVector(-1346,Y,593),FVector(8,1970,8));
        Add(Gold,FVector(-1454,Y,593),FVector(8,1970,8));
        // The cap projects to x=-1330/-1470. Put lettering beyond those faces
        // and below its gold trim so elevated approach cameras cannot mask it.
        // These are presentation offsets only; the defended footprint is unchanged.
        const FColor SignColor=Team==0?FColor(178,233,219):FColor(243,188,164);
        Text(TEXT("DEFEND THE TOWN"),FVector(-1312,Y,535),53,SignColor,FRotator::ZeroRotator);
        Text(TEXT("DEFEND THE TOWN"),FVector(-1488,Y,535),53,SignColor,FRotator(0,180,0));
        Text(TEXT("LAST LINE  |  BREACH = LOST LIVES"),FVector(-1310,Y,470),25,FColor(229,190,123),FRotator::ZeroRotator);
        Text(TEXT("LAST LINE  |  BREACH = LOST LIVES"),FVector(-1490,Y,470),25,FColor(229,190,123),FRotator(0,180,0));
        Light(FVector(-1380,Y,300),FLinearColor(Color),15000,1050);
        for(int Tier=1;Tier<=3;++Tier) {
            FVector P=CireLanePath::ChallengePosition(GetWorld(),Team,Tier,20);
            Add(Metal,P,FVector(420,430,35)); Add(Gold,P+FVector(0,0,21),FVector(440,440,6));
            Text(FString::Printf(TEXT("CHALLENGE  %d"),Tier),P+FVector(0,0,370),40,FColor(220,171,75));
        }
        const FVector Spawn=CireLanePath::SpawnPosition(GetWorld(),Team,0);
        for(int Side:{-1,1})Add(Pillar,Spawn+FVector(240,Side*580,340),FVector(240,240,680));
        Add(Red,Spawn+FVector(490,0,250),FVector(60,850,480));
        Text(TEXT("THE BREACH"),Spawn+FVector(200,0,730),65,FColor(228,155,137));
    }
    const TCHAR* Names[]={TEXT("THE SUNDERED COURT"),TEXT("ASHEN CIRCLE"),TEXT("THE LAST TRIBUNAL")};
    for(int Arena=0;Arena<3;++Arena) {
        const float Y=10000+Arena*6000;
        Add(Stone,FVector(0,Y,-80),FVector(3400,3200,160));
        for(int X=-1400;X<=1400;X+=280)for(int Z=-1400;Z<=1400;Z+=280)
            Add(Slate,FVector(X,Y+Z,2),FVector(274,274,12));
        for(int Side:{-1,1}) {
            Add(Stone,FVector(Side*1650,Y,190),FVector(120,3250,380));
            Add(Stone,FVector(0,Y+Side*1580,190),FVector(3400,100,380));
            Add(Side<0?Teal:Red,FVector(Side*1450,Y,24),FVector(15,2700,15));
            for(int X=-1400;X<=1400;X+=700) {
                Add(Pillar,FVector(X,Y+Side*1500,350),FVector(150,150,700+Arena*180));
                Add(Orbs,FVector(X,Y+Side*1500,740+Arena*180),FVector(55));
            }
        }
        // Clear centre for deterministic bot navigation; different perimeter silhouettes.
        for(int N=0;N<32;++N) {
            const float A=N*2*PI/32;
            Add(Gold,FVector(FMath::Cos(A)*550,Y+FMath::Sin(A)*550,15),FVector(110,12,8),FRotator(0,FMath::RadiansToDegrees(A)+90,0));
        }
        Text(Names[Arena],FVector(0,Y+1500,920+Arena*180),64,FColor(219,188,130),FRotator(0,-90,0));
        Light(FVector(-800,Y,400),FLinearColor(.2f,.7f,.65f),50000,1600);
        Light(FVector(800,Y,400),FLinearColor(1.f,.25f,.15f),50000,1600);
    }
    auto* Sun=GetWorld()->SpawnActor<ADirectionalLight>(FVector(0,0,3000),FRotator(-48,-30,0));
    Sun->GetLightComponent()->SetIntensity(3.5f); Sun->GetLightComponent()->SetLightColor(FLinearColor(.66f,.75f,1.f));
    auto* Sky=GetWorld()->SpawnActor<ASkyLight>();
    Sky->GetLightComponent()->SetIntensity(.8f);
    Sky->GetLightComponent()->SetLightColor(FLinearColor(.35f,.45f,.6f));
    auto* Fog=GetWorld()->SpawnActor<AExponentialHeightFog>();
    Fog->GetComponent()->SetFogDensity(.009f);
    Fog->GetComponent()->SetFogInscatteringColor(FLinearColor(.035f,.045f,.068f));
    Fog->GetComponent()->SetStartDistance(800.f);
    CireEnvironmentProps::Build(this);
}

void ACireWorld::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    if(RenderedRouteRevision!=CireLanePath::Revision(GetWorld())){RefreshRouteVisuals();CireEnvironmentProps::Refresh(this);}
}
void ACireWorld::RefreshRouteVisuals() {
    if(!RouteRoad||!RouteEdge||!RouteArrows)return;
    RouteRoad->ClearInstances();RouteEdge->ClearInstances();RouteArrows->ClearInstances();
    const auto& R=CireLanePath::Get(GetWorld());
    auto Add=[](UInstancedStaticMeshComponent* C,FVector P,FVector Size,FRotator Rot=FRotator::ZeroRotator){C->AddInstance(FTransform(Rot,P,Size/100.f));};
    for(int Team=0;Team<2;++Team) {
        const auto& Points=R.LocalPoints[Team];
        for(int I=1;I<Points.Num();++I) {
            const FVector A(Points[I-1].X,Points[I-1].Y+CireLanePath::CenterY(Team),0);
            const FVector B(Points[I].X,Points[I].Y+CireLanePath::CenterY(Team),0);
            const FVector D=(B-A).GetSafeNormal(),N(-D.Y,D.X,0);
            const float L=FVector::Dist2D(A,B);const FRotator Rot=D.Rotation();
            Add(RouteRoad,(A+B)*.5f+FVector(0,0,2),FVector(L+20,410,4),Rot);
            for(int Side:{-1,1})Add(RouteEdge,(A+B)*.5f+N*Side*215+FVector(0,0,4),FVector(L,18,8),Rot);
            // Inlaid chevrons indicate travel toward the town without collision.
            for(float T=180;T<L-90;T+=650) {
                const FVector P=A+D*T+FVector(0,0,7);
                for(int Side:{-1,1})Add(RouteArrows,P-D*17+N*Side*17,FVector(52,6,2),FRotator(0,Rot.Yaw-Side*45,0));
            }
            // Fill bends with the same world-aligned paving; no raised step.
            Add(RouteRoad,B+FVector(0,0,2.1f),FVector(410,410,4),Rot);
        }
    }
    RenderedRouteRevision=CireLanePath::Revision(GetWorld());
}
