#include "CireMonsterGallery.h"

#include "CireGame.h"
#include "CireLanePath.h"
#include "CireMonsterArt.h"
#include "CireChampionActions.h"
#include "CireChampionArt.h"
#include "CireWeaponPresentation.h"
#include "GameFramework/GameStateBase.h"
#include "CireNPCArchetypes.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"
#include "CireRaces.h" // monster-races
#include "CireFabAnimation.h" // paladin-hq
#include "CireMobility.h" // paladin-hq: tank body scale
#include "CireMonsterExpansion.h" // monster-expansion
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
    struct FChampion { TWeakObjectPtr<ACireHero> Hero; FString Clip; float Phase = 1.f; float Draw = -1.f;
        FVector Run = FVector::ZeroVector; FString FabKind; }; // paladin-hq: running velocity, Fab roll/death clip held at Phase
    // Hand detail stages: the camera tracks one bone of one character.
    TWeakObjectPtr<ACharacter> Focus; FName FocusBone; FVector FocusOffset = FVector::ZeroVector;
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
    G.Scene.Reset(); G.Wave.Reset(); G.Champions.Reset(); G.Focus.Reset();
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

struct FClose { const TCHAR* Who; const TCHAR* Preset; const TCHAR* Clip; float Phase; const TCHAR* Title; };
void Closeups(const TArray<FClose>& List);

/** One character, camera on a hand: View 0 from the side/front, 1 from above/behind. */
void HandDetail(const FClose& Entry, FName Bone, const FVector& Offset)
{
    Closeups({Entry});
    for (auto& Actor : G.Scene) if (auto* C = Cast<ACharacter>(Actor.Get())) { G.Focus = C; break; }
    G.FocusBone = Bone; G.FocusOffset = Offset;
}

