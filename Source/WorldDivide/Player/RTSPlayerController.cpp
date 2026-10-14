#include "Player/RTSPlayerController.h"

#include "Buildings/RTSBuildPlacement.h"
#include "Buildings/RTSBuilding.h"
#include "Buildings/RTSPlacementGhost.h"
#include "Buildings/RTSProductionComponent.h"
#include "Buildings/RTSSuperweaponComponent.h"
#include "Combat/RTSTargetable.h"
#include "Commands/RTSCommandDispatch.h"
#include "Core/RTSGameMode.h"
#include "Core/RTSPlayerState.h"
#include "Core/RTSSettings.h"
#include "Data/RTSBuilderData.h"
#include "Data/RTSBuildingData.h"
#include "Data/RTSFactionData.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSUpgradeData.h"
#include "Economy/RTSSupplySource.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Player/RTSAlertSound.h"
#include "Player/RTSCameraPawn.h"
#include "Player/RTSHUD.h"
#include "Player/RTSLocalPlayer.h"
#include "UI/RTSHUDWidget.h"
#include "Units/RTSUnit.h"
#include "WorldDivide.h"

namespace
{
	/** Seconds during which hits on the player's buildings near an earlier warning raise no new one. */
	constexpr float BaseAlertCooldown = 10.f;

	/** The same for units: longer, as a fight in the field drifts and should warn once. */
	constexpr float UnitAlertCooldown = 15.f;

	/** Ground distance within which hits count as the same base area for the cooldown. */
	constexpr float AttackAlertRadius = 3000.f;

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

	// Not A: that pans the camera.
	AttackMoveAction = MakeAction(this, TEXT("IA_AttackMove"), EInputActionValueType::Boolean);
	MapKey(MappingContext, AttackMoveAction, EKeys::J);

	CancelAction = MakeAction(this, TEXT("IA_Cancel"), EInputActionValueType::Boolean);
	MapKey(MappingContext, CancelAction, EKeys::Escape);

	CancelConstructionAction = MakeAction(this, TEXT("IA_CancelConstruction"), EInputActionValueType::Boolean);
	MapKey(MappingContext, CancelConstructionAction, EKeys::Delete);

	JumpToAttackAction = MakeAction(this, TEXT("IA_JumpToAttack"), EInputActionValueType::Boolean);
	MapKey(MappingContext, JumpToAttackAction, EKeys::SpaceBar);

	// Not the mouse wheel: that zooms the camera, also while placing.
	RotatePlacementAction = MakeAction(this, TEXT("IA_RotatePlacement"), EInputActionValueType::Boolean);
	MapKey(MappingContext, RotatePlacementAction, EKeys::R);

#if !UE_BUILD_SHIPPING
	ReloadUIAction = MakeAction(this, TEXT("IA_ReloadUI"), EInputActionValueType::Boolean);
	MapKey(MappingContext, ReloadUIAction, EKeys::F5);
#endif

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
	// A headless test run has no real viewport; locking would clip the desktop cursor to a corner.
	const bool bLockMouse = FApp::CanEverRender() && !GIsAutomationTesting;
	InputMode.SetLockMouseToViewportBehavior(bLockMouse ? EMouseLockMode::LockAlways : EMouseLockMode::DoNotLock);
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
	Input->BindAction(AttackMoveAction, ETriggerEvent::Started, this, &ThisClass::OnAttackMoveKey);
	Input->BindAction(CancelAction, ETriggerEvent::Started, this, &ThisClass::OnCancel);
	Input->BindAction(CancelConstructionAction, ETriggerEvent::Started, this, &ThisClass::OnCancelConstruction);
	Input->BindAction(JumpToAttackAction, ETriggerEvent::Started, this, &ThisClass::OnJumpToAttack);
	Input->BindAction(RotatePlacementAction, ETriggerEvent::Started, this, &ThisClass::OnRotatePlacement);
#if !UE_BUILD_SHIPPING
	Input->BindAction(ReloadUIAction, ETriggerEvent::Started, this, &ThisClass::OnReloadUI);
#endif
	for (int32 Index = 0; Index < BuildActions.Num(); ++Index)
	{
		Input->BindAction(BuildActions[Index], ETriggerEvent::Started, this, &ThisClass::OnBuildHotkey, Index);
	}
}

void ARTSPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	SelectedUnits.RemoveAll([](const TWeakObjectPtr<ARTSUnit>& Unit) { return !Unit.IsValid(); });
	UpdateSelection();
	if (bAttackMoveArmed && SelectedUnits.IsEmpty())
	{
		SetAttackMoveArmed(false);
	}
	if (bSuperweaponTargeting && !CanRequestLaunch(GetSelectedSuperweapon()))
	{
		SetSuperweaponTargeting(false);
	}

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

