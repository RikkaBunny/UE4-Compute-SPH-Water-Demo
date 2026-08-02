#include "ComputeWaterComponent.h"

#include "WaterSimulation.h"
#include "WaterSimulationShaders.h"
#include "FluidScreenSpaceShaders.h"

#include "CommonRenderResources.h"
#include "LocalVertexFactory.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PrimitiveSceneProxy.h"
#include "PostProcess/PostProcessMaterial.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "RHIStaticStates.h"
#include "SceneManagement.h"
#include "SceneRendering.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"
#include "UObject/ConstructorHelpers.h"

namespace SPHFluid
{
	static constexpr uint32 ParticleThreadGroupSize = 256;
	static constexpr uint32 ScanItemsPerGroup = 512;
	static constexpr uint32 VerticesPerParticle = 6;
	static constexpr uint32 IndicesPerParticle = 24;
	static constexpr uint32 VerticesPerSurfaceCube = 15;
	static constexpr uint32 MarchingCubesLUTSize = 2460;
	static constexpr float SimulationToCentimeters = 100.0f;
	static constexpr int32 MaxScanLevels = 8;

	struct FDynamicData
	{
		uint32 NumParticles = 0;
		uint32 ParticlesPerAxis = 24;
		uint32 SpawnRegionCount = 1;
		FVector BoundsSize = FVector(8.0f, 5.0f, 3.6f);
		FVector BoundsCenter = FVector(0.0f, 0.0f, 1.8f);
		FVector SpawnCenter = FVector(-2.0f, 0.0f, 0.2f);
		FVector SecondSpawnCenter = FVector(2.0f, 0.0f, 0.2f);
		float SpawnSize = 2.85f;
		float SpawnJitter = 0.035f;
		float SmoothingRadius = 0.2f;
		float TargetDensity = 630.0f;
		float PressureMultiplier = 288.0f;
		float NearPressureMultiplier = 2.15f;
		float ViscosityStrength = 0.001f;
		float Gravity = -10.0f;
		float CollisionDamping = 0.95f;
		float ParticleRenderRadiusCm = 9.0f;
		int32 SurfaceGridResolution = 75;
		float SurfaceIsoLevel = 75.0f;
		float DeltaTime = 1.0f / 60.0f;
		FVector ContainerVelocityDelta = FVector::ZeroVector;
		int32 SimulationSubsteps = 3;
		bool bAnimate = true;
		bool bReset = false;
		bool bRenderContinuousSurface = true;
		bool bRenderScreenSpaceSurface = false;
		bool bFoamActive = false;
		uint32 MaxFoamParticleCount = 1000;
		float TrappedAirSpawnRate = 70.0f;
		float FoamSpawnFadeInTime = 0.5f;
		float FoamSpawnFadeStartTime = 0.0f;
		FVector2D TrappedAirVelocityMinMax = FVector2D(5.0f, 25.0f);
		FVector2D FoamKineticEnergyMinMax = FVector2D(15.0f, 80.0f);
		float BubbleBuoyancy = 1.5f;
		int32 SprayClassifyMaxNeighbours = 5;
		int32 BubbleClassifyMinNeighbours = 15;
		float BubbleScale = 0.5f;
		float BubbleScaleChangeSpeed = 7.0f;
	};

	static FIntVector ParticleDispatch(uint32 Count)
	{
		return FIntVector(
			FMath::DivideAndRoundUp(Count, ParticleThreadGroupSize),
			1,
			1);
	}

	static FIntVector CalculateSurfaceGridSize(
		const FVector& BoundsSize,
		int32 MaximumAxisResolution)
	{
		const FVector PositiveBounds = BoundsSize.GetAbs();
		const float MaximumAxis = FMath::Max3(
			PositiveBounds.X,
			PositiveBounds.Y,
			PositiveBounds.Z);
		const float SafeMaximumAxis = FMath::Max(1.0f, MaximumAxis);
		const int32 Resolution =
			FMath::Clamp(MaximumAxisResolution, 24, 96);
		return FIntVector(
			FMath::Max(
				3,
				FMath::RoundToInt(
					PositiveBounds.X / SafeMaximumAxis * Resolution)),
			FMath::Max(
				3,
				FMath::RoundToInt(
					PositiveBounds.Y / SafeMaximumAxis * Resolution)),
			FMath::Max(
				3,
				FMath::RoundToInt(
					PositiveBounds.Z / SafeMaximumAxis * Resolution)));
	}

	static void UAVBarrier(
		FRHICommandListImmediate& RHICmdList,
		const FUnorderedAccessViewRHIRef& UAV)
	{
		RHICmdList.Transition(FRHITransitionInfo(
			UAV,
			ERHIAccess::UAVCompute,
			ERHIAccess::UAVCompute));
	}

	class FPositionBuffer final : public FVertexBuffer
	{
	public:
		explicit FPositionBuffer(uint32 InVertexCount)
			: VertexCount(InVertexCount)
		{
		}

		virtual void InitRHI() override
		{
			const uint32 BufferSize = VertexCount * 3u * sizeof(float);
			FRHIResourceCreateInfo CreateInfo;
			CreateInfo.DebugName = TEXT("SPHFluidParticlePositions");
			VertexBufferRHI = RHICreateVertexBuffer(
				BufferSize,
				BUF_Static | BUF_UnorderedAccess | BUF_ShaderResource,
				ERHIAccess::UAVCompute,
				CreateInfo);
			UAV = RHICreateUnorderedAccessView(VertexBufferRHI, PF_R32_FLOAT);
			SRV = RHICreateShaderResourceView(
				VertexBufferRHI,
				sizeof(float),
				PF_R32_FLOAT);
		}

		virtual void ReleaseRHI() override
		{
			UAV.SafeRelease();
			SRV.SafeRelease();
			FVertexBuffer::ReleaseRHI();
		}

		FUnorderedAccessViewRHIRef UAV;
		FShaderResourceViewRHIRef SRV;

	private:
		uint32 VertexCount;
	};

	class FTangentBuffer final : public FVertexBuffer
	{
	public:
		explicit FTangentBuffer(uint32 InVertexCount)
			: VertexCount(InVertexCount)
		{
		}

		virtual void InitRHI() override
		{
			const uint32 BufferSize =
				VertexCount * 2u * sizeof(FVector4);
			FRHIResourceCreateInfo CreateInfo;
			CreateInfo.DebugName = TEXT("SPHFluidParticleTangents");
			VertexBufferRHI = RHICreateVertexBuffer(
				BufferSize,
				BUF_Static | BUF_UnorderedAccess | BUF_ShaderResource,
				ERHIAccess::UAVCompute,
				CreateInfo);
			UAV = RHICreateUnorderedAccessView(
				VertexBufferRHI,
				PF_A32B32G32R32F);
			SRV = RHICreateShaderResourceView(
				VertexBufferRHI,
				sizeof(FVector4),
				PF_A32B32G32R32F);
		}

		virtual void ReleaseRHI() override
		{
			UAV.SafeRelease();
			SRV.SafeRelease();
			FVertexBuffer::ReleaseRHI();
		}

		FUnorderedAccessViewRHIRef UAV;
		FShaderResourceViewRHIRef SRV;

	private:
		uint32 VertexCount;
	};

	class FUVBuffer final : public FVertexBuffer
	{
	public:
		explicit FUVBuffer(uint32 InVertexCount)
		{
			UVs.Init(FVector2D::ZeroVector, InVertexCount);
		}

		virtual void InitRHI() override
		{
			const uint32 BufferSize = UVs.Num() * sizeof(FVector2D);
			FRHIResourceCreateInfo CreateInfo;
			CreateInfo.DebugName = TEXT("SPHFluidParticleUVs");
			VertexBufferRHI = RHICreateVertexBuffer(
				BufferSize,
				BUF_Static | BUF_ShaderResource,
				ERHIAccess::VertexOrIndexBuffer,
				CreateInfo);

			void* Destination = RHILockVertexBuffer(
				VertexBufferRHI,
				0,
				BufferSize,
				RLM_WriteOnly);
			FMemory::Memcpy(Destination, UVs.GetData(), BufferSize);
			RHIUnlockVertexBuffer(VertexBufferRHI);
			SRV = RHICreateShaderResourceView(
				VertexBufferRHI,
				sizeof(FVector2D),
				PF_G32R32F);
		}

		virtual void ReleaseRHI() override
		{
			SRV.SafeRelease();
			FVertexBuffer::ReleaseRHI();
		}

		FShaderResourceViewRHIRef SRV;

	private:
		TArray<FVector2D> UVs;
	};

	class FParticleIndexBuffer final : public FIndexBuffer
	{
	public:
		explicit FParticleIndexBuffer(uint32 InNumParticles)
		{
			static const uint32 OctahedronIndices[IndicesPerParticle] =
			{
				0, 2, 3,
				0, 3, 4,
				0, 4, 5,
				0, 5, 2,
				1, 3, 2,
				1, 4, 3,
				1, 5, 4,
				1, 2, 5
			};

			Indices.SetNumUninitialized(InNumParticles * IndicesPerParticle);
			for (uint32 ParticleIndex = 0;
				ParticleIndex < InNumParticles;
				++ParticleIndex)
			{
				const uint32 BaseVertex =
					ParticleIndex * VerticesPerParticle;
				for (uint32 Index = 0; Index < IndicesPerParticle; ++Index)
				{
					Indices[ParticleIndex * IndicesPerParticle + Index] =
						BaseVertex + OctahedronIndices[Index];
				}
			}
		}

		virtual void InitRHI() override
		{
			const uint32 BufferSize = Indices.Num() * sizeof(uint32);
			FRHIResourceCreateInfo CreateInfo;
			CreateInfo.DebugName = TEXT("SPHFluidParticleIndices");
			IndexBufferRHI = RHICreateIndexBuffer(
				sizeof(uint32),
				BufferSize,
				BUF_Static,
				CreateInfo);

			void* Destination = RHILockIndexBuffer(
				IndexBufferRHI,
				0,
				BufferSize,
				RLM_WriteOnly);
			FMemory::Memcpy(Destination, Indices.GetData(), BufferSize);
			RHIUnlockIndexBuffer(IndexBufferRHI);
		}

		uint32 GetPrimitiveCount() const
		{
			return static_cast<uint32>(Indices.Num() / 3);
		}

	private:
		TArray<uint32> Indices;
	};

	class FSequentialIndexBuffer final : public FIndexBuffer
	{
	public:
		explicit FSequentialIndexBuffer(uint32 InVertexCount)
			: VertexCount(InVertexCount)
		{
		}

		virtual void InitRHI() override
		{
			const uint32 BufferSize = VertexCount * sizeof(uint32);
			FRHIResourceCreateInfo CreateInfo;
			CreateInfo.DebugName = TEXT("SPHFluidSurfaceIndices");
			IndexBufferRHI = RHICreateIndexBuffer(
				sizeof(uint32),
				BufferSize,
				BUF_Static,
				CreateInfo);
			uint32* Destination = static_cast<uint32*>(
				RHILockIndexBuffer(
					IndexBufferRHI,
					0,
					BufferSize,
					RLM_WriteOnly));
			for (uint32 Index = 0; Index < VertexCount; ++Index)
			{
				Destination[Index] = Index;
			}
			RHIUnlockIndexBuffer(IndexBufferRHI);
		}

		uint32 GetPrimitiveCount() const
		{
			return VertexCount / 3;
		}

	private:
		uint32 VertexCount;
	};

	BEGIN_SHADER_PARAMETER_STRUCT(FCompositePassParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(
			FSPHScreenCompositePS::FParameters,
			PixelParameters)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
}

class FComputeWaterSceneProxy;

static FComputeWaterSceneProxy* GActiveScreenSpaceWaterProxy = nullptr;
static TSharedPtr<class FComputeWaterViewExtension, ESPMode::ThreadSafe>
	GComputeWaterViewExtension;

class FComputeWaterViewExtension final : public FSceneViewExtensionBase
{
public:
	FComputeWaterViewExtension(const FAutoRegister& AutoRegister)
		: FSceneViewExtensionBase(AutoRegister)
	{
	}

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(
		FSceneViewFamily& InViewFamily,
		FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(
		FSceneViewFamily& InViewFamily) override {}
	virtual void PreRenderViewFamily_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		FSceneViewFamily& InViewFamily) override {}
	virtual void PreRenderView_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		FSceneView& InView) override {}
	virtual bool IsActiveThisFrame(FViewport* InViewport) const override
	{
		return true;
	}

