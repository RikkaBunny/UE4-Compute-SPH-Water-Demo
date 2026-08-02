#include "ComputeWaterActor.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "ComputeWaterComponent.h"
#include "ComputeWaterFreeCameraPawn.h"
#include "WaterSimulation.h"
#include "Engine/LocalPlayer.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Math/RotationMatrix.h"
#include "TimerManager.h"

AComputeWaterActor::AComputeWaterActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bEnableFreeCamera(true)
	, bEnableMouseDrag(true)
	, bRequireCursorOverContainer(false)
	, DragResponsiveness(8.0f)
	, DragMaxSpeedCmPerSecond(1800.0f)
	, DragAccelerationCmPerSecondSquared(6000.0f)
	, bIsDraggingContainer(false)
	, bHasPendingDragTarget(false)
	, bContainerInputBound(false)
	, DragStartWorldPoint(FVector::ZeroVector)
	, DragStartActorLocation(FVector::ZeroVector)
	, DragTargetActorLocation(FVector::ZeroVector)
	, DragVelocityCmPerSecond(FVector::ZeroVector)
{
	WaterSurface = CreateDefaultSubobject<UComputeWaterComponent>(TEXT("WaterSurface"));
	WaterSurface->SetMobility(EComponentMobility::Movable);
	SetRootComponent(WaterSurface);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
}

void AComputeWaterActor::BeginPlay()
{
	Super::BeginPlay();
	if (WaterSurface != nullptr)
	{
		// The component samples the completed actor move during its own tick.
		WaterSurface->AddTickPrerequisiteActor(this);
	}
	if (APlayerController* PlayerController =
		GetWorld() != nullptr
			? GetWorld()->GetFirstPlayerController()
			: nullptr)
	{
		if (bEnableMouseDrag)
		{
			SetupContainerInput(PlayerController);
		}
	}
	if (bEnableFreeCamera)
	{
		SetupFreeCamera();
	}
	if (!bEnableFreeCamera
		|| WaterSurface == nullptr
		|| !WaterSurface->bUseSourceScreenSpace2Preset
		|| !WaterSurface->bFrameSourceDemoOnPlay)
	{
		return;
	}
	ApplySourceDemoCamera();
	GetWorldTimerManager().SetTimerForNextTick(
		this,
		&AComputeWaterActor::ApplySourceDemoCamera);
}

AComputeWaterFreeCameraPawn* AComputeWaterActor::SetupFreeCamera()
{
	APlayerController* PlayerController =
		GetWorld() != nullptr
			? GetWorld()->GetFirstPlayerController()
			: nullptr;
	if (PlayerController == nullptr)
	{
		return nullptr;
	}

	AComputeWaterFreeCameraPawn* FreeCamera =
		Cast<AComputeWaterFreeCameraPawn>(PlayerController->GetPawn());
	if (FreeCamera != nullptr)
	{
		return FreeCamera;
	}

	FVector InitialLocation = GetActorLocation();
	FRotator InitialRotation = GetActorRotation();
	float InitialFov = 90.0f;
	if (PlayerController->PlayerCameraManager != nullptr)
	{
		InitialLocation =
			PlayerController->PlayerCameraManager->GetCameraLocation();
		InitialRotation =
			PlayerController->PlayerCameraManager->GetCameraRotation();
		InitialFov = PlayerController->PlayerCameraManager->GetFOVAngle();
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FreeCamera = GetWorld()->SpawnActor<AComputeWaterFreeCameraPawn>(
		AComputeWaterFreeCameraPawn::StaticClass(),
		FTransform(InitialRotation, InitialLocation),
		SpawnParameters);
	if (FreeCamera == nullptr)
	{
		return nullptr;
	}

	PlayerController->Possess(FreeCamera);
	PlayerController->SetControlRotation(InitialRotation);
	PlayerController->SetViewTarget(FreeCamera);
	if (FreeCamera->Camera != nullptr)
	{
		FreeCamera->Camera->SetFieldOfView(InitialFov);
	}
	if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
	{
		LocalPlayer->AspectRatioAxisConstraint =
			AspectRatio_MaintainYFOV;
	}
	UE_LOG(
		LogWaterSimulation,
		Log,
		TEXT("Free camera ready: RMB look, WASD move, Q/E down/up, LMB drag"));
	return FreeCamera;
}

void AComputeWaterActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	APlayerController* PlayerController =
		GetWorld() != nullptr
			? GetWorld()->GetFirstPlayerController()
			: nullptr;
	if (PlayerController == nullptr || !bEnableMouseDrag)
	{
		bIsDraggingContainer = false;
		bHasPendingDragTarget = false;
		UpdateContainerMovement(DeltaSeconds);
		return;
	}
	if (!DragPlayerController.IsValid())
	{
		SetupContainerInput(PlayerController);
	}
	if (bIsDraggingContainer)
	{
		UpdateContainerDrag(PlayerController);
	}
	UpdateContainerMovement(DeltaSeconds);
}

