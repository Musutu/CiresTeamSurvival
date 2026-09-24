#include "CireKeybindings.h"
#include "CireGame.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ConfigCacheIni.h"

#define LOCTEXT_NAMESPACE "CireKeybindings"

namespace
{
const TCHAR* Section = TEXT("CireUI.Keybindings");
const TCHAR* PlacePrefix = TEXT("Place.");
const FString EmptyPlacement = TEXT("-");

bool IsShiftKey(const FKey& K){return K==EKeys::LeftShift||K==EKeys::RightShift;}
bool IsCtrlKey(const FKey& K){return K==EKeys::LeftControl||K==EKeys::RightControl;}
bool IsAltKey(const FKey& K){return K==EKeys::LeftAlt||K==EKeys::RightAlt;}

struct FMods{bool Shift=false,Ctrl=false,Alt=false;};
FMods HeldMods(const APlayerController* PC)
{
    FMods M;
    M.Shift=PC->IsInputKeyDown(EKeys::LeftShift)||PC->IsInputKeyDown(EKeys::RightShift);
    M.Ctrl=PC->IsInputKeyDown(EKeys::LeftControl)||PC->IsInputKeyDown(EKeys::RightControl);
    M.Alt=PC->IsInputKeyDown(EKeys::LeftAlt)||PC->IsInputKeyDown(EKeys::RightAlt);
    return M;
}
/** Exact modifier match; the chord's own key never counts as its modifier class. */
bool ModsMatch(const FCireKeyChord& C,const FMods& M,bool bExact)
{
    const bool Shift=M.Shift&&!IsShiftKey(C.Key),Ctrl=M.Ctrl&&!IsCtrlKey(C.Key),Alt=M.Alt&&!IsAltKey(C.Key);
    if(!bExact&&!C.bShift&&!C.bCtrl&&!C.bAlt)return true;
    return Shift==C.bShift&&Ctrl==C.bCtrl&&Alt==C.bAlt;
}

TArray<FCireActionInfo> BuildActions()
{
    TArray<FCireActionInfo> A;
    auto Add=[&A](const TCHAR* Id,FText Name,ECireBindCategory Cat,FCireKeyChord P,FCireKeyChord S=FCireKeyChord())
    {auto& I=A.AddDefaulted_GetRef();I.Id=Id;I.DisplayName=MoveTemp(Name);I.Category=Cat;I.Default[0]=P;I.Default[1]=S;};
    using C=ECireBindCategory;
    Add(TEXT("MoveForward"),LOCTEXT("MoveForward","Move forward"),C::Movement,EKeys::W,EKeys::Up);
    Add(TEXT("MoveBackward"),LOCTEXT("MoveBackward","Move backward"),C::Movement,EKeys::S,EKeys::Down);
    Add(TEXT("TurnLeft"),LOCTEXT("TurnLeft","Turn left (strafe while right mouse held)"),C::Movement,EKeys::A);
    Add(TEXT("TurnRight"),LOCTEXT("TurnRight","Turn right (strafe while right mouse held)"),C::Movement,EKeys::D);
    Add(TEXT("StrafeLeft"),LOCTEXT("StrafeLeft","Strafe left"),C::Movement,EKeys::Q);
    Add(TEXT("StrafeRight"),LOCTEXT("StrafeRight","Strafe right"),C::Movement,EKeys::E);
    Add(TEXT("Jump"),LOCTEXT("Jump","Jump"),C::Movement,EKeys::SpaceBar);
    Add(TEXT("ToggleAutoRun"),LOCTEXT("ToggleAutoRun","Toggle autorun"),C::Movement,EKeys::NumLock);
    Add(TEXT("ToggleWalk"),LOCTEXT("ToggleWalk","Toggle walk / run"),C::Movement,EKeys::CapsLock);
    Add(TEXT("DodgeRoll"),LOCTEXT("DodgeRoll","Dodge roll"),C::Movement,EKeys::LeftControl,EKeys::RightControl);
    Add(TEXT("ToggleAutoAttack"),LOCTEXT("ToggleAutoAttack","Toggle auto attack"),C::Combat,EKeys::T);
    Add(TEXT("TargetNextEnemy"),LOCTEXT("TargetNextEnemy","Target nearest / next enemy"),C::Targeting,EKeys::Tab);
    Add(TEXT("TargetPreviousEnemy"),LOCTEXT("TargetPreviousEnemy","Target previous enemy"),C::Targeting,FCireKeyChord(EKeys::Tab,true));
    Add(TEXT("TargetNextAlly"),LOCTEXT("TargetNextAlly","Target next ally"),C::Targeting,EKeys::F);
    Add(TEXT("TargetSelf"),LOCTEXT("TargetSelf","Target self"),C::Targeting,EKeys::F1);
    Add(TEXT("OpenChat"),LOCTEXT("OpenChat","Open chat"),C::Interface,EKeys::Enter);
    Add(TEXT("ToggleShop"),LOCTEXT("ToggleShop","Town shop"),C::Interface,EKeys::B);
    Add(TEXT("ToggleHelp"),LOCTEXT("ToggleHelp","Controls help"),C::Interface,EKeys::H);
    Add(TEXT("RecallToTown"),LOCTEXT("RecallToTown","Recall to town"),C::Interface,EKeys::G);
    Add(TEXT("ToggleOptions"),LOCTEXT("ToggleOptions","Options"),C::Interface,EKeys::F9);
    Add(TEXT("ToggleLayoutEditor"),LOCTEXT("ToggleLayoutEditor","Edit HUD layout"),C::Interface,EKeys::F10);
    Add(TEXT("ToggleDeveloperTools"),LOCTEXT("ToggleDeveloperTools","Developer tools"),C::Interface,EKeys::F8);
    Add(TEXT("RosterPreviousPage"),LOCTEXT("RosterPreviousPage","Champion roster: previous page"),C::Interface,EKeys::PageUp,EKeys::Left);
    Add(TEXT("RosterNextPage"),LOCTEXT("RosterNextPage","Champion roster: next page"),C::Interface,EKeys::PageDown,EKeys::Right);
    Add(TEXT("ToggleSkillOffer"),LOCTEXT("ToggleSkillOffer","New skill choice: open / decide later"),C::Interface,EKeys::N);
    const FKey Digits[]={EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Six,EKeys::Seven,EKeys::Eight,EKeys::Nine,EKeys::Zero};
    for(int32 Bar=1;Bar<=FCireKeybindings::NumBars;++Bar)for(int32 Slot=1;Slot<=FCireKeybindings::SlotsPerBar;++Slot)
    {
        FCireKeyChord Key;
        if(Bar==1)
        {
            if(Slot<=6)Key=FCireKeyChord(Digits[Slot-1]);
            else if(Slot==8)Key=FCireKeyChord(EKeys::R);
            else if(Slot>=9)Key=FCireKeyChord(Digits[Slot-3]); // 7,8,9,0
        }
        else if(Slot<=6)Key=FCireKeyChord(Digits[Slot-1],Bar==2,false,Bar==3);
        const FText Name=Bar==1&&Slot==7?LOCTEXT("PassiveSlot","Action bar 1 slot 7 (passive)"):Bar==1&&Slot==8?LOCTEXT("UltimateSlot","Action bar 1 slot 8 (ultimate)"):
            FText::Format(LOCTEXT("SlotName","Action bar {0} slot {1}"),Bar,Slot);
        auto& I=A.AddDefaulted_GetRef();I.Id=CireKeybindings::SlotAction(Bar,Slot);I.DisplayName=Name;
        I.Category=static_cast<ECireBindCategory>(static_cast<uint8>(ECireBindCategory::ActionBar1)+Bar-1);I.Default[0]=Key;
    }
    return A;
}
FString ShortKey(const FKey& K)
{
    static const TMap<FKey,FString> Names={
        {EKeys::One,TEXT("1")},{EKeys::Two,TEXT("2")},{EKeys::Three,TEXT("3")},{EKeys::Four,TEXT("4")},{EKeys::Five,TEXT("5")},
        {EKeys::Six,TEXT("6")},{EKeys::Seven,TEXT("7")},{EKeys::Eight,TEXT("8")},{EKeys::Nine,TEXT("9")},{EKeys::Zero,TEXT("0")},
        {EKeys::SpaceBar,TEXT("Spc")},{EKeys::MiddleMouseButton,TEXT("M3")},{EKeys::ThumbMouseButton,TEXT("M4")},{EKeys::ThumbMouseButton2,TEXT("M5")},
        {EKeys::LeftControl,TEXT("Ctrl")},{EKeys::RightControl,TEXT("RCtrl")},{EKeys::LeftShift,TEXT("Shift")},{EKeys::RightShift,TEXT("RShift")},
        {EKeys::LeftAlt,TEXT("Alt")},{EKeys::RightAlt,TEXT("RAlt")},{EKeys::CapsLock,TEXT("Caps")},{EKeys::Enter,TEXT("Enter")},{EKeys::Tab,TEXT("Tab")},
        {EKeys::Escape,TEXT("Esc")},{EKeys::PageUp,TEXT("PgUp")},{EKeys::PageDown,TEXT("PgDn")},{EKeys::NumLock,TEXT("NmLk")},
        {EKeys::Hyphen,TEXT("-")},{EKeys::Equals,TEXT("=")},{EKeys::Up,TEXT("Up")},{EKeys::Down,TEXT("Down")},{EKeys::Left,TEXT("Left")},{EKeys::Right,TEXT("Right")},
        {EKeys::Insert,TEXT("Ins")},{EKeys::Home,TEXT("Home")},{EKeys::End,TEXT("End")},{EKeys::Tilde,TEXT("`")},
        {EKeys::NumPadZero,TEXT("N0")},{EKeys::NumPadOne,TEXT("N1")},{EKeys::NumPadTwo,TEXT("N2")},{EKeys::NumPadThree,TEXT("N3")},{EKeys::NumPadFour,TEXT("N4")},
        {EKeys::NumPadFive,TEXT("N5")},{EKeys::NumPadSix,TEXT("N6")},{EKeys::NumPadSeven,TEXT("N7")},{EKeys::NumPadEight,TEXT("N8")},{EKeys::NumPadNine,TEXT("N9")}};
    if(const FString* S=Names.Find(K))return *S;
    return K.GetDisplayName(false).ToString();
}
}