	virtual void SubscribeToPostProcessingPass(
		EPostProcessingPass Pass,
		FAfterPassCallbackDelegateArray& InOutPassCallbacks,
		bool bIsPassEnabled) override;

private:
	FScreenPassTexture AfterTonemap_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);
};

class FComputeWaterSceneProxy final : public FPrimitiveSceneProxy
{
public:
	FComputeWaterSceneProxy(
		const UComputeWaterComponent* Component,
		UMaterialInterface* InMaterial)
		: FPrimitiveSceneProxy(Component)
		, ParticlesPerAxis(
			Component->bUseSourceScreenSpace2Preset
				? 59
				: FMath::Clamp(Component->ParticlesPerAxis, 8, 59))
		, SpawnRegionCount(
			Component->bUseSourceScreenSpace2Preset
				? 2u
				: (Component->bUseSecondSpawnRegion ? 2u : 1u))
		, NumParticles(
			static_cast<uint32>(
				ParticlesPerAxis
					* ParticlesPerAxis
					* ParticlesPerAxis
					* SpawnRegionCount))
		, MaxFoamParticleCount(
			Component->bUseSourceScreenSpace2Preset
				? 1024000u
				: static_cast<uint32>(FMath::Clamp(
					Component->MaxFoamParticleCount,
					1024,
					1024000)))
		, VertexCount(
			NumParticles * SPHFluid::VerticesPerParticle)
		, SurfaceGridSize(
			SPHFluid::CalculateSurfaceGridSize(
				Component->bUseSourceScreenSpace2Preset
					? FVector(3600.0f, 800.0f, 1200.0f)
					: Component->SimulationBoundsCm,
				Component->SurfaceGridResolution))
		, SurfaceGridPointCount(
			static_cast<uint32>(
				SurfaceGridSize.X
					* SurfaceGridSize.Y
					* SurfaceGridSize.Z))
		, SurfaceCubeCount(
			static_cast<uint32>(
				(SurfaceGridSize.X - 1)
					* (SurfaceGridSize.Y - 1)
					* (SurfaceGridSize.Z - 1)))
		, SurfaceVertexCount(
			SurfaceCubeCount * SPHFluid::VerticesPerSurfaceCube)
		, PositionBuffer(VertexCount)
		, TangentBuffer(VertexCount)
		, UVBuffer(VertexCount)
		, IndexBuffer(NumParticles)
		, VertexFactory(
			GetScene().GetFeatureLevel(),
			"FComputeWaterSceneProxy")
		, SurfacePositionBuffer(SurfaceVertexCount)
		, SurfaceTangentBuffer(SurfaceVertexCount)
		, SurfaceUVBuffer(SurfaceVertexCount)
		, SurfaceIndexBuffer(SurfaceVertexCount)
		, SurfaceVertexFactory(
			GetScene().GetFeatureLevel(),
			"FComputeWaterSurfaceSceneProxy")
		, Material(InMaterial)
		, MaterialRelevance(
			InMaterial->GetRelevance_Concurrent(
				GetScene().GetFeatureLevel()))
	{
		TArray<int32> MarchingCubesValues;
		FString MarchingCubesText;
		const FString MarchingCubesPath = FPaths::Combine(
			FPaths::ProjectContentDir(),
			TEXT("ComputeWater/Data/MarchingCubesLUT.txt"));
		if (FFileHelper::LoadFileToString(
				MarchingCubesText,
				*MarchingCubesPath))
		{
			TArray<FString> Tokens;
			MarchingCubesText.ParseIntoArray(
				Tokens,
				TEXT(","),
				true);
			MarchingCubesValues.Reserve(Tokens.Num());
			for (FString& Token : Tokens)
			{
				Token.TrimStartAndEndInline();
				if (!Token.IsEmpty())
				{
					MarchingCubesValues.Add(FCString::Atoi(*Token));
				}
			}
		}
		bSurfaceLUTValid =
			MarchingCubesValues.Num()
				== static_cast<int32>(SPHFluid::MarchingCubesLUTSize);
		if (!bSurfaceLUTValid)
		{
			UE_LOG(
				LogWaterSimulation,
				Error,
				TEXT("Expected %u Marching Cubes LUT entries in '%s', found %d. Falling back to particle rendering."),
				SPHFluid::MarchingCubesLUTSize,
				*MarchingCubesPath,
				MarchingCubesValues.Num());
		}

		SPHFluid::FDynamicData InitialData =
			CreateDynamicData(Component, 0.0f, true);

		FComputeWaterSceneProxy* Self = this;
		ENQUEUE_RENDER_COMMAND(InitializeSPHFluidResources)(
			[Self, InitialData, MarchingCubesValues](
				FRHICommandListImmediate& RHICmdList)
			{
				Self->InitializeResources_RenderThread(
					RHICmdList,
					InitialData,
					MarchingCubesValues);
			});
	}

	virtual ~FComputeWaterSceneProxy() override
	{
		if (GActiveScreenSpaceWaterProxy == this)
		{
			GActiveScreenSpaceWaterProxy = nullptr;
		}
		SurfaceVertexFactory.ReleaseResource();
		SurfaceIndexBuffer.ReleaseResource();
		SurfaceUVBuffer.ReleaseResource();
		SurfaceTangentBuffer.ReleaseResource();
		SurfacePositionBuffer.ReleaseResource();

		VertexFactory.ReleaseResource();
		IndexBuffer.ReleaseResource();
		UVBuffer.ReleaseResource();
		TangentBuffer.ReleaseResource();
		PositionBuffer.ReleaseResource();

		Positions.Release();
		PredictedPositions.Release();
		Velocities.Release();
		Densities.Release();
		SpatialKeys.Release();
		SpatialOffsets.Release();
		SpatialIndices.Release();
		Counts.Release();
		SortedKeys.Release();
		SortedIndices.Release();
		SortTargetPositions.Release();
		SortTargetPredictedPositions.Release();
		SortTargetVelocities.Release();
		WhiteParticles.Release();
		WhiteParticlesCompacted.Release();
		WhiteParticleCounters.Release();
		WhiteParticleDrawArgs.Release();
		DensityGrid.Release();
		MarchingCubesLUT.Release();
		for (int32 Level = 0; Level < ScanLevelCount; ++Level)
		{
			ScanGroupSums[Level].Release();
		}
	}

	static SPHFluid::FDynamicData CreateDynamicData(
		const UComputeWaterComponent* Component,
		float DeltaTime,
		bool bReset)
	{
		SPHFluid::FDynamicData Data;
		const bool bSourcePreset =
			Component->bUseSourceScreenSpace2Preset;
		Data.ParticlesPerAxis = bSourcePreset
			? 59u
			: static_cast<uint32>(
				FMath::Clamp(Component->ParticlesPerAxis, 8, 59));
		Data.SpawnRegionCount = bSourcePreset
			? 2u
			: (Component->bUseSecondSpawnRegion ? 2u : 1u);
		Data.NumParticles = static_cast<uint32>(
			Data.ParticlesPerAxis
			* Data.ParticlesPerAxis
			* Data.ParticlesPerAxis
			* Data.SpawnRegionCount);

		if (bSourcePreset)
		{
			// Fluid ScreenSpace 2.unity values. A +5 m vertical translation
			// places the source's y=-5 floor at UE local Z=0 without changing
			// any relative simulation coordinates.
			Data.BoundsSize = FVector(36.0f, 8.0f, 12.0f);
			Data.BoundsCenter = FVector(0.0f, 0.0f, 6.0f);
			Data.SpawnCenter = FVector(-10.98f, 0.0f, 3.83f);
			Data.SecondSpawnCenter = FVector(10.98f, 0.0f, 3.83f);
			Data.SpawnSize = 7.0f;
			Data.SpawnJitter = 0.035f;
		}
		else
		{
			Data.BoundsSize =
				Component->SimulationBoundsCm
				/ SPHFluid::SimulationToCentimeters;
			Data.BoundsCenter =
				Component->SimulationBoundsCenterCm
				/ SPHFluid::SimulationToCentimeters;
			Data.SpawnCenter =
				Component->SpawnCenterCm
				/ SPHFluid::SimulationToCentimeters;
			Data.SecondSpawnCenter =
				Component->SecondSpawnCenterCm
				/ SPHFluid::SimulationToCentimeters;
			Data.SpawnSize =
				Component->SpawnSizeCm
				/ SPHFluid::SimulationToCentimeters;
			Data.SpawnJitter =
				Component->SpawnJitterCm
				/ SPHFluid::SimulationToCentimeters;
		}
		Data.SmoothingRadius = bSourcePreset
			? 0.2f
			: Component->SmoothingRadiusCm
				/ SPHFluid::SimulationToCentimeters;
		Data.TargetDensity = bSourcePreset
			? 630.0f
			: Component->TargetDensity;
		Data.PressureMultiplier = bSourcePreset
			? 288.0f
			: Component->PressureMultiplier;
		Data.NearPressureMultiplier = bSourcePreset
			? 2.16f
			: Component->NearPressureMultiplier;
		Data.ViscosityStrength = bSourcePreset
			? 0.0f
			: Component->ViscosityStrength;
		Data.Gravity = bSourcePreset
			? -10.0f
			: Component->Gravity;
		Data.CollisionDamping = bSourcePreset
			? 0.95f
			: Component->CollisionDamping;
		Data.ParticleRenderRadiusCm =
			Component->ParticleRenderRadiusCm;
		Data.SurfaceGridResolution =
			FMath::Clamp(Component->SurfaceGridResolution, 24, 96);
		Data.SurfaceIsoLevel =
			FMath::Max(1.0f, Component->SurfaceIsoLevel);
		Data.DeltaTime = DeltaTime;
		Data.ContainerVelocityDelta =
			Component->GetContainerVelocityDeltaMetersPerSecond();
		Data.SimulationSubsteps = bSourcePreset
			? 3
			: FMath::Clamp(Component->SimulationSubsteps, 1, 8);
		Data.bAnimate = Component->bAnimate;
		Data.bReset = bReset;
		Data.bRenderContinuousSurface =
			Component->bRenderContinuousSurface;
		Data.bRenderScreenSpaceSurface = bSourcePreset
			? true
			: Component->bRenderScreenSpaceSurface;
		Data.bFoamActive = bSourcePreset
			? true
			: Component->bFoamActive;
		Data.MaxFoamParticleCount = bSourcePreset
			? 1024000u
			: static_cast<uint32>(FMath::Clamp(
				Component->MaxFoamParticleCount,
				1024,
				1024000));
		Data.TrappedAirSpawnRate = bSourcePreset
			? 120.0f
			: Component->TrappedAirSpawnRate;
		Data.FoamSpawnFadeInTime = bSourcePreset
			? 0.35f
			: Component->FoamSpawnFadeInTime;
		Data.FoamSpawnFadeStartTime = bSourcePreset
			? 0.2f
			: Component->FoamSpawnFadeStartTime;
		Data.TrappedAirVelocityMinMax = bSourcePreset
			? FVector2D(15.0f, 25.0f)
			: Component->TrappedAirVelocityMinMax;
		Data.FoamKineticEnergyMinMax = bSourcePreset
			? FVector2D(15.0f, 30.0f)
			: Component->FoamKineticEnergyMinMax;
		Data.BubbleBuoyancy = bSourcePreset
			? 1.4f
			: Component->BubbleBuoyancy;
		Data.SprayClassifyMaxNeighbours = bSourcePreset
			? 5
			: Component->SprayClassifyMaxNeighbours;
		Data.BubbleClassifyMinNeighbours = bSourcePreset
			? 15
			: Component->BubbleClassifyMinNeighbours;
		Data.BubbleScale = bSourcePreset
			? 0.3f
			: Component->BubbleScale;
		Data.BubbleScaleChangeSpeed = bSourcePreset
			? 7.0f
			: Component->BubbleScaleChangeSpeed;
		return Data;
	}

