#include "CireGame.h"
#include "CireLanePath.h"
#include "CireTownMap.h"
#include "CireArenas.h" // medieval-kingdom
#include "Net/UnrealNetwork.h"
#include "CireEnvironmentProps.h"
#include "CireVendors.h"
#include "CireNav.h" // nav-paths
#include "CireTownGoal.h" // nav-paths
#include "Components/BoxComponent.h" // nav-paths
#include "EngineUtils.h" // nav-paths
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h" // world-scale: town colour grade
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
const TCHAR* const MeadowMaterial=TEXT("/Game/Environment/Town/Materials/MI_TownW_Meadow.MI_TownW_Meadow"); // world-scale
const TCHAR* const FlagstoneMaterial=TEXT("/Game/Environment/Town/Materials/MI_TownW_Flagstone.MI_TownW_Flagstone");
const TCHAR* const CastleMaterial=TEXT("/Game/Environment/Town/Materials/MI_TownW_CastleW.MI_TownW_CastleW");
const TCHAR* const StoneMaterial=TEXT("/Game/Environment/Town/Materials/MI_TownW_StoneW.MI_TownW_StoneW");
const TCHAR* const CliffMaterial=TEXT("/Game/Environment/Town/Materials/MI_TownW_CliffW.MI_TownW_CliffW");
const TCHAR* const RiftMaterial=TEXT("/Game/Environment/Town/Materials/MI_Town_Ember.MI_Town_Ember");
const TCHAR* const SkyMaterial=TEXT("/Game/Environment/Town/Sky/M_TownSky.M_TownSky");
}

