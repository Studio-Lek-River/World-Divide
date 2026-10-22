#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RTSGarageComponent.generated.h"

class ARTSBuilding;
class ARTSUnit;
class ULocalLightComponent;
class UStaticMeshComponent;
class URTSUnitData;
struct FRealCurve;

/** Where a garage door is in its cycle. */
UENUM()
enum class ERTSGarageDoor : uint8
{
	Closed,
	Opening,
	Open,
	Closing,
};

/**
 * The door's state as the server last set it: the phase, and when it started from which openness, so every machine poses the
 * door alike from the server's clock without a replicated update per frame.
 */
USTRUCT()
struct FRTSGarageDoorState
{
	GENERATED_BODY()

	UPROPERTY()
	ERTSGarageDoor Phase = ERTSGarageDoor::Closed;

	/** Server world seconds (AGameStateBase::GetServerWorldTimeSeconds) the phase started. */
	UPROPERTY()
	double StartTime = 0.0;

	/** The door's openness, 0 closed to 1 open, when the phase started. */
	UPROPERTY()
	float StartOpenness = 0.f;
};

/**
 * The garage a building with one (URTSBuildingData::HasGarage, the USA HQ) sends its produced vehicles out of (#192).
 * URTSProductionComponent stays in charge of cost, queue and progress; the garage only presents what it completes:
 *  1. While the front entry is a vehicle due within the door's opening time, and the owner is not low on power, the warning
 *     lights come on and the door opens, so it is open when the vehicle completes. Low on power, it opens only once one did.
 *  2. Each completed vehicle waits in the garage until the door is open and the bay and its lane are clear of every unit,
 *     then appears at the garage's spawn socket facing out, one at a time.
 *  3. It drives straight out along the lane, through the bay that stays cut out of the navmesh (a scripted drive, not a path:
 *     URTSUnitMovement::SetCommandOverride), waiting while a unit stands on the lane ahead of it; the owner's idle units there
 *     are sent off the lane. Once its hull is where a building without a garage puts the unit, it goes to the rally point.
 *  4. With nothing more to send out and the bay and lane clear for a moment, the door closes; a vehicle due again opens it
 *     again, even halfway down. It never closes on a unit.
 * A destroyed building sends nothing more out; a vehicle on its way out goes on to the rally point.
 * Every building has one; it stays inert unless the data has a garage. The server runs it from the building's tick; the
 * door's state replicates, and every machine but a dedicated server poses the door and flashes the lights from it.
 */
UCLASS(ClassGroup = (RTS))
class WORLDDIVIDE_API URTSGarageComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URTSGarageComponent();

	/** The building's data has a garage. */
	bool HasGarage() const { return bHasGarage; }

	/** A vehicle of Data leaves through the garage: the building has one and Data drives on running gear, not on legs or wings. */
	bool Accepts(const URTSUnitData& Data) const;

	/** Server only. Data completed: the garage sends a unit of it out once the door is open and the lane is clear. */
	void Enqueue(URTSUnitData& Data);

	/** Server only, each tick once the building is constructed: the door, the waiting vehicles and the one driving out. */
	void UpdateGarage(float DeltaSeconds);

	/** Server only. The building goes: nothing more comes out, and a vehicle on its way out goes on to the rally point. */
	void Abandon();

	/** Reads the garage and the door's motion from the building's data, on every machine; called when the data arrives. */
	void ApplyBuildingData();

	/** The door's openness now, 0 closed to 1 open, from the replicated state and the server's clock. */
	float GetOpenness() const;

	ERTSGarageDoor GetDoorPhase() const { return Door.Phase; }

	/** Completed vehicles still waiting to come out. */
	int32 GetPendingCount() const { return Pending.Num(); }

	/** Server only. The vehicle driving out of the garage now, or null. */
	ARTSUnit* GetExitingUnit() const { return Exiting.Get(); }

	/** Server only. True while a unit's hull reaches into the bay or onto its lane outside, Ignore aside. */
	bool IsLaneOccupied(const ARTSUnit* Ignore = nullptr) const;

	/** Where a vehicle leaving the garage appears, in world space, and its yaw: the spawn socket, facing out of the door. */
	FTransform GetSpawnTransform() const;

	/** The warning lights are on: the door is not closed. */
	bool AreLightsOn() const { return Door.Phase != ERTSGarageDoor::Closed; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	ARTSBuilding* GetBuilding() const;

	/** The server's world seconds, on every machine. */
	double GetServerTime() const;

	/** Server only. Starts Phase from the openness now. */
	void SetPhase(ERTSGarageDoor Phase);

	/** Server only. The front of the queue is a vehicle the garage sends out, due within the door's opening time at full power. */
	bool IsVehicleDueSoon() const;

	/** Server only. Spawns Data at the spawn socket and starts it driving out. */
	ARTSUnit* SpawnExiting(URTSUnitData& Data);

	/** Server only. Drives the exiting vehicle on, holds it while its lane is blocked, and sends it on once it is out. */
	void UpdateExiting();

	/** Server only. Sends the owner's idle units in Box, part of the lane, on past it, so they never hold a vehicle in the garage. */
	void ClearLane(const ARTSUnit* Ignore, const FBox2D& Box);

	/** Whether Unit's hull reaches into Box, in the building's frame on the ground. */
	bool Overlaps(const ARTSUnit& Unit, const FBox2D& Box) const;

	UFUNCTION()
	void OnRep_Door();

	/** Poses the door at Openness and flashes the lights; nothing on a dedicated server. */
	void UpdateVisuals();

	void PoseDoor(float Openness);

	UPROPERTY(ReplicatedUsing = OnRep_Door)
	FRTSGarageDoorState Door;

	/** Server only. Completed vehicles, one entry each, in the order they completed. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<URTSUnitData>> Pending;

	TWeakObjectPtr<ARTSUnit> Exiting;

	/** Server only. Seconds the door has stood open with nothing to send out and its lane clear. */
	float IdleOpenSeconds = 0.f;

	bool bHasGarage = false;

	/** In the building's frame, scaled: the spawn socket and the lane's middle along Y. */
	FVector SpawnLocal = FVector::ZeroVector;

	/** The bay and the lane on from it to the end of the exit lane, in the building's frame on the ground. */
	FBox2D LaneBox = FBox2D(ForceInit);

	/** One door section, seal, cable or shaft the door's motion moves, and the curves that move it. */
	struct FDoorPart
	{
		TWeakObjectPtr<UStaticMeshComponent> Component;
		/** The instance in an instanced Component, INDEX_NONE for a part drawn on its own. */
		int32 Instance = INDEX_NONE;
		FTransform Rest;
		const FRealCurve* X = nullptr;
		const FRealCurve* Z = nullptr;
		const FRealCurve* Pitch = nullptr;
		/** Scales the part along its Z, for the lift cables. */
		const FRealCurve* Length = nullptr;
	};
	TArray<FDoorPart> DoorParts;

	/** The door was last posed at this openness; below zero before it was posed at all. */
	float PosedOpenness = -1.f;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ULocalLightComponent>> Lights;
};
