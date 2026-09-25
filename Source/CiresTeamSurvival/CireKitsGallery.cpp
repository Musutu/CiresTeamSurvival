// scaling-kits: -CireKitsGallery renders the new kit moments on the real lane (offscreen 1920x1080,
// Saved/KitsGallery/<stamp>): the Mechanical Tank taunting, a shield BLOCK (floating text + combat log),
// the level-15 Artillery bomb and the Skill Shop tooltip with its "Lv 15: +..." line.
// Run via Tools/RunKitsGallery.py. Optional -CireKitsGalleryOnly=<stage,...>.
#include "CireKitsGallery.h"
#include "CireBuffs.h"
#include "CireCombatEvents.h"
#include "CireConstruct.h"
#include "CireGame.h"
#include "CireHUD.h"
#include "CireItems.h"
#include "CireLanePath.h"
#include "CireMechTank.h"
#include "CireNPCCombat.h"
#include "CireScalingKits.h"
#include "CireShopUI.h"
#include "CireThreat.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/TextRenderActor.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireKitsGallery, Log, All);

namespace
{
struct FStage { FString Name; float Settle = 1.5f; bool bHud = false; };
struct FGallery
{
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACireController> Controller;
    TWeakObjectPtr<ACameraActor> Camera;
    TArray<TWeakObjectPtr<AActor>> Scene;
    TWeakObjectPtr<ACireHero> Player;
    TArray<FStage> Stages;
    TArray<FString> Captures, Only;
    FString Directory;
    FVector Hold, Forward = FVector(1, 0, 0), Right = FVector(0, 1, 0);
    double Started = 0, StageStarted = 0, LastPulse = 0;
    int32 Stage = -1, Pulses = 0;
    bool bCaptured = false, bDone = false, bPass = true, bBuilt = false;
    TWeakObjectPtr<ACireMonster> Attacker;
};
FGallery G;

UWorld* World() { return G.Mode.IsValid() ? G.Mode->GetWorld() : nullptr; }
void Fail(const FString& Why) { G.bPass = false; UE_LOG(LogCireKitsGallery, Error, TEXT("CIRE_KITS_GALLERY_CHECK_FAIL %s"), *Why); }
void Finish()
{
    if (G.bDone) return;
    G.bDone = true;
    for (const FString& File : G.Captures) if (IFileManager::Get().FileSize(*File) < 1024) Fail(TEXT("capture not written: ") + File);
    UE_LOG(LogCireKitsGallery, Display, TEXT("CIRE_KITS_GALLERY_%s captures=%d directory=%s"), G.bPass ? TEXT("PASS") : TEXT("FAIL"), G.Captures.Num(), *G.Directory);
    FPlatformMisc::RequestExitWithStatus(false, G.bPass ? 0 : 1);
}
float FloorZ(const FVector& P)
{
    FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(CireKitsGalleryFloor), false);
    for (const auto& Actor : G.Scene) if (Actor.IsValid()) Query.AddIgnoredActor(Actor.Get());
    if (G.Player.IsValid()) Query.AddIgnoredActor(G.Player.Get());
    return World()->LineTraceSingleByObjectType(Hit, P + FVector(0, 0, 2000), P - FVector(0, 0, 4000), FCollisionObjectQueryParams(ECC_WorldStatic), Query) ? Hit.ImpactPoint.Z : P.Z;
}
FVector Ground(float Along, float Side) { const FVector P = G.Hold + G.Forward * Along + G.Right * Side; return FVector(P.X, P.Y, FloorZ(P)); }
void Look(const FVector& Eye, const FVector& Target, float Fov = 60.f)
{
    if (!G.Camera.IsValid()) return;
    G.Camera->SetActorLocation(Eye); G.Camera->SetActorRotation((Target - Eye).Rotation());
    G.Camera->GetCameraComponent()->SetFieldOfView(Fov);
}
void Label(const FVector& At, const FString& Text, const FColor& Color, float Size)
{
    if (!G.Camera.IsValid()) return;
    if (auto* L = World()->SpawnActor<ATextRenderActor>(At, (G.Camera->GetActorLocation() - At).GetSafeNormal2D().Rotation()))
    {
        L->GetTextRender()->SetText(FText::FromString(Text)); L->GetTextRender()->SetWorldSize(Size);
        L->GetTextRender()->SetTextRenderColor(Color); L->GetTextRender()->SetHorizontalAlignment(EHTA_Center);
        G.Scene.Add(L);
    }
}
void ClearScene()
{
    for (TActorIterator<ACireConstruct> It(World()); It; ++It) if (It->HasAuthority()) It->Destroy();
    for (TActorIterator<ACireMechTank> It(World()); It; ++It) It->Destroy();
    for (auto& Actor : G.Scene) if (Actor.IsValid()) Actor->Destroy();
    G.Scene.Reset(); G.Attacker.Reset();
    if (G.Mode.IsValid())
    {
        G.Mode->Monsters.RemoveAll([](ACireMonster* M) { return !IsValid(M) || M->IsActorBeingDestroyed(); });
        G.Mode->Heroes.RemoveAll([](ACireHero* H) { return !IsValid(H) || H->IsActorBeingDestroyed(); });
    }
}
void Place(ACireHero* H, float Along, float Side, float Yaw)
{
    const FVector At = Ground(Along, Side);
    H->SetActorLocation(FVector(At.X, At.Y, At.Z + H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2));
    H->SetActorRotation(FRotator(0, Yaw, 0));
}
ACireHero* Hero(const TCHAR* Profile, float Along, float Side, float Yaw)
{
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* H = World()->SpawnActor<ACireHero>(Ground(Along, Side) + FVector(0, 0, 200), FRotator(0, Yaw, 0), Params);
    if (!H) { Fail(FString(TEXT("spawn ")) + Profile); return nullptr; }
    G.Scene.Add(H); H->TeamId = 0;
    if (!H->DraftProfile(Profile)) Fail(FString(TEXT("draft ")) + Profile);
    H->Health = H->MaxHealth = 1.e6f; H->Mana = H->MaxMana = 1.e5f; H->Energy = 100; H->Offers.Reset();
    Place(H, Along, Side, Yaw);
    G.Mode->Heroes.Add(H);
    return H;
}
ACireMonster* Monster(FName Id, float Along, float Side, bool bThink)
{
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    const FVector At = Ground(Along, Side);
    auto* M = World()->SpawnActor<ACireMonster>(At + FVector(0, 0, 140), (-G.Forward).Rotation(), Params);
    if (!M) return nullptr;
    G.Scene.Add(M); M->Lane = 0; G.Mode->Monsters.Add(M);
    CireNPCCombat::ConfigureArchetype(M, Id, 4, 0, 1);
    M->Health = M->MaxHealth = FMath::Max(M->MaxHealth, 25000.f);
    M->SetActorLocation(FVector(At.X, At.Y, At.Z + M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2));
    if (!bThink) { M->SetActorTickEnabled(false); M->GetCharacterMovement()->DisableMovement(); }
    return M;
}
void Learn(ACireHero* H, const TCHAR* Id, int32 Level)
{
    if (!H->Skills.Contains(Id)) { H->Skills.Add(Id); H->Cooldowns.Add(0.f); }
    if (H->Inventory) { H->Inventory->SkillRanks.RemoveAll([&](const FCireSkillRank& R) { return R.Id == Id; }); FCireSkillRank R; R.Id = Id; R.Level = Level; H->Inventory->SkillRanks.Add(R); }
}
void SetPhase(int32 Phase)
{
    G.Mode->Clock = Cires::MatchClock();
    if (Phase == 1) G.Mode->Clock.BeginIntermission();
    if (auto* S = G.Mode->GetGameState<ACireGameState>()) { S->Phase = Phase; S->SecondsLeft = Phase == 1 ? 42.f : -1.f; S->Wave = 7; }
}
void ShowHud(bool bShow) { if (G.Controller.IsValid() && G.Controller->GetHUD()) G.Controller->GetHUD()->bShowHUD = bShow; }

