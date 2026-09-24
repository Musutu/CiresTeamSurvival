#include "CireTargeting.h"
#include "CireSelection.h"
#include "CireGame.h"
#include "CireHUD.h"
#include "CireDeveloperTools.h"
#include "CireSkillTuning.h"
#include "CireAbilityLibrary.h"
#include "CireSkillRuntime.h"
#include "CireConstruct.h"
#include "CireLanePath.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/OverlapResult.h"
#include "InputCoreTypes.h"
#include "Algo/Reverse.h"

namespace
{
struct FTargetState
{
    FCireTargetingSnapshot View;
    TWeakObjectPtr<ACireHero> Hero;
    TWeakObjectPtr<AActor> Preview;
    TWeakObjectPtr<UProceduralMeshComponent> Mesh;
    int32 Phase=INDEX_NONE;
    uint64 ArmedFrame=0;
    TArray<FVector2D> CachedBoundary;
    bool bMeshBuilt=false,bLastValid=false;
};
TMap<TWeakObjectPtr<ACireController>,FTargetState> States;
FCireTargetDescriptor RuntimeDescriptor(UWorld* World,const FString& Id)
{
    auto D=CireTargeting::Describe(Id);
    if(D.bProjectile)if(const auto* Authored=CireSkillTuning::FindSkillshot(Id))
    {
        auto Spec=*Authored;CireDeveloperTools::AdjustSkillshot(World,Spec);
        D.Footprint.Width=Spec.Radius*2;
        D.Footprint.Length=FMath::Min(Spec.MaxRange,Spec.Speed*Spec.LifetimeSeconds);
    }
    return D;
}
bool InRealm(ACireHero* H,FVector P,float Margin=0)
{
    if(!H||P.ContainsNaN()||H->TeamId<0||H->TeamId>1)return false;
    if(auto* M=H->GetWorld()->GetAuthGameMode<ACireGameMode>())return CireSkillRuntime::InRealmBounds(M,H->TeamId,P,Margin);
    const auto* S=H->GetWorld()->GetGameState<ACireGameState>();if(!S)return false;
    if(S->Phase==2)return FMath::Abs(P.X)<=2100-Margin&&FMath::Abs(P.Y-(10000+S->ArenaIndex*6000))<=1450-Margin;
    return S->Phase==0&&CireLanePath::Contains(H->GetWorld(),H->TeamId,P,Margin);
}
bool FloorAt(ACireHero* H,FVector P,FVector& Ground,float Tolerance=700,float MinNormal=.8f)
{
    FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(CireAimFloor),false,H);
    if(!H->GetWorld()->LineTraceSingleByObjectType(Hit,P+FVector(0,0,150),P-FVector(0,0,Tolerance),FCollisionObjectQueryParams(ECC_WorldStatic),Q)||Hit.ImpactNormal.Z<MinNormal)return false;
    Ground=Hit.ImpactPoint;return true;
}
bool Sight(ACireHero* H,FVector P)
{
    FCollisionQueryParams Q(SCENE_QUERY_STAT(CireAimSight),false,H);
    FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    TArray<FHitResult> Hits;H->GetWorld()->LineTraceMultiByObjectType(Hits,H->GetActorLocation()+FVector(0,0,35),P+FVector(0,0,60),Objects,Q);
    for(const auto& Hit:Hits)
        if(Hit.GetComponent()&&Hit.GetComponent()->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block)return false;
    return true;
}
bool CursorGround(ACireController* C,FVector& P)
{
    FVector Origin,Direction;if(!C->DeprojectMousePositionToWorld(Origin,Direction))return false;
    FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(CireAimCursor),false,C->GetPawn());
    if(!C->GetWorld()->LineTraceSingleByObjectType(Hit,Origin,Origin+Direction*50000,FCollisionObjectQueryParams(ECC_WorldStatic),Q)||Hit.ImpactNormal.Z<.8f)return false;
    P=Hit.ImpactPoint;return true;
}
double Cross(FVector2D A,FVector2D B){return A.X*B.Y-A.Y*B.X;}
TArray<int32> Triangulate(const TArray<FVector2D>& Points)
{
    TArray<int32> Remaining,Result;double Area=0;
    for(int32 I=0;I<Points.Num();++I){Remaining.Add(I);Area+=Cross(Points[I],Points[(I+1)%Points.Num()]);}
    if(Area<0)Algo::Reverse(Remaining);
    for(int32 Budget=Points.Num()*Points.Num();Remaining.Num()>2&&Budget>0;--Budget)
    {
        bool Clipped=false;
        for(int32 I=0;I<Remaining.Num();++I)
        {
            const int32 A=Remaining[(I+Remaining.Num()-1)%Remaining.Num()],B=Remaining[I],C=Remaining[(I+1)%Remaining.Num()];
            if(Cross(Points[B]-Points[A],Points[C]-Points[B])<=.001)continue;
            bool Occupied=false;
            for(int32 J:Remaining)if(J!=A&&J!=B&&J!=C&&Cross(Points[B]-Points[A],Points[J]-Points[A])>=-.001&&
                Cross(Points[C]-Points[B],Points[J]-Points[B])>=-.001&&Cross(Points[A]-Points[C],Points[J]-Points[C])>=-.001){Occupied=true;break;}
            if(Occupied)continue;Result.Append({A,B,C});Remaining.RemoveAt(I);Clipped=true;break;
        }
        if(!Clipped)break;
    }
    return Result;
}
void Render(ACireController* C,FTargetState& S,const FCireTargetDescriptor& D,FVector Center,FRotator Heading)
{
    if(!S.Preview.IsValid())
    {
        FActorSpawnParameters P;P.Owner=C;P.ObjectFlags|=RF_Transient;
        auto* A=C->GetWorld()->SpawnActor<AActor>(AActor::StaticClass(),Center,Heading,P);if(!A)return;
        A->SetReplicates(false);A->SetActorEnableCollision(false);A->SetActorTickEnabled(false);
        auto* Mesh=NewObject<UProceduralMeshComponent>(A,TEXT("LocalCastPreview"));A->SetRootComponent(Mesh);A->AddInstanceComponent(Mesh);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetGenerateOverlapEvents(false);Mesh->SetCanEverAffectNavigation(false);Mesh->SetCastShadow(false);Mesh->RegisterComponent();
        auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/M_GroundArea.M_GroundArea"));
        if(!Material)Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
        Mesh->SetMaterial(0,Material);Mesh->SetMaterial(1,Material);
        S.Preview=A;S.Mesh=Mesh;
    }
    if(!S.Mesh.IsValid())return;S.Preview->SetActorHiddenInGame(false);S.Preview->SetActorLocationAndRotation(Center,Heading);
    const auto Points=ACireAreaEffect::BoundaryPoints(D.Footprint);
    if(S.bMeshBuilt&&S.bLastValid==S.View.bValid&&S.CachedBoundary==Points)return;
    S.bMeshBuilt=true;S.bLastValid=S.View.bValid;S.CachedBoundary=Points;
    const auto Fill=Triangulate(Points);
    const FLinearColor Tint=S.View.bValid?FLinearColor(.1f,1.1f,.65f,.18f):FLinearColor(1.3f,.12f,.07f,.18f);
    TArray<FVector> V,N;TArray<FVector2D> UV;TArray<FLinearColor> Colors;TArray<int32> Indices;
    for(FVector2D P:Points){V.Add(FVector(P.X,P.Y,6));N.Add(FVector::UpVector);UV.Add(P/2000);Colors.Add(Tint);}
    S.Mesh->CreateMeshSection_LinearColor(0,V,Fill,N,UV,Colors,TArray<FProcMeshTangent>(),false);
    V.Reset();N.Reset();UV.Reset();Colors.Reset();
    for(int32 I=0;I<Points.Num();++I)
    {
        const auto A=Points[I],B=Points[(I+1)%Points.Num()],Dir=(B-A).GetSafeNormal();const FVector2D Offset(-Dir.Y*3,Dir.X*3);const int32 First=V.Num();
        for(auto P:{A-Offset,B-Offset,B+Offset,A+Offset}){V.Add(FVector(P.X,P.Y,7));N.Add(FVector::UpVector);UV.Add(P/2000);Colors.Add(FLinearColor(Tint.R,Tint.G,Tint.B,.9f));}
        Indices.Append({First,First+1,First+2,First,First+2,First+3});
    }
    S.Mesh->CreateMeshSection_LinearColor(1,V,Indices,N,UV,Colors,TArray<FProcMeshTangent>(),false);
}
}

