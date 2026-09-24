using UnrealBuildTool;
using System.Collections.Generic;
public class CiresTeamSurvivalEditorTarget : TargetRules {
    public CiresTeamSurvivalEditorTarget(TargetInfo Target) : base(Target) {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("CiresTeamSurvival");
    }
}
