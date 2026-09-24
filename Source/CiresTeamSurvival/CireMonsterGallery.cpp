#include "CireMonsterGallery.h"

#include "CireGame.h"
#include "CireLanePath.h"
#include "CireMonsterArt.h"
#include "CireChampionActions.h"
#include "CireChampionArt.h"
#include "CireNPCArchetypes.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireMonsterGallery, Log, All);

namespace
{
struct FStage { FString Name; float Settle = 3.f; bool bKeepScene = false; };
struct FGallery
{
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACireController> Controller;
    TWeakObjectPtr<ACameraActor> Camera;
    TArray<TWeakObjectPtr<AActor>> Scene;
    TArray<TWeakObjectPtr<ACireMonster>> Wave;
    TWeakObjectPtr<ACireHero> Hero;
    struct FChampion { TWeakObjectPtr<ACireHero> Hero; FString Clip; float Phase = 1.f; };
    TArray<FChampion> Champions;
    TArray<FStage> Stages;
    TArray<FString> Captures, Only;
    FString Directory;
    FVector Studio = FVector(0, -2100, 9000);
    FVector TownForward = FVector(1, 0, 0);
    double Started = 0, StageStarted = 0;
    int32 Stage = -1;
    bool bCaptured = false, bDone = false, bPass = true, bBuilt = false;
};
FGallery G;

void Fail(const FString& Why) { G.bPass = false; UE_LOG(LogCireMonsterGallery, Error, TEXT("CIRE_MONSTER_GALLERY_CHECK_FAIL %s"), *Why); }
void Finish()
{
    if (G.bDone) return;
    G.bDone = true;
    for (const FString& File : G.Captures) if (IFileManager::Get().FileSize(*File) < 1024) Fail(TEXT("capture not written: ") + File);
    UE_LOG(LogCireMonsterGallery, Display, TEXT("CIRE_MONSTER_GALLERY_%s captures=%d directory=%s"), G.bPass ? TEXT("PASS") : TEXT("FAIL"), G.Captures.Num(), *G.Directory);
    FPlatformMisc::RequestExitWithStatus(false, G.bPass ? 0 : 1);
}

UWorld* World() { return G.Mode.IsValid() ? G.Mode->GetWorld() : nullptr; }

void ClearScene()
{
    for (auto& Actor : G.Scene) if (Actor.IsValid()) Actor->Destroy();
    G.Scene.Reset(); G.Wave.Reset(); G.Champions.Reset();
    for (TActorIterator<ACireMonsterCorpse> It(World()); It; ++It) It->Destroy();
    if (G.Mode.IsValid()) G.Mode->Monsters.RemoveAll([](ACireMonster* M) { return !IsValid(M) || M->IsActorBeingDestroyed(); });
}

void Label(const FVector& At, const FString& Text, const FColor& Color, float Size, float Yaw)
{
    if (auto* L = World()->SpawnActor<ATextRenderActor>(At, FRotator(0, Yaw, 0)))
    {
        L->GetTextRender()->SetText(FText::FromString(Text)); L->GetTextRender()->SetWorldSize(Size);
        L->GetTextRender()->SetTextRenderColor(Color); L->GetTextRender()->SetHorizontalAlignment(EHTA_Center);
        G.Scene.Add(L);
    }
}

void Look(const FVector& Eye, const FVector& Target, float Fov = 50.f)
{
    if (!G.Camera.IsValid()) return;
    G.Camera->SetActorLocation(Eye); G.Camera->SetActorRotation((Target - Eye).Rotation());
    G.Camera->GetCameraComponent()->SetFieldOfView(Fov);
}

float FloorZ(const FVector& P)
{
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CireMonsterGalleryFloor), false);
    for (const auto& Actor : G.Scene) if (Actor.IsValid() && Actor->IsA<ACharacter>()) Query.AddIgnoredActor(Actor.Get());
    return World()->LineTraceSingleByChannel(Hit, P + FVector(0, 0, 2000), P - FVector(0, 0, 4000), ECC_Visibility, Query) ? Hit.ImpactPoint.Z : P.Z;
}