/** Close-up pairs: each subject idle and mid-attack, framed on the hands. Who = champion profile or monster archetype. */
void Closeups(const TArray<FClose>& List)
{
    const FVector C = G.Studio;
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    for (int32 I = 0; I < List.Num(); ++I)
    {
        const FClose& E = List[I];
        const FVector At = C + FVector(0, (I - (List.Num() - 1) * .5f) * 95.f, 0);
        if (CireNPCArchetypes::Find(FName(E.Who)))
        {
            ACireMonster* M = Spawn(FName(E.Who), 0, At, 0);
            if (M && FCString::Strlen(E.Clip) > 0)
            {
                const CireMonsterArt::FClipWindow W = M->MonsterArt->WindowOf(E.Clip);
                Pose(M, E.Clip, E.Phase <= 1.f ? FMath::Lerp(W.Start, W.Contact, E.Phase) : FMath::Lerp(W.Contact, W.End, E.Phase - 1.f));
            }
            else if (M) M->MonsterArt->PoseForTest(TEXT("idle"), .3f);
        }
        else
        {
            auto* H = World()->SpawnActor<ACireHero>(At + FVector(0, 0, 200), FRotator::ZeroRotator, Params);
            if (!H) { Fail(TEXT("spawn champion")); continue; }
            G.Scene.Add(H); H->TeamId = 0;
            if (!H->DraftProfile(E.Who)) Fail(FString(TEXT("draft ")) + E.Who);
            H->SetActorTickEnabled(false); H->GetCharacterMovement()->DisableMovement();
            H->SetActorLocation(FVector(At.X, At.Y, FloorZ(At) + H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1));
            H->SetActorRotation(FRotator(0, 0, 0));
            H->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            if (H->ChampionArt) H->ChampionArt->UpdateVisuals(*H, 0.f);
            if (FCString::Strlen(E.Preset) > 0)
                if (auto* Weapons = H->FindComponentByClass<UCireWeaponPresentation>())
                    for (int32 Try = 0; Try < 4 && Weapons->GetEquippedLoadout() != E.Preset; ++Try) { FString Message; Weapons->CyclePreview(*H, Message); }
            const bool bBowDraw = FCString::Strcmp(E.Clip, TEXT("attack_bow")) == 0;
            G.Champions.Add({H, E.Clip, E.Phase, bBowDraw ? .15f : -1.f});
        }
        Label(At + FVector(0, 0, 212), E.Title, FColor(246, 219, 155), 7, 0);
    }
    Look(C + FVector(215, -55, 150), C + FVector(0, 0, 118), 50);
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

// ---- monster-races: race lineups, rank colours side by side, reskins of one body ------------------
FColor RankLabelColor(ECireNPCRank Rank)
{
    const FLinearColor C = CireRaces::Rank(Rank).Color;
    return Rank == ECireNPCRank::Normal ? FColor(235, 230, 215) : C.ToFColor(true);
}
void RaceLineup(FName RaceId)
{
    const FCireRace* Race = CireRaces::FindRace(RaceId);
    if (!Race) { Fail(TEXT("race missing: ") + RaceId.ToString()); return; }
    const FVector C = G.Studio;
    // Six units across the front, the two bosses behind them.
    float Y = -(5 * 300.f) * .5f;
    for (int32 I = 0; I < Race->Units.Num(); ++I)
    {
        const bool bBoss = I >= 6;
        const FVector At = bBoss ? C + FVector(-620, (I == 6 ? -1 : 1) * 520.f, 0) : C + FVector(0, Y + I * 300.f, 0);
        ACireMonster* M = Spawn(Race->Units[I], 0, At, 0);
        if (!M) continue;
        CireRaces::ApplyRank(M, bBoss ? ECireNPCRank::Warlord : ECireNPCRank::Normal, 0);
        const FCireNPCArchetype* A = M->NPCState ? M->NPCState->Archetype() : nullptr;
        const FString Slot = A ? A->Slot.ToString() : FString();
        const bool bOwn = CireMonsterArt::HasOwnBody(Race->Units[I]);
        Label(At + FVector(0, 0, M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2 + 40),
            FString::Printf(TEXT("%s\n%s%s"), *M->GetNPCDisplayName(), *Slot, bOwn ? TEXT("") : *FString::Printf(TEXT(" (body: %s)"), *VariantName(M))),
            bBoss ? RankLabelColor(ECireNPCRank::Warlord) : FColor::White, bBoss ? 17.f : 13.f, 0);
    }
    Label(C + FVector(-620, 0, 620), Race->Name + TEXT("  |  6 units + 2 bosses"), FColor::White, 28, 0);
    Look(C + FVector(2050, 0, 720), C + FVector(-250, 0, 170), 52);
}
void RankLineup(FName Unit, bool bFar)
{
    const FVector C = G.Studio;
    const int32 Count = static_cast<int32>(ECireNPCRank::Count);
    for (int32 I = 0; I < Count; ++I)
    {
        const ECireNPCRank Rank = static_cast<ECireNPCRank>(I);
        const FVector At = C + FVector(0, (I - (Count - 1) * .5f) * 290.f, 0);
        ACireMonster* M = Spawn(Unit, 0, At, 0);
        if (!M) continue;
        CireRaces::ApplyRank(M, Rank, 0);
        M->SetActorScale3D(FVector((M->NPCState && M->NPCState->Archetype() ? M->NPCState->Archetype()->Scale : 1.f) * CireRaces::RankSize(M)));
        M->SetActorLocation(FVector(At.X, At.Y, FloorZ(At) + M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
        if (!bFar) Label(At + FVector(0, 0, M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2 + 40), CireRaces::Rank(Rank).Label, RankLabelColor(Rank), 17, 0);
    }
    const FCireNPCArchetype* A = CireNPCArchetypes::Find(Unit);
    if (bFar)
    {
        // Gameplay distance: the default camera boom looks down at ~50 degrees from ~22 m.
        const FVector Pivot = C + FVector(0, 0, 90);
        const FRotator View(-50, 180, 0);
        Look(Pivot - View.Vector() * 2200.f, Pivot, 60.f);
    }
    else
    {
        Label(C + FVector(0, 0, 470), (A ? A->DisplayName : Unit.ToString()) + TEXT(": normal, veteran, elite, champion, warlord, mythic"), FColor::White, 22, 0);
        Look(C + FVector(1500, 0, 380), C + FVector(0, 0, 130), 55);
    }
}
void Reskins()
{
    // One Tripo body (HollowInfantry) under five race palettes: the cheap way to field a new race.
    const TCHAR* Units[] = {TEXT("hollow_infantry"), TEXT("abyssal_stalker"), TEXT("vinelasher"), TEXT("ironhide_grunt"), TEXT("rift_stalker"), TEXT("fallen_squire")};
    const FVector C = G.Studio;
    for (int32 I = 0; I < UE_ARRAY_COUNT(Units); ++I)
    {
        const FVector At = C + FVector(0, (I - 2.5f) * 290.f, 0);
        ACireMonster* M = Spawn(FName(Units[I]), 0, At, 0);
        if (!M) continue;
        CireRaces::ApplyRank(M, ECireNPCRank::Normal, 0);
        const FCireRace* Race = CireRaces::FindRace(CireRaces::RaceOf(FName(Units[I])));
        Label(At + FVector(0, 0, M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2 + 40),
            M->GetNPCDisplayName() + TEXT("\n") + (Race ? Race->Short : FString()), FColor::White, 14, 0);
    }
    Label(C + FVector(0, 0, 470), TEXT("One body, six race palettes (HollowInfantry)"), FColor::White, 22, 0);
    Look(C + FVector(1500, 0, 380), C + FVector(0, 0, 130), 55);
}
void PaletteSets(FName Unit)
{
    // The same unit in each of its race's reskin sets (per-wave-set palettes).
    const FCireRace* Race = CireRaces::FindRace(CireRaces::RaceOf(Unit));
    if (!Race) return;
    const FVector C = G.Studio;
    const int32 Count = Race->Variants.Num();
    for (int32 I = 0; I < Count; ++I)
    {
        const FVector At = C + FVector(0, (I - (Count - 1) * .5f) * 300.f, 0);
        ACireMonster* M = Spawn(Unit, 0, At, 0);
        if (!M) continue;
        CireRaces::ApplyRank(M, ECireNPCRank::Normal, I);
        Label(At + FVector(0, 0, M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2 + 40), Race->Variants[I].Name, FColor::White, 15, 0);
    }
    Label(C + FVector(0, 0, 470), Race->Name + TEXT(": reskin sets"), FColor::White, 22, 0);
    Look(C + FVector(1400, 0, 380), C + FVector(0, 0, 130), 55);
}

void Closeup(const FString& Spec)
{
    // close_<unit>+<unit>+...: art review of new bodies. Front row idle, back row at attack contact.
    TArray<FString> Units; Spec.ParseIntoArray(Units, TEXT("+"), true);
    const FVector C = G.Studio;
    for (int32 I = 0; I < Units.Num(); ++I)
    {
        const float Y = (I - (Units.Num() - 1) * .5f) * 330.f;
        for (int32 Row = 0; Row < 2; ++Row)
        {
            const FVector At = C + FVector(Row ? -420.f : 0.f, Y, 0);
            ACireMonster* M = Spawn(FName(*Units[I]), 0, At, Row ? 35.f : 0.f);
            if (!M) continue;
            CireRaces::ApplyRank(M, ECireNPCRank::Normal, 0);
            if (Row && M->MonsterArt) Pose(M, TEXT("attack"), M->MonsterArt->WindowOf(TEXT("attack")).Contact);
            if (!Row) Label(At + FVector(0, 0, M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2 + 30), M->GetNPCDisplayName() + TEXT("\n") + VariantName(M), FColor::White, 13, 0);
        }
    }
    Look(C + FVector(330.f * Units.Num() + 250.f, 0, 260), C + FVector(-200, 0, 110), 50);
}

// monster-rig: face_<unit>+<unit>+...: upper-body review (tentacle beards, skin sway masks with
// -dpcvars=cire.Monsters.SwayDebug=1). Each unit twice: facing the lens and turned 75 degrees.
void FaceCloseup(const FString& Spec)
{
    TArray<FString> Units; Spec.ParseIntoArray(Units, TEXT("+"), true);
    const FVector C = G.Studio;
    const int32 Count = Units.Num() * 2;
    float Top = 150.f;
    for (int32 I = 0; I < Count; ++I)
    {
        const FVector At = C + FVector(0, (I - (Count - 1) * .5f) * 150.f, 0);
        ACireMonster* M = Spawn(FName(*Units[I / 2]), 0, At, I % 2 ? 75.f : 0.f);
        if (!M) continue;
        CireRaces::ApplyRank(M, ECireNPCRank::Normal, 0);
        Top = FMath::Max(Top, M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.f);
        if (I % 2 == 0) Label(At + FVector(0, 75, 40), VariantName(M), FColor::White, 9, 0);
    }
    Look(C + FVector(150.f * Count + 120.f, 0, Top * .75f), C + FVector(0, 0, Top * .62f), 40);
}

// paladin-hq: the Iron Warden and both Relic Paladins (Polyphoria plate bodies) in the town, through the real
// champion path: pala_idle, pala_run, pala_attack, pala_cast, pala_roll, pala_death, pala_game (gameplay camera),
// pala_detail (head-and-hands close-up).
void Paladins(const FString& Mode)
{
    UWorld* W = World();
    const FVector Hold = CireLanePath::PointAlongRoute(W, 0, .56f), Ahead = CireLanePath::PointAlongRoute(W, 0, .58f);
    FVector Fwd = (Ahead - Hold).GetSafeNormal2D();
    if (Fwd.IsNearlyZero()) Fwd = FVector(-1, 0, 0);
    G.TownForward = Fwd;
    const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd);
    const TCHAR* Ids[] = {TEXT("knight"), TEXT("paladin_righteous"), TEXT("paladin_holy")};
    const bool bRun = Mode == TEXT("run"), bGame = Mode == TEXT("game"), bDetail = Mode == TEXT("detail") || Mode == TEXT("front") || Mode == TEXT("back");
    const float Spacing = bDetail ? 150.f : 210.f;
    // Heroes face the camera (standing in front of it along +Fwd), turned 20 degrees for a three-quarter view.
    const FVector ToCamera = bGame ? -Fwd : Fwd;
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    for (int32 I = 0; I < UE_ARRAY_COUNT(Ids); ++I)
    {
        const FVector At = Hold + Right * ((I - 1.f) * Spacing);
        auto* H = W->SpawnActor<ACireHero>(At + FVector(0, 0, 200), FRotator::ZeroRotator, Params);
        if (!H) { Fail(TEXT("spawn champion")); continue; }
        G.Scene.Add(H); H->TeamId = 0;
        if (!H->DraftProfile(Ids[I])) Fail(FString(TEXT("draft ")) + Ids[I]);
        CireMovement::ApplyToHero(*H); // tanks are 15% larger, as in play
        H->SetActorTickEnabled(false); H->GetCharacterMovement()->DisableMovement();
        H->SetActorLocation(FVector(At.X, At.Y, FloorZ(At) + H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1));
        FVector Facing = bRun ? (ToCamera.RotateAngleAxis(-55.f, FVector::UpVector)) : ToCamera.RotateAngleAxis(bGame || Mode == TEXT("front") ? 0.f : 20.f, FVector::UpVector);
        if (Mode == TEXT("back")) Facing = -ToCamera.RotateAngleAxis(25.f, FVector::UpVector);
        if (Mode == TEXT("attack") || Mode == TEXT("cast")) Facing = ToCamera.RotateAngleAxis(-62.f, FVector::UpVector); // the swing arm toward the lens
        if (bGame) Facing = Fwd;
        H->SetActorRotation(Facing.Rotation());
        H->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        if (H->ChampionArt) H->ChampionArt->UpdateVisuals(*H, 0.f);
        H->PrestreamTextures(30.f, true);
        {   // paladin-hq: where each held prop sits (review aid in the log)
            TArray<UStaticMeshComponent*> Props; H->GetComponents(Props);
            for (const UStaticMeshComponent* Prop : Props)
                if (Prop && Prop->ComponentHasTag(TEXT("CireWeaponProp")) && Prop->GetStaticMesh())
                    UE_LOG(LogCireMonsterGallery, Display, TEXT("CIRE_PALADIN_PROP %s %s bone=%s at=%s actor=%s extent=%s scale=%s visible=%d lod=%d"), *H->ChampionProfileId,
                        *Prop->GetStaticMesh()->GetName(), *Prop->GetAttachSocketName().ToString(), *Prop->GetComponentLocation().ToString(), *H->GetActorLocation().ToString(),
                        *Prop->Bounds.BoxExtent.ToString(), *Prop->GetComponentScale().ToString(), Prop->IsVisible(), Prop->GetStaticMesh()->GetNumLODs());
        }
        FGallery::FChampion C; C.Hero = H; C.Clip = TEXT(""); C.Phase = 1.f;
        if (Mode == TEXT("attack")) { C.Clip = TEXT("slash"); C.Phase = 1.05f; }
        else if (Mode == TEXT("cast")) { C.Clip = TEXT("cast_a_spell"); C.Phase = .95f; }
        else if (Mode == TEXT("roll")) { C.FabKind = TEXT("roll"); C.Phase = .45f; }
        else if (Mode == TEXT("death")) { C.FabKind = TEXT("death"); H->bDead = true; }
        if (bRun) C.Run = Facing * 520.f;
        if (bGame && I == 1) G.Hero = H;
        G.Champions.Add(C);
    }
    if (bGame) { GameplayCamera(900, -24); return; }
    const float Distance = Mode == TEXT("detail") ? 420.f : bDetail ? 560.f : 700.f;
    const FVector Floor(Hold.X, Hold.Y, FloorZ(Hold));
    const FVector Target = Floor + FVector(0, 0, Mode == TEXT("detail") ? 150.f : 112.f);
    Look(Target + ToCamera * Distance + FVector(0, 0, Mode == TEXT("detail") ? 30.f : 45.f), Target, Mode == TEXT("detail") ? 34.f : 45.f);
}

// monster-expansion: the Bestiary creatures (Docs/MonsterExpansion.md). One creature in four states side by side
// (idle, walk mid-stride, attack at contact, end of death), the full lineup next to a hollow infantry for scale,
// and the Rare Spawn / Bonus Loot looks.
float BodyHeight(const ACireMonster* M) { return M ? M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.f : 180.f; }
float BodyWidth(const ACireMonster* M)
{
    if (!M || !M->GetMesh()) return 200.f;
    const FBoxSphereBounds B = M->GetMesh()->CalcBounds(M->GetMesh()->GetComponentTransform());
    return FMath::Clamp(static_cast<float>(FMath::Max(B.BoxExtent.X, B.BoxExtent.Y)) * 2.f, 120.f, 900.f);
}
void CreaturePhases(FName Id)
{
    const FVector C = G.Studio;
    const TCHAR* Roles[] = {TEXT("idle"), TEXT("walk"), TEXT("attack"), TEXT("death")};
    const TCHAR* Captions[] = {TEXT("idle"), TEXT("walk"), TEXT("attack (contact)"), TEXT("death")};
    ACireMonster* Probe = Spawn(Id, 0, C + FVector(0, 4000, 0), 0);
    const float Width = BodyWidth(Probe), Height = BodyHeight(Probe);
    if (Probe) { G.Scene.Remove(Probe); Probe->Destroy(); }
    const float Step = FMath::Max(300.f, Width * .9f);
    for (int32 I = 0; I < 4; ++I)
    {
        const FVector At = C + FVector(0, (1.5f - I) * Step, 0); // left to right on screen: idle, walk, attack, death
        ACireMonster* M = Spawn(Id, 0, At, 0);
        if (!M || !M->MonsterArt) continue;
        const CireMonsterArt::FClipWindow W = M->MonsterArt->WindowOf(Roles[I]);
        const float T = I == 0 ? W.End * .3f : I == 1 ? W.End * .35f : I == 2 ? W.Contact : FMath::Max(0.f, W.End - .05f);
        Pose(M, Roles[I], T);
        Label(At + FVector(0, 0, Height + 40), Captions[I], FColor(246, 219, 155), FMath::Clamp(Height * .09f, 13.f, 30.f), 0);
    }
    const FCireNPCArchetype* A = CireNPCArchetypes::Find(Id);
    Label(C + FVector(0, 0, Height + 140), (A ? A->DisplayName : Id.ToString()) + TEXT("  (") + (Probe ? VariantName(Probe) : FString()) + TEXT(")"), FColor::White, FMath::Clamp(Height * .12f, 20.f, 40.f), 0);
    const float Span = Step * 4.f;
    Look(C + FVector(FMath::Max(1400.f, Span * 1.05f), 0, Height * .7f + 120.f), C + FVector(0, 0, Height * .42f), 50);
}
void BestiaryLineup()
{
    const FVector C = G.Studio;
    TArray<FName> Ids = {TEXT("hollow_infantry")};
    for (const auto& Creature : CireMonsterExpansion::Creatures()) Ids.Add(Creature.Id);
    TArray<ACireMonster*> Bodies; TArray<float> Widths; float Total = 0.f, Tallest = 0.f;
    for (const FName Id : Ids)
    {
        ACireMonster* M = Spawn(Id, 0, C + FVector(0, 6000 + Bodies.Num() * 900.f, 0), 0);
        Bodies.Add(M); const float Wd = FMath::Max(180.f, BodyWidth(M) * .75f); Widths.Add(Wd); Total += Wd; Tallest = FMath::Max(Tallest, BodyHeight(M));
    }
    float Y = -Total * .5f;
    for (int32 I = 0; I < Bodies.Num(); ++I)
    {
        ACireMonster* M = Bodies[I];
        Y += Widths[I] * .5f;
        if (M)
        {
            const FVector At = C + FVector(0, Y, 0);
            M->SetActorLocation(FVector(At.X, At.Y, FloorZ(At) + M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
            Label(At + FVector(0, 0, BodyHeight(M) + 30), M->GetNPCDisplayName(), FColor::White, 15, 0);
        }
        Y += Widths[I] * .5f;
    }
    Label(C + FVector(0, 0, Tallest + 120), TEXT("Bestiary: the new creatures next to a Hollow Infantry"), FColor::White, 26, 0);
    Look(C + FVector(FMath::Max(1600.f, Total * 1.2f), 0, Tallest * .6f + 150.f), C + FVector(0, 0, Tallest * .35f), 52);
}
void SpecialLook(uint8 Kind)
{
    const FVector C = G.Studio;
    TArray<FName> Ids;
    if (Kind == 2) Ids = {TEXT("treasure_goblin"), TEXT("treasure_goblin"), TEXT("gilded_stag"), TEXT("treasure_goblin")};
    else Ids = {TEXT("lich_revenant"), TEXT("horned_brute"), TEXT("frostfang_alpha"), TEXT("storm_griffon"), TEXT("cinder_drake")};
    float Tallest = 0.f, Total = 0.f;
    TArray<ACireMonster*> Bodies; TArray<float> Widths;
    for (int32 I = 0; I < Ids.Num(); ++I)
    {
        ACireMonster* M = Spawn(Ids[I], 0, C + FVector(0, 6000 + I * 900.f, 0), Kind == 2 ? 20.f * (I - 1.5f) : 0.f);
        Bodies.Add(M); Widths.Add(FMath::Max(300.f, BodyWidth(M) * .8f)); Total += Widths.Last();
    }
    float Y = Total * .5f;
    for (int32 I = 0; I < Ids.Num(); ++I)
    {
        ACireMonster* M = Bodies[I];
        Y -= Widths[I] * .5f;
        const FVector At = C + FVector(0, Y, 0);
        Y -= Widths[I] * .5f;
        if (!M) continue;
        M->SetActorLocation(FVector(At.X, At.Y, FloorZ(At) + M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
        CireRaces::ApplyRank(M, Kind == 1 ? ECireNPCRank::Elite : ECireNPCRank::Normal, 0);
        const FCireNPCArchetype* A = M->NPCState ? M->NPCState->Archetype() : nullptr;
        M->SpecialSpawn = Kind;
        M->MonsterName = Kind == 1 ? FString(TEXT("Rare ")) + (A ? A->DisplayName : FString()) : (A ? A->DisplayName : FString());
        CireRaces::ApplySkin(M);
        if (Kind == 2 && M->MonsterArt) Pose(M, TEXT("run"), M->MonsterArt->WindowOf(TEXT("run")).End * (.2f + .15f * I));
        Tallest = FMath::Max(Tallest, BodyHeight(M));
        Label(M->GetActorLocation() + FVector(0, 0, BodyHeight(M) * .5f + 30), M->GetNPCDisplayName(),
            Kind == 1 ? FColor(51, 242, 255) : FColor(255, 199, 31), 15, 0);
    }
    Label(C + FVector(0, 0, Tallest + 130), Kind == 1 ? TEXT("Rare Spawns: rare glow, Rare plate, aura") : TEXT("Bonus Loot Wave: the goblin hoard fleeing"), FColor::White, 24, 0);
    Look(C + FVector(FMath::Max(1500.f, Total * 1.15f), 0, Tallest * .6f + 150.f), C + FVector(0, 0, Tallest * .35f), 55);
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
    else if (N.StartsWith(TEXT("races_"))) RaceLineup(FName(*N.Mid(6))); // monster-races
    else if (N.StartsWith(TEXT("close_"))) Closeup(N.Mid(6));
    else if (N.StartsWith(TEXT("face_"))) FaceCloseup(N.Mid(5)); // monster-rig
    else if (N.StartsWith(TEXT("pala_"))) Paladins(N.Mid(5)); // paladin-hq
    else if (N.StartsWith(TEXT("creature_"))) CreaturePhases(FName(*N.Mid(9))); // monster-expansion
    else if (N == TEXT("bestiary")) BestiaryLineup();
    else if (N == TEXT("rare_look")) SpecialLook(1);
    else if (N == TEXT("bonus_look")) SpecialLook(2);
    else if (N == TEXT("ranks_close")) RankLineup(TEXT("tidecaller"), false);
    else if (N == TEXT("ranks_close_hollow")) RankLineup(TEXT("hollow_infantry"), false);
    else if (N == TEXT("ranks_gameplay")) RankLineup(TEXT("deepspawn_thrall"), true);
    else if (N == TEXT("ranks_gameplay_hollow")) RankLineup(TEXT("hollow_shieldbearer"), true);
    else if (N == TEXT("reskins")) Reskins();
    else if (N == TEXT("palettes_blightwood")) PaletteSets(TEXT("sapling_brute"));
    else if (N == TEXT("palettes_drowned")) PaletteSets(TEXT("coralshell_guardian"));
    else if (N == TEXT("hand_sword_front")) HandDetail({TEXT("knight"), TEXT(""), TEXT(""), 0, TEXT("")}, TEXT("hand_r"), FVector(120, 90, 15));
    else if (N == TEXT("hand_sword_side")) HandDetail({TEXT("knight"), TEXT(""), TEXT(""), 0, TEXT("")}, TEXT("hand_r"), FVector(10, 140, 25));
    else if (N == TEXT("hand_sword_attack")) HandDetail({TEXT("knight"), TEXT(""), TEXT("slash"), 1.f, TEXT("")}, TEXT("hand_r"), FVector(110, 110, 30));
    else if (N == TEXT("hand_shield")) HandDetail({TEXT("knight"), TEXT(""), TEXT(""), 0, TEXT("")}, TEXT("lowerarm_l"), FVector(110, -130, 30));
    else if (N == TEXT("hand_bow")) HandDetail({TEXT("ranger"), TEXT(""), TEXT(""), 0, TEXT("")}, TEXT("hand_l"), FVector(110, -110, 20));
    else if (N == TEXT("hand_bow_draw")) HandDetail({TEXT("ranger"), TEXT(""), TEXT("attack_bow"), .85f, TEXT("")}, TEXT("hand_r"), FVector(60, 130, 40));
    else if (N == TEXT("hand_staff")) HandDetail({TEXT("scholar"), TEXT(""), TEXT(""), 0, TEXT("")}, TEXT("hand_r"), FVector(130, 90, 20));
    else if (N == TEXT("hand_axe")) HandDetail({TEXT("orc_chieftain"), TEXT(""), TEXT(""), 0, TEXT("")}, TEXT("hand_r"), FVector(260, 200, 40));
    else if (N == TEXT("styles_windup") || N == TEXT("styles_contact"))
    {
        const float Phase = N == TEXT("styles_windup") ? .75f : 1.15f;
        Closeups({{TEXT("knight"), TEXT(""), TEXT("slash"), Phase, TEXT("Sword: diagonal slash")}, {TEXT("orc_chieftain"), TEXT(""), TEXT("slash"), Phase, TEXT("Axe: sweep")},
            {TEXT("knight"), TEXT("hammer_shield"), TEXT("slash"), Phase, TEXT("Hammer: overhead")}, {TEXT("troll_berserker_melee"), TEXT("dual_daggers"), TEXT("cast_a_spell"), Phase, TEXT("Daggers: thrust")},
            {TEXT("lancer"), TEXT(""), TEXT("cast_a_spell"), Phase, TEXT("Lance: thrust")}});
        for (auto& Actor : G.Scene) if (auto* Text = Cast<ATextRenderActor>(Actor.Get())) Text->GetTextRender()->SetWorldSize(9.f);
        Look(G.Studio + FVector(620, -360, 240), G.Studio + FVector(0, 0, 120), 50);
    }
    else if (N == TEXT("hand_totem")) HandDetail({TEXT("totemic_behemoth"), TEXT(""), TEXT(""), 0, TEXT("")}, TEXT("hand_r"), FVector(190, 150, 20));
    else if (N == TEXT("hand_crossbow")) HandDetail({TEXT("ranger"), TEXT("ranger_crossbow"), TEXT(""), 0, TEXT("")}, TEXT("hand_r"), FVector(120, 20, 25));
    else if (N == TEXT("hand_monster")) HandDetail({TEXT("hollow_infantry"), TEXT(""), TEXT(""), 0, TEXT("")}, TEXT("hand_r"), FVector(110, 90, 15));
    else if (N == TEXT("grips_melee_a")) Closeups({{TEXT("knight"), TEXT(""), TEXT(""), 0, TEXT("Warden sword+shield idle")}, {TEXT("knight"), TEXT(""), TEXT("slash"), .8f, TEXT("Warden windup")}});
    else if (N == TEXT("grips_melee_b")) Closeups({{TEXT("orc_chieftain"), TEXT(""), TEXT(""), 0, TEXT("Chieftain axe idle")}, {TEXT("orc_chieftain"), TEXT(""), TEXT("slash"), 1.f, TEXT("Chieftain axe contact")}});
    else if (N == TEXT("grips_heavy_a")) Closeups({{TEXT("dwarf_miner"), TEXT(""), TEXT(""), 0, TEXT("Miner pick idle")}, {TEXT("paladin_holy"), TEXT(""), TEXT(""), 0, TEXT("Paladin flail+shield idle")}});
    else if (N == TEXT("grips_heavy_b")) Closeups({{TEXT("knight"), TEXT("hammer_shield"), TEXT("slash"), 1.f, TEXT("Hammer contact")}, {TEXT("totemic_behemoth"), TEXT(""), TEXT(""), 0, TEXT("Behemoth totem two-hand")}});
    else if (N == TEXT("grips_ranged_a")) Closeups({{TEXT("ranger"), TEXT(""), TEXT(""), 0, TEXT("Ranger bow idle")}, {TEXT("ranger"), TEXT(""), TEXT("attack_bow"), .85f, TEXT("Ranger bow draw")}});
    else if (N == TEXT("grips_ranged_b")) Closeups({{TEXT("ranger"), TEXT("ranger_crossbow"), TEXT(""), 0, TEXT("Crossbow idle")}, {TEXT("ranger"), TEXT("ranger_crossbow"), TEXT("attack_crossbow"), 1.f, TEXT("Crossbow shot")}});
    else if (N == TEXT("grips_casters_a")) Closeups({{TEXT("scholar"), TEXT(""), TEXT(""), 0, TEXT("Scholar staff two-hand")}, {TEXT("wizard"), TEXT(""), TEXT("cast_a_spell"), 1.f, TEXT("Wizard cast")}});
    else if (N == TEXT("grips_casters_b")) Closeups({{TEXT("summoner"), TEXT(""), TEXT(""), 0, TEXT("Summoner staff+dagger")}, {TEXT("keeper_of_light"), TEXT(""), TEXT(""), 0, TEXT("Keeper lantern staff")}});
    else if (N == TEXT("grips_light_a")) Closeups({{TEXT("lancer"), TEXT(""), TEXT(""), 0, TEXT("Lancer lance idle")}, {TEXT("lancer"), TEXT(""), TEXT("slash"), .8f, TEXT("Lancer windup")}});
    else if (N == TEXT("grips_light_b")) Closeups({{TEXT("troll_berserker_melee"), TEXT(""), TEXT(""), 0, TEXT("Troll throwing axes")}, {TEXT("troll_berserker_melee"), TEXT("dual_daggers"), TEXT("slash"), 1.f, TEXT("Daggers contact")}});
    else if (N == TEXT("grips_monsters_a")) Closeups({{TEXT("hollow_infantry"), TEXT(""), TEXT(""), 0, TEXT("Infantry dagger idle")}, {TEXT("ironbound_bruiser"), TEXT(""), TEXT("attack"), .8f, TEXT("Bruiser axe windup")}});
    else if (N == TEXT("grips_monsters_b")) Closeups({{TEXT("hollow_shieldbearer"), TEXT(""), TEXT(""), 0, TEXT("Shieldbearer idle")}, {TEXT("barbed_hunter"), TEXT(""), TEXT("attack"), .9f, TEXT("Hunter bow draw")}});
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
        TEXT("variants"), TEXT("locomotion"), TEXT("deaths"), TEXT("champions"),
        TEXT("hand_sword_front"), TEXT("hand_sword_side"), TEXT("hand_sword_attack"), TEXT("hand_shield"), TEXT("hand_bow"), TEXT("hand_bow_draw"),
        TEXT("styles_windup"), TEXT("styles_contact"),
        TEXT("hand_staff"), TEXT("hand_axe"), TEXT("hand_totem"), TEXT("hand_crossbow"), TEXT("hand_monster"),
        TEXT("grips_melee_a"), TEXT("grips_melee_b"), TEXT("grips_heavy_a"), TEXT("grips_heavy_b"), TEXT("grips_ranged_a"), TEXT("grips_ranged_b"), TEXT("grips_casters_a"), TEXT("grips_casters_b"), TEXT("grips_light_a"), TEXT("grips_light_b"), TEXT("grips_monsters_a"), TEXT("grips_monsters_b")};
    for (const TCHAR* Name : Names)
        if (G.Only.IsEmpty() || G.Only.ContainsByPredicate([Name](const FString& Prefix) { return FString(Name).StartsWith(Prefix); })) G.Stages.Add({Name, 2.5f, false});
    // monster-races: every race's lineup, ranks side by side (close and at gameplay distance), reskins.
    {
        TArray<FString> RaceStages;
        for (const FName Race : CireRaces::Get().Order) RaceStages.Add(TEXT("races_") + Race.ToString());
        RaceStages.Append({TEXT("ranks_close"), TEXT("ranks_close_hollow"), TEXT("ranks_gameplay"), TEXT("ranks_gameplay_hollow"), TEXT("reskins"),
            TEXT("palettes_blightwood"), TEXT("palettes_drowned")});
        for (const FString& Name : RaceStages)
            if (G.Only.IsEmpty() || G.Only.ContainsByPredicate([&Name](const FString& Prefix) { return Name.StartsWith(Prefix); })) G.Stages.Add({Name, 3.f, false});
    }
    for (const FString& Only : G.Only) if (Only.StartsWith(TEXT("close_")) || Only.StartsWith(TEXT("face_"))) G.Stages.Add({Only, 3.f, false});
    for (const TCHAR* Name : {TEXT("pala_front"), TEXT("pala_back"), TEXT("pala_idle"), TEXT("pala_run"), TEXT("pala_attack"), TEXT("pala_cast"), TEXT("pala_roll"), TEXT("pala_death"), TEXT("pala_game"), TEXT("pala_detail")})
        if (G.Only.ContainsByPredicate([Name](const FString& Prefix) { return FString(Name).StartsWith(Prefix); })) G.Stages.Add({Name, 4.f, false}); // paladin-hq
    // monster-expansion: bestiary lineup, every creature in four states, rare and bonus looks.
    {
        TArray<FString> Expansion = {TEXT("bestiary")};
        for (const auto& Creature : CireMonsterExpansion::Creatures()) Expansion.Add(TEXT("creature_") + Creature.Id.ToString());
        Expansion.Append({TEXT("rare_look"), TEXT("bonus_look")});
        for (const FString& Name : Expansion)
            if (G.Only.IsEmpty() || G.Only.ContainsByPredicate([&Name](const FString& Prefix) { return Name.StartsWith(Prefix); })) G.Stages.Add({Name, 3.f, false});
    }
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
    if (ACharacter* F = G.Focus.Get())
    {
        const FVector Hand = F->GetMesh()->GetSocketLocation(G.FocusBone);
        Look(Hand + F->GetActorRotation().RotateVector(G.FocusOffset), Hand, 38.f);
        static double LastLog = 0;
        if (FPlatformTime::Seconds() - LastLog > 1.0)
        {
            LastLog = FPlatformTime::Seconds();
            const auto* Mesh = F->GetMesh();
            const TCHAR* Side = G.FocusBone == TEXT("hand_l") ? TEXT("_l") : TEXT("_r");
            FString Line = FString::Printf(TEXT("CIRE_GRIP_DEBUG stage=%s hand=%s"), *G.Stages[G.Stage].Name, *Mesh->GetSocketLocation(FName(FString(TEXT("hand")) + Side)).ToString());
            for (const TCHAR* B : {TEXT("index_01"), TEXT("pinky_01"), TEXT("middle_01"), TEXT("middle_03"), TEXT("thumb_03"), TEXT("lowerarm")})
                Line += FString::Printf(TEXT(" %s=%s"), B, *Mesh->GetSocketLocation(FName(FString(B) + Side)).ToString());
            TArray<USceneComponent*> Children; Mesh->GetChildrenComponents(false, Children);
            for (USceneComponent* Child : Children)
                if (auto* Part = Cast<UStaticMeshComponent>(Child); Part && Part->GetStaticMesh())
                    Line += FString::Printf(TEXT(" | %s@%s origin=%s up=%s fwd=%s"), *Part->GetStaticMesh()->GetName(), *Part->GetAttachSocketName().ToString(),
                        *Part->GetComponentLocation().ToString(), *Part->GetUpVector().ToString(), *Part->GetForwardVector().ToString());
            UE_LOG(LogCireMonsterGallery, Display, TEXT("%s"), *Line);
        }
    }
    // Champions pose through the real ChampionArt path (-CireTripoChampions) with their action clip held.
    for (auto& Champion : G.Champions)
        if (ACireHero* H = Champion.Hero.Get(); H && H->ChampionArt)
        {
            if (Champion.Draw >= 0.f)
            {
                // Bow draw: the replicated attack is mid-draw so the string hand pinches the nock.
                const AGameStateBase* State = H->GetWorld()->GetGameState();
                const float ServerNow = State ? State->GetServerWorldTimeSeconds() : H->GetWorld()->GetTimeSeconds();
                H->AttackSerial = 7; H->AttackDuration = .65f; H->AttackStartedServerTime = ServerNow - Champion.Draw;
            }
            if (!Champion.Run.IsZero()) H->GetCharacterMovement()->Velocity = Champion.Run; // paladin-hq: run cycle
            H->ChampionArt->UpdateVisuals(*H, FApp::GetDeltaTime());
            if (!Champion.FabKind.IsEmpty() && Champion.FabKind != TEXT("death"))
                if (auto* Combat = Cast<UCireCombatAnimInstance>(H->GetMesh()->GetAnimInstance()))
                {   // paladin-hq: a Fab reaction clip held at Phase (dodge roll)
                    UAnimSequence* Clip = nullptr; FString Name; CireChampionActions::FWindow Wn;
                    USkeletalMesh* Body = H->GetMesh()->GetSkeletalMeshAsset();
                    if (CireFabAnimation::Pick(Body, CireFabAnimation::FolderFor(Body), CireChampionActions::StyleName(*H), CireChampionActions::MotionFor(*H), Champion.FabKind, 0, Clip, Name) && CireFabAnimation::Window(Name, Wn))
                    { Combat->AttackSequence = Clip; Combat->AttackTime = FMath::Lerp(Wn.Start, Wn.End, Champion.Phase); Combat->AttackWeight = 1.f; Combat->AttackLowerBody = 1.f; Combat->RollProgress = -1.f; }
                    else if (!G.bCaptured) Fail(TEXT("fab clip missing: ") + H->ChampionProfileId + TEXT(" ") + Champion.FabKind);
                }
            if (!Champion.Clip.IsEmpty() && H->ChampionArt->IsApplied() && !CireChampionActions::Hold(*H, Champion.Clip, Champion.Phase) && G.bCaptured == false && Now - G.StageStarted > G.Stages[G.Stage].Settle - .1)
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
