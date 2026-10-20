#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Buildings/RTSBuilding.h"
#include "Core/RTSPlayerState.h"
#include "Core/RTSSkirmishGameMode.h"
#include "Data/RTSFactionData.h"
#include "Data/RTSMapData.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Player/RTSCameraPawn.h"
#include "Player/RTSPlayerController.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestMaps.h"
#include "Tests/RTSTestPIE.h"

/**
 * Every map the lobby offers and every test map, played with all its start spots taken: the player from spot 0 and an AI
 * from each other spot. PIE starts the map with its default seats and travels to it again with these, as the lobby's start and the
 * restart do. The map loads with the skirmish game mode, each start spot has its PlayerStart tagged Team<N>, and every team
 * gets its headquarters, completed, on its own spot. The player's camera pans to every corner of the ground, and no
 * further than a small margin past it.
 */
namespace RTSMapSmokeTest
{
	/** Real seconds for PIE, the navmesh and the headquarters to come up. */
	constexpr double StartTimeout = 60.0;

	/** How far a headquarters may stand from its PlayerStart, in cm. */
	constexpr double SpotTolerance = 200.0;

	/** The Seats option that plays every spot of a map with StartSpots spots, team N from spot N. */
	FString MakeSeatsOption(int32 StartSpots)
	{
		FString Seats = TEXT("?Seats=Open@0");
		for (int32 Spot = 1; Spot < StartSpots; ++Spot)
		{
			Seats += FString::Printf(TEXT(",AI@%d"), Spot);
		}
		return Seats;
	}

	/** The headquarters of TeamId: its faction's headquarters building, completed. */
	const ARTSBuilding* FindHeadquarters(UWorld& World, int32 TeamId)
	{
		const ARTSPlayerState* Player = ARTSPlayerState::FindForTeam(World, TeamId);
		const URTSBuildingData* Data = Player && Player->GetFaction() ? Player->GetFaction()->Headquarters.Get() : nullptr;
		for (TActorIterator<ARTSBuilding> It(&World); It && Data; ++It)
		{
			if (It->GetTeamId() == TeamId && It->GetBuildingData() == Data && It->IsConstructed())
			{
				return *It;
			}
		}
		return nullptr;
	}

	/**
	 * Travels the PIE session to MapPath with every spot played, waits until it plays there with a headquarters for every
	 * spot, then checks its start spots and headquarters.
	 */
	class FRTSVerifyMapPlays : public IAutomationLatentCommand
	{
	public:
		FRTSVerifyMapPlays(FAutomationTestBase* InTest, FString InName, FString InMapPath, int32 InStartSpots, const FBox2D& InGround)
			: Test(InTest), Name(MoveTemp(InName)), MapPath(MoveTemp(InMapPath)), StartSpots(InStartSpots), Ground(InGround)
		{
		}

		virtual bool Update() override
		{
			if (StartTime == 0.0)
			{
				StartTime = FPlatformTime::Seconds();
			}
			UWorld* World = GEditor->IsPlaySessionRequestQueued() ? nullptr : AutomationCommon::GetAnyGameWorld();
			if (!bTravelled && World && World->HasBegunPlay())
			{
				// The world before the travel; the next one plays the seats.
				FirstWorld = World;
				bTravelled = World->ServerTravel(MapPath + MakeSeatsOption(StartSpots));
				if (!Test->TestTrue(FString::Printf(TEXT("%s: PIE travels to it with every spot played"), *Name), bTravelled))
				{
					return true;
				}
				return false;
			}
			const bool bReady = bTravelled && World && World != FirstWorld.Get() && World->HasBegunPlay()
				&& RTSTests::IsNavigationReady(*World) && HasAllHeadquarters(*World);
			if (!bReady)
			{
				if (FPlatformTime::Seconds() - StartTime > StartTimeout)
				{
					Test->AddError(FString::Printf(TEXT("%s: PIE did not start with a headquarters on every spot in time"), *Name));
					if (World)
					{
						Verify(*World);
					}
					return true;
				}
				return false;
			}
			Verify(*World);
			return true;
		}

	private:
		bool HasAllHeadquarters(UWorld& World) const
		{
			for (int32 TeamId = 0; TeamId < StartSpots; ++TeamId)
			{
				if (!FindHeadquarters(World, TeamId))
				{
					return false;
				}
			}
			return true;
		}

		void Verify(UWorld& World) const
		{
			Test->TestTrue(FString::Printf(TEXT("%s plays with the skirmish game mode"), *Name),
				Cast<ARTSSkirmishGameMode>(World.GetAuthGameMode()) != nullptr);

			TMap<int32, TArray<const APlayerStart*>> Starts;
			for (TActorIterator<APlayerStart> It(&World); It; ++It)
			{
				const FString Tag = It->PlayerStartTag.ToString();
				if (Tag.StartsWith(TEXT("Team")))
				{
					Starts.FindOrAdd(FCString::Atoi(*Tag.Mid(4))).Add(*It);
				}
			}
			Test->TestEqual(FString::Printf(TEXT("%s: a tagged PlayerStart per start spot"), *Name), Starts.Num(), StartSpots);

			for (int32 Spot = 0; Spot < StartSpots; ++Spot)
			{
				const TArray<const APlayerStart*>* Tagged = Starts.Find(Spot);
				if (!Test->TestTrue(FString::Printf(TEXT("%s: one PlayerStart tagged Team%d"), *Name, Spot),
					Tagged && Tagged->Num() == 1))
				{
					continue;
				}
				// The seats put team N on spot N.
				const ARTSBuilding* Headquarters = FindHeadquarters(World, Spot);
				if (!Test->TestNotNull(FString::Printf(TEXT("%s: team %d has its headquarters"), *Name, Spot), Headquarters))
				{
					continue;
				}
				const double Distance = FVector::Dist2D(Headquarters->GetActorLocation(), (*Tagged)[0]->GetActorLocation());
				Test->TestTrue(FString::Printf(TEXT("%s: team %d's headquarters stands on spot %d, %.0f cm from it"), *Name, Spot,
					Spot, Distance), Distance <= SpotTolerance);
			}

			const ARTSPlayerController* Player = Cast<ARTSPlayerController>(World.GetFirstPlayerController());
			ARTSCameraPawn* Camera = Player ? Player->GetPawn<ARTSCameraPawn>() : nullptr;
			if (Test->TestNotNull(FString::Printf(TEXT("%s: the player has a camera"), *Name), Camera))
			{
				VerifyCameraBounds(*Camera);
			}
		}

