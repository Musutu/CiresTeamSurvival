#include "CireTooltipGallery.h"
#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireHUD.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

namespace
{
struct FCase {const TCHAR* Id;const TCHAR* Label;int32 Mode;float Scale;FVector2D Cursor;};
const FCase Cases[]={
    {TEXT("cursor_small_edge"),TEXT("1 / Cursor / 60% / lower-right edge"),0,.6f,FVector2D(1276,716)},
    {TEXT("cursor_large_edge"),TEXT("2 / Cursor / 140% / lower-right edge"),0,1.4f,FVector2D(1276,716)},
    {TEXT("fixed_small_edge"),TEXT("3 / Fixed / 60% / lower-right anchor"),1,.6f,FVector2D(640,360)},
    {TEXT("fixed_large_edge"),TEXT("4 / Fixed / 140% / lower-right anchor"),1,1.4f,FVector2D(640,360)},
    {TEXT("radial_default_edge"),TEXT("5 / Radial / 80% / upper-left edge"),2,.8f,FVector2D(4,4)},
    {TEXT("radial_large_edge"),TEXT("6 / Radial / 140% / upper-left edge"),2,1.4f,FVector2D(4,4)}
};
const FString Description=TEXT("Summon a protective barrier at the aimed location. The barrier blocks incoming hostile projectiles and stops enemies from walking through it until its health is depleted or its duration expires. Allies can reposition around the barrier while ranged attackers continue to seek a clear line of fire. Use the tooltip size control to adjust this description without moving other interface panels. This final sentence confirms that the complete long description remains visible after wrapping.");
struct FGallery
{
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACireHUD> HUD;
    TWeakObjectPtr<ACireController> Controller;
    FString Directory;
    TArray<TSharedPtr<FJsonValue>> Captures;
    TArray<FString> Files;
    TArray<FCireUIRect> Rects;
    TArray<float> Fonts;
    double Start=0,Ready=-1;
    int32 Stage=INDEX_NONE,Captured=INDEX_NONE,Checks=0;
    bool bPass=true,bDone=false;
} G;
bool Check(bool Value,const FString& Why)
{
    ++G.Checks;
    if(!Value){G.bPass=false;UE_LOG(LogTemp,Error,TEXT("CIRE_TOOLTIP_CHECK_FAIL %s"),*Why);}
    return Value;
}
TSharedPtr<FJsonObject> JsonVector(FVector2D V)
{
    auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("x"),V.X);O->SetNumberField(TEXT("y"),V.Y);return O;
}
void Finish(bool Passed)
{
    if(G.bDone)return;G.bDone=true;
    auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("schemaVersion"),1);Root->SetBoolField(TEXT("passed"),Passed);
    Root->SetBoolField(TEXT("visualReviewAccepted"),false);Root->SetNumberField(TEXT("checks"),G.Checks);
    Root->SetStringField(TEXT("body"),Description);Root->SetArrayField(TEXT("captures"),G.Captures);
    FString Serialized;
    const bool Saved=FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Serialized))&&
        FFileHelper::SaveStringToFile(Serialized,*FPaths::Combine(G.Directory,TEXT("manifest.json")));
    if(G.HUD.IsValid())G.HUD->DebugTooltipClear();
    Passed &= Saved;
    UE_LOG(LogTemp,Display,TEXT("CIRE_TOOLTIP_GALLERY_%s captures=%d checks=%d directory=%s"),Passed?TEXT("PASS"):TEXT("FAIL"),G.Files.Num(),G.Checks,*G.Directory);
    FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);
}
bool Setup(ACireController* PC,ACireHUD* HUD)
{
    auto* H=Cast<ACireHero>(PC->GetPawn());if(!H)return false;
    G.HUD=HUD;G.Controller=PC;
    HUD->UISettings.Load(FPaths::Combine(G.Directory,TEXT("isolated-profile.ini")));
    HUD->UISettings.bTooltips=true;HUD->UISettings.bShowFPS=false;HUD->UISettings.bShowNetwork=false;
    HUD->UISettings.bLayoutLocked=false;HUD->UISettings.SetPanelLocked(TEXT("Tooltip"),false);
    Check(HUD->UISettings.SetRect(TEXT("Tooltip"),{1120,610,340,148},FVector2D(1280,720)),TEXT("fixed anchor editable"));
    HUD->UISettings.bLayoutLocked=true;
    HUD->DebugOptionsPage(0,0,false);
    H->Draft(0);H->Offers.Reset();H->bBot=false;H->bAutoAttack=false;H->SetActorTickEnabled(false);
    H->GetCharacterMovement()->DisableMovement();H->Notice.Reset();PC->bShop=false;PC->bHelp=false;
    G.Ready=FPlatformTime::Seconds();return true;
}
void Configure(int32 Stage)
{
    const auto& C=Cases[Stage];auto* HUD=G.HUD.Get();
    HUD->UISettings.TooltipMode=C.Mode;HUD->UISettings.TooltipScale=C.Scale;
    HUD->UISettings.TooltipAngleDegrees=225;HUD->UISettings.TooltipDistance=180;
    HUD->DebugTooltip(C.Label,Description,C.Cursor);G.Stage=Stage;
}
void Capture(int32 Stage)
{
    const auto& C=Cases[Stage];const auto* HUD=G.HUD.Get();
    const FCireUIRect R=HUD->DebugTooltipRect();const FVector2D View=HUD->DebugTooltipViewport();
    const int32 Lines=HUD->DebugTooltipBodyLines();const float Font=HUD->DebugTooltipBodyFontSize();
    int32 W=0,H=0;G.Controller->GetViewportSize(W,H);Check(W==1920&&H==1080,TEXT("exact rendered viewport"));
    Check(FMath::IsFinite(R.X)&&FMath::IsFinite(R.Y)&&FMath::IsFinite(R.W)&&FMath::IsFinite(R.H)&&R.W>0&&R.H>0,TEXT("finite measured rectangle"));
    Check(R.X>=3.5f&&R.Y>=3.5f&&R.X+R.W<=View.X-3.5f&&R.Y+R.H<=View.Y-3.5f,TEXT("tooltip clamped within viewport"));
    Check(Lines>=3&&FMath::IsFinite(Font)&&Font>0,TEXT("long description wraps with a positive font size"));
    Check(FMath::IsNearlyEqual(HUD->UISettings.TooltipScale,C.Scale),TEXT("requested scale applied"));
    Check(HUD->UISettings.GetFilename()==FPaths::Combine(G.Directory,TEXT("isolated-profile.ini")),TEXT("player settings isolated"));
    if(C.Mode==2)Check(R.X<=4.5f&&R.Y<=4.5f,TEXT("radial upper-left clamp exercised"));
    if(Stage%2==1&&G.Rects.Num()>0)
    {
        const auto& Before=G.Rects.Last();
        Check(R.W>Before.W&&R.H>Before.H&&Font>G.Fonts.Last(),TEXT("larger scale grows measured box and text"));
    }
    const FString File=FPaths::Combine(G.Directory,FString(C.Id)+TEXT(".png"));
    auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("id"),C.Id);O->SetNumberField(TEXT("mode"),C.Mode);
    O->SetNumberField(TEXT("scale"),C.Scale);O->SetStringField(TEXT("title"),C.Label);
    O->SetObjectField(TEXT("cursor"),JsonVector(C.Cursor));O->SetObjectField(TEXT("logicalViewport"),JsonVector(View));
    auto Rect=MakeShared<FJsonObject>();Rect->SetNumberField(TEXT("x"),R.X);Rect->SetNumberField(TEXT("y"),R.Y);
    Rect->SetNumberField(TEXT("w"),R.W);Rect->SetNumberField(TEXT("h"),R.H);O->SetObjectField(TEXT("rect"),Rect);
    O->SetNumberField(TEXT("bodyLines"),Lines);O->SetNumberField(TEXT("bodyFontSize"),Font);
    G.Captures.Add(MakeShared<FJsonValueObject>(O));G.Rects.Add(R);G.Fonts.Add(Font);G.Files.Add(File);
    FScreenshotRequest::RequestScreenshot(File,false,false,false,FIntRect(),true);G.Captured=Stage;
}
}
bool CireTooltipGallery::Initialize(ACireGameMode* Mode)
{
    G={};if(!FParse::Param(FCommandLine::Get(),TEXT("CireTooltipGallery")))return false;
    G.Mode=Mode;G.Start=FPlatformTime::Seconds();
    G.Directory=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("TooltipGallery"),
        FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))+TEXT("-")+FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(6)));
    if(!Mode||Mode->GetNetMode()!=NM_Standalone||!IFileManager::Get().MakeDirectory(*G.Directory,true)){Finish(false);return true;}
    Mode->bBotsFilled=true;Mode->BotFillTimer=MAX_flt;Mode->WaveTimer=MAX_flt;return true;
}
bool CireTooltipGallery::Tick(ACireGameMode* Mode)
{
    if(G.Mode.Get()!=Mode)return false;if(G.bDone)return true;
    if(FPlatformTime::Seconds()-G.Start>60){Finish(false);return true;}
    if(G.Ready<0)
    {
        auto* PC=Cast<ACireController>(Mode->GetWorld()->GetFirstPlayerController());auto* HUD=PC?Cast<ACireHUD>(PC->GetHUD()):nullptr;
        if(PC&&HUD&&PC->GetPawn()&&!Setup(PC,HUD))Finish(false);return true;
    }
    if(!G.HUD.IsValid()||!G.Controller.IsValid()){Finish(false);return true;}
    const double Age=FPlatformTime::Seconds()-G.Ready;
    const int32 Stage=FMath::Clamp(FMath::FloorToInt((Age-3)/3),0,5);
    if(G.Stage!=Stage)Configure(Stage);
    if(Age>=5+Stage*3&&G.Captured<Stage)Capture(Stage);
    if(Age>23)
    {
        for(const auto& File:G.Files)Check(IFileManager::Get().FileSize(*File)>10000,TEXT("capture saved"));
        Check(G.Files.Num()==UE_ARRAY_COUNT(Cases),TEXT("exactly six captures"));Finish(G.bPass);
    }
    return true;
}
#endif