	FScreenPassTexture RenderScreenSpace_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs)
	{
		check(IsInRenderingThread());
		const FScreenPassTexture SceneColor =
			Inputs.GetInput(EPostProcessMaterialInput::SceneColor);
		if (!bResourcesInitialized
			|| !bRenderScreenSpaceSurface
			|| !SceneColor.IsValid()
			|| GetScene().GetFeatureLevel() < ERHIFeatureLevel::SM5)
		{
			return SceneColor;
		}

		const FIntPoint TextureExtent =
			SceneColor.Texture->Desc.Extent;
		const FIntRect ViewRect = SceneColor.ViewRect;
		const FIntPoint ViewSize = ViewRect.Size();
		if (ViewSize.X <= 0 || ViewSize.Y <= 0)
		{
			return SceneColor;
		}

		// FluidRenderTest.FrameBoundsOrtho: frame the converted 36 x 12 x 8 m
		// source bounds for the current viewport, then add the source 0.5 m.
		const FVector SourceShadowRight(
			-0.12961042f,
			-0.47626843f,
			-0.86969507f);
		const FVector SourceShadowUp(
			0.71920730f,
			0.55864170f,
			-0.41311050f);
		const FVector SourceHalfBoundsCm(1800.0f, 400.0f, 600.0f);
		const float SourceShadowMaxX =
			FMath::Abs(SourceShadowRight.X) * SourceHalfBoundsCm.X
			+ FMath::Abs(SourceShadowRight.Y) * SourceHalfBoundsCm.Y
			+ FMath::Abs(SourceShadowRight.Z) * SourceHalfBoundsCm.Z;
		const float SourceShadowMaxY =
			FMath::Abs(SourceShadowUp.X) * SourceHalfBoundsCm.X
			+ FMath::Abs(SourceShadowUp.Y) * SourceHalfBoundsCm.Y
			+ FMath::Abs(SourceShadowUp.Z) * SourceHalfBoundsCm.Z;
		const float SourceViewAspect =
			static_cast<float>(ViewSize.X)
			/ static_cast<float>(ViewSize.Y);
		const float SourceShadowHalfHeightCm =
			FMath::Max(
				SourceShadowMaxY,
				SourceShadowMaxX / SourceViewAspect)
			+ 50.0f;
		const float SourceShadowHalfWidthCm =
			SourceShadowHalfHeightCm * SourceViewAspect;

		const uint32 ScreenPixelCount =
			static_cast<uint32>(
				TextureExtent.X * TextureExtent.Y);
		const FRDGBufferDesc ScreenBufferDesc =
			FRDGBufferDesc::CreateStructuredDesc(
				sizeof(uint32),
				ScreenPixelCount);
		FRDGBufferRef FluidDepth = GraphBuilder.CreateBuffer(
			ScreenBufferDesc,
			TEXT("SPHFluidDepth"));
		FRDGBufferRef Thickness = GraphBuilder.CreateBuffer(
			ScreenBufferDesc,
			TEXT("SPHFluidThickness"));
		FRDGBufferRef FoamDepth = GraphBuilder.CreateBuffer(
			ScreenBufferDesc,
			TEXT("SPHFoamDepth"));
		FRDGBufferRef FoamMask = GraphBuilder.CreateBuffer(
			ScreenBufferDesc,
			TEXT("SPHFoamMask"));

		float ClearDepthValue = 65504.0f;
		uint32 ClearDepthBits = 0;
		FMemory::Memcpy(
			&ClearDepthBits,
			&ClearDepthValue,
			sizeof(uint32));
		AddClearUAVPass(
			GraphBuilder,
			GraphBuilder.CreateUAV(FluidDepth),
			ClearDepthBits);
		AddClearUAVPass(
			GraphBuilder,
			GraphBuilder.CreateUAV(Thickness),
			0u);
		AddClearUAVPass(
			GraphBuilder,
			GraphBuilder.CreateUAV(FoamDepth),
			ClearDepthBits);
		AddClearUAVPass(
			GraphBuilder,
			GraphBuilder.CreateUAV(FoamMask),
			0u);

		auto AddComputeSplatPass =
			[&](
				const TCHAR* PassName,
				uint32 bUseWhiteParticles,
				uint32 bOccludeThicknessWithFoam,
				float RadiusCm,
				FRDGBufferRef DepthOutput,
				FRDGBufferRef AccumulationOutput,
				FRDGBufferRef FoamOcclusionDepth)
			{
				FSPHScreenSplatCS::FParameters* PassParameters =
					GraphBuilder.AllocParameters<
						FSPHScreenSplatCS::FParameters>();
				PassParameters->View =
					View.ViewUniformBuffer;
				PassParameters->LocalToWorld =
					GetLocalToWorld();
				PassParameters->ViewSize = TextureExtent;
				PassParameters->UseWhiteParticles =
					bUseWhiteParticles;
				PassParameters->MaxParticleCount =
					bUseWhiteParticles != 0
						? MaxFoamParticleCount
						: NumParticles;
				PassParameters->ParticleRadiusCm =
					RadiusCm;
				PassParameters->FoamRadiusCm = 2.0f;
				PassParameters->OccludeThicknessWithFoam =
					bOccludeThicknessWithFoam;
				PassParameters->FluidPositions =
					Positions.SRV;
				PassParameters->WhiteParticles =
					WhiteParticles.SRV;
				PassParameters->WhiteParticleCounters =
					WhiteParticleCounters.SRV;
				PassParameters->FoamOcclusionDepth =
					GraphBuilder.CreateSRV(FoamOcclusionDepth);
				PassParameters->ScreenDepthOutput =
					GraphBuilder.CreateUAV(DepthOutput);
				PassParameters->ScreenAccumulationOutput =
					GraphBuilder.CreateUAV(AccumulationOutput);
				TShaderMapRef<FSPHScreenSplatCS> SplatShader(
					GetGlobalShaderMap(View.GetFeatureLevel()));
				const FIntVector DispatchGroups(
						FMath::DivideAndRoundUp(
							bUseWhiteParticles != 0
								? MaxFoamParticleCount
								: NumParticles,
							256u),
						1,
						1);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("%s", PassName),
					SplatShader,
					PassParameters,
					DispatchGroups);
			};

		if (CurrentDynamicData.bFoamActive)
		{
			AddComputeSplatPass(
				TEXT("SPH Foam Screen Splat"),
				1u,
				0u,
				2.0f,
				FoamDepth,
				FoamMask,
				FluidDepth);
		}

		AddComputeSplatPass(
			TEXT("SPH Fluid Screen Splat"),
			0u,
			CurrentDynamicData.bFoamActive ? 1u : 0u,
			10.0f,
			FluidDepth,
			Thickness,
			FoamDepth);

		const FIntPoint ShadowExtent(
			FMath::Max(1, TextureExtent.X / 4),
			FMath::Max(1, TextureExtent.Y / 4));
		const FRDGBufferDesc ShadowBufferDesc =
			FRDGBufferDesc::CreateStructuredDesc(
				sizeof(uint32),
				static_cast<uint32>(
					ShadowExtent.X * ShadowExtent.Y));
		FRDGBufferRef RawShadow =
			GraphBuilder.CreateBuffer(
				ShadowBufferDesc,
				TEXT("SPHSourceRawShadow"));
		FRDGBufferRef HorizontalShadow =
			GraphBuilder.CreateBuffer(
				ShadowBufferDesc,
				TEXT("SPHSourceHorizontalShadow"));
		FRDGBufferRef SmoothedShadow =
			GraphBuilder.CreateBuffer(
				ShadowBufferDesc,
				TEXT("SPHSourceSmoothedShadow"));
		AddClearUAVPass(
			GraphBuilder,
			GraphBuilder.CreateUAV(RawShadow),
			0u);

		{
			FSPHShadowSplatCS::FParameters* ShadowParameters =
				GraphBuilder.AllocParameters<
					FSPHShadowSplatCS::FParameters>();
			ShadowParameters->MaxParticleCount = NumParticles;
			ShadowParameters->ShadowSize = ShadowExtent;
			ShadowParameters->SourceShadowHalfWidthCm =
				SourceShadowHalfWidthCm;
			ShadowParameters->SourceShadowHalfHeightCm =
				SourceShadowHalfHeightCm;
			ShadowParameters->FluidPositions = Positions.SRV;
			ShadowParameters->ShadowAccumulationOutput =
				GraphBuilder.CreateUAV(RawShadow);
			TShaderMapRef<FSPHShadowSplatCS> ShadowShader(
				GetGlobalShaderMap(View.GetFeatureLevel()));
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SPH Source Shadow Splat"),
				ShadowShader,
				ShadowParameters,
				FIntVector(
					FMath::DivideAndRoundUp(
						NumParticles,
						256u),
					1,
					1));
		}

		auto AddShadowSmoothPass =
			[&](
				const TCHAR* PassName,
				FRDGBufferRef Input,
				FRDGBufferRef Output,
				const FIntPoint& Direction,
				uint32 bInputIsFixedPoint)
			{
				FSPHShadowSmoothCS::FParameters* Parameters =
					GraphBuilder.AllocParameters<
						FSPHShadowSmoothCS::FParameters>();
				Parameters->ShadowSize = ShadowExtent;
				Parameters->ShadowFilterDirection = Direction;
				Parameters->ShadowInputIsFixedPoint =
					bInputIsFixedPoint;
				Parameters->ShadowInputBuffer =
					GraphBuilder.CreateSRV(Input);
				Parameters->ShadowOutputBuffer =
					GraphBuilder.CreateUAV(Output);
				TShaderMapRef<FSPHShadowSmoothCS> Shader(
					GetGlobalShaderMap(View.GetFeatureLevel()));
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("%s", PassName),
					Shader,
					Parameters,
					FIntVector(
						FMath::DivideAndRoundUp(
							ShadowExtent.X,
							8),
						FMath::DivideAndRoundUp(
							ShadowExtent.Y,
							8),
						1));
			};
		AddShadowSmoothPass(
			TEXT("SPH Source Shadow Horizontal"),
			RawShadow,
			HorizontalShadow,
			FIntPoint(1, 0),
			1u);
		AddShadowSmoothPass(
			TEXT("SPH Source Shadow Vertical"),
			HorizontalShadow,
			SmoothedShadow,
			FIntPoint(0, 1),
			0u);

		const float ProjectionScaleX =
			View.ViewMatrices.GetProjectionMatrix().M[0][0];
		FRDGBufferRef SmoothedDepth =
			GraphBuilder.CreateBuffer(
				ScreenBufferDesc,
				TEXT("SPHFluidDepthDecodeHorizontal"));
		FRDGBufferRef SmoothedThickness =
			GraphBuilder.CreateBuffer(
				ScreenBufferDesc,
				TEXT("SPHFluidThicknessDecodeHorizontal"));
		FSPHDecodeSmoothDepthCS::FParameters* DecodeParameters =
			GraphBuilder.AllocParameters<
				FSPHDecodeSmoothDepthCS::FParameters>();
		DecodeParameters->ViewSize = TextureExtent;
		DecodeParameters->FilterDirection = FIntPoint(1, 0);
		DecodeParameters->ProjectionScaleX = ProjectionScaleX;
		DecodeParameters->WorldFilterRadiusCm = 2.0f;
		DecodeParameters->MaxFilterRadius = 32u;
		DecodeParameters->FilterStrength = 0.45f;
		DecodeParameters->DepthDifferenceStrength = 3.7f;
		DecodeParameters->DecodeInputDepth =
			GraphBuilder.CreateSRV(FluidDepth);
		DecodeParameters->DecodeInputThickness =
			GraphBuilder.CreateSRV(Thickness);
		DecodeParameters->DecodeOutputDepth =
			GraphBuilder.CreateUAV(SmoothedDepth);
		DecodeParameters->DecodeOutputThickness =
			GraphBuilder.CreateUAV(SmoothedThickness);
		TShaderMapRef<FSPHDecodeSmoothDepthCS> DecodeShader(
			GetGlobalShaderMap(View.GetFeatureLevel()));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SPH Bilateral Depth 0 H Decode"),
			DecodeShader,
			DecodeParameters,
			FIntVector(
				FMath::DivideAndRoundUp(TextureExtent.X, 8),
				FMath::DivideAndRoundUp(TextureExtent.Y, 8),
				1));

		for (int32 Iteration = 0; Iteration < 5; ++Iteration)
		{
			const int32 FirstAxis = Iteration == 0 ? 1 : 0;
			for (int32 Axis = FirstAxis; Axis < 2; ++Axis)
			{
				FRDGBufferRef SmoothOutput =
					GraphBuilder.CreateBuffer(
						ScreenBufferDesc,
						Axis == 0
							? TEXT("SPHFluidDepthSmoothHorizontal")
							: TEXT("SPHFluidDepthSmoothVertical"));
				FRDGBufferRef SmoothThicknessOutput =
					GraphBuilder.CreateBuffer(
						ScreenBufferDesc,
						Axis == 0
							? TEXT("SPHFluidThicknessSmoothHorizontal")
							: TEXT("SPHFluidThicknessSmoothVertical"));
				FSPHSmoothDepthCS::FParameters* SmoothParameters =
					GraphBuilder.AllocParameters<
						FSPHSmoothDepthCS::FParameters>();
				SmoothParameters->ViewSize = TextureExtent;
				SmoothParameters->FilterDirection =
					Axis == 0
						? FIntPoint(1, 0)
						: FIntPoint(0, 1);
				SmoothParameters->ProjectionScaleX =
					ProjectionScaleX;
				SmoothParameters->WorldFilterRadiusCm = 2.0f;
				SmoothParameters->MaxFilterRadius = 32u;
				SmoothParameters->FilterStrength = 0.45f;
				SmoothParameters->DepthDifferenceStrength = 3.7f;
				SmoothParameters->RawDepthBuffer =
					GraphBuilder.CreateSRV(FluidDepth);
				SmoothParameters->InputDepthBuffer =
					GraphBuilder.CreateSRV(SmoothedDepth);
				SmoothParameters->InputThicknessBuffer =
					GraphBuilder.CreateSRV(SmoothedThickness);
				SmoothParameters->OutputDepthBuffer =
					GraphBuilder.CreateUAV(SmoothOutput);
				SmoothParameters->OutputThicknessBuffer =
					GraphBuilder.CreateUAV(SmoothThicknessOutput);
				TShaderMapRef<FSPHSmoothDepthCS> SmoothShader(
					GetGlobalShaderMap(View.GetFeatureLevel()));
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME(
						"SPH Bilateral Depth %d %s",
						Iteration,
						Axis == 0 ? TEXT("H") : TEXT("V")),
					SmoothShader,
					SmoothParameters,
					FIntVector(
						FMath::DivideAndRoundUp(TextureExtent.X, 8),
						FMath::DivideAndRoundUp(TextureExtent.Y, 8),
						1));
				SmoothedDepth = SmoothOutput;
				SmoothedThickness = SmoothThicknessOutput;
			}
		}

		FScreenPassRenderTarget CompositeRenderTarget;
		if (Inputs.OverrideOutput.IsValid())
		{
			CompositeRenderTarget = Inputs.OverrideOutput;
		}
		else
		{
			FRDGTextureDesc OutputDesc = SceneColor.Texture->Desc;
			OutputDesc.Flags |=
				TexCreate_RenderTargetable | TexCreate_ShaderResource;
			FRDGTextureRef CompositeOutput =
				GraphBuilder.CreateTexture(
					OutputDesc,
					TEXT("SPHFluidComposite"));
			CompositeRenderTarget = FScreenPassRenderTarget(
				CompositeOutput,
				ViewRect,
				ERenderTargetLoadAction::ENoAction);
		}
		SPHFluid::FCompositePassParameters* CompositeParameters =
			GraphBuilder.AllocParameters<
				SPHFluid::FCompositePassParameters>();
		CompositeParameters->PixelParameters.View =
			View.ViewUniformBuffer;
		const FMatrix ComponentLocalToWorld = GetLocalToWorld();
		CompositeParameters->PixelParameters.ViewSize = TextureExtent;
		CompositeParameters->PixelParameters.ExtinctionCoefficients =
			FVector(4.75f, 0.53f, 0.33f);
		CompositeParameters->PixelParameters.ExtinctionMultiplier =
			1.31f;
		CompositeParameters->PixelParameters.RefractionMultiplier =
			1.25f;
		CompositeParameters->PixelParameters.BoundsSizeCm =
			FVector(3600.0f, 800.0f, 1200.0f);
		CompositeParameters->PixelParameters.DirToSunWorld =
			ComponentLocalToWorld
				.TransformVector(
					FVector(
						0.68259943f,
						-0.67903448f,
						0.27012995f))
				.GetSafeNormal();
		CompositeParameters->PixelParameters.CameraRightWorld =
			View.GetViewRight().GetSafeNormal();
		CompositeParameters->PixelParameters.CameraUpWorld =
			View.GetViewUp().GetSafeNormal();
		CompositeParameters->PixelParameters.CameraForwardWorld =
			View.GetViewDirection().GetSafeNormal();
		CompositeParameters->PixelParameters.CameraTanHalfFov =
			FVector2D(
				1.0f / ProjectionScaleX,
				1.0f
					/ View.ViewMatrices
						.GetProjectionMatrix()
						.M[1][1]);
		CompositeParameters->PixelParameters.CameraPositionWorld =
			View.ViewMatrices.GetViewOrigin();
		CompositeParameters->PixelParameters.SimulationOriginWorld =
			ComponentLocalToWorld.GetOrigin();
		CompositeParameters->PixelParameters.ShadowSize =
			ShadowExtent;
		CompositeParameters->PixelParameters.SourceShadowHalfWidthCm =
			SourceShadowHalfWidthCm;
		CompositeParameters->PixelParameters.SourceShadowHalfHeightCm =
			SourceShadowHalfHeightCm;
		CompositeParameters->PixelParameters.FoamMax =
			MaxFoamParticleCount;
		CompositeParameters->PixelParameters.WhiteParticleCounters =
			WhiteParticleCounters.SRV;
		CompositeParameters->PixelParameters.FluidDepthBuffer =
			GraphBuilder.CreateSRV(SmoothedDepth);
		CompositeParameters->PixelParameters.SmoothedThicknessBuffer =
			GraphBuilder.CreateSRV(SmoothedThickness);
		CompositeParameters->PixelParameters.FoamDepthBuffer =
			GraphBuilder.CreateSRV(FoamDepth);
		CompositeParameters->PixelParameters.FoamMaskBuffer =
			GraphBuilder.CreateSRV(FoamMask);
		CompositeParameters->PixelParameters.ShadowBuffer =
			GraphBuilder.CreateSRV(SmoothedShadow);
		CompositeParameters->RenderTargets[0] =
			CompositeRenderTarget.GetRenderTargetBinding();

		TShaderMapRef<FScreenPassVS> CompositeVertexShader(
			GetGlobalShaderMap(View.GetFeatureLevel()));
		TShaderMapRef<FSPHScreenCompositePS> CompositePixelShader(
			GetGlobalShaderMap(View.GetFeatureLevel()));
		const FScreenPassTextureViewport OutputViewport(
			CompositeRenderTarget);
		const FScreenPassTextureViewport InputViewport(SceneColor);
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("SPH Fluid Composite"),
			CompositeParameters,
			ERDGPassFlags::Raster,
			[
				CompositeParameters,
				CompositeVertexShader,
				CompositePixelShader,
				OutputViewport,
				InputViewport,
				&View
			](FRHICommandListImmediate& RHICmdList)
			{
				DrawScreenPass(
					RHICmdList,
					static_cast<const FViewInfo&>(View),
					OutputViewport,
					InputViewport,
					FScreenPassPipelineState(
						CompositeVertexShader,
						CompositePixelShader),
					EScreenPassDrawFlags::None,
					[&](
						FRHICommandListImmediate&
							ImmediateRHICmdList)
					{
						SetShaderParameters(
							ImmediateRHICmdList,
							CompositePixelShader,
							CompositePixelShader.GetPixelShader(),
							CompositeParameters->PixelParameters);
					});
			});

		return MoveTemp(CompositeRenderTarget);
	}

	virtual SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	void UpdateSimulation_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		const SPHFluid::FDynamicData& Data)
	{
		check(IsInRenderingThread());
		if (!bResourcesInitialized
			|| GetScene().GetFeatureLevel() < ERHIFeatureLevel::SM5)
		{
			return;
		}
		CurrentDynamicData = Data;

		if (Data.bReset)
		{
			SimulationTime = 0.0f;
			InitializeParticles_RenderThread(RHICmdList, Data);
		}

		if (Data.bAnimate)
		{
			const int32 SubstepCount =
				FMath::Clamp(Data.SimulationSubsteps, 1, 8);
			const float FrameDeltaTime =
				FMath::Clamp(Data.DeltaTime, 0.0f, 1.0f / 60.0f);
			const float SubstepDeltaTime =
				FrameDeltaTime / static_cast<float>(SubstepCount);

			for (int32 Substep = 0; Substep < SubstepCount; ++Substep)
			{
				SimulationTime += SubstepDeltaTime;
				RunSimulationStep_RenderThread(
					RHICmdList,
					Data,
					SubstepDeltaTime,
					Substep == 0
						? Data.ContainerVelocityDelta
						: FVector::ZeroVector);
			}
		}

		if (Data.bAnimate && Data.bFoamActive)
		{
			UpdateWhiteParticles_RenderThread(
				RHICmdList,
				Data,
				FMath::Clamp(Data.DeltaTime, 0.0f, 1.0f / 60.0f));
		}
		PrepareWhiteParticleDrawArgs_RenderThread(RHICmdList);

		bRenderScreenSpaceSurface = Data.bRenderScreenSpaceSurface;
		bRenderContinuousSurface =
			!bRenderScreenSpaceSurface
			&& Data.bRenderContinuousSurface
			&& bSurfaceLUTValid;
		if (bRenderScreenSpaceSurface)
		{
			// The post-process view extension consumes the live SPH buffers.
		}
		else if (bRenderContinuousSurface)
		{
			BuildSurfaceMesh_RenderThread(RHICmdList, Data);
		}
		else
		{
			BuildParticleMesh_RenderThread(RHICmdList, Data);
		}
	}

	virtual void GetDynamicMeshElements(
		const TArray<const FSceneView*>& Views,
		const FSceneViewFamily& ViewFamily,
		uint32 VisibilityMap,
		FMeshElementCollector& Collector) const override
	{
		if (bRenderScreenSpaceSurface)
		{
			return;
		}

		const bool bUseSurface =
			bRenderContinuousSurface && bSurfaceMeshReady;
		if (!bUseSurface && !bParticleMeshReady)
		{
			return;
		}

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			if ((VisibilityMap & (1 << ViewIndex)) == 0)
			{
				continue;
			}

			FMeshBatch& Mesh = Collector.AllocateMesh();
			Mesh.VertexFactory =
				bUseSurface ? &SurfaceVertexFactory : &VertexFactory;
			Mesh.MaterialRenderProxy = Material->GetRenderProxy();
			Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
			Mesh.CastShadow = IsShadowCast(Views[ViewIndex]);
			Mesh.Type = PT_TriangleList;
			Mesh.DepthPriorityGroup = SDPG_World;
			Mesh.bCanApplyViewModeOverrides = true;
			Mesh.bDisableBackfaceCulling = bUseSurface;

			FMeshBatchElement& Element = Mesh.Elements[0];
			Element.IndexBuffer = bUseSurface
				? static_cast<const FIndexBuffer*>(&SurfaceIndexBuffer)
				: static_cast<const FIndexBuffer*>(&IndexBuffer);
			Element.FirstIndex = 0;
			Element.NumPrimitives = bUseSurface
				? SurfaceIndexBuffer.GetPrimitiveCount()
				: IndexBuffer.GetPrimitiveCount();
			Element.MinVertexIndex = 0;
			Element.MaxVertexIndex = bUseSurface
				? SurfaceVertexCount - 1
				: VertexCount - 1;

			Collector.AddMesh(ViewIndex, Mesh);
		}
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(
		const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = IsShown(View);
		Result.bDynamicRelevance = true;
		Result.bShadowRelevance = IsShadowCast(View);
		Result.bRenderInMainPass = ShouldRenderInMainPass();
		Result.bEditorPrimitiveRelevance = UseEditorCompositing(View);
		MaterialRelevance.SetPrimitiveViewRelevance(Result);
		Result.bVelocityRelevance =
			IsMovable() && Result.bOpaque && Result.bRenderInMainPass;
		return Result;
	}

	virtual bool CanBeOccluded() const override
	{
		return !MaterialRelevance.bDisableDepthTest;
	}

	virtual uint32 GetMemoryFootprint() const override
	{
		return sizeof(*this) + GetAllocatedSize();
	}

	uint32 GetAllocatedSize() const
	{
		return FPrimitiveSceneProxy::GetAllocatedSize();
	}

