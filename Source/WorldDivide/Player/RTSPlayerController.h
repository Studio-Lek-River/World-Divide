#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Commands/RTSCommand.h"
#include "RTSPlayerController.generated.h"

class ARTSBuilding;
class ARTSCameraPawn;
class ARTSPlacementGhost;
class ARTSUnit;
class URTSBuildingData;
class URTSEntityData;
class URTSFactionData;
class URTSUnitData;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 * Turns mouse and keyboard into camera movement, a local selection and FRTSCommands for the server.
 *
 * Controls: WASD/arrows or screen edge pan, mouse wheel zoom, Q/E or middle-mouse drag rotate,
 * left click/drag select (Shift adds; a click on an own building selects that building), right click move, attack an
 * enemy unit or building, or harvest a supply source (Shift queues), X stop. J then left click attack-moves the selection to
 * the clicked spot (Shift queues and keeps attack-move armed); right click, Esc or an emptied selection cancels it, and the
 * HUD shows a hint while it is armed. Delete cancels the own
 * construction site under the cursor, or else the selected one, as does the HUD's cancel button.
 * With a builder selected, 1-9 pick a building from its list to place: left click sends the builder (Shift queues it and
 * keeps placing), right click or Esc cancels. A right click on an own construction site sends builders to resume it.
 * With rts.DebugBuild on and no builder selected, 1-9 pick from the faction's buildings and place them without a builder.
 * With an own production building selected, 1-9 or a click on the HUD's menu queue a unit, right click sets the rally point,
 * and a click on a queued unit, or Delete for the last one, cancels it for a refund.
 */
UCLASS()
class WORLDDIVIDE_API ARTSPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ARTSPlayerController();

	const TArray<TWeakObjectPtr<ARTSUnit>>& GetSelectedUnits() const { return SelectedUnits; }

	/** The own building selected by a click, or null. Units and a building are never selected together. */
	ARTSBuilding* GetSelectedBuilding() const { return SelectedBuilding.Get(); }

	/** Selects Building in place of the current selection; false, changing nothing, when it is not the player's own. */
	bool SelectBuilding(ARTSBuilding* Building);

	/** Selects the player's own units among Units in place of the current selection, the selected building included. */
	void SelectUnits(const TArray<ARTSUnit*>& Units);

	/** Building is one of the player's own construction sites. The server checks again before it cancels. */
	bool CanCancelConstruction(const ARTSBuilding* Building) const;

	/** Asks the server to cancel Building for a refund; false when CanCancelConstruction refuses it locally. */
	bool RequestCancelConstruction(ARTSBuilding* Building);

	/** True while the player drags a selection box; outputs its corners in viewport pixels. */
	bool GetSelectionBox(FVector2D& OutStart, FVector2D& OutEnd) const;

	int32 GetTeamId() const;

	/** The building of any team under the mouse cursor, or null. */
	ARTSBuilding* GetBuildingUnderCursor() const;

	/** The local player's faction, or null before the player state has replicated. */
	URTSFactionData* GetFaction() const;

	/** The local player's credits, 0 before the player state has replicated. */
	int32 GetCredits() const;

	/** The local player has the credits for Data's cost. The server checks again before it charges. */
	bool CanAfford(const URTSEntityData& Data) const;

	/** Number of build menu entries the 1-9 keys reach. */
	static constexpr int32 MaxBuildHotkeys = 9;

	/** The first selected unit that can build, or null. Its list is the build menu. */
	ARTSUnit* GetSelectedBuilder() const;

	/**
	 * What 1-9 place: the selected builder's buildings, else the faction's buildings while debug build is on
	 * (RTSCommands::IsDebugBuildEnabled) and no production building is selected, else nothing.
	 */
	TArray<URTSBuildingData*> GetBuildMenu() const;

	/** The selected building while it is the player's own and can produce, or null. Its units are the production menu. */
	ARTSBuilding* GetProductionBuilding() const;

	/** What 1-9 queue at the selected production building; empty without one. */
	TArray<URTSUnitData*> GetProductionMenu() const;

	/** The first of Data's prerequisites the local player does not own, or null. The server checks again before it queues. */
	const URTSEntityData* GetMissingPrerequisite(const URTSEntityData& Data) const;

	/**
	 * Building would take Data as far as the client knows: the player's own, Data on its list, prerequisites owned,
	 * affordable and the queue not full. The server checks again before it charges.
	 */
	bool CanRequestProduce(const ARTSBuilding* Building, const URTSUnitData* Data) const;

	/** Asks the server to queue Data at Building; false when CanRequestProduce refuses it locally. */
	bool RequestProduce(ARTSBuilding* Building, URTSUnitData* Data);

	/** Asks the server to take the last queued Data off Building's queue for a refund; false when none is queued there. */
	bool RequestCancelProduction(ARTSBuilding* Building, URTSUnitData* Data);

	/** Asks the server to move Building's rally point to Location; false when it is not an own production building. */
	bool RequestRallyPoint(ARTSBuilding* Building, const FVector& Location);

	/**
	 * Local. Shows a ghost of Data under the cursor until the player builds or cancels. Confirming sends the selected
	 * builder, or without one places the building directly in debug build.
	 */
	void BeginPlacement(URTSBuildingData* Data);
	void CancelPlacement();
	URTSBuildingData* GetPlacementData() const { return PlacementData; }

	/** Local. Arms attack-move for the selected units until it is sent or cancelled; false, changing nothing, without units. */
	bool BeginAttackMove();
	void CancelAttackMove();
	bool IsAttackMoveArmed() const { return bAttackMoveArmed; }

	/** Sends the selected units an attack-move to Location. bQueue queues it and keeps attack-move armed for the next waypoint. */
	void ConfirmAttackMove(const FVector& Location, bool bQueue);

	/**
	 * The only way orders reach the server. Units not owned by this player's team are ignored.
	 * A Build order without units is placed directly, after the server re-checks faction, spot and credits, but only in debug
	 * build; otherwise it is refused.
	 */
	UFUNCTION(Server, Reliable)
	void Server_IssueCommand(const TArray<ARTSUnit*>& Units, const FRTSCommand& Command);

