#include "Combat/RTSWeaponComponent.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSWeaponData.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "UObject/Package.h"
#include "Units/RTSUnit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RTSRecoilTests
{
	using RTSTests::FRTSTestWorld;

	constexpr float Step = 1.f / 60.f;
	/** How close a part has to come back to where it rests, in cm. */
	constexpr float RestTolerance = 0.01f;
	/** Longer than any of the M20's reactions takes to settle. */
	constexpr float SettleLimit = 5.f;

	URTSUnitData* LoadM20(FAutomationTestBase& Test)
	{
		URTSUnitData* M20 = LoadObject<URTSUnitData>(nullptr, TEXT("/Game/Data/Units/DA_TestM20Guardian.DA_TestM20Guardian"));
		if (!Test.TestNotNull(TEXT("DA_TestM20Guardian loads"), M20) || !Test.TestNotNull(TEXT("It has a weapon"), M20->Weapon.Get())
			|| !Test.TestTrue(TEXT("Its gun recoils, jumps and rocks the hull"),
				M20->GunRecoil.StrokeFraction > 0.f && M20->GunRecoil.JumpDegrees > 0.f && M20->GunRecoil.HullRockDegrees > 0.f))
		{
			return nullptr;
		}
		return M20;
	}

	/** The index in URTSUnitData::Parts of the M20's barrel, the part that recoils. */
	int32 FindBarrel(const URTSUnitData& Data)
	{
		return Data.Parts.IndexOfByPredicate([](const FRTSVehiclePart& Part) { return Part.bRecoils; });
	}

	FTransform GetBarrel(const ARTSUnit& Unit, int32 Barrel)
	{
		FTransform Transform;
		Unit.GetPartTransform(Barrel, Transform);
		return Transform;
	}

	/** The barrel on the gun, from its muzzle socket: the gun's jump and the hull's rock move both alike. */
	FTransform GetBarrelOnGun(const ARTSUnit& Unit, int32 Barrel)
	{
		return GetBarrel(Unit, Barrel).GetRelativeTransform(Unit.GetMuzzleTransform());
	}

	/** The component drawing the unit's hull mesh. */
	const UStaticMeshComponent* FindHull(const ARTSUnit& Unit)
	{
		const UStaticMeshComponent* Hull = nullptr;
		Unit.ForEachComponent<UStaticMeshComponent>(false, [&Hull, &Unit](const UStaticMeshComponent* Component)
		{
			Hull = Component->GetStaticMesh() == Unit.GetUnitData()->Mesh ? Component : Hull;
		});
		return Hull;
	}

	/** Steps the recoil until everything has settled; false if it never does. */
	bool Settle(ARTSUnit& Unit)
	{
		for (float Time = 0.f; Time < SettleLimit; Time += Step)
		{
			if (!Unit.IsRecoiling())
			{
				return true;
			}
			Unit.UpdateGunRecoil(Step);
		}
		return !Unit.IsRecoiling();
	}

	URTSUnitData* MakeTarget()
	{
		URTSUnitData* Data = NewObject<URTSUnitData>();
		Data->MaxHealth = 1.0e9f;
		return Data;
	}
}