// ---------------------------------------------------------------------------------------------
FString FCireKeyChord::ToString() const
{
    if(!IsBound())return FString();
    FString S;if(bCtrl)S+=TEXT("Ctrl+");if(bAlt)S+=TEXT("Alt+");if(bShift)S+=TEXT("Shift+");
    return S+Key.GetFName().ToString();
}
bool FCireKeyChord::Parse(const FString& Text,FCireKeyChord& Out)
{
    Out=FCireKeyChord();FString Rest=Text.TrimStartAndEnd();if(Rest.IsEmpty())return true;
    for(;;)
    {
        if(Rest.StartsWith(TEXT("Ctrl+"))){Out.bCtrl=true;Rest.RightChopInline(5);}
        else if(Rest.StartsWith(TEXT("Alt+"))){Out.bAlt=true;Rest.RightChopInline(4);}
        else if(Rest.StartsWith(TEXT("Shift+"))){Out.bShift=true;Rest.RightChopInline(6);}
        else break;
    }
    const FKey Key(*Rest);
    if(!Key.IsValid()||!FCireKeybindings::IsBindableKey(Key)){Out=FCireKeyChord();return false;}
    Out.Key=Key;return true;
}
FString FCireKeyChord::ShortLabel() const
{
    if(!IsBound())return FString();
    FString S;if(bCtrl)S+=TEXT("C-");if(bAlt)S+=TEXT("A-");if(bShift)S+=TEXT("S-");
    return S+ShortKey(Key);
}
FString FCireKeyChord::LongLabel() const
{
    if(!IsBound())return FString();
    FString S;if(bCtrl)S+=TEXT("Ctrl+");if(bAlt)S+=TEXT("Alt+");if(bShift)S+=TEXT("Shift+");
    const FString Name=Key==EKeys::ThumbMouseButton?TEXT("Mouse 4"):Key==EKeys::ThumbMouseButton2?TEXT("Mouse 5"):Key==EKeys::MiddleMouseButton?TEXT("Mouse 3"):Key.GetDisplayName().ToString();
    return S+Name;
}

