#include "CireSpellPresentation.h"
#include "CireAreaEffects.h"
#include "CireGame.h"
#include "CireHUD.h"
#include "CireChampionRoster.h"
#include "CireAuraVisuals.h" // aura-vfx
#include "CireSpellMesh.h" // ability-vfx
#include "CireAbilityShapes.h" // ability-vfx
#include "CireAbilityVFX.h" // ability-vfx
#include "CireFabVFX.h" // fab-integration
#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "NiagaraComponent.h" // fab-integration
#include "NiagaraSystem.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundConcurrency.h"
#include <limits>

namespace
{
constexpr int32 MaxEffects = 64, MaxVertices = 8192, MaxCoreVertices = 6144, MaxSoftVertices = 2048, MaxLights = 8;
int32 CurrentPhase(UWorld* World)
{
    if(const auto* Mode=World?World->GetAuthGameMode<ACireGameMode>():nullptr) return static_cast<int32>(Mode->Clock.Phase());
    if(const auto* State=World?World->GetGameState<ACireGameState>():nullptr) return State->Phase;
    return INDEX_NONE;
}
float CombatVolume(UWorld* World)
{
    const auto* PC=World?World->GetFirstPlayerController():nullptr;
    const auto* HUD=PC?Cast<ACireHUD>(PC->GetHUD()):nullptr;
    if(!HUD)return .85f*.85f;
    return HUD->UISettings.bMuteAudio?0.f:HUD->UISettings.MasterVolume*HUD->UISettings.SFXVolume;
}
enum EFamily { Steel, Fire, Frost, Storm, Shadow, Life, Holy, Poison, Arcane, Earth, Nature, Spirit, Blood, Tide, Void }; // ability-vfx: Tide/Void monster schools
FString Normalize(FName Id)
{
    FString S = Id.ToString().ToLower();
    S.ReplaceInline(TEXT(" "),TEXT("_")); S.ReplaceInline(TEXT("'"),TEXT(""));
    return S;
}
// Previous id-substring families, kept for cire.AbilityVFX 0 A/B captures.
int32 LegacyFamilyFor(FName Id)
{
    const FString S = Normalize(Id);
    if (S.Contains(TEXT("venom")) || S.Contains(TEXT("blight")) || S.Contains(TEXT("poison"))) return Poison;
    if (S.Contains(TEXT("ember")) || S.Contains(TEXT("cinder")) || S.Contains(TEXT("cataclysm"))) return Fire;
    if (S.Contains(TEXT("frost"))) return Frost;
    if (S.Contains(TEXT("spark")) || S.Contains(TEXT("lightning"))) return Storm;
    if (S.Contains(TEXT("seismic")) || S.Contains(TEXT("last_stand")) || S.Contains(TEXT("stone")) || S.Contains(TEXT("tremor")) || S.Contains(TEXT("totem")) || S.Contains(TEXT("ore"))) return Earth;
    if (S.Contains(TEXT("blood")) || S.Contains(TEXT("red_moon")) || S.Contains(TEXT("troll"))) return Blood;
    if (S.Contains(TEXT("grove")) || S.Contains(TEXT("dryad")) || S.Contains(TEXT("thorn")) || S.Contains(TEXT("seed")) || S.Contains(TEXT("spring"))) return Nature;
    if (S.Contains(TEXT("spectral_hunt")) || S.Contains(TEXT("spirit")) || S.Contains(TEXT("whisp")) || S.Contains(TEXT("kindred"))) return Spirit;
    if (S.Contains(TEXT("shadow")) || S.Contains(TEXT("grave")) || S.Contains(TEXT("spectral"))) return Shadow;
    if (S.Contains(TEXT("restoring")) || S.Contains(TEXT("renewal")) || S.Contains(TEXT("purify"))) return Life;
    if (S.Contains(TEXT("bastion")) || S.Contains(TEXT("sanctuary")) || S.Contains(TEXT("guard")) || S.Contains(TEXT("ashen")) || S.Contains(TEXT("protection")) || S.Contains(TEXT("oathbound")) || S.Contains(TEXT("aegis")) || S.Contains(TEXT("second_wind")) || S.Contains(TEXT("keeper")) || S.Contains(TEXT("dawn"))) return Holy;
    if (S.Contains(TEXT("arcane")) || S.Contains(TEXT("scholar")) || S.Contains(TEXT("starfall")) || S.Contains(TEXT("rift"))) return Arcane;
    return Steel;
}
// ability-vfx: one school table for champions and monsters (CireAbilityShapes), incl. Tide/Void monster schools.
int32 FamilyFor(FName Id) { return CireAbilityVFX::Enabled() ? static_cast<int32>(CireAbilityShapes::SchoolFor(Id)) : LegacyFamilyFor(Id); }
FLinearColor ColorFor(int32 Family) { return CireAbilityShapes::SchoolColor(static_cast<ECireSchool>(FMath::Clamp(Family,0,static_cast<int32>(ECireSchool::Count)-1))); }
float Fract(float N) { return N-FMath::FloorToFloat(N); }
FVector Polar(float R, float A, float Z=0) { return FVector(FMath::Cos(A)*R,FMath::Sin(A)*R,Z); }

// ability-vfx: modeled core / soft sections now live in CireSpellMesh.h (shared with telegraphs).
using FMesh=FCireSpellMesh;
using FSoftMesh=FCireSoftMesh;
bool Capacity(UWorld* World)
{
    int32 Count=0;
    for(TActorIterator<ACireSpellVisual> It(World);It;++It)
        if(!It->IsActorBeingDestroyed() && ++Count>=MaxEffects) return false;
    return true;
}
}

