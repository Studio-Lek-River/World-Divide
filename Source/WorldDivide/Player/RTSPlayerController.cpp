#include "Player/RTSPlayerController.h"

#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSPlacementGhost.h"
#include "Commands/RTSCommandDispatch.h"
#include "Core/RTSPlayerState.h"
#include "Core/RTSSettings.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Player/RTSCameraPawn.h"
#include "Units/RTSUnit.h"

namespace
{
	UInputAction* MakeAction(UObject* Outer, const TCHAR* Name, EInputActionValueType ValueType)
	{
		UInputAction* Action = NewObject<UInputAction>(Outer, Name);
		Action->ValueType = ValueType;
		return Action;
	}

	const FKey BuildHotkeys[ARTSPlayerController::MaxBuildHotkeys] = {
		EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine,
	};

	void MapKey(UInputMappingContext* Context, UInputAction* Action, const FKey& Key, bool bNegate = false, bool bSwizzle = false)
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		// Swizzle moves a 1D key onto the Y axis of a 2D action.
		if (bSwizzle)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Context));
		}
		if (bNegate)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
		}
	}
}

ARTSPlayerController::ARTSPlayerController()
{
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
}

void ARTSPlayerController::CreateInputActions()
{
	// Built in code rather than as assets so every binding is reviewable text.
	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_RTS"));

	PanAction = MakeAction(this, TEXT("IA_Pan"), EInputActionValueType::Axis2D);
	MapKey(MappingContext, PanAction, EKeys::D);
	MapKey(MappingContext, PanAction, EKeys::A, /*bNegate*/ true);
	MapKey(MappingContext, PanAction, EKeys::W, false, /*bSwizzle*/ true);
	MapKey(MappingContext, PanAction, EKeys::S, true, true);
	MapKey(MappingContext, PanAction, EKeys::Right);
	MapKey(MappingContext, PanAction, EKeys::Left, true);
	MapKey(MappingContext, PanAction, EKeys::Up, false, true);
	MapKey(MappingContext, PanAction, EKeys::Down, true, true);

	ZoomAction = MakeAction(this, TEXT("IA_Zoom"), EInputActionValueType::Axis1D);
	MapKey(MappingContext, ZoomAction, EKeys::MouseWheelAxis);

	RotateAction = MakeAction(this, TEXT("IA_Rotate"), EInputActionValueType::Axis1D);
	MapKey(MappingContext, RotateAction, EKeys::E);
	MapKey(MappingContext, RotateAction, EKeys::Q, true);

	RotateDragAction = MakeAction(this, TEXT("IA_RotateDrag"), EInputActionValueType::Boolean);
	MapKey(MappingContext, RotateDragAction, EKeys::MiddleMouseButton);

	SelectAction = MakeAction(this, TEXT("IA_Select"), EInputActionValueType::Boolean);
	MapKey(MappingContext, SelectAction, EKeys::LeftMouseButton);

	MoveOrderAction = MakeAction(this, TEXT("IA_MoveOrder"), EInputActionValueType::Boolean);
	MapKey(MappingContext, MoveOrderAction, EKeys::RightMouseButton);

	StopOrderAction = MakeAction(this, TEXT("IA_StopOrder"), EInputActionValueType::Boolean);
	MapKey(MappingContext, StopOrderAction, EKeys::X);

	CancelAction = MakeAction(this, TEXT("IA_Cancel"), EInputActionValueType::Boolean);
	MapKey(MappingContext, CancelAction, EKeys::Escape);

	BuildActions.Reset();
	for (int32 Index = 0; Index < MaxBuildHotkeys; ++Index)
	{
		UInputAction* Action = MakeAction(this, *FString::Printf(TEXT("IA_Build%d"), Index + 1), EInputActionValueType::Boolean);
		MapKey(MappingContext, Action, BuildHotkeys[Index]);
		BuildActions.Add(Action);
	}
}

void ARTSPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	CreateInputActions();

	// Runs once the local player is attached, which on a joining client is after BeginPlay.
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(MappingContext, 0);
	}

	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);

	UEnhancedInputComponent* Input = CastChecked<UEnhancedInputComponent>(InputComponent);
	Input->BindAction(PanAction, ETriggerEvent::Triggered, this, &ThisClass::OnPan);
	Input->BindAction(ZoomAction, ETriggerEvent::Triggered, this, &ThisClass::OnZoom);
	Input->BindAction(RotateAction, ETriggerEvent::Triggered, this, &ThisClass::OnRotate);
	Input->BindAction(RotateDragAction, ETriggerEvent::Started, this, &ThisClass::OnRotateDragStarted);
	Input->BindAction(RotateDragAction, ETriggerEvent::Completed, this, &ThisClass::OnRotateDragCompleted);
	Input->BindAction(SelectAction, ETriggerEvent::Started, this, &ThisClass::OnSelectStarted);
	Input->BindAction(SelectAction, ETriggerEvent::Completed, this, &ThisClass::OnSelectCompleted);
	Input->BindAction(MoveOrderAction, ETriggerEvent::Started, this, &ThisClass::OnMoveOrder);
	Input->BindAction(StopOrderAction, ETriggerEvent::Started, this, &ThisClass::OnStopOrder);
	Input->BindAction(CancelAction, ETriggerEvent::Started, this, &ThisClass::OnCancel);
	for (int32 Index = 0; Index < BuildActions.Num(); ++Index)
	{
		Input->BindAction(BuildActions[Index], ETriggerEvent::Started, this, &ThisClass::OnBuildHotkey, Index);
	}
}

void ARTSPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	SelectedUnits.RemoveAll([](const TWeakObjectPtr<ARTSUnit>& Unit) { return !Unit.IsValid(); });

	if (bSelecting)
	{
		GetMousePosition(SelectCurrent.X, SelectCurrent.Y);
	}

	ApplyDragRotate();
	ApplyEdgeScroll();
	UpdatePlacement();
}

void ARTSPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelPlacement();

	Super::EndPlay(EndPlayReason);
}

ARTSCameraPawn* ARTSPlayerController::GetCameraPawn() const
{
	return GetPawn<ARTSCameraPawn>();
}

int32 ARTSPlayerController::GetTeamId() const
{
	const ARTSPlayerState* State = GetPlayerState<ARTSPlayerState>();
	return State ? State->GetTeamId() : INDEX_NONE;
}

URTSFactionData* ARTSPlayerController::GetFaction() const
{
	const ARTSPlayerState* State = GetPlayerState<ARTSPlayerState>();
	return State ? State->GetFaction() : nullptr;
}

ARTSBuilding* ARTSPlayerController::GetBuildingUnderCursor() const
{
	FHitResult Hit;
	return GetHitResultUnderCursor(ECC_Visibility, false, Hit) ? Cast<ARTSBuilding>(Hit.GetActor()) : nullptr;
}

bool ARTSPlayerController::IsOwnUnit(const ARTSUnit* Unit) const
{
	return Unit && Unit->GetTeamId() == GetTeamId();
}

bool ARTSPlayerController::IsShiftDown() const
{
	return IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
}

void ARTSPlayerController::OnPan(const FInputActionValue& Value)
{
	if (ARTSCameraPawn* CameraPawn = GetCameraPawn())
	{
		CameraPawn->AddPanInput(Value.Get<FVector2D>());
	}
}

void ARTSPlayerController::OnZoom(const FInputActionValue& Value)
{
	if (ARTSCameraPawn* CameraPawn = GetCameraPawn())
	{
		CameraPawn->AddZoomInput(Value.Get<float>());
	}
}

void ARTSPlayerController::OnRotate(const FInputActionValue& Value)
{
	if (ARTSCameraPawn* CameraPawn = GetCameraPawn())
	{
		CameraPawn->AddRotateInput(Value.Get<float>() * KeyRotateSpeed * GetWorld()->GetDeltaSeconds());
	}
}

void ARTSPlayerController::OnRotateDragStarted()
{
	bRotateDragging = GetMousePosition(LastRotateMouse.X, LastRotateMouse.Y);
}

void ARTSPlayerController::OnRotateDragCompleted()
{
	bRotateDragging = false;
}

void ARTSPlayerController::ApplyDragRotate()
{
	FVector2D Mouse;
	if (!bRotateDragging || !GetMousePosition(Mouse.X, Mouse.Y))
	{
		return;
	}
	if (ARTSCameraPawn* CameraPawn = GetCameraPawn())
	{
		CameraPawn->AddRotateInput((Mouse.X - LastRotateMouse.X) * DragRotateSpeed);
	}
	LastRotateMouse = Mouse;
}

