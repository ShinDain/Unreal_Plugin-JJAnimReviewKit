using UnrealBuildTool;

public class JJAnimReviewKitEditor : ModuleRules
{
    public JJAnimReviewKitEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core", "CoreUObject", "Engine"
        });
        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "AnimationBlueprintLibrary", "Json", "JsonUtilities", "UnrealEd",
            "AnimationEditor", "Persona", "Slate", "SlateCore", "RenderCore", "RHI"
        });
    }
}
