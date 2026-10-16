#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Commands/RTSCommand.h"
#include "Data/RTSUnitData.h"
#include "Editor.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "PlayInEditorDataTypes.h"
#include "Player/RTSPlayerController.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSTestFlags.h"
#include "Tests/RTSTestScale.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitMovement.h"

/**
 * Vehicle locomotion over the network (#138). The server alone drives the gear; clients see the hull through the unit's
 * replicated movement and rebuild what its tracks did from the replicated speed and the few bytes of gear state
 * (FRTSLocomotionNet). Played as a listen server with one client, and as a dedicated server with two clients under
 * 100 ms of round trip and about 2 % packet loss. After a stop, a reverse, a pivot and a drive into a block, each client's
 * hull comes to rest within 10 cm and 2 degrees of the server's within a second of the server's rest reaching it; a pivot
 * runs its tracks opposite ways on the client too; a correction of the client's copy is no travel; a client that first
 * sees a vehicle under way starts its travel from nothing. The listen server also measures what the gear state costs.
 */
namespace RTSVehicleNetTests
{
	const TCHAR* TestMap = TEXT("/Game/Maps/L_TestMap");
	const TCHAR* M20Path = TEXT("/Game/Data/Units/DA_TestM20Guardian.DA_TestM20Guardian");
	const TCHAR* TankPath = TEXT("/Game/Data/Units/DA_TestTank.DA_TestTank");
	constexpr double SessionTimeout = 90.0;
	constexpr double PhaseTimeout = 60.0;

	/** #138: rest converges within 10 cm and 2 degrees, within a second of the server's rest reaching the client. */
	constexpr float ConvergeDistance = 10.f;
	constexpr float ConvergeYaw = 2.f;
	constexpr double ConvergeSeconds = 1.0;

	/** The empty strip north of Block_North, where the test drives. */
	const FVector DriveStart(-8000.f, 20000.f, 0.f);

	/** The traffic measurement: this many tanks drive back and forth for this many seconds, with and without the gear state. */
	constexpr int32 TrafficUnits = 240;
	constexpr double TrafficWarmup = 3.0;
	constexpr double TrafficSeconds = 10.0;
	constexpr double TrafficMoveSeconds = 5.0;
	/** #138: the gear state may add at most this share to the traffic. */
	constexpr double TrafficShare = 0.1;