protected:
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Pixels from the viewport edge where the camera starts to scroll. */
	UPROPERTY(EditAnywhere, Category = "RTS|Input")
	float EdgeScrollMargin = 12.f;

	/** A left-button press that moves less than this many pixels is a click, not a box. */
	UPROPERTY(EditAnywhere, Category = "RTS|Input")
	float DragThreshold = 6.f;

	UPROPERTY(EditAnywhere, Category = "RTS|Input")
	float KeyRotateSpeed = 90.f;

	/** Degrees of yaw per pixel of middle-mouse drag. */
	UPROPERTY(EditAnywhere, Category = "RTS|Input")
	float DragRotateSpeed = 0.3f;

private:
	void CreateInputActions();

	void OnPan(const FInputActionValue& Value);
	void OnZoom(const FInputActionValue& Value);
	void OnRotate(const FInputActionValue& Value);
	void OnRotateDragStarted();
	void OnRotateDragCompleted();
	void OnSelectStarted();
	void OnSelectCompleted();
	void OnMoveOrder();
	void OnStopOrder();
	void OnAttackMoveKey();
	void OnBuildHotkey(int32 Index);
	void OnCancel();
	void OnCancelConstruction();

	void ApplyEdgeScroll() const;
	void ApplyDragRotate();
	void SelectUnderCursor(bool bAdditive);
	void SelectInBox(const FVector2D& Start, const FVector2D& End, bool bAdditive);
	void IssueToSelection(const FRTSCommand& Command);
	void UpdatePlacement();
	void ConfirmPlacement();
	void SetAttackMoveArmed(bool bArmed);
	bool IsOwnUnit(const ARTSUnit* Unit) const;
	bool IsShiftDown() const;
	ARTSCameraPawn* GetCameraPawn() const;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> PanAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ZoomAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> RotateAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> RotateDragAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SelectAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MoveOrderAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> StopOrderAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> AttackMoveAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CancelAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CancelConstructionAction;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> BuildActions;

	TArray<TWeakObjectPtr<ARTSUnit>> SelectedUnits;
	TWeakObjectPtr<ARTSBuilding> SelectedBuilding;

	bool bSelecting = false;
	FVector2D SelectStart = FVector2D::ZeroVector;
	FVector2D SelectCurrent = FVector2D::ZeroVector;

	bool bRotateDragging = false;
	FVector2D LastRotateMouse = FVector2D::ZeroVector;

	/** Set while placing a building. */
	UPROPERTY(Transient)
	TObjectPtr<URTSBuildingData> PlacementData;

	UPROPERTY(Transient)
	TObjectPtr<ARTSPlacementGhost> PlacementGhost;

	/** The builder that gets the order; placement ends when it dies. Null while placing from the debug menu. */
	TWeakObjectPtr<ARTSUnit> PlacementBuilder;
	bool bPlacementDebug = false;

	FVector PlacementLocation = FVector::ZeroVector;
	bool bPlacementValid = false;

	/** Set by J until the next left click sends the attack-move, or right click or Esc cancels it. */
	bool bAttackMoveArmed = false;
};