FCireTargetDescriptor CireTargeting::Describe(const FString& Id)
{
    FCireTargetDescriptor D;
    if(ACireHero::IsPassive(Id)){D.Label=TEXT("Self / Passive");return D;}
    if(const auto* A=CireAbilityLibrary::Find(Id))
    {D.Kind=ECireTargetKind::Ground;D.Label=TEXT("Ground");D.Range=A->CastRange;D.Footprint=A->Area;D.bHasFootprint=true;D.bDirectional=A->Area.Shape==ECireAreaShape::Cone||A->Area.Shape==ECireAreaShape::Line;return D;}
    if(Id==TEXT("ember_lance")||Id==TEXT("frost_bind")||Id==TEXT("piercing_shot"))
    {
        if(const auto* S=CireSkillTuning::FindSkillshot(Id)){D.Kind=ECireTargetKind::Ground;D.Label=TEXT("Ground aim / Enemy skillshot");D.Range=S->CastRange;D.bDirectional=D.bProjectile=D.bHasFootprint=true;D.Footprint.Shape=ECireAreaShape::Line;D.Footprint.Length=FMath::Min(S->MaxRange,S->Speed*S->LifetimeSeconds);D.Footprint.Width=S->Radius*2;}
        return D;
    }
    if(Id==TEXT("summoned_wall")||Id==TEXT("protection_dome"))
    {
        if(const auto* S=CireSkillTuning::FindConstruct(Id)){D.Kind=ECireTargetKind::Ground;D.Label=TEXT("Ground placement");D.Range=S->CastRange;D.bHasFootprint=true;D.Footprint.Shape=ECireAreaShape::Custom;
            const float X=S->Depth*.5f,Y=S->Width*.5f;D.Footprint.CustomPolygon={{-X,-Y},{X,-Y},{X,Y},{-X,Y}};}
        return D;
    }
    if(Id==TEXT("oathbound_guardian")||Id==TEXT("spectral_pack"))
    {
        if(const auto* S=CireSkillTuning::FindSummon(Id)){D.Kind=ECireTargetKind::Ground;D.Label=S->bCommandable?TEXT("Ground / summon ally"):TEXT("Ground / Enemy required");D.Range=S->CastRange;D.bNeedsHostile=!S->bCommandable;D.bHasFootprint=true;D.Footprint.Radius=S->Count==1?45:230;}
        return D;
    }
    if(Id==TEXT("starfall"))
    {if(const auto* S=CireSkillTuning::FindRoleSkill(Id)){D.Kind=ECireTargetKind::Ground;D.Label=TEXT("Ground");D.Range=S->CastRange;D.bHasFootprint=true;D.Footprint.Radius=S->Radius;}return D;}
    if(Id==TEXT("restoring_light")||Id==TEXT("purify")||Id==TEXT("wellspring"))
    {D.Kind=ECireTargetKind::Friendly;D.Label=TEXT("Ally / Self fallback");D.bSelfFallback=true;D.Range=1200;if(const auto* S=CireSkillTuning::FindRoleSkill(Id))D.Range=S->CastRange;return D;}
    if(Id==TEXT("iron_guard")||Id==TEXT("second_wind")||Id==TEXT("last_stand")||Id==TEXT("bastion_of_dawn")||
        Id==TEXT("war_cry")||Id==TEXT("seismic_reprisal")||Id==TEXT("challenge_of_iron")||Id==TEXT("sanctuary")||Id==TEXT("renewal")||Id==TEXT("mass_aegis"))
    {
        D.Kind=ECireTargetKind::Self;D.Label=TEXT("Self");
        if(Id==TEXT("sanctuary")||Id==TEXT("renewal")||Id==TEXT("mass_aegis"))D.Label=TEXT("Self / nearby Allies");
        if(Id==TEXT("bastion_of_dawn"))D.Label=TEXT("Self heal / nearby Ally guard");
        if(Id==TEXT("war_cry")||Id==TEXT("seismic_reprisal")||Id==TEXT("challenge_of_iron"))D.Label=TEXT("Self / nearby Enemies");
        return D;
    }
    bool Known=false;for(const auto& Skill:Cires::StarterSkillPool())Known|=Id==UTF8_TO_TCHAR(Skill.Id.c_str());
    if(!Known)return D;
    D.Kind=ECireTargetKind::Hostile;D.Label=TEXT("Enemy");D.Range=1200;
    if(Id==TEXT("shield_slam"))D.Range=240;else if(Id==TEXT("cleaving_strike"))D.Range=300;else if(Id==TEXT("shadow_step"))D.Range=850;
    else if(Id==TEXT("cataclysm")||Id==TEXT("executioners_verdict"))D.Range=1500;
    if(const auto* S=CireSkillTuning::FindRoleSkill(Id))D.Range=S->CastRange;
    return D;
}