	UWorld* FindWorld(ENetMode NetMode, int32 Index = 0)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (Context.WorldType == EWorldType::PIE && World && World->GetNetMode() == NetMode && Index-- == 0)
			{
				return World;
			}
		}
		return nullptr;
	}

	/** The copy of ServerUnit in a client's world: the unit of the same data standing closest to it. */
	ARTSUnit* FindCopy(UWorld& ClientWorld, const ARTSUnit& ServerUnit)
	{
		ARTSUnit* Best = nullptr;
		double BestDistance = 500.0;
		for (TActorIterator<ARTSUnit> It(&ClientWorld); It; ++It)
		{
			const double Distance = FVector::Dist2D(It->GetActorLocation(), ServerUnit.GetActorLocation());
			if (It->GetUnitData() == ServerUnit.GetUnitData() && Distance < BestDistance)
			{
				Best = *It;
				BestDistance = Distance;
			}
		}
		return Best;
	}

	ARTSUnit* SpawnUnit(UWorld& World, const TCHAR* DataPath, int32 TeamId, const FVector& Location, float Yaw)
	{
		URTSUnitData* Data = LoadObject<URTSUnitData>(nullptr, DataPath);
		if (!Data)
		{
			return nullptr;
		}
		FTransform Transform(FRotator(0.f, Yaw, 0.f), Location);
		ARTSUnit* Unit = World.SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Unit->InitFromData(Data, TeamId);
		Transform.AddToTranslation(FVector(0.f, 0.f, Unit->GetGroundOffset()));
		Unit->FinishSpawning(Transform);
		return Unit;
	}

	/** The server's controller of team 0, whose units the test drives; it issues the test's orders, as its player would. */
	ARTSPlayerController* GetServerController(UWorld& ServerWorld)
	{
		for (TActorIterator<ARTSPlayerController> It(&ServerWorld); It; ++It)
		{
			if (It->GetTeamId() == 0)
			{
				return *It;
			}
		}
		return nullptr;
	}

	void Order(ARTSUnit& Unit, ERTSCommandType Type, const FVector& Target = FVector::ZeroVector)
	{
		ARTSPlayerController* Controller = GetServerController(*Unit.GetWorld());
		FRTSCommand Command;
		Command.Type = Type;
		Command.TargetLocation = Target;
		if (Controller)
		{
			Controller->Server_IssueCommand({ &Unit }, Command);
		}
	}

	/** One step of the test: called every frame until it returns true. */
	using FPhase = TFunction<bool(double)>;

	struct FSession
	{
		bool bDedicated = false;
		int32 ClientCount = 1;
		UWorld* ServerWorld = nullptr;
		TArray<UWorld*> ClientWorlds;
		double PhaseStart = 0.0;
		TArray<TPair<FString, FPhase>> Phases;
		int32 PhaseIndex = 0;
		TWeakObjectPtr<ARTSUnit> Unit;
	};

	class FRTSStartNetSession : public IAutomationLatentCommand
	{
	public:
		explicit FRTSStartNetSession(bool bInDedicated) : bDedicated(bInDedicated) {}

		virtual bool Update() override
		{
			ULevelEditorPlaySettings* PlaySettings = NewObject<ULevelEditorPlaySettings>();
			PlaySettings->SetPlayNetMode(bDedicated ? EPlayNetMode::PIE_Client : EPlayNetMode::PIE_ListenServer);
			PlaySettings->SetPlayNumberOfClients(2);
			PlaySettings->bLaunchSeparateServer = false;
			PlaySettings->SetRunUnderOneProcess(true);
			FLevelEditorPlayNetworkEmulationSettings& Emulation = PlaySettings->NetworkEmulationSettings;
			Emulation.bIsNetworkEmulationEnabled = bDedicated;
			if (bDedicated)
			{
				// 25 ms out and 25 ms in on each side: 100 ms round trip; 1 % lost at each of the two, about 2 % a packet.
				Emulation.EmulationTarget = NetworkEmulationTarget::Any;
				for (FNetworkEmulationPacketSettings* Packets : { &Emulation.OutPackets, &Emulation.InPackets })
				{
					Packets->MinLatency = 25;
					Packets->MaxLatency = 25;
					Packets->PacketLossPercentage = 1;
				}
			}
			FRequestPlaySessionParams Params;
			Params.WorldType = EPlaySessionWorldType::PlayInEditor;
			Params.EditorPlaySettings = PlaySettings;
			GEditor->RequestPlaySession(Params);
			return true;
		}

	private:
		bool bDedicated;
	};
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVehicleNetWaitForSession, FAutomationTestBase*, Test, TSharedRef<RTSVehicleNetTests::FSession>, Session);
bool FRTSVehicleNetWaitForSession::Update()
{
	using namespace RTSVehicleNetTests;
	if (Session->PhaseStart == 0.0)
	{
		Session->PhaseStart = FPlatformTime::Seconds();
	}
	UWorld* Server = FindWorld(Session->bDedicated ? NM_DedicatedServer : NM_ListenServer);
	TArray<UWorld*> Clients;
	for (int32 Index = 0; Index < Session->ClientCount; ++Index)
	{
		UWorld* Client = FindWorld(NM_Client, Index);
		const ARTSPlayerController* Controller = Client ? Cast<ARTSPlayerController>(Client->GetFirstPlayerController()) : nullptr;
		if (Controller && Controller->GetTeamId() != INDEX_NONE)
		{
			Clients.Add(Client);
		}
	}
	int32 Teams = 0;
	if (Server)
	{
		for (TActorIterator<ARTSPlayerController> It(Server); It; ++It)
		{
			Teams += It->GetTeamId() != INDEX_NONE ? 1 : 0;
		}
	}
	if (Server && Clients.Num() == Session->ClientCount && Teams == 2 && RTSTests::IsNavigationReady(*Server))
	{
		Session->ServerWorld = Server;
		Session->ClientWorlds = Clients;
		Session->PhaseStart = Server->GetTimeSeconds();
		Test->AddInfo(FString::Printf(TEXT("%s session with %d client(s) ready"), Session->bDedicated ? TEXT("Dedicated") : TEXT("Listen"), Clients.Num()));
		return true;
	}
	if (FPlatformTime::Seconds() - Session->PhaseStart > SessionTimeout)
	{
		Test->AddError(TEXT("The session did not start in time"));
		return true;
	}
	return false;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FRTSVehicleNetRunPhases, FAutomationTestBase*, Test, TSharedRef<RTSVehicleNetTests::FSession>, Session);
