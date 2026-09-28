#include "CireDraftHoverProbe.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireDraftProbe,Log,All);

namespace
{
constexpr double BrowseDwell=.70,ScrubDwell=.06,RevisitDwell=.70,LingerDwell=4.0,WarmupSeconds=12.0;
// Diagnostics while the probe runs: synchronous package loads (a flush on the hover path) and garbage collections.
struct FHoverDiag{int32 SyncLoads=0;double GcMs=0,GcStart=0;int32 Logged=0;bool bInstalled=false;};
FHoverDiag& HoverDiag(){static FHoverDiag D;return D;}
void HoverInstallDiagnostics()
{
    FHoverDiag& D=HoverDiag();if(D.bInstalled)return;D.bInstalled=true;
#if WITH_EDITOR || UE_ENABLE_ASSET_READ_LOGGER
    FCoreUObjectDelegates::OnEndLoadPackage.AddLambda([](const FEndLoadPackageContext& C)
    {
        if(!C.bSynchronous||C.RecursiveDepth>0||C.LoadedPackages.Num()==0)return;
        FHoverDiag& Diag=HoverDiag();++Diag.SyncLoads;
        if(Diag.Logged++<200)UE_LOG(LogCireDraftProbe,Display,TEXT("CIRE_DRAFT_HOVER_SYNCLOAD packages=%d first=%s"),C.LoadedPackages.Num(),C.LoadedPackages[0]?*C.LoadedPackages[0]->GetName():TEXT("?"));
    });
#endif
    FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddLambda([]{HoverDiag().GcStart=FPlatformTime::Seconds();});
    FCoreUObjectDelegates::GetPostGarbageCollect().AddLambda([]
    {
        FHoverDiag& Diag=HoverDiag();if(Diag.GcStart<=0)return;
        const double Ms=(FPlatformTime::Seconds()-Diag.GcStart)*1000.0;Diag.GcMs+=Ms;Diag.GcStart=0;
        UE_LOG(LogCireDraftProbe,Display,TEXT("CIRE_DRAFT_HOVER_GC ms=%.1f"),Ms);
    });
}
FString HoverJsonEscape(const FString& In){return In.Replace(TEXT("\\"),TEXT("\\\\")).Replace(TEXT("\""),TEXT("\\\""));}
}

double FCireDraftHoverProbe::Percentile(TArray<double> Values,double Q)
{
    if(Values.IsEmpty())return 0;
    Values.Sort();
    const int32 Index=FMath::Clamp(FMath::CeilToInt(Q*Values.Num())-1,0,Values.Num()-1);
    return Values[Index];
}