void EnterStage(const FStage& S)
{
    ClearScene(); CireShopUI::DebugMouse(FVector2D(-1, -1));
    if (G.Controller.IsValid()) G.Controller->bShop = false;
    ShowHud(S.bHud);
    ACireHero* P = G.Player.Get();
    const FString& N = S.Name;
    UE_LOG(LogCireKitsGallery, Display, TEXT("CIRE_KITS_GALLERY_STAGE %s"), *N);
    if (N == TEXT("mech_taunt"))
    {
        SetPhase(0);
        ACireHero* Knight = Hero(TEXT("knight"), 0, 0, (-G.Forward).Rotation().Yaw);
        ACireHero* Ranger = Hero(TEXT("ranger"), 380, 260, (-G.Forward).Rotation().Yaw);
        ACireMonster* A = Monster(TEXT("ironbound_bruiser"), -520, 360, false);
        ACireMonster* B = Monster(TEXT("hollow_infantry"), -320, -120, false);
        if (!Knight || !Ranger || !A || !B) { Fail(TEXT("mech scene")); return; }
        CireThreat::Engage(A, Ranger); A->Victim = Ranger; CireThreat::Engage(B, Knight); B->Victim = Knight;
        ACireMechTank* Mech = ACireMechTank::SpawnFor(Knight, Ground(-160, 200));
        if (!Mech) { Fail(TEXT("mech spawn")); return; }
        Mech->SetActorTickEnabled(false); G.Scene.Add(Mech);
        AActor* Taunted = Mech->TryTaunt();
        if (Taunted != A) Fail(TEXT("mech taunted the wrong enemy"));
        UE_LOG(LogCireKitsGallery, Display, TEXT("CIRE_KITS_GALLERY_TAUNT target=%s victim_now=%s"), Taunted ? *Taunted->GetName() : TEXT("none"), A->Victim ? *A->Victim->HeroName : TEXT("none"));
        Label(Mech->GetActorLocation() + FVector(0, 0, 250), TEXT("MECHANICAL TANK: TAUNT"), FColor(160, 200, 255), 42);
        Label(A->GetActorLocation() + FVector(0, 0, 230), TEXT("was hitting the Ranger"), FColor(255, 190, 120), 30);
        const FVector Mid = (Mech->GetActorLocation() + A->GetActorLocation()) * .5f;
        Look(Mid - G.Forward * -900 + G.Right * -700 + FVector(0, 0, 620), Mid, 62.f);
    }
    else if (N == TEXT("shield_block"))
    {
        SetPhase(0);
        if (!P) { Fail(TEXT("player pawn")); return; }
        P->DraftProfile(TEXT("knight")); P->TeamId = 0; P->Health = P->MaxHealth = 1.e6f; P->SetActorHiddenInGame(false); P->SetActorEnableCollision(true);
        P->Offers.Reset(); P->CurrentOffer = {}; P->Skills = {TEXT("shield_slam"), TEXT("shield_bash"), TEXT("iron_guard"), TEXT("stone_skin")}; P->Cooldowns.Init(0, 4);
        Place(P, 0, 0, (-G.Forward).Rotation().Yaw);
        ACireMonster* A = Monster(TEXT("ironbound_bruiser"), -170, 0, false);
        if (!A) { Fail(TEXT("block attacker")); return; }
        G.Attacker = A; G.Pulses = 0; G.LastPulse = 0;
        const FVector Pivot = P->GetActorLocation() + FVector(0, 0, 90);
        Look(Pivot + G.Forward * 520 + G.Right * 260 + FVector(0, 0, 190), Pivot - G.Forward * 80, 55.f);
    }
    else if (N == TEXT("artillery_bomb"))
    {
        SetPhase(0);
        ACireHero* Ranger = Hero(TEXT("ranger"), 900, 0, (-G.Forward).Rotation().Yaw);
        if (!Ranger) return;
        TArray<ACireMonster*> Pack;
        const FName Ids[] = {TEXT("hollow_infantry"), TEXT("ironbound_bruiser"), TEXT("hollow_infantry"), TEXT("blight_caster"), TEXT("hollow_infantry")};
        for (int32 I = 0; I < 5; ++I) if (auto* M = Monster(Ids[I], -1400 + (I % 2) * 160, -300 + I * 150, false)) Pack.Add(M);
        if (Pack.Num() < 3) { Fail(TEXT("artillery pack")); return; }
        Learn(Ranger, TEXT("artillery"), 15); G.Pulses = 0;
        if (auto* Sub = UCireKitsSubsystem::Get(World())) Sub->LastBombDamage = 0;
        Ranger->Target = Pack[2]; Ranger->Energy = 100;
        if (!CireKits::Cast(Ranger, Ranger->Skills.IndexOfByKey(TEXT("artillery")), TEXT("artillery"))) Fail(TEXT("artillery cast: ") + Ranger->Notice);
        // Eight seconds of volleys, compressed: the window records every basic hit, then ends.
        for (int32 I = 0; I < 12; ++I) CireCombat::ApplyDamage(Ranger, Pack[I % Pack.Num()], 140.f, TEXT("bow strike"));
        if (auto* Sub = UCireKitsSubsystem::Get(World())) if (auto* St = Sub->Artillery.Find(Ranger)) { St->Remaining = .05; Sub->ArtilleryLastTarget.Add(Ranger, Pack[2]); }
        const FVector Center = Pack[2]->GetActorLocation();
        Look(Center + G.Forward * 1500 + G.Right * -900 + FVector(0, 0, 1100), Center, 60.f);
    }
    else if (N == TEXT("skill_shop_lv15"))
    {
        SetPhase(1);
        if (!P || !G.Controller.IsValid()) { Fail(TEXT("player pawn")); return; }
        P->DraftProfile(TEXT("knight")); P->TeamId = 0; P->Gold = 900; P->Offers.Reset(); P->CurrentOffer = {};
        P->Skills.Reset(); P->Cooldowns.Reset(); if (P->Inventory) P->Inventory->SkillRanks.Reset();
        Learn(P, TEXT("shield_bash"), 15); Learn(P, TEXT("shield_slam"), 9); Learn(P, TEXT("war_cry"), 4); Learn(P, TEXT("stone_skin"), 15);
        Place(P, 0, 0, (-G.Forward).Rotation().Yaw);
        CireShopUI::DebugReset();
        G.Controller->bShop = true;
        CireShopUI::DebugSkillTab(FString());
        const FVector Pivot = P->GetActorLocation() + FVector(0, 0, 90);
        Look(Pivot + G.Forward * 600 + FVector(0, 0, 300), Pivot, 60.f);
    }
}

