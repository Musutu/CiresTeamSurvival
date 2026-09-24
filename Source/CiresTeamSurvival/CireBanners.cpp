#include "CireBanners.h"
#include "HAL/PlatformTime.h"

namespace
{
struct FQueued { ECireBanner Type; FCireBannerSpec Spec; int32 Priority; uint64 Order; };
TArray<FQueued> GQueue;
FQueued GActive;
bool GHasActive = false;
double GActiveStart = 0.0, GFreeAt = 0.0;
uint64 GOrder = 0;
TMap<FString, double> GRecent;
}

int32 CireBanners::Priority(ECireBanner Type)
{
    switch (Type)
    {
    case ECireBanner::Victory: case ECireBanner::Defeat: return 100;
    case ECireBanner::BossSpawned: return 80;
    case ECireBanner::Arena: case ECireBanner::PrepPhase: case ECireBanner::Recovery: return 70;
    case ECireBanner::ChallengeUnlocked: return 60;
    case ECireBanner::WaveIncoming: return 50;
    case ECireBanner::LevelUp: return 45;
    case ECireBanner::WaveCleared: return 40;
    default: return 30;
    }
}

FCireBannerSpec CireBanners::DefaultSpec(ECireBanner Type)
{
    FCireBannerSpec S;
    switch (Type)
    {
    case ECireBanner::LevelUp: S.Kicker = TEXT("YOU HAVE REACHED"); S.Color = FLinearColor(1.f, .80f, .22f, 1); S.Duration = 3.6f; break;
    case ECireBanner::WaveIncoming: S.Kicker = TEXT("PREPARE YOURSELVES"); S.Color = FLinearColor(1.f, .45f, .22f, 1); break;
    case ECireBanner::WaveCleared: S.Kicker = TEXT("THE LINE HOLDS"); S.Color = FLinearColor(.55f, .95f, .45f, 1); S.Duration = 2.8f; break;
    case ECireBanner::PrepPhase: S.Kicker = TEXT("TOWN PREPARATION"); S.Color = FLinearColor(.45f, .85f, 1.f, 1); S.Duration = 4.f; break;
    case ECireBanner::Arena: S.Kicker = TEXT("THE PORTAL OPENS"); S.Color = FLinearColor(1.f, .28f, .25f, 1); S.Duration = 4.f; break;
    case ECireBanner::Recovery: S.Kicker = TEXT("CATCH YOUR BREATH"); S.Color = FLinearColor(.35f, .9f, .75f, 1); break;
    case ECireBanner::ChallengeUnlocked: S.Kicker = TEXT("CHALLENGE UNLOCKED"); S.Color = FLinearColor(.8f, .55f, 1.f, 1); S.Duration = 3.8f; break;
    case ECireBanner::BossSpawned: S.Kicker = TEXT("BOSS INCOMING"); S.Color = FLinearColor(1.f, .22f, .16f, 1); S.Duration = 4.f; break;
    case ECireBanner::Victory: S.Kicker = TEXT("THE BATTLE IS DECIDED"); S.Color = FLinearColor(1.f, .84f, .3f, 1); S.Duration = 5.f; break;
    case ECireBanner::Defeat: S.Kicker = TEXT("THE BATTLE IS DECIDED"); S.Color = FLinearColor(.75f, .3f, .3f, 1); S.Duration = 5.f; break;
    default: break;
    }
    return S;
}

void CireBanners::Show(ECireBanner Type, const FString& Title, const FString& Subtitle, const FString& Kicker)
{
    const double Now = FPlatformTime::Seconds();
    const FString Key = FString::FromInt(static_cast<int32>(Type)) + Title;
    if (const double* Last = GRecent.Find(Key); Last && Now - *Last < 1.5) return;
    GRecent.Add(Key, Now);
    FQueued Q;
    Q.Type = Type; Q.Spec = DefaultSpec(Type); Q.Spec.Title = Title; Q.Spec.Subtitle = Subtitle;
    if (!Kicker.IsEmpty()) Q.Spec.Kicker = Kicker;
    Q.Priority = Priority(Type); Q.Order = ++GOrder;
    // A higher-priority banner cuts a lower one short (it is re-queued only if barely started).
    if (GHasActive && Q.Priority > GActive.Priority + 20 && Now - GActiveStart < GActive.Spec.Duration - .8)
    {
        if (Now - GActiveStart < .6) GQueue.Add(GActive);
        GHasActive = false; GFreeAt = Now;
    }
    GQueue.Add(Q);
    if (GQueue.Num() > 8)
    {
        GQueue.Sort([](const FQueued& A, const FQueued& B) { return A.Priority != B.Priority ? A.Priority > B.Priority : A.Order < B.Order; });
        GQueue.SetNum(8);
    }
}

bool CireBanners::Draw(const FCireUIPainter& Painter, float ViewW, float ViewH, ECireBanner& OutStarted)
{
    const double Now = FPlatformTime::Seconds();
    bool bStarted = false;
    if (GHasActive && Now - GActiveStart > GActive.Spec.Duration) { GHasActive = false; GFreeAt = Now + .35; }
    if (!GHasActive && Now >= GFreeAt && GQueue.Num() > 0)
    {
        int32 Best = 0;
        for (int32 I = 1; I < GQueue.Num(); ++I)
            if (GQueue[I].Priority > GQueue[Best].Priority || (GQueue[I].Priority == GQueue[Best].Priority && GQueue[I].Order < GQueue[Best].Order)) Best = I;
        GActive = GQueue[Best]; GQueue.RemoveAt(Best); GHasActive = true; GActiveStart = Now;
        OutStarted = GActive.Type; bStarted = true;
    }
    if (GHasActive) CireUIStyle::Banner(Painter, ViewW, ViewH * .15f, GActive.Spec, static_cast<float>(Now - GActiveStart));
    return bStarted;
}

void CireBanners::Clear() { GQueue.Reset(); GHasActive = false; GRecent.Reset(); }
int32 CireBanners::QueuedCount() { return GQueue.Num(); }
bool CireBanners::IsShowing() { return GHasActive; }
