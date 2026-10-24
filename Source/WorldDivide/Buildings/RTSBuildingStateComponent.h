#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RTSBuildingStateComponent.generated.h"

class ARTSBuilding;

/** What a building shows (#196), highest priority last: a building that is several of these at once shows the last. */
UENUM()
enum class ERTSBuildingState : uint8
{
	Idle,
	Producing,
	LowPower,
	Damaged,
	Critical,
	Destroyed,
};

/** What the presentation state is worked out from, all of it replicated to every machine. */
struct FRTSBuildingStateInputs
{
	bool bConstructed = true;
	float HealthFraction = 1.f;
	bool bLowPower = false;
	bool bProducing = false;
	/** URTSBuildingData::DamagedHealthFraction and CriticalHealthFraction. */
	float DamagedFraction = 0.5f;
	float CriticalFraction = 0.25f;
};

/** How a building looks in a state: its equipment motion scale, its lamps (CPD LampOn) and its lighting groups. */
struct FRTSBuildingLook
{
	float MotionScale = 1.f;
	bool bLampsOn = true;
	bool bWindowsOn = true;
	bool bFixturesOn = true;
	bool bAccentsOn = true;

	bool operator==(const FRTSBuildingLook& Other) const = default;
};

/**
 * How a mounted kit piece's lens and real light look in a state (BUILDING_KIT.md, States), as RTSKitLensData writes it: the
 * light shines at Level times its candelas and goes out with its lens's fault and flash phases (RTSKit::IsLensLit).
 */
struct FRTSKitLook
{
	/** 0 off, 1 full, EmergencyLevel on battery. */
	float Level = 1.f;
	/** Flicker phase, 0 for none. */
	float Fault = 0.f;
	/** Flash phase, 0 for steady. */
	float Flash = 0.f;
	/** On battery: a cooler, dimmer glow. */
	bool bBattery = false;

	bool operator==(const FRTSKitLook& Other) const = default;
};

struct FRTSKitPart;

namespace RTSBuildingState
{
	/**
	 * The state by priority: Destroyed (no health left) > Critical > Damaged (health at or below the fractions) > LowPower
	 * (the owner low on power, or not yet constructed and so not on the grid) > Producing (anything queued) > Idle.
	 */
	WORLDDIVIDE_API ERTSBuildingState Resolve(const FRTSBuildingStateInputs& Inputs);

	/**
	 * The look, each channel the most restrictive of every condition that holds, so a damaged building low on power is as
	 * dark and still as one only low on power: low power stops the equipment and puts out the lamps, windows, fixtures and
	 * accents; Damaged slows the equipment to 0.6 and puts out the accents; Critical slows it to 0.2 and puts out the
	 * fixtures too; Destroyed stops and puts out everything.
	 */
	WORLDDIVIDE_API FRTSBuildingLook GetLook(const FRTSBuildingStateInputs& Inputs);

	/** An Emergency light's level on battery. */
	constexpr float EmergencyLevel = 0.3f;

	/** What battery power does to a light's colour: cooler (BUILDING_KIT.md, States). */
	inline const FLinearColor BatteryTint(0.72f, 0.85f, 1.f);

	/** How many of a building's Flicker mounts flicker when it is damaged, the first in mount order. */
	constexpr int32 MaxFlickering = 2;

	/**
	 * Part's look, the most restrictive of every condition that holds as GetLook's: destroyed, everything is out. Low power
	 * (or Critical) puts the normal lights out but for a group still on, keeps Emergency ones at EmergencyLevel and LEDs
	 * (emergency pieces without a real light: the mast beacons) at full on battery, and flashes the warning lights, which are
	 * off otherwise. Damage flickers the first MaxFlickering Flicker mounts (FlickerRank, its place among them) and puts out
	 * the Off ones; Critical puts every Flicker mount out. A normal light follows its group (Windows, Fixtures, Accents).
	 */
	WORLDDIVIDE_API FRTSKitLook GetKitLook(const FRTSBuildingStateInputs& Inputs, const FRTSKitPart& Part, int32 FlickerRank);

	/** Inputs changed to show State, for rts.Debug.BuildingState. */
	WORLDDIVIDE_API FRTSBuildingStateInputs ForceState(FRTSBuildingStateInputs Inputs, ERTSBuildingState State);
}

DECLARE_MULTICAST_DELEGATE_TwoParams(FRTSBuildingStateChanged, ERTSBuildingState /*Old*/, ERTSBuildingState /*New*/);

/**
 * A building's presentation state (#196): worked out on every machine that draws from the replicated health, the owner's
 * power (ARTSBuilding::IsOwnerLowPower) and the production queue, and shown through the building's hooks: the equipment
 * motion scale (#351), the lamps (CPD LampOn: beacons and lit windows) and the lighting groups Windows, Fixtures and Accents
 * (#195), and each mounted kit piece's lens and light (FRTSKitLook, #391). It only reads; gameplay stays with the building's components. The garage keeps its door, beacons and GarageLamps.
 * No tick of its own: the building's tick drives it. A destroyed building leaves an ARTSBuildingRuin.
 */
UCLASS(ClassGroup = (RTS))
class WORLDDIVIDE_API URTSBuildingStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URTSBuildingStateComponent();

	/** Every machine that draws, each building tick: looks at the inputs every UpdateInterval seconds. */
	void Update(float DeltaSeconds);

	/** Works the state out now and shows it; with bForce writes every hook even if nothing changed, as after the parts are remade. */
	void Refresh(bool bForce = false);

	/** The inputs now, from the building. */
	FRTSBuildingStateInputs GatherInputs() const;

	ERTSBuildingState GetState() const { return State; }
	const FRTSBuildingLook& GetLook() const { return Look; }

#if !UE_BUILD_SHIPPING
	/** Shows this building in State whatever the game has (unset: as the game has it), for building shots (#442); rts.Debug.BuildingState outranks it. */
	void SetDebugState(TOptional<ERTSBuildingState> InState) { DebugState = InState; }
#endif

	/** Every machine that draws, when the state changes; for the VFX and audio (#197). */
	FRTSBuildingStateChanged OnStateChanged;

	/** Seconds between looks at the inputs. */
	static constexpr float UpdateInterval = 0.25f;

private:
	ARTSBuilding* GetBuilding() const;

	void ApplyLook() const;

	/** The kit pieces' looks for Inputs, in the building's GetKitParts order. */
	void GetKitLooks(const FRTSBuildingStateInputs& Inputs, TArray<FRTSKitLook>& OutLooks) const;

	ERTSBuildingState State = ERTSBuildingState::Idle;
	FRTSBuildingLook Look;
	/** The kit pieces' looks of the last refresh; kept so a refresh does not allocate. */
	TArray<FRTSKitLook> KitLooks;
	bool bApplied = false;
	float SinceUpdate = 0.f;
#if !UE_BUILD_SHIPPING
	TOptional<ERTSBuildingState> DebugState;
#endif
};