ACireSpellVisual::ACireSpellVisual()
{
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickGroup=TG_PostPhysics;
    bReplicates=false;
    Mesh=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ModeledSpell")); SetRootComponent(Mesh);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->SetGenerateOverlapEvents(false);
    Mesh->SetCanEverAffectNavigation(false); Mesh->SetCastShadow(false); Mesh->bUseAsyncCooking=false;
    SoftMesh=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SoftSpellVeil"));SoftMesh->SetupAttachment(Mesh);
    SoftMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);SoftMesh->SetGenerateOverlapEvents(false);
    SoftMesh->SetCanEverAffectNavigation(false);SoftMesh->SetCastShadow(false);SoftMesh->bUseAsyncCooking=false;
    // ability-vfx: flat ground layer for telegraphs, shock rings and splashes.
    GroundMesh=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SpellGround"));GroundMesh->SetupAttachment(Mesh);
    GroundMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);GroundMesh->SetGenerateOverlapEvents(false);
    GroundMesh->SetCanEverAffectNavigation(false);GroundMesh->SetCastShadow(false);GroundMesh->bUseAsyncCooking=false;
    GroundMesh->SetUsingAbsoluteScale(true);
    Light=CreateDefaultSubobject<UPointLightComponent>(TEXT("SpellAccentLight"));Light->SetupAttachment(Mesh);
    Light->SetMobility(EComponentMobility::Movable);
    Light->SetIntensityUnits(ELightUnits::Lumens);Light->SetIntensity(0);Light->SetAttenuationRadius(280);
    Light->SetCastShadows(false);Light->SetVisibility(false);
    Audio=CreateDefaultSubobject<UAudioComponent>(TEXT("SpellAudio")); Audio->SetupAttachment(Mesh);
    Audio->bAutoActivate=false; Audio->bAutoDestroy=false; Audio->bAllowSpatialization=true;
    Audio->bOverrideAttenuation=true;
    Audio->AttenuationOverrides.bAttenuate=true; Audio->AttenuationOverrides.bSpatialize=true;
    Audio->AttenuationOverrides.AttenuationShapeExtents=FVector(160,0,0);
    Audio->AttenuationOverrides.FalloffDistance=1800;
}

bool ACireSpellVisual::HasMaterial() const
{ return Mesh&&SoftMesh&&Mesh->GetMaterial(0)&&SoftMesh->GetMaterial(0)&&
    Mesh->GetMaterial(0)->GetName()==TEXT("M_SpellCore")&&SoftMesh->GetMaterial(0)->GetName()==TEXT("M_SpellSoft"); }
bool ACireSpellVisual::GeometryValid() const
{
    if(LastVertexCount<=0||LastVertexCount>MaxVertices||ScratchColors.Num()!=ScratchVertices.Num()||
       SoftColors.Num()!=SoftVertices.Num()||SoftUVs.Num()!=SoftVertices.Num()||TrailPoints.Num()>12)return false;
    for(const FVector P:ScratchVertices)if(P.ContainsNaN())return false;
    for(const FVector P:SoftVertices)if(P.ContainsNaN())return false;
    for(const FVector2D UV:SoftUVs)if(UV.ContainsNaN())return false;
    for(const int32 I:ScratchIndices)if(!ScratchVertices.IsValidIndex(I))return false;
    for(const int32 I:SoftIndices)if(!SoftVertices.IsValidIndex(I))return false;
    return true;
}

void ACireSpellVisual::Configure(FName Id,FVector From,FVector To,ECireSpellCue InCue,float InScale,bool bSound)
{
    Skill=Id; Cue=InCue; Family=FamilyFor(Id); Tint=ColorFor(Family); Size=FMath::Clamp(InScale,.1f,5.f);
    Start=From; End=To; Age=0;
    OriginPhase=CurrentPhase(GetWorld());
    Duration=Cue==ECireSpellCue::Launch?.38f:(Cue==ECireSpellCue::Impact?.65f:1.25f);
    if(Cue==ECireSpellCue::Critical) Duration=.72f;
    if(Normalize(Id).Contains(TEXT("cataclysm"))) Duration=1.65f;
    if(Normalize(Id)==TEXT("starfall"))Duration=1.55f;
    SetActorLocation(Cue==ECireSpellCue::Launch?From:To);
    SetActorRotation((To-From).GetSafeNormal2D().Rotation());
    UMaterialInterface* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Effects/CireSpellPolish01/M_SpellCore.M_SpellCore"));
    if(!Material)Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Effects/CireSpell/M_SpellGlow.M_SpellGlow"));
    if(!Material) Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/M_GroundArea.M_GroundArea"));
    Mesh->SetMaterial(0,Material);
    SoftMesh->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Effects/CireSpellPolish01/M_SpellSoft.M_SpellSoft")));
    GroundMesh->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/M_GroundArea.M_GroundArea")));
    ClassifyCue(); // ability-vfx: shape-true telegraph / flare / shock modes (may re-anchor at the caster)
    bLightGranted=false;Light->SetVisibility(false);
    if(Family!=Steel&&!bFollowArea&&Cue!=ECireSpellCue::Wall&&Cue!=ECireSpellCue::Protection)
    {
        int32 Count=0;for(TActorIterator<ACireSpellVisual> It(GetWorld());It;++It)
            if(*It!=this&&!It->IsActorBeingDestroyed()&&It->bLightGranted)++Count;
        bLightGranted=Count<MaxLights;
    }
    Rebuild();
    if(bSound){if(StartDelay>0)bSoundPending=true;else StartSound();} // ability-vfx: delayed cues sound on release
}

void ACireSpellVisual::StartSound()
{
    const FString S=Normalize(Skill);
    FString Name;
    if(Cue==ECireSpellCue::Critical) Name=TEXT("Critical");
    else if(Cue==ECireSpellCue::Impact) Name=Family==Life?TEXT("Heal"):(Family==Steel?TEXT("Impact"):TEXT("MagicImpact"));
    else if(S.Contains(TEXT("bow")) || S.Contains(TEXT("arrow")) || S.Contains(TEXT("piercing"))) Name=TEXT("Bow");
    else if(S.Contains(TEXT("lancer")) || S==TEXT("lance")) Name=TEXT("Lance");
    else if(Family==Steel) Name=TEXT("Swing");
    else if(Family==Life) Name=TEXT("Heal");
    else if(Family==Holy) Name=TEXT("Ward");
    else Name=TEXT("MagicCast");
    const FString Path=FString::Printf(TEXT("/Game/Audio/CireCombat/S_%s.S_%s"),*Name,*Name);
    auto* Sound=LoadObject<USoundWave>(nullptr,*Path);
    if(!Sound) return;
    if(auto* Concurrency=LoadObject<USoundConcurrency>(nullptr,TEXT("/Game/Audio/CireCombat/SC_Combat.SC_Combat")))
        Audio->ConcurrencySet.Add(Concurrency);
    Audio->SetSound(Sound); Audio->SetVolumeMultiplier((Cue==ECireSpellCue::Impact?.6f:.45f)*CombatVolume(GetWorld()));
    Audio->SetPitchMultiplier(.97f+Fract(GetUniqueID()*.6180339f)*.06f); Audio->Play();
}

