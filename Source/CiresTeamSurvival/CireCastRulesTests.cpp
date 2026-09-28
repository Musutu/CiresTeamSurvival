// casting-rules (Playtest 6): native checks for the data-driven cast-time / healing rules and the ground-shape audit.
#include "CireSkillTuning.h"
#include "CireAbilityDB.h"
#include "CireAbilityShapes.h"
#include "CireAreaEffects.h"

#if !UE_BUILD_SHIPPING
DEFINE_LOG_CATEGORY_STATIC(LogCireCastRules, Log, All);

namespace
{
struct FCastRuleChecks
{
    int32 Count = 0; bool bPassed = true;
    void Check(bool bCondition, const FString& Message)
    {
        ++Count;
        if (!bCondition) { bPassed = false; UE_LOG(LogCireCastRules, Error, TEXT("CIRE_CAST_RULES_CHECK_FAIL %s"), *Message); }
    }
};
FCireAbilityDef SyntheticAbility(const TCHAR* Id, const TCHAR* Component, const TCHAR* Targeting, float Radius, float Base, float Primary, const TCHAR* Label)
{
    FCireAbilityDef D; D.Id = Id; D.Name = Id; D.Kind = TEXT("active"); D.Section = TEXT("spell"); D.Targeting = Targeting;
    D.ScaleComponent = Component; D.ScaleBase = Base; D.ScalePrimary = Primary; D.Base.Effect = Base; D.EffectLabel = Label;
    D.Radius = CireAbilityShapes::AoE(Radius); // rows are stored aoe-scaled
    return D;
}
}

