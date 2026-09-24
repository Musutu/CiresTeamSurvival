#include "CireFeedbackPreview.h"

#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireCombatEvents.h"
#include "CireHUD.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireFeedbackPreview, Log, All);

namespace
{
struct FPreviewState
{
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACireController> Controller;
    TWeakObjectPtr<ACireHero> Hero;
    TWeakObjectPtr<ACireHero> Ally;
    TArray<TWeakObjectPtr<ACireMonster>> Targets;
    TArray<FString> Screenshots;
    FString Directory;
    FString PendingScreenshot;
    double Started = 0;
    double ReadyAt = -1;
    double ScreenshotAt = 0;
    int32 Stage = 0;
    int32 Assertions = 0;
    bool bPassed = true;
    bool bDone = false;
};
FPreviewState Preview;

void Check(bool bCondition, const TCHAR* Label)
{
    ++Preview.Assertions;
    if (!bCondition)
    {
        Preview.bPassed = false;
        UE_LOG(LogCireFeedbackPreview, Error, TEXT("CIRE_FEEDBACK_PREVIEW_CHECK_FAIL %s"), Label);
    }
}

void Finish(bool bPassed, const TCHAR* Reason)
{
    if (Preview.bDone) return;
    Preview.bDone = true;
    UE_LOG(LogCireFeedbackPreview, Display,
        TEXT("CIRE_FEEDBACK_PREVIEW_%s checks=%d screenshots=%d reason=%s directory=%s"),
        bPassed ? TEXT("PASS") : TEXT("FAIL"), Preview.Assertions, Preview.Screenshots.Num(), Reason, *Preview.Directory);
    FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1);
}

bool BuildFixture(ACireGameMode* Mode, ACireController* Controller, ACireHero* Hero)
{
    Preview.Controller = Controller;
    Preview.Hero = Hero;
    Hero->TeamId = 0;
    Hero->Draft(2);
    Hero->HeroName = TEXT("Veil Scholar");
    Hero->ReviveAt(FVector(0, -2100, 110));
    Hero->bBot = false;
    Hero->bAutoAttack = false;
    Hero->Skills = {TEXT("ember_lance"), TEXT("chain_spark"), TEXT("restoring_light"), TEXT("executioners_verdict")};
    Hero->Cooldowns.Init(0.f, Hero->Skills.Num());
    Hero->Offers.Reset();
    Hero->Notice = TEXT("Combat feedback preview | real ability damage and healing");
    Hero->GetCharacterMovement()->StopMovementImmediately();
    Hero->GetCharacterMovement()->DisableMovement();
    Hero->Arm->TargetArmLength = 650.f;
    Controller->SetControlRotation(FRotator(-12.f, 0, 0));
    Controller->SetIgnoreMoveInput(true);
    Controller->SetIgnoreLookInput(true);
    Controller->bHelp = false;
    Controller->bShop = false;
    Controller->CombatEvents.Reset();
    if (auto* HUD = Cast<ACireHUD>(Controller->GetHUD()))
    {
        // This modifies only the preview's in-memory copy, never the user's saved profile.
        HUD->UISettings.Reset();
        HUD->UISettings.bCombatTextEnabled = true;
        HUD->UISettings.bShowFloatingNumbers = true;
        HUD->UISettings.bShowScrollingText = true;
    }

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Ally = Mode->GetWorld()->SpawnActor<ACireHero>(ACireHero::StaticClass(),
        FVector(390, -1740, 110), FRotator(0, 180, 0), Params);
    if (!Ally) return false;
    Preview.Ally = Ally;
    Ally->TeamId = 0;
    Ally->Draft(0);
    Ally->HeroName = TEXT("Iron Warden");
    Ally->Health = Ally->MaxHealth - 180.f;
    Ally->bBot = false;
    Ally->bAutoAttack = false;
    Ally->GetCharacterMovement()->DisableMovement();
    Ally->SetActorTickEnabled(false);
    Mode->Heroes.Add(Ally);

    const FVector Positions[] = {FVector(600, -2100, 110), FVector(610, -2380, 110), FVector(690, -1850, 110)};
    for (int32 Index = 0; Index < 3; ++Index)
    {
        auto* Target = Mode->GetWorld()->SpawnActor<ACireMonster>(ACireMonster::StaticClass(),
            Positions[Index], FRotator(0, 180, 0), Params);
        if (!Target) return false;
        Target->Lane = 0;
        Target->Health = Target->MaxHealth = 1400.f;
        Target->Damage = 0;
        Target->SpawnPosition = Positions[Index];
        Target->MonsterName = Index == 0 ? TEXT("Hollow vanguard") : TEXT("Hollow infantry");
        Target->GetCharacterMovement()->DisableMovement();
        Target->SetActorTickEnabled(false);
        Mode->Monsters.Add(Target);
        Preview.Targets.Add(Target);
    }
    Hero->Target = Preview.Targets[0].Get();
    Controller->SetFocusTarget(Ally);
    Preview.ReadyAt = FPlatformTime::Seconds();
    UE_LOG(LogCireFeedbackPreview, Display, TEXT("CIRE_FEEDBACK_PREVIEW_READY stages=6 duration=18 directory=%s"), *Preview.Directory);
    return true;
}

