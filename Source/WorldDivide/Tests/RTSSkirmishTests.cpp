#include "Core/RTSSkirmishSeats.h"
#include "Misc/AutomationTest.h"
#include "Tests/RTSTestFlags.h"

#if WITH_DEV_AUTOMATION_TESTS

// Named rather than anonymous: unity builds merge anonymous namespaces, and other test files have helpers of the same names.
namespace RTSSkirmishTests
{
	using RTSTests::TestFlags;

	FRTSSeat MakeSeat(ERTSSeatType Type, int32 StartSpot = INDEX_NONE, ERTSAIDifficulty Difficulty = ERTSAIDifficulty::Normal)
	{
		FRTSSeat Seat;
		Seat.Type = Type;
		Seat.StartSpot = StartSpot;
		Seat.Difficulty = Difficulty;
		return Seat;
	}

	FRTSSeat MakeHuman(int32 PlayerId, const FString& Token, int32 StartSpot = INDEX_NONE)
	{
		FRTSSeat Seat = MakeSeat(ERTSSeatType::Human, StartSpot);
		Seat.PlayerId = PlayerId;
		Seat.PlayerName = FString::Printf(TEXT("Player %d"), PlayerId);
		Seat.PlayerToken = Token;
		return Seat;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSSkirmishParseSeatsTest, "WorldDivide.Skirmish.ParseSeats", TestFlags)

