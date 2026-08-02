#pragma once

#include "GameFramework/Actor.h"
#include "ComputeWaterActor.generated.h"

class UComputeWaterComponent;
class APlayerController;
class AComputeWaterFreeCameraPawn;

UCLASS(BlueprintType)
class WATERSIMULATION_API AComputeWaterActor : public AActor
{
	GENERATED_BODY()

public:
	AComputeWaterActor(const FObjectInitializer& ObjectInitializer);
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Compute Water")
	UComputeWaterComponent* WaterSurface;

	/** Use an editor-style fly camera instead of a fixed camera actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Compute Water|Camera")
	bool bEnableFreeCamera;

	/** Allow the source-demo container to be grabbed with the left mouse button. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Compute Water|Container Drag")
	bool bEnableMouseDrag;

	/** Only begin a drag when the cursor is over the simulation box footprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Compute Water|Container Drag")
	bool bRequireCursorOverContainer;

	/** How strongly the container follows the current cursor target. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Compute Water|Container Drag",
		meta = (ClampMin = "0.1"))
	float DragResponsiveness;

	/** Maximum translation speed while dragging, in centimetres per second. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Compute Water|Container Drag",
		meta = (ClampMin = "1.0"))
	float DragMaxSpeedCmPerSecond;

	/** Maximum acceleration/deceleration used by the interactive drag. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Compute Water|Container Drag",
		meta = (ClampMin = "1.0"))
	float DragAccelerationCmPerSecondSquared;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Compute Water|Container Drag")
	bool bIsDraggingContainer;

private:
	AComputeWaterFreeCameraPawn* SetupFreeCamera();
	void ApplySourceDemoCamera();
	bool ProjectMouseToDragPlane(
		APlayerController* PlayerController,
		FVector& OutWorldPoint) const;
	bool IsPointInsideContainerFootprint(const FVector& WorldPoint) const;
	void SetupContainerInput(APlayerController* PlayerController);
	void HandleContainerDragPressed();
	void HandleContainerDragReleased();
	void BeginContainerDrag(APlayerController* PlayerController);
	void UpdateContainerDrag(APlayerController* PlayerController);
	void UpdateContainerMovement(float DeltaSeconds);

	bool bHasPendingDragTarget;
	bool bContainerInputBound;
	TWeakObjectPtr<APlayerController> DragPlayerController;
	FVector DragStartWorldPoint;
	FVector DragStartActorLocation;
	FVector DragTargetActorLocation;
	FVector DragVelocityCmPerSecond;
};