void AComputeWaterActor::SetupContainerInput(
	APlayerController* PlayerController)
{
	if (PlayerController == nullptr)
	{
		return;
	}
	DragPlayerController = PlayerController;
	PlayerController->bShowMouseCursor = true;
	PlayerController->bEnableClickEvents = true;
	PlayerController->bEnableMouseOverEvents = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PlayerController->SetInputMode(InputMode);

	EnableInput(PlayerController);
	if (InputComponent == nullptr)
	{
		UE_LOG(
			LogWaterSimulation,
			Warning,
			TEXT("Unable to create an input component for container dragging"));
		return;
	}
	InputComponent->Priority = 10;
	if (!bContainerInputBound)
	{
		FInputKeyBinding& PressedBinding = InputComponent->BindKey(
			EKeys::LeftMouseButton,
			IE_Pressed,
			this,
			&AComputeWaterActor::HandleContainerDragPressed);
		PressedBinding.bConsumeInput = false;
		FInputKeyBinding& ReleasedBinding = InputComponent->BindKey(
			EKeys::LeftMouseButton,
			IE_Released,
			this,
			&AComputeWaterActor::HandleContainerDragReleased);
		ReleasedBinding.bConsumeInput = false;
		bContainerInputBound = true;
	}
	UE_LOG(
		LogWaterSimulation,
		Log,
		TEXT("Container mouse drag input is ready"));
}

void AComputeWaterActor::HandleContainerDragPressed()
{
	if (bEnableMouseDrag && DragPlayerController.IsValid())
	{
		BeginContainerDrag(DragPlayerController.Get());
	}
}

void AComputeWaterActor::HandleContainerDragReleased()
{
	if (bIsDraggingContainer && DragPlayerController.IsValid())
	{
		// Preserve the release coordinate for very short flick-style drags.
		UpdateContainerDrag(DragPlayerController.Get());
	}
	bIsDraggingContainer = false;
}