bool CireTargeting::ValidateGround(ACireHero* H,const FString& Id,FVector Point,FVector& Center,FRotator& Heading,FString& Reason)
{
    auto Fail=[&](const TCHAR* Text){Reason=Text;return false;};
    if(!IsValid(H)||H->bDead||!H->bDrafted||Point.ContainsNaN())return Fail(TEXT("Champion unavailable."));
    const int32 Phase=CireSkillRuntime::Phase(H->GetWorld());if(Phase!=0&&Phase!=2)return Fail(TEXT("Abilities require an active combat phase."));
    const auto D=Describe(Id);if(D.Kind!=ECireTargetKind::Ground)return Fail(TEXT("This skill does not target ground."));
    if(D.bNeedsHostile&&!H->IsHostile(H->Target))return Fail(TEXT("Select an Enemy before placing these summons."));
    if(!InRealm(H,Point))return Fail(TEXT("Aim inside your battlefield."));
    if(FVector::DistSquared2D(H->GetActorLocation(),Point)>FMath::Square(D.Range))return Fail(TEXT("Ground is outside casting range."));
    FVector Ground;if(!FloorAt(H,Point,Ground)||FMath::Abs(Ground.Z-Point.Z)>35)return Fail(TEXT("Aim at supported ground."));
    const FVector Direction=(Point-H->GetActorLocation()).GetSafeNormal2D();
    if(D.bProjectile&&Direction.IsNearlyZero())return Fail(TEXT("Aim away from the caster to launch a projectile."));
    Heading=Direction.IsNearlyZero()?H->GetActorRotation():Direction.Rotation();Heading.Pitch=0;Heading.Roll=0;Center=Ground;
    if(D.bDirectional&&!FloorAt(H,H->GetActorLocation(),Center))return Fail(TEXT("No ground under caster."));
    if(!Sight(H,Ground))return Fail(TEXT("A wall blocks this aim."));
    if(const auto* S=CireSkillTuning::FindConstruct(Id))
    {
        const auto Rotation=Heading.Quaternion();const FVector Half(S->Depth*.5f,S->Width*.5f,S->Height*.5f);
        if(CireSkillRuntime::Phase(H->GetWorld())!=2)
        {
            const FVector Offset=FVector(-1850,CireLanePath::CenterY(H->TeamId),Center.Z)-Center;
            const FVector XAxis=Rotation.GetAxisX(),YAxis=Rotation.GetAxisY();
            const bool bSeparated=FMath::Abs(Offset.X)>450+FMath::Abs(XAxis.X)*Half.X+FMath::Abs(YAxis.X)*Half.Y||
                FMath::Abs(Offset.Y)>900+FMath::Abs(XAxis.Y)*Half.X+FMath::Abs(YAxis.Y)*Half.Y||
                FMath::Abs(FVector::DotProduct(Offset,XAxis))>Half.X+450*FMath::Abs(XAxis.X)+900*FMath::Abs(XAxis.Y)||
                FMath::Abs(FVector::DotProduct(Offset,YAxis))>Half.Y+450*FMath::Abs(YAxis.X)+900*FMath::Abs(YAxis.Y);
            if(!bSeparated)return Fail(TEXT("Construct cannot overlap town."));
        }
        for(float X:{-1.f,0.f,1.f})for(float Y:{-1.f,0.f,1.f})
        {
            const FVector P=Center+Rotation.RotateVector(FVector(X*Half.X,Y*Half.Y,0));FVector Support;
            if(!InRealm(H,P,10)||!FloorAt(H,P,Support,50,.9f)||FMath::Abs(Support.Z-Center.Z)>12)return Fail(TEXT("Footprint crosses an edge or lacks level support."));
        }
        FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);Objects.AddObjectTypesToQuery(ECC_Pawn);
        TArray<FOverlapResult> Hits;FCollisionQueryParams Q(SCENE_QUERY_STAT(CireAimConstruct),false);
        H->GetWorld()->OverlapMultiByObjectType(Hits,Center+FVector(0,0,Half.Z+3),Rotation,Objects,FCollisionShape::MakeBox(Half),Q);
        for(const auto& Hit:Hits)if(Hit.GetComponent()&&(Cast<ACharacter>(Hit.GetActor())||Cast<ACireConstruct>(Hit.GetActor())||Hit.GetComponent()->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block))return Fail(TEXT("Construct overlaps a unit or obstacle."));
    }
    Reason=TEXT("Left click to cast; Esc or right mouse cancels.");return true;
}

