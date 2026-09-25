// ability-vfx: shape-true presentation modes for ACireSpellVisual (telegraph lanes, area warnings with
// progress, detonations, projectile heads/trails, school impacts, caster flares, self shockwaves).
// Local-only cosmetics: nothing here decides a hit, moves a unit or replicates.
#include "NiagaraComponent.h" // fab-integration
#include "CireSpellPresentation.h"
#include "CireSpellMesh.h"
#include "CireAbilityVFX.h"
#include "CireAbilityShapes.h"
#include "CireAreaEffects.h"
#include "CireGame.h"
#include "CireNPCArchetypes.h"
#include "CireNPCState.h"
#include "CireSkillshot.h"
#include "CireAbilityDB.h"
#include "CirePylonField.h"
#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "ProceduralMeshComponent.h"

using namespace CireSpellMesh;
using EMode = ACireSpellVisual::EMode;

namespace
{
FString NormId(FName Id){FString S=Id.ToString().ToLower();S.ReplaceInline(TEXT(" "),TEXT("_"));S.ReplaceInline(TEXT("'"),TEXT(""));return S;}
// The unit that issued a cue: cast cues start at the caster's actor location.
AActor* SourceAt(UWorld* World,FVector From)
{
    if(!World||From.ContainsNaN())return nullptr;
    AActor* Best=nullptr;double BestD=FMath::Square(70.0);
    for(TActorIterator<ACharacter> It(World);It;++It)
    {
        if(!Cast<ACireHero>(*It)&&!Cast<ACireMonster>(*It))continue;
        if(FMath::Abs(It->GetActorLocation().Z-From.Z)>160)continue;
        const double D=FVector::DistSquared2D(It->GetActorLocation(),From);if(D<BestD){BestD=D;Best=*It;}
    }
    return Best;
}
ACireHero* LocalHero(UWorld* World)
{
    const auto* PC=World?World->GetFirstPlayerController():nullptr;return PC?Cast<ACireHero>(PC->GetPawn()):nullptr;
}
bool HostileToLocal(UWorld* World,const AActor* Source)
{
    if(Cast<ACireMonster>(Source))return true;
    const auto* Hero=Cast<ACireHero>(Source);const auto* Local=LocalHero(World);
    return Hero&&Local&&Hero!=Local&&Hero->TeamId>=0&&Local->TeamId>=0&&Hero->TeamId!=Local->TeamId;
}
double ServerNow(UWorld* World)
{
    const AGameStateBase* State=World?World->GetGameState():nullptr;return State?State->GetServerWorldTimeSeconds():World?World->GetTimeSeconds():0.0;
}
const FCireNPCArchetype* ArchetypeOf(const AActor* Actor)
{
    const auto* M=Cast<ACireMonster>(Actor);return M&&M->NPCState?M->NPCState->Archetype():nullptr;
}
FLinearColor Boost(FLinearColor C,float Scale){C.R*=Scale;C.G*=Scale;C.B*=Scale;return C;}
float Ease(float U){U=FMath::Clamp(U,0.f,1.f);return 1-FMath::Square(1-U);}
constexpr float Gravity=-980.f;
}

void ACireSpellVisual::SetStartDelay(float Seconds)
{
    if(!FMath::IsFinite(Seconds)||Seconds<=.001f||bPreview||bFollowArea||bFollowActor)return;
    StartDelay=FMath::Min(Seconds,2.f);Age=-StartDelay; // Duration still counts from the release frame
    SetActorHiddenInGame(true);Light->SetVisibility(false);
    if(Audio->IsPlaying()){Audio->Stop();bSoundPending=true;}
}

void ACireSpellVisual::ProbeGround()
{
    bGroundProbed=true;GroundZ=-88.f;
    if(bFollowArea){GroundZ=0;return;}
    FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(CireSpellGround),false,this);
    if(CastSource.IsValid())Q.AddIgnoredActor(CastSource.Get());
    const FVector At=GetActorLocation();
    if(GetWorld()->LineTraceSingleByObjectType(Hit,At+FVector(0,0,40),At-FVector(0,0,700),FCollisionObjectQueryParams(ECC_WorldStatic),Q))
        GroundZ=static_cast<float>(Hit.ImpactPoint.Z-At.Z);
}

void ACireSpellVisual::ClassifyCue()
{
    Mode=EMode::Legacy;bHostile=false;CastSource=nullptr;FadeOutAt=-1;ReleasedAge=-1;AreaActiveAge=-1;bGroundProbed=false;LaneLength=LaneWidth=0;
    if(!CireAbilityVFX::Enabled())return; // A/B: previous presentation
    if(bFollowArea){Mode=EMode::AreaFollow;return;}
    if(bFollowActor){if(Cue==ECireSpellCue::Projectile)Mode=EMode::Projectile;return;}
    if(bPreview)return;
    AActor* Source=SourceAt(GetWorld(),Start);CastSource=Source;
    const FCireNPCArchetype* Arch=ArchetypeOf(Source);
    Shape=CireAbilityShapes::Describe(Skill,Arch);
    const bool bMonster=Cast<ACireMonster>(Source)!=nullptr||(Source==nullptr&&CireAbilityShapes::FindOwner(Skill)!=nullptr);
    bHostile=bMonster||HostileToLocal(GetWorld(),Source);
    if(Arch||bMonster){Family=static_cast<int32>(Shape.School);}
    const FVector Aim=(End-Start).GetSafeNormal2D();
    auto Anchor=[&]{SetActorLocation(Start);SetActorRotation(Aim.IsNearlyZero()?FRotator::ZeroRotator:Aim.Rotation());};
    if(Cue==ECireSpellCue::Impact&&NormId(Skill)==TEXT("void_rift"))
    {
        // CireCrowdControl::VoidBurst: the rift opens exactly here. The cue carries the outer radius (scale x 250);
        // the inner radius comes from the ability that owns that outer radius in the Ability Database.
        Mode=EMode::VoidZone;const float Outer=FMath::Max(60.f,Size*250.f);
        Shape=FCireHitShape();Shape.Id=Skill;Shape.School=ECireSchool::Void;Shape.VoidOuter=Outer;Shape.VoidInner=Outer*.43f;Shape.VoidSeconds=1.6f;
        float Best=1e9f;
        for(const FCireAbilityDef& D:CireAbilityDB::All())
            if(D.Void.bValid&&FMath::Abs(D.Void.OuterRadius-Outer)<Best)
            {Best=FMath::Abs(D.Void.OuterRadius-Outer);Shape.VoidInner=D.Void.InnerRadius*Outer/FMath::Max(1.f,D.Void.OuterRadius);
             Shape.VoidSeconds=FMath::Max(D.Void.InnerDuration,D.Void.OuterDuration);Shape.bVoidHeal=D.Void.SelfHealMaxHealthFraction>0;}
        Duration=FMath::Clamp(Shape.VoidSeconds,.8f,3.f)+.3f;Size=1.f;
        // The rift's caster is the champion that just landed here (its victims stand close by too).
        ACireHero* Caster=nullptr;double Best2=FMath::Square(320.0);
        for(TActorIterator<ACireHero> It(GetWorld());It;++It){const double D=FVector::DistSquared2D(It->GetActorLocation(),Start);if(D<Best2&&!It->bDead){Best2=D;Caster=*It;}}
        bHostile=Caster?HostileToLocal(GetWorld(),Caster):false;
        SetActorRotation(FRotator::ZeroRotator);return;
    }
    if(Cue==ECireSpellCue::Impact||Cue==ECireSpellCue::Critical)
    {
        Mode=Cue==ECireSpellCue::Impact?EMode::Impact:EMode::Legacy;bChainHop=false;
        // Chain Spark: each further victim's impact arcs from the previous victim (same cast, same instant).
        if(NormId(Skill)==TEXT("chain_spark"))
        {
            ACireSpellVisual* Previous=nullptr;
            for(TActorIterator<ACireSpellVisual> It(GetWorld());It;++It)
                if(*It!=this&&!It->IsActorBeingDestroyed()&&It->Skill==Skill&&(It->Cue==ECireSpellCue::Impact||It->Cue==ECireSpellCue::Critical)&&It->Age<.15f&&
                   (!Previous||It->GetUniqueID()>Previous->GetUniqueID()))Previous=*It;
            if(Previous&&FVector::Dist2D(Previous->End,End)>20){bChainHop=true;HopFrom=Previous->End;Mode=EMode::Impact;}
        }
        return;
    }
    if(Cue!=ECireSpellCue::Cast)return;
    const bool bSelfCircle=Shape.Kind==ECireHitShape::Circle&&Shape.bFromCaster;
    if(const auto* Hero=Cast<ACireHero>(Source);Hero&&!Hero->CastSkill.IsNone()&&NormId(Hero->CastSkill)==NormId(Skill)&&FMath::IsNearlyEqual(Size,.7f,.05f))
    {
        // Timed cast start (CireCrowdControl::GateCast): the telegraph reads during the whole cast bar.
        Mode=EMode::Channel;Duration=FMath::Max(.3f,Hero->CastEndTime-Hero->CastStartTime)+.25f;Size=1.f;Anchor();
        if(!(Shape.Kind==ECireHitShape::Circle&&Shape.bFromCaster))
            if(AActor* Target=Hero->Target;IsValid(Target)&&Shape.Kind==ECireHitShape::Unit)
            {
                const auto* Ally=Cast<ACireHero>(Target);
                if(!Shape.bHeal||(Ally&&Ally->TeamId==Hero->TeamId))SetActorLocation(Target->GetActorLocation());
            }
        return;
    }
    // Void zones on a cast: ground-aimed portals and monster teleports. Targeted champion rifts (Shadow Step)
    // arrive as the authoritative "void_rift" impact at their true destination instead.
    if(Shape.HasVoidZone()&&!(Cast<ACireHero>(Source)&&Shape.Kind==ECireHitShape::Unit))
    {
        // Teleport / portal: the void zone (outer slow ring, inner stun circle) appears where the portal opens.
        Mode=EMode::VoidZone;Duration=FMath::Max(Shape.VoidSeconds,.6f)+.3f;
        SetActorLocation(Shape.bVoidAtOrigin?Start:End);SetActorRotation(FRotator::ZeroRotator);
        return;
    }
    if(Shape.bProjectile&&bMonster)
    {
        // Enemy skillshot: amber lane with the projectile's true corridor for the whole cast bar.
        Mode=EMode::Lane;LaneLength=Shape.Length;LaneWidth=Shape.Width;Duration=FMath::Max(Shape.WarningSeconds,.3f)+.18f;Anchor();
    }
    else if(Shape.bProjectile||Shape.Kind==ECireHitShape::Line||Shape.Kind==ECireHitShape::Cone){Mode=EMode::CasterFlare;Duration=.55f;Anchor();}
    else if(Shape.Kind==ECireHitShape::Chain){Mode=EMode::Chain;Duration=.62f;}
    else if(bMonster){Mode=EMode::Gather;Duration=FMath::Max(Shape.WarningSeconds,.45f)+.12f;Anchor();}
    else if(bSelfCircle){Mode=Shape.WarningSeconds>0?EMode::CasterFlare:EMode::SelfShock;Duration=Mode==EMode::SelfShock?FMath::Max(Duration,1.f):.55f;Anchor();}
    else if(Shape.Kind==ECireHitShape::Self){Mode=EMode::SelfShock;Shape.Radius=120.f;Anchor();} // personal pulse on the caster, never on a selected enemy
    else if(Shape.Kind==ECireHitShape::Circle&&!Shape.bGroundAim){Mode=EMode::TargetMark;}
    else if(Shape.Kind==ECireHitShape::Unit)
    {
        // Ally spells whose selection is an enemy fall back to the caster (the gameplay target), so the
        // heal never appears on the hostile unit that happened to be selected.
        AActor* Target=SourceAt(GetWorld(),End);
        const auto* Hero=Cast<ACireHero>(Source);const auto* TargetHero=Cast<ACireHero>(Target);
        if(!Shape.bHostileOnly&&Hero&&!(TargetHero&&TargetHero->TeamId==Hero->TeamId)){End=Start;SetActorLocation(Start);}
        Mode=EMode::TargetMark;
    }
}