bool FRTSVehicleNetRunPhases::Update()
{
	using namespace RTSVehicleNetTests;
	if (!Session->ServerWorld || !Session->Phases.IsValidIndex(Session->PhaseIndex))
	{
		return true;
	}
	const double Now = Session->ServerWorld->GetTimeSeconds();
	TPair<FString, FPhase>& Phase = Session->Phases[Session->PhaseIndex];
	if (Phase.Value(Now - Session->PhaseStart))
	{
		++Session->PhaseIndex;
		Session->PhaseStart = Now;
		return !Session->Phases.IsValidIndex(Session->PhaseIndex);
	}
	if (Now - Session->PhaseStart > PhaseTimeout)
	{
		Test->AddError(FString::Printf(TEXT("%s did not finish in %.0f s"), *Phase.Key, PhaseTimeout));
		return true;
	}
	return false;
}

namespace RTSVehicleNetTests
{
	/**
	 * Waits for the server's unit to rest, then for each client's copy to see it resting, and checks every copy comes within
	 * ConvergeDistance and ConvergeYaw of the server's pose within ConvergeSeconds after that.
	 */
	FPhase MakeConvergeCheck(FAutomationTestBase& Test, const TSharedRef<FSession>& Session, const FString& What)
	{
		struct FData
		{
			TOptional<double> ServerRest;
			TArray<TOptional<double>> ClientRest;
			TArray<bool> bConverged;
			TArray<FString> Worst;
		};
		TSharedRef<FData> Data = MakeShared<FData>();
		return [&Test, Session, What, Data](double Elapsed)
		{
			ARTSUnit* Unit = Session->Unit.Get();
			if (!Unit)
			{
				Test.AddError(FString::Printf(TEXT("%s: the unit is gone"), *What));
				return true;
			}
			const FRTSRealizedMotion& Motion = Unit->GetRealizedMotion();
			const bool bServerRests = (Motion.Status == ERTSLocomotionStatus::Idle || Motion.Status == ERTSLocomotionStatus::Blocked)
				&& FMath::Abs(Motion.ForwardSpeed) < 1.f && FMath::Abs(Motion.YawRate) < 0.1f && Unit->GetQueuedCommandCount() == 0;
			if (!Data->ServerRest)
			{
				if (!bServerRests)
				{
					return false;
				}
				Data->ServerRest = Elapsed;
				Data->ClientRest.SetNum(Session->ClientWorlds.Num());
				Data->bConverged.Init(false, Session->ClientWorlds.Num());
				Data->Worst.SetNum(Session->ClientWorlds.Num());
			}
			bool bDone = true;
			for (int32 Index = 0; Index < Session->ClientWorlds.Num(); ++Index)
			{
				ARTSUnit* Copy = FindCopy(*Session->ClientWorlds[Index], *Unit);
				if (!Copy)
				{
					bDone = false;
					continue;
				}
				const FRTSLocomotionNet& Net = Copy->GetLocomotionNet();
				const bool bClientRests = Copy->GetVelocity().Size2D() < 1.f && Net.YawRate == 0 && Net.Status != ERTSLocomotionStatus::Driving
					&& Net.Status != ERTSLocomotionStatus::Pivoting && Net.Status != ERTSLocomotionStatus::Reversing;
				if (!Data->ClientRest[Index])
				{
					if (bClientRests)
					{
						Data->ClientRest[Index] = Elapsed;
					}
					bDone = false;
					continue;
				}
				const float Distance = FVector::Dist2D(Copy->GetActorLocation(), Unit->GetActorLocation());
				const float Yaw = FMath::Abs(FMath::FindDeltaAngleDegrees(Copy->GetActorRotation().Yaw, Unit->GetActorRotation().Yaw));
				Data->Worst[Index] = FString::Printf(TEXT("%.2f cm, %.2f degrees"), Distance, Yaw);
				if (Distance <= ConvergeDistance && Yaw <= ConvergeYaw)
				{
					if (!Data->bConverged[Index])
					{
						Data->bConverged[Index] = true;
						Test.AddInfo(FString::Printf(TEXT("%s: client %d converged %.2f s after the rest reached it (%s)"), *What, Index + 1,
							Elapsed - *Data->ClientRest[Index], *Data->Worst[Index]));
					}
					continue;
				}
				if (Elapsed - *Data->ClientRest[Index] > ConvergeSeconds)
				{
					Test.AddError(FString::Printf(TEXT("%s: client %d is still %s off the server's hull %.1f s after the rest reached it"), *What,
						Index + 1, *Data->Worst[Index], ConvergeSeconds));
					Data->bConverged[Index] = true;
					continue;
				}
				bDone = false;
			}
			return bDone;
		};
	}

