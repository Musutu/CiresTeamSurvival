#include "CireUIStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/PackageName.h"
#include "CireUITheme.h" // ui-themes
#include "CireAudio.h" // audio: one hover tick for every menu button
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/Texture2D.h"
#include "EngineFontServices.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "GlobalRenderResources.h"
#include "UObject/Package.h"

namespace
{
using namespace CireUIColors;
const TCHAR* FacePaths[]={TEXT("/Game/UI/WowUI/Fonts/UIBody.UIBody"),TEXT("/Game/UI/WowUI/Fonts/UIHeading.UIHeading"),
    TEXT("/Game/UI/WowUI/Fonts/UIBold.UIBold"),TEXT("/Game/UI/WowUI/Fonts/UINumbers.UINumbers")};
const TCHAR* DisplayFacePath=TEXT("/Game/UI/WowUI/Fonts/UIDisplay.UIDisplay"); // progression-shop: Cinzel (OFL)
FString TexturePath(const TCHAR* Name){return FString::Printf(TEXT("/Game/UI/WowUI/Textures/%s.%s"),Name,Name);}
UTexture2D* LoadTexture(const TCHAR* Name)
{
    UTexture2D* T=LoadObject<UTexture2D>(nullptr,*TexturePath(Name),nullptr,LOAD_NoWarn|LOAD_Quiet);
    if(T)T->AddToRoot();
    return T;
}
CireUIStyle::FAssets GAssets;
bool GTexturesTried=false,GFontsTried=false;
}

// ---------------------------------------------------------------------------
// Assets
// ---------------------------------------------------------------------------
const TArray<FString>& CireUIStyle::AssetPaths()
{
    static TArray<FString> Paths;
    if(Paths.IsEmpty())
    {
        for(const TCHAR* P:FacePaths)Paths.Add(P);
        Paths.Add(DisplayFacePath);
        for(const TCHAR* N:{TEXT("T_Panel"),TEXT("T_Border"),TEXT("T_Button"),TEXT("T_ButtonUlt"),TEXT("T_ButtonPassive"),TEXT("T_Glow"),
            TEXT("T_Gloss"),TEXT("T_IconBg"),TEXT("T_Gem"),TEXT("T_Header")})Paths.Add(TexturePath(N));
    }
    return Paths;
}
const CireUIStyle::FAssets& CireUIStyle::Assets()
{
    if(!GTexturesTried)
    {
        GTexturesTried=true;
        GAssets.Panel=LoadTexture(TEXT("T_Panel"));GAssets.Border=LoadTexture(TEXT("T_Border"));GAssets.Button=LoadTexture(TEXT("T_Button"));
        GAssets.ButtonUlt=LoadTexture(TEXT("T_ButtonUlt"));GAssets.ButtonPassive=LoadTexture(TEXT("T_ButtonPassive"));GAssets.Glow=LoadTexture(TEXT("T_Glow"));
        GAssets.Gloss=LoadTexture(TEXT("T_Gloss"));GAssets.IconBg=LoadTexture(TEXT("T_IconBg"));GAssets.Gem=LoadTexture(TEXT("T_Gem"));GAssets.Header=LoadTexture(TEXT("T_Header"));
        GAssets.bTextures=GAssets.Panel&&GAssets.Border&&GAssets.Button&&GAssets.Glow&&GAssets.Gloss;
    }
    // Fonts need the engine font services (measurement), which exist once the game renders.
    if(!GFontsTried&&FEngineFontServices::IsInitialized()&&GEngine&&GEngine->GetMediumFont())
    {
        GFontsTried=true;
        UFontFace* Faces[4]={};
        for(int32 I=0;I<4;++I)Faces[I]=LoadObject<UFontFace>(nullptr,FacePaths[I],nullptr,LOAD_NoWarn|LOAD_Quiet);
        auto Measure=FEngineFontServices::Get().GetFontMeasure();
        if(Faces[0]&&Faces[1]&&Faces[2]&&Faces[3]&&Measure.IsValid())
        {
            // Calibrate so a logical Size keeps the line height the layout was designed
            // with (engine Roboto at Size/16); Alegreya's small x-height gets a boost.
            const float Reference=Measure->GetMaxCharacterHeight(GEngine->GetMediumFont()->GetLegacySlateFontInfo(),1.f)/16.f;
            const float Boost[]={1.14f,1.0f,1.12f,1.12f};
            for(int32 I=0;I<4;++I)
            {
                Faces[I]->AddToRoot();
                UFont* Font=NewObject<UFont>(GetTransientPackage(),NAME_None,RF_Transient);
                Font->AddToRoot();
                Font->FontCacheType=EFontCacheType::Runtime;
                FTypefaceEntry Entry(TEXT("Regular"));Entry.Font=FFontData(Faces[I]);
                Font->CompositeFont.DefaultTypeface.Fonts.Add(Entry);
                Font->LegacyFontSize=16;
                GAssets.Fonts[I]=Font;
                const float Height=Measure->GetMaxCharacterHeight(FSlateFontInfo(Font,100.f),1.f)/100.f;
                GAssets.Calibration[I]=Height>0?Reference/Height*Boost[I]:1.f;
            }
            // progression-shop: optional display face; missing -> ResolveFont falls back to Heading.
            if(UFontFace* Display=LoadObject<UFontFace>(nullptr,DisplayFacePath,nullptr,LOAD_NoWarn|LOAD_Quiet))
            {
                Display->AddToRoot();
                UFont* Font=NewObject<UFont>(GetTransientPackage(),NAME_None,RF_Transient);
                Font->AddToRoot();
                Font->FontCacheType=EFontCacheType::Runtime;
                FTypefaceEntry Entry(TEXT("Regular"));Entry.Font=FFontData(Display);
                Font->CompositeFont.DefaultTypeface.Fonts.Add(Entry);
                Font->LegacyFontSize=16;
                GAssets.Fonts[4]=Font;
                const float Height=Measure->GetMaxCharacterHeight(FSlateFontInfo(Font,100.f),1.f)/100.f;
                GAssets.Calibration[4]=Height>0?Reference/Height*1.05f:1.f;
            }
            GAssets.bFonts=true;
        }
    }
    return GAssets;
}
UFont* CireUIStyle::ResolveFont(ECireFont Font,const FString& Text,float Size,int32* OutIndex)
{
    const FAssets& A=Assets();
    if(!A.bFonts){if(OutIndex)*OutIndex=INDEX_NONE;return GEngine?GEngine->GetMediumFont():nullptr;}
    if(Font==ECireFont::Auto)
    {
        bool bLower=false,bLetter=false;
        for(const TCHAR C:Text){bLower|=FChar::IsLower(C)!=0;bLetter|=FChar::IsAlpha(C)!=0;}
        Font=!bLetter?ECireFont::Numbers:!bLower?ECireFont::Heading:Size>=14.f?ECireFont::Bold:ECireFont::Body;
    }
    const int32 Index=Font==ECireFont::Display?(A.Fonts[4]?4:1):FMath::Clamp(static_cast<int32>(Font)-1,0,3);
    if(OutIndex)*OutIndex=Index;
    return A.Fonts[Index];
}

// ---------------------------------------------------------------------------
// Painter primitives
// ---------------------------------------------------------------------------
void FCireUIPainter::Rect(float X,float Y,float W,float H,FLinearColor Color) const
{
    if(!Canvas||W<=0||H<=0)return;
    const FVector2D A=ToScreen(X,Y);
    FCanvasTileItem Item(A,FVector2D(W*Stretch.X*Scale,H*Stretch.Y*Scale),Fade(Color));Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);
}
void FCireUIPainter::Line(float X1,float Y1,float X2,float Y2,FLinearColor Color,float Width) const
{
    if(!Canvas)return;
    FCanvasLineItem Item(ToScreen(X1,Y1),ToScreen(X2,Y2));Item.SetColor(Fade(Color));Item.LineThickness=Width*K();Canvas->DrawItem(Item);
}
void FCireUIPainter::Disc(float X,float Y,float R,FLinearColor Color,int32 Sides) const
{
    if(!Canvas||R<=0)return;
    FCanvasNGonItem Item(ToScreen(X,Y),FVector2D(R*K(),R*K()),FMath::Max(3,Sides),GWhiteTexture,Fade(Color));Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);
}
void FCireUIPainter::Circle(float X,float Y,float R,FLinearColor Color,float Width,int32 Sides) const
{
    for(int32 I=0;I<Sides;++I){const float A=I*2*PI/Sides,B=(I+1)*2*PI/Sides;Line(X+FMath::Cos(A)*R,Y+FMath::Sin(A)*R,X+FMath::Cos(B)*R,Y+FMath::Sin(B)*R,Color,Width);}
}
void FCireUIPainter::Tri(FVector2D A,FVector2D B,FVector2D C,FLinearColor Color) const
{
    if(!Canvas)return;
    FCanvasTriangleItem Item(ToScreen(A.X,A.Y),ToScreen(B.X,B.Y),ToScreen(C.X,C.Y),GWhiteTexture);Item.SetColor(Fade(Color));Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);
}
void FCireUIPainter::TexDisc(UTexture2D* Texture,float CX,float CY,float R,FLinearColor Color,float U0,float V0,float U1,float V1,int32 Sides) const
{
    if(!Canvas||!Texture||!Texture->GetResource()||R<=0)return;
    const FLinearColor C=Fade(Color);
    const FVector2D Centre=ToScreen(CX,CY);const FVector2D UC((U0+U1)*.5f,(V0+V1)*.5f),UR((U1-U0)*.5f,(V1-V0)*.5f);
    TArray<FCanvasUVTri> Tris;Tris.Reserve(Sides);
    for(int32 I=0;I<Sides;++I)
    {
        const float A=I*2*PI/Sides,B=(I+1)*2*PI/Sides;
        FCanvasUVTri T;
        T.V0_Pos=Centre;T.V0_UV=UC;T.V0_Color=C;
        T.V1_Pos=ToScreen(CX+FMath::Cos(A)*R,CY+FMath::Sin(A)*R);T.V1_UV=FVector2D(UC.X+FMath::Cos(A)*UR.X,UC.Y+FMath::Sin(A)*UR.Y);T.V1_Color=C;
        T.V2_Pos=ToScreen(CX+FMath::Cos(B)*R,CY+FMath::Sin(B)*R);T.V2_UV=FVector2D(UC.X+FMath::Cos(B)*UR.X,UC.Y+FMath::Sin(B)*UR.Y);T.V2_Color=C;
        Tris.Add(T);
    }
    FCanvasTriangleItem Item(Tris,Texture->GetResource());Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);
}
void FCireUIPainter::Tex(UTexture2D* Texture,float X,float Y,float W,float H,FLinearColor Color,float U0,float V0,float U1,float V1,bool bAdditive) const
{
    if(!Canvas||!Texture||!Texture->GetResource()||W<=0||H<=0)return;
    FCanvasTileItem Item(ToScreen(X,Y),Texture->GetResource(),FVector2D(W*Stretch.X*Scale,H*Stretch.Y*Scale),FVector2D(U0,V0),FVector2D(U1,V1),Fade(Color));
    Item.BlendMode=bAdditive?SE_BLEND_Additive:SE_BLEND_Translucent;Canvas->DrawItem(Item);
}
void FCireUIPainter::NineSlice(UTexture2D* Texture,float X,float Y,float W,float H,float Corner,FLinearColor Color,float S) const
{
    if(!Texture)return;
    const float C=FMath::Min(Corner,FMath::Min(W,H)*.5f);
    const float Xs[]={X,X+C,X+W-C,X+W},Ys[]={Y,Y+C,Y+H-C,Y+H},Us[]={0,S,1-S,1};
    for(int32 R=0;R<3;++R)for(int32 Q=0;Q<3;++Q)
    {
        if(R==1&&Q==1)continue; // hollow centre
        Tex(Texture,Xs[Q],Ys[R],Xs[Q+1]-Xs[Q],Ys[R+1]-Ys[R],Color,Us[Q],Us[R],Us[Q+1],Us[R+1]);
    }
}
void FCireUIPainter::Text(const FString& Text,float X,float Y,float Size,FLinearColor Color,ECireFont Font,bool bOutline,bool bShadow) const
{
    Color=Fade(Color);
    if(Text.IsEmpty()||!Canvas||Color.A<=.004f)return;
    Size=CireUIStyle::ReadableSize(Size); // readability: global boost + floor
    const FVector2D At=ToScreen(X,Y);const FVector2D Pixel(FMath::RoundToFloat(At.X),FMath::RoundToFloat(At.Y));
    int32 Index=INDEX_NONE;UFont* Resolved=CireUIStyle::ResolveFont(Font,Text,Size,&Index);
    if(!Resolved)return;
    if(Index==INDEX_NONE)
    {
        FCanvasTextItem Item(Pixel,FText::FromString(Text),Resolved,Color);Item.Scale=FVector2D(Size/16.f*K());
        if(bShadow)Item.EnableShadow(FLinearColor(0,0,0,1));
        if(bOutline){Item.bOutlined=true;Item.OutlineColor=FLinearColor(0,0,0,Color.A);}
        Canvas->DrawItem(Item);return;
    }
    // Rasterize at the final pixel size (whole points: stable hinting, small glyph cache).
    const float Points=FMath::Max(4.f,FMath::RoundToFloat(Size*CireUIStyle::Assets().Calibration[Index]*K()));
    FCanvasTextItem Item(Pixel,FText::FromString(Text),FSlateFontInfo(Resolved,Points),Color);
    if(bShadow)Item.EnableShadow(FLinearColor(0,0,0,1),FVector2D(Points>=20?2.f:1.f,Points>=20?2.f:1.f));
    if(bOutline){Item.bOutlined=true;Item.OutlineColor=FLinearColor(0,0,0,Color.A*.92f);}
    Canvas->DrawItem(Item);
}
float FCireUIPainter::TextWidth(const FString& Text,float Size,ECireFont Font) const
{
    int32 Index=INDEX_NONE;UFont* Resolved=CireUIStyle::ResolveFont(Font,Text,Size,&Index);
    if(!Resolved||Text.IsEmpty())return 0.f;
    Size=CireUIStyle::ReadableSize(Size); // readability: measured exactly as drawn
    const float KS=FMath::Max(.05f,Scale);
    if(Index==INDEX_NONE)
    {
        int32 W=0,H=0;Resolved->GetStringHeightAndWidth(Text,H,W);return W*Size/16.f;
    }
    auto Measure=FEngineFontServices::Get().GetFontMeasure();
    if(!Measure.IsValid())return Text.Len()*Size*.5f;
    const float Points=FMath::Max(4.f,FMath::RoundToFloat(Size*CireUIStyle::Assets().Calibration[Index]*KS));
    return static_cast<float>(Measure->Measure(Text,FSlateFontInfo(Resolved,Points),1.f).X)/KS;
}
FString FCireUIPainter::Fit(const FString& In,float Size,float MaxWidth,ECireFont Font) const
{
    if(TextWidth(In,Size,Font)<=MaxWidth)return In;
    FString S=In;
    while(S.Len()>1&&TextWidth(S+TEXT(".."),Size,Font)>MaxWidth)S.LeftChopInline(1);
    return S.TrimEnd()+TEXT("..");
}
int32 FCireUIPainter::Wrapped(const FString& Body,float X,float Y,float Width,float Size,FLinearColor Color,int32 MaxLines,ECireFont Font,float LineGap) const
{
    TArray<FString> Words;Body.ParseIntoArrayWS(Words);FString Row;int32 Count=0;
    const float Step=CireUIStyle::ReadableSize(Size)+LineGap; // readability: lines step by the drawn size
    for(const FString& Word:Words)
    {
        const FString Next=Row.IsEmpty()?Word:Row+TEXT(" ")+Word;
        if(!Row.IsEmpty()&&TextWidth(Next,Size,Font)>Width)
        {
            Text(Row,X,Y+Count*Step,Size,Color,Font);if(++Count>=MaxLines)return Count;Row=Word;
        }
        else Row=Next;
    }
    if(!Row.IsEmpty()&&Count<MaxLines){Text(Fit(Row,Size,Width,Font),X,Y+Count*Step,Size,Color,Font);++Count;}
    return Count;
}

