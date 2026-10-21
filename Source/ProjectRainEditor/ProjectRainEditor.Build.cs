using UnrealBuildTool;

public class ProjectRainEditor : ModuleRules
{
	public ProjectRainEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[] {
			"ProjectRain", "UnrealEd", "AssetTools", "AssetRegistry", "Niagara", "NiagaraCore", "NiagaraEditor",
			"NiagaraShader", "GraphEditor", "Slate", "SlateCore", "MaterialEditor", "RenderCore", "RHI", "Projects", "PythonScriptPlugin"
		});
	}
}
