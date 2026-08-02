#include "WaterSimulationShaders.h"

#define IMPLEMENT_SPH_SHADER(ShaderClass, EntryPoint) \
	IMPLEMENT_GLOBAL_SHADER( \
		ShaderClass, \
		"/WaterSimulation/Private/SPHFluid.usf", \
		EntryPoint, \
		SF_Compute)

IMPLEMENT_SPH_SHADER(FSPHInitializeCS, "InitializeParticles");
IMPLEMENT_SPH_SHADER(FSPHExternalForcesCS, "ExternalForces");
IMPLEMENT_SPH_SHADER(FSPHUpdateSpatialHashCS, "UpdateSpatialHash");
IMPLEMENT_SPH_SHADER(FSPHClearCountsCS, "ClearCounts");
IMPLEMENT_SPH_SHADER(FSPHCalculateCountsCS, "CalculateCounts");
IMPLEMENT_SPH_SHADER(FSPHBlockScanCS, "BlockScan");
IMPLEMENT_SPH_SHADER(FSPHBlockCombineCS, "BlockCombine");
IMPLEMENT_SPH_SHADER(FSPHScatterCS, "ScatterOutput");
IMPLEMENT_SPH_SHADER(FSPHSortCopyBackCS, "SortCopyBack");
IMPLEMENT_SPH_SHADER(FSPHInitializeOffsetsCS, "InitializeOffsets");
IMPLEMENT_SPH_SHADER(FSPHCalculateOffsetsCS, "CalculateOffsets");
IMPLEMENT_SPH_SHADER(FSPHReorderCS, "ReorderParticles");
IMPLEMENT_SPH_SHADER(FSPHReorderCopyBackCS, "ReorderCopyBack");
IMPLEMENT_SPH_SHADER(FSPHCalculateDensitiesCS, "CalculateDensities");
IMPLEMENT_SPH_SHADER(FSPHCalculatePressureCS, "CalculatePressureForce");
IMPLEMENT_SPH_SHADER(FSPHCalculateViscosityCS, "CalculateViscosity");
IMPLEMENT_SPH_SHADER(FSPHUpdatePositionsCS, "UpdatePositions");
IMPLEMENT_SPH_SHADER(FSPHUpdateWhiteParticlesCS, "UpdateWhiteParticles");
IMPLEMENT_SPH_SHADER(FSPHPrepareWhiteParticlesCS, "WhiteParticlePrepareNextFrame");
IMPLEMENT_SPH_SHADER(FSPHPrepareWhiteDrawArgsCS, "PrepareWhiteParticleDrawArgs");
IMPLEMENT_SPH_SHADER(FSPHSampleDensityGridCS, "SampleDensityGrid");
IMPLEMENT_SPH_SHADER(FSPHMarchingCubesCS, "BuildMarchingCubesSurface");
IMPLEMENT_SPH_SHADER(FSPHBuildParticleMeshCS, "BuildParticleMesh");

#undef IMPLEMENT_SPH_SHADER