	/** The test's drives, with a convergence check after each. */
	void AddDrives(FAutomationTestBase& Test, const TSharedRef<FSession>& Session)
	{
		auto Spawn = [&Test, Session](double)
		{
			Session->Unit = SpawnUnit(*Session->ServerWorld, M20Path, 0, DriveStart, 0.f);
			return Test.TestNotNull(TEXT("The M20 spawns on the server"), Session->Unit.Get());
		};
		auto OrderTo = [Session](ERTSCommandType Type, FVector Offset)
		{
			return [Session, Type, Offset](double)
			{
				if (ARTSUnit* Unit = Session->Unit.Get())
				{
					Order(*Unit, Type, DriveStart + Offset);
				}
				return true;
			};
		};
		Session->Phases.Add({ TEXT("Spawn"), Spawn });
		Session->Phases.Add({ TEXT("First sight"), [Session](double Elapsed) { return Elapsed > 2.0; } });

		// A drive ahead, stopped half way.
		Session->Phases.Add({ TEXT("Drive"), OrderTo(ERTSCommandType::Move, FVector(4000.f, 0.f, 0.f)) });
		Session->Phases.Add({ TEXT("Under way"), [](double Elapsed) { return Elapsed > 2.0; } });
		Session->Phases.Add({ TEXT("Stop"), OrderTo(ERTSCommandType::Stop, FVector::ZeroVector) });
		Session->Phases.Add({ TEXT("Stop converges"), MakeConvergeCheck(Test, Session, TEXT("Stop")) });

		// A short way back: reversing.
		Session->Phases.Add({ TEXT("Reverse"), [Session](double)
		{
			if (ARTSUnit* Unit = Session->Unit.Get())
			{
				Order(*Unit, ERTSCommandType::Move, Unit->GetActorLocation() - Unit->GetActorForwardVector() * 600.f);
			}
			return true;
		} });
		Session->Phases.Add({ TEXT("Reverse converges"), MakeConvergeCheck(Test, Session, TEXT("Reverse")) });

		// Far behind: a pivot, whose tracks the clients must see run opposite ways.
		struct FPivot { TArray<double> Left; TArray<double> Right; bool bChecked = false; };
		TSharedRef<FPivot> Pivot = MakeShared<FPivot>();
		Session->Phases.Add({ TEXT("Pivot"), [Session, Pivot](double)
		{
			if (ARTSUnit* Unit = Session->Unit.Get())
			{
				Order(*Unit, ERTSCommandType::Move, Unit->GetActorLocation() - Unit->GetActorForwardVector() * 3000.f + Unit->GetActorRightVector() * 300.f);
			}
			return true;
		} });
		Session->Phases.Add({ TEXT("Pivoting"), [&Test, Session, Pivot](double Elapsed)
		{
			ARTSUnit* Unit = Session->Unit.Get();
			if (!Unit)
			{
				return true;
			}
			const bool bPivoting = Unit->GetRealizedMotion().Status == ERTSLocomotionStatus::Pivoting;
			for (int32 Index = 0; Index < Session->ClientWorlds.Num(); ++Index)
			{
				const ARTSUnit* Copy = FindCopy(*Session->ClientWorlds[Index], *Unit);
				if (Pivot->Left.Num() <= Index)
				{
					Pivot->Left.Add(Copy ? Copy->GetRealizedMotion().LeftTravel : 0.0);
					Pivot->Right.Add(Copy ? Copy->GetRealizedMotion().RightTravel : 0.0);
				}
			}
			if (!bPivoting && Elapsed < 1.0)
			{
				return false;
			}
			if (bPivoting && Elapsed < 2.5)
			{
				return false;
			}
			for (int32 Index = 0; Index < Session->ClientWorlds.Num(); ++Index)
			{
				const ARTSUnit* Copy = FindCopy(*Session->ClientWorlds[Index], *Unit);
				if (!Test.TestNotNull(TEXT("The client sees the pivoting M20"), Copy))
				{
					continue;
				}
				const double Left = Copy->GetRealizedMotion().LeftTravel - Pivot->Left[Index];
				const double Right = Copy->GetRealizedMotion().RightTravel - Pivot->Right[Index];
				Test.TestTrue(FString::Printf(TEXT("Client %d sees the pivot's tracks run opposite ways (left %.1f, right %.1f cm)"), Index + 1, Left, Right),
					Left * Right < 0.0 && FMath::Abs(Left + Right) < 0.25 * (FMath::Abs(Left) + FMath::Abs(Right)));
			}
			return true;
		} });
		Session->Phases.Add({ TEXT("Pivot converges"), MakeConvergeCheck(Test, Session, TEXT("Pivot")) });

		// Flat out into Block_North, from its north side.
		Session->Phases.Add({ TEXT("Contact"), [Session](double)
		{
			ARTSUnit* Unit = Session->Unit.Get();
			const FBox2D Block = RTSTests::GetLabelledBox(*Session->ServerWorld, TEXT("Block_North"));
			if (Unit && Block.bIsValid)
			{
				Unit->SetActorLocationAndRotation(FVector(Block.GetCenter().X, Block.Max.Y + 2500.f, Unit->GetActorLocation().Z), FRotator(0.f, -90.f, 0.f));
				RTSLocomotion::FCommand Command;
				Command.Speed = 1.e5f;
				Unit->GetUnitMovement()->SetCommandOverride(Command);
			}
			return true;
		} });
		Session->Phases.Add({ TEXT("Against the block"), [Session](double Elapsed)
		{
			ARTSUnit* Unit = Session->Unit.Get();
			if (!Unit || (Unit->GetRealizedMotion().Status == ERTSLocomotionStatus::Blocked && Elapsed > 1.0))
			{
				if (Unit)
				{
					RTSLocomotion::FCommand Stop;
					Unit->GetUnitMovement()->SetCommandOverride(Stop);
				}
				return true;
			}
			return false;
		} });
		Session->Phases.Add({ TEXT("Contact converges"), MakeConvergeCheck(Test, Session, TEXT("Contact")) });

		// A correction of a client's copy, as the network makes: no travel on its tracks.
		Session->Phases.Add({ TEXT("Correction"), [&Test, Session](double Elapsed)
		{
			ARTSUnit* Unit = Session->Unit.Get();
			ARTSUnit* Copy = Unit ? FindCopy(*Session->ClientWorlds[0], *Unit) : nullptr;
			if (!Copy)
			{
				return true;
			}
			const FVector ServerLocation = Unit->GetActorLocation();
			const double Driven = Copy->GetRealizedMotion().DrivenDistance;
			Copy->SetActorLocation(Copy->GetActorLocation() + Copy->GetActorForwardVector() * 300.f);
			Copy->GetUnitMovement()->TickComponent(0.1f, LEVELTICK_All, nullptr);
			Copy->UpdateRunningGear();
			Test.TestTrue(FString::Printf(TEXT("A correction of the client's copy is no travel (%.2f cm)"), Copy->GetRealizedMotion().DrivenDistance - Driven),
				FMath::Abs(Copy->GetRealizedMotion().DrivenDistance - Driven) < 0.1);
			Test.TestTrue(TEXT("and moves nothing on the server"), Unit->GetActorLocation().Equals(ServerLocation, 0.01));
			Copy->SetActorLocation(Copy->GetActorLocation() - Copy->GetActorForwardVector() * 300.f);
			return true;
		} });

		// A vehicle under way before the clients first see it: their travel starts from nothing, not from what it drove before.
		struct FLate { TWeakObjectPtr<ARTSUnit> Unit; double ServerDriven = 0.0; };
		TSharedRef<FLate> Late = MakeShared<FLate>();
		Session->Phases.Add({ TEXT("Hidden drive"), [Session, Late](double)
		{
			ARTSUnit* Unit = SpawnUnit(*Session->ServerWorld, TankPath, 0, DriveStart + FVector(0.f, 3000.f, 0.f), 0.f);
			if (Unit)
			{
				Unit->SetReplicates(false);
				Order(*Unit, ERTSCommandType::Move, DriveStart + FVector(12000.f, 3000.f, 0.f));
			}
			Late->Unit = Unit;
			return true;
		} });
		Session->Phases.Add({ TEXT("Under way unseen"), [](double Elapsed) { return Elapsed > 5.0; } });
		Session->Phases.Add({ TEXT("Revealed"), [Session, Late](double)
		{
			if (ARTSUnit* Unit = Late->Unit.Get())
			{
				Late->ServerDriven = Unit->GetRealizedMotion().DrivenDistance;
				Unit->SetReplicates(true);
			}
			return true;
		} });
		Session->Phases.Add({ TEXT("Late join"), [&Test, Session, Late](double Elapsed)
		{
			ARTSUnit* Unit = Late->Unit.Get();
			if (!Unit)
			{
				return true;
			}
			bool bAll = true;
			for (int32 Index = 0; Index < Session->ClientWorlds.Num(); ++Index)
			{
				const ARTSUnit* Copy = FindCopy(*Session->ClientWorlds[Index], *Unit);
				bAll &= Copy != nullptr;
			}
			if (!bAll && Elapsed < 10.0)
			{
				return false;
			}
			for (int32 Index = 0; Index < Session->ClientWorlds.Num(); ++Index)
			{
				const ARTSUnit* Copy = FindCopy(*Session->ClientWorlds[Index], *Unit);
				if (Test.TestNotNull(FString::Printf(TEXT("Client %d sees the tank once it replicates"), Index + 1), Copy))
				{
					// What it drove since it was first seen, at most since it was revealed.
					const double Since = Unit->GetRealizedMotion().DrivenDistance - Late->ServerDriven;
					Test.TestTrue(FString::Printf(TEXT("Client %d's travel starts from nothing (%.0f cm, the server drove %.0f before it was seen)"), Index + 1,
						Copy->GetRealizedMotion().DrivenDistance, Late->ServerDriven), Copy->GetRealizedMotion().DrivenDistance <= Since + 1.0);
				}
			}
			Unit->Destroy();
			return true;
		} });
	}

