#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Commands/RTSCommand.h"
#include "RTSPlayerController.generated.h"

class ARTSCameraPawn;
class ARTSPlacementGhost;
class ARTSUnit;
class URTSBuildingData;
class URTSFactionData;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 * Turns mouse and keyboard into camera movement, a local selection and FRTSCommands for the server.
 *
 * Controls: WASD/arrows or screen edge pan, mouse wheel zoom, Q/E or middle-mouse drag rotate,
 * left click/drag select (Shift adds), right click move or attack an enemy (Shift queues), X stop.
 * Debug build menu: 1-9 pick a faction building to place, left click builds, right click or Esc cancels.
 * Placement mode stays once builder units exist; only the hotkey menu that starts it is a stand-in.
 */
UCLASS()
class WORLDDIVIDE_API ARTSPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ARTSPlayerController();

	const TArray<TWeakObjectPtr<ARTSUnit>>& GetSelectedUnits() const { return SelectedUnits; }

	/** True while the player drags a selection box; outputs its corners in viewport pixels. */
	bool GetSelectionBox(FVector2D& OutStart, FVector2D& OutEnd) const;

	int32 GetTeamId() const;

	/** The local player's faction, or null before the player state has replicated. */
	URTSFactionData* GetFaction() const;

	/** Number of faction buildings the debug build menu reaches through the 1-9 keys. */
	static constexpr int32 MaxBuildHotkeys = 9;

	/** Local. Shows a ghost of Data under the cursor until the player builds or cancels. */
	void BeginPlacement(URTSBuildingData* Data);
	void CancelPlacement();
	URTSBuildingData* GetPlacementData() const { return PlacementData; }

	/**
	 * The only way orders reach the server. Units not owned by this player's team are ignored.
	 * A Build order is placed directly for now, without units, after the server re-checks faction and spot.
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
	void OnBuildHotkey(int32 Index);
	void OnCancel();

	void ApplyEdgeScroll() const;
	void ApplyDragRotate();
	void SelectUnderCursor(bool bAdditive);
	void SelectInBox(const FVector2D& Start, const FVector2D& End, bool bAdditive);
	void IssueToSelection(const FRTSCommand& Command);
	void UpdatePlacement();
	void ConfirmPlacement();
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
	TObjectPtr<UInputAction> CancelAction;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> BuildActions;

	TArray<TWeakObjectPtr<ARTSUnit>> SelectedUnits;

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

	FVector PlacementLocation = FVector::ZeroVector;
	bool bPlacementValid = false;
};
