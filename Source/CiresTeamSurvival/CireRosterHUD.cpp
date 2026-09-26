// Champion draft screen (League/Dota-style select): a numbered step flow
// (1 choose a role -> 2 pick your champion -> 3 lock in), role filter tabs over a
// grid of large portrait cards, a splash showcase of the live 3D champion on a
// role-coloured backdrop, the team strip and a pulsing LOCK IN button. Every
// rectangle is derived from the logical viewport, so the screen fits 16:9,
// 16:10, 4:3, ultrawide and small windows at any interface scale. Drawn by
// ACireHUD while the local hero is undrafted; a short "LOCKED IN" outro plays
// over the opening-ability offer after the lock is confirmed.
#include "CireHUD.h"
#include "CireChampionRoster.h"
#include "CireChampionProfiles.h"
#include "CireDraftStage.h"
#include "CireAbilityIcons.h"
#include "CireClassTraits.h"
#include "CireUIStyle.h"
#include "CireUITheme.h"
#include "CireGame.h"
#include "CireSummon.h"
#include "CireItems.h"    // rules-conformance: game-mode picker (host RPC on the hero's inventory)
#include "CireShopArt.h"  // rules-conformance: scroll / crest icons for the mode picker
#include "CireSkillShop.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "EngineFontServices.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "ImageUtils.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "Dom/JsonObject.h" // new-champions: DraftBackgrounds.json
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "ContentStreaming.h"
#include "UObject/Package.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireDraft,Log,All);

namespace
{
constexpr int32 PageSize=6;
constexpr float OutroSeconds=1.9f;
constexpr int32 TeamSlots=5;
FLinearColor SRGB(uint8 R,uint8 G,uint8 B,uint8 A=255){return FLinearColor::FromSRGBColor(FColor(R,G,B,A));}
// Scale a colour's perceived (sRGB) brightness; used for role-tinted backgrounds and borders.
FLinearColor Tint(const FLinearColor& Color,float K,float Alpha=1.f)
{const FColor C=Color.ToFColor(true);FLinearColor Out=FLinearColor::FromSRGBColor(FColor(uint8(FMath::Min(255.f,C.R*K)),uint8(FMath::Min(255.f,C.G*K)),uint8(FMath::Min(255.f,C.B*K))));Out.A=Alpha;return Out;}
FLinearColor WithAlpha(FLinearColor C,float A){C.A=A;return C;}
// champ-select-hq: champion select follows the chosen UI theme (UI consistency ruling). Its chrome was
// authored in Gilded Citadel; ThemeUI() transfers a chrome colour to the active theme by the per-channel ratio
// between the two themes' matching palette entry (dark panels -> Ink, neutral text -> Text, warm trim ->
// Trim, cool accents -> Accent). Gilded Citadel draws exactly as authored. Role, stat and lock colours
// are semantic and stay fixed.
FLinearColor ThemeUI(uint8 R,uint8 G,uint8 B,uint8 A=255)
{
    const FLinearColor C=SRGB(R,G,B,A);
    const FCireUITheme* Now=CireUITheme::Active();const FCireUITheme* Base=CireUITheme::Find(CireUITheme::DefaultId());
    if(!Now||!Base||Now==Base)return C;
    const float Hi=FMath::Max3(C.R,C.G,C.B),Lo=FMath::Min3(C.R,C.G,C.B),Sat=Hi>1e-4f?(Hi-Lo)/Hi:0.f;
    const bool bDark=C.GetLuminance()<.05f;
    const FLinearColor& From=bDark?Base->Ink:Sat<.3f?Base->Text:(C.R>=C.B?Base->Trim:Base->Accent);
    const FLinearColor& To=bDark?Now->Ink:Sat<.3f?Now->Text:(C.R>=C.B?Now->Trim:Now->Accent);
    const auto Ratio=[](float T,float F){return FMath::Clamp(T/FMath::Max(F,.004f),.2f,5.f);};
    return FLinearColor(FMath::Min(1.f,C.R*Ratio(To.R,From.R)),FMath::Min(1.f,C.G*Ratio(To.G,From.G)),FMath::Min(1.f,C.B*Ratio(To.B,From.B)),C.A);
}
FLinearColor Backdrop,Ink,Card,CardHi,Gold,GoldDim,BrightGold,Text,Muted,Faint;
void RefreshDraftPalette()
{
    Backdrop=ThemeUI(9,10,13);Ink=ThemeUI(16,18,23,248);Card=ThemeUI(24,27,33);CardHi=ThemeUI(40,44,52);
    Gold=ThemeUI(214,170,98);GoldDim=ThemeUI(104,82,48);BrightGold=ThemeUI(255,214,120);Text=ThemeUI(236,230,214);Muted=ThemeUI(150,154,156);Faint=ThemeUI(58,62,66);
}
const FLinearColor TankColor=SRGB(92,148,228),DpsColor=SRGB(216,80,64),SupportColor=SRGB(88,198,126);
const FLinearColor LockRed=SRGB(150,34,34),PassiveColor=SRGB(190,156,236);

struct FRoleColumn { Cires::SkillDraftRole Role; const TCHAR* Title; const TCHAR* Sigil; const TCHAR* Blurb; FLinearColor Color; };
const FRoleColumn Columns[3]={
    {Cires::SkillDraftRole::Tank,TEXT("TANK"),TEXT("role0"),TEXT("Hold threat, guard allies, endure"),TankColor},
    {Cires::SkillDraftRole::Damage,TEXT("DPS"),TEXT("cleaving_strike"),TEXT("Burst, sustained and area damage"),DpsColor},
    {Cires::SkillDraftRole::Support,TEXT("SUPPORT"),TEXT("role2"),TEXT("Heal, cleanse, shield and enable"),SupportColor}};

FLinearColor RoleColor(Cires::SkillDraftRole Role)
{return Role==Cires::SkillDraftRole::Tank?TankColor:Role==Cires::SkillDraftRole::Support?SupportColor:DpsColor;}
const TCHAR* RoleName(Cires::SkillDraftRole Role)
{return Role==Cires::SkillDraftRole::Tank?TEXT("TANK"):Role==Cires::SkillDraftRole::Support?TEXT("SUPPORT"):TEXT("DPS");}
const TCHAR* RoleSigil(Cires::SkillDraftRole Role)
{return Role==Cires::SkillDraftRole::Tank?TEXT("role0"):Role==Cires::SkillDraftRole::Support?TEXT("role2"):TEXT("cleaving_strike");}
int32 RoleIndex(Cires::SkillDraftRole Role)
{return Role==Cires::SkillDraftRole::Tank?0:Role==Cires::SkillDraftRole::Support?2:1;}
Cires::SkillDraftRole RoleForBit(Cires::RoleMask Bit)
{return Bit==Cires::RoleTank?Cires::SkillDraftRole::Tank:Bit==Cires::RoleSupport?Cires::SkillDraftRole::Support:Cires::SkillDraftRole::Damage;}
FString PrimaryName(const FString& Primary)
{return Primary==TEXT("strength")?TEXT("STRENGTH"):Primary==TEXT("agility")?TEXT("AGILITY"):TEXT("INTELLIGENCE");}
FString StyleLabel(const FString& Style)
{
    if(Style==TEXT("axes"))return TEXT("Axes");
    if(Style==TEXT("arcane"))return TEXT("Arcane focus");
    FString Result=Style;if(!Result.IsEmpty())Result[0]=FChar::ToUpper(Result[0]);return Result;
}
const TCHAR* DifficultyWord(int32 D){return D<=1?TEXT("EASY"):D==2?TEXT("MODERATE"):TEXT("HARD");}
TArray<Cires::SkillDraftRole> HybridRoles(const FCireChampionProfile& P)
{
    TArray<Cires::SkillDraftRole> Out;const auto Primary=CireChampionProfiles::PrimaryRole(P);
    for(const Cires::RoleMask Bit:{Cires::RoleTank,Cires::RoleDamage,Cires::RoleSupport})
        if((CireChampionProfiles::ProfileRoleMask(P)&Bit)&&RoleForBit(Bit)!=Primary)Out.Add(RoleForBit(Bit));
    return Out;
}
// One line a first-time player can act on: what this champion does in a fight.
FString Playstyle(const FCireChampionProfile& P)
{
    const auto Role=CireChampionProfiles::PrimaryRole(P);const bool bRanged=P.BasicAttackRange>300;
    FString Line=Role==Cires::SkillDraftRole::Tank?FString(TEXT("Frontline tank: holds threat and shields allies.")):
        Role==Cires::SkillDraftRole::Support?FString(bRanged?TEXT("Support: heals, cleanses and empowers allies from the back line."):TEXT("Support: heals and protects allies from the front line.")):
        FString(bRanged?TEXT("Ranged damage: pressures enemies from a safe distance."):TEXT("Melee damage: dives in for burst and sustained damage."));
    const TArray<Cires::SkillDraftRole> Extra=HybridRoles(P);
    for(int32 I=0;I<Extra.Num();++I)Line+=FString(I==0?TEXT(" Also fills "):TEXT(" and "))+RoleName(Extra[I]);
    if(Extra.Num()>0)Line+=TEXT(".");
    return Line;
}
// Planned abilities have no dedicated sigil yet: pick one from how they are delivered.
FString SkillSigil(const FCireChampionSkill& Skill)
{
    if(Skill.IsImplemented())return Skill.Id;
    const FString& D=Skill.Delivery;
    if(D==TEXT("passive"))return TEXT("battle_rhythm");
    if(D==TEXT("ally"))return TEXT("restoring_light");
    if(D==TEXT("self"))return TEXT("iron_guard");
    if(D==TEXT("chain"))return TEXT("chain_spark");
    if(D==TEXT("projectile"))return TEXT("piercing_shot");
    if(D==TEXT("summon"))return TEXT("role4");
    if(D==TEXT("construct"))return TEXT("runic_wall");
    if(D==TEXT("transformation"))return TEXT("shadow_step");
    if(D==TEXT("ground_circle")||D==TEXT("ground_square")||D==TEXT("ground_polygon"))return TEXT("sanctuary");
    if(D==TEXT("ground_line")||D==TEXT("ground_cone"))return TEXT("war_cry");
    return TEXT("cleaving_strike");
}
int32 RosterSize(){return CireChampionRoster::Count()>0?CireChampionRoster::Count():5;}

struct FRect
{
    float X=0,Y=0,W=0,H=0;
    float R() const {return X+W;}
    float B() const {return Y+H;}
    bool Contains(const FRect& O,float Tol=1.f) const {return O.X>=X-Tol&&O.Y>=Y-Tol&&O.R()<=R()+Tol&&O.B()<=B()+Tol;}
    FRect Inset(float D) const {return FRect{X+D,Y+D,W-2*D,H-2*D};}
};

struct FTile
{
    const FCireChampionProfile* Profile=nullptr;
    int32 Column=0;          // role index of this listing (0 tank, 1 dps, 2 support)
    bool bSecondary=false;   // hybrid listing in a role that is not its primary
    float X=0,Y=0,W=0,H=0;
};
// Layout audit (gallery only): every text run and card with the box it must stay inside.
struct FAuditItem { FString What; FRect Rect,Box; float Size=0; int32 Kind=0; }; // Kind 0 text, 1 card name, 2 card, 3 title

struct FPortraitFixture
{
    bool bActive=false,bDone=false,bPass=true;
    TArray<FString> Ids;TArray<FString> Files;int32 Index=-1;uint64 StreamedFrame=0;int32 Attempt=0;
    TMap<FString,float> Exposure;
    TStrongObjectPtr<UTextureRenderTarget2D> Target;
    FString Directory;double Started=0;
};
struct FGalleryFixture
{
    bool bActive=false,bDone=false,bPass=true,bShotThisFrame=false;
    int32 Stage=-1;double StageAt=0,Started=0,ShotAt=0;
    FString Directory;TArray<FString> Files;FString PendingShot;
    int32 LayoutFailures=0;
};

struct FDraftUI
{
    TWeakObjectPtr<ACireDraftStage> Stage;
    TArray<FTile> Tiles;
    int32 Cursor=0;
    int32 Filter=-1;                 // -1 all champions, else role index
    bool bChosen=false,bDrawer=false;
    FString SelectedId,CursorId;
    FString Hovered,ForcedHover,LastClickId;
    double HoverSince=0,LastClickAt=0,LockRequestedAt=-100;
    FString LockRequestedId;
    FString SplashId;double SplashSince=0;
    double OpenAt=-100,OutroStart=-100;FString OutroId;bool bForceOutro=false;
    FString SentHoverId;             // last selection told to the server (teammates see it)
    float ForcedTimer=-1.f;          // gallery: timer value to display
    int32 InfoTab=0;                 // 0 overview, 1 abilities, 2 lore
    bool bDropdown=false;
    FString BgId,PrevBgId;double BgSince=-100;
    bool bAudit=false;TArray<FAuditItem> Audit;
    FPortraitFixture Portraits;
    FGalleryFixture Gallery;
    bool bInitialized=false;
    FRect FigurePx;float FigureUV[4]={0,0,1,1}; // last live figure draw (gallery diagnostics)
    uint8 LastMode=255;double ModeFlashAt=-100; // rules-conformance: game-mode picker feedback (any client sees the host's change)
};
TMap<TWeakObjectPtr<const ACireHUD>,FDraftUI> States;
FDraftUI& StateFor(const ACireHUD* HUD)
{
    for(auto It=States.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
    return States.FindOrAdd(HUD);
}

UTexture2D* Portrait(const FString& Id)
{
    // Generated from the real champion meshes by Tools/RunDraftPortraits.py.
    static TMap<FString,TStrongObjectPtr<UTexture2D>> Cache;
    if(const auto* Found=Cache.Find(Id))return Found->Get();
    const FString Path=FString::Printf(TEXT("/Game/UI/Draft/Portraits/T_Portrait_%s.T_Portrait_%s"),*Id,*Id);
    UTexture2D* Texture=FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Path))?LoadObject<UTexture2D>(nullptr,*Path):nullptr;
    Cache.Add(Id,TStrongObjectPtr<UTexture2D>(Texture));
    return Texture;
}

int32 Nearest(const TArray<FTile>& Tiles,int32 From,int32 DX,int32 DY)
{
    if(!Tiles.IsValidIndex(From))return Tiles.Num()>0?0:INDEX_NONE;
    const FVector2D A(Tiles[From].X+Tiles[From].W*.5f,Tiles[From].Y+Tiles[From].H*.5f);
    int32 Best=From;float BestScore=MAX_flt;
    for(int32 I=0;I<Tiles.Num();++I)
    {
        if(I==From)continue;
        const FVector2D B(Tiles[I].X+Tiles[I].W*.5f,Tiles[I].Y+Tiles[I].H*.5f),D=B-A;
        const float Along=DX!=0?D.X*DX:D.Y*DY,Across=DX!=0?FMath::Abs(D.Y):FMath::Abs(D.X);
        if(Along<=1.f)continue;
        const float Score=Along+Across*2.5f;
        if(Score<BestScore){BestScore=Score;Best=I;}
    }
    return Best;
}
// Keyboard/number-key browsing selects what it lands on.
void ChooseTile(FDraftUI& S,int32 Index)
{
    if(!S.Tiles.IsValidIndex(Index))return;
    S.Cursor=Index;S.CursorId=S.Tiles[Index].Profile->Id;S.SelectedId=S.CursorId;S.bChosen=true;
    S.Hovered.Reset();S.ForcedHover.Reset();
}