void FCireDraftHoverProbe::Begin(const TArray<FString>& RosterIds,double Now,const FString& Tag,int32 Count)
{
    bActive=true;bDone=false;bPassed=false;Current=-1;Plan.Reset();
    for(TArray<double>& F:AllFrames)F.Reset();
    FParse::Value(FCommandLine::Get(),TEXT("CireDraftHoverCount="),Count);
    FParse::Value(FCommandLine::Get(),TEXT("CireDraftHoverHitchMs="),HitchMs);
    Count=FMath::Clamp(Count,1,64);
    TArray<FString> Browse,Scrub;
    for(int32 I=0;I<RosterIds.Num()&&Browse.Num()<Count;I+=3)Browse.Add(RosterIds[I]);
    for(int32 I=1;I<RosterIds.Num()&&Scrub.Num()<FMath::Max(1,Count*2/3);I+=3)Scrub.Add(RosterIds[I]);
    const auto Add=[&](const TCHAR* Phase,const FString& Id){FSwitch& S=Plan.AddDefaulted_GetRef();S.Phase=Phase;S.Id=Id;};
    for(const FString& Id:Browse)Add(TEXT("browse"),Id);
    for(const FString& Id:Scrub)Add(TEXT("scrub"),Id);
    for(int32 I=0;I<Browse.Num()&&I<10;++I)Add(TEXT("revisit"),Browse[I]);
    // Linger: heroes not hovered before, held long enough for the live 3D figure (time to figure), 4 authored + 4 Paragon.
    {
        TArray<FString> Linger;
        for(int32 I=2;I<RosterIds.Num()&&Linger.Num()<4;I+=3)if(!RosterIds[I].StartsWith(TEXT("pg_")))Linger.Add(RosterIds[I]);
        for(int32 I=RosterIds.Num()-1;I>=0&&Linger.Num()<8;I-=3)if(RosterIds[I].StartsWith(TEXT("pg_"))&&!Browse.Contains(RosterIds[I])&&!Scrub.Contains(RosterIds[I]))Linger.Add(RosterIds[I]);
        for(const FString& Id:Linger)Add(TEXT("linger"),Id);
    }
    // A final settle hover so the last revisit switch has a closing window.
    Add(TEXT("end"),Browse.IsEmpty()?FString():Browse[0]);
    WarmupUntil=Now+WarmupSeconds;HardDeadline=Now+600.0;
    Directory=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("DraftHoverProbe"),FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ"))));
    if(!Tag.IsEmpty())Directory+=TEXT("_")+Tag;
    IFileManager::Get().MakeDirectory(*Directory,true);
    HoverInstallDiagnostics();
    UE_LOG(LogCireDraftProbe,Display,TEXT("CIRE_DRAFT_HOVER_BEGIN switches=%d browse=%d scrub=%d hitch_ms=%.0f directory=%s"),Plan.Num(),Browse.Num(),Scrub.Num(),HitchMs,*Directory);
}

FString FCireDraftHoverProbe::Tick(double Now,double FrameMs,const FCireDraftHoverSample& Sample)
{
    if(!bActive||bDone)return FString();
    if(Now>HardDeadline){Finish(true);return FString();}
    // Warm-up: at least 12 s, and 8 s without a long frame (the editor is still compiling textures / meshes at start).
    if(Current<0&&FrameMs>100.0)WarmupUntil=FMath::Max(WarmupUntil,Now+8.0);
    if(Now<WarmupUntil||Plan.IsEmpty())return FString();
    // A hover set at tick t is drawn in frame t+1, whose duration is measured at tick t+2: attribute with that lag.
    if(Current<0){Lag[0]=Lag[1]=-1;}
    const int32 Owner=Lag[0];
    if(Plan.IsValidIndex(Owner)&&FrameMs>0)
    {
        FSwitch& O=Plan[Owner];
        O.MaxFrameMs=FMath::Max(O.MaxFrameMs,FrameMs);++O.Frames;O.Hitches+=FrameMs>HitchMs;
        if(FrameMs>400.0)UE_LOG(LogCireDraftProbe,Display,TEXT("CIRE_DRAFT_HOVER_LONGFRAME ms=%.0f switch=%s:%s sync_loads=%d gc_ms=%.0f"),FrameMs,*O.Phase,*O.Id,HoverDiag().SyncLoads,HoverDiag().GcMs);
        const int32 PhaseIndex=O.Phase==TEXT("browse")?0:O.Phase==TEXT("scrub")?1:O.Phase==TEXT("revisit")?2:O.Phase==TEXT("linger")?3:-1;
        if(PhaseIndex>=0)AllFrames[PhaseIndex].Add(FrameMs);
    }
    const auto Start=[&](int32 Index)
    {
        FSwitch& S=Plan[Index];S.StartedAt=Now;S.Start=Sample;
        S.EndsAt=Now+(S.Phase==TEXT("scrub")?ScrubDwell:S.Phase==TEXT("revisit")?RevisitDwell:S.Phase==TEXT("linger")?(S.Id.StartsWith(TEXT("pg_"))?LingerDwell*2.5:LingerDwell):S.Phase==TEXT("end")?1.0:BrowseDwell);
    };
    if(Current<0){Current=0;Start(0);HardDeadline=Now+420.0;}
    else
    {
        FSwitch& C=Plan[Current];
        const double Age=(Now-C.StartedAt)*1000.0;
        if(C.SplashMs<0&&Sample.SplashId==C.Id)C.SplashMs=Age;
        if(C.FigureMs<0&&Sample.bFigureShown&&Sample.SplashId==C.Id)C.FigureMs=Age;
        if(C.ReadyMs<0&&Sample.bStageReady&&Sample.StageId==C.Id)C.ReadyMs=Age;
        if(Now>=C.EndsAt)
        {
            C.End=Sample;
            if(++Current>=Plan.Num()){Finish(false);return FString();}
            Start(Current);
        }
    }
    Lag[0]=Lag[1];Lag[1]=Current;
    return Plan[Current].Id;
}