// ---------------------------------------------------------------------------------------------
const TArray<FCireActionInfo>& CireKeybindings::Actions(){static const TArray<FCireActionInfo> List=BuildActions();return List;}
const FCireActionInfo* CireKeybindings::Find(FName Action){return Actions().FindByPredicate([Action](const FCireActionInfo& I){return I.Id==Action;});}
FName CireKeybindings::SlotAction(int32 Bar,int32 Slot){return FName(*FString::Printf(TEXT("ActionBar%d_Slot%d"),Bar,Slot));}
bool CireKeybindings::ParseSlotAction(FName Action,int32& OutBar,int32& OutSlot)
{
    const FString S=Action.ToString();
    if(!S.StartsWith(TEXT("ActionBar"))||S.Len()<16)return false;
    int32 Under=INDEX_NONE;if(!S.FindChar(TEXT('_'),Under))return false;
    OutBar=FCString::Atoi(*S.Mid(9,Under-9));OutSlot=FCString::Atoi(*S.Mid(Under+5));
    return S.Mid(Under,5)==TEXT("_Slot")&&OutBar>=1&&OutBar<=FCireKeybindings::NumBars&&OutSlot>=1&&OutSlot<=FCireKeybindings::SlotsPerBar;
}
FText CireKeybindings::CategoryName(ECireBindCategory C)
{
    switch(C)
    {
    case ECireBindCategory::Movement:return LOCTEXT("CatMovement","Movement");
    case ECireBindCategory::Combat:return LOCTEXT("CatCombat","Combat");
    case ECireBindCategory::Targeting:return LOCTEXT("CatTargeting","Targeting");
    case ECireBindCategory::Interface:return LOCTEXT("CatInterface","Interface");
    case ECireBindCategory::ActionBar1:return LOCTEXT("CatBar1","Action bar 1");
    case ECireBindCategory::ActionBar2:return LOCTEXT("CatBar2","Action bar 2");
    default:return LOCTEXT("CatBar3","Action bar 3");
    }
}
const FCireKeybindings& CireKeybindings::Defaults(){static const FCireKeybindings D;return D;}