void TickStage(const FStage& S, double Now)
{
    if (S.Name == TEXT("shield_block") && G.Attacker.IsValid() && G.Player.IsValid() && Now - G.LastPulse > .35 && G.Pulses < 12)
    {
        // Monster swings until at least one lands on the shield (30% each), then keeps the log fresh.
        G.LastPulse = Now; ++G.Pulses;
        CireCombat::ApplyDamage(G.Attacker.Get(), G.Player.Get(), 60.f, TEXT("Monster attack"));
    }
    if (S.Name == TEXT("artillery_bomb") && G.Pulses == 0)
        if (auto* Sub = UCireKitsSubsystem::Get(World()); Sub && Sub->LastBombDamage > 0)
        {
            G.Pulses = 1;
            Label(Sub->LastBombCenter + FVector(0, 0, 420), FString::Printf(TEXT("ARTILLERY BOMB: %.0f damage"), Sub->LastBombDamage), FColor(255, 170, 70), 60);
            if (G.Player.IsValid()) CireCombat::PlayCue(G.Player.Get(), nullptr, TEXT("cataclysm"), Sub->LastBombCenter, Sub->LastBombCenter, ECireSpellCue::Impact, 3.f, true);
        }
    if (S.Name == TEXT("skill_shop_lv15"))
    {
        const FVector2D Pos = CireShopUI::DebugSkillGridPos(TEXT("shield_bash"));
        if (Pos.X >= 0) CireShopUI::DebugMouse(Pos);
    }
}