void ARTSPlayerController::ApplyEdgeScroll() const
{
	ARTSCameraPawn* CameraPawn = GetCameraPawn();
	FVector2D Mouse;
	int32 ViewportX = 0;
	int32 ViewportY = 0;
	if (!CameraPawn || bRotateDragging || !GetMousePosition(Mouse.X, Mouse.Y))
	{
		return;
	}
	GetViewportSize(ViewportX, ViewportY);
	if (ViewportX <= 0 || ViewportY <= 0)
	{
		return;
	}

	FVector2D Pan = FVector2D::ZeroVector;
	if (Mouse.X <= EdgeScrollMargin)
	{
		Pan.X = -1.f;
	}
	else if (Mouse.X >= ViewportX - EdgeScrollMargin)
	{
		Pan.X = 1.f;
	}
	// Screen Y grows downward, camera forward is up.
	if (Mouse.Y <= EdgeScrollMargin)
	{
		Pan.Y = 1.f;
	}
	else if (Mouse.Y >= ViewportY - EdgeScrollMargin)
	{
		Pan.Y = -1.f;
	}

	if (!Pan.IsZero())
	{
		CameraPawn->AddPanInput(Pan);
	}
}

void ARTSPlayerController::OnSelectStarted()
{
	if (PlacementData)
	{
		ConfirmPlacement();
		return;
	}
	bSelecting = GetMousePosition(SelectStart.X, SelectStart.Y);
	SelectCurrent = SelectStart;
}

void ARTSPlayerController::OnSelectCompleted()
{
	if (!bSelecting)
	{
		return;
	}
	bSelecting = false;
	GetMousePosition(SelectCurrent.X, SelectCurrent.Y);

	if (FVector2D::Distance(SelectStart, SelectCurrent) < DragThreshold)
	{
		SelectUnderCursor(IsShiftDown());
	}
	else
	{
		SelectInBox(SelectStart, SelectCurrent, IsShiftDown());
	}
}

bool ARTSPlayerController::GetSelectionBox(FVector2D& OutStart, FVector2D& OutEnd) const
{
	if (!bSelecting || FVector2D::Distance(SelectStart, SelectCurrent) < DragThreshold)
	{
		return false;
	}
	OutStart = SelectStart;
	OutEnd = SelectCurrent;
	return true;
}

void ARTSPlayerController::SelectUnderCursor(bool bAdditive)
{
	FHitResult Hit;
	GetHitResultUnderCursor(ECC_Visibility, false, Hit);
	ARTSUnit* Unit = Cast<ARTSUnit>(Hit.GetActor());

	if (!bAdditive)
	{
		SelectedUnits.Reset();
	}
	if (!IsOwnUnit(Unit))
	{
		return;
	}

	// Shift-click toggles a unit in or out of the selection.
	if (bAdditive && SelectedUnits.Contains(Unit))
	{
		SelectedUnits.Remove(Unit);
	}
	else
	{
		SelectedUnits.AddUnique(Unit);
	}
}

void ARTSPlayerController::SelectInBox(const FVector2D& Start, const FVector2D& End, bool bAdditive)
{
	const FBox2D Box(FVector2D::Min(Start, End), FVector2D::Max(Start, End));

	if (!bAdditive)
	{
		SelectedUnits.Reset();
	}

	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
	{
		ARTSUnit* Unit = *It;
		FVector2D ScreenLocation;
		if (IsOwnUnit(Unit)
			&& ProjectWorldLocationToScreen(Unit->GetActorLocation(), ScreenLocation)
			&& Box.IsInside(ScreenLocation))
		{
			SelectedUnits.AddUnique(Unit);
		}
	}
}

void ARTSPlayerController::OnMoveOrder()
{
	if (PlacementData)
	{
		CancelPlacement();
		return;
	}

	FHitResult Hit;
	if (SelectedUnits.IsEmpty() || !GetHitResultUnderCursor(ECC_Visibility, false, Hit))
	{
		return;
	}

	// Right-clicking an enemy unit or building attacks it; anywhere else is a move.
	FRTSCommand Command;
	const IRTSTargetable* Clicked = Cast<IRTSTargetable>(Hit.GetActor());
	if (Clicked && Clicked->GetTeamId() != GetTeamId())
	{
		Command.Type = ERTSCommandType::Attack;
		Command.TargetActor = Hit.GetActor();
	}
	else
	{
		Command.Type = ERTSCommandType::Move;
		Command.TargetLocation = Hit.Location;
	}
	Command.bQueue = IsShiftDown();
	IssueToSelection(Command);
}

