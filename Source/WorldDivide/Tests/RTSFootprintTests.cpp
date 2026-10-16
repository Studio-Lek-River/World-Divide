#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/RTSUnitData.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Player/RTSCameraPawn.h"
#include "Player/RTSHUD.h"
#include "SceneView.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestWorld.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitShape.h"

/**
 * A unit's collision box, selection bounds and footprint come from its hull, not from SelectionRadius: the M20, the
 * placeholder tank, infantry and a unit without a mesh, as the real data gives them.
 */
// Named rather than anonymous: unity builds merge anonymous namespaces, and other test files have helpers of the same names.
namespace RTSFootprintTests
{
	using RTSTests::TestFlags;
	using RTSTests::FRTSTestWorld;

	const TCHAR* M20Path = TEXT("/Game/Data/Units/DA_TestM20Guardian.DA_TestM20Guardian");
	const TCHAR* TankPath = TEXT("/Game/Data/Units/DA_TestTank.DA_TestTank");
	const TCHAR* InfantryPath = TEXT("/Game/Data/Units/DA_TestInfantry.DA_TestInfantry");

	/** cm of slack for float maths on boxes a few metres across. */
	constexpr float Tolerance = 1.f;

	/** The view the HUD projects through: the camera's field of view on a 1920 x 1080 screen. */
	const FIntRect Screen(0, 0, 1920, 1080);

	/** A click 1 m beside the hull. */
	constexpr float BesideHull = 100.f;

	struct FUnitCase
	{
		const TCHAR* Name;
		URTSUnitData* Data;
	};

	/** The three real units, and the same infantry with its mesh taken away. */
	TArray<FUnitCase> LoadCases(FAutomationTestBase& Test)
	{
		TArray<FUnitCase> Cases;
		for (const TPair<const TCHAR*, const TCHAR*>& Entry : { TPair<const TCHAR*, const TCHAR*>(TEXT("M20"), M20Path),
			TPair<const TCHAR*, const TCHAR*>(TEXT("Tank"), TankPath), TPair<const TCHAR*, const TCHAR*>(TEXT("Infantry"), InfantryPath) })
		{
			URTSUnitData* Data = LoadObject<URTSUnitData>(nullptr, Entry.Value);
			if (Test.TestNotNull(*FString::Printf(TEXT("%s data loads"), Entry.Key), Data))
			{
				Cases.Add({ Entry.Key, Data });
			}
		}
		URTSUnitData* NoMesh = NewObject<URTSUnitData>();
		NoMesh->MaxHealth = 100.f;
		NoMesh->SelectionRadius = 45.f;
		Cases.Add({ TEXT("No mesh"), NoMesh });
		return Cases;
	}

	/** Spawns the unit standing on the ground plane Z = 0 at Location's XY, turned by Yaw. */
	ARTSUnit* SpawnOnGround(const FRTSTestWorld& TestWorld, URTSUnitData* Data, const FVector& Location, float Yaw = 0.f)
	{
		ARTSUnit* Unit = TestWorld.SpawnUnit(Data, 0, Location);
		Unit->SetActorLocationAndRotation(FVector(Location.X, Location.Y, Unit->GetGroundOffset()), FRotator(0.f, Yaw, 0.f));
		return Unit;
	}

	FBox ToWorld(const FBox& Local, const FTransform& Transform)
	{
		return Local.TransformBy(Transform);
	}

