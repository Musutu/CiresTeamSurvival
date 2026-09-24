#include "CireGame.h"
#include "CireLanePath.h"
#include "CireEnvironmentProps.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
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

namespace
{
// Town surfaces. Material slots in TownAssetSlots.json may replace any of these without code changes.
const TCHAR* const RoadMaterial=TEXT("/Game/Environment/Town/Materials/MI_TownW_Cobble.MI_TownW_Cobble");
const TCHAR* const PlazaMaterial=TEXT("/Game/Environment/Town/Materials/MI_TownW_Plaza.MI_TownW_Plaza");
const TCHAR* const GroundMaterial=TEXT("/Game/Environment/Town/Materials/MI_TownW_Ground.MI_TownW_Ground");
const TCHAR* const FieldMaterial=TEXT("/Game/Environment/Town/Materials/MI_TownW_Field.MI_TownW_Field");
const TCHAR* const FlagstoneMaterial=TEXT("/Game/Environment/Town/Materials/MI_TownW_Flagstone.MI_TownW_Flagstone");
const TCHAR* const CastleMaterial=TEXT("/Game/Environment/Town/Materials/MI_TownW_CastleW.MI_TownW_CastleW");
const TCHAR* const StoneMaterial=TEXT("/Game/Environment/Town/Materials/MI_TownW_StoneW.MI_TownW_StoneW");
const TCHAR* const RiftMaterial=TEXT("/Game/Environment/Town/Materials/MI_Town_Ember.MI_Town_Ember");
const TCHAR* const SkyMaterial=TEXT("/Game/Environment/Town/Sky/M_TownSky.M_TownSky");
}

