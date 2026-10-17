#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ObjectKey.h"
#include "Commands/RTSCommand.h"
#include "Core/RTSGameState.h"
#include "Player/RTSCommandButton.h"
#include "RTSPlayerController.generated.h"

class ARTSBuilding;
class ARTSCameraPawn;
class ARTSPlacementGhost;
class ARTSPlayerState;
class ARTSUnit;
class URTSBuildingData;
class URTSEntityData;
class URTSFactionData;
class URTSUnitData;
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

/** The selected units of one type, as the selection panel shows them together. */
USTRUCT()
struct FRTSSelectionGroup
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<const URTSUnitData> Data;

	TArray<TWeakObjectPtr<ARTSUnit>> Units;
};

DECLARE_MULTICAST_DELEGATE(FRTSSelectionChangedSignature);

/**
 * Turns mouse and keyboard into camera movement, a local selection and FRTSCommands for the server.
 *
 * Controls: WASD/arrows or screen edge pan, mouse wheel zoom, Q/E or middle-mouse drag rotate,
 * left click/drag select (Shift adds; a click on an own building selects that building), right click move, attack an
 * enemy unit or building, or harvest a supply source (Shift queues), X stop. J then left click attack-moves the selection to
 * the clicked spot (Shift queues and keeps attack-move armed); right click, Esc or an emptied selection cancels it, and the
 * HUD shows a hint while it is armed. Delete cancels the own
 * construction site under the cursor, or else the selected one, as does the selection panel's cancel button.
 * The command and upgrades panels show what the selection can do (GetCommandButtons); a click on a button, or its hotkey (the
 * entity's own key, else its position 1-9), runs it through ExecuteCommandButton.
 * With a builder selected, its buildings are the commands: one starts placing it, R turns it a quarter turn, left click sends the
 * builder (Shift queues it and keeps placing), right click or Esc cancels. The ghost turns red over sites own builders
 * were ordered to place and have not placed yet. A right click on an own construction site sends builders to resume it.
 * With rts.DebugBuild on and no builder selected, the faction's buildings are the commands and are placed without a builder,
 * turned and kept placing with Shift the same way.
 * With an own production building selected, its units and upgrades are the commands, right click sets the rally point,
 * and a click on a queued unit, or Delete for the last one, cancels it for a refund.
 * With an own superweapon selected that is ready, 1 or a click on the HUD's launch entry arms targeting: left click launches it
 * at the clicked spot, right click, Esc or a change of selection cancels it.
 * Space centres the camera on the latest attack warning, for up to AttackJumpMaxAge seconds after it.
 * Once the match is over the HUD shows Victory or Defeat, and on the host a restart button.
 * Esc with nothing to cancel opens the in-game menu: Resume, or Leave match for the main menu. While it is open the
 * match runs on, the camera keys still work and every other input is ignored.
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

	/**
	 * Broadcast on this client whenever the selection changes: a click, a box, a deselect, a group picked in the selection
	 * panel, or a selected unit or building that died or was cancelled (noticed on the next tick).
	 */
	FRTSSelectionChangedSignature OnSelectionChanged;

	/** The selected units grouped by type, in the order each type was first selected. */
	const TArray<FRTSSelectionGroup>& GetSelectionGroups() const { return SelectionGroups; }

	/** Units grouped by their unit data, in the order each type first appears; units that are gone or have no data are left out. */
	static TArray<FRTSSelectionGroup> GroupByType(const TArray<TWeakObjectPtr<ARTSUnit>>& Units);

	/** Narrows the selection to the units of GetSelectionGroups()[Index]; false, changing nothing, when there is no such group. */
	bool SelectGroup(int32 Index);

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

	/** The first selected unit that can build, or null. Its list is the build menu. */
	ARTSUnit* GetSelectedBuilder() const;

	/**
	 * What the commands place: the selected builder's buildings, else the faction's buildings while debug build is on
	 * (RTSCommands::IsDebugBuildEnabled) and no production building is selected, else nothing.
	 */
	TArray<URTSBuildingData*> GetBuildMenu() const;

	/** The selected building while it is the player's own and can produce, or null. Its units and upgrades are the production menu. */
	ARTSBuilding* GetProductionBuilding() const;

	/** What the commands queue at the selected production building: its units, then its upgrades; empty without one. */
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

	/**
	 * Asks the server to take Data off Building's queue for a refund: the entry at QueueIndex, or the last queued one when it is
	 * INDEX_NONE. False when that entry is not Data, or none is queued there.
	 */
	bool RequestCancelProduction(ARTSBuilding* Building, URTSEntityData* Data, int32 QueueIndex = INDEX_NONE);

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

	/** Local. Turns the ghost a quarter turn clockwise while placing (R). */
	void RotatePlacement();
	int32 GetPlacementQuarterTurns() const { return PlacementQuarterTurns; }

	/**
	 * Local. Moves the ghost to the spot under CursorLocation and checks it as the server will, including the sites own
	 * builders plan; the cursor drives it every tick.
	 */
	void UpdatePlacementAt(const FVector& CursorLocation);
	bool IsPlacementValid() const { return bPlacementValid; }

	/**
	 * Local. Sends the ghost's building when its spot is valid. bKeepPlacing (Shift) keeps placement on for the next one and
	 * queues a builder's order behind its others; otherwise placement ends.
	 */
	void ConfirmPlacement(bool bKeepPlacing);

	/**
	 * What the selection can do, in order: the production building's units and upgrades, else the build menu, then Stop and
	 * Attack-move while units are selected. Availability, its reason and the hotkeys are filled in; the panels only show them.
	 */
	TArray<FRTSCommandButton> GetCommandButtons() const;

	/**
	 * Local. Runs Button as a click on it does: queues its unit or upgrade, starts placing its building, stops the selection
	 * or arms attack-move. False, changing nothing, when the button is disabled or the in-game menu is open.
	 */
	bool ExecuteCommandButton(const FRTSCommandButton& Button);

	/**
	 * Local. A key press: 1 arms a selected superweapon, else the command button with Key as its hotkey runs. False when
	 * nothing took the key. Unit orders are not looked up here, their keys have their own input actions.
	 */
	bool HandleCommandKey(const FKey& Key);

	/** The keys the controller's own input actions use, which command buttons never get. */
	TSet<FKey> GetReservedKeys() const;

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
	 * Local. What a left-button press at Point, in viewport pixels, does: nothing on a HUD panel, a button of the canvas
	 * menus, confirms what is armed, else starts a selection there. The mouse input calls it with the cursor.
	 */
	void HandleSelectPressed(const FVector2D& Point);

	/** Local. Releasing the left button at Point ends the selection a press started: a click selects under the cursor, a drag the box. */
	void HandleSelectReleased(const FVector2D& Point);

	/** Point, in viewport pixels, lies on a panel of the UMG HUD, where clicks never select, order or place. */
	bool IsOnHUDPanel(const FVector2D& Point) const;

	/** Local. What Esc does: cancels placement, attack-move or superweapon targeting while one is armed, else opens or closes the in-game menu. */
	void HandleEscape();

	/** Local. Opening the in-game menu cancels placement, attack-move and superweapon targeting; nothing new can be armed while it is open. */
	void SetInGameMenuOpen(bool bOpen);
	bool IsInGameMenuOpen() const { return bInGameMenuOpen; }

	/**
	 * Local. Forgets the seat token and opens the main menu. A client's match runs on without it; the host's closes, and its
	 * clients land on the main menu with "The host left the match".
	 */
	void LeaveMatch();

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

	/**
	 * Server only. A move order Issuer gave cannot be carried out: no spot near Location is reachable, or a unit stuck at
	 * Location gave the move up (ARTSUnit::UpdateBlockedMove). Tells Issuer's controller through Client_OrderRefused, at
	 * most once per OrderRefusalCooldown, and once per OrderId (FRTSCommand::OrderId, 0 for none), so a refused group gives
	 * one message even when its units' paths are searched over more than the cooldown (#174).
	 * Nothing for an AI player or a null Issuer.
	 */
	static void NotifyOrderRefused(const ARTSPlayerState* Issuer, const FVector& Location, int32 OrderId = 0);

	static constexpr float OrderRefusalCooldown = 1.f;

	/** Refusals received, for tests; the HUD shows the last one for a few seconds. */
	int32 GetOrderRefusalCount() const { return ReceivedOrderRefusal.Count; }

	/** False before any refusal; else where the last refused order pointed and the seconds since it arrived. */
	bool GetLastOrderRefusal(FVector& OutLocation, float& OutAge) const;

	/** Owner only: a move order to Location was refused. */
	UFUNCTION(Client, Reliable)
	void Client_OrderRefused(const FVector& Location);

