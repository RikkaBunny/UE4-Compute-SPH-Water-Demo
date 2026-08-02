using UnrealBuildTool;
using System.Collections.Generic;

public class WaterSimulationEditorTarget : TargetRules
{
	public WaterSimulationEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V2;
		ExtraModuleNames.AddRange(new string[] { "WaterSimulation" });
	}
}