bool ACireSpellVisual::TickModes(float DeltaSeconds)
{
    if(bPreview)return false;
    if(Age<0)
    {
        SetActorHiddenInGame(true);Light->SetVisibility(false);return true;
    }
    if(StartDelay>0&&!bFollowArea&&!bFollowActor&&IsHidden())SetActorHiddenInGame(false);
    if(bSoundPending){bSoundPending=false;StartSound();}
    if(Mode==EMode::AreaFollow)
    {
        ACireAreaEffect* Area=FollowedArea.Get();
        if(!IsValid(Area)||Area->IsActorBeingDestroyed())
        {
            // The zone ended: dissolve over 0.3 s instead of popping out.
            if(FadeOutAt<0){FadeOutAt=Age;if(CachedArea.AbilityName.IsEmpty())return false;}
            if(Age-FadeOutAt>.3f||IsHidden()){Destroy();return true;}
            Rebuild();return true;
        }
        CachedArea=Area->AreaSpec;Area->bPresentationOwnsGround=true;bAreaPersistent=Area->AreaSpec.bPersistent;
        // balance: pylon fields share one fill budget with the pylon fields they overlap (re-counted 4x a second).
        if(PylonCheckedAt<0||Age-PylonCheckedAt>.25f){PylonCheckedAt=Age;bPylonField=CirePylonField::IsPylonField(Area);PylonOverlaps=bPylonField?CirePylonField::CountOverlaps(Area):1;}
        bHostile=Cast<ACireMonster>(Area->GetOwner())!=nullptr||HostileToLocal(GetWorld(),Area->GetOwner());
        if(Area->IsActive()&&AreaActiveAge<0)AreaActiveAge=Age;
        // A monster's zero-damage area is a buff radius (rally), not a threat.
        bHarmlessArea=Cast<ACireMonster>(Area->GetOwner())&&(Area->AreaSpec.bPersistent?Area->AreaSpec.DamagePerSecond<=0:Area->AreaSpec.BurstDamage<=0);
        if(!bShapeResolved)
        {
            // The area carries its display name; resolve the ability (and its rune theme) once.
            bShapeResolved=true;bool bFound=false;
            if(const auto* Arch=ArchetypeOf(Area->GetOwner()))
                for(const auto& Ab:Arch->Abilities)if(Ab.Name.Left(80)==Area->AreaSpec.AbilityName){Shape=CireAbilityShapes::DescribeMonster(Ab,Arch);bFound=true;break;}
            if(!bFound)Shape=CireAbilityShapes::Describe(FName(*NormId(FName(*Area->AreaSpec.AbilityName))));
            if(Shape.Kind==ECireHitShape::None&&Area->AreaSpec.bPoison)Shape.School=ECireSchool::Poison;
        }
        return false;
    }
    if(Mode==EMode::Projectile)
    {
        AActor* Actor=FollowedActor.Get();
        if(!IsValid(Actor)||Actor->IsActorBeingDestroyed())
        {
            // Projectile ended (hit, wall, range): small dissipating burst where it stopped.
            if(!IsHidden()&&TrailPoints.Num()>0&&ReleasedAge>=0&&LaneLength>0)
            {
                const FVector Last=TrailPoints.Last(),Dir=TrailPoints.Num()>1?(Last-TrailPoints[TrailPoints.Num()-2]).GetSafeNormal():GetActorForwardVector();
                CireSpellPresentation::Play(GetWorld(),Skill,Last-Dir*40,Last,ECireSpellCue::Impact,.55f,false);
            }
            Destroy();return true;
        }
        if(auto* Shot=Cast<ACireSkillshot>(Actor))
        {
            if(LaneLength<=0)
            {
                LaneLength=FMath::Min(Shot->ShotSpec.MaxRange,Shot->ShotSpec.Speed*Shot->ShotSpec.LifetimeSeconds);LaneWidth=Shot->ShotSpec.Radius*2;
                LaneOrigin=Actor->GetActorLocation();LaneDirection=Actor->GetActorForwardVector().GetSafeNormal2D();
                bHostile=Cast<ACireMonster>(Shot->GetOwner())!=nullptr||HostileToLocal(GetWorld(),Shot->GetOwner());
                // Rune theme from the ability (display name), else the skillshot's visual style.
                Shape=CireAbilityShapes::Describe(FName(*NormId(FName(*Shot->AbilityName))));
                if(Shape.Kind==ECireHitShape::None||Shape.School==ECireSchool::Steel)Shape.School=CireAbilityShapes::SchoolFor(Skill);
                FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(CireLaneGround),false,Actor);Q.AddIgnoredActor(Shot->GetOwner());
                GroundZ=GetWorld()->LineTraceSingleByObjectType(Hit,LaneOrigin+FVector(0,0,40),LaneOrigin-FVector(0,0,700),FCollisionObjectQueryParams(ECC_WorldStatic),Q)
                    ?static_cast<float>(Hit.ImpactPoint.Z-LaneOrigin.Z):-88.f;
                bGroundProbed=true;
            }
            if(!Shot->bReleased)LaneDirection=Actor->GetActorForwardVector().GetSafeNormal2D();
            if(Shot->bReleased&&ReleasedAge<0)ReleasedAge=Age;
        }
        else if(ReleasedAge<0)ReleasedAge=Age;
        return false;
    }
    if(Mode==EMode::Channel)
    {
        const auto* Hero=Cast<ACireHero>(CastSource.Get());
        const bool bCasting=Hero&&!Hero->CastSkill.IsNone()&&NormId(Hero->CastSkill)==NormId(Skill);
        if(!bCasting&&Age>.15f&&FadeOutAt<0){FadeOutAt=Age;Duration=FMath::Min(Duration,Age+.25f);}
        return false;
    }
    if(Mode==EMode::Lane||Mode==EMode::Gather)
    {
        // A monster whose cast is interrupted (kick, stun, death) drops its telegraph immediately.
        const auto* M=Cast<ACireMonster>(CastSource.Get());
        const bool bGone=CastSource.IsStale()||(M&&M->Health<=0);
        const bool bStopped=M&&Age>.25f&&M->CastingAbility.IsEmpty()&&Age<Duration-.3f;
        if((bGone||bStopped)&&FadeOutAt<0){FadeOutAt=Age;Duration=FMath::Min(Duration,Age+.2f);}
    }
    return false;
}