// ---------------------------------------------------------------------------
// Components
// ---------------------------------------------------------------------------
bool CireUIStyle::HasThemeArt()
{
    const FCireUITheme* T=CireUITheme::Active();
    return T&&CireUITheme::LoadArt(*T);
}
namespace
{
using CireUITheme::Draw;
// True when Accent is the theme trim (no special state to show on the frame).
bool IsTrim(const FLinearColor& Accent)
{
    return FMath::Abs(Accent.R-Gold.R)+FMath::Abs(Accent.G-Gold.G)+FMath::Abs(Accent.B-Gold.B)<.08f;
}
// Corner scale so ornate corners stay proportionate on small frames.
float CornerScaleFor(float W,float H,float Reference=120.f){return FMath::Clamp(FMath::Min(W,H)/Reference,.5f,1.f);}
void ThemedFrame(const FCireUIPainter& P,const FCireUITheme& T,float X,float Y,float W,float H,FLinearColor Accent,ECireFrame Kind)
{
    const bool bTrim=IsTrim(Accent);
    switch(Kind)
    {
    case ECireFrame::Tooltip:
        CireUIStyle::TooltipFrame(P,X,Y,W,H,Accent,.94f);return;
    case ECireFrame::Inset:
        P.Rect(X,Y,W,H,FLinearColor(0,0,0,.6f));
        CireUITheme::DrawFill(P,X,Y,W,H,PanelTint*FLinearColor(.5f,.5f,.55f,.85f));
        P.Line(X,Y,X+W,Y,FLinearColor(0,0,0,.9f),1.2f);P.Line(X,Y,X,Y+H,FLinearColor(0,0,0,.9f),1.2f);
        P.Line(X,Y+H,X+W,Y+H,Gold*FLinearColor(1,1,1,.18f));P.Line(X+W,Y,X+W,Y+H,Gold*FLinearColor(1,1,1,.18f));
        return;
    case ECireFrame::Card:
        P.Rect(X+1,Y+2,W,H,FLinearColor(0,0,0,.28f));
        CireUITheme::DrawFill(P,X,Y,W,H,PanelTint*FLinearColor(1.1f,1.1f,1.15f,T.PanelOpacity));
        Draw(P,ECireThemePiece::Card,X,Y,W,H,bTrim?FLinearColor::White:FLinearColor(1,1,1,.9f),CornerScaleFor(W,H,60.f));
        if(!bTrim)P.Line(X+6,Y+2.f,X+W-6,Y+2.f,Accent*FLinearColor(1,1,1,.6f),1.f);
        return;
    default: break;
    }
    // Panel / Unit: drop shadow, themed fill with a soft top sheen and bottom shade, the ornate
    // nine-slice frame, an accent line for state colours (aggro, role), and the crest ornament.
    P.Rect(X+3,Y+5,W,H,FLinearColor(0,0,0,.38f));
    CireUITheme::DrawFill(P,X+2,Y+2,W-4,H-4,PanelTint*FLinearColor(1,1,1,T.PanelOpacity));
    // Dark gradient: the theme's hover tone lifts the top of the panel (navy / ember-warm / violet).
    for(int32 I=0;I<6;++I){const float B=(H-4)/6.f;P.Rect(X+2,Y+2+I*B,W-4,B,FLinearColor(Hover.R,Hover.G,Hover.B,.34f-I*.06f));}
    P.Rect(X+2,Y+H*.62f,W-4,H*.38f-2,FLinearColor(0,0,0,.16f));
    if(const CireUIStyle::FAssets& A=CireUIStyle::Assets();A.Gloss)P.Tex(A.Gloss,X+3,Y+3,W-6,FMath::Min(H*.4f,50.f),ThemeGlow*FLinearColor(1,1,1,.035f));
    // Inner shadow: the panel reads recessed behind its dimensional frame (concept look).
    for(int32 I=0;I<3;++I)
    {
        const float D=2.f+I*2.5f,Aa=.16f-I*.045f;
        P.Rect(X+D,Y+D,W-2*D,2.5f,FLinearColor(0,0,0,Aa));P.Rect(X+D,Y+H-D-2.5f,W-2*D,2.5f,FLinearColor(0,0,0,Aa));
        P.Rect(X+D,Y+D+2.5f,2.5f,H-2*D-5,FLinearColor(0,0,0,Aa));P.Rect(X+W-D-2.5f,Y+D+2.5f,2.5f,H-2*D-5,FLinearColor(0,0,0,Aa));
    }
    const float CS=CornerScaleFor(W,H);
    const float Out=6.f*CS; // the frame's ornate border sits mostly outside the content rectangle
    Draw(P,ECireThemePiece::Panel,X-Out,Y-Out,W+2*Out,H+2*Out,FLinearColor(1.12f,1.12f,1.12f,1),CS);
    if(!bTrim)
    {
        const float In=T.Piece(ECireThemePiece::Panel).Corner*CS*.7f;
        P.Line(X+In,Y+2.f,X+W-In,Y+2.f,Accent*FLinearColor(1,1,1,.85f),1.6f);
        CireUIStyle::Glow(P,X,Y-1,W,3,Accent*FLinearColor(1,1,1,.35f));
    }
    if(T.bGem&&W>120&&Kind==ECireFrame::Panel)CireUIStyle::Ornament(P,X+W*.5f,Y-Out*.5f,FMath::Clamp(H*.16f,9.f,15.f),bTrim?FLinearColor::White:Accent*1.2f+FLinearColor(.2f,.2f,.2f,0));
}
}

