#include "Core/RTSSkirmishSeats.h"

namespace
{
	const TCHAR* GetTypeName(ERTSSeatType Type)
	{
		switch (Type)
		{
		case ERTSSeatType::Open: return TEXT("Open");
		case ERTSSeatType::Human: return TEXT("Human");
		case ERTSSeatType::AI: return TEXT("AI");
		default: return TEXT("Closed");
		}
	}

	/** One seat token: Type[-Difficulty][@Spot][#Token]. */
	bool ParseSeat(FString Text, FRTSSeat& OutSeat)
	{
		FRTSSeat Seat;
		FString Token;
		if (Text.Split(TEXT("~"), &Text, &Token))
		{
			if (Token.IsEmpty())
			{
				return false;
			}
			Seat.PlayerToken = Token;
		}
		FString Spot;
		if (Text.Split(TEXT("@"), &Text, &Spot))
		{
			if (Spot.IsEmpty() || !Spot.IsNumeric() || Spot.Contains(TEXT(".")) || Spot.StartsWith(TEXT("-")))
			{
				return false;
			}
			Seat.StartSpot = FCString::Atoi(*Spot);
		}
		FString Difficulty;
		if (Text.Split(TEXT("-"), &Text, &Difficulty) && !RTSSkirmish::ParseDifficulty(Difficulty, Seat.Difficulty))
		{
			return false;
		}

		bool bKnown = false;
		for (ERTSSeatType Type : { ERTSSeatType::Open, ERTSSeatType::Human, ERTSSeatType::AI, ERTSSeatType::Closed })
		{
			if (Text.Equals(GetTypeName(Type), ESearchCase::IgnoreCase))
			{
				Seat.Type = Type;
				bKnown = true;
			}
		}
		// A difficulty belongs to an AI, a token to a human.
		if (!bKnown || (!Difficulty.IsEmpty() && Seat.Type != ERTSSeatType::AI)
			|| (!Seat.PlayerToken.IsEmpty() && Seat.Type != ERTSSeatType::Human))
		{
			return false;
		}
		OutSeat = Seat;
		return true;
	}

	bool IsSpotTaken(TConstArrayView<FRTSSeat> Seats, int32 Spot, int32 IgnoredIndex)
	{
		for (int32 Index = 0; Index < Seats.Num(); ++Index)
		{
			if (Index != IgnoredIndex && RTSSkirmish::IsPlayed(Seats[Index]) && Seats[Index].StartSpot == Spot)
			{
				return true;
			}
		}
		return false;
	}
}

namespace RTSSkirmish
{
	bool ParseDifficulty(const FString& Text, ERTSAIDifficulty& OutDifficulty)
	{
		const UEnum* Enum = StaticEnum<ERTSAIDifficulty>();
		for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
		{
			if (Enum->GetNameStringByIndex(Index).Equals(Text, ESearchCase::IgnoreCase))
			{
				OutDifficulty = static_cast<ERTSAIDifficulty>(Enum->GetValueByIndex(Index));
				return true;
			}
		}
		return false;
	}