bool ACireSpellVisual::RebuildModes(FCireSpellMesh& M,FCireSoftMesh& Soft,float T,float Fade,float Expand)
{
    FLinearColor Main=Tint;Main.A=Fade;
    FLinearColor Glow=Tint*.55f;Glow.A=Fade*.22f;
    FLinearColor Core=FMath::Lerp(Tint,FLinearColor(2.3f,2.3f,2.1f,1),.45f);Core.A=Fade*.9f;
    if(Shape.bHeal&&(Mode==EMode::SelfShock||Mode==EMode::TargetMark||Mode==EMode::Gather||Mode==EMode::Channel))
    {
        // Healing is unmistakable: soft green/gold "+" motes shimmer upward around the healed unit.
        const FLinearColor Green=CireAbilityVFX::RuneColor(CireAbilityVFX::ERuneSet::Heal),Gold(1.55f,1.15f,.25f,1);
        for(int32 J=0;J<10;++J)
        {
            const float U=Fract(Age*.8f+J*.1f);const float A=J*2.39996f+Age*.6f;
            const FVector P=Polar(38.f+14.f*FMath::Sin(J*1.7f),A,-70.f+U*150.f);const float S=3.5f+2.f*(1-U);
            const FLinearColor C=WithAlpha(J%2?Gold:Green,FMath::Sin(U*PI)*.9f*Fade);
            M.Tube(P-FVector(0,S,0),P+FVector(0,S,0),.9f,C,3);M.Tube(P-FVector(0,0,S),P+FVector(0,0,S),.9f,C,3);
            if(J%3==0)Soft.Glow(P,9.f,WithAlpha(Green*.6f,.25f*C.A));
        }
    }
    switch(Mode)
    {
    case EMode::VoidZone:
    {
        // Void motes drifting up from the stun circle.
        const FLinearColor V=CireAbilityVFX::RuneColor(CireAbilityVFX::ERuneSet::Void);
        for(int32 J=0;J<12;++J)
        {
            const float U=Fract(Age*.5f+J*.083f);const FVector P=Polar(Shape.VoidInner*(.2f+.6f*Fract(J*.37f)),J*2.39996f+Age,GroundZ+8.f+U*90.f);
            M.Star(P,2.5f+2.f*(1-U),WithAlpha(V,FMath::Sin(U*PI)*.9f*Fade),J);
        }
        Soft.Glow(FVector(0,0,GroundZ+30.f),FMath::Min(Shape.VoidInner,140.f),WithAlpha(V*.6f,.22f*Fade));
        return true;
    }
    case EMode::AreaFollow:
    {
        if(bHarmlessArea&&!bPylonField){DrawAreaParticles(M,Soft,0);return true;} // harmless buff radius: ring + border sparks
        if(bPylonField){DrawAreaParticles(M,Soft,0);return true;} // balance: pylon fields never detonate
        const float Burst=AreaActiveAge>=0&&!bAreaPersistent?FMath::Clamp(1-(Age-AreaActiveAge)/.4f,0.f,1.f):0.f;
        DrawAreaParticles(M,Soft,Burst);return true;
    }
    case EMode::Projectile:DrawProjectile(M,Soft);return true;
    case EMode::Impact:DrawImpact(M,Soft,T,Fade);return true;
    case EMode::Lane:case EMode::Gather:
    {
        // Wind-up: motes spiral into the caster's hands while the telegraph fills.
        const float Charge=FMath::Clamp(Age/FMath::Max(Duration-.18f,.1f),0.f,1.f);
        const FVector Hand(28,0,38);
        FLinearColor C=bHostile&&Mode==EMode::Lane?FLinearColor(2.2f,.8f,.12f,Fade):Main;
        for(int32 J=0;J<10;++J)
        {
            const float U=Fract(Age*1.4f+J*.1f);const float A=J*2.39996f+Age*3.f;
            const FVector P=Hand+Polar((1-U)*70.f,A,(1-U)*(J%2?40.f:-30.f));
            FLinearColor Mote=C;Mote.A*=U*.9f;M.Star(P,2.5f+U*2.f,Mote,A);
            if(J%2==0)Soft.Glow(P,6.f+U*4,WithAlpha(C*.5f,.25f*U*Fade));
        }
        Soft.Glow(Hand,18.f+Charge*26.f,WithAlpha(C*.6f,(.18f+.3f*Charge)*Fade));
        M.Star(Hand,4.f+Charge*8.f,WithAlpha(FMath::Lerp(C,FLinearColor(2.4f,2.4f,2.2f,1),.4f),.8f*Fade),Age*4.f);
        return true;
    }
    case EMode::CasterFlare:
    {
        // Release flare at the hands, pointed along the aim: spark fan + soft bloom.
        const FVector Hand(30,0,34);
        Soft.Glow(Hand,26.f+Expand*34.f,WithAlpha(Glow,Glow.A*1.6f));
        M.Star(Hand,8.f*(1-T)+3.f,Core,Age*3.f);
        for(int32 J=0;J<9;++J)
        {
            const float A=(J-4)*.16f;const float Len=(20.f+J%3*14.f)*Expand+6.f;
            const FVector Dir(FMath::Cos(A),FMath::Sin(A),FMath::Sin(J*1.3f)*.25f);
            M.Tube(Hand+Dir*(8.f+Expand*30.f),Hand+Dir*(8.f+Expand*30.f+Len),1.1f,WithAlpha(Core,Core.A*(1-T)),3);
        }
        return true;
    }
    case EMode::Chain:
    {
        const FVector A=GetActorTransform().InverseTransformPosition(Start)+FVector(0,0,20),B=FVector(0,0,10);
        const int32 Seed=static_cast<int32>(Age*14.f);
        FLinearColor Bright=FMath::Lerp(Tint,FLinearColor(2.6f,2.6f,3.f,1),.55f);Bright.A=Fade;
        M.Bolt(A,B,3.2f,Bright,Seed,26.f,14);M.Bolt(A,B,1.2f,WithAlpha(Tint,Fade*.7f),Seed+7,40.f,12);
        for(int32 J=1;J<14;J+=3){const FVector P=FMath::Lerp(A,B,J/14.f);Soft.Glow(P,20,WithAlpha(Tint*.6f,.25f*Fade));}
        Soft.Glow(B,52.f,WithAlpha(Tint*.7f,.3f*Fade));M.Star(B,14.f*(1-T)+4.f,Core,Age*5.f);
        return true;
    }
    default:return false;
    }
}