FString CireKeybindings::SlotAbilityId(const FCireKeybindings& B,const ACireHero& Hero,FName Slot)
{
    bool bExplicit=false;const FString Placed=B.SlotAssignment(Hero.ChampionProfileId,Slot,bExplicit);
    if(bExplicit)return Placed;
    int32 Bar=0,Index=0;if(!ParseSlotAction(Slot,Bar,Index)||Bar!=1)return FString();
    // Automatic bar-1 layout: actives in learn order, then passive, then ultimate.
    int32 Skill=INDEX_NONE;
    if(Index<=6)Skill=Hero.ActiveSkillSlot(Index-1);
    else if(Index==7){for(int32 I=0;I<Hero.Skills.Num();++I)if(ACireHero::IsPassive(Hero.Skills[I])){Skill=I;break;}}
    else if(Index==8)Skill=Hero.UltimateSkillSlot();
    return Hero.Skills.IsValidIndex(Skill)?Hero.Skills[Skill]:FString();
}
int32 CireKeybindings::ResolveSlot(const FCireKeybindings& B,const ACireHero& Hero,FName Slot)
{
    const FString Id=SlotAbilityId(B,Hero,Slot);
    if(Id.IsEmpty()||ACireHero::IsPassive(Id))return INDEX_NONE;
    return Hero.Skills.IndexOfByKey(Id);
}

// ---------------------------------------------------------------------------------------------
FCireKeybindings::FCireKeybindings(){ResetToDefaults();}
void FCireKeybindings::ResetToDefaults()
{
    Map.Reset();
    for(const auto& I:CireKeybindings::Actions()){TStaticArray<FCireKeyChord,2> V;V[0]=I.Default[0];V[1]=I.Default[1];Map.Add(I.Id,V);}
}
bool FCireKeybindings::IsDefault() const
{
    for(const auto& I:CireKeybindings::Actions())if(Get(I.Id,0)!=I.Default[0]||Get(I.Id,1)!=I.Default[1])return false;
    return true;
}
const FCireKeyChord& FCireKeybindings::Get(FName Action,int32 Index) const
{
    static const FCireKeyChord None;
    const auto* V=Map.Find(Action);return V&&(Index==0||Index==1)?(*V)[Index]:None;
}
FName FCireKeybindings::FindConflict(const FCireKeyChord& Chord,FName IgnoreAction,int32 IgnoreIndex,int32* OutIndex) const
{
    if(!Chord.IsBound())return NAME_None;
    for(const auto& I:CireKeybindings::Actions())for(int32 K=0;K<2;++K)
    {
        if(I.Id==IgnoreAction&&(IgnoreIndex==INDEX_NONE||IgnoreIndex==K))continue;
        if(Get(I.Id,K)==Chord){if(OutIndex)*OutIndex=K;return I.Id;}
    }
    return NAME_None;
}
FCireBindResult FCireKeybindings::Bind(FName Action,int32 Index,const FCireKeyChord& Chord,ECireBindPolicy Policy)
{
    FCireBindResult R;auto* V=Map.Find(Action);
    if(!V||(Index!=0&&Index!=1)||(Chord.IsBound()&&!IsBindableKey(Chord.Key)))return R;
    const FCireKeyChord Previous=(*V)[Index];
    if(Previous==Chord)return R;
    int32 OtherIndex=INDEX_NONE;const FName Other=FindConflict(Chord,Action,Index,&OtherIndex);
    if(!Other.IsNone())
    {
        auto& O=Map[Other];R.ConflictAction=Other;R.ConflictIndex=OtherIndex;
        // Never duplicate within the other action: swapping onto an identical chord just unbinds.
        const bool bSwap=Policy==ECireBindPolicy::Swap&&Previous.IsBound()&&O[1-OtherIndex]!=Previous;
        O[OtherIndex]=bSwap?Previous:FCireKeyChord();R.ConflictNowBound=O[OtherIndex];
    }
    (*V)[Index]=Chord;
    // The same action never keeps one chord twice.
    if(Chord.IsBound()&&(*V)[1-Index]==Chord)(*V)[1-Index]=FCireKeyChord();
    R.bChanged=true;return R;
}
void FCireKeybindings::Unbind(FName Action,int32 Index){if(auto* V=Map.Find(Action);V&&(Index==0||Index==1))(*V)[Index]=FCireKeyChord();}
FString FCireKeybindings::Label(FName Action) const
{
    const auto& P=Get(Action,0);return P.IsBound()?P.ShortLabel():Get(Action,1).ShortLabel();
}
FString FCireKeybindings::FullLabel(FName Action) const
{
    const FString A=Get(Action,0).LongLabel(),B=Get(Action,1).LongLabel();
    return A.IsEmpty()?B:B.IsEmpty()?A:A+TEXT(" / ")+B;
}

bool FCireKeybindings::WasPressed(const APlayerController* PC,FName Action) const
{
    if(!PC)return false;const FMods M=HeldMods(PC);
    for(int32 K=0;K<2;++K){const auto& C=Get(Action,K);if(C.IsBound()&&PC->WasInputKeyJustPressed(C.Key)&&ModsMatch(C,M,true))return true;}
    return false;
}
bool FCireKeybindings::WasReleased(const APlayerController* PC,FName Action) const
{
    if(!PC)return false;
    for(int32 K=0;K<2;++K){const auto& C=Get(Action,K);if(C.IsBound()&&PC->WasInputKeyJustReleased(C.Key))return true;}
    return false;
}
bool FCireKeybindings::IsDown(const APlayerController* PC,FName Action) const
{
    if(!PC)return false;const FMods M=HeldMods(PC);
    for(int32 K=0;K<2;++K){const auto& C=Get(Action,K);if(C.IsBound()&&PC->IsInputKeyDown(C.Key)&&ModsMatch(C,M,false))return true;}
    return false;
}