void ACireSpellVisual::Follow(ACireAreaEffect* Area)
{
    bFollowArea=true; FollowedArea=Area; Duration=65;
    if(Area) Configure(FName(*Area->AreaSpec.AbilityName),Area->GetActorLocation(),Area->GetActorLocation(),ECireSpellCue::Cast,1,false);
    Duration=65;
}
void ACireSpellVisual::FollowActor(AActor* Actor,FName Id,ECireSpellCue InCue,FVector Bounds)
{
    if(!Actor) return;
    bFollowActor=true; FollowedActor=Actor; FollowBounds=Bounds;
    TrailPoints.Reset();TrailPoints.Add(Actor->GetActorLocation());
    Configure(Id,Actor->GetActorLocation(),Actor->GetActorLocation(),InCue,1,false);
    Duration=65; SetActorTransform(Actor->GetActorTransform());
}
void ACireSpellVisual::SetPreviewAge(float Seconds) { bPreview=true; Age=FMath::Max(0.f,Seconds); Rebuild(); }
void ACireSpellVisual::SetTint(FLinearColor Color)
{
    if(!FMath::IsFinite(Color.R)||!FMath::IsFinite(Color.G)||!FMath::IsFinite(Color.B))return;
    Tint=FLinearColor(FMath::Clamp(Color.R,0.f,8.f),FMath::Clamp(Color.G,0.f,8.f),FMath::Clamp(Color.B,0.f,8.f),1);
    Rebuild();
}
void ACireSpellVisual::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(Audio->IsPlaying())Audio->SetVolumeMultiplier((Cue==ECireSpellCue::Impact?.6f:.45f)*CombatVolume(GetWorld()));
    if(!bPreview && !bFollowArea && OriginPhase!=CurrentPhase(GetWorld())) { Destroy(); return; }
    if(!bPreview) Age+=FMath::Clamp(DeltaSeconds,0.f,.25f);
    if(TickModes(DeltaSeconds)) return; // ability-vfx: release delay, fade-out after the source ends, cancelled telegraphs
    if(bFollowArea)
    {
        ACireAreaEffect* Area=FollowedArea.Get();
        if(!IsValid(Area) || Area->IsActorBeingDestroyed()) { Destroy(); return; }
        const auto* Local=GetWorld()->GetFirstPlayerController();
        const bool bVisible=Local && Local->IsLocalController() && Area->CanObserve(Local) && !Area->IsHidden();
        SetActorHiddenInGame(!bVisible);
        Light->SetVisibility(false);
        if(!bVisible) return;
        Skill=FName(*Area->AreaSpec.AbilityName); Family=Area->AreaSpec.bPoison?Poison:FamilyFor(Skill); Tint=Area->AreaSpec.Color*1.5f;Tint.A=1;
        SetActorTransform(Area->GetActorTransform());
    }
    else if(bFollowActor)
    {
        auto* Actor=FollowedActor.Get();
        if(!IsValid(Actor) || Actor->IsActorBeingDestroyed()) { Destroy(); return; }
        SetActorTransform(Actor->GetActorTransform());
        SetActorHiddenInGame(Actor->IsHidden());
        if(IsHidden()){Light->SetVisibility(false);TrailPoints.Reset();return;}
        if(Cue==ECireSpellCue::Projectile&&(TrailPoints.IsEmpty()||FVector::DistSquared(TrailPoints.Last(),GetActorLocation())>16))
        {
            // Real authoritative actor history preserves curved/reflected paths.
            TrailPoints.Add(GetActorLocation());if(TrailPoints.Num()>12)TrailPoints.RemoveAt(0);
        }
    }
    else if(!bPreview && Age>=Duration) { Destroy(); return; }
    Rebuild();
}