void CireUIStyle::Frame(const FCireUIPainter& P,float X,float Y,float W,float H,FLinearColor Accent,ECireFrame Kind)
{
    if(const FCireUITheme* T=CireUITheme::Active();T&&CireUITheme::LoadArt(*T)){ThemedFrame(P,*T,X,Y,W,H,Accent,Kind);return;}
    const FAssets& A=Assets();
    if(!A.bTextures)
    {
        P.Rect(X+3,Y+4,W,H,FLinearColor(0,0,0,.30f));P.Rect(X,Y,W,H,Ink);P.Line(X,Y,X+W,Y,Accent,.85f);return;
    }
    const float KS=FMath::Max(.1f,P.K());
    switch(Kind)
    {
    case ECireFrame::Tooltip:
        TooltipFrame(P,X,Y,W,H,Accent,.94f);return;
    case ECireFrame::Inset:
        P.Rect(X,Y,W,H,FLinearColor(0,0,0,.55f));
        P.Tex(A.Panel,X,Y,W,H,FLinearColor(.55f,.55f,.6f,.8f),0,0,W*KS/256.f,H*KS/256.f);
        P.Line(X,Y,X+W,Y,FLinearColor(0,0,0,.9f),1.2f);P.Line(X,Y,X,Y+H,FLinearColor(0,0,0,.9f),1.2f);
        P.Line(X,Y+H,X+W,Y+H,FLinearColor(1,1,1,.07f));P.Line(X+W,Y,X+W,Y+H,FLinearColor(1,1,1,.07f));
        return;
    case ECireFrame::Card:
        P.Tex(A.Panel,X,Y,W,H,FLinearColor(1.25f,1.25f,1.3f,.96f),0,0,W*KS/256.f,H*KS/256.f);
        P.Tex(A.Gloss,X,Y,W,H,FLinearColor(1,1,1,.05f));
        P.NineSlice(A.Border,X,Y,W,H,FMath::Min(10.f,H*.35f),FLinearColor(.75f,.72f,.66f,.8f));
        P.Line(X+6,Y+1.5f,X+W-6,Y+1.5f,Accent*FLinearColor(1,1,1,.55f),1.f);
        return;
    default: break;
    }
    // Panel / Unit: drop shadow, textured slate, vertical sheen, iron+gold 9-slice
    // border, accent trim with a centre gem.
    P.Rect(X+3,Y+5,W,H,FLinearColor(0,0,0,.38f));
    P.Tex(A.Panel,X,Y,W,H,FLinearColor(1,1,1,.97f),0,0,W*KS/256.f,H*KS/256.f);
    P.Tex(A.Gloss,X+2,Y+2,W-4,FMath::Min(H*.45f,60.f),FLinearColor(.55f,.62f,.75f,.07f));
    P.Rect(X+2,Y+H*.6f,W-4,H*.4f-2,FLinearColor(0,0,0,.18f));
    const float Corner=FMath::Clamp(FMath::Min(W,H)*.22f,10.f,20.f);
    P.NineSlice(A.Border,X-2,Y-2,W+4,H+4,Corner,FLinearColor::White);
    P.Line(X+Corner,Y+.5f,X+W-Corner,Y+.5f,Accent*FLinearColor(1,1,1,.9f),1.4f);
    if(A.Gem&&W>90&&Kind==ECireFrame::Panel)
    {
        const float G=6.5f;P.Tex(A.Gem,X+W*.5f-G,Y-G+1,2*G,2*G,Accent*1.15f+FLinearColor(.05f,.05f,.05f,0));
    }
}
void CireUIStyle::Slider(const FCireUIPainter& P,float X,float Y,float W,float Fraction,bool bEnabled,bool bHover)
{
    const FAssets& A=Assets();Fraction=FMath::Clamp(Fraction,0.f,1.f);
    Frame(P,X,Y,W,8,Gold,ECireFrame::Inset);
    const FLinearColor Fill=bEnabled?FLinearColor(1.1f,.85f,.4f,1):Muted;
    if(A.Gloss)P.Tex(A.Gloss,X+1,Y+1,(W-2)*Fraction,6,Fill);else P.Rect(X+1,Y+1,(W-2)*Fraction,6,Fill);
    if(bHover&&bEnabled)Glow(P,X+(W-2)*Fraction-8,Y-4,16,16,FLinearColor(1.f,.85f,.5f,.4f));
    if(A.Gem)P.Tex(A.Gem,X+(W-2)*Fraction-8,Y-4,16,16,bEnabled?FLinearColor(1.f,.85f,.45f,1):Muted);
    else P.Rect(X+(W-4)*Fraction,Y-4,5,16,bEnabled?Parchment:Muted);
}
void CireUIStyle::Chevron(const FCireUIPainter& P,float X,float Y,float S,bool bCollapsed,FLinearColor Color)
{
    if(bCollapsed)P.Tri(FVector2D(X+S*.3f,Y+S*.15f),FVector2D(X+S*.3f,Y+S*.85f),FVector2D(X+S*.85f,Y+S*.5f),Color);
    else P.Tri(FVector2D(X+S*.15f,Y+S*.3f),FVector2D(X+S*.85f,Y+S*.3f),FVector2D(X+S*.5f,Y+S*.85f),Color);
}
void CireUIStyle::Header(const FCireUIPainter& P,float X,float Y,float W,const FString& Caption,FLinearColor Color,float Size)
{
    const FAssets& A=Assets();
    if(HasThemeArt())Divider(P,X,Y+Size+3,W,FLinearColor(1,1,1,.9f));
    else if(A.Header)P.Tex(A.Header,X,Y,W,Size+10,FLinearColor::White);
    else P.Line(X,Y+Size+8,X+W,Y+Size+8,Color*.7f,1.2f);
    P.Text(Caption,X+10,Y+2,Size,Color,ECireFont::Heading,false,true);
}
void CireUIStyle::Glow(const FCireUIPainter& P,float X,float Y,float W,float H,FLinearColor Color)
{
    // Procedural halo: stacked translucent fills growing outward. No texture blend
    // mode involved, so it can never render as an opaque square (Color.A = strength).
    const float Spread=FMath::Clamp(FMath::Min(W,H)*.18f,2.f,10.f);
    for(int32 Ring=4;Ring>=1;--Ring)
    {
        const float O=Ring*Spread*.5f;
        P.Rect(X-O,Y-O,W+2*O,H+2*O,FLinearColor(Color.R,Color.G,Color.B,Color.A*.14f));
    }
}
void CireUIStyle::Button(const FCireUIPainter& P,float X,float Y,float W,float H,const FString& Label,ECireButtonState State,FLinearColor Accent,float TextSize)
{
    const FAssets& A=Assets();
    const bool bHover=State==ECireButtonState::Hover,bPress=State==ECireButtonState::Pressed,bOff=State==ECireButtonState::Disabled,bSel=State==ECireButtonState::Selected;
    if(bHover)CireAudio::NoteUIHover(X,Y); // audio: consistent hover tick across menus
    const float Dy=bPress?1.f:0.f;
    // readability: a dark, readable face fills the whole button; the theme's painted frame is only a thin
    // rim around it (it used to be 9-sliced at a corner size of half the button height, which squeezed the
    // art into a bright gold rail running straight through the label). The label sits centred in the face.
    const float Rim=FMath::Clamp(H*.2f,3.5f,7.f);
    const float GlowK=HasThemeArt()?CireUITheme::Active()->GlowStrength:1.f;
    P.Rect(X+1.5f,Y+2.5f,W,H,FLinearColor(0,0,0,.45f));
    {
        const float FX=X+1,FY=Y+1+Dy,FW=W-2,FH=H-2;
        const FLinearColor Top=bOff?FLinearColor(.035f,.035f,.04f,1):Hover*(bHover?1.7f:bSel?1.4f:bPress?.7f:1.1f);
        const FLinearColor Bottom=bOff?FLinearColor(.012f,.012f,.014f,1):Ink*(bPress?.7f:1.f);
        constexpr int32 Bands=6;
        for(int32 I=0;I<Bands;++I){FLinearColor C=FMath::Lerp(Top,Bottom,I/float(Bands-1));C.A=.97f;P.Rect(FX,FY+FH*I/Bands,FW,FH/Bands+.5f,C);}
        if(bSel)P.Rect(FX,FY,FW,FH,Accent*FLinearColor(1,1,1,.17f));
        if(bHover)P.Rect(FX,FY,FW,FH,ThemeGlow*FLinearColor(1,1,1,.08f));
        if(A.Gloss&&!bOff)P.Tex(A.Gloss,FX+Rim,FY+Rim*.6f,FW-2*Rim,FH*.42f,FLinearColor(1,1,1,bPress?.02f:.07f));
    }
    bool bFramed=false;
    if(HasThemeArt())
    {
        // hud-art: the painted state piece (hover / pressed / disabled), drawn with a corner cell of ~1.2 rims.
        const FCireUITheme& T=*CireUITheme::Active();
        ECireThemePiece Piece=bOff?ECireThemePiece::ButtonDisabled:bPress?ECireThemePiece::ButtonPressed:bHover||bSel?ECireThemePiece::ButtonHover:ECireThemePiece::Button;
        if(!T.Piece(Piece).bValid)Piece=T.Piece(ECireThemePiece::Button).bValid?ECireThemePiece::Button:ECireThemePiece::Card;
        const float Corner=FMath::Max(1.f,T.Piece(Piece).Corner);
        bFramed=CireUITheme::Draw(P,Piece,X,Y+Dy,W,H,bOff?FLinearColor(.55f,.55f,.55f,.9f):FLinearColor::White,Rim*1.2f/Corner);
    }
    if(!bFramed&&A.bTextures)bFramed=(P.NineSlice(A.Border,X,Y+Dy,W,H,FMath::Min(7.f,H*.3f),bOff?FLinearColor(.5f,.5f,.5f,.8f):FLinearColor::White),true);
    if(!bFramed){const FLinearColor B=bOff?Muted*.6f:Gold;P.Line(X,Y+Dy,X+W,Y+Dy,B);P.Line(X,Y+H+Dy,X+W,Y+H+Dy,B);P.Line(X,Y+Dy,X,Y+H+Dy,B);P.Line(X+W,Y+Dy,X+W,Y+H+Dy,B);}
    // State: selection gets an accent bar along the bottom of the face and a glow; hover a soft glow.
    if(bSel){P.Rect(X+Rim+3,Y+H-Rim-2.2f+Dy,W-2*Rim-6,1.8f,Accent*1.25f+FLinearColor(.1f,.1f,.1f,0));Glow(P,X+W*.08f,Y+Dy,W*.84f,H,Accent*FLinearColor(1,1,1,.22f*GlowK));}
    else if(bHover)Glow(P,X+W*.08f,Y+Dy,W*.84f,H,ThemeGlow*FLinearColor(1,1,1,.14f*GlowK));
    if(Label.IsEmpty())return;
    // Label: mixed case in the bold face, caps in the engraved heading face; as large as the face allows
    // (at least 10 design units = ~17 px at 1080p), outlined + shadowed, centred in the face.
    bool bLower=false,bLetter=false;for(const TCHAR C:Label){bLower|=FChar::IsLower(C)!=0;bLetter|=FChar::IsAlpha(C)!=0;}
    const ECireFont Font=!bLetter?ECireFont::Numbers:bLower?ECireFont::Bold:ECireFont::Heading;
    float TS=FMath::Max(TextSize,10.f);
    const float MaxEff=H*.64f;
    while(TS>6.f&&ReadableSize(TS)>MaxEff&&TS*TextBoost>MinTextSize)TS-=.25f;
    while(TS>6.f&&TS*TextBoost>MinTextSize&&P.TextWidth(Label,TS,Font)>W-2*Rim-6)TS-=.25f; // narrow buttons shrink to the floor, then truncate
    const FString Text=P.Fit(Label,TS,W-2*Rim-6,Font);
    const float Eff=ReadableSize(TS),TW=P.TextWidth(Text,TS,Font);
    const bool bSpecial=!IsTrim(Accent)&&!bSel&&!bOff;
    FLinearColor TextColor=bOff?Muted*.9f:bSel?FLinearColor(1.f,.9f,.55f,1):bHover?FLinearColor(1.f,.98f,.9f,1):FLinearColor(.97f,.94f,.86f,1);
    if(bSpecial)TextColor=FMath::Lerp(TextColor,Accent,.45f);
    P.Text(Text,X+(W-TW)*.5f,Y+Dy+(H-Eff*1.28f)*.5f,TS,TextColor,Font,true,true);
}
void CireUIStyle::CooldownSweep(const FCireUIPainter& P,float X,float Y,float Size,float Remaining)
{
    Remaining=FMath::Clamp(Remaining,0.f,1.f);if(Remaining<=0)return;
    const FVector2D C(X+Size*.5f,Y+Size*.5f);const float Half=Size*.5f;
    // Dark sector covering the remaining time, clockwise from 12 o'clock (WoW).
    const float Start=-PI*.5f+(1.f-Remaining)*2*PI,End=PI*1.5f;
    auto Edge=[&](float A){const FVector2D D(FMath::Cos(A),FMath::Sin(A));return C+D*(Half/FMath::Max(FMath::Abs(D.X),FMath::Abs(D.Y)));};
    const int32 Steps=FMath::Max(2,FMath::CeilToInt((End-Start)/(PI/24)));
    FVector2D Prev=Edge(Start);
    for(int32 I=1;I<=Steps;++I){const FVector2D Next=Edge(FMath::Lerp(Start,End,I/static_cast<float>(Steps)));P.Tri(C,Prev,Next,FLinearColor(0,0,0,.64f));Prev=Next;}
    const FVector2D Hand=Edge(Start);P.Line(C.X,C.Y,Hand.X,Hand.Y,FLinearColor(1.f,.9f,.6f,.55f),1.2f);
}
void CireUIStyle::IconSlot(const FCireUIPainter& P,float X,float Y,float S,const FCireIconSlot& Slot,double Time)
{
    const FAssets& A=Assets();
    const bool bUlt=Slot.Kind==ECireSlotKind::Ultimate,bPassive=Slot.Kind==ECireSlotKind::Passive;
    const float Dy=Slot.bPressed?1.f:0.f;
    P.Rect(X+2,Y+3,S,S,FLinearColor(0,0,0,.45f));
    const bool bThemed=HasThemeArt();
    const ECireThemePiece FramePiece=bUlt?ECireThemePiece::SlotUltimate:bPassive?ECireThemePiece::SlotPassive:ECireThemePiece::Slot;
    // Themed: the bevelled frame sits just inside the slot (a dark gap separates neighbours) and the
    // icon is inset within it, like the concept's framed buttons; the ultimate's crest overhangs.
    const float FramePad=bUlt?S*.06f:-S*.03f;
    if(Slot.bEmpty)
    {
        P.Rect(X,Y,S,S,FLinearColor(.02f,.025f,.03f,.9f));
        if(bThemed){CireUITheme::DrawFill(P,X+2,Y+2,S-4,S-4,PanelTint*FLinearColor(.6f,.6f,.65f,.9f));CireUITheme::Draw(P,FramePiece,X-FramePad,Y-FramePad,S+2*FramePad,S+2*FramePad,Slot.bHover?FLinearColor(1.1f,1.1f,1.1f,1):FLinearColor(.7f,.7f,.7f,.95f));}
        else if(A.Button)P.Tex(A.Button,X,Y,S,S,FLinearColor(.45f,.45f,.45f,.9f));
        if(!Slot.KeyLabel.IsEmpty())P.Text(Slot.KeyLabel,X+S-4-P.TextWidth(Slot.KeyLabel,S*.2f,ECireFont::Numbers),Y+2,S*.2f,Muted,ECireFont::Numbers,true,false);
        return;
    }
    // Icon art: painted texture if available, else a school-tinted backdrop with the sigil.
    const float Inset=bThemed&&!bPassive?S*.11f:2.f;
    if(Slot.IconTexture){if(bThemed)P.Rect(X+Inset-1,Y+Inset-1+Dy,S-2*Inset+2,S-2*Inset+2,FLinearColor(0,0,0,.9f));P.Tex(Slot.IconTexture,X+Inset,Y+Inset+Dy,S-2*Inset,S-2*Inset,FLinearColor::White);}
    else
    {
        const FLinearColor Deep=Slot.Tint*FLinearColor(.28f,.28f,.28f,1.f);
        P.Rect(X+2,Y+2+Dy,S-4,S-4,FLinearColor(.015f,.018f,.025f,1));
        if(A.IconBg)P.Tex(A.IconBg,X+2,Y+2+Dy,S-4,S-4,FLinearColor(Deep.R+.03f,Deep.G+.03f,Deep.B+.04f,1));
        Sigil(P,Slot.IconId,X+S*.13f+.8f,Y+S*.13f+Dy+.8f,S*.74f,FLinearColor(0,0,0,.7f));
        Sigil(P,Slot.IconId,X+S*.13f,Y+S*.13f+Dy,S*.74f,Slot.Tint*1.15f+FLinearColor(.12f,.12f,.12f,0));
        if(A.Gloss)P.Tex(A.Gloss,X+2,Y+2+Dy,S-4,(S-4)*.45f,FLinearColor(1,1,1,.10f));
    }
    // State tints (WoW): out of range red, not enough resource blue, cooldown sweep.
    if(Slot.bOutOfRange)P.Rect(X+2,Y+2+Dy,S-4,S-4,FLinearColor(.8f,.05f,.05f,.45f));
    else if(Slot.bNoResource)P.Rect(X+2,Y+2+Dy,S-4,S-4,FLinearColor(.1f,.2f,.85f,.45f));
    if(Slot.CooldownFraction>0)CooldownSweep(P,X+2,Y+2+Dy,S-4,Slot.CooldownFraction);
    // Frame.
    UTexture2D* FrameTex=bUlt?A.ButtonUlt:bPassive?A.ButtonPassive:A.Button;
    // ui-themes: themed slot frames; hover brightens, pressed darkens (normal/hover/pressed states).
    if(bThemed)
    {
        // hud-art: painted state frames (hover / pressed / cooldown) when the theme has them, else tint.
        const ECireThemePiece StatePiece=FramePiece!=ECireThemePiece::Slot?FramePiece:Slot.bPressed?ECireThemePiece::SlotPressed:Slot.bHover?ECireThemePiece::SlotHover:Slot.CooldownFraction>0?ECireThemePiece::SlotCooldown:ECireThemePiece::Slot;
        if(StatePiece==FramePiece||!CireUITheme::Draw(P,StatePiece,X-FramePad,Y-FramePad+Dy,S+2*FramePad,S+2*FramePad,FLinearColor::White))
            CireUITheme::Draw(P,FramePiece,X-FramePad,Y-FramePad+Dy,S+2*FramePad,S+2*FramePad,Slot.bPressed?FLinearColor(.78f,.78f,.8f,1):Slot.bHover?FLinearColor(1.3f,1.25f,1.15f,1):FLinearColor::White);
    }
    else if(FrameTex)P.Tex(FrameTex,X-(bUlt?2.f:0.f),Y-(bUlt?2.f:0.f)+Dy,S+(bUlt?4.f:0.f),S+(bUlt?4.f:0.f),FLinearColor::White);
    else{P.Line(X,Y,X+S,Y,Gold);P.Line(X,Y+S,X+S,Y+S,Gold);P.Line(X,Y,X,Y+S,Gold);P.Line(X+S,Y,X+S,Y+S,Gold);}
    const float GlowK=bThemed?CireUITheme::Active()->GlowStrength:1.f;
    if(Slot.bHover){Glow(P,X,Y,S,S,ThemeGlow*FLinearColor(.9f,.9f,.9f,.35f*GlowK));P.Rect(X+3,Y+3,S-6,S-6,FLinearColor(1,1,1,.07f));}
    // Proc / ready glow: a bright border with marching light (WoW's spell alert).
    if(Slot.bGlow)
    {
        const float Pulse=.6f+.4f*FMath::Sin(static_cast<float>(Time)*6.f);
        Glow(P,X-3,Y-3,S+6,S+6,ThemeGlow*FLinearColor(1.f,.95f,.6f,.55f*Pulse*GlowK));
        const float Per=4*S;const float T=FMath::Fmod(static_cast<float>(Time)*S*2.2f,Per);
        for(int32 I=0;I<4;++I)
        {
            float D=FMath::Fmod(T+I*Per/4,Per);FVector2D Pt;
            if(D<S)Pt=FVector2D(X+D,Y);else if(D<2*S)Pt=FVector2D(X+S,Y+D-S);else if(D<3*S)Pt=FVector2D(X+S-(D-2*S),Y+S);else Pt=FVector2D(X,Y+S-(D-3*S));
            P.Disc(Pt.X,Pt.Y+Dy,S*.07f,FLinearColor(1.f,.95f,.7f,.95f),10);
            P.Disc(Pt.X,Pt.Y+Dy,S*.14f,FLinearColor(1.f,.8f,.3f,.35f),12);
        }
        P.Line(X,Y+Dy,X+S,Y+Dy,FLinearColor(1.f,.85f,.35f,Pulse),1.5f);P.Line(X,Y+S+Dy,X+S,Y+S+Dy,FLinearColor(1.f,.85f,.35f,Pulse),1.5f);
        P.Line(X,Y+Dy,X,Y+S+Dy,FLinearColor(1.f,.85f,.35f,Pulse),1.5f);P.Line(X+S,Y+Dy,X+S,Y+S+Dy,FLinearColor(1.f,.85f,.35f,Pulse),1.5f);
    }
    if(Slot.Flash>0){P.Rect(X+2,Y+2+Dy,S-4,S-4,FLinearColor(1.f,.95f,.8f,.45f*Slot.Flash));Glow(P,X,Y,S,S,FLinearColor(1.f,.9f,.6f,.5f*Slot.Flash));}
    // Countdown, keybind, charges.
    if(Slot.CooldownRemaining>.05f)
    {
        const FString CD=Slot.CooldownRemaining>=60.f?FString::Printf(TEXT("%dm"),FMath::CeilToInt(Slot.CooldownRemaining/60.f)):
            Slot.CooldownRemaining>=3.f?FString::Printf(TEXT("%d"),FMath::CeilToInt(Slot.CooldownRemaining)):FString::Printf(TEXT("%.1f"),Slot.CooldownRemaining);
        const float CS=S*.36f;
        P.Text(CD,X+(S-P.TextWidth(CD,CS,ECireFont::Numbers))*.5f,Y+(S-CS)*.5f-S*.05f+Dy,CS,Slot.CooldownRemaining<3.f?FLinearColor(1.f,.35f,.3f,1):FLinearColor(1.f,.95f,.8f,1),ECireFont::Numbers,true,true);
    }
    if(!Slot.KeyLabel.IsEmpty())
    {
        const float KSz=FMath::Max(7.f,S*.22f);
        P.Text(Slot.KeyLabel,X+S-4-P.TextWidth(Slot.KeyLabel,KSz,ECireFont::Numbers),Y+2+Dy,KSz,Slot.bOutOfRange?FLinearColor(1.f,.3f,.25f,1):FLinearColor(.92f,.92f,.92f,1),ECireFont::Numbers,true,false);
    }
    if(Slot.Charges>1)
    {
        const FString C=FString::FromInt(Slot.Charges);const float CS=S*.26f;
        P.Text(C,X+S-4-P.TextWidth(C,CS,ECireFont::Numbers),Y+S-CS-3+Dy,CS,FLinearColor::White,ECireFont::Numbers,true,false);
    }
}
UTexture2D* CireUIStyle::FindAbilityIcon(const FString& Id)
{
    // Shared lookup with the champion-draft icon set (Content/UI/Abilities/T_<id>);
    // misses are cached so this is cheap per frame.
    static TMap<FString,TWeakObjectPtr<UTexture2D>> Found;static TSet<FString> Missing;
    if(Id.IsEmpty()||Missing.Contains(Id))return nullptr;
    if(const auto* Hit=Found.Find(Id);Hit&&Hit->IsValid())return Hit->Get();
    // Champion-draft icon set names its textures T_Ability_<id>; T_<id> is also accepted.
    UTexture2D* T=nullptr;
    for(const TCHAR* Prefix:{TEXT("T_Ability_"),TEXT("T_")})
    {
        const FString Name=FString(Prefix)+Id,Package=TEXT("/Game/UI/Abilities/")+Name;
        if(FPackageName::DoesPackageExist(Package)){T=LoadObject<UTexture2D>(nullptr,*(Package+TEXT(".")+Name),nullptr,LOAD_NoWarn|LOAD_Quiet);if(T)break;}
    }
    if(T){T->AddToRoot();Found.Add(Id,T);}else Missing.Add(Id);
    return T;
}
void CireUIStyle::Bar(const FCireUIPainter& P,float X,float Y,float W,float H,float Value,FLinearColor Color,FCireBarTrail* Trail,double Now,const FString& Text,float TextSize)
{
    const FAssets& A=Assets();
    Value=FMath::IsFinite(Value)?FMath::Clamp(Value,0.f,1.f):0.f;
    float Shown=Value,TrailValue=Value;
    if(Trail)
    {
        // Smooth fill; losses leave a pale chunk that holds briefly then drains (WoW-like).
        if(Trail->Shown<0){Trail->Shown=Trail->Trail=Value;Trail->DropTime=Now;}
        if(Value<Trail->Shown-.0005f){if(Trail->Trail<Trail->Shown)Trail->Trail=FMath::Max(Trail->Trail,Trail->Shown);else Trail->Trail=Trail->Shown;Trail->DropTime=Now;}
        Trail->Shown=Value<Trail->Shown?Value:FMath::FInterpTo(Trail->Shown,Value,1.f/60.f,9.f);
        if(Now-Trail->DropTime>.35)Trail->Trail=FMath::FInterpConstantTo(Trail->Trail,Trail->Shown,1.f/60.f,.9f);
        Trail->Trail=FMath::Max(Trail->Trail,Trail->Shown);
        Shown=Trail->Shown;TrailValue=Trail->Trail;
    }
    const bool bThemed=HasThemeArt();
    if(bThemed&&Capsule(P,X,Y,0,H,1,FLinearColor::White))
    {
        // ui-themes: rounded WoW-style bar.
        RoundBar(P,X,Y,W,H,Shown,Color,TrailValue>Shown?TrailValue:-1.f);
        if(!Text.IsEmpty())
        {
            const float TS=TextSize>0?TextSize:FMath::Max(7.f,H*.64f/TextBoost);
            P.Text(Text,X+(W-P.TextWidth(Text,TS,ECireFont::Numbers))*.5f,Y+(H-ReadableSize(TS)*1.28f)*.5f,TS,FLinearColor::White,ECireFont::Numbers,true,false);
        }
        return;
    }
    P.Rect(X,Y,W,H,bThemed?BarBack:FLinearColor(0,0,0,.85f));
    const float IW=FMath::Max(0.f,W-2),IH=FMath::Max(0.f,H-2);
    P.Rect(X+1,Y+1,IW,IH,Color*FLinearColor(.12f,.12f,.12f,1));
    if(TrailValue>Shown)P.Rect(X+1+IW*Shown,Y+1,IW*(TrailValue-Shown),IH,FLinearColor(1.f,.92f,.72f,.85f));
    if(bThemed&&CireUITheme::DrawBarFill(P,X+1,Y+1,IW*Shown,IH,Color*1.3f)){}
    else if(A.Gloss)P.Tex(A.Gloss,X+1,Y+1,IW*Shown,IH,Color*1.25f);
    else{P.Rect(X+1,Y+1,IW*Shown,IH,Color);P.Rect(X+1,Y+1,IW*Shown,IH*.22f,FLinearColor(1,1,1,.11f));}
    if(Shown>0&&Shown<1)P.Line(X+1+IW*Shown,Y+1,X+1+IW*Shown,Y+H-1,FLinearColor(1,1,1,.45f),1.f);
    P.Line(X,Y,X+W,Y,FLinearColor(0,0,0,1),1.f);
    if(bThemed&&H>=8.f)BarFrame(P,X,Y,W,H);
    if(!Text.IsEmpty())
    {
        const float TS=TextSize>0?TextSize:FMath::Max(7.f,H*.64f/TextBoost);
        P.Text(Text,X+(W-P.TextWidth(Text,TS,ECireFont::Numbers))*.5f,Y+(H-ReadableSize(TS)*1.28f)*.5f,TS,FLinearColor::White,ECireFont::Numbers,true,false);
    }
}
void CireUIStyle::TooltipFrame(const FCireUIPainter& P,float X,float Y,float W,float H,FLinearColor Border,float Opacity)
{
    const FAssets& A=Assets();
    if(HasThemeArt())
    {
        // Themed tooltip: dark backdrop at the player's opacity, a faint fill grain, the tooltip frame.
        P.Rect(X+2,Y+3,W,H,FLinearColor(0,0,0,.35f*Opacity));
        P.Rect(X+1,Y+1,W-2,H-2,FLinearColor(TooltipBg.R,TooltipBg.G,TooltipBg.B,Opacity));
        CireUITheme::DrawFill(P,X+1,Y+1,W-2,H-2,PanelTint*FLinearColor(1,1,1,.35f*Opacity));
        if(A.Gloss)P.Tex(A.Gloss,X+2,Y+2,W-4,FMath::Min(24.f,H*.3f),ThemeGlow*FLinearColor(1,1,1,.05f*Opacity));
        // A state border (rarity, hostile) tints the frame; the neutral default keeps the art colours.
        const bool bNeutral=FMath::Abs(Border.R-.55f)+FMath::Abs(Border.G-.58f)+FMath::Abs(Border.B-.64f)<.05f||IsTrim(Border);
        const float CS=FMath::Clamp(FMath::Min(W,H)/90.f,.5f,1.f);
        CireUITheme::Draw(P,ECireThemePiece::Tooltip,X-2*CS,Y-2*CS,W+4*CS,H+4*CS,bNeutral?FLinearColor(1,1,1,FMath::Max(.6f,Opacity)):FLinearColor(1,1,1,FMath::Max(.6f,Opacity)),CS);
        if(!bNeutral){const FLinearColor B=Border*FLinearColor(1,1,1,.8f);P.Line(X+4,Y+1.5f,X+W-4,Y+1.5f,B,1.2f);}
        return;
    }
    P.Rect(X+2,Y+3,W,H,FLinearColor(0,0,0,.35f*Opacity));
    P.Rect(X,Y,W,H,FLinearColor(.02f,.025f,.06f,Opacity));
    if(A.Gloss)P.Tex(A.Gloss,X+1,Y+1,W-2,FMath::Min(26.f,H*.35f),FLinearColor(.6f,.7f,1.f,.06f*Opacity));
    const FLinearColor B=Border*FLinearColor(1,1,1,FMath::Max(.5f,Opacity));
    P.Line(X,Y,X+W,Y,B,1.2f);P.Line(X,Y+H,X+W,Y+H,B,1.2f);P.Line(X,Y,X,Y+H,B,1.2f);P.Line(X+W,Y,X+W,Y+H,B,1.2f);
    const FLinearColor Inner(0,0,0,.6f*Opacity);P.Line(X+2,Y+2,X+W-2,Y+2,Inner);P.Line(X+2,Y+H-2,X+W-2,Y+H-2,Inner);
}
float CireUIStyle::Tooltip(const FCireUIPainter& P,float X,float Y,float W,const FString& Title,const FString& Body,float S,float Opacity,bool bDraw)
{
    const float Pad=10*S,TS=14*S,BS=10.5f*S;
    TArray<FString> Lines;{TArray<FString> Words;Body.ParseIntoArrayWS(Words);FString Row;
        for(const FString& Word:Words){const FString Next=Row.IsEmpty()?Word:Row+TEXT(" ")+Word;if(!Row.IsEmpty()&&P.TextWidth(Next,BS,ECireFont::Body)>W-2*Pad){Lines.Add(Row);Row=Word;}else Row=Next;}
        if(!Row.IsEmpty())Lines.Add(Row);}
    const float H=2*Pad+ReadableSize(TS)*1.2f+4*S+(Lines.IsEmpty()?0.f:4*S+Lines.Num()*(ReadableSize(BS)*1.12f+3*S));
    if(!bDraw)return H;
    TooltipFrame(P,X,Y,W,H,FLinearColor(.55f,.58f,.64f,1),Opacity);
    P.Text(Title,X+Pad,Y+Pad,TS,CireUIColors::TitleText,ECireFont::Bold);
    for(int32 I=0;I<Lines.Num();++I)P.Text(Lines[I],X+Pad,Y+Pad+ReadableSize(TS)*1.2f+8*S+I*(ReadableSize(BS)*1.12f+3*S),BS,FLinearColor(.86f,.87f,.84f,1),ECireFont::Body);
    return H;
}
void CireUIStyle::Toast(const FCireUIPainter& P,float X,float Y,float W,const FString& IconId,const FString& Title,const FString& Body,float Age,float Life,FLinearColor Accent)
{
    const float In=FMath::Clamp(Age/.25f,0.f,1.f),Out=FMath::Clamp((Life-Age)/.5f,0.f,1.f);
    if(In<=0||Out<=0)return;
    FCireUIPainter Q=P;Q.Alpha*=FMath::Min(In,Out);
    const float Slide=(1.f-FMath::Square(1.f-In))*1.f;const float OX=X+(1.f-Slide)*60.f;const float H=46;
    Frame(Q,OX,Y,W,H,Accent,ECireFrame::Card);
    FCireIconSlot Icon;Icon.IconId=IconId;Icon.Tint=Accent;Icon.IconTexture=FindAbilityIcon(IconId);
    IconSlot(Q,OX+6,Y+6,34,Icon,Age);
    Q.Text(Title,OX+48,Y+6,11,Accent*1.15f,ECireFont::Bold,false,true);
    Q.Wrapped(Body,OX+48,Y+22,W-56,9,Parchment,2,ECireFont::Body,2.f);
}
void CireUIStyle::Banner(const FCireUIPainter& P,float ViewW,float Y,const FCireBannerSpec& Spec,float Age)
{
    const float D=Spec.Duration;
    const float In=FMath::Clamp(Age/.35f,0.f,1.f),Out=FMath::Clamp((D-Age)/.6f,0.f,1.f);
    if(In<=0||Out<=0)return;
    FCireUIPainter Q=P;Q.Alpha*=FMath::Min(In,Out);
    const float Ease=1.f-FMath::Square(1.f-In);
    const float Pop=1.f+.22f*(1.f-Ease);
    const float TS=36.f*Pop;
    const float TW=Q.TextWidth(Spec.Title,TS,ECireFont::Heading);
    const float BandW=FMath::Max(TW+220.f,440.f)*(.6f+.4f*Ease),BandX=(ViewW-BandW)*.5f,BandH=TS+(Spec.Subtitle.IsEmpty()?34.f:54.f);
    // ui-themes: the theme's banner ribbon behind the title band.
    if(HasThemeArt()){const float RH=BandH*1.15f;CireUITheme::Draw(Q,ECireThemePiece::Banner,BandX-BandW*.08f,Y-16-(RH-BandH)*.5f,BandW*1.16f,RH,FLinearColor(1,1,1,.92f));}
    // Dark band that fades at both ends, gold rules above and below.
    for(int32 I=0;I<8;++I){const float Inset=I*BandW*.06f;Q.Rect(BandX+Inset,Y-16,BandW-2*Inset,BandH,FLinearColor(0,0,0,.13f));}
    if(const FAssets& A=Assets();A.Gloss)Q.Tex(A.Gloss,BandX+BandW*.2f,Y-16,BandW*.6f,BandH*.5f,Spec.Color*FLinearColor(1,1,1,.06f));
    const FLinearColor Rule=Spec.Color*FLinearColor(1,1,1,.85f);
    Q.Line(BandX+BandW*.12f,Y-16,BandX+BandW*.88f,Y-16,Rule,1.5f);Q.Line(BandX+BandW*.12f,Y-16+BandH,BandX+BandW*.88f,Y-16+BandH,Rule,1.5f);
    // Wings: tapered blades pointing outward from the title.
    for(int32 S=0;S<2;++S)
    {
        const float Dir=S?1.f:-1.f,X0=ViewW*.5f+Dir*(TW*.5f+18.f),CY=Y+TS*.55f;
        for(int32 I=0;I<3;++I)
        {
            const float L=(70.f-I*18.f)*Ease,Off=(I-1)*7.f;
            Q.Tri(FVector2D(X0,CY+Off-2.5f),FVector2D(X0+Dir*L,CY+Off*1.6f),FVector2D(X0,CY+Off+2.5f),Spec.Color*FLinearColor(1,1,1,.9f-I*.2f));
        }
        Q.Disc(X0,CY,4.f,Spec.Color*1.2f,12);
    }
    if(!Spec.Kicker.IsEmpty())Q.Text(Spec.Kicker,(ViewW-Q.TextWidth(Spec.Kicker,11,ECireFont::Heading))*.5f,Y-12,11,FLinearColor(1.f,.93f,.75f,1),ECireFont::Heading,true,true);
    Q.Text(Spec.Title,(ViewW-TW)*.5f,Y+4,TS,Spec.Color,ECireFont::Heading,true,true);
    // Shimmer sweeping across the title once.
    const float Sweep=FMath::Clamp((Age-.25f)/.9f,0.f,1.f);
    if(Sweep>0&&Sweep<1){const float SX=(ViewW-TW)*.5f-40+Sweep*(TW+80);Glow(Q,SX-30,Y+2,60,TS+6,FLinearColor(1.f,.95f,.8f,.5f*(1.f-FMath::Abs(Sweep*2-1))));}
    if(!Spec.Subtitle.IsEmpty())Q.Text(Spec.Subtitle,(ViewW-Q.TextWidth(Spec.Subtitle,13,ECireFont::Body))*.5f,Y+TS+12,13,FLinearColor(.95f,.93f,.86f,1),ECireFont::Body,true,true);
}

