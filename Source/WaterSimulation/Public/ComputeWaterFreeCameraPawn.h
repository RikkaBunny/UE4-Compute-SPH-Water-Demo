#pragma once

#include "GameFramework/Pawn.h"
#include "ComputeWaterFreeCameraPawn.generated.h"

class UCameraComponent;
class UFloatingPawnMovement;
class USceneComponent;

/**
 * Editor-style fly camera used by the standalone water demo.
 * Hold the right mouse button to look, use WASD to move, and Q/E to move
 * vertically. The visible cursor remains available for left-button dragging.
 */
UCLASS()
class WATERSIMULATION_API AComputeWaterFreeCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	AComputeWaterFreeCameraPawn(const FObjectInitializer& ObjectInitializer);

	virtual void SetupPlayerInputComponent(
		UInputComponent* PlayerInputComponent) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Free Camera")
	USceneComponent* SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Free Camera")
	UCameraComponent* Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Free Camera")
	UFloatingPawnMovement* Movement;

	/** Multiplier applied to mouse-look input. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Free Camera",
		meta = (ClampMin = "0.01"))
	float MouseLookSensitivity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Free Camera")
	bool bIsFreeLooking;

private:
	void MoveForward(float Value);
	void MoveRight(float Value);
	void MoveUp(float Value);
	void Turn(float Value);
	void LookUp(float Value);
	void BeginFreeLook();
	void EndFreeLook();

	bool bHasSavedCursorPosition;
	int32 SavedCursorX;
	int32 SavedCursorY;
};