FCireTargetingSnapshot CireTargeting::Snapshot(const ACireController* C)
{
    if(const auto* S=States.Find(TWeakObjectPtr<ACireController>(const_cast<ACireController*>(C))))return S->View;
    return {};
}
void CireTargeting::Cleanup(ACireController* C)
{
    const TWeakObjectPtr<ACireController> Key(C);if(auto* S=States.Find(Key)){if(S->Preview.IsValid())S->Preview->Destroy();States.Remove(Key);}
}
void CireTargeting::Cancel(ACireController* C){Cleanup(C);}
void CireTargeting::Request(ACireController* C,int32 Slot)
{
    if(!IsValid(C)||!C->IsLocalController())return;
    auto* H=Cast<ACireHero>(C->GetPawn());auto* HUD=Cast<ACireHUD>(C->GetHUD());
    if(!H||H->bDead||!H->bDrafted||!H->Skills.IsValidIndex(Slot)||!H->Cooldowns.IsValidIndex(Slot)||H->Cooldowns[Slot]>0)return;
    const FString Id=H->Skills[Slot];const auto D=Describe(Id);Cancel(C);
    const int32 Phase=CireSkillRuntime::Phase(H->GetWorld());if(Phase!=0&&Phase!=2){H->Notice=TEXT("Abilities require an active combat phase.");return;}
    if(D.Kind==ECireTargetKind::None){H->Notice=TEXT("This passive is always active.");return;}
    if(D.Kind==ECireTargetKind::Hostile&&(!H->IsHostile(H->Target)||!H->InRange(H->Target,D.Range))){H->Notice=TEXT("Select an Enemy within the ability range.");return;}
    if(D.Kind!=ECireTargetKind::Ground){C->ServerAction(2,Slot,nullptr);return;}
    if(D.bNeedsHostile&&!H->IsHostile(H->Target)){H->Notice=TEXT("Select an Enemy before placing these summons.");return;}
    if(HUD&&HUD->UISettings.bQuickGroundCast)
    {
        FVector Point,Center;FRotator Heading;FString Reason;
        if(CursorGround(C,Point)&&ValidateGround(H,Id,Point,Center,Heading,Reason))C->ServerCastAt(Slot,Point);
        else H->Notice=Reason.IsEmpty()?TEXT("Aim at battlefield ground."):Reason;
        return;
    }
    auto& S=States.Add(TWeakObjectPtr<ACireController>(C));S.Hero=H;S.Phase=CireSkillRuntime::Phase(C->GetWorld());S.ArmedFrame=GFrameCounter;
    S.View.bActive=true;S.View.Slot=Slot;S.View.SkillId=Id;S.View.Range=D.Range;S.View.Message=TEXT("Aim at ground, then left click. Esc/right mouse cancels.");
    C->bSummonMoveTargeting=false;
}
bool CireTargeting::Tick(ACireController* C)
{
    for(auto It=States.CreateIterator();It;++It)if(!It.Key().IsValid()){if(It.Value().Preview.IsValid())It.Value().Preview->Destroy();It.RemoveCurrent();}
    auto* S=States.Find(TWeakObjectPtr<ACireController>(C));if(!S)return false;
    auto* H=Cast<ACireHero>(C->GetPawn());auto* HUD=Cast<ACireHUD>(C->GetHUD());
    if(H!=S->Hero.Get()||!H||H->bDead||!H->bDrafted||!H->Skills.IsValidIndex(S->View.Slot)||H->Skills[S->View.Slot]!=S->View.SkillId||
        S->Phase!=CireSkillRuntime::Phase(C->GetWorld())||C->bShop||C->bChatInput||(H->Offers.Num()>0&&(!HUD||HUD->IsSkillOfferOpen()))||(HUD&&HUD->IsBlockingGameplayInput())){Cancel(C);return false;}
    if(C->WasInputKeyJustPressed(EKeys::Escape)||C->WasInputKeyJustPressed(EKeys::RightMouseButton)){Cancel(C);return true;}
    const auto D=RuntimeDescriptor(C->GetWorld(),S->View.SkillId);FVector Center=FVector::ZeroVector;FRotator Heading=FRotator::ZeroRotator;
    const bool bGround=CursorGround(C,S->View.Point);S->View.bValid=bGround&&ValidateGround(H,S->View.SkillId,S->View.Point,Center,Heading,S->View.Message);
    if(!bGround){S->View.Message=TEXT("Aim at battlefield ground.");if(S->Preview.IsValid())S->Preview->SetActorHiddenInGame(true);}
    else
    {
        if(!S->View.bValid){Center=S->View.Point;Heading=(Center-H->GetActorLocation()).GetSafeNormal2D().Rotation();if(D.bDirectional)FloorAt(H,H->GetActorLocation(),Center);}
        Render(C,*S,D,Center,Heading);
    }
    if(C->WasInputKeyJustPressed(EKeys::LeftMouseButton)&&GFrameCounter>S->ArmedFrame&&(!HUD||!HUD->IsPointerOverInterface()))
    {
        if(S->View.bValid){const int32 Slot=S->View.Slot;const FVector Point=S->View.Point;Cancel(C);C->ServerCastAt(Slot,Point);}
        return true;
    }
    return false;
}

