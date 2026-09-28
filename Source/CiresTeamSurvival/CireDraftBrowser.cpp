#include "CireDraftBrowser.h"
#include "CireChampionRoster.h"
#include "CireChampionProfiles.h"
#include "CireParagonChampions.h"
#include "CireDraftAssets.h"
#include "CireDraftHoverProbe.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireDraftBrowser,Log,All);

namespace
{
FString& BrowserFavouritesOverride(){static FString Path;return Path;}
FString BrowserFavouritesFile()
{
    return BrowserFavouritesOverride().IsEmpty()?FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("CireDraftFavourites.json")):BrowserFavouritesOverride();
}
TSet<FString>& BrowserFavouriteSet(){static TSet<FString> Set;return Set;}
bool& BrowserFavouritesLoaded(){static bool b=false;return b;}
void BrowserLoadFavourites()
{
    BrowserFavouritesLoaded()=true;BrowserFavouriteSet().Reset();
    FString Json;TSharedPtr<FJsonObject> Root;const TArray<TSharedPtr<FJsonValue>>* Ids=nullptr;
    if(!FFileHelper::LoadFileToString(Json,*BrowserFavouritesFile())||Json.Len()>65536)return;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root||!Root->TryGetArrayField(TEXT("favourites"),Ids))return;
    for(const auto& V:*Ids){FString Id;if(V->TryGetString(Id)&&Id.Len()<=64&&!Id.IsEmpty())BrowserFavouriteSet().Add(Id);}
}
void BrowserSaveFavourites()
{
    TArray<FString> Ids=BrowserFavouriteSet().Array();Ids.Sort();
    FString Out=TEXT("{\n  \"favourites\": [");
    for(int32 I=0;I<Ids.Num();++I)Out+=FString::Printf(TEXT("%s\"%s\""),I?TEXT(", "):TEXT(""),*Ids[I].Replace(TEXT("\""),TEXT("")));
    Out+=TEXT("]\n}\n");
    FFileHelper::SaveStringToFile(Out,*BrowserFavouritesFile(),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
// Keyword aliases a player may type.
bool BrowserKeyword(const CireDraftBrowser::FEntry& E,const FString& W)
{
    const auto Is=[&](std::initializer_list<const TCHAR*> Words){for(const TCHAR* K:Words)if(FString(K).StartsWith(W,ESearchCase::IgnoreCase)&&W.Len()>=2)return true;return false;};
    if(Is({TEXT("tank")}))return (E.RoleMask&1)!=0;
    if(Is({TEXT("dps"),TEXT("damage")}))return (E.RoleMask&2)!=0;
    if(Is({TEXT("support"),TEXT("healer")}))return (E.RoleMask&4)!=0;
    if(W.Len()>=3&&(FString(TEXT("strength")).StartsWith(W,ESearchCase::IgnoreCase)))return E.PrimaryStat==TEXT("strength");
    if(W.Len()>=3&&(FString(TEXT("agility")).StartsWith(W,ESearchCase::IgnoreCase)))return E.PrimaryStat==TEXT("agility");
    if(W.Len()>=3&&(FString(TEXT("intelligence")).StartsWith(W,ESearchCase::IgnoreCase)))return E.PrimaryStat==TEXT("intelligence");
    if(W.Len()>=3&&FString(TEXT("paragon")).StartsWith(W,ESearchCase::IgnoreCase))return E.bParagon;
    if(W.Len()>=3&&(FString(TEXT("authored")).StartsWith(W,ESearchCase::IgnoreCase)||FString(TEXT("original")).StartsWith(W,ESearchCase::IgnoreCase)))return !E.bParagon;
    return false;
}
}

CireDraftBrowser::FEntry CireDraftBrowser::MakeEntry(const FCireChampionProfile& P,int32 Order)
{
    FEntry E;E.Id=P.Id;E.Name=P.DisplayName;E.ClassType=P.ClassType;E.Race=P.Race;E.PrimaryStat=P.PrimaryStat;
    E.RoleMask=CireChampionProfiles::ProfileRoleMask(P);
    const auto Primary=CireChampionProfiles::PrimaryRole(P);
    E.PrimaryRole=Primary==Cires::SkillDraftRole::Tank?0:Primary==Cires::SkillDraftRole::Support?2:1;
    E.RoleMask|=uint8(1<<E.PrimaryRole);
    E.Difficulty=FMath::Clamp(P.Difficulty,1,3);E.bParagon=CireParagonChampions::IsParagon(P.Id);E.Order=Order;
    return E;
}
const TArray<CireDraftBrowser::FEntry>& CireDraftBrowser::RosterEntries()
{
    static TArray<FEntry> Cache;static FString Key;
    const auto& All=CireChampionRoster::All();
    const FString Now=FString::Printf(TEXT("%d|%s|%s"),All.Num(),All.Num()?*All[0].Id:TEXT(""),All.Num()?*All.Last().Id:TEXT(""));
    if(Now!=Key){Key=Now;Cache.Reset();for(int32 I=0;I<All.Num();++I)Cache.Add(MakeEntry(All[I],I));}
    return Cache;
}
bool CireDraftBrowser::MatchesSearch(const FEntry& E,const FString& Search)
{
    TArray<FString> Words;Search.TrimStartAndEnd().ParseIntoArrayWS(Words);
    for(const FString& W:Words)
    {
        const bool bText=E.Name.Contains(W)||E.ClassType.Contains(W)||E.Race.Contains(W)||E.Id.Contains(W);
        if(!bText&&!BrowserKeyword(E,W))return false;
    }
    return true;
}
bool CireDraftBrowser::Matches(const FEntry& E,const FQuery& Q,const TSet<FString>& Favourites)
{
    if(Q.Role!=ERole::All&&!(E.RoleMask&(1<<(int32(Q.Role)-1))))return false;
    if(Q.Stat==EStat::Strength&&E.PrimaryStat!=TEXT("strength"))return false;
    if(Q.Stat==EStat::Agility&&E.PrimaryStat!=TEXT("agility"))return false;
    if(Q.Stat==EStat::Intelligence&&E.PrimaryStat!=TEXT("intelligence"))return false;
    if(Q.Source==ESource::Authored&&E.bParagon)return false;
    if(Q.Source==ESource::Paragon&&!E.bParagon)return false;
    if(Q.bFavouritesOnly&&!Favourites.Contains(E.Id))return false;
    return Q.Search.IsEmpty()||MatchesSearch(E,Q.Search);
}
TArray<int32> CireDraftBrowser::Filter(const TArray<FEntry>& Entries,const FQuery& Q,const TSet<FString>& Favourites)
{
    TArray<int32> Out;
    for(int32 I=0;I<Entries.Num();++I)if(Matches(Entries[I],Q,Favourites))Out.Add(I);
    // In a role tab, champions whose PRIMARY role it is come first, then the hybrids who can also fill it.
    const int32 TabRole=Q.Role==ERole::All?-1:int32(Q.Role)-1;
    Out.StableSort([&](int32 A,int32 B)
    {
        const FEntry& X=Entries[A];const FEntry& Y=Entries[B];
        if(Q.bPinFavourites){const bool FX=Favourites.Contains(X.Id),FY=Favourites.Contains(Y.Id);if(FX!=FY)return FX;}
        if(TabRole>=0){const bool PX=X.PrimaryRole==TabRole,PY=Y.PrimaryRole==TabRole;if(PX!=PY)return PX;}
        switch(Q.Sort)
        {
        case ESort::Name: {const int32 C=X.Name.Compare(Y.Name,ESearchCase::IgnoreCase);if(C!=0)return C<0;break;}
        case ESort::Difficulty: if(X.Difficulty!=Y.Difficulty)return X.Difficulty<Y.Difficulty;{const int32 C=X.Name.Compare(Y.Name,ESearchCase::IgnoreCase);if(C!=0)return C<0;}break;
        case ESort::Source: if(X.bParagon!=Y.bParagon)return !X.bParagon;break;
        default: if(X.PrimaryRole!=Y.PrimaryRole)return X.PrimaryRole<Y.PrimaryRole;if(X.bParagon!=Y.bParagon)return !X.bParagon;break;
        }
        return X.Order<Y.Order;
    });
    return Out;
}
const TCHAR* CireDraftBrowser::SortLabel(ESort S)
{return S==ESort::Name?TEXT("Name"):S==ESort::Difficulty?TEXT("Difficulty"):S==ESort::Source?TEXT("Source"):TEXT("Role");}
const TCHAR* CireDraftBrowser::StatLabel(EStat S)
{return S==EStat::Strength?TEXT("STR"):S==EStat::Agility?TEXT("AGI"):S==EStat::Intelligence?TEXT("INT"):TEXT("ANY");}
const TCHAR* CireDraftBrowser::SourceLabel(ESource S)
{return S==ESource::Authored?TEXT("Authored"):S==ESource::Paragon?TEXT("Paragon"):TEXT("All sources");}

CireDraftBrowser::FGridFit CireDraftBrowser::FitGrid(float W,float H,float Gap,float MinTileW,float MaxTileW,float Aspect)
{
    FGridFit F;Aspect=FMath::Max(.2f,Aspect);MinTileW=FMath::Max(8.f,MinTileW);MaxTileW=FMath::Max(MinTileW,MaxTileW);
    F.Cols=FMath::Max(1,FMath::FloorToInt((W+Gap)/(MinTileW+Gap)));
    F.TileW=FMath::Min(MaxTileW,(W-(F.Cols-1)*Gap)/F.Cols);
    // Rows that fit at this width; if none fit, shrink the tile to the height.
    F.TileH=F.TileW*Aspect;
    F.Rows=FMath::FloorToInt((H+Gap)/(F.TileH+Gap));
    if(F.Rows<1){F.Rows=1;F.TileH=FMath::Max(8.f,H);F.TileW=F.TileH/Aspect;}
    return F;
}
int32 CireDraftBrowser::PageCount(int32 Items,int32 PageSize){return FMath::Max(1,FMath::DivideAndRoundUp(FMath::Max(0,Items),FMath::Max(1,PageSize)));}
int32 CireDraftBrowser::PageOf(int32 Index,int32 PageSize){return Index<0?0:Index/FMath::Max(1,PageSize);}
int32 CireDraftBrowser::ClampPage(int32 Page,int32 Items,int32 PageSize){return FMath::Clamp(Page,0,PageCount(Items,PageSize)-1);}
int32 CireDraftBrowser::CycleSkin(int32 Current,int32 Step,int32 SkinCount)
{
    const int32 N=FMath::Max(0,SkinCount)+1;
    return ((FMath::Clamp(Current,0,N-1)+Step)%N+N)%N;
}

const TSet<FString>& CireDraftBrowser::Favourites(){if(!BrowserFavouritesLoaded())BrowserLoadFavourites();return BrowserFavouriteSet();}
bool CireDraftBrowser::IsFavourite(const FString& Id){return Favourites().Contains(Id);}
void CireDraftBrowser::ToggleFavourite(const FString& Id)
{
    if(Id.IsEmpty()||Id.Len()>64)return;
    Favourites();
    if(BrowserFavouriteSet().Contains(Id))BrowserFavouriteSet().Remove(Id);else BrowserFavouriteSet().Add(Id);
    BrowserSaveFavourites();
}
void CireDraftBrowser::SetFavouritesFileForTests(const FString& Path){BrowserFavouritesOverride()=Path;BrowserFavouritesLoaded()=false;}

bool CireDraftBrowser::FLru::Touch(const FString& Key,TArray<FString>* OutEvicted)
{
    const bool bHit=Keys.Remove(Key)>0;
    Keys.Add(Key);
    while(Keys.Num()>Capacity){if(OutEvicted)OutEvicted->Add(Keys[0]);Keys.RemoveAt(0);}
    return bHit;
}
void CireDraftBrowser::FLru::SetCapacity(int32 In,TArray<FString>* OutEvicted)
{
    Capacity=FMath::Max(1,In);
    while(Keys.Num()>Capacity){if(OutEvicted)OutEvicted->Add(Keys[0]);Keys.RemoveAt(0);}
}
bool CireDraftBrowser::FDebounce::Update(const FString& Hovered,double Now,double Delay)
{
    if(Hovered!=Pending){Pending=Hovered;Since=Now;}
    if(!Pending.IsEmpty()&&Pending!=Settled&&Now-Since>=Delay){Settled=Pending;return true;}
    return false;
}

#if !UE_BUILD_SHIPPING
bool CireDraftBrowser::RunTests(ACireGameMode* Mode)
{
    bool bPassed=true;int32 Checks=0;
    const auto Check=[&](bool bOk,const TCHAR* What){++Checks;if(!bOk){bPassed=false;UE_LOG(LogCireDraftBrowser,Error,TEXT("CIRE_DRAFT_BROWSER_CHECK_FAIL %s"),What);}};
    // Synthetic roster: 2 authored + 3 Paragon heroes.
    TArray<FEntry> E;
    const auto Add=[&](const TCHAR* Id,const TCHAR* Name,uint8 Mask,int32 Primary,const TCHAR* Stat,int32 Diff,bool bPg)
    {FEntry X;X.Id=Id;X.Name=Name;X.ClassType=TEXT("Class ")+FString(Name);X.Race=TEXT("human");X.RoleMask=Mask;X.PrimaryRole=Primary;X.PrimaryStat=Stat;X.Difficulty=Diff;X.bParagon=bPg;X.Order=E.Num();E.Add(X);};
    Add(TEXT("knight"),TEXT("Iron Warden"),1,0,TEXT("strength"),1,false);
    Add(TEXT("wizard"),TEXT("Cinder Arcanist"),2|4,1,TEXT("intelligence"),2,false);
    Add(TEXT("pg_greystone"),TEXT("Greystone"),1|2,0,TEXT("strength"),2,true);
    Add(TEXT("pg_sparrow"),TEXT("Sparrow"),2,1,TEXT("agility"),3,true);
    Add(TEXT("pg_muriel"),TEXT("Muriel"),4,2,TEXT("intelligence"),1,true);
    const TSet<FString> NoFav;
    FQuery Q;
    Check(Filter(E,Q,NoFav).Num()==5,TEXT("all heroes listed"));
    Q.Role=ERole::Tank;TArray<int32> R=Filter(E,Q,NoFav);
    Check(R.Num()==2&&E[R[0]].Id==TEXT("knight")&&E[R[1]].Id==TEXT("pg_greystone"),TEXT("tank tab: primary tanks (authored first)"));
    Q.Role=ERole::Support;R=Filter(E,Q,NoFav);
    Check(R.Num()==2&&E[R[0]].Id==TEXT("pg_muriel")&&E[R[1]].Id==TEXT("wizard"),TEXT("support tab: primary supports first, then hybrids"));
    Q=FQuery();Q.Stat=EStat::Intelligence;Check(Filter(E,Q,NoFav).Num()==2,TEXT("stat filter INT"));
    Q=FQuery();Q.Source=ESource::Paragon;Check(Filter(E,Q,NoFav).Num()==3,TEXT("source filter Paragon"));
    Q.Source=ESource::Authored;Check(Filter(E,Q,NoFav).Num()==2,TEXT("source filter authored"));
    Q=FQuery();Q.Search=TEXT("GREY");R=Filter(E,Q,NoFav);Check(R.Num()==1&&E[R[0]].Id==TEXT("pg_greystone"),TEXT("search is case-insensitive"));
    Q.Search=TEXT("paragon tank");R=Filter(E,Q,NoFav);Check(R.Num()==1&&E[R[0]].Id==TEXT("pg_greystone"),TEXT("search keywords AND"));
    Q.Search=TEXT("  agi ");R=Filter(E,Q,NoFav);Check(R.Num()==1&&E[R[0]].Id==TEXT("pg_sparrow"),TEXT("search stat keyword"));
    Q.Search=TEXT("zzzz");Check(Filter(E,Q,NoFav).Num()==0,TEXT("search no match"));
    Q=FQuery();Q.Sort=ESort::Name;R=Filter(E,Q,NoFav);
    Check(E[R[0]].Name==TEXT("Cinder Arcanist")&&E[R.Last()].Name==TEXT("Sparrow"),TEXT("sort by name"));
    Q.Sort=ESort::Difficulty;R=Filter(E,Q,NoFav);Check(E[R[0]].Difficulty==1&&E[R.Last()].Difficulty==3,TEXT("sort by difficulty"));
    Q.Sort=ESort::Source;R=Filter(E,Q,NoFav);Check(!E[R[0]].bParagon&&!E[R[1]].bParagon&&E[R[2]].bParagon,TEXT("sort by source"));
    TSet<FString> Fav;Fav.Add(TEXT("pg_muriel"));
    Q=FQuery();R=Filter(E,Q,Fav);Check(E[R[0]].Id==TEXT("pg_muriel"),TEXT("favourites pinned first"));
    Q.bFavouritesOnly=true;R=Filter(E,Q,Fav);Check(R.Num()==1,TEXT("favourites only"));
    // Grid and paging.
    const FGridFit G=FitGrid(547,444,8,84,110,1.12f);
    Check(G.Cols>=5&&G.Rows>=3&&G.Cols*G.TileW+(G.Cols-1)*8<=547.5f&&G.Rows*G.TileH+(G.Rows-1)*8<=444.5f,TEXT("grid fits its panel"));
    const FGridFit Tiny=FitGrid(100,50,8,84,110,1.12f);Check(Tiny.Rows==1&&Tiny.TileH<=50.5f,TEXT("grid shrinks when nothing fits"));
    Check(PageCount(89,24)==4&&PageCount(0,24)==1&&PageOf(30,24)==1&&PageOf(23,24)==0&&ClampPage(9,89,24)==3&&ClampPage(-2,89,24)==0,TEXT("paging math"));
    // Skins.
    Check(CycleSkin(0,1,3)==1&&CycleSkin(3,1,3)==0&&CycleSkin(0,-1,3)==3&&CycleSkin(0,1,0)==0,TEXT("skin cycling wraps over Default + reskins"));
    // LRU.
    {
        FLru L(3);TArray<FString> Ev;
        Check(!L.Touch(TEXT("a"),&Ev)&&!L.Touch(TEXT("b"),&Ev)&&!L.Touch(TEXT("c"),&Ev)&&Ev.IsEmpty(),TEXT("lru fills"));
        Check(L.Touch(TEXT("a"),&Ev),TEXT("lru hit"));
        L.Touch(TEXT("d"),&Ev);Check(Ev.Num()==1&&Ev[0]==TEXT("b")&&L.Contains(TEXT("a"))&&!L.Contains(TEXT("b")),TEXT("lru evicts the least recently used"));
        Ev.Reset();L.SetCapacity(1,&Ev);Check(Ev.Num()==2&&L.Num()==1&&L.Contains(TEXT("d")),TEXT("lru shrink evicts oldest"));
    }
    // Debounce: a quick scrub never settles; a dwell settles once.
    {
        FDebounce D;bool bAny=false;
        for(int32 I=0;I<6;++I)bAny|=D.Update(FString::Printf(TEXT("h%d"),I),I*.05,.15);
        Check(!bAny&&D.Settled.IsEmpty(),TEXT("debounce ignores a fast scrub"));
        Check(!D.Update(TEXT("h5"),.30,.15)&&D.Update(TEXT("h5"),.41,.15)&&D.Settled==TEXT("h5"),TEXT("debounce settles after the delay"));
        Check(!D.Update(TEXT("h5"),.9,.15),TEXT("debounce settles once"));
    }
    // Favourites persistence round trip (temp file).
    {
        const FString File=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Tests"),TEXT("DraftFavourites.test.json"));
        IFileManager::Get().Delete(*File,false,true,true);
        SetFavouritesFileForTests(File);
        Check(Favourites().IsEmpty(),TEXT("favourites start empty"));
        ToggleFavourite(TEXT("pg_greystone"));ToggleFavourite(TEXT("knight"));ToggleFavourite(TEXT("knight"));
        SetFavouritesFileForTests(File); // reload from disk
        Check(Favourites().Num()==1&&IsFavourite(TEXT("pg_greystone")),TEXT("favourites persist"));
        IFileManager::Get().Delete(*File,false,true,true);
        SetFavouritesFileForTests(FString());
    }
    // Probe math.
    Check(FMath::IsNearlyEqual(FCireDraftHoverProbe::Percentile({1,2,3,4,5,6,7,8,9,10},.95),10.0)&&FMath::IsNearlyEqual(FCireDraftHoverProbe::Percentile({4,1,3},.5),3.0),TEXT("probe percentile"));
    // The real roster: every hero once, skins never listed as heroes, Paragon heroes flagged.
    {
        const TArray<FEntry>& Live=RosterEntries();TSet<FString> Seen;int32 Pg=0;bool bUnique=true,bNoSkinRows=true;
        for(const FEntry& X:Live){bUnique&=!Seen.Contains(X.Id);Seen.Add(X.Id);Pg+=X.bParagon;bNoSkinRows&=!X.Id.Contains(TEXT("@"));}
        Check(Live.Num()==CireChampionRoster::Count(),TEXT("roster entries match the roster"));
        Check(bUnique&&bNoSkinRows,TEXT("each hero listed once; skins are not heroes"));
        Check(Pg==CireParagonChampions::HeroIds().Num(),TEXT("Paragon heroes flagged"));
        FQuery All;int32 Sum=0;for(const ERole Role:{ERole::Tank,ERole::Damage,ERole::Support}){FQuery Q2;Q2.Role=Role;Sum+=Filter(Live,Q2,NoFav).Num();}
        Check(Filter(Live,All,NoFav).Num()==Live.Num()&&Sum>=Live.Num(),TEXT("role tabs cover every hero"));
        UE_LOG(LogCireDraftBrowser,Display,TEXT("CIRE_DRAFT_BROWSER_ROSTER heroes=%d paragon=%d"),Live.Num(),Pg);
    }
    bPassed=CireDraftAssets::RunTests()&&bPassed;
    UE_LOG(LogCireDraftBrowser,Display,TEXT("CIRE_DRAFT_BROWSER_%s checks=%d"),bPassed?TEXT("PASS"):TEXT("FAIL"),Checks);
    return bPassed;
}
#endif