	bool ParseSeats(const FString& Text, TArray<FRTSSeat>& OutSeats)
	{
		OutSeats.Reset();
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT(","), false);
		for (const FString& Part : Parts)
		{
			FRTSSeat Seat;
			if (!ParseSeat(Part.TrimStartAndEnd(), Seat))
			{
				OutSeats.Reset();
				return false;
			}
			OutSeats.Add(Seat);
		}
		return !OutSeats.IsEmpty();
	}

	FString FormatSeats(TConstArrayView<FRTSSeat> Seats)
	{
		TArray<FString> Parts;
		for (const FRTSSeat& Seat : Seats)
		{
			FString Part = GetTypeName(Seat.Type);
			if (Seat.Type == ERTSSeatType::AI)
			{
				Part += TEXT("-") + StaticEnum<ERTSAIDifficulty>()->GetNameStringByValue(static_cast<int64>(Seat.Difficulty));
			}
			if (Seat.StartSpot != INDEX_NONE)
			{
				Part += FString::Printf(TEXT("@%d"), Seat.StartSpot);
			}
			if (Seat.Type == ERTSSeatType::Human && !Seat.PlayerToken.IsEmpty())
			{
				Part += TEXT("~") + Seat.PlayerToken;
			}
			Parts.Add(MoveTemp(Part));
		}
		return FString::Join(Parts, TEXT(","));
	}

	bool IsPlayed(const FRTSSeat& Seat)
	{
		return Seat.Type != ERTSSeatType::Closed;
	}

	bool AssignStartSpots(TArray<FRTSSeat>& Seats, int32 SpotCount, FRandomStream& Random)
	{
		TArray<FRTSSeat> Result = Seats;
		TArray<int32> FreeSpots;
		for (int32 Spot = 0; Spot < SpotCount; ++Spot)
		{
			FreeSpots.Add(Spot);
		}
		for (FRTSSeat& Seat : Result)
		{
			if (!IsPlayed(Seat))
			{
				Seat.StartSpot = INDEX_NONE;
			}
			else if (Seat.StartSpot != INDEX_NONE && FreeSpots.Remove(Seat.StartSpot) == 0)
			{
				// Out of range, or already another seat's.
				return false;
			}
		}
		for (FRTSSeat& Seat : Result)
		{
			if (IsPlayed(Seat) && Seat.StartSpot == INDEX_NONE)
			{
				if (FreeSpots.IsEmpty())
				{
					return false;
				}
				const int32 Pick = Random.RandRange(0, FreeSpots.Num() - 1);
				Seat.StartSpot = FreeSpots[Pick];
				FreeSpots.RemoveAt(Pick);
			}
		}
		Seats = MoveTemp(Result);
		return true;
	}

	int32 FindSeatForPlayer(TConstArrayView<FRTSSeat> Seats, const FString& Token, const TSet<int32>& TakenSeats)
	{
		const auto FindFirst = [&](TFunctionRef<bool(const FRTSSeat&)> Matches)
		{
			for (int32 Index = 0; Index < Seats.Num(); ++Index)
			{
				if (!TakenSeats.Contains(Index) && Matches(Seats[Index]))
				{
					return Index;
				}
			}
			return int32(INDEX_NONE);
		};

		int32 Seat = Token.IsEmpty() ? INDEX_NONE : FindFirst([&Token](const FRTSSeat& Candidate)
		{
			return Candidate.Type == ERTSSeatType::Human && Candidate.PlayerToken == Token;
		});
		if (Seat == INDEX_NONE)
		{
			Seat = FindFirst([](const FRTSSeat& Candidate) { return Candidate.Type == ERTSSeatType::Open; });
		}
		if (Seat == INDEX_NONE)
		{
			Seat = FindFirst([](const FRTSSeat& Candidate)
			{
				return Candidate.Type == ERTSSeatType::Human && Candidate.PlayerToken.IsEmpty();
			});
		}
		if (Seat == INDEX_NONE)
		{
			Seat = FindFirst([](const FRTSSeat& Candidate) { return Candidate.Type == ERTSSeatType::Human; });
		}
		return Seat;
	}

	int32 TakeOpenSeat(TArray<FRTSSeat>& Seats, int32 PlayerId, const FString& PlayerName, const FString& Token, bool bIsHost)
	{
		for (int32 Index = 0; Index < Seats.Num(); ++Index)
		{
			FRTSSeat& Seat = Seats[Index];
			if (Seat.Type == ERTSSeatType::Open)
			{
				Seat.Type = ERTSSeatType::Human;
				Seat.PlayerId = PlayerId;
				Seat.PlayerName = PlayerName;
				Seat.PlayerToken = Token;
				Seat.bReady = bIsHost;
				return Index;
			}
		}
		return INDEX_NONE;
	}

	bool ReleaseSeat(TArray<FRTSSeat>& Seats, int32 PlayerId)
	{
		for (FRTSSeat& Seat : Seats)
		{
			if (Seat.Type == ERTSSeatType::Human && Seat.PlayerId == PlayerId)
			{
				Seat.Type = ERTSSeatType::Open;
				Seat.PlayerId = INDEX_NONE;
				Seat.PlayerName.Reset();
				Seat.PlayerToken.Reset();
				Seat.bReady = false;
				return true;
			}
		}
		return false;
	}

	bool CanChangeType(TConstArrayView<FRTSSeat> Seats, int32 Index, bool bIsHost, ERTSSeatType NewType)
	{
		return bIsHost && Seats.IsValidIndex(Index) && Seats[Index].Type != ERTSSeatType::Human
			&& NewType != ERTSSeatType::Human;
	}

	bool CanChangeStartSpot(TConstArrayView<FRTSSeat> Seats, int32 Index, bool bIsHost, int32 PlayerId, int32 Spot,
		int32 SpotCount)
	{
		if (!Seats.IsValidIndex(Index) || !IsPlayed(Seats[Index]))
		{
			return false;
		}
		const bool bOwnSeat = Seats[Index].Type == ERTSSeatType::Human && Seats[Index].PlayerId == PlayerId;
		if (!bIsHost && !bOwnSeat)
		{
			return false;
		}
		return Spot == INDEX_NONE || (Spot >= 0 && Spot < SpotCount && !IsSpotTaken(Seats, Spot, Index));
	}

	bool ChangeStartSpot(TArray<FRTSSeat>& Seats, int32 Index, bool bIsHost, int32 PlayerId, int32 Spot, int32 SpotCount)
	{
		if (!CanChangeStartSpot(Seats, Index, bIsHost, PlayerId, Spot, SpotCount))
		{
			return false;
		}
		FRTSSeat& Seat = Seats[Index];
		const bool bHostsSeat = bIsHost && Seat.Type == ERTSSeatType::Human && Seat.PlayerId == PlayerId;
		if (Seat.StartSpot != Spot && !bHostsSeat)
		{
			Seat.bReady = false;
		}
		Seat.StartSpot = Spot;
		return true;
	}

	bool SetReady(TArray<FRTSSeat>& Seats, int32 PlayerId, bool bIsHost, bool bReady)
	{
		FRTSSeat* Seat = Seats.FindByPredicate([PlayerId](const FRTSSeat& Candidate)
		{
			return Candidate.Type == ERTSSeatType::Human && Candidate.PlayerId == PlayerId;
		});
		if (bIsHost || !Seat)
		{
			return false;
		}
		Seat->bReady = bReady;
		return true;
	}

	bool CanStart(TConstArrayView<FRTSSeat> Seats)
	{
		int32 Humans = 0;
		int32 Players = 0;
		for (const FRTSSeat& Seat : Seats)
		{
			if (Seat.Type == ERTSSeatType::Human && !Seat.bReady)
			{
				return false;
			}
			Humans += Seat.Type == ERTSSeatType::Human ? 1 : 0;
			Players += Seat.Type == ERTSSeatType::Human || Seat.Type == ERTSSeatType::AI ? 1 : 0;
		}
		return Humans >= 1 && Players >= 2;
	}

	TArray<FRTSSeat> ToMatchSeats(TConstArrayView<FRTSSeat> Seats)
	{
		TArray<FRTSSeat> Result;
		for (const FRTSSeat& Seat : Seats)
		{
			FRTSSeat& MatchSeat = Result.Add_GetRef(Seat);
			if (MatchSeat.Type == ERTSSeatType::Open)
			{
				MatchSeat.Type = ERTSSeatType::Closed;
				MatchSeat.StartSpot = INDEX_NONE;
			}
			MatchSeat.PlayerId = INDEX_NONE;
			MatchSeat.PlayerName.Reset();
			MatchSeat.bReady = false;
		}
		return Result;
	}
}