ACireWorld::ACireWorld() {
    bReplicates=true; bAlwaysRelevant=true;
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=.25f;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("WorldRoot"));
}
void ACireWorld::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACireWorld,bCastleTown);
}
void ACireWorld::BeginPlay() {
    Super::BeginPlay();
    // medieval-kingdom: the server chose the map (CireTownMap::InitializeServer); a client follows the replicated
    // choice, switches its realm frame and streams the same two town copies before anything is placed.
    if(HasAuthority())bCastleTown=CireTownMap::IsActive();
    else {CireTownMap::SetActive(bCastleTown);if(bCastleTown)CireTownMap::LoadRealms(GetWorld());}
    const bool bTown=bCastleTown;
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
    auto* Cliff=Make(TEXT("SunderingCliff"),Cube,Mat(TEXT("cliff_material"),CliffMaterial),true);
    auto* Stone=Make(TEXT("Stone"),Cube,Mat(TEXT("stone_material"),StoneMaterial),true);
    auto* Earth=Make(TEXT("CourtyardGround"),Cube,Mat(TEXT("ground_material"),GroundMaterial),true,false);
    auto* Field=Make(TEXT("FieldGround"),Cube,Mat(TEXT("field_material"),FieldMaterial),false,false);
    auto* Meadow=Make(TEXT("MeadowGround"),Cube,Mat(TEXT("meadow_material"),MeadowMaterial),false,false); // world-scale: green countryside
    auto* Plaza=Make(TEXT("PlazaPaving"),Cube,Mat(TEXT("plaza_material"),PlazaMaterial),false,false);
    auto* Flagstone=Make(TEXT("Flagstones"),Cube,Mat(TEXT("flagstone_material"),FlagstoneMaterial),false,false);
    auto* Disc=Make(TEXT("ChallengeDais"),Cylinder,Mat(TEXT("flagstone_material"),FlagstoneMaterial),false,false);
    auto* Rift=Make(TEXT("BreachRift"),Cube,Legacy(RiftMaterial),false,false);
    auto Add=[](UInstancedStaticMeshComponent* C,FVector P,FVector Size,FRotator R=FRotator::ZeroRotator){C->AddInstance(FTransform(R,P,Size/100.f));};
    const auto& Routes=CireLanePath::Get(GetWorld());
    const float HW=Routes.HalfWidth;
    // The Sundering Cliff: an opaque, very tall rock face keeps the two PvE realms visually separate even
    // from elevated cameras. The town is built against it; town pieces stop 60 cm short on both sides.
    const float WallMinX=Routes.MinX-3200,WallMaxX=Routes.MaxX+3000,CentreX=(WallMinX+WallMaxX)*.5f,Length=WallMaxX-WallMinX;
    if(!bTown) { // medieval-kingdom: the pack town realms are separated by distance, on the pack's own landscape
    Add(Cliff,FVector(CentreX,0,1600),FVector(Length,200,3400));
    // Broken strata so the silhouette above the roofs is not a ruler-straight slab (kept inside +/-100 cm).
    for(float X=WallMinX+400;X<WallMaxX;X+=1150)
    {
        const float H=3300+FMath::Fmod(X*.37f,500.f)+(FMath::Fmod(X,2300.f)<1150?250:-150);
        Add(Cliff,FVector(X,0,H*.5f),FVector(900+FMath::Fmod(X*.13f,500.f),190,H),FRotator(0,0,0));
    }
    // A distant plain under the dusk sky so high cameras never see the edge of the world.
    // world-scale: 2 km of plain under the backdrop mountains (the dome below is larger still). Its own component, kept out of
    // distance-field lighting: a 2 km scaled cube has a useless mesh distance field and rendered almost black under DFAO/Lumen.
    {
        auto* Plain=Make(TEXT("FarPlain"),Cube,Mat(TEXT("ground_material"),GroundMaterial),false,false);
        Plain->bAffectDistanceFieldLighting=false;Plain->bAffectDynamicIndirectLighting=false;Plain->SetVisibleInRayTracing(false);
        Plain->MarkRenderStateDirty();
        Add(Plain,FVector(CentreX,0,-3),FVector(Length+200000,200000,2));
    }
    } // !bTown
    RouteRoad=Make(TEXT("CireRouteRoad"),Cube,Mat(TEXT("cobblestone_material"),RoadMaterial),false,false);
    RouteEdge=Make(TEXT("CireRouteEdge"),Cube,Mat(TEXT("castle_material"),CastleMaterial),false,false);
    RouteArrows=Make(TEXT("CireRouteArrows"),Cube,Mat(TEXT("stone_material"),StoneMaterial),false,false);
    BayDais=Disc;BreachRift=Rift; // dev-route-tools
    BayStone=Make(TEXT("ChallengeStone"),Cube,Mat(TEXT("stone_material"),StoneMaterial),true);
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
        if(!bTown) { // medieval-kingdom: procedural floor, districts, ward edge, gate text and keep light
        // Collision floor: from beyond the castle keep to past the breach, clipped at the divider.
        const float InnerY=Outer*100.f,OuterY=Y+Outer*(HW+2600);
        const float FloorMinX=Routes.MinX-3100,FloorMaxX=Routes.MaxX+2800;
        Add(Earth,FVector((FloorMinX+FloorMaxX)*.5f,(InnerY+OuterY)*.5f,-70),FVector(FloorMaxX-FloorMinX,FMath::Abs(OuterY-InnerY),140));
        // Surface districts (visual only, stacked a few millimetres apart to avoid z-fighting).
        // world-scale: authored per district in TownLayout.json "surfaces"; the original town is the fallback.
        const auto& Surfaces=CireEnvironmentProps::Surfaces();
        if(Surfaces.IsEmpty())
        {
            Add(Field,FVector((11550+FloorMaxX)*.5f,(InnerY+OuterY)*.5f,.6f),FVector(FloorMaxX-11550,FMath::Abs(OuterY-InnerY)-4,1));
            Add(Plaza,FVector(7250,Y,.9f),FVector(3300,2*HW-60,1));   // market square
            Add(Plaza,FVector(1650,Y,.9f),FVector(2700,2*HW-60,1));   // town square
            Add(Flagstone,FVector(-500,Y,1.2f),FVector(1600,1900,1));  // castle approach
            Add(Flagstone,FVector(-2600,Y,1.2f),FVector(3000,2*HW+500,1)); // inner bailey
        }
        for(const auto& S:Surfaces)
        {
            UInstancedStaticMeshComponent* Target=S.Slot==TEXT("field_material")?Field:S.Slot==TEXT("flagstone_material")?Flagstone:
                S.Slot==TEXT("meadow_material")?Meadow:Plaza;
            const float Z=Target==Field?.6f:Target==Meadow?.75f:Target==Flagstone?1.2f:.9f;
            if(S.Width<=0)Add(Target,FVector(S.X,(InnerY+OuterY)*.5f,Z),FVector(S.Length,FMath::Abs(OuterY-InnerY)-4,1)); // full realm floor
            else Add(Target,FVector(S.X,Y+S.Y,Z),FVector(S.Length,S.Width,1));
        }
        // Private-realm edge for the castle ward: players may not leave the realm.
        Add(Castle,FVector(FloorMinX-60,(InnerY+OuterY)*.5f,700),FVector(120,FMath::Abs(OuterY-InnerY),1400));
        // Team identity above the castle gate and the breach rift.
        Text(Team==0?TEXT("EMBER KEEP"):TEXT("DUSK KEEP"),FVector(-150,Y,1260),72,Color,FRotator::ZeroRotator);
        Text(TEXT("HOLD THE CASTLE GATE"),FVector(-150,Y,1185),28,FColor(229,190,123),FRotator::ZeroRotator);
        Light(FVector(-2000,Y,520),FLinearColor(1.f,.6f,.32f),14000,1500);
        } // !bTown
        // dev-route-tools: the challenge packs (1..16) and the breach rift are built by RefreshRouteVisuals so live route
        // edits (the route editor's Apply) move them with the road.
    }
    // arenas: the PvP arenas are no longer pre-built here. CireArenas (Content/Data/Arenas.json) builds the
    // randomly picked arena on every peer during prep, shows it for the fight and destroys it in recovery.
    // The old three-court build lives on as its built-in fallback ("The Sundered Court").
    // Dusk: low warm sun under a sunset sky dome, cool sky fill, thick valley fog.
    if(auto* SkyMat=LoadObject<UMaterialInterface>(nullptr,SkyMaterial)) { // medieval-kingdom: in the town this dome only surrounds the PvP arena
        auto* Dome=NewObject<UStaticMeshComponent>(this,TEXT("SkyDome"));
        Dome->SetupAttachment(RootComponent);Dome->SetStaticMesh(Sphere);Dome->SetMaterial(0,SkyMat);
        // world-scale: centred on the three-times-longer realm, large enough to hold the backdrop mountains.
        Dome->SetWorldLocation(FVector(CentreX,0,-2000));Dome->SetWorldScale3D(FVector(FMath::Max(1600.f,(Length+260000.f)/100.f)));
        if(bTown){Dome->SetWorldLocation(CireArenas::Origin()-FVector(0,0,2000));Dome->SetWorldScale3D(FVector(3000.f));} // 1.5 km: the realms sit in their own skies far away
        Dome->SetCollisionEnabled(ECollisionEnabled::NoCollision);Dome->SetCastShadow(false);
        Dome->bAffectDistanceFieldLighting=false;Dome->SetVisibleInRayTracing(false);Dome->bAffectDynamicIndirectLighting=false;
        Dome->RegisterComponent();AddInstanceComponent(Dome);
    }
    // The sun sets behind the breach (+X), straight down both lanes, so the Sundering Cliff never
    // shades one team's realm more than the other's and the gate is silhouetted against the dusk.
    // world-scale (art direction "vibrant, fun and crisp"): a higher, whiter golden-hour sun instead of the murky dusk.
    if(bTown)CireTownMap::BuildRealmLighting(this); // medieval-kingdom: Daylight / Darknight suns on lighting channels 0 / 1, pack sky spheres
    else {
    auto* Sun=GetWorld()->SpawnActor<ADirectionalLight>(FVector(0,0,3000),FRotator(-30,180,0));
    Sun->GetLightComponent()->SetIntensity(7.0f); Sun->GetLightComponent()->SetLightColor(FLinearColor(1.f,.92f,.80f));
    }
    auto* Sky=GetWorld()->SpawnActor<ASkyLight>();
    Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Sky->GetLightComponent()->bRealTimeCapture=true;
    Sky->GetLightComponent()->SetIntensity(1.45f);
    Sky->GetLightComponent()->SetLightColor(FLinearColor(.78f,.86f,1.f));
    Sky->GetLightComponent()->RecaptureSky();
    auto* Fog=GetWorld()->SpawnActor<AExponentialHeightFog>();
    // world-scale: thin, sky-blue aerial haze that only softens the far districts and the mountains (the old thick brown
    // valley fog flattened everything past the next street).
    Fog->GetComponent()->SetFogDensity(.0055f);
    Fog->GetComponent()->SetFogHeightFalloff(.22f);
    Fog->GetComponent()->SetFogInscatteringColor(FLinearColor(.62f,.72f,.90f));
    Fog->GetComponent()->SetStartDistance(4000.f);
    Fog->GetComponent()->SetFogMaxOpacity(.85f);
    Fog->GetComponent()->SetVolumetricFog(true);
    Fog->GetComponent()->SetVolumetricFogScatteringDistribution(.35f);
    Fog->GetComponent()->SetVolumetricFogExtinctionScale(.35f);
    // world-scale: a second, low fog layer thickens with distance near the ground only, so the far plain and the backdrop
    // mountains fade into a light horizon haze while the streets within ~60 m stay clear and crisp.
    Fog->GetComponent()->SecondFogData.FogDensity=.03f;Fog->GetComponent()->SecondFogData.FogHeightFalloff=.45f;
    Fog->GetComponent()->SecondFogData.FogHeightOffset=0.f;Fog->GetComponent()->MarkRenderStateDirty();
    // world-scale: town colour grade (unbound, below the arenas' priority-5 grade; CireArenas disables it in the arena).
    {
        auto* Grade=NewObject<UPostProcessComponent>(this,TEXT("TownGrade"));
        Grade->SetupAttachment(RootComponent);Grade->bUnbound=true;Grade->Priority=0.f;Grade->BlendWeight=1.f;
        auto& S=Grade->Settings;
        S.bOverride_ColorSaturation=true;S.ColorSaturation=FVector4(1.1f,1.1f,1.1f,1.f);
        S.bOverride_ColorContrast=true;S.ColorContrast=FVector4(1.1f,1.1f,1.1f,1.f);
        S.bOverride_ColorSaturationShadows=true;S.ColorSaturationShadows=FVector4(1.08f,1.08f,1.08f,1.f);
        S.bOverride_WhiteTemp=true;S.WhiteTemp=6900.f; // slightly cool white balance: neutral greys, clean greens
        S.bOverride_BloomIntensity=true;S.BloomIntensity=.45f;
        S.bOverride_VignetteIntensity=true;S.VignetteIntensity=.22f;
        S.bOverride_AutoExposureBias=true;S.AutoExposureBias=.25f;
        // video-crash: the "vibrant and crisp" tonemapper sharpen, here instead of the global r.Tonemapper.Sharpen so
        // the champion-select capture can turn it off.
        S.bOverride_Sharpen=true;S.Sharpen=.6f;
        Grade->RegisterComponent();AddInstanceComponent(Grade);
    }
    CireEnvironmentProps::Build(this);
    CireVendors::SpawnAll(this); // vendors: the three town merchants, stalls and signs (every peer, like the props)
    // nav-paths: the navmesh is generated once the town, its props and the collision floor exist
    // (server/standalone only; clients have no navigation system).
    if(HasAuthority())CireNav::Initialize(GetWorld());
}
void ACireWorld::SyncGoalZones() {
    // nav-paths: the castle leak zone follows the editable goal zone (BattlefieldRoutes.json "goal").
    if(!HasAuthority())return;
    const FVector2D Extent=CireLanePath::GoalZoneExtent(GetWorld());
    for(TActorIterator<ACireTownGoal> It(GetWorld());It;++It) {
        const FVector Center=CireLanePath::GoalZoneCenter(GetWorld(),It->TeamId,150);
        if(!It->GetActorLocation().Equals(Center,1.))It->SetActorLocation(Center);
        if(It->GoalVolume&&!FVector2D(It->GoalVolume->GetUnscaledBoxExtent()).Equals(Extent,1.))It->GoalVolume->SetBoxExtent(FVector(Extent.X,Extent.Y,250));
    }
}