void FCireDraftHoverProbe::Finish(bool bTimedOut)
{
    bDone=true;
    FString Json=TEXT("{\n  \"switches\": [\n");
    for(int32 I=0;I<Plan.Num();++I)
    {
        const FSwitch& S=Plan[I];
        const double Bg=S.End.BackgroundMs-S.Start.BackgroundMs,Pt=S.End.PortraitMs-S.Start.PortraitMs,Body=S.End.BodyMs-S.Start.BodyMs;
        const double Bind=S.End.BindMs-S.Start.BindMs,Vis=S.End.VisualsMs-S.Start.VisualsMs;
        Json+=FString::Printf(TEXT("    {\"phase\": \"%s\", \"id\": \"%s\", \"maxFrameMs\": %.2f, \"frames\": %d, \"hitches\": %d, \"backgroundMs\": %.2f, \"backgroundLoads\": %d, \"portraitMs\": %.2f, \"portraitLoads\": %d, \"bodyMs\": %.2f, \"bindMs\": %.2f, \"visualsMs\": %.2f, \"bodyShows\": %d, \"asyncRequests\": %d, \"asyncCancelled\": %d, \"cacheHits\": %d, \"splashMs\": %.1f, \"figureMs\": %.1f, \"readyMs\": %.1f}%s\n"),
            *S.Phase,*HoverJsonEscape(S.Id),S.MaxFrameMs,S.Frames,S.Hitches,Bg,S.End.BackgroundLoads-S.Start.BackgroundLoads,Pt,S.End.PortraitLoads-S.Start.PortraitLoads,Body,Bind,Vis,S.End.BodyShows-S.Start.BodyShows,
            S.End.AsyncRequests-S.Start.AsyncRequests,S.End.AsyncCancelled-S.Start.AsyncCancelled,S.End.CacheHits-S.Start.CacheHits,S.SplashMs,S.FigureMs,S.ReadyMs,I+1<Plan.Num()?TEXT(","):TEXT(""));
        if(S.Phase!=TEXT("end"))
            UE_LOG(LogCireDraftProbe,Display,TEXT("CIRE_DRAFT_HOVER_SWITCH phase=%s id=%s max_frame_ms=%.1f hitches=%d bg_ms=%.1f portrait_ms=%.1f body_ms=%.1f bind_ms=%.1f visuals_ms=%.1f shows=%d splash_ms=%.0f figure_ms=%.0f ready_ms=%.0f"),
                *S.Phase,*S.Id,S.MaxFrameMs,S.Hitches,Bg,Pt,Body,Bind,Vis,S.End.BodyShows-S.Start.BodyShows,S.SplashMs,S.FigureMs,S.ReadyMs);
    }
    Json+=TEXT("  ],\n  \"summary\": {\n");
    static const TCHAR* Phases[4]={TEXT("browse"),TEXT("scrub"),TEXT("revisit"),TEXT("linger")};
    int32 Summaries=0;
    for(int32 P=0;P<4;++P)
    {
        TArray<double> Stall,Ready,Splash,Body,Bg;int32 Hitches=0,N=0,NotReady=0,Shows=0;
        for(const FSwitch& S:Plan)if(S.Phase==Phases[P])
        {
            ++N;Stall.Add(S.MaxFrameMs);Hitches+=S.Hitches;
            Body.Add(S.End.BodyMs-S.Start.BodyMs);Bg.Add(S.End.BackgroundMs-S.Start.BackgroundMs);Shows+=S.End.BodyShows-S.Start.BodyShows;
            if(S.SplashMs>=0)Splash.Add(S.SplashMs);
            if(S.ReadyMs>=0)Ready.Add(S.ReadyMs);else ++NotReady;
        }
        const auto Avg=[](const TArray<double>& V){double Sum=0;for(double X:V)Sum+=X;return V.Num()?Sum/V.Num():0.0;};
        const auto Max=[](const TArray<double>& V){double M=0;for(double X:V)M=FMath::Max(M,X);return M;};
        UE_LOG(LogCireDraftProbe,Display,TEXT("CIRE_DRAFT_HOVER_SUMMARY phase=%s switches=%d stall_avg_ms=%.1f stall_p95_ms=%.1f stall_max_ms=%.1f hitches=%d frame_p95_ms=%.1f body_avg_ms=%.1f body_max_ms=%.1f body_shows=%d bg_avg_ms=%.1f bg_max_ms=%.1f splash_avg_ms=%.0f ready_avg_ms=%.0f ready_p95_ms=%.0f not_ready=%d"),
            Phases[P],N,Avg(Stall),Percentile(Stall,.95),Max(Stall),Hitches,Percentile(AllFrames[P],.95),Avg(Body),Max(Body),Shows,Avg(Bg),Max(Bg),Avg(Splash),Avg(Ready),Percentile(Ready,.95),NotReady);
        Json+=FString::Printf(TEXT("%s    \"%s\": {\"switches\": %d, \"stallAvgMs\": %.2f, \"stallP95Ms\": %.2f, \"stallMaxMs\": %.2f, \"hitches\": %d, \"frameP95Ms\": %.2f, \"bodyAvgMs\": %.2f, \"bodyMaxMs\": %.2f, \"bodyShows\": %d, \"backgroundAvgMs\": %.2f, \"backgroundMaxMs\": %.2f, \"splashAvgMs\": %.1f, \"readyAvgMs\": %.1f, \"readyP95Ms\": %.1f, \"notReady\": %d}"),
            Summaries++?TEXT(",\n"):TEXT(""),Phases[P],N,Avg(Stall),Percentile(Stall,.95),Max(Stall),Hitches,Percentile(AllFrames[P],.95),Avg(Body),Max(Body),Shows,Avg(Bg),Max(Bg),Avg(Splash),Avg(Ready),Percentile(Ready,.95),NotReady);
    }
    Json+=FString::Printf(TEXT("\n  },\n  \"hitchMs\": %.1f,\n  \"timedOut\": %s\n}\n"),HitchMs,bTimedOut?TEXT("true"):TEXT("false"));
    FFileHelper::SaveStringToFile(Json,*FPaths::Combine(Directory,TEXT("hover.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    bPassed=!bTimedOut;
    // Optional budget (after the optimisation): fail when the browse p95 stall exceeds it.
    double Budget=0;
    if(FParse::Value(FCommandLine::Get(),TEXT("CireDraftHoverBudgetMs="),Budget)&&Budget>0)
    {
        TArray<double> Stall;for(const FSwitch& S:Plan)if(S.Phase==TEXT("browse")||S.Phase==TEXT("revisit"))Stall.Add(S.MaxFrameMs);
        const double P95=Percentile(Stall,.95);
        if(P95>Budget){bPassed=false;UE_LOG(LogCireDraftProbe,Error,TEXT("CIRE_DRAFT_HOVER_BUDGET_FAIL p95=%.1f budget=%.1f"),P95,Budget);}
    }
    UE_LOG(LogCireDraftProbe,Display,TEXT("CIRE_DRAFT_HOVER_%s switches=%d directory=%s"),bPassed?TEXT("PASS"):TEXT("FAIL"),Plan.Num(),*Directory);
    FPlatformMisc::RequestExitWithStatus(false,bPassed?0:1);
}