/**
 * The M20's barrel on one shot (#218), with World in Conflict's stroke (#353): it snaps back along the bore by a third of its
 * barrel within a frame, stays back to 0.1 s after the shot, is half way out at 0.3 s, nearly home at 0.4 s and home at 0.5 s,
 * exactly where it rested.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSRecoilStrokeTest, "WorldDivide.Combat.Recoil.Stroke", RTSTests::TestFlags)

bool FRTSRecoilStrokeTest::RunTest(const FString& Parameters)
{
	using namespace RTSRecoilTests;

	URTSUnitData* M20 = LoadM20(*this);
	if (!M20)
	{
		return false;
	}
	const int32 Barrel = FindBarrel(*M20);
	if (!TestTrue(TEXT("The M20 has a barrel that recoils"), Barrel != INDEX_NONE))
	{
		return false;
	}
	const FRTSTestWorld TestWorld;
	ARTSUnit* Tank = TestWorld.SpawnUnit(M20, 0, FVector::ZeroVector);
	const FTransform Rest = GetBarrel(*Tank, Barrel);
	const FTransform RestOnGun = GetBarrelOnGun(*Tank, Barrel);

	// The stroke follows the barrel: about 0.3 of its length, a third of the part outside the mantlet.
	const FBox BarrelBox = M20->Parts[Barrel].Mesh->GetBoundingBox();
	const float BarrelLength = BarrelBox.Max.X - BarrelBox.Min.X;
	const float Stroke = Tank->GetGunRecoilStroke();
	TestTrue(FString::Printf(TEXT("The stroke is about 0.3 of the barrel (%.1f of %.1f cm)"), Stroke, BarrelLength),
		Stroke > BarrelLength * 0.27f && Stroke < BarrelLength * 0.33f);

	Tank->PlayGunRecoil();
	TestTrue(TEXT("The stroke starts at rest"), GetBarrelOnGun(*Tank, Barrel).Equals(RestOnGun, RestTolerance));
	Tank->UpdateGunRecoil(Step);
	TestTrue(FString::Printf(TEXT("One frame on it is fully back (%.2f of %.2f cm)"), Tank->GetGunRecoilOffset(), Stroke),
		FMath::IsNearlyEqual(Tank->GetGunRecoilOffset(), Stroke, 0.01f));
	const FTransform Back = GetBarrelOnGun(*Tank, Barrel);
	const FVector Moved = Back.GetLocation() - RestOnGun.GetLocation();
	TestTrue(FString::Printf(TEXT("Back along the bore (%s)"), *Moved.ToString()),
		Moved.Equals(FVector(-Stroke * M20->MeshScale.X, 0.f, 0.f), 0.05f));
	TestTrue(TEXT("Without turning on the gun"), Back.GetRotation().Equals(RestOnGun.GetRotation(), 1.0e-4f));

	// WiC's curve, as shares of the stroke after the shot: still back at 0.1 s, half at 0.3 s, a sixth or less at 0.4 s.
	float Time = Step;
	const auto RunTo = [&Time, Tank](float Seconds)
	{
		Tank->UpdateGunRecoil(Seconds - Time);
		Time = Seconds;
		return Tank->GetGunRecoilOffset();
	};
	const float AtTenth = RunTo(0.1f);
	TestTrue(FString::Printf(TEXT("Still back at 0.1 s (%.2f cm)"), AtTenth), AtTenth > Stroke * 0.95f);
	const float AtThreeTenths = RunTo(0.3f);
	TestTrue(FString::Printf(TEXT("About half way out at 0.3 s (%.2f cm)"), AtThreeTenths),
		AtThreeTenths > Stroke * 0.4f && AtThreeTenths < Stroke * 0.6f);
	const float AtFourTenths = RunTo(0.4f);
	TestTrue(FString::Printf(TEXT("Nearly home at 0.4 s (%.2f cm)"), AtFourTenths), AtFourTenths > 0.f && AtFourTenths < Stroke * 0.25f);
	TestEqual(TEXT("Home at 0.5 s"), RunTo(0.5f + Step), 0.f);

	TestTrue(TEXT("Everything settles"), Settle(*Tank));
	TestEqual(TEXT("It is out again"), Tank->GetGunRecoilOffset(), 0.f);
	TestTrue(TEXT("Exactly where it rested"), GetBarrel(*Tank, Barrel).Equals(Rest, RestTolerance));
	return true;
}

/**
 * The gun's jump and the hull's rock on one shot (#218): the gun kicks up by about JumpDegrees and swings back past rest a
 * little; the hull lifts on the side the gun points to, hinged on the opposite bottom edge so that nothing goes below where it
 * rests; the actor and its collision do not move; and all of it settles at exactly its rest.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSRecoilRockTest, "WorldDivide.Combat.Recoil.Rock", RTSTests::TestFlags)

bool FRTSRecoilRockTest::RunTest(const FString& Parameters)
{
	using namespace RTSRecoilTests;

	URTSUnitData* M20 = LoadM20(*this);
	if (!M20)
	{
		return false;
	}
	const FRTSGunRecoil& Recoil = M20->GunRecoil;
	const FRTSTestWorld TestWorld;
	ARTSUnit* Tank = TestWorld.SpawnUnit(M20, 0, FVector::ZeroVector);
	const UStaticMeshComponent* Hull = FindHull(*Tank);
	if (!TestNotNull(TEXT("The hull is drawn"), Hull))
	{
		return false;
	}
	const FBox HullBox = M20->Mesh->GetBoundingBox();
	const FTransform ActorRest = Tank->GetActorTransform();
	const FTransform HullRest = Hull->GetComponentTransform();
	const FTransform MuzzleRest = Tank->GetMuzzleTransform();

	// The gun ahead, then to the right and behind: the hull lifts that way, the edge across from it stays down.
	for (const float Yaw : { 0.f, 90.f, 180.f })
	{
		Tank->SetAimOverride(FVector2D(Yaw, 0.f));
		for (float Time = 0.f; Time < 5.f; Time += Step)
		{
			Tank->UpdateTurret(Step);
		}
		const FVector Toward = FRotator(0.f, Yaw, 0.f).Vector();
		const FVector Lifted = HullBox.GetCenter() + FVector(Toward.X * HullBox.GetExtent().X, Toward.Y * HullBox.GetExtent().Y, -HullBox.GetExtent().Z);
		const FVector Hinged = HullBox.GetCenter() + FVector(-Toward.X * HullBox.GetExtent().X, -Toward.Y * HullBox.GetExtent().Y, -HullBox.GetExtent().Z);

		Tank->PlayGunRecoil();
		float MostJump = 0.f;
		float LeastJump = 0.f;
		float MostLift = 0.f;
		float LowestHinge = 0.f;
		bool bActorStill = true;
		for (float Time = 0.f; Time < SettleLimit && Tank->IsRecoiling(); Time += Step)
		{
			Tank->UpdateGunRecoil(Step);
			MostJump = FMath::Max(MostJump, Tank->GetGunJump());
			LeastJump = FMath::Min(LeastJump, Tank->GetGunJump());
			const FTransform HullNow = Hull->GetComponentTransform();
			MostLift = FMath::Max(MostLift, HullNow.TransformPosition(Lifted).Z - HullRest.TransformPosition(Lifted).Z);
			LowestHinge = FMath::Min(LowestHinge, HullNow.TransformPosition(Hinged).Z - HullRest.TransformPosition(Hinged).Z);
			bActorStill &= Tank->GetActorTransform().Equals(ActorRest, 0.f);
		}
		const FString Gun = FString::Printf(TEXT("Gun at %.0f degrees"), Yaw);
		TestTrue(FString::Printf(TEXT("%s: the gun jumps up by %.2f degrees (%.2f)"), *Gun, MostJump, Recoil.JumpDegrees),
			FMath::IsNearlyEqual(MostJump, Recoil.JumpDegrees, Recoil.JumpDegrees * 0.15f));
		TestTrue(FString::Printf(TEXT("%s: and swings back past rest a little (%.2f degrees)"), *Gun, LeastJump),
			LeastJump < 0.f && LeastJump > -Recoil.JumpDegrees * 0.5f);
		const float ExpectedLift = FMath::Sin(FMath::DegreesToRadians(Recoil.HullRockDegrees)) * 2.f
			* FVector2D(Toward.X * HullBox.GetExtent().X, Toward.Y * HullBox.GetExtent().Y).Size();
		TestTrue(FString::Printf(TEXT("%s: the hull lifts %.1f cm on the gun's side (about %.1f)"), *Gun, MostLift, ExpectedLift),
			MostLift > ExpectedLift * 0.8f && MostLift < ExpectedLift * 1.2f);
		TestTrue(FString::Printf(TEXT("%s: the edge across from it goes no lower than it rests (%.3f cm)"), *Gun, LowestHinge),
			LowestHinge > -0.5f);
		TestTrue(FString::Printf(TEXT("%s: the actor and its collision stay put"), *Gun), bActorStill);
		TestFalse(FString::Printf(TEXT("%s: everything settles"), *Gun), Tank->IsRecoiling());
		TestTrue(FString::Printf(TEXT("%s: the hull is back at rest"), *Gun), Hull->GetComponentTransform().Equals(HullRest, RestTolerance));
		TestEqual(FString::Printf(TEXT("%s: the gun is back at its pitch"), *Gun), Tank->GetGunJump(), 0.f);
	}
	Tank->SetAimOverride(FVector2D::ZeroVector);
	for (float Time = 0.f; Time < 5.f; Time += Step)
	{
		Tank->UpdateTurret(Step);
	}
	TestTrue(TEXT("The muzzle is back where it rested"), Tank->GetMuzzleTransform().Equals(MuzzleRest, RestTolerance));
	return true;
}

/**
 * The M20 firing again and again (#218): its own shots each start a recoil; shots that come before it has settled carry on
 * from where the barrel, the gun and the hull are, so none of them jumps and the barrel never goes further back than a
 * stroke; after the last shot everything comes back to its rest.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSRecoilRepeatedTest, "WorldDivide.Combat.Recoil.Repeated", RTSTests::TestFlags)

bool FRTSRecoilRepeatedTest::RunTest(const FString& Parameters)
{
	using namespace RTSRecoilTests;

	URTSUnitData* M20 = LoadM20(*this);
	if (!M20)
	{
		return false;
	}
	const int32 Barrel = FindBarrel(*M20);
	const FRTSTestWorld TestWorld;
	ARTSUnit* Tank = TestWorld.SpawnUnit(M20, 0, FVector::ZeroVector);
	TestWorld.SpawnUnit(MakeTarget(), 1, FVector(2000.f, 0.f, 0.f));
	const UStaticMeshComponent* Hull = FindHull(*Tank);
	// On the gun: the gun elevates toward the target while it fires.
	const FTransform Rest = GetBarrelOnGun(*Tank, Barrel);
	const FTransform HullRest = Hull ? Hull->GetComponentTransform() : FTransform::Identity;

	// Its own gun: each shot is a recoil.
	int32 Shots = 0;
	float MostBack = 0.f;
	Tank->OnShot().AddLambda([&Shots](const FRTSShotEvent&) { ++Shots; });
	const float Seconds = M20->Weapon->ReloadSeconds * 3.5f;
	for (float Time = 0.f; Time < Seconds; Time += Step)
	{
		Tank->UpdateTurret(Step);
		Tank->GetWeaponComponent()->UpdateWeapon(Step);
		Tank->UpdateGunRecoil(Step);
		MostBack = FMath::Max(MostBack, Tank->GetGunRecoilOffset());
	}
	TestTrue(FString::Printf(TEXT("It fired again and again (%d shots)"), Shots), Shots >= 3);
	TestTrue(FString::Printf(TEXT("Each shot drove the barrel back (%.2f cm)"), MostBack), MostBack > Tank->GetGunRecoilStroke() * 0.95f);

	// Shots faster than the recoil settles, as quick as every couple of frames: at the moment of each, nothing jumps.
	float BiggestJump = 0.f;
	MostBack = 0.f;
	const float Intervals[] = { 0.03f, 0.1f, 0.2f, 0.05f, 0.35f, 0.08f, 0.15f, 0.6f, 0.04f };
	for (const float Interval : Intervals)
	{
		const float Back = Tank->GetGunRecoilOffset();
		const float Jump = Tank->GetGunJump();
		const FVector2D Rock = Tank->GetHullRock();
		Tank->PlayGunRecoil();
		BiggestJump = FMath::Max3(BiggestJump, FMath::Abs(Tank->GetGunRecoilOffset() - Back),
			FMath::Max(FMath::Abs(Tank->GetGunJump() - Jump), static_cast<float>((Tank->GetHullRock() - Rock).Size())));
		for (float Time = 0.f; Time < Interval; Time += Step)
		{
			Tank->UpdateGunRecoil(Step);
			MostBack = FMath::Max(MostBack, Tank->GetGunRecoilOffset());
		}
	}
	TestTrue(FString::Printf(TEXT("A shot never makes the barrel, gun or hull jump (%.5f)"), BiggestJump), BiggestJump <= 1.0e-4f);
	TestTrue(FString::Printf(TEXT("Nor the barrel go further back than a stroke (%.2f cm)"), MostBack), MostBack <= Tank->GetGunRecoilStroke() + KINDA_SMALL_NUMBER);

	TestTrue(TEXT("After the last shot everything settles"), Settle(*Tank));
	TestEqual(TEXT("The barrel is out again"), Tank->GetGunRecoilOffset(), 0.f);
	const FTransform End = GetBarrelOnGun(*Tank, Barrel);
	TestTrue(FString::Printf(TEXT("Back where it rested on the gun (%s)"), *End.GetRelativeTransform(Rest).ToString()), End.Equals(Rest, RestTolerance));
	TestTrue(TEXT("The hull too"), Hull && Hull->GetComponentTransform().Equals(HullRest, RestTolerance));
	return true;
}

/**
 * Turret and gun while the tank recoils (#218): they traverse and elevate frame for frame as without recoil, the barrel stays
 * on the gun's bore through the jump and the rock, and held at full stroke it is a stroke back along the raised bore.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSRecoilTrackingTest, "WorldDivide.Combat.Recoil.Tracking", RTSTests::TestFlags)

bool FRTSRecoilTrackingTest::RunTest(const FString& Parameters)
{
	using namespace RTSRecoilTests;

	URTSUnitData* M20 = LoadM20(*this);
	if (!M20)
	{
		return false;
	}
	const int32 Barrel = FindBarrel(*M20);
	const FRTSTestWorld TestWorld;
	ARTSUnit* Tank = TestWorld.SpawnUnit(M20, 0, FVector::ZeroVector);
	const FVector2D Aim(60.f, 10.f);
	const float Seconds = FMath::Max(Aim.X / M20->TurretTurnRate, Aim.Y / M20->GunElevationRate) + 0.5f;

	// The same traverse and elevation twice, the second time recoiling all the way.
	TArray<FVector2D> Poses[2];
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		Tank->SetAimOverride(FVector2D::ZeroVector);
		for (float Time = 0.f; Time < Seconds; Time += Step)
		{
			Tank->UpdateTurret(Step);
		}
		TestTrue(TEXT("Settled before the pass"), Settle(*Tank));
		Tank->SetAimOverride(Aim);
		int32 Frame = 0;
		for (float Time = 0.f; Time < Seconds; Time += Step, ++Frame)
		{
			if (Pass == 1 && Frame % 6 == 0)
			{
				Tank->PlayGunRecoil();
			}
			Tank->UpdateTurret(Step);
			Tank->UpdateGunRecoil(Step);
			Poses[Pass].Add(FVector2D(Tank->GetTurretYaw(), Tank->GetGunPitch()));
			if (Pass == 1)
			{
				const FTransform OnGun = GetBarrelOnGun(*Tank, Barrel);
				if (!OnGun.GetRotation().IsIdentity(1.0e-4f) || !FMath::IsNearlyZero(OnGun.GetLocation().Y, 0.01f)
					|| !FMath::IsNearlyZero(FMath::Abs(OnGun.GetLocation().Z), 0.01f))
				{
					AddError(FString::Printf(TEXT("Frame %d: the barrel left the gun's bore (%s)"), Frame, *OnGun.ToString()));
					break;
				}
			}
		}
	}
	TestTrue(TEXT("The recoil did not hold the turret or the gun back"), Poses[0] == Poses[1]);
	TestTrue(FString::Printf(TEXT("The turret came round (%.1f degrees)"), Tank->GetTurretYaw()), FMath::IsNearlyEqual(Tank->GetTurretYaw(), Aim.X, 0.5f));
	TestTrue(FString::Printf(TEXT("The gun rose (%.1f degrees)"), Tank->GetGunPitch()), FMath::IsNearlyEqual(Tank->GetGunPitch(), Aim.Y, 0.5f));

	// Held at full stroke, for the captures: a stroke back on the raised gun; let go, all of it back at rest.
	TestTrue(TEXT("Settled before the hold"), Settle(*Tank));
	const FTransform Rest = GetBarrel(*Tank, Barrel);
	const FTransform RestOnGun = GetBarrelOnGun(*Tank, Barrel);
	Tank->SetGunRecoilHold(M20->GunRecoil.RecoilSeconds);
	const FVector Moved = GetBarrelOnGun(*Tank, Barrel).GetLocation() - RestOnGun.GetLocation();
	TestTrue(FString::Printf(TEXT("Held at full stroke, %s back along the bore"), *Moved.ToString()),
		Moved.Equals(FVector(-Tank->GetGunRecoilStroke() * M20->MeshScale.X, 0.f, 0.f), 0.05f));
	Tank->UpdateGunRecoil(1.f);
	TestTrue(TEXT("It stays held"), FMath::IsNearlyEqual(Tank->GetGunRecoilOffset(), Tank->GetGunRecoilStroke(), 0.01f));
	Tank->SetGunRecoilHold({});
	TestFalse(TEXT("Let go, it is at rest"), Tank->IsRecoiling());
	TestTrue(TEXT("Exactly where it rested"), GetBarrel(*Tank, Barrel).Equals(Rest, RestTolerance));
	return true;
}

/**
 * The rock is drawn only (#219): an M20 rocking and one whose data has no rock, at the gun's 0, 90 and 180 degrees, keep
 * their actor and collision box exactly where they rest through every frame of a shot; and on the rocking one the turret,
 * the tracks and every wheel stay where they sit on the hull, so nothing opens a gap between them.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSRecoilRockVisualOnlyTest, "WorldDivide.Combat.Recoil.RockVisualOnly", RTSTests::TestFlags)

bool FRTSRecoilRockVisualOnlyTest::RunTest(const FString& Parameters)
{
	using namespace RTSRecoilTests;

	URTSUnitData* M20 = LoadM20(*this);
	if (!M20)
	{
		return false;
	}
	URTSUnitData* Still = DuplicateObject<URTSUnitData>(M20, GetTransientPackage());
	Still->GunRecoil.HullRockDegrees = 0.f;
	const FRTSTestWorld TestWorld;
	ARTSUnit* Tanks[2] = { TestWorld.SpawnUnit(M20, 0, FVector::ZeroVector), TestWorld.SpawnUnit(Still, 0, FVector(3000.f, 0.f, 0.f)) };
	const UStaticMeshComponent* Hull = FindHull(*Tanks[0]);
	TArray<UInstancedStaticMeshComponent*> WheelSets;
	Tanks[0]->GetComponents(WheelSets);
	if (!TestNotNull(TEXT("The hull is drawn"), Hull) || !TestFalse(TEXT("The M20 has wheels"), WheelSets.IsEmpty()))
	{
		return false;
	}
	// The parts on the hull and the turret: the gun's own jump and the barrel's stroke are the recoil (Recoil.Rock).
	TArray<int32> Parts;
	for (int32 Index = 0; Index < M20->Parts.Num(); ++Index)
	{
		if (M20->Parts[Index].MountedOn != ERTSVehiclePartMount::Gun)
		{
			Parts.Add(Index);
		}
	}

	for (const float Yaw : { 0.f, 90.f, 180.f })
	{
		const FString Gun = FString::Printf(TEXT("Gun at %.0f degrees"), Yaw);
		FTransform ActorRest[2];
		FTransform CollisionRest[2];
		FBox BoundsRest[2];
		for (int32 Tank = 0; Tank < 2; ++Tank)
		{
			Tanks[Tank]->SetAimOverride(FVector2D(Yaw, 0.f));
			for (float Time = 0.f; Time < 5.f; Time += Step)
			{
				Tanks[Tank]->UpdateTurret(Step);
			}
			Tanks[Tank]->UpdateRunningGear();
			const UBoxComponent* Collision = Tanks[Tank]->FindComponentByClass<UBoxComponent>();
			ActorRest[Tank] = Tanks[Tank]->GetActorTransform();
			CollisionRest[Tank] = Collision->GetComponentTransform();
			BoundsRest[Tank] = Collision->Bounds.GetBox();
		}
		TestTrue(FString::Printf(TEXT("%s: both collision boxes are the same size"), *Gun),
			BoundsRest[0].GetExtent().Equals(BoundsRest[1].GetExtent(), 0.f));

		// Where everything sits on the hull at rest: the parts, and each wheel instance as drawn.
		const FTransform HullRest = Hull->GetComponentTransform();
		TArray<FTransform> PartsRest;
		for (const int32 Part : Parts)
		{
			FTransform Transform;
			Tanks[0]->GetPartTransform(Part, Transform);
			PartsRest.Add(Transform.GetRelativeTransform(HullRest));
		}
		auto GetWheelsOnHull = [&WheelSets](const FTransform& HullNow)
		{
			TArray<FTransform> OnHull;
			for (const UInstancedStaticMeshComponent* Set : WheelSets)
			{
				for (int32 Instance = 0; Instance < Set->GetInstanceCount(); ++Instance)
				{
					FTransform Transform;
					Set->GetInstanceTransform(Instance, Transform, /*bWorldSpace*/ true);
					OnHull.Add(Transform.GetRelativeTransform(HullNow));
				}
			}
			return OnHull;
		};
		const TArray<FTransform> WheelsRest = GetWheelsOnHull(HullRest);

		Tanks[0]->PlayGunRecoil();
		Tanks[1]->PlayGunRecoil();
		bool bStill = true;
		float MostRock = 0.f;
		float MostPartGap = 0.f;
		float MostWheelGap = 0.f;
		for (float Time = 0.f; Time < SettleLimit && (Tanks[0]->IsRecoiling() || Tanks[1]->IsRecoiling()); Time += Step)
		{
			for (int32 Tank = 0; Tank < 2; ++Tank)
			{
				Tanks[Tank]->UpdateGunRecoil(Step);
				Tanks[Tank]->UpdateRunningGear();
				const UBoxComponent* Collision = Tanks[Tank]->FindComponentByClass<UBoxComponent>();
				bStill &= Tanks[Tank]->GetActorTransform().Equals(ActorRest[Tank], 0.f)
					&& Collision->GetComponentTransform().Equals(CollisionRest[Tank], 0.f) && Collision->Bounds.GetBox() == BoundsRest[Tank];
			}
			const FTransform HullNow = Hull->GetComponentTransform();
			MostRock = FMath::Max(MostRock, static_cast<float>(Tanks[0]->GetHullRock().Size()));
			for (int32 Index = 0; Index < Parts.Num(); ++Index)
			{
				FTransform Transform;
				Tanks[0]->GetPartTransform(Parts[Index], Transform);
				MostPartGap = FMath::Max(MostPartGap,
					static_cast<float>(FVector::Dist(Transform.GetRelativeTransform(HullNow).GetLocation(), PartsRest[Index].GetLocation())));
			}
			const TArray<FTransform> Wheels = GetWheelsOnHull(HullNow);
			for (int32 Index = 0; Index < Wheels.Num(); ++Index)
			{
				MostWheelGap = FMath::Max(MostWheelGap,
					static_cast<float>(FVector::Dist(Wheels[Index].GetLocation(), WheelsRest[Index].GetLocation())));
			}
		}
		TestTrue(FString::Printf(TEXT("%s: the hull rocked (%.2f degrees)"), *Gun, MostRock), MostRock > M20->GunRecoil.HullRockDegrees * 0.5f);
		TestTrue(FString::Printf(TEXT("%s: the actors and their collision stay exactly where they rest, rocking or not"), *Gun), bStill);
		TestTrue(FString::Printf(TEXT("%s: the turret and the tracks stay on the hull (%.4f cm)"), *Gun, MostPartGap), MostPartGap < RestTolerance);
		TestTrue(FString::Printf(TEXT("%s: every wheel stays on the hull (%.4f cm)"), *Gun, MostWheelGap), MostWheelGap < RestTolerance);
	}
	return true;
}

