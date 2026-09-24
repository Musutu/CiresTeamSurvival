using UnrealBuildTool;
public class CiresTeamSurvival : ModuleRules {
    public CiresTeamSurvival(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] {"Core","CoreUObject","Engine","InputCore","AIModule","NavigationSystem","ProceduralMeshComponent","Json","JsonUtilities","NetworkReplayStreaming"});
    }
}
