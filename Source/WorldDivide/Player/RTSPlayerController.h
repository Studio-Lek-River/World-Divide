#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Commands/RTSCommand.h"
#include "Core/RTSGameState.h"
#include "RTSPlayerController.generated.h"

class ARTSBuilding;
class ARTSCameraPawn;
class ARTSPlacementGhost;
class ARTSUnit;
class URTSBuildingData;
class URTSEntityData;
class URTSFactionData;
class URTSUpgradeData;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/** What an attack warning is about: the player's buildings, or their units in the field. */
UENUM()
enum class ERTSAttackAlert : uint8
{
	Base,
	Units,
};

/** What a superweapon warning is about: a superweapon completed, or one charged and ready to launch. */
UENUM()
enum class ERTSSuperweaponAlert : uint8
{
	Built,
	Ready,
};

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
 * With an own superweapon selected that is ready, 1 or a click on the HUD's launch entry arms targeting: left click launches it
 * at the clicked spot, right click, Esc or a change of selection cancels it.
 * Space centres the camera on the latest attack warning, for up to AttackJumpMaxAge seconds after it.
 * Once the match is over the HUD shows Victory or Defeat, and on the host a restart button.
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

	/** The unit of any team under the mouse cursor, or null. */
	ARTSUnit* GetUnitUnderCursor() const;

	/** The unit of any team first hit by the ray from Origin along the unit vector Direction, as the cursor casts it, or null. */
	ARTSUnit* GetUnitAlongRay(const FVector& Origin, const FVector& Direction) const;

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

	/** The selected building while it is the player's own and can produce, or null. Its units and upgrades are the production menu. */
	ARTSBuilding* GetProductionBuilding() const;

	/** What 1-9 queue at the selected production building: its units, then its upgrades; empty without one. */
	TArray<URTSEntityData*> GetProductionMenu() const;

	/** The local player has researched Upgrade. */
	bool HasUpgrade(const URTSUpgradeData& Upgrade) const;

	/** The first of Data's prerequisites the local player does not own, or null. The server checks again before it charges. */
	const URTSEntityData* GetMissingPrerequisite(const URTSEntityData& Data) const;

	/** The local player owns Data's MaxCount of it, construction sites included. The server checks again before it charges. */
	bool IsAtBuildLimit(const URTSBuildingData& Data) const;

	/**
	 * Building would take Data as far as the client knows: the player's own, Data on its list, prerequisites owned,
	 * affordable and the queue not full, and an upgrade neither researched nor queued. The server checks again before it charges.
	 */
	bool CanRequestProduce(const ARTSBuilding* Building, const URTSEntityData* Data) const;

	/** Asks the server to queue Data, a unit or an upgrade, at Building; false when CanRequestProduce refuses it locally. */
	bool RequestProduce(ARTSBuilding* Building, URTSEntityData* Data);

	/** Asks the server to take the last queued Data off Building's queue for a refund; false when none is queued there. */
	bool RequestCancelProduction(ARTSBuilding* Building, URTSEntityData* Data);

	/** Asks the server to move Building's rally point to Location; false when it is not an own production building. */
	bool RequestRallyPoint(ARTSBuilding* Building, const FVector& Location);

	/**
	 * Local. Shows a ghost of Data under the cursor until the player builds or cancels. Confirming sends the selected
	 * builder, or without one places the building directly in debug build. Does nothing while a prerequisite is missing
	 * or the player is at Data's limit.
	 */
	void BeginPlacement(URTSBuildingData* Data);
	void CancelPlacement();
	URTSBuildingData* GetPlacementData() const { return PlacementData; }

	/** The selected building while it is the player's own constructed, standing superweapon, or null. */
	ARTSBuilding* GetSelectedSuperweapon() const;

	/** Building is the player's own superweapon and ready to launch as far as the client knows. The server checks again. */
	bool CanRequestLaunch(const ARTSBuilding* Building) const;

	/** Asks the server to launch Building at Location; false when CanRequestLaunch refuses it locally. */
	bool RequestLaunchSuperweapon(ARTSBuilding* Building, const FVector& Location);

	/**
	 * Local. Arms targeting for the selected superweapon until the next left click launches it, or it is cancelled; false,
	 * changing nothing, unless CanRequestLaunch accepts the selected superweapon.
	 */
	bool BeginSuperweaponTargeting();
	void CancelSuperweaponTargeting();
	bool IsSuperweaponTargeting() const { return bSuperweaponTargeting; }

	/** Local. Arms attack-move for the selected units until it is sent or cancelled; false, changing nothing, without units. */
	bool BeginAttackMove();
	void CancelAttackMove();
	bool IsAttackMoveArmed() const { return bAttackMoveArmed; }

	/** Sends the selected units an attack-move to Location. bQueue queues it and keeps attack-move armed for the next waypoint. */
	void ConfirmAttackMove(const FVector& Location, bool bQueue);

	/** The local player's team's result, InProgress before the game state has replicated. */
	ERTSMatchResult GetMatchResult() const;

	/** The match is over and this is the host's local controller, which restarts for every player. */
	bool CanRestart() const;

	/** Restarts the match on the same map, taking any clients along; false, changing nothing, when CanRestart refuses. */
	bool RequestRestart();

	/**
	 * The only way orders reach the server. Every order is refused once the match is over. Units not owned by this player's team are ignored.
	 * A Build order without units is placed directly, after the server re-checks faction, spot and credits, but only in debug
	 * build; otherwise it is refused.
	 */
	UFUNCTION(Server, Reliable)
	void Server_IssueCommand(const TArray<ARTSUnit*>& Units, const FRTSCommand& Command);

	/**
	 * Server only. Victim, on TeamId, was hit: unless DamageInstigator is on TeamId too, every player controller of
	 * TeamId gets NotifyAttacked. A null instigator, a shooter that died while its shell flew, counts as the enemy.
	 */
	static void NotifyTeamAttacked(const AActor& Victim, int32 TeamId, AActor* DamageInstigator, ERTSAttackAlert Kind);

	/**
	 * Server only. Something of the player's at Location was hit by the enemy. Warns the player through
	 * Client_UnderAttack, unless a warning of the same kind for a spot near Location went out within its cooldown.
	 */
	void NotifyAttacked(const FVector& Location, ERTSAttackAlert Kind);

	/** Warnings of Kind received, for tests; the HUD shows the last ones for a few seconds. */
	int32 GetAttackAlertCount(ERTSAttackAlert Kind) const { return ReceivedAttackAlerts[static_cast<int32>(Kind)].Count; }

	/** False before any warning of Kind; else where the last one points and the seconds since it arrived. */
	bool GetLastAttackAlert(ERTSAttackAlert Kind, FVector& OutLocation, float& OutAge) const;

	/** As GetLastAttackAlert, for the newest warning of either kind. */
	bool GetLatestAttackAlert(FVector& OutLocation, float& OutAge) const;

	/** Seconds after which Space no longer jumps to a warning. */
	static constexpr float AttackJumpMaxAge = 20.f;

	/** Local. Centres the camera on the newest warning; false, changing nothing, without one or once it is too old. */
	bool JumpToLatestAttack();

	/** Alert sounds played for warnings of Kind, for tests; worlds without audio count them but stay silent. */
	int32 GetAlertSoundCount(ERTSAttackAlert Kind) const { return ReceivedAttackAlerts[static_cast<int32>(Kind)].SoundCount; }

	/** Owner only: where the attacked building or unit stood, for the HUD's message and marker. */
	UFUNCTION(Client, Reliable)
	void Client_UnderAttack(const FVector& Location, ERTSAttackAlert Kind);

	/**
	 * Server only. Superweapon was built or is ready: every player controller of another team gets the warning, and for Ready
	 * the superweapon's own players too.
	 */
	static void NotifySuperweapon(const ARTSBuilding& Superweapon, ERTSSuperweaponAlert Kind);

	/** Superweapon warnings of Kind received, for tests; the HUD shows the last one for a few seconds. */
	int32 GetSuperweaponAlertCount(ERTSSuperweaponAlert Kind) const { return ReceivedSuperweaponAlerts[static_cast<int32>(Kind)].Count; }

	/** False before any superweapon warning; else the newest one's kind, whether it is about the player's own superweapon, and the seconds since it arrived. */
	bool GetLatestSuperweaponAlert(ERTSSuperweaponAlert& OutKind, bool& bOutOwn, float& OutAge) const;

	/** Owner only: a superweapon, the player's own when bOwn, was built or is ready. */
	UFUNCTION(Client, Reliable)
	void Client_SuperweaponAlert(ERTSSuperweaponAlert Kind, bool bOwn);

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
	void OnJumpToAttack();

	void ApplyEdgeScroll() const;
	void ApplyDragRotate();
	void SelectUnderCursor(bool bAdditive);
	void SelectInBox(const FVector2D& Start, const FVector2D& End, bool bAdditive);
	void IssueToSelection(const FRTSCommand& Command);
	void UpdatePlacement();
	void ConfirmPlacement();
	void SetAttackMoveArmed(bool bArmed);
	void SetSuperweaponTargeting(bool bTargeting);
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
	TObjectPtr<UInputAction> JumpToAttackAction;

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

	/** Set by 1 or the HUD's launch entry with a ready superweapon selected, until the next left click launches it or it is cancelled. */
	bool bSuperweaponTargeting = false;

	struct FSentAttackAlert
	{
		FVector Location;
		float Time;
		ERTSAttackAlert Kind;
	};

	/** Server only. Warnings sent within their cooldown, which hold back further ones of the same kind near the same spot. */
	TArray<FSentAttackAlert> SentAttackAlerts;

	struct FReceivedAttackAlert
	{
		int32 Count = 0;
		int32 SoundCount = 0;
		FVector Location = FVector::ZeroVector;
		float Time = 0.f;
	};

	/** Owning client. Indexed by ERTSAttackAlert. */
	FReceivedAttackAlert ReceivedAttackAlerts[2];

	struct FReceivedSuperweaponAlert
	{
		int32 Count = 0;
		bool bOwn = false;
		float Time = 0.f;
	};

	/** Owning client. Indexed by ERTSSuperweaponAlert. */
	FReceivedSuperweaponAlert ReceivedSuperweaponAlerts[2];
};