void QueueScreenshot(const TCHAR* Name, double Now)
{
    Preview.PendingScreenshot = FPaths::Combine(Preview.Directory, FString(Name) + TEXT(".png"));
    Preview.ScreenshotAt = Now + .30;
}

void RunStage(int32 Stage, double Now)
{
    auto* Hero = Preview.Hero.Get();
    auto* Ally = Preview.Ally.Get();
    auto* Controller = Preview.Controller.Get();
    auto* Target = Preview.Targets.IsValidIndex(0) ? Preview.Targets[0].Get() : nullptr;
    if (!Hero || !Ally || !Controller || !Target)
    {
        Finish(false, TEXT("fixture actor unavailable before final hit"));
        return;
    }
    Hero->Target = Target;
    const float DamageBefore = Hero->DamageDone;
    const float HealingBefore = Hero->HealingDone;
    if (Stage == 0)
    {
        Hero->Cast(0);
        Check(Hero->DamageDone > DamageBefore, TEXT("outgoing cast produced actual damage"));
        QueueScreenshot(TEXT("01_outgoing"), Now);
    }
    else if (Stage == 1)
    {
        const float Applied = CireCombat::ApplyDamage(Target, Hero, 45.f, TEXT("Raking strike"));
        Check(FMath::IsNearlyEqual(Applied, 45.f), TEXT("incoming damage applied"));
        QueueScreenshot(TEXT("02_incoming"), Now);
    }
    else if (Stage == 2)
    {
        Hero->Target = Ally;
        Hero->Cast(2);
        Check(Hero->HealingDone > HealingBefore, TEXT("friendly cast produced effective healing"));
        QueueScreenshot(TEXT("03_healing"), Now);
    }
    else if (Stage == 3)
    {
        float Before[3];
        for (int32 I = 0; I < 3; ++I) Before[I] = Preview.Targets[I]->Health;
        Hero->Cast(1);
        int32 Affected = 0;
        for (int32 I = 0; I < 3; ++I) if (Preview.Targets[I]->Health < Before[I]) ++Affected;
        Check(Affected == 3, TEXT("chain spell damaged three distinct targets"));
        QueueScreenshot(TEXT("04_multi_target"), Now);
    }
    else if (Stage == 4)
    {
        // Several real impacts in one frame exercise independent numbers and stacking.
        CireCombat::ApplyDamage(Hero, Target, 34.f, TEXT("Basic attack"));
        CireCombat::ApplyDamage(Hero, Target, 47.f, TEXT("Basic attack"));
        CireCombat::ApplyDamage(Hero, Target, 26.f, TEXT("Basic attack"));
        Check(FMath::IsNearlyEqual(Hero->DamageDone - DamageBefore, 107.f), TEXT("burst impacts are all recorded"));
        QueueScreenshot(TEXT("05_burst"), Now);
    }
    else if (Stage == 5)
    {
        Target->Health = 137.f;
        const FName TargetId = Target->GetFName();
        Hero->Cast(3);
        Check(FMath::IsNearlyEqual(Hero->DamageDone - DamageBefore, 137.f), TEXT("lethal hit excludes overkill"));
        Check(!IsValid(Target) || Target->IsActorBeingDestroyed(), TEXT("lethal hit destroys the target"));
        bool bReceivedFinalHit = false;
        for (const auto& Event : Controller->CombatEvents)
            if (Event.TargetId == TargetId && Event.AbilityName == TEXT("Executioner's Verdict") &&
                FMath::IsNearlyEqual(Event.Amount, 137.f) && Event.bLocalSource)
                bReceivedFinalHit = true;
        Check(bReceivedFinalHit, TEXT("lethal event survives destroyed actor in client feedback buffer"));
        QueueScreenshot(TEXT("06_lethal"), Now);
    }
    Check(!Controller->CombatEvents.IsEmpty(), TEXT("actual gameplay events reach local HUD buffer"));
    UE_LOG(LogCireFeedbackPreview, Display, TEXT("CIRE_FEEDBACK_PREVIEW_STAGE stage=%d damage=%.1f healing=%.1f events=%d"),
        Stage, Hero->DamageDone, Hero->HealingDone, Controller->CombatEvents.Num());
}
}

