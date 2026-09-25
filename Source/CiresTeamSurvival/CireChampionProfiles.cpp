#include "CireChampionProfiles.h"
#include "CireScalingKits.h" // scaling-kits
#include "CireChampionRoster.h"
#include "CireRollSkills.h" // champion-draft: dodge-roll skills
#include "CireGame.h"
#include "CireSkillTuning.h"
#include "CireSummon.h"
#include "CireThreat.h"
#include "CireAbilityLibrary.h"
#include "CireRoleSkills.h"
#include "CirePets.h" // pets
#include "Engine/World.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireChampionProfiles,Log,All);

const TCHAR* CireChampionProfiles::LegacyProfileId(int32 Choice)
{
    static const TCHAR* Ids[]={TEXT("knight"),TEXT("ranger"),TEXT("scholar"),TEXT("lancer"),TEXT("summoner")};
    return Choice>=0&&Choice<UE_ARRAY_COUNT(Ids)?Ids[Choice]:nullptr;
}

Cires::RoleMask CireChampionProfiles::RoleMaskFromNames(const TArray<FString>& Roles)
{
    Cires::RoleMask Mask=Cires::RoleNone;
    for(const FString& Role:Roles)
        Mask|=Role==TEXT("tank")?Cires::RoleTank:Role==TEXT("damage")?Cires::RoleDamage:
            (Role==TEXT("healer")||Role==TEXT("support"))?Cires::RoleSupport:Cires::RoleNone;
    return Mask;
}

static Cires::SkillDraftRole RoleFromThreat(const FString& ThreatRole,const TArray<FString>& Roles)
{
    // The threat role is the champion's primary gameplay bucket; extra roles are hybrids.
    if(ThreatRole==TEXT("tank"))return Cires::SkillDraftRole::Tank;
    if(ThreatRole==TEXT("healer"))return Cires::SkillDraftRole::Support;
    if(ThreatRole==TEXT("damage"))return Cires::SkillDraftRole::Damage;
    return Roles.Contains(TEXT("support"))||Roles.Contains(TEXT("healer"))?Cires::SkillDraftRole::Support:Cires::SkillDraftRole::Damage;
}

Cires::SkillDraftRole CireChampionProfiles::PrimaryRole(const FCireChampionProfile& Profile)
{
    return RoleFromThreat(Profile.ThreatRole,Profile.Roles);
}

Cires::RoleMask CireChampionProfiles::ProfileRoleMask(const FCireChampionProfile& Profile)
{
    return static_cast<Cires::RoleMask>(Cires::RoleBit(PrimaryRole(Profile))|RoleMaskFromNames(Profile.Roles));
}

Cires::SkillDraftRole CireChampionProfiles::DraftRole(const ACireHero* Hero)
{
    if(!Hero)return Cires::SkillDraftRole::Any;
    if(!Hero->ChampionProfileId.IsEmpty())return RoleFromThreat(Hero->ProfileThreatRole,Hero->ProfileRoles);
    return Hero->Archetype==0?Cires::SkillDraftRole::Tank:Hero->Archetype==2?Cires::SkillDraftRole::Support:Cires::SkillDraftRole::Damage;
}

Cires::RoleMask CireChampionProfiles::SecondaryRoles(const ACireHero* Hero)
{
    if(!Hero||Hero->ChampionProfileId.IsEmpty())return Cires::RoleNone;
    return static_cast<Cires::RoleMask>(RoleMaskFromNames(Hero->ProfileRoles)&~Cires::RoleBit(DraftRole(Hero))&Cires::RoleAll);
}

const ACireHero* CireChampionProfiles::PickedByTeammate(const ACireHero* Hero,const FString& ProfileId,bool bHumansOnly)
{
    if(!Hero||!Hero->GetWorld()||ProfileId.IsEmpty()||Hero->TeamId<0)return nullptr;
    for(TActorIterator<ACireHero> It(Hero->GetWorld());It;++It)
    {
        const ACireHero* Other=*It;
        if(Other==Hero||Other->IsA<ACireSummon>()||Other->TeamId!=Hero->TeamId||!Other->bDrafted||Other->ChampionProfileId!=ProfileId)continue;
        if(bHumansOnly&&Other->bBot)continue;
        return Other;
    }
    return nullptr;
}

