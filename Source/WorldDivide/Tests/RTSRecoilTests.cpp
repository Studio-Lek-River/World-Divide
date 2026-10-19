#include "Combat/RTSWeaponComponent.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSWeaponData.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSUnit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	constexpr float Step = 1.f / 60.f;
	/** How close the barrel has to come back to where it rests, in cm. */
	constexpr float RestTolerance = 0.01f;

	URTSUnitData* LoadM20(FAutomationTestBase& Test)
	{
		URTSUnitData* M20 = LoadObject<URTSUnitData>(nullptr, TEXT("/Game/Data/Units/DA_TestM20Guardian.DA_TestM20Guardian"));
		if (!Test.TestNotNull(TEXT("DA_TestM20Guardian loads"), M20) || !Test.TestNotNull(TEXT("It has a weapon"), M20->Weapon.Get())
			|| !Test.TestTrue(TEXT("Its gun recoils"), M20->GunRecoil.Distance > 0.f))
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

	/** The barrel on the gun, from its muzzle: the hull's own fire kick, which the test does not run out, moves both alike. */
	FTransform GetBarrelOnGun(const ARTSUnit& Unit, int32 Barrel)
	{
		return GetBarrel(Unit, Barrel).GetRelativeTransform(Unit.GetMuzzleTransform());
	}

	URTSUnitData* MakeTarget()
	{
		URTSUnitData* Data = NewObject<URTSUnitData>();
		Data->MaxHealth = 1.0e9f;
		return Data;
	}
}