void ACireSpellVisual::Rebuild()
{
    FMesh M(ScratchVertices,ScratchIndices,ScratchColors);
    FVector CameraLocation=FVector::ZeroVector;FRotator CameraRotation=FRotator::ZeroRotator;
    if(auto* PC=GetWorld()->GetFirstPlayerController())PC->GetPlayerViewPoint(CameraLocation,CameraRotation);
    const FRotationMatrix Camera(CameraRotation);
    FSoftMesh Soft(SoftVertices,SoftIndices,SoftColors,SoftUVs,
        GetActorTransform().InverseTransformVectorNoScale(Camera.GetUnitAxis(EAxis::Y)),
        GetActorTransform().InverseTransformVectorNoScale(Camera.GetUnitAxis(EAxis::Z)));
    const float T=FMath::Clamp(Age/FMath::Max(Duration,.05f),0.f,1.f);
    const float Fade=(bFollowArea||bFollowActor)?1.f:FMath::Clamp((1-T)*3.f,0.f,1.f)*FMath::Clamp(Age*25.f+.12f,0.f,1.f);
    FLinearColor Main=Tint; Main.A=Fade;
    FLinearColor Dim=Tint*.45f; Dim.A=Fade*.28f;
    FLinearColor Glow=Tint*.55f;Glow.A=Fade*.2f;
    FLinearColor Core=FMath::Lerp(Tint,FLinearColor(2.3f,2.3f,2.1f,1),.4f);Core.A=Fade*.85f;
    const FString Id=Normalize(Skill);
    const float Expand=1-FMath::Square(1-T);
    if(RebuildModes(M,Soft,T,Fade,Expand)) {} // ability-vfx: telegraphs, projectiles, impacts, flares
    else if(bFollowArea && FollowedArea.IsValid())
    {
        auto* Area=FollowedArea.Get(); const auto& Spec=Area->AreaSpec;
        const bool Active=Area->IsActive(); const auto Boundary=ACireAreaEffect::BoundaryPoints(Spec);
        FLinearColor Edge=Active?Tint:FLinearColor(1.8f,.52f,.06f,1);
        Edge.A=Active?.65f:(.22f+.2f*FMath::Sin(Age*8));
        const float InnerScale=.93f;
        for(int32 I=0;I<Boundary.Num();++I)
        {
            const FVector A(Boundary[I].X*InnerScale,Boundary[I].Y*InnerScale,5);
            const auto B2=Boundary[(I+1)%Boundary.Num()]; const FVector B(B2.X*InnerScale,B2.Y*InnerScale,5);
            if(I%2==0) M.Tube(A,B,1.4f,Edge,3);
            // Deliberately thin inner ticks preserve the authoritative outline.
            if(!Active&&I%4==0)M.Tube(A*.97f,A,.75f,Edge,3);
        }
        if(Active)
        {
            FVector2D Lo(MAX_flt,MAX_flt),Hi(-MAX_flt,-MAX_flt);
            for(auto P:Boundary) { Lo.X=FMath::Min(Lo.X,P.X);Lo.Y=FMath::Min(Lo.Y,P.Y);Hi.X=FMath::Max(Hi.X,P.X);Hi.Y=FMath::Max(Hi.Y,P.Y); }
            int32 Spawned=0;
            for(int32 I=0;I<80 && Spawned<18;++I)
            {
                const FVector P(FMath::Lerp(Lo.X,Hi.X,Fract(I*.618034f+.12f)),FMath::Lerp(Lo.Y,Hi.Y,Fract(I*.414214f+.29f)),0);
                if(!ACireAreaEffect::ContainsPoint(Spec,FVector::ZeroVector,FRotator::ZeroRotator,P)) continue;
                ++Spawned; const float Cycle=Fract(Age*(Family==Fire?1.1f:.45f)+I*.17f);
                FLinearColor C=Tint; C.A=FMath::Sin(Cycle*PI)*.42f;
                FLinearColor Halo=C;Halo.A*=.3f;Soft.Glow(P+FVector(0,0,10+Cycle*50),Family==Fire?16:9,Halo);
                if(Family==Frost) M.Shard(P+FVector(0,0,8),8,55,C,I);
                else if(Family==Fire) M.Shard(P+FVector(0,0,5+Cycle*55),9*(1-Cycle),35*(1-Cycle)+10,C,I);
                else
                {
                    const FVector Rise=P+FVector(FMath::Sin(Cycle*PI*2+I)*12,FMath::Cos(Cycle*PI*2+I)*12,7+Cycle*65);
                    M.Shard(Rise,5+Cycle*3,10,C,I);
                    if(Spawned%3==0) M.Rune(P+FVector(0,0,6),10,Age*.2f+I,C);
                }
            }
        }
    }
    else if(Cue==ECireSpellCue::Projectile)
    {
        const float R=FMath::Clamp(float(FollowBounds.X),4.f,80.f);
        // A faceted head and tapered wake follow actual authoritative movement;
        // no interpolation towards a target and no invisible homing here.
        M.Tube(FVector(-R,0,0),FVector(R*1.7f,0,0),R*.36f,Core,6);
        if(Family!=Steel)Soft.Glow(FVector(R*.4f,0,0),R*2.1f,Glow);
        if(Family==Frost)
            for(int32 I=0;I<4;++I)M.Shard(FVector(0,FMath::Cos(I*PI*.5f)*R*.5f,FMath::Sin(I*PI*.5f)*R*.5f),R*.18f,R*.9f,Core,I);
        for(int32 I=0;I<4;++I)
        {
            const float A=I*PI*.5f+Age*5;
            const FVector Tip(R*2.5f,0,0),Tail(-R,FMath::Cos(A)*R*.6f,FMath::Sin(A)*R*.6f);
            M.Tri(Tip,Tail,Tail+FVector(-R,0,0),Core);
        }
        for(int32 I=0;I<8;++I)
        {
            const float U=I/8.f;
            const float A=Age*7+I*1.7f;
            FLinearColor C=Tint; C.A=(1-U)*.58f;
            const FVector P(-R*(1.7f+U*7),FMath::Cos(A)*R*(.1f+U*.45f),FMath::Sin(A)*R*(.1f+U*.45f));
            M.Shard(P,R*.15f*(1-U)+1,R*.4f,C,A);
            if(I<7) M.Tube(P,P+FVector(-R*.7f,0,0),R*.09f*(1-U)+.4f,C,4);
        }
        for(int32 I=1;I<TrailPoints.Num();++I)
        {
            const float U=I/float(TrailPoints.Num());FLinearColor Trail=Tint;Trail.A=U*.22f;
            const FVector A=GetActorTransform().InverseTransformPosition(TrailPoints[I-1]),B=GetActorTransform().InverseTransformPosition(TrailPoints[I]);
            Soft.Ribbon(A,B,R*(.2f+U*.55f),Trail);M.Tube(A,B,R*.055f*U,Trail,3);
        }
    }
    else if(Cue==ECireSpellCue::Wall)
    {
        FLinearColor C=Tint;C.A=.8f;
        const float X=FMath::Max(5.f,float(FollowBounds.X))+1,Y=FMath::Max(15.f,float(FollowBounds.Y)),Z=FMath::Max(20.f,float(FollowBounds.Z));
        for(int32 Side=-1;Side<=1;Side+=2)
        {
            // Local XY wall convention supplied by gameplay. Glowing seams sit
            // just above both long faces; physical block remains opaque stone.
            for(int32 I=0;I<7;++I)
            {
                const float Offset=-Y+2*Y*(I+.5f)/7;
                const FVector A(Side*X,Offset,-Z*.7f),B(Side*X,Offset+7,Z*.7f);
                M.Tube(A,B,1.1f,C,3);
                M.Tube(FVector(Side*X,Offset-12,0),FVector(Side*X,Offset+12,Z*.17f),1.4f,C,3);
            }
            M.Tube(FVector(Side*X,-Y,Z*.76f),FVector(Side*X,Y,Z*.76f),1.8f,C,4);
            M.Tube(FVector(Side*X,-Y,-Z*.75f),FVector(Side*X,Y,-Z*.75f),1.8f,C,4);
        }
    }
    else if(Cue==ECireSpellCue::Protection)
    {
        const float R=FMath::Max(25.f,float(FollowBounds.X)),H=FMath::Max(35.f,float(FollowBounds.Z));
        FLinearColor C=Tint,D=Tint*.25f;C.A=.65f;D.A=.15f;
        for(int32 Row=0;Row<5;++Row)
        {
            const float Z=-H+2*H*Row/5.f;
            M.Ring(R*(.95f+.04f*FMath::Sin(Age*1.5f+Row)),2.2f,Z,C,Age*.12f+Row,PI*1.88f,42);
        }
        for(int32 J=0;J<12;++J)
        {
            const float A=J*PI/6;
            M.Tube(Polar(R,A,-H),Polar(R,A,H),1.4f,C,3);
            M.Quad(Polar(R,A,-H),Polar(R,A,H),Polar(R,A+PI/6,H),Polar(R,A+PI/6,-H),D);
        }
    }
    else if(Cue==ECireSpellCue::Critical)
    {
        FLinearColor C(3.f,1.2f,.05f,Fade);
        const float H=22*(.7f+FMath::Sin(T*PI)*.3f);
        M.Shard(FVector(0,0,14+T*18),H*.27f,H,C,PI*.25f);
        for(int32 I=0;I<6;++I) M.Tube(Polar(18+Expand*5,I*PI/3,20),Polar(26+Expand*10,I*PI/3,20),1.5f,C,4);
        Soft.Glow(FVector(0,0,24),38,Glow);
    }
    else if(Cue==ECireSpellCue::Launch)
    {
        const float R=25+Expand*40;
        M.Ring(R,5,10,Main,-PI*.35f,PI*.7f,18);
        for(int32 I=0;I<5;++I) M.Shard(FVector(Expand*(25+I*9),FMath::Sin(I*2.4f)*12,20+FMath::Cos(static_cast<float>(I))*9),2.5f,7,Main,I);
        if(Family!=Steel)Soft.Glow(FVector(12,0,20),35,Glow);
    }
    else if(Cue==ECireSpellCue::Impact)
    {
        const float R=12+Expand*56;M.Star(FVector::ZeroVector,18*(1-T)+5,Core,Age*.8f);
        Soft.Glow(FVector::ZeroVector,40+Expand*30,Glow);
        for(int32 I=0;I<10;++I)
        {
            const float A=I*2.39996f;const FVector P=Polar(R,A,15*FMath::Sin(A)+Expand*24);
            M.Tube(P,P+Polar((1-T)*13+3,A,7),Family==Steel?.8f:1.3f,Main,4);
            if(Family==Frost||Family==Earth)M.Shard(P,4*(1-T)+1,13,Dim,A);
            if(I%2==0)Soft.Glow(P,8*(1-T)+3,Glow);
        }
    }
    else if(Id==TEXT("starfall"))
    {
        M.Sigil(90,-70,-Age*.12f,Dim);
        for(int32 I=0;I<6;++I)
        {
            const float A=I*2.39996f,U=Fract(Age*.95f+I*.14f);const FVector P=Polar(20+I*10,A,190-U*250);
            M.Star(P,8+I%3,Core,A);M.Tube(P,P+FVector(-7,0,42),1.2f,Main,4);
            Soft.Ribbon(P,P+FVector(-12,0,95),10,Glow);Soft.Glow(P,18,Glow);
        }
        M.Ring(12+Expand*80,2,-68,Main);
    }
    else if(Id==TEXT("mass_aegis"))
    {
        FLinearColor Blue(.26f,.85f,1.65f,Fade*.75f);M.Sigil(95,-70,Age*.08f,Blue);
        for(int32 I=0;I<6;++I)
        {
            const float A=I*PI/3+Age*.18f;const FVector P=Polar(70,A,8),Side=Polar(18,A+PI/2);
            M.Tube(P-Side+FVector(0,0,24),P+Side+FVector(0,0,24),1.8f,Blue,4);
            M.Tube(P-Side+FVector(0,0,24),P-Side,1.8f,Blue,4);M.Tube(P+Side+FVector(0,0,24),P+Side,1.8f,Blue,4);
            M.Tube(P-Side,P-FVector(0,0,22),1.8f,Blue,4);M.Tube(P+Side,P-FVector(0,0,22),1.8f,Blue,4);
            M.Star(P+FVector(0,0,4),9,Core,A);FLinearColor H=Blue;H.A=.08f*Fade;Soft.Glow(P,26,H);
        }
    }
    else if(Family==Earth)
    {
        const float Radius=Id==TEXT("seismic_reprisal")?120:75;
        M.Ring(20+Expand*Radius,2.3f,-68,Main);M.Ring(28+Expand*Radius,5,-69,Dim);
        for(int32 I=0;I<9;++I)
        {
            const float A=I*2.39996f;const FVector P=Polar(Radius*.7f,A,-66);
            FLinearColor Rock=Tint*.2f;Rock.A=Fade*.72f;
            M.Shard(P,10+I%3*3,(25+I%4*8)*FMath::Sin(T*PI),Rock,A);
            FVector Previous=Polar(8,A,-67);
            for(int32 J=1;J<5;++J)
            {const FVector Next=Polar(Radius*J*.22f,A+FMath::Sin(I+J*3.5f)*.09f,-67);M.Tube(Previous,Next,.9f,Main,3);Previous=Next;}
            Soft.Glow(P+FVector(0,0,10),18,Glow);
        }
    }
    else if(Family==Nature)
    {
        M.Sigil(68,-71,-Age*.16f,Dim);
        for(int32 I=0;I<7;++I)
        {
            const float A=I*2*PI/7;FVector Previous=Polar(12,A,-68);
            for(int32 J=1;J<13;++J)
            {
                const float U=J/12.f;const FVector Next=Polar(12+FMath::Sin(U*PI)*48,A+U*.3f,-68+U*120);
                M.Tube(Previous,Next,1.2f,Main,4);Previous=Next;
                if(J==6||J==9)M.Shard(Next+Polar(4,A),4,14,Main,A);
            }
            const float U=Fract(Age*.6f+I*.17f);const FVector Mote=Polar(34,A+Age*.4f,-48+U*130);
            Soft.Glow(Mote,10,Glow);M.Star(Mote,3,Core,A);
        }
        Soft.Glow(FVector(0,0,-8),35,Glow);
    }
    else if(Family==Spirit)
    {
        M.Sigil(70,-70,Age*.22f,Dim);
        const int32 Count=Id==TEXT("spectral_hunt")?3:5;
        for(int32 I=0;I<Count;++I)
        {
            const float A=I*PI*2/Count+Age*.65f;const FVector P=Polar(45,A,15+FMath::Sin(Age*2+I)*22);
            M.Shard(P,6,19,Core,A);Soft.Glow(P,25,Glow);
            FVector Previous=P;
            for(int32 J=1;J<14;++J)
            {
                const float U=J/13.f;const FVector Next=P+Polar(U*30,A+U*3,-U*65);
                FLinearColor Tail=Main;Tail.A*=(1-U)*.5f;
                M.Tube(Previous,Next,2.1f*(1-U)+.3f,Tail,4);Soft.Ribbon(Previous,Next,5*(1-U)+1,Tail*.3f);Previous=Next;
            }
        }
    }
    else if(Family==Blood)
    {
        M.Ring(75,1.5f,-70,Dim);
        for(int32 I=0;I<3;++I)
        {
            const float A=Age*(I%2?2:-2)+I*PI*.7f;
            M.Ring(35+I*18,6-I,5+I*12,Main,A,PI*1.15f,30);
            M.Ring(36+I*18,.8f,6+I*12,Core,A,PI,30);
        }
        for(int32 I=0;I<8;++I){const FVector P=Polar(50,I*2.4f,Fract(Age+I*.2f)*85-40);M.Shard(P,2,9,Main,I);Soft.Glow(P,8,Glow);}
    }
    else if(Family==Storm && Cue==ECireSpellCue::Cast)
    {
        const FVector A=GetActorTransform().InverseTransformPosition(Start),B=GetActorTransform().InverseTransformPosition(End);
        for(int32 Branch=0;Branch<2;++Branch)
        {
            FVector Prev=A;
            for(int32 J=1;J<=14;++J)
            {
                const float U=J/14.f;
                FVector P=FMath::Lerp(A,B,U);
                if(J<14) P+=FVector(0,FMath::Sin(J*7.21f+Branch*2)*26,FMath::Cos(J*4.91f+Branch)*23);
                M.Tube(Prev,P,Branch?1.1f:3.f,Branch?Dim:Main,4); Prev=P;
                if(Branch==0&&J%3==0)Soft.Glow(P,12,Glow);
            }
        }
        M.Ring(22+Expand*52,3,2,Main);
    }
    else if(Family==Frost)
    {
        M.Ring(68+Expand*26,4,-65,Main);
        M.Ring(76+Expand*30,2,-61,Dim);
        for(int32 I=0;I<8;++I)
        {
            const float A=I*PI*.25f;
            M.Shard(Polar(58,A,-66),9,55+25*FMath::Sin(I*1.7f),Main,A);
            M.Tube(Polar(40,A,-60),Polar(18,A,25),1.2f,Dim);
        }
    }
    else if(Family==Life || Family==Holy || Family==Arcane)
    {
        const float R=Family==Holy?92:65;
        M.Sigil(R,-74,Age*.1f,Main);
        M.Helix(R*.72f,165,Age*2.4f,Main,Family==Life?3:2);
        if(Family==Holy)
            for(int32 I=0;I<6;++I)
            {
                const float A=I*PI/3+Age*.4f;
                for(int32 J=0;J<12;++J)
                    M.Tube(Polar(R*FMath::Cos(J*PI/24),A,J*8-65),Polar(R*FMath::Cos((J+1)*PI/24),A,(J+1)*8-65),1.4f,Dim,3);
            }
        for(int32 I=0;I<9;++I)
        { const float U=Fract(Age*.55f+I*.13f);const FVector P=Polar(R*.6f,I*2.4f,U*130-55);
            M.Star(P,3.5f,Core,I);Soft.Glow(P,9,Glow); }
        Soft.Glow(FVector(0,0,8),42,Glow);
    }
    else if(Family==Fire)
    {
        const bool Ultimate=Normalize(Skill).Contains(TEXT("cataclysm"));
        const float R=Ultimate?140.f:65.f;
        M.Ring(15+Expand*R,7*(1-T)+2,-55,Main);
        M.Ring(30+Expand*(R+20),11,-52,Dim);
        for(int32 I=0;I<11;++I)
        {
            const float A=I*2.39996f+Age*.7f;
            const float U=Fract(T*1.3f+I*.09f);
            M.Shard(Polar((R*.5f)*Expand,A,-50+U*90),12*(1-U)+2,70*(1-U)+5,Main,A);
            M.Shard(Polar(R*.8f*Expand,A+1,U*120-35),2.5f,7,Main,A);
            FLinearColor Heat=Glow;Heat.A*=.6f;Soft.Glow(Polar(R*.45f*Expand,A,-25+U*80),20*(1-U)+8,Heat);
        }
        M.Helix(R*.3f,90,Age*3,Dim,2);
    }
    else if(Family==Shadow || Family==Poison)
    {
        M.Ring(75+Expand*30,8,-70,Main,Age*2,PI*1.6f,40);
        M.Ring(55+Expand*25,3,-66,Dim,-Age*3,PI*1.5f,36);
        M.Helix(48,145,Age*-3,Main,3);
        for(int32 I=0;I<7;++I) M.Shard(Polar(50+Expand*40,I*2.4f,T*100-25),4,17,Dim,I);
    }
    else
    {
        const bool Cry=Id.Contains(TEXT("war_cry"))||Id.Contains(TEXT("challenge_of_iron"));
        if(Cry)
        { M.Ring(40+Expand*190,5,-64,Main); M.Ring(30+Expand*155,12,-62,Dim); }
        else
        {
            // Forged-metal crescent, a bright inner edge and dim broad trail.
            M.Ring(60+Expand*40,12,0,Dim,-PI*.65f+T*PI*.5f,PI*1.35f,28);
            M.Ring(60+Expand*40,2.5f,1,Main,-PI*.65f+T*PI*.5f,PI*1.35f,28);
            for(int32 I=0;I<9;++I)
            {
                const FVector P=Polar(20+Expand*(25+I*5),I*2.4f,15+Expand*(I*8-25));
                M.Tube(P,P+Polar(7,I*2.4f,4),1.3f,Main,3);
            }
        }
    }
    if(!bFollowArea && Size!=1){for(auto& V:M.V)V*=Size;for(auto& V:Soft.V)V*=Size;}
    LastVertexCount=M.V.Num()+Soft.V.Num();
    RebuildGround(T,Fade); // ability-vfx
    // All indices are sequential triangles. A stable topology can update its
    // existing render buffers rather than recreating a scene proxy every frame.
    const auto* Section=Mesh->GetProcMeshSection(0);
    if(Section&&Section->ProcVertexBuffer.Num()==M.V.Num()&&Section->ProcIndexBuffer.Num()==M.I.Num())
        Mesh->UpdateMeshSection_LinearColor(0,M.V,TArray<FVector>(),TArray<FVector2D>(),M.C,TArray<FProcMeshTangent>(),false);
    else Mesh->CreateMeshSection_LinearColor(0,M.V,M.I,TArray<FVector>(),TArray<FVector2D>(),M.C,TArray<FProcMeshTangent>(),false);
    const auto* Veil=SoftMesh->GetProcMeshSection(0);
    if(Soft.V.IsEmpty())SoftMesh->ClearMeshSection(0);
    else if(Veil&&Veil->ProcVertexBuffer.Num()==Soft.V.Num()&&Veil->ProcIndexBuffer.Num()==Soft.I.Num())
        SoftMesh->UpdateMeshSection_LinearColor(0,Soft.V,TArray<FVector>(),Soft.UV,Soft.C,TArray<FProcMeshTangent>(),false);
    else SoftMesh->CreateMeshSection_LinearColor(0,Soft.V,Soft.I,TArray<FVector>(),Soft.UV,Soft.C,TArray<FProcMeshTangent>(),false);
    const bool bLit=bLightGranted&&!IsHidden()&&Fade>.01f;
    Light->SetVisibility(bLit);Light->SetLightColor(Tint.GetClamped(0,1));
    Light->SetIntensity(bLit?(Family==Fire?85.f:45.f)*Fade*(.92f+.08f*FMath::Sin(Age*9)):0);
    Light->SetAttenuationRadius(FMath::Clamp(280*Size,140.f,600.f));
    UpdateFabVFX(); // fab-integration
}