FString CireChampionProfiles::SkillTargeting(const FString& Id)
{
    if(CireRoleSkills::Handles(Id))return CireRoleSkills::Targeting(Id);
    if(ACireHero::IsPassive(Id))return TEXT("Self / passive");
    if(CireRollSkills::IsActive(Id))return Id==TEXT("tumble_strike")?TEXT("Hostile / selected target (rolls to it)"):TEXT("Self / empowers your next rolls"); // champion-draft
    if(Id==TEXT("restoring_light")||Id==TEXT("purify"))return TEXT("Friendly / self fallback");
    if(Id==TEXT("sanctuary")||Id==TEXT("renewal"))return TEXT("Friendly area / centered on self");
    if(Id==TEXT("bastion_of_dawn"))return TEXT("Self heal / friendly area guard");
    if(Id==TEXT("iron_guard"))return TEXT("Self");
    if(Id==TEXT("war_cry"))return TEXT("Hostile area / centered on self");
    if(CireAbilityLibrary::Find(Id)||Id==TEXT("ember_lance")||Id==TEXT("frost_bind")||Id==TEXT("piercing_shot")||
        Id==TEXT("summoned_wall")||Id==TEXT("protection_dome")||Id==TEXT("oathbound_guardian"))return TEXT("Ground / aimed location");
    if(Id==TEXT("spectral_pack"))return TEXT("Hostile / summon at aimed ground");
    return TEXT("Hostile / selected target");
}

const FCireChampionProfile* ACireHero::ChampionProfile() const
{
    return ChampionProfileId.IsEmpty()?nullptr:CireChampionRoster::Find(ChampionProfileId);
}

bool ACireHero::DraftProfile(const FString& Id)
{
    if(!HasAuthority()||bDrafted||bDead||Id.IsEmpty()||Id.Len()>64)return false;
    const auto* Profile=CireChampionRoster::Find(Id);
    if(!Profile)return false;
    // Copy all gameplay values at selection. Reloading the catalogue cannot
    // silently alter existing champions, and clients need no matching JSON.
    ChampionProfileId=Profile->Id;Archetype=Profile->RuntimeArchetype;
    StatPrimaryOverride=Profile->PrimaryStat==TEXT("strength")?0:Profile->PrimaryStat==TEXT("agility")?1:2;
    ProfileBasicAttackRange=Profile->BasicAttackRange;ProfileAttackSeconds=Profile->AttackSeconds;
    ProfileAttackStyle=Profile->AttackStyle;ProfileThreatRole=Profile->ThreatRole;ProfileRoles=Profile->Roles;
    Progression=Cires::Progression{};
    Progression.Primary=PrimaryStat();
    Progression.DraftRole=CireChampionProfiles::DraftRole(this);
    Progression.SecondaryRoles=CireChampionProfiles::SecondaryRoles(this);
    Progression.Stats={Profile->Strength,Profile->Agility,Profile->Intelligence};
    HeroName=Profile->DisplayName;Skills.Reset();Cooldowns.Reset();Offers.Reset();CurrentOffer={};
    bDrafted=true;Recalculate(true);
    // One starting skill point: the opening offer (primary-role actives) is ready immediately;
    // bots pick theirs in BotThink, humans get the opening cards.
    Notice=TEXT("Champion bound. Choose your opening ability.");RefreshOffer();ForceNetUpdate();return true;
}

