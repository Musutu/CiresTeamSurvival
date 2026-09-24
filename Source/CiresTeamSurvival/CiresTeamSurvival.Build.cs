using UnrealBuildTool;
public class CiresTeamSurvival : ModuleRules {
    public CiresTeamSurvival(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // Many .cpp files define same-named helpers in anonymous namespaces; unity batching collides them.
        bUseUnity = false;
        PublicDependencyModuleNames.AddRange(new string[] {"Core","CoreUObject","Engine","InputCore","AIModule","NavigationSystem","ProceduralMeshComponent","Json","JsonUtilities","NetworkReplayStreaming"});
        // HUD: runtime TTF fonts (SlateCore) and filled canvas primitives (RenderCore white texture).
        PrivateDependencyModuleNames.AddRange(new string[] {"SlateCore","RenderCore"});
    }
}
