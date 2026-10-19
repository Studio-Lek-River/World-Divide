#include "Combat/RTSWeaponComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSWeaponData.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
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
				M20->GunRecoil.Distance > 0.f && M20->GunRecoil.JumpDegrees > 0.f && M20->GunRecoil.HullRockDegrees > 0.f))
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
 * The M20's barrel on one shot (#218): it snaps back along the bore by GunRecoil.Distance within a frame, stays back for
 * HoldSeconds, runs out quickly at first and creeps the last of the way, and ends exactly where it rested.
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
	const FRTSGunRecoil& Recoil = M20->GunRecoil;
	const int32 Barrel = FindBarrel(*M20);
	if (!TestTrue(TEXT("The M20 has a barrel that recoils"), Barrel != INDEX_NONE))
	{
		return false;
	}
	const FRTSTestWorld TestWorld;
	ARTSUnit* Tank = TestWorld.SpawnUnit(M20, 0, FVector::ZeroVector);
	const FTransform Rest = GetBarrel(*Tank, Barrel);
	const FTransform RestOnGun = GetBarrelOnGun(*Tank, Barrel);

	Tank->PlayGunRecoil();
	TestTrue(TEXT("The stroke starts at rest"), GetBarrelOnGun(*Tank, Barrel).Equals(RestOnGun, RestTolerance));
	Tank->UpdateGunRecoil(Step);
	TestTrue(FString::Printf(TEXT("One frame on it is fully back (%.2f of %.2f cm)"), Tank->GetGunRecoilOffset(), Recoil.Distance),
		FMath::IsNearlyEqual(Tank->GetGunRecoilOffset(), Recoil.Distance, 0.01f));
	const FTransform Back = GetBarrelOnGun(*Tank, Barrel);
	const FVector Moved = Back.GetLocation() - RestOnGun.GetLocation();
	TestTrue(FString::Printf(TEXT("Back along the bore (%s)"), *Moved.ToString()),
		Moved.Equals(FVector(-Recoil.Distance * M20->MeshScale.X, 0.f, 0.f), 0.05f));
	TestTrue(TEXT("Without turning on the gun"), Back.GetRotation().Equals(RestOnGun.GetRotation(), 1.0e-4f));

	// Held, then out: half the way by 40% of the run-out, the last tenth taking over a quarter of it.
	Tank->UpdateGunRecoil(Recoil.RecoilSeconds + Recoil.HoldSeconds - Step - 0.001f);
	TestTrue(TEXT("It stays back while held"), FMath::IsNearlyEqual(Tank->GetGunRecoilOffset(), Recoil.Distance, 0.01f));
	Tank->UpdateGunRecoil(0.001f + Recoil.ReturnSeconds * 0.4f);
	const float AtTwoFifths = Tank->GetGunRecoilOffset();
	TestTrue(FString::Printf(TEXT("40%% of the run-out has taken it half way (%.2f cm back)"), AtTwoFifths),
		AtTwoFifths < Recoil.Distance * 0.5f && AtTwoFifths > Recoil.Distance * 0.3f);
	float Time = Recoil.ReturnSeconds * 0.4f;
	float TenthLeftAt = -1.f;
	for (; Tank->GetGunRecoilOffset() > 0.f && Time < SettleLimit; Time += Step)
	{
		Tank->UpdateGunRecoil(Step);
		if (TenthLeftAt < 0.f && Tank->GetGunRecoilOffset() <= Recoil.Distance * 0.1f)
		{
			TenthLeftAt = Time;
		}
	}
	TestTrue(FString::Printf(TEXT("The buffer slows the last tenth (%.2f of %.2f s)"), Recoil.ReturnSeconds - TenthLeftAt, Recoil.ReturnSeconds),
		TenthLeftAt > 0.f && Recoil.ReturnSeconds - TenthLeftAt > Recoil.ReturnSeconds * 0.25f);

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
	const FRTSGunRecoil& Recoil = M20->GunRecoil;
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
	TestTrue(FString::Printf(TEXT("Each shot drove the barrel back (%.2f cm)"), MostBack), MostBack > Recoil.Distance * 0.95f);

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
	TestTrue(FString::Printf(TEXT("Nor the barrel go further back than a stroke (%.2f cm)"), MostBack), MostBack <= Recoil.Distance + KINDA_SMALL_NUMBER);

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
		Moved.Equals(FVector(-M20->GunRecoil.Distance * M20->MeshScale.X, 0.f, 0.f), 0.05f));
	Tank->UpdateGunRecoil(1.f);
	TestTrue(TEXT("It stays held"), FMath::IsNearlyEqual(Tank->GetGunRecoilOffset(), M20->GunRecoil.Distance, 0.01f));
	Tank->SetGunRecoilHold({});
	TestFalse(TEXT("Let go, it is at rest"), Tank->IsRecoiling());
	TestTrue(TEXT("Exactly where it rested"), GetBarrel(*Tank, Barrel).Equals(Rest, RestTolerance));
	return true;
}

#endif
