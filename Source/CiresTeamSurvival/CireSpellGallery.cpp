#include "CireSpellGallery.h"
#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireAreaEffects.h"
#include "CireSpellPresentation.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "CireFabVFX.h" // telegraphs: Fab ground-effect page
#include "CireAbilityVFX.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "HAL/IConsoleManager.h"

namespace
{
struct FGallery
{
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACireController> PC;
    TWeakObjectPtr<ACireHero> Source;
    TWeakObjectPtr<ACameraActor> Camera;
    TWeakObjectPtr<AStaticMeshActor> Floor;
    TArray<TWeakObjectPtr<AActor>> PageActors;
    TArray<TWeakObjectPtr<ACireSpellVisual>> Visuals;
    TArray<FString> Files;
    FString Directory;
    double Start=0,Ready=-1,PageAt=-1;
    int32 Page=-1;
    bool bDone=false,bCaptured=false,bChecks=true;
    bool bFabGround=false; // telegraphs: -CireFabGroundGallery (two pages: stock vendor ground systems / fitted + dimmed)
    int32 FabFitted=0,FabSkippedNotRound=0,FabDimmed=0,PageFrames=0,CaptureFrames=0;double CapturedAt=0;
    TArray<TWeakObjectPtr<UFXSystemComponent>> FabComponents;
    TArray<float> FabSpawnScale,FabMaxReach; // page 0: stock spawn scale and the largest XY reach seen (footprint measurement)
};
FGallery G;
const FVector Stage(0,0,8000);
void Finish(bool Pass)
{
    if(G.bDone) return; G.bDone=true;
    UE_LOG(LogTemp,Display,TEXT("CIRE_SPELL_GALLERY_%s captures=%d directory=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),G.Files.Num(),*G.Directory);
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
void Label(UWorld* World,const FString& Name,FVector Position,float Font=18)
{
    auto* Text=World->SpawnActor<ATextRenderActor>(Position,FRotator::ZeroRotator);
    if(!Text) { G.bChecks=false; return; }
    Text->SetActorRotation((G.Camera->GetActorLocation()-Position).Rotation());
    Text->GetTextRender()->SetText(FText::FromString(Name));
    Text->GetTextRender()->SetWorldSize(Font);
    Text->GetTextRender()->SetTextRenderColor(FColor(219,203,174));
    Text->GetTextRender()->SetHorizontalAlignment(EHTA_Center);
    G.PageActors.Add(Text);
}
FVector Cell(int32 I) { return Stage+FVector(360-(I/3)*360,-420+(I%3)*420,85); }
bool Setup(ACireGameMode* Mode,ACireController* PC)
{
    UWorld* World=Mode->GetWorld(); G.PC=PC;
    if(auto* H=Cast<ACireHero>(PC->GetPawn()))
    {
        H->bDrafted=true; H->TeamId=0; H->SetActorHiddenInGame(true); H->SetActorEnableCollision(false); H->SetActorTickEnabled(false);
    }
    PC->SetIgnoreMoveInput(true); PC->SetIgnoreLookInput(true); PC->bShowMouseCursor=false;
    if(PC->GetHUD()) PC->GetHUD()->bShowHUD=false;
    G.Source=World->SpawnActor<ACireHero>(Stage+FVector(-1600,0,100),FRotator::ZeroRotator);
    if(!G.Source.IsValid()) return false;
    G.Source->bDrafted=true; G.Source->TeamId=0; G.Source->bBot=false; G.Source->SetActorHiddenInGame(true);
    G.Source->SetActorEnableCollision(false); G.Source->SetActorTickEnabled(false); G.Source->GetCharacterMovement()->DisableMovement();
    auto* Floor=World->SpawnActor<AStaticMeshActor>(Stage-FVector(0,0,15),FRotator::ZeroRotator);
    if(!Floor) return false;
    Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
    Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    if(!Floor->GetStaticMeshComponent()->GetStaticMesh())return false;
    G.Floor=Floor;
    Floor->SetActorScale3D(G.bFabGround?FVector(70,50,.25):FVector(22,20,.25));
    Floor->GetStaticMeshComponent()->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Effects/CireSpell/M_Runestone.M_Runestone")));
    auto* Light=World->SpawnActor<ADirectionalLight>(Stage+FVector(400,0,2000),FRotator(-48,-120,0));
    if(!Light) return false;
    Light->GetLightComponent()->SetIntensity(4); Light->GetLightComponent()->SetLightColor(FLinearColor(.8f,.86f,1.f));
    G.Camera=World->SpawnActor<ACameraActor>(G.bFabGround?Stage+FVector(1150,0,3300):Stage+FVector(1750,0,1600),FRotator::ZeroRotator);
    if(!G.Camera.IsValid()) return false;
    G.Camera->SetActorRotation((Stage+FVector(G.bFabGround?-330:0,0,40)-G.Camera->GetActorLocation()).Rotation());
    auto* Camera=G.Camera->GetCameraComponent(); Camera->SetFieldOfView(G.bFabGround?62:44); Camera->SetAspectRatio(16.f/9.f); Camera->bConstrainAspectRatio=true;
    auto& Post=Camera->PostProcessSettings;
    Post.bOverride_AutoExposureMethod=true; Post.AutoExposureMethod=EAutoExposureMethod::AEM_Manual;
    Post.bOverride_AutoExposureApplyPhysicalCameraExposure=true; Post.AutoExposureApplyPhysicalCameraExposure=false;
    Post.bOverride_AutoExposureBias=true; Post.AutoExposureBias=.5f;
    Post.bOverride_BloomIntensity=true; Post.BloomIntensity=.45f;
    Post.bOverride_MotionBlurAmount=true; Post.MotionBlurAmount=0;
    PC->SetViewTarget(G.Camera.Get()); G.Ready=FPlatformTime::Seconds();
    G.bChecks &= CireSpellPresentation::RunSmoke(World);
    return true;
}
void ClearPage()
{
    for(auto V:G.PageActors) if(V.IsValid()) V->Destroy();
    for(auto V:G.Visuals) if(V.IsValid()) V->Destroy();
    for(int32 J=0;J<G.FabComponents.Num();++J)
        if(G.FabComponents[J].IsValid()||G.FabMaxReach[J]>0)
        {
            // Footprint at scale 1 for FabVFX.json "groundRadius" (largest reach over the page / the stock spawn scale).
            const UFXSystemComponent* Cmp=G.FabComponents[J].Get();
            UE_LOG(LogTemp,Display,TEXT("CIRE_FAB_GROUND_MEASURE system=%s radius=%.0f"),Cmp&&Cmp->GetFXSystemAsset()?*Cmp->GetFXSystemAsset()->GetPathName():TEXT("?"),
                G.FabMaxReach[J]/FMath::Max(.01f,G.FabSpawnScale[J]));
        }
    for(auto C:G.FabComponents) if(C.IsValid()) C->DestroyComponent();
    G.PageActors.Reset(); G.Visuals.Reset(); G.FabComponents.Reset(); G.FabSpawnScale.Reset(); G.FabMaxReach.Reset();
}
const TCHAR* FabSchools[]={TEXT("arcane"),TEXT("blood"),TEXT("stone"),TEXT("fire"),TEXT("frost"),TEXT("holy"),TEXT("renewal"),TEXT("grove"),
    TEXT("poison"),TEXT("shadow"),TEXT("storm"),TEXT("tide"),TEXT("void")};
FVector FabCell(int32 I){return Stage+FVector(700-(I/5)*1000,-1800+(I%5)*900,0);}
constexpr float FabRadius=250.f;
// telegraphs: page 0 = the stock vendor ground system as the game spawned it before (Radius/200 scale, undimmed) beside the
// procedural zone; page 1 = the same zones through the real presentation (circle-only, fitted inside the rim, dimmed).
// Page 2: every other school area candidate (second, third choices), stock, for the footprint measurement.
TArray<FString> FabExtraCandidates()
{
    TArray<FString> Out;TSet<FString> First;
    for(int32 I=0;I<static_cast<int32>(ECireSchool::Count);++I)
        if(const CireFabVFX::FEntry* E=CireFabVFX::Find(static_cast<ECireSchool>(I),CireFabVFX::ERole::Area))
            if(E->Candidates.Num())First.Add(E->Candidates[0]);
    for(int32 I=0;I<static_cast<int32>(ECireSchool::Count);++I)
        if(const CireFabVFX::FEntry* E=CireFabVFX::Find(static_cast<ECireSchool>(I),CireFabVFX::ERole::Area))
            for(int32 J=1;J<E->Candidates.Num();++J)if(!First.Contains(E->Candidates[J]))Out.AddUnique(E->Candidates[J]);
    return Out;
}
void MakeFabCandidatePage()
{
    UWorld* World=G.Mode->GetWorld();const TArray<FString> Paths=FabExtraCandidates();
    for(int32 I=0;I<Paths.Num()&&I<15;++I)
    {
        CireFabVFX::FEntry One;One.Candidates.Add(Paths[I]);
        UNiagaraSystem* N=Cast<UNiagaraSystem>(CireFabVFX::Resolve(&One));const FVector P=FabCell(I);
        if(N)if(auto* C=UNiagaraFunctionLibrary::SpawnSystemAtLocation(World,N,P+FVector(0,0,1),FRotator::ZeroRotator,FVector(1.f),true,true,ENCPoolMethod::None,true))
            {G.FabComponents.Add(C);G.FabSpawnScale.Add(1.f);G.FabMaxReach.Add(0.f);}
        Label(World,N?N->GetName():Paths[I],P+FVector(-FabRadius-90,0,20),26);
    }
    Label(World,TEXT("FAB GROUND OVERLAYS / OTHER AREA CANDIDATES AT SCALE 1 (FOOTPRINT MEASUREMENT)"),Stage+FVector(1100,0,60),40);
}
void MakeFabPage(int32 Page)
{
    UWorld* World=G.Mode->GetWorld();
    if(Page==2){MakeFabCandidatePage();return;}
    IConsoleVariable* FabCVar=IConsoleManager::Get().FindConsoleVariable(TEXT("cire.FabVFX"));
    if(FabCVar)FabCVar->Set(Page==0?0:1,ECVF_SetByCode); // page 0: the follower draws only the procedural zone
    for(int32 I=0;I<UE_ARRAY_COUNT(FabSchools);++I)
    {
        FCireAreaSpec S;S.Shape=ECireAreaShape::Circle;S.Radius=FabRadius;S.AbilityName=FabSchools[I];S.WarningSeconds=0;S.DurationSeconds=30;
        S.bPersistent=true;S.bPoison=false;S.TickInterval=.5f;S.DamagePerSecond=1;S.BurstDamage=0;S.Color=CireAbilityShapes::SchoolColor(CireAbilityShapes::SchoolFor(FName(FabSchools[I])));S.Color.A=.22f;
        const FVector P=FabCell(I);
        auto* A=ACireAreaEffect::Spawn(G.Source.Get(),S,P,FRotator::ZeroRotator);
        if(!A){G.bChecks=false;continue;}
        G.PageActors.Add(A);if(auto* V=CireSpellPresentation::FollowArea(A))G.Visuals.Add(V);else G.bChecks=false;
        const ECireSchool School=CireAbilityShapes::SchoolFor(FName(FabSchools[I]));
        FString SystemName=TEXT("(no pack)");
        if(const CireFabVFX::FEntry* E=CireFabVFX::Find(School,CireFabVFX::ERole::Area))
            if(UFXSystemAsset* Sys=CireFabVFX::Resolve(E))
            {
                SystemName=Sys->GetName();
                if(Page==0)if(UNiagaraSystem* N=Cast<UNiagaraSystem>(Sys))
                {
                    // The pre-fix path: uniform Radius/200 scale, stock colours.
                    if(auto* C=UNiagaraFunctionLibrary::SpawnSystemAtLocation(World,N,P+FVector(0,0,1),FRotator::ZeroRotator,FVector(E->Scale*FMath::Clamp(FabRadius/200.f,.4f,3.f)),true,true,ENCPoolMethod::None,true))
                        {G.FabComponents.Add(C);G.FabSpawnScale.Add(E->Scale*FMath::Clamp(FabRadius/200.f,.4f,3.f));G.FabMaxReach.Add(0.f);}
                }
                if(Page==1&&!CireFabVFX::IsGroundOverlay(Sys))++G.FabSkippedNotRound;
            }
        Label(World,FString::Printf(TEXT("%s / %s"),*CireAbilityShapes::SchoolName(School).ToUpper(),*SystemName),P+FVector(-FabRadius-90,0,20),26);
    }
    Label(World,Page==0?TEXT("FAB GROUND OVERLAYS / BEFORE: STOCK VENDOR SYSTEM (RADIUS/200 SCALE, UNDIMMED)"):
        TEXT("FAB GROUND OVERLAYS / AFTER: CIRCLE ZONES ONLY, FITTED INSIDE THE RIM, DIMMED WITH THE SLIDER"),Stage+FVector(1100,0,60),40);
    UE_LOG(LogTemp,Display,TEXT("CIRE_FAB_GROUND_GALLERY_PAGE index=%d zones=%d"),Page,G.Visuals.Num());
}
void MakePage(int32 Page)
{
    ClearPage(); UWorld* World=G.Mode->GetWorld(); G.Page=Page; G.PageAt=FPlatformTime::Seconds(); G.bCaptured=false; G.PageFrames=0;
    if(G.bFabGround){MakeFabPage(Page);G.PageAt=FPlatformTime::Seconds();return;}
    const TCHAR* Pages[][9]={
        {TEXT("iron_guard"),TEXT("shield_slam"),TEXT("war_cry"),TEXT("chain_spark"),TEXT("ember_lance"),TEXT("frost_bind"),TEXT("cleaving_strike"),TEXT("piercing_shot"),TEXT("shadow_step")},
        {TEXT("restoring_light"),TEXT("sanctuary"),TEXT("purify"),TEXT("bastion_of_dawn"),TEXT("cataclysm"),TEXT("executioners_verdict"),TEXT("renewal"),TEXT("oathbound_guardian"),TEXT("spectral_pack")},
        {TEXT("venom_ground"),TEXT("cinder_cone"),TEXT("grave_line"),TEXT("ashen_square"),TEXT("blight_sigil"),TEXT("npc_shadow_bolt"),TEXT("npc_barbed_shot"),TEXT("npc_bruiser_slam"),TEXT("npc_blight_pool")}};
    if(Page<3)
    {
        for(int32 I=0;I<9;++I)
        {
            const FVector P=Cell(I);
            auto* V=CireSpellPresentation::Play(World,FName(Pages[Page][I]),P-FVector(90,0,0),P,ECireSpellCue::Cast,.85f,false);
            if(!V) { G.bChecks=false; continue; }
            V->SetPreviewAge(.36f); G.Visuals.Add(V);
            Label(World,FString(Pages[Page][I]).Replace(TEXT("_"),TEXT(" ")),P+FVector(120,0,-66),15);
        }
    }
    else if(Page<5)
    {
        const TCHAR* Names[]={TEXT("Venom Ground"),TEXT("Cinder Cone"),TEXT("Grave Line"),TEXT("Ashen Ward"),TEXT("Blight Sigil")};
        const FLinearColor Colors[]={FLinearColor(.28f,.8f,.12f,.22f),FLinearColor(1.f,.21f,.04f,.20f),FLinearColor(.46f,.14f,.8f,.20f),FLinearColor(.94f,.62f,.18f,.20f),FLinearColor(.44f,.68f,.13f,.20f)};
        for(int32 I=0;I<5;++I)
        {
            FCireAreaSpec S; S.Shape=I==3?ECireAreaShape::Circle:static_cast<ECireAreaShape>(I); S.AbilityName=Names[I]; // telegraphs: Ashen Ward is a circle now
            S.Color=Colors[I];
            S.Radius=110; S.Width=150; S.Length=220; S.WarningSeconds=Page==3?10:0; S.DurationSeconds=8;
            S.DamagePerSecond=0; S.BurstDamage=0; S.bPoison=I==0||I==4;
            if(I==4) S.CustomPolygon={FVector2D(-90,-85),FVector2D(90,-85),FVector2D(90,-15),FVector2D(15,-15),FVector2D(15,85),FVector2D(-90,85)};
            const FVector P=Cell(I)+FVector(I==1||I==2?-90:0,0,-83);
            auto* A=ACireAreaEffect::Spawn(G.Source.Get(),S,P,FRotator(0,0,0));
            if(!A) { G.bChecks=false; continue; }
            G.PageActors.Add(A); auto* V=CireSpellPresentation::FollowArea(A);
            if(V) G.Visuals.Add(V); else G.bChecks=false;
            Label(World,FString(Names[I])+(Page==3?TEXT(" / WARNING"):TEXT(" / ACTIVE")),Cell(I)+FVector(120,0,-65),14);
        }
    }
    else if(Page==5)
    {
        const ECireSpellCue Cues[]={ECireSpellCue::Projectile,ECireSpellCue::Projectile,ECireSpellCue::Projectile,
            ECireSpellCue::Critical,ECireSpellCue::Impact,ECireSpellCue::Impact};
        const TCHAR* Ids[]={TEXT("ember_lance"),TEXT("piercing_shot"),TEXT("npc_shadow_bolt"),TEXT("sword"),TEXT("sword"),TEXT("restoring_light")};
        for(int32 I=0;I<6;++I)
        {
            const FVector P=Cell(I);
            auto* V=CireSpellPresentation::Play(World,FName(Ids[I]),P-FVector(60,0,0),P,Cues[I],1,false);
            if(V) { V->SetPreviewAge(.2f); G.Visuals.Add(V); } else G.bChecks=false;
            Label(World,I==3?TEXT("CRITICAL STRIKE"):FString(Ids[I]).Replace(TEXT("_"),TEXT(" ")),P+FVector(120,0,-66),15);
        }
        for(int32 I=6;I<8;++I)
        {
            auto* Body=World->SpawnActor<AStaticMeshActor>(Cell(I),FRotator::ZeroRotator);
            if(!Body) { G.bChecks=false; continue; }
            Body->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
            Body->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            G.bChecks &= Body->GetStaticMeshComponent()->GetStaticMesh()!=nullptr;
            Body->GetStaticMeshComponent()->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Effects/CireSpell/M_Runestone.M_Runestone")));
            Body->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            if(I==6) Body->SetActorScale3D(FVector(.44,1.84,1.32));
            else Body->GetStaticMeshComponent()->SetVisibility(false);
            auto* Visual=CireSpellPresentation::AttachConstruct(Body,I==7,I==6?FVector(50):FVector(86,86,70));
            if(Visual) G.Visuals.Add(Visual); else G.bChecks=false;
            G.PageActors.Add(Body);
            Label(World,I==6?TEXT("RUNIC WALL TRIM"):TEXT("PROTECTION CAGE"),Cell(I)+FVector(120,0,-66),15);
        }
    }
    else
    {
        const TCHAR* Ids[]={TEXT("second_wind"),TEXT("last_stand"),TEXT("challenge_of_iron"),TEXT("seismic_reprisal"),
            TEXT("starfall"),TEXT("spectral_hunt"),TEXT("mass_aegis"),TEXT("wellspring")};
        for(int32 I=0;I<UE_ARRAY_COUNT(Ids);++I)
        {
            const FVector P=Cell(I);auto* V=CireSpellPresentation::Play(World,FName(Ids[I]),P-FVector(90,0,0),P,ECireSpellCue::Cast,.85f,false);
            if(V){V->SetPreviewAge(.36f);G.Visuals.Add(V);}else G.bChecks=false;
            Label(World,FString(Ids[I]).Replace(TEXT("_"),TEXT(" ")),P+FVector(120,0,-66),15);
        }
    }
    Label(World,FString::Printf(TEXT("CIRE'S TEAM SURVIVAL / NATIVE SPELL PRESENTATION %d"),Page+1),Stage+FVector(-630,0,160),23);
    UE_LOG(LogTemp,Display,TEXT("CIRE_SPELL_GALLERY_PAGE index=%d visuals=%d"),Page,G.Visuals.Num());
}
void Capture()
{
    int32 W=0,H=0; G.PC->GetViewportSize(W,H); G.bChecks &= W>=1280 && H>=720;
    G.bChecks &= G.Floor.IsValid() && G.Floor->GetStaticMeshComponent()->GetStaticMesh()!=nullptr;
    for(auto V:G.Visuals)
    {
        bool Valid=V.IsValid() && V->VertexCount()>0 && V->VertexCount()<=8192 && V->HasMaterial() && V->GeometryValid() && !V->IsHidden();
        FVector2D Pixel;
        if(V.IsValid()) Valid &= G.PC->ProjectWorldLocationToScreen(V->GetActorLocation(),Pixel) && Pixel.X>10 && Pixel.X<W-10 && Pixel.Y>10 && Pixel.Y<H-10;
        G.bChecks &= Valid;
    }
    if(G.bFabGround&&G.Page==1)
        for(auto V:G.Visuals)if(V.IsValid()&&V->HasFabGroundOverlay())
        {
            // Fitted overlays sit inside the true radius (the procedural rim stays the outermost line).
            ++G.FabFitted;
        }
    const FString File=FPaths::Combine(G.Directory,G.bFabGround?FString::Printf(TEXT("%02d_fab_ground_%s.png"),G.Page+1,G.Page==0?TEXT("before_stock"):G.Page==1?TEXT("after_fitted"):TEXT("candidates")):
        FString::Printf(TEXT("%02d_%s.png"),G.Page+1,G.Page==3?TEXT("telegraphs"):G.Page==4?TEXT("ground_active"):TEXT("modeled_skills")));
    FScreenshotRequest::RequestScreenshot(File,false,false,false,FIntRect(),true); G.Files.Add(File); G.bCaptured=true;
}
}
bool CireSpellGallery::Initialize(ACireGameMode* Mode)
{
    G={}; if(!FParse::Param(FCommandLine::Get(),TEXT("CireSpellGallery"))) return false;
    G.bFabGround=FParse::Param(FCommandLine::Get(),TEXT("CireFabGroundGallery")); // telegraphs
    G.Mode=Mode; G.Start=FPlatformTime::Seconds();
    G.Directory=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("SpellGallery"),FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));
    if(!Mode || Mode->GetNetMode()!=NM_Standalone || !IFileManager::Get().MakeDirectory(*G.Directory,true)) { Finish(false); return true; }
    Mode->bBotsFilled=true; Mode->BotFillTimer=MAX_flt; Mode->WaveTimer=MAX_flt; return true;
}
bool CireSpellGallery::Tick(ACireGameMode* Mode)
{
    if(G.Mode.Get()!=Mode) return false;
    if(G.bDone) return true;
    // telegraphs: a cold start (asset registry / shaders after a rebuild) can exceed a minute before the first page; the
    // 60 s bound applies from the moment the stage is ready.
    if(FPlatformTime::Seconds()-(G.Ready>0?G.Ready:G.Start)>(G.Ready>0?60:180)) { Finish(false); return true; }
    if(G.Ready<0)
    {
        auto* PC=Cast<ACireController>(Mode->GetWorld()->GetFirstPlayerController());
        if(PC && PC->GetPawn() && PC->GetHUD() && !Setup(Mode,PC)) Finish(false);
        return true;
    }
    if(G.Page<0) { if(FPlatformTime::Seconds()-G.Ready>3) MakePage(0); return true; }
    const double Age=FPlatformTime::Seconds()-G.PageAt;
    // telegraphs: a hitch (first Niagara load) can make one frame longer than the whole page; count frames too so the
    // screenshot is taken after the page has rendered and the page is not replaced before the screenshot resolves.
    ++G.PageFrames;
    for(int32 J=0;J<G.FabComponents.Num();++J)if(G.FabComponents[J].IsValid())G.FabMaxReach[J]=FMath::Max(G.FabMaxReach[J],CireFabVFX::MeasureReach(G.FabComponents[J].Get()));
    if(Age>2 && G.PageFrames>20 && !G.bCaptured) { Capture(); G.CapturedAt=FPlatformTime::Seconds(); G.CaptureFrames=0; }
    if(G.bCaptured) ++G.CaptureFrames;
    if(Age>3 && G.bCaptured && G.CaptureFrames>5 && FPlatformTime::Seconds()-G.CapturedAt>.5)
    {
        const int32 Pages=G.bFabGround?3:7;
        if(G.Page<Pages-1) MakePage(G.Page+1);
        else
        {
            for(const auto& File:G.Files) G.bChecks &= IFileManager::Get().FileSize(*File)>10000;
            if(G.bFabGround)
            {
                ClearPage(); // logs the last page's footprint measurements
                if(IConsoleVariable* FabCVar=IConsoleManager::Get().FindConsoleVariable(TEXT("cire.FabVFX")))FabCVar->Set(1,ECVF_SetByCode);
                UE_LOG(LogTemp,Display,TEXT("CIRE_FAB_GROUND_GALLERY fitted=%d skipped_not_round=%d"),G.FabFitted,G.FabSkippedNotRound);
            }
            Finish(G.bChecks && G.Files.Num()==Pages);
        }
    }
    return true;
}
#endif
