#include "WaterSimulation.h"

#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY(LogWaterSimulation);

class FWaterSimulationModule final : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		const FString ShaderDirectory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/WaterSimulation"), ShaderDirectory);
		UE_LOG(LogWaterSimulation, Log, TEXT("Mapped WaterSimulation shaders from %s"), *ShaderDirectory);
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FWaterSimulationModule, WaterSimulation, "WaterSimulation");
