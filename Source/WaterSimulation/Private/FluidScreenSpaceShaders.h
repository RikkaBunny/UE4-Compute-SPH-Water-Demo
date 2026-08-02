#pragma once

#include "GlobalShader.h"
#include "RenderGraphResources.h"
#include "SceneView.h"
#include "ShaderParameterStruct.h"

class FSPHScreenSplatCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHScreenSplatCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHScreenSplatCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(FMatrix, LocalToWorld)
		SHADER_PARAMETER(FIntPoint, ViewSize)
		SHADER_PARAMETER(uint32, UseWhiteParticles)
		SHADER_PARAMETER(uint32, MaxParticleCount)
		SHADER_PARAMETER(float, ParticleRadiusCm)
		SHADER_PARAMETER(float, FoamRadiusCm)
		SHADER_PARAMETER(uint32, OccludeThicknessWithFoam)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, FluidPositions)
		SHADER_PARAMETER_SRV(StructuredBuffer<FSPHWhiteParticle>, WhiteParticles)
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, WhiteParticleCounters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(
			StructuredBuffer<uint>,
			FoamOcclusionDepth)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, ScreenDepthOutput)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, ScreenAccumulationOutput)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(
		const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(
			Parameters.Platform,
			ERHIFeatureLevel::SM5);
	}
};

class FSPHShadowSplatCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHShadowSplatCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHShadowSplatCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, MaxParticleCount)
		SHADER_PARAMETER(FIntPoint, ShadowSize)
		SHADER_PARAMETER(float, SourceShadowHalfWidthCm)
		SHADER_PARAMETER(float, SourceShadowHalfHeightCm)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, FluidPositions)
		SHADER_PARAMETER_RDG_BUFFER_UAV(
			RWStructuredBuffer<uint>,
			ShadowAccumulationOutput)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(
		const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(
			Parameters.Platform,
			ERHIFeatureLevel::SM5);
	}
};

class FSPHShadowSmoothCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHShadowSmoothCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHShadowSmoothCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, ShadowSize)
		SHADER_PARAMETER(FIntPoint, ShadowFilterDirection)
		SHADER_PARAMETER(uint32, ShadowInputIsFixedPoint)
		SHADER_PARAMETER_RDG_BUFFER_SRV(
			StructuredBuffer<uint>,
			ShadowInputBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(
			RWStructuredBuffer<uint>,
			ShadowOutputBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(
		const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(
			Parameters.Platform,
			ERHIFeatureLevel::SM5);
	}
};

class FSPHDecodeSmoothDepthCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHDecodeSmoothDepthCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHDecodeSmoothDepthCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, ViewSize)
		SHADER_PARAMETER(FIntPoint, FilterDirection)
		SHADER_PARAMETER(float, ProjectionScaleX)
		SHADER_PARAMETER(float, WorldFilterRadiusCm)
		SHADER_PARAMETER(uint32, MaxFilterRadius)
		SHADER_PARAMETER(float, FilterStrength)
		SHADER_PARAMETER(float, DepthDifferenceStrength)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, DecodeInputDepth)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, DecodeInputThickness)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, DecodeOutputDepth)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, DecodeOutputThickness)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(
		const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(
			Parameters.Platform,
			ERHIFeatureLevel::SM5);
	}
};

class FSPHSmoothDepthCS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHSmoothDepthCS);
	SHADER_USE_PARAMETER_STRUCT(FSPHSmoothDepthCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, ViewSize)
		SHADER_PARAMETER(FIntPoint, FilterDirection)
		SHADER_PARAMETER(float, ProjectionScaleX)
		SHADER_PARAMETER(float, WorldFilterRadiusCm)
		SHADER_PARAMETER(uint32, MaxFilterRadius)
		SHADER_PARAMETER(float, FilterStrength)
		SHADER_PARAMETER(float, DepthDifferenceStrength)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, RawDepthBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InputDepthBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, InputThicknessBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutputDepthBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, OutputThicknessBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(
		const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(
			Parameters.Platform,
			ERHIFeatureLevel::SM5);
	}
};

class FSPHScreenCompositePS final : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSPHScreenCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FSPHScreenCompositePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(FIntPoint, ViewSize)
		SHADER_PARAMETER(FVector, ExtinctionCoefficients)
		SHADER_PARAMETER(float, ExtinctionMultiplier)
		SHADER_PARAMETER(float, RefractionMultiplier)
		SHADER_PARAMETER(FVector, BoundsSizeCm)
		SHADER_PARAMETER(FVector, DirToSunWorld)
		SHADER_PARAMETER(FVector, CameraRightWorld)
		SHADER_PARAMETER(FVector, CameraUpWorld)
		SHADER_PARAMETER(FVector, CameraForwardWorld)
		SHADER_PARAMETER(FVector2D, CameraTanHalfFov)
		SHADER_PARAMETER(FVector, CameraPositionWorld)
		SHADER_PARAMETER(FVector, SimulationOriginWorld)
		SHADER_PARAMETER(FVector, EnvironmentOriginWorld)
		SHADER_PARAMETER(FIntPoint, ShadowSize)
		SHADER_PARAMETER(float, SourceShadowHalfWidthCm)
		SHADER_PARAMETER(float, SourceShadowHalfHeightCm)
		SHADER_PARAMETER(uint32, FoamMax)
		SHADER_PARAMETER_SRV(
			StructuredBuffer<uint>,
			WhiteParticleCounters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, FluidDepthBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, SmoothedThicknessBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, FoamDepthBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, FoamMaskBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ShadowBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(
		const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(
			Parameters.Platform,
			ERHIFeatureLevel::SM5);
	}
};