Cires::PrimaryStat ACireHero::PrimaryStat() const
{
    const int32 Primary=StatPrimaryOverride>=0&&StatPrimaryOverride<=2?StatPrimaryOverride:
        Archetype==3?1:Archetype==4?2:FMath::Clamp(Archetype,0,2);
    return static_cast<Cires::PrimaryStat>(Primary);
}
int32 ACireHero::PrimaryAttribute() const
{
    const auto Primary=PrimaryStat();
    return Primary==Cires::PrimaryStat::Strength?Strength:Primary==Cires::PrimaryStat::Agility?Agility:Intelligence;
}
float ACireHero::BasicAttackRange() const
{
    if(const auto* Summon=::Cast<ACireSummon>(this))return Summon->SummonSpec.AttackRange;
    return CireKits::BasicRange(this,ProfileBasicAttackRange>0?ProfileBasicAttackRange:Archetype==0?220.f:Archetype==1?1500.f:Archetype==3?1300.f:1200.f); // scaling-kits: range skills
}
float ACireHero::BaseAttackSeconds() const {return ProfileAttackSeconds>0?ProfileAttackSeconds:1.5f;}
FString ACireHero::BasicAttackStyle() const
{
    if(!ProfileAttackStyle.IsEmpty())return ProfileAttackStyle;
    return Archetype==0?TEXT("sword"):Archetype==1?TEXT("bow"):Archetype==3?TEXT("lance"):TEXT("arcane");
}
bool ACireHero::IsRangedBasicAttack() const
{
    // The authored roster uses 220 cm contact attacks and 1200..1500 cm targeted
    // projectiles. Summons retain their explicit attack range too.
    return BasicAttackRange()>300.f;
}
bool ACireHero::HasChampionRole(const FString& RoleName) const
{
    if(!ChampionProfileId.IsEmpty())return ProfileRoles.Contains(RoleName);
    return RoleName==(Archetype==0?TEXT("tank"):Archetype==2?TEXT("healer"):TEXT("damage"));
}
float ACireHero::DamageThreatMultiplier() const
{
    if(const auto* Pet=::Cast<ACirePet>(this))return Pet->PetThreatMultiplier(); // pets: Pets.json threatMultiplier
    const auto& T=CireSkillTuning::Get();
    const bool bTank=ChampionProfileId.IsEmpty()?Archetype==0:ProfileThreatRole==TEXT("tank");
    return bTank?T.TankDamageThreatMultiplier:T.DpsDamageThreatMultiplier;
}

bool ACireHero::LoadThematicBuild()
{
#if UE_BUILD_SHIPPING
    return false;
#else
    if(!HasAuthority()||!bDrafted||bDead||IsA<ACireSummon>())return false;
    bool bLegacy=false;
    for(int32 I=0;I<5;++I)bLegacy|=ChampionProfileId==CireChampionProfiles::LegacyProfileId(I);
    const auto* Profile=ChampionProfile();
    if(!bLegacy||!Profile||Profile->Actives.Num()!=Cires::MaxActiveSkills)return false;
    const auto Pool=Cires::StarterSkillPool(CireChampionProfiles::DraftRole(this));
    std::vector<Cires::SkillDefinition> Learned;
    TArray<FString> NewSkills;
    const auto Add=[&](const FCireChampionSkill& Skill,Cires::SkillKind Kind)
    {
        if(!Skill.IsImplemented()||NewSkills.Contains(Skill.Id))return false;
        for(const auto& Definition:Pool)
            if(Skill.Id==UTF8_TO_TCHAR(Definition.Id.c_str())&&Definition.Kind==Kind)
            {Learned.push_back(Definition);NewSkills.Add(Skill.Id);return true;}
        return false;
    };
    for(const auto& Skill:Profile->Actives)if(!Add(Skill,Cires::SkillKind::Active))return false;
    if(!Add(Profile->Passive,Cires::SkillKind::Passive)||!Add(Profile->Ultimate,Cires::SkillKind::Ultimate))return false;
    if(Progression.Level<24&&!Cires::GainLevels(Progression,24-Progression.Level))return false;
    Progression.LearnedSkills=MoveTemp(Learned);Progression.NextAugmentLevel=Cires::BreakpointForSkill(Cires::MaxSkills);
    Skills=MoveTemp(NewSkills);Cooldowns.Init(0.f,Skills.Num());Recalculate(false);
    Offers.Reset();CurrentOffer={};GlobalCooldown=0;PendingAttackTarget.Reset();BasicTimer=0;
    Notice=TEXT("Developer thematic build loaded: 6 actives, 1 passive, 1 ultimate.");ForceNetUpdate();return true;
#endif
}