// fab-integration: one Niagara overlay per presentation, chosen by school and role from Content/Data/FabVFX.json.
// Warnings (enemy lanes, gathers, void zones, area wind-ups) never get one: telegraphs stay the dim procedural fill.
bool ACireSpellVisual::HasFabVFX() const { return FabFX.IsValid(); }
void ACireSpellVisual::UpdateFabVFX()
{
    if(bFabTried||IsHidden()||Age<0||!CireFabVFX::Enabled())return;
    CireFabVFX::ERole FabRole=CireFabVFX::ERole::Count;
    bool bAttach=true,bLoop=false;float Extra=1.f;
    switch(Mode)
    {
    case EMode::Projectile: FabRole=CireFabVFX::ERole::Projectile;bLoop=true;break;
    case EMode::Impact: FabRole=CireFabVFX::ERole::Impact;bAttach=false;break;
    case EMode::TargetMark: FabRole=Shape.bHeal?CireFabVFX::ERole::Cast:CireFabVFX::ERole::Impact;bAttach=false;break;
    case EMode::CasterFlare: case EMode::SelfShock: case EMode::Channel: FabRole=CireFabVFX::ERole::Cast;break;
    case EMode::AreaFollow:
        if(const ACireAreaEffect* Area=FollowedArea.Get();Area&&Area->IsActive()&&!bHarmlessArea)
        {FabRole=CireFabVFX::ERole::Area;bLoop=Area->AreaSpec.bPersistent;Extra=FMath::Clamp(Area->AreaSpec.Radius/200.f,.4f,3.f);}
        else return; // still winding up: keep trying until the zone goes live
        break;
    default:
        if(Mode==EMode::Legacy&&Cue==ECireSpellCue::Critical){FabRole=CireFabVFX::ERole::Impact;bAttach=false;break;}
        bFabTried=true;return;
    }
    bFabTried=true;
    const ECireSchool School=Shape.bHeal?ECireSchool::Life:static_cast<ECireSchool>(FMath::Clamp(Family,0,static_cast<int32>(ECireSchool::Count)-1));
    const CireFabVFX::FEntry* Entry=CireFabVFX::Find(School,FabRole);
    UNiagaraSystem* System=CireFabVFX::Resolve(Entry);
    if(!System){UE_LOG(LogTemp,Verbose,TEXT("CIRE_FAB_VFX_NONE skill=%s role=%s"),*Skill.ToString(),*CireFabVFX::RoleName(FabRole));return;} // pack not installed: the procedural presentation carries the cue alone
    const float Scale=Entry->Scale*Extra*(bFollowArea?1.f:Size);
    UNiagaraComponent* C=bAttach?CireFabVFX::SpawnAttached(System,Mesh,FVector::ZeroVector,Scale,!bLoop)
        :CireFabVFX::SpawnAt(GetWorld(),System,GetActorLocation(),GetActorRotation(),Scale);
    CireFabVFX::ApplyTint(C,Entry->Tint);
    FabFX=C;
    UE_LOG(LogTemp,Verbose,TEXT("CIRE_FAB_VFX_SPAWN skill=%s role=%s school=%s system=%s ok=%d"),*Skill.ToString(),*CireFabVFX::RoleName(FabRole),
        *CireAbilityShapes::SchoolName(School),*System->GetName(),C!=nullptr);
}