void Capture(const FStage& S)
{
    const FString File = FPaths::Combine(G.Directory, FString::Printf(TEXT("%02d_%s.png"), G.Stage + 1, *S.Name));
    FScreenshotRequest::RequestScreenshot(File, S.bHud, false, false, FIntRect(), true);
    G.Captures.Add(File);
    FString Extra;
    if (S.Name == TEXT("artillery_bomb")) if (auto* Sub = UCireKitsSubsystem::Get(World())) Extra = FString::Printf(TEXT(" bomb=%.0f"), Sub->LastBombDamage);
    if (S.Name == TEXT("shield_block") && G.Controller.IsValid())
    {
        int32 Blocks = 0; for (const auto& E : G.Controller->CombatEvents) Blocks += E.Outcome == ECireHitOutcome::Block ? 1 : 0;
        Extra = FString::Printf(TEXT(" blocks=%d"), Blocks);
        if (Blocks == 0) Fail(TEXT("no BLOCK event reached the player"));
    }
    UE_LOG(LogCireKitsGallery, Display, TEXT("CIRE_KITS_GALLERY_CAPTURE stage=%s%s file=%s"), *S.Name, *Extra, *File);
}

bool Build(ACireGameMode& Mode, ACireController& Controller)
{
    G.Controller = &Controller;
    if (auto* Player = ::Cast<ACireHero>(Controller.GetPawn()))
    { Player->TeamId = 0; Player->Draft(0); Player->SetActorHiddenInGame(true); Player->SetActorEnableCollision(false); G.Player = Player; }
    Controller.SetIgnoreMoveInput(true); Controller.SetIgnoreLookInput(true); Controller.bShowMouseCursor = false;
    for (auto* M : Mode.Monsters) if (IsValid(M)) M->Destroy();
    Mode.Monsters.Reset();
    UWorld* W = Mode.GetWorld();
    G.Hold = CireLanePath::PointAlongRoute(W, 0, .58f);
    const FVector Ahead = CireLanePath::PointAlongRoute(W, 0, .60f);
    G.Forward = (Ahead - G.Hold).GetSafeNormal2D(); if (G.Forward.IsNearlyZero()) G.Forward = FVector(-1, 0, 0);
    G.Right = FVector::CrossProduct(FVector::UpVector, G.Forward);
    if (G.Player.IsValid()) G.Player->SetActorLocation(Ground(0, 0) + FVector(0, 0, 100));
    G.Camera = W->SpawnActor<ACameraActor>();
    if (!G.Camera.IsValid()) return false;
    auto* Camera = G.Camera->GetCameraComponent();
    Camera->SetAspectRatio(16.f / 9.f); Camera->bConstrainAspectRatio = true;
    Camera->PostProcessSettings.bOverride_MotionBlurAmount = true; Camera->PostProcessSettings.MotionBlurAmount = 0;
    Controller.SetViewTarget(G.Camera.Get());
    const FStage All[] = {{TEXT("mech_taunt"), 1.2f}, {TEXT("shield_block"), 2.6f, true}, {TEXT("artillery_bomb"), .45f}, {TEXT("skill_shop_lv15"), 2.4f, true}};
    for (const FStage& S : All)
        if (G.Only.IsEmpty() || G.Only.ContainsByPredicate([&S](const FString& Prefix) { return S.Name.StartsWith(Prefix); })) G.Stages.Add(S);
    G.bBuilt = true;
    UE_LOG(LogCireKitsGallery, Display, TEXT("CIRE_KITS_GALLERY_READY stages=%d"), G.Stages.Num());
    return G.Stages.Num() > 0;
}
}

