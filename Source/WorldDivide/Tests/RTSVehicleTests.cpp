#include "Algo/Count.h"
#include "Combat/RTSWeaponComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSWeaponData.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstance.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSPrimitiveData.h"
#include "Units/RTSRunningGearSubsystem.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitMovement.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Vehicles built from parts: a gun that elevates on the turret, add-ons on the hull, turret and gun, and road wheels that
 * roll with the ground. The parts are engine shapes; the real M20 Guardian is checked against its export in RTSArtTests.
 */
namespace
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	const FVector TurretOffset(0.f, 0.f, 100.f);
	const FVector GunOffset(50.f, 0.f, 20.f);
	const FVector GunMuzzleOffset(300.f, 0.f, 0.f);
	const FVector StationOffset(-20.f, 40.f, 60.f);
	const FVector SightOffset(100.f, 0.f, 10.f);
	const FVector LeftWheelOffset(100.f, -150.f, 50.f);
	const FVector RightWheelOffset(100.f, 150.f, 50.f);

	/** Parts in the order MakeVehicleData lists them. */
	enum EPart : int32 { Station, Sight, LeftWheel, RightWheel, LeftRearWheel };

	UStaticMesh* LoadShape(const TCHAR* Name)
	{
		return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name));
	}

	URTSUnitData* MakeVehicleData(URTSWeaponData* Weapon = nullptr)
	{
		URTSUnitData* Data = NewObject<URTSUnitData>();
		Data->MaxHealth = 100.f;
		Data->Weapon = Weapon;
		Data->SelectionRadius = 200.f;
		Data->Mesh = LoadShape(TEXT("Cube"));
		Data->TurretMesh = LoadShape(TEXT("Cube"));
		Data->TurretOffset = TurretOffset;
		Data->TurretTurnRate = 90.f;
		Data->GunMesh = LoadShape(TEXT("Cylinder"));
		Data->GunOffset = GunOffset;
		Data->GunMuzzleOffset = GunMuzzleOffset;
		Data->GunMinPitch = -10.f;
		Data->GunMaxPitch = 20.f;
		Data->GunElevationRate = 30.f;

		auto AddPart = [Data](ERTSVehiclePartMount Mount, const FVector& Offset, const FRotator& Rotation, bool bWheel)
		{
			FRTSVehiclePart& Part = Data->Parts.AddDefaulted_GetRef();
			Part.Mesh = LoadShape(bWheel ? TEXT("Cylinder") : TEXT("Cube"));
			Part.MountedOn = Mount;
			Part.Offset = Offset;
			Part.Rotation = Rotation;
			Part.bWheel = bWheel;
		};
		AddPart(ERTSVehiclePartMount::Turret, StationOffset, FRotator::ZeroRotator, false);
		AddPart(ERTSVehiclePartMount::Gun, SightOffset, FRotator::ZeroRotator, false);
		// The right wheel is the left one turned round, as the M20 exports a single wheel.
		AddPart(ERTSVehiclePartMount::Hull, LeftWheelOffset, FRotator::ZeroRotator, true);
		AddPart(ERTSVehiclePartMount::Hull, RightWheelOffset, FRotator(0.f, 180.f, 0.f), true);
		AddPart(ERTSVehiclePartMount::Hull, LeftWheelOffset - FVector(200.f, 0.f, 0.f), FRotator::ZeroRotator, true);
		return Data;
	}

	URTSWeaponData* MakeWeapon()
	{
		URTSWeaponData* Weapon = NewObject<URTSWeaponData>();
		Weapon->Range = 3000.f;
		Weapon->Damage = 1.f;
		Weapon->ReloadSeconds = 100.f;
		return Weapon;
	}

	FTransform GetPart(const ARTSUnit& Unit, int32 Part)
	{
		FTransform Transform;
		Unit.GetPartTransform(Part, Transform);
		return Transform;
	}

	/** Where the wheel's top, straight up at rest in the hull's frame, points now. */
	FVector GetWheelTop(const ARTSUnit& Unit, int32 Part)
	{
		const FTransform Transform = GetPart(Unit, Part);
		const FQuat Rest = Part == RightWheel ? FRotator(0.f, 180.f, 0.f).Quaternion() : FQuat::Identity;
		return Transform.GetRotation() * Rest.Inverse() * FVector::UpVector;
	}

	/** Drives the unit by Distance along its heading, then turns it by Yaw, and lets the running gear catch up. */
	void Drive(ARTSUnit& Unit, float Distance, float Yaw = 0.f)
	{
		Unit.SetActorLocation(Unit.GetActorLocation() + Unit.GetActorForwardVector() * Distance);
		Unit.SetActorRotation(Unit.GetActorRotation() + FRotator(0.f, Yaw, 0.f));
		Unit.UpdateRunningGear();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSVehiclePartsTest, "WorldDivide.Units.VehicleParts.Attach", TestFlags)