// ---------------------------------------------------------------------------------------------
bool FCireKeybindings::IsBindableKey(const FKey& K)
{
    if(!K.IsValid()||K==EKeys::AnyKey||K==EKeys::Escape||K==EKeys::BackSpace||K==EKeys::Delete)return false;
    if(K==EKeys::LeftMouseButton||K==EKeys::RightMouseButton||K==EKeys::MouseScrollUp||K==EKeys::MouseScrollDown)return false;
    if(K.IsGamepadKey()||K.IsTouch()||K.IsGesture()||K.IsAxis1D()||K.IsAxis2D()||K.IsAxis3D()||K.IsButtonAxis()||K.IsDeprecated())return false;
    const FName Category=K.GetMenuCategory();
    return Category==EKeys::NAME_KeyboardCategory||Category==EKeys::NAME_MouseCategory;
}
void FCireKeybindings::BeginCapture(FName Action,int32 Index)
{
    CaptureActionId=Map.Contains(Action)&&(Index==0||Index==1)?Action:NAME_None;CaptureSlot=Index;PendingModifier=FKey();
}
void FCireKeybindings::CancelCapture(){CaptureActionId=NAME_None;PendingModifier=FKey();}
FCireCaptureResult FCireKeybindings::ApplyCapturedKey(const FKey& Key,bool bShift,bool bCtrl,bool bAlt,ECireBindPolicy Policy)
{
    FCireCaptureResult R;R.Action=CaptureActionId;R.Index=CaptureSlot;
    if(!IsCapturing())return R;
    if(Key==EKeys::Escape){R.Kind=FCireCaptureResult::Cancelled;CancelCapture();LastResult=R;return R;}
    if(Key==EKeys::BackSpace||Key==EKeys::Delete){Unbind(CaptureActionId,CaptureSlot);R.Kind=FCireCaptureResult::Unbound;CancelCapture();LastResult=R;return R;}
    if(!IsBindableKey(Key)){R.Kind=FCireCaptureResult::Rejected;return R;}
    R.Chord=FCireKeyChord(Key,bShift&&!IsShiftKey(Key),bCtrl&&!IsCtrlKey(Key),bAlt&&!IsAltKey(Key));
    R.Bind=Bind(CaptureActionId,CaptureSlot,R.Chord,Policy);R.Kind=FCireCaptureResult::Bound;CancelCapture();
    LastResult=R;return R;
}
bool FCireKeybindings::TickCapture(const APlayerController* PC,FCireCaptureResult& Out,ECireBindPolicy Policy)
{
    Out=FCireCaptureResult();
    if(!IsCapturing()||!PC)return false;
    static TArray<FKey> Keys;
    if(Keys.IsEmpty()){TArray<FKey> All;EKeys::GetAllKeys(All);for(const FKey& K:All)if(IsBindableKey(K)||K==EKeys::Escape||K==EKeys::BackSpace||K==EKeys::Delete)Keys.Add(K);}
    const FMods M=HeldMods(PC);
    for(const FKey& K:Keys)
    {
        if(!PC->WasInputKeyJustPressed(K))continue;
        if(K.IsModifierKey()){PendingModifier=K;continue;}
        Out=ApplyCapturedKey(K,M.Shift,M.Ctrl,M.Alt,Policy);
        if(Out.Kind!=FCireCaptureResult::Rejected)return true;
    }
    // A modifier pressed and released on its own binds by itself (e.g. Ctrl = dodge roll).
    if(PendingModifier.IsValid()&&PC->WasInputKeyJustReleased(PendingModifier))
    {Out=ApplyCapturedKey(PendingModifier,false,false,false,Policy);return true;}
    return true;
}

// ---------------------------------------------------------------------------------------------
void FCireKeybindings::AssignSlot(const FString& ProfileId,FName Slot,const FString& AbilityId)
{
    int32 Bar,Index;if(!CireKeybindings::ParseSlotAction(Slot,Bar,Index))return;
    Placements.FindOrAdd(ProfileId).Add(Slot,AbilityId.IsEmpty()?EmptyPlacement:AbilityId);
}
void FCireKeybindings::ClearSlot(const FString& ProfileId,FName Slot){AssignSlot(ProfileId,Slot,FString());}
void FCireKeybindings::ResetSlot(const FString& ProfileId,FName Slot)
{
    if(auto* P=Placements.Find(ProfileId)){P->Remove(Slot);if(P->IsEmpty())Placements.Remove(ProfileId);}
}
FString FCireKeybindings::SlotAssignment(const FString& ProfileId,FName Slot,bool& bOutExplicit) const
{
    bOutExplicit=false;const auto* P=Placements.Find(ProfileId);const FString* V=P?P->Find(Slot):nullptr;
    if(!V)return FString();bOutExplicit=true;return *V==EmptyPlacement?FString():*V;
}