// Rendered line height (logical units) of the style kit's font at this size.
float LineHeight(const FCireUIPainter& P,float Size,ECireFont Font)
{
    int32 Index=INDEX_NONE;UFont* Resolved=CireUIStyle::ResolveFont(Font,TEXT("Ag"),Size,&Index);
    if(!Resolved||Index==INDEX_NONE)return Size*1.3f;
    auto Measure=FEngineFontServices::Get().GetFontMeasure();
    if(!Measure.IsValid())return Size*1.3f;
    const float KS=FMath::Max(.05f,P.Scale);
    const float Points=FMath::Max(4.f,FMath::RoundToFloat(Size*CireUIStyle::Assets().Calibration[Index]*KS));
    return static_cast<float>(Measure->GetMaxCharacterHeight(FSlateFontInfo(Resolved,Points),1.f))/KS;
}
// Greedy word wrap into at most MaxLines; the last line gets the kit's ".." ellipsis when cut.
TArray<FString> WrapLines(const FCireUIPainter& P,const FString& Body,float Width,float Size,ECireFont Font,int32 MaxLines)
{
    TArray<FString> Words;Body.ParseIntoArrayWS(Words);TArray<FString> Lines;FString Row;
    for(int32 I=0;I<Words.Num();++I)
    {
        const FString Next=Row.IsEmpty()?Words[I]:Row+TEXT(" ")+Words[I];
        if(!Row.IsEmpty()&&P.TextWidth(Next,Size,Font)>Width)
        {
            if(Lines.Num()+1>=MaxLines)
            {
                FString Rest=Row;for(int32 K=I;K<Words.Num();++K)Rest+=TEXT(" ")+Words[K];
                Lines.Add(P.Fit(Rest,Size,Width,Font));return Lines;
            }
            Lines.Add(P.Fit(Row,Size,Width,Font));Row=Words[I];
        }
        else Row=Next;
    }
    if(!Row.IsEmpty()&&MaxLines>0)Lines.Add(P.Fit(Row,Size,Width,Font));
    return Lines;
}
// Champion name on one line, or balanced over two; never wider than the card.
TArray<FString> NameLines(const FCireUIPainter& P,const FString& Name,float Size,float Width,ECireFont Font)
{
    TArray<FString> Lines;
    if(P.TextWidth(Name,Size,Font)<=Width){Lines.Add(Name);return Lines;}
    TArray<FString> W;Name.ParseIntoArrayWS(W);
    if(W.Num()<2){Lines.Add(P.Fit(Name,Size,Width,Font));return Lines;}
    const auto Join=[&](int32 From,int32 To){FString Out;for(int32 K=From;K<To;++K)Out+=(Out.IsEmpty()?TEXT(""):TEXT(" "))+W[K];return Out;};
    int32 Best=1;float BestWidth=MAX_flt;
    for(int32 Split=1;Split<W.Num();++Split)
    {
        const float Wd=FMath::Max(P.TextWidth(Join(0,Split),Size,Font),P.TextWidth(Join(Split,W.Num()),Size,Font));
        if(Wd<BestWidth){BestWidth=Wd;Best=Split;}
    }
    Lines.Add(P.Fit(Join(0,Best),Size,Width,Font));Lines.Add(P.Fit(Join(Best,W.Num()),Size,Width,Font));
    return Lines;
}
// champ-select-hq: the name balanced over two lines (unfitted), for shrink-to-fit.
TArray<FString> BalancedLines(const FCireUIPainter& P,const FString& Name,float Size,ECireFont Font)
{
    TArray<FString> W;Name.ParseIntoArrayWS(W);TArray<FString> Lines;
    if(W.Num()<2){Lines.Add(Name);return Lines;}
    const auto Join=[&](int32 From,int32 To){FString Out;for(int32 K=From;K<To;++K)Out+=(Out.IsEmpty()?TEXT(""):TEXT(" "))+W[K];return Out;};
    int32 Best=1;float BestWidth=MAX_flt;
    for(int32 Split=1;Split<W.Num();++Split)
    {
        const float Wd=FMath::Max(P.TextWidth(Join(0,Split),Size,Font),P.TextWidth(Join(Split,W.Num()),Size,Font));
        if(Wd<BestWidth){BestWidth=Wd;Best=Split;}
    }
    Lines.Add(Join(0,Best));Lines.Add(Join(Best,W.Num()));
    return Lines;
}
// Per-champion painted backdrop (Content/UI/Draft/Backgrounds); variants of one body share it.
// new-champions: Content/Data/DraftBackgrounds.json names a painting per champion that has none yet, and the
// role-themed painting of an existing champion to show until it is painted (Tools/AuthorNewChampions.py).
struct FDraftBackgroundRow{FString Background,Fallback,Mood;};
const TMap<FString,FDraftBackgroundRow>& DraftBackgroundRows()
{
    static TMap<FString,FDraftBackgroundRow> Rows;static bool bLoaded=false;
    if(bLoaded)return Rows;bLoaded=true;
    FString Json;TSharedPtr<FJsonObject> Root;const TSharedPtr<FJsonObject>* Champions=nullptr;
    if(!FFileHelper::LoadFileToString(Json,*FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/DraftBackgrounds.json")))||Json.Len()>65536||
       !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root||!Root->TryGetObjectField(TEXT("champions"),Champions))return Rows;
    for(const auto& Pair:(*Champions)->Values)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;FDraftBackgroundRow Row;
        if(!Pair.Value->TryGetObject(O)||!(*O)->TryGetStringField(TEXT("background"),Row.Background)||!(*O)->TryGetStringField(TEXT("fallback"),Row.Fallback))continue;
        (*O)->TryGetStringField(TEXT("mood"),Row.Mood);Rows.Add(FString(Pair.Key),Row);
    }
    return Rows;
}
bool HasBackgroundTexture(const FString& Id)
{
    static TMap<FString,bool> Known; // package lookups hit the disk: once per id
    if(const bool* Found=Known.Find(Id))return *Found;
    const FString Path=FString::Printf(TEXT("/Game/UI/Draft/Backgrounds/T_DraftBg_%s.T_DraftBg_%s"),*Id,*Id);
    return Known.Add(Id,FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Path)));
}
FString BackgroundId(const FString& ProfileId)
{
    // champ-select-hq: every champion has its own painting; a variant without one yet shares its body family's.
    if(HasBackgroundTexture(ProfileId))return ProfileId;
    for(const TCHAR* Family:{TEXT("ether_golem"),TEXT("paladin"),TEXT("troll_berserker")})if(ProfileId.StartsWith(Family))return Family;
    if(const FDraftBackgroundRow* Row=DraftBackgroundRows().Find(ProfileId))
        return HasBackgroundTexture(Row->Background)?Row->Background:Row->Fallback; // new-champions: painted slot, or the role-themed stand-in
    return ProfileId;
}
UTexture2D* Background(const FString& Id)
{
    static TMap<FString,TStrongObjectPtr<UTexture2D>> Cache;
    if(const auto* Found=Cache.Find(Id))return Found->Get();
    const FString Path=FString::Printf(TEXT("/Game/UI/Draft/Backgrounds/T_DraftBg_%s.T_DraftBg_%s"),*Id,*Id);
    UTexture2D* Texture=FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Path))?LoadObject<UTexture2D>(nullptr,*Path):nullptr;
    Cache.Add(Id,TStrongObjectPtr<UTexture2D>(Texture));
    return Texture;
}
// Short, list-free guidance on what this champion's skills are like (skills come from the Skill Shop).
FString Capitalized(FString S){if(!S.IsEmpty())S[0]=FChar::ToUpper(S[0]);return S;}
TArray<FString> HowItPlays(const FCireChampionProfile& P)
{
    // champ-select-hq: Eric's ruling: a brief description of the class's skills (the role pool the
    // champion drafts from), never a signature-kit list.
    TArray<FString> Out;
    const auto Primary=CireChampionProfiles::PrimaryRole(P);
    const auto ClassSkills=[](Cires::SkillDraftRole Role)->FString
    {
        return Role==Cires::SkillDraftRole::Tank?TEXT("Tank skills: taunts, guards, stone walls and shield strikes that pin monsters on you."):
            Role==Cires::SkillDraftRole::Support?TEXT("Support skills: heals, cleanses, wards and domes that keep the party standing."):
            TEXT("DPS skills: projectiles, ground zones, chains and summons that melt packs and bosses.");
    };
    Out.Add(ClassSkills(Primary));
    const TArray<Cires::SkillDraftRole> Extra=HybridRoles(P);
    if(Extra.Num()>0)
    {
        FString Line=TEXT("Hybrid: also drafts ");
        for(int32 I=0;I<Extra.Num();++I)Line+=FString(I==0?TEXT(""):TEXT(" and "))+RoleName(Extra[I]);
        Out.Add(Line+TEXT(" skills."));
    }
    Out.Add(FString::Printf(TEXT("Opens with one %s ability; more unlock as you level."),*Capitalized(FString(RoleName(Primary)).ToLower()).Replace(TEXT("Dps"),TEXT("DPS"))));
    return Out;
}
// Stage light colours per painted scene so the champion reads as standing in it.
struct FSceneMood{FLinearColor Key,Rim,Fill;};
FSceneMood MoodFor(const FString& Bg)
{
    // Key stays near-white (true material colour); the rim carries the scene's light.
    const FLinearColor Key(1.f,.94f,.86f),Fire(1.f,.50f,.18f),Candle(1.f,.72f,.38f),Moon(.50f,.68f,1.f),Ether(.40f,1.f,.80f),Blood(1.f,.30f,.25f),Violet(.68f,.50f,1.f),Dawn(1.f,.82f,.55f);
    const auto Fill=[](const FLinearColor& C){return C*.30f+FLinearColor(.18f,.18f,.20f);};
    FLinearColor Rim=Candle;
    if(Bg==TEXT("wizard")||Bg==TEXT("drakish_footman")||Bg==TEXT("dwarf_miner")||Bg==TEXT("orc_chieftain"))Rim=Fire;
    else if(Bg==TEXT("dryad")||Bg==TEXT("whisp")||Bg==TEXT("bear")||Bg==TEXT("ranger")||Bg==TEXT("keeper_of_light"))Rim=Moon;
    else if(Bg==TEXT("ether_golem"))Rim=Ether;
    else if(Bg==TEXT("ether_golem_support"))Rim=Dawn;
    else if(Bg==TEXT("ether_golem_bruiser"))Rim=FLinearColor(.45f,1.f,.25f); // fel fire
    else if(Bg==TEXT("paladin_holy"))Rim=Dawn;
    else if(Bg==TEXT("troll_berserker_ranged"))Rim=Blood;
    else if(Bg==TEXT("troll_berserker"))Rim=Blood;
    else if(Bg==TEXT("summoner")||Bg==TEXT("lancer"))Rim=Violet;
    else if(Bg==TEXT("evergrove_centaur")||Bg==TEXT("totemic_behemoth"))Rim=Dawn;
    return {Key,Rim,Fill(Rim)};
}

// First word of the variant ("Granite", "Thrown", "Holy") tells look-alike champions apart.
FString VariantWord(const FCireChampionProfile& P){FString W=P.Variant;int32 Space;if(W.FindChar(TEXT(' '),Space))W.LeftInline(Space);return W;}
bool HasTwin(const FCireChampionProfile& P){int32 N=0;for(const auto& Q:CireChampionRoster::All())N+=Q.DisplayName==P.DisplayName;return N>1;}
FString FullName(const FCireChampionProfile& P){return HasTwin(P)?P.DisplayName+TEXT(" · ")+VariantWord(P):P.DisplayName;}
// UI material that composites the stage's alpha render (inverse opacity) over the backdrop.
UMaterialInstanceDynamic* CutoutMaterial()
{
    static TStrongObjectPtr<UMaterialInstanceDynamic> Instance;static bool bTried=false;
    if(!Instance&&!bTried)
    {
        bTried=true;
        if(UMaterialInterface* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/UI/Draft/M_DraftCutout.M_DraftCutout")))
            Instance.Reset(UMaterialInstanceDynamic::Create(Base,GetTransientPackage()));
    }
    return Instance.Get();
}
}

int32 ACireHUD::DraftRosterPageCount() const {return FMath::Max(1,FMath::DivideAndRoundUp(RosterSize(),PageSize));}
FString ACireHUD::DraftRosterIdForSlot(int32 Slot) const
{
    // Stable index mapping kept for developer galleries/probes; the draft screen itself
    // is a filtered card grid (see DrawDraftRoster).
    if(Slot<0||Slot>=PageSize)return {};
    const int32 Page=FMath::Clamp(RosterPage,0,DraftRosterPageCount()-1),Index=Page*PageSize+Slot;
    if(const auto* Profile=CireChampionRoster::FindByIndex(Index))return Profile->Id;
    if(CireChampionRoster::Count()==0)if(const TCHAR* Id=CireChampionProfiles::LegacyProfileId(Index))return Id;
    return {};
}
void ACireHUD::ChangeDraftRosterPage(int32 Delta)
{
    // Left/Right arrows (routed here by the controller) move the selection sideways.
    auto* H=PlayerOwner?Cast<ACireHero>(PlayerOwner->GetPawn()):nullptr;
    if(!H||H->bDrafted||bSettings||bEditLayout)return;
    RosterPage=FMath::Clamp(RosterPage+FMath::Clamp(Delta,-1,1),0,DraftRosterPageCount()-1);
    auto& S=StateFor(this);
    if(S.Tiles.Num()>0&&Delta!=0)ChooseTile(S,S.bChosen?Nearest(S.Tiles,S.Cursor,Delta>0?1:-1,0):S.Cursor);
}
bool ACireHUD::DraftRosterSlot(int32 Slot)
{
    // Number keys 1-6 select the Nth card of the current role (the cursor's role in ALL).
    // Locking in is a separate, deliberate action (Space / LOCK IN / double-click).
    auto* PC=Cast<ACireController>(PlayerOwner);auto* H=PC?Cast<ACireHero>(PC->GetPawn()):nullptr;
    const auto* State=GetWorld()?GetWorld()->GetGameState<ACireGameState>():nullptr;
    if(!PC||!H||H->bDrafted||H->bDead||bSettings||bEditLayout||(State&&State->Phase==3))return false;
    if(CireChampionRoster::Count()==0)
    {if(Slot<0||Slot>=5)return false;PC->ServerAction(5,Slot,nullptr);return true;}
    auto& S=StateFor(this);
    if(!S.Tiles.IsValidIndex(S.Cursor))return false;
    const int32 Column=S.Tiles[S.Cursor].Column;int32 Seen=0;
    for(int32 I=0;I<S.Tiles.Num();++I)if(S.Tiles[I].Column==Column&&Seen++==Slot){ChooseTile(S,I);return true;}
    return false;
}