int32 ARTSPlayerController::GetCredits() const
{
	const ARTSPlayerState* State = GetPlayerState<ARTSPlayerState>();
	return State ? State->GetCredits() : 0;
}

bool ARTSPlayerController::CanAfford(const URTSEntityData& Data) const
{
	return GetCredits() >= Data.Cost;
}

ARTSBuilding* ARTSPlayerController::GetBuildingUnderCursor() const
{
	FHitResult Hit;
	return GetHitResultUnderCursor(ECC_Visibility, false, Hit) ? Cast<ARTSBuilding>(Hit.GetActor()) : nullptr;
}

ARTSUnit* ARTSPlayerController::GetUnitUnderCursor() const
{
	FVector Origin;
	FVector Direction;
	return DeprojectMousePositionToWorld(Origin, Direction) ? GetUnitAlongRay(Origin, Direction) : nullptr;
}

ARTSUnit* ARTSPlayerController::GetUnitAlongRay(const FVector& Origin, const FVector& Direction) const
{
	// The trace GetHitResultUnderCursor makes from the deprojected cursor.
	FHitResult Hit;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(ClickableTrace), false);
	return GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * HitResultTraceDistance, ECC_Visibility, Params)
		? Cast<ARTSUnit>(Hit.GetActor())
		: nullptr;
}

bool ARTSPlayerController::SelectBuilding(ARTSBuilding* Building)
{
	if (!IsValid(Building) || Building->GetTeamId() != GetTeamId())
	{
		return false;
	}
	SelectedUnits.Reset();
	SelectedBuilding = Building;
	UpdateSelection();
	return true;
}

void ARTSPlayerController::SelectUnits(const TArray<ARTSUnit*>& Units)
{
	SelectedUnits.Reset();
	SelectedBuilding = nullptr;
	for (ARTSUnit* Unit : Units)
	{
		if (IsValid(Unit) && IsOwnUnit(Unit))
		{
			SelectedUnits.AddUnique(Unit);
		}
	}
	UpdateSelection();
}

TArray<FRTSSelectionGroup> ARTSPlayerController::GroupByType(const TArray<TWeakObjectPtr<ARTSUnit>>& Units)
{
	TArray<FRTSSelectionGroup> Groups;
	for (const TWeakObjectPtr<ARTSUnit>& Unit : Units)
	{
		const URTSUnitData* Data = Unit.IsValid() ? Unit->GetUnitData() : nullptr;
		if (!Data)
		{
			continue;
		}
		FRTSSelectionGroup* Group = Groups.FindByPredicate([Data](const FRTSSelectionGroup& Each) { return Each.Data == Data; });
		if (!Group)
		{
			Group = &Groups.AddDefaulted_GetRef();
			Group->Data = Data;
		}
		Group->Units.Add(Unit);
	}
	return Groups;
}

bool ARTSPlayerController::SelectGroup(int32 Index)
{
	if (!SelectionGroups.IsValidIndex(Index))
	{
		return false;
	}
	TArray<ARTSUnit*> Units;
	for (const TWeakObjectPtr<ARTSUnit>& Unit : SelectionGroups[Index].Units)
	{
		if (Unit.IsValid())
		{
			Units.Add(Unit.Get());
		}
	}
	SelectUnits(Units);
	return true;
}

void ARTSPlayerController::UpdateSelection()
{
	TArray<FObjectKey> Units;
	Units.Reserve(SelectedUnits.Num());
	for (const TWeakObjectPtr<ARTSUnit>& Unit : SelectedUnits)
	{
		Units.Add(FObjectKey(Unit.Get()));
	}
	const FObjectKey Building(SelectedBuilding.Get());
	if (Units == AnnouncedUnits && Building == AnnouncedBuilding)
	{
		return;
	}
	AnnouncedUnits = MoveTemp(Units);
	AnnouncedBuilding = Building;
	SelectionGroups = GroupByType(SelectedUnits);
	OnSelectionChanged.Broadcast();
}

bool ARTSPlayerController::CanCancelConstruction(const ARTSBuilding* Building) const
{
	return IsValid(Building) && Building->GetTeamId() == GetTeamId() && !Building->IsConstructed();
}

bool ARTSPlayerController::RequestCancelConstruction(ARTSBuilding* Building)
{
	if (!CanCancelConstruction(Building))
	{
		return false;
	}
	FRTSCommand Command;
	Command.Type = ERTSCommandType::CancelConstruction;
	Command.TargetActor = Building;
	Server_IssueCommand(TArray<ARTSUnit*>(), Command);
	return true;
}