#if !UE_BUILD_SHIPPING
#include "Misc/ScopeExit.h"

bool CireChampionProfiles::RunSmoke(ACireGameMode* Mode)
{
    if(!IsValid(Mode)||!Mode->HasAuthority())return false;
    bool bPassed=true;int32 Checks=0;TArray<AActor*> Actors;
    const auto Check=[&](bool bValue,const TCHAR* Why)
    {++Checks;if(!bValue){bPassed=false;UE_LOG(LogCireChampionProfiles,Error,TEXT("CIRE_CHAMPION_PROFILE_FAIL %s"),Why);}};
    ON_SCOPE_EXIT {for(auto* Actor:Actors)if(IsValid(Actor))Actor->Destroy();};
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FVector Origin(0,-2100,4500);
    const auto Make=[&]()
    {
        auto* H=Mode->GetWorld()->SpawnActor<ACireHero>(Origin,FRotator::ZeroRotator,P);
        if(H){Actors.Add(H);H->SetActorTickEnabled(false);H->SetActorEnableCollision(false);H->SetActorHiddenInGame(true);H->TeamId=0;}
        return H;
    };
    Check(CireChampionRoster::Count()>=22,TEXT("complete authored roster available"));
    for(const auto& Profile:CireChampionRoster::All())
    {
        auto* H=Make();if(!H){Check(false,TEXT("hero fixture spawned"));return false;}
        Check(!H->DraftProfile(TEXT("missing_profile"))&&!H->bDrafted&&H->ChampionProfileId.IsEmpty(),TEXT("unknown draft is transactional"));
        Check(H->DraftProfile(Profile.Id)&&H->ChampionProfileId==Profile.Id&&H->Archetype==Profile.RuntimeArchetype,TEXT("known profile selects fallback body independently"));
        Check(H->Skills.IsEmpty()&&H->Cooldowns.IsEmpty()&&H->Progression.LearnedSkills.empty(),TEXT("normal draft learns no thematic or planned skills"));
        {
            // One starting skill point: four opening actives from the primary role, no passives.
            bool bOpening=H->Offers.Num()==4&&H->CurrentOffer.BreakpointLevel==1;
            for(const FString& Id:H->Offers)bOpening&=!ACireHero::IsPassive(Id)&&!ACireHero::IsUltimate(Id)&&Cires::IsOpeningSkill(TCHAR_TO_UTF8(*Id),PrimaryRole(Profile));
            Check(bOpening,TEXT("draft grants the role-specific opening offer"));
            H->bBot=true;H->BotThink(.1f);
            Check(H->Skills.Num()==1&&H->Offers.IsEmpty()&&Cires::IsOpeningSkill(TCHAR_TO_UTF8(*H->Skills[0]),PrimaryRole(Profile)),TEXT("bots learn their opening skill"));
            H->bBot=false;H->Skills.Reset();H->Cooldowns.Reset();H->Progression.LearnedSkills.clear();H->Progression.NextAugmentLevel=1;H->Offers.Reset();H->CurrentOffer={};
        }
        Check(H->Progression.DraftRole==DraftRole(H)&&H->Progression.DraftRole!=Cires::SkillDraftRole::Any,TEXT("draft snapshots an explicit gameplay role bucket"));
        Check(H->Progression.DraftRole==PrimaryRole(Profile)&&Cires::EffectiveRoleMask(H->Progression)==ProfileRoleMask(Profile),TEXT("draft snapshots primary plus hybrid roles"));
        Check(H->Strength==Profile.Strength&&H->Agility==Profile.Agility&&H->Intelligence==Profile.Intelligence&&
            H->MaxHealth==Profile.Strength*25.f&&H->MaxMana==Profile.Intelligence*30.f,TEXT("authored base stats preserve native per-point formula"));
        Check(H->BasicAttackRange()==Profile.BasicAttackRange&&H->BaseAttackSeconds()==Profile.AttackSeconds&&
            H->BasicAttackStyle()==Profile.AttackStyle,TEXT("attack profile matches authored range timing and style"));
        Check(H->DamageThreatMultiplier()==(Profile.ThreatRole==TEXT("tank")?CireSkillTuning::Get().TankDamageThreatMultiplier:CireSkillTuning::Get().DpsDamageThreatMultiplier),TEXT("threat uses gameplay role instead of body"));
        const int32 PrimaryBefore=H->PrimaryAttribute();
        Cires::GainLevels(H->Progression);H->Recalculate(true);
        Check(H->PrimaryAttribute()==PrimaryBefore+2&&H->Level==2&&
            FMath::IsNearlyEqual(H->AttackDamage(),(12.f+H->PrimaryAttribute())*Mode->Power(0)),TEXT("level growth and basic damage use authored primary"));
        Check(!H->DraftProfile(TEXT("knight"))&&H->ChampionProfileId==Profile.Id,TEXT("redraft cannot replace profile"));
        const bool bFirstFive=Profile.Id==TEXT("knight")||Profile.Id==TEXT("ranger")||Profile.Id==TEXT("scholar")||Profile.Id==TEXT("lancer")||Profile.Id==TEXT("summoner");
        const bool bLoaded=H->LoadThematicBuild();
        Check(bLoaded==bFirstFive&&(bFirstFive?H->Skills.Num()==8&&Cires::CountSkills(H->Progression,Cires::SkillKind::Active)==6&&
            Cires::HasPassive(H->Progression)&&Cires::HasUltimate(H->Progression):H->Skills.IsEmpty()),TEXT("developer loadout is implemented-only and restricted to initial five"));
        H->Destroy();
    }
    auto* Tank=Make();auto* Bruiser=Make();auto* Troll=Make();auto* Holy=Make();
    auto* M=Mode->GetWorld()->SpawnActor<ACireMonster>(Origin,FRotator::ZeroRotator,P);
    if(!Tank||!Bruiser||!Troll||!Holy||!M){Check(false,TEXT("role comparison actors spawned"));return false;}
    Actors.Add(M);M->SetActorTickEnabled(false);M->SetActorEnableCollision(false);M->Lane=0;
    Tank->DraftProfile(TEXT("ether_golem_tank"));Bruiser->DraftProfile(TEXT("ether_golem_bruiser"));
    Troll->DraftProfile(TEXT("troll_berserker_melee"));Holy->DraftProfile(TEXT("paladin_holy"));
    CireThreat::Damage(M,Tank,10);CireThreat::Damage(M,Bruiser,10);
    Check(FMath::IsNearlyEqual(M->Threat.FindRef(Tank),10*Tank->DamageThreatMultiplier())&&
        FMath::IsNearlyEqual(M->Threat.FindRef(Bruiser),10*Bruiser->DamageThreatMultiplier()),TEXT("equal damage applies distinct tank and bruiser threat multipliers"));
    Check(Troll->PrimaryStat()==Cires::PrimaryStat::Agility&&!Troll->IsRangedBasicAttack()&&
        Holy->PrimaryStat()==Cires::PrimaryStat::Intelligence&&!Holy->IsRangedBasicAttack(),TEXT("melee agility and intelligence profiles stay melee"));
    for(int32 I=0;I<5;++I)
    {
        auto* H=Make();if(!H){Check(false,TEXT("legacy fixture spawned"));return false;}
        H->Draft(I);Check(H->ChampionProfileId==LegacyProfileId(I)&&H->Archetype==I&&H->Skills.IsEmpty(),TEXT("legacy numeric draft remains compatible"));
    }
    UE_LOG(LogCireChampionProfiles,Display,TEXT("CIRE_CHAMPION_PROFILES_SMOKE_%s checks=%d profiles=%d"),bPassed?TEXT("PASS"):TEXT("FAIL"),Checks,CireChampionRoster::Count());
    return bPassed;
}
#endif