bool AComputeWaterActor::ProjectMouseToDragPlane(
	APlayerController* PlayerController,
	FVector& OutWorldPoint) const
{
	if (PlayerController == nullptr || WaterSurface == nullptr)
	{
		return false;
	}

	FVector RayOrigin;
	FVector RayDirection;
	if (!PlayerController->DeprojectMousePositionToWorld(
		RayOrigin,
		RayDirection))
	{
		return false;
	}

	const FVector EffectiveCenterCm =
		WaterSurface->bUseSourceScreenSpace2Preset
			? FVector(0.0f, 0.0f, 600.0f)
			: WaterSurface->SimulationBoundsCenterCm;
	const FTransform& ComponentTransform =
		WaterSurface->GetComponentTransform();
	const FVector PlanePoint =
		ComponentTransform.TransformPosition(EffectiveCenterCm);
	const FVector PlaneNormal =
		ComponentTransform.TransformVectorNoScale(FVector::UpVector)
			.GetSafeNormal();
	const float Denominator = FVector::DotProduct(RayDirection, PlaneNormal);
	if (FMath::Abs(Denominator) <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const float Distance =
		FVector::DotProduct(PlanePoint - RayOrigin, PlaneNormal)
		/ Denominator;
	if (Distance < 0.0f)
	{
		return false;
	}
	OutWorldPoint = RayOrigin + RayDirection * Distance;
	return true;
}

bool AComputeWaterActor::IsPointInsideContainerFootprint(
	const FVector& WorldPoint) const
{
	if (WaterSurface == nullptr)
	{
		return false;
	}
	const FVector EffectiveBoundsCm =
		WaterSurface->bUseSourceScreenSpace2Preset
			? FVector(3600.0f, 800.0f, 1200.0f)
			: WaterSurface->SimulationBoundsCm;
	const FVector EffectiveCenterCm =
		WaterSurface->bUseSourceScreenSpace2Preset
			? FVector(0.0f, 0.0f, 600.0f)
			: WaterSurface->SimulationBoundsCenterCm;
	const FVector LocalPoint =
		WaterSurface->GetComponentTransform()
			.InverseTransformPosition(WorldPoint);
	const FVector Offset = LocalPoint - EffectiveCenterCm;
	const FVector HalfBounds = EffectiveBoundsCm.GetAbs() * 0.5f;
	return FMath::Abs(Offset.X) <= HalfBounds.X
		&& FMath::Abs(Offset.Y) <= HalfBounds.Y;
}

void AComputeWaterActor::BeginContainerDrag(
	APlayerController* PlayerController)
{
	// A hidden cursor means the right mouse button currently owns input for
	// free-camera look. Never start a container drag in that mode.
	if (PlayerController == nullptr || !PlayerController->bShowMouseCursor)
	{
		return;
	}

	FVector WorldPoint;
	if (!ProjectMouseToDragPlane(PlayerController, WorldPoint)
		|| (bRequireCursorOverContainer
			&& !IsPointInsideContainerFootprint(WorldPoint)))
	{
		return;
	}

	bIsDraggingContainer = true;
	bHasPendingDragTarget = true;
	DragStartWorldPoint = WorldPoint;
	DragStartActorLocation = GetActorLocation();
	DragTargetActorLocation = DragStartActorLocation;
	UE_LOG(
		LogWaterSimulation,
		Log,
		TEXT("Container mouse drag started at %s"),
		*DragStartActorLocation.ToCompactString());
}

void AComputeWaterActor::UpdateContainerDrag(
	APlayerController* PlayerController)
{
	FVector WorldPoint;
	if (!ProjectMouseToDragPlane(PlayerController, WorldPoint))
	{
		return;
	}
	DragTargetActorLocation =
		DragStartActorLocation + WorldPoint - DragStartWorldPoint;
	// Interactive dragging is translation-only, so gravity stays aligned to
	// the simulation's local Z axis.
	DragTargetActorLocation.Z = DragStartActorLocation.Z;
}

void AComputeWaterActor::UpdateContainerMovement(float DeltaSeconds)
{
	if (DeltaSeconds <= SMALL_NUMBER)
	{
		return;
	}
	const FVector CurrentLocation = GetActorLocation();
	FVector DesiredVelocity = FVector::ZeroVector;
	if (bIsDraggingContainer || bHasPendingDragTarget)
	{
		DesiredVelocity =
			(DragTargetActorLocation - CurrentLocation)
			* FMath::Max(0.1f, DragResponsiveness);
		DesiredVelocity = DesiredVelocity.GetClampedToMaxSize(
			FMath::Max(1.0f, DragMaxSpeedCmPerSecond));
	}

	DragVelocityCmPerSecond = FMath::VInterpConstantTo(
		DragVelocityCmPerSecond,
		DesiredVelocity,
		DeltaSeconds,
		FMath::Max(1.0f, DragAccelerationCmPerSecondSquared));
	if (DragVelocityCmPerSecond.IsNearlyZero(0.01f))
	{
		DragVelocityCmPerSecond = FVector::ZeroVector;
		return;
	}

	FVector NewLocation =
		CurrentLocation + DragVelocityCmPerSecond * DeltaSeconds;
	if (bIsDraggingContainer || bHasPendingDragTarget)
	{
		const FVector ToTarget = DragTargetActorLocation - CurrentLocation;
		const FVector Remaining = DragTargetActorLocation - NewLocation;
		if (FVector::DotProduct(ToTarget, DragVelocityCmPerSecond) > 0.0f
			&& FVector::DotProduct(ToTarget, Remaining) <= 0.0f)
		{
			NewLocation = DragTargetActorLocation;
			DragVelocityCmPerSecond = FVector::ZeroVector;
			bHasPendingDragTarget = false;
			UE_LOG(
				LogWaterSimulation,
				Log,
				TEXT("Container drag completed; displacement %s cm"),
				*(NewLocation - DragStartActorLocation)
					.ToCompactString());
		}
	}
	SetActorLocation(NewLocation, false, nullptr, ETeleportType::None);
}

void AComputeWaterActor::ApplySourceDemoCamera()
{
	APlayerController* PlayerController =
		GetWorld() != nullptr
			? GetWorld()->GetFirstPlayerController()
			: nullptr;
	if (PlayerController == nullptr)
	{
		return;
	}
	AComputeWaterFreeCameraPawn* SourceCamera = SetupFreeCamera();
	if (SourceCamera == nullptr)
	{
		return;
	}

	// Unity source camera (13.28, 8.79, 20.05), converted to UE Z-up
	// and translated with the simulation floor.
	const FVector LocalCameraPosition(1328.0f, 2005.0f, 1379.0f);
	const FVector LocalCameraForward(
		-0.34214359f,
		-0.80586492f,
		-0.48323906f);
	const FVector LocalCameraUp(
		-0.18884851f,
		-0.44481033f,
		0.87548846f);
	const FVector CameraPosition =
		GetActorTransform().TransformPosition(LocalCameraPosition);
	const FRotator CameraRotation =
		FRotationMatrix::MakeFromXZ(
			GetActorTransform().TransformVectorNoScale(
				LocalCameraForward),
			GetActorTransform().TransformVectorNoScale(
				LocalCameraUp))
		.Rotator();
	SourceCamera->SetActorLocationAndRotation(
		CameraPosition,
		CameraRotation);
	PlayerController->SetControlRotation(CameraRotation);
	if (UCameraComponent* CameraComponent = SourceCamera->Camera)
	{
		// Unity preserves a 60-degree vertical FOV for every viewport aspect.
		// UE stores a horizontal FOV, so retain the 16:9 reference aspect and
		// ask the local player projection to maintain Y FOV dynamically.
		CameraComponent->SetAspectRatio(16.0f / 9.0f);
		CameraComponent->SetConstraintAspectRatio(false);
		CameraComponent->SetFieldOfView(91.492844f);
	}
	if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
	{
		LocalPlayer->AspectRatioAxisConstraint =
			AspectRatio_MaintainYFOV;
	}
	PlayerController->SetViewTarget(SourceCamera);
}