bool FRTSVehiclePartsTest::RunTest(const FString& Parameters)
{
	const FRTSTestWorld TestWorld;
	ARTSUnit* Unit = TestWorld.SpawnUnit(MakeVehicleData(), 0, FVector::ZeroVector);
	TestTrue(TEXT("The data gives the unit a gun"), Unit->HasGun());

	FTransform Unused;
	for (int32 Part = Station; Part <= LeftRearWheel; ++Part)
	{
		TestTrue(*FString::Printf(TEXT("Part %d is placed"), Part), Unit->GetPartTransform(Part, Unused));
	}
	TestFalse(TEXT("No part past the data's"), Unit->GetPartTransform(LeftRearWheel + 1, Unused));

	TArray<UStaticMeshComponent*> Meshes;
	Unit->GetComponents(Meshes);
	// Hull, turret, gun, station and sight, and one instanced component for the three wheels, which share a mesh.
	TestEqual(TEXT("Wheels of one mesh share a component"), Meshes.Num(), 6);
	for (const UStaticMeshComponent* Mesh : Meshes)
	{
		TestFalse(*FString::Printf(TEXT("%s does not collide"), *Mesh->GetName()), Mesh->IsCollisionEnabled());
	}

	// The wheels hang off the hull at their offsets; the unit stands unturned at the origin.
	const FVector LeftWheelBefore = GetPart(*Unit, LeftWheel).GetLocation();
	TestTrue(TEXT("The wheels sit at their offsets on the hull"),
		(GetPart(*Unit, LeftRearWheel).GetLocation() - LeftWheelBefore).Equals(FVector(-200.f, 0.f, 0.f), 0.1f)
		&& (GetPart(*Unit, RightWheel).GetLocation() - LeftWheelBefore).Equals(FVector(0.f, 300.f, 0.f), 0.1f)
		&& FMath::IsNearlyEqual(LeftWheelBefore.Y, LeftWheelOffset.Y, 0.1f));
	const FVector StationBefore = GetPart(*Unit, Station).GetLocation();
	const FVector SightBefore = GetPart(*Unit, Sight).GetLocation();
	const FVector MuzzleBefore = Unit->GetMuzzleLocation();
	TestTrue(TEXT("The muzzle is on the gun, ahead of the turret"), MuzzleBefore.X > GunOffset.X + GunMuzzleOffset.X * 0.9f);

	// A quarter turn of the turret and a raised gun.
	Unit->SetAimOverride(FVector2D(90.f, 20.f));
	for (int32 Frame = 0; Frame < 40; ++Frame)
	{
		Unit->UpdateTurret(0.05f);
	}
	TestTrue(FString::Printf(TEXT("The turret turned (%.1f)"), Unit->GetTurretYaw()), FMath::IsNearlyEqual(Unit->GetTurretYaw(), 90.f, 0.1f));
	TestTrue(FString::Printf(TEXT("The gun rose (%.1f)"), Unit->GetGunPitch()), FMath::IsNearlyEqual(Unit->GetGunPitch(), 20.f, 0.1f));

	// A quarter turn right takes the station from the turret's right (+Y) round to its back (-X).
	const FVector StationAfter = GetPart(*Unit, Station).GetLocation();
	TestTrue(TEXT("The station turned with the turret"), StationBefore.Y > 0.f && StationAfter.X < StationBefore.X - 20.f
		&& FMath::IsNearlyEqual(StationAfter.Z, StationBefore.Z, 0.1f));
	TestTrue(TEXT("The sight on the gun turned with the turret and rose with the gun"),
		GetPart(*Unit, Sight).GetLocation().Y > SightBefore.Y + 50.f && GetPart(*Unit, Sight).GetLocation().Z > SightBefore.Z);
	const FVector Muzzle = Unit->GetMuzzleLocation();
	TestTrue(TEXT("The muzzle points out to the right and up"), Muzzle.Y > 250.f && Muzzle.Z > MuzzleBefore.Z + 50.f);
	TestTrue(TEXT("The wheels stayed on the hull"), GetPart(*Unit, LeftWheel).GetLocation().Equals(LeftWheelBefore, 0.1f));

	Unit->SetAimOverride({});
	Unit->UpdateTurret(100.f);
	TestTrue(TEXT("Without the override and a target the turret and gun come back to the front"),
		FMath::IsNearlyZero(Unit->GetTurretYaw()) && FMath::IsNearlyZero(Unit->GetGunPitch()));
	return true;
}

namespace
{
	/** A 7 x 3 m hull of engine cube, tracked like the M20, with its tracks and a sprocket each side (#151). */
	constexpr float TrackedGauge = 264.f;
	constexpr float TrackedPitch = 16.f;
	constexpr float SprocketRadius = 36.f;
	const FVector TrackedScale(7.f, 3.f, 2.f);

	/** Parts in the order MakeTrackedData lists them. */
	enum ETrackedPart : int32 { LeftTrack, RightTrack, LeftSprocket, RightSprocket };

	/** #151's acceptance: the phase within 1 % of a link of the travel, the wheel within 1 degree, a stopped phase still. */
	constexpr float PhaseTolerance = 0.01f;
	constexpr float WheelTolerance = 1.f;
	constexpr float StillPhase = 0.001f;
	constexpr double GearRate = 60.0;
	/** The M20's loop, in links: what a blocked track's drift is measured against. */
	constexpr float LoopLinks = 85.f;
	/** Below half a link a frame, so the links' steps are told apart from their wrap. */
	constexpr float CruiseSpeed = 300.f;