void ACireSpellVisual::DrawAreaParticles(FCireSpellMesh& M,FCireSoftMesh& Soft,float Burst)
{
    const FCireAreaSpec& Spec=FollowedArea.IsValid()?FollowedArea->AreaSpec:CachedArea;
    const bool bActive=AreaActiveAge>=0;
    const float Fade=FadeOutAt>=0?FMath::Clamp(1-(Age-FadeOutAt)/.3f,0.f,1.f):1.f;
    const auto Boundary=ACireAreaEffect::BoundaryPoints(Spec);
    if(!bActive||(bHarmlessArea&&!bPylonField))
    {
        // Warning: a few sparks drift up off the true border (never over the interior), so the edge reads
        // even where the ground is busy. Amber for enemies, school colour for your own team.
        const FLinearColor Edge=bHostile&&!bHarmlessArea?FLinearColor(2.2f,.8f,.12f,1):Tint;
        for(int32 J=0;J<10&&Boundary.Num()>1;++J)
        {
            const float U=Fract(J*.1f+Age*.07f);const float Rise=Fract(Age*.8f+J*.37f);
            const float At=U*Boundary.Num();const int32 K=FMath::FloorToInt(At)%Boundary.Num();
            const FVector2D P=FMath::Lerp(Boundary[K],Boundary[(K+1)%Boundary.Num()],At-FMath::FloorToFloat(At));
            FLinearColor C=Edge;C.A=FMath::Sin(Rise*PI)*.75f*Fade;
            M.Star(FVector(P.X,P.Y,6+Rise*38),2.2f+Rise*1.5f,C,J+Age);
            if(J%3==0)Soft.Glow(FVector(P.X,P.Y,10+Rise*30),8,WithAlpha(Edge*.5f,C.A*.3f));
        }
        return;
    }
    FVector2D Lo(MAX_flt,MAX_flt),Hi(-MAX_flt,-MAX_flt);
    for(auto P:Boundary){Lo.X=FMath::Min(Lo.X,P.X);Lo.Y=FMath::Min(Lo.Y,P.Y);Hi.X=FMath::Max(Hi.X,P.X);Hi.Y=FMath::Max(Hi.Y,P.Y);}
    const ECireSchool School=Shape.School!=ECireSchool::Steel?Shape.School:static_cast<ECireSchool>(FMath::Clamp(Family,0,static_cast<int32>(ECireSchool::Count)-1));
    const float AreaScale=FMath::Clamp(FMath::Sqrt(FMath::Max(1.f,float((Hi.X-Lo.X)*(Hi.Y-Lo.Y))))/300.f,.6f,2.2f);
    const int32 Want=bPylonField?CirePylonField::ParticleBudget(PylonOverlaps):FMath::Clamp(FMath::RoundToInt(16*AreaScale),10,30); // balance: pylon overlap budget
    int32 Spawned=0;
    for(int32 I=0;I<120&&Spawned<Want;++I)
    {
        const FVector P(FMath::Lerp(Lo.X,Hi.X,Fract(I*.618034f+.12f)),FMath::Lerp(Lo.Y,Hi.Y,Fract(I*.414214f+.29f)),0);
        if(!ACireAreaEffect::ContainsPoint(Spec,FVector::ZeroVector,FRotator::ZeroRotator,P))continue;
        ++Spawned;const float Cycle=Fract(Age*(School==ECireSchool::Fire?1.1f:.5f)+I*.17f);
        FLinearColor C=Tint;C.A=FMath::Sin(Cycle*PI)*.5f*Fade;
        FLinearColor Halo=C;Halo.A*=.35f;
        switch(School)
        {
        case ECireSchool::Frost:M.Shard(P+FVector(0,0,4),7,40+20*Fract(I*.37f),C,I);Soft.Glow(P+FVector(0,0,20),14,Halo);break;
        case ECireSchool::Fire:M.Shard(P+FVector(0,0,5+Cycle*60),9*(1-Cycle)+1,38*(1-Cycle)+8,C,I);Soft.Glow(P+FVector(0,0,12+Cycle*40),18,Halo);break;
        case ECireSchool::Poison:case ECireSchool::Nature:
        {
            // Bubbling vapour: rising puffs with a low haze.
            const FVector Rise=P+FVector(FMath::Sin(Cycle*PI*2+I)*10,FMath::Cos(Cycle*PI*2+I)*10,4+Cycle*55);
            Soft.Glow(Rise,10+Cycle*18,WithAlpha(Tint*.7f,C.A*.5f));M.Star(Rise,2.5f+Cycle*2,C,I);
            if(Spawned%3==0)Soft.Glow(P+FVector(0,0,8),34,WithAlpha(Tint*.5f,.1f*Fade));
            break;
        }
        case ECireSchool::Shadow:case ECireSchool::Void:
        {
            const FVector Wisp=P+FVector(FMath::Sin(Age*2+I)*14,FMath::Cos(Age*2+I)*14,6+Cycle*70);
            M.Tube(Wisp,Wisp+FVector(0,0,16),2.2f*(1-Cycle)+.4f,C,4);Soft.Glow(Wisp,12,Halo);break;
        }
        case ECireSchool::Tide:
        {
            // Churning water: droplets circling outward from each spout.
            const float R=6+Cycle*26;
            for(int32 K=0;K<5;++K)M.Star(P+Polar(R,K*2*PI/5+Age,4+FMath::Sin(Cycle*PI)*12),2.2f,C,K);
            Soft.Glow(P+FVector(0,0,10),14,Halo);M.Shard(P,3,14*FMath::Sin(Cycle*PI),C,I);break;
        }
        default:
        {
            const FVector Rise=P+FVector(FMath::Sin(Cycle*PI*2+I)*12,FMath::Cos(Cycle*PI*2+I)*12,7+Cycle*65);
            M.Shard(Rise,5+Cycle*3,10,C,I);Soft.Glow(Rise,9,Halo);
            if(Spawned%3==0)M.Rune(P+FVector(0,0,6),10,Age*.2f+I,C);
        }
        }
    }
    if(Burst>0)
    {
        // Detonation: spikes erupt across the zone and a bright bloom at the pivot.
        const FVector2D Pivot=Spec.Shape==ECireAreaShape::Line?FVector2D(Spec.Length*.5f,0):Spec.Shape==ECireAreaShape::Cone?FVector2D(Spec.Radius*.5f,0):Centroid(Boundary);
        int32 Count=0;
        for(int32 I=0;I<90&&Count<18;++I)
        {
            const FVector P(FMath::Lerp(Lo.X,Hi.X,Fract(I*.754877f+.31f)),FMath::Lerp(Lo.Y,Hi.Y,Fract(I*.569840f+.07f)),0);
            if(!ACireAreaEffect::ContainsPoint(Spec,FVector::ZeroVector,FRotator::ZeroRotator,P))continue;++Count;
            FLinearColor C=FMath::Lerp(Tint,FLinearColor(2.4f,2.3f,2.1f,1),.3f);C.A=Burst*.95f;
            M.Spike(P,P+FVector(FMath::Sin(I*1.7f)*12,FMath::Cos(I*2.3f)*12,(40+Fract(I*.37f)*70)*(1.2f-Burst*.5f)),6*Burst+1.5f,C,I);
        }
        Soft.Glow(FVector(Pivot.X,Pivot.Y,30),FMath::Min(260.f,60+AreaScale*80),WithAlpha(Tint*.8f,.45f*Burst));
    }
}