#if !UE_BUILD_SHIPPING
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "Misc/ScopeExit.h"

bool CireTargeting::RunDescriptorSmoke()
{
    bool Pass=true;int32 Checks=0;
    auto Check=[&](bool Value){++Checks;Pass&=Value;};
    Check(Describe(TEXT("second_wind")).Kind==ECireTargetKind::Self);
    Check(Describe(TEXT("restoring_light")).Kind==ECireTargetKind::Friendly&&Describe(TEXT("restoring_light")).bSelfFallback);
    Check(Describe(TEXT("shield_slam")).Kind==ECireTargetKind::Hostile);
    Check(Describe(TEXT("stone_skin")).Kind==ECireTargetKind::None);
    for(const TCHAR* Id:{TEXT("venom_ground"),TEXT("cinder_cone"),TEXT("grave_line"),TEXT("ashen_square"),TEXT("blight_sigil")})
    {
        const auto D=Describe(Id);const auto* A=CireAbilityLibrary::Find(Id);Check(A&&D.Kind==ECireTargetKind::Ground&&D.Range==A->CastRange);
        if(A)Check(ACireAreaEffect::BoundaryPoints(D.Footprint)==ACireAreaEffect::BoundaryPoints(A->Area));
    }
    const auto Shot=Describe(TEXT("piercing_shot"));const auto* S=CireSkillTuning::FindSkillshot(TEXT("piercing_shot"));
    Check(S&&Shot.bDirectional&&Shot.bProjectile&&Shot.Footprint.Width==S->Radius*2&&Shot.Footprint.Length==FMath::Min(S->MaxRange,S->Speed*S->LifetimeSeconds));
    const auto* Wall=CireSkillTuning::FindConstruct(TEXT("summoned_wall"));const auto D=Describe(TEXT("summoned_wall"));
    Check(Wall&&D.Footprint.CustomPolygon.Num()==4);
    if(Wall&&D.Footprint.CustomPolygon.Num()==4)Check(D.Footprint.CustomPolygon[2].Equals(FVector2D(Wall->Depth*.5f,Wall->Width*.5f)));
    UE_LOG(LogTemp,Display,TEXT("CIRE_TARGETING_DESCRIPTOR_%s checks=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Checks);return Pass;
}

bool CireTargeting::RunRuntimeSmoke(ACireGameMode* Mode)
{
    if(!IsValid(Mode)||!Mode->HasAuthority())return false;
    auto* Controller=Cast<ACireController>(Mode->GetWorld()->GetFirstPlayerController());
    if(!Controller||!Controller->IsLocalController())return false;
    bool Pass=true;int32 Checks=0;TArray<AActor*> Actors;
    auto Check=[&](bool Value,const TCHAR* Why){++Checks;if(!Value){Pass=false;UE_LOG(LogTemp,Error,TEXT("CIRE_TARGETING_RUNTIME_FAIL %s"),Why);}};
    const auto SavedClock=Mode->Clock;APawn* SavedPawn=Controller->GetPawn();AActor* SavedView=Controller->GetViewTarget();
    const auto SavedRotation=Controller->GetControlRotation();const bool SavedSummonAim=Controller->bSummonMoveTargeting;
    auto* HUD=Cast<ACireHUD>(Controller->GetHUD());const bool SavedQuick=HUD&&HUD->UISettings.bQuickGroundCast;
    Mode->Clock=Cires::MatchClock();if(HUD)HUD->UISettings.bQuickGroundCast=false;
    ON_SCOPE_EXIT
    {
        Cleanup(Controller);Controller->UnPossess();if(IsValid(SavedPawn))Controller->Possess(SavedPawn);
        if(IsValid(SavedView))Controller->SetViewTarget(SavedView);Controller->SetControlRotation(SavedRotation);
        Controller->bSummonMoveTargeting=SavedSummonAim;if(HUD)HUD->UISettings.bQuickGroundCast=SavedQuick;
        for(int32 I=Actors.Num()-1;I>=0;--I)if(IsValid(Actors[I])){ACireAreaEffect::ClearForActor(Actors[I]);Actors[I]->Destroy();}
        Mode->Clock=SavedClock;
    };
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FVector Ground(0,-2100,5200),Aim=Ground+FVector(500,0,0);
    auto Box=[&](FVector Center,FVector Half)
    {
        auto* A=Mode->GetWorld()->SpawnActor<AActor>(Center,FRotator::ZeroRotator,Spawn);if(!A)return A;Actors.Add(A);
        auto* B=NewObject<UBoxComponent>(A);A->SetRootComponent(B);A->AddInstanceComponent(B);
        B->SetBoxExtent(Half);B->SetCollisionObjectType(ECC_WorldStatic);B->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        B->SetCollisionResponseToAllChannels(ECR_Block);B->RegisterComponent();A->SetActorLocation(Center);return A;
    };
    if(!Box(Ground-FVector(0,0,50),FVector(1100,400,50)))return false;
    auto* Hero=Mode->GetWorld()->SpawnActor<ACireHero>(Ground+FVector(0,0,92),FRotator::ZeroRotator,Spawn);
    if(!Hero)return false;Actors.Add(Hero);Hero->SetActorTickEnabled(false);Hero->SetActorEnableCollision(false);Hero->TeamId=0;Hero->Draft(1);
    Hero->Skills={TEXT("venom_ground")};Hero->Cooldowns={0};Hero->Offers.Reset();Hero->GlobalCooldown=0;Hero->Mana=1000;
    Controller->Possess(Hero);
    FVector Center;FRotator Heading;FString Reason;
    Check(ValidateGround(Hero,TEXT("venom_ground"),Aim,Center,Heading,Reason)&&Center.Equals(Aim,.1),TEXT("supported in-range aim accepts the exact cursor floor"));
    Check(!ValidateGround(Hero,TEXT("venom_ground"),Ground+FVector(1500,0,0),Center,Heading,Reason)&&Reason.Contains(TEXT("range")),TEXT("out-of-range aim rejected"));
    Check(!ValidateGround(Hero,TEXT("venom_ground"),FVector(0,2100,5200),Center,Heading,Reason)&&Reason.Contains(TEXT("battlefield")),TEXT("opposing PvE realm rejected"));
    Check(!ValidateGround(Hero,TEXT("venom_ground"),Ground+FVector(0,600,0),Center,Heading,Reason)&&Reason.Contains(TEXT("ground")),TEXT("unsupported elevated aim rejected without snapping to distant floor"));
    auto* Wall=Box(Ground+FVector(250,0,170),FVector(25,160,170));
    Check(Wall&&!ValidateGround(Hero,TEXT("venom_ground"),Aim,Center,Heading,Reason)&&Reason.Contains(TEXT("wall")),TEXT("solid wall blocks local and server preflight"));
    if(Wall)Wall->SetActorEnableCollision(false);
    Check(ValidateGround(Hero,TEXT("cinder_cone"),Aim,Center,Heading,Reason)&&Center.Equals(Ground,.1)&&Heading.IsNearlyZero(),TEXT("directional cone anchors beneath caster and faces cursor"));
    Check(ValidateGround(Hero,TEXT("summoned_wall"),Aim,Center,Heading,Reason),TEXT("clear supported construct footprint accepted"));
    auto* Obstacle=Box(Aim+FVector(0,0,80),FVector(20,20,80));
    Check(Obstacle&&!ValidateGround(Hero,TEXT("summoned_wall"),Aim,Center,Heading,Reason),TEXT("occupied construct footprint rejected"));
    if(Obstacle)Obstacle->SetActorEnableCollision(false);
    auto AreaCount=[&](){int32 N=0;for(TActorIterator<ACireAreaEffect> It(Mode->GetWorld());It;++It)if(!It->IsActorBeingDestroyed())++N;return N;};
    const int32 Before=AreaCount();const float ManaBefore=Hero->Mana;
    Request(Controller,0);const auto Armed=Snapshot(Controller);
    Check(Armed.bActive&&Armed.Slot==0&&FMath::IsNearlyEqual(Armed.Range,Describe(TEXT("venom_ground")).Range),TEXT("request arms selected skill and publishes its casting range"));
    if(auto* State=States.Find(TWeakObjectPtr<ACireController>(Controller)))
    {
        State->View.bValid=true;State->View.Point=Aim;Render(Controller,*State,Describe(TEXT("venom_ground")),Aim,FRotator::ZeroRotator);
        Check(State->Preview.IsValid()&&State->Mesh.IsValid()&&!State->Preview->GetIsReplicated()&&!State->Preview->GetActorEnableCollision()&&
            State->Mesh->GetCollisionEnabled()==ECollisionEnabled::NoCollision&&!State->Mesh->CastShadow,TEXT("preview is local cosmetic geometry without replication collision or shadow"));
        const auto* Fill=State->Mesh.IsValid()?State->Mesh->GetProcMeshSection(0):nullptr;
        Check(Fill&&Fill->ProcVertexBuffer.Num()==ACireAreaEffect::BoundaryPoints(Describe(TEXT("venom_ground")).Footprint).Num()&&!Fill->ProcIndexBuffer.IsEmpty(),TEXT("filled preview uses native area boundary vertices"));
        const TWeakObjectPtr<AActor> Preview=State->Preview;
        Cancel(Controller);Check(!Snapshot(Controller).bActive&&(!Preview.IsValid()||Preview->IsActorBeingDestroyed()),TEXT("cancel destroys preview and clears aiming state"));
    }
    Check(AreaCount()==Before&&Hero->Mana==ManaBefore&&Hero->Cooldowns[0]==0,TEXT("arming and rendering never spawn damaging areas or charge resources"));
    Request(Controller,0);Hero->bDead=true;Tick(Controller);
    Check(!Snapshot(Controller).bActive,TEXT("death immediately cancels local aiming"));Hero->bDead=false;
    Controller->ServerCastAt_Implementation(0,Ground+FVector(1500,0,0));
    Check(AreaCount()==Before&&Hero->Mana==ManaBefore&&Hero->Cooldowns[0]==0,TEXT("authoritative RPC rejects invalid aim without payment"));
    Controller->ServerCastAt_Implementation(0,Aim);
    Check(AreaCount()==Before+1&&Hero->Mana<ManaBefore&&Hero->Cooldowns[0]>0,TEXT("confirmed authoritative aim creates one paid native ground effect"));
    bool Found=false;for(TActorIterator<ACireAreaEffect> It(Mode->GetWorld());It;++It)
        if(!It->IsActorBeingDestroyed()&&FVector::DistSquared2D(It->GetActorLocation(),Aim)<1&&FMath::Abs(It->GetActorLocation().Z-Aim.Z)<20)
        {Found=true;Check(!It->IsActive(),TEXT("confirmed ground effect remains harmless during warning"));}
    Check(Found,TEXT("ground actor appears at confirmed cursor location"));
    {
        // WoW tab targeting: camera cone first, outward by distance, Shift reverses, dead/other-realm excluded.
        auto Monster=[&](FVector Offset,int32 Lane){auto* M=Mode->GetWorld()->SpawnActor<ACireMonster>(Ground+Offset+FVector(0,0,90),FRotator::ZeroRotator,Spawn);
            if(M){Actors.Add(M);M->SetActorTickEnabled(false);M->Lane=Lane;M->Health=M->MaxHealth=100;}return M;};
        auto* Near=Monster(FVector(300,0,0),0);auto* Far=Monster(FVector(700,40,0),0);auto* Behind=Monster(FVector(-250,0,0),0);
        auto* OtherRealm=Monster(FVector(350,0,0),1);
        const FTransform Front(FRotator::ZeroRotator,Ground+FVector(-100,0,300));const FTransform Side(FRotator(0,90,0),Ground+FVector(-100,0,300));
        Check(Near&&Far&&Behind&&OtherRealm,TEXT("tab fixture monsters spawned"));
        // Park unrelated world monsters in the other realm for the duration of the tab fixture.
        TArray<TPair<TWeakObjectPtr<ACireMonster>,int32>> Parked;
        for(TActorIterator<ACireMonster> It(Mode->GetWorld());It;++It)
            if(*It!=Near&&*It!=Far&&*It!=Behind&&*It!=OtherRealm&&It->Lane==0){Parked.Add({*It,It->Lane});It->Lane=1;}
        ON_SCOPE_EXIT{for(auto& Entry:Parked)if(Entry.Key.IsValid())Entry.Key->Lane=Entry.Value;};
        if(Near&&Far&&Behind&&OtherRealm)
        {
            Hero->Target=nullptr;
            AActor* T=CireSelection::NextTarget(Controller,false,false,&Front);Hero->Target=T;
            Check(T==Near,TEXT("tab picks nearest hostile in front of the camera, not the closer one behind"));
            T=CireSelection::NextTarget(Controller,false,false,&Front);Hero->Target=T;
            Check(T==Far,TEXT("second tab cycles outward by distance"));
            T=CireSelection::NextTarget(Controller,false,false,&Front);Hero->Target=T;
            Check(T==Near,TEXT("tab wraps to the nearest after visiting every front candidate"));
            T=CireSelection::NextTarget(Controller,false,true,&Front);Hero->Target=T;
            Check(T==Far,TEXT("shift-tab walks back through the tab history"));
            T=CireSelection::NextTarget(Controller,false,false,&Side);Hero->Target=T;
            Check(T!=OtherRealm&&T!=nullptr,TEXT("other-realm monster never tab-selected"));
            Hero->Target=nullptr;Behind->Health=0;
            T=CireSelection::NextTarget(Controller,false,false,&Side);
            Check(T==Near,TEXT("no candidate in view falls back to nearest living hostile around the hero"));
            Behind->Health=100;
            Hero->Target=Near;CireSelection::HandleTargetLoss(Controller,false);Near->Health=0;CireSelection::HandleTargetLoss(Controller,false);
            Check(Hero->Target==nullptr,TEXT("target clears when the hostile target dies"));
            Hero->Target=Far;CireSelection::HandleTargetLoss(Controller,true);Far->Health=0;CireSelection::HandleTargetLoss(Controller,true);
            Check(Hero->Target==Behind,TEXT("optional auto-reacquire selects the next living hostile"));
        }
    }
    UE_LOG(LogTemp,Display,TEXT("CIRE_TARGETING_RUNTIME_%s checks=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Checks);return Pass;
}
#endif
