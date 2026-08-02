#include "ComputeWaterFreeCameraPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/PlayerController.h"

AComputeWaterFreeCameraPawn::AComputeWaterFreeCameraPawn(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, MouseLookSensitivity(0.6f)
	, bIsFreeLooking(false)
	, bHasSavedCursorPosition(false)
	, SavedCursorX(0)
	, SavedCursorY(0)
{
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SceneRoot);
	Camera->bUsePawnControlRotation = true;

	Movement =
		CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
	Movement->UpdatedComponent = SceneRoot;
	Movement->MaxSpeed = 2400.0f;
	Movement->Acceleration = 8000.0f;
	Movement->Deceleration = 8000.0f;

	AutoPossessPlayer = EAutoReceiveInput::Disabled;
	PrimaryActorTick.bCanEverTick = false;
}

void AComputeWaterFreeCameraPawn::SetupPlayerInputComponent(
	UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	check(PlayerInputComponent != nullptr);

	PlayerInputComponent->BindAxis(
		TEXT("ComputeWaterMoveForward"),
		this,
		&AComputeWaterFreeCameraPawn::MoveForward);
	PlayerInputComponent->BindAxis(
		TEXT("ComputeWaterMoveRight"),
		this,
		&AComputeWaterFreeCameraPawn::MoveRight);
	PlayerInputComponent->BindAxis(
		TEXT("ComputeWaterMoveUp"),
		this,
		&AComputeWaterFreeCameraPawn::MoveUp);
	PlayerInputComponent->BindAxis(
		TEXT("ComputeWaterTurn"),
		this,
		&AComputeWaterFreeCameraPawn::Turn);
	PlayerInputComponent->BindAxis(
		TEXT("ComputeWaterLookUp"),
		this,
		&AComputeWaterFreeCameraPawn::LookUp);
	PlayerInputComponent->BindAction(
		TEXT("ComputeWaterFreeLook"),
		IE_Pressed,
		this,
		&AComputeWaterFreeCameraPawn::BeginFreeLook);
	PlayerInputComponent->BindAction(
		TEXT("ComputeWaterFreeLook"),
		IE_Released,
		this,
		&AComputeWaterFreeCameraPawn::EndFreeLook);
}

void AComputeWaterFreeCameraPawn::MoveForward(float Value)
{
	if (bIsFreeLooking
		&& !FMath::IsNearlyZero(Value)
		&& Controller != nullptr)
	{
		AddMovementInput(Controller->GetControlRotation().Vector(), Value);
	}
}

void AComputeWaterFreeCameraPawn::MoveRight(float Value)
{
	if (bIsFreeLooking
		&& !FMath::IsNearlyZero(Value)
		&& Controller != nullptr)
	{
		const FVector Right = FRotationMatrix(
			Controller->GetControlRotation()).GetScaledAxis(EAxis::Y);
		AddMovementInput(Right, Value);
	}
}

void AComputeWaterFreeCameraPawn::MoveUp(float Value)
{
	if (bIsFreeLooking && !FMath::IsNearlyZero(Value))
	{
		AddMovementInput(FVector::UpVector, Value);
	}
}

void AComputeWaterFreeCameraPawn::Turn(float Value)
{
	if (bIsFreeLooking && !FMath::IsNearlyZero(Value))
	{
		AddControllerYawInput(Value * MouseLookSensitivity);
	}
}

void AComputeWaterFreeCameraPawn::LookUp(float Value)
{
	if (bIsFreeLooking && !FMath::IsNearlyZero(Value))
	{
		AddControllerPitchInput(Value * MouseLookSensitivity);
	}
}

void AComputeWaterFreeCameraPawn::BeginFreeLook()
{
	APlayerController* PlayerController =
		Cast<APlayerController>(GetController());
	if (PlayerController == nullptr)
	{
		return;
	}

	float CursorX = 0.0f;
	float CursorY = 0.0f;
	bHasSavedCursorPosition =
		PlayerController->GetMousePosition(CursorX, CursorY);
	SavedCursorX = FMath::RoundToInt(CursorX);
	SavedCursorY = FMath::RoundToInt(CursorY);
	bIsFreeLooking = true;
	PlayerController->bShowMouseCursor = false;
	FInputModeGameOnly InputMode;
	PlayerController->SetInputMode(InputMode);
}

void AComputeWaterFreeCameraPawn::EndFreeLook()
{
	APlayerController* PlayerController =
		Cast<APlayerController>(GetController());
	bIsFreeLooking = false;
	if (PlayerController == nullptr)
	{
		return;
	}

	PlayerController->bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PlayerController->SetInputMode(InputMode);
	if (bHasSavedCursorPosition)
	{
		PlayerController->SetMouseLocation(SavedCursorX, SavedCursorY);
	}
}