	/** A solid engine cube filling Box, as RTSVehicleMovementTests spawns them. */
	AActor* SpawnTestSolid(UWorld& World, const FBox& Box)
	{
		const FTransform Transform(FRotator::ZeroRotator, Box.GetCenter(), Box.GetSize() / 100.f);
		AStaticMeshActor* Actor = World.SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform);
		UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(LoadShape(TEXT("Cube")));
		Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Component->SetCanEverAffectNavigation(false);
		Actor->FinishSpawning(Transform);
		return Actor;
	}

	URTSUnitData* MakeTrackedData()
	{
		URTSUnitData* Data = NewObject<URTSUnitData>();
		Data->MaxHealth = 100.f;
		Data->Mesh = LoadShape(TEXT("Cube"));
		Data->MeshScale = TrackedScale;
		Data->MoveSpeed = 1250.f;
		Data->TurnRate = 100.f;
		FRTSLocomotionProfile& Profile = Data->Locomotion;
		Profile.Type = ERTSLocomotionType::Tracked;
		Profile.ReverseSpeed = 400.f;
		Profile.Acceleration = 400.f;
		Profile.Braking = 1000.f;
		Profile.TrackGauge = TrackedGauge;
		// Lengths in the mesh's space: the hull's scale makes them world cm.
		for (const float Side : { -1.f, 1.f })
		{
			FRTSVehiclePart& Track = Data->Parts.AddDefaulted_GetRef();
			Track.Mesh = LoadShape(TEXT("Cube"));
			Track.Offset = FVector(0.f, Side * 0.5f * TrackedGauge / TrackedScale.Y, 0.f);
			Track.TrackPitch = TrackedPitch / TrackedScale.X;
			Track.RigidMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		}
		for (const float Side : { -1.f, 1.f })
		{
			FRTSVehiclePart& Sprocket = Data->Parts.AddDefaulted_GetRef();
			Sprocket.Mesh = LoadShape(TEXT("Cylinder"));
			Sprocket.Offset = FVector(-40.f, Side * 0.5f * TrackedGauge / TrackedScale.Y, 0.f);
			Sprocket.Rotation = FRotator(0.f, Side > 0.f ? 180.f : 0.f, 0.f);
			Sprocket.bWheel = true;
			Sprocket.RollRadius = SprocketRadius / TrackedScale.Z;
		}
		return Data;
	}

	/** Runs the unit's gear on Command for Seconds at GearRate, the running gear after every update as the frame does it. */
	void RunGear(ARTSUnit& Unit, const RTSLocomotion::FCommand& Command, double Seconds, TFunctionRef<void()> Each = [] {},
		const FRTSTrackViews* Views = nullptr)
	{
		Unit.GetUnitMovement()->SetCommandOverride(Command);
		const int32 Steps = FMath::RoundToInt32(Seconds * GearRate);
		for (int32 Index = 0; Index < Steps; ++Index)
		{
			Unit.GetWorld()->TimeSeconds += 1.0 / GearRate;
			Unit.GetUnitMovement()->TickComponent(1.f / GearRate, LEVELTICK_All, nullptr);
			Unit.UpdateRunningGear(Views);
			Each();
		}
	}

	/** Phase A less phase B, wrapped to [-0.5, 0.5) of a link. */
	float PhaseDelta(float A, float B)
	{
		return FMath::Frac(A - B + 0.5f) - 0.5f;
	}

	/** Radians A less B, wrapped to [-pi, pi). */
	float AngleDelta(float A, float B)
	{
		return FMath::UnwindRadians(A - B);
	}

	/** What each side's track and sprocket stood at, and the realized travel then, to check them against later. */
	struct FGearMark
	{
		float Phase[2] = { 0.f, 0.f };
		float Angle[2] = { 0.f, 0.f };
		double Travel[2] = { 0.0, 0.0 };

		explicit FGearMark(const ARTSUnit& Unit)
		{
			const FRTSRealizedMotion& Motion = Unit.GetRealizedMotion();
			Travel[0] = Motion.LeftTravel;
			Travel[1] = Motion.RightTravel;
			for (int32 Side = 0; Side < 2; ++Side)
			{
				Unit.GetTrackPhase(LeftTrack + Side, Phase[Side]);
				Unit.GetWheelAngle(LeftSprocket + Side, Angle[Side]);
			}
		}
	};

	/**
	 * Since Mark: each track's phase moved by its side's realized travel over the pitch, backward driving forward (the
	 * bottom of the loop runs back under the hull), and each sprocket turned by that travel over its radius; true when both
	 * hold. Returns each side's travel.
	 */
	bool FollowsTravel(FAutomationTestBase& Test, const TCHAR* What, const ARTSUnit& Unit, const FGearMark& Mark, double OutTravel[2])
	{
		const FGearMark Now(Unit);
		bool bFollows = true;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			OutTravel[Side] = Now.Travel[Side] - Mark.Travel[Side];
			const float Expected = FMath::Frac(static_cast<float>(-OutTravel[Side] / TrackedPitch));
			const float PhaseError = FMath::Abs(PhaseDelta(PhaseDelta(Now.Phase[Side], Mark.Phase[Side]), Expected));
			const float WheelError = FMath::RadiansToDegrees(FMath::Abs(AngleDelta(AngleDelta(Now.Angle[Side], Mark.Angle[Side]),
				static_cast<float>(FMath::Fmod(OutTravel[Side] / SprocketRadius, UE_DOUBLE_TWO_PI)))));
			bFollows &= Test.TestTrue(FString::Printf(TEXT("%s: the %s track's phase follows its %.1f cm of travel (off by %.4f of a link)"),
				What, Side == 0 ? TEXT("left") : TEXT("right"), OutTravel[Side], PhaseError), PhaseError <= PhaseTolerance);
			bFollows &= Test.TestTrue(FString::Printf(TEXT("%s: and its sprocket turns with it (off by %.3f degrees)"), What, WheelError),
				WheelError <= WheelTolerance);
		}
		return bFollows;
	}

	/** The largest change of either track's phase from one frame to the next while Run goes, in links. */
	float LargestStep(ARTSUnit& Unit, TFunctionRef<void(TFunctionRef<void()>)> Run, TArray<float>* Steps = nullptr)
	{
		float Before[2];
		Unit.GetTrackPhase(LeftTrack, Before[0]);
		Unit.GetTrackPhase(RightTrack, Before[1]);
		float Largest = 0.f;
		Run([&]
		{
			for (int32 Side = 0; Side < 2; ++Side)
			{
				float Now = 0.f;
				Unit.GetTrackPhase(LeftTrack + Side, Now);
				const float Step = FMath::Abs(PhaseDelta(Now, Before[Side]));
				Largest = FMath::Max(Largest, Step);
				if (Steps && Side == 0)
				{
					Steps->Add(Step);
				}
				Before[Side] = Now;
			}
		});
		return Largest;
	}

	/**
	 * The tracks and wheels of a tracked hull driven by its gear (#151): each side moves by the realized travel of its own side,
	 * forward, back, on an arc, pivoting either way, ramping up and down, and not at all standing, blocked, teleported or
	 * pushed out of a solid. The material is given the phase only where it is drawn, and resumes where the phase has got to.
	 */
	void TestTrackedGear(FAutomationTestBase& Test)
	{
		const FRTSTestWorld TestWorld;
		ARTSUnit* Unit = TestWorld.SpawnUnit(MakeTrackedData(), 0, FVector::ZeroVector);
		const RTSLocomotion::FLimits& Limits = Unit->GetUnitMovement()->GetLimits();
		float Unused = 0.f;
		if (!Test.TestTrue(TEXT("Both tracks keep a phase"), Unit->GetTrackPhase(LeftTrack, Unused) && Unit->GetTrackPhase(RightTrack, Unused))
			|| !Test.TestFalse(TEXT("A sprocket is no track"), Unit->GetTrackPhase(LeftSprocket, Unused)))
		{
			return;
		}
		double Travel[2];

		RTSLocomotion::FCommand Ahead;
		Ahead.Speed = 600.f;
		{
			const FGearMark Mark(*Unit);
			RunGear(*Unit, Ahead, 2.0);
			FollowsTravel(Test, TEXT("Forward"), *Unit, Mark, Travel);
			Test.TestTrue(FString::Printf(TEXT("Forward: both sides travel alike (%.2f, %.2f cm)"), Travel[0], Travel[1]),
				Travel[0] > 100.0 && FMath::IsNearlyEqual(Travel[0], Travel[1], 0.01 * Travel[0]));
		}
		RunGear(*Unit, RTSLocomotion::FCommand(), 2.0);
		{
			RTSLocomotion::FCommand Back;
			Back.Speed = -300.f;
			const FGearMark Mark(*Unit);
			RunGear(*Unit, Back, 2.0);
			FollowsTravel(Test, TEXT("Reverse"), *Unit, Mark, Travel);
			Test.TestTrue(FString::Printf(TEXT("Reverse: both sides travel back alike (%.2f, %.2f cm)"), Travel[0], Travel[1]),
				Travel[0] < -100.0 && FMath::IsNearlyEqual(Travel[0], Travel[1], 0.01 * FMath::Abs(Travel[0])));
		}
		RunGear(*Unit, RTSLocomotion::FCommand(), 2.0);
		{
			const FGearMark Mark(*Unit);
			RunGear(*Unit, RTSLocomotion::FCommand::FromTracks(Limits, 500.f, 250.f), 3.0);
			FollowsTravel(Test, TEXT("Right turn"), *Unit, Mark, Travel);
			Test.TestTrue(FString::Printf(TEXT("A right turn runs the outer, left track faster (%.1f against %.1f cm)"), Travel[0], Travel[1]),
				Travel[0] > 1.5 * Travel[1] && Travel[1] > 100.0);
		}
		RunGear(*Unit, RTSLocomotion::FCommand(), 2.0);
		{
			const FGearMark Mark(*Unit);
			RunGear(*Unit, RTSLocomotion::FCommand::FromTracks(Limits, 100.f, 450.f), 3.0);
			FollowsTravel(Test, TEXT("Sharp left turn"), *Unit, Mark, Travel);
			Test.TestTrue(FString::Printf(TEXT("A sharp left turn runs the outer, right track faster (%.1f against %.1f cm)"), Travel[1], Travel[0]),
				Travel[1] > 3.0 * Travel[0] && Travel[0] > 10.0);
		}
		RunGear(*Unit, RTSLocomotion::FCommand(), 2.0);
		for (const float Way : { -1.f, 1.f })
		{
			const TCHAR* What = Way < 0.f ? TEXT("Pivot left") : TEXT("Pivot right");
			const FGearMark Mark(*Unit);
			RunGear(*Unit, RTSLocomotion::FCommand::FromTracks(Limits, Way * 200.f, -Way * 200.f), 2.0);
			FollowsTravel(Test, What, *Unit, Mark, Travel);
			Test.TestTrue(FString::Printf(TEXT("%s: the tracks run opposite ways, as far each (%.2f, %.2f cm)"), What, Travel[0], Travel[1]),
				Way * Travel[0] > 50.0 && FMath::IsNearlyEqual(Travel[0], -Travel[1], 0.01 * FMath::Abs(Travel[0])));
			RunGear(*Unit, RTSLocomotion::FCommand(), 1.0);
		}

		// From a standstill to cruising and back: the links speed up and slow down frame by frame, never by a jump.
		{
			RTSLocomotion::FCommand Cruise;
			Cruise.Speed = CruiseSpeed;
			TArray<float> Up;
			const FGearMark Mark(*Unit);
			const float Largest = LargestStep(*Unit, [Unit, &Cruise](TFunctionRef<void()> Each) { RunGear(*Unit, Cruise, 1.5, Each); }, &Up);
			TArray<float> Down;
			LargestStep(*Unit, [Unit](TFunctionRef<void()> Each) { RunGear(*Unit, RTSLocomotion::FCommand(), 1.5, Each); }, &Down);
			FollowsTravel(Test, TEXT("Accelerating and braking"), *Unit, Mark, Travel);
			// 400 cm/s2 adds 6.7 cm/s a frame: 0.007 of a link a frame at 60 frames a second.
			auto Smooth = [](const TArray<float>& Steps, float Sign)
			{
				float Worst = 0.f;
				for (int32 Index = 1; Index < Steps.Num(); ++Index)
				{
					Worst = FMath::Max(Worst, Sign * (Steps[Index - 1] - Steps[Index]));
				}
				return Worst;
			};
			Test.TestTrue(FString::Printf(TEXT("Accelerating, the links speed up every frame (worst slowdown %.5f of a link a frame)"), Smooth(Up, 1.f)),
				Smooth(Up, 1.f) < 1.e-4f && Up.Last() > Up[0]);
			Test.TestTrue(FString::Printf(TEXT("Braking, they slow down every frame (worst speed-up %.5f) to a stop"), Smooth(Down, -1.f)),
				Smooth(Down, -1.f) < 1.e-4f && Down.Last() < 1.e-5f);
			Test.TestTrue(FString::Printf(TEXT("Never by more than the speed allows (%.4f of a link a frame)"), Largest),
				Largest <= CruiseSpeed / GearRate / TrackedPitch + 0.01f);
		}

		// Standing for 5 s: still.
		{
			const FGearMark Mark(*Unit);
			RunGear(*Unit, RTSLocomotion::FCommand(), 5.0);
			const FGearMark Now(*Unit);
			Test.TestTrue(FString::Printf(TEXT("Standing 5 s, the tracks do not move (%.5f, %.5f of a link)"), PhaseDelta(Now.Phase[0], Mark.Phase[0]),
				PhaseDelta(Now.Phase[1], Mark.Phase[1])), FMath::Abs(PhaseDelta(Now.Phase[0], Mark.Phase[0])) <= StillPhase
				&& FMath::Abs(PhaseDelta(Now.Phase[1], Mark.Phase[1])) <= StillPhase);
		}

		// The material has the phase, and no link rate once standing, so the links do not smear.
		const UStaticMeshComponent* LeftComponent = nullptr;
		{
			TArray<UStaticMeshComponent*> Meshes;
			Unit->GetComponents(Meshes);
			float Phase = 0.f;
			Unit->GetTrackPhase(LeftTrack, Phase);
			// The left track: a cube on the hull's left; the hull itself is the cube in the middle.
			for (const UStaticMeshComponent* Mesh : Meshes)
			{
				if (Mesh->GetStaticMesh() == LoadShape(TEXT("Cube")) && Mesh->GetRelativeLocation().Y < 0.f)
				{
					LeftComponent = Mesh;
				}
			}
			const TArray<float>* Data = LeftComponent ? &LeftComponent->GetCustomPrimitiveData().Data : nullptr;
			Test.TestTrue(TEXT("The left track's material has its phase, with no link rate while it stands"),
				Data && Data->Num() >= 3 && (*Data)[RTSPrimitiveData::TrackPhase] == Phase && (*Data)[RTSPrimitiveData::TrackRate] == 0.f);

			// The team colour beside it (#150): each kept where it is when the other changes.
			auto HasColor = [Data](int32 TeamId)
			{
				const FLinearColor Color = ARTSUnit::GetTeamColor(TeamId);
				constexpr int32 Index = RTSPrimitiveData::TeamColor;
				return Data && Data->Num() >= Index + 4 && (*Data)[Index] == Color.R && (*Data)[Index + 1] == Color.G
					&& (*Data)[Index + 2] == Color.B && (*Data)[Index + 3] == Color.A;
			};
			Test.TestTrue(TEXT("The track has its team colour beside the phase"), HasColor(0));
			FIntProperty* TeamProperty = FindFProperty<FIntProperty>(ARTSUnit::StaticClass(), TEXT("TeamId"));
			TeamProperty->SetPropertyValue_InContainer(Unit, 1);
			Unit->ProcessEvent(Unit->FindFunctionChecked(TEXT("OnRep_TeamId")), nullptr);
			Test.TestTrue(TEXT("A new team colour leaves the phase as it was"), HasColor(1) && Data && (*Data)[RTSPrimitiveData::TrackPhase] == Phase
				&& (*Data)[RTSPrimitiveData::TrackRate] == 0.f);
			RunGear(*Unit, Ahead, 0.5);
			float Moved = 0.f;
			float Drawn = 0.f;
			Unit->GetTrackPhase(LeftTrack, Moved);
			Unit->GetTrackMaterialPhase(LeftTrack, Drawn);
			Test.TestTrue(TEXT("and a new phase leaves the team colour"), HasColor(1) && Data && (*Data)[RTSPrimitiveData::TrackRate] < 0.f
				&& FMath::Abs(PhaseDelta(Drawn, Moved)) <= ARTSUnit::TrackDriftLimit && Moved != Phase);
			RunGear(*Unit, RTSLocomotion::FCommand(), 2.0);
		}

		// Teleported, and put in a solid that pushes it out: the hull may move, the tracks and wheels do not.
		{
			const FGearMark Mark(*Unit);
			Unit->SetActorLocation(Unit->GetActorLocation() + FVector(3000.f, 1000.f, 0.f));
			RunGear(*Unit, RTSLocomotion::FCommand(), 0.5);
			FBox Solid = Unit->GetComponentsBoundingBox();
			AActor* Overlap = SpawnTestSolid(*TestWorld.World, FBox(Solid.GetCenter() + FVector(250.f, -400.f, -100.f), Solid.GetCenter() + FVector(450.f, 400.f, 300.f)));
			RunGear(*Unit, RTSLocomotion::FCommand(), 1.0);
			Overlap->Destroy();
			const FGearMark Now(*Unit);
			Test.TestTrue(FString::Printf(TEXT("A teleport and a solid it was put in move no track (%.5f, %.5f of a link; %d recoveries)"),
				PhaseDelta(Now.Phase[0], Mark.Phase[0]), PhaseDelta(Now.Phase[1], Mark.Phase[1]), Unit->GetRealizedMotion().RecoveryCount),
				FMath::Abs(PhaseDelta(Now.Phase[0], Mark.Phase[0])) <= StillPhase && FMath::Abs(PhaseDelta(Now.Phase[1], Mark.Phase[1])) <= StillPhase
				&& FMath::Abs(AngleDelta(Now.Angle[0], Mark.Angle[0])) <= FMath::DegreesToRadians(WheelTolerance));
		}

		// Flat out into a solid: once blocked, for 5 s, the tracks stand.
		{
			const FVector Front = Unit->GetActorLocation() + Unit->GetActorForwardVector() * 800.f;
			AActor* Wall = SpawnTestSolid(*TestWorld.World, FBox(Front + FVector(0.f, -600.f, -100.f), Front + FVector(200.f, 600.f, 300.f)));
			RTSLocomotion::FCommand Flat;
			Flat.Speed = 1.e5f;
			bool bBlocked = false;
			RunGear(*Unit, Flat, 4.0, [Unit, &bBlocked] { bBlocked |= Unit->GetRealizedMotion().Status == ERTSLocomotionStatus::Blocked; });
			const FGearMark Mark(*Unit);
			RunGear(*Unit, Flat, 5.0);
			const FGearMark Now(*Unit);
			Test.TestTrue(TEXT("The wall blocks the hull"), bBlocked);
			Test.TestTrue(FString::Printf(TEXT("Blocked 5 s, the tracks stand within 0.1 %% of a loop (%.5f, %.5f of a link)"),
				PhaseDelta(Now.Phase[0], Mark.Phase[0]), PhaseDelta(Now.Phase[1], Mark.Phase[1])),
				FMath::Abs(PhaseDelta(Now.Phase[0], Mark.Phase[0])) <= 0.001f * LoopLinks && FMath::Abs(PhaseDelta(Now.Phase[1], Mark.Phase[1])) <= 0.001f * LoopLinks);
			FollowsTravel(Test, TEXT("Blocked"), *Unit, Mark, Travel);
			Wall->Destroy();
			RunGear(*Unit, RTSLocomotion::FCommand(), 2.0);
		}

		// Not drawn here: the phase counts on, the material keeps the last it had; drawn again, it has the phase as it is.
		if (LeftComponent)
		{
			const float Sent = LeftComponent->GetCustomPrimitiveData().Data[0];
			FRTSTrackViews Far;
			Far.Locations.Add(Unit->GetActorLocation() + FVector(0.f, 0.f, 1.e6f));
			Far.Range = 5000.f;
			const FGearMark Mark(*Unit);
			RunGear(*Unit, Ahead, 1.0, [] {}, &Far);
			float Phase = 0.f;
			Unit->GetTrackPhase(LeftTrack, Phase);
			Test.TestTrue(FString::Printf(TEXT("Out of sight the phase counts on (%.3f) while the material keeps its own (%.3f)"), Phase, Sent),
				FMath::Abs(PhaseDelta(Phase, Mark.Phase[0])) > 0.01f && LeftComponent->GetCustomPrimitiveData().Data[0] == Sent);
			FRTSTrackViews Near = Far;
			Near.Locations[0] = Unit->GetActorLocation();
			// The count the bench reads (#212): one primitive data update per track whose phase goes to its material.
			Test.TestEqual(TEXT("Out of sight no track sends its phase"), Unit->UpdateRunningGear(&Far), 0);
			Test.TestEqual(TEXT("In sight both tracks send the phase they kept"), Unit->UpdateRunningGear(&Near), 2);
			Test.TestEqual(TEXT("Standing, a sent phase is not sent again"), Unit->UpdateRunningGear(&Near), 0);
			// Off screen: a camera looking away keeps the phase from the material until it turns to the tracks (#212).
			RunGear(*Unit, Ahead, 0.25, [] {}, &Far);
			FRTSTrackViews Away;
			Away.Locations.Add(Unit->GetActorLocation() - FVector(5000.f, 0.f, 0.f));
			Away.Directions.Add(FVector(-1.f, 0.f, 0.f));
			Away.HalfAngles.Add(0.5f);
			Test.TestEqual(TEXT("A camera looking away gets no phase"), Unit->UpdateRunningGear(&Away), 0);
			Away.Directions[0] = FVector(1.f, 0.f, 0.f);
			Test.TestEqual(TEXT("Turned to the tracks, the camera gets both phases"), Unit->UpdateRunningGear(&Away), 2);
			RunGear(*Unit, RTSLocomotion::FCommand(), 2.0, [] {}, &Near);
			Unit->GetTrackPhase(LeftTrack, Phase);
			Test.TestTrue(TEXT("In sight again, the material has the phase it got to, not a restart"),
				LeftComponent->GetCustomPrimitiveData().Data[0] == Phase);
			FRTSTrackViews Server;
			Server.bDraws = false;
			RunGear(*Unit, Ahead, 0.5, [] {}, &Server);
			Test.TestTrue(TEXT("A machine that draws nothing never sends it"), LeftComponent->GetCustomPrimitiveData().Data[0] == Phase);
		}

		// Rigid tracks, as the bench's comparison draws them: no phase, the rigid material.
		{
			IConsoleVariable* Animation = IConsoleManager::Get().FindConsoleVariable(TEXT("rts.Vehicle.TrackAnimation"));
			Animation->Set(false, ECVF_SetByCode);
			ARTSUnit* Rigid = TestWorld.SpawnUnit(MakeTrackedData(), 0, FVector(0.f, 5000.f, 0.f));
			Animation->Set(true, ECVF_SetByCode);
			TArray<UStaticMeshComponent*> Meshes;
			Rigid->GetComponents(Meshes);
			const UMaterialInterface* RigidMaterial = Rigid->GetUnitData()->Parts[LeftTrack].RigidMaterial;
			// The material itself: the team colour is no dynamic instance over it (#150).
			const int32 Drawn = Algo::CountIf(Meshes, [RigidMaterial](const UStaticMeshComponent* Mesh)
			{
				return Mesh->GetMaterial(0) == RigidMaterial;
			});
			Test.TestFalse(TEXT("With rts.Vehicle.TrackAnimation 0 a track keeps no phase"), Rigid->GetTrackPhase(LeftTrack, Unused));
			Test.TestEqual(TEXT("and both are drawn with the rigid material"), Drawn, 2);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSRunningGearTest, "WorldDivide.Units.RunningGear.Spin", TestFlags)

bool FRTSRunningGearTest::RunTest(const FString& Parameters)
{
	const FRTSTestWorld TestWorld;
	ARTSUnit* Unit = TestWorld.SpawnUnit(MakeVehicleData(), 0, FVector::ZeroVector);
	// The first call only notes where the unit stands.
	Unit->UpdateRunningGear();

	auto Pitch = [Unit](int32 Wheel)
	{
		// Forward of straight up, in the hull's frame: positive once the wheel rolled forward.
		return FVector::DotProduct(GetWheelTop(*Unit, Wheel), Unit->GetActorForwardVector());
	};

	const FTransform Rest = GetPart(*Unit, LeftWheel);
	Unit->UpdateRunningGear();
	TestTrue(TEXT("A unit standing still leaves its wheels alone"), GetPart(*Unit, LeftWheel).Equals(Rest));

	// The cylinder is 50 cm round: 10 cm is about a quarter radian.
	Drive(*Unit, 10.f);
	TestTrue(FString::Printf(TEXT("Driving forward rolls the left wheel's top forward (%.3f)"), Pitch(LeftWheel)), Pitch(LeftWheel) > 0.1f);
	TestTrue(FString::Printf(TEXT("and the right one's, though it is turned round (%.3f)"), Pitch(RightWheel)), Pitch(RightWheel) > 0.1f);
	TestTrue(TEXT("Wheels of one side roll together"), FMath::IsNearlyEqual(Pitch(LeftWheel), Pitch(LeftRearWheel), 0.001f));
	TestTrue(TEXT("The wheels stay where they are on the hull"),
		GetPart(*Unit, LeftWheel).GetLocation().Equals(Rest.GetLocation() + FVector(10.f, 0.f, 0.f), 0.1f));

	Drive(*Unit, -20.f);
	TestTrue(FString::Printf(TEXT("Reversing rolls them back past where they started (%.3f)"), Pitch(LeftWheel)), Pitch(LeftWheel) < -0.1f);

	Drive(*Unit, 10.f);
	TestTrue(TEXT("Back at the start, the wheels are as they were"), FMath::IsNearlyZero(Pitch(LeftWheel), 0.001f));

	// Turning right on the spot drives the left side forward and the right side back.
	Drive(*Unit, 0.f, 5.f);
	TestTrue(FString::Printf(TEXT("A right turn rolls the left wheels forward (%.3f)"), Pitch(LeftWheel)), Pitch(LeftWheel) > 0.1f);
	TestTrue(FString::Printf(TEXT("and the right wheels back (%.3f)"), Pitch(RightWheel)), Pitch(RightWheel) < -0.1f);

	const FTransform Turned = GetPart(*Unit, LeftWheel);
	Unit->UpdateRunningGear();
	TestTrue(TEXT("Once the unit stops, the wheels stop"), GetPart(*Unit, LeftWheel).Equals(Turned));

	TestTrackedGear(*this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSTrackRateTest, "WorldDivide.Units.RunningGear.TrackRate", TestFlags)

/**
 * #216: a track's material works its phase out from a link rate and a start time, sent only when the rate changes or the
 * phase drifts from the realized travel. In every frame the drawn phase stays within ARTSUnit::TrackDriftLimit of the
 * travel's, a steady drive hardly sends, and a stopped track has its phase exactly, without a rate.
 */
bool FRTSTrackRateTest::RunTest(const FString& Parameters)
{
	const FRTSTestWorld TestWorld;
	ARTSUnit* Unit = TestWorld.SpawnUnit(MakeTrackedData(), 0, FVector::ZeroVector);
	const RTSLocomotion::FLimits& Limits = Unit->GetUnitMovement()->GetLimits();
	TArray<const UStaticMeshComponent*, TInlineAllocator<2>> Components;
	Unit->GetTrackComponents(Components);
	if (!TestEqual(TEXT("Both tracks are there"), Components.Num(), 2))
	{
		return false;
	}

	/**
	 * Over one run: the largest gap between the drawn and the travel's phase, and at its end; the phases sent, of one a track
	 * and frame at most; and each side's rate at its end.
	 */
	struct FRun
	{
		float WorstDrift = 0.f;
		float EndDrift = 0.f;
		int32 Sent = 0;
		int32 Frames = 0;
		float Rate[2] = { 0.f, 0.f };
	};
	auto Run = [Unit, &Components](const RTSLocomotion::FCommand& Command, double Seconds)
	{
		FRun Out;
		Unit->GetUnitMovement()->SetCommandOverride(Command);
		for (int32 Index = FMath::RoundToInt32(Seconds * GearRate); Index > 0; --Index)
		{
			Unit->GetWorld()->TimeSeconds += 1.0 / GearRate;
			Unit->GetUnitMovement()->TickComponent(1.f / GearRate, LEVELTICK_All, nullptr);
			Out.Sent += Unit->UpdateRunningGear();
			Out.Frames += 2;
			Out.EndDrift = 0.f;
			for (int32 Side = 0; Side < 2; ++Side)
			{
				float Phase = 0.f;
				float Drawn = 0.f;
				Unit->GetTrackPhase(LeftTrack + Side, Phase);
				Unit->GetTrackMaterialPhase(LeftTrack + Side, Drawn);
				Out.EndDrift = FMath::Max(Out.EndDrift, FMath::Abs(PhaseDelta(Drawn, Phase)));
			}
			Out.WorstDrift = FMath::Max(Out.WorstDrift, Out.EndDrift);
		}
		for (int32 Side = 0; Side < 2; ++Side)
		{
			Out.Rate[Side] = Components[Side]->GetCustomPrimitiveData().Data[RTSPrimitiveData::TrackRate];
		}
		return Out;
	};
	auto Within = [this](const TCHAR* What, const FRun& Done)
	{
		TestTrue(FString::Printf(TEXT("%s: the drawn phase stays within the drift limit (%.4f of a link; %d sends in %d track frames)"), What,
			Done.WorstDrift, Done.Sent, Done.Frames), Done.WorstDrift <= ARTSUnit::TrackDriftLimit + 1.e-4f);
	};
	auto Stands = [this, &Within](const TCHAR* What, const FRun& Done)
	{
		Within(What, Done);
		TestTrue(FString::Printf(TEXT("%s: stopped, the material has the phase exactly (%.6f) and no rate (%.3f, %.3f)"), What, Done.EndDrift,
			Done.Rate[0], Done.Rate[1]), Done.EndDrift == 0.f && Done.Rate[0] == 0.f && Done.Rate[1] == 0.f);
	};

	RTSLocomotion::FCommand Ahead;
	Ahead.Speed = 600.f;
	const FRun Accelerate = Run(Ahead, 2.0);
	Within(TEXT("Accelerating"), Accelerate);
	TestTrue(FString::Printf(TEXT("Accelerating sends in at most one track frame of twenty (%d of %d)"), Accelerate.Sent, Accelerate.Frames),
		20 * Accelerate.Sent <= Accelerate.Frames);
	const FRun Cruise = Run(Ahead, 3.0);
	Within(TEXT("Cruising"), Cruise);
	TestTrue(FString::Printf(TEXT("Cruising sends in at most one track frame of twenty (%d of %d)"), Cruise.Sent, Cruise.Frames),
		20 * Cruise.Sent <= Cruise.Frames);
	TestTrue(FString::Printf(TEXT("Forward, both tracks count down (%.2f, %.2f links/s)"), Cruise.Rate[0], Cruise.Rate[1]),
		Cruise.Rate[0] < 0.f && Cruise.Rate[1] < 0.f);
	Stands(TEXT("Braking"), Run(RTSLocomotion::FCommand(), 2.0));
	const FRun Still = Run(RTSLocomotion::FCommand(), 1.0);
	TestEqual(TEXT("Standing sends nothing"), Still.Sent, 0);

	RTSLocomotion::FCommand Back;
	Back.Speed = -300.f;
	const FRun Reverse = Run(Back, 3.0);
	Within(TEXT("Reversing"), Reverse);
	TestTrue(FString::Printf(TEXT("Reversing, both tracks count up (%.2f, %.2f links/s)"), Reverse.Rate[0], Reverse.Rate[1]),
		Reverse.Rate[0] > 0.f && Reverse.Rate[1] > 0.f);
	Stands(TEXT("Stopping from reverse"), Run(RTSLocomotion::FCommand(), 2.0));

	const FRun Pivot = Run(RTSLocomotion::FCommand::FromTracks(Limits, 200.f, -200.f), 2.0);
	Within(TEXT("Pivoting right"), Pivot);
	TestTrue(FString::Printf(TEXT("Pivoting right, the left track counts down and the right one up (%.2f, %.2f links/s)"), Pivot.Rate[0],
		Pivot.Rate[1]), Pivot.Rate[0] < 0.f && Pivot.Rate[1] > 0.f);
	Stands(TEXT("Stopping a pivot"), Run(RTSLocomotion::FCommand(), 2.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSRunningGearPlacementTest, "WorldDivide.Units.RunningGear.FollowsHull", TestFlags)

bool FRTSRunningGearPlacementTest::RunTest(const FString& Parameters)
{
	const FRTSTestWorld TestWorld;
	ARTSUnit* Unit = TestWorld.SpawnUnit(MakeVehicleData(), 0, FVector(100.f, 200.f, 0.f));
	URTSRunningGearSubsystem* Gear = TestWorld.World->GetSubsystem<URTSRunningGearSubsystem>();
	if (!TestNotNull(TEXT("The world places running gear"), Gear))
	{
		return false;
	}

	TArray<UInstancedStaticMeshComponent*> Sets;
	Unit->GetComponents(Sets);
	if (!TestEqual(TEXT("The three wheels share one set"), Sets.Num(), 1))
	{
		return false;
	}
	UInstancedStaticMeshComponent& Set = *Sets[0];
	TestTrue(TEXT("The set is placed in world space, so the hull's move does not carry it (#165)"),
		Set.IsUsingAbsoluteLocation() && Set.IsUsingAbsoluteRotation() && Set.IsUsingAbsoluteScale());
	TestTrue(TEXT("It stays attached to the hull, for its visibility"), Set.GetAttachParent() != nullptr);

	// Instances in the order MakeVehicleData lists the wheels.
	const int32 WheelParts[] = { LeftWheel, RightWheel, LeftRearWheel };
	auto IsDrawnOnHull = [this, Unit, &Set, &WheelParts](const TCHAR* What)
	{
		bool bAll = true;
		for (int32 Instance = 0; Instance < UE_ARRAY_COUNT(WheelParts); ++Instance)
		{
			FTransform Drawn;
			Set.GetInstanceTransform(Instance, Drawn, /*bWorldSpace*/ true);
			bAll &= Drawn.Equals(GetPart(*Unit, WheelParts[Instance]), 0.01f);
		}
		TestTrue(What, bAll);
		return bAll;
	};
	IsDrawnOnHull(TEXT("The wheels are drawn on the hull from the start"));

	Gear->PlaceAll();
	const FVector Before = GetPart(*Unit, LeftWheel).GetLocation();
	// A push sideways, as avoidance gives: the hull moves, the wheels do not roll.
	const FVector Push(0.f, 30.f, 0.f);
	Unit->SetActorLocation(Unit->GetActorLocation() + Push);
	FTransform Stale;
	Set.GetInstanceTransform(0, Stale, /*bWorldSpace*/ true);
	TestTrue(TEXT("The hull's move alone leaves the instances where they were"), Stale.GetLocation().Equals(Before, 0.01f));
	Gear->PlaceAll();
	IsDrawnOnHull(TEXT("The gear pass puts them back on the pushed hull"));
	TestTrue(TEXT("Pushed sideways, they moved with the hull"), GetPart(*Unit, LeftWheel).GetLocation().Equals(Before + Push, 0.01f));

	// Forward, back, and a turn on the spot: rolled and placed together.
	Drive(*Unit, 40.f);
	IsDrawnOnHull(TEXT("Driven forward, the wheels are drawn rolled on the hull"));
	Drive(*Unit, -60.f);
	IsDrawnOnHull(TEXT("Reversed, too"));
	Drive(*Unit, 0.f, 30.f);
	IsDrawnOnHull(TEXT("Pivoted, too"));

	// A hull tilted on its own, as on a slope, carries the wheels too.
	Unit->SetActorRotation(Unit->GetActorRotation() + FRotator(5.f, 0.f, 0.f));
	Gear->PlaceAll();
	IsDrawnOnHull(TEXT("Tilted, too"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSRunningGearMotionTest, "WorldDivide.Units.RunningGear.PreviousTransforms", TestFlags)

bool FRTSRunningGearMotionTest::RunTest(const FString& Parameters)
{
	const FRTSTestWorld TestWorld;
	ARTSUnit* Unit = TestWorld.SpawnUnit(MakeVehicleData(), 0, FVector(100.f, 200.f, 0.f));
	URTSRunningGearSubsystem* Gear = TestWorld.World->GetSubsystem<URTSRunningGearSubsystem>();
	TArray<UInstancedStaticMeshComponent*> Sets;
	Unit->GetComponents(Sets);
	if (!TestNotNull(TEXT("The world places running gear"), Gear) || !TestEqual(TEXT("The three wheels share one set"), Sets.Num(), 1))
	{
		return false;
	}
	UInstancedStaticMeshComponent& Set = *Sets[0];
	// Instances in the order MakeVehicleData lists the wheels.
	const int32 WheelParts[] = { LeftWheel, RightWheel, LeftRearWheel };

	// The motion vectors the renderer draws the wheels with go from each instance's previous place to its current one (#243).
	auto Places = [&Set, &WheelParts](bool bPrevious)
	{
		TArray<FTransform> Out;
		for (int32 Instance = 0; Instance < UE_ARRAY_COUNT(WheelParts); ++Instance)
		{
			FTransform Place;
			if (bPrevious ? Set.GetInstancePrevTransform(Instance, Place, /*bWorldSpace*/ true)
				: Set.GetInstanceTransform(Instance, Place, /*bWorldSpace*/ true))
			{
				Out.Add(Place);
			}
		}
		return Out;
	};
	auto Same = [](const TArray<FTransform>& A, const TArray<FTransform>& B)
	{
		bool bSame = A.Num() == B.Num();
		for (int32 Index = 0; bSame && Index < A.Num(); ++Index)
		{
			bSame = A[Index].Equals(B[Index], 0.01f);
		}
		return bSame;
	};

	TestEqual(TEXT("Every wheel has a previous place from the start"), Places(true).Num(), static_cast<int32>(UE_ARRAY_COUNT(WheelParts)));
	TestTrue(TEXT("and a new unit's wheels show no motion"), Same(Places(true), Places(false)));

	Gear->PlaceAll();
	Gear->PlaceAll();
	const TArray<FTransform> Before = Places(false);
	Drive(*Unit, 40.f);
	TestTrue(TEXT("Driven, each wheel's previous place is where it was drawn the frame before"), Same(Places(true), Before));
	TestFalse(TEXT("and its current one is not: the wheels show their drive and roll"), Same(Places(false), Before));
	TArray<FTransform> Rolled = Places(false);
	Drive(*Unit, 40.f);
	TestTrue(TEXT("Driving on, the previous place follows one frame behind"), Same(Places(true), Rolled));

	// A push sideways, as avoidance gives: moved without a roll, still a motion to draw.
	Rolled = Places(false);
	Unit->SetActorLocation(Unit->GetActorLocation() + FVector(0.f, 30.f, 0.f));
	Gear->PlaceAll();
	TestTrue(TEXT("Pushed, the previous place is the one before the push"), Same(Places(true), Rolled));
	TestFalse(TEXT("and the wheels moved with the hull"), Same(Places(false), Rolled));

	const TArray<FTransform> Stopped = Places(false);
	Gear->PlaceAll();
	TestTrue(TEXT("Stopped, the wheels stay where they are"), Same(Places(false), Stopped));
	TestTrue(TEXT("and the next pass makes their previous place their current one, so they show no motion"), Same(Places(true), Stopped));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSGunElevationTest, "WorldDivide.Combat.Turret.GunElevation", TestFlags)

bool FRTSGunElevationTest::RunTest(const FString& Parameters)
{
	const FRTSTestWorld TestWorld;
	ARTSUnit* Tank = TestWorld.SpawnUnit(MakeVehicleData(MakeWeapon()), 0, FVector::ZeroVector);
	URTSUnitData* TargetData = NewObject<URTSUnitData>();
	TargetData->MaxHealth = 100.f;
	// Ahead and well above the gun, as on a ridge: 8 m above the ground under the tank.
	const float GroundZ = Tank->GetNavAgentLocation().Z;
	ARTSUnit* High = TestWorld.SpawnUnit(TargetData, 1, FVector(2000.f, 0.f, GroundZ + 800.f));

	Tank->GetWeaponComponent()->UpdateWeapon(0.05f);
	TestTrue(TEXT("The tank picks the target"), Tank->GetWeaponComponent()->GetTarget() == High);
	const float MuzzleBefore = Tank->GetMuzzleLocation().Z;
	for (int32 Frame = 0; Frame < 40; ++Frame)
	{
		Tank->UpdateTurret(0.05f);
	}
	// The trunnion stands about 170 cm above the ground: some 630 cm up over 1950 cm, about 18 degrees, under the limit.
	TestTrue(FString::Printf(TEXT("The gun rose toward the target (%.1f)"), Tank->GetGunPitch()),
		Tank->GetGunPitch() > 15.f && Tank->GetGunPitch() < 19.5f);
	TestTrue(TEXT("The muzzle rose with it"), Tank->GetMuzzleLocation().Z > MuzzleBefore + 20.f);

	// Far above: the gun stops at its elevation limit.
	High->SetActorLocation(FVector(500.f, 0.f, 3000.f));
	for (int32 Frame = 0; Frame < 40; ++Frame)
	{
		Tank->UpdateTurret(0.05f);
	}
	TestEqual(TEXT("The gun stops at its highest"), Tank->GetGunPitch(), 20.f);
	return true;
}

#endif