void ARTSPlayerController::OnStopOrder()
{
	FRTSCommand Command;
	Command.Type = ERTSCommandType::Stop;
	IssueToSelection(Command);
}

void ARTSPlayerController::OnBuildHotkey(int32 Index)
{
	const URTSFactionData* Faction = GetFaction();
	if (Faction && Faction->Buildings.IsValidIndex(Index))
	{
		BeginPlacement(Faction->Buildings[Index]);
	}
}

void ARTSPlayerController::OnCancel()
{
	CancelPlacement();
}

void ARTSPlayerController::BeginPlacement(URTSBuildingData* Data)
{
	CancelPlacement();
	if (!Data)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	PlacementGhost = GetWorld()->SpawnActor<ARTSPlacementGhost>(Params);
	if (!PlacementGhost)
	{
		return;
	}
	PlacementGhost->SetBuildingData(*Data, GetDefault<URTSSettings>()->PlacementGhostMaterial.LoadSynchronous());
	PlacementGhost->SetActorHiddenInGame(true);
	PlacementData = Data;
	// A half-dragged selection box would otherwise finish on the confirming click.
	bSelecting = false;
}

void ARTSPlayerController::CancelPlacement()
{
	if (PlacementGhost)
	{
		PlacementGhost->Destroy();
	}
	PlacementGhost = nullptr;
	PlacementData = nullptr;
	bPlacementValid = false;
}

void ARTSPlayerController::UpdatePlacement()
{
	if (!PlacementData || !PlacementGhost)
	{
		return;
	}

	// Only the ground and obstacles position the ghost; units and buildings under the cursor make it invalid instead.
	FHitResult Hit;
	if (!GetHitResultUnderCursorForObjects({ UEngineTypes::ConvertToObjectType(ECC_WorldStatic) }, false, Hit))
	{
		bPlacementValid = false;
		PlacementGhost->SetActorHiddenInGame(true);
		return;
	}

	// The same check the server repeats, so a green ghost is a spot the server accepts.
	bPlacementValid = RTSBuildPlacement::CanPlace(*GetWorld(), *PlacementData, Hit.Location, PlacementLocation);
	PlacementGhost->SetActorLocation(PlacementLocation);
	PlacementGhost->SetValid(bPlacementValid);
	PlacementGhost->SetActorHiddenInGame(false);
}

void ARTSPlayerController::ConfirmPlacement()
{
	if (!bPlacementValid)
	{
		return;
	}

	// No builder units yet: the debug menu sends the order without units and the server places the building directly.
	FRTSCommand Command;
	Command.Type = ERTSCommandType::Build;
	Command.EntityData = PlacementData;
	Command.TargetLocation = PlacementLocation;
	Server_IssueCommand(TArray<ARTSUnit*>(), Command);
	CancelPlacement();
}

void ARTSPlayerController::IssueToSelection(const FRTSCommand& Command)
{
	TArray<ARTSUnit*> Units;
	Units.Reserve(SelectedUnits.Num());
	for (const TWeakObjectPtr<ARTSUnit>& Unit : SelectedUnits)
	{
		if (Unit.IsValid())
		{
			Units.Add(Unit.Get());
		}
	}
	if (!Units.IsEmpty())
	{
		Server_IssueCommand(Units, Command);
	}
}

void ARTSPlayerController::Server_IssueCommand_Implementation(const TArray<ARTSUnit*>& Units, const FRTSCommand& Command)
{
	const ARTSPlayerState* State = GetPlayerState<ARTSPlayerState>();
	if (!State)
	{
		return;
	}
	if (Command.Type == ERTSCommandType::Build)
	{
		// Debug build menu path. Once builder units exist, a Build order goes to them through Dispatch instead.
		RTSCommands::PlaceBuilding(*GetWorld(), State->GetFaction(), State->GetTeamId(), Command);
		return;
	}
	RTSCommands::Dispatch(RTSCommands::FilterOwnedUnits(Units, State->GetTeamId()), Command);
}