private:
	void InitializeResources_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		const SPHFluid::FDynamicData& InitialData,
		const TArray<int32>& MarchingCubesValues)
	{
		check(IsInRenderingThread());

		Positions.Initialize(
			sizeof(FVector4),
			NumParticles,
			BUF_Static,
			TEXT("SPHPositions"));
		PredictedPositions.Initialize(
			sizeof(FVector4),
			NumParticles,
			BUF_Static,
			TEXT("SPHPredictedPositions"));
		Velocities.Initialize(
			sizeof(FVector4),
			NumParticles,
			BUF_Static,
			TEXT("SPHVelocities"));
		Densities.Initialize(
			sizeof(FVector2D),
			NumParticles,
			BUF_Static,
			TEXT("SPHDensities"));
		WhiteParticles.Initialize(
			sizeof(FVector4) * 2,
			MaxFoamParticleCount,
			BUF_Static,
			TEXT("SPHWhiteParticles"));
		WhiteParticlesCompacted.Initialize(
			sizeof(FVector4) * 2,
			MaxFoamParticleCount,
			BUF_Static,
			TEXT("SPHWhiteParticlesCompacted"));
		WhiteParticleCounters.Initialize(
			sizeof(uint32),
			2,
			BUF_Static,
			TEXT("SPHWhiteParticleCounters"));
		WhiteParticleDrawArgs.Initialize(
			sizeof(uint32),
			4,
			PF_R32_UINT,
			ERHIAccess::UAVCompute,
			BUF_DrawIndirect,
			TEXT("SPHWhiteParticleDrawArgs"));

		SpatialKeys.Initialize(
			sizeof(uint32),
			NumParticles,
			BUF_Static,
			TEXT("SPHSpatialKeys"));
		SpatialOffsets.Initialize(
			sizeof(uint32),
			NumParticles,
			BUF_Static,
			TEXT("SPHSpatialOffsets"));
		SpatialIndices.Initialize(
			sizeof(uint32),
			NumParticles,
			BUF_Static,
			TEXT("SPHSpatialIndices"));
		Counts.Initialize(
			sizeof(uint32),
			NumParticles,
			BUF_Static,
			TEXT("SPHSortCounts"));
		SortedKeys.Initialize(
			sizeof(uint32),
			NumParticles,
			BUF_Static,
			TEXT("SPHSortedKeys"));
		SortedIndices.Initialize(
			sizeof(uint32),
			NumParticles,
			BUF_Static,
			TEXT("SPHSortedIndices"));

		SortTargetPositions.Initialize(
			sizeof(FVector4),
			NumParticles,
			BUF_Static,
			TEXT("SPHSortTargetPositions"));
		SortTargetPredictedPositions.Initialize(
			sizeof(FVector4),
			NumParticles,
			BUF_Static,
			TEXT("SPHSortTargetPredictedPositions"));
		SortTargetVelocities.Initialize(
			sizeof(FVector4),
			NumParticles,
			BUF_Static,
			TEXT("SPHSortTargetVelocities"));
		DensityGrid.Initialize(
			sizeof(float),
			SurfaceGridPointCount,
			BUF_Static,
			TEXT("SPHDensityGrid"));
		MarchingCubesLUT.Initialize(
			sizeof(int32),
			SPHFluid::MarchingCubesLUTSize,
			BUF_Static,
			TEXT("SPHMarchingCubesLUT"));

		if (bSurfaceLUTValid)
		{
			const uint32 LUTSizeBytes =
				SPHFluid::MarchingCubesLUTSize * sizeof(int32);
			void* LUTDestination = RHILockStructuredBuffer(
				MarchingCubesLUT.Buffer,
				0,
				LUTSizeBytes,
				RLM_WriteOnly);
			FMemory::Memcpy(
				LUTDestination,
				MarchingCubesValues.GetData(),
				LUTSizeBytes);
			RHIUnlockStructuredBuffer(MarchingCubesLUT.Buffer);
		}

		uint32 ScanItemCount = NumParticles;
		while (ScanLevelCount < SPHFluid::MaxScanLevels)
		{
			const uint32 GroupCount = FMath::DivideAndRoundUp(
				ScanItemCount,
				SPHFluid::ScanItemsPerGroup);
			ScanGroupSums[ScanLevelCount].Initialize(
				sizeof(uint32),
				FMath::Max(1u, GroupCount),
				BUF_Static,
				TEXT("SPHScanGroupSums"));
			++ScanLevelCount;
			if (GroupCount <= 1)
			{
				break;
			}
			ScanItemCount = GroupCount;
		}

		PositionBuffer.InitResource();
		TangentBuffer.InitResource();
		UVBuffer.InitResource();
		IndexBuffer.InitResource();

		FLocalVertexFactory::FDataType VertexData;
		VertexData.PositionComponent = FVertexStreamComponent(
			&PositionBuffer,
			0,
			3 * sizeof(float),
			VET_Float3);
		VertexData.PositionComponentSRV = PositionBuffer.SRV;

		VertexData.TangentBasisComponents[0] = FVertexStreamComponent(
			&TangentBuffer,
			0,
			2 * sizeof(FVector4),
			VET_Float4,
			EVertexStreamUsage::ManualFetch);
		VertexData.TangentBasisComponents[1] = FVertexStreamComponent(
			&TangentBuffer,
			sizeof(FVector4),
			2 * sizeof(FVector4),
			VET_Float4,
			EVertexStreamUsage::ManualFetch);
		VertexData.TangentsSRV = TangentBuffer.SRV;

		VertexData.TextureCoordinates.Add(FVertexStreamComponent(
			&UVBuffer,
			0,
			sizeof(FVector2D),
			VET_Float2,
			EVertexStreamUsage::ManualFetch));
		VertexData.TextureCoordinatesSRV = UVBuffer.SRV;
		VertexData.NumTexCoords = 1;
		VertexData.LightMapCoordinateIndex = 0;

		VertexFactory.SetData(VertexData);
		VertexFactory.InitResource();

		SurfacePositionBuffer.InitResource();
		SurfaceTangentBuffer.InitResource();
		SurfaceUVBuffer.InitResource();
		SurfaceIndexBuffer.InitResource();

		FLocalVertexFactory::FDataType SurfaceVertexData;
		SurfaceVertexData.PositionComponent = FVertexStreamComponent(
			&SurfacePositionBuffer,
			0,
			3 * sizeof(float),
			VET_Float3);
		SurfaceVertexData.PositionComponentSRV =
			SurfacePositionBuffer.SRV;

		SurfaceVertexData.TangentBasisComponents[0] =
			FVertexStreamComponent(
				&SurfaceTangentBuffer,
				0,
				2 * sizeof(FVector4),
				VET_Float4,
				EVertexStreamUsage::ManualFetch);
		SurfaceVertexData.TangentBasisComponents[1] =
			FVertexStreamComponent(
				&SurfaceTangentBuffer,
				sizeof(FVector4),
				2 * sizeof(FVector4),
				VET_Float4,
				EVertexStreamUsage::ManualFetch);
		SurfaceVertexData.TangentsSRV =
			SurfaceTangentBuffer.SRV;

		SurfaceVertexData.TextureCoordinates.Add(
			FVertexStreamComponent(
				&SurfaceUVBuffer,
				0,
				sizeof(FVector2D),
				VET_Float2,
				EVertexStreamUsage::ManualFetch));
		SurfaceVertexData.TextureCoordinatesSRV =
			SurfaceUVBuffer.SRV;
		SurfaceVertexData.NumTexCoords = 1;
		SurfaceVertexData.LightMapCoordinateIndex = 0;

		SurfaceVertexFactory.SetData(SurfaceVertexData);
		SurfaceVertexFactory.InitResource();

		bResourcesInitialized = true;
		CurrentDynamicData = InitialData;
		GActiveScreenSpaceWaterProxy = this;
		InitializeParticles_RenderThread(RHICmdList, InitialData);
		bRenderScreenSpaceSurface =
			InitialData.bRenderScreenSpaceSurface;
		bRenderContinuousSurface =
			!bRenderScreenSpaceSurface
			&& InitialData.bRenderContinuousSurface
			&& bSurfaceLUTValid;
		if (bRenderScreenSpaceSurface)
		{
			PrepareWhiteParticleDrawArgs_RenderThread(RHICmdList);
		}
		else if (bRenderContinuousSurface)
		{
			BuildSurfaceMesh_RenderThread(RHICmdList, InitialData);
		}
		else
		{
			BuildParticleMesh_RenderThread(RHICmdList, InitialData);
		}
	}

	void InitializeParticles_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		const SPHFluid::FDynamicData& Data)
	{
		FSPHInitializeCS::FParameters Parameters;
		Parameters.NumParticles = NumParticles;
		Parameters.ParticlesPerAxis = ParticlesPerAxis;
		Parameters.SpawnRegionCount = SpawnRegionCount;
		Parameters.SpawnCenter = Data.SpawnCenter;
		Parameters.SecondSpawnCenter = Data.SecondSpawnCenter;
		Parameters.SpawnSize = FMath::Max(0.2f, Data.SpawnSize);
		Parameters.JitterStrength = FMath::Max(0.0f, Data.SpawnJitter);
		Parameters.Positions = Positions.UAV;
		Parameters.PredictedPositions = PredictedPositions.UAV;
		Parameters.Velocities = Velocities.UAV;
		Parameters.Densities = Densities.UAV;

		TShaderMapRef<FSPHInitializeCS> Shader(
			GetGlobalShaderMap(GetScene().GetFeatureLevel()));
		FComputeShaderUtils::Dispatch(
			RHICmdList,
			Shader,
			Parameters,
			SPHFluid::ParticleDispatch(NumParticles));
		SPHFluid::UAVBarrier(RHICmdList, Positions.UAV);
		SPHFluid::UAVBarrier(RHICmdList, PredictedPositions.UAV);
		SPHFluid::UAVBarrier(RHICmdList, Velocities.UAV);
		SPHFluid::UAVBarrier(RHICmdList, Densities.UAV);
		RHICmdList.ClearUAVUint(
			WhiteParticleCounters.UAV,
			FUintVector4(0u, 0u, 0u, 0u));
		SPHFluid::UAVBarrier(
			RHICmdList,
			WhiteParticleCounters.UAV);
	}

	void RunSimulationStep_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		const SPHFluid::FDynamicData& Data,
		float StepDeltaTime,
		const FVector& ContainerVelocityDelta)
	{
		const FIntVector Dispatch =
			SPHFluid::ParticleDispatch(NumParticles);
		const float Radius = FMath::Max(0.01f, Data.SmoothingRadius);
		const float KSpikyPow2 =
			15.0f / (2.0f * PI * FMath::Pow(Radius, 5.0f));
		const float KSpikyPow3 =
			15.0f / (PI * FMath::Pow(Radius, 6.0f));
		const float KSpikyPow2Grad =
			15.0f / (PI * FMath::Pow(Radius, 5.0f));
		const float KSpikyPow3Grad =
			45.0f / (PI * FMath::Pow(Radius, 6.0f));

		{
			FSPHExternalForcesCS::FParameters Parameters;
			Parameters.NumParticles = NumParticles;
			Parameters.DeltaTime = StepDeltaTime;
			Parameters.Gravity = Data.Gravity;
			Parameters.ContainerVelocityDelta =
				ContainerVelocityDelta;
			Parameters.Positions = Positions.UAV;
			Parameters.PredictedPositions = PredictedPositions.UAV;
			Parameters.Velocities = Velocities.UAV;
			TShaderMapRef<FSPHExternalForcesCS> Shader(
				GetGlobalShaderMap(GetScene().GetFeatureLevel()));
			FComputeShaderUtils::Dispatch(
				RHICmdList,
				Shader,
				Parameters,
				Dispatch);
			SPHFluid::UAVBarrier(RHICmdList, PredictedPositions.UAV);
			SPHFluid::UAVBarrier(RHICmdList, Velocities.UAV);
		}

		{
			FSPHUpdateSpatialHashCS::FParameters Parameters;
			Parameters.NumParticles = NumParticles;
			Parameters.SmoothingRadius = Radius;
			Parameters.PredictedPositions = PredictedPositions.UAV;
			Parameters.SpatialKeys = SpatialKeys.UAV;
			TShaderMapRef<FSPHUpdateSpatialHashCS> Shader(
				GetGlobalShaderMap(GetScene().GetFeatureLevel()));
			FComputeShaderUtils::Dispatch(
				RHICmdList,
				Shader,
				Parameters,
				Dispatch);
			SPHFluid::UAVBarrier(RHICmdList, SpatialKeys.UAV);
		}

		RunSpatialSort_RenderThread(RHICmdList);

		{
			FSPHReorderCS::FParameters Parameters;
			Parameters.NumParticles = NumParticles;
			Parameters.SortedIndices = SpatialIndices.UAV;
			Parameters.Positions = Positions.UAV;
			Parameters.PredictedPositions = PredictedPositions.UAV;
			Parameters.Velocities = Velocities.UAV;
			Parameters.SortTargetPositions = SortTargetPositions.UAV;
			Parameters.SortTargetPredictedPositions =
				SortTargetPredictedPositions.UAV;
			Parameters.SortTargetVelocities = SortTargetVelocities.UAV;
			TShaderMapRef<FSPHReorderCS> Shader(
				GetGlobalShaderMap(GetScene().GetFeatureLevel()));
			FComputeShaderUtils::Dispatch(
				RHICmdList,
				Shader,
				Parameters,
				Dispatch);
			SPHFluid::UAVBarrier(
				RHICmdList,
				SortTargetPositions.UAV);
			SPHFluid::UAVBarrier(
				RHICmdList,
				SortTargetPredictedPositions.UAV);
			SPHFluid::UAVBarrier(
				RHICmdList,
				SortTargetVelocities.UAV);
		}

		{
			FSPHReorderCopyBackCS::FParameters Parameters;
			Parameters.NumParticles = NumParticles;
			Parameters.Positions = Positions.UAV;
			Parameters.PredictedPositions = PredictedPositions.UAV;
			Parameters.Velocities = Velocities.UAV;
			Parameters.SortTargetPositions = SortTargetPositions.UAV;
			Parameters.SortTargetPredictedPositions =
				SortTargetPredictedPositions.UAV;
			Parameters.SortTargetVelocities = SortTargetVelocities.UAV;
			TShaderMapRef<FSPHReorderCopyBackCS> Shader(
				GetGlobalShaderMap(GetScene().GetFeatureLevel()));
			FComputeShaderUtils::Dispatch(
				RHICmdList,
				Shader,
				Parameters,
				Dispatch);
			SPHFluid::UAVBarrier(RHICmdList, Positions.UAV);
			SPHFluid::UAVBarrier(RHICmdList, PredictedPositions.UAV);
			SPHFluid::UAVBarrier(RHICmdList, Velocities.UAV);
		}

		{
			FSPHCalculateDensitiesCS::FParameters Parameters;
			Parameters.NumParticles = NumParticles;
			Parameters.SmoothingRadius = Radius;
			Parameters.KSpikyPow2 = KSpikyPow2;
			Parameters.KSpikyPow3 = KSpikyPow3;
			Parameters.PredictedPositions = PredictedPositions.UAV;
			Parameters.Densities = Densities.UAV;
			Parameters.SpatialKeys = SpatialKeys.UAV;
			Parameters.SpatialOffsets = SpatialOffsets.UAV;
			TShaderMapRef<FSPHCalculateDensitiesCS> Shader(
				GetGlobalShaderMap(GetScene().GetFeatureLevel()));
			FComputeShaderUtils::Dispatch(
				RHICmdList,
				Shader,
				Parameters,
				Dispatch);
			SPHFluid::UAVBarrier(RHICmdList, Densities.UAV);
		}

		{
			FSPHCalculatePressureCS::FParameters Parameters;
			Parameters.NumParticles = NumParticles;
			Parameters.DeltaTime = StepDeltaTime;
			Parameters.SmoothingRadius = Radius;
			Parameters.TargetDensity = Data.TargetDensity;
			Parameters.PressureMultiplier = Data.PressureMultiplier;
			Parameters.NearPressureMultiplier =
				Data.NearPressureMultiplier;
			Parameters.KSpikyPow2Grad = KSpikyPow2Grad;
			Parameters.KSpikyPow3Grad = KSpikyPow3Grad;
			Parameters.SimTime = SimulationTime;
			const float FadeTime =
				FMath::Max(0.0f, Data.FoamSpawnFadeInTime);
			const float FadeT = FadeTime <= 0.0f
				? 1.0f
				: FMath::Clamp(
					(SimulationTime - Data.FoamSpawnFadeStartTime)
						/ FadeTime,
					0.0f,
					1.0f);
			Parameters.TrappedAirSpawnRate =
				Data.TrappedAirSpawnRate * FadeT * FadeT;
			Parameters.TrappedAirVelocityMinMax =
				Data.TrappedAirVelocityMinMax;
			Parameters.KineticEnergyMinMax =
				Data.FoamKineticEnergyMinMax;
			Parameters.BubbleScale = Data.BubbleScale;
			Parameters.MaxWhiteParticleCount =
				MaxFoamParticleCount;
			Parameters.FoamActive = Data.bFoamActive ? 1u : 0u;
			Parameters.PredictedPositions = PredictedPositions.UAV;
			Parameters.Velocities = Velocities.UAV;
			Parameters.Densities = Densities.UAV;
			Parameters.SpatialKeys = SpatialKeys.UAV;
			Parameters.SpatialOffsets = SpatialOffsets.UAV;
			Parameters.WhiteParticles =
				WhiteParticles.UAV;
			Parameters.WhiteParticleCounters =
				WhiteParticleCounters.UAV;
			TShaderMapRef<FSPHCalculatePressureCS> Shader(
				GetGlobalShaderMap(GetScene().GetFeatureLevel()));
			FComputeShaderUtils::Dispatch(
				RHICmdList,
				Shader,
				Parameters,
				Dispatch);
			SPHFluid::UAVBarrier(RHICmdList, Velocities.UAV);
			if (Data.bFoamActive)
			{
				SPHFluid::UAVBarrier(
					RHICmdList,
					WhiteParticles.UAV);
				SPHFluid::UAVBarrier(
					RHICmdList,
					WhiteParticleCounters.UAV);
			}
		}

		if (Data.ViscosityStrength > 0.0f)
		{
			FSPHCalculateViscosityCS::FParameters Parameters;
			Parameters.NumParticles = NumParticles;
			Parameters.DeltaTime = StepDeltaTime;
			Parameters.SmoothingRadius = Radius;
			Parameters.ViscosityStrength = Data.ViscosityStrength;
			Parameters.PredictedPositions = PredictedPositions.UAV;
			Parameters.Velocities = Velocities.UAV;
			Parameters.Densities = Densities.UAV;
			Parameters.SpatialKeys = SpatialKeys.UAV;
			Parameters.SpatialOffsets = SpatialOffsets.UAV;
			TShaderMapRef<FSPHCalculateViscosityCS> Shader(
				GetGlobalShaderMap(GetScene().GetFeatureLevel()));
			FComputeShaderUtils::Dispatch(
				RHICmdList,
				Shader,
				Parameters,
				Dispatch);
			SPHFluid::UAVBarrier(RHICmdList, Velocities.UAV);
		}

		{
			FSPHUpdatePositionsCS::FParameters Parameters;
			Parameters.NumParticles = NumParticles;
			Parameters.DeltaTime = StepDeltaTime;
			Parameters.CollisionDamping =
				FMath::Clamp(Data.CollisionDamping, 0.0f, 1.0f);
			Parameters.BoundsSize = Data.BoundsSize;
			Parameters.BoundsCenter = Data.BoundsCenter;
			Parameters.Positions = Positions.UAV;
			Parameters.Velocities = Velocities.UAV;
			TShaderMapRef<FSPHUpdatePositionsCS> Shader(
				GetGlobalShaderMap(GetScene().GetFeatureLevel()));
			FComputeShaderUtils::Dispatch(
				RHICmdList,
				Shader,
				Parameters,
				Dispatch);
			SPHFluid::UAVBarrier(RHICmdList, Positions.UAV);
			SPHFluid::UAVBarrier(RHICmdList, Velocities.UAV);
		}
	}

	void UpdateWhiteParticles_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		const SPHFluid::FDynamicData& Data,
		float FrameStep)
	{
		const FIntVector Dispatch =
			SPHFluid::ParticleDispatch(MaxFoamParticleCount);

		{
			FSPHUpdateWhiteParticlesCS::FParameters Parameters;
			Parameters.NumParticles = NumParticles;
			Parameters.MaxWhiteParticleCount = MaxFoamParticleCount;
			Parameters.WhiteParticleDeltaTime = FrameStep;
			Parameters.Gravity = Data.Gravity;
			Parameters.ContainerVelocityDelta =
				Data.ContainerVelocityDelta;
			Parameters.SmoothingRadius =
				FMath::Max(0.01f, Data.SmoothingRadius);
			Parameters.BubbleBuoyancy = Data.BubbleBuoyancy;
			Parameters.SprayClassifyMaxNeighbours =
				Data.SprayClassifyMaxNeighbours;
			Parameters.BubbleClassifyMinNeighbours =
				Data.BubbleClassifyMinNeighbours;
			Parameters.BubbleScale = Data.BubbleScale;
			Parameters.BubbleScaleChangeSpeed =
				Data.BubbleScaleChangeSpeed;
			Parameters.BoundsSize = Data.BoundsSize;
			Parameters.BoundsCenter = Data.BoundsCenter;
			Parameters.WhiteParticles =
				WhiteParticles.UAV;
			Parameters.WhiteParticlesCompacted =
				WhiteParticlesCompacted.UAV;
			Parameters.WhiteParticleCounters =
				WhiteParticleCounters.UAV;
			Parameters.PredictedPositions = PredictedPositions.UAV;
			Parameters.Velocities = Velocities.UAV;
			Parameters.SpatialKeys = SpatialKeys.UAV;
			Parameters.SpatialOffsets = SpatialOffsets.UAV;
			TShaderMapRef<FSPHUpdateWhiteParticlesCS> Shader(
				GetGlobalShaderMap(GetScene().GetFeatureLevel()));
			FComputeShaderUtils::Dispatch(
				RHICmdList,
				Shader,
				Parameters,
				Dispatch);
			SPHFluid::UAVBarrier(
				RHICmdList,
				WhiteParticlesCompacted.UAV);
			SPHFluid::UAVBarrier(
				RHICmdList,
				WhiteParticleCounters.UAV);
		}

		{
			FSPHPrepareWhiteParticlesCS::FParameters Parameters;
			Parameters.MaxWhiteParticleCount = MaxFoamParticleCount;
			Parameters.WhiteParticles =
				WhiteParticles.UAV;
			Parameters.WhiteParticlesCompacted =
				WhiteParticlesCompacted.UAV;
			Parameters.WhiteParticleCounters =
				WhiteParticleCounters.UAV;
			TShaderMapRef<FSPHPrepareWhiteParticlesCS> Shader(
				GetGlobalShaderMap(GetScene().GetFeatureLevel()));
			FComputeShaderUtils::Dispatch(
				RHICmdList,
				Shader,
				Parameters,
				Dispatch);
			SPHFluid::UAVBarrier(
				RHICmdList,
				WhiteParticles.UAV);
			SPHFluid::UAVBarrier(
				RHICmdList,
				WhiteParticleCounters.UAV);
		}
	}

	void PrepareWhiteParticleDrawArgs_RenderThread(
		FRHICommandListImmediate& RHICmdList)
	{
		if (bWhiteDrawArgsReady)
		{
			RHICmdList.Transition(FRHITransitionInfo(
				WhiteParticleDrawArgs.UAV,
				ERHIAccess::IndirectArgs,
				ERHIAccess::UAVCompute));
		}

		FSPHPrepareWhiteDrawArgsCS::FParameters Parameters;
		Parameters.MaxWhiteParticleCount = MaxFoamParticleCount;
		Parameters.WhiteParticleCounters =
			WhiteParticleCounters.UAV;
		Parameters.WhiteParticleDrawArgs =
			WhiteParticleDrawArgs.UAV;
		TShaderMapRef<FSPHPrepareWhiteDrawArgsCS> Shader(
			GetGlobalShaderMap(GetScene().GetFeatureLevel()));
		FComputeShaderUtils::Dispatch(
			RHICmdList,
			Shader,
			Parameters,
			FIntVector(1, 1, 1));
		RHICmdList.Transition(FRHITransitionInfo(
			WhiteParticleDrawArgs.UAV,
			ERHIAccess::UAVCompute,
			ERHIAccess::IndirectArgs));
		bWhiteDrawArgsReady = true;
	}

	void RunSpatialSort_RenderThread(
		FRHICommandListImmediate& RHICmdList)
	{
		const FIntVector Dispatch =
			SPHFluid::ParticleDispatch(NumParticles);

		{
			FSPHClearCountsCS::FParameters Parameters;
			Parameters.NumInputs = NumParticles;
			Parameters.Counts = Counts.UAV;
			Parameters.InputItems = SpatialIndices.UAV;
			TShaderMapRef<FSPHClearCountsCS> Shader(
				GetGlobalShaderMap(GetScene().GetFeatureLevel()));
			FComputeShaderUtils::Dispatch(
				RHICmdList,
				Shader,
				Parameters,
				Dispatch);
			SPHFluid::UAVBarrier(RHICmdList, Counts.UAV);
			SPHFluid::UAVBarrier(RHICmdList, SpatialIndices.UAV);
		}

		{
			FSPHCalculateCountsCS::FParameters Parameters;
			Parameters.NumInputs = NumParticles;
			Parameters.InputKeys = SpatialKeys.UAV;
			Parameters.Counts = Counts.UAV;
			TShaderMapRef<FSPHCalculateCountsCS> Shader(
				GetGlobalShaderMap(GetScene().GetFeatureLevel()));
			FComputeShaderUtils::Dispatch(
				RHICmdList,
				Shader,
				Parameters,
				Dispatch);
			SPHFluid::UAVBarrier(RHICmdList, Counts.UAV);
		}

		ScanBuffer_RenderThread(
			RHICmdList,
			Counts.UAV,
			NumParticles,
			0);

		{
			FSPHScatterCS::FParameters Parameters;
			Parameters.NumInputs = NumParticles;
			Parameters.InputItems = SpatialIndices.UAV;
			Parameters.InputKeys = SpatialKeys.UAV;
			Parameters.Counts = Counts.UAV;
			Parameters.SortedItems = SortedIndices.UAV;
			Parameters.SortedKeys = SortedKeys.UAV;
			TShaderMapRef<FSPHScatterCS> Shader(
				GetGlobalShaderMap(GetScene().GetFeatureLevel()));
			FComputeShaderUtils::Dispatch(
				RHICmdList,
				Shader,
				Parameters,
				Dispatch);
			SPHFluid::UAVBarrier(RHICmdList, SortedIndices.UAV);
			SPHFluid::UAVBarrier(RHICmdList, SortedKeys.UAV);
		}

		{
			FSPHSortCopyBackCS::FParameters Parameters;
			Parameters.NumInputs = NumParticles;
			Parameters.InputItems = SpatialIndices.UAV;
			Parameters.InputKeys = SpatialKeys.UAV;
			Parameters.SortedItems = SortedIndices.UAV;
			Parameters.SortedKeys = SortedKeys.UAV;
			TShaderMapRef<FSPHSortCopyBackCS> Shader(
				GetGlobalShaderMap(GetScene().GetFeatureLevel()));
			FComputeShaderUtils::Dispatch(
				RHICmdList,
				Shader,
				Parameters,
				Dispatch);
			SPHFluid::UAVBarrier(RHICmdList, SpatialIndices.UAV);
			SPHFluid::UAVBarrier(RHICmdList, SpatialKeys.UAV);
		}

		{
			FSPHInitializeOffsetsCS::FParameters Parameters;
			Parameters.NumInputs = NumParticles;
			Parameters.SpatialOffsets = SpatialOffsets.UAV;
			TShaderMapRef<FSPHInitializeOffsetsCS> Shader(
				GetGlobalShaderMap(GetScene().GetFeatureLevel()));
			FComputeShaderUtils::Dispatch(
				RHICmdList,
				Shader,
				Parameters,
				Dispatch);
			SPHFluid::UAVBarrier(RHICmdList, SpatialOffsets.UAV);
		}

		{
			FSPHCalculateOffsetsCS::FParameters Parameters;
			Parameters.NumInputs = NumParticles;
			Parameters.SpatialKeys = SpatialKeys.UAV;
			Parameters.SpatialOffsets = SpatialOffsets.UAV;
			TShaderMapRef<FSPHCalculateOffsetsCS> Shader(
				GetGlobalShaderMap(GetScene().GetFeatureLevel()));
			FComputeShaderUtils::Dispatch(
				RHICmdList,
				Shader,
				Parameters,
				Dispatch);
			SPHFluid::UAVBarrier(RHICmdList, SpatialOffsets.UAV);
		}
	}

	void ScanBuffer_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		FUnorderedAccessViewRHIRef ElementsUAV,
		uint32 ItemCount,
		int32 Level)
	{
		check(Level < ScanLevelCount);
		const uint32 GroupCount = FMath::DivideAndRoundUp(
			ItemCount,
			SPHFluid::ScanItemsPerGroup);

		{
			FSPHBlockScanCS::FParameters Parameters;
			Parameters.ItemCount = ItemCount;
			Parameters.Elements = ElementsUAV;
			Parameters.GroupSums = ScanGroupSums[Level].UAV;
			TShaderMapRef<FSPHBlockScanCS> Shader(
				GetGlobalShaderMap(GetScene().GetFeatureLevel()));
			FComputeShaderUtils::Dispatch(
				RHICmdList,
				Shader,
				Parameters,
				FIntVector(GroupCount, 1, 1));
			SPHFluid::UAVBarrier(RHICmdList, ElementsUAV);
			SPHFluid::UAVBarrier(
				RHICmdList,
				ScanGroupSums[Level].UAV);
		}

		if (GroupCount > 1)
		{
			ScanBuffer_RenderThread(
				RHICmdList,
				ScanGroupSums[Level].UAV,
				GroupCount,
				Level + 1);

			FSPHBlockCombineCS::FParameters Parameters;
			Parameters.ItemCount = ItemCount;
			Parameters.Elements = ElementsUAV;
			Parameters.GroupSums = ScanGroupSums[Level].UAV;
			TShaderMapRef<FSPHBlockCombineCS> Shader(
				GetGlobalShaderMap(GetScene().GetFeatureLevel()));
			FComputeShaderUtils::Dispatch(
				RHICmdList,
				Shader,
				Parameters,
				FIntVector(GroupCount, 1, 1));
			SPHFluid::UAVBarrier(RHICmdList, ElementsUAV);
		}
	}

	void BuildParticleMesh_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		const SPHFluid::FDynamicData& Data)
	{
		if (bParticleMeshReady)
		{
			RHICmdList.Transition(FRHITransitionInfo(
				PositionBuffer.UAV,
				ERHIAccess::VertexOrIndexBuffer,
				ERHIAccess::UAVCompute));
			RHICmdList.Transition(FRHITransitionInfo(
				TangentBuffer.UAV,
				ERHIAccess::VertexOrIndexBuffer,
				ERHIAccess::UAVCompute));
		}

		FSPHBuildParticleMeshCS::FParameters Parameters;
		Parameters.NumParticles = NumParticles;
		Parameters.RenderRadiusCm =
			FMath::Max(1.0f, Data.ParticleRenderRadiusCm);
		Parameters.SimulationToCentimeters =
			SPHFluid::SimulationToCentimeters;
		Parameters.Positions = Positions.UAV;
		Parameters.OutputPositions = PositionBuffer.UAV;
		Parameters.OutputTangents = TangentBuffer.UAV;
		TShaderMapRef<FSPHBuildParticleMeshCS> Shader(
			GetGlobalShaderMap(GetScene().GetFeatureLevel()));
		FComputeShaderUtils::Dispatch(
			RHICmdList,
			Shader,
			Parameters,
			SPHFluid::ParticleDispatch(VertexCount));

		RHICmdList.Transition(FRHITransitionInfo(
			PositionBuffer.UAV,
			ERHIAccess::UAVCompute,
			ERHIAccess::VertexOrIndexBuffer));
		RHICmdList.Transition(FRHITransitionInfo(
			TangentBuffer.UAV,
			ERHIAccess::UAVCompute,
			ERHIAccess::VertexOrIndexBuffer));
		bParticleMeshReady = true;
	}

	void BuildSurfaceMesh_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		const SPHFluid::FDynamicData& Data)
	{
		if (bSurfaceMeshReady)
		{
			RHICmdList.Transition(FRHITransitionInfo(
				SurfacePositionBuffer.UAV,
				ERHIAccess::VertexOrIndexBuffer,
				ERHIAccess::UAVCompute));
			RHICmdList.Transition(FRHITransitionInfo(
				SurfaceTangentBuffer.UAV,
				ERHIAccess::VertexOrIndexBuffer,
				ERHIAccess::UAVCompute));
		}

		const float Radius = FMath::Max(0.01f, Data.SmoothingRadius);
		const float KSpikyPow2 =
			15.0f / (2.0f * PI * FMath::Pow(Radius, 5.0f));
		const float KSpikyPow3 =
			15.0f / (PI * FMath::Pow(Radius, 6.0f));

		{
			FSPHSampleDensityGridCS::FParameters Parameters;
			Parameters.NumParticles = NumParticles;
			Parameters.SurfaceGridSize = SurfaceGridSize;
			Parameters.SmoothingRadius = Radius;
			Parameters.KSpikyPow2 = KSpikyPow2;
			Parameters.KSpikyPow3 = KSpikyPow3;
			Parameters.BoundsSize = Data.BoundsSize;
			Parameters.BoundsCenter = Data.BoundsCenter;
			Parameters.PredictedPositions = PredictedPositions.UAV;
			Parameters.SpatialKeys = SpatialKeys.UAV;
			Parameters.SpatialOffsets = SpatialOffsets.UAV;
			Parameters.DensityGrid = DensityGrid.UAV;
			TShaderMapRef<FSPHSampleDensityGridCS> Shader(
				GetGlobalShaderMap(GetScene().GetFeatureLevel()));
			FComputeShaderUtils::Dispatch(
				RHICmdList,
				Shader,
				Parameters,
				FIntVector(
					FMath::DivideAndRoundUp(SurfaceGridSize.X, 4),
					FMath::DivideAndRoundUp(SurfaceGridSize.Y, 4),
					FMath::DivideAndRoundUp(SurfaceGridSize.Z, 4)));
			SPHFluid::UAVBarrier(RHICmdList, DensityGrid.UAV);
		}

		{
			FSPHMarchingCubesCS::FParameters Parameters;
			Parameters.SurfaceGridSize = SurfaceGridSize;
			Parameters.SurfaceIsoLevel =
				FMath::Max(1.0f, Data.SurfaceIsoLevel);
			Parameters.SimulationToCentimeters =
				SPHFluid::SimulationToCentimeters;
			Parameters.BoundsSize = Data.BoundsSize;
			Parameters.BoundsCenter = Data.BoundsCenter;
			Parameters.DensityGrid = DensityGrid.UAV;
			Parameters.MarchingCubesLUT = MarchingCubesLUT.UAV;
			Parameters.OutputPositions = SurfacePositionBuffer.UAV;
			Parameters.OutputTangents = SurfaceTangentBuffer.UAV;
			TShaderMapRef<FSPHMarchingCubesCS> Shader(
				GetGlobalShaderMap(GetScene().GetFeatureLevel()));
			FComputeShaderUtils::Dispatch(
				RHICmdList,
				Shader,
				Parameters,
				FIntVector(
					FMath::DivideAndRoundUp(
						SurfaceGridSize.X - 1,
						4),
					FMath::DivideAndRoundUp(
						SurfaceGridSize.Y - 1,
						4),
					FMath::DivideAndRoundUp(
						SurfaceGridSize.Z - 1,
						4)));
		}

		RHICmdList.Transition(FRHITransitionInfo(
			SurfacePositionBuffer.UAV,
			ERHIAccess::UAVCompute,
			ERHIAccess::VertexOrIndexBuffer));
		RHICmdList.Transition(FRHITransitionInfo(
			SurfaceTangentBuffer.UAV,
			ERHIAccess::UAVCompute,
			ERHIAccess::VertexOrIndexBuffer));
		bSurfaceMeshReady = true;
	}

	int32 ParticlesPerAxis;
	uint32 SpawnRegionCount;
	uint32 NumParticles;
	uint32 MaxFoamParticleCount;
	uint32 VertexCount;
	FIntVector SurfaceGridSize;
	uint32 SurfaceGridPointCount;
	uint32 SurfaceCubeCount;
	uint32 SurfaceVertexCount;

	SPHFluid::FPositionBuffer PositionBuffer;
	SPHFluid::FTangentBuffer TangentBuffer;
	SPHFluid::FUVBuffer UVBuffer;
	SPHFluid::FParticleIndexBuffer IndexBuffer;
	FLocalVertexFactory VertexFactory;

	SPHFluid::FPositionBuffer SurfacePositionBuffer;
	SPHFluid::FTangentBuffer SurfaceTangentBuffer;
	SPHFluid::FUVBuffer SurfaceUVBuffer;
	SPHFluid::FSequentialIndexBuffer SurfaceIndexBuffer;
	FLocalVertexFactory SurfaceVertexFactory;

	FRWBufferStructured Positions;
	FRWBufferStructured PredictedPositions;
	FRWBufferStructured Velocities;
	FRWBufferStructured Densities;
	FRWBufferStructured SpatialKeys;
	FRWBufferStructured SpatialOffsets;
	FRWBufferStructured SpatialIndices;
	FRWBufferStructured Counts;
	FRWBufferStructured SortedKeys;
	FRWBufferStructured SortedIndices;
	FRWBufferStructured SortTargetPositions;
	FRWBufferStructured SortTargetPredictedPositions;
	FRWBufferStructured SortTargetVelocities;
	FRWBufferStructured WhiteParticles;
	FRWBufferStructured WhiteParticlesCompacted;
	FRWBufferStructured WhiteParticleCounters;
	FRWBuffer WhiteParticleDrawArgs;
	FRWBufferStructured DensityGrid;
	FRWBufferStructured MarchingCubesLUT;
	FRWBufferStructured ScanGroupSums[SPHFluid::MaxScanLevels];
	int32 ScanLevelCount = 0;

	UMaterialInterface* Material;
	FMaterialRelevance MaterialRelevance;
	bool bResourcesInitialized = false;
	bool bParticleMeshReady = false;
	bool bSurfaceMeshReady = false;
	bool bSurfaceLUTValid = false;
	bool bRenderContinuousSurface = true;
	bool bRenderScreenSpaceSurface = false;
	bool bWhiteDrawArgsReady = false;
	float SimulationTime = 0.0f;
	SPHFluid::FDynamicData CurrentDynamicData;
};