const FCireNPCArchetype* Arch(const TCHAR* Id) { return CireNPCArchetypes::Find(FName(Id)); }

/** A posed, non-thinking monster standing on the floor at At. */
ACireMonster* Spawn(FName Id, int32 Variant, const FVector& At, float Yaw, int32 Tier = 0)
{
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* M = World()->SpawnActor<ACireMonster>(At + FVector(0, 0, 200), FRotator(0, Yaw, 0), Params);
    if (!M) { Fail(TEXT("spawn ") + Id.ToString()); return nullptr; }
    G.Scene.Add(M);
    M->Lane = 0; M->SetActorTickEnabled(false); M->GetCharacterMovement()->DisableMovement();
    if (M->MonsterArt) M->MonsterArt->ForceVariant(Variant);
    CireNPCCombat::ConfigureArchetype(M, Id, 3, Tier, 1);
    const float Floor = FloorZ(At);
    M->SetActorLocation(FVector(At.X, At.Y, Floor + M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    M->SetActorRotation(FRotator(0, Yaw, 0));
    M->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    if (!M->MonsterArt || !M->MonsterArt->IsTripoApplied()) Fail(TEXT("Tripo body missing for ") + Id.ToString());
    return M;
}

void Pose(ACireMonster* M, const FString& Clip, float Seconds)
{
    if (M && M->MonsterArt && !M->MonsterArt->PoseClip(Clip, Seconds)) Fail(FString::Printf(TEXT("pose %s %s@%.2f"), *M->GetNPCDisplayName(), *Clip, Seconds));
}

struct FEntry { const TCHAR* Id; int32 Variant; };
const FEntry Everyone[] = {
    {TEXT("hollow_infantry"), 0}, {TEXT("hollow_infantry"), 1}, {TEXT("ironbound_bruiser"), 0}, {TEXT("hollow_shieldbearer"), 0},
    {TEXT("gravemaw_pack_leader"), 0}, {TEXT("blight_caster"), 0}, {TEXT("blight_caster"), 1}, {TEXT("barbed_hunter"), 0},
    {TEXT("barbed_hunter"), 1}, {TEXT("hollow_siegebreaker"), 0}};

FString VariantName(const ACireMonster* M) { return M && M->MonsterArt ? M->MonsterArt->GetAppliedVariant() : FString(TEXT("?")); }

void BuildStudio()
{
    UWorld* W = World();
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Floor = W->SpawnActor<AStaticMeshActor>(G.Studio, FRotator::ZeroRotator, Params);
    if (Floor)
    {
        Floor->SetMobility(EComponentMobility::Movable);
        auto* Mesh = Floor->GetStaticMeshComponent();
        Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")));
        UMaterialInterface* Cobble = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Environment/Town/Materials/MI_TownW_Cobble.MI_TownW_Cobble"));
        if (!Cobble) Cobble = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/M_Stone.M_Stone"));
        Mesh->SetMaterial(0, Cobble);
        Floor->SetActorScale3D(FVector(90, 90, 1));
        Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Mesh->SetCollisionResponseToAllChannels(ECR_Block);
    }
    if (auto* Sun = W->SpawnActor<ADirectionalLight>(G.Studio + FVector(0, 0, 800), FRotator(-38, 150, 0)))
    {
        Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable); Sun->GetLightComponent()->SetIntensity(4.5f);
    }
}

// ---- stages -----------------------------------------------------------------------------------
void LineUp(int32 First, int32 Count, const FString& Title)
{
    const FVector C = G.Studio;
    for (int32 I = 0; I < Count; ++I)
    {
        const FEntry& E = Everyone[First + I];
        const FVector At = C + FVector(0, (I - (Count - 1) * .5f) * 330.f, 0);
        ACireMonster* M = Spawn(E.Id, E.Variant, At, 0);
        if (!M) continue;
        const float Top = M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2 + 40;
        Label(At + FVector(0, 0, Top), VariantName(M), FColor(246, 219, 155), 16, 0);
    }
    Label(C + FVector(0, 0, 420), Title, FColor::White, 26, 0);
    Look(C + FVector(1750, 0, 330), C + FVector(0, 0, 135), 50);
}

void Pack()
{
    const auto& D = CireNPCArchetypes::Get();
    const FVector C = G.Studio;
    const int32 Members = D.PackMembers.Num();
    for (int32 I = 0; I <= Members; ++I)
    {
        const bool bLeader = I == Members;
        const FVector At = C + (bLeader ? FVector(-120, 0, 0) : FVector(260, (I - (Members - 1) * .5f) * 300.f, 0));
        if (ACireMonster* M = Spawn(bLeader ? D.PackLeader : D.PackMembers[I], 0, At, 0, 2))
            Label(At + FVector(0, 0, M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2 + 40), M->GetNPCDisplayName(),
                bLeader ? FColor(255, 200, 60) : FColor::White, bLeader ? 20.f : 14.f, 0);
    }
    Look(C + FVector(1500, -900, 620), C + FVector(0, 0, 140), 52);
}

void Attack(const TCHAR* Id)
{
    const FCireNPCArchetype* A = Arch(Id);
    const float Scale = A ? A->Scale : 1.f;
    const FVector C = G.Studio;
    const float Spacing = 175.f * FMath::Max(1.f, Scale);
    TArray<ACireMonster*> Bodies;
    for (int32 I = 0; I < 4; ++I) Bodies.Add(Spawn(FName(Id), 0, C + FVector(0, (I - 1.5f) * Spacing, 0), 0));
    if (Bodies.Contains(nullptr)) return;
    UCireMonsterArt* Art = Bodies[0]->MonsterArt;
    const CireMonsterArt::FClipWindow W = Art->WindowOf(TEXT("attack"));
    Pose(Bodies[0], TEXT("attack"), W.Start + .6f * (W.Contact - W.Start));
    Pose(Bodies[1], TEXT("attack"), W.Contact);
    Pose(Bodies[2], TEXT("attack"), W.Contact + .45f * (W.End - W.Contact));
    FString Special = TEXT("hit");
    if (FCString::Strcmp(Id, TEXT("gravemaw_pack_leader")) == 0) Special = TEXT("war_cry");
    else if (FCString::Strcmp(Id, TEXT("hollow_siegebreaker")) == 0) Special = TEXT("ground_slam");
    const CireMonsterArt::FClipWindow S = Art->WindowOf(Special);
    Pose(Bodies[3], Special, S.Contact);
    const TCHAR* Titles[] = {TEXT("windup"), TEXT("contact"), TEXT("follow-through"), nullptr};
    for (int32 I = 0; I < 4; ++I)
        Label(Bodies[I]->GetActorLocation() + FVector(0, 0, Bodies[I]->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 40),
            I < 3 ? FString(Titles[I]) : Special, FColor(246, 219, 155), 14.f * FMath::Max(1.f, Scale * .8f), 0);
    Label(C + FVector(0, 0, 300 * Scale + 120), (A ? A->DisplayName : FString(Id)) + TEXT("  |  ") + VariantName(Bodies[0]), FColor::White, 24.f * FMath::Max(1.f, Scale * .8f), 0);
    Look(C + FVector(820, -560, 230) * FMath::Max(1.f, Scale * .9f), C + FVector(0, 0, 105 * Scale), 50);
}

void Variants()
{
    const FVector C = G.Studio;
    const FEntry List[] = {{TEXT("hollow_infantry"), 1}, {TEXT("blight_caster"), 1}, {TEXT("barbed_hunter"), 1}, {TEXT("barbed_hunter"), 0}, {TEXT("blight_caster"), 0}};
    for (int32 I = 0; I < UE_ARRAY_COUNT(List); ++I)
    {
        const FVector At = C + FVector(0, (I - 2.f) * 320.f, 0);
        ACireMonster* M = Spawn(List[I].Id, List[I].Variant, At, 0);
        if (!M) continue;
        const CireMonsterArt::FClipWindow W = M->MonsterArt->WindowOf(TEXT("attack"));
        Pose(M, TEXT("attack"), W.Contact);
        Label(At + FVector(0, 0, M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2 + 40), VariantName(M) + TEXT(" @ release"), FColor(246, 219, 155), 15, 0);
    }
    Look(C + FVector(1350, -700, 360), C + FVector(0, 0, 120), 55);
}

void Locomotion()
{
    const FVector C = G.Studio;
    for (int32 Row = 0; Row < 2; ++Row)
        for (int32 I = 0; I < 4; ++I)
        {
            const FVector At = C + FVector((I - 1.5f) * 230.f, Row * -360.f, 0);
            ACireMonster* M = Spawn(TEXT("hollow_infantry"), Row, At, 0);
            if (!M) continue;
            const FString Clip = Row == 0 ? TEXT("walk") : TEXT("run");
            if (!M->MonsterArt->PoseForTest(Clip, I / 4.f)) Fail(TEXT("locomotion pose ") + Clip);
            Label(At + FVector(0, 0, 225), FString::Printf(TEXT("%s %d%%"), *Clip, I * 25), FColor(246, 219, 155), 13, -90);
        }
    Look(C + FVector(0, 1500, 260), C + FVector(0, -180, 100), 55);
}

void Deaths()
{
    const FVector C = G.Studio;
    for (int32 I = 0; I < 10; ++I)
    {
        const bool bFallen = I < 5;
        const FVector At = C + FVector(bFallen ? 0.f : -520.f, ((I % 5) - 2.f) * 360.f, 0);
        ACireMonster* M = Spawn(Everyone[I].Id, Everyone[I].Variant, At, 0);
        if (!M) continue;
        Pose(M, TEXT("death"), bFallen ? 3.f : 1.05f);
        Label(At + FVector(bFallen ? 160 : 0, 0, bFallen ? 90 : 240), VariantName(M) + (bFallen ? TEXT(" (fallen)") : TEXT(" (falling)")), FColor(246, 219, 155), 13, 0);
    }
    Look(C + FVector(1500, -500, 900), C + FVector(-250, 0, 40), 55);
}

void Champions()
{
    const FVector C = G.Studio;
    struct FEntry { const TCHAR* Profile; const TCHAR* Clip; float Phase; const TCHAR* Title; };
    const FEntry List[] = {
        {TEXT("knight"), TEXT("slash"), 1.f, TEXT("Warden: slash @ release")},
        {TEXT("ranger"), TEXT("attack_bow"), 1.f, TEXT("Ranger: bow @ release")},
        {TEXT("scholar"), TEXT("cast_a_spell"), 1.f, TEXT("Scholar: cast @ release")},
        {TEXT("orc_chieftain"), TEXT("slash"), .7f, TEXT("Orc Chieftain: slash windup")},
        {TEXT("drakish_footman"), TEXT("war_cry"), 1.f, TEXT("Drakish Footman: war cry")},
        {TEXT("wizard"), TEXT("cast_a_spell"), 1.f, TEXT("Wizard: cast @ release")}};
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    for (int32 I = 0; I < UE_ARRAY_COUNT(List); ++I)
    {
        const FVector At = C + FVector(0, (I - 2.5f) * 300.f, 0);
        auto* H = World()->SpawnActor<ACireHero>(At + FVector(0, 0, 200), FRotator::ZeroRotator, Params);
        if (!H) { Fail(TEXT("spawn champion")); continue; }
        G.Scene.Add(H);
        H->TeamId = 0;
        if (!H->DraftProfile(List[I].Profile)) Fail(FString(TEXT("draft ")) + List[I].Profile);
        H->SetActorTickEnabled(false); H->GetCharacterMovement()->DisableMovement();
        H->SetActorLocation(FVector(At.X, At.Y, FloorZ(At) + H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1));
        H->SetActorRotation(FRotator(0, 0, 0));
        H->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        G.Champions.Add({H, List[I].Clip, List[I].Phase});
        Label(At + FVector(0, 0, 250), List[I].Title, FColor(246, 219, 155), 14, 0);
    }
    Look(C + FVector(1900, -700, 380), C + FVector(0, 0, 110), 50);
}

void TownWave()
{
    UWorld* W = World();
    ClearScene();
    const FVector Hold = CireLanePath::PointAlongRoute(W, 0, .56f);
    const FVector Ahead = CireLanePath::PointAlongRoute(W, 0, .58f);
    G.TownForward = (Ahead - Hold).GetSafeNormal2D();
    if (G.TownForward.IsNearlyZero()) G.TownForward = FVector(-1, 0, 0);
    const FVector Right = FVector::CrossProduct(FVector::UpVector, G.TownForward);
    if (auto* Hero = Cast<ACireHero>(G.Controller->GetPawn()))
    {
        G.Hero = Hero;
        Hero->SetActorHiddenInGame(false); Hero->SetActorEnableCollision(true);
        Hero->Health = Hero->MaxHealth = 1.e6f;
        Hero->SetActorLocation(FVector(Hold.X, Hold.Y, FloorZ(Hold) + Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2));
        Hero->SetActorRotation((-G.TownForward).Rotation());
        Hero->GetCharacterMovement()->StopMovementImmediately();
    }
    const TCHAR* Ids[] = {TEXT("hollow_infantry"), TEXT("hollow_infantry"), TEXT("ironbound_bruiser"), TEXT("hollow_shieldbearer"),
        TEXT("hollow_infantry"), TEXT("blight_caster"), TEXT("barbed_hunter"), TEXT("hollow_siegebreaker")};
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    for (int32 I = 0; I < UE_ARRAY_COUNT(Ids); ++I)
    {
        const FVector Base = CireLanePath::PointAlongRoute(W, 0, .50f - (I / 3) * .012f);
        const FVector At = Base + Right * ((I % 3) - 1.f) * 150.f;
        auto* M = W->SpawnActor<ACireMonster>(FVector(At.X, At.Y, FloorZ(At) + 140), G.TownForward.Rotation(), Params);
        if (!M) continue;
        G.Scene.Add(M); G.Wave.Add(M);
        M->Lane = 0; G.Mode->Monsters.Add(M);
        CireNPCCombat::ConfigureArchetype(M, FName(Ids[I]), 4, 0, 1, I == 7);
        M->SetActorLocation(FVector(At.X, At.Y, FloorZ(At) + M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2));
        if (!M->MonsterArt || !M->MonsterArt->IsTripoApplied()) Fail(TEXT("wave body missing ") + FString(Ids[I]));
    }
}

void GameplayCamera(float Boom, float Pitch)
{
    if (!G.Hero.IsValid()) return;
    const FVector Pivot = G.Hero->GetActorLocation() + FVector(0, 0, G.Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * .85f);
    // Same framing as CireCamera: an absolute boom behind the champion, pitched down, looking up the lane.
    const FVector Facing = -G.TownForward;
    const FRotator View(Pitch, Facing.Rotation().Yaw, 0);
    Look(Pivot - View.Vector() * Boom, Pivot, 90.f);
}

void EnterStage(const FStage& S)
{
    if (!S.bKeepScene) ClearScene();
    const FString& N = S.Name;
    if (N == TEXT("lineup_melee")) LineUp(0, 5, TEXT("Tripo monsters: melee and Pack Leader (1.7x)"));
    else if (N == TEXT("lineup_ranged")) LineUp(5, 5, TEXT("Tripo monsters: casters, hunters, Siegebreaker"));
    else if (N == TEXT("pack")) Pack();
    else if (N.StartsWith(TEXT("attack_"))) Attack(*N.Mid(7));
    else if (N == TEXT("variants")) Variants();
    else if (N == TEXT("locomotion")) Locomotion();
    else if (N == TEXT("deaths")) Deaths();
    else if (N == TEXT("champions")) Champions();
    else if (N == TEXT("town_march")) { TownWave(); GameplayCamera(900, -24); }
    else if (N == TEXT("town_fight")) GameplayCamera(1100, -30);
    else if (N == TEXT("town_kill"))
    {
        int32 Killed = 0;
        for (auto& M : G.Wave)
            if (M.IsValid() && Killed < 2 && M->Health > 0 && G.Hero.IsValid() && !M->IsLaneBoss()) { M->TakeDamage(1.e8f, FDamageEvent(), nullptr, G.Hero.Get()); ++Killed; }
        if (Killed == 0) Fail(TEXT("no wave monster could be killed for the death capture"));
        GameplayCamera(900, -26);
    }
    else if (N == TEXT("town_corpses")) GameplayCamera(900, -26);
}

void Capture(const FStage& S)
{
    if (S.Name == TEXT("town_fight"))
    {
        int32 Swings = 0;
        for (auto& M : G.Wave) if (M.IsValid() && M->MonsterArt) Swings += M->MonsterArt->SwingSerial > 0 || !M->CastingAbility.IsEmpty();
        UE_LOG(LogCireMonsterGallery, Display, TEXT("CIRE_MONSTER_GALLERY_WAVE attacking=%d of %d"), Swings, G.Wave.Num());
        if (Swings == 0) Fail(TEXT("no wave monster engaged the champion"));
    }
    if (S.Name == TEXT("town_kill") && ACireMonsterCorpse::LiveCount() == 0) Fail(TEXT("killed monsters left no falling corpse"));
    const FString File = FPaths::Combine(G.Directory, FString::Printf(TEXT("%02d_%s.png"), G.Stage + 1, *S.Name));
    FScreenshotRequest::RequestScreenshot(File, false, false, false, FIntRect(), true);
    G.Captures.Add(File);
    UE_LOG(LogCireMonsterGallery, Display, TEXT("CIRE_MONSTER_GALLERY_CAPTURE stage=%s file=%s"), *S.Name, *File);
}

bool Build(ACireGameMode& Mode, ACireController& Controller)
{
    G.Controller = &Controller;
    if (auto* Player = Cast<ACireHero>(Controller.GetPawn()))
    {
        Player->TeamId = 0; Player->Draft(0); Player->SetActorHiddenInGame(true); Player->SetActorEnableCollision(false);
    }
    Controller.SetIgnoreMoveInput(true); Controller.SetIgnoreLookInput(true); Controller.bShowMouseCursor = false;
    if (Controller.GetHUD()) Controller.GetHUD()->bShowHUD = false;
    for (auto* M : Mode.Monsters) if (IsValid(M)) M->Destroy();
    Mode.Monsters.Reset();
    BuildStudio();
    G.Camera = Mode.GetWorld()->SpawnActor<ACameraActor>();
    if (!G.Camera.IsValid()) return false;
    auto* Camera = G.Camera->GetCameraComponent();
    Camera->SetAspectRatio(16.f / 9.f); Camera->bConstrainAspectRatio = true;
    auto& Post = Camera->PostProcessSettings;
    Post.bOverride_AutoExposureMethod = true; Post.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
    Post.bOverride_AutoExposureApplyPhysicalCameraExposure = true; Post.AutoExposureApplyPhysicalCameraExposure = false;
    Post.bOverride_AutoExposureBias = true; Post.AutoExposureBias = .6f;
    Post.bOverride_MotionBlurAmount = true; Post.MotionBlurAmount = 0;
    Controller.SetViewTarget(G.Camera.Get());
    const TCHAR* Names[] = {TEXT("lineup_melee"), TEXT("lineup_ranged"), TEXT("pack"),
        TEXT("attack_hollow_infantry"), TEXT("attack_ironbound_bruiser"), TEXT("attack_hollow_shieldbearer"), TEXT("attack_blight_caster"),
        TEXT("attack_barbed_hunter"), TEXT("attack_hollow_siegebreaker"), TEXT("attack_gravemaw_pack_leader"),
        TEXT("variants"), TEXT("locomotion"), TEXT("deaths"), TEXT("champions")};
    for (const TCHAR* Name : Names)
        if (G.Only.IsEmpty() || G.Only.Contains(Name)) G.Stages.Add({Name, 2.5f, false});
    if (G.Only.IsEmpty() || G.Only.Contains(TEXT("town")))
    {
        G.Stages.Add({TEXT("town_march"), 4.f, false});
        G.Stages.Add({TEXT("town_fight"), 7.f, true});
        G.Stages.Add({TEXT("town_kill"), 1.1f, true});
        G.Stages.Add({TEXT("town_corpses"), 3.f, true});
    }
    G.bBuilt = true;
    UE_LOG(LogCireMonsterGallery, Display, TEXT("CIRE_MONSTER_GALLERY_READY stages=%d"), G.Stages.Num());
    return G.Stages.Num() > 0;
}
}