void ACireWorld::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    if(bCastleTown&&GetWorld()->GetTimeSeconds()<30.f)CireTownMap::PrepareRealmLevels(GetWorld()); // medieval-kingdom: late Level Instances
    if(RenderedRouteRevision!=CireLanePath::Revision(GetWorld())){RefreshRouteVisuals();CireEnvironmentProps::Refresh(this);CireNav::InvalidatePaths(GetWorld());}
    SyncGoalZones(); // nav-paths: cheap (two actors); also covers a goal authored in the JSON at startup
}
void ACireWorld::RefreshRouteVisuals() {
    if(!RouteRoad||!RouteEdge||!RouteArrows)return;
    RouteRoad->ClearInstances();RouteEdge->ClearInstances();RouteArrows->ClearInstances();
    const auto& R=CireLanePath::Get(GetWorld());
    auto Add=[](UInstancedStaticMeshComponent* C,FVector P,FVector Size,FRotator Rot=FRotator::ZeroRotator){C->AddInstance(FTransform(Rot,P,Size/100.f));};
    const float RoadWidth=R.LaneWidth; // nav-paths: editable lane width (was a fixed 520 cm)
    for(int Team=0;Team<2&&!bCastleTown;++Team) { // medieval-kingdom: the pack town marches on its own streets
        const auto& Points=R.LocalPoints[Team];
        for(int I=1;I<Points.Num();++I) {
            const FVector A(Points[I-1]+CireLanePath::RealmOrigin(Team),0);
            const FVector B(Points[I]+CireLanePath::RealmOrigin(Team),0);
            const FVector D=(B-A).GetSafeNormal(),N(-D.Y,D.X,0);
            const float L=FVector2D::Distance(FVector2D(A),FVector2D(B));const FRotator Rot=D.Rotation();
            // Cobbled marching road, flush with the ground (top at +3 cm), readable against mud and plazas.
            Add(RouteRoad,(A+B)*.5f+FVector(0,0,1.5f),FVector(L+RoadWidth*.5f,RoadWidth,3),Rot);
            // Low kerb stones (visual only) mark the road edges without tripping units.
            for(int Side:{-1,1})Add(RouteEdge,(A+B)*.5f+N*Side*(RoadWidth*.5f+10)+FVector(0,0,3),FVector(FMath::Max(10.f,L-RoadWidth*.5f),20,6),Rot);
            // Worn setts every few metres hint the marching direction without gamey arrows.
            Add(RouteRoad,B+FVector(0,0,1.6f),FVector(RoadWidth,RoadWidth,3),Rot);
        }
    }
    // dev-route-tools: challenge packs (each with its radius) and the breach rift at the wave start, rebuilt on every edit.
    if(BayDais&&BayStone&&BreachRift)
    {
        BayDais->ClearInstances();BayStone->ClearInstances();BreachRift->ClearInstances();
        for(UTextRenderComponent* Label:RouteLabels)if(IsValid(Label))Label->DestroyComponent();
        for(AActor* Lamp:RouteLights)if(IsValid(Lamp))Lamp->Destroy();
        RouteLabels.Reset();RouteLights.Reset();
        auto Text=[&](const FString& Str,FVector P,float Size,FColor Color) {
            auto* T=NewObject<UTextRenderComponent>(this);T->SetupAttachment(RootComponent);
            T->SetWorldLocation(P);T->SetWorldRotation(FRotator(0,180,0));T->SetText(FText::FromString(Str));
            T->SetHorizontalAlignment(EHTA_Center);T->SetWorldSize(Size);T->SetTextRenderColor(Color);
            T->RegisterComponent();AddInstanceComponent(T);RouteLabels.Add(T);
        };
        auto Light=[&](FVector P,FLinearColor Color,float Intensity,float Radius) {
            FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;
            auto* L=GetWorld()->SpawnActor<APointLight>(P,FRotator::ZeroRotator,Params);if(!L)return;
            L->PointLightComponent->SetIntensity(Intensity);L->PointLightComponent->SetLightColor(Color);
            L->PointLightComponent->SetAttenuationRadius(Radius);L->PointLightComponent->SetCastShadows(false);
            if(bCastleTown&&CireTownMap::RealmAt(P)==1){L->PointLightComponent->LightingChannels.bChannel0=false;L->PointLightComponent->LightingChannels.bChannel1=true;} // medieval-kingdom
            RouteLights.Add(L);
        };
        for(int Team=0;Team<2;++Team) {
            for(int32 Bay=1,Bays=CireLanePath::BayCount(GetWorld(),Team);Bay<=Bays;++Bay) {
                const FCireChallengeBay Pack=CireLanePath::BayAt(R,Team,Bay);
                const FVector P=CireLanePath::ChallengePosition(GetWorld(),Team,Bay,0);
                const float Dais=Pack.Radius*470.f/FCireChallengeBay::DefaultRadius; // 470 cm across at the default radius
                Add(BayDais,P+FVector(0,0,1),FVector(Dais,Dais,6));
                Add(BayStone,P+FVector(0,0,3),FVector(40,40,6));
                Text(FString::Printf(TEXT("CHALLENGE  %d"),Pack.Tier),P+FVector(0,0,380),40,FColor(220,171,75));
                Light(P+FVector(0,0,300),FLinearColor(1.f,.55f,.25f),5000,FMath::Max(700.f,Pack.Radius*1.5f));
            }
            const FVector Spawn=CireLanePath::SpawnPosition(GetWorld(),Team,0);
            // The breach: a glowing rift in the dead fields beyond the town gate.
            // A jagged, burning crack hanging in the air: narrow zig-zag shards rather than a slab.
            FVector Prev=Spawn+FVector(430,0,20);
            const float Offsets[]={-38,46,-22,58,-50,30,-12,40,-26,8};
            for(int32 K=0;K<10;++K)
            {
                const FVector Next=Spawn+FVector(430,Offsets[K],80+K*62);
                const FVector Mid=(Prev+Next)*.5f,Dir=(Next-Prev);
                const float Width=K<2||K>7?10.f:22.f-FMath::Abs(K-4.5f)*2.f;
                Add(BreachRift,Mid,FVector(8,Width,Dir.Size()+6),FRotator(0,0,FMath::RadiansToDegrees(FMath::Atan2(Dir.Y,Dir.Z))));
                Prev=Next;
            }
            Light(Spawn+FVector(300,0,220),FLinearColor(1.f,.25f,.08f),16000,1400);
            Text(TEXT("THE BREACH"),Spawn+FVector(380,0,560),60,FColor(228,155,137));
        }
    }
    RenderedRouteRevision=CireLanePath::Revision(GetWorld());
}