	void GetCorners(const FBox& Local, const FTransform& Transform, TArray<FVector>& OutCorners)
	{
		OutCorners.Reset();
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			OutCorners.Add(Transform.TransformPosition(FVector((Corner & 1) ? Local.Max.X : Local.Min.X,
				(Corner & 2) ? Local.Max.Y : Local.Min.Y, (Corner & 4) ? Local.Max.Z : Local.Min.Z)));
		}
	}

	/** The RTS camera at ArmLength, at its pitch for that zoom and the given yaw, looking at Target. */
	FMatrix MakeViewProjection(const FVector& Target, float ArmLength, float Yaw)
	{
		FMinimalViewInfo View;
		View.Rotation = FRotator(GetDefault<ARTSCameraPawn>()->GetPitchForZoom(ArmLength), Yaw, 0.f);
		View.Location = Target - View.Rotation.Vector() * ArmLength;
		View.FOV = GetDefault<ARTSCameraPawn>()->GetCameraComponent()->FieldOfView;
		View.AspectRatio = static_cast<float>(Screen.Width()) / Screen.Height();
		View.bConstrainAspectRatio = true;
		FMatrix ViewMatrix;
		FMatrix ProjectionMatrix;
		FMatrix ViewProjection;
		UGameplayStatics::GetViewProjectionMatrix(View, ViewMatrix, ProjectionMatrix, ViewProjection);
		return ViewProjection;
	}

	/** The unit hit by a Visibility ray from the camera at Yaw (default pitch) to Point, as a click on Point traces. */
	AActor* TraceClick(UWorld& World, const FVector& Point, float Yaw)
	{
		const FVector Direction = FRotator(GetDefault<ARTSCameraPawn>()->GetPitchForZoom(GetDefault<ARTSCameraPawn>()->GetDefaultZoom()),
			Yaw, 0.f).Vector();
		FHitResult Hit;
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(RTSFootprintTest), false);
		return World.LineTraceSingleByChannel(Hit, Point - Direction * 10000.f, Point + Direction * 1.f, ECC_Visibility, Params)
			? Hit.GetActor()
			: nullptr;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSUnitFootprintTest, "WorldDivide.Units.Footprint", TestFlags)

	bool FRTSUnitFootprintTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		for (const FUnitCase& Case : LoadCases(*this))
		{
			const URTSUnitData& Data = *Case.Data;
			ARTSUnit* Unit = SpawnOnGround(TestWorld, Case.Data, FVector::ZeroVector);
			const FRTSUnitShape& Shape = Unit->GetShape();
			const FTransform Transform = Unit->GetActorTransform();

			// The collision box is the hull at mesh scale, with the capsule's responses.
			const FBox HullBox = RTSUnitShape::GetHullBox(Data);
			const FVector HullExtent = HullBox.IsValid ? HullBox.GetExtent() * Data.MeshScale : FVector(Data.SelectionRadius);
			const UBoxComponent* Box = Cast<UBoxComponent>(Unit->GetRootComponent());
			if (!TestNotNull(*FString::Printf(TEXT("%s: the root is a box"), Case.Name), Box))
			{
				continue;
			}
			TestTrue(*FString::Printf(TEXT("%s: the box is the hull (%s against %s)"), Case.Name,
				*Box->GetScaledBoxExtent().ToCompactString(), *HullExtent.ToCompactString()), Box->GetScaledBoxExtent().Equals(HullExtent, Tolerance));
			TestTrue(*FString::Printf(TEXT("%s: it keeps the Pawn responses"), Case.Name),
				Box->GetCollisionObjectType() == ECC_Pawn && Box->GetCollisionResponseToChannel(ECC_WorldStatic) == ECR_Ignore
				&& Box->GetCollisionResponseToChannel(ECC_WorldDynamic) == ECR_Ignore && Box->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Overlap
				&& Box->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block && !Box->GetGenerateOverlapEvents()
				&& !Box->CanEverAffectNavigation());

			// Half the hull's height above the ground, and the hull itself on the ground, centred on the actor.
			TestNearlyEqual(*FString::Printf(TEXT("%s: the ground offset is half the hull's height"), Case.Name),
				Unit->GetGroundOffset(), static_cast<float>(HullExtent.Z), Tolerance);
			TestNearlyEqual(*FString::Printf(TEXT("%s: its feet are on the ground"), Case.Name),
				static_cast<float>(Unit->GetNavAgentLocation().Z), 0.f, Tolerance);
			if (HullBox.IsValid)
			{
				const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Unit->GetDefaultSubobjectByName(TEXT("Mesh")));
				const FBox DrawnHull = HullBox.TransformBy(Mesh->GetComponentTransform());
				TestNearlyEqual(*FString::Printf(TEXT("%s: the drawn hull stands on the ground"), Case.Name),
					static_cast<float>(DrawnHull.Min.Z), 0.f, Tolerance);
				TestTrue(*FString::Printf(TEXT("%s: and fills the box"), Case.Name),
					DrawnHull.Equals(ToWorld(Shape.Hull, Transform), Tolerance));
			}

			// The selection bounds: the hull and the turret with what rides on it, without the gun, from the ground up.
			FBox Expected = ToWorld(Shape.Hull, Transform);
			if (Data.TurretMesh)
			{
				const UStaticMeshComponent* Turret = Cast<UStaticMeshComponent>(Unit->GetDefaultSubobjectByName(TEXT("Turret")));
				Expected += Data.TurretMesh->GetBoundingBox().TransformBy(Turret->GetComponentTransform());
				for (int32 Index = 0; Index < Data.Parts.Num(); ++Index)
				{
					FTransform Part;
					if (Data.Parts[Index].MountedOn == ERTSVehiclePartMount::Turret && Data.Parts[Index].Mesh && Unit->GetPartTransform(Index, Part))
					{
						Expected += Data.Parts[Index].Mesh->GetBoundingBox().TransformBy(Part);
					}
				}
			}
			const FBox Selection = ToWorld(Unit->GetSelectionBounds(), Transform);
			TestTrue(*FString::Printf(TEXT("%s: the selection bounds are the hull and the turret (%s against %s)"), Case.Name,
				*Selection.ToString(), *Expected.ToString()), Selection.Equals(Expected, Tolerance));
			TestNearlyEqual(*FString::Printf(TEXT("%s: from the ground"), Case.Name), static_cast<float>(Selection.Min.Z), 0.f, Tolerance);

			// The footprint is the hull's length and width; the edge is measured to it.
			const float HalfLength = Shape.GetHalfLength();
			const float HalfWidth = Shape.GetHalfWidth();
			const FRTSFootprint Footprint = Unit->GetFootprint();
			TestNearlyEqual(*FString::Printf(TEXT("%s: the footprint reaches the hull's ends"), Case.Name),
				Footprint.HalfSegment + Footprint.Radius, FMath::Max(HalfLength, HalfWidth), Tolerance);
			TestNearlyEqual(*FString::Printf(TEXT("%s: and its sides"), Case.Name), Footprint.Radius, FMath::Min(HalfLength, HalfWidth), Tolerance);
			TestNearlyEqual(*FString::Printf(TEXT("%s: the edge facing ahead is the front"), Case.Name),
				static_cast<float>(Unit->GetClosestFootprintPoint(FVector(10000.f, 0.f, 0.f)).X), HalfLength, Tolerance);
			TestNearlyEqual(*FString::Printf(TEXT("%s: the edge facing aside is the side"), Case.Name),
				static_cast<float>(Unit->GetClosestFootprintPoint(FVector(0.f, 10000.f, 0.f)).Y), HalfWidth, Tolerance);
			Unit->SetActorRotation(FRotator(0.f, 90.f, 0.f));
			TestNearlyEqual(*FString::Printf(TEXT("%s: the footprint turns with the unit"), Case.Name),
				static_cast<float>(Unit->GetClosestFootprintPoint(FVector(0.f, 10000.f, 0.f)).Y), HalfLength, Tolerance);
			Unit->Destroy();
		}

		// Infantry is picked and drag-selected where it was: its origin stays at the middle of its body, the old capsule
		// centre, and a drag box tests that point.
		if (URTSUnitData* Infantry = LoadObject<URTSUnitData>(nullptr, InfantryPath))
		{
			ARTSUnit* Unit = SpawnOnGround(TestWorld, Infantry, FVector::ZeroVector);
			TestTrue(TEXT("Infantry's origin stands as high as its old capsule's centre"), Unit->GetGroundOffset() >= Infantry->SelectionRadius);
			TestTrue(TEXT("Infantry is drag-selected at its origin, as before"), Unit->GetSelectionCenter().Equals(Unit->GetActorLocation(), Tolerance));
		}
		return true;
	}

	/** Component's drawn bounds in the actor's frame; for an instanced component, those of all its instances. */
	FBox GetLocalBounds(const UPrimitiveComponent& Component, const AActor& Actor)
	{
		return Component.CalcBounds(Component.GetComponentTransform().GetRelativeTransform(Actor.GetActorTransform())).GetBox();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCollisionMatchesHullTest, "WorldDivide.Units.CollisionMatchesHull", TestFlags)

	/**
	 * The collision box against what is drawn: the hull mesh and everything mounted on it (tracks, wheels, sprockets), as
	 * their components place them, with the unit turned; the turret, the gun and what rides on them stay out of the box.
	 */
	bool FRTSCollisionMatchesHullTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		for (const FUnitCase& Case : LoadCases(*this))
		{
			if (!Case.Data->Mesh)
			{
				continue;
			}
			for (const float Yaw : { 0.f, 45.f, 160.f })
			{
				ARTSUnit* Unit = SpawnOnGround(TestWorld, Case.Data, FVector(300.f, 200.f, 0.f), Yaw);
				const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Unit->GetDefaultSubobjectByName(TEXT("Mesh")));
				const USceneComponent* Turret = Cast<USceneComponent>(Unit->GetDefaultSubobjectByName(TEXT("Turret")));
				FBox Hull = GetLocalBounds(*Mesh, *Unit);
				FBox OnTurret(ForceInit);
				TArray<UPrimitiveComponent*> Primitives;
				Unit->GetComponents(Primitives);
				for (const UPrimitiveComponent* Primitive : Primitives)
				{
					if (Primitive == Mesh || Primitive == Unit->GetRootComponent() || !Primitive->IsVisible())
					{
						continue;
					}
					// Everything under the turret, the gun included, against everything else on the hull.
					(Primitive == Turret || Primitive->IsAttachedTo(Turret) ? OnTurret : Hull) += GetLocalBounds(*Primitive, *Unit);
				}

				const UBoxComponent* Box = Cast<UBoxComponent>(Unit->GetRootComponent());
				const FBox Collision(-Box->GetUnscaledBoxExtent(), Box->GetUnscaledBoxExtent());
				const FString What = FString::Printf(TEXT("%s turned %.0f"), Case.Name, Yaw);
				TestTrue(*FString::Printf(TEXT("%s: the collision box is the drawn hull (%s against %s)"), *What,
					*Collision.ToString(), *Hull.ToString()), Collision.Equals(Hull, Tolerance));
				TestTrue(*FString::Printf(TEXT("%s: the box is centred on the drawn hull"), *What),
					Hull.GetCenter().Equals(FVector::ZeroVector, Tolerance));
				if (OnTurret.IsValid)
				{
					TestTrue(*FString::Printf(TEXT("%s: the turret and gun reach past the box, outside it"), *What),
						OnTurret.Max.Z > Collision.Max.Z + Tolerance || OnTurret.Max.X > Collision.Max.X + Tolerance);
				}
				Unit->Destroy();
			}
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSSelectionBracketsOnHullTest, "WorldDivide.UI.SelectionBracketsOnHull", TestFlags)

	bool FRTSSelectionBracketsOnHullTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		const ARTSCameraPawn* Camera = GetDefault<ARTSCameraPawn>();
		// A close look, the default, between and the farthest zoom. The close look keeps the camera a quarter of the longest
		// hull beyond it, or 10 m, whichever is further: closer than about 10 m the camera is inside an M20.
		const TArray<FUnitCase> Cases = LoadCases(*this);
		float Longest = 0.f;
		for (const FUnitCase& Case : Cases)
		{
			const FRTSUnitShape Shape = RTSUnitShape::FromData(*Case.Data);
			Longest = FMath::Max(Longest, 2.f * Shape.GetHalfLength());
		}
		const float Zooms[] = { FMath::Max(1000.f, 1.25f * Longest), Camera->GetDefaultZoom(), (Camera->GetDefaultZoom() + Camera->GetMaxZoom()) * 0.5f,
			Camera->GetMaxZoom() };
		const float TopDownPitch = Camera->GetPitchForZoom(Camera->GetMaxZoom());
		for (const FUnitCase& Case : Cases)
		{
			for (const float UnitYaw : { 0.f, 45.f, 90.f })
			{
				ARTSUnit* Unit = SpawnOnGround(TestWorld, Case.Data, FVector(500.f, -300.f, 0.f), UnitYaw);
				const FTransform Transform = Unit->GetActorTransform();
				TArray<FVector> HullCorners;
				GetCorners(Unit->GetShape().Hull, Transform, HullCorners);
				for (const float Zoom : Zooms)
				{
					const FMatrix ViewProjection = MakeViewProjection(Unit->GetActorLocation(), Zoom, 30.f);
					const auto Project = [&ViewProjection](const FVector& World, FVector2D& Out)
					{
						return FSceneView::ProjectWorldToScreen(World, Screen, ViewProjection, Out);
					};
					const FString What = FString::Printf(TEXT("%s turned %.0f at zoom %.0f"), Case.Name, UnitYaw, Zoom);
					// The hull's own screen rect: up close, perspective moves the projected 3D centre off it.
					FBox2D Rect;
					FBox2D HullRect(ForceInit);
					bool bProjects = ARTSHUD::GetUnitScreenRect(*Unit, Project, Rect);
					for (const FVector& Corner : HullCorners)
					{
						FVector2D CornerScreen;
						bProjects &= Project(Corner, CornerScreen);
						HullRect += CornerScreen;
					}
					if (!TestTrue(*FString::Printf(TEXT("%s: the unit projects"), *What), bProjects))
					{
						continue;
					}

					const FVector2D Size = Rect.GetSize();
					TestTrue(*FString::Printf(TEXT("%s: the brackets are at least %.0f px"), *What, ARTSHUD::MinUnitRectSize),
						Size.X >= ARTSHUD::MinUnitRectSize - KINDA_SMALL_NUMBER && Size.Y >= ARTSHUD::MinUnitRectSize - KINDA_SMALL_NUMBER);
					// Seen from low and from the side up close, a big turret pulls the brackets up by a little more: they frame
					// it by design, so the centring holds where the camera looks down, from the default zoom out.
					const FVector2D Off = (Rect.GetCenter() - HullRect.GetCenter()).GetAbs();
					if (FMath::IsNearlyEqual(Camera->GetPitchForZoom(Zoom), TopDownPitch))
					{
						TestTrue(*FString::Printf(TEXT("%s: the brackets centre on the hull (%.1f, %.1f px off in %.0f x %.0f)"), *What,
							Off.X, Off.Y, Size.X, Size.Y), Off.X <= Size.X * 0.1f && Off.Y <= Size.Y * 0.1f);
					}
					else
					{
						AddInfo(FString::Printf(TEXT("%s: the brackets are %.1f %%, %.1f %% off the hull's centre"), *What,
							Off.X / Size.X * 100.f, Off.Y / Size.Y * 100.f));
					}
					TestTrue(*FString::Printf(TEXT("%s: the brackets enclose the hull"), *What), Rect.ExpandBy(0.5f).IsInside(HullRect));
				}
				Unit->Destroy();
			}
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSPickOnHullTest, "WorldDivide.UI.PickOnHull", TestFlags)

	bool FRTSPickOnHullTest::RunTest(const FString& Parameters)
	{
		const FRTSTestWorld TestWorld;
		for (const TCHAR* Path : { M20Path, TankPath })
		{
			URTSUnitData* Data = LoadObject<URTSUnitData>(nullptr, Path);
			if (!TestNotNull(*FString::Printf(TEXT("%s loads"), Path), Data))
			{
				continue;
			}
			ARTSUnit* Unit = SpawnOnGround(TestWorld, Data, FVector::ZeroVector);
			const FString Name = Data->GetName();
			const float HalfLength = Unit->GetShape().GetHalfLength();
			const float HalfWidth = Unit->GetShape().GetHalfWidth();
			const float Top = 2.f * Unit->GetGroundOffset();

			// The camera looks along +X, then along +Y; each click lands on the hull or on the ground on the camera's side of it.
			TestTrue(*FString::Printf(TEXT("%s: a click on the roof selects it"), *Name), TraceClick(*TestWorld.World, FVector(0.f, 0.f, Top), 0.f) == Unit);
			TestTrue(*FString::Printf(TEXT("%s: a click on the rear selects it"), *Name),
				TraceClick(*TestWorld.World, FVector(-HalfLength, 0.f, Top * 0.5f), 0.f) == Unit);
			TestTrue(*FString::Printf(TEXT("%s: a click on the side selects it"), *Name),
				TraceClick(*TestWorld.World, FVector(0.f, -HalfWidth, Top * 0.5f), 90.f) == Unit);
			TestNull(*FString::Printf(TEXT("%s: 1 m behind the hull selects nothing"), *Name),
				TraceClick(*TestWorld.World, FVector(-HalfLength - BesideHull, 0.f, 0.f), 0.f));
			TestNull(*FString::Printf(TEXT("%s: 1 m beside the hull selects nothing"), *Name),
				TraceClick(*TestWorld.World, FVector(0.f, -HalfWidth - BesideHull, 0.f), 90.f));
			TestNull(*FString::Printf(TEXT("%s: nor 1 m beside it seen from behind"), *Name),
				TraceClick(*TestWorld.World, FVector(0.f, HalfWidth + BesideHull, 0.f), 0.f));
			Unit->Destroy();
		}
		return true;
	}
}

#endif