// ---------------------------------------------------------------------------
// Sigils (original procedural vector icons)
// ---------------------------------------------------------------------------
void CireUIStyle::Sigil(const FCireUIPainter& P,const FString& Id,float X,float Y,float S,FLinearColor Color)
{
    // Original geometric sigils: each silhouette remains distinct without licensed artwork.
    auto L=[&](float A,float B,float C,float D,float Weight=1.8f){P.Line(X+A*S,Y+B*S,X+C*S,Y+D*S,Color,Weight);};
    auto Ring=[&](float Radius){for(int32 I=0;I<24;++I){float A=I*PI/12,B=(I+1)*PI/12; L(.5f+FMath::Cos(A)*Radius,.5f+FMath::Sin(A)*Radius,.5f+FMath::Cos(B)*Radius,.5f+FMath::Sin(B)*Radius,.75f);}};
    if(Id.IsEmpty()) { L(.43f,.5f,.57f,.5f,.6f); L(.5f,.43f,.5f,.57f,.6f); return; }
    if(Id==TEXT("role_caster")) {
        // Caster: a staff crowned by a four-pointed star.
        L(.3f,.9f,.62f,.38f,2.4f);for(int32 I=0;I<12;++I){const float A=I*PI/6,B=(I+1)*PI/6;L(.72f+FMath::Cos(A)*.1f,.25f+FMath::Sin(A)*.1f,.72f+FMath::Cos(B)*.1f,.25f+FMath::Sin(B)*.1f,1.f);}L(.72f,.06f,.72f,.44f,1.4f);L(.53f,.25f,.91f,.25f,1.4f);L(.6f,.13f,.84f,.37f,.8f);L(.84f,.13f,.6f,.37f,.8f);
        return;
    }
    if(Id==TEXT("role4")||Id==TEXT("oathbound_guardian")||Id==TEXT("spectral_pack")) {
        Ring(.36f);Ring(.22f);L(.5f,.14f,.5f,.86f);L(.18f,.7f,.82f,.7f);L(.18f,.7f,.5f,.18f);L(.5f,.18f,.82f,.7f);
    } else if(Id==TEXT("venom_ground")||Id==TEXT("blight_sigil")||Id==TEXT("npc_blight_pool")) {
        Ring(.34f);for(int32 I=0;I<3;++I){const float A=I*2*PI/3;L(.5f,.5f,.5f+FMath::Cos(A)*.28f,.5f+FMath::Sin(A)*.28f,3);}Ring(.10f);
    } else if(Id==TEXT("runic_wall")) {
        L(.17f,.78f,.83f,.78f);L(.17f,.78f,.17f,.25f);L(.83f,.78f,.83f,.25f);L(.17f,.25f,.83f,.25f);L(.17f,.51f,.83f,.51f);L(.5f,.25f,.5f,.51f);L(.33f,.51f,.33f,.78f);L(.67f,.51f,.67f,.78f);
    } else if(Id==TEXT("bastion_of_dawn")) {
        Ring(.39f);L(.25f,.75f,.25f,.32f);L(.25f,.32f,.38f,.32f);L(.38f,.32f,.38f,.21f);L(.38f,.21f,.61f,.21f);L(.61f,.21f,.61f,.32f);L(.61f,.32f,.75f,.32f);L(.75f,.32f,.75f,.75f);L(.25f,.75f,.75f,.75f);L(.43f,.75f,.43f,.53f);L(.43f,.53f,.57f,.53f);L(.57f,.53f,.57f,.75f);
    } else if(Id==TEXT("cataclysm")) {
        L(.21f,.79f,.57f,.43f,3);L(.57f,.43f,.8f,.19f,3);L(.38f,.44f,.66f,.13f);L(.63f,.65f,.90f,.35f);L(.19f,.61f,.19f,.8f);L(.19f,.8f,.4f,.8f);L(.13f,.88f,.5f,.88f);L(.45f,.71f,.58f,.77f);L(.45f,.71f,.40f,.61f);
    } else if(Id==TEXT("executioners_verdict")) {
        L(.2f,.82f,.74f,.26f,3);L(.8f,.82f,.26f,.26f,3);L(.64f,.15f,.83f,.14f,3);L(.83f,.14f,.87f,.33f,3);L(.87f,.33f,.64f,.39f,3);L(.36f,.15f,.17f,.14f,3);L(.17f,.14f,.13f,.33f,3);L(.13f,.33f,.36f,.39f,3);
    } else if(Id==TEXT("renewal")) {
        Ring(.16f);for(int32 I=0;I<8;++I){float A=I*PI/4,B=A+PI/8;L(.5f+FMath::Cos(A)*.17f,.5f+FMath::Sin(A)*.17f,.5f+FMath::Cos(B)*.4f,.5f+FMath::Sin(B)*.4f);L(.5f+FMath::Cos(B)*.4f,.5f+FMath::Sin(B)*.4f,.5f+FMath::Cos(A+PI/4)*.17f,.5f+FMath::Sin(A+PI/4)*.17f);}
    } else if(Id==TEXT("iron_guard")||Id==TEXT("shield_slam")||Id==TEXT("stone_skin")||Id==TEXT("role0")) {
        L(.24f,.24f,.5f,.15f);L(.5f,.15f,.76f,.24f);L(.76f,.24f,.71f,.61f);L(.71f,.61f,.5f,.84f);L(.5f,.84f,.29f,.61f);L(.29f,.61f,.24f,.24f);
        L(.5f,.27f,.5f,.66f);L(.36f,.43f,.64f,.43f);
        if(Id==TEXT("shield_slam")){L(.77f,.12f,.91f,.08f);L(.82f,.31f,.96f,.33f);}
    } else if(Id==TEXT("restoring_light")||Id==TEXT("purify")||Id==TEXT("soul_conduit")||Id==TEXT("role2")) {
        Ring(.33f); L(.5f,.22f,.5f,.78f,3);L(.22f,.5f,.78f,.5f,3);L(.32f,.32f,.68f,.68f,.7f);L(.68f,.32f,.32f,.68f,.7f);
    } else if(Id==TEXT("polymorph")) {
        // progression-shop: placeholder until the painted icon lands: a woolly critter with a sparkle.
        Ring(.2f); L(.36f,.44f,.64f,.44f); L(.36f,.56f,.64f,.56f); L(.66f,.40f,.80f,.34f); L(.80f,.34f,.82f,.46f); L(.82f,.46f,.68f,.50f);
        L(.38f,.64f,.36f,.80f); L(.62f,.64f,.64f,.80f); L(.22f,.20f,.30f,.20f); L(.26f,.16f,.26f,.24f);
    } else if(Id==TEXT("frost_bind")) {
        for(int32 I=0;I<6;++I){float A=I*PI/3;float DX=FMath::Cos(A),DY=FMath::Sin(A);L(.5f,.5f,.5f+DX*.37f,.5f+DY*.37f);L(.5f+DX*.24f,.5f+DY*.24f,.5f+DX*.22f-DY*.12f,.5f+DY*.22f+DX*.12f);}
    } else if(Id==TEXT("chain_spark")||Id==TEXT("deep_reserves")) {
        L(.62f,.12f,.29f,.52f,3);L(.29f,.52f,.61f,.46f,3);L(.61f,.46f,.39f,.87f,3);
        if(Id==TEXT("chain_spark")){L(.77f,.32f,.88f,.41f);L(.2f,.65f,.1f,.77f);}
    } else if(Id==TEXT("ember_lance")) {
        L(.48f,.12f,.28f,.43f,2);L(.28f,.43f,.22f,.67f,2);L(.22f,.67f,.47f,.87f,2);L(.47f,.87f,.77f,.63f,2);L(.77f,.63f,.69f,.30f,2);L(.69f,.30f,.56f,.52f,2);L(.56f,.52f,.48f,.12f,2);L(.47f,.57f,.43f,.78f,2);
    } else if(Id==TEXT("sanctuary")) {
        Ring(.34f);Ring(.23f);L(.19f,.77f,.81f,.77f);L(.5f,.15f,.5f,.64f);L(.28f,.39f,.72f,.39f);
    } else if(Id==TEXT("war_cry")) {
        L(.24f,.34f,.7f,.16f);L(.7f,.16f,.7f,.69f);L(.7f,.69f,.24f,.55f);L(.24f,.55f,.24f,.34f);L(.35f,.59f,.43f,.84f);L(.78f,.2f,.9f,.1f);L(.8f,.44f,.94f,.44f);L(.78f,.67f,.89f,.77f);
    } else if(Id==TEXT("piercing_shot")||Id==TEXT("role1")) {
        L(.23f,.77f,.78f,.22f,2.5f);L(.55f,.2f,.8f,.2f);L(.8f,.2f,.8f,.46f);L(.24f,.60f,.24f,.77f);L(.24f,.77f,.41f,.77f);L(.34f,.69f,.34f,.51f);
    } else if(Id==TEXT("shadow_step")) {
        L(.60f,.13f,.32f,.31f);L(.32f,.31f,.25f,.62f);L(.25f,.62f,.47f,.84f);L(.47f,.84f,.68f,.7f);L(.68f,.7f,.46f,.65f);L(.46f,.65f,.46f,.4f);L(.46f,.4f,.6f,.13f);L(.7f,.4f,.88f,.4f);L(.73f,.52f,.91f,.52f);
    } else if(Id==TEXT("battle_rhythm")) {
        Ring(.33f);L(.24f,.50f,.38f,.50f);L(.38f,.5f,.45f,.3f);L(.45f,.3f,.56f,.72f);L(.56f,.72f,.63f,.5f);L(.63f,.5f,.79f,.5f);
    } else {
        L(.23f,.8f,.7f,.26f,3);L(.7f,.26f,.85f,.15f,3);L(.85f,.15f,.77f,.38f,2);L(.77f,.38f,.3f,.84f,2);L(.22f,.6f,.46f,.84f,3);L(.18f,.85f,.26f,.93f,3);
        if(Id==TEXT("cleaving_strike")){L(.15f,.43f,.28f,.24f);L(.28f,.24f,.53f,.13f);}
    }
}