void ACireSpellVisual::DrawProjectile(FCireSpellMesh& M,FCireSoftMesh& Soft)
{
    // Readability floor: thin collision spheres (arrows, 18 cm piercing shot) still get a visible head.
    const float R=FMath::Clamp(FMath::Max(float(FollowBounds.X),20.f),4.f,80.f);
    const ECireSchool School=Shape.School;
    const float Flicker=.85f+.15f*FMath::Sin(Age*37.f);
    FLinearColor Core=FMath::Lerp(Tint,FLinearColor(2.6f,2.5f,2.3f,1),.5f);Core.A=.95f;
    FLinearColor Main=Tint;Main.A=.85f;
    FLinearColor Glow=Tint*.7f;Glow.A=.34f;
    const bool bWaiting=ReleasedAge<0;
    const float Grow=bWaiting?FMath::Clamp(Age/.35f,.25f,1.f):1.f; // head gathers in the hand during the warning
    const float S=R*Grow;
    // Readable head: bright core, soft corona, and a hot white centre.
    Soft.Glow(FVector(S*.3f,0,0),S*3.4f,Glow);Soft.Glow(FVector(S*.2f,0,0),S*1.5f,WithAlpha(Core*.8f,.5f));
    switch(School)
    {
    case ECireSchool::Steel:case ECireSchool::Blood:
    {
        // Arrow / thrown lance: shaft, broad head, fletching and a white streak.
        FLinearColor Shaft(.55f,.42f,.3f,1);M.Tube(FVector(-S*7,0,0),FVector(S*1.6f,0,0),FMath::Max(1.2f,S*.14f),Shaft,5);
        M.Spike(FVector(S*1.4f,0,0),FVector(S*3.4f,0,0),S*.42f,Core);
        for(int32 J=0;J<3;++J){const float A=J*2*PI/3+Age*2;M.Tri(FVector(-S*6.6f,0,0),FVector(-S*5.2f,0,0),FVector(-S*6.9f,FMath::Cos(A)*S*.7f,FMath::Sin(A)*S*.7f),WithAlpha(Main,.8f));}
        break;
    }
    case ECireSchool::Fire:
    {
        M.Spike(FVector(-S*.6f,0,0),FVector(S*2.2f,0,0),S*.55f,Core,Age*3);
        for(int32 J=0;J<7;++J)
        {
            const float A=J*2*PI/7+Age*9;const float Len=S*(2.2f+1.6f*Fract(J*.37f+Age*3.1f))*Flicker;
            const FVector Base(S*.1f,FMath::Cos(A)*S*.45f,FMath::Sin(A)*S*.45f);
            M.Spike(Base,Base+FVector(-Len,FMath::Cos(A)*S*.5f,FMath::Sin(A)*S*.5f+S*.25f),S*.22f,WithAlpha(J%2?Main:Core,.8f),A);
        }
        break;
    }
    case ECireSchool::Frost:
    {
        M.Spike(FVector(-S*1.4f,0,0),FVector(S*2.8f,0,0),S*.5f,Core,PI*.25f);
        for(int32 J=0;J<4;++J){const float A=J*PI*.5f+Age*1.5f;const FVector Side(0,FMath::Cos(A)*S*.55f,FMath::Sin(A)*S*.55f);
            M.Spike(Side,Side+FVector(-S*2.2f,FMath::Cos(A)*S*.6f,FMath::Sin(A)*S*.6f),S*.2f,WithAlpha(Main,.85f),A);}
        break;
    }
    case ECireSchool::Shadow:case ECireSchool::Void:case ECireSchool::Spirit:
    {
        M.Star(FVector::ZeroVector,S*1.1f,Core,Age*6);
        for(int32 Strand=0;Strand<3;++Strand)
        {
            FVector Prev(S*.5f,0,0);
            for(int32 J=1;J<10;++J)
            {
                const float U=J/9.f,A=Strand*2*PI/3+Age*7+U*4;
                const FVector P(-U*S*6,FMath::Cos(A)*S*(.6f+U),FMath::Sin(A)*S*(.6f+U));
                M.Tube(Prev,P,S*.16f*(1-U)+.4f,WithAlpha(Main,.8f*(1-U)),4);Prev=P;
            }
        }
        break;
    }
    default:
    {
        // Arcane / storm / holy / poison orbs with orbiting sparks.
        M.Star(FVector::ZeroVector,S*1.2f,Core,Age*5);M.Star(FVector::ZeroVector,S*.8f,Core,-Age*4+.4f);
        for(int32 J=0;J<5;++J){const float A=J*2*PI/5+Age*8;const FVector P(FMath::Sin(A*.5f)*S*.4f,FMath::Cos(A)*S*1.3f,FMath::Sin(A)*S*1.3f);M.Star(P,S*.3f,WithAlpha(Main,.9f),A);}
    }
    }
    // Wake: the real recorded path (curves and reflections included), bright near the head.
    for(int32 I=1;I<TrailPoints.Num();++I)
    {
        const float U=I/float(TrailPoints.Num());
        const FVector A=GetActorTransform().InverseTransformPosition(TrailPoints[I-1]),B=GetActorTransform().InverseTransformPosition(TrailPoints[I]);
        Soft.Ribbon(A,B,R*(.35f+U*1.1f),WithAlpha(Tint,U*.4f));M.Tube(A,B,R*.08f*U+.3f,WithAlpha(Core,U*.5f),3);
        if(School==ECireSchool::Fire&&I%2==0)M.Star(A+FVector(0,0,Fract(Age*2+I*.3f)*R*1.5f),R*.18f*U+.6f,WithAlpha(Main,U*.8f),I);
    }
}