ACireSpellVisual* CireSpellPresentation::Play(UWorld* World,FName SkillId,FVector From,FVector To,ECireSpellCue Cue,float Scale,bool bSound)
{
    // aura-vfx: monster melee cues carry no source actor; the aura system finds an empowered attacker at From.
    if(World&&World->GetNetMode()!=NM_DedicatedServer&&Cue==ECireSpellCue::Impact&&!From.ContainsNaN()&&!To.ContainsNaN())
        if(auto* Auras=CireAuraVisuals::Get(World))Auras->NotifyAttackCue(SkillId,From,To);
    if(!World || World->GetNetMode()==NM_DedicatedServer || From.ContainsNaN() || To.ContainsNaN() ||
        !FMath::IsFinite(Scale) || Scale<=0 || static_cast<uint8>(Cue)>static_cast<uint8>(ECireSpellCue::Protection) || !Capacity(World)) return nullptr;
    FActorSpawnParameters P; P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Visual=World->SpawnActor<ACireSpellVisual>(To,FRotator::ZeroRotator,P);
    if(Visual) Visual->Configure(SkillId,From,To,Cue,Scale,bSound);
    if(Visual) Visual->SetStartDelay(ReleaseDelay(World,SkillId,Cue,From)); // ability-vfx: appear on the clip's release frame
    return Visual;
}
ACireSpellVisual* CireSpellPresentation::FollowArea(ACireAreaEffect* Area)
{
    if(!IsValid(Area) || Area->GetNetMode()==NM_DedicatedServer || !Capacity(Area->GetWorld())) return nullptr;
    for(TActorIterator<ACireSpellVisual> It(Area->GetWorld());It;++It)
        if(It->GetOwner()==Area && !It->IsActorBeingDestroyed()) return *It;
    FActorSpawnParameters P; P.Owner=Area;
    P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Visual=Area->GetWorld()->SpawnActor<ACireSpellVisual>(Area->GetActorLocation(),Area->GetActorRotation(),P);
    if(Visual) { Visual->Follow(Area); Visual->SetActorHiddenInGame(true); Visual->Tick(0); }
    return Visual;
}
ACireSpellVisual* CireSpellPresentation::AttachProjectile(AActor* Projectile,FName SkillId,float Radius)
{
    if(!IsValid(Projectile) || Projectile->GetNetMode()==NM_DedicatedServer || !FMath::IsFinite(Radius) || Radius<=0) return nullptr;
    auto* Visual=Play(Projectile->GetWorld(),SkillId,Projectile->GetActorLocation(),Projectile->GetActorLocation(),ECireSpellCue::Projectile,1,false);
    if(Visual) { Visual->SetOwner(Projectile); Visual->FollowActor(Projectile,SkillId,ECireSpellCue::Projectile,FVector(Radius)); Visual->SetActorHiddenInGame(Projectile->IsHidden()); }
    return Visual;
}
ACireSpellVisual* CireSpellPresentation::AttachConstruct(AActor* Construct,bool bProtection,FVector HalfExtents)
{
    if(!IsValid(Construct) || Construct->GetNetMode()==NM_DedicatedServer || HalfExtents.ContainsNaN()) return nullptr;
    const ECireSpellCue Cue=bProtection?ECireSpellCue::Protection:ECireSpellCue::Wall;
    const FName Id(bProtection?TEXT("protection_dome"):TEXT("runic_wall"));
    auto* Visual=Play(Construct->GetWorld(),Id,Construct->GetActorLocation(),Construct->GetActorLocation(),Cue,1,false);
    if(Visual) { Visual->SetOwner(Construct); Visual->FollowActor(Construct,Id,Cue,HalfExtents); Visual->SetActorHiddenInGame(Construct->IsHidden()); }
    return Visual;
}
bool CireSpellPresentation::IsSupported(FName SkillId)
{
    static const TSet<FString> Names={TEXT("iron_guard"),TEXT("shield_slam"),TEXT("war_cry"),TEXT("chain_spark"),TEXT("ember_lance"),
        TEXT("frost_bind"),TEXT("cleaving_strike"),TEXT("piercing_shot"),TEXT("shadow_step"),TEXT("restoring_light"),TEXT("sanctuary"),
        TEXT("purify"),TEXT("bastion_of_dawn"),TEXT("cataclysm"),TEXT("executioners_verdict"),TEXT("renewal"),
        TEXT("venom_ground"),TEXT("cinder_cone"),TEXT("grave_line"),TEXT("ashen_ward"),TEXT("ashen_square"),TEXT("blight_sigil"),
        TEXT("sword"),TEXT("bow"),TEXT("lance"),TEXT("arcane"),TEXT("runic_wall"),TEXT("protection_dome"),
        TEXT("npc_shadow_bolt"),TEXT("npc_barbed_shot"),TEXT("npc_bruiser_slam"),TEXT("npc_blight_pool"),
        TEXT("oathbound_guardian"),TEXT("spectral_pack"),TEXT("second_wind"),TEXT("last_stand"),TEXT("challenge_of_iron"),
        TEXT("seismic_reprisal"),TEXT("starfall"),TEXT("spectral_hunt"),TEXT("mass_aegis"),TEXT("wellspring")};
    const FString Normalized=Normalize(SkillId);if(Names.Contains(Normalized))return true;
    for(const auto& Profile:CireChampionRoster::All())
    {
        if(Normalized==Profile.Passive.Id||Normalized==Profile.Ultimate.Id)return true;
        for(const auto& Ability:Profile.Actives)if(Normalized==Ability.Id)return true;
    }
    return false;
}