void FComputeWaterViewExtension::SubscribeToPostProcessingPass(
	EPostProcessingPass Pass,
	FAfterPassCallbackDelegateArray& InOutPassCallbacks,
	bool bIsPassEnabled)
{
	if (Pass == EPostProcessingPass::Tonemap)
	{
		InOutPassCallbacks.Add(
			FAfterPassCallbackDelegate::CreateRaw(
				this,
				&FComputeWaterViewExtension::AfterTonemap_RenderThread));
	}
}

FScreenPassTexture
FComputeWaterViewExtension::AfterTonemap_RenderThread(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs)
{
	if (GActiveScreenSpaceWaterProxy != nullptr)
	{
		return GActiveScreenSpaceWaterProxy
			->RenderScreenSpace_RenderThread(
				GraphBuilder,
				View,
				Inputs);
	}
	return Inputs.GetInput(EPostProcessMaterialInput::SceneColor);
}

UComputeWaterComponent::UComputeWaterComponent(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bUseSourceScreenSpace2Preset(true)
	, ParticlesPerAxis(24)
	, SimulationBoundsCm(800.0f, 500.0f, 360.0f)
	, SimulationBoundsCenterCm(0.0f, 0.0f, 180.0f)
	, SpawnCenterCm(-200.0f, 0.0f, 20.0f)
	, bUseSecondSpawnRegion(false)
	, SecondSpawnCenterCm(200.0f, 0.0f, 20.0f)
	, SpawnSizeCm(285.0f)
	, SpawnJitterCm(3.5f)
	, SmoothingRadiusCm(20.0f)
	, TargetDensity(630.0f)
	, PressureMultiplier(288.0f)
	, NearPressureMultiplier(2.15f)
	, ViscosityStrength(0.001f)
	, Gravity(-10.0f)
	, CollisionDamping(0.95f)
	, SimulationSubsteps(3)
	, bAnimate(true)
	, bApplyContainerInertia(true)
	, ContainerInertiaScale(1.0f)
	, MaxContainerAcceleration(80.0f)
	, ParticleRenderRadiusCm(9.0f)
	, bRenderContinuousSurface(true)
	, bRenderScreenSpaceSurface(false)
	, SurfaceGridResolution(75)
	, SurfaceIsoLevel(75.0f)
	, bFoamActive(false)
	, MaxFoamParticleCount(128000)
	, TrappedAirSpawnRate(70.0f)
	, FoamSpawnFadeInTime(0.5f)
	, FoamSpawnFadeStartTime(0.0f)
	, TrappedAirVelocityMinMax(5.0f, 25.0f)
	, FoamKineticEnergyMinMax(15.0f, 80.0f)
	, BubbleBuoyancy(1.5f)
	, SprayClassifyMaxNeighbours(5)
	, BubbleClassifyMinNeighbours(15)
	, BubbleScale(0.5f)
	, BubbleScaleChangeSpeed(7.0f)
	, bFrameSourceDemoOnPlay(true)
	, FrameDeltaTime(1.0f / 60.0f)
	, bResetRequested(true)
	, bContainerMotionInitialized(false)
	, PreviousContainerWorldLocationCm(FVector::ZeroVector)
	, PreviousContainerWorldVelocityMetersPerSecond(FVector::ZeroVector)
	, ContainerVelocityDeltaMetersPerSecond(FVector::ZeroVector)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	CastShadow = true;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface>
		WaterMaterialFinder(
			TEXT("/Game/StarterContent/Materials/M_Water_Lake.M_Water_Lake"));
	if (WaterMaterialFinder.Succeeded())
	{
		SetMaterial(0, WaterMaterialFinder.Object);
	}
}