ERTSMatchResult ARTSPlayerController::GetMatchResult() const
{
	const ARTSGameState* GameState = GetWorld()->GetGameState<ARTSGameState>();
	return GameState ? GameState->GetMatchResult(GetTeamId()) : ERTSMatchResult::InProgress;
}

bool ARTSPlayerController::CanRestart() const
{
	return IsLocalController() && HasAuthority() && ARTSGameState::IsMatchOver(*GetWorld());
}

bool ARTSPlayerController::RequestRestart()
{
	if (!CanRestart())
	{
		return false;
	}
	// Reloads the current map with the same seats; clients travel along.
	const ARTSGameMode* GameMode = GetWorld()->GetAuthGameMode<ARTSGameMode>();
	return GetWorld()->ServerTravel(GameMode ? GameMode->GetRestartURL() : TEXT("?Restart"));
}

void ARTSPlayerController::HandleEscape()
{
	if (PlacementData || bAttackMoveArmed || bSuperweaponTargeting)
	{
		CancelPlacement();
		CancelAttackMove();
		CancelSuperweaponTargeting();
		return;
	}
	SetInGameMenuOpen(!bInGameMenuOpen);
}

void ARTSPlayerController::SetInGameMenuOpen(bool bOpen)
{
	if (bOpen)
	{
		CancelPlacement();
		CancelAttackMove();
		CancelSuperweaponTargeting();
		bSelecting = false;
	}
	bInGameMenuOpen = bOpen;
}