void ACireHUD::DrawDraftRoster(ACireHero* Hero,ACireController* Controller)
{
    if(!Hero)return;
    ResetTransform();
    auto& S=StateFor(this);
    UWorld* World=GetWorld();
    const double Now=FPlatformTime::Seconds();
    RefreshDraftPalette();
    const auto PX=[&](float V){return V*Scale;};
    const float VW=ViewW,VH=ViewH;
    const FRect Screen{0,0,VW,VH};

    // ---------- Text helpers (kit fonts; every run is audited in the gallery) ----------
    const auto Pen=[&](){return Painter();};
    const auto TW=[&](const FString& Str,float Size,ECireFont Font){return Pen().TextWidth(Str,Size,Font);};
    const auto LH=[&](float Size,ECireFont Font){return LineHeight(Pen(),Size,Font);};
    const auto Txt=[&](const FString& Str,float X,float Y,float Size,const FLinearColor& Color,const FRect& Box,ECireFont Font,bool bOutline=false,int32 Kind=0)
    {
        if(Str.IsEmpty())return;
        TextFx(Str,X,Y,Size,Color,Font,bOutline,true);
        if(S.bAudit)S.Audit.Add({Str,FRect{X,Y,TW(Str,Size,Font),LH(Size,Font)},Box,Size,Kind});
    };
    // Fitted single line (kit ellipsis), left / centred / right aligned in MaxW.
    const auto Line=[&](const FString& Str,float X,float Y,float MaxW,float Size,const FLinearColor& Color,const FRect& Box,ECireFont Font,int32 Align=0,bool bOutline=false,int32 Kind=0)
    {
        if(MaxW<8.f)return 0.f;
        const FString Fitted=Pen().Fit(Str,Size,MaxW,Font);const float W=TW(Fitted,Size,Font);
        Txt(Fitted,Align==1?X+(MaxW-W)*.5f:Align==2?X+MaxW-W:X,Y,Size,Color,Box,Font,bOutline,Kind);
        return W;
    };
    const auto Para=[&](const FString& Str,float X,float Y,float W,float Size,const FLinearColor& Color,int32 MaxLines,const FRect& Box,ECireFont Font,int32 Align=0)
    {
        if(MaxLines<1)return 0.f;
        const TArray<FString> Lines=WrapLines(Pen(),Str,W,Size,Font,MaxLines);const float Step=LH(Size,Font);
        for(int32 I=0;I<Lines.Num();++I){const float LW=Align?TW(Lines[I],Size,Font):0.f;Txt(Lines[I],Align==1?X+(W-LW)*.5f:Align==2?X+W-LW:X,Y+I*Step,Size,Color,Box,Font);}
        return Lines.Num()*Step;
    };
    const auto Seg=[&](float X1,float Y1,float X2,float Y2,const FLinearColor& Color,float Width){ACireHUD::Line(X1,Y1,X2,Y2,Color,Width);};
    const auto Check=[&](float CX,float CY,float R,const FLinearColor& Color,float Width)
    {Seg(CX-R*.55f,CY,CX-R*.12f,CY+R*.45f,Color,Width);Seg(CX-R*.12f,CY+R*.45f,CX+R*.6f,CY-R*.45f,Color,Width);};
    const auto Outline=[&](const FRect& R,float W,const FLinearColor& C)
    {Panel(R.X-W,R.Y-W,R.W+2*W,W,C);Panel(R.X-W,R.B(),R.W+2*W,W,C);Panel(R.X-W,R.Y,W,R.H,C);Panel(R.R(),R.Y,W,R.H,C);};
    const auto Diamond=[&](float CX,float CY,float R,const FLinearColor& C)
    {Tri(FVector2D(CX,CY-R),FVector2D(CX+R,CY),FVector2D(CX,CY+R),C);Tri(FVector2D(CX,CY-R),FVector2D(CX-R,CY),FVector2D(CX,CY+R),C);};
    const auto Ellipse=[&](float CX,float CY,float RX,float RY,const FLinearColor& C)
    {for(int32 I=0;I<24;++I){const float A=I*2*PI/24,B=(I+1)*2*PI/24;Tri(FVector2D(CX,CY),FVector2D(CX+FMath::Cos(A)*RX,CY+FMath::Sin(A)*RY),FVector2D(CX+FMath::Cos(B)*RX,CY+FMath::Sin(B)*RY),C);}};
    // Filigree panel: translucent navy, thin gold rule, corner diamonds.
    const auto Ornate=[&](const FRect& R,float Alpha,const FLinearColor& Edge)
    {
        Panel(R.X,R.Y,R.W,R.H,ThemeUI(8,12,20,uint8(255*Alpha)));
        Outline(R,1,WithAlpha(Edge,.55f));Outline(R.Inset(3),1,WithAlpha(Edge,.18f));
        for(const FVector2D& C:{FVector2D(R.X,R.Y),FVector2D(R.R(),R.Y),FVector2D(R.X,R.B()),FVector2D(R.R(),R.B())})Diamond(C.X,C.Y,4,WithAlpha(Edge,.9f));
    };

    // Legacy fallback if the roster data failed to load: the five original bodies.
    if(CireChampionRoster::Count()==0)
    {
        if(Hero->bDrafted)return;
        const bool Interactive=Controller&&!bSettings&&!bEditLayout;
        Panel(0,0,VW,VH,Backdrop);
        const FString Title=TEXT("CHOOSE YOUR CHAMPION");
        Txt(Title,(VW-TW(Title,26,ECireFont::Heading))*.5f,VH*.18f,26,Text,Screen,ECireFont::Heading,true);
        static const TCHAR* Names[]={TEXT("Iron Warden"),TEXT("Ash Ranger"),TEXT("Veil Scholar"),TEXT("Lancer"),TEXT("Rift Summoner")};
        const float BW=FMath::Min(180.f,(VW-80)/5.f-12),BY=VH*.36f;
        for(int32 I=0;I<5;++I)
        {
            const float BX=(VW-5*(BW+12)+12)*.5f+I*(BW+12);const bool Over=Interactive&&Hit(BX,BY,BW,90);
            Panel(BX,BY,BW,90,Over?CardHi:Card);Line(Names[I],BX+10,BY+34,BW-20,14,Over?Gold:Text,FRect{BX,BY,BW,90},ECireFont::Bold,1);
            if(Over&&Clicked){DraftRosterSlot(I);Clicked=false;}
        }
        return;
    }

    // ---------- Locked view: after the server confirms, the screen holds with a "LOCKED IN"
    // stamp for a moment, then fades out over the opening-ability offer. ----------
    bool bLockedView=false;float OutroAge=0;
    if(Hero->bDrafted)
    {
        if(S.OpenAt>0&&Now-S.OpenAt<1.0&&S.OutroStart<S.OpenAt){S.OutroStart=Now;S.OutroId=Hero->ChampionProfileId.IsEmpty()?S.LockRequestedId:Hero->ChampionProfileId;}
        S.OpenAt=-100;
        if(Now-S.OutroStart>=OutroSeconds||!CireChampionRoster::Find(S.OutroId))return;
        bLockedView=true;OutroAge=static_cast<float>(Now-S.OutroStart);
        S.SelectedId=S.OutroId;S.bChosen=true;S.ForcedHover.Reset();
    }
    else S.OpenAt=Now;
#if !UE_BUILD_SHIPPING
    if(!S.bInitialized)
    {
        S.Portraits.bActive=FParse::Param(FCommandLine::Get(),TEXT("CireDraftPortraits"));
        S.Gallery.bActive=!S.Portraits.bActive&&FParse::Param(FCommandLine::Get(),TEXT("CireDraftGallery"));
    }
    S.bAudit=S.Gallery.bActive&&!S.Gallery.bDone&&!Hero->bDrafted;
    if(S.bForceOutro&&!Hero->bDrafted){bLockedView=true;OutroAge=.9f;}
#endif
    S.bInitialized=true;
    S.Audit.Reset();
    const float Fade=bLockedView&&OutroAge>1.2f?FMath::Clamp(1.f-(OutroAge-1.2f)/(OutroSeconds-1.2f),0.f,1.f):1.f;
    PanelAlpha=Fade;
    const auto Tex=[&](UTexture* T,const FRect& R,float U0,float V0,float UW,float VHt,FLinearColor Color,bool bAlpha=false)
    {
        if(!T)return;Color.A*=Fade;
        DrawTexture(T,PX(R.X),PX(R.Y),PX(R.W),PX(R.H),U0,V0,UW,VHt,Color,bAlpha||Color.A<.999f?BLEND_Translucent:BLEND_Opaque);
    };
    const bool bTyping=Controller&&(Controller->bDraftSearch||Controller->bChatInput);
    const bool Interactive=Controller&&!bSettings&&!bEditLayout&&!bLockedView;

    if(!S.Stage.IsValid())S.Stage=ACireDraftStage::SpawnStage(World);
    ACireDraftStage* Stage=S.Stage.Get();
    if(Stage){Stage->Touch();if(!S.Portraits.bActive)Stage->SetCutout(CutoutMaterial()!=nullptr);}

    // =====================================================================
    // Layout, derived from the logical viewport:
    //   header: team (left) | title + timer ring (centre) | nav (right)
    //   body:   roster panel (left) | champion over the backdrop + info panel + LOCK IN (centre) | identity (right)
    // =====================================================================
    const float M=FMath::Clamp(FMath::Min(VW,VH)*.022f,12.f,26.f);           // safe margin
    float LX=M,CW=VW-2*M;
    if(CW>VH*2.6f){CW=VH*2.6f;LX=(VW-CW)*.5f;}                               // super-wide: centre the content
    const FRect Safe{LX,M,CW,VH-2*M};
    const float Gap=FMath::Clamp(CW*.011f,10.f,18.f);
    const float MidX=LX+CW*.5f;
    // Header.
    const float TitleSize=24.f,TitleLH=LH(TitleSize,ECireFont::Display);
    const float RingR=FMath::Clamp(VH*.042f,24.f,40.f),RingCY=M+TitleLH+RingR-2.f;
    const float PrepSize=11.f,PrepY=RingCY+RingR+4.f,PrepLH=LH(PrepSize,ECireFont::Heading);
    const float BodyY=PrepY+PrepLH+8.f;
    const bool bTagline=VH>=840.f;
    const float TagSize=9.5f,TagLH=LH(TagSize,ECireFont::Heading);
    const float BodyB=VH-M-(bTagline?TagLH+6.f:0.f);
    // Columns.
    const float LeftW=FMath::Clamp(CW*.40f,360.f,760.f),RightW=FMath::Clamp(CW*.19f,230.f,380.f);
    const FRect LeftR{LX,BodyY,LeftW,BodyB-BodyY};
    const FRect RightR{LX+CW-RightW,BodyY,RightW,BodyB-BodyY};
    const float CX0=LeftR.R()+Gap,CWc=RightR.X-Gap-CX0;
    // Centre: LOCK IN at the bottom, the info panel above it, the champion above that.
    const float BtnH=FMath::Max(48.f,LH(17,ECireFont::Heading)+22.f),BtnW=FMath::Clamp(CWc*.62f,240.f,380.f);
    const FRect BtnR{CX0+(CWc-BtnW)*.5f,BodyB-BtnH,BtnW,BtnH};
    const float InfoH=FMath::Clamp((BodyB-BodyY)*.36f,186.f,290.f);
    const FRect InfoR{CX0,BtnR.Y+BtnH*.35f-InfoH,CWc,InfoH};
    // The champion stands from just under the timer to the info panel (feet on its top edge).
    const FRect FigureR{CX0,BodyY-6.f,CWc,InfoR.Y+InfoH*.10f-(BodyY-6.f)};
    // Roster panel internals: filter row(s), then the card grid.
    const float TabSize=12.5f,RowH=FMath::Max(34.f,LH(TabSize,ECireFont::Heading)+14.f),LPad=10.f;
    const FRect LeftIn=LeftR.Inset(LPad);
    static const int32 TabOrder[]={0,1,2};
    float TabWs[3],TabsW=0;
    for(int32 I=0;I<3;++I){TabWs[I]=TW(Columns[TabOrder[I]].Title,TabSize,ECireFont::Heading)+48.f;TabsW+=TabWs[I]+(I?6.f:0.f);}
    const float DropW=TW(TEXT("All Champions"),11.5f,ECireFont::Body)+40.f,SearchMin=150.f;
    const bool bOneRow=TabsW+DropW+SearchMin+2*8.f<=LeftIn.W;
    const FRect TabsR{LeftIn.X,LeftIn.Y,TabsW,RowH};
    const FRect DropR=bOneRow?FRect{TabsR.R()+8,LeftIn.Y,DropW,RowH}:FRect{LeftIn.X,LeftIn.Y+RowH+8,DropW,RowH};
    const FRect SearchR=FRect{DropR.R()+8,DropR.Y,LeftIn.R()-(DropR.R()+8),RowH};
    const float GridTop=DropR.B()+10.f;
    const FRect GridR{LeftIn.X,GridTop,LeftIn.W,LeftIn.B()-GridTop};
    const float CardGap=10.f;
    const FRect CardsR=GridR.Inset(4.f);

    // ---------- Search (controller-owned text) ----------
    FString Search=Controller?Controller->DraftSearch.TrimStartAndEnd():FString();
    if(bLockedView)Search.Reset();

    // ---------- Tiles: ALL = every champion once (by role); a role tab adds its hybrids ----------
    S.Filter=FMath::Clamp(S.Filter,-1,2);
    S.Tiles.Reset();
    for(int32 C=0;C<3;++C)
    {
        if(S.Filter>=0&&S.Filter!=C)continue;
        for(int32 Pass=0;Pass<(S.Filter<0?1:2);++Pass)for(const auto& P:CireChampionRoster::All())
        {
            const bool bPrimary=CireChampionProfiles::PrimaryRole(P)==Columns[C].Role;
            const bool bSecondary=!bPrimary&&(CireChampionProfiles::ProfileRoleMask(P)&Cires::RoleBit(Columns[C].Role))!=0;
            if((Pass==0&&!bPrimary)||(Pass==1&&!bSecondary))continue;
            if(!Search.IsEmpty()&&!P.DisplayName.Contains(Search)&&!P.ClassType.Contains(Search)&&!P.Race.Contains(Search))continue;
            FTile T;T.Profile=&P;T.Column=C;T.bSecondary=Pass==1;S.Tiles.Add(T);
        }
    }
    TMap<FString,int32> NameUses;for(const auto& P:CireChampionRoster::All())NameUses.FindOrAdd(P.DisplayName)++;
    // Card grid: the column count (4-7) that gives the largest card that fits the panel.
    const auto NameSizeFor=[](float W){return FMath::Clamp(W*.12f,10.f,14.f);};
    const auto PlateFor=[&](float W){return 2*LH(NameSizeFor(W),ECireFont::Bold)+8.f;};
    if(S.Tiles.Num()>0)
    {
        const int32 N=S.Tiles.Num();float Best=-1;int32 GridCols=1;
        for(int32 Cols=FMath::Min(4,N);Cols<=FMath::Max(FMath::Min(7,N),1);++Cols)
        {
            const int32 Rows=FMath::DivideAndRoundUp(N,Cols);
            float W=FMath::Min(170.f,(CardsR.W-(Cols-1)*CardGap)/Cols);
            const float MaxH=(CardsR.H-(Rows-1)*CardGap)/Rows;
            for(int32 It=0;It<8&&W+PlateFor(W)>MaxH;++It)W=MaxH-PlateFor(W);
            if(W+PlateFor(W)>MaxH)W-=W+PlateFor(W)-MaxH;
            if(W>Best+.5f){Best=W;GridCols=Cols;}
        }
        const float CardW=FMath::Max(36.f,FMath::FloorToFloat(Best)),CardH=CardW+PlateFor(CardW);
        const int32 Rows=FMath::DivideAndRoundUp(N,GridCols);
        const float UsedW=GridCols*CardW+(GridCols-1)*CardGap;
        const float OX=CardsR.X+(CardsR.W-UsedW)*.5f,OY=CardsR.Y+3.f;
        for(int32 I=0;I<N;++I){FTile& T=S.Tiles[I];T.X=OX+(I%GridCols)*(CardW+CardGap);T.Y=OY+(I/GridCols)*(CardH+CardGap);T.W=CardW;T.H=CardH;}
        (void)Rows;
        int32 Found=INDEX_NONE;
        for(int32 I=0;I<N&&Found==INDEX_NONE;++I)if(S.Tiles[I].Profile->Id==S.CursorId&&!S.Tiles[I].bSecondary)Found=I;
        for(int32 I=0;I<N&&Found==INDEX_NONE;++I)if(S.Tiles[I].Profile->Id==S.CursorId)Found=I;
        S.Cursor=Found!=INDEX_NONE?Found:FMath::Clamp(S.Cursor,0,N-1);
        if(S.CursorId.IsEmpty())S.CursorId=S.Tiles[S.Cursor].Profile->Id;
    }

    // ---------- Keyboard: Up/Down/Home/Space/Tab here; Left/Right and 1-6 arrive via the controller ----------
    const auto SetFilter=[&](int32 F){if(F!=S.Filter){S.Filter=F;S.Hovered.Reset();PlayWowSound(4,.35f);}S.bDropdown=false;};
    if(Interactive&&!bTyping&&PlayerOwner&&S.Tiles.Num()>0)
    {
        if(PlayerOwner->WasInputKeyJustPressed(EKeys::Up))ChooseTile(S,S.bChosen?Nearest(S.Tiles,S.Cursor,0,-1):S.Cursor);
        if(PlayerOwner->WasInputKeyJustPressed(EKeys::Down))ChooseTile(S,S.bChosen?Nearest(S.Tiles,S.Cursor,0,1):S.Cursor);
        if(PlayerOwner->WasInputKeyJustPressed(EKeys::Home))ChooseTile(S,0);
    }
    if(Interactive&&!bTyping&&PlayerOwner&&PlayerOwner->WasInputKeyJustPressed(EKeys::Tab))
    {
        static const int32 Order[]={-1,0,1,2};int32 At=0;for(int32 I=0;I<4;++I)if(Order[I]==S.Filter)At=I;
        const bool bBack=PlayerOwner->IsInputKeyDown(EKeys::LeftShift)||PlayerOwner->IsInputKeyDown(EKeys::RightShift);
        SetFilter(Order[(At+(bBack?3:1))%4]);
    }

    // Team: a human teammate's lock blocks that champion; bot picks never do.
    TArray<ACireHero*> Mates;
    TMap<FString,const ACireHero*> Picked;
    if(World)for(TActorIterator<ACireHero> It(World);It;++It)
    {
        ACireHero* Other=*It;
        if(Other==Hero||Other->IsA<ACireSummon>()||Other->TeamId<0||Other->TeamId!=Hero->TeamId)continue;
        Mates.Add(Other);
        if(Other->bDrafted&&!Other->ChampionProfileId.IsEmpty())
        {const ACireHero** Existing=Picked.Find(Other->ChampionProfileId);if(!Existing||(*Existing)->bBot)Picked.Add(Other->ChampionProfileId,Other);}
    }
    Mates.Sort([](const ACireHero& A,const ACireHero& B){return A.bBot!=B.bBot?!A.bBot:A.GetName()<B.GetName();});
    const auto MateName=[](const ACireHero* M)
    {
        if(M->bBot)return M->HeroName.IsEmpty()||M->HeroName==TEXT("Unbound")?FString(TEXT("Bot")):M->HeroName;
        if(M->GetPlayerState()&&!M->GetPlayerState()->GetPlayerName().IsEmpty())return M->GetPlayerState()->GetPlayerName();
        return M->HeroName.IsEmpty()||M->HeroName==TEXT("Unbound")?FString(TEXT("Player")):M->HeroName;
    };

    // Hover detection before drawing so the splash reacts this frame.
    FString HoverNow;
    const bool bOverDropdown=S.bDropdown&&Hit(DropR.X,DropR.B(),DropR.W+60,4*(RowH-6)+8);
    if(Interactive&&!bOverDropdown)for(const FTile& T:S.Tiles)if(Hit(T.X,T.Y,T.W,T.H)){HoverNow=T.Profile->Id;break;}
    if(!S.ForcedHover.IsEmpty())HoverNow=S.ForcedHover;
    if(HoverNow!=S.Hovered){S.Hovered=HoverNow;S.HoverSince=Now;}
    const FCireChampionProfile* Selected=S.bChosen?CireChampionRoster::Find(S.SelectedId):nullptr;
    if(!Selected)S.bChosen=false;
    const bool bHoverSettled=!S.Hovered.IsEmpty()&&(Now-S.HoverSince>.12||!S.ForcedHover.IsEmpty());
    const FCireChampionProfile* Shown=bLockedView?Selected:bHoverSettled?CireChampionRoster::Find(S.Hovered):nullptr;
    if(!Shown)Shown=Selected;
    // Browsing with nothing hovered or selected still presents the champion under the cursor.
    const bool bIdle=!Shown;
    if(!Shown&&S.Tiles.IsValidIndex(S.Cursor))Shown=S.Tiles[S.Cursor].Profile;
    if(!Shown)Shown=&CireChampionRoster::All()[0];
    if(Stage&&!S.Portraits.bActive)
    {
        Stage->ShowProfile(Shown->Id);
        // Face the camera in a 3/4 front pose with a slow idle sway, lit to match the scene.
        Stage->SetTurntable(false,-18.f+6.f*FMath::Sin(static_cast<float>(Now)*.45f));
        const FSceneMood Mood=MoodFor(BackgroundId(Shown->Id));
        Stage->SetMood(Mood.Key,Mood.Rim,Mood.Fill);
    }
    if(Shown->Id!=S.SplashId){S.SplashId=Shown->Id;S.SplashSince=Now;}
    const auto ShownPrimary=CireChampionProfiles::PrimaryRole(*Shown);
    const FLinearColor ShownColor=RoleColor(ShownPrimary);
    const ACireHero* const* SelectedTaker=Selected?Picked.Find(Selected->Id):nullptr;
    const bool bSelectedBlocked=SelectedTaker&&!(*SelectedTaker)->bBot&&!bLockedView;
    const bool bPending=!bLockedView&&Selected&&S.LockRequestedId==Selected->Id&&Now-S.LockRequestedAt<2.0;
    // Tell the server what we selected (teammates see it; the timer locks it at zero).
    if(!bLockedView&&Controller&&!S.Gallery.bActive)
    {
        const FString Want=Selected?Selected->Id:FString();
        if(Want!=S.SentHoverId){S.SentHoverId=Want;Controller->ServerDraftHover(Want);}
    }
    // Timer (server world time); the gallery forces a value.
    float Remaining=-1.f,Total=1.f;
    if(Hero->DraftDeadline>0&&World)
    {
        const ACireGameState* GS=World->GetGameState<ACireGameState>();
        const float ServerNow=GS?GS->GetServerWorldTimeSeconds():World->GetTimeSeconds();
        Remaining=FMath::Max(0.f,Hero->DraftDeadline-ServerNow);Total=FMath::Max(1.f,Hero->DraftTimerTotal);
    }
    if(S.ForcedTimer>=0){Remaining=S.ForcedTimer;Total=90.f;}

    // =====================================================================
    // Drawing
    // =====================================================================
    // ---------- Background: the champion's painted scene, crossfading on change ----------
    {
        const FString Want=BackgroundId(Shown->Id);
        if(Want!=S.BgId){S.PrevBgId=S.BgId;S.BgId=Want;S.BgSince=Now;}
        const float Blend=FMath::Clamp(static_cast<float>(Now-S.BgSince)/.6f,0.f,1.f);
        Panel(0,0,VW,VH,Backdrop);
        const auto DrawBackground=[&](const FString& Id,float Alpha)
        {
            if(Alpha<=0||Id.IsEmpty())return;
            const FCireChampionProfile* P=nullptr;for(const auto& Q:CireChampionRoster::All())if(BackgroundId(Q.Id)==Id){P=&Q;break;}
            const FLinearColor Col=P?RoleColor(CireChampionProfiles::PrimaryRole(*P)):Gold;
            if(UTexture2D* Bg=Background(Id))
            {
                // Cover the screen without stretching (crop the long axis, keep the floor in view).
                const float TA=Bg->GetSizeX()/FMath::Max(1.f,static_cast<float>(Bg->GetSizeY())),SA=VW/VH;
                float U0=0,V0=0,UW=1,VHt=1;
                if(SA>TA){VHt=TA/SA;V0=(1-VHt)*.55f;}else{UW=SA/TA;U0=(1-UW)*.5f;}
                Tex(Bg,Screen,U0,V0,UW,VHt,FLinearColor(1,1,1,Alpha));
            }
            else
            {
                // Role-themed fallback until a painted scene exists for this champion.
                for(int32 I=0;I<16;++I)Panel(0,VH*I/16.f,VW,VH/16.f,Tint(Col,FMath::Lerp(.34f,.06f,I/15.f),Alpha));
                for(int32 I=0;I<5;++I)
                {
                    const float X0=VW*(.30f+.11f*I),Spread=VW*.035f;
                    Tri(FVector2D(X0,0),FVector2D(X0+Spread,0),FVector2D(X0+Spread*2.4f-VW*.05f,VH),WithAlpha(Tint(Col,1.6f),.035f*Alpha));
                }
                if(P)Icon(RoleSigil(CireChampionProfiles::PrimaryRole(*P)),MidX-VH*.3f,VH*.12f,VH*.6f,WithAlpha(Col,.05f*Alpha));
                for(int32 I=0;I<8;++I)Panel(0,VH*.62f+I*VH*.05f,VW,VH*.05f,WithAlpha(Tint(Col,.12f),.25f*Alpha));
            }
        };
        if(Blend<1.f)DrawBackground(S.PrevBgId,1.f);
        DrawBackground(S.BgId,Blend<1.f?Blend:1.f);
        // Grade: darker under the side panels and the header, a vignette at the bottom.
        // Exact, non-overlapping strips (overlaps would show as bands).
        const int32 Steps=24;
        const float LW=LeftR.R()*1.05f,RW=VW-RightR.X+Gap,HW=BodyY,FW=VH*.25f;
        for(int32 I=0;I<Steps;++I)
        {
            const float A=1.f-(I+.5f)/Steps;
            Panel(I*LW/Steps,0,LW/Steps,VH,FLinearColor(0,0,0,.34f*A));
            Panel(VW-(I+1)*RW/Steps,0,RW/Steps,VH,FLinearColor(0,0,0,.30f*A));
            Panel(0,I*HW/Steps,VW,HW/Steps,FLinearColor(0,0,0,.28f*A));
            Panel(0,VH-(I+1)*FW/Steps,VW,FW/Steps,FLinearColor(0,0,0,.30f*A));
        }
    }

    // ---------- Champion: live model standing on the backdrop ----------
    {
        const float Age=static_cast<float>(Now-S.SplashSince),T=FMath::Clamp(Age/.5f,0.f,1.f),Ease=1.f-FMath::Pow(1.f-T,3.f);
        // The render frames the body in roughly the middle 90% of its height: crop to that so
        // the champion fills the column, and never stretch (UV window keeps the target's aspect).
        const float BH=FigureR.H,BW=FMath::Min(FigureR.W,BH*.80f);
        const FRect Box{FigureR.X+(FigureR.W-BW)*.5f,FigureR.Y,BW,BH};
        // champ-select-hq: 2x supersampled (the cutout material resolves a 4x4 tap grid per screen pixel).
        if(Stage&&!S.Portraits.bActive)Stage->SetPreviewHeight(FMath::RoundToInt(PX(BH)/.86f*2.f));
        const bool bLive=Stage&&!S.Portraits.bActive&&Stage->GetProfileId()==Shown->Id&&Stage->IsPreviewReady();
        // Ground shadow and a role-coloured halo behind the figure.
        CireUIStyle::Glow(Pen(),Box.X+Box.W*.2f,Box.Y+Box.H*.15f,Box.W*.6f,Box.H*.7f,WithAlpha(ShownColor,.10f*Ease));
        // Contact shadow under the feet (the stage reports where its floor lands in the render), in soft layers.
        {
            float FeetY=Box.Y+Box.H*.975f;
            if(Stage&&!S.Portraits.bActive){float VHt0=.86f;const float UW0=(BW/BH)*VHt0/.75f;if(UW0>1.f)VHt0/=UW0;FeetY=Box.Y+FMath::Clamp((Stage->GetFeetV()-(.06f+(.86f-VHt0)*.5f))/VHt0,.5f,1.f)*Box.H;}
            for(int32 L=0;L<4;++L)Ellipse(Box.X+Box.W*.5f,FeetY,Box.W*(.34f-L*.06f),Box.H*(.035f-L*.006f),FLinearColor(0,0,0,.16f*Ease));
        }
        if(bLive)
        {
            float VHt=.86f;float UW=(BW/BH)*VHt/.75f;if(UW>1.f){VHt/=UW;UW=1.f;}
            const float Zoom=1.f+.05f*(1.f-Ease);UW/=Zoom;VHt/=Zoom;
            UMaterialInstanceDynamic* Cutout=CutoutMaterial();
            if(Cutout&&Stage->IsCutout())
            {
                Cutout->SetTextureParameterValue(TEXT("Figure"),Stage->GetRenderTarget());Cutout->SetScalarParameterValue(TEXT("Fade"),Ease*Fade);
                if(Stage->GetDepthTarget()){Cutout->SetTextureParameterValue(TEXT("Depth"),Stage->GetDepthTarget());Cutout->SetScalarParameterValue(TEXT("MaxDepth"),Stage->GetCutoutMaxDepth());}
                DrawMaterial(Cutout,PX(Box.X),PX(Box.Y),PX(Box.W),PX(Box.H),(1-UW)*.5f,.06f+(.86f-VHt)*.5f,UW,VHt);
                S.FigurePx=FRect{PX(Box.X),PX(Box.Y),PX(Box.W),PX(Box.H)};S.FigureUV[0]=(1-UW)*.5f;S.FigureUV[1]=.06f+(.86f-VHt)*.5f;S.FigureUV[2]=UW;S.FigureUV[3]=VHt;
            }
            else Tex(Stage->GetRenderTarget(),Box,(1-UW)*.5f,.06f+(.86f-VHt)*.5f,UW,VHt,FLinearColor(1,1,1,Ease));
        }
        else if(UTexture2D* Face=Portrait(Shown->Id))Tex(Face,FRect{Box.X+Box.W*.2f,Box.Y+Box.H*.2f,Box.W*.6f,Box.W*.6f},0,0,1,1,FLinearColor(1,1,1,.35f));
        if(T<1.f&&bLive){const float SX=Box.X+Box.W*(-.2f+1.4f*Ease);CireUIStyle::Glow(Pen(),SX-20,Box.Y,40,Box.H,FLinearColor(1.f,.92f,.75f,.20f*(1.f-T)));}
        // State tag over the figure's shoulder.
        const ACireHero* const* Taker=Picked.Find(Shown->Id);const bool bTaken=Taker&&!(*Taker)->bBot&&!bLockedView;
        const bool bIsSel=Selected==Shown;
        const FString Tag=bLockedView?FString(TEXT("LOCKED IN")):bTaken?FString(TEXT("LOCKED BY "))+MateName(*Taker).ToUpper():bIsSel?FString(TEXT("SELECTED")):bIdle?FString(TEXT("BROWSING")):FString(TEXT("PREVIEW"));
        const float TS=11.f,TagH=LH(TS,ECireFont::Heading)+8,TagW=FMath::Min(FigureR.W-16,TW(Tag,TS,ECireFont::Heading)+(bIsSel?34.f:18.f));
        const FRect TagR{FigureR.X+(FigureR.W-TagW)*.5f,FigureR.Y+2,TagW,TagH};
        Panel(TagR.X,TagR.Y,TagR.W,TagR.H,bLockedView||bTaken?WithAlpha(LockRed,.92f):bIsSel?WithAlpha(Tint(Gold,.55f),.95f):FLinearColor(0,0,0,.55f));
        Outline(TagR,1,bIsSel||bLockedView?Gold:WithAlpha(Faint,.9f));
        float TX=TagR.X+9;
        if(bIsSel&&!bTaken&&!bLockedView){Check(TagR.X+15,TagR.Y+TagH*.5f,8,BrightGold,2.f);TX+=18;}
        Line(Tag,TX,TagR.Y+4,TagR.R()-TX-6,TS,bIsSel||bTaken||bLockedView?Text:ThemeUI(206,202,190),TagR,ECireFont::Heading);
    }

    // ---------- Header: team (left), title + timer ring (centre), nav (right) ----------
    {
        // Filigree rule across the header, broken by the ring.
        const float RuleY=RingCY;
        Seg(LX,RuleY,MidX-RingR-24,RuleY,WithAlpha(Gold,.35f),1.f);Seg(MidX+RingR+24,RuleY,LX+CW,RuleY,WithAlpha(Gold,.35f),1.f);
        Diamond(MidX-RingR-20,RuleY,3.5f,Gold);Diamond(MidX+RingR+20,RuleY,3.5f,Gold);
        const FString Title=TEXT("CHOOSE YOUR CHAMPION");
        const FRect HeaderR{LX,M,CW,BodyY-M};
        Txt(Title,MidX-TW(Title,TitleSize,ECireFont::Display)*.5f,M,TitleSize,ThemeUI(248,236,208),Safe,ECireFont::Display,true,3);
        // Timer ring: dark disc, gold rings, a progress arc, the seconds inside.
        const bool bLow=Remaining>=0&&Remaining<=10.f&&!bLockedView;
        Disc(MidX,RingCY,RingR,ThemeUI(8,12,20,235));
        Circle(MidX,RingCY,RingR,WithAlpha(Gold,.55f),1.f,48);Circle(MidX,RingCY,RingR-5,WithAlpha(Gold,.25f),1.f,48);
        const float Frac=bLockedView?1.f:Remaining>=0?FMath::Clamp(Remaining/Total,0.f,1.f):1.f;
        const int32 Segs=FMath::Max(1,FMath::RoundToInt(64*Frac));
        for(int32 I=0;I<Segs;++I)
        {
            const float A=-PI*.5f+I*2*PI/64,B=-PI*.5f+(I+1)*2*PI/64;
            Seg(MidX+FMath::Cos(A)*(RingR-2.5f),RingCY+FMath::Sin(A)*(RingR-2.5f),MidX+FMath::Cos(B)*(RingR-2.5f),RingCY+FMath::Sin(B)*(RingR-2.5f),bLow?DpsColor:BrightGold,3.f);
        }
        const FLinearColor TimeColor=bLow?FMath::Lerp(DpsColor,BrightGold,.5f+.5f*FMath::Sin(static_cast<float>(Now)*8.f)):Text;
        const FRect RingBox{MidX-RingR,RingCY-RingR,2*RingR,2*RingR};
        const FString Num=bLockedView?FString(TEXT("OK")):Remaining>=0?FString::FromInt(FMath::CeilToInt(Remaining)):FString(TEXT("--"));
        const float NS=FMath::Min(RingR*.95f,32.f);
        Txt(Num,MidX-TW(Num,NS,Remaining>=0&&!bLockedView?ECireFont::Numbers:ECireFont::Heading)*.5f,RingCY-LH(NS,ECireFont::Numbers)*.5f,NS,TimeColor,RingBox,Remaining>=0&&!bLockedView?ECireFont::Numbers:ECireFont::Heading,true);
        const FString Prep=bLockedView?TEXT("LOCKED IN  |  FINALIZING"):bLow?TEXT("HURRY: AUTO LOCK AT ZERO"):S.bChosen?TEXT("LOCK IN YOUR CHAMPION"):TEXT("PREPARE FOR BATTLE");
        Line(Prep,MidX-150,PrepY,300,PrepSize,bLow?DpsColor:ThemeUI(222,196,140),HeaderR,ECireFont::Heading,1);
        Tip(TEXT("Pick timer"),Remaining>=0?TEXT("When it reaches zero your selected champion is locked in (a random free one if you have not selected any)."):TEXT("This session has no pick timer."),RingBox.X,RingBox.Y,RingBox.W,RingBox.H);

        // Team (left): you and four slots; ghosted = selected but not locked, solid = locked.
        {
            int32 Locked=bLockedView?1:0;for(const auto* Mate:Mates)Locked+=Mate->bDrafted;
            const float Room=MidX-TW(Title,TitleSize,ECireFont::Display)*.5f-20-LX;
            const FString Head=FString::Printf(TEXT("YOUR TEAM  |  %d / %d LOCKED"),Locked,TeamSlots);
            Line(Head,LX,M,Room,11,Gold,HeaderR,ECireFont::Heading);
            const float SlotS=FMath::Clamp(BodyY-M-LH(11,ECireFont::Heading)-14,26.f,40.f),SY=M+LH(11,ECireFont::Heading)+5;
            for(int32 I=0;I<TeamSlots;++I)
            {
                const FRect R{LX+I*(SlotS+6),SY,SlotS,SlotS};
                if(R.R()>LX+Room)break;
                const ACireHero* Mate=I==0?nullptr:Mates.IsValidIndex(I-1)?Mates[I-1]:nullptr;
                const FCireChampionProfile* MP=I==0?Selected:Mate?CireChampionRoster::Find(Mate->bDrafted?Mate->ChampionProfileId:Mate->DraftHoverId):nullptr;
                const bool bSolid=I==0?bLockedView:Mate&&Mate->bDrafted;
                Panel(R.X,R.Y,R.W,R.H,ThemeUI(8,12,20,230));
                if(UTexture2D* Face=MP?Portrait(MP->Id):nullptr)Tex(Face,R,.15f,.08f,.70f,.70f,FLinearColor(1,1,1,bSolid?1.f:.45f));
                else if(I>0&&!Mate){Panel(R.X+R.W*.5f-1,R.Y+R.H*.3f,2,R.H*.4f,Faint);Panel(R.X+R.W*.3f,R.Y+R.H*.5f-1,R.W*.4f,2,Faint);}
                Outline(R,I==0?2.f:1.f,I==0?Gold:bSolid&&MP?RoleColor(CireChampionProfiles::PrimaryRole(*MP)):WithAlpha(Faint,.9f));
                if(bSolid){Disc(R.R()-4,R.B()-4,5,Backdrop);Disc(R.R()-4,R.B()-4,4,BrightGold);}
                const FString Who=I==0?FString(TEXT("You")):Mate?MateName(Mate):FString(TEXT("Open slot"));
                const FString What=MP?MP->DisplayName+(bSolid?TEXT(" (locked in)"):TEXT(" (selected, not locked)")):I>0&&!Mate?FString(TEXT("A bot joins at match start")):FString(TEXT("Choosing a champion"));
                Tip(Who,What,R.X,R.Y,R.W,R.H);
            }
        }
        // Nav (right): CHAMPIONS (here), LOADOUTS (explains the opening ability), SETTINGS (options).
        {
            static const TCHAR* Items[]={TEXT("SETTINGS"),TEXT("LOADOUTS"),TEXT("CHAMPIONS")};
            float X=LX+CW;const float NS2=11.5f,NY=M+2;
            for(int32 I=0;I<3;++I)
            {
                const float W=TW(Items[I],NS2,ECireFont::Heading)+22;X-=W;
                const FRect R{X,NY-2,W,LH(NS2,ECireFont::Heading)+8};
                if(R.X<MidX+TW(Title,TitleSize,ECireFont::Display)*.5f+16)break;
                const bool bHere=I==2,bOver=Interactive&&Hit(R.X,R.Y,R.W,R.H);
                Circle(R.X+6,R.Y+R.H*.5f,4.5f,bHere?Gold:Muted,1.2f,16);
                Txt(Items[I],R.X+16,R.Y+4,NS2,bHere?Gold:bOver?Text:Muted,R,ECireFont::Heading);
                if(bHere)Panel(R.X+16,R.B(),R.W-22,1.5f,Gold);
                if(I==0&&bOver&&Clicked){ToggleSettings();Clicked=false;}
                Tip(Items[I],I==0?TEXT("Open the options."):I==1?TEXT("Loadouts start after lock-in: choose your opening ability, then buy more skills in the Skill Shop as you level."):TEXT("Choose the champion you will play this match."),R.X,R.Y,R.W,R.H);
                X-=14;
            }
        }
        // rules-conformance: GAME MODE picker (Eric: "Skill Shop" vs "Classic Draft", default Skill Shop). The host picks
        // (UCireInventory::ServerSetProgressionMode, host-only, before the first wave); the choice replicates on
        // ACireGameState::ProgressionMode, so every client's picker shows it and flashes when it changes.
        {
            const ACireGameState* GS=World?World->GetGameState<ACireGameState>():nullptr;
            const bool bShopMode=!GS||GS->ProgressionMode!=0;
            const bool bHost=World&&World->GetNetMode()!=NM_Client;
            const bool bModeLocked=GS&&(GS->Wave>0||GS->Phase!=0);
            const uint8 ModeNow=bShopMode?1:0;
            if(S.LastMode!=ModeNow){if(S.LastMode!=255){S.ModeFlashAt=Now;PlayWowSound(4,.5f);}S.LastMode=ModeNow;}
            const float CapS=9.5f,BS=10.5f,PY=M+LH(11,ECireFont::Heading)+5;
            const float BH=FMath::Clamp(FMath::Min(BodyY-M-LH(11,ECireFont::Heading)-14,RingCY-7.f-PY),24.f,40.f),IconS=BH-8; // stays above the header rule
            static const TCHAR* ModeNames[2]={TEXT("SKILL SHOP"),TEXT("CLASSIC DRAFT")};
            static const TCHAR* ModeBlurbs[2]={
                TEXT("Default. Skills are bought and levelled in the Skill Shop that opens after every cleared wave; levelling up only raises your stats."),
                TEXT("Level-up draft: at each level breakpoint you pick a new skill from a role-matched offer.")};
            const FString Cap=TEXT("GAME MODE");
            const float MinX=MidX+TW(TEXT("CHOOSE YOUR CHAMPION"),TitleSize,ECireFont::Display)*.5f+16;
            float SegW[2];for(int32 I=0;I<2;++I)SegW[I]=IconS+TW(ModeNames[I],BS,ECireFont::Heading)+24;
            const float CapW=TW(Cap,CapS,ECireFont::Heading)+12;
            bool bCaption=true,bLabels=true;
            if(LX+CW-(CapW+SegW[0]+SegW[1]+4)<MinX)bCaption=false;
            if(LX+CW-(SegW[0]+SegW[1]+4)<MinX){bLabels=false;SegW[0]=SegW[1]=IconS+14;}
            const float ModeW=(bCaption?CapW:0.f)+SegW[0]+SegW[1]+4;
            float X=LX+CW-ModeW;
            if(X>=MinX)
            {
                const FRect Whole{X,PY,ModeW,BH};
                if(bCaption){Txt(Cap,X,PY+(BH-LH(CapS,ECireFont::Heading))*.5f,CapS,Gold,Whole,ECireFont::Heading);X+=CapW;}
                for(int32 I=0;I<2;++I)
                {
                    const FRect R{X,PY,SegW[I],BH};X+=SegW[I]+4;
                    const bool bOn=(I==0)==bShopMode,bCan=Interactive&&bHost&&!bModeLocked&&Hero->Inventory!=nullptr;
                    const bool bOver=Interactive&&Hit(R.X,R.Y,R.W,R.H);
                    const float FlashAge=static_cast<float>(Now-S.ModeFlashAt);
                    if(bOn&&FlashAge<.9f)CireUIStyle::Glow(Pen(),R.X-8,R.Y-8,R.W+16,R.H+16,FLinearColor(1.f,.8f,.35f,.55f*(1.f-FlashAge/.9f)));
                    Panel(R.X,R.Y,R.W,R.H,bOn?SRGB(46,36,14,242):bOver&&bCan?SRGB(24,30,42,235):SRGB(10,14,22,215));
                    Outline(R,1,bOn?Gold:bOver&&bCan?WithAlpha(Gold,.8f):WithAlpha(GoldDim,.9f));
                    if(bOn){Outline(R.Inset(2),1,WithAlpha(Gold,.3f));Panel(R.X+R.W*.25f,R.B()-2,R.W*.5f,2,BrightGold);Diamond(R.X+R.W*.5f,R.B(),3,BrightGold);}
                    UTexture2D* Glyph=I==0?CireShopArt::ScrollTexture(CireShopArt::EScroll::Golden):CireShopArt::CrestTexture(CireShopArt::EScroll::Prismatic);
                    if(Glyph)Tex(Glyph,FRect{R.X+6,R.Y+4,IconS,IconS},0,0,1,1,FLinearColor(1,1,1,bOn?1.f:.5f),true);
                    else Icon(I==0?TEXT("role2"):TEXT("role1"),R.X+6,R.Y+4,IconS,bOn?BrightGold:Muted);
                    if(bLabels)Line(ModeNames[I],R.X+IconS+14,R.Y+(BH-LH(BS,ECireFont::Heading))*.5f,R.W-IconS-18,BS,bOn?Text:bOver&&bCan?Gold:Muted,R,ECireFont::Heading);
                    if(!bOn&&!bCan&&(!bHost||bModeLocked)){const float LkX=R.R()-9,LkY=R.Y+7;Panel(LkX-3,LkY+2,7,5,Muted);Circle(LkX+.5f,LkY+1,2.5f,Muted,1.f,10);} // lock: not yours to change
                    if(bOver&&Clicked&&bCan&&!bOn)
                    {
                        Hero->Inventory->ServerSetProgressionMode(I==0?1:0);
                        PlayWowSound(4,.45f);Clicked=false;
                    }
                    const FString Why=!bHost?FString(TEXT("Only the host picks the game mode; everyone sees the choice here.")):bModeLocked?FString(TEXT("Locked: the mode is fixed once the first wave starts.")):
                        bOn?FString(TEXT("Selected for this match.")):FString(TEXT("Click to play this mode (the whole team switches)."));
                    Tip(FString(I==0?TEXT("Skill Shop"):TEXT("Classic Draft"))+(bOn?TEXT("  (selected)"):TEXT("")),
                        FString(ModeBlurbs[I])+TEXT("\n")+Why,R.X,R.Y,R.W,R.H);
                }
            }
        }
    }

    // ---------- Roster panel: role tabs, scope dropdown, search, card grid ----------
    Ornate(LeftR,.72f,Gold);
    {
        float X=TabsR.X;
        for(int32 I=0;I<3;++I)
        {
            const int32 F=TabOrder[I];const FRect R{X,TabsR.Y,TabWs[I],RowH};X+=TabWs[I]+6;
            const bool bActive=S.Filter==F,bOver=Interactive&&Hit(R.X,R.Y,R.W,R.H);
            const FLinearColor Col=Columns[F].Color;
            Panel(R.X,R.Y,R.W,R.H,bActive?ThemeUI(18,34,58,240):bOver?ThemeUI(24,30,42,235):ThemeUI(10,14,22,215));
            Outline(R,1,bActive?Gold:WithAlpha(Tint(Col,.8f),.55f));
            if(bActive){Panel(R.X+R.W*.3f,R.B()-2,R.W*.4f,2,Gold);Diamond(R.X+R.W*.5f,R.B(),3,Gold);}
            const float IS=FMath::Min(20.f,R.H-12);
            Icon(Columns[F].Sigil,R.X+12,R.Y+(R.H-IS)*.5f,IS,Col);
            Txt(Columns[F].Title,R.X+18+IS,R.Y+(R.H-LH(TabSize,ECireFont::Heading))*.5f,TabSize,bActive?Text:bOver?Gold:ThemeUI(206,202,190),R,ECireFont::Heading);
            if(bOver&&Clicked){SetFilter(bActive?-1:F);Clicked=false;}
            Tip(Columns[F].Title,FString(Columns[F].Blurb)+TEXT(". Shows its champions plus hybrids who can also fill it. Click again (or Tab) for all champions."),R.X,R.Y,R.W,R.H);
        }
        // Scope dropdown.
        {
            const bool bOver=Interactive&&Hit(DropR.X,DropR.Y,DropR.W,DropR.H);
            Panel(DropR.X,DropR.Y,DropR.W,DropR.H,S.bDropdown?ThemeUI(24,30,42,240):ThemeUI(10,14,22,215));Outline(DropR,1,S.bDropdown||bOver?Gold:WithAlpha(GoldDim,.9f));
            const FString Label=S.Filter<0?FString(TEXT("All Champions")):Capitalized(FString(Columns[S.Filter].Title).ToLower())+TEXT(" only");
            Line(S.Filter==1?FString(TEXT("DPS only")):Label,DropR.X+10,DropR.Y+(RowH-LH(11.5f,ECireFont::Body))*.5f,DropR.W-34,11.5f,Text,DropR,ECireFont::Body);
            CireUIStyle::Chevron(Pen(),DropR.R()-20,DropR.Y+(RowH-10)*.5f,10,false,Gold);
            if(bOver&&Clicked){S.bDropdown=!S.bDropdown;Clicked=false;}
        }
        // Search box.
        {
            const FRect& B=SearchR;
            const bool bFocus=Controller&&Controller->bDraftSearch,bOver=Interactive&&Hit(B.X,B.Y,B.W,B.H);
            Panel(B.X,B.Y,B.W,B.H,bFocus?ThemeUI(20,24,34,245):ThemeUI(10,14,22,215));Outline(B,bFocus?1.5f:1.f,bFocus?Gold:bOver?GoldDim:WithAlpha(GoldDim,.7f));
            const float GX=B.X+15,GY=B.Y+B.H*.5f-2;Circle(GX,GY,5.5f,bFocus?Gold:Muted,1.5f,20);Seg(GX+4,GY+4,GX+8,GY+8,bFocus?Gold:Muted,2.f);
            const float TX=B.X+30,TWd=B.W-38,SY=B.Y+(B.H-LH(12,ECireFont::Body))*.5f;
            const FString Typed=Controller?Controller->DraftSearch:FString();
            if(Typed.IsEmpty()&&!bFocus)Line(TEXT("Search champions..."),TX,SY,TWd,12,Muted,B,ECireFont::Body);
            else
            {
                FString Vis=Typed;while(Vis.Len()>0&&TW(Vis,12,ECireFont::Body)>TWd-8)Vis.RightChopInline(1);
                const float W=Line(Vis,TX,SY,TWd-6,12,Text,B,ECireFont::Body);
                if(bFocus&&FMath::Fmod(static_cast<float>(Now),1.f)<.55f)Panel(TX+W+2,SY+2,2,LH(12,ECireFont::Body)-4,Gold);
            }
            if(Interactive&&Clicked&&Controller){if(bOver){Controller->bDraftSearch=true;Clicked=false;}else Controller->bDraftSearch=false;}
            Tip(TEXT("Search"),TEXT("Click and type to filter champions by name, class or race. Enter keeps the filter, Esc clears it."),B.X,B.Y,B.W,B.H);
        }
        if(S.Tiles.IsEmpty())
        {
            Line(FString::Printf(TEXT("No champion matches \"%s\""),*Search),GridR.X,GridR.Y+GridR.H*.35f,GridR.W,14,Text,GridR,ECireFont::Body,1);
            Line(TEXT("Press Esc in the search box to clear it"),GridR.X,GridR.Y+GridR.H*.35f+LH(14,ECireFont::Body)+4,GridR.W,11.5f,Muted,GridR,ECireFont::Body,1);
        }
    }
    // Cards. Draw order: plain, hovered, selected (it overlaps its neighbours).
    TArray<int32> Order;for(int32 I=0;I<S.Tiles.Num();++I)Order.Add(I);
    const auto Rank=[&](int32 I){const auto& P=*S.Tiles[I].Profile;return Selected==&P?2:S.Hovered==P.Id?1:0;};
    Order.StableSort([&](int32 A,int32 B){return Rank(A)<Rank(B);});
    for(const int32 I:Order)
    {
        const FTile& T=S.Tiles[I];const auto& P=*T.Profile;
        const auto Primary=CireChampionProfiles::PrimaryRole(P);
        const FLinearColor Col=RoleColor(Primary);
        const bool bSel=Selected==&P,bOver=S.Hovered==P.Id&&!bLockedView,bKey=I==S.Cursor&&!bSel&&S.bChosen;
        const ACireHero* const* Taker=Picked.Find(P.Id);
        const bool bHumanTaken=Taker&&!(*Taker)->bBot;
        const float Grow=bSel?.03f:0.f,Lift=bOver&&!bSel?3.f:0.f;
        const FRect C{T.X-T.W*Grow,T.Y-T.H*Grow-Lift,T.W*(1+2*Grow),T.H*(1+2*Grow)};
        const float PW=C.W;
        if(S.bAudit)S.Audit.Add({TEXT("card:")+P.Id,C,LeftR.Inset(2.f),0,2});
        Panel(C.X+2,C.Y+(bOver||bSel?6.f:3.f),C.W,C.H,FLinearColor(0,0,0,.5f));
        if(bSel)CireUIStyle::Glow(Pen(),C.X-3,C.Y-3,C.W+6,C.H+6,WithAlpha(BrightGold,.45f+.15f*FMath::Sin(static_cast<float>(Now)*4.f)));
        const float BW=bSel?3.f:bOver||bKey?2.f:1.f;
        Outline(C,BW,bSel?BrightGold:bOver?WithAlpha(Gold,.9f):bKey?GoldDim:bHumanTaken?Faint:ThemeUI(58,72,96));
        Panel(C.X,C.Y,PW,PW,ThemeUI(14,20,30));
        // Portrait graded toward the target's painted look: warm key, role-tinted base, dark vignette.
        // Painted portraits are shown untinted (their own colour); only taken/hybrid listings dim.
        const FLinearColor FaceTint=bHumanTaken?SRGB(70,70,70):T.bSecondary?FLinearColor(.82f,.82f,.82f):FLinearColor::White;
        if(UTexture2D* Face=Portrait(P.Id))Tex(Face,FRect{C.X,C.Y,PW,PW},.15f,.08f,.70f,.70f,FaceTint);
        else
        {
            for(int32 B=0;B<6;++B)Panel(C.X,C.Y+B*PW/6,PW,PW/6,Tint(Col,.20f*(6-B)/6.f+.05f));
            Icon(RoleSigil(Primary),C.X+PW*.25f,C.Y+PW*.2f,PW*.5f,WithAlpha(Col,.55f));
        }
        {
            const int32 VS=8;const float E=PW*.18f/VS;
            for(int32 V=0;V<VS;++V)
            {
                const float A=.26f*(1.f-(V+.5f)/VS);
                Panel(C.X+V*E,C.Y,E,PW,FLinearColor(0,0,0,A));Panel(C.R()-(V+1)*E,C.Y,E,PW,FLinearColor(0,0,0,A));
                Panel(C.X,C.Y+V*E,PW,E,FLinearColor(0,0,0,A*.8f));
                Panel(C.X,C.Y+PW-(V+1)*E*1.6f,PW,E*1.6f,WithAlpha(Tint(Col,.30f),A*.9f));
            }
        }
        if(bOver&&!bSel)Panel(C.X,C.Y,PW,PW,FLinearColor(1.f,.9f,.7f,.07f));
        if(bHumanTaken)Panel(C.X,C.Y,PW,PW,FLinearColor(0,0,0,.35f));
        // Status band along the portrait's bottom edge (never across the face): difficulty pips,
        // plus BOT PICK / LOCKED BY.
        const float BandS=9.5f,BandH=LH(BandS,ECireFont::Heading)+4;
        const FRect Band{C.X,C.Y+PW-BandH,PW,BandH};
        Panel(Band.X,Band.Y,Band.W,Band.H,bHumanTaken?WithAlpha(LockRed,.92f):FLinearColor(0,0,0,.62f));
        const float PipW=FMath::Clamp(PW*.08f,6.f,12.f),PipH=4.f;
        float PipEnd=Band.X+4;
        // Look-alikes whose name needs two lines carry their variant on the band instead of the pips.
        const bool bBandVariant=NameUses.FindRef(P.DisplayName)>1&&TW(P.DisplayName,10.f,ECireFont::Bold)>C.W-6&&!Taker;
        if(bBandVariant)Line(VariantWord(P).ToUpper(),Band.X+3,Band.Y+2,Band.W-6,BandS,BrightGold,Band,ECireFont::Heading,1);
        else if(!bHumanTaken)for(int32 D=0;D<3;++D){Panel(Band.X+5+D*(PipW+3),Band.Y+(BandH-PipH)*.5f,PipW,PipH,D<P.Difficulty?Gold:WithAlpha(Faint,.9f));PipEnd=Band.X+5+(D+1)*(PipW+3);}
        if(Taker)
        {
            const FString What=bHumanTaken?FString(TEXT("LOCKED BY "))+MateName(*Taker).ToUpper():FString(TEXT("BOT PICK"));
            Line(What,PipEnd+2,Band.Y+2,Band.R()-PipEnd-6,BandS,bHumanTaken?Text:ThemeUI(190,194,198),Band,ECireFont::Heading,bHumanTaken?1:2);
        }
        const float BadgeR=FMath::Clamp(PW*.11f,8.f,15.f);
        Disc(C.X+4+BadgeR,C.Y+4+BadgeR,BadgeR+1.5f,WithAlpha(Col,.95f));Disc(C.X+4+BadgeR,C.Y+4+BadgeR,BadgeR,WithAlpha(Backdrop,.92f));
        Icon(RoleSigil(Primary),C.X+4+BadgeR*.35f,C.Y+4+BadgeR*.35f,BadgeR*1.3f,Col);
        {
            float PipX=C.R()-10-(bSel?BadgeR*2+6:0.f);
            for(const Cires::SkillDraftRole Extra:HybridRoles(P))
                if(RoleIndex(Extra)!=T.Column||!T.bSecondary){Panel(PipX-1,C.Y+5,9,9,Backdrop);Panel(PipX,C.Y+6,7,7,RoleColor(Extra));PipX-=11;}
            if(T.bSecondary)
            {
                const float TagS=9.5f,TagW=TW(TEXT("HYBRID"),TagS,ECireFont::Heading)+8,TagX=PipX-TagW+6;
                if(TagX>C.X+4+2*BadgeR+4)
                {
                    const FRect TagR{TagX,C.Y+4,TagW,LH(TagS,ECireFont::Heading)+2};
                    Panel(TagR.X,TagR.Y,TagR.W,TagR.H,FLinearColor(0,0,0,.78f));
                    Txt(TEXT("HYBRID"),TagR.X+4,TagR.Y+1,TagS,Columns[T.Column].Color,TagR,ECireFont::Heading);
                }
            }
        }
        if(bSel)
        {
            const float CR=FMath::Clamp(PW*.12f,9.f,16.f),CXc=C.R()-5-CR,CYc=C.Y+5+CR;
            Disc(CXc,CYc,CR+2,Backdrop);Disc(CXc,CYc,CR,BrightGold);Check(CXc,CYc,CR,Backdrop,FMath::Max(2.f,CR*.22f));
        }
        const FRect Plate{C.X,C.Y+PW,C.W,C.H-PW};
        Panel(Plate.X,Plate.Y,Plate.W,Plate.H,bSel?ThemeUI(58,46,22,250):bOver?ThemeUI(30,38,54,250):ThemeUI(10,15,24,245));
        {
            const float NS=NameSizeFor(T.W),NL=LH(NS,ECireFont::Bold);
            const bool bTwin=NameUses.FindRef(P.DisplayName)>1;
            TArray<FString> Lines;float Size=NS;
            // champ-select-hq: names always read in full. Shrink (never below 10) until the name fits on
            // one line, else balance it over two lines (shrinking those too); look-alikes whose name
            // needs both lines show their variant on the portrait's status band instead of a third line.
            const float NameW=Plate.W-6;
            const auto Fits=[&](const TArray<FString>& L,float Sz){for(const FString& X:L)if(TW(X,Sz,ECireFont::Bold)>NameW)return false;return true;};
            while(Size>10.f&&TW(P.DisplayName,Size,ECireFont::Bold)>NameW&&(bTwin||Size>NS-1.5f))Size=FMath::Max(10.f,Size-.5f);
            if(TW(P.DisplayName,Size,ECireFont::Bold)<=NameW)Lines.Add(P.DisplayName);
            else
            {
                Size=NS;Lines=BalancedLines(Pen(),P.DisplayName,Size,ECireFont::Bold);
                while(Size>10.f&&!Fits(Lines,Size)){Size=FMath::Max(10.f,Size-.5f);Lines=BalancedLines(Pen(),P.DisplayName,Size,ECireFont::Bold);}
                for(FString& X:Lines)X=Pen().Fit(X,Size,NameW,ECireFont::Bold);
            }
            const bool bVariantLine=bTwin&&Lines.Num()==1;
            const float VS=FMath::Max(10.f,NS*.85f),VL=LH(VS,ECireFont::Body);
            const float LineH=LH(Size,ECireFont::Bold);
            float Y=Plate.Y+(Plate.H-Lines.Num()*LineH-(bVariantLine?VL:0.f))*.5f;
            for(const FString& L:Lines){Txt(L,Plate.X+(Plate.W-TW(L,Size,ECireFont::Bold))*.5f,Y,Size,bSel?BrightGold:bHumanTaken?Muted:Text,Plate,ECireFont::Bold,false,1);Y+=LineH;}
            if(bVariantLine)Line(FString(TEXT("· "))+VariantWord(P)+TEXT(" ·"),Plate.X+3,Y,Plate.W-6,VS,bSel?Text:Gold,Plate,ECireFont::Body,1);
        }
        if(Interactive&&bOver&&Clicked&&S.ForcedHover.IsEmpty()&&!bOverDropdown)
        {
            const bool bDouble=S.LastClickId==P.Id&&Now-S.LastClickAt<.35;
            if(!bSel)PlayWowSound(4,.45f);
            S.Cursor=I;S.CursorId=P.Id;S.SelectedId=P.Id;S.bChosen=true;S.LastClickId=P.Id;S.LastClickAt=Now;Clicked=false;
            if(Controller)Controller->bDraftSearch=false;
            if(bDouble&&!bHumanTaken&&Controller){Controller->ServerDraftProfile(P.Id);S.LockRequestedAt=Now;S.LockRequestedId=P.Id;}
        }
        if(!bOverDropdown)
        {
            FString TipBody=P.ClassType+TEXT(" | ")+RoleName(Primary);
            for(const auto Extra:HybridRoles(P))TipBody+=FString(TEXT(" + "))+RoleName(Extra);
            if(Taker)TipBody+=FString::Printf(TEXT(". Picked by %s%s"),*MateName(*Taker),(*Taker)->bBot?TEXT(" (bot; still available)"):TEXT(""));
            Tip(P.DisplayName,TipBody+TEXT(". Click to select; double-click or Space to lock in."),T.X,T.Y,T.W,T.H);
        }
    }
    // Scope dropdown list (over the grid).
    if(S.bDropdown&&!bLockedView)
    {
        static const int32 Scopes[]={-1,0,1,2};static const TCHAR* Names[]={TEXT("All Champions"),TEXT("Tank only"),TEXT("DPS only"),TEXT("Support only")};
        const float IH=RowH-6;const FRect L{DropR.X,DropR.B()+2,DropR.W+60,4*IH+8};
        Panel(L.X,L.Y,L.W,L.H,ThemeUI(8,12,20,250));Outline(L,1,Gold);
        for(int32 I=0;I<4;++I)
        {
            const FRect R{L.X+4,L.Y+4+I*IH,L.W-8,IH};const bool bOver=Interactive&&Hit(R.X,R.Y,R.W,R.H);
            if(bOver||S.Filter==Scopes[I])Panel(R.X,R.Y,R.W,R.H,bOver?ThemeUI(30,38,54,250):ThemeUI(22,28,40,250));
            Line(Names[I],R.X+8,R.Y+(IH-LH(11.5f,ECireFont::Body))*.5f,R.W-16,11.5f,S.Filter==Scopes[I]?Gold:Text,R,ECireFont::Body);
            if(bOver&&Clicked){SetFilter(Scopes[I]);Clicked=false;}
        }
        if(Clicked&&!Hit(L.X,L.Y,L.W,L.H)){S.bDropdown=false;}
    }

    // ---------- Identity column (right): who they are, the key facts, the trait, how they play ----------
    {
        const float Age=static_cast<float>(Now-S.SplashSince),T=FMath::Clamp(Age/.45f,0.f,1.f),Ease=1.f-FMath::Pow(1.f-T,3.f);
        const float Slide=18.f*(1.f-Ease);
        PanelAlpha=Fade;
        // Dark panel behind the text so it reads over any painted scene.
        const FRect Back{RightR.X-14,RightR.Y-10,RightR.W+28,RightR.H+10};
        Panel(Back.X,Back.Y,Back.W,Back.H,ThemeUI(6,9,15,196));
        for(int32 I=0;I<8;++I){const float A=(8-I)/8.f*.6f;Panel(Back.X-(I+1)*3.f,Back.Y,3.f,Back.H,ThemeUI(6,9,15,uint8(196*A)));}
        Outline(Back,1,WithAlpha(Gold,.22f));
        PanelAlpha=Fade*Ease;
        const FRect R{RightR.X+Slide,RightR.Y,RightR.W-Slide,RightR.H};
        const float RK=FMath::Clamp(RightW/260.f,1.f,1.25f);
        float Y=R.Y+2;
        const auto Section=[&](const FString& Head)
        {
            const float HS=12.5f*RK,HL=LH(HS,ECireFont::Display);
            Diamond(R.X+4,Y+HL*.5f,3.5f,Gold);
            const float W=Line(Head,R.X+14,Y,R.W-30,HS,Gold,R,ECireFont::Display);
            Seg(R.X+14+W+10,Y+HL*.5f,R.R()-8,Y+HL*.5f,WithAlpha(Gold,.35f),1.f);Diamond(R.R()-4,Y+HL*.5f,2.5f,WithAlpha(Gold,.7f));
            Y+=HL+6;
        };
        // Quote.
        if(!Shown->Lore.IsEmpty())
        {
            const float QS=12.5f*RK;
            Y+=Para(TEXT("\"")+Shown->Lore+TEXT("\""),R.X,Y,R.W,QS,ThemeUI(226,196,140),3,R,ECireFont::Body)+10;
        }
        // Name (display serif), then class and race in small caps.
        const float NS=FMath::Clamp(R.W*.125f,24.f,40.f);
        Y+=Para(FullName(*Shown),R.X,Y,R.W,NS,ThemeUI(248,236,208),2,R,ECireFont::Display);
        FString Caption=Shown->ClassType.ToUpper();if(!Shown->Race.IsEmpty())Caption+=TEXT("   ·   ")+Shown->Race.ToUpper();
        Y+=Para(Caption,R.X,Y+2,R.W,11.5f*RK,Gold,2,R,ECireFont::Heading)+12;
        // Role badge + one-line identity.
        {
            float ChipX=R.X;const float CS=11.f*RK,ChipH=FMath::Max(24.f,LH(CS,ECireFont::Heading)+10);
            const auto Chip=[&](Cires::SkillDraftRole ChipRole,bool bPrimaryRole)
            {
                const FString RoleText=bPrimaryRole?FString(RoleName(ChipRole)):FString(RoleName(ChipRole))+TEXT(" HYBRID");
                const float W=TW(RoleText,CS,ECireFont::Heading)+32;const FLinearColor Col=RoleColor(ChipRole);
                if(ChipX+W>R.R())return;
                const FRect ChipR{ChipX,Y,W,ChipH};
                Panel(ChipR.X,ChipR.Y,ChipR.W,ChipR.H,bPrimaryRole?Tint(Col,.50f,.97f):ThemeUI(10,14,22,230));Outline(ChipR,1,WithAlpha(Col,.8f));
                Icon(RoleSigil(ChipRole),ChipR.X+7,ChipR.Y+(ChipH-16)*.5f,16,bPrimaryRole?Text:Col);
                Txt(RoleText,ChipR.X+27,ChipR.Y+(ChipH-LH(CS,ECireFont::Heading))*.5f,CS,bPrimaryRole?Text:Col,ChipR,ECireFont::Heading);ChipX+=W+6;
            };
            Chip(ShownPrimary,true);
            for(const auto Extra:HybridRoles(*Shown))Chip(Extra,false);
            Y+=ChipH+8;
            Y+=Para(Playstyle(*Shown),R.X,Y,R.W,13.f*RK,Text,2,R,ECireFont::Bold)+12;
        }
        // Key facts: labelled cells in a 2x2 grid.
        {
            const float LS=9.5f*RK,VS=12.5f*RK,LL=LH(LS,ECireFont::Heading),VL=LH(VS,ECireFont::Bold);
            const float CellW=(R.W-8)*.5f,CellH=LL+VL+12;
            const bool bRanged=Shown->BasicAttackRange>300;
            const FLinearColor PrimeCol=Shown->PrimaryStat==TEXT("strength")?SRGB(232,120,90):Shown->PrimaryStat==TEXT("agility")?SRGB(120,210,120):SRGB(120,160,240);
            struct FCell{const TCHAR* Label;FString Value;FLinearColor Color;int32 Pips;};
            const FCell Cells[]={
                {TEXT("DIFFICULTY"),Capitalized(FString(DifficultyWord(Shown->Difficulty)).ToLower()),Gold,Shown->Difficulty},
                {TEXT("PRIMARY STAT"),Capitalized(PrimaryName(Shown->PrimaryStat).ToLower()),PrimeCol,0},
                {TEXT("ATTACK RANGE"),FString::Printf(TEXT("%s  %.1f m"),bRanged?TEXT("Ranged"):TEXT("Melee"),Shown->BasicAttackRange/100.f),Text,0},
                {TEXT("WEAPON"),FString::Printf(TEXT("%s  %.1f s"),*StyleLabel(Shown->AttackStyle),Shown->AttackSeconds),Text,0}};
            for(int32 I=0;I<4;++I)
            {
                const FRect C{R.X+(I%2)*(CellW+8),Y+(I/2)*(CellH+6),CellW,CellH};
                Panel(C.X,C.Y,C.W,C.H,ThemeUI(14,19,30,225));Panel(C.X,C.Y,2,C.H,WithAlpha(Cells[I].Color,.9f));
                Line(Cells[I].Label,C.X+9,C.Y+5,C.W-14,LS,Muted,C,ECireFont::Heading);
                float VX=C.X+9;
                if(Cells[I].Pips>0){for(int32 D=0;D<3;++D)Panel(VX+D*12,C.Y+5+LL+VL*.5f-3,9,6,D<Cells[I].Pips?Gold:Faint);VX+=40;}
                Line(Cells[I].Value,VX,C.Y+6+LL,C.R()-VX-5,VS,Cells[I].Color,C,ECireFont::Bold);
            }
            Y+=2*CellH+6+14;
        }
        // Class trait: short and bold.
        {
            const FCireClassTrait Trait=CireClassTraits::Info(ShownPrimary);
            if(!Trait.Id.IsEmpty())
            {
                Section(TEXT("Class Trait"));
                const float TS=13.f*RK,BS=11.5f*RK,IS=34.f;
                FCireIconSlot Slot;Slot.IconId=Trait.Id;Slot.IconTexture=CireAbilityIcons::Texture(Trait.Id);Slot.Tint=Trait.Color;Slot.Kind=ECireSlotKind::Passive;
                CireUIStyle::IconSlot(Pen(),R.X+2,Y+2,IS,Slot,Now);
                const float TX=R.X+IS+12;
                Line(Trait.Name,TX,Y,R.R()-TX,TS,Trait.Color,R,ECireFont::Bold);
                const float Used=Para(Trait.Summary,TX,Y+LH(TS,ECireFont::Bold),R.R()-TX,BS,ThemeUI(226,220,206),2,R,ECireFont::Body);
                Tip(Trait.Name+TEXT("  (class trait)"),Trait.Tooltip,R.X,Y,R.W,FMath::Max(IS,LH(TS,ECireFont::Bold)+Used));
                Y+=FMath::Max(IS+4,LH(TS,ECireFont::Bold)+Used)+14;
            }
        }
        // How it plays: three short bullets.
        {
            Section(TEXT("How It Plays"));
            const float PS=12.f*RK,PL=LH(PS,ECireFont::Body);
            for(const FString& B:HowItPlays(*Shown))
            {
                const int32 MaxLines=FMath::Min(2,FMath::FloorToInt((R.B()-4-Y)/PL));
                if(MaxLines<1)break;
                Diamond(R.X+5,Y+PL*.5f,3,Gold);
                Y+=Para(B,R.X+16,Y,R.W-16,PS,ThemeUI(226,220,206),MaxLines,R,ECireFont::Body)+6;
            }
        }
        PanelAlpha=Fade;
    }

    // ---------- Info panel (centre): OVERVIEW | ABILITIES | LORE ----------
    {
        Ornate(InfoR,.88f,Gold);
        static const TCHAR* Tabs[]={TEXT("OVERVIEW"),TEXT("ABILITIES"),TEXT("LORE")};
        const float TabRowH=FMath::Max(30.f,LH(13.5f,ECireFont::Display)+12),TW3=InfoR.W/3.f;
        for(int32 I=0;I<3;++I)
        {
            const FRect R{InfoR.X+I*TW3,InfoR.Y+2,TW3,TabRowH};
            const bool bActive=S.InfoTab==I,bOver=Interactive&&Hit(R.X,R.Y,R.W,R.H);
            Line(Tabs[I],R.X,R.Y+(TabRowH-LH(13.5f,ECireFont::Display))*.5f,R.W,13.5f,bActive?ThemeUI(248,236,208):bOver?Gold:ThemeUI(176,172,160),R,ECireFont::Display,1);
            if(bActive){Panel(R.X+R.W*.25f,R.B()-2,R.W*.5f,2,ThemeUI(120,170,230));Diamond(R.X+R.W*.5f,R.B()-1,3,ThemeUI(160,200,240));}
            if(bOver&&Clicked){S.InfoTab=I;Clicked=false;}
        }
        Panel(InfoR.X+8,InfoR.Y+2+TabRowH+2,InfoR.W-16,1,WithAlpha(Gold,.3f));
        const FRect Body{InfoR.X+14,InfoR.Y+TabRowH+12,InfoR.W-28,BtnR.Y-6-(InfoR.Y+TabRowH+12)};
        const float Age=static_cast<float>(Now-S.SplashSince),T=FMath::Clamp(Age/.35f,0.f,1.f);
        PanelAlpha=Fade*T;
        // Larger type when the panel has room (tall and wide screens).
        const float TK=FMath::Clamp(FMath::Min(InfoH/200.f,CWc/560.f),1.f,1.3f);
        const float HS=11.f*TK,HL=FMath::Max(LH(HS,ECireFont::Heading),LH(HS,ECireFont::Display)),BS=11.5f*TK,BL=LH(BS,ECireFont::Body);
        const float HalfW=(Body.W-20)*.5f;
        const FRect L{Body.X,Body.Y,HalfW,Body.H},Rr{Body.X+HalfW+20,Body.Y,HalfW,Body.H};
        const Cires::RoleMask Bit=Cires::RoleBit(ShownPrimary);
        if(S.InfoTab==0)
        {
            // Base attributes + difficulty, combat style | class trait summary + opening ability.
            float Y=L.Y;
            Line(TEXT("BASE ATTRIBUTES"),L.X,Y,L.W*.6f,HS,Gold,L,ECireFont::Display);
            for(int32 D=0;D<3;++D)Panel(L.R()-3*20+D*20,Y+HL*.5f-3,16,6,D<Shown->Difficulty?Gold:Faint);
            Y+=HL+4;
            struct FStatRow {const TCHAR* Key;int32 Value;const TCHAR* Stat;};
            const FStatRow Rows[]={{TEXT("STRENGTH"),Shown->Strength,TEXT("strength")},{TEXT("AGILITY"),Shown->Agility,TEXT("agility")},{TEXT("INTELLIGENCE"),Shown->Intelligence,TEXT("intelligence")}};
            const float SS=10.f*TK,SL=LH(SS,ECireFont::Heading);
            const float RowStep=FMath::Clamp((L.H-HL-4-HL*2-10)/3.f,SL+8,SL+22);
            for(const FStatRow& Row:Rows)
            {
                const bool bPrime=Shown->PrimaryStat==Row.Stat;
                // champ-select-hq: readable stat rows: label and value on one baseline, value in large numerals, bar under both.
                Txt(Row.Key,L.X,Y,SS,bPrime?Gold:ThemeUI(206,206,200),L,ECireFont::Heading);
                const FString V=FString::FromInt(Row.Value);const float VS2=SS*1.3f;
                Txt(V,L.R()-TW(V,VS2,ECireFont::Numbers),Y+SL-LH(VS2,ECireFont::Numbers),VS2,bPrime?BrightGold:Text,L,ECireFont::Numbers);
                Bar(L.X,Y+SL+3,L.W,7,Row.Value/30.f,bPrime?Gold:ThemeUI(110,122,138));
                Y+=RowStep;
            }
            const bool bRanged=Shown->BasicAttackRange>300;
            Line(TEXT("COMBAT STYLE"),L.X,Y+2,L.W,HS,Gold,L,ECireFont::Display);
            Line(FString::Printf(TEXT("%s  |  %s  |  %.1f m  |  %.1f s"),bRanged?TEXT("RANGED"):TEXT("MELEE"),*StyleLabel(Shown->AttackStyle),Shown->BasicAttackRange/100.f,Shown->AttackSeconds),L.X,Y+2+HL+2,L.W,BS,Text,L,ECireFont::Body);
            Panel(Rr.X-10,Rr.Y,1,Rr.H,WithAlpha(Gold,.25f));
            float RY=Rr.Y;
            Line(TEXT("OPENING ABILITY"),Rr.X,RY,Rr.W,HS,Gold,Rr,ECireFont::Display);RY+=HL+2;
            RY+=Para(FString::Printf(TEXT("Right after lock-in you choose 1 of 4 %s actives. Passives and ultimates come from level 3."),RoleName(ShownPrimary)),Rr.X,RY,Rr.W,BS,ThemeUI(222,216,200),FMath::FloorToInt((Rr.B()-RY)/BL*.6f),Rr,ECireFont::Body)+8;
            if(RY+HL+BL<=Rr.B())
            {
                const ACireGameState* ModeGS=World?World->GetGameState<ACireGameState>():nullptr;
                const bool bShopMode=!ModeGS||ModeGS->ProgressionMode!=0; // rules-conformance: follows the picked game mode
                Line(bShopMode?TEXT("SKILL SHOP"):TEXT("CLASSIC DRAFT"),Rr.X,RY,Rr.W,HS,Gold,Rr,ECireFont::Display);RY+=HL+2;
                Para(bShopMode?TEXT("Buy and level more skills from your role's pool after every cleared wave."):TEXT("New skill offers from your role's pool arrive as you level."),
                    Rr.X,RY,Rr.W,BS,ThemeUI(222,216,200),FMath::FloorToInt((Rr.B()-RY)/BL),Rr,ECireFont::Body);
            }
        }
        else if(S.InfoTab==1)
        {
            // What the skills are like (no list): themes, role pool size, where they come from.
            float Y=L.Y;
            Line(TEXT("SKILL STYLE"),L.X,Y,L.W,HS,Gold,L,ECireFont::Display);Y+=HL+4;
            const TArray<FString> Bullets=HowItPlays(*Shown);
            for(int32 I=0;I<Bullets.Num();++I)
            {
                const int32 MaxLines=FMath::FloorToInt((L.B()-Y)/BL);if(MaxLines<1)break;
                Diamond(L.X+4,Y+BL*.5f,3,Gold);Y+=Para(Bullets[I],L.X+14,Y,L.W-14,BS,ThemeUI(222,216,200),FMath::Min(3,MaxLines),L,ECireFont::Body)+4;
            }
            const auto Pool=Cires::StarterSkillPoolForRoles(static_cast<Cires::RoleMask>(CireChampionProfiles::ProfileRoleMask(*Shown)));
            int32 Actives=0,Passives=0,Ultimates=0;
            for(const auto& Skill:Pool){Actives+=Skill.Kind==Cires::SkillKind::Active;Passives+=Skill.Kind==Cires::SkillKind::Passive;Ultimates+=Skill.Kind==Cires::SkillKind::Ultimate;}
            Panel(Rr.X-10,Rr.Y,1,Rr.H,WithAlpha(Gold,.25f));
            float RY=Rr.Y;
            FString PoolName=RoleName(ShownPrimary);for(const auto Extra:HybridRoles(*Shown))PoolName+=FString(TEXT(" + "))+RoleName(Extra);
            Line(PoolName+TEXT(" SKILL POOL"),Rr.X,RY,Rr.W,HS,Gold,Rr,ECireFont::Display);RY+=HL+4;
            RY+=Para(FString::Printf(TEXT("%d actives, %d passives and %d ultimates to draft from, shared with the rest of your role."),Actives,Passives,Ultimates),Rr.X,RY,Rr.W,BS,Text,3,Rr,ECireFont::Body)+8;
            (void)Bit;
            if(RY+HL<=Rr.B())
            {
                Line(TEXT("CLASS TRAIT"),Rr.X,RY,Rr.W,HS,Gold,Rr,ECireFont::Display);RY+=HL+2;
                const FCireClassTrait Trait=CireClassTraits::Info(ShownPrimary);
                Para(Trait.Name+TEXT(": ")+Trait.Summary,Rr.X,RY,Rr.W,BS,ThemeUI(222,216,200),FMath::FloorToInt((Rr.B()-RY)/BL),Rr,ECireFont::Body);
            }
        }
        else
        {
            float Y=Body.Y;
            if(!Shown->Lore.IsEmpty())Y+=Para(TEXT("\"")+Shown->Lore+TEXT("\""),Body.X,Y,Body.W,14.f*TK,ThemeUI(226,190,120),3,Body,ECireFont::Body)+12;
            const FString Facts=FString::Printf(TEXT("%s  |  %s  |  %s"),*Shown->ClassType,*Capitalized(Shown->Race),*StyleLabel(Shown->AttackStyle));
            Line(Facts,Body.X,Y,Body.W,12.5f*TK,Text,Body,ECireFont::Body);Y+=LH(12.5f*TK,ECireFont::Body)+10;
            Para(Playstyle(*Shown),Body.X,Y,Body.W,BS,ThemeUI(206,200,186),FMath::FloorToInt((Body.B()-Y)/BL),Body,ECireFont::Body);
        }
        PanelAlpha=Fade;
    }

    // ---------- LOCK IN ----------
    {
        const bool bCanLock=!bLockedView&&Selected&&!bSelectedBlocked&&!bPending;
        const bool bOver=Interactive&&bCanLock&&Hit(BtnR.X,BtnR.Y,BtnR.W,BtnR.H);
        if(bCanLock)CireUIStyle::Glow(Pen(),BtnR.X-6,BtnR.Y-6,BtnR.W+12,BtnR.H+12,FLinearColor(1.f,.75f,.3f,.26f+.18f*FMath::Sin(static_cast<float>(Now)*3.5f)));
        // Ornate end caps.
        const FLinearColor CapC=bCanLock||bLockedView?Gold:WithAlpha(GoldDim,.9f);
        for(int32 Side=0;Side<2;++Side)
        {
            const float X=Side?BtnR.R():BtnR.X,D=Side?1.f:-1.f,CY=BtnR.Y+BtnR.H*.5f;
            Tri(FVector2D(X,BtnR.Y),FVector2D(X+D*BtnR.H*.45f,CY),FVector2D(X,BtnR.B()),ThemeUI(14,18,28,240));
            Seg(X,BtnR.Y,X+D*BtnR.H*.45f,CY,CapC,1.5f);Seg(X+D*BtnR.H*.45f,CY,X,BtnR.B(),CapC,1.5f);
            Diamond(X+D*(BtnR.H*.45f+8),CY,4,CapC);
        }
        CireUIStyle::Button(Pen(),BtnR.X,BtnR.Y,BtnR.W,BtnR.H,FString(),bLockedView?ECireButtonState::Selected:bCanLock?(bOver?ECireButtonState::Hover:ECireButtonState::Normal):ECireButtonState::Disabled,Gold,16.f);
        Outline(BtnR.Inset(3),1,WithAlpha(CapC,.6f));
        const FString Main=bLockedView?FString(TEXT("LOCKED IN")):bPending?FString(TEXT("LOCKING IN...")):bSelectedBlocked?FString(TEXT("TAKEN BY "))+MateName(*SelectedTaker).ToUpper():Selected?FString(TEXT("LOCK IN  "))+FullName(*Selected).ToUpper():FString(TEXT("SELECT A CHAMPION"));
        const FString Sub=bLockedView?FString(TEXT("Next: choose your opening ability")):!Selected?FString(TEXT("Click a portrait to select it")):bSelectedBlocked?FString(TEXT("Pick another champion")):bPending?FString(TEXT("Waiting for the server")):FString(TEXT("Space or double-click also locks in"));
        const float MS=17.f,SubS=10.5f,ML=LH(MS,ECireFont::Heading),SubL=LH(SubS,ECireFont::Body),BY=BtnR.Y+(BtnR.H-ML-SubL)*.5f;
        Line(Main,BtnR.X+14,BY,BtnR.W-28,MS,bCanLock||bLockedView?(bOver?FLinearColor(1.f,.93f,.72f,1):BrightGold):Muted,BtnR,ECireFont::Heading,1,true);
        Line(Sub,BtnR.X+14,BY+ML,BtnR.W-28,SubS,bCanLock||bLockedView?ThemeUI(226,220,204):Muted,BtnR,ECireFont::Body,1);
        bool bLock=false;
        if(bOver&&Clicked){bLock=true;Clicked=false;}
        if(Interactive&&!bTyping&&PlayerOwner&&PlayerOwner->WasInputKeyJustPressed(EKeys::SpaceBar))bLock=true;
        if(bLock&&bCanLock&&Controller){Controller->ServerDraftProfile(Selected->Id);S.LockRequestedAt=Now;S.LockRequestedId=Selected->Id;}
        if(!bLockedView&&!Hero->Notice.IsEmpty()&&Now-S.LockRequestedAt<6.0&&Now-S.LockRequestedAt>.5)
            Line(Hero->Notice,CX0,InfoR.Y-LH(11,ECireFont::Body)-4,CWc,11,DpsColor,FigureR,ECireFont::Body,1);
    }
    // Taglines under the panels on tall screens.
    if(bTagline)
    {
        const float Y=VH-M-TagLH;const FRect Foot{LX,Y,CW,TagLH};
        Line(TEXT("LEGENDS RISE.  SO DO YOU."),LX,Y,LeftW,TagSize,WithAlpha(Muted,.8f),Foot,ECireFont::Heading);
        Line(TEXT("SKILL  |  STRATEGY  |  A BRIGHTER TOMORROW"),RightR.X-RightW,Y,RightW*2,TagSize,WithAlpha(Muted,.8f),Foot,ECireFont::Heading,2);
    }

    // ---------- Locked view: the stamp ----------
    if(bLockedView)
    {
        const float Slam=FMath::Clamp(OutroAge/.22f,0.f,1.f),Ease=1.f-FMath::Pow(1.f-Slam,3.f);
        const float K=FMath::Lerp(1.6f,1.f,Ease),StampSize=34*K;
        const FString Stamp=TEXT("LOCKED IN");
        const float SW=TW(Stamp,StampSize,ECireFont::Heading)+56*K,SH=LH(StampSize,ECireFont::Heading)+24*K;
        const FRect StampR{CX0+(CWc-SW)*.5f,FigureR.Y+FigureR.H*.52f-SH*.5f,SW,SH};
        PanelAlpha=Fade*Ease;
        Panel(StampR.X,StampR.Y,StampR.W,StampR.H,WithAlpha(LockRed,.9f));
        for(float D:{4.f,8.f})Outline(StampR.Inset(D),1.5f,BrightGold);
        Txt(Stamp,StampR.X+(StampR.W-TW(Stamp,StampSize,ECireFont::Heading))*.5f,StampR.Y+(StampR.H-LH(StampSize,ECireFont::Heading))*.5f,StampSize,Text,StampR,ECireFont::Heading,true);
        PanelAlpha=Fade*FMath::Clamp((OutroAge-.35f)/.3f,0.f,1.f);
        Line(TEXT("NEXT  |  CHOOSE YOUR OPENING ABILITY"),CX0,StampR.B()+10,CWc,13.5f,Text,FigureR,ECireFont::Heading,1,true);
        PanelAlpha=Fade;
    }
    PanelAlpha=1.f;
    if(Hero->bDrafted){ResetTransform();return;}

#if !UE_BUILD_SHIPPING
    // ---------- Fixture: portraits from the real meshes (Tools/RunDraftPortraits.py) ----------
    if(S.Portraits.bActive&&!S.Portraits.bDone&&Stage)
    {
        auto& F=S.Portraits;
        if(F.Index<0)
        {
            F.Started=Now;F.Target.Reset(NewObject<UTextureRenderTarget2D>());
            F.Target->RenderTargetFormat=ETextureRenderTargetFormat::RTF_RGBA8_SRGB;F.Target->ClearColor=FLinearColor(.006f,.007f,.009f,1);
            F.Target->InitAutoFormat(512,512);F.Target->UpdateResourceImmediate(true);
            F.Directory=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("DraftPortraits"),FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ"))));
            IFileManager::Get().MakeDirectory(*F.Directory,true);
            for(const auto& P:CireChampionRoster::All())F.Ids.Add(P.Id);
            FString Only;if(FParse::Value(FCommandLine::Get(),TEXT("CireDraftPortraitIds="),Only,false)){TArray<FString> Subset;Only.ParseIntoArray(Subset,TEXT(","));if(!Subset.IsEmpty())F.Ids=Subset;}
            F.Index=0;Stage->SetTurntable(false,-16.f);Stage->SetPortraitTarget(F.Target.Get());Stage->ShowProfile(F.Ids[0]);
        }
        else if(F.Ids.IsValidIndex(F.Index)&&Stage->GetProfileId()!=F.Ids[F.Index])
        {F.StreamedFrame=0;Stage->SetTurntable(false,-16.f);Stage->SetPortraitTarget(F.Target.Get());Stage->ShowProfile(F.Ids[F.Index]);}
        else if(F.Ids.IsValidIndex(F.Index)&&Stage->IsPreviewReady()&&Stage->SecondsShown()>2.2f&&Stage->FramesShown()>45&&F.StreamedFrame==0)
        {
            // Block once until the close-up textures are resident, then let TAA settle again.
            IStreamingManager::Get().StreamAllResources(8.f);F.StreamedFrame=GFrameCounter;
        }
        else if(F.Ids.IsValidIndex(F.Index)&&F.StreamedFrame>0&&GFrameCounter>F.StreamedFrame+30)
        {
            F.StreamedFrame=0;
            TArray<FColor> Pixels;
            FTextureRenderTargetResource* Resource=F.Target->GameThread_GetRenderTargetResource();
            bool bOk=Resource&&Resource->ReadPixels(Pixels)&&Pixels.Num()==512*512;
            double Luma=0;int32 Bright=0,Clipped=0;
            for(FColor& Pixel:Pixels){Pixel.A=255;const double L=.2126*Pixel.R+.7152*Pixel.G+.0722*Pixel.B;Luma+=L;Bright+=L>40;Clipped+=L>236;}
            if(bOk){Luma/=Pixels.Num();bOk=Luma>6&&Bright>512*512/40;}
            // Auto exposure trim: pale stone/fel bodies clip under the key light. Step down
            // (or up for very dark bodies) and re-render; the result is stored for the live preview.
            const float ClipShare=Bright>0?static_cast<float>(Clipped)/Bright:0.f,SubjectLuma=Bright>0?static_cast<float>(Luma*Pixels.Num()/Bright):0.f;
            if(bOk&&F.Attempt<3&&((ClipShare>.15f&&Stage->GetExposureOffset()>-1.5f)||(SubjectLuma<45.f&&Stage->GetExposureOffset()<1.f)))
            {
                const float StepStops=ClipShare>.15f?-FMath::Clamp(ClipShare*3.f,.3f,.8f):.4f;
                ++F.Attempt;Stage->SetExposureOffset(Stage->GetExposureOffset()+StepStops);
                UE_LOG(LogCireDraft,Display,TEXT("CIRE_DRAFT_PORTRAIT_EXPOSURE id=%s clipped=%.3f subject=%.1f offset=%.2f"),*F.Ids[F.Index],ClipShare,SubjectLuma,Stage->GetExposureOffset());
                F.StreamedFrame=GFrameCounter;return;
            }
            F.Exposure.Add(F.Ids[F.Index],Stage->GetExposureOffset());F.Attempt=0;
            const FString File=FPaths::Combine(F.Directory,F.Ids[F.Index]+TEXT(".png"));
            bOk=bOk&&FImageUtils::SaveImageByExtension(*File,FImageView(Pixels.GetData(),512,512));
            UE_LOG(LogCireDraft,Display,TEXT("CIRE_DRAFT_PORTRAIT %s id=%s luma=%.1f file=%s"),bOk?TEXT("PASS"):TEXT("FAIL"),*F.Ids[F.Index],Luma,*File);
            F.bPass&=bOk;if(bOk)F.Files.Add(File);
            if(++F.Index<F.Ids.Num())Stage->ShowProfile(F.Ids[F.Index]);
        }
        else if(F.Ids.IsValidIndex(F.Index)&&Stage->SecondsShown()>25.f)
        {
            UE_LOG(LogCireDraft,Error,TEXT("CIRE_DRAFT_PORTRAIT FAIL id=%s (preview did not bind)"),*F.Ids[F.Index]);
            F.bPass=false;if(++F.Index<F.Ids.Num())Stage->ShowProfile(F.Ids[F.Index]);
        }
        if(F.Index>=F.Ids.Num()||Now-F.Started>240)
        {
            F.bDone=true;F.bPass&=F.Index>=F.Ids.Num()&&F.Files.Num()==F.Ids.Num();
            {
                FString Json=TEXT("{\n");int32 N=0;
                for(const auto& Pair:F.Exposure)Json+=FString::Printf(TEXT("%s  \"%s\": %.2f"),N++?TEXT(",\n"):TEXT(""),*Pair.Key,Pair.Value);
                Json+=TEXT("\n}\n");
                FFileHelper::SaveStringToFile(Json,*FPaths::Combine(F.Directory,TEXT("Exposure.json")));
            }
            UE_LOG(LogCireDraft,Display,TEXT("CIRE_DRAFT_PORTRAITS_%s count=%d directory=%s"),F.bPass?TEXT("PASS"):TEXT("FAIL"),F.Files.Num(),*F.Directory);
            FPlatformMisc::RequestExitWithStatus(false,F.bPass?0:1);
        }
        Panel(CX0,GridR.Y+GridR.H*.5f-20,CWc,40,FLinearColor(0,0,0,.8f));
        Txt(FString::Printf(TEXT("PORTRAIT %d / %d"),FMath::Min(F.Index+1,F.Ids.Num()),F.Ids.Num()),CX0+20,GridR.Y+GridR.H*.5f-8,12,Gold,Screen,ECireFont::Heading);
    }
    // ---------- Fixture: screenshot gallery with layout audit (Tools/RunDraftGallery.py) ----------
    if(S.Gallery.bActive&&!S.Gallery.bDone)
    {
        auto& G=S.Gallery;
        // Filter: -1 all, 0 tank, 1 dps, 2 support. Empty Select = nothing chosen yet. Timer = seconds shown.
        struct FShot{const TCHAR* Name;int32 Filter;const TCHAR* Hover;const TCHAR* Select;int32 Tab;bool bOutro;float Timer;const TCHAR* Search;};
        static const FShot Shots[]={
            {TEXT("01_browse_all"),-1,TEXT(""),TEXT(""),0,false,84.f,TEXT("")},
            {TEXT("02_tank_hover_knight"),0,TEXT("knight"),TEXT(""),0,false,77.f,TEXT("")},
            {TEXT("03_tank_selected_knight"),0,TEXT(""),TEXT("knight"),0,false,69.f,TEXT("")},
            {TEXT("04_dps_selected_hybrid_wizard"),1,TEXT(""),TEXT("wizard"),0,false,61.f,TEXT("")},
            {TEXT("05_support_abilities_hover_keeper"),2,TEXT("keeper_of_light"),TEXT("wizard"),1,false,52.f,TEXT("")},
            {TEXT("06_teammate_locked_dryad_lore"),2,TEXT("dryad"),TEXT("whisp"),2,false,41.f,TEXT("")},
            {TEXT("07_search_golem"),-1,TEXT("ether_golem_support"),TEXT(""),0,false,33.f,TEXT("golem")},
            {TEXT("08_timer_low_behemoth"),-1,TEXT(""),TEXT("totemic_behemoth"),0,false,6.f,TEXT("")},
            {TEXT("09_locked_in_knight"),-1,TEXT(""),TEXT("knight"),0,true,0.f,TEXT("")}};
        // new-champions: -CireDraftGalleryChampions=a,b,... replaces the fixed states with each champion
        // selected (overview) and on its abilities tab, so new rosters can be reviewed in champion select.
        static TArray<FShot> ShotList;static TArray<FString> ShotStrings;
        if(ShotList.IsEmpty())
        {
            FString Ids;
            if(FParse::Value(FCommandLine::Get(),TEXT("CireDraftGalleryChampions="),Ids,false))
            {
                TArray<FString> List;Ids.ParseIntoArray(List,TEXT(","),true);
                ShotStrings.Reserve(List.Num()*4);
                for(const FString& Id:List)
                {
                    const FString& Stored=ShotStrings.Add_GetRef(Id);
                    const FString& Overview=ShotStrings.Add_GetRef(FString::Printf(TEXT("%02d_selected_%s"),ShotList.Num()+1,*Id));
                    ShotList.Add({*Overview,-1,TEXT(""),*Stored,0,false,60.f,TEXT("")});
                    const FString& Kit=ShotStrings.Add_GetRef(FString::Printf(TEXT("%02d_abilities_%s"),ShotList.Num()+1,*Id));
                    ShotList.Add({*Kit,-1,TEXT(""),*Stored,1,false,50.f,TEXT("")});
                }
            }
            if(ShotList.IsEmpty())for(const FShot& Shot:Shots)ShotList.Add(Shot);
        }
        // Audit the frame in which the previous shot was requested (the frame is complete now).
        const auto RunAudit=[&](const FString& ShotName)
        {
            TArray<FString> Issues;int32 Names=0;float MinText=MAX_flt,MinName=MAX_flt;
            FString Json=FString::Printf(TEXT("{\n  \"shot\": \"%s\",\n  \"pixels\": [%d, %d],\n  \"scale\": %.4f,\n  \"logical\": [%.1f, %.1f],\n  \"items\": [\n"),*ShotName,Canvas?int32(Canvas->ClipX):0,Canvas?int32(Canvas->ClipY):0,Scale,VW,VH);
            for(int32 I=0;I<S.Audit.Num();++I)
            {
                const FAuditItem& A=S.Audit[I];
                const bool bInBox=A.Box.Contains(A.Rect),bOnScreen=Safe.Inset(-M+1.f).Contains(A.Rect);
                if(!bInBox)Issues.Add(FString::Printf(TEXT("%s '%s' [%.1f,%.1f %.1fx%.1f] outside box [%.1f,%.1f %.1fx%.1f]"),A.Kind==2?TEXT("card"):TEXT("text"),*A.What,A.Rect.X,A.Rect.Y,A.Rect.W,A.Rect.H,A.Box.X,A.Box.Y,A.Box.W,A.Box.H));
                if(!bOnScreen)Issues.Add(FString::Printf(TEXT("'%s' off screen"),*A.What));
                if(A.Kind==3&&!Safe.Contains(A.Rect))Issues.Add(TEXT("title outside the safe area"));
                if(A.Kind!=2)
                {
                    MinText=FMath::Min(MinText,A.Size);
                    if(A.Size<9.4f)Issues.Add(FString::Printf(TEXT("text '%s' too small (%.1f)"),*A.What,A.Size));
                }
                if(A.Kind==1){++Names;MinName=FMath::Min(MinName,A.Size);if(A.Size<10.f)Issues.Add(FString::Printf(TEXT("card name '%s' too small (%.1f)"),*A.What,A.Size));}
                FString What=A.What.Replace(TEXT("\\"),TEXT("\\\\")).Replace(TEXT("\""),TEXT("\\\""));
                Json+=FString::Printf(TEXT("    {\"kind\": %d, \"what\": \"%s\", \"size\": %.1f, \"rect\": [%.1f, %.1f, %.1f, %.1f], \"box\": [%.1f, %.1f, %.1f, %.1f], \"ok\": %s}%s\n"),
                    A.Kind,*What,A.Size,A.Rect.X,A.Rect.Y,A.Rect.W,A.Rect.H,A.Box.X,A.Box.Y,A.Box.W,A.Box.H,bInBox&&bOnScreen?TEXT("true"):TEXT("false"),I+1<S.Audit.Num()?TEXT(","):TEXT(""));
            }
            bool bTitle=false;for(const FAuditItem& A:S.Audit)bTitle|=A.Kind==3;
            if(!bTitle)Issues.Add(TEXT("title not drawn"));
            Json+=TEXT("  ],\n  \"issues\": [\n");
            for(int32 I=0;I<Issues.Num();++I)Json+=FString::Printf(TEXT("    \"%s\"%s\n"),*Issues[I].Replace(TEXT("\""),TEXT("'")),I+1<Issues.Num()?TEXT(","):TEXT(""));
            Json+=TEXT("  ]\n}\n");
            FFileHelper::SaveStringToFile(Json,*FPaths::Combine(G.Directory,ShotName+TEXT(".layout.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            if(!Issues.IsEmpty())++G.LayoutFailures;
            UE_LOG(LogCireDraft,Display,TEXT("CIRE_DRAFT_LAYOUT_%s shot=%s items=%d cards=%d min_text=%.1f min_name=%.1f issues=%d%s%s"),Issues.IsEmpty()?TEXT("PASS"):TEXT("FAIL"),*ShotName,
                S.Audit.Num(),Names,MinText==MAX_flt?0.f:MinText,MinName==MAX_flt?0.f:MinName,Issues.Num(),Issues.IsEmpty()?TEXT(""):TEXT(" first="),Issues.IsEmpty()?TEXT(""):*Issues[0]);
        };
        if(G.Stage<0)
        {
            G.Started=Now;G.Stage=0;G.StageAt=Now;
            G.Directory=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("DraftGallery"),FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ"))));
            FString Tag;if(FParse::Value(FCommandLine::Get(),TEXT("CireDraftGalleryTag="),Tag))G.Directory+=TEXT("_")+Tag;
            IFileManager::Get().MakeDirectory(*G.Directory,true);
        }
        // Standalone waits for the human to draft before bots fill, so the fixture adds four
        // inert teammates: a human who locked Thornweave Dryad, two bot picks and one still picking.
        if(Mates.IsEmpty()&&World&&World->GetNetMode()==NM_Standalone)
        {
            // Teammates: Mira locked Thornweave Dryad, two bot picks, Tobin still choosing (Iron Warden
            // selected, not locked). Enemies: two locked, one picking; their champions stay hidden.
            static const TCHAR* MateIds[]={TEXT("dryad"),TEXT("ranger"),TEXT("dwarf_miner"),TEXT(""),TEXT("lancer"),TEXT("bear"),TEXT("")};
            static const TCHAR* MateNames[]={TEXT("Mira"),TEXT("Ember 3"),TEXT("Ember 4"),TEXT("Tobin"),TEXT("Kestrel"),TEXT("Dusk 2"),TEXT("Vale")};
            for(int32 I=0;I<7;++I)
            {
                FActorSpawnParameters SP;SP.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;SP.bDeferConstruction=true;
                const FTransform Where(FVector(120000.f+I*400.f,120000.f,90000.f));
                ACireHero* Mate=World->SpawnActor<ACireHero>(ACireHero::StaticClass(),Where,SP);if(!Mate)continue;
                Mate->SetReplicates(false);Mate->TeamId=I<4?Hero->TeamId:1-FMath::Clamp(Hero->TeamId,0,1);Mate->bBot=I==1||I==2||I==5;Mate->FinishSpawning(Where);
                Mate->SetActorTickEnabled(false);Mate->SetActorHiddenInGame(true);Mate->SetActorEnableCollision(false);
                Mate->GetCharacterMovement()->DisableMovement();Mate->GetCharacterMovement()->SetComponentTickEnabled(false);
                if(*MateIds[I])Mate->DraftProfile(MateIds[I]);
                Mate->HeroName=MateNames[I];
                if(I==3)Mate->DraftHoverId=TEXT("knight");
            }
        }
        if(G.bShotThisFrame){G.bShotThisFrame=false;RunAudit(G.PendingShot);}
        int32 ShotCount=ShotList.Num();
        FParse::Value(FCommandLine::Get(),TEXT("CireDraftGalleryShots="),ShotCount);ShotCount=FMath::Clamp(ShotCount,1,ShotList.Num());
        if(G.Stage<ShotCount)
        {
            const FShot& Shot=ShotList[G.Stage];
            DebugSetPointer(FVector2D(VW-1.f,VH-1.f)); // keep the real cursor from raising tooltips
            S.Filter=Shot.Filter;S.InfoTab=Shot.Tab;S.ForcedHover=Shot.Hover;S.bForceOutro=Shot.bOutro;S.ForcedTimer=Shot.Timer;
            S.bChosen=*Shot.Select!=0;S.SelectedId=Shot.Select;if(S.bChosen)S.CursorId=Shot.Select;
            if(Controller){Controller->DraftSearch=Shot.Search;Controller->bDraftSearch=*Shot.Search!=0;}
            FString Want=FString(Shot.Hover).IsEmpty()?FString(Shot.Select):FString(Shot.Hover);
            if(Want.IsEmpty()&&S.Tiles.IsValidIndex(S.Cursor))Want=S.Tiles[S.Cursor].Profile->Id;
            // Freshly imported textures compile asynchronously in -game: wait until the backdrop
            // and every visible portrait are real (not the default checker).
            bool bTexturesReady=true;
            if(UTexture2D* Bg=Background(S.BgId))bTexturesReady&=!Bg->IsDefaultTexture();
            for(const FTile& T:S.Tiles)if(UTexture2D* Face=Portrait(T.Profile->Id))bTexturesReady&=!Face->IsDefaultTexture();
            const bool bStageReady=Want.IsEmpty()||(Stage&&Stage->GetProfileId()==Want&&Stage->IsPreviewReady()&&Stage->SecondsShown()>2.6f&&Stage->FramesShown()>60);
            const bool bReady=bStageReady&&bTexturesReady&&Now-G.Started>6.0&&Now-S.SplashSince>1.0&&Now-G.StageAt>.5&&Now-S.BgSince>.8;
            if(G.ShotAt==0&&bReady)
            {
                const FString File=FPaths::Combine(G.Directory,FString(Shot.Name)+TEXT(".png"));
                if(Stage&&Stage->GetRenderTarget()&&FParse::Param(FCommandLine::Get(),TEXT("CireDraftDumpFigure")))
                {
                    // Debug: the raw figure render (RGBA as stored) and its depth, next to the screenshot.
                    TArray<FColor> Px;FTextureRenderTargetResource* RT=Stage->GetRenderTarget()->GameThread_GetRenderTargetResource();
                    const int32 W=Stage->GetRenderTarget()->SizeX,H=Stage->GetRenderTarget()->SizeY;
                    if(RT&&RT->ReadPixels(Px)&&Px.Num()==W*H)FImageUtils::SaveImageByExtension(*FPaths::Combine(G.Directory,FString(Shot.Name)+TEXT(".figure.png")),FImageView(Px.GetData(),W,H));
                    TArray<FLinearColor> Depth;
                    if(UTextureRenderTarget2D* DT=Stage->GetDepthTarget())if(FTextureRenderTargetResource* DR=DT->GameThread_GetRenderTargetResource();DR&&DR->ReadLinearColorPixels(Depth)&&Depth.Num()==int32(DT->SizeX*DT->SizeY))
                    {
                        TArray<FColor> Vis;Vis.SetNum(Depth.Num());const float Max=Stage->GetCutoutMaxDepth();
                        for(int32 I=0;I<Depth.Num();++I){const uint8 V=uint8(FMath::Clamp(Depth[I].R/Max,0.f,1.f)*255);Vis[I]=FColor(V,V,V,255);}
                        FImageUtils::SaveImageByExtension(*FPaths::Combine(G.Directory,FString(Shot.Name)+TEXT(".depth.png")),FImageView(Vis.GetData(),DT->SizeX,DT->SizeY));
                    }
                }
                if(G.Stage==2&&Stage&&Stage->GetRenderTarget())
                {
                    // Cutout check: the live figure's render must carry alpha (empty corners, solid centre).
                    TArray<FColor> Px;FTextureRenderTargetResource* RT=Stage->GetRenderTarget()->GameThread_GetRenderTargetResource();
                    if(RT&&RT->ReadPixels(Px)&&Px.Num()>0)
                    {
                        const int32 W=Stage->GetRenderTarget()->SizeX,H=Stage->GetRenderTarget()->SizeY;int32 Opaque=0;
                        for(const FColor& C:Px)Opaque+=C.A>200;
                        const auto At=[&](float FX,float FY){const FColor& C=Px[FMath::Clamp(int32(FY*H),0,H-1)*W+FMath::Clamp(int32(FX*W),0,W-1)];return FString::Printf(TEXT("%d/%d/%d/%d"),C.R,C.G,C.B,C.A);};
                        UE_LOG(LogCireDraft,Display,TEXT("CIRE_DRAFT_CUTOUT top=%s centre=%s feet=%s side=%s corner=%s opaque_share=%.3f"),*At(.5f,.04f),*At(.5f,.45f),*At(.5f,.97f),*At(.06f,.5f),*At(0,0),Opaque/static_cast<float>(Px.Num()));
                    }
                }
                FScreenshotRequest::RequestScreenshot(File,false,false,false,FIntRect(),true);G.Files.Add(File);G.ShotAt=Now;
                G.bShotThisFrame=true;G.PendingShot=Shot.Name;
                UE_LOG(LogCireDraft,Display,TEXT("CIRE_DRAFT_GALLERY_SHOT %s preview=%s pixels=%dx%d logical=%.0fx%.0f scale=%.3f figure_px=%.1f,%.1f,%.1f,%.1f figure_uv=%.4f,%.4f,%.4f,%.4f target=%dx%d exposure=%.2f"),*File,*Want,Canvas?int32(Canvas->ClipX):0,Canvas?int32(Canvas->ClipY):0,VW,VH,Scale,
                    S.FigurePx.X,S.FigurePx.Y,S.FigurePx.W,S.FigurePx.H,S.FigureUV[0],S.FigureUV[1],S.FigureUV[2],S.FigureUV[3],Stage&&Stage->GetRenderTarget()?int32(Stage->GetRenderTarget()->SizeX):0,Stage&&Stage->GetRenderTarget()?int32(Stage->GetRenderTarget()->SizeY):0,Stage?Stage->GetExposureOffset():0.f);
            }
            if(G.ShotAt>0&&Now-G.ShotAt>1.2){++G.Stage;G.ShotAt=0;G.StageAt=Now;UE_LOG(LogCireDraft,Display,TEXT("CIRE_DRAFT_GALLERY_STAGE %d"),G.Stage);}
        }
        else if(Now-G.StageAt>1.0)
        {
            G.bDone=true;S.bForceOutro=false;S.ForcedTimer=-1;S.InfoTab=0;if(Controller){Controller->DraftSearch.Reset();Controller->bDraftSearch=false;}
            for(const FString& File:G.Files)G.bPass&=IFileManager::Get().FileSize(*File)>20000;
            G.bPass&=G.Files.Num()==ShotCount&&G.LayoutFailures==0;
            UE_LOG(LogCireDraft,Display,TEXT("CIRE_DRAFT_GALLERY_%s captures=%d layout_failures=%d directory=%s"),G.bPass?TEXT("PASS"):TEXT("FAIL"),G.Files.Num(),G.LayoutFailures,*G.Directory);
            FPlatformMisc::RequestExitWithStatus(false,G.bPass?0:1);
        }
        if(!G.bDone&&Now-G.Started>240){G.bDone=true;UE_LOG(LogCireDraft,Error,TEXT("CIRE_DRAFT_GALLERY_FAIL timeout"));FPlatformMisc::RequestExitWithStatus(false,1);}
    }
#endif
    ResetTransform();
}