void UComputeWaterComponent::OnRegister()
{
	if (!GComputeWaterViewExtension.IsValid())
	{
		GComputeWaterViewExtension =
			FSceneViewExtensions::NewExtension<
				FComputeWaterViewExtension>();
	}
	ParticlesPerAxis = FMath::Clamp(ParticlesPerAxis, 8, 59);
	SimulationBoundsCm.X =
		FMath::Max(100.0f, SimulationBoundsCm.X);
	SimulationBoundsCm.Y =
		FMath::Max(100.0f, SimulationBoundsCm.Y);
	SimulationBoundsCm.Z =
		FMath::Max(100.0f, SimulationBoundsCm.Z);
	SpawnSizeCm = FMath::Max(20.0f, SpawnSizeCm);
	SmoothingRadiusCm = FMath::Max(1.0f, SmoothingRadiusCm);
	SurfaceGridResolution =
		FMath::Clamp(SurfaceGridResolution, 24, 96);
	SurfaceIsoLevel = FMath::Max(1.0f, SurfaceIsoLevel);
	MaxFoamParticleCount =
		FMath::Clamp(MaxFoamParticleCount, 1024, 1024000);
	bResetRequested = true;
	bContainerMotionInitialized = false;
	ContainerVelocityDeltaMetersPerSecond = FVector::ZeroVector;
	Super::OnRegister();
}

void UComputeWaterComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	FrameDeltaTime = DeltaTime;
	ContainerVelocityDeltaMetersPerSecond = FVector::ZeroVector;
	const FVector CurrentWorldLocationCm = GetComponentLocation();
	if (!bContainerMotionInitialized || DeltaTime <= SMALL_NUMBER)
	{
		bContainerMotionInitialized = true;
		PreviousContainerWorldLocationCm = CurrentWorldLocationCm;
		PreviousContainerWorldVelocityMetersPerSecond = FVector::ZeroVector;
	}
	else
	{
		const FVector CurrentWorldVelocityMetersPerSecond =
			(CurrentWorldLocationCm - PreviousContainerWorldLocationCm)
			/ (DeltaTime * SPHFluid::SimulationToCentimeters);
		if (bApplyContainerInertia && ContainerInertiaScale > 0.0f)
		{
			const FVector WorldVelocityDelta =
				CurrentWorldVelocityMetersPerSecond
				- PreviousContainerWorldVelocityMetersPerSecond;
			const FVector LocalVelocityDelta =
				GetComponentTransform()
					.InverseTransformVectorNoScale(WorldVelocityDelta);
			const float MaximumVelocityDelta =
				FMath::Max(0.0f, MaxContainerAcceleration)
				* DeltaTime;
			ContainerVelocityDeltaMetersPerSecond =
				(-LocalVelocityDelta * ContainerInertiaScale)
				.GetClampedToMaxSize(MaximumVelocityDelta);
		}
		PreviousContainerWorldLocationCm = CurrentWorldLocationCm;
		PreviousContainerWorldVelocityMetersPerSecond =
			CurrentWorldVelocityMetersPerSecond;
	}
	MarkRenderDynamicDataDirty();
}