bool CireSkillTuning::RunCastRulesSmoke()
{
    FCastRuleChecks T;
    const FCireCastRules& R = CastRules();
    T.Check(R.bEnabled && R.HealingScale < 1.f, TEXT("CastRules.json loaded with a healing nerf"));

    // 1) Every Ability Database row obeys its rule's window.
    int32 AoE = 0, Direct = 0, Area = 0;
    for (const FCireAbilityDef& D : CireAbilityDB::All())
    {
        const FString Id = D.Id;
        if (D.CastRule == TEXT("aoeDamage"))
        { ++AoE; T.Check(D.CastTime >= R.AoEDamageMinCast - .001f && D.CastTime <= R.AoEDamageMaxCast + .001f, Id + TEXT(": AoE damage cast 0.5-3.5 s")); }
        else if (D.CastRule == TEXT("directHeal"))
        { ++Direct; T.Check(D.CastTime >= R.DirectHealMinCast - .001f && D.CastTime <= R.DirectHealMaxCast + .001f && FMath::IsNearlyEqual(D.HealScale, R.HealingScale), Id + TEXT(": direct heal cast 1.5-3.5 s, healing nerfed")); }
        else if (D.CastRule == TEXT("aoeHeal"))
        { ++Area; T.Check(D.CastTime >= 0 && D.CastTime <= R.AoEHealMaxCast + .001f && D.HealScale < R.HealingScale && D.HealScale >= R.HealingScale * R.AoEHealInstantEffect - .001f, Id + TEXT(": AoE heal instant..1.5 s with its effect cut")); }
        if (D.ScaleComponent == TEXT("heal")) T.Check(D.HealScale <= R.HealingScale + .001f, Id + TEXT(": every ability heal carries the nerf"));
        T.Check(D.Base.CastTime == D.CastTime, Id + TEXT(": level curve reads the ruled cast time"));
        UE_LOG(LogCireCastRules, Display, TEXT("CIRE_CAST_RULE_ROW %s rule=%s cast=%.1f authored=%.1f metric=%.0f healScale=%.2f"),
            *Id, *D.CastRule.ToString(), D.CastTime, D.AuthoredCastTime, D.CastMetric, D.HealScale);
    }
    T.Check(AoE >= 10 && Direct >= 5 && Area >= 5, FString::Printf(TEXT("rules classify the pool (aoe %d, direct %d, area heal %d)"), AoE, Direct, Area));
    if (const auto* Starfall = CireAbilityDB::Find(TEXT("starfall"))) T.Check(Starfall->CastRule == TEXT("aoeDamage") && Starfall->CastTime > 2.f, TEXT("Starfall is a long AoE cast"));
    if (const auto* Light = CireAbilityDB::Find(TEXT("restoring_light"))) T.Check(Light->CastRule == TEXT("directHeal"), TEXT("Restoring Light is a direct heal"));
    if (const auto* Sanct = CireAbilityDB::Find(TEXT("sanctuary"))) T.Check(Sanct->CastRule == TEXT("aoeHeal") && Sanct->CastTime <= 1.5f, TEXT("Sanctuary is an AoE heal at <= 1.5 s"));
    if (const auto* Pounce = CireAbilityDB::Find(TEXT("shadow_step"))) T.Check(Pounce->CastRule == TEXT("none"), TEXT("gap closers are exempt"));
    for (const TCHAR* Id : {TEXT("cleaving_strike"), TEXT("ember_lance"), TEXT("summoned_wall"), TEXT("oathbound_guardian"), TEXT("venom_tumble")})
        if (const auto* D = CireAbilityDB::Find(Id)) T.Check(D->CastRule == TEXT("none"), FString(Id) + TEXT(": attacks, skillshots, constructs, summons and rolls keep their own cast"));

    // 2) New abilities inherit the rules and the cast time grows with impact / power.
    const auto Small = EvaluateCastRule(SyntheticAbility(TEXT("new_small_nova"), TEXT("damage"), TEXT("aim"), 200, 30, .5f, TEXT("damage")), R);
    const auto Big = EvaluateCastRule(SyntheticAbility(TEXT("new_big_nova"), TEXT("damage"), TEXT("aim"), 500, 250, 3.f, TEXT("damage")), R);
    T.Check(Small.Rule == ECireCastRule::AoEDamage && Big.Rule == ECireCastRule::AoEDamage && Small.CastTime < Big.CastTime &&
        FMath::IsNearlyEqual(Small.CastTime, R.AoEDamageMinCast) && FMath::IsNearlyEqual(Big.CastTime, R.AoEDamageMaxCast), TEXT("a new AoE damage spell inherits 0.5-3.5 s by impact"));
    const auto LowHeal = EvaluateCastRule(SyntheticAbility(TEXT("new_mend"), TEXT("heal"), TEXT("ally"), 0, 60, .5f, TEXT("healing")), R);
    const auto HighHeal = EvaluateCastRule(SyntheticAbility(TEXT("new_greater_mend"), TEXT("heal"), TEXT("ally"), 0, 300, 4.f, TEXT("healing")), R);
    T.Check(LowHeal.Rule == ECireCastRule::DirectHeal && FMath::IsNearlyEqual(LowHeal.CastTime, R.DirectHealMinCast) && FMath::IsNearlyEqual(HighHeal.CastTime, R.DirectHealMaxCast),
        TEXT("a new direct heal inherits 1.5-3.5 s by power"));
    const auto QuickRain = EvaluateCastRule(SyntheticAbility(TEXT("new_rain"), TEXT("heal"), TEXT("aim"), 400, 40, .3f, TEXT("healing per ally")), R);
    const auto SlowRain = EvaluateCastRule(SyntheticAbility(TEXT("new_monsoon"), TEXT("heal"), TEXT("self"), 800, 500, 1.f, TEXT("healing per second")), R);
    T.Check(QuickRain.Rule == ECireCastRule::AoEHeal && QuickRain.CastTime == 0 && SlowRain.CastTime > QuickRain.CastTime && SlowRain.CastTime <= R.AoEHealMaxCast &&
        QuickRain.HealScale < SlowRain.HealScale, TEXT("a new AoE heal is instant..1.5 s and the faster cast heals less"));
    auto Pinned = SyntheticAbility(TEXT("new_pinned"), TEXT("damage"), TEXT("aim"), 500, 250, 3.f, TEXT("damage"));
    T.Check(EvaluateCastRule(Pinned, R, TEXT("exempt")).Rule == ECireCastRule::None && EvaluateCastRule(Pinned, R, TEXT("directHeal")).Rule == ECireCastRule::DirectHeal,
        TEXT("a row can pin itself with castRule"));

    // 3) Rules file validation.
    FCireCastRules Parsed; FString Error;
    T.Check(!ParseCastRules(TEXT("{}"), Parsed, Error) && !ParseCastRules(TEXT("{\"schemaVersion\":1,\"profile\":\"CireCastRules\",\"aoeDamage\":{\"minCast\":-1}}"), Parsed, Error),
        TEXT("invalid cast rules are rejected"));
    T.Check(ParseCastRules(TEXT("{\"schemaVersion\":1,\"profile\":\"CireCastRules\",\"healing\":{\"abilityHealingScale\":0.5}}"), Parsed, Error) && FMath::IsNearlyEqual(Parsed.HealingScale, .5f),
        TEXT("partial cast rules keep defaults"));
    T.Check(HealScaleFor(TEXT("Mending Strikes")) == 1.f && HealScaleFor(TEXT("probe heal")) == 1.f, TEXT("items and class traits are not nerfed"));
    T.Check(!ShouldPierce(nullptr, TEXT("Ember Lance")), TEXT("only heroes' skillshots pierce by rule"));

    // 4) Shape audit: ground effects are exactly Line / Barrier / Cone / Circle, and the telegraph is the hit shape.
    int32 Audited = 0;
    for (const FCireAbilityDef& D : CireAbilityDB::All())
    {
        if (D.IsPassive() || !D.IsImplemented()) continue;
        const FCireHitShape S = CireAbilityShapes::Describe(FName(*D.Id));
        const bool bGround = S.HasGroundShape();
        const bool bAllowed = S.Kind != ECireHitShape::Square && (S.Kind != ECireHitShape::Custom || ACireAreaEffect::IsBarrierPolygon(S.Polygon));
        T.Check(bAllowed, D.Id + TEXT(": ground shape is Line / Barrier / Cone / Circle"));
        if (bGround)
        {
            FCireAreaSpec A = S.AsArea(); const ECireAreaShape Before = A.Shape; ACireAreaEffect::NormalizeShape(A);
            T.Check(A.Shape == Before, D.Id + TEXT(": telegraph already matches the spawned hit shape"));
        }
        ++Audited;
        UE_LOG(LogCireCastRules, Display, TEXT("CIRE_SHAPE_AUDIT %s kind=%s radius=%.0f length=%.0f width=%.0f angle=%.0f projectile=%d ground=%d"),
            *D.Id, *CireAbilityShapes::ShapeName(S.Kind), S.Radius, S.Length, S.Width, S.Angle, S.bProjectile ? 1 : 0, S.bGroundAim ? 1 : 0);
    }
    FCireAreaSpec Square; Square.Shape = ECireAreaShape::Square; Square.Width = 400; ACireAreaEffect::NormalizeShape(Square);
    FCireAreaSpec Odd; Odd.Shape = ECireAreaShape::Custom; Odd.Radius = 280; Odd.CustomPolygon = {{-260, -200}, {260, -200}, {260, -50}, {80, -50}, {80, 230}, {-260, 230}}; ACireAreaEffect::NormalizeShape(Odd);
    FCireAreaSpec Plank; Plank.Shape = ECireAreaShape::Custom; Plank.CustomPolygon = {{-30, -200}, {30, -200}, {30, 200}, {-30, 200}}; ACireAreaEffect::NormalizeShape(Plank);
    T.Check(Square.Shape == ECireAreaShape::Circle && FMath::IsNearlyEqual(Square.Radius, 200.f) && Odd.Shape == ECireAreaShape::Circle && FMath::IsNearlyEqual(Odd.Radius, 280.f) &&
        Plank.Shape == ECireAreaShape::Custom, TEXT("square -> circle, abnormal polygon -> circle, barrier rectangle stays"));
    T.Check(Audited > 50, FString::Printf(TEXT("shape audit covered %d abilities"), Audited));

    UE_LOG(LogCireCastRules, Display, TEXT("CIRE_CAST_RULES_%s checks=%d"), T.bPassed ? TEXT("PASS") : TEXT("FAIL"), T.Count);
    return T.bPassed;
}
#endif