bool CireKitsGallery::Initialize(ACireGameMode* Mode)
{
    G = FGallery();
    if (!FParse::Param(FCommandLine::Get(), TEXT("CireKitsGallery"))) return false;
    G.Mode = Mode; G.Started = FPlatformTime::Seconds();
    FString Only;
    if (FParse::Value(FCommandLine::Get(), TEXT("CireKitsGalleryOnly="), Only, false)) Only.ParseIntoArray(G.Only, TEXT(","), true);
    G.Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("KitsGallery"), FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));
    if (!Mode || Mode->GetNetMode() != NM_Standalone || !IFileManager::Get().MakeDirectory(*G.Directory, true)) { Fail(TEXT("standalone match and capture directory")); Finish(); return true; }
    Mode->bBotsFilled = true; Mode->BotFillTimer = MAX_flt; Mode->WaveTimer = MAX_flt;
    return true;
}

bool CireKitsGallery::Tick(ACireGameMode* Mode)
{
    if (G.Mode.Get() != Mode) return false;
    if (G.bDone) return true;
    const double Now = FPlatformTime::Seconds();
    if (Now - G.Started > 400) { Fail(TEXT("gallery exceeded 400 seconds")); Finish(); return true; }
    if (!G.bBuilt)
    {
        auto* Controller = ::Cast<ACireController>(Mode->GetWorld()->GetFirstPlayerController());
        if (Controller && Controller->GetPawn() && Controller->GetHUD() && !Build(*Mode, *Controller)) { Fail(TEXT("build")); Finish(); }
        return true;
    }
    if (G.Stage < 0 && GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0 && Now - G.Started < 240) return true;
    if (G.Stage < 0 || (G.bCaptured && Now - G.StageStarted > G.Stages[G.Stage].Settle + 1.0))
    {
        if (G.Stage + 1 >= G.Stages.Num()) { ClearScene(); Finish(); return true; }
        ++G.Stage; G.bCaptured = false; G.StageStarted = Now;
        EnterStage(G.Stages[G.Stage]);
        return true;
    }
    TickStage(G.Stages[G.Stage], Now);
    if (!G.bCaptured && Now - G.StageStarted >= G.Stages[G.Stage].Settle) { Capture(G.Stages[G.Stage]); G.bCaptured = true; }
    return true;
}