bool CireMonsterGallery::Initialize(ACireGameMode* Mode)
{
    G = FGallery();
    if (!FParse::Param(FCommandLine::Get(), TEXT("CireMonsterGallery"))) return false;
    G.Mode = Mode; G.Started = FPlatformTime::Seconds();
    FString Only;
    if (FParse::Value(FCommandLine::Get(), TEXT("CireMonsterGalleryOnly="), Only, false)) Only.ParseIntoArray(G.Only, TEXT(","), true);
    G.Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MonsterGallery"), FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));
    if (!Mode || Mode->GetNetMode() != NM_Standalone || !IFileManager::Get().MakeDirectory(*G.Directory, true)) { Fail(TEXT("standalone match and capture directory")); Finish(); return true; }
    Mode->bBotsFilled = true; Mode->BotFillTimer = MAX_flt; Mode->WaveTimer = MAX_flt;
    return true;
}

bool CireMonsterGallery::Tick(ACireGameMode* Mode)
{
    if (G.Mode.Get() != Mode) return false;
    if (G.bDone) return true;
    const double Now = FPlatformTime::Seconds();
    if (Now - G.Started > 600) { Fail(TEXT("gallery exceeded 600 seconds")); Finish(); return true; }
    if (!G.bBuilt)
    {
        auto* Controller = Cast<ACireController>(Mode->GetWorld()->GetFirstPlayerController());
        if (Controller && Controller->GetPawn() && Controller->GetHUD() && !Build(*Mode, *Controller)) { Fail(TEXT("build")); Finish(); }
        return true;
    }
    if (G.Stage < 0 || (G.bCaptured && Now - G.StageStarted > G.Stages[G.Stage].Settle + 1.2))
    {
        if (G.Stage + 1 >= G.Stages.Num()) { ClearScene(); Finish(); return true; }
        ++G.Stage; G.bCaptured = false; G.StageStarted = Now;
        EnterStage(G.Stages[G.Stage]);
        return true;
    }
    // Champions pose through the real ChampionArt path (-CireTripoChampions) with their action clip held.
    for (auto& Champion : G.Champions)
        if (ACireHero* H = Champion.Hero.Get(); H && H->ChampionArt)
        {
            H->ChampionArt->UpdateVisuals(*H, FApp::GetDeltaTime());
            if (H->ChampionArt->IsApplied() && !CireChampionActions::Hold(*H, Champion.Clip, Champion.Phase) && G.bCaptured == false && Now - G.StageStarted > G.Stages[G.Stage].Settle - .1)
                Fail(TEXT("champion clip missing: ") + H->ChampionProfileId + TEXT(" ") + Champion.Clip);
        }
    // Hold captures while shaders compile so no placeholder materials are recorded.
    if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0 && Now - G.Started < 300) { G.StageStarted = Now; return true; }
    if (!G.bCaptured && Now - G.StageStarted >= G.Stages[G.Stage].Settle)
    {
        Capture(G.Stages[G.Stage]);
        G.bCaptured = true;
    }
    return true;
}
