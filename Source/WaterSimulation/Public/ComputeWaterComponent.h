#pragma once

#include "Components/MeshComponent.h"
#include "ComputeWaterComponent.generated.h"

/**
 * GPU-only 3D SPH fluid simulation ported from SebLague/Fluid-Sim.
 *
 * Particle integration, spatial hashing, count sort, prefix scan, neighbour
 * search, density, pressure, viscosity and render-mesh generation all run on
 * the GPU. The CPU only uploads settings and submits dispatches.
 */
UCLASS(ClassGroup = Rendering, meta = (BlueprintSpawnableComponent))
class WATERSIMULATION_API UComputeWaterComponent : public UMeshComponent
{
	GENERATED_BODY()

public:
	UComputeWaterComponent(const FObjectInitializer& ObjectInitializer);

	/**
	 * Reproduce the checked-in Fluid ScreenSpace 2 Unity scene.
	 *
	 * This overrides the simulation, two spawn regions, foam and rendering
	 * settings with the serialized source values. Disable it to use the
	 * individual tuning properties below.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Demo")
	bool bUseSourceScreenSpace2Preset;

	/** Cubic particle lattice dimension. Total particle count is Axis^3. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "SPH Fluid|Particles",
		meta = (ClampMin = "8", ClampMax = "59", UIMin = "8", UIMax = "59"))
	int32 ParticlesPerAxis;

	/** Axis-aligned local simulation container in Unreal centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SPH Fluid|Container")
	FVector SimulationBoundsCm;

	/** Local centre of the simulation container in Unreal centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SPH Fluid|Container")
	FVector SimulationBoundsCenterCm;

	/** Centre of the initial particle cube, in local Unreal centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SPH Fluid|Spawn")
	FVector SpawnCenterCm;

	/** Enable a second source-compatible cubic spawn region. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SPH Fluid|Spawn")
	bool bUseSecondSpawnRegion;

	/** Centre of the second particle cube, in local Unreal centimeters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SPH Fluid|Spawn")
	FVector SecondSpawnCenterCm;

	/** Side length of the initial particle cube, in centimeters. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "SPH Fluid|Spawn",
		meta = (ClampMin = "20.0"))
	float SpawnSizeCm;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "SPH Fluid|Spawn",
		meta = (ClampMin = "0.0"))
	float SpawnJitterCm;

	/** SPH support radius. Sebastian's source default is 0.2 m = 20 cm. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "SPH Fluid|Simulation",
		meta = (ClampMin = "1.0"))
	float SmoothingRadiusCm;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH Fluid|Simulation")
	float TargetDensity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH Fluid|Simulation")
	float PressureMultiplier;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH Fluid|Simulation")
	float NearPressureMultiplier;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "SPH Fluid|Simulation",
		meta = (ClampMin = "0.0"))
	float ViscosityStrength;

	/** Gravity in simulation metres per second squared; negative Z is down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH Fluid|Simulation")
	float Gravity;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "SPH Fluid|Simulation",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CollisionDamping;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "SPH Fluid|Simulation",
		meta = (ClampMin = "1", ClampMax = "8"))
	int32 SimulationSubsteps;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH Fluid|Simulation")
	bool bAnimate;

	/**
	 * Apply the opposite of the container's velocity change to the particles.
	 * This makes externally translated containers push and slosh the fluid
	 * instead of carrying every particle rigidly with the component transform.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH Fluid|Container Motion")
	bool bApplyContainerInertia;

	/** Multiplier for the moving-frame velocity impulse. One is physical. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "SPH Fluid|Container Motion",
		meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float ContainerInertiaScale;

	/** Stability clamp for container acceleration, in metres per second squared. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "SPH Fluid|Container Motion",
		meta = (ClampMin = "0.0"))
	float MaxContainerAcceleration;

	/**
	 * Radius of the source-compatible particle visualization. The continuous
	 * surface renderer consumes the same SPH position buffer.
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "SPH Fluid|Rendering",
		meta = (ClampMin = "1.0"))
	float ParticleRenderRadiusCm;

	/** Generate the continuous Marching Cubes surface from the SPH density. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH Fluid|Rendering")
	bool bRenderContinuousSurface;

	/** Source final renderer: depth/thickness splats, smoothing and compositing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH Fluid|Rendering")
	bool bRenderScreenSpaceSurface;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "SPH Fluid|Rendering",
		meta = (ClampMin = "24", ClampMax = "96", UIMin = "24", UIMax = "96"))
	int32 SurfaceGridResolution;

	/** Density threshold used by the source Marching Cubes renderer. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "SPH Fluid|Rendering",
		meta = (ClampMin = "1.0"))
	float SurfaceIsoLevel;

	/** Enable the source foam, spray and bubble simulation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH Fluid|Foam")
	bool bFoamActive;

	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "SPH Fluid|Foam",
		meta = (ClampMin = "1024", ClampMax = "1024000"))
	int32 MaxFoamParticleCount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH Fluid|Foam")
	float TrappedAirSpawnRate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH Fluid|Foam")
	float FoamSpawnFadeInTime;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH Fluid|Foam")
	float FoamSpawnFadeStartTime;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH Fluid|Foam")
	FVector2D TrappedAirVelocityMinMax;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH Fluid|Foam")
	FVector2D FoamKineticEnergyMinMax;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH Fluid|Foam")
	float BubbleBuoyancy;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH Fluid|Foam")
	int32 SprayClassifyMaxNeighbours;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH Fluid|Foam")
	int32 BubbleClassifyMinNeighbours;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH Fluid|Foam")
	float BubbleScale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SPH Fluid|Foam")
	float BubbleScaleChangeSpeed;

	/** Match the source Fluid ScreenSpace 2 camera framing on BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Source Demo")
	bool bFrameSourceDemoOnPlay;

	UFUNCTION(BlueprintCallable, Category = "SPH Fluid")
	void ResetFluid();

	// Compatibility alias retained for existing Blueprints.
	UFUNCTION(BlueprintCallable, Category = "SPH Fluid")
	void ResetWater();

	virtual void OnRegister() override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void SendRenderDynamicData_Concurrent() override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(
		const FTransform& LocalToWorld) const override;
	virtual int32 GetNumMaterials() const override;

	/** Local moving-frame velocity impulse accumulated for the current frame. */
	FVector GetContainerVelocityDeltaMetersPerSecond() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(
		FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	float FrameDeltaTime;
	bool bResetRequested;
	bool bContainerMotionInitialized;
	FVector PreviousContainerWorldLocationCm;
	FVector PreviousContainerWorldVelocityMetersPerSecond;
	FVector ContainerVelocityDeltaMetersPerSecond;
};