	/**
	 * The traffic of TrafficUnits tanks driving back and forth, with and then without the gear state, on the server's net
	 * driver: bytes and packets a second, both to the client. Added gear traffic must stay within TrafficShare.
	 */
	void AddTraffic(FAutomationTestBase& Test, const TSharedRef<FSession>& Session)
	{
		struct FTraffic
		{
			TArray<TWeakObjectPtr<ARTSUnit>> Units;
			bool bForward = false;
			double LastMove = -1.0e9;
			int64 BytesStart = 0;
			int64 PacketsStart = 0;
			double Start = 0.0;
			double Rates[2][2] = {};
		};
		TSharedRef<FTraffic> Traffic = MakeShared<FTraffic>();
		IConsoleVariable* GearState = IConsoleManager::Get().FindConsoleVariable(TEXT("rts.Net.LocomotionState"));
		Session->Phases.Add({ TEXT("Traffic spawn"), [Session, Traffic](double)
		{
			const int32 Columns = 20;
			for (int32 Index = 0; Index < TrafficUnits; ++Index)
			{
				const FVector Location(-20000.f + (Index % Columns) * 1400.f, 16000.f + (Index / Columns) * 700.f, 0.f);
				Traffic->Units.Add(SpawnUnit(*Session->ServerWorld, TankPath, 0, Location, 0.f));
			}
			return true;
		} });
		auto Drive = [Session, Traffic](double Elapsed)
		{
			const double Now = Session->ServerWorld->GetTimeSeconds();
			if (Now - Traffic->LastMove < TrafficMoveSeconds)
			{
				return;
			}
			Traffic->LastMove = Now;
			Traffic->bForward = !Traffic->bForward;
			TArray<ARTSUnit*> Units;
			for (const TWeakObjectPtr<ARTSUnit>& Unit : Traffic->Units)
			{
				if (Unit.IsValid())
				{
					Units.Add(Unit.Get());
				}
			}
			FRTSCommand Move;
			Move.Type = ERTSCommandType::Move;
			Move.TargetLocation = FVector(Traffic->bForward ? -2000.f : -8000.f, 18000.f, 0.f);
			if (ARTSPlayerController* Controller = GetServerController(*Session->ServerWorld))
			{
				Controller->Server_IssueCommand(Units, Move);
			}
		};
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			Session->Phases.Add({ FString::Printf(TEXT("Traffic warm-up %d"), Pass), [Traffic, Session, Drive, GearState, Pass](double Elapsed)
			{
				if (GearState)
				{
					GearState->Set(Pass == 0 ? 1 : 0, ECVF_SetByCode);
				}
				Drive(Elapsed);
				if (Elapsed < TrafficWarmup)
				{
					return false;
				}
				const UNetDriver* Driver = Session->ServerWorld->GetNetDriver();
				Traffic->BytesStart = Driver ? Driver->OutTotalBytes : 0;
				Traffic->PacketsStart = Driver ? Driver->OutTotalPackets : 0;
				Traffic->Start = FPlatformTime::Seconds();
				return true;
			} });
			Session->Phases.Add({ FString::Printf(TEXT("Traffic %d"), Pass), [Traffic, Session, Drive, Pass](double Elapsed)
			{
				Drive(Elapsed);
				if (Elapsed < TrafficSeconds)
				{
					return false;
				}
				const UNetDriver* Driver = Session->ServerWorld->GetNetDriver();
				const double Seconds = FMath::Max(Elapsed, 1.0);
				Traffic->Rates[Pass][0] = Driver ? (Driver->OutTotalBytes - Traffic->BytesStart) / Seconds : 0.0;
				Traffic->Rates[Pass][1] = Driver ? (Driver->OutTotalPackets - Traffic->PacketsStart) / Seconds : 0.0;
				return true;
			} });
		}
		Session->Phases.Add({ TEXT("Traffic result"), [&Test, Traffic, GearState](double)
		{
			if (GearState)
			{
				GearState->Set(1, ECVF_SetByCode);
			}
			for (const TWeakObjectPtr<ARTSUnit>& Unit : Traffic->Units)
			{
				if (Unit.IsValid())
				{
					Unit->Destroy();
				}
			}
			const double With = Traffic->Rates[0][0];
			const double Without = Traffic->Rates[1][0];
			Test.AddInfo(FString::Printf(TEXT("Traffic of %d moving tanks per game second: %.0f bytes and %.0f packets with the gear state, %.0f bytes and %.0f packets without (%+.1f %%)"),
				TrafficUnits, With, Traffic->Rates[0][1], Without, Traffic->Rates[1][1], Without > 0.0 ? 100.0 * (With - Without) / Without : 0.0));
			Test.TestTrue(FString::Printf(TEXT("The gear state adds at most %.0f %% to the traffic"), 100.0 * TrafficShare), With <= (1.0 + TrafficShare) * Without);
			return true;
		} });
	}
}

