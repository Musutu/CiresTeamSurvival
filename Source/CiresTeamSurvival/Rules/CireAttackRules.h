#pragma once
#include <cmath>

namespace Cires {
enum class AttackResult { Hit, Miss, Dodge };
// Distinct sequential rolls: default success=.95*.95=.9025; uphill=.60*.95=.57.
constexpr double BaseMissChance=.05;
constexpr double DodgeChance=.05;
constexpr double UphillMissChance=.40;
constexpr double UphillToleranceCm=10.;
inline double MissChance(bool Ranged,double SourceFeetZ,double TargetFeetZ) {
    return Ranged && TargetFeetZ-SourceFeetZ>UphillToleranceCm ? UphillMissChance : BaseMissChance;
}
inline AttackResult ResolveAttack(double Miss,double MissRoll,double DodgeRoll) {
    if(!std::isfinite(Miss)||!std::isfinite(MissRoll)||!std::isfinite(DodgeRoll)||
       Miss<0||Miss>1||MissRoll<0||MissRoll>=1||DodgeRoll<0||DodgeRoll>=1) return AttackResult::Miss;
    if(MissRoll<Miss)return AttackResult::Miss;
    return DodgeRoll<DodgeChance?AttackResult::Dodge:AttackResult::Hit;
}
}