ACireWorld::ACireWorld() {
    bReplicates=true; bAlwaysRelevant=true;
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=.25f;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("WorldRoot"));
}
void ACireWorld::BeginPlay() {
    Super::BeginPlay();
    // Built identically on each peer; gameplay actors are replicated independently.
    CireEnvironmentProps::Reload();
    auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto* Cylinder=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    auto* Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    auto Make=[&](const TCHAR* Name,UStaticMesh* Mesh,UMaterialInterface* Material,bool Collision,bool Shadow=true) {
        auto* C=NewObject<UInstancedStaticMeshComponent>(this,FName(Name));
        C->SetNetAddressable();
        C->SetupAttachment(RootComponent); C->SetStaticMesh(Mesh);
        C->SetMaterial(0,Material);
        C->SetCollisionObjectType(ECC_WorldStatic);
        C->SetCollisionEnabled(Collision?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
        C->SetCollisionResponseToAllChannels(ECR_Block); C->SetCastShadow(Shadow);
        C->RegisterComponent(); AddInstanceComponent(C); return C;
    };
    auto Mat=[](const TCHAR* Slot,const TCHAR* Path){return CireEnvironmentProps::SurfaceMaterial(Slot,Path);};
    auto Legacy=[](const TCHAR* Path){return LoadObject<UMaterialInterface>(nullptr,Path);};
    auto* Castle=Make(TEXT("CastleMasonry"),Cube,Mat(TEXT("castle_material"),CastleMaterial),true);
    auto* Stone=Make(TEXT("Stone"),Cube,Mat(TEXT("stone_material"),StoneMaterial),true);
    auto* Earth=Make(TEXT("CourtyardGround"),Cube,Mat(TEXT("ground_material"),GroundMaterial),true,false);
    auto* Field=Make(TEXT("FieldGround"),Cube,Mat(TEXT("field_material"),FieldMaterial),false,false);
    auto* Plaza=Make(TEXT("PlazaPaving"),Cube,Mat(TEXT("plaza_material"),PlazaMaterial),false,false);
    auto* Flagstone=Make(TEXT("Flagstones"),Cube,Mat(TEXT("flagstone_material"),FlagstoneMaterial),false,false);
    auto* Disc=Make(TEXT("ChallengeDais"),Cylinder,Mat(TEXT("flagstone_material"),FlagstoneMaterial),false,false);
    auto* Rift=Make(TEXT("BreachRift"),Cube,Legacy(RiftMaterial),false,false);
    auto* Teal=Make(TEXT("Ember"),Cube,Legacy(TEXT("/Game/Art/Materials/M_Ember.M_Ember")),false,false);
    auto* Red=Make(TEXT("Dusk"),Cube,Legacy(TEXT("/Game/Art/Materials/M_Dusk.M_Dusk")),false,false);
    auto* Pillar=Make(TEXT("Columns"),Cylinder,Mat(TEXT("castle_material"),CastleMaterial),true);
    auto* Orbs=Make(TEXT("Braziers"),Sphere,Legacy(TEXT("/Game/Art/Materials/M_Gold.M_Gold")),false);
    auto Add=[](UInstancedStaticMeshComponent* C,FVector P,FVector Size,FRotator R=FRotator::ZeroRotator){C->AddInstance(FTransform(R,P,Size/100.f));};
    const auto& Routes=CireLanePath::Get(GetWorld());
    const float HW=Routes.HalfWidth;
    // The Sundering Wall: an opaque, very tall rampart keeps the two PvE realms visually separate even
    // from elevated cameras. Town pieces are clipped 60 cm short of it on both sides.
    const float WallMinX=Routes.MinX-3200,WallMaxX=Routes.MaxX+3000,CentreX=(WallMinX+WallMaxX)*.5f,Length=WallMaxX-WallMinX;
    Add(Castle,FVector(CentreX,0,1600),FVector(Length,200,3400));
    Add(Castle,FVector(CentreX,0,3330),FVector(Length,300,60));
    RouteRoad=Make(TEXT("CireRouteRoad"),Cube,Mat(TEXT("cobblestone_material"),RoadMaterial),false,false);
    RouteEdge=Make(TEXT("CireRouteEdge"),Cube,Mat(TEXT("stone_material"),StoneMaterial),false,false);
    RouteArrows=Make(TEXT("CireRouteArrows"),Cube,Mat(TEXT("stone_material"),StoneMaterial),false,false);
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
    for(int32 Team=0;Team<2;++Team) {
        const float Y=CireLanePath::CenterY(Team);
        const float Outer=Team==0?-1.f:1.f;  // realm side away from the Sundering Wall
        const FColor Color=Team==0?FColor(71,208,189):FColor(233,111,83);
        // Collision floor: from beyond the castle keep to past the breach, clipped at the divider.
        const float InnerY=Outer*100.f,OuterY=Y+Outer*(HW+2600);
        const float FloorMinX=Routes.MinX-3100,FloorMaxX=Routes.MaxX+2800;
        Add(Earth,FVector((FloorMinX+FloorMaxX)*.5f,(InnerY+OuterY)*.5f,-70),FVector(FloorMaxX-FloorMinX,FMath::Abs(OuterY-InnerY),140));
        // Surface districts (visual only, stacked a few millimetres apart to avoid z-fighting).
        Add(Field,FVector((11550+FloorMaxX)*.5f,(InnerY+OuterY)*.5f,.6f),FVector(FloorMaxX-11550,FMath::Abs(OuterY-InnerY)-4,1));
        Add(Plaza,FVector(7250,Y,.9f),FVector(3300,2*HW-60,1));   // market square
        Add(Plaza,FVector(1650,Y,.9f),FVector(2700,2*HW-60,1));   // town square
        Add(Flagstone,FVector(-500,Y,1.2f),FVector(1600,1900,1));  // castle approach
        Add(Flagstone,FVector(-2600,Y,1.2f),FVector(3000,2*HW+500,1)); // inner bailey
        // Private-realm edge for the castle ward: players may not leave the realm.
        Add(Castle,FVector(FloorMinX-60,(InnerY+OuterY)*.5f,700),FVector(120,FMath::Abs(OuterY-InnerY),1400));
        // Team identity above the castle gate and the breach rift.
        Text(Team==0?TEXT("EMBER KEEP"):TEXT("DUSK KEEP"),FVector(-150,Y,1260),72,Color,FRotator::ZeroRotator);
        Text(TEXT("HOLD THE CASTLE GATE"),FVector(-150,Y,1185),28,FColor(229,190,123),FRotator::ZeroRotator);
        Light(FVector(-2000,Y,520),FLinearColor(Color),26000,1500);
        for(int32 Tier=1;Tier<=3;++Tier) {
            FVector P=CireLanePath::ChallengePosition(GetWorld(),Team,Tier,0);
            Add(Disc,P+FVector(0,0,1),FVector(470,470,6));
            Add(Stone,P+FVector(0,0,3),FVector(40,40,6));
            Text(FString::Printf(TEXT("CHALLENGE  %d"),Tier),P+FVector(0,0,380),40,FColor(220,171,75));
            Light(P+FVector(0,0,300),FLinearColor(1.f,.55f,.25f),5000,700);
        }
        const FVector Spawn=CireLanePath::SpawnPosition(GetWorld(),Team,0);
        // The breach: a glowing rift in the dead fields beyond the town gate.
        Add(Rift,Spawn+FVector(420,0,230),FVector(40,700,440),FRotator(0,0,0));
        Add(Team==0?Teal:Red,Spawn+FVector(440,0,230),FVector(10,760,470));
        Light(Spawn+FVector(300,0,220),FLinearColor(1.f,.25f,.08f),16000,1400);
        Text(TEXT("THE BREACH"),Spawn+FVector(380,0,560),60,FColor(228,155,137));
    }
    const TCHAR* Names[]={TEXT("THE SUNDERED COURT"),TEXT("ASHEN CIRCLE"),TEXT("THE LAST TRIBUNAL")};
    auto* ArenaFloor=Make(TEXT("ArenaFloor"),Cube,Mat(TEXT("flagstone_material"),FlagstoneMaterial),true,false);
    auto* ArenaTiles=Make(TEXT("ArenaTiles"),Cube,Mat(TEXT("plaza_material"),PlazaMaterial),false,false);
    for(int Arena=0;Arena<3;++Arena) {
        const float Y=10000+Arena*6000;
        Add(ArenaFloor,FVector(0,Y,-80),FVector(3400,3200,160));
        Add(ArenaTiles,FVector(0,Y,1),FVector(1300,1300,1));
        for(int Side:{-1,1}) {
            Add(Castle,FVector(Side*1650,Y,190),FVector(120,3250,380));
            Add(Castle,FVector(0,Y+Side*1580,190),FVector(3400,100,380));
            Add(Side<0?Teal:Red,FVector(Side*1450,Y,24),FVector(15,2700,15));
            for(int X=-1400;X<=1400;X+=700) {
                Add(Pillar,FVector(X,Y+Side*1500,350),FVector(150,150,700+Arena*180));
                Add(Orbs,FVector(X,Y+Side*1500,740+Arena*180),FVector(55));
            }
        }
        // Clear centre for deterministic bot navigation; different perimeter silhouettes.
        for(int N=0;N<32;++N) {
            const float A=N*2*PI/32;
            Add(Stone,FVector(FMath::Cos(A)*550,Y+FMath::Sin(A)*550,2),FVector(110,12,4),FRotator(0,FMath::RadiansToDegrees(A)+90,0));
        }
        Text(Names[Arena],FVector(0,Y+1500,920+Arena*180),64,FColor(219,188,130),FRotator(0,-90,0));
        Light(FVector(-800,Y,400),FLinearColor(.2f,.7f,.65f),50000,1600);
        Light(FVector(800,Y,400),FLinearColor(1.f,.25f,.15f),50000,1600);
    }
    // Dusk: low warm sun under a sunset sky dome, cool sky fill, thick valley fog.
    if(auto* SkyMat=LoadObject<UMaterialInterface>(nullptr,SkyMaterial)) {
        auto* Dome=NewObject<UStaticMeshComponent>(this,TEXT("SkyDome"));
        Dome->SetupAttachment(RootComponent);Dome->SetStaticMesh(Sphere);Dome->SetMaterial(0,SkyMat);
        Dome->SetWorldLocation(FVector(5000,0,-2000));Dome->SetWorldScale3D(FVector(1600));
        Dome->SetCollisionEnabled(ECollisionEnabled::NoCollision);Dome->SetCastShadow(false);
        Dome->bAffectDistanceFieldLighting=false;Dome->SetVisibleInRayTracing(false);Dome->bAffectDynamicIndirectLighting=false;
        Dome->RegisterComponent();AddInstanceComponent(Dome);
    }
    auto* Sun=GetWorld()->SpawnActor<ADirectionalLight>(FVector(0,0,3000),FRotator(-14,-148,0));
    Sun->GetLightComponent()->SetIntensity(4.2f); Sun->GetLightComponent()->SetLightColor(FLinearColor(1.f,.62f,.38f));
    auto* Sky=GetWorld()->SpawnActor<ASkyLight>();
    Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Sky->GetLightComponent()->bRealTimeCapture=true;
    Sky->GetLightComponent()->SetIntensity(1.1f);
    Sky->GetLightComponent()->SetLightColor(FLinearColor(.72f,.78f,1.f));
    Sky->GetLightComponent()->RecaptureSky();
    auto* Fog=GetWorld()->SpawnActor<AExponentialHeightFog>();
    Fog->GetComponent()->SetFogDensity(.018f);
    Fog->GetComponent()->SetFogHeightFalloff(.35f);
    Fog->GetComponent()->SetFogInscatteringColor(FLinearColor(.16f,.11f,.09f));
    Fog->GetComponent()->SetStartDistance(1200.f);
    Fog->GetComponent()->SetVolumetricFog(true);
    Fog->GetComponent()->SetVolumetricFogScatteringDistribution(.45f);
    Fog->GetComponent()->SetVolumetricFogExtinctionScale(.8f);
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
    constexpr float RoadWidth=520.f;
    for(int Team=0;Team<2;++Team) {
        const auto& Points=R.LocalPoints[Team];
        for(int I=1;I<Points.Num();++I) {
            const FVector A(Points[I-1].X,Points[I-1].Y+CireLanePath::CenterY(Team),0);
            const FVector B(Points[I].X,Points[I].Y+CireLanePath::CenterY(Team),0);
            const FVector D=(B-A).GetSafeNormal(),N(-D.Y,D.X,0);
            const float L=FVector2D::Distance(FVector2D(A),FVector2D(B));const FRotator Rot=D.Rotation();
            // Cobbled marching road, flush with the ground (top at +3 cm), readable against mud and plazas.
            Add(RouteRoad,(A+B)*.5f+FVector(0,0,1.5f),FVector(L+RoadWidth*.5f,RoadWidth,3),Rot);
            // Low kerb stones (visual only) mark the road edges without tripping units.
            for(int Side:{-1,1})Add(RouteEdge,(A+B)*.5f+N*Side*(RoadWidth*.5f+10)+FVector(0,0,3),FVector(FMath::Max(10.f,L-RoadWidth*.5f),20,6),Rot);
            // Worn setts every few metres hint the marching direction without gamey arrows.
            for(float T=260;T<L-160;T+=780)Add(RouteArrows,A+D*T+FVector(0,0,3.3f),FVector(70,RoadWidth-80,.6f),Rot);
            Add(RouteRoad,B+FVector(0,0,1.6f),FVector(RoadWidth,RoadWidth,3),Rot);
        }
    }
    RenderedRouteRevision=CireLanePath::Revision(GetWorld());
}