void ARTSPlayerController::LeaveMatch()
{
	bInGameMenuOpen = false;
	if (URTSLocalPlayer* LocalPlayer = Cast<URTSLocalPlayer>(GetLocalPlayer()))
	{
		LocalPlayer->SetSeatToken(FString());
	}
	// Opening a map without "listen" also closes a hosted match; its clients lose the connection and return to the menu.
	UGameplayStatics::OpenLevelBySoftObjectPtr(this, GetDefault<URTSSettings>()->MainMenuMap);
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
	if (!CameraPawn || bRotateDragging || bInGameMenuOpen || !GetMousePosition(Mouse.X, Mouse.Y))
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

bool ARTSPlayerController::IsOnHUDPanel(const FVector2D& Point) const
{
	const ARTSHUD* RTSHUD = GetHUD<ARTSHUD>();
	const URTSHUDWidget* Widget = RTSHUD ? RTSHUD->GetHUDWidget() : nullptr;
	return Widget && Widget->IsOnPanel(Point);
}

void ARTSPlayerController::OnSelectStarted()
{
	FVector2D Mouse;
	if (GetMousePosition(Mouse.X, Mouse.Y))
	{
		HandleSelectPressed(Mouse);
	}
}

void ARTSPlayerController::HandleSelectPressed(const FVector2D& Mouse)
{
	// The menu is a modal screen of the HUD widget, which takes every click while it is open.
	if (bInGameMenuOpen)
	{
		return;
	}
	// The widget keeps the click from the game, but a press that still arrives here must not reach the world either.
	if (IsOnHUDPanel(Mouse))
	{
		return;
	}
	if (PlacementData)
	{
		ConfirmPlacement(IsShiftDown());
		return;
	}
	if (bAttackMoveArmed)
	{
		FHitResult Hit;
		if (GetHitResultUnderCursor(ECC_Visibility, false, Hit))
		{
			ConfirmAttackMove(Hit.Location, IsShiftDown());
		}
		return;
	}
	if (bSuperweaponTargeting)
	{
		FHitResult Hit;
		if (GetHitResultUnderCursor(ECC_Visibility, false, Hit))
		{
			RequestLaunchSuperweapon(GetSelectedSuperweapon(), Hit.Location);
		}
		CancelSuperweaponTargeting();
		return;
	}
	if (const ARTSHUD* RTSHUD = GetHUD<ARTSHUD>())
	{
		if (RTSHUD->IsOnLaunchButton(Mouse))
		{
			BeginSuperweaponTargeting();
			return;
		}
		// The production menu of the selected building; the HUD only draws it while there is one.
		ARTSBuilding* Producer = GetProductionBuilding();
		const TArray<URTSEntityData*> Menu = GetProductionMenu();
		const int32 MenuIndex = RTSHUD->GetProduceButtonAt(Mouse);
		if (Producer && Menu.IsValidIndex(MenuIndex))
		{
			RequestProduce(Producer, Menu[MenuIndex]);
			return;
		}
	}
	bSelecting = true;
	SelectStart = Mouse;
	SelectCurrent = SelectStart;
}

void ARTSPlayerController::OnSelectCompleted()
{
	FVector2D Mouse = SelectCurrent;
	GetMousePosition(Mouse.X, Mouse.Y);
	HandleSelectReleased(Mouse);
}

void ARTSPlayerController::HandleSelectReleased(const FVector2D& Point)
{
	if (!bSelecting)
	{
		return;
	}
	bSelecting = false;
	SelectCurrent = Point;

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
	if (SelectBuilding(Cast<ARTSBuilding>(Hit.GetActor())))
	{
		return;
	}
	ARTSUnit* Unit = Cast<ARTSUnit>(Hit.GetActor());

	if (!bAdditive)
	{
		SelectedUnits.Reset();
		SelectedBuilding = nullptr;
	}
	if (IsOwnUnit(Unit))
	{
		SelectedBuilding = nullptr;

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
	UpdateSelection();
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
	if (!bAdditive || !SelectedUnits.IsEmpty())
	{
		SelectedBuilding = nullptr;
	}
	UpdateSelection();
}

void ARTSPlayerController::OnMoveOrder()
{
	if (bInGameMenuOpen)
	{
		return;
	}
	if (PlacementData)
	{
		CancelPlacement();
		return;
	}
	if (bAttackMoveArmed)
	{
		CancelAttackMove();
		return;
	}
	if (bSuperweaponTargeting)
	{
		CancelSuperweaponTargeting();
		return;
	}

	FVector2D Mouse;
	FHitResult Hit;
	if (!GetMousePosition(Mouse.X, Mouse.Y) || IsOnHUDPanel(Mouse) || !GetHitResultUnderCursor(ECC_Visibility, false, Hit))
	{
		return;
	}
	if (SelectedUnits.IsEmpty())
	{
		// A production building is selected on its own: the click moves its rally point.
		RequestRallyPoint(GetProductionBuilding(), Hit.Location);
		return;
	}

	// Right-clicking an enemy unit or building attacks it, an own construction site resumes it, a supply source harvests it;
	// anywhere else is a move.
	FRTSCommand Command;
	const IRTSTargetable* Clicked = Cast<IRTSTargetable>(Hit.GetActor());
	const ARTSBuilding* ClickedBuilding = Cast<ARTSBuilding>(Hit.GetActor());
	if (Clicked && Clicked->GetTeamId() != GetTeamId())
	{
		Command.Type = ERTSCommandType::Attack;
		Command.TargetActor = Hit.GetActor();
	}
	else if (ClickedBuilding && !ClickedBuilding->IsConstructed())
	{
		// An own construction site: builders resume it, the others walk up to it.
		Command.Type = ERTSCommandType::Build;
		Command.TargetActor = Hit.GetActor();
	}
	else if (Hit.GetActor() && Hit.GetActor()->IsA<ARTSSupplySource>())
	{
		Command.Type = ERTSCommandType::Harvest;
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
	if (bInGameMenuOpen)
	{
		return;
	}
	FRTSCommand Command;
	Command.Type = ERTSCommandType::Stop;
	IssueToSelection(Command);
}

void ARTSPlayerController::OnAttackMoveKey()
{
	BeginAttackMove();
}

bool ARTSPlayerController::BeginAttackMove()
{
	if (SelectedUnits.IsEmpty() || bInGameMenuOpen)
	{
		return false;
	}
	CancelPlacement();
	CancelSuperweaponTargeting();
	SetAttackMoveArmed(true);
	return true;
}

void ARTSPlayerController::CancelAttackMove()
{
	SetAttackMoveArmed(false);
}

void ARTSPlayerController::SetAttackMoveArmed(bool bArmed)
{
	bAttackMoveArmed = bArmed;
	CurrentMouseCursor = bAttackMoveArmed || bSuperweaponTargeting ? EMouseCursor::Crosshairs : EMouseCursor::Default;
}

ARTSBuilding* ARTSPlayerController::GetSelectedSuperweapon() const
{
	ARTSBuilding* Building = GetSelectedBuilding();
	return Building && Building->GetTeamId() == GetTeamId() && Building->GetSuperweaponComponent()->IsOperational() ? Building : nullptr;
}

bool ARTSPlayerController::CanRequestLaunch(const ARTSBuilding* Building) const
{
	return IsValid(Building) && Building->GetTeamId() == GetTeamId() && Building->GetSuperweaponComponent()->CanLaunch();
}

bool ARTSPlayerController::RequestLaunchSuperweapon(ARTSBuilding* Building, const FVector& Location)
{
	if (!CanRequestLaunch(Building))
	{
		return false;
	}
	FRTSCommand Command;
	Command.Type = ERTSCommandType::LaunchSuperweapon;
	Command.TargetActor = Building;
	Command.TargetLocation = Location;
	Server_IssueCommand(TArray<ARTSUnit*>(), Command);
	return true;
}

bool ARTSPlayerController::BeginSuperweaponTargeting()
{
	if (bInGameMenuOpen || !CanRequestLaunch(GetSelectedSuperweapon()))
	{
		return false;
	}
	CancelPlacement();
	CancelAttackMove();
	SetSuperweaponTargeting(true);
	return true;
}

void ARTSPlayerController::CancelSuperweaponTargeting()
{
	SetSuperweaponTargeting(false);
}

void ARTSPlayerController::SetSuperweaponTargeting(bool bTargeting)
{
	bSuperweaponTargeting = bTargeting;
	CurrentMouseCursor = bAttackMoveArmed || bSuperweaponTargeting ? EMouseCursor::Crosshairs : EMouseCursor::Default;
}

void ARTSPlayerController::ConfirmAttackMove(const FVector& Location, bool bQueue)
{
	// Units fight whatever comes into range on the way, the clicked enemy included.
	FRTSCommand Command;
	Command.Type = ERTSCommandType::AttackMove;
	Command.TargetLocation = Location;
	Command.bQueue = bQueue;
	IssueToSelection(Command);
	// Shift keeps attack-move armed for the next waypoint, as it keeps placing buildings.
	if (!bQueue)
	{
		CancelAttackMove();
	}
}

ARTSUnit* ARTSPlayerController::GetSelectedBuilder() const
{
	for (const TWeakObjectPtr<ARTSUnit>& Unit : SelectedUnits)
	{
		// Unit data replicates; the builder component only knows its data on the server.
		if (Unit.IsValid() && Cast<URTSBuilderData>(Unit->GetUnitData()))
		{
			return Unit.Get();
		}
	}
	return nullptr;
}

TArray<URTSBuildingData*> ARTSPlayerController::GetBuildMenu() const
{
	TArray<URTSBuildingData*> Menu;
	const URTSFactionData* Faction = GetFaction();
	if (const ARTSUnit* Builder = GetSelectedBuilder())
	{
		for (URTSBuildingData* Data : CastChecked<URTSBuilderData>(Builder->GetUnitData())->Buildings)
		{
			Menu.Add(Data);
		}
	}
	else if (Faction && RTSCommands::IsDebugBuildEnabled() && !GetProductionBuilding())
	{
		for (URTSBuildingData* Data : Faction->Buildings)
		{
			Menu.Add(Data);
		}
	}
	return Menu;
}

ARTSBuilding* ARTSPlayerController::GetProductionBuilding() const
{
	ARTSBuilding* Building = GetSelectedBuilding();
	return Building && Building->GetTeamId() == GetTeamId() && Building->GetProductionComponent()->CanProduce() ? Building : nullptr;
}

TArray<URTSEntityData*> ARTSPlayerController::GetProductionMenu() const
{
	TArray<URTSEntityData*> Menu;
	if (const ARTSBuilding* Building = GetProductionBuilding())
	{
		for (URTSUnitData* Data : Building->GetBuildingData()->Units)
		{
			Menu.Add(Data);
		}
		for (URTSUpgradeData* Data : Building->GetBuildingData()->Upgrades)
		{
			Menu.Add(Data);
		}
	}
	return Menu;
}

bool ARTSPlayerController::HasUpgrade(const URTSUpgradeData& Upgrade) const
{
	const ARTSPlayerState* State = GetPlayerState<ARTSPlayerState>();
	return State && State->HasUpgrade(&Upgrade);
}

const URTSEntityData* ARTSPlayerController::GetMissingPrerequisite(const URTSEntityData& Data) const
{
	return RTSProduction::FindMissingPrerequisite(*GetWorld(), GetTeamId(), Data);
}

bool ARTSPlayerController::IsAtBuildLimit(const URTSBuildingData& Data) const
{
	return RTSProduction::IsAtBuildLimit(*GetWorld(), GetTeamId(), Data);
}

bool ARTSPlayerController::CanRequestProduce(const ARTSBuilding* Building, const URTSEntityData* Data) const
{
	if (!IsValid(Building) || Building->GetTeamId() != GetTeamId() || !Data)
	{
		return false;
	}
	const URTSUpgradeData* Upgrade = Cast<URTSUpgradeData>(Data);
	if (Upgrade && (HasUpgrade(*Upgrade) || RTSProduction::IsUpgradeQueued(*GetWorld(), GetTeamId(), *Upgrade)))
	{
		return false;
	}
	const URTSProductionComponent* Production = Building->GetProductionComponent();
	return Production->CanProduce(Data) && Production->GetQueue().Num() < URTSProductionComponent::MaxQueue
		&& CanAfford(*Data) && !GetMissingPrerequisite(*Data);
}

bool ARTSPlayerController::RequestProduce(ARTSBuilding* Building, URTSEntityData* Data)
{
	if (!CanRequestProduce(Building, Data))
	{
		return false;
	}
	FRTSCommand Command;
	Command.Type = ERTSCommandType::Produce;
	Command.TargetActor = Building;
	Command.EntityData = Data;
	Server_IssueCommand(TArray<ARTSUnit*>(), Command);
	return true;
}

bool ARTSPlayerController::RequestCancelProduction(ARTSBuilding* Building, URTSEntityData* Data, int32 QueueIndex)
{
	if (!IsValid(Building) || Building->GetTeamId() != GetTeamId() || !Data)
	{
		return false;
	}
	const TArray<TObjectPtr<URTSEntityData>>& Queue = Building->GetProductionComponent()->GetQueue();
	if (QueueIndex == INDEX_NONE ? !Queue.Contains(Data) : !Queue.IsValidIndex(QueueIndex) || Queue[QueueIndex] != Data)
	{
		return false;
	}
	FRTSCommand Command;
	Command.Type = ERTSCommandType::CancelProduction;
	Command.TargetActor = Building;
	Command.EntityData = Data;
	Command.QueueIndex = QueueIndex;
	Server_IssueCommand(TArray<ARTSUnit*>(), Command);
	return true;
}

bool ARTSPlayerController::RequestRallyPoint(ARTSBuilding* Building, const FVector& Location)
{
	if (!IsValid(Building) || Building->GetTeamId() != GetTeamId() || !Building->GetProductionComponent()->CanProduce())
	{
		return false;
	}
	FRTSCommand Command;
	Command.Type = ERTSCommandType::SetRallyPoint;
	Command.TargetActor = Building;
	Command.TargetLocation = Location;
	Server_IssueCommand(TArray<ARTSUnit*>(), Command);
	return true;
}

void ARTSPlayerController::OnBuildHotkey(int32 Index)
{
	if (bInGameMenuOpen)
	{
		return;
	}
	// A selected superweapon has one entry: launch.
	if (GetSelectedSuperweapon())
	{
		if (Index == 0)
		{
			BeginSuperweaponTargeting();
		}
		return;
	}
	if (ARTSBuilding* Producer = GetProductionBuilding())
	{
		const TArray<URTSEntityData*> Menu = GetProductionMenu();
		if (Menu.IsValidIndex(Index))
		{
			RequestProduce(Producer, Menu[Index]);
		}
		return;
	}

	const TArray<URTSBuildingData*> Menu = GetBuildMenu();
	if (Menu.IsValidIndex(Index))
	{
		BeginPlacement(Menu[Index]);
	}
}

void ARTSPlayerController::OnCancel()
{
	HandleEscape();
}

void ARTSPlayerController::OnReloadUI()
{
	URTSHUDWidget::ReloadAll(GetWorld());
}

void ARTSPlayerController::OnCancelConstruction()
{
	if (bInGameMenuOpen)
	{
		return;
	}
	ARTSBuilding* Hovered = GetBuildingUnderCursor();
	if (RequestCancelConstruction(CanCancelConstruction(Hovered) ? Hovered : GetSelectedBuilding()))
	{
		return;
	}
	// No construction site to cancel: Delete takes the last entry off the selected production building's queue.
	if (ARTSBuilding* Producer = GetProductionBuilding())
	{
		const TArray<TObjectPtr<URTSEntityData>>& Queue = Producer->GetProductionComponent()->GetQueue();
		if (!Queue.IsEmpty())
		{
			RequestCancelProduction(Producer, Queue.Last());
		}
	}
}

void ARTSPlayerController::BeginPlacement(URTSBuildingData* Data)
{
	CancelPlacement();
	CancelAttackMove();
	if (!Data || bInGameMenuOpen || GetMissingPrerequisite(*Data) || IsAtBuildLimit(*Data))
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
	PlacementBuilder = GetSelectedBuilder();
	bPlacementDebug = !PlacementBuilder.IsValid();
	PlacementQuarterTurns = 0;
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
	PlacementBuilder = nullptr;
	bPlacementDebug = false;
	bPlacementValid = false;
	PlacementQuarterTurns = 0;
}

void ARTSPlayerController::RotatePlacement()
{
	if (PlacementData)
	{
		PlacementQuarterTurns = (PlacementQuarterTurns + 1) % 4;
	}
}

void ARTSPlayerController::UpdatePlacement()
{
	if (!PlacementData || !PlacementGhost)
	{
		return;
	}
	if (!bPlacementDebug && !PlacementBuilder.IsValid())
	{
		// The builder died: nobody is left to send.
		CancelPlacement();
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
	UpdatePlacementAt(Hit.Location);
}

void ARTSPlayerController::UpdatePlacementAt(const FVector& CursorLocation)
{
	if (!PlacementData || !PlacementGhost)
	{
		return;
	}

	// The same checks the server repeats, so a green ghost is a spot the server accepts. Own units may stand on a builder's
	// spot: it sends them off when it arrives.
	const ARTSUnit* Builder = PlacementBuilder.Get();
	const int32 TeamId = GetTeamId();
	const bool bSpotValid = Builder
		? RTSBuildPlacement::CanBuilderPlace(*GetWorld(), *PlacementData, CursorLocation, PlacementLocation, Builder->GetTeamId(),
			PlacementQuarterTurns)
		: RTSBuildPlacement::CanPlace(*GetWorld(), *PlacementData, CursorLocation, PlacementLocation, nullptr, PlacementQuarterTurns);
	// Sites own builders still have to place keep their spot. Without Shift the builder's own are dropped by the new order.
	const ARTSUnit* Replaced = IsShiftDown() ? nullptr : Builder;
	const bool bPlanned = RTSBuildPlacement::OverlapsPlannedSite(*GetWorld(), TeamId, *PlacementData, CursorLocation,
		PlacementQuarterTurns, Replaced);
	// A prerequisite lost or the limit reached while placing turns the ghost red; the server would refuse the order.
	bPlacementValid = bSpotValid && !bPlanned && CanAfford(*PlacementData) && !GetMissingPrerequisite(*PlacementData)
		&& !IsAtBuildLimit(*PlacementData);
	PlacementGhost->SetActorLocationAndRotation(PlacementLocation, FRotator(0.f, RTSBuildPlacement::GetYaw(PlacementQuarterTurns), 0.f));
	PlacementGhost->SetValid(bPlacementValid);
	PlacementGhost->SetActorHiddenInGame(false);
}

void ARTSPlayerController::ConfirmPlacement(bool bKeepPlacing)
{
	if (!bPlacementValid)
	{
		return;
	}

	FRTSCommand Command;
	Command.Type = ERTSCommandType::Build;
	Command.EntityData = PlacementData;
	Command.TargetLocation = PlacementLocation;
	Command.QuarterTurns = static_cast<uint8>(PlacementQuarterTurns);
	if (ARTSUnit* Builder = PlacementBuilder.Get())
	{
		// The builder walks there, places the building and constructs it. Shift queues it behind its other orders.
		Command.bQueue = bKeepPlacing;
		Server_IssueCommand({ Builder }, Command);
	}
	else if (bPlacementDebug)
	{
		// The debug menu sends the order without units and the server places the building directly.
		Server_IssueCommand(TArray<ARTSUnit*>(), Command);
	}
	if (!bKeepPlacing)
	{
		CancelPlacement();
	}
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
	if (ARTSPlayerState* State = GetPlayerState<ARTSPlayerState>())
	{
		RTSCommands::Issue(*State, Units, Command);
	}
}

void ARTSPlayerController::NotifyTeamAttacked(const AActor& Victim, int32 TeamId, AActor* DamageInstigator, ERTSAttackAlert Kind)
{
	const IRTSTargetable* Attacker = Cast<IRTSTargetable>(DamageInstigator);
	if (Attacker && Attacker->GetTeamId() == TeamId)
	{
		return;
	}
	// Only player controllers: the AI and an empty seat get no warning.
	for (FConstPlayerControllerIterator It = Victim.GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ARTSPlayerController* Controller = Cast<ARTSPlayerController>(It->Get());
		if (Controller && Controller->GetTeamId() == TeamId)
		{
			Controller->NotifyAttacked(Victim.GetActorLocation(), Kind);
		}
	}
}

void ARTSPlayerController::NotifyAttacked(const FVector& Location, ERTSAttackAlert Kind)
{
	check(HasAuthority());
	const float Now = GetWorld()->GetTimeSeconds();
	SentAttackAlerts.RemoveAll([Now](const FSentAttackAlert& Sent)
	{
		return Now - Sent.Time >= (Sent.Kind == ERTSAttackAlert::Base ? BaseAlertCooldown : UnitAlertCooldown);
	});
	const bool bRecentNearby = SentAttackAlerts.ContainsByPredicate([&Location, Kind](const FSentAttackAlert& Sent)
	{
		return Sent.Kind == Kind && FVector::DistSquared2D(Sent.Location, Location) < FMath::Square(AttackAlertRadius);
	});
	if (bRecentNearby)
	{
		return;
	}
	SentAttackAlerts.Add({ Location, Now, Kind });
	Client_UnderAttack(Location, Kind);
}

void ARTSPlayerController::Client_UnderAttack_Implementation(const FVector& Location, ERTSAttackAlert Kind)
{
	FReceivedAttackAlert& Received = ReceivedAttackAlerts[static_cast<int32>(Kind)];
	++Received.Count;
	Received.Location = Location;
	Received.Time = GetWorld()->GetTimeSeconds();
	// Only the owner gets this call, and the server's cooldown already spaces the warnings, so the sound needs no limit of its own.
	++Received.SoundCount;
	RTSAlertSound::Play(*this, Kind);
}

void ARTSPlayerController::NotifySuperweapon(const ARTSBuilding& Superweapon, ERTSSuperweaponAlert Kind)
{
	// Only player controllers: the AI and an empty seat get no warning.
	for (FConstPlayerControllerIterator It = Superweapon.GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ARTSPlayerController* Controller = Cast<ARTSPlayerController>(It->Get());
		if (!Controller)
		{
			continue;
		}
		const bool bOwn = Controller->GetTeamId() == Superweapon.GetTeamId();
		if (!bOwn || Kind == ERTSSuperweaponAlert::Ready)
		{
			Controller->Client_SuperweaponAlert(Kind, bOwn);
		}
	}
}

void ARTSPlayerController::Client_SuperweaponAlert_Implementation(ERTSSuperweaponAlert Kind, bool bOwn)
{
	FReceivedSuperweaponAlert& Received = ReceivedSuperweaponAlerts[static_cast<int32>(Kind)];
	++Received.Count;
	Received.bOwn = bOwn;
	Received.Time = GetWorld()->GetTimeSeconds();
	// The base alarm until superweapons get a sound of their own.
	RTSAlertSound::Play(*this, ERTSAttackAlert::Base);
}

bool ARTSPlayerController::GetLatestSuperweaponAlert(ERTSSuperweaponAlert& OutKind, bool& bOutOwn, float& OutAge) const
{
	bool bFound = false;
	for (const ERTSSuperweaponAlert Kind : { ERTSSuperweaponAlert::Built, ERTSSuperweaponAlert::Ready })
	{
		const FReceivedSuperweaponAlert& Received = ReceivedSuperweaponAlerts[static_cast<int32>(Kind)];
		const float Age = GetWorld()->GetTimeSeconds() - Received.Time;
		if (Received.Count > 0 && (!bFound || Age < OutAge))
		{
			OutKind = Kind;
			bOutOwn = Received.bOwn;
			OutAge = Age;
			bFound = true;
		}
	}
	return bFound;
}

bool ARTSPlayerController::GetLastAttackAlert(ERTSAttackAlert Kind, FVector& OutLocation, float& OutAge) const
{
	const FReceivedAttackAlert& Received = ReceivedAttackAlerts[static_cast<int32>(Kind)];
	if (Received.Count == 0)
	{
		return false;
	}
	OutLocation = Received.Location;
	OutAge = GetWorld()->GetTimeSeconds() - Received.Time;
	return true;
}

bool ARTSPlayerController::GetLatestAttackAlert(FVector& OutLocation, float& OutAge) const
{
	bool bFound = false;
	for (const ERTSAttackAlert Kind : { ERTSAttackAlert::Base, ERTSAttackAlert::Units })
	{
		FVector Location;
		float Age = 0.f;
		if (GetLastAttackAlert(Kind, Location, Age) && (!bFound || Age < OutAge))
		{
			OutLocation = Location;
			OutAge = Age;
			bFound = true;
		}
	}
	return bFound;
}

bool ARTSPlayerController::JumpToLatestAttack()
{
	ARTSCameraPawn* CameraPawn = GetCameraPawn();
	FVector Location;
	float Age = 0.f;
	if (!CameraPawn || !GetLatestAttackAlert(Location, Age) || Age >= AttackJumpMaxAge)
	{
		return false;
	}
	CameraPawn->JumpTo(Location);
	return true;
}

void ARTSPlayerController::OnJumpToAttack()
{
	if (bInGameMenuOpen)
	{
		return;
	}
	JumpToLatestAttack();
}

void ARTSPlayerController::OnRotatePlacement()
{
	if (bInGameMenuOpen)
	{
		return;
	}
	RotatePlacement();
}