/**
 * The recoil plays only where it can be seen (#219): within rts.Vehicle.RecoilDistance of a camera on this machine, the M20's
 * LOD2 switch at 110 m; past it, or with no camera at all, the tank skips its gun's recoil and the hull's rock. 0 plays it
 * everywhere.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSRecoilDistanceTest, "WorldDivide.Combat.Recoil.Distance", RTSTests::TestFlags)

bool FRTSRecoilDistanceTest::RunTest(const FString& Parameters)
{
	using namespace RTSRecoilTests;

	URTSUnitData* M20 = LoadM20(*this);
	IConsoleVariable* RecoilDistance = IConsoleManager::Get().FindConsoleVariable(TEXT("rts.Vehicle.RecoilDistance"));
	if (!M20 || !TestNotNull(TEXT("rts.Vehicle.RecoilDistance exists"), RecoilDistance))
	{
		return false;
	}
	const float Saved = RecoilDistance->GetFloat();
	const FRTSTestWorld TestWorld;
	const ARTSUnit* Tank = TestWorld.SpawnUnit(M20, 0, FVector(1000.f, 2000.f, 0.f));
	const FVector Location = Tank->GetActorLocation();
	const FVector Near = Location + FVector(-6000.f, 0.f, 8000.f);
	const FVector Far = Location + FVector(-10000.f, 0.f, 15000.f);

	RecoilDistance->Set(11000.f, ECVF_SetByConsole);
	TestTrue(TEXT("Seen from 100 m"), Tank->IsRecoilSeen({ Near }));
	TestFalse(TEXT("Not from 180 m, the default RTS view"), Tank->IsRecoilSeen({ Far }));
	TestTrue(TEXT("Seen when one of the cameras is near"), Tank->IsRecoilSeen({ Far, Near }));
	TestFalse(TEXT("Not without a camera"), Tank->IsRecoilSeen({}));
	RecoilDistance->Set(0.f, ECVF_SetByConsole);
	TestTrue(TEXT("At 0, from anywhere"), Tank->IsRecoilSeen({ Far }));
	TestTrue(TEXT("At 0, even without a camera"), Tank->IsRecoilSeen({}));
	RecoilDistance->Set(Saved, ECVF_SetByConsole);

	// The drive's body motion (#297) goes by the same distance, and a dedicated server never shows it.
	TestTrue(TEXT("Body motion: on a client within the distance"), Tank->ShowsBodyMotion(NM_Client, { Near }));
	TestTrue(TEXT("Body motion: on a listen server within the distance"), Tank->ShowsBodyMotion(NM_ListenServer, { Near }));
	TestFalse(TEXT("Body motion: not beyond it"), Tank->ShowsBodyMotion(NM_Client, { Far }));
	TestFalse(TEXT("Body motion: never on a dedicated server"), Tank->ShowsBodyMotion(NM_DedicatedServer, { Near }));
	return true;
}

/**
 * The hull moves with the drive (#297): it pitches with the sign of the acceleration, nose up pulling away and down braking,
 * rolls with the sign of the turn, right side up turning right, and settles to exactly level driving straight at a steady
 * speed. Where it is not shown (ShowsBodyMotion: a dedicated server, beyond the recoil distance) it never moves.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSRecoilBodyMotionTest, "WorldDivide.Combat.Recoil.BodyMotion", RTSTests::TestFlags)

bool FRTSRecoilBodyMotionTest::RunTest(const FString& Parameters)
{
	using namespace RTSRecoilTests;

	URTSUnitData* M20 = LoadM20(*this);
	if (!M20 || !TestTrue(TEXT("The M20's hull moves with its drive"), M20->BodyMotion.HasHullMotion()))
	{
		return false;
	}
	const FRTSTestWorld TestWorld;
	ARTSUnit* Tank = TestWorld.SpawnUnit(M20, 0, FVector::ZeroVector);
	float Speed = 0.f;
	// Drives Seconds at a steady YawRate, the speed ramping by Acceleration (cm/s²) up to Top; the extreme sway it reached.
	auto Drive = [Tank, &Speed](float Seconds, float Acceleration, float Top, float YawRate, bool bShown = true)
	{
		FVector2D Most = FVector2D::ZeroVector;
		for (float Time = 0.f; Time < Seconds; Time += Step)
		{
			Speed = Acceleration >= 0.f ? FMath::Min(Speed + Acceleration * Step, Top) : FMath::Max(Speed + Acceleration * Step, Top);
			Tank->StepBodyMotion(Step, Speed, YawRate, bShown);
			const FVector2D Sway = Tank->GetHullSway();
			Most.X = FMath::Abs(Sway.X) > FMath::Abs(Most.X) ? Sway.X : Most.X;
			Most.Y = FMath::Abs(Sway.Y) > FMath::Abs(Most.Y) ? Sway.Y : Most.Y;
		}
		return Most;
	};
	auto TestLevel = [this, Tank](const TCHAR* What)
	{
		TestTrue(FString::Printf(TEXT("%s: level"), What), Tank->GetHullSway().IsZero());
		TestFalse(FString::Printf(TEXT("%s: at rest"), What), Tank->IsBodyMoving());
	};

	Drive(0.5f, 0.f, 0.f, 0.f);
	TestLevel(TEXT("Standing"));

	const FVector2D PullAway = Drive(0.6f, 1000.f, 600.f, 0.f);
	TestTrue(FString::Printf(TEXT("Pulling away the nose lifts (%.3f deg)"), PullAway.X), PullAway.X > 0.1f);
	TestEqual(TEXT("Pulling away straight it does not roll"), PullAway.Y, 0.0);
	Drive(SettleLimit, 0.f, 600.f, 0.f);
	TestLevel(TEXT("Driving straight at a steady speed"));

	const FVector2D Braking = Drive(0.24f, -2500.f, 0.f, 0.f);
	TestTrue(FString::Printf(TEXT("Braking the nose drops (%.3f deg)"), Braking.X), Braking.X < -0.1f);
	TestTrue(TEXT("No further than MaxDegrees, past which it may swing by a little"), Braking.X >= -M20->BodyMotion.MaxDegrees * 1.2f);
	Drive(SettleLimit, 0.f, 0.f, 0.f);
	TestLevel(TEXT("Stopped"));

	Drive(SettleLimit, 1000.f, 600.f, 0.f);
	const FVector2D Right = Drive(1.f, 0.f, 600.f, 30.f);
	TestTrue(FString::Printf(TEXT("Turning right the right side lifts (%.3f deg)"), Right.Y), Right.Y > 0.1f);
	Drive(SettleLimit, 0.f, 600.f, 0.f);
	TestLevel(TEXT("Straight again after the right turn"));
	const FVector2D Left = Drive(1.f, 0.f, 600.f, -30.f);
	TestTrue(FString::Printf(TEXT("Turning left the left side lifts (%.3f deg)"), Left.Y), Left.Y < -0.1f);
	Drive(SettleLimit, 0.f, 600.f, 0.f);
	TestLevel(TEXT("Straight again after the left turn"));

	// Not shown: the same drive leaves the hull still, and a hull that was moving comes to rest at once.
	Drive(SettleLimit, -2500.f, 0.f, 0.f);
	const FVector2D Unseen = Drive(1.f, 1000.f, 600.f, 30.f, false);
	TestTrue(TEXT("Not shown, pulling away into a turn moves nothing"), Unseen.IsZero());
	Drive(0.3f, -2500.f, 0.f, 30.f);
	TestTrue(TEXT("Shown, braking in a turn moves it"), Tank->IsBodyMoving());
	Drive(Step, 0.f, 0.f, 0.f, false);
	TestLevel(TEXT("Once it is no longer shown"));
	return true;
}

#endif