// ---------------------------------------------------------------------------
// ui-themes: themed pieces (fallbacks keep the procedural look)
// ---------------------------------------------------------------------------
void CireUIStyle::PortraitRing(const FCireUIPainter& P,float CX,float CY,float R,FLinearColor Tint)
{
    // The ring art's opening is ~72% of the piece; size it so the opening matches the portrait.
    const float Outer=R/.72f;
    if(HasThemeArt()&&CireUITheme::Draw(P,ECireThemePiece::Ring,CX-Outer,CY-Outer,2*Outer,2*Outer,Tint))return;
    P.Circle(CX,CY,R,Gold*Tint,2.2f);P.Circle(CX,CY,R+2.5f,FLinearColor(0,0,0,.9f),1.f);
}
float CireUIStyle::FrameCornerClear(float W,float H)
{
    const FCireUITheme* T=HasThemeArt()?CireUITheme::Active():nullptr;
    if(!T)return 0.f;
    const float CS=CornerScaleFor(W,H);
    // Mirrors ThemedFrame: the panel piece is drawn 6*CS outside the rectangle; its corner cell is Corner*CS
    // (the ornament fills most of the cell, so a little under the full cell is enough to clear it).
    return FMath::Max(0.f,T->Piece(ECireThemePiece::Panel).Corner*CS*.85f-6.f*CS);
}
UTexture2D* CireUIStyle::ChampionPortrait(const FString& ProfileId)
{
    if(ProfileId.IsEmpty())return nullptr;
    static TMap<FString,TStrongObjectPtr<UTexture2D>> Cache;
    if(const auto* Found=Cache.Find(ProfileId))return Found->Get();
    const FString Path=FString::Printf(TEXT("/Game/UI/Draft/Portraits/T_Portrait_%s.T_Portrait_%s"),*ProfileId,*ProfileId);
    UTexture2D* Texture=FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Path))?LoadObject<UTexture2D>(nullptr,*Path,nullptr,LOAD_NoWarn|LOAD_Quiet):nullptr;
    Cache.Add(ProfileId,TStrongObjectPtr<UTexture2D>(Texture));
    return Texture;
}
bool CireUIStyle::PortraitFace(const FCireUIPainter& P,const FString& ProfileId,float CX,float CY,float R,bool bDead)
{
    UTexture2D* Face=ChampionPortrait(ProfileId);
    if(!Face)return false;
    // Face crop of the 512 px bust render (same framing as champion select), clipped round so the ring's
    // opening shows only the face; a soft top light and a rim shade keep it crisp and readable at 30 px.
    P.Disc(CX,CY,R+1.f,FLinearColor(0,0,0,.95f),40);
    P.TexDisc(Face,CX,CY,R,bDead?FLinearColor(.32f,.32f,.34f,1):FLinearColor::White,.2f,.06f,.8f,.66f);
    if(!bDead)P.Disc(CX,CY-R*.35f,R*.55f,FLinearColor(1,.97f,.9f,.05f),28);
    P.Circle(CX,CY,R-.5f,FLinearColor(0,0,0,.45f),1.5f,40);
    return true;
}
void CireUIStyle::RoleBadge(const FCireUIPainter& P,float CX,float CY,float R,const FString& SigilId,FLinearColor Tint,const FString& PaintedIcon)
{
    Medallion(P,CX,CY,R,FString(),FLinearColor::White);
    if(UTexture2D* Tex=!PaintedIcon.IsEmpty()&&R>=7.f?FindAbilityIcon(PaintedIcon):nullptr)P.TexDisc(Tex,CX,CY,R*.86f,FLinearColor::White,.08f,.08f,.92f,.92f);
    else Sigil(P,SigilId,CX-R*.72f,CY-R*.72f,R*1.44f,Tint);
}
void CireUIStyle::Medallion(const FCireUIPainter& P,float CX,float CY,float R,const FString& Text,FLinearColor TextColor)
{
    P.Disc(CX,CY,R+1.5f,FLinearColor(0,0,0,.9f));P.Disc(CX,CY,R,FLinearColor(Ink.R*1.6f,Ink.G*1.6f,Ink.B*1.6f,1),24);
    if(!(HasThemeArt()&&CireUITheme::Draw(P,ECireThemePiece::Ring,CX-R*1.3f,CY-R*1.3f,R*2.6f,R*2.6f,FLinearColor::White)))P.Circle(CX,CY,R,Gold,1.2f,20);
    if(!Text.IsEmpty()){const float S=FMath::Max(R*.95f,R*1.15f/TextBoost);P.Text(Text,CX-P.TextWidth(Text,S,ECireFont::Numbers)*.5f,CY-ReadableSize(S)*.64f,S,TextColor,ECireFont::Numbers,true,true);}
}
void CireUIStyle::MinimapFrame(const FCireUIPainter& P,float X,float Y,float W,float H)
{
    if(HasThemeArt()&&CireUITheme::Draw(P,ECireThemePiece::Minimap,X-5,Y-5,W+10,H+10,FLinearColor::White,FMath::Clamp(FMath::Min(W,H)/120.f,.5f,1.f)))return;
    P.Line(X,Y,X+W,Y,Gold*.6f);P.Line(X,Y+H,X+W,Y+H,Gold*.6f);P.Line(X,Y,X,Y+H,Gold*.6f);P.Line(X+W,Y,X+W,Y+H,Gold*.6f);
}
void CireUIStyle::Divider(const FCireUIPainter& P,float X,float Y,float W,FLinearColor Tint)
{
    if(HasThemeArt())
    {
        const FCireUITheme& T=*CireUITheme::Active();const FCireThemePiece& D=T.Piece(ECireThemePiece::Divider);
        const float H=FMath::Clamp(W*D.SizePx.Y/FMath::Max(1.f,D.SizePx.X),4.f,10.f);
        if(CireUITheme::Draw(P,ECireThemePiece::Divider,X,Y-H*.5f,W,H,Tint))return;
    }
    P.Line(X,Y,X+W,Y,Gold*Tint*FLinearColor(1,1,1,.7f),1.2f);
}
void CireUIStyle::Ornament(const FCireUIPainter& P,float CX,float Y,float Height,FLinearColor Tint)
{
    if(HasThemeArt())
    {
        const FCireThemePiece& O=CireUITheme::Active()->Piece(ECireThemePiece::Ornament);
        const float W=Height*O.SizePx.X/FMath::Max(1.f,O.SizePx.Y);
        if(CireUITheme::Draw(P,ECireThemePiece::Ornament,CX-W*.5f,Y-Height*.5f,W,Height,Tint))return;
    }
    const FAssets& A=Assets();
    if(A.Gem){const float G=Height*.5f;P.Tex(A.Gem,CX-G,Y-G,2*G,2*G,Gold*Tint);}
}
void CireUIStyle::BarFrame(const FCireUIPainter& P,float X,float Y,float W,float H)
{
    // The frame's straight middle section spans the whole bar (no end caps crowding the text);
    // short bevelled end posts close it. Thickness follows the bar height.
    // A crisp bevelled trim in the theme colour (the concept bars): dark gap, trim line, soft top highlight.
    if(!HasThemeArt())return;
    const float T=H>=14.f?1.4f:1.1f;const FLinearColor Tr=Gold*FLinearColor(1.1f,1.1f,1.1f,.95f);
    const FLinearColor Gap(0,0,0,.85f);P.Rect(X-1,Y-1,W+2,1,Gap);P.Rect(X-1,Y+H,W+2,1,Gap);P.Rect(X-1,Y,1,H,Gap);P.Rect(X+W,Y,1,H,Gap);
    P.Rect(X-1-T,Y-1-T,W+2+2*T,T,Tr);P.Rect(X-1-T,Y+H+1,W+2+2*T,T,Tr*FLinearColor(.7f,.7f,.7f,1));
    P.Rect(X-1-T,Y-1,T,H+2,Tr*FLinearColor(.85f,.85f,.85f,1));P.Rect(X+W+1,Y-1,T,H+2,Tr*FLinearColor(.85f,.85f,.85f,1));
    P.Rect(X,Y+1,W,FMath::Max(1.f,H*.18f),FLinearColor(1,1,1,.06f));
}
void CireUIStyle::CastBar(const FCireUIPainter& P,float X,float Y,float W,float H,float Progress,FLinearColor Color,const FString& Name,const FString& Time,float TextSize)
{
    Progress=FMath::IsFinite(Progress)?FMath::Clamp(Progress,0.f,1.f):0.f;
    const bool bThemed=HasThemeArt();
    if(bThemed&&Capsule(P,X,Y,0,H,1,FLinearColor::White))
    {
        // ui-themes: rounded cast bar with a small theme crest at each end (outside the bar).
        RoundBar(P,X,Y,W,H,Progress,Color);
        if(H>=10.f){Ornament(P,X-1.f,Y+H*.5f,H*1.1f);Ornament(P,X+W+1.f,Y+H*.5f,H*1.1f);}
        const float TS=TextSize>0?TextSize:FMath::Max(7.f,H*.6f/TextBoost);const float TY=Y+(H-ReadableSize(TS)*1.28f)*.5f;
        if(!Name.IsEmpty())P.Text(P.Fit(Name,TS,W-(Time.IsEmpty()?16.f:48.f),ECireFont::Bold),X+H*.5f+3,TY,TS,FLinearColor::White,ECireFont::Bold,true,false);
        if(!Time.IsEmpty())P.Text(Time,X+W-H*.5f-3-P.TextWidth(Time,TS,ECireFont::Numbers),TY,TS,FLinearColor::White,ECireFont::Numbers,true,false);
        return;
    }
    P.Rect(X,Y,W,H,bThemed?BarBack:FLinearColor(0,0,0,.85f));
    if(!(bThemed&&CireUITheme::DrawBarFill(P,X+1,Y+1,(W-2)*Progress,H-2,Color*1.2f)))
    {
        const FAssets& A=Assets();
        if(A.Gloss)P.Tex(A.Gloss,X+1,Y+1,(W-2)*Progress,H-2,Color);else P.Rect(X+1,Y+1,(W-2)*Progress,H-2,Color);
    }
    if(Progress>0&&Progress<1)P.Line(X+1+(W-2)*Progress,Y+1,X+1+(W-2)*Progress,Y+H-1,FLinearColor(1,1,1,.6f),1.2f);
    if(bThemed)
    {
        // Ornamental end caps sit outside the bar so the spell name and time stay clear.
        const FCireThemePiece& C=CireUITheme::Active()->Piece(ECireThemePiece::CastFrame);
        const float Pad=FMath::Clamp(H*.28f,2.f,5.f),FH=H+2*Pad;
        const float Cap=FH*C.Slice*C.SizePx.X/FMath::Max(1.f,C.SizePx.Y);
        CireUITheme::Draw(P,ECireThemePiece::CastFrame,X-Cap*.6f,Y-Pad,W+Cap*1.2f,FH,FLinearColor(1.1f,1.1f,1.1f,1));
    }
    const float TS=TextSize>0?TextSize:FMath::Max(7.f,H*.6f/TextBoost);const float TY=Y+(H-ReadableSize(TS)*1.28f)*.5f;
    if(!Name.IsEmpty())P.Text(P.Fit(Name,TS,W-(Time.IsEmpty()?12.f:44.f),ECireFont::Bold),X+6,TY,TS,FLinearColor::White,ECireFont::Bold,true,false);
    if(!Time.IsEmpty())P.Text(Time,X+W-6-P.TextWidth(Time,TS,ECireFont::Numbers),TY,TS,FLinearColor::White,ECireFont::Numbers,true,false);
}