// ---------------------------------------------------------------------------------------------
void FCireKeybindings::LoadFrom(const FConfigFile& Config)
{
    ResetToDefaults();Placements.Reset();
    const FConfigSection* S=Config.FindSection(Section);
    if(!S)return; // UI schema <= 3 profiles: WoW defaults
    FString VersionText;Config.GetString(Section,TEXT("KeybindingsVersion"),VersionText);
    const int32 Version=FCString::Atoi(*VersionText);
    if(Version<1||Version>SchemaVersion)return;
    for(const auto& Pair:*S)
    {
        const FString Key=Pair.Key.ToString(),Value=Pair.Value.GetValue();
        if(Key.StartsWith(PlacePrefix))
        {
            // Place.<profile>.<slot action>
            const FString Rest=Key.RightChop(FCString::Strlen(PlacePrefix));int32 Dot=INDEX_NONE;
            if(!Rest.FindLastChar(TEXT('.'),Dot))continue;
            const FString Profile=Rest.Left(Dot);const FName Slot(*Rest.Mid(Dot+1));int32 Bar,Index;
            if(Profile.IsEmpty()||Profile.Len()>64||!CireKeybindings::ParseSlotAction(Slot,Bar,Index)||Value.Len()>64)continue;
            Placements.FindOrAdd(Profile).Add(Slot,Value.IsEmpty()?EmptyPlacement:Value);
            continue;
        }
        auto* V=Map.Find(Pair.Key);if(!V)continue;
        FString A,B;if(!Value.Split(TEXT("|"),&A,&B)){A=Value;B.Reset();}
        FCireKeyChord P,Q;
        if(FCireKeyChord::Parse(A,P)&&FCireKeyChord::Parse(B,Q)){(*V)[0]=P;(*V)[1]=Q;}
    }
    // Repair duplicates from hand-edited files: the first action in display order keeps the chord.
    TSet<FString> Seen;
    for(const auto& I:CireKeybindings::Actions())for(int32 K=0;K<2;++K)
    {
        auto& C=Map[I.Id][K];if(!C.IsBound())continue;
        const FString Id=C.ToString();if(Seen.Contains(Id))C=FCireKeyChord();else Seen.Add(Id);
    }
}
void FCireKeybindings::SaveTo(FConfigFile& Config) const
{
    Config.SetString(Section,TEXT("KeybindingsVersion"),*FString::FromInt(SchemaVersion));
    for(const auto& I:CireKeybindings::Actions())
        Config.SetString(Section,*I.Id.ToString(),*(Get(I.Id,0).ToString()+TEXT("|")+Get(I.Id,1).ToString()));
    for(const auto& P:Placements)for(const auto& Slot:P.Value)
        Config.SetString(Section,*(FString(PlacePrefix)+P.Key+TEXT(".")+Slot.Key.ToString()),*Slot.Value);
}

#undef LOCTEXT_NAMESPACE