	bool FRTSSkirmishParseSeatsTest::RunTest(const FString& Parameters)
	{
		TArray<FRTSSeat> Seats;
		TestTrue(TEXT("A full list parses"), RTSSkirmish::ParseSeats(TEXT("Human@3~abc,ai-hard@1,Open,Closed,AI"), Seats));
		if (!TestEqual(TEXT("Five seats"), Seats.Num(), 5))
		{
			return false;
		}
		TestEqual(TEXT("A human"), Seats[0].Type, ERTSSeatType::Human);
		TestEqual(TEXT("Its spot"), Seats[0].StartSpot, 3);
		TestEqual(TEXT("Its token"), Seats[0].PlayerToken, FString(TEXT("abc")));
		TestEqual(TEXT("An AI, ignoring case"), Seats[1].Type, ERTSSeatType::AI);
		TestEqual(TEXT("Its difficulty"), Seats[1].Difficulty, ERTSAIDifficulty::Hard);
		TestEqual(TEXT("Its spot"), Seats[1].StartSpot, 1);
		TestEqual(TEXT("An open seat"), Seats[2].Type, ERTSSeatType::Open);
		TestEqual(TEXT("Without a spot it is random"), Seats[2].StartSpot, int32(INDEX_NONE));
		TestEqual(TEXT("A closed seat"), Seats[3].Type, ERTSSeatType::Closed);
		TestEqual(TEXT("An AI without a difficulty is Normal"), Seats[4].Difficulty, ERTSAIDifficulty::Normal);

		TArray<FRTSSeat> Again;
		TestTrue(TEXT("The formatted list parses again"), RTSSkirmish::ParseSeats(RTSSkirmish::FormatSeats(Seats), Again));
		TestTrue(TEXT("Into the same seats"), Again == Seats);
		TestEqual(TEXT("Formatted"), RTSSkirmish::FormatSeats(Seats), FString(TEXT("Human@3~abc,AI-Hard@1,Open,Closed,AI-Normal")));

		for (const TCHAR* Bad : { TEXT(""), TEXT("Human,Robot"), TEXT("AI-Insane"), TEXT("Open-Hard"), TEXT("AI~abc"),
			TEXT("Human@x"), TEXT("Human@-1"), TEXT("Human@"), TEXT("Human~") })
		{
			TestFalse(FString::Printf(TEXT("\"%s\" is refused"), Bad), RTSSkirmish::ParseSeats(Bad, Seats));
			TestTrue(FString::Printf(TEXT("\"%s\" leaves no seats"), Bad), Seats.IsEmpty());
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSSkirmishStartSpotsTest, "WorldDivide.Skirmish.StartSpots", TestFlags)

	bool FRTSSkirmishStartSpotsTest::RunTest(const FString& Parameters)
	{
		FRandomStream Random(7);
		for (int32 Round = 0; Round < 20; ++Round)
		{
			TArray<FRTSSeat> Seats = { MakeSeat(ERTSSeatType::Open), MakeSeat(ERTSSeatType::AI, 2), MakeSeat(ERTSSeatType::Closed, 0),
				MakeSeat(ERTSSeatType::Human) };
			if (!TestTrue(TEXT("Random spots are assigned"), RTSSkirmish::AssignStartSpots(Seats, 4, Random)))
			{
				return false;
			}
			TestEqual(TEXT("A chosen spot stays"), Seats[1].StartSpot, 2);
			TestEqual(TEXT("A closed seat has no spot"), Seats[2].StartSpot, int32(INDEX_NONE));
			TestTrue(TEXT("Random spots are free ones"), Seats[0].StartSpot != Seats[3].StartSpot
				&& Seats[0].StartSpot != 2 && Seats[3].StartSpot != 2
				&& Seats[0].StartSpot >= 0 && Seats[0].StartSpot < 4 && Seats[3].StartSpot >= 0 && Seats[3].StartSpot < 4);
		}

		TArray<FRTSSeat> Shared = { MakeSeat(ERTSSeatType::Open, 1), MakeSeat(ERTSSeatType::AI, 1) };
		const TArray<FRTSSeat> Before = Shared;
		TestFalse(TEXT("Two seats on one spot are refused"), RTSSkirmish::AssignStartSpots(Shared, 4, Random));
		TestTrue(TEXT("Nothing changes"), Shared == Before);
		TArray<FRTSSeat> OutOfRange = { MakeSeat(ERTSSeatType::Open, 4) };
		TestFalse(TEXT("A spot the map does not have is refused"), RTSSkirmish::AssignStartSpots(OutOfRange, 4, Random));
		TArray<FRTSSeat> TooMany = { MakeSeat(ERTSSeatType::Open), MakeSeat(ERTSSeatType::AI), MakeSeat(ERTSSeatType::AI) };
		TestFalse(TEXT("More seats than spots are refused"), RTSSkirmish::AssignStartSpots(TooMany, 2, Random));
		TArray<FRTSSeat> ClosedExtra = { MakeSeat(ERTSSeatType::Open), MakeSeat(ERTSSeatType::AI), MakeSeat(ERTSSeatType::Closed, 9) };
		TestTrue(TEXT("Closed seats need no spot"), RTSSkirmish::AssignStartSpots(ClosedExtra, 2, Random));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSSkirmishFindSeatTest, "WorldDivide.Skirmish.FindSeatForPlayer", TestFlags)

	bool FRTSSkirmishFindSeatTest::RunTest(const FString& Parameters)
	{
		const TArray<FRTSSeat> Lobby = { MakeSeat(ERTSSeatType::AI), MakeHuman(1, TEXT("host")), MakeSeat(ERTSSeatType::Closed),
			MakeHuman(2, TEXT("guest")) };
		TestEqual(TEXT("A token finds its seat"), RTSSkirmish::FindSeatForPlayer(Lobby, TEXT("guest"), {}), 3);
		TestEqual(TEXT("Whatever the order players join in"), RTSSkirmish::FindSeatForPlayer(Lobby, TEXT("host"), { 3 }), 1);
		TestEqual(TEXT("An unknown token takes the first free human seat"), RTSSkirmish::FindSeatForPlayer(Lobby, TEXT("other"), { 1 }), 3);
		TestEqual(TEXT("No seat once the human seats are taken"), RTSSkirmish::FindSeatForPlayer(Lobby, TEXT("other"), { 1, 3 }),
			int32(INDEX_NONE));
		TestEqual(TEXT("A token whose seat is taken finds none"), RTSSkirmish::FindSeatForPlayer(Lobby, TEXT("host"), { 1, 3 }),
			int32(INDEX_NONE));

		TArray<FRTSSeat> Direct = { MakeSeat(ERTSSeatType::AI), MakeSeat(ERTSSeatType::Human), MakeSeat(ERTSSeatType::Open),
			MakeHuman(INDEX_NONE, TEXT("held")) };
		TestEqual(TEXT("Without a token: an open seat first"), RTSSkirmish::FindSeatForPlayer(Direct, FString(), {}), 2);
		TestEqual(TEXT("Then a human seat no token holds"), RTSSkirmish::FindSeatForPlayer(Direct, FString(), { 2 }), 1);
		TestEqual(TEXT("Then one a token holds"), RTSSkirmish::FindSeatForPlayer(Direct, FString(), { 1, 2 }), 3);
		TestEqual(TEXT("Never an AI or closed seat"), RTSSkirmish::FindSeatForPlayer(Direct, FString(), { 1, 2, 3 }), int32(INDEX_NONE));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSLobbySeatsTest, "WorldDivide.Lobby.Seats", TestFlags)

	bool FRTSLobbySeatsTest::RunTest(const FString& Parameters)
	{
		TArray<FRTSSeat> Seats = { MakeSeat(ERTSSeatType::Open, 0), MakeSeat(ERTSSeatType::AI, 1), MakeSeat(ERTSSeatType::Open),
			MakeSeat(ERTSSeatType::Closed) };
		TestFalse(TEXT("No match without a human"), RTSSkirmish::CanStart(Seats));
		TestEqual(TEXT("The host takes the first open seat"), RTSSkirmish::TakeOpenSeat(Seats, 10, TEXT("Host"), TEXT("t10"), true), 0);
		TestEqual(TEXT("Its name"), Seats[0].PlayerName, FString(TEXT("Host")));
		TestTrue(TEXT("A human and an AI can start"), RTSSkirmish::CanStart(Seats));
		TestEqual(TEXT("A guest takes the next open seat"), RTSSkirmish::TakeOpenSeat(Seats, 11, TEXT("Guest"), TEXT("t11"), false), 2);
		TestEqual(TEXT("No seat is left"), RTSSkirmish::TakeOpenSeat(Seats, 12, TEXT("Late"), TEXT("t12"), false), int32(INDEX_NONE));

		// Seat types: the host's call, never for a seat a human holds.
		TestTrue(TEXT("The host may make an AI harder"), RTSSkirmish::CanChangeType(Seats, 1, true, ERTSSeatType::AI));
		TestTrue(TEXT("The host may close a seat"), RTSSkirmish::CanChangeType(Seats, 1, true, ERTSSeatType::Closed));
		TestFalse(TEXT("A guest may not"), RTSSkirmish::CanChangeType(Seats, 1, false, ERTSSeatType::Closed));
		TestFalse(TEXT("Not even the host changes a seat a human holds"), RTSSkirmish::CanChangeType(Seats, 2, true, ERTSSeatType::Closed));
		TestFalse(TEXT("A seat is never made human by hand"), RTSSkirmish::CanChangeType(Seats, 3, true, ERTSSeatType::Human));
		TestFalse(TEXT("A seat that does not exist"), RTSSkirmish::CanChangeType(Seats, 4, true, ERTSSeatType::Open));

		// Removing a player: the host's call, for another human only.
		TestTrue(TEXT("The host may remove the guest"), RTSSkirmish::CanRemovePlayer(Seats, 2, true, 10));
		TestFalse(TEXT("The guest may not remove the host"), RTSSkirmish::CanRemovePlayer(Seats, 0, false, 11));
		TestFalse(TEXT("Nor itself"), RTSSkirmish::CanRemovePlayer(Seats, 2, false, 11));
		TestFalse(TEXT("The host may not remove itself"), RTSSkirmish::CanRemovePlayer(Seats, 0, true, 10));
		TestFalse(TEXT("Nobody to remove from an AI seat"), RTSSkirmish::CanRemovePlayer(Seats, 1, true, 10));
		TestFalse(TEXT("Nor from a closed one"), RTSSkirmish::CanRemovePlayer(Seats, 3, true, 10));
		TestFalse(TEXT("Nor from a seat that does not exist"), RTSSkirmish::CanRemovePlayer(Seats, 4, true, 10));

		// Start spots: the host sets any played seat's, a guest only its own, and two seats never share one.
		TestTrue(TEXT("The guest moves itself"), RTSSkirmish::CanChangeStartSpot(Seats, 2, false, 11, 3, 4));
		TestTrue(TEXT("Back to random"), RTSSkirmish::CanChangeStartSpot(Seats, 2, false, 11, INDEX_NONE, 4));
		TestFalse(TEXT("The guest may not move the AI"), RTSSkirmish::CanChangeStartSpot(Seats, 1, false, 11, 3, 4));
		TestFalse(TEXT("Nor the host"), RTSSkirmish::CanChangeStartSpot(Seats, 0, false, 11, 3, 4));
		TestTrue(TEXT("The host moves the AI"), RTSSkirmish::CanChangeStartSpot(Seats, 1, true, 10, 3, 4));
		TestFalse(TEXT("Onto a spot another seat has"), RTSSkirmish::CanChangeStartSpot(Seats, 1, true, 10, 0, 4));
		TestTrue(TEXT("Its own spot again"), RTSSkirmish::CanChangeStartSpot(Seats, 1, true, 10, 1, 4));
		TestFalse(TEXT("A spot the map does not have"), RTSSkirmish::CanChangeStartSpot(Seats, 1, true, 10, 4, 4));
		TestFalse(TEXT("A closed seat has no spot"), RTSSkirmish::CanChangeStartSpot(Seats, 3, true, 10, 3, 4));

		TestTrue(TEXT("The guest leaves"), RTSSkirmish::ReleaseSeat(Seats, 11));
		TestEqual(TEXT("Its seat is open again"), Seats[2].Type, ERTSSeatType::Open);
		TestTrue(TEXT("Without its token"), Seats[2].PlayerToken.IsEmpty());
		TestFalse(TEXT("A player without a seat leaves nothing"), RTSSkirmish::ReleaseSeat(Seats, 11));

		// The match: an open seat nobody took is closed; the token goes along, the lobby's names and ids do not.
		const TArray<FRTSSeat> Match = RTSSkirmish::ToMatchSeats(Seats);
		TestEqual(TEXT("The host plays"), Match[0].Type, ERTSSeatType::Human);
		TestEqual(TEXT("By its token"), Match[0].PlayerToken, FString(TEXT("t10")));
		TestEqual(TEXT("Without its lobby id"), Match[0].PlayerId, int32(INDEX_NONE));
		TestEqual(TEXT("The open seat is closed"), Match[2].Type, ERTSSeatType::Closed);
		TestEqual(TEXT("Formatted for the match"), RTSSkirmish::FormatSeats(Match), FString(TEXT("Human@0~t10,AI-Normal@1,Closed,Closed")));

		TArray<FRTSSeat> Alone = { MakeHuman(1, TEXT("t1")), MakeSeat(ERTSSeatType::Open), MakeSeat(ERTSSeatType::Closed) };
		Alone[0].bReady = true;
		TestFalse(TEXT("A human alone cannot start"), RTSSkirmish::CanStart(Alone));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSLobbyReadyTest, "WorldDivide.Lobby.Ready", TestFlags)

	bool FRTSLobbyReadyTest::RunTest(const FString& Parameters)
	{
		TArray<FRTSSeat> Seats = { MakeSeat(ERTSSeatType::Open, 0), MakeSeat(ERTSSeatType::AI, 1), MakeSeat(ERTSSeatType::Open),
			MakeSeat(ERTSSeatType::Open) };
		RTSSkirmish::TakeOpenSeat(Seats, 10, TEXT("Host"), TEXT("t10"), true);
		RTSSkirmish::TakeOpenSeat(Seats, 11, TEXT("Guest"), TEXT("t11"), false);
		TestTrue(TEXT("The host is ready from the start"), Seats[0].bReady);
		TestFalse(TEXT("A guest is not"), Seats[2].bReady);
		TestFalse(TEXT("No start while a human is not ready"), RTSSkirmish::CanStart(Seats));

		TestFalse(TEXT("The host does not ready up"), RTSSkirmish::SetReady(Seats, 10, true, false));
		TestTrue(TEXT("So stays ready"), Seats[0].bReady);
		TestFalse(TEXT("A player without a seat has nothing to ready"), RTSSkirmish::SetReady(Seats, 12, false, true));
		TestTrue(TEXT("The guest readies up"), RTSSkirmish::SetReady(Seats, 11, false, true));
		TestTrue(TEXT("Then the match may start"), RTSSkirmish::CanStart(Seats));
		TestTrue(TEXT("The guest takes it back"), RTSSkirmish::SetReady(Seats, 11, false, false));
		TestFalse(TEXT("And the match waits again"), RTSSkirmish::CanStart(Seats));

		// Moving a seat makes it ready up again, unless it is the host's own.
		RTSSkirmish::SetReady(Seats, 11, false, true);
		TestTrue(TEXT("The guest moves itself"), RTSSkirmish::ChangeStartSpot(Seats, 2, false, 11, 3, 4));
		TestEqual(TEXT("To its new spot"), Seats[2].StartSpot, 3);
		TestFalse(TEXT("And must ready up again"), Seats[2].bReady);
		RTSSkirmish::SetReady(Seats, 11, false, true);
		TestTrue(TEXT("The host moves the guest"), RTSSkirmish::ChangeStartSpot(Seats, 2, true, 10, 2, 4));
		TestFalse(TEXT("Which must ready up again"), Seats[2].bReady);
		RTSSkirmish::SetReady(Seats, 11, false, true);
		TestTrue(TEXT("Asking for the spot a seat has"), RTSSkirmish::ChangeStartSpot(Seats, 2, false, 11, 2, 4));
		TestTrue(TEXT("Keeps it ready"), Seats[2].bReady);
		TestFalse(TEXT("A refused move"), RTSSkirmish::ChangeStartSpot(Seats, 2, false, 11, 0, 4));
		TestTrue(TEXT("Keeps it ready too"), Seats[2].bReady);
		TestTrue(TEXT("The host moves itself"), RTSSkirmish::ChangeStartSpot(Seats, 0, true, 10, 3, 4));
		TestTrue(TEXT("And stays ready"), Seats[0].bReady);
		TestTrue(TEXT("The host moves the AI"), RTSSkirmish::ChangeStartSpot(Seats, 1, true, 10, 0, 4));
		TestTrue(TEXT("Which leaves the guest ready"), Seats[2].bReady && RTSSkirmish::CanStart(Seats));

		const TArray<FRTSSeat> Match = RTSSkirmish::ToMatchSeats(Seats);
		TestTrue(TEXT("Ready is the lobby's alone"), !Match[0].bReady && !Match[2].bReady);
		RTSSkirmish::ReleaseSeat(Seats, 11);
		TestFalse(TEXT("A seat its player left is not ready"), Seats[2].bReady);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSLobbyResizeSeatsTest, "WorldDivide.Lobby.ResizeSeats", TestFlags)

	bool FRTSLobbyResizeSeatsTest::RunTest(const FString& Parameters)
	{
		const TArray<FRTSSeat> FourDefaults = { MakeSeat(ERTSSeatType::Open, 0), MakeSeat(ERTSSeatType::AI, 1),
			MakeSeat(ERTSSeatType::Closed) };
		const TArray<FRTSSeat> TwoDefaults = { MakeSeat(ERTSSeatType::Open, 0), MakeSeat(ERTSSeatType::AI, 1) };

		TArray<FRTSSeat> Seats = RTSSkirmish::MakeLobbySeats(FourDefaults, 4);
		TestEqual(TEXT("One seat per start spot"), Seats.Num(), 4);
		TestEqual(TEXT("A closed default is open in the lobby"), Seats[2].Type, ERTSSeatType::Open);
		TestEqual(TEXT("So is a missing one"), Seats[3].Type, ERTSSeatType::Open);
		RTSSkirmish::TakeOpenSeat(Seats, 10, TEXT("Host"), TEXT("t10"), true);
		RTSSkirmish::TakeOpenSeat(Seats, 11, TEXT("Guest"), TEXT("t11"), false);
		RTSSkirmish::ChangeStartSpot(Seats, 2, false, 11, 3, 4);
		RTSSkirmish::SetReady(Seats, 11, false, true);
		Seats[3] = MakeSeat(ERTSSeatType::AI, 2, ERTSAIDifficulty::Hard);

		TestFalse(TEXT("Only the host changes the map"), RTSSkirmish::CanChangeMap(Seats, false, 2));
		TestTrue(TEXT("To one with a spot for each human"), RTSSkirmish::CanChangeMap(Seats, true, 2));
		TestFalse(TEXT("Not to one with fewer spots than humans"), RTSSkirmish::CanChangeMap(Seats, true, 1));
		TArray<FRTSSeat> Unchanged = Seats;
		TestFalse(TEXT("Which resizes nothing"), RTSSkirmish::ResizeSeats(Unchanged, TwoDefaults, 1, 10));
		TestTrue(TEXT("Nothing at all"), Unchanged == Seats);

		// Four spots to two: the guest beyond them takes the AI's seat, and its base 3 is gone.
		TestTrue(TEXT("The seats resize to two spots"), RTSSkirmish::ResizeSeats(Seats, TwoDefaults, 2, 10));
		TestEqual(TEXT("Two seats"), Seats.Num(), 2);
		TestEqual(TEXT("The host keeps seat 0"), Seats[0].PlayerId, 10);
		TestEqual(TEXT("And its base"), Seats[0].StartSpot, 0);
		TestTrue(TEXT("And stays ready"), Seats[0].bReady);
		TestEqual(TEXT("The guest takes the seat no human holds"), Seats[1].PlayerId, 11);
		TestEqual(TEXT("With its token"), Seats[1].PlayerToken, FString(TEXT("t11")));
		TestEqual(TEXT("Its base the map lacks is random"), Seats[1].StartSpot, int32(INDEX_NONE));
		TestFalse(TEXT("And it must ready up again"), Seats[1].bReady);

		// Back to four: the humans stay, the new seats are the map's defaults, open.
		RTSSkirmish::ChangeStartSpot(Seats, 1, false, 11, 1, 2);
		TestTrue(TEXT("The seats resize to four spots"), RTSSkirmish::ResizeSeats(Seats, FourDefaults, 4, 10));
		TestEqual(TEXT("Four seats"), Seats.Num(), 4);
		TestTrue(TEXT("The humans keep their seats"), Seats[0].PlayerId == 10 && Seats[1].PlayerId == 11);
		TestEqual(TEXT("And their bases"), Seats[1].StartSpot, 1);
		TestEqual(TEXT("A new seat comes open"), Seats[2].Type, ERTSSeatType::Open);
		TestEqual(TEXT("Another too"), Seats[3].Type, ERTSSeatType::Open);

		// A default seat whose base a human has goes back to Random.
		TArray<FRTSSeat> Duel = { MakeHuman(10, TEXT("t10"), 1) };
		TestTrue(TEXT("One seat grows to two"), RTSSkirmish::ResizeSeats(Duel, TwoDefaults, 2, 10));
		TestEqual(TEXT("The human keeps base 1"), Duel[0].StartSpot, 1);
		TestEqual(TEXT("The AI that had it is random"), Duel[1].StartSpot, int32(INDEX_NONE));
		return true;
	}
}

#endif
