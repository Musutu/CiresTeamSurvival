// ability-vfx: native tests for shape-true telegraphs, line indicators, lifecycles, release sync and budgets.
#include "CireAbilityVFX.h"
#if !UE_BUILD_SHIPPING
#include "CireAbilityLibrary.h"
#include "CireAbilityShapes.h"
#include "CireAreaEffects.h"
#include "CireChampionActions.h"
#include "CireGame.h"
#include "CireNPCArchetypes.h"
#include "CireNPCCombat.h"
#include "CireSkillshot.h"
#include "CireSkillTuning.h"
#include "CireSpellMesh.h"
#include "CireSpellPresentation.h"
#include "CireTargeting.h"
#include "CireThreat.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/ScopeExit.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireAbilityVFX,Log,All);

namespace
{
struct FSuite
{
    int32 Checks=0,Failed=0;
    void Check(bool bValue,const FString& Why){++Checks;if(!bValue){++Failed;UE_LOG(LogCireAbilityVFX,Error,TEXT("CIRE_ABILITY_VFX_CHECK_FAIL %s"),*Why);}}
};
bool Near(float A,float B,float Tolerance=.5f){return FMath::Abs(A-B)<=Tolerance;}
TArray<FVector2D> Boundary(const FCireAreaSpec& S){return ACireAreaEffect::BoundaryPoints(S);}
bool SameBoundary(const FCireAreaSpec& A,const FCireAreaSpec& B)
{
    const auto X=Boundary(A),Y=Boundary(B);if(X.Num()!=Y.Num())return false;
    for(int32 J=0;J<X.Num();++J)if(!X[J].Equals(Y[J],.5))return false;
    return true;
}
// Lane vertices projected into the lane frame (origin at the caster's ground, +X along the aim).
bool LaneWithin(const ACireSpellVisual* V,FVector Origin,FVector Dir,float Length,float Width,float Slack,float& MaxAlong)
{
    MaxAlong=-1e9;if(!V||V->GetGroundVertices().IsEmpty())return false;
    const FTransform T=V->GetActorTransform();const FVector Side=FVector::CrossProduct(FVector::UpVector,Dir);
    for(const FVector& L:V->GetGroundVertices())
    {
        const FVector W=T.TransformPosition(L)-Origin;
        const float Along=static_cast<float>(FVector::DotProduct(W,Dir)),Across=static_cast<float>(FVector::DotProduct(W,Side));
        // The under-head glow is a disc around the moving projectile; ignore it (it is not the lane).
        if(Along<-Slack||Along>Length+Slack||FMath::Abs(Across)>Width*.5f+Slack)
        {
            const FVector Head=V->GetActorLocation()-Origin;
            if(FVector::Dist2D(W,Head)>Width*1.3f+12.f)return false; // head glow radius = 2.6 x collision radius
        }
        MaxAlong=FMath::Max(MaxAlong,Along);
    }
    return true;
}
}