#if !UE_BUILD_SHIPPING
bool CireSpellPresentation::RunSmoke(UWorld* World)
{
    bool bPass=true; int32 Checked=0;
    const TCHAR* Ids[]={TEXT("iron_guard"),TEXT("shield_slam"),TEXT("war_cry"),TEXT("chain_spark"),TEXT("ember_lance"),TEXT("frost_bind"),
        TEXT("cleaving_strike"),TEXT("piercing_shot"),TEXT("shadow_step"),TEXT("restoring_light"),TEXT("sanctuary"),TEXT("purify"),
        TEXT("bastion_of_dawn"),TEXT("cataclysm"),TEXT("executioners_verdict"),TEXT("renewal"),TEXT("venom_ground"),TEXT("cinder_cone"),
        TEXT("grave_line"),TEXT("ashen_square"),TEXT("blight_sigil"),TEXT("second_wind"),TEXT("last_stand"),
        TEXT("challenge_of_iron"),TEXT("seismic_reprisal"),TEXT("starfall"),TEXT("spectral_hunt"),TEXT("mass_aegis"),TEXT("wellspring")};
    for(auto* Id:Ids)
    {
        auto* V=Play(World,FName(Id),FVector(0,0,100),FVector(200,0,100),ECireSpellCue::Cast,1,false);
        if(!V) { bPass=false; continue; }
        V->SetPreviewAge(.35f);
        const bool bOk = IsSupported(FName(Id)) && V->VertexCount()>0 && V->VertexCount()<=MaxVertices &&
            V->Mesh->GetCollisionEnabled()==ECollisionEnabled::NoCollision &&
            V->SoftMesh->GetCollisionEnabled()==ECollisionEnabled::NoCollision && V->HasMaterial()&&V->GeometryValid();
        if(!bOk)UE_LOG(LogTemp,Error,TEXT("CIRE_SPELL_PRESENTATION_CASE_FAIL %s supported=%d vertices=%d material=%d geometry=%d"),Id,IsSupported(FName(Id)),V->VertexCount(),V->HasMaterial(),V->GeometryValid()); // ability-vfx
        bPass &= bOk;
        ++Checked; V->Destroy();
    }
    const FVector Invalid(std::numeric_limits<double>::quiet_NaN(),0,0);
    bPass &= Play(World,TEXT("bow"),Invalid,FVector::ZeroVector)==nullptr;
    bPass &= Play(World,TEXT("bow"),FVector::ZeroVector,FVector::ZeroVector,ECireSpellCue::Cast,-1)==nullptr;
    // ability-vfx: schools come from the Ability Database (champion-draft) when it lists the ability.
    bPass &= FamilyFor(TEXT("Starfall"))==Arcane&&FamilyFor(TEXT("Seismic Reprisal"))==Earth;
    for(const TCHAR* Id:{TEXT("spectral_hunt"),TEXT("wellspring"),TEXT("starfall"),TEXT("seismic_reprisal")})
        bPass &= FamilyFor(FName(Id))==static_cast<int32>(CireAbilityShapes::SchoolFor(FName(Id)));
    UE_LOG(LogTemp,Display,TEXT("CIRE_SPELL_PRESENTATION_%s families=13 skills=%d bounded_vertices=%d light_budget=%d"),bPass?TEXT("PASS"):TEXT("FAIL"),Checked,MaxVertices,MaxLights);
    return bPass;
}
#endif