protected:
	virtual void SetupInputComponent() override;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;
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
	void OnCancel();
	void OnCancelConstruction();
	void OnJumpToAttack();
	void OnRotatePlacement();

	/** F5 outside shipping builds: rereads the UI layout files, as rts.UI.Reload does. */
	void OnReloadUI();

	void ApplyEdgeScroll() const;
	void ApplyDragRotate();
	void SelectUnderCursor(bool bAdditive);
	void SelectInBox(const FVector2D& Start, const FVector2D& End, bool bAdditive);
	void IssueToSelection(const FRTSCommand& Command);
	void UpdatePlacement();
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
	TObjectPtr<UInputAction> RotatePlacementAction;

	/** Mapped outside shipping builds only. */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ReloadUIAction;

	TArray<TWeakObjectPtr<ARTSUnit>> SelectedUnits;
	TWeakObjectPtr<ARTSBuilding> SelectedBuilding;

	/** Rebuilds SelectionGroups and broadcasts OnSelectionChanged when the selection differs from the one last broadcast. */
	void UpdateSelection();

	UPROPERTY(Transient)
	TArray<FRTSSelectionGroup> SelectionGroups;

	/** The selection as OnSelectionChanged last announced it. Keys, since two stale weak pointers compare equal. */
	TArray<FObjectKey> AnnouncedUnits;
	FObjectKey AnnouncedBuilding;

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

	/** 90-degree turns of the ghost, 0 to 3. */
	int32 PlacementQuarterTurns = 0;

	/** Set by J until the next left click sends the attack-move, or right click or Esc cancels it. */
	bool bAttackMoveArmed = false;

	/** Set by 1 or the HUD's launch entry with a ready superweapon selected, until the next left click launches it or it is cancelled. */
	bool bSuperweaponTargeting = false;

	/** Hotkey clashes already logged by GetCommandButtons. */
	mutable TSet<FString> LoggedHotkeyWarnings;

	/** Set by Esc with nothing to cancel, until Esc again or the menu's Resume. */
	bool bInGameMenuOpen = false;

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

	/** Server only. When the last refusal went out; within OrderRefusalCooldown of it further ones are held back. */
	TOptional<float> LastOrderRefusalSent;

	/** Server only. The OrderId of the last refusal told; that order's other units are not told again. */
	int32 LastOrderRefusalId = 0;

	struct FReceivedOrderRefusal
	{
		int32 Count = 0;
		FVector Location = FVector::ZeroVector;
		float Time = 0.f;
	};

	/** Owning client. */
	FReceivedOrderRefusal ReceivedOrderRefusal;
};