bool CireAbilityVFX::RunTests(ACireGameMode* Mode)
{
    if(!IsValid(Mode)||!Mode->HasAuthority())return false;
    UWorld* World=Mode->GetWorld();FSuite S;
    auto Check=[&](bool bValue,const FString& Why){S.Check(bValue,Why);};
    const bool bWasEnabled=Enabled();
    Check(bWasEnabled,TEXT("new presentation enabled by default"));

    // ---------------------------------------------------------------- 1. every ability has a true hit shape
    const auto Champions=CireAbilityShapes::ChampionAbilityIds();
    Check(Champions.Num()>=37,FString::Printf(TEXT("champion ability pool covered (%d ids)"),Champions.Num()));
    for(const FName Id:Champions)
    {
        const auto Shape=CireAbilityShapes::Describe(Id);const FString Name=Id.ToString();
        if(ACireHero::IsPassive(Name)){Check(Shape.Kind==ECireHitShape::None,Name+TEXT(" passive has no telegraph"));continue;}
        Check(Shape.Kind!=ECireHitShape::None,Name+TEXT(" has a hit shape"));
        if(Shape.HasGroundShape())Check(ACireAreaEffect::ValidateSpec(Shape.AsArea()),Name+TEXT(" hit shape is a valid ground boundary"));
        // Aim footprint (cursor preview) == hit footprint for every ground-aimed ability.
        const auto D=CireTargeting::Describe(Name);
        if(Shape.bGroundAim)
        {
            Check(D.Kind==ECireTargetKind::Ground&&D.bHasFootprint,Name+TEXT(" aims on the ground"));
            Check(SameBoundary(D.Footprint,Shape.AsArea()),Name+TEXT(" aim preview boundary equals the hit boundary"));
            Check(D.bDirectional==(Shape.bFromCaster&&(Shape.Kind==ECireHitShape::Line||Shape.Kind==ECireHitShape::Cone)),Name+TEXT(" lines/cones anchor at the caster, circles at the cursor"));
        }
        if(const auto* A=CireAbilityLibrary::Find(Name))Check(SameBoundary(A->Area,Shape.AsArea())&&Near(A->Area.WarningSeconds,Shape.WarningSeconds,.001f),Name+TEXT(" telegraph matches the authored area and warning"));
        if(const auto* P=Name.StartsWith(TEXT("basic_"))?nullptr:CireSkillTuning::FindSkillshot(Name))
            Check(Shape.Kind==ECireHitShape::Line&&Shape.bProjectile&&Near(Shape.Width,P->Radius*2)&&Near(Shape.Length,FMath::Min(P->MaxRange,P->Speed*P->LifetimeSeconds)),
                Name+TEXT(" skillshot telegraph is a line of the collision diameter and true travel"));
        if(const auto* R=CireSkillTuning::FindRoleSkill(Name);R&&Shape.Kind==ECireHitShape::Circle)Check(Near(Shape.Radius,R->Radius),Name+TEXT(" role circle uses its tuned radius"));
    }
    // Monster abilities: every authored ability maps to the shape its behaviour resolves.
    int32 MonsterAbilities=0,Lines=0;
    for(const auto& Pair:CireNPCArchetypes::Get().Archetypes)for(const auto& A:Pair.Value.Abilities)
    {
        ++MonsterAbilities;const auto Shape=CireAbilityShapes::DescribeMonster(A,&Pair.Value);const FString Name=A.Id.ToString();
        switch(A.Kind)
        {
        case ECireNPCAbilityKind::Cone:Check(Shape.Kind==ECireHitShape::Cone&&Shape.bFromCaster&&Near(Shape.Radius,A.Radius)&&Near(Shape.Angle,A.Angle),Name+TEXT(" cone telegraph")); break;
        case ECireNPCAbilityKind::TargetCircle:Check(Shape.Kind==ECireHitShape::Circle&&Shape.bAtTarget&&Near(Shape.Radius,A.Radius),Name+TEXT(" target circle telegraph")); break;
        case ECireNPCAbilityKind::SelfCircle:Check(Shape.Kind==ECireHitShape::Circle&&Shape.bFromCaster&&Near(Shape.Radius,A.Radius),Name+TEXT(" self circle telegraph")); break;
        case ECireNPCAbilityKind::Charge:case ECireNPCAbilityKind::Pull:
            ++Lines;Check(Shape.Kind==ECireHitShape::Line&&Shape.bFromCaster&&Near(Shape.Width,A.Width)&&Near(Shape.Length,A.Length),Name+TEXT(" charge/pull is a lane of its true width")); break;
        case ECireNPCAbilityKind::Projectile:
            if(const auto* P=CireSkillTuning::FindSkillshot(A.Skillshot)){++Lines;Check(Shape.Kind==ECireHitShape::Line&&Shape.bProjectile&&Near(Shape.Width,P->Radius*2)&&Near(Shape.WarningSeconds,A.CastTime,.001f),Name+TEXT(" monster projectile is a lane for its cast bar"));}
            break;
        default:Check(Shape.Kind==ECireHitShape::Self||Shape.Kind==ECireHitShape::Unit||Shape.Kind==ECireHitShape::Circle,Name+TEXT(" buff/support has a caster or unit presentation"));
        }
        Check(Shape.School!=ECireSchool::Count,Name+TEXT(" has a school"));
    }
    Check(MonsterAbilities>=250&&Lines>=40,FString::Printf(TEXT("monster abilities covered (%d, %d lanes)"),MonsterAbilities,Lines));
    Check(CireAbilityShapes::SchoolFor(TEXT("drowned_rend"))==ECireSchool::Tide&&CireAbilityShapes::SchoolFor(TEXT("void_rift"))==ECireSchool::Void&&
        CireAbilityShapes::SchoolFor(TEXT("fire"))==ECireSchool::Fire&&CireAbilityShapes::SchoolFor(TEXT("frost_bind"))==ECireSchool::Frost,TEXT("school theming: tide/void monsters, skillshot styles"));

    // ---------------------------------------------------------------- 2. painter: line indicator geometry
    {
        TArray<FVector> V;TArray<int32> I;TArray<FLinearColor> C;
        for(const FName Id:{FName(TEXT("ember_lance")),FName(TEXT("frost_bind")),FName(TEXT("piercing_shot")),FName(TEXT("grave_line"))})
        {
            const auto Shape=CireAbilityShapes::Describe(Id);const auto Spec=Shape.AsArea();
            FCireGroundMesh G(V,I,C);
            const auto R=PaintTelegraph(G,Spec,StyleFor(ETone::AimValid,FLinearColor::White),.5f,1.3f,1.f,PaintArrow|PaintCenter|PaintProgress|PaintPulse);
            const FString Name=Id.ToString();
            Check(Near(R.FillBounds.Min.X,0)&&Near(R.FillBounds.Max.X,Shape.Length)&&Near(R.FillBounds.Min.Y,-Shape.Width*.5f)&&Near(R.FillBounds.Max.Y,Shape.Width*.5f),
                Name+TEXT(" line indicator spans exactly length x width from the caster"));
            Check(R.ArrowTip.X<=Shape.Length&&R.ArrowTip.X>=Shape.Length-12.f&&Near(R.ArrowTip.Y,0),Name+TEXT(" arrowhead sits at the far end, inside the lane"));
            Check(R.Chevrons>=1,Name+TEXT(" travel chevrons animate along the lane"));
            bool bInside=true;const float Glow=26.f;
            for(const FVector& P:V)bInside&=P.X>=-Glow&&P.X<=Shape.Length+Glow&&FMath::Abs(P.Y)<=Shape.Width*.5f+Glow;
            Check(bInside&&V.Num()<=CireSpellMesh::MaxGroundVertices,Name+TEXT(" no decoration leaves the lane (feather only)"));
        }
        // Cones stay cones, circles stay circles with the designated-spot marker.
        FCireAreaSpec Cone;Cone.Shape=ECireAreaShape::Cone;Cone.Radius=440;Cone.ConeAngleDegrees=120;
        {FCireGroundMesh G(V,I,C);const auto R=PaintTelegraph(G,Cone,StyleFor(ETone::Hostile,FLinearColor::White),.3f,0,1,PaintArrow|PaintProgress);
         Check(Near(R.FillBounds.Max.X,440)&&Near(R.FillBounds.Min.X,FMath::Min(0.f,440*FMath::Cos(FMath::DegreesToRadians(60.f))))&&R.Chevrons==2,TEXT("cone telegraph uses the true radius/arc"));}
        FCireAreaSpec Circle;Circle.Shape=ECireAreaShape::Circle;Circle.Radius=300;
        {FCireGroundMesh G(V,I,C);const auto R=PaintTelegraph(G,Circle,StyleFor(ETone::Hostile,FLinearColor::White),.3f,0,1,PaintCenter|PaintProgress);
         Check(Near(R.FillBounds.Max.X,300)&&Near(R.FillBounds.Min.Y,-300,1.f)&&R.Chevrons==0,TEXT("circle telegraph uses the true radius"));}
    }

    // Test stage: a raised platform in team 0's realm, the local controller possessing a test champion.
    auto* PC=Cast<ACireController>(World->GetFirstPlayerController());
    if(!PC){Check(false,TEXT("local controller"));UE_LOG(LogCireAbilityVFX,Display,TEXT("CIRE_ABILITY_VFX_TESTS_FAIL checks=%d failed=%d"),S.Checks,S.Failed);return false;}
    const auto SavedClock=Mode->Clock;APawn* SavedPawn=PC->GetPawn();AActor* SavedView=PC->GetViewTarget();
    TArray<AActor*> Actors;TArray<ACireMonster*> AddedMonsters;TArray<ACireHero*> AddedHeroes;
    Mode->Clock=Cires::MatchClock();
    ON_SCOPE_EXIT
    {
        for(auto* M:AddedMonsters)Mode->Monsters.Remove(M);
        for(auto* H:AddedHeroes)Mode->Heroes.Remove(H);
        PC->UnPossess();if(IsValid(SavedPawn))PC->Possess(SavedPawn);if(IsValid(SavedView))PC->SetViewTarget(SavedView);
        for(int32 J=Actors.Num()-1;J>=0;--J)if(IsValid(Actors[J])){ACireAreaEffect::ClearForActor(Actors[J]);ACireSkillshot::ClearForActor(Actors[J]);Actors[J]->Destroy();}
        for(TActorIterator<ACireSpellVisual> It(World);It;++It)if(!It->GetOwner()||!IsValid(It->GetOwner()))It->Destroy();
        Mode->Clock=SavedClock;
    };
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FVector Stage(6000,-2100,5600);
    if(auto* Floor=World->SpawnActor<AActor>(Stage-FVector(0,0,20),FRotator::ZeroRotator,Spawn))
    {
        Actors.Add(Floor);auto* Mesh=NewObject<UStaticMeshComponent>(Floor);Floor->SetRootComponent(Mesh);Mesh->RegisterComponent();
        Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Mesh->SetWorldScale3D(FVector(40,24,.4f));
        Mesh->SetWorldLocation(Stage-FVector(0,0,20));Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Mesh->SetCollisionObjectType(ECC_WorldStatic);
        Mesh->SetCollisionResponseToAllChannels(ECR_Block);
    }
    auto MakeHero=[&](FVector At,int32 Team,const TCHAR* Profile)
    {
        auto* H=World->SpawnActor<ACireHero>(At+FVector(0,0,92),FRotator::ZeroRotator,Spawn);
        if(H){Actors.Add(H);H->TeamId=Team;H->DraftProfile(Profile);H->Offers.Reset();H->CurrentOffer={};H->SetActorTickEnabled(false);
            H->MaxHealth=H->Health=5000;H->Mana=H->MaxMana=5000;H->Energy=100;H->Intelligence=H->Strength=H->Agility=30;Mode->Heroes.Add(H);AddedHeroes.Add(H);}
        return H;
    };
    auto MakeMonster=[&](FVector At,FName Arch)
    {
        auto* M=World->SpawnActor<ACireMonster>(At+FVector(0,0,92),FRotator(0,180,0),Spawn);
        if(M){Actors.Add(M);M->Lane=0;CireNPCCombat::ConfigureArchetype(M,Arch,5,0,1);M->MaxHealth=M->Health=100000;M->SetActorTickEnabled(false);
            Mode->Monsters.Add(M);AddedMonsters.Add(M);}
        return M;
    };
    auto* Hero=MakeHero(Stage,0,TEXT("wizard"));
    Check(Hero!=nullptr,TEXT("test champion spawned"));if(!Hero)return false;
    PC->Possess(Hero);
    auto Ready=[&](ACireHero* H,const TCHAR* Id){H->Skills={Id};H->Cooldowns={0};H->GlobalCooldown=0;H->Mana=5000;H->Energy=100;};
    // The probe runs inside one frame: nothing ticks the transient cues the fixture casts, so each case
    // purges the presentation actors it created (the real lifecycles are exercised explicitly below).
    TSet<TWeakObjectPtr<ACireSpellVisual>> Baseline;
    for(TActorIterator<ACireSpellVisual> It(World);It;++It)Baseline.Add(*It);
    auto PurgeNew=[&](){for(TActorIterator<ACireSpellVisual> It(World);It;++It)if(!Baseline.Contains(*It)&&!It->IsActorBeingDestroyed())It->Destroy();};
    auto VisualsNow=[&](){int32 N=0;for(TActorIterator<ACireSpellVisual> It(World);It;++It)if(!It->IsActorBeingDestroyed())++N;return N;};
    auto Pump=[&](AActor* Only,float Seconds,float Step=.05f){for(float T=0;T<Seconds;T+=Step)for(TActorIterator<ACireSpellVisual> It(World);It;++It)if(!It->IsActorBeingDestroyed()&&(!Only||*It==Only))It->Tick(Step);};

    // ---------------------------------------------------------------- 3. behavioural radius parity (hard-coded champion radii)
    {
        auto HitRing=[&](const TCHAR* Id,float Radius,bool bAroundTarget)
        {
            for(auto* M:AddedMonsters)if(IsValid(M))M->SetActorLocation(Stage+FVector(-3000,0,92));
            const FVector Center=bAroundTarget?Stage+FVector(700,0,0):Stage;
            ACireMonster* Target=bAroundTarget?MakeMonster(Center,TEXT("hollow_infantry")):nullptr;
            auto* In=MakeMonster(Center+FVector(0,Radius-25.f,0),TEXT("hollow_infantry"));
            auto* Out=MakeMonster(Center+FVector(0,-(Radius+25.f),0),TEXT("hollow_infantry"));
            if(!In||!Out)return;
            Ready(Hero,Id);Hero->Target=Target?Target:In;const float InBefore=In->Health,OutBefore=Out->Health;
            Hero->Cast(0);
            const FString Name(Id);
            if(FString(Id)==TEXT("war_cry"))Check(In->ForcedVictim==Hero&&Out->ForcedVictim!=Hero,Name+TEXT(" taunts inside its drawn radius only"));
            else Check(In->Health<InBefore&&Out->Health==OutBefore,Name+FString::Printf(TEXT(" hits inside its drawn %.0f cm radius only"),Radius));
            PurgeNew();
        };
        HitRing(TEXT("cleaving_strike"),CireAbilityShapes::CleaveRadius,false);
        HitRing(TEXT("war_cry"),CireAbilityShapes::WarCryRadius,false);
        HitRing(TEXT("cataclysm"),CireAbilityShapes::CataclysmRadius,true);
        HitRing(TEXT("chain_spark"),CireAbilityShapes::ChainRadius,true);
        auto HealRing=[&](const TCHAR* Id,float Radius)
        {
            auto* In=MakeHero(Stage+FVector(0,Radius-25.f,0),0,TEXT("knight"));auto* Out=MakeHero(Stage+FVector(0,-(Radius+25.f),0),0,TEXT("knight"));
            if(!In||!Out)return;In->Health=Out->Health=100;In->ShieldUntil=Out->ShieldUntil=0;
            Ready(Hero,Id);Hero->Target=nullptr;Hero->Cast(0);
            const bool bInside=In->Health>100||In->ShieldUntil>0,bOutside=Out->Health>100||Out->ShieldUntil>0;
            Check(bInside&&!bOutside,FString(Id)+FString::Printf(TEXT(" reaches allies inside its drawn %.0f cm radius only"),Radius));
            PurgeNew();In->SetActorLocation(Stage+FVector(-4000,0,92));Out->SetActorLocation(Stage+FVector(-4000,300,92));
        };
        HealRing(TEXT("sanctuary"),CireAbilityShapes::SanctuaryRadius);
        HealRing(TEXT("bastion_of_dawn"),CireAbilityShapes::BastionRadius);
        HealRing(TEXT("renewal"),CireAbilityShapes::RenewalRadius);
        for(auto* M:AddedMonsters)if(IsValid(M))M->SetActorLocation(Stage+FVector(-3000,0,92));
    }

    // ---------------------------------------------------------------- 4. player skillshot: warning lane + arrow, then release
    for(const TCHAR* Id:{TEXT("ember_lance"),TEXT("frost_bind"),TEXT("piercing_shot")})
    {
        const FString Name(Id);const auto Shape=CireAbilityShapes::Describe(FName(Id));
        auto Spec=*CireSkillTuning::FindSkillshot(Name);
        auto* Shot=ACireSkillshot::Spawn(Hero,Spec,Hero->GetActorLocation()+FVector(500,300,0),Name);
        Check(Shot!=nullptr,Name+TEXT(" skillshot spawned"));if(!Shot)continue;Actors.Add(Shot);
        ACireSpellVisual* V=nullptr;for(TActorIterator<ACireSpellVisual> It(World);It;++It)if(It->GetOwner()==Shot)V=*It;
        Check(V&&V->GetMode()==ACireSpellVisual::EMode::Projectile,Name+TEXT(" projectile presentation attached"));if(!V)continue;
        V->Tick(.1f);
        const FVector Dir=FVector(500,300,0).GetSafeNormal2D();float MaxAlong=0;
        Check(Near(V->TelegraphLength(),Shape.Length)&&Near(V->TelegraphWidth(),Shape.Width),Name+TEXT(" warning lane uses true width and travel length"));
        const FVector Ground(Shot->GetActorLocation().X,Shot->GetActorLocation().Y,Stage.Z);
        const bool bLane=LaneWithin(V,Ground,Dir,Shape.Length,Shape.Width,30.f,MaxAlong);
        Check(bLane&&MaxAlong>=Shape.Length-20.f,Name+FString::Printf(TEXT(" lane is drawn from the caster along the aim, full length, true width (within=%d reach=%.0f of %.0f)"),bLane?1:0,MaxAlong,Shape.Length));
        Check(V->GroundArrowTip().X>=Shape.Length-12.f&&V->GroundChevrons()>=1,Name+TEXT(" lane has an arrowhead and travel chevrons"));
        Check(!V->IsHostileTelegraph(),Name+TEXT(" own skillshot is drawn in its school colour, not amber"));
        Shot->Tick(Spec.WarningSeconds+.02f);V->Tick(.02f);V->Tick(.4f);
        Check(Shot->bReleased,Name+TEXT(" released after its warning"));
        Shot->Destroy();V->Tick(.02f);
        Check(!IsValid(V)||V->IsActorBeingDestroyed(),Name+TEXT(" projectile presentation cleans up with its projectile"));
        PurgeNew();
    }

    // ---------------------------------------------------------------- 5. monster lanes/cones/circles: amber warnings of the true area
    {
        auto* Victim=Hero;
        int32 Areas=0,LaneCues=0;
        struct FCase{FName Arch;FName Ability;};
        TArray<FCase> Cases;
        for(const auto& Pair:CireNPCArchetypes::Get().Archetypes)for(const auto& A:Pair.Value.Abilities)
            if(A.Kind==ECireNPCAbilityKind::Cone||A.Kind==ECireNPCAbilityKind::TargetCircle||A.Kind==ECireNPCAbilityKind::SelfCircle||
               A.Kind==ECireNPCAbilityKind::Charge||A.Kind==ECireNPCAbilityKind::Pull||(A.Kind==ECireNPCAbilityKind::Projectile&&CireSkillTuning::FindSkillshot(A.Skillshot)))
                Cases.Add({Pair.Key,A.Id});
        Cases.Sort([](const FCase& A,const FCase& B){return A.Ability.LexicalLess(B.Ability);});
        for(const FCase& Case:Cases)
        {
            const auto* Arch=CireNPCArchetypes::Find(Case.Arch);const auto* A=Arch?Arch->FindAbility(Case.Ability):nullptr;if(!A)continue;
            const float Distance=FMath::Clamp(FMath::Min(A->Range*.7f,700.f),FMath::Max(A->MinRange+60.f,150.f),FMath::Max(A->Range-15.f,160.f));
            auto* M=MakeMonster(Stage+FVector(Distance,0,0),Case.Arch);if(!M)continue;
            const auto Shape=CireAbilityShapes::DescribeMonster(*A,Arch);const FString Name=Case.Ability.ToString();
            const bool bStarted=CireNPCCombat::DebugStartAbility(M,Case.Ability,Victim);
            Check(bStarted,Name+TEXT(" starts in the test fixture"));
            if(!bStarted){Mode->Monsters.Remove(M);M->Destroy();continue;}
            if(A->Kind==ECireNPCAbilityKind::Projectile)
            {
                // The cast cue draws the amber lane on the client for the whole cast bar.
                auto* V=CireSpellPresentation::Play(World,Case.Ability,M->GetActorLocation(),Victim->GetActorLocation(),ECireSpellCue::Cast,1,false);
                Check(V&&V->GetMode()==ACireSpellVisual::EMode::Lane&&V->IsHostileTelegraph()&&Near(V->TelegraphLength(),Shape.Length)&&Near(V->TelegraphWidth(),Shape.Width),
                    Name+TEXT(" enemy projectile shows an amber lane of its true corridor"));
                if(V)
                {
                    V->Tick(.1f);float MaxAlong=0;const FVector Dir=(Victim->GetActorLocation()-M->GetActorLocation()).GetSafeNormal2D();
                    const FVector Ground(M->GetActorLocation().X,M->GetActorLocation().Y,Stage.Z);
                    Check(LaneWithin(V,Ground,Dir,Shape.Length,Shape.Width,30.f,MaxAlong)&&MaxAlong>=Shape.Length-20.f,Name+TEXT(" enemy lane drawn from the caster toward the aim"));
                    ++LaneCues;
                    // Interrupt: the telegraph fades out and is destroyed.
                    CireNPCCombat::Interrupt(M);Pump(V,.6f);
                    Check(!IsValid(V)||V->IsActorBeingDestroyed(),Name+TEXT(" interrupted cast removes its lane"));
                }
            }
            else
            {
                ACireAreaEffect* Area=nullptr;for(TActorIterator<ACireAreaEffect> It(World);It;++It)if(It->GetOwner()==M&&!It->IsActorBeingDestroyed())Area=*It;
                Check(Area&&!Area->IsActive(),Name+TEXT(" spawns a harmless warning area"));
                if(Area)
                {
                    ++Areas;const auto& Spec=Area->AreaSpec;
                    bool bMatch=false;
                    switch(Shape.Kind)
                    {
                    case ECireHitShape::Cone:bMatch=Spec.Shape==ECireAreaShape::Cone&&Near(Spec.Radius,Shape.Radius)&&Near(Spec.ConeAngleDegrees,FMath::Clamp(Shape.Angle,1.f,179.f));break;
                    case ECireHitShape::Circle:bMatch=Spec.Shape==ECireAreaShape::Circle&&Near(Spec.Radius,Shape.Radius);break;
                    case ECireHitShape::Line:bMatch=Spec.Shape==ECireAreaShape::Line&&Near(Spec.Width,Shape.Width)&&Spec.Length<=Shape.Length+.5f;break;
                    default:break;
                    }
                    Check(bMatch,Name+TEXT(" warning area is the ability's true shape"));
                    ACireSpellVisual* V=CireSpellPresentation::FollowArea(Area);
                    if(V){V->Tick(.05f);Check(V->GetMode()==ACireSpellVisual::EMode::AreaFollow&&V->IsHostileTelegraph()&&V->GroundVertexCount()>0&&Area->bPresentationOwnsGround,
                        Name+TEXT(" amber telegraph painted over the true area"));
                        if(Shape.Kind==ECireHitShape::Line)Check(Near(V->GroundFillBounds().Max.X,Spec.Length)&&Near(V->GroundFillBounds().Max.Y,Spec.Width*.5f)&&V->GroundArrowTip().X>0,
                            Name+TEXT(" line warning has arrowhead within its length"));}
                    else Check(false,Name+TEXT(" area presentation"));
                    // Lifecycle: area destroyed -> telegraph fades out, then is removed; the area's own ground is handed back.
                    Area->Destroy();if(V){Pump(V,.45f);Check(!IsValid(V)||V->IsActorBeingDestroyed(),Name+TEXT(" telegraph cleaned up after its area"));}
                }
                CireNPCCombat::Interrupt(M);
            }
            Mode->Monsters.Remove(M);AddedMonsters.Remove(M);M->Destroy();PurgeNew();
        }
        Check(Areas>=150&&LaneCues>=2,FString::Printf(TEXT("monster telegraphs exercised (%d areas, %d projectile lanes)"),Areas,LaneCues));
    }

    // ---------------------------------------------------------------- 6. cast/animation release sync
    for(const FName Id:Champions)
    {
        const FString Name=Id.ToString();if(ACireHero::IsPassive(Name)||Name.StartsWith(TEXT("basic_")))continue;
        const float Windup=CireChampionActions::SkillWindup(World,Name);
        const float CastDelay=CireSpellPresentation::ReleaseDelay(World,Id,ECireSpellCue::Cast,Hero->GetActorLocation());
        Check(Near(Windup,CastDelay,.001f),Name+FString::Printf(TEXT(" clip contact (%.2fs) and cast cue (%.2fs) release together"),Windup,CastDelay));
        if(const auto* P=CireSkillTuning::FindSkillshot(Name);P&&CireAbilityShapes::Describe(Id).bProjectile)
            Check(Near(Windup,P->WarningSeconds,.001f),Name+TEXT(" clip contact lands on the projectile release"));
        const auto Shape=CireAbilityShapes::Describe(Id);
        const float ImpactDelay=CireSpellPresentation::ReleaseDelay(World,Id,ECireSpellCue::Impact,Hero->GetActorLocation());
        if(!Shape.bProjectile&&Shape.WarningSeconds<=0)Check(Near(ImpactDelay,Windup,.001f),Name+TEXT(" instant impact waits for the contact frame"));
        else Check(ImpactDelay==0,Name+TEXT(" travelling/warned impacts are never delayed"));
    }
    Check(CireSpellPresentation::ReleaseDelay(World,TEXT("cleaving_strike"),ECireSpellCue::Cast,Stage+FVector(0,5000,0))==0,TEXT("cues not issued by a champion are never delayed"));
    {
        auto* V=CireSpellPresentation::Play(World,TEXT("cleaving_strike"),Hero->GetActorLocation(),Hero->GetActorLocation(),ECireSpellCue::Cast,1,false);
        Check(V&&V->IsHidden()&&Near(V->GetStartDelay(),InstantLead,.001f),TEXT("delayed cue is hidden until the release frame"));
        if(V){V->Tick(InstantLead+.02f);Check(!V->IsHidden(),TEXT("delayed cue appears on the release frame"));Pump(V,1.4f);Check(!IsValid(V)||V->IsActorBeingDestroyed(),TEXT("delayed cue ends"));}
    }

    // ---------------------------------------------------------------- 7. modes, anchoring, budgets
    PurgeNew();
    {
        const int32 Before=VisualsNow();
        auto* Self=CireSpellPresentation::Play(World,TEXT("iron_guard"),Hero->GetActorLocation(),Hero->GetActorLocation()+FVector(700,0,0),ECireSpellCue::Cast,1,false);
        Check(Self&&FVector::Dist2D(Self->GetActorLocation(),Hero->GetActorLocation())<1,TEXT("self buff visual stays on the caster even with an enemy selected"));
        auto* Shock=CireSpellPresentation::Play(World,TEXT("war_cry"),Hero->GetActorLocation(),Hero->GetActorLocation(),ECireSpellCue::Cast,1,false);
        Check(Shock&&Shock->GetMode()==ACireSpellVisual::EMode::SelfShock,TEXT("self circle shows an expanding ring to its true radius"));
        auto* Flare=CireSpellPresentation::Play(World,TEXT("ember_lance"),Hero->GetActorLocation(),Hero->GetActorLocation()+FVector(600,0,0),ECireSpellCue::Cast,1,false);
        Check(Flare&&Flare->GetMode()==ACireSpellVisual::EMode::CasterFlare&&FVector::Dist2D(Flare->GetActorLocation(),Hero->GetActorLocation())<1,TEXT("skillshot cast shows a caster flare, never a point marker at the aim"));
        auto* Chain=CireSpellPresentation::Play(World,TEXT("chain_spark"),Hero->GetActorLocation(),Hero->GetActorLocation()+FVector(600,0,0),ECireSpellCue::Cast,1,false);
        Check(Chain&&Chain->GetMode()==ACireSpellVisual::EMode::Chain,TEXT("chain spark first hop is a bolt from caster to target"));
        auto* Hit=CireSpellPresentation::Play(World,TEXT("drowned_rend"),Hero->GetActorLocation()+FVector(300,0,0),Hero->GetActorLocation(),ECireSpellCue::Impact,1,false);
        Check(Hit&&Hit->GetMode()==ACireSpellVisual::EMode::Impact,TEXT("impact burst mode"));
        TArray<ACireSpellVisual*> Many;
        const TCHAR* Ids[]={TEXT("cataclysm"),TEXT("frost_bind"),TEXT("venom_ground"),TEXT("void_rift"),TEXT("drowned_rend"),TEXT("starfall"),TEXT("renewal"),TEXT("blight_rot_pool")};
        for(int32 J=0;J<40;++J)if(auto* V=CireSpellPresentation::Play(World,Ids[J%8],Hero->GetActorLocation()+FVector(J*20,0,0),Hero->GetActorLocation()+FVector(J*20+300,0,0),J%2?ECireSpellCue::Impact:ECireSpellCue::Cast,1.4f,false))Many.Add(V);
        Pump(nullptr,.3f);
        int32 Lights=0;bool bBudget=true;
        for(TActorIterator<ACireSpellVisual> It(World);It;++It)if(!It->IsActorBeingDestroyed())
        {
            Lights+=It->LightGranted()&&It->Light->IsVisible();
            bBudget&=It->VertexCount()<=8192&&It->GroundVertexCount()<=CireSpellMesh::MaxGroundVertices&&(It->VertexCount()==0||It->GeometryValid());
        }
        Check(bBudget,TEXT("every effect stays within its vertex budgets"));
        Check(Lights<=8,FString::Printf(TEXT("transient spell lights capped (%d)"),Lights));
        Check(VisualsNow()<=64,TEXT("presentation actor cap respected"));
        Pump(nullptr,2.5f);
        Check(VisualsNow()<=Before,FString::Printf(TEXT("all transient cues cleaned up (%d -> %d)"),Before,VisualsNow()));
    }
    // ---------------------------------------------------------------- 8. themed rune telegraphs
    {
        PurgeNew();
        TArray<FVector> V;TArray<int32> I;TArray<FLinearColor> C;
        // Every school has its own glyph geometry (no two sets draw the same strokes).
        TMap<uint32,ERuneSet> Signatures;
        for(int32 J=0;J<static_cast<int32>(ERuneSet::Count);++J)
        {
            FCireGroundMesh G(V,I,C);PaintGlyph(G,static_cast<ERuneSet>(J),FVector2D::ZeroVector,40.f,0.f,FLinearColor::White,0.f,1);
            uint32 Hash=GetTypeHash(G.V.Num());for(int32 K=0;K<G.V.Num();K+=7)Hash=HashCombine(Hash,GetTypeHash(FIntPoint(FMath::RoundToInt(G.V[K].X),FMath::RoundToInt(G.V[K].Y))));
            Check(G.V.Num()>0&&!Signatures.Contains(Hash),RuneSetName(static_cast<ERuneSet>(J))+TEXT(" rune set has its own glyph"));Signatures.Add(Hash,static_cast<ERuneSet>(J));
        }
        // Every ability resolves a rune set; heals use the heal set; buffs are calm, damage sharp.
        int32 Resolved=0,Heals=0;TSet<ERuneSet> Used;
        auto Resolve=[&](const FCireHitShape& Shape,const FString& Name)
        {
            if(Shape.Kind==ECireHitShape::None)return;
            const auto Theme=ThemeFor(Shape);++Resolved;Used.Add(Theme.Set);
            Check(Theme.Set!=ERuneSet::Count&&RuneSetName(Theme.Set)!=TEXT("none"),Name+TEXT(" resolves a school rune set"));
            if(Shape.bHeal)
            {
                ++Heals;const FLinearColor G=Theme.Glyph;
                Check(Theme.Set==ERuneSet::Heal&&!Theme.bSharp&&G.G>G.R&&G.G>G.B,Name+TEXT(" heal telegraph uses the green/gold cross rune style, calm"));
            }
            else Check(Theme.Set!=ERuneSet::Heal,Name+TEXT(" non-heal never borrows the heal style"));
            if(Shape.bBuff)Check(!Theme.bSharp,Name+TEXT(" buff zone is calm"));
            if(Shape.bHostileOnly&&!Shape.bHeal)Check(Theme.bSharp,Name+TEXT(" damage zone is sharp"));
        };
        for(const FName Id:Champions)Resolve(CireAbilityShapes::Describe(Id),Id.ToString());
        for(const auto& Pair:CireNPCArchetypes::Get().Archetypes)for(const auto& A:Pair.Value.Abilities)Resolve(CireAbilityShapes::DescribeMonster(A,&Pair.Value),A.Id.ToString());
        Check(Heals>=10&&Resolved>=330&&Used.Num()>=12,FString::Printf(TEXT("rune sets resolved for %d abilities (%d heals, %d distinct sets)"),Resolved,Heals,Used.Num()));
        for(const TCHAR* Id:{TEXT("restoring_light"),TEXT("sanctuary"),TEXT("renewal"),TEXT("purify"),TEXT("wellspring"),TEXT("second_wind")})
            Check(CireAbilityShapes::Describe(FName(Id)).bHeal,FString(Id)+TEXT(" is a heal"));
        Check(RuneSetFor(CireAbilityShapes::Describe(TEXT("ember_lance")))==ERuneSet::Fire&&RuneSetFor(CireAbilityShapes::Describe(TEXT("frost_bind")))==ERuneSet::Frost&&
            RuneSetFor(CireAbilityShapes::Describe(TEXT("cleaving_strike")))==ERuneSet::Physical&&RuneSetFor(CireAbilityShapes::Describe(TEXT("drowned_rend")))==ERuneSet::Tide&&
            RuneSetFor(CireAbilityShapes::Describe(TEXT("seismic_reprisal")))==ERuneSet::Earth,TEXT("fire/cold/physical/water/earth abilities get their own rune sets"));
        // Runes stay inside every true boundary (edge motifs point inward, glyph bands are inset).
        for(ECireAreaShape Kind:{ECireAreaShape::Circle,ECireAreaShape::Cone,ECireAreaShape::Line,ECireAreaShape::Square})
            for(int32 J=0;J<static_cast<int32>(ERuneSet::Count);++J)
            {
                FCireAreaSpec Spec;Spec.Shape=Kind;Spec.Radius=300;Spec.Width=Kind==ECireAreaShape::Line?160:400;Spec.Length=900;Spec.ConeAngleDegrees=90;
                FCireGroundMesh G(V,I,C);FRuneTheme T;T.Set=static_cast<ERuneSet>(J);T.Glyph=RuneColor(T.Set);T.Edge=T.Glyph;
                const auto R=PaintRunes(G,Spec,T,1.3f,1.f,true);
                int32 Outside=0;
                for(const FVector& P:G.V)
                {
                    // Feathered stroke edges may reach 8 cm past the core line; everything else is inside.
                    FCireAreaSpec Grown=Spec;Grown.Radius+=8;Grown.Width+=16;Grown.Length+=8;
                    const FVector2D Q(P.X,P.Y);
                    const bool bIn=ACireAreaEffect::ContainsPoint(Grown,FVector::ZeroVector,FRotator::ZeroRotator,FVector(Q.X,Q.Y,0))||
                        (Kind==ECireAreaShape::Line&&Q.X>=-8.f&&Q.X<=Spec.Length+8.f&&FMath::Abs(Q.Y)<=Spec.Width*.5f+8.f)||Q.Size()<30.f;
                    Outside+=!bIn;
                }
                Check(R.Glyphs>0&&R.EdgeMotifs>0&&Outside==0&&G.V.Num()<=CireSpellMesh::MaxGroundVertices,
                    FString::Printf(TEXT("%s runes stay inside shape %d (%d outside, %d verts)"),*RuneSetName(T.Set),static_cast<int32>(Kind),Outside,G.V.Num()));
            }
        // Enemy warnings keep amber urgency with the school runes inside.
        {
            const auto Hostile=ThemedStyle(ETone::Hostile,CireAbilityShapes::Describe(TEXT("drakkari_ember_breath")));
            Check(Hostile.bRunes&&Hostile.Runes.Set==ERuneSet::Fire&&Hostile.Edge.R>Hostile.Edge.B*4.f,TEXT("enemy warning: amber edge, fire runes inside"));
            const auto Aim=ThemedStyle(ETone::AimValid,CireAbilityShapes::Describe(TEXT("venom_ground")));
            Check(Aim.bRunes&&Aim.Runes.Set==ERuneSet::Poison,TEXT("aim preview carries the poison rune set"));
        }
        // Heal cast in the world: the heal visual uses the heal theme.
        if(auto* Heal=CireSpellPresentation::Play(World,TEXT("sanctuary"),Hero->GetActorLocation(),Hero->GetActorLocation(),ECireSpellCue::Cast,1,false))
        {
            Heal->Tick(InstantLead+.3f);
            Check(Heal->GetShape().bHeal&&ThemeFor(Heal->GetShape()).Set==ERuneSet::Heal&&Heal->GroundVertexCount()>0,TEXT("sanctuary cast paints heal runes"));
        }
        PurgeNew();
    }

    // ---------------------------------------------------------------- 9. void zones (teleport / portal)
    {
        const auto Stub=CireAbilityShapes::Describe(TEXT("shadow_step"));
        Check(Stub.HasVoidZone(),TEXT("teleport skill carries a void zone (stub until the Ability Database)"));
        TArray<FVector> V;TArray<int32> I;TArray<FLinearColor> C;
        for(ETone Tone:{ETone::AimValid,ETone::Hostile,ETone::Friendly})
        {
            FCireGroundMesh G(V,I,C);const auto R=PaintVoidZone(G,FVector2D::ZeroVector,300.f,120.f,Tone,.7f,1.f,false);
            float MaxR=0;int32 NearInner=0,NearOuter=0;
            for(const FVector& P:G.V){const float D=FVector2D(P.X,P.Y).Size();MaxR=FMath::Max(MaxR,D);NearInner+=FMath::Abs(D-120.f)<4.f;NearOuter+=FMath::Abs(D-300.f)<4.f;}
            Check(Near(R.Outer,300)&&Near(R.Inner,120)&&MaxR<=300.f+10.f&&NearInner>20&&NearOuter>20&&R.SlowIcons>0&&R.StunIcons>0,
                FString::Printf(TEXT("void zone draws both radii with slow/stun icons (tone %d, max %.0f)"),static_cast<int32>(Tone),MaxR));
        }
        // Preview: a ground-aimed teleport armed by the cursor shows both zones at the aimed spot.
        CireAbilityShapes::DebugUseDatabase(TEXT("{\"abilities\":[{\"id\":\"starfall\",\"school\":\"void\",\"voidZone\":{\"outerRadius\":320,\"innerRadius\":130,\"duration\":2}},{\"id\":\"cleaving_strike\",\"school\":\"fire\"}]}"));
        Check(CireAbilityShapes::SchoolFor(TEXT("cleaving_strike"))==ECireSchool::Fire&&CireAbilityShapes::Describe(TEXT("starfall")).VoidOuter==320.f,TEXT("Ability Database school and void zone override the built-in mapping"));
        Ready(Hero,TEXT("starfall"));
        const FVector Aim=Hero->GetActorLocation()+FVector(500,0,-92);
        CireTargeting::DebugSetAimOverride(Aim);CireTargeting::Request(PC,0);CireTargeting::Tick(PC);
        const FVector Preview=CireTargeting::DebugPreviewVoid(PC);
        Check(Near(Preview.X,320)&&Near(Preview.Y,130)&&Preview.Z>0,FString::Printf(TEXT("aim preview renders both void radii (%.0f / %.0f)"),Preview.X,Preview.Y));
        CireTargeting::DebugSetAimOverride({});CireTargeting::Cancel(PC);
        if(auto* Own=CireSpellPresentation::Play(World,TEXT("starfall"),Hero->GetActorLocation(),Aim,ECireSpellCue::Cast,1,false))
        {
            for(int32 J=0;J<6;++J)Own->Tick(.1f); // Tick clamps each step to 0.25 s
            Check(Own->GetMode()==ACireSpellVisual::EMode::VoidZone&&Near(Own->VoidRadiiDrawn().X,320)&&Near(Own->VoidRadiiDrawn().Y,130)&&!Own->IsHostileTelegraph()&&
                FVector::Dist2D(Own->GetActorLocation(),Aim)<1,TEXT("void zone cast renders both radii at the portal"));
            Pump(Own,3.f);Check(!IsValid(Own)||Own->IsActorBeingDestroyed(),TEXT("void zone cleans up"));
        }
        CireAbilityShapes::ReloadDatabase();
        if(auto* M=MakeMonster(Stage+FVector(400,0,0),TEXT("rift_stalker")))
        {
            auto* Warn=CireSpellPresentation::Play(World,TEXT("void_blink"),M->GetActorLocation(),M->GetActorLocation()+FVector(600,0,0),ECireSpellCue::Cast,.8f,false);
            if(Warn)
            {
                for(int32 J=0;J<4;++J)Warn->Tick(.1f);const auto Shape=CireAbilityShapes::Describe(TEXT("void_blink"));
                Check(Warn->GetMode()==ACireSpellVisual::EMode::VoidZone&&Warn->IsHostileTelegraph()&&Near(Warn->VoidRadiiDrawn().X,Shape.VoidOuter)&&Near(Warn->VoidRadiiDrawn().Y,Shape.VoidInner)&&Warn->VoidIconsDrawn()>0,
                    TEXT("monster void zone shows as an amber warning with both radii"));
            }
            else Check(false,TEXT("monster void zone cue"));
            Mode->Monsters.Remove(M);AddedMonsters.Remove(M);M->Destroy();
        }
        PurgeNew();
    }
    UE_LOG(LogCireAbilityVFX,Display,TEXT("CIRE_ABILITY_VFX_TESTS_%s checks=%d failed=%d"),S.Failed==0?TEXT("PASS"):TEXT("FAIL"),S.Checks,S.Failed);
    return S.Failed==0;
}
#endif