void ACireSpellVisual::DrawImpact(FCireSpellMesh& M,FCireSoftMesh& Soft,float T,float Fade)
{
    const ECireSchool School=static_cast<ECireSchool>(FMath::Clamp(Family,0,static_cast<int32>(ECireSchool::Count)-1));
    const float Life=Age;const float E=Ease(T);
    FLinearColor Core=FMath::Lerp(Tint,FLinearColor(2.6f,2.5f,2.3f,1),.5f);Core.A=Fade;
    FLinearColor Main=Tint;Main.A=Fade;
    FVector Away=(End-Start).GetSafeNormal();if(Away.IsNearlyZero())Away=FVector::ForwardVector;
    Away=GetActorTransform().InverseTransformVectorNoScale(Away);
    // Flash: white-hot bloom that collapses in the first 0.15 s.
    const float Flash=FMath::Clamp(1-Life/.16f,0.f,1.f);
    if(NormId(Skill)==TEXT("npc_interrupted"))
    {
        // Interrupt: the caster's rune circle shatters. Bright flash, ring shards thrown outward and falling.
        const FLinearColor Ice(1.6f,1.9f,2.6f,1),White(2.6f,2.6f,2.6f,1);
        Soft.Glow(FVector(0,0,70),60+90*Flash,WithAlpha(Ice*.8f,(.25f+.6f*Flash)*Fade));
        if(Flash>0)M.Star(FVector(0,0,70),40*Flash+10,WithAlpha(White,Flash),Life*3);
        for(int32 I=0;I<12;++I)
        {
            const float A=I*PI/6+.13f;const float Tm=FMath::Min(Life,.55f);
            const FVector V=Polar(260.f+60.f*Fract(I*.37f),A,120.f+80.f*Fract(I*.61f));
            const FVector P=FVector(0,0,70)+Polar(26,A)+V*Tm+FVector(0,0,.5f*Gravity*Tm*Tm);
            const FVector Tangent=Polar(1,A+PI*.5f);
            // Each shard is a piece of the broken cast circle: a short curved plate tumbling.
            const float Spin=Life*9.f+I;
            M.Quad(P-Tangent*9.f,P+Tangent*9.f,P+Tangent*7.f+FVector(0,0,4.f*FMath::Sin(Spin)),P-Tangent*7.f+FVector(0,0,4.f*FMath::Cos(Spin)),WithAlpha(I%2?Ice:White,Fade*(1-T*.6f)));
        }
        M.Ring(30+60*E,2.5f*(1-T)+.5f,70,WithAlpha(White,Fade*(1-T)),Life,PI*1.4f,24);
        return;
    }
    if(bChainHop)
    {
        // Hop arc from the previous chain victim, bright for the first 0.25 s.
        const FVector A=GetActorTransform().InverseTransformPosition(HopFrom)+FVector(0,0,10);
        const float Arc=FMath::Clamp(1-Life/.3f,0.f,1.f);
        FLinearColor Bright=FMath::Lerp(Tint,FLinearColor(2.6f,2.6f,3.f,1),.55f);Bright.A=Arc;
        if(Arc>0){M.Bolt(A,FVector(0,0,10),2.6f,Bright,static_cast<int32>(Life*14.f)+3,22.f,12);M.Bolt(A,FVector(0,0,10),1.f,WithAlpha(Tint,Arc*.7f),static_cast<int32>(Life*14.f)+9,34.f,10);}
    }
    Soft.Glow(FVector::ZeroVector,40+70*Flash+E*25,WithAlpha(Tint*.9f,(.18f+.5f*Flash)*Fade));
    if(Flash>0)Soft.Glow(FVector::ZeroVector,26*Flash+8,WithAlpha(FLinearColor(2.5f,2.4f,2.2f,1),.7f*Flash));
    M.Star(FVector::ZeroVector,22*(1-T)+4,Core,Life*.8f);
    // Sparks on ballistic arcs, biased away from the attacker.
    for(int32 I=0;I<14;++I)
    {
        const float A=I*2.39996f,Up=.25f+Fract(I*.618f)*.8f;
        FVector V=(FVector(FMath::Cos(A),FMath::Sin(A),Up).GetSafeNormal()+Away*.8f).GetSafeNormal()*(260.f+Fract(I*.37f)*260.f);
        const float Tm=FMath::Min(Life,.45f);
        const FVector P=V*Tm+FVector(0,0,.5f*Gravity*Tm*Tm),Q=V*FMath::Max(0.f,Tm-.035f)+FVector(0,0,.5f*Gravity*FMath::Square(FMath::Max(0.f,Tm-.035f)));
        M.Tube(Q,P,School==ECireSchool::Steel?.7f:1.1f,WithAlpha(Core,Fade*(1-T)),3);
    }
    switch(School)
    {
    case ECireSchool::Frost:
        for(int32 I=0;I<7;++I){const float A=I*2*PI/7;const FVector Dir=FVector(FMath::Cos(A),FMath::Sin(A),.9f).GetSafeNormal();
            M.Spike(Dir*6,Dir*(18+34*E),5*(1-T*.5f),WithAlpha(Main,Fade*.9f),A);}
        break;
    case ECireSchool::Fire:
        for(int32 I=0;I<9;++I){const float A=I*2.39996f;const float U=Fract(Life*1.6f+I*.13f);const FVector P=Polar(18+E*40,A,U*70);
            M.Shard(P,6*(1-U)+1,20*(1-U)+4,WithAlpha(I%2?Main:Core,Fade*(1-U)),A);}
        break;
    case ECireSchool::Holy:case ECireSchool::Life:
        for(int32 I=0;I<8;++I){const float A=I*PI*.25f+Life;M.Tube(Polar(10,A,-10),Polar(16+E*20,A,60+E*80),1.6f,WithAlpha(Core,Fade*.8f*(1-T)),4);}
        M.Ring(20+E*50,2.4f,-6,WithAlpha(Main,Fade),Life,2*PI,36);
        break;
    case ECireSchool::Shadow:case ECireSchool::Void:
        // Implosion: wisps pulled into the point of impact.
        for(int32 I=0;I<8;++I){const float A=I*PI*.25f+Life*3;const FVector P=Polar(70*(1-E)+6,A,30*FMath::Sin(A+Life));
            M.Tube(P,P*.6f,2.4f*(1-T)+.4f,WithAlpha(Main,Fade*.9f),4);Soft.Glow(P,10,WithAlpha(Tint*.6f,.25f*Fade));}
        break;
    case ECireSchool::Poison:case ECireSchool::Nature:
        for(int32 I=0;I<9;++I){const float A=I*2.39996f;const float Tm=FMath::Min(Life,.5f);
            const FVector P=Polar(120*Tm,A,160*Tm+.5f*Gravity*Tm*Tm);Soft.Glow(P,8,WithAlpha(Tint,.4f*Fade));M.Star(P,3,WithAlpha(Main,Fade),A);}
        Soft.Glow(FVector(0,0,10),50+E*40,WithAlpha(Tint*.5f,.18f*Fade));
        break;
    case ECireSchool::Tide:
        // Water crown: a ring of spray thrown up and falling back.
        for(int32 I=0;I<12;++I){const float A=I*PI/6;const float Tm=FMath::Min(Life,.55f);
            const FVector P=Polar(20+110*Tm,A,-40+220*Tm+.5f*Gravity*Tm*Tm*1.2f);M.Spike(P-FVector(0,0,12),P,3,WithAlpha(Main,Fade*.85f),A);}
        break;
    case ECireSchool::Storm:
        for(int32 I=0;I<4;++I){const float A=I*PI*.5f+Life*9;M.Bolt(FVector::ZeroVector,Polar(55*E+10,A,20*FMath::Sin(A)),1.2f,WithAlpha(Core,Fade),I+int32(Life*20),9.f,5);}
        break;
    case ECireSchool::Earth:case ECireSchool::Steel:case ECireSchool::Blood:default:
        for(int32 I=0;I<7;++I){const float A=I*2.39996f;const float Tm=FMath::Min(Life,.6f);
            const FVector P=Polar(150*Tm,A,-20+230*Tm+.5f*Gravity*Tm*Tm);
            FLinearColor Rock=School==ECireSchool::Blood?Main:FLinearColor(.18f,.14f,.11f,Fade*.9f);M.Shard(P,4,8,Rock,A+Life*6);}
    }
}

