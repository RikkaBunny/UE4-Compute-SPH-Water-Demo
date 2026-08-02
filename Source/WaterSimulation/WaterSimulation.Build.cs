using UnrealBuildTool;
using System.IO;

public class WaterSimulation : ModuleRules
{
	public WaterSimulation(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"InputCore",
				"Projects",
				"RenderCore",
				"RHI",
				"Renderer",
				"Slate",
				"SlateCore"
			}
		);

		PrivateIncludePaths.Add(
			Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Private")
		);
	}
}