bool CireFeedbackPreview::Initialize(ACireGameMode* Mode)
{
    Preview = {};
    if (!FParse::Param(FCommandLine::Get(), TEXT("CireFeedbackPreview"))) return false;
    Preview.Mode = Mode;
    Preview.Started = FPlatformTime::Seconds();
    Preview.Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
        TEXT("FeedbackPreview"), FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8)));
    if (!IsValid(Mode) || Mode->GetNetMode() != NM_Standalone ||
        !IFileManager::Get().MakeDirectory(*Preview.Directory, true))
    {
        Finish(false, TEXT("preview requires a local standalone game and writable screenshot directory"));
        return true;
    }
    Mode->bBotsFilled = true;
    Mode->BotFillTimer = MAX_flt;
    Mode->WaveTimer = MAX_flt;
    if (auto* State = Mode->GetGameState<ACireGameState>())
    {
        State->NextWaveSeconds = 0;
        State->Announcement = TEXT("COMBAT FEEDBACK PREVIEW | Outgoing, incoming, healing, multiple targets, lethal hit");
    }
    return true;
}

bool CireFeedbackPreview::Tick(ACireGameMode* Mode)
{
    if (Preview.Mode.Get() != Mode) return false;
    if (Preview.bDone) return true;
    const double Now = FPlatformTime::Seconds();
    if (Now - Preview.Started > 30)
    {
        Finish(false, TEXT("preview exceeded 30 second safety limit"));
        return true;
    }
    if (Preview.ReadyAt < 0)
    {
        auto* Controller = Cast<ACireController>(Mode->GetWorld()->GetFirstPlayerController());
        auto* Hero = Controller ? Cast<ACireHero>(Controller->GetPawn()) : nullptr;
        if (Controller && Hero && Controller->GetHUD() && !BuildFixture(Mode, Controller, Hero))
            Finish(false, TEXT("fixture actor spawn failed"));
        return true;
    }
    if (!Preview.PendingScreenshot.IsEmpty() && Now >= Preview.ScreenshotAt)
    {
        FScreenshotRequest::RequestScreenshot(Preview.PendingScreenshot, true, false, false, FIntRect(), true);
        Preview.Screenshots.Add(Preview.PendingScreenshot);
        Preview.PendingScreenshot.Reset();
    }
    const double Elapsed = Now - Preview.ReadyAt;
    const double StageTimes[] = {4.0, 5.5, 7.0, 8.5, 10.0, 11.5};
    if (Preview.Stage < static_cast<int32>(UE_ARRAY_COUNT(StageTimes)) && Elapsed >= StageTimes[Preview.Stage])
        RunStage(Preview.Stage++, Now);
    if (Elapsed >= 18)
    {
        Check(Preview.Stage == 6, TEXT("all six feedback scenarios completed"));
        Check(Preview.Screenshots.Num() == 6, TEXT("six rendered capture requests issued"));
        for (const FString& Filename : Preview.Screenshots)
            Check(IFileManager::Get().FileSize(*Filename) > 1024, TEXT("rendered screenshot file saved"));
        Finish(Preview.bPassed, TEXT("gameplay events and screenshot files checked; image readability requires visual inspection"));
    }
    return true;
}
#endif