/**
 * Listen server and one client on L_TestMap: the M20's stop, reverse, pivot and drive into a block converge on the client,
 * the pivot runs the client's tracks opposite ways, a correction of the client's copy is no travel, a vehicle first seen
 * under way starts its travel from nothing; and 240 moving tanks cost at most 10 % more traffic with the gear state.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSVehicleNetListenTest, "WorldDivide.Net.Vehicle.ListenServer", RTSTests::TestFlags)

bool FRTSVehicleNetListenTest::RunTest(const FString& Parameters)
{
	using namespace RTSVehicleNetTests;
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}
	TSharedRef<FSession> Session = MakeShared<FSession>();
	AddTraffic(*this, Session);
	AddDrives(*this, Session);
	ADD_LATENT_AUTOMATION_COMMAND(FRTSStartNetSession(false));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVehicleNetWaitForSession(this, Session));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVehicleNetRunPhases(this, Session));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

/**
 * A dedicated server and two clients on L_TestMap, under 100 ms of round trip and about 2 % packet loss, at real time:
 * the same drives converge on both clients.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSVehicleNetDedicatedTest, "WorldDivide.Net.Vehicle.Dedicated", RTSTests::TestFlags)

bool FRTSVehicleNetDedicatedTest::RunTest(const FString& Parameters)
{
	using namespace RTSVehicleNetTests;
	if (!AutomationOpenMap(TestMap))
	{
		AddError(FString::Printf(TEXT("Could not open %s"), TestMap));
		return false;
	}
	TSharedRef<FSession> Session = MakeShared<FSession>();
	Session->bDedicated = true;
	Session->ClientCount = 2;
	AddDrives(*this, Session);
	ADD_LATENT_AUTOMATION_COMMAND(FRTSStartNetSession(true));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVehicleNetWaitForSession(this, Session));
	ADD_LATENT_AUTOMATION_COMMAND(FRTSVehicleNetRunPhases(this, Session));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
