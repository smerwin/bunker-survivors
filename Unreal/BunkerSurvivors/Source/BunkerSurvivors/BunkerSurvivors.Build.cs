using UnrealBuildTool;

public class BunkerSurvivors : ModuleRules
{
	public BunkerSurvivors(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "Slate", "SlateCore", "AssetRegistry", "Json", "ProceduralMeshComponent"
		});
	}
}