#if !UE_BUILD_SHIPPING
#include "CireUISettings.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
bool CireKeybindings::RunSmoke()
{
    bool Pass=true;int32 Count=0;
    auto Check=[&](bool Value,const TCHAR* Name){++Count;Pass&=Value;if(!Value)UE_LOG(LogTemp,Error,TEXT("CIRE_KEYBIND_ASSERT %s"),Name);};
    FCireKeybindings B;
    // --- WoW defaults ---
    Check(B.Get(TEXT("StrafeLeft"),0)==FCireKeyChord(EKeys::Q)&&B.Get(TEXT("StrafeRight"),0)==FCireKeyChord(EKeys::E),TEXT("Q/E strafe"));
    Check(B.Get(TEXT("Jump"),0)==FCireKeyChord(EKeys::SpaceBar)&&B.Get(TEXT("ToggleAutoAttack"),0)==FCireKeyChord(EKeys::T),TEXT("Space jump, T auto attack"));
    Check(B.Get(SlotAction(1,8),0)==FCireKeyChord(EKeys::R)&&B.Get(TEXT("RecallToTown"),0)==FCireKeyChord(EKeys::G),TEXT("R ultimate (bar 1 slot 8), recall moved to G"));
    Check(B.Get(TEXT("TurnLeft"),0)==FCireKeyChord(EKeys::A)&&B.Get(TEXT("TargetPreviousEnemy"),0)==FCireKeyChord(EKeys::Tab,true),TEXT("A turn, Shift+Tab previous enemy"));
    Check(B.Get(SlotAction(1,1),0)==FCireKeyChord(EKeys::One)&&B.Get(SlotAction(2,3),0)==FCireKeyChord(EKeys::Three,true)&&
        B.Get(SlotAction(3,6),0)==FCireKeyChord(EKeys::Six,false,false,true)&&!B.Get(SlotAction(1,7),0).IsBound()&&!B.Get(SlotAction(3,12),0).IsBound(),TEXT("action bar defaults"));
    Check(Actions().Num()==25+FCireKeybindings::NumBars*FCireKeybindings::SlotsPerBar,TEXT("action list size"));
    {
        TSet<FString> Seen;bool Unique=true;
        for(const auto& I:Actions())for(int32 K=0;K<2;++K)if(I.Default[K].IsBound()){const FString Id=I.Default[K].ToString();Unique&=!Seen.Contains(Id);Seen.Add(Id);}
        Check(Unique,TEXT("default layout has no key collisions"));
    }
    // --- labels ---
    Check(B.Label(SlotAction(2,1))==TEXT("S-1")&&B.Label(SlotAction(3,2))==TEXT("A-2")&&B.Label(TEXT("Jump"))==TEXT("Spc")&&B.Label(TEXT("TargetPreviousEnemy"))==TEXT("S-Tab"),TEXT("short labels"));
    Check(FCireKeyChord(EKeys::Two,false,true).ShortLabel()==TEXT("C-2")&&FCireKeyChord(EKeys::ThumbMouseButton).ShortLabel()==TEXT("M4")&&
        FCireKeyChord(EKeys::Tab,true).LongLabel()==TEXT("Shift+Tab"),TEXT("modifier and mouse labels"));
    FCireKeyChord Parsed;
    Check(FCireKeyChord::Parse(TEXT("Ctrl+Shift+Two"),Parsed)&&Parsed==FCireKeyChord(EKeys::Two,true,true)&&FCireKeyChord::Parse(FCireKeyChord(EKeys::Tab,true).ToString(),Parsed)&&Parsed==FCireKeyChord(EKeys::Tab,true),TEXT("chord text roundtrip"));
    Check(!FCireKeyChord::Parse(TEXT("NotAKey"),Parsed)&&!FCireKeyChord::Parse(TEXT("LeftMouseButton"),Parsed)&&!FCireKeyChord::Parse(TEXT("Escape"),Parsed),TEXT("invalid/reserved keys rejected"));
    // --- rebinding + conflicts ---
    auto R=B.Bind(TEXT("Jump"),0,FCireKeyChord(EKeys::E),ECireBindPolicy::Swap);
    Check(R.bChanged&&R.ConflictAction==FName(TEXT("StrafeRight"))&&B.Get(TEXT("StrafeRight"),0)==FCireKeyChord(EKeys::SpaceBar)&&B.Get(TEXT("Jump"),0)==FCireKeyChord(EKeys::E),TEXT("conflict swaps keys"));
    R=B.Bind(TEXT("Jump"),0,FCireKeyChord(EKeys::Q),ECireBindPolicy::UnbindOther);
    Check(R.ConflictAction==FName(TEXT("StrafeLeft"))&&!B.Get(TEXT("StrafeLeft"),0).IsBound(),TEXT("conflict can unbind the other action"));
    Check(B.FindConflict(FCireKeyChord(EKeys::Q))==FName(TEXT("Jump"))&&B.FindConflict(FCireKeyChord(EKeys::Q,true)).IsNone(),TEXT("conflict lookup is chord-exact"));
    B.Bind(TEXT("Jump"),1,FCireKeyChord(EKeys::Q));
    Check(B.Get(TEXT("Jump"),0)!=B.Get(TEXT("Jump"),1),TEXT("an action never holds one chord twice"));
    B.Unbind(TEXT("Jump"),0);B.Unbind(TEXT("Jump"),1);Check(!B.Get(TEXT("Jump"),0).IsBound()&&B.Label(TEXT("Jump")).IsEmpty(),TEXT("unbind"));
    Check(!B.Bind(TEXT("Jump"),0,FCireKeyChord(EKeys::LeftMouseButton)).bChanged&&!B.Bind(TEXT("NoSuchAction"),0,FCireKeyChord(EKeys::K)).bChanged,TEXT("reserved key / unknown action refused"));
    B.ResetToDefaults();Check(B.IsDefault(),TEXT("reset to defaults"));
    // --- capture ---
    B.BeginCapture(TEXT("TargetNextAlly"),1);
    auto C=B.ApplyCapturedKey(EKeys::Escape,false,false,false);Check(C.Kind==FCireCaptureResult::Cancelled&&!B.IsCapturing()&&B.IsDefault(),TEXT("Esc cancels capture"));
    B.BeginCapture(TEXT("TargetNextAlly"),1);C=B.ApplyCapturedKey(EKeys::G,true,false,false);
    Check(C.Kind==FCireCaptureResult::Bound&&B.Get(TEXT("TargetNextAlly"),1)==FCireKeyChord(EKeys::G,true)&&B.Label(TEXT("TargetNextAlly"))==TEXT("F"),TEXT("capture binds a Shift chord to the secondary"));
    B.BeginCapture(TEXT("Jump"),0);C=B.ApplyCapturedKey(EKeys::LeftShift,true,false,false);
    Check(C.Kind==FCireCaptureResult::Bound&&B.Get(TEXT("Jump"),0)==FCireKeyChord(EKeys::LeftShift),TEXT("a lone modifier binds without itself as modifier"));
    B.BeginCapture(TEXT("Jump"),0);C=B.ApplyCapturedKey(EKeys::Delete,false,false,false);
    Check(C.Kind==FCireCaptureResult::Unbound&&!B.Get(TEXT("Jump"),0).IsBound()&&B.LastCapture().Kind==FCireCaptureResult::Unbound,TEXT("Delete unbinds"));
    B.BeginCapture(TEXT("Jump"),0);C=B.ApplyCapturedKey(EKeys::LeftMouseButton,false,false,false);
    Check(C.Kind==FCireCaptureResult::Rejected&&B.IsCapturing(),TEXT("left mouse stays reserved while capturing"));B.CancelCapture();
    // --- placements + persistence through the real UI profile ---
    B.ResetToDefaults();B.Bind(TEXT("StrafeLeft"),1,FCireKeyChord(EKeys::ThumbMouseButton));B.Bind(SlotAction(2,7),0,FCireKeyChord(EKeys::Seven,false,false,true));
    B.AssignSlot(TEXT("bear"),SlotAction(2,1),TEXT("maul"));B.ClearSlot(TEXT("bear"),SlotAction(1,2));
    const FString Dir=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("OptionsTests"));IFileManager::Get().MakeDirectory(*Dir,true);
    const FString File=FPaths::Combine(Dir,TEXT("keys-")+FGuid::NewGuid().ToString()+TEXT(".ini"));
    {
        FCireUISettings S;S.Load(File);Check(S.Keybindings.IsDefault(),TEXT("new profile gets WoW defaults"));
        S.Keybindings=B;Check(S.Save(),TEXT("profile save"));
        FCireUISettings L;L.Load(File);
        Check(L.Keybindings.Get(TEXT("StrafeLeft"),1)==FCireKeyChord(EKeys::ThumbMouseButton)&&L.Keybindings.Get(SlotAction(2,7),0)==FCireKeyChord(EKeys::Seven,false,false,true),TEXT("bindings roundtrip"));
        bool bExplicit=false;
        Check(L.Keybindings.SlotAssignment(TEXT("bear"),SlotAction(2,1),bExplicit)==TEXT("maul")&&bExplicit,TEXT("slot placement roundtrip"));
        Check(L.Keybindings.SlotAssignment(TEXT("bear"),SlotAction(1,2),bExplicit).IsEmpty()&&bExplicit,TEXT("explicitly empty slot roundtrip"));
        L.Keybindings.SlotAssignment(TEXT("bear"),SlotAction(1,3),bExplicit);Check(!bExplicit,TEXT("untouched slot keeps automatic default"));
    }
    // Existing schema-3 profile with no keybinding section: preferences kept, WoW defaults supplied.
    FFileHelper::SaveStringToFile(TEXT("[CireUI.Preferences]\nVersion=3\nbShowChat=False\nCameraDistance=950\n"),*File);
    {FCireUISettings M;M.Load(File);Check(M.Keybindings.IsDefault()&&!M.bShowChat&&M.CameraDistance==950,TEXT("schema 3 profile migrates to default keybindings"));}
    // Damaged/hand-edited entries fall back per action; duplicates are repaired; future versions ignored.
    FFileHelper::SaveStringToFile(TEXT("[CireUI.Preferences]\nVersion=3\n[CireUI.Keybindings]\nKeybindingsVersion=1\nJump=Banana|\nStrafeLeft=F|\nToggleShop=Shift+K|Z\n"),*File);
    {
        FCireUISettings M;M.Load(File);
        Check(M.Keybindings.Get(TEXT("Jump"),0)==FCireKeyChord(EKeys::SpaceBar),TEXT("unparsable chord keeps default"));
        Check(M.Keybindings.Get(TEXT("ToggleShop"),0)==FCireKeyChord(EKeys::K,true)&&M.Keybindings.Get(TEXT("ToggleShop"),1)==FCireKeyChord(EKeys::Z),TEXT("custom chords load"));
        Check(M.Keybindings.Get(TEXT("StrafeLeft"),0)==FCireKeyChord(EKeys::F)&&!M.Keybindings.Get(TEXT("TargetNextAlly"),0).IsBound(),TEXT("duplicate chord repaired on load"));
    }
    FFileHelper::SaveStringToFile(TEXT("[CireUI.Preferences]\nVersion=3\n[CireUI.Keybindings]\nKeybindingsVersion=99\nJump=K|\n"),*File);
    {FCireUISettings M;M.Load(File);Check(M.Keybindings.IsDefault(),TEXT("future keybinding schema ignored safely"));}
    IFileManager::Get().Delete(*File);
    UE_LOG(LogTemp,Display,TEXT("CIRE_KEYBINDINGS_%s checks=%d actions=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Count,Actions().Num());
    return Pass;
}
#endif