/**
 * The M20's barrel on one shot (#218): it slides back along the bore by GunRecoil.Distance in RecoilSeconds, the rest of the gun
 * staying put, and runs out to exactly where it rested.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSRecoilStrokeTest, "WorldDivide.Combat.Recoil.Stroke", TestFlags)

bool FRTSRecoilStrokeTest::RunTest(const FString& Parameters)
{
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
	const FTransform Muzzle = Tank->GetMuzzleTransform();

	Tank->PlayGunRecoil();
	TestTrue(TEXT("The stroke starts at rest"), GetBarrel(*Tank, Barrel).Equals(Rest, RestTolerance));
	float Time = 0.f;
	for (; Time + Step <= Recoil.RecoilSeconds + KINDA_SMALL_NUMBER; Time += Step)
	{
		Tank->UpdateGunRecoil(Step);
	}
	Tank->UpdateGunRecoil(Recoil.RecoilSeconds - Time);
	const FTransform Back = GetBarrel(*Tank, Barrel);
	const FVector Moved = Back.GetLocation() - Rest.GetLocation();
	TestTrue(FString::Printf(TEXT("At full stroke it is %.2f cm back (%.2f)"), Moved.Size(), Recoil.Distance),
		FMath::IsNearlyEqual(Moved.Size(), Recoil.Distance * M20->MeshScale.X, 0.5f));
	TestTrue(TEXT("Back along the bore"), FVector::DotProduct(Moved.GetSafeNormal(), -Muzzle.GetUnitAxis(EAxis::X)) > 0.9999f);
	TestTrue(TEXT("Without turning"), Back.GetRotation().Equals(Rest.GetRotation(), 1.0e-4f));
	TestTrue(TEXT("The muzzle the shots leave from stays put"), Tank->GetMuzzleTransform().Equals(Muzzle, RestTolerance));

	for (Time = 0.f; Time < Recoil.ReturnSeconds + Step; Time += Step)
	{
		Tank->UpdateGunRecoil(Step);
	}
	TestEqual(TEXT("It is out again"), Tank->GetGunRecoilOffset(), 0.f);
	TestTrue(TEXT("Exactly where it rested"), GetBarrel(*Tank, Barrel).Equals(Rest, RestTolerance));
	return true;
}

/**
 * The M20 firing again and again (#218): its own shots each start a stroke, shots that come before the barrel is out again
 * start the next stroke where it is, so it never jumps or goes further back than a full stroke, and after the last shot it
 * comes back to its rest within RestTolerance.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSRecoilRepeatedTest, "WorldDivide.Combat.Recoil.Repeated", TestFlags)

bool FRTSRecoilRepeatedTest::RunTest(const FString& Parameters)
{
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
	const FTransform Rest = GetBarrelOnGun(*Tank, Barrel);

	// Its own gun: each shot is a stroke.
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

	// Shots faster than a stroke, as quick as every few frames.
	const float FastestBack = 2.f * Recoil.Distance / Recoil.RecoilSeconds * Step;
	float Previous = Tank->GetGunRecoilOffset();
	float BiggestStep = 0.f;
	MostBack = 0.f;
	const float Intervals[] = { 0.03f, 0.1f, 0.2f, 0.05f, 0.35f, 0.08f, 0.15f };
	for (const float Interval : Intervals)
	{
		Tank->PlayGunRecoil();
		BiggestStep = FMath::Max(BiggestStep, FMath::Abs(Tank->GetGunRecoilOffset() - Previous));
		Previous = Tank->GetGunRecoilOffset();
		for (float Time = 0.f; Time < Interval; Time += Step)
		{
			Tank->UpdateGunRecoil(Step);
			BiggestStep = FMath::Max(BiggestStep, FMath::Abs(Tank->GetGunRecoilOffset() - Previous));
			MostBack = FMath::Max(MostBack, Tank->GetGunRecoilOffset());
			Previous = Tank->GetGunRecoilOffset();
		}
	}
	TestTrue(FString::Printf(TEXT("A shot never makes it jump (%.2f cm in a frame, %.2f the fastest stroke)"), BiggestStep, FastestBack),
		BiggestStep <= FastestBack + KINDA_SMALL_NUMBER);
	TestTrue(FString::Printf(TEXT("Nor go further back than a stroke (%.2f cm)"), MostBack), MostBack <= Recoil.Distance + KINDA_SMALL_NUMBER);

	for (float Time = 0.f; Time < Recoil.GetSeconds() + Step; Time += Step)
	{
		Tank->UpdateGunRecoil(Step);
	}
	TestEqual(TEXT("After the last shot it is out again"), Tank->GetGunRecoilOffset(), 0.f);
	const FTransform End = GetBarrelOnGun(*Tank, Barrel);
	const FVector Off = End.GetLocation() - Rest.GetLocation();
	TestTrue(FString::Printf(TEXT("Back where it rested (%.4f cm off)"), Off.Size()), End.Equals(Rest, RestTolerance));
	return true;
}

/**
 * Turret and gun while the barrel recoils (#218): they traverse and elevate frame for frame as without recoil, the barrel turns
 * with the gun, and the muzzle the shots leave from does not move with the barrel.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSRecoilTrackingTest, "WorldDivide.Combat.Recoil.Tracking", TestFlags)

bool FRTSRecoilTrackingTest::RunTest(const FString& Parameters)
{
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

	// The same traverse and elevation twice, the second time with the barrel recoiling all the way.
	TArray<FVector2D> Poses[2];
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		Tank->SetAimOverride(FVector2D::ZeroVector);
		for (float Time = 0.f; Time < Seconds; Time += Step)
		{
			Tank->UpdateTurret(Step);
		}
		Tank->UpdateGunRecoil(M20->GunRecoil.GetSeconds());
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
				const FTransform BarrelNow = GetBarrel(*Tank, Barrel);
				const FTransform Muzzle = Tank->GetMuzzleTransform();
				if (FVector::DotProduct(BarrelNow.GetUnitAxis(EAxis::X), Muzzle.GetUnitAxis(EAxis::X)) < 0.9999f)
				{
					AddError(FString::Printf(TEXT("Frame %d: the barrel does not point along the gun"), Frame));
					break;
				}
			}
		}
	}
	TestTrue(TEXT("The recoiling barrel did not hold the turret or the gun back"), Poses[0] == Poses[1]);
	TestTrue(FString::Printf(TEXT("The turret came round (%.1f degrees)"), Tank->GetTurretYaw()), FMath::IsNearlyEqual(Tank->GetTurretYaw(), Aim.X, 0.5f));
	TestTrue(FString::Printf(TEXT("The gun rose (%.1f degrees)"), Tank->GetGunPitch()), FMath::IsNearlyEqual(Tank->GetGunPitch(), Aim.Y, 0.5f));

	// Held at full stroke: the barrel is back along the raised bore, the muzzle and the gun are not.
	Tank->UpdateGunRecoil(M20->GunRecoil.GetSeconds());
	const FTransform Rest = GetBarrel(*Tank, Barrel);
	const FTransform Muzzle = Tank->GetMuzzleTransform();
	Tank->SetGunRecoilHold(M20->GunRecoil.RecoilSeconds);
	const FVector Moved = GetBarrel(*Tank, Barrel).GetLocation() - Rest.GetLocation();
	TestTrue(FString::Printf(TEXT("Held at full stroke, %.2f cm back along the raised bore"), Moved.Size()),
		FMath::IsNearlyEqual(Moved.Size(), M20->GunRecoil.Distance * M20->MeshScale.X, 0.5f)
		&& FVector::DotProduct(Moved.GetSafeNormal(), -Muzzle.GetUnitAxis(EAxis::X)) > 0.9999f);
	TestTrue(TEXT("The muzzle stays where the gun's is"), Tank->GetMuzzleTransform().Equals(Muzzle, RestTolerance));
	Tank->SetGunRecoilHold({});
	Tank->UpdateGunRecoil(M20->GunRecoil.GetSeconds());
	TestTrue(TEXT("Let go, it runs out to rest"), GetBarrel(*Tank, Barrel).Equals(Rest, RestTolerance));
	return true;
}

#endif