void ACireSpellVisual::RebuildGround(float T,float Fade)
{
    if(!GroundMesh)return;
    if(Mode==EMode::Legacy&&GroundVertices.IsEmpty())return;
    if(!bGroundProbed&&Mode!=EMode::Legacy)ProbeGround();
    FCireGroundMesh G(GroundVertices,GroundIndices,GroundColors);
    G.Z=GroundZ+4.f;LastChevrons=0;
    auto ToLocal=[&](int32 From,const FTransform& Frame)
    {
        // Vertices were built in Frame (e.g. a lane anchored in the world); move them into actor space.
        const FTransform Inverse=Frame.GetRelativeTransform(GetActorTransform());
        for(int32 J=From;J<G.V.Num();++J)G.V[J]=Inverse.TransformPosition(G.V[J]);
    };
    switch(Mode)
    {
    case EMode::AreaFollow:
    {
        const FCireAreaSpec& Spec=FollowedArea.IsValid()?FollowedArea->AreaSpec:CachedArea;
        const float Alpha=FadeOutAt>=0?FMath::Clamp(1-(Age-FadeOutAt)/.3f,0.f,1.f):1.f;
        G.Z=4.f;
        CireAbilityVFX::FPaintResult R;
        if(bPylonField)
        {
            // balance: construct pylon field: flat 15-25% fill shared across overlaps, readable rim (CirePylonField).
            // The pylon's own tint (gold haste, ice gravity, cyan aegis, rose disruption) so stacked fields stay distinguishable.
            FLinearColor C=Spec.Color*1.25f;
            const float Rise=AreaActiveAge<0?1.f:FMath::Clamp((Age-AreaActiveAge)/.35f,0.f,1.f);
            CirePylonField::Paint(G,Spec.Radius,C,Age,Alpha*FMath::Max(.2f,Rise),CirePylonField::Intensity(GetWorld()),PylonOverlaps);
            LastFill=FBox2D(FVector2D(-Spec.Radius,-Spec.Radius),FVector2D(Spec.Radius,Spec.Radius));
        }
        // Harmless zones (a rally's buff radius) are information, not danger: a calm ring, never amber, no detonation.
        else if(bHarmlessArea)
        {
            FLinearColor C=Spec.Color*1.6f;C.A=.55f*Alpha*(AreaActiveAge<0?1.f:FMath::Clamp(1-(Age-AreaActiveAge)/.4f,0.f,1.f));
            G.Ring(FVector2D::ZeroVector,Spec.Shape==ECireAreaShape::Circle?Spec.Radius:FMath::Max(Spec.Radius,Spec.Width*.5f),2.5f,14.f,C,72);
            const float U=Fract(Age*.7f);G.Ring(FVector2D::ZeroVector,Spec.Radius*(.2f+.8f*U),1.5f,8.f,WithAlpha(C,C.A*.6f*(1-U)),64);
            LastFill=FBox2D(ACireAreaEffect::BoundaryPoints(Spec));
        }
        else if(AreaActiveAge<0)
        {
            float Progress=0;
            if(auto* Area=FollowedArea.Get())
                Progress=Spec.WarningSeconds>0?FMath::Clamp(float(ServerNow(GetWorld())-Area->StartServerTime)/Spec.WarningSeconds,0.f,1.f):1.f;
            const auto Style=CireAbilityVFX::ThemedStyle(bHostile?CireAbilityVFX::ETone::Hostile:CireAbilityVFX::ETone::Friendly,Shape);
            R=CireAbilityVFX::PaintTelegraph(G,Spec,Style,Progress,Age,Alpha,CireAbilityVFX::PaintArrow|CireAbilityVFX::PaintCenter|CireAbilityVFX::PaintProgress|CireAbilityVFX::PaintPulse);
        }
        else
        {
            const float Burst=bAreaPersistent?FMath::Clamp(1-(Age-AreaActiveAge)/.35f,0.f,1.f)*.8f:FMath::Clamp(1-(Age-AreaActiveAge)/.4f,0.f,1.f);
            // Pale authored colours washed out to white on release: pull the zone toward its school colour.
            FLinearColor C=Spec.Color*1.7f;
            if(Shape.School!=ECireSchool::Steel)C=FMath::Lerp(C,CireAbilityShapes::SchoolColor(Shape.School)*.9f,.55f);
            C.A=1;
            const auto Theme=CireAbilityVFX::ThemeFor(Shape);
            R=CireAbilityVFX::PaintActive(G,Spec,C,Age,Alpha*(bAreaPersistent?1.f:FMath::Clamp(1-(Age-AreaActiveAge)/.5f,0.f,1.f)*1.2f),Burst,bAreaPersistent,&Theme);
        }
        LastFill=R.FillBounds;LastArrowTip=R.ArrowTip;LastChevrons=R.Chevrons;
        break;
    }
    case EMode::Lane:
    {
        FCireAreaSpec Lane;Lane.Shape=ECireAreaShape::Line;Lane.Length=FMath::Max(1.f,LaneLength);Lane.Width=FMath::Max(1.f,LaneWidth);
        const float Warn=FMath::Max(Duration-.18f,.1f);
        const float Alpha=FMath::Clamp(Age/.08f,0.f,1.f)*(Age>Warn?FMath::Clamp(1-(Age-Warn)/.18f,0.f,1.f):1.f);
        const auto Style=CireAbilityVFX::ThemedStyle(bHostile?CireAbilityVFX::ETone::Hostile:CireAbilityVFX::ETone::Friendly,Shape);
        const auto R=CireAbilityVFX::PaintTelegraph(G,Lane,Style,FMath::Clamp(Age/Warn,0.f,1.f),Age,Alpha,CireAbilityVFX::PaintArrow|CireAbilityVFX::PaintProgress|CireAbilityVFX::PaintPulse);
        LastFill=R.FillBounds;LastArrowTip=R.ArrowTip;LastChevrons=R.Chevrons;
        break;
    }
    case EMode::Projectile:
    {
        if(LaneLength>0)
        {
            const float Alpha=ReleasedAge<0?FMath::Clamp(Age/.06f,0.f,1.f):FMath::Clamp(1-(Age-ReleasedAge)/.35f,0.f,1.f);
            if(Alpha>0)
            {
                FCireAreaSpec Lane;Lane.Shape=ECireAreaShape::Line;Lane.Length=LaneLength;Lane.Width=LaneWidth;
                const auto Style=CireAbilityVFX::ThemedStyle(bHostile?CireAbilityVFX::ETone::Hostile:CireAbilityVFX::ETone::Friendly,Shape);
                const auto* Shot=Cast<ACireSkillshot>(FollowedActor.Get());
                const float WarnSeconds=Shot?FMath::Max(Shot->ShotSpec.WarningSeconds,.05f):.35f;
                const int32 From=G.V.Num();
                const auto R=CireAbilityVFX::PaintTelegraph(G,Lane,Style,ReleasedAge<0?FMath::Clamp(Age/WarnSeconds,0.f,1.f):1.f,Age,Alpha,
                    CireAbilityVFX::PaintArrow|CireAbilityVFX::PaintProgress|CireAbilityVFX::PaintPulse);
                LastFill=R.FillBounds;LastArrowTip=R.ArrowTip;LastChevrons=R.Chevrons;
                ToLocal(From,FTransform(LaneDirection.Rotation(),LaneOrigin));
            }
        }
        // Ground glow under the head: makes height and path readable for dodging.
        if(LaneLength<=0)break;
        const int32 From=G.V.Num();
        const float R=FMath::Clamp(float(FollowBounds.X),4.f,80.f);
        FLinearColor C=Tint*.8f;C.A=.3f;
        G.Z=4.f;G.Disc(FVector2D::ZeroVector,R*2.6f,C,WithAlpha(C,0),20);
        const FVector Here=GetActorLocation();
        ToLocal(From,FTransform(FRotator::ZeroRotator,FVector(Here.X,Here.Y,LaneOrigin.Z+GroundZ)));
        break;
    }
    case EMode::SelfShock:
    {
        const auto Theme=CireAbilityVFX::ThemeFor(Shape);
        CireAbilityVFX::PaintShock(G,FVector2D::ZeroVector,FMath::Max(40.f,Shape.Radius),Shape.bHeal?CireAbilityVFX::RuneColor(CireAbilityVFX::ERuneSet::Heal):Tint,
            FMath::Clamp(Age/(Shape.bHeal||Shape.bBuff?.8f:.55f),0.f,1.f),Fade,&Theme);
        break;
    }
    case EMode::Channel:
    {
        // Cast-time telegraph: the true area (self circles) or a rune ring on the target, filling until release.
        const auto* Hero=Cast<ACireHero>(CastSource.Get());
        float Progress=FMath::Clamp(Age/FMath::Max(Duration-.25f,.1f),0.f,1.f);
        if(Hero&&Hero->CastEndTime>Hero->CastStartTime)Progress=FMath::Clamp(float((ServerNow(GetWorld())-Hero->CastStartTime)/(Hero->CastEndTime-Hero->CastStartTime)),0.f,1.f);
        const float Alpha=FMath::Clamp(Age/.1f,0.f,1.f)*(FadeOutAt>=0?FMath::Clamp(1-(Age-FadeOutAt)/.25f,0.f,1.f):1.f);
        if(Shape.Kind==ECireHitShape::Circle&&Shape.Radius>0)
        {
            FCireAreaSpec Spec=Shape.AsArea();
            const auto Style=CireAbilityVFX::ThemedStyle(bHostile?CireAbilityVFX::ETone::Hostile:CireAbilityVFX::ETone::Friendly,Shape);
            const auto R=CireAbilityVFX::PaintTelegraph(G,Spec,Style,Progress,Age,Alpha,CireAbilityVFX::PaintProgress|CireAbilityVFX::PaintPulse);
            LastFill=R.FillBounds;
        }
        else
        {
            const auto Theme=CireAbilityVFX::ThemeFor(Shape);
            CireAbilityVFX::PaintRuneRing(G,FVector2D::ZeroVector,90.f,Theme,Age,.9f*Alpha);
            FLinearColor C=Theme.Glyph;C.A=.5f*Alpha;G.Ring(FVector2D::ZeroVector,40.f+60.f*Progress,2.f,6.f,C,40);
        }
        break;
    }
    case EMode::VoidZone:
    {
        G.Z=GroundZ+4.f;
        const float Alpha=FMath::Clamp(Age/.12f,0.f,1.f)*FMath::Clamp((Duration-Age)/.3f,0.f,1.f);
        // The zone opens from the centre over 0.25 s.
        const float Open=Ease(FMath::Clamp(Age/.25f,0.f,1.f));
        const auto R=CireAbilityVFX::PaintVoidZone(G,FVector2D::ZeroVector,Shape.VoidOuter*FMath::Max(.3f,Open),Shape.VoidInner*FMath::Max(.3f,Open),
            bHostile?CireAbilityVFX::ETone::Hostile:CireAbilityVFX::ETone::Friendly,Age,Alpha,Shape.bVoidHeal);
        LastVoidRadii=FVector2D(R.Outer,R.Inner);LastVoidIcons=R.SlowIcons+R.StunIcons;LastFill=R.Bounds;
        // Portal streak from the other end of the teleport.
        const FVector Other=GetActorTransform().InverseTransformPosition(Shape.bVoidAtOrigin?End:Start);
        FLinearColor C=CireAbilityVFX::RuneColor(CireAbilityVFX::ERuneSet::Void);C.A=.6f*Alpha*(1-FMath::Clamp(Age/.6f,0.f,1.f));
        if(FVector2D(Other.X,Other.Y).Size()>60.f&&C.A>0)G.Stroke(FVector2D(Other.X,Other.Y),FVector2D::ZeroVector,3.f,8.f,C,.5f);
        break;
    }
    case EMode::TargetMark:
        if(Shape.Kind==ECireHitShape::Circle&&Shape.WarningSeconds<=0)
        {
            const auto Theme=CireAbilityVFX::ThemeFor(Shape);
            CireAbilityVFX::PaintShock(G,FVector2D::ZeroVector,FMath::Max(40.f,Shape.Radius),Tint,FMath::Clamp(Age/.6f,0.f,1.f),Fade,&Theme);
        }
        else if(Shape.Kind==ECireHitShape::Unit)
        {
            // Unit strike / heal: a streak along the ground from the caster to the unit, then a ring on it.
            const FVector Local=GetActorTransform().InverseTransformPosition(Start);
            const FVector2D From(Local.X,Local.Y);const float U=FMath::Clamp(Age/.25f,0.f,1.f);
            FLinearColor C=Tint;C.A=.75f*Fade*(1-FMath::Clamp((Age-.25f)/.5f,0.f,1.f));
            if(From.Size()>60.f&&C.A>0)G.Stroke(From,FMath::Lerp(From,FVector2D::ZeroVector,Ease(U)),3.5f,9.f,C,.5f);
            const auto Theme=CireAbilityVFX::ThemeFor(Shape);
            CireAbilityVFX::PaintShock(G,FVector2D::ZeroVector,Shape.bHeal?95.f:75.f,Shape.bHeal?CireAbilityVFX::RuneColor(CireAbilityVFX::ERuneSet::Heal):Tint,
                FMath::Clamp(Age/(Shape.bHeal?.8f:.5f),0.f,1.f),Fade,&Theme);
        }
        break;
    case EMode::Chain:
    {
        FLinearColor C=Tint;C.A=.5f*Fade;G.Ring(FVector2D::ZeroVector,Shape.Radius>0?Shape.Radius:500.f,1.8f,6.f,C,64);
        const auto Theme=CireAbilityVFX::ThemeFor(Shape);
        CireAbilityVFX::PaintShock(G,FVector2D::ZeroVector,90.f,Tint,FMath::Clamp(Age/.4f,0.f,1.f),Fade,&Theme);
        break;
    }
    case EMode::CasterFlare:
        CireAbilityVFX::PaintShock(G,FVector2D::ZeroVector,85.f,Tint,FMath::Clamp(Age/.45f,0.f,1.f),Fade*.8f);
        break;
    case EMode::Gather:
    {
        const float Charge=FMath::Clamp(Age/FMath::Max(Duration-.12f,.1f),0.f,1.f);
        FLinearColor C=Tint;C.A=(.35f+.4f*Charge)*Fade;
        G.Ring(FVector2D::ZeroVector,70.f-20.f*Charge,2.f,7.f,C,40);
        // School rune ring gathering at the caster's feet (heal casts show the green/gold cross runes).
        const auto Theme=CireAbilityVFX::ThemeFor(Shape);
        CireAbilityVFX::PaintRuneRing(G,FVector2D::ZeroVector,95.f-25.f*Charge,Theme,Age,(.5f+.4f*Charge)*Fade);
        break;
    }
    case EMode::Impact:
        if(Size>=.5f)CireAbilityVFX::PaintShock(G,FVector2D::ZeroVector,70.f*FMath::Clamp(Size,.5f,2.f),Tint,FMath::Clamp(Age/.5f,0.f,1.f),Fade*.75f);
        break;
    default:break;
    }
    if(!bShakeDone&&Mode==EMode::Impact&&Age>=0)
    {
        bShakeDone=true;
        // Only a heavy hit on, or by, the local champion kicks the camera.
        const auto* Local=LocalHero(GetWorld());
        if(Local&&(FVector::Dist(Local->GetActorLocation(),End)<120||FVector::Dist(Local->GetActorLocation(),Start)<120))
            CireAbilityVFX::ImpactShake(GetWorld(),End,Cue==ECireSpellCue::Critical?2.2f:1.2f*FMath::Clamp(Size,.5f,1.5f));
    }
    // Ground radii above already carry Size; the modeled core is scaled separately by Rebuild.
    // Brightness: player intensity, and overlapping zones share one brightness budget.
    if(Mode==EMode::AreaFollow&&(Age-OverlapCheckedAt>.25f||OverlapCount<1))
    {
        OverlapCheckedAt=Age;OverlapCount=1;
        const float Mine=LastFill.bIsValid?static_cast<float>(LastFill.GetExtent().Size()):200.f;
        for(TActorIterator<ACireSpellVisual> It(GetWorld());It;++It)
            if(*It!=this&&!It->IsActorBeingDestroyed()&&!It->IsHidden()&&It->GetMode()==EMode::AreaFollow)
            {
                const float Other=It->GroundFillBounds().bIsValid?static_cast<float>(It->GroundFillBounds().GetExtent().Size()):200.f;
                if(FVector::Dist2D(It->GetActorLocation(),GetActorLocation())<(Mine+Other)*.7f)++OverlapCount;
            }
    }
    // balance: pylon fields already paint their final, overlap-shared alpha from the same slider (CirePylonField).
    if(!(Mode==EMode::AreaFollow&&bPylonField))
    CireAbilityVFX::Temper(G.C,0,CireAbilityVFX::GroundIntensity(GetWorld()),Mode==EMode::AreaFollow?1.f/FMath::Sqrt(static_cast<float>(FMath::Max(1,OverlapCount))):1.f);
    if(G.V.IsEmpty()){GroundMesh->ClearMeshSection(0);return;}
    const auto* Section=GroundMesh->GetProcMeshSection(0);
    if(Section&&Section->ProcVertexBuffer.Num()==G.V.Num()&&Section->ProcIndexBuffer.Num()==G.I.Num())
        GroundMesh->UpdateMeshSection_LinearColor(0,G.V,TArray<FVector>(),TArray<FVector2D>(),G.C,TArray<FProcMeshTangent>(),false);
    else GroundMesh->CreateMeshSection_LinearColor(0,G.V,G.I,TArray<FVector>(),TArray<FVector2D>(),G.C,TArray<FProcMeshTangent>(),false);
}