FVector UComputeWaterComponent::GetContainerVelocityDeltaMetersPerSecond() const
{
	return ContainerVelocityDeltaMetersPerSecond;
}

void UComputeWaterComponent::ResetFluid()
{
	bResetRequested = true;
	bContainerMotionInitialized = false;
	ContainerVelocityDeltaMetersPerSecond = FVector::ZeroVector;
	MarkRenderDynamicDataDirty();
}

void UComputeWaterComponent::ResetWater()
{
	ResetFluid();
}

void UComputeWaterComponent::SendRenderDynamicData_Concurrent()
{
	Super::SendRenderDynamicData_Concurrent();
	if (SceneProxy == nullptr)
	{
		return;
	}

	SPHFluid::FDynamicData Data =
		FComputeWaterSceneProxy::CreateDynamicData(
			this,
			FrameDeltaTime,
			bResetRequested);
	bResetRequested = false;

	FComputeWaterSceneProxy* WaterSceneProxy =
		static_cast<FComputeWaterSceneProxy*>(SceneProxy);
	ENQUEUE_RENDER_COMMAND(UpdateSPHFluid)(
		[WaterSceneProxy, Data](FRHICommandListImmediate& RHICmdList)
		{
			WaterSceneProxy->UpdateSimulation_RenderThread(
				RHICmdList,
				Data);
		});
}

FPrimitiveSceneProxy* UComputeWaterComponent::CreateSceneProxy()
{
	UMaterialInterface* Material = GetMaterial(0);
	if (Material == nullptr)
	{
		Material = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	FPrimitiveSceneProxy* NewProxy =
		new FComputeWaterSceneProxy(this, Material);
	bResetRequested = false;
	return NewProxy;
}

FBoxSphereBounds UComputeWaterComponent::CalcBounds(
	const FTransform& LocalToWorld) const
{
	const FVector EffectiveBounds = bUseSourceScreenSpace2Preset
		? FVector(3600.0f, 800.0f, 1200.0f)
		: SimulationBoundsCm;
	const FVector EffectiveCenter = bUseSourceScreenSpace2Preset
		? FVector(0.0f, 0.0f, 600.0f)
		: SimulationBoundsCenterCm;
	const FVector Extent =
		EffectiveBounds.GetAbs() * 0.5f
		+ FVector(FMath::Max(1.0f, ParticleRenderRadiusCm));
	return FBoxSphereBounds(
		FBox(EffectiveCenter - Extent, EffectiveCenter + Extent))
		.TransformBy(LocalToWorld);
}

int32 UComputeWaterComponent::GetNumMaterials() const
{
	return 1;
}

#if WITH_EDITOR
void UComputeWaterComponent::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent)
{
	ParticlesPerAxis = FMath::Clamp(ParticlesPerAxis, 8, 59);
	SimulationBoundsCm.X =
		FMath::Max(100.0f, SimulationBoundsCm.X);
	SimulationBoundsCm.Y =
		FMath::Max(100.0f, SimulationBoundsCm.Y);
	SimulationBoundsCm.Z =
		FMath::Max(100.0f, SimulationBoundsCm.Z);
	SpawnSizeCm = FMath::Max(20.0f, SpawnSizeCm);
	SpawnJitterCm = FMath::Max(0.0f, SpawnJitterCm);
	SmoothingRadiusCm = FMath::Max(1.0f, SmoothingRadiusCm);
	SimulationSubsteps = FMath::Clamp(SimulationSubsteps, 1, 8);
	ParticleRenderRadiusCm =
		FMath::Max(1.0f, ParticleRenderRadiusCm);
	SurfaceGridResolution =
		FMath::Clamp(SurfaceGridResolution, 24, 96);
	SurfaceIsoLevel = FMath::Max(1.0f, SurfaceIsoLevel);
	MaxFoamParticleCount =
		FMath::Clamp(MaxFoamParticleCount, 1024, 1024000);
	ContainerInertiaScale =
		FMath::Clamp(ContainerInertiaScale, 0.0f, 2.0f);
	MaxContainerAcceleration =
		FMath::Max(0.0f, MaxContainerAcceleration);

	Super::PostEditChangeProperty(PropertyChangedEvent);
	MarkRenderStateDirty();
}
#endif
