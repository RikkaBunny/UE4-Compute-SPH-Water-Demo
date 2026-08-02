#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FSPHInitializeCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHInitializeCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHInitializeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumParticles)
		SHADER_PARAMETER(uint32, ParticlesPerAxis)
		SHADER_PARAMETER(uint32, SpawnRegionCount)
		SHADER_PARAMETER(FVector, SpawnCenter)
		SHADER_PARAMETER(FVector, SecondSpawnCenter)
		SHADER_PARAMETER(float, SpawnSize)
		SHADER_PARAMETER(float, JitterStrength)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, Positions)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, PredictedPositions)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, Velocities)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float2>, Densities)
	END_SHADER_PARAMETER_STRUCT()
};

class FSPHExternalForcesCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHExternalForcesCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHExternalForcesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumParticles)
		SHADER_PARAMETER(float, DeltaTime)
		SHADER_PARAMETER(float, Gravity)
		SHADER_PARAMETER(FVector, ContainerVelocityDelta)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, Positions)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, PredictedPositions)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, Velocities)
	END_SHADER_PARAMETER_STRUCT()
};

class FSPHUpdateSpatialHashCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHUpdateSpatialHashCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHUpdateSpatialHashCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumParticles)
		SHADER_PARAMETER(float, SmoothingRadius)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, PredictedPositions)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, SpatialKeys)
	END_SHADER_PARAMETER_STRUCT()
};

class FSPHClearCountsCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHClearCountsCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHClearCountsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumInputs)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, Counts)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, InputItems)
	END_SHADER_PARAMETER_STRUCT()
};

class FSPHCalculateCountsCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHCalculateCountsCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHCalculateCountsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumInputs)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, InputKeys)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, Counts)
	END_SHADER_PARAMETER_STRUCT()
};

class FSPHBlockScanCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHBlockScanCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHBlockScanCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ItemCount)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, Elements)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, GroupSums)
	END_SHADER_PARAMETER_STRUCT()
};

class FSPHBlockCombineCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHBlockCombineCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHBlockCombineCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ItemCount)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, Elements)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, GroupSums)
	END_SHADER_PARAMETER_STRUCT()
};

class FSPHScatterCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHScatterCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHScatterCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumInputs)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, InputItems)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, InputKeys)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, Counts)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, SortedItems)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, SortedKeys)
	END_SHADER_PARAMETER_STRUCT()
};

class FSPHSortCopyBackCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHSortCopyBackCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHSortCopyBackCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumInputs)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, InputItems)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, InputKeys)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, SortedItems)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, SortedKeys)
	END_SHADER_PARAMETER_STRUCT()
};

class FSPHInitializeOffsetsCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHInitializeOffsetsCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHInitializeOffsetsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumInputs)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, SpatialOffsets)
	END_SHADER_PARAMETER_STRUCT()
};

class FSPHCalculateOffsetsCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHCalculateOffsetsCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHCalculateOffsetsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumInputs)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, SpatialKeys)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, SpatialOffsets)
	END_SHADER_PARAMETER_STRUCT()
};

class FSPHReorderCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHReorderCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHReorderCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumParticles)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, SortedIndices)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, Positions)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, PredictedPositions)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, Velocities)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, SortTargetPositions)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, SortTargetPredictedPositions)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, SortTargetVelocities)
	END_SHADER_PARAMETER_STRUCT()
};

class FSPHReorderCopyBackCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHReorderCopyBackCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHReorderCopyBackCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumParticles)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, Positions)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, PredictedPositions)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, Velocities)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, SortTargetPositions)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, SortTargetPredictedPositions)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, SortTargetVelocities)
	END_SHADER_PARAMETER_STRUCT()
};

class FSPHCalculateDensitiesCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHCalculateDensitiesCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHCalculateDensitiesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumParticles)
		SHADER_PARAMETER(float, SmoothingRadius)
		SHADER_PARAMETER(float, KSpikyPow2)
		SHADER_PARAMETER(float, KSpikyPow3)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, PredictedPositions)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float2>, Densities)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, SpatialKeys)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, SpatialOffsets)
	END_SHADER_PARAMETER_STRUCT()
};

class FSPHCalculatePressureCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHCalculatePressureCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHCalculatePressureCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumParticles)
		SHADER_PARAMETER(float, DeltaTime)
		SHADER_PARAMETER(float, SmoothingRadius)
		SHADER_PARAMETER(float, TargetDensity)
		SHADER_PARAMETER(float, PressureMultiplier)
		SHADER_PARAMETER(float, NearPressureMultiplier)
		SHADER_PARAMETER(float, KSpikyPow2Grad)
		SHADER_PARAMETER(float, KSpikyPow3Grad)
		SHADER_PARAMETER(float, SimTime)
		SHADER_PARAMETER(float, TrappedAirSpawnRate)
		SHADER_PARAMETER(FVector2D, TrappedAirVelocityMinMax)
		SHADER_PARAMETER(FVector2D, KineticEnergyMinMax)
		SHADER_PARAMETER(float, BubbleScale)
		SHADER_PARAMETER(uint32, MaxWhiteParticleCount)
		SHADER_PARAMETER(uint32, FoamActive)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, PredictedPositions)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, Velocities)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float2>, Densities)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, SpatialKeys)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, SpatialOffsets)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<FSPHWhiteParticle>, WhiteParticles)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, WhiteParticleCounters)
	END_SHADER_PARAMETER_STRUCT()
};

class FSPHCalculateViscosityCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHCalculateViscosityCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHCalculateViscosityCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumParticles)
		SHADER_PARAMETER(float, DeltaTime)
		SHADER_PARAMETER(float, SmoothingRadius)
		SHADER_PARAMETER(float, ViscosityStrength)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, PredictedPositions)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, Velocities)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float2>, Densities)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, SpatialKeys)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, SpatialOffsets)
	END_SHADER_PARAMETER_STRUCT()
};

class FSPHUpdatePositionsCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHUpdatePositionsCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHUpdatePositionsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumParticles)
		SHADER_PARAMETER(float, DeltaTime)
		SHADER_PARAMETER(float, CollisionDamping)
		SHADER_PARAMETER(FVector, BoundsSize)
		SHADER_PARAMETER(FVector, BoundsCenter)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, Positions)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, Velocities)
	END_SHADER_PARAMETER_STRUCT()
};

class FSPHUpdateWhiteParticlesCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHUpdateWhiteParticlesCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHUpdateWhiteParticlesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumParticles)
		SHADER_PARAMETER(uint32, MaxWhiteParticleCount)
		SHADER_PARAMETER(float, WhiteParticleDeltaTime)
		SHADER_PARAMETER(float, Gravity)
		SHADER_PARAMETER(FVector, ContainerVelocityDelta)
		SHADER_PARAMETER(float, SmoothingRadius)
		SHADER_PARAMETER(float, BubbleBuoyancy)
		SHADER_PARAMETER(int32, SprayClassifyMaxNeighbours)
		SHADER_PARAMETER(int32, BubbleClassifyMinNeighbours)
		SHADER_PARAMETER(float, BubbleScale)
		SHADER_PARAMETER(float, BubbleScaleChangeSpeed)
		SHADER_PARAMETER(FVector, BoundsSize)
		SHADER_PARAMETER(FVector, BoundsCenter)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<FSPHWhiteParticle>, WhiteParticles)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<FSPHWhiteParticle>, WhiteParticlesCompacted)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, WhiteParticleCounters)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, PredictedPositions)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, Velocities)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, SpatialKeys)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, SpatialOffsets)
	END_SHADER_PARAMETER_STRUCT()
};

class FSPHPrepareWhiteParticlesCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHPrepareWhiteParticlesCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHPrepareWhiteParticlesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, MaxWhiteParticleCount)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<FSPHWhiteParticle>, WhiteParticles)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<FSPHWhiteParticle>, WhiteParticlesCompacted)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, WhiteParticleCounters)
	END_SHADER_PARAMETER_STRUCT()
};

class FSPHPrepareWhiteDrawArgsCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHPrepareWhiteDrawArgsCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHPrepareWhiteDrawArgsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, MaxWhiteParticleCount)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, WhiteParticleCounters)
		SHADER_PARAMETER_UAV(RWBuffer<uint>, WhiteParticleDrawArgs)
	END_SHADER_PARAMETER_STRUCT()
};

class FSPHSampleDensityGridCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHSampleDensityGridCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHSampleDensityGridCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumParticles)
		SHADER_PARAMETER(FIntVector, SurfaceGridSize)
		SHADER_PARAMETER(float, SmoothingRadius)
		SHADER_PARAMETER(float, KSpikyPow2)
		SHADER_PARAMETER(float, KSpikyPow3)
		SHADER_PARAMETER(FVector, BoundsSize)
		SHADER_PARAMETER(FVector, BoundsCenter)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, PredictedPositions)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, SpatialKeys)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, SpatialOffsets)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, DensityGrid)
	END_SHADER_PARAMETER_STRUCT()
};

class FSPHMarchingCubesCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHMarchingCubesCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHMarchingCubesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector, SurfaceGridSize)
		SHADER_PARAMETER(float, SurfaceIsoLevel)
		SHADER_PARAMETER(float, SimulationToCentimeters)
		SHADER_PARAMETER(FVector, BoundsSize)
		SHADER_PARAMETER(FVector, BoundsCenter)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float>, DensityGrid)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<int>, MarchingCubesLUT)
		SHADER_PARAMETER_UAV(RWBuffer<float>, OutputPositions)
		SHADER_PARAMETER_UAV(RWBuffer<float4>, OutputTangents)
	END_SHADER_PARAMETER_STRUCT()
};

class FSPHBuildParticleMeshCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHBuildParticleMeshCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHBuildParticleMeshCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumParticles)
		SHADER_PARAMETER(float, RenderRadiusCm)
		SHADER_PARAMETER(float, SimulationToCentimeters)
		SHADER_PARAMETER_UAV(RWStructuredBuffer<float4>, Positions)
		SHADER_PARAMETER_UAV(RWBuffer<float>, OutputPositions)
		SHADER_PARAMETER_UAV(RWBuffer<float4>, OutputTangents)
	END_SHADER_PARAMETER_STRUCT()
};