		/** Pans the camera from the middle towards each corner of the ground, and jumps it far past each corner. */
		void VerifyCameraBounds(ARTSCameraPawn& Camera) const
		{
			const FVector2D Corners[] = {
				Ground.Min, Ground.Max, FVector2D(Ground.Min.X, Ground.Max.Y), FVector2D(Ground.Max.X, Ground.Min.Y) };
			for (const FVector2D& Corner : Corners)
			{
				const FVector2D Outward = (Corner - Ground.GetCenter()).GetSafeNormal();

				Camera.JumpTo(FVector(Ground.GetCenter(), 0.0));
				const FRotator YawOnly(0.f, Camera.GetActorRotation().Yaw, 0.f);
				const FVector2D Forward(YawOnly.RotateVector(FVector::ForwardVector));
				const FVector2D Right(YawOnly.RotateVector(FVector::RightVector));
				for (int32 Frame = 0; Frame < 600; ++Frame)
				{
					Camera.AddPanInput(FVector2D(Outward | Right, Outward | Forward));
					Camera.Tick(1.f / 10.f);
				}
				VerifyAtCorner(Camera, Corner, TEXT("Panning"));

				Camera.JumpTo(FVector(Corner + Outward * 1000000.0, 0.0));
				VerifyAtCorner(Camera, Corner, TEXT("Jumping"));
			}
		}

		/**
		 * The ground the camera's view shows reaches Corner of the ground, or past it by no more than its EdgeMargin along
		 * each axis (#295); along an axis where the view is wider than the ground and its margins, it is centred instead.
		 */
		void VerifyAtCorner(const ARTSCameraPawn& Camera, const FVector2D& Corner, const TCHAR* How) const
		{
			const FBox2D View = Camera.GetViewFootprintBounds();
			const FVector2D Outward(FMath::Sign(Corner.X - Ground.GetCenter().X), FMath::Sign(Corner.Y - Ground.GetCenter().Y));
			const FVector2D Edge(Outward.X > 0.0 ? View.Max.X : View.Min.X, Outward.Y > 0.0 ? View.Max.Y : View.Min.Y);
			const FVector2D Past = (Edge - Corner) * Outward;
			const double MaxEdgeMargin = Camera.GetTuning().EdgeMargin;
			const FBox2D& Area = Camera.GetPanArea();
			bool bReaches = true;
			for (int32 Axis = 0; Axis < 2; ++Axis)
			{
				const bool bCentred = View.GetSize()[Axis] > Area.GetSize()[Axis] + 1.0;
				bReaches &= bCentred ? FMath::IsNearlyEqual(View.GetCenter()[Axis], Area.GetCenter()[Axis], 1.0)
					: Past[Axis] >= -1.0 && Past[Axis] <= MaxEdgeMargin + 1.0;
			}
			Test->TestTrue(FString::Printf(TEXT("%s: %s towards corner %s takes the view to it, showing %s"), *Name, How,
				*Corner.ToString(), *View.ToString()), bReaches);
		}

		FAutomationTestBase* Test;
		FString Name;
		FString MapPath;
		int32 StartSpots;
		/** The world XY rectangle of the map's ground. */
		FBox2D Ground;
		double StartTime = 0.0;
		bool bTravelled = false;
		TWeakObjectPtr<UWorld> FirstWorld;
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FRTSMapSmokeTest, "WorldDivide.Smoke.Maps", RTSTests::TestFlags)

void FRTSMapSmokeTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	for (const TSoftObjectPtr<URTSMapData>& Map : RTSTests::GetCheckedMaps())
	{
		OutBeautifiedNames.Add(Map.GetAssetName());
		OutTestCommands.Add(Map.ToSoftObjectPath().ToString());
	}
}

bool FRTSMapSmokeTest::RunTest(const FString& Parameters)
{
	const URTSMapData* Map = Cast<URTSMapData>(FSoftObjectPath(Parameters).TryLoad());
	if (!Map)
	{
		AddError(FString::Printf(TEXT("Could not load %s"), *Parameters));
		return false;
	}
	const FString MapPath = Map->Map.GetLongPackageName();
	if (!AutomationOpenMap(MapPath))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), *MapPath));
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(RTSTests::FRTSStartStandalonePIE());
	ADD_LATENT_AUTOMATION_COMMAND(RTSMapSmokeTest::FRTSVerifyMapPlays(this, Map->GetName(), MapPath, Map->StartSpots,
		Map->PreviewArea));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
