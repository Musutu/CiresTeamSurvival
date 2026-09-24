// Champion draft screen (DOTA-style): role-column portrait grid, live 3D preview
// of the real champion body on a lit stage, class/role/stat/lore/ability details,
// teammate picks and lock-in. Drawn by ACireHUD while the local hero is undrafted.
#include "CireHUD.h"
#include "CireChampionRoster.h"
#include "CireChampionProfiles.h"
#include "CireDraftStage.h"
#include "CireAbilityIcons.h"
#include "CireUIStyle.h"
#include "CireGame.h"
#include "CireSummon.h"
#include "Engine/Canvas.h"
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
#include "UnrealClient.h"
#include "ContentStreaming.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireDraft,Log,All);

namespace
{
constexpr int32 PageSize=6;
constexpr float DesignW=1280.f;
FLinearColor SRGB(uint8 R,uint8 G,uint8 B,uint8 A=255){return FLinearColor::FromSRGBColor(FColor(R,G,B,A));}
// Scale a colour's perceived (sRGB) brightness; used for role-tinted backgrounds and borders.
FLinearColor Tint(const FLinearColor& Color,float K,float Alpha=1.f)
{const FColor C=Color.ToFColor(true);FLinearColor Out=FLinearColor::FromSRGBColor(FColor(uint8(C.R*K),uint8(C.G*K),uint8(C.B*K)));Out.A=Alpha;return Out;}
const FLinearColor Backdrop=SRGB(9,10,13),Ink=SRGB(16,18,23,248),Card=SRGB(24,27,33),CardHi=SRGB(40,44,52);
const FLinearColor Gold=SRGB(214,170,98),GoldDim=SRGB(104,82,48),Text=SRGB(236,230,214),Muted=SRGB(138,142,145),Faint=SRGB(58,62,66);
const FLinearColor TankColor=SRGB(92,148,228),DpsColor=SRGB(216,80,64),SupportColor=SRGB(88,198,126);
const FLinearColor LockRed=SRGB(150,34,34);

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
FString Initials(const FString& Name)
{
    TArray<FString> Words;Name.ParseIntoArrayWS(Words);FString Out;
    for(const FString& W:Words)if(!W.IsEmpty()&&FChar::IsAlpha(W[0])&&Out.Len()<2)Out.AppendChar(FChar::ToUpper(W[0]));
    return Out;
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

struct FTile
{
    const FCireChampionProfile* Profile=nullptr;
    int32 Column=0;
    bool bSecondary=false;
    float X=0,Y=0,W=0,H=0;
};

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
    bool bActive=false,bDone=false,bPass=true;
    int32 Stage=-1;double StageAt=0,Started=0,ShotAt=0;
    FString Directory;TArray<FString> Files;
};

struct FDraftUI
{
    TWeakObjectPtr<ACireDraftStage> Stage;
    TArray<FTile> Tiles;
    int32 Cursor=0;
    FString Hovered,ForcedHover,LastClickId;
    double HoverSince=0,LastClickAt=0,LockRequestedAt=-100;
    FString LockRequestedId;
    FPortraitFixture Portraits;
    FGalleryFixture Gallery;
    bool bInitialized=false;
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
}

int32 ACireHUD::DraftRosterPageCount() const {return FMath::Max(1,FMath::DivideAndRoundUp(RosterSize(),PageSize));}
FString ACireHUD::DraftRosterIdForSlot(int32 Slot) const
{
    // Stable index mapping kept for developer galleries/probes; the draft screen itself
    // is a role grid (see DrawDraftRoster).
    if(Slot<0||Slot>=PageSize)return {};
    const int32 Page=FMath::Clamp(RosterPage,0,DraftRosterPageCount()-1),Index=Page*PageSize+Slot;
    if(const auto* Profile=CireChampionRoster::FindByIndex(Index))return Profile->Id;
    if(CireChampionRoster::Count()==0)if(const TCHAR* Id=CireChampionProfiles::LegacyProfileId(Index))return Id;
    return {};
}
void ACireHUD::ChangeDraftRosterPage(int32 Delta)
{
    // Left/Right arrows (routed here by the controller) move the grid cursor sideways.
    auto* H=PlayerOwner?Cast<ACireHero>(PlayerOwner->GetPawn()):nullptr;
    if(!H||H->bDrafted||bSettings||bEditLayout)return;
    RosterPage=FMath::Clamp(RosterPage+FMath::Clamp(Delta,-1,1),0,DraftRosterPageCount()-1);
    auto& S=StateFor(this);
    if(S.Tiles.Num()>0&&Delta!=0){S.Cursor=Nearest(S.Tiles,S.Cursor,Delta>0?1:-1,0);S.Hovered.Reset();S.ForcedHover.Reset();}
}
bool ACireHUD::DraftRosterSlot(int32 Slot)
{
    // Number keys 1-6 select (preview) the Nth champion in the cursor's role column.
    // Locking in is a separate, deliberate action (Space / LOCK IN / double-click).
    auto* PC=Cast<ACireController>(PlayerOwner);auto* H=PC?Cast<ACireHero>(PC->GetPawn()):nullptr;
    const auto* State=GetWorld()?GetWorld()->GetGameState<ACireGameState>():nullptr;
    if(!PC||!H||H->bDrafted||H->bDead||bSettings||bEditLayout||(State&&State->Phase==3))return false;
    if(CireChampionRoster::Count()==0)
    {if(Slot<0||Slot>=5)return false;PC->ServerAction(5,Slot,nullptr);return true;}
    auto& S=StateFor(this);
    if(!S.Tiles.IsValidIndex(S.Cursor))return false;
    const int32 Column=S.Tiles[S.Cursor].Column;int32 Seen=0;
    for(int32 I=0;I<S.Tiles.Num();++I)if(S.Tiles[I].Column==Column&&Seen++==Slot){S.Cursor=I;S.Hovered.Reset();S.ForcedHover.Reset();return true;}
    return false;
}

void ACireHUD::DrawDraftRoster(ACireHero* Hero,ACireController* Controller)
{
    if(!Hero||Hero->bDrafted)return;
    ResetTransform();
    auto& S=StateFor(this);
    UWorld* World=GetWorld();
    const double Now=FPlatformTime::Seconds();
    const bool Interactive=Controller&&!bSettings&&!bEditLayout;
    const auto PX=[&](float V){return V*Scale;};

    // Legacy fallback if the roster data failed to load: the five original bodies.
    if(CireChampionRoster::Count()==0)
    {
        Panel(0,0,ViewW,ViewH,Backdrop);Label(TEXT("CHOOSE YOUR CHAMPION"),ViewW*.5f-160,120,26,Text);
        static const TCHAR* Names[]={TEXT("Iron Warden"),TEXT("Ash Ranger"),TEXT("Veil Scholar"),TEXT("Lancer"),TEXT("Rift Summoner")};
        for(int32 I=0;I<5;++I)
        {
            const float BX=ViewW*.5f-400+I*164,BY=260;const bool Over=Interactive&&Hit(BX,BY,150,90);
            Panel(BX,BY,150,90,Over?CardHi:Card);Label(Names[I],BX+12,BY+34,14,Over?Gold:Text);
            if(Over&&Clicked){DraftRosterSlot(I);Clicked=false;}
        }
        return;
    }

    // Development fixtures: portrait generation and the screenshot gallery.
#if !UE_BUILD_SHIPPING
    if(!S.bInitialized)
    {
        S.Portraits.bActive=FParse::Param(FCommandLine::Get(),TEXT("CireDraftPortraits"));
        S.Gallery.bActive=!S.Portraits.bActive&&FParse::Param(FCommandLine::Get(),TEXT("CireDraftGallery"));
    }
#endif
    S.bInitialized=true;

    if(!S.Stage.IsValid())S.Stage=ACireDraftStage::SpawnStage(World);
    ACireDraftStage* Stage=S.Stage.Get();
    if(Stage)Stage->Touch();

    // ---------- Backdrop ----------
    const float X0=FMath::Max(0.f,(ViewW-DesignW)*.5f);
    Panel(0,0,ViewW,ViewH,Backdrop);
    for(int32 I=0;I<8;++I)Panel(0,I*8.f,ViewW,8,SRGB(70,50,26,uint8(26*(8-I)/8)));
    Panel(0,66,ViewW,1,FLinearColor(Gold.R,Gold.G,Gold.B,.35f));

    // ---------- Tiles: primary role first, then hybrids flagged in their other columns ----------
    S.Tiles.Reset();
    const float GridX=X0+20,GridY=112,ColW=198,ColGap=11,TileW=58,TileH=82,TileGap=6;
    for(int32 C=0;C<3;++C)
    {
        int32 N=0;
        for(int32 Pass=0;Pass<2;++Pass)for(const auto& P:CireChampionRoster::All())
        {
            const bool bPrimary=CireChampionProfiles::PrimaryRole(P)==Columns[C].Role;
            const bool bSecondary=!bPrimary&&(CireChampionProfiles::ProfileRoleMask(P)&Cires::RoleBit(Columns[C].Role))!=0;
            if((Pass==0&&!bPrimary)||(Pass==1&&!bSecondary))continue;
            FTile T;T.Profile=&P;T.Column=C;T.bSecondary=Pass==1;
            T.X=GridX+C*(ColW+ColGap)+8+(N%3)*(TileW+TileGap+2);T.Y=GridY+52+(N/3)*(TileH+TileGap);T.W=TileW;T.H=TileH;
            S.Tiles.Add(T);++N;
        }
    }
    if(S.Tiles.IsEmpty())return;
    S.Cursor=FMath::Clamp(S.Cursor,0,S.Tiles.Num()-1);

    // ---------- Keyboard: Up/Down/Home/Space here; Left/Right and 1-6 arrive via the controller ----------
    if(Interactive&&PlayerOwner)
    {
        if(PlayerOwner->WasInputKeyJustPressed(EKeys::Up)){S.Cursor=Nearest(S.Tiles,S.Cursor,0,-1);S.Hovered.Reset();S.ForcedHover.Reset();}
        if(PlayerOwner->WasInputKeyJustPressed(EKeys::Down)){S.Cursor=Nearest(S.Tiles,S.Cursor,0,1);S.Hovered.Reset();S.ForcedHover.Reset();}
        if(PlayerOwner->WasInputKeyJustPressed(EKeys::Home)){S.Cursor=0;S.Hovered.Reset();}
    }

    // Teammate picks: a human teammate's lock blocks that champion; bot picks never do.
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

    // Hover detection before drawing so the preview reacts this frame.
    FString HoverNow;
    if(Interactive)for(const FTile& T:S.Tiles)if(Hit(T.X,T.Y,T.W,T.H)){HoverNow=T.Profile->Id;break;}
    if(!S.ForcedHover.IsEmpty())HoverNow=S.ForcedHover;
    if(HoverNow!=S.Hovered){S.Hovered=HoverNow;S.HoverSince=Now;}
    const FCireChampionProfile* Selected=S.Tiles[S.Cursor].Profile;
    const bool bHoverSettled=!S.Hovered.IsEmpty()&&(Now-S.HoverSince>.12||!S.ForcedHover.IsEmpty());
    const FCireChampionProfile* Shown=bHoverSettled?CireChampionRoster::Find(S.Hovered):Selected;
    if(!Shown)Shown=Selected;
    if(Stage&&!S.Portraits.bActive)Stage->ShowProfile(Shown->Id);
    const auto ShownPrimary=CireChampionProfiles::PrimaryRole(*Shown);
    const FLinearColor ShownColor=RoleColor(ShownPrimary);

    // ---------- Header ----------
    Label(TEXT("CHOOSE YOUR CHAMPION"),X0+22,14,24,Text);
    Label(TEXT("Every champion starts with a basic attack. As you level, you draft six actives, one passive and one ultimate from your role's skill pool."),X0+23,46,10.5f,Muted);
    {
        int32 Locked=0;for(const auto* M:Mates)Locked+=M->bDrafted;
        const FString Team=FString::Printf(TEXT("%s TEAM  |  %d / %d LOCKED"),Hero->TeamId==0?TEXT("EMBER"):TEXT("DUSK"),Locked,Mates.Num()+1);
        Label(Team,X0+DesignW-22-TextWidth(Team,12),16,12,Gold);
        const FString Sub=FString::Printf(TEXT("%d champions  |  %d hybrid listings"),CireChampionRoster::Count(),S.Tiles.Num()-CireChampionRoster::Count());
        Label(Sub,X0+DesignW-22-TextWidth(Sub,9.5f),38,9.5f,Muted);
    }

    // ---------- Role columns ----------
    for(int32 C=0;C<3;++C)
    {
        const float CX=GridX+C*(ColW+ColGap),CY=GridY-30;
        const FLinearColor Col=Columns[C].Color;
        CireUIStyle::Frame(Painter(),CX,CY,ColW,ViewH-CY-86,Col,ECireFrame::Panel);
        Panel(CX+3,CY+3,ColW-6,ViewH-CY-92,Tint(Col,.30f,.10f));
        Icon(Columns[C].Sigil,CX+8,CY+9,26,Col);
        Label(Columns[C].Title,CX+40,CY+8,17,Text);
        int32 Primary=0,Hybrid=0;for(const FTile& T:S.Tiles)if(T.Column==C){Primary+=!T.bSecondary;Hybrid+=T.bSecondary;}
        const FString Count=Hybrid>0?FString::Printf(TEXT("%d + %d hybrid"),Primary,Hybrid):FString::Printf(TEXT("%d"),Primary);
        Label(Count,CX+ColW-10-TextWidth(Count,9),CY+13,9,Muted);
        Label(Columns[C].Blurb,CX+40,CY+30,8.5f,Muted);
        // Role skill pool: what leveling offers this role (authoritative rules tags).
        const Cires::RoleMask Bit=Cires::RoleBit(Columns[C].Role);
        const auto Pool=Cires::StarterSkillPoolForRoles(Bit);
        int32 Actives=0,Passives=0,Ultimates=0;FString Exclusive,Shared,Universal;
        for(const auto& Skill:Pool)
        {
            Actives+=Skill.Kind==Cires::SkillKind::Active;Passives+=Skill.Kind==Cires::SkillKind::Passive;Ultimates+=Skill.Kind==Cires::SkillKind::Ultimate;
            const Cires::RoleMask SkillTags=Cires::SkillRoleTags(Skill.Id);const FString Name=UTF8_TO_TCHAR(Skill.Name.c_str());
            if(SkillTags==Bit)Exclusive+=(Exclusive.IsEmpty()?TEXT(""):TEXT(", "))+Name;
            else if(SkillTags!=Cires::RoleAll)Shared+=(Shared.IsEmpty()?TEXT(""):TEXT(", "))+Name;
            else if(Skill.Kind!=Cires::SkillKind::Passive)Universal+=(Universal.IsEmpty()?TEXT(""):TEXT(", "))+Name;
        }
        const float PY=GridY+52+3*(TileH+TileGap)+14,PW=ColW-16;
        Panel(CX+8,PY,PW,1,Tint(Col,.45f));
        Label(TEXT("ROLE SKILL POOL"),CX+10,PY+4,8.5f,Col);
        const FString Counts=FString::Printf(TEXT("%d actives  |  %d passives  |  %d ultimates"),Actives,Passives,Ultimates);
        Label(Counts,CX+10,PY+17,8.5f,Text);
        Label(TEXT("ONLY THIS ROLE"),CX+10,PY+34,7.5f,Muted);
        Wrapped(Exclusive,CX+10,PY+45,PW-4,8.5f,SRGB(206,200,186),3);
        Label(TEXT("SHARED WITH ANOTHER ROLE"),CX+10,PY+86,7.5f,Muted);
        Wrapped(Shared,CX+10,PY+97,PW-4,8.5f,SRGB(170,166,156),2);
        Label(TEXT("UNIVERSAL"),CX+10,PY+126,7.5f,Muted);
        Wrapped(Universal+TEXT(" + all passives"),CX+10,PY+137,PW-4,8.5f,SRGB(150,148,140),3);
    }
    for(int32 I=0;I<S.Tiles.Num();++I)
    {
        const FTile& T=S.Tiles[I];const auto& P=*T.Profile;
        const auto Primary=CireChampionProfiles::PrimaryRole(P);
        const FLinearColor Col=RoleColor(Primary);
        const bool bCursor=I==S.Cursor,bOver=S.Hovered==P.Id,bShown=Shown==&P;
        const ACireHero* const* Taker=Picked.Find(P.Id);
        const bool bHumanTaken=Taker&&!(*Taker)->bBot;
        Panel(T.X-1,T.Y-1,T.W+2,T.H+2,bCursor?Gold:bOver?FLinearColor(Gold.R,Gold.G,Gold.B,.7f):Tint(Col,.42f));
        if(bCursor)Panel(T.X-3,T.Y-3,T.W+6,1,Gold);
        Panel(T.X,T.Y,T.W,T.W,Card);
        if(UTexture2D* Face=Portrait(P.Id))
            // Tiles zoom into the face/shoulders of the 512px bust so they read at icon size.
            DrawTexture(Face,PX(T.X),PX(T.Y),PX(T.W),PX(T.W),.15f,.08f,.70f,.70f,T.bSecondary?SRGB(150,150,150):FLinearColor::White,BLEND_Opaque);
        else
        {
            for(int32 B=0;B<6;++B){Panel(T.X,T.Y+B*T.W/6,T.W,T.W/6,Tint(Col,.20f*(6-B)/6.f+.05f));}
            Icon(RoleSigil(Primary),T.X+13,T.Y+8,32,FLinearColor(Col.R,Col.G,Col.B,.55f));
            const FString In=Initials(P.DisplayName);Label(In,T.X+T.W-8-TextWidth(In,12),T.Y+T.W-18,12,Text);
        }
        // Name strip.
        Panel(T.X,T.Y+T.W,T.W,T.H-T.W,bCursor||bShown?CardHi:Ink);
        // Full champion name, balanced over up to two lines (variants are told apart by portrait and pips).
        {
            const float NameSize=7.5f;TArray<FString> Lines;
            if(TextWidth(P.DisplayName,NameSize)<=T.W-4)Lines.Add(P.DisplayName);
            else
            {
                TArray<FString> W;P.DisplayName.ParseIntoArrayWS(W);
                int32 Best=1;float BestWidth=MAX_flt;
                for(int32 Split=1;Split<W.Num();++Split)
                {
                    FString A,B;for(int32 K=0;K<W.Num();++K)(K<Split?A:B)+=(K<Split?(A.IsEmpty()?TEXT(""):TEXT(" ")):(B.IsEmpty()?TEXT(""):TEXT(" ")))+W[K];
                    const float Wd=FMath::Max(TextWidth(A,NameSize),TextWidth(B,NameSize));if(Wd<BestWidth){BestWidth=Wd;Best=Split;}
                }
                FString A,B;for(int32 K=0;K<W.Num();++K)(K<Best?A:B)+=(K<Best?(A.IsEmpty()?TEXT(""):TEXT(" ")):(B.IsEmpty()?TEXT(""):TEXT(" ")))+W[K];
                Lines.Add(A);if(!B.IsEmpty())Lines.Add(B);
            }
            const float LineY=T.Y+T.W+(Lines.Num()>1?2.f:7.f);
            for(int32 L=0;L<Lines.Num();++L)
            {
                FString Row=Lines[L];while(Row.Len()>3&&TextWidth(Row,NameSize)>T.W-2)Row.LeftChopInline(1);
                Label(Row,T.X+(T.W-TextWidth(Row,NameSize))*.5f,LineY+L*10.5f,NameSize,bCursor?Gold:Text);
            }
        }
        // Hybrid pips: every other role this champion also fills.
        float PipX=T.X+T.W-8;
        for(const Cires::RoleMask Bit:{Cires::RoleSupport,Cires::RoleDamage,Cires::RoleTank})
            if((CireChampionProfiles::ProfileRoleMask(P)&Bit)&&RoleForBit(Bit)!=Columns[T.Column].Role)
            {Panel(PipX-1,T.Y+2,8,8,Backdrop);Panel(PipX,T.Y+3,6,6,RoleColor(RoleForBit(Bit)));PipX-=9;}
        if(T.bSecondary){Panel(T.X,T.Y,31,11,FLinearColor(0,0,0,.75f));Label(TEXT("HYBRID"),T.X+2,T.Y+1,6.5f,Columns[T.Column].Color);}
        if(Taker)
        {
            Panel(T.X,T.Y,T.W,T.W,FLinearColor(0,0,0,bHumanTaken?.70f:.40f));
            const TCHAR* Tag=bHumanTaken?TEXT("LOCKED"):TEXT("BOT PICK");
            Panel(T.X,T.Y+T.W-13,T.W,12,bHumanTaken?LockRed:SRGB(28,30,34,235));
            Label(Tag,T.X+(T.W-TextWidth(Tag,7))*.5f,T.Y+T.W-12,7,Text);
        }
        if(Interactive&&bOver&&Clicked&&S.ForcedHover.IsEmpty())
        {
            const bool bDouble=S.LastClickId==P.Id&&Now-S.LastClickAt<.35;
            S.Cursor=I;S.LastClickId=P.Id;S.LastClickAt=Now;Clicked=false;
            if(bDouble&&!bHumanTaken&&Controller){Controller->ServerDraftProfile(P.Id);S.LockRequestedAt=Now;S.LockRequestedId=P.Id;}
        }
        FString TipBody=P.ClassType+TEXT(" | ")+RoleName(Primary);
        for(const Cires::RoleMask Bit:{Cires::RoleTank,Cires::RoleDamage,Cires::RoleSupport})
            if((CireChampionProfiles::ProfileRoleMask(P)&Bit)&&RoleForBit(Bit)!=Primary)TipBody+=FString(TEXT(" + "))+RoleName(RoleForBit(Bit));
        if(Taker)TipBody+=FString::Printf(TEXT(". Picked by %s%s"),*(*Taker)->HeroName,(*Taker)->bBot?TEXT(" (bot; still available)"):TEXT(""));
        Tip(P.DisplayName,TipBody+TEXT(". Click to preview; double-click or Space to lock in."),T.X,T.Y,T.W,T.H);
    }

    // ---------- Live preview ----------
    const float PVX=X0+650,PVY=82,PVW=300,PVH=400;
    CireUIStyle::Frame(Painter(),PVX-5,PVY-5,PVW+10,PVH+10,ShownColor,ECireFrame::Inset);Panel(PVX,PVY,PVW,PVH,Backdrop);
    if(Stage&&!S.Portraits.bActive&&Stage->GetProfileId()==Shown->Id&&Stage->IsPreviewReady())
    {
        DrawTexture(Stage->GetRenderTarget(),PX(PVX),PX(PVY),PX(PVW),PX(PVH),0,0,1,1,FLinearColor::White,BLEND_Opaque);
        for(int32 I=0;I<6;++I)
        {
            const float A=.16f*(6-I)/6.f,E=I*4.f;
            Panel(PVX,PVY+E,PVW,4,FLinearColor(0,0,0,A));Panel(PVX,PVY+PVH-E-4,PVW,4,FLinearColor(0,0,0,FMath::Min(1.f,A*1.4f)));
            Panel(PVX+E,PVY,4,PVH,FLinearColor(0,0,0,A));Panel(PVX+PVW-E-4,PVY,4,PVH,FLinearColor(0,0,0,A));
        }
    }
    else
    {
        const FString Wait=TEXT("SUMMONING...");Label(Wait,PVX+(PVW-TextWidth(Wait,12))*.5f,PVY+PVH*.5f-8,12,Muted);
    }
    Panel(PVX,PVY,PVW,2,ShownColor);
    Label(TEXT("LIVE PREVIEW"),PVX+8,PVY+7,8,FLinearColor(Text.R,Text.G,Text.B,.7f));
    {
        const FString Art=Shown->ArtStatus==TEXT("approved_asset")?TEXT("FINAL ART"):TEXT("PROTOTYPE ART");
        Label(Art,PVX+PVW-8-TextWidth(Art,7.5f),PVY+8,7.5f,FLinearColor(Muted.R,Muted.G,Muted.B,.8f));
    }
    Panel(PVX,PVY+PVH-44,PVW,44,FLinearColor(0,0,0,.55f));
    Label(Shown->DisplayName,PVX+(PVW-TextWidth(Shown->DisplayName,17))*.5f,PVY+PVH-40,17,Text);
    {
        const FString Class=Shown->ClassType.ToUpper();
        Label(Class,PVX+(PVW-TextWidth(Class,9))*.5f,PVY+PVH-17,9,Gold);
    }

    // ---------- Attributes / basic attack under the preview ----------
    {
        const float AX=PVX,AY=PVY+PVH+12,AW=PVW;
        CireUIStyle::Frame(Painter(),AX-5,AY,AW+10,ViewH-AY-86,Gold,ECireFrame::Panel);
        Label(TEXT("PRIMARY ATTRIBUTE"),AX+10,AY+8,8,Muted);
        Label(PrimaryName(Shown->PrimaryStat),AX+10,AY+20,13,Gold);
        Label(TEXT("DIFFICULTY"),AX+AW-92,AY+8,8,Muted);
        for(int32 D=0;D<3;++D)Panel(AX+AW-92+D*26,AY+24,22,8,D<Shown->Difficulty?Gold:Faint);
        struct FStatRow {const TCHAR* Key;int32 Value;const TCHAR* Stat;};
        const FStatRow Rows[]={{TEXT("STR"),Shown->Strength,TEXT("strength")},{TEXT("AGI"),Shown->Agility,TEXT("agility")},{TEXT("INT"),Shown->Intelligence,TEXT("intelligence")}};
        for(int32 I=0;I<3;++I)
        {
            const float RY=AY+44+I*17;const bool bPrime=Shown->PrimaryStat==Rows[I].Stat;
            Label(Rows[I].Key,AX+10,RY,9,bPrime?Gold:Muted);
            Bar(AX+42,RY+2,AW-86,9,Rows[I].Value/30.f,bPrime?Gold:SRGB(92,98,104));
            Label(FString::FromInt(Rows[I].Value),AX+AW-36,RY,9,bPrime?Text:Muted);
        }
        const bool bRanged=Shown->BasicAttackRange>300;
        const FString Attack=FString::Printf(TEXT("%s  |  %s  |  %.1f m  |  %.1f s"),bRanged?TEXT("RANGED"):TEXT("MELEE"),*StyleLabel(Shown->AttackStyle),Shown->BasicAttackRange/100.f,Shown->AttackSeconds);
        Label(TEXT("BASIC ATTACK"),AX+10,AY+98,8,Muted);
        Label(Attack,AX+10,AY+110,10.5f,Text);
    }

    // ---------- Info panel: roles, lore, signature kit ----------
    {
        const float IX=PVX+PVW+14,IY=PVY-2,IW=X0+DesignW-20-IX;
        CireUIStyle::Frame(Painter(),IX,IY-3,IW,ViewH-IY-83,ShownColor,ECireFrame::Panel);
        float Y=IY+10;
        float ChipX=IX+12;
        const auto Chip=[&](Cires::SkillDraftRole ChipRole,bool bPrimaryRole)
        {
            const FString RoleText=bPrimaryRole?FString(RoleName(ChipRole)):FString(RoleName(ChipRole))+TEXT(" HYBRID");
            const float W=TextWidth(RoleText,9)+26,Cx=ChipX;const FLinearColor Col=RoleColor(ChipRole);
            Panel(Cx,Y,W,18,bPrimaryRole?Tint(Col,.34f):Card);
            Panel(Cx,Y,3,18,Col);
            Icon(RoleSigil(ChipRole),Cx+5,Y+2,14,Col);
            Label(RoleText,Cx+21,Y+3,9,bPrimaryRole?Text:Col);ChipX+=W+6;
        };
        Chip(ShownPrimary,true);
        for(const Cires::RoleMask Bit:{Cires::RoleTank,Cires::RoleDamage,Cires::RoleSupport})
            if((CireChampionProfiles::ProfileRoleMask(*Shown)&Bit)&&RoleForBit(Bit)!=ShownPrimary)Chip(RoleForBit(Bit),false);
        Y+=28;
        Label(Shown->DisplayName,IX+12,Y,20,Text);Y+=28;
        {
            // The variant adds information only when it is not generic or a repeat of the class caption.
            const bool bVariant=!Shown->Variant.IsEmpty()&&!Shown->ClassType.Contains(Shown->Variant)&&
                Shown->Variant!=TEXT("Damage")&&Shown->Variant!=TEXT("Healer")&&Shown->Variant!=TEXT("Support");
            Label(bVariant?Shown->ClassType+TEXT("  |  ")+Shown->Variant:Shown->ClassType,IX+12,Y,10,Gold);Y+=20;
        }
        if(!Shown->Lore.IsEmpty()){Wrapped(TEXT("\"")+Shown->Lore+TEXT("\""),IX+12,Y,IW-24,9.5f,SRGB(196,188,170),3);Y+=42;}
        Panel(IX+12,Y,IW-24,1,Faint);Y+=8;
        Label(TEXT("SIGNATURE KIT"),IX+12,Y,10,Gold);
        FString PoolNote=FString(RoleName(ShownPrimary));
        for(const Cires::RoleMask Bit:{Cires::RoleTank,Cires::RoleDamage,Cires::RoleSupport})
            if((CireChampionProfiles::ProfileRoleMask(*Shown)&Bit)&&RoleForBit(Bit)!=ShownPrimary)PoolNote+=FString(TEXT(" + "))+RoleName(RoleForBit(Bit));
        const FString KitNote=TEXT("drafted from the ")+PoolNote+TEXT(" pool as you level");
        Label(KitNote,IX+IW-12-TextWidth(KitNote,8),Y+2,8,Muted);Y+=18;
        TArray<TPair<const FCireChampionSkill*,int32>> Kit; // 0 active, 1 passive, 2 ultimate
        for(const auto& A:Shown->Actives)Kit.Add({&A,0});
        Kit.Add({&Shown->Passive,1});Kit.Add({&Shown->Ultimate,2});
        const float RowH=FMath::Min(33.f,(ViewH-86-Y-6)/Kit.Num());
        for(const auto& Entry:Kit)
        {
            const FCireChampionSkill& Skill=*Entry.Key;const bool bPassive=Entry.Value==1,bUlt=Entry.Value==2,bPlanned=!Skill.IsImplemented();
            const bool bRowOver=Interactive&&Hit(IX+8,Y-2,IW-16,RowH);
            if(bRowOver)Panel(IX+8,Y-2,IW-16,RowH,Card);
            const FLinearColor Accent=bUlt?Gold:bPassive?SRGB(170,130,222):ShownColor;
            {
                FCireIconSlot Slot;Slot.IconId=Skill.Id;Slot.IconTexture=CireAbilityIcons::Texture(Skill.Id);Slot.Tint=CireAbilityIcons::Accent(Skill.Id);
                if(!Slot.IconTexture)Slot.IconId=SkillSigil(Skill);
                Slot.Kind=bUlt?ECireSlotKind::Ultimate:bPassive?ECireSlotKind::Passive:ECireSlotKind::Normal;
                CireUIStyle::IconSlot(Painter(),IX+12,Y-1,26,Slot,Now);
                if(bPlanned)Panel(IX+14,Y+1,22,22,FLinearColor(0,0,0,.35f));
            }
            Label(Skill.DisplayName,IX+44,Y,10.5f,bPlanned?Muted:Text);
            float TagX=IX+44+TextWidth(Skill.DisplayName,10.5f)+8;
            const auto Tag=[&](const TCHAR* TagText,FLinearColor Col)
            {const float W=TextWidth(TagText,7)+8;Panel(TagX,Y+2,W,11,Tint(Col,.28f));Label(TagText,TagX+4,Y+2.5f,7,Col);TagX+=W+4;};
            if(bUlt)Tag(TEXT("ULTIMATE"),Gold);
            if(bPassive)Tag(TEXT("PASSIVE"),SRGB(190,156,236));
            if(bPlanned)Tag(TEXT("PLANNED"),Muted);
            FString Line=Skill.Mechanic;
            if(TextWidth(Line,8.5f)>IW-60){while(Line.Len()>8&&TextWidth(Line+TEXT("..."),8.5f)>IW-60)Line.LeftChopInline(1);Line=Line.TrimEnd()+TEXT("...");}
            Label(Line,IX+44,Y+14,8.5f,Muted);
            FString Body=Skill.Mechanic;
            if(Skill.IsImplemented()){const FString Detail=ACireHero::SkillDescription(Skill.Id);if(!Detail.IsEmpty()&&Detail!=Skill.Mechanic)Body+=TEXT(" ")+Detail;}
            else Body+=TEXT(" Planned thematic ability: shown as design intent, not yet in the draft pool.");
            Tip(Skill.DisplayName,Body,IX+8,Y-2,IW-16,RowH);
            Y+=RowH;
        }
    }

    // ---------- Footer: teammates and lock in ----------
    {
        const float FY=ViewH-74;
        Panel(0,FY-6,ViewW,ViewH-FY+6,SRGB(12,13,17));Panel(0,FY-6,ViewW,1,FLinearColor(Gold.R,Gold.G,Gold.B,.3f));
        Label(TEXT("YOUR TEAM"),X0+22,FY+2,9,Muted);
        for(int32 I=0;I<FMath::Min(4,Mates.Num());++I)
        {
            const ACireHero* M=Mates[I];const auto* MP=M->bDrafted?CireChampionRoster::Find(M->ChampionProfileId):nullptr;
            const float BX=X0+22+I*152,BY=FY+16;
            CireUIStyle::Frame(Painter(),BX,BY,144,44,MP?RoleColor(CireChampionProfiles::PrimaryRole(*MP)):Faint,ECireFrame::Card);
            if(MP)Panel(BX,BY,144,1,RoleColor(CireChampionProfiles::PrimaryRole(*MP)));
            if(UTexture2D* Face=MP?Portrait(MP->Id):nullptr)DrawTexture(Face,PX(BX+2),PX(BY+2),PX(40),PX(40),.15f,.08f,.70f,.70f,FLinearColor::White,BLEND_Opaque);
            else {Panel(BX+2,BY+2,40,40,Backdrop);if(MP)Icon(RoleSigil(CireChampionProfiles::PrimaryRole(*MP)),BX+8,BY+8,28,RoleColor(CireChampionProfiles::PrimaryRole(*MP)));}
            FString Who=M->bBot?FString(TEXT("Bot")):(M->GetPlayerState()?M->GetPlayerState()->GetPlayerName():FString(TEXT("Player")));
            if(Who.Len()>16)Who=Who.Left(15)+TEXT(".");
            Label(Who,BX+48,BY+4,8.5f,M->bBot?Muted:Text);
            FString Pick=MP?MP->DisplayName:FString(TEXT("choosing..."));if(Pick.Len()>19)Pick=Pick.Left(18)+TEXT(".");
            Label(Pick,BX+48,BY+16,9,MP?RoleColor(CireChampionProfiles::PrimaryRole(*MP)):Muted);
            Label(M->bDrafted?(M->bBot?TEXT("BOT PICK"):TEXT("LOCKED IN")):TEXT("PICKING"),BX+48,BY+30,7,M->bDrafted&&!M->bBot?Gold:Muted);
        }
        const FString Keys=TEXT("ARROWS / 1-6 browse  |  CLICK select  |  SPACE or DOUBLE-CLICK lock in");
        Label(Keys,PVX+(PVW-TextWidth(Keys,8))*.5f,FY+34,8,Muted);
        const ACireHero* const* Taker=Picked.Find(Selected->Id);
        const bool bBlocked=Taker&&!(*Taker)->bBot;
        const bool bPending=S.LockRequestedId==Selected->Id&&Now-S.LockRequestedAt<2.0;
        const float BW=286,BH=46,BX=X0+DesignW-20-BW,BY=FY+12;
        const bool bOver=Interactive&&!bBlocked&&Hit(BX,BY,BW,BH);
        FString Button=bBlocked?FString(TEXT("TAKEN BY ")+(*Taker)->HeroName).ToUpper():bPending?FString(TEXT("LOCKING IN...")):FString(TEXT("LOCK IN  ")+Selected->DisplayName.ToUpper());
        while(Button.Len()>6&&TextWidth(Button,14)>BW-20)Button.LeftChopInline(1);
        if(!bBlocked)CireUIStyle::Glow(Painter(),BX-6,BY-6,BW+12,BH+12,FLinearColor(1.f,.75f,.3f,.18f+.10f*FMath::Sin(static_cast<float>(Now)*3.f)));
        CireUIStyle::Button(Painter(),BX,BY,BW,BH,Button,bBlocked?ECireButtonState::Disabled:bOver?ECireButtonState::Hover:ECireButtonState::Normal,Gold,14.f);
        bool bLock=false;
        if(bOver&&Clicked){bLock=true;Clicked=false;}
        if(Interactive&&PlayerOwner&&PlayerOwner->WasInputKeyJustPressed(EKeys::SpaceBar))bLock=true;
        if(bLock&&!bBlocked&&!bPending&&Controller){Controller->ServerDraftProfile(Selected->Id);S.LockRequestedAt=Now;S.LockRequestedId=Selected->Id;}
        if(!Hero->Notice.IsEmpty()&&Now-S.LockRequestedAt<6.0&&Now-S.LockRequestedAt>.5)
            Label(Hero->Notice,BX+BW-TextWidth(Hero->Notice,8.5f),BY-14,8.5f,DpsColor);
    }

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
                const float Step=ClipShare>.15f?-FMath::Clamp(ClipShare*3.f,.3f,.8f):.4f;
                ++F.Attempt;Stage->SetExposureOffset(Stage->GetExposureOffset()+Step);
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
        Panel(PVX,PVY+PVH*.5f-20,PVW,40,FLinearColor(0,0,0,.8f));
        Label(FString::Printf(TEXT("PORTRAIT %d / %d"),FMath::Min(F.Index+1,F.Ids.Num()),F.Ids.Num()),PVX+20,PVY+PVH*.5f-8,12,Gold);
    }
    // ---------- Fixture: 1080p screenshot gallery (Tools/RunDraftGallery.py) ----------
    if(S.Gallery.bActive&&!S.Gallery.bDone)
    {
        auto& G=S.Gallery;
        struct FShot{const TCHAR* Name;const TCHAR* Hover;const TCHAR* Select;};
        static const FShot Shots[]={
            {TEXT("01_tank_hover_knight"),TEXT("knight"),TEXT("knight")},
            {TEXT("02_dps_hover_ranger"),TEXT("ranger"),TEXT("knight")},
            {TEXT("03_support_hover_keeper"),TEXT("keeper_of_light"),TEXT("knight")},
            {TEXT("04_hybrid_select_wizard"),TEXT(""),TEXT("wizard")},
            {TEXT("05_tank_hover_behemoth"),TEXT("totemic_behemoth"),TEXT("wizard")},
            {TEXT("06_support_hover_whisp"),TEXT("whisp"),TEXT("wizard")}};
        if(G.Stage<0)
        {
            G.Started=Now;G.Stage=0;G.StageAt=Now;
            G.Directory=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("DraftGallery"),FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ"))));
            IFileManager::Get().MakeDirectory(*G.Directory,true);
        }
        // Standalone waits for the human to draft before bots fill, so the fixture adds four
        // inert teammates: a human who locked Thornweave Dryad, two bot picks and one still picking.
        if(Mates.IsEmpty()&&World&&World->GetNetMode()==NM_Standalone)
        {
            static const TCHAR* MateIds[]={TEXT("dryad"),TEXT("ranger"),TEXT("dwarf_miner"),TEXT("")};
            for(int32 I=0;I<4;++I)
            {
                FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;P.bDeferConstruction=true;
                const FTransform Where(FVector(120000.f+I*400.f,120000.f,90000.f));
                ACireHero* Mate=World->SpawnActor<ACireHero>(ACireHero::StaticClass(),Where,P);if(!Mate)continue;
                Mate->SetReplicates(false);Mate->TeamId=Hero->TeamId;Mate->bBot=I==1||I==2;Mate->FinishSpawning(Where);
                Mate->SetActorTickEnabled(false);Mate->SetActorHiddenInGame(true);Mate->SetActorEnableCollision(false);
                Mate->GetCharacterMovement()->DisableMovement();Mate->GetCharacterMovement()->SetComponentTickEnabled(false);
                if(*MateIds[I])Mate->DraftProfile(MateIds[I]);
            }
        }
        int32 ShotCount=static_cast<int32>(UE_ARRAY_COUNT(Shots));
        FParse::Value(FCommandLine::Get(),TEXT("CireDraftGalleryShots="),ShotCount);ShotCount=FMath::Clamp(ShotCount,1,static_cast<int32>(UE_ARRAY_COUNT(Shots)));
        if(G.Stage<ShotCount)
        {
            const FShot& Shot=Shots[G.Stage];
            S.ForcedHover=Shot.Hover;
            for(int32 I=0;I<S.Tiles.Num();++I)if(S.Tiles[I].Profile->Id==Shot.Select&&!S.Tiles[I].bSecondary){S.Cursor=I;break;}
            const FString Want=FString(Shot.Hover).IsEmpty()?FString(Shot.Select):FString(Shot.Hover);
            const bool bReady=Stage&&Stage->GetProfileId()==Want&&Stage->IsPreviewReady()&&Stage->SecondsShown()>2.6f&&Stage->FramesShown()>60&&Now-G.Started>6.0;
            if(G.ShotAt==0&&bReady)
            {
                const FString File=FPaths::Combine(G.Directory,FString(Shot.Name)+TEXT(".png"));
                FScreenshotRequest::RequestScreenshot(File,false,false,false,FIntRect(),true);G.Files.Add(File);G.ShotAt=Now;
                UE_LOG(LogCireDraft,Display,TEXT("CIRE_DRAFT_GALLERY_SHOT %s preview=%s"),*File,*Want);
            }
            if(G.ShotAt>0&&Now-G.ShotAt>1.2){++G.Stage;G.ShotAt=0;G.StageAt=Now;}
        }
        else if(Now-G.StageAt>1.0)
        {
            G.bDone=true;
            for(const FString& File:G.Files)G.bPass&=IFileManager::Get().FileSize(*File)>20000;
            G.bPass&=G.Files.Num()==ShotCount;
            UE_LOG(LogCireDraft,Display,TEXT("CIRE_DRAFT_GALLERY_%s captures=%d directory=%s"),G.bPass?TEXT("PASS"):TEXT("FAIL"),G.Files.Num(),*G.Directory);
            FPlatformMisc::RequestExitWithStatus(false,G.bPass?0:1);
        }
        if(!G.bDone&&Now-G.Started>180){G.bDone=true;UE_LOG(LogCireDraft,Error,TEXT("CIRE_DRAFT_GALLERY_FAIL timeout"));FPlatformMisc::RequestExitWithStatus(false,1);}
    }
#endif
    ResetTransform();
}