// ---------------------------------------------------------------------------
// ui-themes: rounded (capsule) bars
// ---------------------------------------------------------------------------
namespace
{
UTexture2D* CapsuleTexture()
{
    static TWeakObjectPtr<UTexture2D> Cached;static bool bTried=false;
    if(!bTried){bTried=true;if(UTexture2D* T=LoadObject<UTexture2D>(nullptr,TEXT("/Game/UI/Themes/Common/T_BarCapsule.T_BarCapsule"),nullptr,LOAD_NoWarn|LOAD_Quiet)){T->AddToRoot();Cached=T;}}
    return Cached.Get();
}
}
bool CireUIStyle::Capsule(const FCireUIPainter& P,float X,float Y,float W,float H,float Fraction,FLinearColor Color,int32 Layer)
{
    UTexture2D* T=CapsuleTexture();
    if(!T)return false;
    const float F=W*FMath::Clamp(Fraction,0.f,1.f);
    if(F<=.01f||H<=0)return true;
    const float C=FMath::Min(H*.5f,W*.5f),CapU=.125f,V0=Layer?.5f:0.f,V1=Layer?1.f:.5f;
    if(F>=2*C)
    {
        P.Tex(T,X,Y,C,H,Color,0,V0,CapU,V1);
        if(F>2*C)P.Tex(T,X+C,Y,F-2*C,H,Color,CapU,V0,1-CapU,V1);
        P.Tex(T,X+F-C,Y,C,H,Color,1-CapU,V0,1,V1);
    }
    else
    {
        // Very low fill: a small pill made of the start of the left cap and the end of the right cap.
        const float L=FMath::Min(F,C),R=F-L;
        P.Tex(T,X,Y,L,H,Color,0,V0,CapU*L/C,V1);
        if(R>0)P.Tex(T,X+L,Y,R,H,Color,1-CapU*R/C,V0,1,V1);
    }
    return true;
}
void CireUIStyle::RoundBar(const FCireUIPainter& P,float X,float Y,float W,float H,float Fraction,FLinearColor Color,float Trail)
{
    Fraction=FMath::IsFinite(Fraction)?FMath::Clamp(Fraction,0.f,1.f):0.f;
    // Everything stays inside X..X+W / Y..Y+H so stacked bars never overlap: a faint theme rim,
    // the thin dark border, then the rounded content inset.
    const bool bRim=H>=8.f;
    const float Rim=bRim?1.f:0.f,Border=H>=12.f?1.3f:.9f,In=Rim+Border;
    if(bRim)Capsule(P,X,Y,W,H,1,Gold*FLinearColor(1,1,1,.6f));
    Capsule(P,X+Rim,Y+Rim,W-2*Rim,H-2*Rim,1,FLinearColor(0,0,0,.94f));
    const float CX=X+In,CY=Y+In,CW=FMath::Max(0.f,W-2*In),CH=FMath::Max(1.f,H-2*In);
    Capsule(P,CX,CY,CW,CH,1,FLinearColor(Color.R*.13f+.01f,Color.G*.13f+.01f,Color.B*.13f+.015f,1));
    if(Trail>Fraction)Capsule(P,CX,CY,CW,CH,Trail,FLinearColor(1.f,.92f,.72f,.85f));
    Capsule(P,CX,CY,CW,CH,Fraction,Color*1.2f);
    Capsule(P,CX,CY,CW,CH,Fraction,FLinearColor(1,1,1,.9f),1);
    Capsule(P,CX,CY,CW,CH,1,FLinearColor(1,1,1,.1f),1); // faint glass over the empty part
}