float CireSpellPresentation::ReleaseDelay(UWorld* World,FName SkillId,ECireSpellCue Cue,FVector From)
{
    if(!World||!CireAbilityVFX::Enabled()||(Cue!=ECireSpellCue::Cast&&Cue!=ECireSpellCue::Impact&&Cue!=ECireSpellCue::Critical))return 0;
    if(!Cast<ACireHero>(SourceAt(World,From)))return 0;
    const FString Id=NormId(SkillId);
    if(!CireAbilityShapes::ChampionAbilityIds().Contains(FName(*Id))||Id.StartsWith(TEXT("basic_")))return 0;
    if(Cue==ECireSpellCue::Cast)return CireAbilityVFX::ReleaseLead(World,Id);
    // Impacts: only abilities that resolve on the cast frame wait for the clip's contact.
    const auto Shape=CireAbilityShapes::Describe(FName(*Id));
    return Shape.bProjectile||Shape.WarningSeconds>0||Shape.Kind==ECireHitShape::None?0.f:CireAbilityVFX::InstantLead;
}

void ACireSpellVisual::EndPlay(const EEndPlayReason::Type Reason)
{
    // Hand the ground back to the area if this presentation goes away first (capacity, cleanup).
    if(auto* Area=FollowedArea.Get())Area->bPresentationOwnsGround=false;
    // fab-integration: let a looping Fab overlay (projectile trail, zone) finish its particles instead of popping.
    if(UNiagaraComponent* FX=FabFX.Get()){FX->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);FX->SetAutoDestroy(true);FX->Deactivate();}
    Super::EndPlay(Reason);
}

bool CireSpellPresentation::IsAreaPresented(const ACireAreaEffect* Area)
{
    if(!IsValid(Area)||!Area->bPresentationOwnsGround)return false;
    for(TActorIterator<ACireSpellVisual> It(Area->GetWorld());It;++It)
        if(It->GetOwner()==Area&&!It->IsActorBeingDestroyed()&&!It->IsHidden()&&It->GroundVertexCount()>0&&It->GroundMesh&&It->GroundMesh->IsVisible())return true;
    return false;
}