// ---------------------------------------------------------------------------
// readability: rich (WoW-style) tooltips and bevelled cards
// ---------------------------------------------------------------------------
namespace
{
const FLinearColor TipBody(.90f,.90f,.87f,1.f);
}
FCireTooltipSpec& FCireTooltipSpec::Text(const FString& T,FLinearColor C,float Size,ECireFont F)
{
    if(T.IsEmpty())return *this;
    FCireTooltipRow R;R.Kind=FCireTooltipRow::EKind::Text;R.Left=T;R.LeftColor=C;R.Size=Size;R.Font=F;Rows.Add(R);return *this;
}
FCireTooltipSpec& FCireTooltipSpec::Pair(const FString& L,const FString& Rt,FLinearColor LC,FLinearColor RC)
{
    if(L.IsEmpty()&&Rt.IsEmpty())return *this;
    FCireTooltipRow R;R.Kind=FCireTooltipRow::EKind::Pair;R.Left=L;R.Right=Rt;R.LeftColor=LC;R.RightColor=RC;Rows.Add(R);return *this;
}
FCireTooltipSpec& FCireTooltipSpec::Header(const FString& T,FLinearColor C)
{
    FCireTooltipRow R;R.Kind=FCireTooltipRow::EKind::Header;R.Left=T;R.LeftColor=C;R.Font=ECireFont::Heading;Rows.Add(R);return *this;
}
FCireTooltipSpec& FCireTooltipSpec::Stat(const FString& T,FLinearColor C)
{
    if(T.IsEmpty())return *this;
    FCireTooltipRow R;R.Kind=FCireTooltipRow::EKind::Stat;R.Left=T;R.LeftColor=C;R.Font=ECireFont::Bold;Rows.Add(R);return *this;
}
FCireTooltipSpec& FCireTooltipSpec::Divider()
{
    if(Rows.Num()&&Rows.Last().Kind==FCireTooltipRow::EKind::Divider)return *this;
    FCireTooltipRow R;R.Kind=FCireTooltipRow::EKind::Divider;Rows.Add(R);return *this;
}
FCireTooltipSpec& FCireTooltipSpec::Bar(float Fraction,const FString& Label,FLinearColor C)
{
    FCireTooltipRow R;R.Kind=FCireTooltipRow::EKind::Bar;R.Fraction=FMath::Clamp(Fraction,0.f,1.f);R.Left=Label;R.LeftColor=C;Rows.Add(R);return *this;
}
FLinearColor CireUIStyle::StatColor(const FString& Line)
{
    // "+20 Attack" / "DEF +20%" read green, "DEF -20%" / "Healing -50%" red, anything else gold.
    for(int32 I=0;I+1<Line.Len();++I)
        if((Line[I]==TEXT('+')||Line[I]==TEXT('-')||Line[I]==0x2212)&&FChar::IsDigit(Line[I+1]))
            return Line[I]==TEXT('+')?FLinearColor(.42f,1.f,.48f,1):FLinearColor(1.f,.42f,.36f,1); // '-' or U+2212
    return FLinearColor(1.f,.84f,.4f,1);
}
bool CireUIStyle::IsStatLine(const FString& Line)
{
    // Short symbol lines: "+20 Attack", "DEF +20%  ·  8s left", "Healing -50%".
    if(Line.Len()>64)return false;
    for(int32 I=0;I+1<Line.Len();++I)
        if((Line[I]==TEXT('+')||Line[I]==TEXT('-')||Line[I]==0x2212)&&FChar::IsDigit(Line[I+1])&&(I==0||Line[I-1]==TEXT(' ')))return true;
    return false;
}
FCireTooltipSpec CireUIStyle::TooltipFromText(const FString& Title,const FString& Body)
{
    FCireTooltipSpec Spec;Spec.Title=Title;
    TArray<FString> Paragraphs;Body.ParseIntoArrayLines(Paragraphs,false);
    bool bFirst=true;
    for(FString Paragraph:Paragraphs)
    {
        Paragraph.TrimStartAndEndInline();
        if(Paragraph.IsEmpty()){Spec.Divider();continue;}
        // "UNIQUE PASSIVE  text" / "ACTIVE  text" / "USE  text" sections get a header.
        static const TCHAR* Sections[]={TEXT("UNIQUE PASSIVE"),TEXT("ACTIVE"),TEXT("USE"),TEXT("APOTHEOSIS")};
        bool bSection=false;
        for(const TCHAR* S:Sections)
            if(Paragraph.StartsWith(FString(S)+TEXT("  ")))
            {
                Spec.Divider();Spec.Header(S,FString(S)==TEXT("ACTIVE")||FString(S)==TEXT("USE")?FLinearColor(.4f,.9f,.8f,1):FLinearColor(1.f,.8f,.3f,1));
                Spec.Text(Paragraph.Mid(FCString::Strlen(S)).TrimStart(),TipBody);bSection=true;break;
            }
        if(bSection){bFirst=false;continue;}
        if(IsStatLine(Paragraph)&&Paragraph.Len()<=48)Spec.Stat(Paragraph,StatColor(Paragraph));
        else Spec.Text(Paragraph,bFirst?FLinearColor(1.f,.86f,.5f,1):TipBody);
        bFirst=false;
    }
    return Spec;
}
float CireUIStyle::RichTooltip(const FCireUIPainter& P,float X,float Y,float W,const FCireTooltipSpec& Spec,float S,float Opacity,bool bDraw,float MaxHeight,int32* OutLines)
{
    using EKind=FCireTooltipRow::EKind;
    const float Pad=12.f*S,BodyS=11.f*S,TitleS=15.5f*S,TagS=9.f*S,SubS=10.f*S,HeadS=9.f*S,StatS=11.5f*S,FootS=9.5f*S;
    auto Step=[&](float Size){return ReadableSize(Size)*1.18f+2.5f*S;};
    const bool bPortrait=!Spec.PortraitId.IsEmpty()&&ChampionPortrait(Spec.PortraitId)!=nullptr;
    const bool bIcon=bPortrait||Spec.Icon||!Spec.Sigil.IsEmpty();
    const float IconS=bIcon?46.f*S:0.f,IconGap=bIcon?11.f*S:0.f;
    const float TextX=X+Pad+IconS+IconGap,TextW=W-2*Pad-IconS-IconGap,RowW=W-2*Pad;
    // Header block: title (with the tag on its right), subtitle.
    const float TagW=Spec.Tag.IsEmpty()?0.f:P.TextWidth(Spec.Tag,TagS,ECireFont::Heading)+10.f*S;
    const FString Title=P.Fit(Spec.Title,TitleS,FMath::Max(40.f,TextW-TagW),ECireFont::Bold);
    TArray<FString> SubLines;
    if(!Spec.Subtitle.IsEmpty())
    {
        TArray<FString> Words;Spec.Subtitle.ParseIntoArrayWS(Words);FString Row;
        for(const FString& Word:Words){const FString Next=Row.IsEmpty()?Word:Row+TEXT(" ")+Word;if(!Row.IsEmpty()&&P.TextWidth(Next,SubS,ECireFont::Body)>TextW){SubLines.Add(Row);Row=Word;}else Row=Next;}
        if(!Row.IsEmpty())SubLines.Add(Row);
        if(SubLines.Num()>2){SubLines.SetNum(2);SubLines[1]=P.Fit(SubLines[1]+TEXT(" ..."),SubS,TextW,ECireFont::Body);}
    }
    const float TitleBlock=Step(TitleS)+SubLines.Num()*Step(SubS);
    const float HeadH=FMath::Max(IconS,TitleBlock);
    // Rows -> laid-out lines.
    struct FLine { EKind Kind; FString L,R; float Size; FLinearColor LC,RC; ECireFont Font; float Fraction; float H; };
    TArray<FLine> Lines;
    auto Wrap=[&](const FString& Text,float Size,ECireFont Font,FLinearColor Color,EKind Kind)
    {
        TArray<FString> Words;Text.ParseIntoArrayWS(Words);FString Row;
        auto Emit=[&](const FString& L){Lines.Add({Kind,L,FString(),Size,Color,Color,Font,0.f,Step(Size)});};
        for(const FString& Word:Words)
        {
            const FString Next=Row.IsEmpty()?Word:Row+TEXT(" ")+Word;
            if(!Row.IsEmpty()&&P.TextWidth(Next,Size,Font)>RowW){Emit(Row);Row=Word;}else Row=Next;
        }
        if(!Row.IsEmpty())Emit(P.Fit(Row,Size,RowW,Font));
    };
    for(const FCireTooltipRow& R:Spec.Rows)
    {
        switch(R.Kind)
        {
        case EKind::Text: Wrap(R.Left,R.Size>0?R.Size*S:BodyS,R.Font,R.LeftColor,EKind::Text);break;
        case EKind::Stat: Wrap(R.Left,StatS,ECireFont::Bold,R.LeftColor,EKind::Stat);break;
        case EKind::Header: Lines.Add({EKind::Header,R.Left,FString(),HeadS,R.LeftColor,R.LeftColor,ECireFont::Heading,0.f,Step(HeadS)+4.f*S});break;
        case EKind::Divider: if(Lines.Num())Lines.Add({EKind::Divider,FString(),FString(),0.f,FLinearColor::White,FLinearColor::White,ECireFont::Body,0.f,11.f*S});break;
        case EKind::Bar: Lines.Add({EKind::Bar,R.Left,FString(),10.f*S,R.LeftColor,FLinearColor::White,ECireFont::Numbers,R.Fraction,17.f*S+5.f*S});break;
        case EKind::Gap: Lines.Add({EKind::Gap,FString(),FString(),0.f,FLinearColor::White,FLinearColor::White,ECireFont::Body,0.f,6.f*S});break;
        case EKind::Pair:
        {
            const float RW=R.Right.IsEmpty()?0.f:P.TextWidth(R.Right,BodyS,ECireFont::Body)+12.f*S;
            Lines.Add({EKind::Pair,P.Fit(R.Left,BodyS,RowW-RW,ECireFont::Body),R.Right,BodyS,R.LeftColor,R.RightColor,ECireFont::Body,0.f,Step(BodyS)});break;
        }
        }
    }
    while(Lines.Num()&&Lines.Last().Kind==EKind::Divider)Lines.Pop();
    const float FootH=Spec.Footer.IsEmpty()?0.f:6.f*S+Step(FootS);
    const float DividerH=Lines.Num()?12.f*S:4.f*S;
    float H=Pad+HeadH+DividerH+FootH+Pad*.85f;
    for(const FLine& L:Lines)H+=L.H;
    // Too tall for the screen: drop trailing lines and mark the cut.
    if(MaxHeight>0&&H>MaxHeight)
    {
        while(Lines.Num()>1&&H>MaxHeight){H-=Lines.Last().H;Lines.Pop();}
        if(Lines.Num()&&(Lines.Last().Kind==EKind::Text||Lines.Last().Kind==EKind::Stat))Lines.Last().L=P.Fit(Lines.Last().L+TEXT(" ..."),Lines.Last().Size,RowW,Lines.Last().Font);
    }
    if(OutLines){int32 N=0;for(const FLine& L:Lines)N+=L.Kind==EKind::Text||L.Kind==EKind::Stat?1:0;*OutLines=N;}
    if(!bDraw)return H;
    // Frame: the themed tooltip frame, a title band washed in the accent, the crest on top.
    TooltipFrame(P,X,Y,W,H,Spec.Accent,Opacity);
    const float BandH=Pad+HeadH+4.f*S;
    for(int32 I=0;I<6;++I)P.Rect(X+3,Y+3+I*BandH/6.f,W-6,BandH/6.f+.5f,Spec.Accent*FLinearColor(1,1,1,(.2f-I*.032f)*FMath::Max(.5f,Opacity)));
    if(HasThemeArt())Ornament(P,X+W*.5f,Y+1.f,FMath::Clamp(13.f*S,9.f,16.f));
    // Icon (painted art, sigil or a champion portrait) in the kit's frame.
    if(bIcon)
    {
        const float IX=X+Pad,IY=Y+Pad;
        if(bPortrait)
        {
            const float R=IconS*.5f-2.f;
            PortraitFace(P,Spec.PortraitId,IX+IconS*.5f,IY+IconS*.5f,R);PortraitRing(P,IX+IconS*.5f,IY+IconS*.5f,R);
        }
        else
        {
            FCireIconSlot Slot;Slot.IconTexture=Spec.Icon;Slot.IconId=Spec.Sigil;Slot.Tint=Spec.IconTint;Slot.Kind=Spec.IconKind;
            IconSlot(P,IX,IY,IconS,Slot,0.0);
        }
    }
    float TY=Y+Pad+FMath::Max(0.f,(HeadH-TitleBlock)*.5f);
    P.Text(Title,TextX,TY,TitleS,Spec.TitleColor,ECireFont::Bold,true,true);
    if(!Spec.Tag.IsEmpty())P.Text(Spec.Tag,X+W-Pad-P.TextWidth(Spec.Tag,TagS,ECireFont::Heading),TY+(ReadableSize(TitleS)-ReadableSize(TagS))*.55f,TagS,Spec.TagColor,ECireFont::Heading,true,true);
    TY+=Step(TitleS);
    for(const FString& Sub:SubLines){P.Text(Sub,TextX,TY,SubS,Spec.SubtitleColor,ECireFont::Body,false,true);TY+=Step(SubS);}
    float LY=Y+Pad+HeadH+4.f*S;
    if(Lines.Num()){Divider(P,X+Pad*.6f,LY+3.f*S,W-Pad*1.2f,FLinearColor(1,1,1,.9f));LY+=DividerH-4.f*S;}
    for(const FLine& L:Lines)
    {
        switch(L.Kind)
        {
        case EKind::Divider: Divider(P,X+Pad*1.5f,LY+L.H*.5f,W-Pad*3.f,FLinearColor(1,1,1,.55f));break;
        case EKind::Header: P.Text(L.L,X+Pad,LY+4.f*S,L.Size,L.LC,ECireFont::Heading,true,true);break;
        case EKind::Bar:
        {
            const float BH=17.f*S;
            P.Rect(X+Pad,LY+2.f*S,RowW,BH,FLinearColor(0,0,0,.9f));
            P.Rect(X+Pad+1,LY+2.f*S+1,(RowW-2)*L.Fraction,BH-2,L.LC);
            P.Rect(X+Pad+1,LY+2.f*S+1,(RowW-2)*L.Fraction,(BH-2)*.4f,FLinearColor(1,1,1,.16f));
            if(!L.L.IsEmpty())P.Text(L.L,X+Pad+(RowW-P.TextWidth(L.L,L.Size,ECireFont::Numbers))*.5f,LY+2.f*S+(BH-ReadableSize(L.Size)*1.18f)*.5f,L.Size,FLinearColor::White,ECireFont::Numbers,true,false);
            break;
        }
        case EKind::Pair:
            P.Text(L.L,X+Pad,LY,L.Size,L.LC,ECireFont::Body,false,true);
            if(!L.R.IsEmpty())P.Text(L.R,X+W-Pad-P.TextWidth(L.R,L.Size,ECireFont::Body),LY,L.Size,L.RC,ECireFont::Body,false,true);
            break;
        case EKind::Gap: break;
        default: P.Text(L.L,X+Pad,LY,L.Size,L.LC,L.Font,L.Kind==EKind::Stat,true);break;
        }
        LY+=L.H;
    }
    if(!Spec.Footer.IsEmpty())P.Text(P.Fit(Spec.Footer,FootS,RowW,ECireFont::Body),X+Pad,LY+6.f*S,FootS,Muted*1.15f,ECireFont::Body,false,true);
    return H;
}
void CireUIStyle::Bevel(const FCireUIPainter& P,float X,float Y,float W,float H,float C,FLinearColor Color)
{
    C=FMath::Clamp(C,0.f,FMath::Min(W,H)*.5f);
    if(C<=.5f){P.Rect(X,Y,W,H,Color);return;}
    P.Rect(X+C,Y,W-2*C,H,Color);P.Rect(X,Y+C,C,H-2*C,Color);P.Rect(X+W-C,Y+C,C,H-2*C,Color);
    P.Tri(FVector2D(X,Y+C),FVector2D(X+C,Y),FVector2D(X+C,Y+C),Color);
    P.Tri(FVector2D(X+W-C,Y),FVector2D(X+W,Y+C),FVector2D(X+W-C,Y+C),Color);
    P.Tri(FVector2D(X,Y+H-C),FVector2D(X+C,Y+H-C),FVector2D(X+C,Y+H),Color);
    P.Tri(FVector2D(X+W-C,Y+H-C),FVector2D(X+W,Y+H-C),FVector2D(X+W-C,Y+H),Color);
}
void CireUIStyle::BevelOutline(const FCireUIPainter& P,float X,float Y,float W,float H,float C,FLinearColor Color,float Width)
{
    C=FMath::Clamp(C,0.f,FMath::Min(W,H)*.5f);
    const FVector2D Pts[]={{X+C,Y},{X+W-C,Y},{X+W,Y+C},{X+W,Y+H-C},{X+W-C,Y+H},{X+C,Y+H},{X,Y+H-C},{X,Y+C}};
    for(int32 I=0;I<8;++I){const FVector2D& A=Pts[I];const FVector2D& B=Pts[(I+1)%8];P.Line(A.X,A.Y,B.X,B.Y,Color,Width);}
}
void CireUIStyle::BevelCard(const FCireUIPainter& P,float X,float Y,float W,float H,FLinearColor Rarity,bool bHover,bool bSelected,bool bDim)
{
    const float C=FMath::Clamp(FMath::Min(W,H)*.14f,4.f,11.f);
    const float GlowK=HasThemeArt()?CireUITheme::Active()->GlowStrength:1.f;
    // Rarity glow behind the card (stronger on hover / selection), then a soft drop shadow.
    Glow(P,X+2,Y+2,W-4,H-4,Rarity*FLinearColor(1,1,1,(bSelected?.5f:bHover?.42f:bDim?.08f:.2f)*GlowK));
    Bevel(P,X+2,Y+4,W,H,C,FLinearColor(0,0,0,.5f));
    // Rim: rarity metal, a dark gap, then the body with a rarity wash fading down.
    const FLinearColor Rim=Rarity*FLinearColor(bDim?.45f:1.f,bDim?.45f:1.f,bDim?.45f:1.f,1);
    Bevel(P,X,Y,W,H,C,Rim);
    Bevel(P,X+1.6f,Y+1.6f,W-3.2f,H-3.2f,C-.6f,FLinearColor(0,0,0,.95f));
    Bevel(P,X+2.6f,Y+2.6f,W-5.2f,H-5.2f,C-1.f,FLinearColor(Ink.R*1.3f+.01f,Ink.G*1.3f+.01f,Ink.B*1.3f+.015f,1));
    const float BX=X+2.6f+C,BW=W-5.2f-2*C;
    for(int32 I=0;I<5;++I)P.Rect(BX-C*.5f,Y+3+I*H*.1f,BW+C,H*.1f,Rarity*FLinearColor(1,1,1,(bDim?.05f:.16f)-I*.028f));
    P.Line(X+C,Y+2.8f,X+W-C,Y+2.8f,FLinearColor(1,1,1,bDim?.05f:.14f),1.f);
    if(bHover||bSelected)BevelOutline(P,X-1.5f,Y-1.5f,W+3,H+3,C+.6f,(bSelected?FLinearColor(1.f,.85f,.45f,1):Rarity*1.3f)*FLinearColor(1,1,1,.9f),1.4f);
}
